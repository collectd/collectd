/**
 * collectd - src/nginx.c
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Sebastian Harl
 * Copyright (C) 2016       Maxim Chindyasov
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
 *   Maxim Chindyasov <max at chindyasov.ru>
 *   Gerry Calderhead <gerry at fetchtv.com.au>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <curl/curl.h>

#define NO_INSTNAME NULL 

struct nginx_inst_config_s /* {{{ */
{
  char *inst_name; 
  char *host_name;
  char *url;
  char *user;
  char *pass;
  char *verify_peer;
  char *verify_host;
  char *cacert;
  char *timeout;

  CURL *curl;

  char nginx_buffer[16384];
  size_t nginx_buffer_len;
  char nginx_curl_error[CURL_ERROR_SIZE];
};
typedef struct nginx_inst_config_s nginx_inst_config_t; /* }}} */

static size_t nginx_curl_callback(void *buf, size_t size, size_t nmemb,
                                  void *user_data) {  
  nginx_inst_config_t *inst_config = (nginx_inst_config_t *)user_data;
  size_t len = size * nmemb;

  /* Check if the data fits into the memory. If not, truncate it. */
  if ((inst_config->nginx_buffer_len + len) >=
      sizeof(inst_config->nginx_buffer)) {
    assert(sizeof(inst_config->nginx_buffer) > inst_config->nginx_buffer_len);
    len = 
        (sizeof(inst_config->nginx_buffer) - 1) - inst_config->nginx_buffer_len;
  }

  if (len == 0)
    return len;

  memcpy(&inst_config->nginx_buffer[inst_config->nginx_buffer_len], buf, len);
  inst_config->nginx_buffer_len += len;
  inst_config->nginx_buffer[inst_config->nginx_buffer_len] = 0;
  return len;
}

static int config_set(char **var, const char *value) {
  if (*var != NULL) {
    free(*var);
    *var = NULL;
  }

  if ((*var = strdup(value)) == NULL)
    return 1;
  else
    return 0;
} /* int config_set */

static int config_inst_parameter(nginx_inst_config_t *inst_config,
                                 const char *key, const char *value) {
  if (strcasecmp(key, "url") == 0)
    return config_set(&inst_config->url, value);
  else if (strcasecmp(key, "user") == 0)
    return config_set(&inst_config->user, value);
  else if (strcasecmp(key, "password") == 0)
    return config_set(&inst_config->pass, value);
  else if (strcasecmp(key, "verifypeer") == 0)
    return config_set(&inst_config->verify_peer, value);
  else if (strcasecmp(key, "verifyhost") == 0)
    return config_set(&inst_config->verify_host, value);
  else if (strcasecmp(key, "cacert") == 0)
    return config_set(&inst_config->cacert, value);
  else if (strcasecmp(key, "timeout") == 0)
    return config_set(&inst_config->timeout, value);
  else if (strcasecmp(key, "hostname") == 0)
    return config_set(&inst_config->host_name, value);
  else
    return -1;
} /* int config_inst_parameter */

static int init_curl(nginx_inst_config_t *inst_config) {
  if (inst_config->curl != NULL)
    curl_easy_cleanup(inst_config->curl);

  if ((inst_config->curl = curl_easy_init()) == NULL) {
    ERROR("nginx plugin: curl_easy_init failed.");
    return -1;
  }

  curl_easy_setopt(inst_config->curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(inst_config->curl, CURLOPT_WRITEFUNCTION,
                   nginx_curl_callback);
  curl_easy_setopt(inst_config->curl, CURLOPT_WRITEDATA, inst_config);
  curl_easy_setopt(inst_config->curl, CURLOPT_USERAGENT, COLLECTD_USERAGENT);
  curl_easy_setopt(inst_config->curl, CURLOPT_ERRORBUFFER,
                   inst_config->nginx_curl_error);

  if (inst_config->user != NULL) {
#ifdef HAVE_CURLOPT_USERNAME
    curl_easy_setopt(inst_config->curl, CURLOPT_USERNAME, inst_config->user);
    curl_easy_setopt(inst_config->curl, CURLOPT_PASSWORD,
                     (inst_config->pass == NULL) ? "" : inst_config->pass);
#else
    static char credentials[1024];
    int status =
        snprintf(credentials, sizeof(credentials), "%s:%s", inst_config->user,
                  inst_config->pass == NULL ? "" : inst_config->pass);
    if ((status < 0) || ((size_t)status >= sizeof(credentials))) {
      ERROR("nginx plugin: Credentials would have been truncated.");
      return -1;
    }

    curl_easy_setopt(inst_config->curl, CURLOPT_USERPWD, credentials);
#endif
  }

  if (inst_config->url != NULL) {
    curl_easy_setopt(inst_config->curl, CURLOPT_URL, inst_config->url);
  }

  curl_easy_setopt(inst_config->curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(inst_config->curl, CURLOPT_MAXREDIRS, 50L);

  if ((inst_config->verify_peer == NULL) || IS_TRUE(inst_config->verify_peer)) {
    curl_easy_setopt(inst_config->curl, CURLOPT_SSL_VERIFYPEER, 1L);
  } else {
    curl_easy_setopt(inst_config->curl, CURLOPT_SSL_VERIFYPEER, 0L);
  }

  if ((inst_config->verify_host == NULL) || IS_TRUE(inst_config->verify_host)) {
    curl_easy_setopt(inst_config->curl, CURLOPT_SSL_VERIFYHOST, 2L);
  } else {
    curl_easy_setopt(inst_config->curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  if (inst_config->cacert != NULL) {
    curl_easy_setopt(inst_config->curl, CURLOPT_CAINFO, inst_config->cacert);
  }

#ifdef HAVE_CURLOPT_TIMEOUT_MS
  if (inst_config->timeout != NULL) {
    curl_easy_setopt(inst_config->curl, CURLOPT_TIMEOUT_MS,
                     atol(inst_config->timeout));
  } else {
    curl_easy_setopt(inst_config->curl, CURLOPT_TIMEOUT_MS,
                     (long)CDTIME_T_TO_MS(plugin_get_interval()));
  }
#endif

  return (0);
} /* void init_curl */

static void submit(const nginx_inst_config_t *inst_config,
                   const char *type, const char *inst,
                   long long value) {

  const char* inst_name = inst_config->inst_name;

  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  if (strcmp(type, "nginx_connections") == 0)
    values[0].gauge = value;
  else if (strcmp(type, "nginx_requests") == 0)
    values[0].derive = value;
  else if (strcmp(type, "connections") == 0)
    values[0].derive = value;
  else
    return;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);

  if (inst_name != NO_INSTNAME) {
    char* host_name = inst_config->host_name;
    if (host_name==NULL)
      host_name = hostname_g;

    sstrncpy(vl.host, host_name, sizeof(vl.host));
    sstrncpy(vl.plugin_instance, inst_name, sizeof(vl.plugin_instance));
  }

  sstrncpy(vl.plugin, "nginx", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));

  if (inst != NULL)
    sstrncpy(vl.type_instance, inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void submit */

static int nginx_read(user_data_t *ud) {
  char *ptr;
  char *lines[16];
  int lines_num = 0;
  char *saveptr;

  char *fields[16];
  int fields_num;

  nginx_inst_config_t *inst_config = (nginx_inst_config_t *)ud->data;

  if (inst_config->curl == NULL)
    return -1;
  if (inst_config->url == NULL)
    return -1;

  inst_config->nginx_buffer_len = 0;

  if (curl_easy_perform(inst_config->curl) != CURLE_OK) {
    WARNING("nginx plugin: curl_easy_perform failed: %s",
            inst_config->nginx_curl_error);
    return -1;
  }

  ptr = inst_config->nginx_buffer;
  saveptr = NULL;
  while ((lines[lines_num] = strtok_r(ptr, "\n\r", &saveptr)) != NULL) {
    ptr = NULL;
    lines_num++;

    if (lines_num >= 16)
      break;
  }

  /*
   * Active connections: 291
   * server accepts handled requests
   *  101059015 100422216 347910649
   * Reading: 6 Writing: 179 Waiting: 106
   */
  for (int i = 0; i < lines_num; i++) {
    fields_num =
        strsplit(lines[i], fields, (sizeof(fields) / sizeof(fields[0])));

    if (fields_num == 3) {
      if ((strcmp(fields[0], "Active") == 0) &&
          (strcmp(fields[1], "connections:") == 0)) {
        submit(inst_config, "nginx_connections", "active",atoll(fields[2]));
      } else if ((atoll(fields[0]) != 0) && (atoll(fields[1]) != 0) &&
                 (atoll(fields[2]) != 0)) {
        submit(inst_config, "connections", "accepted", atoll(fields[0]));
        /* TODO: The legacy metric "handled", which is the sum of "accepted" and
         * "failed", is reported for backwards compatibility only. Remove in the
         * next major version. */ 
        submit(inst_config, "connections", "handled", atoll(fields[1]));
        submit(inst_config, "connections", "failed", 
               (atoll(fields[0]) - atoll(fields[1])));
        submit(inst_config, "nginx_requests", NULL, atoll(fields[2]));
      }
    } else if (fields_num == 6) {
      if ((strcmp(fields[0], "Reading:") == 0) &&
          (strcmp(fields[2], "Writing:") == 0) &&
          (strcmp(fields[4], "Waiting:") == 0)) {
        submit(inst_config, "nginx_connections", "reading", atoll(fields[1]));
        submit(inst_config, "nginx_connections", "writing", atoll(fields[3]));
        submit(inst_config, "nginx_connections", "waiting", atoll(fields[5]));
      }
    }
  }

  inst_config->nginx_buffer_len = 0;

  return 0;
} /* int nginx_read */

static void nginx_inst_cleanup(void *cleanup_data) {
  nginx_inst_config_t *inst_config = (nginx_inst_config_t *)cleanup_data;

  DEBUG("nginx plugin: cleaning data for %s (%s).",
        inst_config->url, inst_config->inst_name);

  if (inst_config->curl != NULL)
    curl_easy_cleanup(inst_config->curl);

  sfree(cleanup_data);
} /* void nginx_inst_cleanup */


static int config_inst(char* inst_name, oconfig_item_t *inst_config_item) {
  nginx_inst_config_t *inst_config;

  oconfig_item_t *config_parameter;
  char *inst_config_key;
  char *inst_config_value;
  int status;

  inst_config = calloc(1, sizeof(*inst_config));

  if (inst_config == NULL) {
    ERROR("nginx plugin: calloc failed.");
    return -1;
  }

  inst_config->host_name = NULL;
  inst_config->inst_name = (inst_name==NULL) ? NULL : strdup(inst_name);

  for (int i = 0; i < inst_config_item->children_num; i++) {
    config_parameter = inst_config_item->children + i;
    inst_config_key = config_parameter->key;
    inst_config_value = config_parameter->values[0].value.string;
    status =
        config_inst_parameter(inst_config, inst_config_key, inst_config_value);
    if (status != 0) {
      ERROR("nginx plugin: config parsing failed.");
      sfree(inst_config);
      return -1;
    }
  }

  status = init_curl(inst_config);
  if (status != 0) {
    ERROR("nginx plugin: curl initialization failed.");
    sfree(inst_config);
    return -1;
  }

  user_data_t ud;
  char cb_name[DATA_MAX_NAME_LEN];

  if (inst_config->inst_name == NO_INSTNAME) {
    DEBUG("nginx plugin: Registering new read callback for single instance %s",
          inst_config->url);
    snprintf(cb_name, sizeof(cb_name), "%s", "nginx");
  } else {
    DEBUG("nginx plugin: Registering new read callback for instance %s (%s).",
          inst_config->url, inst_config->inst_name);
    snprintf(cb_name, sizeof(cb_name), "nginx.%s",
             inst_config->inst_name);
  }   

  memset(&ud, 0, sizeof(ud));

  ud.data = inst_config;
  ud.free_func = nginx_inst_cleanup;

  plugin_register_complex_read(NULL, cb_name, nginx_read, 0, &ud);

  return 0;
} /* config_inst */

static int config_multiinstance(oconfig_item_t *ci) {

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Instance", child->key) == 0) {

      if ((child->values_num != 1) ||
          (child->values[0].type != OCONFIG_TYPE_STRING)) {
        WARNING("nginx plugin: The Instance block "
                "needs exactly one string argument.");
      } else {
        if (config_inst(child->values[0].value.string, child) != 0)
          return -1;
      }

    } else {
      WARNING("nginx plugin: Ignoring unknown config option `%s'.  "
              "expected 'Instance' block.", child->key);
    }
  }
  return 0;
} /* int config_multiinstance */

static int config_singleinstance(oconfig_item_t *ci) {
  return config_inst(NO_INSTNAME, ci);
} /* int config_singleinstance */

static int config_plugin(oconfig_item_t *ci) {
  int multi_inst = 0;
  int rv = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Instance", child->key) == 0) {
      multi_inst = 1;
      break; 
    }
  }

  if (multi_inst != 0) {
    DEBUG("nginx plugin: initializing as multi-instance.");
    rv = config_multiinstance(ci);
  } else {
    DEBUG("nginx plugin: initializing as single-instance "
          "(backward compatibility).");
    rv = config_singleinstance(ci);
  }
  return rv;
} /* int config_plugin */

void module_register(void) {
  plugin_register_complex_config("nginx", config_plugin);
} /* void module_register */
