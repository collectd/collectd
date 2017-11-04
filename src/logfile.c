/**
 * collectd - src/logfile.c
 * Copyright (C) 2007       Sebastian Harl
 * Copyright (C) 2007,2008  Florian Forster
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
 *   Sebastian Harl <sh at tokkee.org>
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#if COLLECT_DEBUG
static int log_level = LOG_DEBUG;
#else
static int log_level = LOG_INFO;
#endif /* COLLECT_DEBUG */

static pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

static char *log_file = NULL;
static int print_timestamp = 1;
static int print_severity = 0;

static const char *config_keys[] = {"LogLevel", "File", "Timestamp",
                                    "PrintSeverity"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int logfile_config(const char *key, const char *value) {
  if (0 == strcasecmp(key, "LogLevel")) {
    log_level = parse_log_severity(value);
    if (log_level < 0) {
      log_level = LOG_INFO;
      ERROR("logfile: invalid loglevel [%s] defaulting to 'info'", value);
      return 1;
    }
  } else if (0 == strcasecmp(key, "File")) {
    sfree(log_file);
    log_file = strdup(value);
  } else if (0 == strcasecmp(key, "Timestamp")) {
    if (IS_FALSE(value))
      print_timestamp = 0;
    else
      print_timestamp = 1;
  } else if (0 == strcasecmp(key, "PrintSeverity")) {
    if (IS_FALSE(value))
      print_severity = 0;
    else
      print_severity = 1;
  } else {
    return -1;
  }
  return 0;
} /* int logfile_config (const char *, const char *) */

static void logfile_print(const char *msg, int severity,
                          cdtime_t timestamp_time) {
  FILE *fh;
  _Bool do_close = 0;
  char timestamp_str[64];
  char level_str[16] = "";

  if (print_severity) {
    switch (severity) {
    case LOG_ERR:
      snprintf(level_str, sizeof(level_str), "[error] ");
      break;
    case LOG_WARNING:
      snprintf(level_str, sizeof(level_str), "[warning] ");
      break;
    case LOG_NOTICE:
      snprintf(level_str, sizeof(level_str), "[notice] ");
      break;
    case LOG_INFO:
      snprintf(level_str, sizeof(level_str), "[info] ");
      break;
    case LOG_DEBUG:
      snprintf(level_str, sizeof(level_str), "[debug] ");
      break;
    default:
      break;
    }
  }

  if (print_timestamp) {
    struct tm timestamp_tm;
    localtime_r(&CDTIME_T_TO_TIME_T(timestamp_time), &timestamp_tm);

    strftime(timestamp_str, sizeof(timestamp_str), "%Y-%m-%d %H:%M:%S",
             &timestamp_tm);
    timestamp_str[sizeof(timestamp_str) - 1] = '\0';
  }

  pthread_mutex_lock(&file_lock);

  if (log_file == NULL) {
    fh = stderr;
  } else if (strcasecmp(log_file, "stderr") == 0)
    fh = stderr;
  else if (strcasecmp(log_file, "stdout") == 0)
    fh = stdout;
  else {
    fh = fopen(log_file, "a");
    do_close = 1;
  }

  if (fh == NULL) {
    fprintf(stderr, "logfile plugin: fopen (%s) failed: %s\n", log_file,
            STRERRNO);
  } else {
    if (print_timestamp)
      fprintf(fh, "[%s] %s%s\n", timestamp_str, level_str, msg);
    else
      fprintf(fh, "%s%s\n", level_str, msg);

    if (do_close) {
      fclose(fh);
    } else {
      fflush(fh);
    }
  }

  pthread_mutex_unlock(&file_lock);

  return;
} /* void logfile_print */

static void logfile_log(int severity, const char *msg,
                        user_data_t __attribute__((unused)) * user_data) {
  if (severity > log_level)
    return;

  logfile_print(msg, severity, cdtime());
} /* void logfile_log (int, const char *) */

static int logfile_notification(const notification_t *n,
                                user_data_t __attribute__((unused)) *
                                    user_data) {
  char buf[1024] = "";
  char *buf_ptr = buf;
  int buf_len = sizeof(buf);
  int status;

  status = snprintf(
      buf_ptr, buf_len, "Notification: severity = %s",
      (n->severity == NOTIF_FAILURE)
          ? "FAILURE"
          : ((n->severity == NOTIF_WARNING)
                 ? "WARNING"
                 : ((n->severity == NOTIF_OKAY) ? "OKAY" : "UNKNOWN")));
  if (status > 0) {
    buf_ptr += status;
    buf_len -= status;
  }

#define APPEND(bufptr, buflen, key, value)                                     \
  if ((buflen > 0) && (strlen(value) > 0)) {                                   \
    status = snprintf(bufptr, buflen, ", %s = %s", key, value);                \
    if (status > 0) {                                                          \
      bufptr += status;                                                        \
      buflen -= status;                                                        \
    }                                                                          \
  }
  APPEND(buf_ptr, buf_len, "host", n->host);
  APPEND(buf_ptr, buf_len, "plugin", n->plugin);
  APPEND(buf_ptr, buf_len, "plugin_instance", n->plugin_instance);
  APPEND(buf_ptr, buf_len, "type", n->type);
  APPEND(buf_ptr, buf_len, "type_instance", n->type_instance);
  APPEND(buf_ptr, buf_len, "message", n->message);

  buf[sizeof(buf) - 1] = '\0';

  logfile_print(buf, LOG_INFO, (n->time != 0) ? n->time : cdtime());

  return 0;
} /* int logfile_notification */

void module_register(void) {
  plugin_register_config("logfile", logfile_config, config_keys,
                         config_keys_num);
  plugin_register_log("logfile", logfile_log, /* user_data = */ NULL);
  plugin_register_notification("logfile", logfile_notification,
                               /* user_data = */ NULL);
} /* void module_register (void) */
