/**
 * collectd - src/nginx.c
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Sebastian Harl
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
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <curl/curl.h>

/* Available options for nginx instance - see nginx.conf description */
static char *config_keys[] = {"URL",        "User",   "Password", "VerifyPeer",
                              "VerifyHost", "CACert", "Timeout",  "Socket"};

static const int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
/******************/

/* A temporary list of strings where per nginx instance settings from collectd
config are stored before passing the to curl an index of setting in this list
corellates to the one from config_keys list, e.g.
parsed_values_for_nginx_instance[0] stores URL string,
parsed_values_for_nginx_instance[1] - User etc. */
static char **parsed_values_for_nginx_instance = NULL;
/******************/

/* Stores name and curl options of the nginx instance */
#define MAX_NGINX_INSTANCES                                                    \
  1000 // Max nginx instances that plugin can serve simultaneously

static struct nginx_instance {
  char name[DATA_MAX_NAME_LEN];
  CURL *curl;
} ngx_inst[MAX_NGINX_INSTANCES];

static int instance_num_counter =
    0; // Counts number of configured nginx instances
/******************/

static char nginx_buffer[16384];
static size_t nginx_buffer_len;
static char nginx_curl_error[CURL_ERROR_SIZE];

static size_t nginx_curl_callback(void *buf, size_t size, size_t nmemb,
                                  void __attribute__((unused)) * stream) {
  size_t len = size * nmemb;

  /* Check if the data fits into the memory. If not, truncate it. */
  if ((nginx_buffer_len + len) >= sizeof(nginx_buffer)) {
    assert(sizeof(nginx_buffer) > nginx_buffer_len);
    len = (sizeof(nginx_buffer) - 1) - nginx_buffer_len;
  }

  if (len == 0)
    return len;

  memcpy(&nginx_buffer[nginx_buffer_len], buf, len);
  nginx_buffer_len += len;
  nginx_buffer[nginx_buffer_len] = 0;

  return len;
}

/* Zeroing per nginx instance temporary stringlist for getting settings from
 * collectd config for each nginx instance */
static void init_nginx_instance_settings_array(void) {
  for (int i = 0; i < config_keys_num; i++) {
    for (int j = 0; j < DATA_MAX_NAME_LEN; j++) {
      parsed_values_for_nginx_instance[i][j] = '\0';
    }
  }
}
/******************/

/* Cleanup per nginx instance temporary stringlist for storing settings from
 * collectd config for each nginx instance  */
static void deinit_nginx_instance_settings_array(void) {
  for (int i = 0; i < config_keys_num; i++) {
    if (parsed_values_for_nginx_instance[i] != NULL) {
      sfree(parsed_values_for_nginx_instance[i]);
      parsed_values_for_nginx_instance[i] = NULL;
    }
  }
  if (parsed_values_for_nginx_instance != NULL) {
    sfree(parsed_values_for_nginx_instance);
    parsed_values_for_nginx_instance = NULL;
  }
  DEBUG("nginx plugin: finished shutdown");
}
/******************/

/* Configure default curl options for nginx instance */
static void set_default_curl_opts() {
  curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_WRITEFUNCTION,
                   nginx_curl_callback);
  curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_USERAGENT,
                   COLLECTD_USERAGENT);
  curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_ERRORBUFFER,
                   nginx_curl_error);
  curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_FOLLOWLOCATION,
                   1L);
  curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_MAXREDIRS, 50L);
}
/******************/

/* Configure optional curl settings for nginx instance from collectd
 * configuration file */
static int set_optional_curl_params() {
  if (parsed_values_for_nginx_instance[1][0] != '\0') {
#ifdef HAVE_CURLOPT_USERNAME
    curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_USERNAME,
                     parsed_values_for_nginx_instance[1]);
    curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_PASSWORD,
                     (parsed_values_for_nginx_instance[2][0] == '\0')
                         ? ""
                         : parsed_values_for_nginx_instance[2]);
#else
    static char credentials[1024];
    int status = ssnprintf(credentials, sizeof(credentials), "%s:%s",
                           parsed_values_for_nginx_instance[1],
                           parsed_values_for_nginx_instance[2][0] == '\0'
                               ? ""
                               : parsed_values_for_nginx_instance[2]);
    if ((status < 0) || ((size_t)status >= sizeof(credentials))) {
      ERROR("nginx plugin: Credentials would have been truncated.");
      return -1;
    }

    curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_USERPWD,
                     credentials);
#endif
  }

  if ((parsed_values_for_nginx_instance[3][0] == '\0') ||
      IS_TRUE(parsed_values_for_nginx_instance[3])) {
    curl_easy_setopt(ngx_inst[instance_num_counter].curl,
                     CURLOPT_SSL_VERIFYPEER, 1L);
  } else {
    curl_easy_setopt(ngx_inst[instance_num_counter].curl,
                     CURLOPT_SSL_VERIFYPEER, 0L);
  }

  if ((parsed_values_for_nginx_instance[4][0] == '\0') ||
      IS_TRUE(parsed_values_for_nginx_instance[4])) {
    curl_easy_setopt(ngx_inst[instance_num_counter].curl,
                     CURLOPT_SSL_VERIFYHOST, 2L);
  } else {
    curl_easy_setopt(ngx_inst[instance_num_counter].curl,
                     CURLOPT_SSL_VERIFYHOST, 0L);
  }

  if (parsed_values_for_nginx_instance[5][0] != '\0') {
    curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_CAINFO,
                     parsed_values_for_nginx_instance[5]);
  }

#ifdef HAVE_CURLOPT_TIMEOUT_MS
  if (parsed_values_for_nginx_instance[6][0] != '\0') {
    curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_TIMEOUT_MS,
                     atol(parsed_values_for_nginx_instance[6]));
  } else {
    curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_TIMEOUT_MS,
                     (long)CDTIME_T_TO_MS(plugin_get_interval()));
  }
#endif

#ifdef HAVE_CURLOPT_UNIX_SOCKET_PATH
  if (parsed_values_for_nginx_instance[7][0] != '\0') {
    curl_easy_setopt(ngx_inst[instance_num_counter].curl,
                     CURLOPT_UNIX_SOCKET_PATH,
                     parsed_values_for_nginx_instance[7]);
  }
#endif

  curl_easy_setopt(ngx_inst[instance_num_counter].curl, CURLOPT_URL,
                   parsed_values_for_nginx_instance[0]);
  return 0;
}
/******************/

/* Initialize CURL structure for the conf and set up default options */
static int init_curl_structure() {

  if (ngx_inst[instance_num_counter].curl != NULL)
    curl_easy_cleanup(ngx_inst[instance_num_counter].curl);

  if ((ngx_inst[instance_num_counter].curl = curl_easy_init()) == NULL) {
    ERROR("nginx plugin: curl_easy_init failed.");
    return -1;
  }

  return 0;
}
/******************/

/******* Nginx plugin config parsing. Inspired by ceph plugin config parser
 * implementation ( see ceph.c ) *******/
static int nginx_handle_str(struct oconfig_item_s *item, char *dest,
                            int dest_len) {
  const char *val;
  if (item->values_num != 1) {
    return -ENOTSUP;
  }
  if (item->values[0].type != OCONFIG_TYPE_STRING) {
    return -ENOTSUP;
  }
  val = item->values[0].value.string;
  if (snprintf(dest, dest_len, "%s", val) > (dest_len - 1)) {
    ERROR("nginx plugin: configuration parameter '%s' is too long.\n",
          item->key);
    return -ENAMETOOLONG;
  }
  return 0;
}

static int nginx_add_daemon_config(oconfig_item_t *ci) {
  int ret;

  /* Get instance name first - we need it for building metrics prefix as well */
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("nginx plugin: `InstanceName' block must have only name as an "
            "argument.");
    return -1;
  }

  ret = nginx_handle_str(ci, ngx_inst[instance_num_counter].name,
                         DATA_MAX_NAME_LEN);
  if (ret) {
    return ret;
  }

  if (ngx_inst[instance_num_counter].name[0] == '\0') {
    ERROR("nginx plugin: you must configure nginx instance name.\n");
    return -EINVAL;
  }
  /*****************************************************************************/

  ret = init_curl_structure();
  if (ret) {
    return ret;
  }
  set_default_curl_opts(); // Set default curl params for nginx instance

  init_nginx_instance_settings_array(); // Initialize stringlist berore parsing
                                        // collectd config for nginx instance

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    for (int j = 0; j < config_keys_num; j++) {
      if (strcasecmp(config_keys[j], child->key) == 0) {
        ret = nginx_handle_str(child, parsed_values_for_nginx_instance[j],
                               DATA_MAX_NAME_LEN);
        if (ret) {
          return ret;
        }
        break;
      }
    }
  }

  ret =
      set_optional_curl_params(); // Set optional curl params for nginx instance
  if (ret) {
    return ret;
  }

  instance_num_counter++; // Increase counter of configured nginx instances.

  return 0;
}

static int nginx_config(oconfig_item_t *ci) {
  int ret;

  parsed_values_for_nginx_instance =
      (char **)calloc(config_keys_num, sizeof(char *));
  for (int i = 0; i < config_keys_num; i++) {
    parsed_values_for_nginx_instance[i] =
        (char *)calloc(DATA_MAX_NAME_LEN, sizeof(char));
  }

  for (int i = 0; i < MAX_NGINX_INSTANCES; i++) {
    for (int j = 0; j < DATA_MAX_NAME_LEN; j++) {
      ngx_inst[i].name[j] = '\0';
    }
    ngx_inst[i].curl = NULL;
  }

  for (int i = 0; i < ci->children_num; ++i) {
    if (instance_num_counter > MAX_NGINX_INSTANCES - 1) {
      WARNING("nginx plugin: number of configured nginx instances execeeded. "
              "Limit is %d.",
              MAX_NGINX_INSTANCES);
      break;
    } else {
      oconfig_item_t *child = ci->children + i;
      if (strcasecmp("instancename", child->key) == 0) {
        ret = nginx_add_daemon_config(child);
        if (ret) {
          // process other instances and ignore this one
          continue;
        }
      } else {
        WARNING("nginx plugin: ignoring unknown option %s", child->key);
      }
    }
  }
  DEBUG("nginx plugin: OK before array deinit");
  deinit_nginx_instance_settings_array();
  return 0;
}

/****************************************************************************************/

static void submit(const char *instance_name, const char *type,
                   const char *inst, long long value) {
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
  sstrncpy(vl.plugin, instance_name, sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));

  if (inst != NULL)
    sstrncpy(vl.type_instance, inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void submit */

static int nginx_read(void) {
  char *ptr;
  char *lines[16];
  int lines_num;
  char *saveptr;

  char *fields[16];
  int fields_num;

  for (int n = 0; n < instance_num_counter; n++) {
    lines_num = 0;
    nginx_buffer_len = 0;
    if (curl_easy_perform(ngx_inst[n].curl) != CURLE_OK) {
      WARNING("nginx plugin: curl_easy_perform failed: %s", nginx_curl_error);
      return -1;
    }

    ptr = nginx_buffer;
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
          submit(ngx_inst[n].name, "nginx_connections", "active",
                 atoll(fields[2]));
        } else if ((atoll(fields[0]) != 0) && (atoll(fields[1]) != 0) &&
                   (atoll(fields[2]) != 0)) {
          submit(ngx_inst[n].name, "connections", "accepted", atoll(fields[0]));
          /* TODO: The legacy metric "handled", which is the sum of "accepted"
           * and "failed", is reported for backwards compatibility only. Remove
           * in the next major version. */
          submit(ngx_inst[n].name, "connections", "handled", atoll(fields[1]));
          submit(ngx_inst[n].name, "connections", "failed",
                 (atoll(fields[0]) - atoll(fields[1])));
          submit(ngx_inst[n].name, "nginx_requests", NULL, atoll(fields[2]));
        }
      } else if (fields_num == 6) {
        if ((strcmp(fields[0], "Reading:") == 0) &&
            (strcmp(fields[2], "Writing:") == 0) &&
            (strcmp(fields[4], "Waiting:") == 0)) {
          submit(ngx_inst[n].name, "nginx_connections", "reading",
                 atoll(fields[1]));
          submit(ngx_inst[n].name, "nginx_connections", "writing",
                 atoll(fields[3]));
          submit(ngx_inst[n].name, "nginx_connections", "waiting",
                 atoll(fields[5]));
        }
      }
    }
  }

  return 0;
} /* int nginx_read */

static int nginx_plugin_init(void) {
  DEBUG("nginx plugin: Init completed");
  return 0;
}

void module_register(void) {
  plugin_register_init("nginx", nginx_plugin_init);
  plugin_register_complex_config("nginx", nginx_config);
  plugin_register_read("nginx", nginx_read);
} /* void module_register */
