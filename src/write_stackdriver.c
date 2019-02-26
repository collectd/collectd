/**
 * collectd - src/write_stackdriver.c
 * ISC license
 *
 * Copyright (C) 2017  Florian Forster
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "configfile.h"
#include "plugin.h"
#include "utils/common/common.h"
#include "utils/format_stackdriver/format_stackdriver.h"
#include "utils/gce/gce.h"
#include "utils/oauth/oauth.h"

#include <curl/curl.h>
#include <pthread.h>
#include <yajl/yajl_tree.h>

/*
 * Private variables
 */
#ifndef GCM_API_URL
#define GCM_API_URL "https://monitoring.googleapis.com/v3"
#endif

#ifndef MONITORING_SCOPE
#define MONITORING_SCOPE "https://www.googleapis.com/auth/monitoring"
#endif

struct wg_callback_s {
  /* config */
  char *email;
  char *project;
  char *url;
  sd_resource_t *resource;

  /* runtime */
  oauth_t *auth;
  sd_output_t *formatter;
  CURL *curl;
  char curl_errbuf[CURL_ERROR_SIZE];
  /* used by flush */
  size_t timeseries_count;
  cdtime_t send_buffer_init_time;

  pthread_mutex_t lock;
};
typedef struct wg_callback_s wg_callback_t;

struct wg_memory_s {
  char *memory;
  size_t size;
};
typedef struct wg_memory_s wg_memory_t;

static size_t wg_write_memory_cb(void *contents, size_t size,
                                 size_t nmemb, /* {{{ */
                                 void *userp) {
  size_t realsize = size * nmemb;
  wg_memory_t *mem = (wg_memory_t *)userp;

  if (0x7FFFFFF0 < mem->size || 0x7FFFFFF0 - mem->size < realsize) {
    ERROR("integer overflow");
    return 0;
  }

  mem->memory = (char *)realloc((void *)mem->memory, mem->size + realsize + 1);
  if (mem->memory == NULL) {
    /* out of memory! */
    ERROR("wg_write_memory_cb: not enough memory (realloc returned NULL)");
    return 0;
  }

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
  return realsize;
} /* }}} size_t wg_write_memory_cb */

static char *wg_get_authorization_header(wg_callback_t *cb) { /* {{{ */
  int status = 0;
  char access_token[256];
  char authorization_header[256];

  assert((cb->auth != NULL) || gce_check());
  if (cb->auth != NULL)
    status = oauth_access_token(cb->auth, access_token, sizeof(access_token));
  else
    status = gce_access_token(cb->email, access_token, sizeof(access_token));
  if (status != 0) {
    ERROR("write_stackdriver plugin: Failed to get access token");
    return NULL;
  }

  status = snprintf(authorization_header, sizeof(authorization_header),
                    "Authorization: Bearer %s", access_token);
  if ((status < 1) || ((size_t)status >= sizeof(authorization_header)))
    return NULL;

  return strdup(authorization_header);
} /* }}} char *wg_get_authorization_header */

typedef struct {
  int code;
  char *message;
} api_error_t;

static api_error_t *parse_api_error(char const *body) {
  char errbuf[1024];
  yajl_val root = yajl_tree_parse(body, errbuf, sizeof(errbuf));
  if (root == NULL) {
    ERROR("write_stackdriver plugin: yajl_tree_parse failed: %s", errbuf);
    return NULL;
  }

  api_error_t *err = calloc(1, sizeof(*err));
  if (err == NULL) {
    ERROR("write_stackdriver plugin: calloc failed");
    yajl_tree_free(root);
    return NULL;
  }

  yajl_val code = yajl_tree_get(root, (char const *[]){"error", "code", NULL},
                                yajl_t_number);
  if (YAJL_IS_INTEGER(code)) {
    err->code = YAJL_GET_INTEGER(code);
  }

  yajl_val message = yajl_tree_get(
      root, (char const *[]){"error", "message", NULL}, yajl_t_string);
  if (YAJL_IS_STRING(message)) {
    char const *m = YAJL_GET_STRING(message);
    if (m != NULL) {
      err->message = strdup(m);
    }
  }

  return err;
}

static char *api_error_string(api_error_t *err, char *buffer,
                              size_t buffer_size) {
  if (err == NULL) {
    strncpy(buffer, "Unknown error (API error is NULL)", buffer_size);
  } else if (err->message == NULL) {
    snprintf(buffer, buffer_size, "API error %d", err->code);
  } else {
    snprintf(buffer, buffer_size, "API error %d: %s", err->code, err->message);
  }

  return buffer;
}
#define API_ERROR_STRING(err) api_error_string(err, (char[1024]){""}, 1024)

// do_post does a HTTP POST request, assuming a JSON payload and using OAuth
// authentication. Returns -1 on error and the HTTP status code otherwise.
// ret_content, if not NULL, will contain the server's response.
// If ret_content is provided and the server responds with a 4xx or 5xx error,
// an appropriate message will be logged.
static int do_post(wg_callback_t *cb, char const *url, void const *payload,
                   wg_memory_t *ret_content) {
  if (cb->curl == NULL) {
    cb->curl = curl_easy_init();
    if (cb->curl == NULL) {
      ERROR("write_stackdriver plugin: curl_easy_init() failed");
      return -1;
    }

    curl_easy_setopt(cb->curl, CURLOPT_ERRORBUFFER, cb->curl_errbuf);
    curl_easy_setopt(cb->curl, CURLOPT_NOSIGNAL, 1L);
  }

  curl_easy_setopt(cb->curl, CURLOPT_POST, 1L);
  curl_easy_setopt(cb->curl, CURLOPT_URL, url);

  long timeout_ms = 2 * CDTIME_T_TO_MS(plugin_get_interval());
  if (timeout_ms < 10000) {
    timeout_ms = 10000;
  }
  curl_easy_setopt(cb->curl, CURLOPT_TIMEOUT_MS, timeout_ms);

  /* header */
  char *auth_header = wg_get_authorization_header(cb);
  if (auth_header == NULL) {
    ERROR("write_stackdriver plugin: getting access token failed with");
    return -1;
  }

  struct curl_slist *headers =
      curl_slist_append(NULL, "Content-Type: application/json");
  headers = curl_slist_append(headers, auth_header);
  curl_easy_setopt(cb->curl, CURLOPT_HTTPHEADER, headers);

  curl_easy_setopt(cb->curl, CURLOPT_POSTFIELDS, payload);

  curl_easy_setopt(cb->curl, CURLOPT_WRITEFUNCTION,
                   ret_content ? wg_write_memory_cb : NULL);
  curl_easy_setopt(cb->curl, CURLOPT_WRITEDATA, ret_content);

  int status = curl_easy_perform(cb->curl);

  /* clean up that has to happen in any case */
  curl_slist_free_all(headers);
  sfree(auth_header);
  curl_easy_setopt(cb->curl, CURLOPT_HTTPHEADER, NULL);
  curl_easy_setopt(cb->curl, CURLOPT_WRITEFUNCTION, NULL);
  curl_easy_setopt(cb->curl, CURLOPT_WRITEDATA, NULL);

  if (status != CURLE_OK) {
    ERROR("write_stackdriver plugin: POST %s failed: %s", url, cb->curl_errbuf);
    if (ret_content != NULL) {
      sfree(ret_content->memory);
      ret_content->size = 0;
    }
    return -1;
  }

  long http_code = 0;
  curl_easy_getinfo(cb->curl, CURLINFO_RESPONSE_CODE, &http_code);

  if (ret_content != NULL) {
    if ((http_code >= 400) && (http_code < 500)) {
      ERROR("write_stackdriver plugin: POST %s: %s", url,
            API_ERROR_STRING(parse_api_error(ret_content->memory)));
    } else if (http_code >= 500) {
      WARNING("write_stackdriver plugin: POST %s: %s", url,
              ret_content->memory);
    }
  }

  return (int)http_code;
} /* int do_post */

static int wg_call_metricdescriptor_create(wg_callback_t *cb,
                                           char const *payload) {
  char url[1024];
  snprintf(url, sizeof(url), "%s/projects/%s/metricDescriptors", cb->url,
           cb->project);
  wg_memory_t response = {0};

  int status = do_post(cb, url, payload, &response);
  if (status == -1) {
    ERROR("write_stackdriver plugin: POST %s failed", url);
    return -1;
  }
  sfree(response.memory);

  if (status != 200) {
    ERROR("write_stackdriver plugin: POST %s: unexpected response code: got "
          "%d, want 200",
          url, status);
    return -1;
  }
  return 0;
} /* int wg_call_metricdescriptor_create */

static int wg_call_timeseries_write(wg_callback_t *cb, char const *payload) {
  char url[1024];
  snprintf(url, sizeof(url), "%s/projects/%s/timeSeries", cb->url, cb->project);
  wg_memory_t response = {0};

  int status = do_post(cb, url, payload, &response);
  if (status == -1) {
    ERROR("write_stackdriver plugin: POST %s failed", url);
    return -1;
  }
  sfree(response.memory);

  if (status != 200) {
    ERROR("write_stackdriver plugin: POST %s: unexpected response code: got "
          "%d, want 200",
          url, status);
    return -1;
  }
  return 0;
} /* int wg_call_timeseries_write */

static void wg_reset_buffer(wg_callback_t *cb) /* {{{ */
{
  cb->timeseries_count = 0;
  cb->send_buffer_init_time = cdtime();
} /* }}} wg_reset_buffer */

static int wg_callback_init(wg_callback_t *cb) /* {{{ */
{
  if (cb->curl != NULL)
    return 0;

  cb->formatter = sd_output_create(cb->resource);
  if (cb->formatter == NULL) {
    ERROR("write_stackdriver plugin: sd_output_create failed.");
    return -1;
  }

  cb->curl = curl_easy_init();
  if (cb->curl == NULL) {
    ERROR("write_stackdriver plugin: curl_easy_init failed.");
    return -1;
  }

  curl_easy_setopt(cb->curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(cb->curl, CURLOPT_USERAGENT,
                   PACKAGE_NAME "/" PACKAGE_VERSION);
  curl_easy_setopt(cb->curl, CURLOPT_ERRORBUFFER, cb->curl_errbuf);
  wg_reset_buffer(cb);

  return 0;
} /* }}} int wg_callback_init */

static int wg_flush_nolock(cdtime_t timeout, wg_callback_t *cb) /* {{{ */
{
  if (cb->timeseries_count == 0) {
    cb->send_buffer_init_time = cdtime();
    return 0;
  }

  /* timeout == 0  => flush unconditionally */
  if (timeout > 0) {
    cdtime_t now = cdtime();

    if ((cb->send_buffer_init_time + timeout) > now)
      return 0;
  }

  char *payload = sd_output_reset(cb->formatter);
  int status = wg_call_timeseries_write(cb, payload);
  wg_reset_buffer(cb);
  return status;
} /* }}} wg_flush_nolock */

static int wg_flush(cdtime_t timeout, /* {{{ */
                    const char *identifier __attribute__((unused)),
                    user_data_t *user_data) {
  wg_callback_t *cb;
  int status;

  if (user_data == NULL)
    return -EINVAL;

  cb = user_data->data;

  pthread_mutex_lock(&cb->lock);

  if (cb->curl == NULL) {
    status = wg_callback_init(cb);
    if (status != 0) {
      ERROR("write_stackdriver plugin: wg_callback_init failed.");
      pthread_mutex_unlock(&cb->lock);
      return -1;
    }
  }

  status = wg_flush_nolock(timeout, cb);
  pthread_mutex_unlock(&cb->lock);

  return status;
} /* }}} int wg_flush */

static void wg_callback_free(void *data) /* {{{ */
{
  wg_callback_t *cb = data;
  if (cb == NULL)
    return;

  sd_output_destroy(cb->formatter);
  cb->formatter = NULL;

  sfree(cb->email);
  sfree(cb->project);
  sfree(cb->url);

  oauth_destroy(cb->auth);
  if (cb->curl) {
    curl_easy_cleanup(cb->curl);
  }

  sfree(cb);
} /* }}} void wg_callback_free */

static int wg_metric_descriptors_create(wg_callback_t *cb, const data_set_t *ds,
                                        const value_list_t *vl) {
  /* {{{ */
  for (size_t i = 0; i < ds->ds_num; i++) {
    char buffer[4096];

    int status = sd_format_metric_descriptor(buffer, sizeof(buffer), ds, vl, i);
    if (status != 0) {
      ERROR("write_stackdriver plugin: sd_format_metric_descriptor failed "
            "with status "
            "%d",
            status);
      return status;
    }

    status = wg_call_metricdescriptor_create(cb, buffer);
    if (status != 0) {
      ERROR("write_stackdriver plugin: wg_call_metricdescriptor_create failed "
            "with "
            "status %d",
            status);
      return status;
    }
  }

  return sd_output_register_metric(cb->formatter, ds, vl);
} /* }}} int wg_metric_descriptors_create */

static int wg_write(const data_set_t *ds, const value_list_t *vl, /* {{{ */
                    user_data_t *user_data) {
  wg_callback_t *cb = user_data->data;
  if (cb == NULL)
    return EINVAL;

  pthread_mutex_lock(&cb->lock);

  if (cb->curl == NULL) {
    int status = wg_callback_init(cb);
    if (status != 0) {
      ERROR("write_stackdriver plugin: wg_callback_init failed.");
      pthread_mutex_unlock(&cb->lock);
      return status;
    }
  }

  int status;
  while (42) {
    status = sd_output_add(cb->formatter, ds, vl);
    if (status == 0) { /* success */
      break;
    } else if (status == ENOBUFS) { /* success, flush */
      wg_flush_nolock(0, cb);
      status = 0;
      break;
    } else if (status == EEXIST) {
      /* metric already in the buffer; flush and retry */
      wg_flush_nolock(0, cb);
      continue;
    } else if (status == ENOENT) {
      /* new metric, create metric descriptor first */
      status = wg_metric_descriptors_create(cb, ds, vl);
      if (status != 0) {
        break;
      }
      continue;
    } else {
      break;
    }
  }

  if (status == 0) {
    cb->timeseries_count++;
  }

  pthread_mutex_unlock(&cb->lock);
  return status;
} /* }}} int wg_write */

static void wg_check_scope(char const *email) /* {{{ */
{
  char *scope = gce_scope(email);
  if (scope == NULL) {
    WARNING("write_stackdriver plugin: Unable to determine scope of this "
            "instance.");
    return;
  }

  if (strstr(scope, MONITORING_SCOPE) == NULL) {
    size_t scope_len;

    /* Strip trailing newline characers for printing. */
    scope_len = strlen(scope);
    while ((scope_len > 0) && (iscntrl((int)scope[scope_len - 1])))
      scope[--scope_len] = 0;

    WARNING("write_stackdriver plugin: The determined scope of this instance "
            "(\"%s\") does not contain the monitoring scope (\"%s\"). You need "
            "to add this scope to the list of scopes passed to gcutil with "
            "--service_account_scopes when creating the instance. "
            "Alternatively, to use this plugin on an instance which does not "
            "have this scope, use a Service Account.",
            scope, MONITORING_SCOPE);
  }

  sfree(scope);
} /* }}} void wg_check_scope */

static int wg_config_resource(oconfig_item_t *ci, wg_callback_t *cb) /* {{{ */
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    ERROR("write_stackdriver plugin: The \"%s\" option requires exactly one "
          "string "
          "argument.",
          ci->key);
    return EINVAL;
  }
  char *resource_type = ci->values[0].value.string;

  if (cb->resource != NULL) {
    sd_resource_destroy(cb->resource);
  }

  cb->resource = sd_resource_create(resource_type);
  if (cb->resource == NULL) {
    ERROR("write_stackdriver plugin: sd_resource_create(\"%s\") failed.",
          resource_type);
    return ENOMEM;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Label", child->key) == 0) {
      if ((child->values_num != 2) ||
          (child->values[0].type != OCONFIG_TYPE_STRING) ||
          (child->values[1].type != OCONFIG_TYPE_STRING)) {
        ERROR("write_stackdriver plugin: The \"Label\" option needs exactly "
              "two string arguments.");
        continue;
      }

      sd_resource_add_label(cb->resource, child->values[0].value.string,
                            child->values[1].value.string);
    }
  }

  return 0;
} /* }}} int wg_config_resource */

static int wg_config(oconfig_item_t *ci) /* {{{ */
{
  if (ci == NULL) {
    return EINVAL;
  }

  wg_callback_t *cb = calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("write_stackdriver plugin: calloc failed.");
    return ENOMEM;
  }
  cb->url = strdup(GCM_API_URL);
  pthread_mutex_init(&cb->lock, /* attr = */ NULL);

  char *credential_file = NULL;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Project", child->key) == 0)
      cf_util_get_string(child, &cb->project);
    else if (strcasecmp("Email", child->key) == 0)
      cf_util_get_string(child, &cb->email);
    else if (strcasecmp("Url", child->key) == 0)
      cf_util_get_string(child, &cb->url);
    else if (strcasecmp("CredentialFile", child->key) == 0)
      cf_util_get_string(child, &credential_file);
    else if (strcasecmp("Resource", child->key) == 0)
      wg_config_resource(child, cb);
    else {
      ERROR("write_stackdriver plugin: Invalid configuration option: %s.",
            child->key);
      wg_callback_free(cb);
      return EINVAL;
    }
  }

  /* Set up authentication */
  /* Option 1: Credentials file given => use service account */
  if (credential_file != NULL) {
    oauth_google_t cfg =
        oauth_create_google_file(credential_file, MONITORING_SCOPE);
    if (cfg.oauth == NULL) {
      ERROR("write_stackdriver plugin: oauth_create_google_file failed");
      wg_callback_free(cb);
      return EINVAL;
    }
    cb->auth = cfg.oauth;

    if (cb->project == NULL) {
      cb->project = cfg.project_id;
      INFO("write_stackdriver plugin: Automatically detected project ID: "
           "\"%s\"",
           cb->project);
    } else {
      sfree(cfg.project_id);
    }
  }
  /* Option 2: Look for credentials in well-known places */
  if (cb->auth == NULL) {
    oauth_google_t cfg = oauth_create_google_default(MONITORING_SCOPE);
    cb->auth = cfg.oauth;

    if (cb->project == NULL) {
      cb->project = cfg.project_id;
      INFO("write_stackdriver plugin: Automatically detected project ID: "
           "\"%s\"",
           cb->project);
    } else {
      sfree(cfg.project_id);
    }
  }

  if ((cb->auth != NULL) && (cb->email != NULL)) {
    NOTICE("write_stackdriver plugin: A service account email was configured "
           "but is "
           "not used for authentication because %s used instead.",
           (credential_file != NULL) ? "a credential file was"
                                     : "application default credentials were");
  }

  /* Option 3: Running on GCE => use metadata service */
  if ((cb->auth == NULL) && gce_check()) {
    wg_check_scope(cb->email);
  } else if (cb->auth == NULL) {
    ERROR("write_stackdriver plugin: Unable to determine credentials. Please "
          "either "
          "specify the \"Credentials\" option or set up Application Default "
          "Credentials.");
    wg_callback_free(cb);
    return EINVAL;
  }

  if ((cb->project == NULL) && gce_check()) {
    cb->project = gce_project_id();
  }
  if (cb->project == NULL) {
    ERROR("write_stackdriver plugin: Unable to determine the project number. "
          "Please specify the \"Project\" option manually.");
    wg_callback_free(cb);
    return EINVAL;
  }

  if ((cb->resource == NULL) && gce_check()) {
    /* TODO(octo): add error handling */
    cb->resource = sd_resource_create("gce_instance");
    sd_resource_add_label(cb->resource, "project_id", gce_project_id());
    sd_resource_add_label(cb->resource, "instance_id", gce_instance_id());
    sd_resource_add_label(cb->resource, "zone", gce_zone());
  }
  if (cb->resource == NULL) {
    /* TODO(octo): add error handling */
    cb->resource = sd_resource_create("global");
    sd_resource_add_label(cb->resource, "project_id", cb->project);
  }

  DEBUG("write_stackdriver plugin: Registering write callback with URL %s",
        cb->url);
  assert((cb->auth != NULL) || gce_check());

  user_data_t user_data = {
      .data = cb,
  };
  plugin_register_flush("write_stackdriver", wg_flush, &user_data);

  user_data.free_func = wg_callback_free;
  plugin_register_write("write_stackdriver", wg_write, &user_data);

  return 0;
} /* }}} int wg_config */

static int wg_init(void) {
  /* {{{ */
  /* Call this while collectd is still single-threaded to avoid
   * initialization issues in libgcrypt. */
  curl_global_init(CURL_GLOBAL_SSL);

  return 0;
} /* }}} int wg_init */

void module_register(void) /* {{{ */
{
  plugin_register_complex_config("write_stackdriver", wg_config);
  plugin_register_init("write_stackdriver", wg_init);
} /* }}} void module_register */
