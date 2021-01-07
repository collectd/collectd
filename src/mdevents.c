/**
 * collectd - src/mdevents.c
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Krzysztof Kazimierczak <krzysztof.kazimierczak@intel.com>
 *   Maciej Fijalkowski <maciej.fijalkowski@intel.com>
 *   Michal Kobylinski <michal.kobylinski@intel.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#include <limits.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>

#define MD_EVENTS_PLUGIN "mdevents"
#define DAEMON_NAME "mdadm"

#define MD_EVENTS_ERROR(err_msg, ...)                                          \
  ERROR(MD_EVENTS_PLUGIN ": %s: " err_msg, __FUNCTION__, ##__VA_ARGS__)

// Syslog can be located under different paths on various linux distros;
// The following two cover the debian-based and redhat distros
#define SYSLOG_PATH "/var/log/syslog"
#define SYSLOG_MSG_PATH "/var/log/messages"

#define MAX_SYSLOG_MESSAGE_LENGTH 1024
#define MAX_ERROR_MSG 100
#define MAX_MATCHES 4
#define MD_ARRAY_NAME_PREFIX_LEN 7

static FILE *syslog_file;
static regex_t regex;
static ignorelist_t *event_ignorelist;
static ignorelist_t *array_ignorelist;

static char regex_pattern[] =
    "mdadm[\\[0-9]+\\]: ([a-zA-Z]+) event detected on md"
    " device ([a-z0-9\\/\\.\\-]+)[^\\/\n]*([a-z0-9\\/\\.\\-]+)?";

static const char *config_keys[] = {"Array", "Event", "IgnoreArray",
                                    "IgnoreEvent"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

enum md_events_regex_entries {
  // matches of substrings in whole RE start from index 1
  EVENT = 1,
  MD_DEVICE = 2,
  COMPONENT_DEVICE = 3,
};

typedef struct md_events_event_s {
  char event_name[DATA_MAX_NAME_LEN];
  char md_device[DATA_MAX_NAME_LEN];
  char component_device[DATA_MAX_NAME_LEN];
} md_events_event_t;

static const char *md_events_critical_events[] = {"DeviceDisappeared",
                                                  "DegradedArray", "Fail"};
static const char *md_events_warning_events[] = {
    "SparesMissing", "FailSpare", "MoveSpare", "RebuildFinished"};
static const char *md_events_informative_events[] = {
    "RebuildStarted", "RebuildNN", "SpareActive", "NewArray", "TestMessage"};

static int md_events_classify_event(const char *event_name) {
  int i;

  for (i = 0; i < STATIC_ARRAY_SIZE(md_events_critical_events); i++) {
    if (!strcmp(event_name, md_events_critical_events[i]))
      return NOTIF_FAILURE;
  }
  for (i = 0; i < STATIC_ARRAY_SIZE(md_events_warning_events); i++) {
    if (!strcmp(event_name, md_events_warning_events[i]))
      return NOTIF_WARNING;
  }
  for (i = 0; i < STATIC_ARRAY_SIZE(md_events_informative_events); i++) {
    if (!strcmp(event_name, md_events_informative_events[i]))
      return NOTIF_OKAY;
  }
  // we do not support that event
  return 0;
}

int md_events_parse_events(const char *events, size_t len) {
  char *event_buf;
  char *event;
  char *save_ptr;

  // have an additional byte for nul terminator
  len++;

  if ((event_buf = calloc(1, len)) == NULL) {
    MD_EVENTS_ERROR("calloc failed for event_buf\n");
    return -1;
  }

  // need a non-const copy so that strtok can work on this
  strncpy(event_buf, events, len);
  event_buf[len - 1] = '\0';
  event = strtok_r(event_buf, " ", &save_ptr);
  if (event == NULL) {
    MD_EVENTS_ERROR("Couldn't parse events specified by user\n");
    free(event_buf);
    return -1;
  }
  // verify that user-defined event from config is the one that
  // we/mdadm support
  if (md_events_classify_event(event)) {
    ignorelist_add(event_ignorelist, event);
  } else {
    MD_EVENTS_ERROR("Unclassified event \"%s\"; Ignoring.\n", event);
    free(event_buf);
    return -1;
  }

  while ((event = strtok_r(NULL, " ", &save_ptr)) != NULL) {
    if (md_events_classify_event(event)) {
      ignorelist_add(event_ignorelist, event);
    } else {
      MD_EVENTS_ERROR("Unclassified event \"%s\"; Ignoring.\n", event);
      free(event_buf);
      return -1;
    }
  }
  free(event_buf);
  return 0;
}

static int md_events_parse_boolean(const char *bool_setting,
                                   ignorelist_t *list) {
  if (IS_TRUE(bool_setting)) {
    ignorelist_set_invert(list, 0);
    return 0;
  } else if (IS_FALSE(bool_setting)) {
    ignorelist_set_invert(list, 1);
    return 0;
  }
  return 1;
}

static int md_events_config(const char *key, const char *value) {
  size_t len = strlen(value);

  if (array_ignorelist == NULL) {
    array_ignorelist = ignorelist_create(/* invert = */ 1);
    if (array_ignorelist == NULL)
      return -1;
  }
  if (event_ignorelist == NULL) {
    event_ignorelist = ignorelist_create(/* invert = */ 1);
    if (event_ignorelist == NULL)
      return -1;
  }

  if (!strcasecmp("Event", key) && len) {
    if (md_events_parse_events(value, len)) {
      MD_EVENTS_ERROR(
          "Failed while parsing events, please check your config file");
      return -1;
    }
  }
  if (!strcasecmp("Array", key) && len) {
    if (strncmp("/dev/md", value, MD_ARRAY_NAME_PREFIX_LEN)) {
      MD_EVENTS_ERROR("The array name/regex must start with '/dev/md';"
                      " Ignoring %s\n",
                      value);
      return -1;
    } else {
      ignorelist_add(array_ignorelist, value);
    }
  }
  if (!strcasecmp("IgnoreArray", key)) {
    if (md_events_parse_boolean(value, array_ignorelist)) {
      MD_EVENTS_ERROR("Error while checking 'IgnoreArray' value, "
                      "is it boolean? Check the config file.");
      return -1;
    }
  }
  if (!strcasecmp("IgnoreEvent", key)) {
    if (md_events_parse_boolean(value, event_ignorelist)) {
      MD_EVENTS_ERROR("Error while checking 'IgnoreEvent' value, "
                      "is it boolean? Check the config file.");
      return -1;
    }
  }

  return 0;
}

static void md_events_handle_regex_error(int rc, regex_t *regex,
                                         const char *func_name) {
  char buf[MAX_ERROR_MSG] = {};

  regerror(rc, regex, buf, MAX_ERROR_MSG);
  DEBUG("%s() failed with '%s'\n", func_name, buf);
}

static int md_events_compile_regex(regex_t *regex, const char *regex_pattern) {
  int status = regcomp(regex, regex_pattern, REG_EXTENDED | REG_NEWLINE);

  if (status) {
    md_events_handle_regex_error(status, regex, "regcomp");
    return -1;
  }
  return 0;
}

static int md_events_dispatch_notification(md_events_event_t *event,
                                           notification_t *notif) {
  int offset;
  size_t len;

  if (!notif || !event) {
    MD_EVENTS_ERROR("Null pointer\n");
    return -1;
  }

  len = strlen(hostname_g);
  // we need to make sure that we don't overflow the notif->host buffer
  // keep in mind that strlen(hostname_g) does not include the nul terminator
  if (len > sizeof(notif->host) - 1)
    len = sizeof(notif->host) - 1;
  memcpy(notif->host, hostname_g, len);
  notif->host[len] = '\0';

  // with this string literal we are safe to copy
  strncpy(notif->type, "gauge", sizeof(notif->type));
  offset =
      snprintf(notif->message, NOTIF_MAX_MSG_LEN, "event name %s, md array %s ",
               event->event_name, event->md_device);
  // this one is not present in every event;
  if (event->component_device[0] != '\0') {
    snprintf(notif->message + offset, NOTIF_MAX_MSG_LEN - offset,
             "component device %s\n", event->component_device);
  }
  plugin_dispatch_notification(notif);

  return 0;
}

// helper function to check whether regex match will fit onto buffer
// check whether the difference of indexes is bigger than max allowed length
static inline size_t md_events_get_max_len(regmatch_t match,
                                           size_t max_name_len) {
  size_t len;

  if (match.rm_eo - match.rm_so > max_name_len - 1)
    len = max_name_len - 1;
  else
    len = match.rm_eo - match.rm_so;
  return len;
}

static void md_events_copy_match(char *buf, const char *line,
                                 regmatch_t match) {
  size_t bytes_to_copy = md_events_get_max_len(match, DATA_MAX_NAME_LEN);

  memcpy(buf, &line[match.rm_so], bytes_to_copy);
  buf[bytes_to_copy] = '\0';
}

static int md_events_match_regex(regex_t *regex, const char *to_match) {
  regmatch_t matches[MAX_MATCHES] = {};
  int status, severity;
  md_events_event_t event = {};

  status = regexec(regex, to_match, MAX_MATCHES, matches, 0);
  if (status) {
    md_events_handle_regex_error(status, regex, "regexec");
    return -1;
  }

  // each element from matches array contains the indexes (start/end) within
  // the string that we ran regexp on; use them to retrieve the substrings
  md_events_copy_match(event.event_name, to_match, matches[EVENT]);
  md_events_copy_match(event.md_device, to_match, matches[MD_DEVICE]);

  // this one is not present in every event, regex API sets indexes to -1
  // if the match wasn't found
  if (matches[COMPONENT_DEVICE].rm_so != -1)
    md_events_copy_match(event.component_device, to_match,
                         matches[COMPONENT_DEVICE]);

  if (ignorelist_match(event_ignorelist, event.event_name))
    return -1;

  if (ignorelist_match(array_ignorelist, event.md_device))
    return -1;

  severity = md_events_classify_event(event.event_name);
  if (!severity) {
    MD_EVENTS_ERROR("Unsupported event %s\n", event.event_name);
    return -1;
  }

  md_events_dispatch_notification(&event,
                                  &(notification_t){.severity = severity,
                                                    .time = cdtime(),
                                                    .plugin = MD_EVENTS_PLUGIN,
                                                    .type_instance = ""});
  return 0;
}

static int md_events_read(void) {
  char syslog_line[MAX_SYSLOG_MESSAGE_LENGTH];
  while (fgets(syslog_line, sizeof(syslog_line), syslog_file))
    // don't check the return code here; exiting from read callback with
    // nonzero status causes the suspension of next read call;
    md_events_match_regex(&regex, syslog_line);

  return 0;
}

static int md_events_shutdown(void) {
  if (syslog_file)
    fclose(syslog_file);

  regfree(&regex);
  ignorelist_free(event_ignorelist);
  ignorelist_free(array_ignorelist);

  plugin_unregister_config(MD_EVENTS_PLUGIN);
  plugin_unregister_read(MD_EVENTS_PLUGIN);
  plugin_unregister_shutdown(MD_EVENTS_PLUGIN);

  return 0;
}

static int md_events_init(void) {
  syslog_file = fopen(SYSLOG_PATH, "r");
  if (!syslog_file) {
    syslog_file = fopen(SYSLOG_MSG_PATH, "r");
    if (!syslog_file) {
      MD_EVENTS_ERROR(
          "/var/log/syslog and /var/log/messages files are not present. Are "
          "you sure that you have rsyslog utility installed on your system?\n");
      return -1;
    }
  }

  // monitor events only from point of collectd start
  if (fseek(syslog_file, 0, SEEK_END)) {
    MD_EVENTS_ERROR("fseek on syslog file failed, error: %s\n",
                    strerror(errno));
    fclose(syslog_file);
    return -1;
  }

  if (md_events_compile_regex(&regex, regex_pattern)) {
    fclose(syslog_file);
    return -1;
  }

  return 0;
}

void module_register(void) {
  plugin_register_init(MD_EVENTS_PLUGIN, md_events_init);
  plugin_register_config(MD_EVENTS_PLUGIN, md_events_config, config_keys,
                         config_keys_num);
  plugin_register_read(MD_EVENTS_PLUGIN, md_events_read);
  plugin_register_shutdown(MD_EVENTS_PLUGIN, md_events_shutdown);
}
