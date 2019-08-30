/**
 * collectd - src/arangodb.c
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Sebastian Harl
 * Copyright (C) 2019       Matthew Von-Maszewski, ArangoDB
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 *   Matthew Von-Maszewski <matthewv at arangodb.com>
 **/

#include "collectd.h"

#include <pthread.h>

#include "plugin.h"
#include "utils/common/common.h"

#include "yajl/yajl_tree.h"

#include <curl/curl.h>

//
// Endpoint is ArangoDB term for server address and port
//
struct endpoint_s {
  char * given;
  char * host;
  char * port;
  char * url_scheme;
};

typedef struct endpoint_s endpoint_t;


//
// curl connection and messaging data
//
struct curlinfo_s {
  CURL * curl;
  char curl_errbuf[CURL_ERROR_SIZE];

  char * user;
  char * password;

  char * credentials;

  int buf_size;
  char * temp_buffer;
};  

typedef struct curlinfo_s curlinfo_t;


//
// cluster is one endpoint that is either an ArangoDB coordinator
//   or single database server ... this only exists until first
//   communication with endpoint, then service_t objects replace it
//
struct cluster_s {
  endpoint_t endpoint; // future:  make endpoint an array to allow multiple coordinators
  char * instance_name;
  char * registered_name;
  curlinfo_t curlinfo;
};

typedef struct cluster_s cluster_t;


//
// service is one ArangoDB component:  agent, coordinator, or db server.
//   service_t objects are created dynamically after reading cluster configuration
//   via cluster_t's endpoint
//
struct service_s {
  endpoint_t endpoint;
  char * service_name;
  char * registered_name;
  char * role;
  char * engine;
  bool engine_init_done;
  curlinfo_t curlinfo;
};

typedef struct service_s service_t;

//
// callback declares
//
static int arangodb_complex_config(oconfig_item_t * config_item);
static int arangodb_init(void);

static int cluster_read(user_data_t *ud);
static size_t cluster_write_callback(void *buf,
                                     size_t size, size_t nmemb,
                                     void *user_data);


// support functions
static cluster_t * new_cluster(void);
static void free_cluster(void * arg);
static void free_endpoint(endpoint_t * endpoint);
static void free_curlinfo(curlinfo_t * curlinfo);
static int arangodb_add_cluster(oconfig_item_t * config_item);
static int cluster_set_options(oconfig_item_t * config_item, cluster_t * cluster);
static int cluster_verify_options(cluster_t * cluster);
static void cluster_decode_health(yajl_val health_node);

static void start_curl_session(curlinfo_t * curlinfo);
static int curl_perform(curlinfo_t * curlinfo, endpoint_t * endpoint, char * page, yajl_val * response);

static service_t * new_service_single(cluster_t * cluster);
static service_t * new_service_cluster(yajl_val health_node, const char * service_name);
static void free_service(void * arg);
static int service_read(user_data_t *ud);
static void service_decode_stats(service_t * service, yajl_val srv_stats, yajl_val stats_desc);
static void service_data_set_init(yajl_val figures_array);
static void service_get_engine(service_t * service);
static int service_get_rocksdb(service_t * service);

static char * arangodb_get_role(curlinfo_t * curlinfo, endpoint_t * endpoint);
static bool rocksdb_is_gauge(const char * key);
static bool endpoint_parse(endpoint_t * endpoint);

// management for first time init of server data_set
static pthread_mutex_t service_data_set_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool service_data_set_done = false;

static pthread_mutex_t rocks_data_set_mutex = PTHREAD_MUTEX_INITIALIZER;
static bool rocks_data_set_done = false;


//
// entry point from collectd daemon
//
void module_register(void) {
  plugin_register_complex_config("arangodb", arangodb_complex_config);
  plugin_register_init("arangodb", arangodb_init);
} /* void module_register */


static int arangodb_init(void) {

  // taken from curl_json.c
  /* Call this while collectd is still single-threaded to avoid
   * initialization issues in libgcrypt. */
  curl_global_init(CURL_GLOBAL_ALL);

  return 0;
} // arangodb_init


//
// Config level one:  cluster / singleserver key word
//
static int arangodb_complex_config(oconfig_item_t * config_item) {
  int loop, ret_val;
  oconfig_item_t * child;

  ret_val = 0;

  for (loop=0, child=config_item->children;
       loop<config_item->children_num && 0 == ret_val;
       ++loop, ++child) {
    if (0 == strcasecmp("cluster", child->key) || 0 == strcasecmp("singleserver", child->key)) {
      ret_val = arangodb_add_cluster(child);
    } else {
      WARNING("arangodb plugin: Unknown setting %s.", child->key);
    } // else
  } // for
  
  return ret_val;

} // arangodb_complex_config


//
// Config level two:  create and initialize cluster object
//
static int arangodb_add_cluster(oconfig_item_t * cluster_config) {
  int ret_val;
  cluster_t * cluster;
  
  ret_val = 0;
  
  cluster = new_cluster();
  if (NULL != cluster) {
    ret_val = cluster_set_options(cluster_config, cluster);

    if ( 0 == ret_val) {
      start_curl_session(&cluster->curlinfo);
      
      if ( NULL == cluster->instance_name) {
        cluster->instance_name = strdup("default");
      } // if

      // trying to make a unique name if several clusters
      cluster->registered_name = ssnprintf_alloc("arangodb-%s-%s-%s", cluster->instance_name,
                                                 cluster->endpoint.host, cluster->endpoint.port);

      DEBUG("arangodb plugin: Registering new read callback: %s", cluster->registered_name);

      plugin_register_complex_read(/* group = */ NULL, cluster->registered_name,
                                   cluster_read, 0, //interval,
                                   &(user_data_t){
                                     .data = cluster,
                                     .free_func = free_cluster,
                                    });
    } // if
  } else {
    ret_val = -1;
  } // else

  return ret_val;
  
} // arangodb_add_cluster


//
// Parse cluster options, set cluster_t object accordingly
//  (get endpoint today, maybe other options in future, like user & password)
//
static int cluster_set_options(oconfig_item_t * cluster_config,
                                cluster_t * cluster) {
  int loop, ret_val;
  oconfig_item_t * child;
  
  ret_val = 0;
  
  if (1 == cluster_config->values_num) {
    ret_val = cf_util_get_string(cluster_config, &cluster->instance_name);
  } // if
  
  for (loop=0, child=cluster_config->children;
       loop<cluster_config->children_num && 0 == ret_val;
       ++loop, ++child) {
    if (0 == strcasecmp("endpoint", child->key)) {
      ret_val = cf_util_get_string(child, &cluster->endpoint.given);
    } // if
  } // for

  if (0 == ret_val) {
    ret_val = cluster_verify_options(cluster);
  } // if

  return ret_val;

} // cluster_set_options


//
// review all cluster options as a "set" to see if valid
//  (later verify all things needed for ssl and such)
static int cluster_verify_options(cluster_t * cluster) {
  bool good = true;

  if (good) {
    // transform arangodb endpoint syntax to http stuff
    good = endpoint_parse(&cluster->endpoint);
  } // if

  return (good ? 0 : -1);
  
} // cluster_verify_options


//
// collectd callback every "interval" to poll cluster,
//  used to initiate poll to get cluster configuration
//
static int cluster_read(user_data_t *ud) {
  int ret_val;
  cluster_t * cluster;
  
  ret_val = 0;

  if ( NULL != ud && NULL != ud->data) {
    char * role = NULL;
    cluster = (cluster_t *)ud->data;

    role = arangodb_get_role(&cluster->curlinfo, &cluster->endpoint);

    if (NULL!=role) {
      if (0 == strcasecmp("SINGLE", role)) {
        // single db server
        service_t * service = NULL;

        service = new_service_single(cluster);
        plugin_register_complex_read(/* group = */ NULL, service->registered_name,
                                     service_read, 0, //interval,
                                     &(user_data_t){
                                       .data = service,
                                         .free_func = free_service,
                                         });

        
      } else {
        // cluster of servers
        yajl_val health_node = NULL;

        ret_val = curl_perform(&cluster->curlinfo, &cluster->endpoint, "/_admin/cluster/health", &health_node);

        if (0 == ret_val) {
          cluster_decode_health(health_node);
        } // if

        yajl_tree_free(health_node);

      } // else

      if (0==ret_val) {
        // done with cluster_t work, unregister
        plugin_unregister_read(cluster->registered_name);
        // WARNING: cluster object likely unusable now
        cluster = NULL;
      } // if
    } // if
  } else {
    ERROR("arangodb plugin: cluster_read: Invalid user data.");
    ret_val = -1;
  } // else
  
  return ret_val;

} // cluster_read


//
// use health response to create polling of each cluster service
//
static void cluster_decode_health(yajl_val health_response) {
  yajl_val health_obj;
  const char * health_path[] = { "Health", (const char *) 0 };

  health_obj = yajl_tree_get(health_response, health_path, yajl_t_object);
  if (YAJL_IS_OBJECT(health_obj)) {
    size_t idx;
    yajl_val * value_idx;
    const char ** key_idx;
    
    for (idx = 0, key_idx = health_obj->u.object.keys, value_idx = health_obj->u.object.values;
         idx < health_obj->u.object.len;
         ++idx, ++key_idx, ++value_idx) {

      service_t * service;

      service = new_service_cluster(*value_idx, *key_idx);

      // use internal service name as unique id
      service->registered_name = ssnprintf_alloc("arangodb-%s", *key_idx);
        
      plugin_register_complex_read(/* group = */ NULL, service->registered_name,
                                   service_read, 0, //interval,
                                   &(user_data_t){
                                     .data = service,
                                     .free_func = free_service,
                                   });
      DEBUG("created service  %s\n", *key_idx);
    } // for
  } // if
} // cluster_decode_health


//
// allocate and initialize cluster_t
//
static cluster_t * new_cluster() {
  cluster_t * ret_ptr;

  ret_ptr = smalloc(sizeof(cluster_t));

  if (NULL != ret_ptr) {
    memset(ret_ptr, 0, sizeof(cluster_t));
  } else {
    ERROR("arangodb plugin: malloc failed in new_cluster()");
  } // else

  return ret_ptr;
  
} // new_cluster


static void free_cluster(void * arg) {
  cluster_t * cluster;

  cluster = (cluster_t *)arg;

  free_endpoint(&cluster->endpoint);
  sfree(cluster->instance_name);
  sfree(cluster->registered_name);
  free_curlinfo(&cluster->curlinfo);
  
  sfree(cluster);
  
} // free_cluster


static void free_endpoint(endpoint_t * endpoint) {
  sfree(endpoint->given);
  sfree(endpoint->host);
  sfree(endpoint->port);
  sfree(endpoint->url_scheme);
} // free_endpoint


static void free_curlinfo(curlinfo_t * curlinfo) {
  if ( NULL != curlinfo->curl ) {
    curl_easy_cleanup(curlinfo->curl);
    curlinfo->curl = NULL;
  } // if
  
  sfree(curlinfo->user);
  sfree(curlinfo->password);

  sfree(curlinfo->credentials);

  sfree(curlinfo->temp_buffer);
} // free_curlinfo


//
// allocate and initialize service_t
//
static service_t * new_service_cluster(yajl_val health_node, const char * service_name) {
  service_t * service = NULL;

  service = smalloc(sizeof(service_t));

  if (NULL != service) {
    const char * endpoint_path[] = { "Endpoint", (const char *) 0 };
    const char * role_path[] = { "Role", (const char *) 0 };
    const char * engine_path[] = { "Engine", (const char *) 0 };
    yajl_val role_node = NULL, endpoint_node = NULL, engine_node = NULL;

    memset(service, 0, sizeof(service_t));

    endpoint_node = yajl_tree_get(health_node, endpoint_path, yajl_t_string);
    role_node = yajl_tree_get(health_node, role_path, yajl_t_string);
    engine_node = yajl_tree_get(health_node, engine_path, yajl_t_string);

    if (YAJL_IS_STRING(endpoint_node) && YAJL_IS_STRING(role_node) && YAJL_IS_STRING(engine_node)) {

      service->endpoint.given = sstrdup(endpoint_node->u.string);
      service->service_name = sstrdup(service_name);
      service->role = sstrdup(role_node->u.string);
      service->engine = sstrdup(engine_node->u.string);

      // validate endpoint
      if (!endpoint_parse(&service->endpoint)) {
        free_service(service);
        service = NULL;
        WARNING("arangodb plugin: new_service_cluster given bad endpoint for %s", service_name);
      } // if

      if (NULL != service) {
        start_curl_session(&service->curlinfo);
      } // if
    } else {
      WARNING("arangodb plugin: new_service_cluster given bad JSON for %s", service_name);
    } // else
  } else {
    ERROR("arangodb plugin: malloc failed in new_service_cluster()");
  } // else

  return service;
  
} // new_service_cluster


static service_t * new_service_single(cluster_t * cluster) {
  service_t * service = NULL;

  service = smalloc(sizeof(service_t));

  if (NULL != service) {
    memset(service, 0, sizeof(service_t));

    service->endpoint.given = sstrdup(cluster->endpoint.given);
    // trying to make a unique name if several single servers
    //  NOTE: intentionally using arangod prefix instead of arangodb to be diff from cluster name
    service->registered_name = ssnprintf_alloc("arangod-%s-%s-%s", cluster->instance_name,
                                               cluster->endpoint.host, cluster->endpoint.port);
    
      // reparse endpoint for this object
    if (!endpoint_parse(&service->endpoint)) {
      free_service(service);
      service = NULL;
      WARNING("arangodb plugin: new_service_single given bad endpoint for %s", cluster->instance_name);
    } // if

    service->service_name = ssnprintf_alloc("SINGLE-%s", service->endpoint.port);
    
    if (NULL != service) {
      start_curl_session(&service->curlinfo);

    } // if
  } else {
    ERROR("arangodb plugin: malloc failed in new_service_single()");
  } // else

  return service;
  
} // new_service_single


static void free_service(void * arg) {
  service_t * service;

  service = (service_t *)arg;

  free_endpoint(&service->endpoint);
  sfree(service->service_name);
  sfree(service->registered_name);
  sfree(service->role);
  sfree(service->engine);
  free_curlinfo(&service->curlinfo);
  
  sfree(service);
  
} // free_service




static void start_curl_session(curlinfo_t * curlinfo) {

  if (NULL != curlinfo->curl) {
    curl_easy_cleanup(curlinfo->curl);
    curlinfo->curl = NULL;
  } // if

  // assuming curl_global_init() called elsewhere or thread
  //  safe call is happening here by default
  curlinfo->curl = curl_easy_init();
  
  if (NULL != curlinfo->curl) {
    CURL * curl;

    curl = curlinfo->curl;
    // dns retry is every 5 seconds, so this is 7 to retry after dns fail
    //  (default 300)
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 7L);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cluster_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, curlinfo);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, COLLECTD_USERAGENT);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlinfo->curl_errbuf);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 3L);
  
  } else {
    WARNING("arangodb plugin: curl_easy_init() failed");
  } // else

} // start_curl_session


//
// send http request via curl and process the response's JSON content
//
static int curl_perform(curlinfo_t * curlinfo, endpoint_t * endpoint, char * page, yajl_val * response) {
  int ret_val, status;
  long rc;
  char * url;

  ret_val = 0;
  
  sfree(curlinfo->temp_buffer);
  curlinfo->buf_size = 0;

  url = ssnprintf_alloc("%s%s:%s%s",
                        endpoint->url_scheme,
                        endpoint->host, endpoint->port,
                        page);

  curl_easy_setopt(curlinfo->curl, CURLOPT_URL, url);
  status = curl_easy_perform(curlinfo->curl);
  sfree(url);
  
  if (status != CURLE_OK) {
    ERROR("arangodb plugin: curl_easy_perform failed with status %i: %s (%s)",
          status, curlinfo->curl_errbuf, url);
    ret_val = -1;
  } // if
  
  curl_easy_getinfo(curlinfo->curl, CURLINFO_RESPONSE_CODE, &rc);

  // The response code is zero if a non-HTTP transport was used.
  if ((rc != 0) && (rc != 200)) {
    ERROR("arangodb plugin: curl_easy_perform failed with "
          "response code %ld (%s)",
          rc, url);
    ret_val = -1;
  } // if

  *response = NULL;
  
  // if curl likes it, see if response appears valid
  if (0 == ret_val) {
    *response = yajl_tree_parse(curlinfo->temp_buffer, curlinfo->curl_errbuf, sizeof(curlinfo->curl_errbuf));

    // read result codes within JSON response
    if (NULL != *response && YAJL_IS_OBJECT(*response)) {
      bool good;
      yajl_val code_val, error_val;
      const char * code_path[] = { "code", (const char *) 0 };
      const char * error_path[] = { "error", (const char *) 0 };
      
      code_val = yajl_tree_get(*response, code_path, yajl_t_number);
      error_val = yajl_tree_get(*response, error_path, yajl_t_any);

      // code and error need to both be missing, or both be positive
      
      good = (NULL == code_val && NULL == error_val)
        || (YAJL_IS_INTEGER(code_val) && 200 == YAJL_GET_INTEGER(code_val)
            && YAJL_IS_FALSE(error_val));

      if (!good) {
        ret_val = -1;
        WARNING("arangodb plugin: response object indicates error.");
      } // if
    } else {
      ret_val = -1;
      WARNING("arangodb plugin: response JSON did not parse.");
    } // else
  } // if

  return ret_val;

} // curl_perform


static size_t cluster_write_callback(void *buf,
                                  size_t size, size_t nmemb, void *user_data) {
  size_t len, ret_size, new_len, old_len;
  curlinfo_t * curlinfo;
  char * old_buf, * tmp_ptr;
  
  ret_size = 0;
  curlinfo = user_data;
  len = size * nmemb;

  if (NULL != curlinfo && 0 != len) {
    old_buf = curlinfo->temp_buffer;
    old_len = curlinfo->buf_size;
    new_len = curlinfo->buf_size + len;
    curlinfo->temp_buffer = smalloc(new_len + 1);
    if (NULL != curlinfo->temp_buffer) {
      if (NULL != old_buf && 0 != old_len) {
        memmove(curlinfo->temp_buffer, old_buf, old_len);
      } // if
      
      tmp_ptr = curlinfo->temp_buffer + old_len;
      memmove(tmp_ptr, (char*)buf, len);
      curlinfo->buf_size = new_len;
      *(curlinfo->temp_buffer + new_len) ='\0';
      ret_size = len;
    } else {
      ret_size = -1;
      curlinfo->buf_size = 0;
    } // else
    
    sfree(old_buf);
  } // if
  
  return ret_size;
  
} // cluster_write_callback


static int service_read(user_data_t *ud) {
  int ret_val, ret_rocks;
  service_t * service;
  
  ret_val = 0;
  ret_rocks = -1;
  
  if ( NULL != ud && NULL != ud->data) {
    yajl_val service_node = NULL, desc_node = NULL;
    service = (service_t *)ud->data;

    // role and engine only retrieved once
    if (NULL == service->role) {
      service->role = arangodb_get_role(&service->curlinfo, &service->endpoint);
    } // if
      
    if (NULL == service->engine) {
      service_get_engine(service);
    } // if

    // basic statistics retrieved every pass
    ret_val = curl_perform(&service->curlinfo, &service->endpoint, "/_admin/statistics", &service_node);

    if ( 0 == ret_val ) {
      ret_val = curl_perform(&service->curlinfo, &service->endpoint, "/_admin/statistics-description", &desc_node);
    } // if

    if ( 0 == ret_val ) {

      if (NULL != service_node && NULL != desc_node) {
        service_decode_stats(service, service_node, desc_node);
      } // if
    } // if
    
    yajl_tree_free(service_node);
    yajl_tree_free(desc_node);

    // rocksdb statistics retrieved every pass if engine
    if (NULL != service->engine && 0 == strcasecmp("rocksdb", service->engine)) {
      ret_rocks = service_get_rocksdb(service);
    } // if

    // possible for basic stats to be disabled and rocksdb to be enabled
    //  or vice-versa ... if one is good, show happiness
    if (0 == ret_val || 0 == ret_rocks) {
      ret_val = 0;
    } // if
  } else {
    ERROR("arangodb plugin: service_read: Invalid user data.");
    ret_val = -1;
  } // else
  
  return ret_val;

} // service_read


static void service_decode_stats(service_t * service, yajl_val srv_stats, yajl_val stats_desc) {
  cdtime_t now;
  size_t idx;
  yajl_val figures_array, group, identifier, type, stat, object;
  const char * figures_path[] = { "figures", (const char *) 0 };
  const char * group_path[] = { "group", (const char *) 0 };
  const char * identifier_path[] = { "identifier", (const char *) 0 };
  const char * type_path[] = { "type", (const char *) 0 };
  char * stats_path[] = { NULL, NULL, (char *) 0, (char *) 0  };

  now = cdtime();
  figures_array = yajl_tree_get(stats_desc, figures_path, yajl_t_array);
  if (YAJL_IS_ARRAY(figures_array)) {
    service_data_set_init(figures_array);

    for (object=figures_array->u.array.values[0], idx=0;
         idx<figures_array->u.array.len;
         ++idx, object=figures_array->u.array.values[idx]) {
      stat = NULL;
      group = yajl_tree_get(object, group_path, yajl_t_string);
      identifier = yajl_tree_get(object, identifier_path, yajl_t_string);
      type = yajl_tree_get(object, type_path, yajl_t_string);

      if (YAJL_IS_STRING(group) && YAJL_IS_STRING(identifier) && YAJL_IS_STRING(type)) {
        stats_path[0] = group->u.string;
        stats_path[1] = identifier->u.string;
        stats_path[2] = (char *)0;
        
        stat = yajl_tree_get(srv_stats, (const char **)stats_path, yajl_t_any);
        if (YAJL_IS_OBJECT(stat)) {
          stats_path[2] = "count";
          stat = yajl_tree_get(srv_stats, (const char **)stats_path, yajl_t_number);
        } // if
      } // if
      
      if (NULL != stat && YAJL_IS_NUMBER(stat)) {
        value_t value;
        value_list_t vl = VALUE_LIST_INIT;
        bool skip = false;
        int ret_val;
        
        vl.values = &value;
        vl.values_len = 1;
        vl.time = now;
        // default interval?
        ssnprintf(vl.host, DATA_MAX_NAME_LEN, "%s", service->endpoint.host);
        ssnprintf(vl.plugin, DATA_MAX_NAME_LEN, "arangodb");
        if (NULL != service->service_name) {
          ssnprintf(vl.plugin_instance, DATA_MAX_NAME_LEN, "%s",
                    service->service_name);
        } else {
          ssnprintf(vl.plugin_instance, DATA_MAX_NAME_LEN, "no-role-%s", service->endpoint.port);
        } // else
        ssnprintf(vl.type, DATA_MAX_NAME_LEN, "arangodb-%s", identifier->u.string);
        // no type_instance

        if (0 == strcasecmp("accumulated", type->u.string) || 0 == strcasecmp("distribution", type->u.string) ) {
          value.derive = (derive_t)(YAJL_IS_INTEGER(stat) ? YAJL_GET_INTEGER(stat) : YAJL_GET_DOUBLE(stat));
        } else if (0 == strcasecmp("current", type->u.string)) {
          value.gauge = (gauge_t)(YAJL_IS_DOUBLE(stat) ? YAJL_GET_DOUBLE(stat) : YAJL_GET_INTEGER(stat));
        } else {
          skip = true;
        } // else

        if (!skip) {
          ret_val = plugin_dispatch_values(&vl);

          if ( 0 != ret_val) {
            WARNING("bad return value from plugin_dispatch_values: %d for %s\n", ret_val, vl.type);
          } // if
        } // if
      } // if
      
    } // for
  } // if

} // service_decode_stats


static void service_data_set_init(yajl_val figures_array) {
  pthread_mutex_lock(&service_data_set_mutex);
  const char * identifier_path[] = { "identifier", (const char *) 0 };
  const char * type_path[] = { "type", (const char *) 0 };

  if (!service_data_set_done) {
    size_t idx;
    yajl_val object, identifier, type;
    
    for (object=figures_array->u.array.values[0], idx=0;
         idx<figures_array->u.array.len;
         ++idx, object=figures_array->u.array.values[idx]) {

      identifier = yajl_tree_get(object, identifier_path, yajl_t_string);
      type = yajl_tree_get(object, type_path, yajl_t_string);

      if (YAJL_IS_STRING(identifier) && YAJL_IS_STRING(type)) {
        int ret_val;
        data_set_t data_set;
        data_source_t data_source;

        strcpy(data_source.name,"value");
        data_source.min = 0;
        data_source.max = NAN;
        data_source.type = -1;
        if (0 == strcasecmp("accumulated", type->u.string) || 0 == strcasecmp("distribution", type->u.string) ) {
          data_source.type = DS_TYPE_DERIVE;
        } else if (0 == strcasecmp("current", type->u.string)) {
          data_source.type = DS_TYPE_GAUGE;
        } // else if

        if ( -1 != data_source.type) {
          ssnprintf(data_set.type, DATA_MAX_NAME_LEN, "arangodb-%s", identifier->u.string);
          data_set.ds_num = 1;
          data_set.ds = &data_source;

          ret_val = plugin_register_data_set(&data_set);
          if ( 0 != ret_val) {
            WARNING("arangodb plugin: plugin_register_data_set returned %d on %s.", ret_val, data_set.type);
          } // if
        } else {
          WARNING("arangodb plugin: service_data_set_init ignored index %zd, type %s.", idx, type->u.string);
        } // else
        
      } else {
        WARNING("arangodb plugin: service_data_set_init ignored index %zd.", idx);
      } // else
        
    } // for
    service_data_set_done = true;
  } // if

  pthread_mutex_unlock(&service_data_set_mutex);
  return;
  
} // service_data_set_init


static void service_get_engine(service_t * service) {
  int ret_val;

  if (NULL == service->engine) {
    yajl_val engine_node = NULL;

    ret_val = curl_perform(&service->curlinfo, &service->endpoint, "/_api/engine", &engine_node);

    if (0 == ret_val) {
      bool good;
      yajl_val engine_val;
      const char * engine_path[] = { "name", (const char *) 0 };

      engine_val = yajl_tree_get(engine_node, engine_path, yajl_t_any);
        
      good = (YAJL_IS_STRING(engine_val));

      if (good) {
        service->engine = sstrdup(engine_val->u.string);
        printf("engine is: %s\n", service->engine);
      } // if
    } else {
      WARNING("arangodb plugin: service_get_engine failed");
    } // else

    yajl_tree_free(engine_node);
  } // if

}  // service_get_engine


static int service_get_rocksdb(service_t * service) {
  int ret_val = -1;
  cdtime_t now;

  now = cdtime();
  
  // this test is redundant, but safe
  if (NULL != service->engine && 0 == strcasecmp("rocksdb", service->engine)) {
    yajl_val rocks_node = NULL;

    ret_val = curl_perform(&service->curlinfo, &service->endpoint, "/_api/engine/stats", &rocks_node);

    if (0 == ret_val && YAJL_IS_OBJECT(rocks_node)) {
      const char **key_idx;
      yajl_val *value_idx;
      int idx = 0, temp;
      bool locked = true;

      // only keep lock if first thread loading rocks values
      DEBUG("rocks locked\n");
      pthread_mutex_lock(&rocks_data_set_mutex);
      if (rocks_data_set_done) {
        locked = false;
        pthread_mutex_unlock(&rocks_data_set_mutex);
        DEBUG("rocks unlocked early\n");
      } // if
      
      for (idx = 0, key_idx = rocks_node->u.object.keys, value_idx = rocks_node->u.object.values;
           idx < rocks_node->u.object.len;
           ++idx, ++key_idx, ++value_idx) {

        // ignoring rocksdb stats that are strings or objects
        if (YAJL_IS_NUMBER(*value_idx)) {

          // register data types first time through
          if (!rocks_data_set_done) {
            data_set_t data_set;
            data_source_t data_source;
            
            strcpy(data_source.name,"value");
            data_source.min = 0;
            data_source.max = NAN;

            // most things are derive values, a few are gauge ...
            if (rocksdb_is_gauge(*key_idx)) {
              data_source.type = DS_TYPE_GAUGE;
            } else {
              data_source.type = DS_TYPE_DERIVE;
            } // else

            ssnprintf(data_set.type, DATA_MAX_NAME_LEN, "%s", *key_idx);
            data_set.ds_num = 1;
            data_set.ds = &data_source;

            temp = plugin_register_data_set(&data_set);
            DEBUG("data_set.type: %s (%d)\n", data_set.type, temp);
            
            if ( 0 != temp) {
              WARNING("arangodb plugin: plugin_register_data_set returned %d on %s.", temp, data_set.type);
            } // if
          } // if

          value_t value;
          value_list_t vl = VALUE_LIST_INIT;
        
          vl.values = &value;
          vl.values_len = 1;
          vl.time = now;

          // default interval?
          ssnprintf(vl.host, DATA_MAX_NAME_LEN, "%s", service->endpoint.host);
          ssnprintf(vl.plugin, DATA_MAX_NAME_LEN, "arangodb");
          if (NULL != service->role) {
            ssnprintf(vl.plugin_instance, DATA_MAX_NAME_LEN, "%s-%s",
                      service->role, service->endpoint.port);
          } else {
            ssnprintf(vl.plugin_instance, DATA_MAX_NAME_LEN, "no-role-%s", service->endpoint.port);
          } // else
          ssnprintf(vl.type, DATA_MAX_NAME_LEN, "%s", *key_idx);
          
          // no type_instance

          if (rocksdb_is_gauge(*key_idx)) {
            value.gauge = (gauge_t)(YAJL_IS_DOUBLE(*value_idx) ? YAJL_GET_DOUBLE(*value_idx) : YAJL_GET_INTEGER(*value_idx));
          } else {
            value.derive = (derive_t)(YAJL_IS_INTEGER(*value_idx) ? YAJL_GET_INTEGER(*value_idx) : YAJL_GET_DOUBLE(*value_idx));
          } // else

          temp = plugin_dispatch_values(&vl);
          if ( 0 != temp) {
            WARNING("arangodb plugin: plugin_dispatch_values returned %d on %s.", temp, *key_idx);
          } // if
        } else {
          DEBUG("ignoring non-number %s\n", *key_idx);
        } // else
        
      } // for

      if (locked) {
        rocks_data_set_done = (0 < idx);
        pthread_mutex_unlock(&rocks_data_set_mutex);
        DEBUG("rocks unlocked\n");
      } // if

    } else if (-1 == ret_val) {
      WARNING("arangodb plugin: service_get_rocksdb failed");
    } else {
      WARNING("arangodb plugin: service_get_rocksdb received non-object JSON");
      ret_val = -1;
    } // else

    yajl_tree_free(rocks_node);
  } // if

  return ret_val;

}  // service_get_rocksdb


static char * arangodb_get_role(curlinfo_t * curlinfo, endpoint_t * endpoint) {
  char * ret_ptr = NULL;
  int ret_val;
  yajl_val role_node = NULL;

  ret_val = curl_perform(curlinfo, endpoint, "/_admin/server/role", &role_node);

  if (0 == ret_val) {
    bool good;
    yajl_val role_val;
    const char * role_path[] = { "role", (const char *) 0 };

    role_val = yajl_tree_get(role_node, role_path, yajl_t_any);
        
    good = (YAJL_IS_STRING(role_val));

    if (good) {
      ret_ptr = sstrdup(role_val->u.string);
    } // if
  } else {
    WARNING("arangodb plugin: arangodb_get_role failed");
  } // else

  yajl_tree_free(role_node);

  return ret_ptr;
}  // arangodb_get_role


//
// list of known rocksdb statistics that are a gauge instead of a counter
//
static const char * rocks_gauge_list[] = {
  "rocksdb.num-files-at-level0",
  "rocksdb.compression-ratio-at-level0",
  "rocksdb.num-files-at-level1",
  "rocksdb.compression-ratio-at-level1",
  "rocksdb.num-files-at-level2",
  "rocksdb.compression-ratio-at-level2",
  "rocksdb.num-files-at-level3",
  "rocksdb.compression-ratio-at-level3",
  "rocksdb.num-files-at-level4",
  "rocksdb.compression-ratio-at-level4",
  "rocksdb.num-files-at-level5",
  "rocksdb.compression-ratio-at-level5",
  "rocksdb.num-files-at-level6",
  "rocksdb.compression-ratio-at-level6",
  "rocksdb.num-immutable-mem-table",
  "rocksdb.num-immutable-mem-table-flushed",
  "rocksdb.mem-table-flush-pending",
  "rocksdb.compaction-pending",
  "rocksdb.cur-size-active-mem-table",
  "rocksdb.cur-size-all-mem-tables",
  "rocksdb.size-all-mem-tables",
  "rocksdb.num-entries-active-mem-table",
  "rocksdb.num-entries-imm-mem-tables",
  "rocksdb.num-deletes-active-mem-table",
  "rocksdb.num-deletes-imm-mem-tables",
  "rocksdb.estimate-num-keys",
  "rocksdb.estimate-table-readers-mem",
  "rocksdb.num-snapshots",
  "rocksdb.oldest-snapshot-time",
  "rocksdb.num-live-versions",
  "rocksdb.min-log-number-to-keep",
  "rocksdb.estimate-live-data-size",
  "rocksdb.live-sst-files-size",
  "rocksdb.num-running-compactions",
  "rocksdb.num-running-flushes",
  "rocksdb.is-file-deletions-enabled",
  "rocksdb.estimate-pending-compaction-bytes",
  "rocksdb.base-level",
  "rocksdb.block-cache-capacity",
  "rocksdb.block-cache-usage",
  "rocksdb.block-cache-pinned-usage",
  "rocksdb.total-sst-files-size",
  "rocksdb.actual-delayed-write-rate",
  "rocksdb.is-write-stopped",
  "cache.limit",
  "cache.allocated",
  "rocksdbengine.throttle.bps"
};


static bool rocksdb_is_gauge(const char * key) {
  bool ret_flag = true;

  if (NULL != key) {
    // list only contains gauge keys, search it until one found
    const char ** gauge;
    for (gauge = rocks_gauge_list, ret_flag = false;
         !ret_flag && NULL != *gauge;
         ++gauge) {
      ret_flag = (0 == strcasecmp(*gauge, key));
    } // for
  } // if

  return ret_flag;
  
} // rocksdb_is_gauge


//
// convert ArangoDB endpoint syntax into something
//  useful for curl
//  returns false if parse issues
//
static bool endpoint_parse(endpoint_t * endpoint) {
  bool ret_flag = false;
  char * scheme_end, * ptr;
  int loop, len = 0;
  struct arango_schemes {
    char * arango_prefix;
    char * url_prefix;
  } db_schemes[] = {
    {"http+tcp://","http://"},
    {"http://","http://"},
    {"tcp://","http://"},
    {"http+ssl://","https://"},
    {"ssl://","https://"},
    {NULL, NULL}};

  if ( NULL != endpoint && NULL != endpoint->given) {
    // have scheme?
    endpoint->url_scheme = NULL;
    scheme_end = strstr(endpoint->given, "://");

    if (NULL != scheme_end) {
      len = scheme_end - endpoint->given + 3;
      for (loop=0; NULL == endpoint->url_scheme && NULL != db_schemes[loop].arango_prefix; ++loop) {
        if (0 == strncasecmp(endpoint->given, db_schemes[loop].arango_prefix, len)) {
          endpoint->url_scheme = strdup(db_schemes[loop].url_prefix);
        } // if
      } // for
    } // if

    if (NULL == endpoint->url_scheme) {
      // no scheme, assume http://
      endpoint->url_scheme = strdup("http://");
    } // if

    endpoint->host = strdup(endpoint->given + len);

    // find end of host to harvest port
    /// ipv6?
    ptr = strstr(endpoint->host, "]");
    if (NULL == ptr) {
      ptr = endpoint->host;
    } // if
    ptr = strstr(ptr, ":");

    if (NULL != ptr) {
      *ptr = '\0';
      ++ptr;
      endpoint->port = strdup(ptr);
      ret_flag = ('\0' != *ptr);
    } else {
      // no port, bad endpoint
      ret_flag=false;
    } // else
  } // if

  DEBUG("endpoint_parse: ret_flag %d, given %s, host %s, port %s, scheme %s\n",
         (int)ret_flag, endpoint->given, endpoint->host, endpoint->port, endpoint->url_scheme);

  return ret_flag;
  
} // endpoint_parse
