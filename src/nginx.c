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

typedef struct {
  char *name;
  char *url;
  char *user;
  char *pass;
  bool verify_peer;
  bool verify_host;
  char *cacert;
  char *ssl_ciphers;
  int timeout;
  label_set_t labels;
  char nginx_buffer[16384];
  size_t nginx_buffer_len;
  char nginx_curl_error[CURL_ERROR_SIZE];
  CURL *curl;
} nginx_t;

enum {
  FAM_NGINX_CONN_ACTIVE,
  FAM_NGINX_CONN_ACCEPTED,
  FAM_NGINX_CONN_HANDLED,
  FAM_NGINX_CONN_FAILED,
  FAM_NGINX_CONN_READING,
  FAM_NGINX_CONN_WAITING,
  FAM_NGINX_CONN_WRITING,
  FAM_NGINX_HTTP_REQUESTS,
  FAM_NGINX_MAX
};

static void nginx_free(void *arg) {
  nginx_t *st = arg;

  if (st == NULL)
    return;

  sfree(st->name);
  sfree(st->url);
  sfree(st->user);
  sfree(st->pass);
  sfree(st->cacert);
  sfree(st->ssl_ciphers);
  label_set_reset(&st->labels);
  if (st->curl) {
    curl_easy_cleanup(st->curl);
    st->curl = NULL;
  }
  sfree(st);
}

static size_t nginx_curl_callback(void *buf, size_t size, size_t nmemb,
                                  void *user_data) {
  nginx_t *st = user_data;
  if (st == NULL) {
    ERROR("nginx plugin: nginx_curl_callback: "
          "user_data pointer is NULL.");
    return 0;
  }

  size_t len = size * nmemb;

  /* Check if the data fits into the memory. If not, truncate it. */
  if ((st->nginx_buffer_len + len) >= sizeof(st->nginx_buffer)) {
    assert(sizeof(st->nginx_buffer) > st->nginx_buffer_len);
    len = (sizeof(st->nginx_buffer) - 1) - st->nginx_buffer_len;
  }

  if (len == 0)
    return len;

  memcpy(&st->nginx_buffer[st->nginx_buffer_len], buf, len);
  st->nginx_buffer_len += len;
  st->nginx_buffer[st->nginx_buffer_len] = 0;

  return len;
}

static int init_host(nginx_t *st) {
  if (st->curl != NULL)
    curl_easy_cleanup(st->curl);

  if ((st->curl = curl_easy_init()) == NULL) {
    ERROR("nginx plugin: curl_easy_init failed.");
    return -1;
  }

  curl_easy_setopt(st->curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(st->curl, CURLOPT_WRITEFUNCTION, nginx_curl_callback);
  curl_easy_setopt(st->curl, CURLOPT_WRITEDATA, st);
  curl_easy_setopt(st->curl, CURLOPT_USERAGENT, COLLECTD_USERAGENT);
  curl_easy_setopt(st->curl, CURLOPT_ERRORBUFFER, st->nginx_curl_error);

  if (st->user != NULL) {
#ifdef HAVE_CURLOPT_USERNAME
    curl_easy_setopt(st->curl, CURLOPT_USERNAME, st->user);
    curl_easy_setopt(st->curl, CURLOPT_PASSWORD,
                     (st->pass == NULL) ? "" : st->pass);
#else
    static char credentials[1024];
    int status = ssnprintf(credentials, sizeof(credentials), "%s:%s", st->user,
                           st->pass == NULL ? "" : st->pass);
    if ((status < 0) || ((size_t)status >= sizeof(credentials))) {
      ERROR("nginx plugin: Credentials would have been truncated.");
      curl_easy_cleanup(st->curl);
      st->curl = NULL;
      return -1;
    }

    curl_easy_setopt(st->curl, CURLOPT_USERPWD, credentials);
#endif
  }

  curl_easy_setopt(st->curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(st->curl, CURLOPT_MAXREDIRS, 50L);

  curl_easy_setopt(st->curl, CURLOPT_SSL_VERIFYPEER, (long)st->verify_peer);
  curl_easy_setopt(st->curl, CURLOPT_SSL_VERIFYHOST, st->verify_host ? 2L : 0L);
  if (st->cacert != NULL)
    curl_easy_setopt(st->curl, CURLOPT_CAINFO, st->cacert);
  if (st->ssl_ciphers != NULL)
    curl_easy_setopt(st->curl, CURLOPT_SSL_CIPHER_LIST, st->ssl_ciphers);

#ifdef HAVE_CURLOPT_TIMEOUT_MS
  if (st->timeout >= 0)
    curl_easy_setopt(st->curl, CURLOPT_TIMEOUT_MS, st->timeout);
  else
    curl_easy_setopt(st->curl, CURLOPT_TIMEOUT_MS,
                     (long)CDTIME_T_TO_MS(plugin_get_interval()));
#endif

  return 0;
} /* void init */

static int nginx_read_host(user_data_t *user_data) {
  char *ptr;
  char *lines[16];
  int lines_num = 0;
  char *saveptr;
  char *fields[16];
  int fields_num;
  nginx_t *st = user_data->data;

  metric_family_t fams[FAM_NGINX_MAX] = {
      [FAM_NGINX_CONN_ACTIVE] =
          {
              .name = "nginx_connections_active",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_NGINX_CONN_ACCEPTED] =
          {
              .name = "nginx_connections_accepted",
              .type = METRIC_TYPE_COUNTER,
          },
      [FAM_NGINX_CONN_HANDLED] =
          {
              .name = "nginx_connections_handled",
              .type = METRIC_TYPE_COUNTER,
          },
      [FAM_NGINX_CONN_FAILED] =
          {
              .name = "nginx_connections_failed",
              .type = METRIC_TYPE_COUNTER,
          },
      [FAM_NGINX_CONN_READING] =
          {
              .name = "nginx_connections_reading",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_NGINX_CONN_WAITING] =
          {
              .name = "nginx_connections_waiting",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_NGINX_CONN_WRITING] =
          {
              .name = "nginx_connections_writing",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_NGINX_HTTP_REQUESTS] =
          {
              .name = "nginx_http_requests_total",
              .type = METRIC_TYPE_COUNTER,
          },
  };

  if (st->curl == NULL) {
    if (init_host(st) != 0)
      return -1;
  }

  if (st->url == NULL)
    return -1;

  st->nginx_buffer_len = 0;

  curl_easy_setopt(st->curl, CURLOPT_URL, st->url);

  if (curl_easy_perform(st->curl) != CURLE_OK) {
    WARNING("nginx plugin: curl_easy_perform failed: %s", st->nginx_curl_error);
    return -1;
  }

  ptr = st->nginx_buffer;
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
  metric_t m = {0};
  if (st->name != NULL)
    metric_label_set(&m, "instance", st->name);

  for (size_t i = 0; i < st->labels.num; i++) {
    metric_label_set(&m, st->labels.ptr[i].name, st->labels.ptr[i].value);
  }

  for (size_t i = 0; i < lines_num; i++) {
    fields_num =
        strsplit(lines[i], fields, (sizeof(fields) / sizeof(fields[0])));

    if (fields_num == 3) {
      if ((strcmp(fields[0], "Active") == 0) &&
          (strcmp(fields[1], "connections:") == 0)) {
        m.value.gauge = atoll(fields[2]);
        metric_family_metric_append(&fams[FAM_NGINX_CONN_ACTIVE], m);
      } else if ((atoll(fields[0]) != 0) && (atoll(fields[1]) != 0) &&
                 (atoll(fields[2]) != 0)) {
        m.value.counter = atoll(fields[0]);
        metric_family_metric_append(&fams[FAM_NGINX_CONN_ACCEPTED], m);
        /* TODO: The legacy metric "handled", which is the sum of "accepted" and
         * "failed", is reported for backwards compatibility only. Remove in the
         * next major version. */
        m.value.counter = atoll(fields[1]);
        metric_family_metric_append(&fams[FAM_NGINX_CONN_HANDLED], m);

        m.value.counter = atoll(fields[0]) - atoll(fields[1]);
        metric_family_metric_append(&fams[FAM_NGINX_CONN_FAILED], m);

        m.value.counter = atoll(fields[2]);
        metric_family_metric_append(&fams[FAM_NGINX_HTTP_REQUESTS], m);
      }
    } else if (fields_num == 6) {
      if ((strcmp(fields[0], "Reading:") == 0) &&
          (strcmp(fields[2], "Writing:") == 0) &&
          (strcmp(fields[4], "Waiting:") == 0)) {
        m.value.gauge = atoll(fields[1]);
        metric_family_metric_append(&fams[FAM_NGINX_CONN_READING], m);

        m.value.gauge = atoll(fields[3]);
        metric_family_metric_append(&fams[FAM_NGINX_CONN_WRITING], m);

        m.value.gauge = atoll(fields[5]);
        metric_family_metric_append(&fams[FAM_NGINX_CONN_WAITING], m);
      }
    }
  }

  st->nginx_buffer_len = 0;

  for (size_t i = 0; i < FAM_NGINX_MAX; i++) {
    if (fams[i].metric.num > 0) {
      int status = plugin_dispatch_metric_family(&fams[i]);
      if (status != 0) {
        ERROR("nginx plugin: plugin_dispatch_metric_family failed: %s",
              STRERROR(status));
      }
      metric_family_metric_reset(&fams[i]);
    }
  }

  return 0;
} /* int nginx_read */

/* Configuration handling functiions
 * <Plugin nginx>
 *   <Instance "instance_name">
 *     URL ...
 *   </Instance>
 *   URL ...
 * </Plugin>
 */
static int config_add(oconfig_item_t *ci) {
  nginx_t *st = calloc(1, sizeof(*st));
  if (st == NULL) {
    ERROR("nginx plugin: calloc failed.");
    return -1;
  }

  st->timeout = -1;

  int status = cf_util_get_string(ci, &st->name);
  if (status != 0) {
    sfree(st);
    return status;
  }
  assert(st->name != NULL);

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("URL", child->key) == 0)
      status = cf_util_get_string(child, &st->url);
    else if (strcasecmp("User", child->key) == 0)
      status = cf_util_get_string(child, &st->user);
    else if (strcasecmp("Password", child->key) == 0)
      status = cf_util_get_string(child, &st->pass);
    else if (strcasecmp("VerifyPeer", child->key) == 0)
      status = cf_util_get_boolean(child, &st->verify_peer);
    else if (strcasecmp("VerifyHost", child->key) == 0)
      status = cf_util_get_boolean(child, &st->verify_host);
    else if (strcasecmp("CACert", child->key) == 0)
      status = cf_util_get_string(child, &st->cacert);
    else if (strcasecmp("SSLCiphers", child->key) == 0)
      status = cf_util_get_string(child, &st->ssl_ciphers);
    else if (strcasecmp("Timeout", child->key) == 0)
      status = cf_util_get_int(child, &st->timeout);
    else if (strcasecmp("Label", child->key) == 0)
      status = cf_util_get_label(child, &st->labels);
    else {
      WARNING("nginx plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }
  /* Check if struct is complete.. */
  if ((status == 0) && (st->url == NULL)) {
    ERROR("nginx plugin: Instance `%s': "
          "No URL has been configured.",
          st->name);
    status = -1;
  }

  if (status != 0) {
    nginx_free(st);
    return -1;
  }

  char callback_name[3 * DATA_MAX_NAME_LEN];

  snprintf(callback_name, sizeof(callback_name), "nginx/%s",
           (st->name != NULL) ? st->name : "default");

  return plugin_register_complex_read(
      /* group = */ NULL,
      /* name      = */ callback_name,
      /* callback  = */ nginx_read_host,
      /* interval  = */ 0,
      &(user_data_t){
          .data = st,
          .free_func = nginx_free,
      });
} /* int config_add */

static int config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Instance", child->key) == 0)
      config_add(child);
    else
      WARNING("nginx plugin: The configuration option "
              "\"%s\" is not allowed here. Did you "
              "forget to add an <Instance /> block "
              "around the configuration?",
              child->key);
  } /* for (ci->children) */

  return 0;
} /* int config */

static int init(void) {
  /* Call this while collectd is still single-threaded to avoid
   * initialization issues in libgcrypt. */
  curl_global_init(CURL_GLOBAL_SSL);
  return 0;
}

void module_register(void) {
  plugin_register_complex_config("nginx", config);
  plugin_register_init("nginx", init);
} /* void module_register */
