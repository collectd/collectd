/**
 * collectd - src/notify_nagios.c
 * Copyright (C) 2015       Florian octo Forster
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
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#define NAGIOS_OK 0
#define NAGIOS_WARNING 1
#define NAGIOS_CRITICAL 2
#define NAGIOS_UNKNOWN 3

#ifndef NAGIOS_COMMAND_FILE
#define NAGIOS_COMMAND_FILE "/usr/local/nagios/var/rw/nagios.cmd"
#endif

static char *nagios_command_file;

static int nagios_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("CommandFile", child->key) == 0)
      cf_util_get_string(child, &nagios_command_file);
    else
      WARNING("notify_nagios plugin: Ignoring unknown config option \"%s\".",
              child->key);
  }

  return 0;
} /* }}} nagios_config */

static int nagios_print(char const *buffer) /* {{{ */
{
  char const *file = NAGIOS_COMMAND_FILE;
  int fd;
  int status;
  struct flock lock = {0};

  if (nagios_command_file != NULL)
    file = nagios_command_file;

  fd = open(file, O_WRONLY | O_APPEND);
  if (fd < 0) {
    status = errno;
    ERROR("notify_nagios plugin: Opening \"%s\" failed: %s", file, STRERRNO);
    return status;
  }

  lock.l_type = F_WRLCK;
  lock.l_whence = SEEK_END;

  status = fcntl(fd, F_GETLK, &lock);
  if (status != 0) {
    status = errno;
    ERROR("notify_nagios plugin: Failed to acquire write lock on \"%s\": %s",
          file, STRERRNO);
    close(fd);
    return status;
  }

  status = (int)lseek(fd, 0, SEEK_END);
  if (status == -1) {
    status = errno;
    ERROR("notify_nagios plugin: Seeking to end of \"%s\" failed: %s", file,
          STRERRNO);
    close(fd);
    return status;
  }

  status = (int)swrite(fd, buffer, strlen(buffer));
  if (status != 0) {
    status = errno;
    ERROR("notify_nagios plugin: Writing to \"%s\" failed: %s", file, STRERRNO);
    close(fd);
    return status;
  }

  close(fd);
  return status;
} /* }}} int nagios_print */

static int nagios_notify(const notification_t *n, /* {{{ */
                         __attribute__((unused)) user_data_t *user_data) {
  char svc_description[4 * DATA_MAX_NAME_LEN];
  char buffer[4096];
  int code;
  int status;

  status = format_name(svc_description, (int)sizeof(svc_description),
                       /* host */ "", n->plugin, n->plugin_instance, n->type,
                       n->type_instance);
  if (status != 0) {
    ERROR("notify_nagios plugin: Formatting service name failed.");
    return status;
  }

  switch (n->severity) {
  case NOTIF_OKAY:
    code = NAGIOS_OK;
    break;
  case NOTIF_WARNING:
    code = NAGIOS_WARNING;
    break;
  case NOTIF_FAILURE:
    code = NAGIOS_CRITICAL;
    break;
  default:
    code = NAGIOS_UNKNOWN;
    break;
  }

  snprintf(buffer, sizeof(buffer),
           "[%.0f] PROCESS_SERVICE_CHECK_RESULT;%s;%s;%d;%s\n",
           CDTIME_T_TO_DOUBLE(n->time), n->host, &svc_description[1], code,
           n->message);

  return nagios_print(buffer);
} /* }}} int nagios_notify */

void module_register(void) {
  plugin_register_complex_config("notify_nagios", nagios_config);
  plugin_register_notification("notify_nagios", nagios_notify, NULL);
} /* void module_register (void) */
