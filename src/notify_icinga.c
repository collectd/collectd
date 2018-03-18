/**
 * collectd - src/notify_icinga.c
 * Copyright (C) 2017       Sergey "tnt4brain" Pechenko
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
 *   Sergey "tnt4brain" Pechenko <10977752+tnt4brain@users.noreply.github.com>
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <curl/curl.h>
#include <yajl/yajl_gen.h>

char *statusToString(yajl_gen_status code);
void err(yajl_gen_status code);

#include "utils_complain.h"

#ifndef ICINGA_CHECK_OK
#define ICINGA_CHECK_OK 0
#endif

#ifndef ICINGA_CHECK_WARNING
#define ICINGA_CHECK_WARNING 1
#endif

#ifndef ICINGA_CHECK_CRITICAL
#define ICINGA_CHECK_CRITICAL 2
#endif

#ifndef ICINGA_CHECK_UNKNOWN
#define ICINGA_CHECK_UNKNOWN 3
#endif

#ifndef ICINGA_HOST
#define ICINGA_HOST "127.0.0.1"
#endif

#ifndef ICINGA_PORT
#define ICINGA_PORT 5665
#endif

#ifndef ICINGA_URI_PATH
#define ICINGA_URI_PATH "/v1/actions/process-check-result"
#endif

#ifndef ICINGA_JSON_HEADER
#define ICINGA_JSON_HEADER "Accept: application/json"
#endif

#ifndef ICINGA_HOST_NOTIFICATION
#define ICINGA_HOST_NOTIFICATION "host"
#endif

#ifndef ICINGA_SERVICE_NOTIFICATION
#define ICINGA_SERVICE_NOTIFICATION "service"
#endif

#ifndef ICINGA_BUF_SIZE
#define ICINGA_BUF_SIZE 1428
#endif

#ifndef ICINGA_SERVICE
#define ICINGA_SERVICE "collectd"
#endif

struct ni_callback {
  int sock_fd;
  char *node;        // target host
  unsigned int port; // target port
  char *name; // this is to differ between different callbacks/plugin instances
  char *host; // target icinga-host
  char *service;   // target icinga-service
  char *user;      // user account to authenticate as
  char *password;  // password (see above)
  char *cert_file; // client cert can be used
  char *key_file;  // key file (see above)
  _Bool use_https; // protocol (http/https)
  _Bool use_cert;  // internal flag
  _Bool log_only;  // internal flag - log only, no POSTs
  char send_buf[ICINGA_BUF_SIZE];
  size_t send_buf_free;
  size_t send_buf_fill;

  pthread_mutex_t send_lock;
  c_complain_t init_complaint;
  cdtime_t last_connect_time;

  cdtime_t last_reconnect_time;
  cdtime_t reconnect_interval;
  _Bool reconnect_interval_reached;
};

/*curl -k -s -u root:icinga -H 'Accept: application/json' -X POST
'https://localhost:5665/v1/actions/process-check-result?service=example.localdomain!passive-ping6'
\
-d '{ "exit_status": 2,
 "plugin_output": "PING CRITICAL - Packet loss = 100%",
 "performance_data": [ "rta=5000.000000ms;3000.000000;5000.000000;0.000000",
                       "pl=100%;80;100;0" ],
 "check_source": "example.localdomain" }'

static const char *config_keys[] = {"IcingaHost", "IcingaPort", "IcingaUser",
                                    "IcingaPassword", "IcingaCert",
"IcingaKey"};

static const int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
*/

static void ni_callback_free(void *data) {
  struct ni_callback *cb;

  if (data == NULL)
    return;

  cb = data;

  pthread_mutex_lock(&cb->send_lock);

  if (cb->sock_fd >= 0) {
    close(cb->sock_fd);
    cb->sock_fd = -1;
  }

  sfree(cb->name);
  sfree(cb->host);
  sfree(cb->user);
  sfree(cb->service);
  sfree(cb->password);
  sfree(cb->key_file);
  sfree(cb->cert_file);
  pthread_mutex_unlock(&cb->send_lock);
  pthread_mutex_destroy(&cb->send_lock);

  sfree(cb);
}

static int ni_notify(const notification_t *n, /* {{{ */
                     user_data_t *user_data) {
  // var section
  size_t json_len;
  const unsigned char *json_buf; // full JSON
  char *url_buf;                 // full URL to Icinga
  char *prot_name;               // HTTP or HTTPS
  int ret_code;
  CURL *curl;
  CURLcode res;

  struct curl_slist *headers = NULL;

  /*
  unsigned char value_name[DATA_MAX_NAME_LEN];
  int val_len = sizeof(value_name);*/
  struct ni_callback *cb;

  if (user_data == NULL)
    return EINVAL;
  cb = user_data->data;

  // code starts here
  yajl_gen yajl_gen_handle = yajl_gen_alloc(NULL);
  if (yajl_gen_handle == NULL) {
    ERROR("notify_icinga: Could not allocate memory for yajl_gen "
          "yajl_gen_handle!");
    return 1;
  }
  yajl_gen_status yajl_stat = yajl_gen_status_ok;
  // top-level object is a map
  yajl_stat = yajl_gen_map_open(yajl_gen_handle);
  if (yajl_stat != yajl_gen_status_ok) {
    err(yajl_stat);
    return 1;
  }

  yajl_stat = yajl_gen_string(yajl_gen_handle, (unsigned char *)"exit_status",
                              strlen("exit_status"));
  if (yajl_stat != yajl_gen_status_ok) {
    err(yajl_stat);
    return 1;
  }

  ret_code = (n->severity == NOTIF_FAILURE)
                 ? 2
                 : ((n->severity == NOTIF_WARNING)
                        ? 1
                        : ((n->severity == NOTIF_OKAY) ? 0 : 3));
  yajl_stat = yajl_gen_integer(yajl_gen_handle, ret_code);
  if (yajl_stat != yajl_gen_status_ok) {
    err(yajl_stat);
    return 1;
  }

  yajl_stat = yajl_gen_string(yajl_gen_handle, (unsigned char *)"check_source",
                              strlen("check_source"));
  if (yajl_stat != yajl_gen_status_ok) {
    err(yajl_stat);
    return 1;
  }

  yajl_stat = yajl_gen_string(yajl_gen_handle, (unsigned char *)n->host,
                              strlen(n->host));
  if (yajl_stat != yajl_gen_status_ok) {
    err(yajl_stat);
    return 1;
  }

  yajl_stat = yajl_gen_string(yajl_gen_handle, (unsigned char *)"plugin_output",
                              strlen("plugin_output"));
  if (yajl_stat != yajl_gen_status_ok) {
    err(yajl_stat);
    return 1;
  }

  yajl_stat = yajl_gen_string(yajl_gen_handle, (unsigned char *)n->message,
                              strlen(n->message));
  if (yajl_stat != yajl_gen_status_ok) {
    err(yajl_stat);
    return 1;
  }

  if (cb->use_https) {
    prot_name = ssnprintf_alloc("https");
  } else {
    prot_name = ssnprintf_alloc("http");
  }

  url_buf =
      ssnprintf_alloc("%s://%s:%d" ICINGA_URI_PATH "?service=%s!%s", prot_name,
                      cb->node, cb->port, cb->host, cb->service);
  INFO("notify_icinga: URL=%s", url_buf);

  /*this is a place for future extension -
  see https://www.monitoring-plugins.org/doc/guidelines.html#AEN201

  yajl_stat = yajl_gen_string(yajl_gen_handle, (unsigned char
  *)"performance_data", strlen("performance_data"));
  if (yajl_stat !=  yajl_gen_status_ok) { err(yajl_stat); return 1;}

  yajl_stat = yajl_gen_array_open(yajl_gen_handle);
  if (yajl_stat !=  yajl_gen_status_ok) { err(yajl_stat); return 1;}

  snprintf(value_name, val_len, "value=%f;", n->);

  yajl_stat = yajl_gen_string(yajl_gen_handle, (unsigned_char *)n->plugin

  yajl_stat = yajl_gen_array_close(yajl_gen_handle);
  if (yajl_stat !=  yajl_gen_status_ok) { err(yajl_stat); return 1;}  */

  yajl_stat = yajl_gen_map_close(yajl_gen_handle);
  if (yajl_stat != yajl_gen_status_ok) {
    err(yajl_stat);
    return 1;
  }

  yajl_stat = yajl_gen_get_buf(yajl_gen_handle, &json_buf, &json_len);
  if (yajl_stat != yajl_gen_status_ok) {
    err(yajl_stat);
    return 1;
  }
  INFO("notify_icinga: %s", json_buf);
  if (cb->log_only) {
    return 0;
  }
  curl_global_init(CURL_GLOBAL_ALL);
  curl = curl_easy_init();
  if (curl) {
    headers = curl_slist_append(headers, "Accept: application/json");
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "charsets: utf-8");
    curl_easy_setopt(curl, CURLOPT_URL, url_buf);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, (char*) json_buf);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "collectd-notify-icinga/0.1");
    res = curl_easy_perform(curl);
    INFO("notify_icinga: curl: %d", res);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    curl_global_cleanup();
  }
  return 0;
} /* }}} int ni_notify */

static int ni_config(oconfig_item_t *ci) /* {{{ */
{
  struct stat fb;
  struct ni_callback *cb;
  char callback_name[DATA_MAX_NAME_LEN];
  int status = 0;

  cb = calloc(1, sizeof(*cb));
  if (NULL == cb) {
    ERROR("notify_icinga: calloc failed on cb");
    return -1;
  }
  cb->sock_fd = -1;
  cb->name = NULL;
  cb->service = strdup(ICINGA_SERVICE);
  cb->host = strdup(ICINGA_HOST);
  cb->node = strdup(ICINGA_HOST);
  cb->port = ICINGA_PORT;
  cb->user = NULL;
  cb->password = NULL;
  cb->cert_file = NULL;
  cb->key_file = NULL;
  cb->use_cert = 0;
  cb->log_only = 0;
  cb->use_https = 0;

#define FILETEST(z)                                                            \
  do {                                                                         \
    if (z) {                                                                   \
      if (stat(z, &fb) == -1) {                                                \
        ERROR("notify_icinga: Could not access file %s", z);                   \
        return 1;                                                              \
      }                                                                        \
      if ((fb.st_mode & S_IFMT) != S_IFREG) {                                  \
        ERROR("notify_icinga: Filename %s is not a regular file", z);          \
        return 1;                                                              \
      }                                                                        \
    }                                                                          \
  } while (0)

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (0 == strcasecmp("IcingaHost", child->key)) {
      cf_util_get_string(child, &cb->host);
      INFO("notify_icinga: IcingaHost: %s", cb->host);
    } else if (0 == strcasecmp("IcingaPort", child->key)) {
      cb->port = cf_util_get_port_number(child);
      INFO("notify_icinga: IcingaPort: %d", cb->port);
      if (cb->port == -1) {
        cf_util_get_string(child, &cb->host);
        ERROR("notify_icinga: Wrong port number: %s", cb->host);
        return 1;
      }
    } else if (0 == strcasecmp("IcingaCert", child->key)) {
      cf_util_get_string(child, &cb->cert_file);
      INFO("notify_icinga: IcingaCert: %s", cb->cert_file);
      FILETEST(cb->cert_file);
    } else if (0 == strcasecmp("IcingaKey", child->key)) {
      cf_util_get_string(child, &cb->key_file);
      INFO("notify_icinga: IcingaKey: %s", cb->key_file);
      FILETEST(cb->key_file);
    } else if (0 == strcasecmp("IcingaUser", child->key)) {
      cf_util_get_string(child, &cb->user);
      INFO("notify_icinga: IcingaUser: %s", cb->user);
    } else if (0 == strcasecmp("IcingaPassword", child->key)) {
      cf_util_get_string(child, &cb->password);
      INFO("notify_icinga: IcingaPassword: %s", cb->password);
    } else if (0 == strcasecmp("LogOnly", child->key)) {
      cf_util_get_boolean(child, &cb->log_only);
      INFO("notify_icinga: LogOnly: %d", cb->log_only);
    } else if (0 == strcasecmp("UseHttps", child->key)) {
      cf_util_get_boolean(child, &cb->use_https);
      INFO("notify_icinga: UseHttps: %d", cb->use_https);
    } else {
      WARNING("notify_icinga: Ignoring unknown config option \"%s\".",
              child->key);
      status = -1;
    }
    if (status != 0)
      break;
    if ((cb->key_file != NULL) && (cb->cert_file != NULL))
      cb->use_cert = 1;
  }
  if (status != 0) {
    ni_callback_free(cb);
    return status;
  }
#undef FILETEST

  if (cb->name == NULL)
    snprintf(callback_name, sizeof(callback_name), "notify_icinga/%s/%s",
             cb->node, cb->service);
  else
    snprintf(callback_name, sizeof(callback_name), "notify_icinga/%s",
             cb->name);

  plugin_register_notification(callback_name, ni_notify,
                               &(user_data_t){
                                   .data = cb, .free_func = ni_callback_free,
                               });

  return 0;
} /* }}} icinga_config */

static int ni_init(void) {
  // 0 = everything is OK
  // non-0 = unregister all the functions and unload the plugin
  return 0;
} /* }}} int ni_init */

static int ni_shutdown(void) {
  // 0 = everything is OK
  // non-0 = unregister all the functions and unload the plugin
  return 0;
} /* }}} int ni_shutdown */

void module_register(void) {
  // This will be called upon start
  plugin_register_complex_config("notify_icinga", ni_config);
  plugin_register_init("notify_icinga", ni_init);
  plugin_register_shutdown("notify_shutdown", ni_shutdown);
} /* void module_register (void) */

char *statusToString(yajl_gen_status code) {
  switch (code) {
  case yajl_gen_status_ok:
    return "no error";
    break;
  case yajl_gen_keys_must_be_strings:
    return "at a point where a map key is generated, a function other then "
           "yajl_gen_string was called";
    break;
  case yajl_max_depth_exceeded:
    return "YAJL's maximum generation depth was exeeded, see YAJL_MAX_DEPTH";
    break;
  case yajl_gen_in_error_state:
    return "a generator function was called while in an error state";
    break;
  case yajl_gen_generation_complete:
    return "a complete JSON document has already been generated (tried to add "
           "elements after the top level container was closed";
    break;
  case yajl_gen_invalid_number:
    return "an invalid number was passed in (infinity or NaN)";
    break;
  case yajl_gen_no_buf:
    return "a print callback was passed in, so no internal buffer to get from";
    break;
  case yajl_gen_invalid_string:
    return "an invalid string was passed in to yajl_gen_string() "
           "(yajl_gen_validate_utf8 option is enabled)";
    break;
  default:
    return "invalid code!";
    break;
  }
}

/**
 * prints an error message given a code
 * */
void err(yajl_gen_status code) {
  ERROR("notify_icinga: an error occured while generating the json: %s\n",
        statusToString(code));
}
