/**
 * collectd - src/sysevent.c
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
 *   Red Hat NFVPE
 *     Andrew Bays <abays at redhat.com>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_complain.h"

#include <asm/types.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <yajl/yajl_tree.h>

#define SYSEVENT_REGEX_MATCHES 1

/*
 * Private data types
 */

typedef struct {
  int head;
  int tail;
  int maxLen;
  char **buffer;
} circbuf_t;

struct regexfilterlist_s {
  char *regex_filter;
  regex_t regex_filter_obj;

  struct regexfilterlist_s *next;
};
typedef struct regexfilterlist_s regexfilterlist_t;

/*
 * Private variables
 */

static int sysevent_thread_loop = 0;
static int sysevent_thread_error = 0;
static pthread_t sysevent_thread_id;
static pthread_mutex_t sysevent_lock = PTHREAD_MUTEX_INITIALIZER;
static int sock = -1;
static circbuf_t ring;

static char *listen_ip;
static char *listen_port;
static int listen_buffer_size = 1024;
static int buffer_length = 10;

static regexfilterlist_t *regexfilterlist_head = NULL;

static const char *rsyslog_keys[3] = {"@timestamp", "@source_host", "@message"};
static const char *rsyslog_field_keys[4] = {"facility", "severity", "program",
                                            "processid"};

/*
 * Private functions
 */

static void *sysevent_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&sysevent_lock);

  while (sysevent_thread_loop > 0) {
    int status = 0;

    pthread_mutex_unlock(&sysevent_lock);

    if (sock == -1)
      return ((void *)0);

    char buffer[listen_buffer_size];
    struct sockaddr_storage src_addr;
    socklen_t src_addr_len = sizeof(src_addr);

    memset(buffer, '\0', listen_buffer_size);

    ssize_t count = recvfrom(sock, buffer, sizeof(buffer), 0,
                             (struct sockaddr *)&src_addr, &src_addr_len);

    if (count == -1) {
      ERROR("sysevent plugin: failed to receive data: %s", strerror(errno));
      status = -1;
    } else if (count >= sizeof(buffer)) {
      WARNING("sysevent plugin: datagram too large for buffer: truncated");
    } else {
      // 1. Acquire lock
      // 2. Push to buffer if there is room, otherwise raise warning

      pthread_mutex_lock(&sysevent_lock);

      int next = ring.head + 1;
      if (next >= ring.maxLen)
        next = 0;

      if (next == ring.tail) {
        WARNING("sysevent plugin: ring buffer full");
      } else {
        DEBUG("sysevent plugin: writing %s", buffer);

        strncpy(ring.buffer[ring.head], buffer, sizeof(buffer));
        ring.head = next;
      }

      pthread_mutex_unlock(&sysevent_lock);
    }

    usleep(1000);

    pthread_mutex_lock(&sysevent_lock);

    if (status < 0) {
      WARNING("sysevent plugin: problem with thread status: %d", status);
      sysevent_thread_error = 1;
      break;
    }

    if (sysevent_thread_loop <= 0)
      break;
  } /* while (sysevent_thread_loop > 0) */

  pthread_mutex_unlock(&sysevent_lock);

  // pthread_exit instead of return
  return ((void *)0);
} /* }}} void *sysevent_thread */

static int start_thread(void) /* {{{ */
{
  int status;

  pthread_mutex_lock(&sysevent_lock);

  if (sysevent_thread_loop != 0) {
    pthread_mutex_unlock(&sysevent_lock);
    return (0);
  }

  sysevent_thread_loop = 1;
  sysevent_thread_error = 0;

  DEBUG("sysevent plugin: starting thread");

  status = plugin_thread_create(&sysevent_thread_id, /* attr = */ NULL,
                                sysevent_thread,
                                /* arg = */ (void *)0, "sysevent");
  if (status != 0) {
    sysevent_thread_loop = 0;
    ERROR("sysevent plugin: starting thread failed.");
    pthread_mutex_unlock(&sysevent_lock);
    return (-1);
  }

  pthread_mutex_unlock(&sysevent_lock);
  return (0);
} /* }}} int start_thread */

static int stop_thread(int shutdown) /* {{{ */
{
  int status;

  pthread_mutex_lock(&sysevent_lock);

  if (sysevent_thread_loop == 0) {
    pthread_mutex_unlock(&sysevent_lock);
    return (-1);
  }

  sysevent_thread_loop = 0;
  pthread_mutex_unlock(&sysevent_lock);

  if (shutdown == 1) {
    // Since the thread is blocking, calling pthread_join
    // doesn't actually succeed in stopping it.  It will stick around
    // until a message is received on the socket (at which
    // it will realize that "sysevent_thread_loop" is 0 and will
    // break out of the read loop and be allowed to die).  This is
    // fine when the process isn't supposed to be exiting, but in
    // the case of a process shutdown, we don't want to have an
    // idle thread hanging around.  Calling pthread_cancel here in
    // the case of a shutdown is just assures that the thread is
    // gone and that the process has been fully terminated.

    DEBUG("sysevent plugin: Canceling thread for process shutdown");

    status = pthread_cancel(sysevent_thread_id);

    if (status != 0) {
      ERROR("sysevent plugin: Unable to cancel thread: %d (%s)", status,
            strerror(errno));
      status = -1;
    }
  } else {
    status = pthread_join(sysevent_thread_id, /* return = */ NULL);
    if (status != 0) {
      ERROR("sysevent plugin: Stopping thread failed.");
      status = -1;
    }
  }

  pthread_mutex_lock(&sysevent_lock);
  memset(&sysevent_thread_id, 0, sizeof(sysevent_thread_id));
  sysevent_thread_error = 0;
  pthread_mutex_unlock(&sysevent_lock);

  DEBUG("sysevent plugin: Finished requesting stop of thread");

  return (status);
} /* }}} int stop_thread */

static int sysevent_init(void) /* {{{ */
{
  ring.head = 0;
  ring.tail = 0;
  ring.maxLen = buffer_length;
  ring.buffer = (char **)malloc(buffer_length * sizeof(char *));

  for (int i = 0; i < buffer_length; i++) {
    ring.buffer[i] = malloc(listen_buffer_size);
  }

  if (sock == -1) {
    const char *hostname = listen_ip;
    const char *portname = listen_port;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    hints.ai_flags = AI_PASSIVE | AI_ADDRCONFIG;
    struct addrinfo *res = 0;

    int err = getaddrinfo(hostname, portname, &hints, &res);

    if (err != 0) {
      ERROR("sysevent plugin: failed to resolve local socket address (err=%d)",
            err);
      freeaddrinfo(res);
      return (-1);
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == -1) {
      ERROR("sysevent plugin: failed to open socket: %s", strerror(errno));
      freeaddrinfo(res);
      return (-1);
    }

    if (bind(sock, res->ai_addr, res->ai_addrlen) == -1) {
      ERROR("sysevent plugin: failed to bind socket: %s", strerror(errno));
      freeaddrinfo(res);
      return (-1);
    }

    freeaddrinfo(res);
  }

  DEBUG("sysevent plugin: socket created and bound");

  return (start_thread());
} /* }}} int sysevent_init */

static int sysevent_config_add_listen(const oconfig_item_t *ci) /* {{{ */
{
  if (ci->values_num != 2 || ci->values[0].type != OCONFIG_TYPE_STRING ||
      ci->values[1].type != OCONFIG_TYPE_STRING) {
    ERROR("sysevent plugin: The `%s' config option needs "
          "two string arguments (ip and port).",
          ci->key);
    return (-1);
  }

  listen_ip = strdup(ci->values[0].value.string);
  listen_port = strdup(ci->values[1].value.string);

  return (0);
}

static int sysevent_config_add_buffer_size(const oconfig_item_t *ci) /* {{{ */
{
  int tmp = 0;

  if (cf_util_get_int(ci, &tmp) != 0)
    return (-1);
  else if ((tmp >= 1024) && (tmp <= 65535))
    listen_buffer_size = tmp;
  else {
    WARNING(
        "sysevent plugin: The `BufferSize' must be between 1024 and 65535.");
    return (-1);
  }

  return (0);
}

static int sysevent_config_add_buffer_length(const oconfig_item_t *ci) /* {{{ */
{
  int tmp = 0;

  if (cf_util_get_int(ci, &tmp) != 0)
    return (-1);
  else if ((tmp >= 3) && (tmp <= 1024))
    buffer_length = tmp;
  else {
    WARNING("sysevent plugin: The `Bufferlength' must be between 3 and 1024.");
    return (-1);
  }

  return (0);
}

static int sysevent_config_add_regex_filter(const oconfig_item_t *ci) /* {{{ */
{
  if (ci->values_num != 1 || ci->values[0].type != OCONFIG_TYPE_STRING) {
    ERROR("sysevent plugin: The `%s' config option needs "
          "one string argument, a regular expression.",
          ci->key);
    return (-1);
  }

  regexfilterlist_t *rl;
  char *regexp_str;
  regex_t regexp;
  int status;

  regexp_str = strdup(ci->values[0].value.string);

  status = regcomp(&regexp, regexp_str, REG_EXTENDED);

  if (status != 0) {
    ERROR("sysevent plugin: 'RegexFilter' invalid regular expression: %s",
          regexp_str);
    return (-1);
  }

  rl = malloc(sizeof(*rl));
  if (rl == NULL) {
    char errbuf[1024];
    ERROR("sysevent plugin: malloc failed during "
          "sysevent_config_add_regex_filter: %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    return (-1);
  }

  rl->regex_filter = regexp_str;
  rl->regex_filter_obj = regexp;
  rl->next = regexfilterlist_head;
  regexfilterlist_head = rl;

  return (0);
}

static int sysevent_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Listen", child->key) == 0)
      sysevent_config_add_listen(child);
    else if (strcasecmp("BufferSize", child->key) == 0)
      sysevent_config_add_buffer_size(child);
    else if (strcasecmp("BufferLength", child->key) == 0)
      sysevent_config_add_buffer_length(child);
    else if (strcasecmp("RegexFilter", child->key) == 0)
      sysevent_config_add_regex_filter(child);
    else {
      WARNING("sysevent plugin: Option `%s' is not allowed here.", child->key);
    }
  }

  return (0);
} /* }}} int sysevent_config */

// TODO
static void submit(const char *message, yajl_val *node,
                   const char *type, /* {{{ */
                   gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "sysevent", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));

  // Create metadata to store JSON key-values
  meta_data_t *meta = meta_data_create();

  if (node != NULL) {
    // If we have a parsed-JSON node to work with, use that
    size_t i = 0;

    for (i = 0; i < sizeof(rsyslog_keys) / sizeof(*rsyslog_keys); i++) {
      char json_val[listen_buffer_size];
      const char *key = (const char *)rsyslog_keys[i];
      const char *path[] = {key, (const char *)0};
      yajl_val v = yajl_tree_get(*node, path, yajl_t_string);

      memset(json_val, '\0', listen_buffer_size);

      snprintf(json_val, listen_buffer_size, "%s%c", YAJL_GET_STRING(v), '\0');

      DEBUG("sysevent plugin: adding jsonval: %s", json_val);

      meta_data_add_string(meta, rsyslog_keys[i], json_val);
    }

    for (i = 0; i < sizeof(rsyslog_field_keys) / sizeof(*rsyslog_field_keys);
         i++) {
      char json_val[listen_buffer_size];
      const char *key = (const char *)rsyslog_field_keys[i];
      const char *path[] = {"@fields", key, (const char *)0};
      yajl_val v = yajl_tree_get(*node, path, yajl_t_string);

      memset(json_val, '\0', listen_buffer_size);

      snprintf(json_val, listen_buffer_size, "%s%c", YAJL_GET_STRING(v), '\0');

      DEBUG("sysevent plugin: adding jsonval: %s", json_val);

      meta_data_add_string(meta, rsyslog_field_keys[i], json_val);
    }
  } else {
    // Data was not sent in JSON format, so just treat the whole log entry
    // as the message
    meta_data_add_string(meta, "@message", strdup(message));
  }

  vl.meta = meta;

  DEBUG("sysevent plugin: dispatching message");

  plugin_dispatch_values(&vl);
} /* }}} void sysevent_submit */

static int sysevent_read(void) /* {{{ */
{
  if (sysevent_thread_error != 0) {
    ERROR("sysevent plugin: The sysevent thread had a problem (%d). Restarting "
          "it.",
          sysevent_thread_error);

    stop_thread(0);

    start_thread();

    return (-1);
  } /* if (sysevent_thread_error != 0) */

  pthread_mutex_lock(&sysevent_lock);

  while (ring.head != ring.tail) {
    int is_match = 1;
    char *match_str = NULL;
    regexfilterlist_t *rl = regexfilterlist_head;
    int next = ring.tail + 1;

    if (next >= ring.maxLen)
      next = 0;

    DEBUG("sysevent plugin: reading %s", ring.buffer[ring.tail]);

    // Try to parse JSON, and if it fails, fall back to plain string
    yajl_val node;
    char errbuf[1024];

    errbuf[0] = 0;

    node = yajl_tree_parse((const char *)ring.buffer[ring.tail], errbuf,
                           sizeof(errbuf));

    if (node != NULL) {
      // JSON rsyslog data

      // If we have any regex filters, we need to see if the message portion of
      // the data matches any of them (otherwise we're not interested)
      if (regexfilterlist_head != NULL) {
        char json_val[listen_buffer_size];
        const char *path[] = {"@message", (const char *)0};
        yajl_val v = yajl_tree_get(node, path, yajl_t_string);

        memset(json_val, '\0', listen_buffer_size);

        snprintf(json_val, listen_buffer_size, "%s%c", YAJL_GET_STRING(v),
                 '\0');

        match_str = (char *)&json_val;
      }
    } else {
      // non-JSON rsyslog data

      // If we have any regex filters, we need to see if the message data
      // matches any of them (otherwise we're not interested)
      if (regexfilterlist_head != NULL)
        match_str = ring.buffer[ring.tail];
    }

    // If we care about matching, do that comparison here
    if (match_str != NULL) {
      is_match = 0;

      while (rl != NULL) {
        regmatch_t matches[SYSEVENT_REGEX_MATCHES];

        is_match = (regexec(&rl->regex_filter_obj, match_str,
                            SYSEVENT_REGEX_MATCHES, matches, 0) == 0
                        ? 1
                        : 0);

        if (is_match == 1) {
          DEBUG("sysevent plugin: regex filter match: %s", rl->regex_filter);
          break;
        }

        rl = rl->next;
      }
    }

    if (is_match == 1 && node != NULL)
      submit(NULL, &node, "gauge", 1);
    else if (is_match == 1)
      submit(ring.buffer[ring.tail], NULL, "gauge", 1);

    if (node != NULL)
      yajl_tree_free(node);

    ring.tail = next;
  }

  pthread_mutex_unlock(&sysevent_lock);

  return (0);
} /* }}} int sysevent_read */

static int sysevent_shutdown(void) /* {{{ */
{
  int status;
  regexfilterlist_t *rl;

  DEBUG("sysevent plugin: Shutting down thread.");
  if (stop_thread(1) < 0)
    return (-1);

  if (sock != -1) {
    status = close(sock);
    if (status != 0) {
      ERROR("sysevent plugin: failed to close socket %d: %d (%s)", sock, status,
            strerror(errno));
      return (-1);
    } else
      sock = -1;
  }

  free(listen_ip);
  free(listen_port);

  for (int i = 0; i < buffer_length; i++) {
    free(ring.buffer[i]);
  }

  free(ring.buffer);

  rl = regexfilterlist_head;
  while (rl != NULL) {
    regexfilterlist_t *rl_next;

    rl_next = rl->next;

    free(rl->regex_filter);
    regfree(&rl->regex_filter_obj);

    sfree(rl);

    rl = rl_next;
  }

  return (0);
} /* }}} int sysevent_shutdown */

void module_register(void) {
  plugin_register_complex_config("sysevent", sysevent_config);
  plugin_register_init("sysevent", sysevent_init);
  plugin_register_read("sysevent", sysevent_read);
  plugin_register_shutdown("sysevent", sysevent_shutdown);
} /* void module_register */