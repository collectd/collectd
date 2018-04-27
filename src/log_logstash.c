/**
 * collectd - src/log_logstash.c
 * Copyright (C) 2013       Pierre-Yves Ritschard
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
 *   Pierre-Yves Ritschard <pyr at spootnik.org>
 * Acknowledgements:
 *   This file is largely inspired by logfile.c
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <sys/types.h>
#include <yajl/yajl_common.h>
#include <yajl/yajl_gen.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif
#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
#define HAVE_YAJL_V2 1
#endif

#if COLLECT_DEBUG
static int log_level = LOG_DEBUG;
#else
static int log_level = LOG_INFO;
#endif /* COLLECT_DEBUG */

static pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

static char *log_file = NULL;

static const char *config_keys[] = {"LogLevel", "File"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int log_logstash_config(const char *key, const char *value) {

  if (0 == strcasecmp(key, "LogLevel")) {
    log_level = parse_log_severity(value);
    if (log_level < 0) {
      log_level = LOG_INFO;
      ERROR("log_logstash: invalid loglevel [%s] defaulting to 'info'", value);
      return 1;
    }
  } else if (0 == strcasecmp(key, "File")) {
    sfree(log_file);
    log_file = strdup(value);
  } else {
    return -1;
  }
  return 0;
} /* int log_logstash_config (const char *, const char *) */

static void log_logstash_print(yajl_gen g, int severity,
                               cdtime_t timestamp_time) {
  FILE *fh;
  _Bool do_close = 0;
  struct tm timestamp_tm;
  char timestamp_str[64];
  const unsigned char *buf;
#if HAVE_YAJL_V2
  size_t len;
#else
  unsigned int len;
#endif

  if (yajl_gen_string(g, (u_char *)"level", strlen("level")) !=
      yajl_gen_status_ok)
    goto err;

  switch (severity) {
  case LOG_ERR:
    if (yajl_gen_string(g, (u_char *)"error", strlen("error")) !=
        yajl_gen_status_ok)
      goto err;
    break;
  case LOG_WARNING:
    if (yajl_gen_string(g, (u_char *)"warning", strlen("warning")) !=
        yajl_gen_status_ok)
      goto err;
    break;
  case LOG_NOTICE:
    if (yajl_gen_string(g, (u_char *)"notice", strlen("notice")) !=
        yajl_gen_status_ok)
      goto err;
    break;
  case LOG_INFO:
    if (yajl_gen_string(g, (u_char *)"info", strlen("info")) !=
        yajl_gen_status_ok)
      goto err;
    break;
  case LOG_DEBUG:
    if (yajl_gen_string(g, (u_char *)"debug", strlen("debug")) !=
        yajl_gen_status_ok)
      goto err;
    break;
  default:
    if (yajl_gen_string(g, (u_char *)"unknown", strlen("unknown")) !=
        yajl_gen_status_ok)
      goto err;
    break;
  }

  if (yajl_gen_string(g, (u_char *)"@timestamp", strlen("@timestamp")) !=
      yajl_gen_status_ok)
    goto err;

  gmtime_r(&CDTIME_T_TO_TIME_T(timestamp_time), &timestamp_tm);

  /*
   * format time as a UTC ISO 8601 compliant string
   */
  strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%dT%H:%M:%SZ",
           &timestamp_tm);
  timestamp_str[sizeof(timestamp_str) - 1] = '\0';

  if (yajl_gen_string(g, (u_char *)timestamp_str, strlen(timestamp_str)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_map_close(g) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_get_buf(g, &buf, &len) != yajl_gen_status_ok)
    goto err;
  pthread_mutex_lock(&file_lock);

  if (log_file == NULL) {
    fh = stderr;
  } else if (strcasecmp(log_file, "stdout") == 0) {
    fh = stdout;
    do_close = 0;
  } else if (strcasecmp(log_file, "stderr") == 0) {
    fh = stderr;
    do_close = 0;
  } else {
    fh = fopen(log_file, "a");
    do_close = 1;
  }

  if (fh == NULL) {
    fprintf(stderr, "log_logstash plugin: fopen (%s) failed: %s\n", log_file,
            STRERRNO);
  } else {
    fprintf(fh, "%s\n", buf);
    if (do_close) {
      fclose(fh);
    } else {
      fflush(fh);
    }
  }
  pthread_mutex_unlock(&file_lock);
  yajl_gen_free(g);
  return;

err:
  yajl_gen_free(g);
  fprintf(stderr, "Could not correctly generate JSON message\n");
  return;
} /* void log_logstash_print */

static void log_logstash_log(int severity, const char *msg,
                             user_data_t __attribute__((unused)) * user_data) {
  yajl_gen g;
#if !defined(HAVE_YAJL_V2)
  yajl_gen_config conf = {};

  conf.beautify = 0;
#endif

  if (severity > log_level)
    return;

#if HAVE_YAJL_V2
  g = yajl_gen_alloc(NULL);
#else
  g = yajl_gen_alloc(&conf, NULL);
#endif

  if (g == NULL) {
    fprintf(stderr, "Could not allocate JSON generator.\n");
    return;
  }

  if (yajl_gen_map_open(g) != yajl_gen_status_ok)
    goto err;
  if (yajl_gen_string(g, (u_char *)"message", strlen("message")) !=
      yajl_gen_status_ok)
    goto err;
  if (yajl_gen_string(g, (u_char *)msg, strlen(msg)) != yajl_gen_status_ok)
    goto err;

  log_logstash_print(g, severity, cdtime());
  return;
err:
  yajl_gen_free(g);
  fprintf(stderr, "Could not generate JSON message preamble\n");
  return;

} /* void log_logstash_log (int, const char *) */

static int log_logstash_notification(const notification_t *n,
                                     user_data_t __attribute__((unused)) *
                                         user_data) {
  yajl_gen g;
#if HAVE_YAJL_V2
  g = yajl_gen_alloc(NULL);
#else
  yajl_gen_config conf = {};

  conf.beautify = 0;
  g = yajl_gen_alloc(&conf, NULL);
#endif

  if (g == NULL) {
    fprintf(stderr, "Could not allocate JSON generator.\n");
    return 0;
  }

  if (yajl_gen_map_open(g) != yajl_gen_status_ok)
    goto err;
  if (yajl_gen_string(g, (u_char *)"message", strlen("message")) !=
      yajl_gen_status_ok)
    goto err;
  if (strlen(n->message) > 0) {
    if (yajl_gen_string(g, (u_char *)n->message, strlen(n->message)) !=
        yajl_gen_status_ok)
      goto err;
  } else {
    if (yajl_gen_string(g, (u_char *)"notification without a message",
                        strlen("notification without a message")) !=
        yajl_gen_status_ok)
      goto err;
  }

  if (strlen(n->host) > 0) {
    if (yajl_gen_string(g, (u_char *)"host", strlen("host")) !=
        yajl_gen_status_ok)
      goto err;
    if (yajl_gen_string(g, (u_char *)n->host, strlen(n->host)) !=
        yajl_gen_status_ok)
      goto err;
  }
  if (strlen(n->plugin) > 0) {
    if (yajl_gen_string(g, (u_char *)"plugin", strlen("plugin")) !=
        yajl_gen_status_ok)
      goto err;
    if (yajl_gen_string(g, (u_char *)n->plugin, strlen(n->plugin)) !=
        yajl_gen_status_ok)
      goto err;
  }
  if (strlen(n->plugin_instance) > 0) {
    if (yajl_gen_string(g, (u_char *)"plugin_instance",
                        strlen("plugin_instance")) != yajl_gen_status_ok)
      goto err;
    if (yajl_gen_string(g, (u_char *)n->plugin_instance,
                        strlen(n->plugin_instance)) != yajl_gen_status_ok)
      goto err;
  }
  if (strlen(n->type) > 0) {
    if (yajl_gen_string(g, (u_char *)"type", strlen("type")) !=
        yajl_gen_status_ok)
      goto err;
    if (yajl_gen_string(g, (u_char *)n->type, strlen(n->type)) !=
        yajl_gen_status_ok)
      goto err;
  }
  if (strlen(n->type_instance) > 0) {
    if (yajl_gen_string(g, (u_char *)"type_instance",
                        strlen("type_instance")) != yajl_gen_status_ok)
      goto err;
    if (yajl_gen_string(g, (u_char *)n->type_instance,
                        strlen(n->type_instance)) != yajl_gen_status_ok)
      goto err;
  }

  if (yajl_gen_string(g, (u_char *)"severity", strlen("severity")) !=
      yajl_gen_status_ok)
    goto err;

  switch (n->severity) {
  case NOTIF_FAILURE:
    if (yajl_gen_string(g, (u_char *)"failure", strlen("failure")) !=
        yajl_gen_status_ok)
      goto err;
    break;
  case NOTIF_WARNING:
    if (yajl_gen_string(g, (u_char *)"warning", strlen("warning")) !=
        yajl_gen_status_ok)
      goto err;
    break;
  case NOTIF_OKAY:
    if (yajl_gen_string(g, (u_char *)"ok", strlen("ok")) != yajl_gen_status_ok)
      goto err;
    break;
  default:
    if (yajl_gen_string(g, (u_char *)"unknown", strlen("unknown")) !=
        yajl_gen_status_ok)
      goto err;
    break;
  }

  log_logstash_print(g, LOG_INFO, (n->time != 0) ? n->time : cdtime());
  return 0;

err:
  yajl_gen_free(g);
  fprintf(stderr, "Could not correctly generate JSON notification\n");
  return 0;
} /* int log_logstash_notification */

void module_register(void) {
  plugin_register_config("log_logstash", log_logstash_config, config_keys,
                         config_keys_num);
  plugin_register_log("log_logstash", log_logstash_log,
                      /* user_data = */ NULL);
  plugin_register_notification("log_logstash", log_logstash_notification,
                               /* user_data = */ NULL);
} /* void module_register (void) */
