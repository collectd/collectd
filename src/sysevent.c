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

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"
#include "utils_complain.h"

#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <yajl/yajl_common.h>
#include <yajl/yajl_gen.h>

#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif
#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
#include <yajl/yajl_tree.h>
#define HAVE_YAJL_V2 1
#endif

#define SYSEVENT_DOMAIN_FIELD "domain"
#define SYSEVENT_DOMAIN_VALUE "syslog"
#define SYSEVENT_EVENT_ID_FIELD "eventId"
#define SYSEVENT_EVENT_NAME_FIELD "eventName"
#define SYSEVENT_EVENT_NAME_VALUE "syslog message"
#define SYSEVENT_LAST_EPOCH_MICROSEC_FIELD "lastEpochMicrosec"
#define SYSEVENT_PRIORITY_FIELD "priority"
#define SYSEVENT_PRIORITY_VALUE_HIGH "high"
#define SYSEVENT_PRIORITY_VALUE_LOW "low"
#define SYSEVENT_PRIORITY_VALUE_MEDIUM "medium"
#define SYSEVENT_PRIORITY_VALUE_NORMAL "normal"
#define SYSEVENT_PRIORITY_VALUE_UNKNOWN "unknown"
#define SYSEVENT_REPORTING_ENTITY_NAME_FIELD "reportingEntityName"
#define SYSEVENT_REPORTING_ENTITY_NAME_VALUE "collectd sysevent plugin"
#define SYSEVENT_SEQUENCE_FIELD "sequence"
#define SYSEVENT_SEQUENCE_VALUE "0"
#define SYSEVENT_SOURCE_NAME_FIELD "sourceName"
#define SYSEVENT_SOURCE_NAME_VALUE "syslog"
#define SYSEVENT_START_EPOCH_MICROSEC_FIELD "startEpochMicrosec"
#define SYSEVENT_VERSION_FIELD "version"
#define SYSEVENT_VERSION_VALUE "1.0"

#define SYSEVENT_EVENT_SOURCE_HOST_FIELD "eventSourceHost"
#define SYSEVENT_EVENT_SOURCE_TYPE_FIELD "eventSourceType"
#define SYSEVENT_EVENT_SOURCE_TYPE_VALUE "host"
#define SYSEVENT_SYSLOG_FIELDS_FIELD "syslogFields"
#define SYSEVENT_SYSLOG_FIELDS_VERSION_FIELD "syslogFieldsVersion"
#define SYSEVENT_SYSLOG_FIELDS_VERSION_VALUE "1.0"
#define SYSEVENT_SYSLOG_MSG_FIELD "syslogMsg"
#define SYSEVENT_SYSLOG_PROC_FIELD "syslogProc"
#define SYSEVENT_SYSLOG_SEV_FIELD "syslogSev"
#define SYSEVENT_SYSLOG_TAG_FIELD "syslogTag"
#define SYSEVENT_SYSLOG_TAG_VALUE "NILVALUE"

/*
 * Private data types
 */

typedef struct {
  int head;
  int tail;
  int maxLen;
  char **buffer;
  cdtime_t *timestamp;
} circbuf_t;

/*
 * Private variables
 */

static ignorelist_t *ignorelist = NULL;

static int sysevent_socket_thread_loop = 0;
static int sysevent_socket_thread_error = 0;
static pthread_t sysevent_socket_thread_id;
static int sysevent_dequeue_thread_loop = 0;
static pthread_t sysevent_dequeue_thread_id;
static pthread_mutex_t sysevent_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t sysevent_data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t sysevent_cond = PTHREAD_COND_INITIALIZER;
static int sock = -1;
static int event_id = 0;
static circbuf_t ring;

static char *listen_ip;
static char *listen_port;
static int listen_buffer_size = 4096;
static int buffer_length = 10;

static int monitor_all_messages = 1;

#if HAVE_YAJL_V2
static const char *rsyslog_keys[3] = {"@timestamp", "@source_host", "@message"};
static const char *rsyslog_field_keys[5] = {
    "facility", "severity", "severity-num", "program", "processid"};
#endif

/*
 * Private functions
 */

static int gen_message_payload(const char *msg, char *sev, int sev_num,
                               char *process, char *host, cdtime_t timestamp,
                               char **buf) {
  const unsigned char *buf2;
  yajl_gen g;
  char json_str[DATA_MAX_NAME_LEN];

#if !defined(HAVE_YAJL_V2)
  yajl_gen_config conf = {0};
#endif

#if HAVE_YAJL_V2
  size_t len;
  g = yajl_gen_alloc(NULL);
  yajl_gen_config(g, yajl_gen_beautify, 0);
#else
  unsigned int len;
  g = yajl_gen_alloc(&conf, NULL);
#endif

  yajl_gen_clear(g);

  // *** BEGIN common event header ***

  if (yajl_gen_map_open(g) != yajl_gen_status_ok)
    goto err;

  // domain
  if (yajl_gen_string(g, (u_char *)SYSEVENT_DOMAIN_FIELD,
                      strlen(SYSEVENT_DOMAIN_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)SYSEVENT_DOMAIN_VALUE,
                      strlen(SYSEVENT_DOMAIN_VALUE)) != yajl_gen_status_ok)
    goto err;

  // eventId
  if (yajl_gen_string(g, (u_char *)SYSEVENT_EVENT_ID_FIELD,
                      strlen(SYSEVENT_EVENT_ID_FIELD)) != yajl_gen_status_ok)
    goto err;

  event_id = event_id + 1;
  snprintf(json_str, sizeof(json_str), "%d", event_id);

  if (yajl_gen_number(g, json_str, strlen(json_str)) != yajl_gen_status_ok) {
    goto err;
  }

  // eventName
  if (yajl_gen_string(g, (u_char *)SYSEVENT_EVENT_NAME_FIELD,
                      strlen(SYSEVENT_EVENT_NAME_FIELD)) != yajl_gen_status_ok)
    goto err;

  snprintf(json_str, sizeof(json_str), "host %s rsyslog message", host);

  if (yajl_gen_string(g, (u_char *)json_str, strlen(json_str)) !=
      yajl_gen_status_ok) {
    goto err;
  }

  // lastEpochMicrosec
  if (yajl_gen_string(g, (u_char *)SYSEVENT_LAST_EPOCH_MICROSEC_FIELD,
                      strlen(SYSEVENT_LAST_EPOCH_MICROSEC_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  snprintf(json_str, sizeof(json_str), "%" PRIu64, CDTIME_T_TO_US(cdtime()));

  if (yajl_gen_number(g, json_str, strlen(json_str)) != yajl_gen_status_ok) {
    goto err;
  }

  // priority
  if (yajl_gen_string(g, (u_char *)SYSEVENT_PRIORITY_FIELD,
                      strlen(SYSEVENT_PRIORITY_FIELD)) != yajl_gen_status_ok)
    goto err;

  switch (sev_num) {
  case 4:
    if (yajl_gen_string(g, (u_char *)SYSEVENT_PRIORITY_VALUE_MEDIUM,
                        strlen(SYSEVENT_PRIORITY_VALUE_MEDIUM)) !=
        yajl_gen_status_ok)
      goto err;
    break;
  case 5:
    if (yajl_gen_string(g, (u_char *)SYSEVENT_PRIORITY_VALUE_NORMAL,
                        strlen(SYSEVENT_PRIORITY_VALUE_NORMAL)) !=
        yajl_gen_status_ok)
      goto err;
    break;
  case 6:
  case 7:
    if (yajl_gen_string(g, (u_char *)SYSEVENT_PRIORITY_VALUE_LOW,
                        strlen(SYSEVENT_PRIORITY_VALUE_LOW)) !=
        yajl_gen_status_ok)
      goto err;
    break;
  default:
    if (yajl_gen_string(g, (u_char *)SYSEVENT_PRIORITY_VALUE_UNKNOWN,
                        strlen(SYSEVENT_PRIORITY_VALUE_UNKNOWN)) !=
        yajl_gen_status_ok)
      goto err;
    break;
  }

  // reportingEntityName
  if (yajl_gen_string(g, (u_char *)SYSEVENT_REPORTING_ENTITY_NAME_FIELD,
                      strlen(SYSEVENT_REPORTING_ENTITY_NAME_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)SYSEVENT_REPORTING_ENTITY_NAME_VALUE,
                      strlen(SYSEVENT_REPORTING_ENTITY_NAME_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // sequence
  if (yajl_gen_string(g, (u_char *)SYSEVENT_SEQUENCE_FIELD,
                      strlen(SYSEVENT_SEQUENCE_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_number(g, SYSEVENT_SEQUENCE_VALUE,
                      strlen(SYSEVENT_SEQUENCE_VALUE)) != yajl_gen_status_ok)
    goto err;

  // sourceName
  if (yajl_gen_string(g, (u_char *)SYSEVENT_SOURCE_NAME_FIELD,
                      strlen(SYSEVENT_SOURCE_NAME_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)SYSEVENT_SOURCE_NAME_VALUE,
                      strlen(SYSEVENT_SOURCE_NAME_VALUE)) != yajl_gen_status_ok)
    goto err;

  // startEpochMicrosec
  if (yajl_gen_string(g, (u_char *)SYSEVENT_START_EPOCH_MICROSEC_FIELD,
                      strlen(SYSEVENT_START_EPOCH_MICROSEC_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  snprintf(json_str, sizeof(json_str), "%" PRIu64, CDTIME_T_TO_US(timestamp));

  if (yajl_gen_number(g, json_str, strlen(json_str)) != yajl_gen_status_ok) {
    goto err;
  }

  // version
  if (yajl_gen_string(g, (u_char *)SYSEVENT_VERSION_FIELD,
                      strlen(SYSEVENT_VERSION_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_number(g, SYSEVENT_VERSION_VALUE,
                      strlen(SYSEVENT_VERSION_VALUE)) != yajl_gen_status_ok)
    goto err;

  // *** END common event header ***

  // *** BEGIN syslog fields ***

  if (yajl_gen_string(g, (u_char *)SYSEVENT_SYSLOG_FIELDS_FIELD,
                      strlen(SYSEVENT_SYSLOG_FIELDS_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_map_open(g) != yajl_gen_status_ok)
    goto err;

  // eventSourceHost
  if (yajl_gen_string(g, (u_char *)SYSEVENT_EVENT_SOURCE_HOST_FIELD,
                      strlen(SYSEVENT_EVENT_SOURCE_HOST_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)host, strlen(host)) != yajl_gen_status_ok)
    goto err;

  // eventSourceType
  if (yajl_gen_string(g, (u_char *)SYSEVENT_EVENT_SOURCE_TYPE_FIELD,
                      strlen(SYSEVENT_EVENT_SOURCE_TYPE_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)SYSEVENT_EVENT_SOURCE_TYPE_VALUE,
                      strlen(SYSEVENT_EVENT_SOURCE_TYPE_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // syslogFieldsVersion
  if (yajl_gen_string(g, (u_char *)SYSEVENT_SYSLOG_FIELDS_VERSION_FIELD,
                      strlen(SYSEVENT_SYSLOG_FIELDS_VERSION_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_number(g, SYSEVENT_SYSLOG_FIELDS_VERSION_VALUE,
                      strlen(SYSEVENT_SYSLOG_FIELDS_VERSION_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // syslogMsg
  if (msg != NULL) {
    if (yajl_gen_string(g, (u_char *)SYSEVENT_SYSLOG_MSG_FIELD,
                        strlen(SYSEVENT_SYSLOG_MSG_FIELD)) !=
        yajl_gen_status_ok)
      goto err;

    if (yajl_gen_string(g, (u_char *)msg, strlen(msg)) != yajl_gen_status_ok)
      goto err;
  }

  // syslogProc
  if (process != NULL) {
    if (yajl_gen_string(g, (u_char *)SYSEVENT_SYSLOG_PROC_FIELD,
                        strlen(SYSEVENT_SYSLOG_PROC_FIELD)) !=
        yajl_gen_status_ok)
      goto err;

    if (yajl_gen_string(g, (u_char *)process, strlen(process)) !=
        yajl_gen_status_ok)
      goto err;
  }

  // syslogSev
  if (sev != NULL) {
    if (yajl_gen_string(g, (u_char *)SYSEVENT_SYSLOG_SEV_FIELD,
                        strlen(SYSEVENT_SYSLOG_SEV_FIELD)) !=
        yajl_gen_status_ok)
      goto err;

    if (yajl_gen_string(g, (u_char *)sev, strlen(sev)) != yajl_gen_status_ok)
      goto err;
  }

  // syslogTag
  if (yajl_gen_string(g, (u_char *)SYSEVENT_SYSLOG_TAG_FIELD,
                      strlen(SYSEVENT_SYSLOG_TAG_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)SYSEVENT_SYSLOG_TAG_VALUE,
                      strlen(SYSEVENT_SYSLOG_TAG_VALUE)) != yajl_gen_status_ok)
    goto err;

  // *** END syslog fields ***

  // close syslog and header fields
  if (yajl_gen_map_close(g) != yajl_gen_status_ok ||
      yajl_gen_map_close(g) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_get_buf(g, &buf2, &len) != yajl_gen_status_ok)
    goto err;

  *buf = strdup((char *)buf2);

  if (*buf == NULL) {
    ERROR("sysevent plugin: gen_message_payload strdup failed");
    goto err;
  }

  yajl_gen_free(g);

  return 0;

err:
  yajl_gen_free(g);
  ERROR("sysevent plugin: gen_message_payload failed to generate JSON");
  return -1;
}

static int read_socket() {
  int recv_flags = MSG_DONTWAIT;

  while (42) {
    struct sockaddr_storage src_addr;
    socklen_t src_addr_len = sizeof(src_addr);

    char buffer[listen_buffer_size];
    memset(buffer, '\0', listen_buffer_size);

    ssize_t count = recvfrom(sock, buffer, sizeof(buffer), recv_flags,
                             (struct sockaddr *)&src_addr, &src_addr_len);

    if (count < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        pthread_mutex_lock(&sysevent_data_lock);

        // There was nothing more to receive for now, so...
        // If ring head does not equal ring tail, there is data
        // in the ring buffer for the dequeue thread to read, so
        // signal it
        if (ring.head != ring.tail)
          pthread_cond_signal(&sysevent_cond);

        pthread_mutex_unlock(&sysevent_data_lock);

        // Since there was nothing to receive, set recv to block and
        // try again
        recv_flags = 0;
        continue;
      } else if (errno != EINTR) {
        ERROR("sysevent plugin: failed to receive data: %s", STRERRNO);
        return -1;
      } else {
        // Interrupt, so continue and try again
        continue;
      }
    }

    if (count >= sizeof(buffer)) {
      WARNING("sysevent plugin: datagram too large for buffer: truncated");
    }

    // We successfully received a message, so don't block on the next
    // read in case there are more (and if there aren't, it will be
    // handled above in the EWOULDBLOCK error-checking)
    recv_flags = MSG_DONTWAIT;

    // 1. Acquire data lock
    // 2. Push to buffer if there is room, otherwise raise warning
    //    and allow dequeue thread to take over

    pthread_mutex_lock(&sysevent_data_lock);

    int next = ring.head + 1;
    if (next >= ring.maxLen)
      next = 0;

    if (next == ring.tail) {
      // Buffer is full, signal the dequeue thread to process the buffer
      // and clean it out, and then sleep
      WARNING("sysevent plugin: ring buffer full");

      pthread_cond_signal(&sysevent_cond);
      pthread_mutex_unlock(&sysevent_data_lock);

      usleep(1000);
      continue;
    } else {
      DEBUG("sysevent plugin: writing %s", buffer);

      sstrncpy(ring.buffer[ring.head], buffer, sizeof(buffer));
      ring.timestamp[ring.head] = cdtime();
      ring.head = next;
    }

    pthread_mutex_unlock(&sysevent_data_lock);
  }
}

static void sysevent_dispatch_notification(const char *message,
#if HAVE_YAJL_V2
                                           yajl_val *node,
#endif
                                           cdtime_t timestamp) {
  char *buf = NULL;

  notification_t n = {
      .severity = NOTIF_OKAY,
      .time = cdtime(),
      .plugin = "sysevent",
      .type = "gauge",
  };

#if HAVE_YAJL_V2
  if (node != NULL) {
    // If we have a parsed-JSON node to work with, use that
    // msg
    const char *msg_path[] = {rsyslog_keys[2], (const char *)0};
    yajl_val msg_v = yajl_tree_get(*node, msg_path, yajl_t_string);

    char msg[listen_buffer_size];

    if (msg_v != NULL) {
      memset(msg, '\0', listen_buffer_size);
      snprintf(msg, listen_buffer_size, "%s%c", YAJL_GET_STRING(msg_v), '\0');
    }

    // severity
    const char *severity_path[] = {"@fields", rsyslog_field_keys[1],
                                   (const char *)0};
    yajl_val severity_v = yajl_tree_get(*node, severity_path, yajl_t_string);

    char severity[listen_buffer_size];

    if (severity_v != NULL) {
      memset(severity, '\0', listen_buffer_size);
      snprintf(severity, listen_buffer_size, "%s%c",
               YAJL_GET_STRING(severity_v), '\0');
    }

    // sev_num
    const char *sev_num_str_path[] = {"@fields", rsyslog_field_keys[2],
                                      (const char *)0};
    yajl_val sev_num_str_v =
        yajl_tree_get(*node, sev_num_str_path, yajl_t_string);

    char sev_num_str[listen_buffer_size];
    int sev_num = -1;

    if (sev_num_str_v != NULL) {
      memset(sev_num_str, '\0', listen_buffer_size);
      snprintf(sev_num_str, listen_buffer_size, "%s%c",
               YAJL_GET_STRING(sev_num_str_v), '\0');

      sev_num = atoi(sev_num_str);

      if (sev_num < 4)
        n.severity = NOTIF_FAILURE;
    }

    // process
    const char *process_path[] = {"@fields", rsyslog_field_keys[3],
                                  (const char *)0};
    yajl_val process_v = yajl_tree_get(*node, process_path, yajl_t_string);

    char process[listen_buffer_size];

    if (process_v != NULL) {
      memset(process, '\0', listen_buffer_size);
      snprintf(process, listen_buffer_size, "%s%c", YAJL_GET_STRING(process_v),
               '\0');
    }

    // hostname
    const char *hostname_path[] = {rsyslog_keys[1], (const char *)0};
    yajl_val hostname_v = yajl_tree_get(*node, hostname_path, yajl_t_string);

    char hostname_str[listen_buffer_size];

    if (hostname_v != NULL) {
      memset(hostname_str, '\0', listen_buffer_size);
      snprintf(hostname_str, listen_buffer_size, "%s%c",
               YAJL_GET_STRING(hostname_v), '\0');
    }

    gen_message_payload(
        (msg_v != NULL ? msg : NULL), (severity_v != NULL ? severity : NULL),
        (sev_num_str_v != NULL ? sev_num : -1),
        (process_v != NULL ? process : NULL),
        (hostname_v != NULL ? hostname_str : hostname_g), timestamp, &buf);
  } else {
    // Data was not sent in JSON format, so just treat the whole log entry
    // as the message (and we'll be unable to acquire certain data, so the
    // payload
    // generated below will be less informative)

    gen_message_payload(message, NULL, -1, NULL, hostname_g, timestamp, &buf);
  }
#else
  gen_message_payload(message, NULL, -1, NULL, hostname_g, timestamp, &buf);
#endif

  sstrncpy(n.host, hostname_g, sizeof(n.host));

  int status = plugin_notification_meta_add_string(&n, "ves", buf);

  if (status < 0) {
    sfree(buf);
    ERROR("sysevent plugin: unable to set notification VES metadata: %s",
          STRERRNO);
    return;
  }

  DEBUG("sysevent plugin: notification VES metadata: %s",
        n.meta->nm_value.nm_string);

  DEBUG("sysevent plugin: dispatching message");

  plugin_dispatch_notification(&n);
  plugin_notification_meta_free(n.meta);

  // strdup'd in gen_message_payload
  if (buf != NULL)
    sfree(buf);
}

static void read_ring_buffer() {
  pthread_mutex_lock(&sysevent_data_lock);

  // If there's currently nothing to read from the buffer,
  // then wait
  if (ring.head == ring.tail)
    pthread_cond_wait(&sysevent_cond, &sysevent_data_lock);

  while (ring.head != ring.tail) {
    int next = ring.tail + 1;

    if (next >= ring.maxLen)
      next = 0;

    DEBUG("sysevent plugin: reading from ring buffer: %s",
          ring.buffer[ring.tail]);

    cdtime_t timestamp = ring.timestamp[ring.tail];
    char *match_str = NULL;

#if HAVE_YAJL_V2
    // Try to parse JSON, and if it fails, fall back to plain string
    char errbuf[1024];
    errbuf[0] = 0;
    yajl_val node = yajl_tree_parse((const char *)ring.buffer[ring.tail],
                                    errbuf, sizeof(errbuf));

    if (node != NULL) {
      // JSON rsyslog data

      // If we have any regex filters, we need to see if the message portion of
      // the data matches any of them (otherwise we're not interested)
      if (monitor_all_messages == 0) {
        const char *path[] = {"@message", (const char *)0};
        yajl_val v = yajl_tree_get(node, path, yajl_t_string);

        char json_val[listen_buffer_size];
        memset(json_val, '\0', listen_buffer_size);

        snprintf(json_val, listen_buffer_size, "%s%c", YAJL_GET_STRING(v),
                 '\0');

        match_str = (char *)&json_val;
      }
    } else {
      // non-JSON rsyslog data

      // If we have any regex filters, we need to see if the message data
      // matches any of them (otherwise we're not interested)
      if (monitor_all_messages == 0)
        match_str = ring.buffer[ring.tail];
    }
#else
    // If we have any regex filters, we need to see if the message data
    // matches any of them (otherwise we're not interested)
    if (monitor_all_messages == 0)
      match_str = ring.buffer[ring.tail];
#endif

    int is_match = 1;

    // If we care about matching, do that comparison here
    if (match_str != NULL) {
      if (ignorelist_match(ignorelist, match_str) != 0)
        is_match = 0;
      else
        DEBUG("sysevent plugin: regex filter match");
    }

#if HAVE_YAJL_V2
    if (is_match == 1 && node != NULL) {
      sysevent_dispatch_notification(NULL, &node, timestamp);
      yajl_tree_free(node);
    } else if (is_match == 1)
      sysevent_dispatch_notification(ring.buffer[ring.tail], NULL, timestamp);
#else
    if (is_match == 1)
      sysevent_dispatch_notification(ring.buffer[ring.tail], timestamp);
#endif

    ring.tail = next;
  }

  pthread_mutex_unlock(&sysevent_data_lock);
}

static void *sysevent_socket_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&sysevent_thread_lock);

  while (sysevent_socket_thread_loop > 0) {
    pthread_mutex_unlock(&sysevent_thread_lock);

    if (sock == -1)
      return (void *)0;

    int status = read_socket();

    pthread_mutex_lock(&sysevent_thread_lock);

    if (status < 0) {
      WARNING("sysevent plugin: problem with socket thread (status: %d)",
              status);
      sysevent_socket_thread_error = 1;
      break;
    }
  } /* while (sysevent_socket_thread_loop > 0) */

  pthread_mutex_unlock(&sysevent_thread_lock);

  return (void *)0;
} /* }}} void *sysevent_socket_thread */

// Entry point for thread responsible for reading from
// ring buffer and dispatching notifications
static void *sysevent_dequeue_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&sysevent_thread_lock);

  while (sysevent_dequeue_thread_loop > 0) {
    pthread_mutex_unlock(&sysevent_thread_lock);

    read_ring_buffer();

    pthread_mutex_lock(&sysevent_thread_lock);
  } /* while (sysevent_dequeue_thread_loop > 0) */

  pthread_mutex_unlock(&sysevent_thread_lock);

  return (void *)0;
} /* }}} void *sysevent_dequeue_thread */

static int start_socket_thread(void) /* {{{ */
{
  pthread_mutex_lock(&sysevent_thread_lock);

  if (sysevent_socket_thread_loop != 0) {
    pthread_mutex_unlock(&sysevent_thread_lock);
    return 0;
  }

  sysevent_socket_thread_loop = 1;
  sysevent_socket_thread_error = 0;

  DEBUG("sysevent plugin: starting socket thread");

  int status =
      plugin_thread_create(&sysevent_socket_thread_id, sysevent_socket_thread,
                           /* arg = */ (void *)0, "sysevent");
  if (status != 0) {
    sysevent_socket_thread_loop = 0;
    ERROR("sysevent plugin: starting socket thread failed.");
    pthread_mutex_unlock(&sysevent_thread_lock);
    return -1;
  }

  pthread_mutex_unlock(&sysevent_thread_lock);

  return 0;
} /* }}} int start_socket_thread */

static int start_dequeue_thread(void) /* {{{ */
{
  pthread_mutex_lock(&sysevent_thread_lock);

  if (sysevent_dequeue_thread_loop != 0) {
    pthread_mutex_unlock(&sysevent_thread_lock);
    return 0;
  }

  sysevent_dequeue_thread_loop = 1;

  int status =
      plugin_thread_create(&sysevent_dequeue_thread_id, sysevent_dequeue_thread,
                           /* arg = */ (void *)0, "ssyevent");
  if (status != 0) {
    sysevent_dequeue_thread_loop = 0;
    ERROR("sysevent plugin: Starting dequeue thread failed.");
    pthread_mutex_unlock(&sysevent_thread_lock);
    return -1;
  }

  pthread_mutex_unlock(&sysevent_thread_lock);

  return status;
} /* }}} int start_dequeue_thread */

static int start_threads(void) /* {{{ */
{
  int status = start_socket_thread();
  int status2 = start_dequeue_thread();

  if (status != 0)
    return status;
  else
    return status2;
} /* }}} int start_threads */

static int stop_socket_thread(int shutdown) /* {{{ */
{
  pthread_mutex_lock(&sysevent_thread_lock);

  if (sysevent_socket_thread_loop == 0) {
    pthread_mutex_unlock(&sysevent_thread_lock);
    return -1;
  }

  sysevent_socket_thread_loop = 0;
  pthread_cond_broadcast(&sysevent_cond);
  pthread_mutex_unlock(&sysevent_thread_lock);

  int status;

  if (shutdown == 1) {
    // Since the thread is blocking, calling pthread_join
    // doesn't actually succeed in stopping it.  It will stick around
    // until a message is received on the socket (at which
    // it will realize that "sysevent_socket_thread_loop" is 0 and will
    // break out of the read loop and be allowed to die).  This is
    // fine when the process isn't supposed to be exiting, but in
    // the case of a process shutdown, we don't want to have an
    // idle thread hanging around.  Calling pthread_cancel here in
    // the case of a shutdown is just assures that the thread is
    // gone and that the process has been fully terminated.

    DEBUG("sysevent plugin: Canceling socket thread for process shutdown");

    status = pthread_cancel(sysevent_socket_thread_id);

    if (status != 0 && status != ESRCH) {
      ERROR("sysevent plugin: Unable to cancel socket thread: %d (%s)", status,
            STRERRNO);
      status = -1;
    } else
      status = 0;
  } else {
    status = pthread_join(sysevent_socket_thread_id, /* return = */ NULL);
    if (status != 0 && status != ESRCH) {
      ERROR("sysevent plugin: Stopping socket thread failed.");
      status = -1;
    } else
      status = 0;
  }

  pthread_mutex_lock(&sysevent_thread_lock);
  memset(&sysevent_socket_thread_id, 0, sizeof(sysevent_socket_thread_id));
  sysevent_socket_thread_error = 0;
  pthread_mutex_unlock(&sysevent_thread_lock);

  DEBUG("sysevent plugin: Finished requesting stop of socket thread");

  return status;
} /* }}} int stop_socket_thread */

static int stop_dequeue_thread() /* {{{ */
{
  pthread_mutex_lock(&sysevent_thread_lock);

  if (sysevent_dequeue_thread_loop == 0) {
    pthread_mutex_unlock(&sysevent_thread_lock);
    return -1;
  }

  sysevent_dequeue_thread_loop = 0;
  pthread_cond_broadcast(&sysevent_cond);
  pthread_mutex_unlock(&sysevent_thread_lock);

  // Since the thread is blocking, calling pthread_join
  // doesn't actually succeed in stopping it.  It will stick around
  // until a message is received on the socket (at which
  // it will realize that "sysevent_dequeue_thread_loop" is 0 and will
  // break out of the read loop and be allowed to die).  Since this
  // function is called when the processing is exiting, we don't want to
  // have an idle thread hanging around.  Calling pthread_cancel here
  // just assures that the thread is gone and that the process has been
  // fully terminated.

  DEBUG("sysevent plugin: Canceling dequeue thread for process shutdown");

  int status = pthread_cancel(sysevent_dequeue_thread_id);

  if (status != 0 && status != ESRCH) {
    ERROR("sysevent plugin: Unable to cancel dequeue thread: %d (%s)", status,
          STRERRNO);
    status = -1;
  } else
    status = 0;

  pthread_mutex_lock(&sysevent_thread_lock);
  memset(&sysevent_dequeue_thread_id, 0, sizeof(sysevent_dequeue_thread_id));
  pthread_mutex_unlock(&sysevent_thread_lock);

  DEBUG("sysevent plugin: Finished requesting stop of dequeue thread");

  return status;
} /* }}} int stop_dequeue_thread */

static int stop_threads() /* {{{ */
{
  int status = stop_socket_thread(1);
  int status2 = stop_dequeue_thread();

  if (status != 0)
    return status;
  else
    return status2;
} /* }}} int stop_threads */

static int sysevent_init(void) /* {{{ */
{
  ring.head = 0;
  ring.tail = 0;
  ring.maxLen = buffer_length;
  ring.buffer = (char **)calloc(buffer_length, sizeof(char *));

  if (ring.buffer == NULL) {
    ERROR("sysevent plugin: sysevent_init ring buffer calloc failed");
    return -1;
  }

  for (int i = 0; i < buffer_length; i++) {
    ring.buffer[i] = calloc(1, listen_buffer_size);

    if (ring.buffer[i] == NULL) {
      ERROR("sysevent plugin: sysevent_init ring buffer entry calloc failed");
      return -1;
    }
  }

  ring.timestamp = (cdtime_t *)calloc(buffer_length, sizeof(cdtime_t));

  if (ring.timestamp == NULL) {
    ERROR("sysevent plugin: sysevent_init ring buffer timestamp calloc failed");
    return -1;
  }

  if (sock == -1) {
    struct addrinfo hints = {
        .ai_family = AF_UNSPEC,
        .ai_socktype = SOCK_DGRAM,
        .ai_protocol = 0,
        .ai_flags = AI_PASSIVE | AI_ADDRCONFIG,
    };
    struct addrinfo *res = 0;

    int err = getaddrinfo(listen_ip, listen_port, &hints, &res);

    if (err != 0) {
      ERROR("sysevent plugin: failed to resolve local socket address (err=%d)",
            err);
      freeaddrinfo(res);
      return -1;
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == -1) {
      ERROR("sysevent plugin: failed to open socket: %s", STRERRNO);
      freeaddrinfo(res);
      return -1;
    }

    if (bind(sock, res->ai_addr, res->ai_addrlen) == -1) {
      ERROR("sysevent plugin: failed to bind socket: %s", STRERRNO);
      freeaddrinfo(res);
      sock = -1;
      return -1;
    }

    freeaddrinfo(res);
  }

  DEBUG("sysevent plugin: socket created and bound");

  return start_threads();
} /* }}} int sysevent_init */

static int sysevent_config_add_listen(const oconfig_item_t *ci) /* {{{ */
{
  if (ci->values_num != 2 || ci->values[0].type != OCONFIG_TYPE_STRING ||
      ci->values[1].type != OCONFIG_TYPE_STRING) {
    ERROR("sysevent plugin: The `%s' config option needs "
          "two string arguments (ip and port).",
          ci->key);
    return -1;
  }

  listen_ip = strdup(ci->values[0].value.string);
  listen_port = strdup(ci->values[1].value.string);

  return 0;
}

static int sysevent_config_add_buffer_size(const oconfig_item_t *ci) /* {{{ */
{
  int tmp = 0;

  if (cf_util_get_int(ci, &tmp) != 0)
    return -1;
  else if ((tmp >= 1024) && (tmp <= 65535))
    listen_buffer_size = tmp;
  else {
    WARNING(
        "sysevent plugin: The `BufferSize' must be between 1024 and 65535.");
    return -1;
  }

  return 0;
}

static int sysevent_config_add_buffer_length(const oconfig_item_t *ci) /* {{{ */
{
  int tmp = 0;

  if (cf_util_get_int(ci, &tmp) != 0)
    return -1;
  else if ((tmp >= 3) && (tmp <= 4096))
    buffer_length = tmp;
  else {
    WARNING("sysevent plugin: The `Bufferlength' must be between 3 and 4096.");
    return -1;
  }

  return 0;
}

static int sysevent_config_add_regex_filter(const oconfig_item_t *ci) /* {{{ */
{
  if (ci->values_num != 1 || ci->values[0].type != OCONFIG_TYPE_STRING) {
    ERROR("sysevent plugin: The `%s' config option needs "
          "one string argument, a regular expression.",
          ci->key);
    return -1;
  }

#if HAVE_REGEX_H
  if (ignorelist == NULL)
    ignorelist = ignorelist_create(/* invert = */ 1);

  int status = ignorelist_add(ignorelist, ci->values[0].value.string);

  if (status != 0) {
    ERROR("sysevent plugin: invalid regular expression: %s",
          ci->values[0].value.string);
    return 1;
  }

  monitor_all_messages = 0;
#else
  WARNING("sysevent plugin: The plugin has been compiled without support "
          "for the \"RegexFilter\" option.");
#endif

  return 0;
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

  return 0;
} /* }}} int sysevent_config */

static int sysevent_read(void) /* {{{ */
{
  pthread_mutex_lock(&sysevent_thread_lock);

  if (sysevent_socket_thread_error != 0) {
    pthread_mutex_unlock(&sysevent_thread_lock);

    ERROR("sysevent plugin: The sysevent socket thread had a problem (%d). "
          "Restarting it.",
          sysevent_socket_thread_error);

    stop_threads();

    start_threads();

    return -1;
  } /* if (sysevent_socket_thread_error != 0) */

  pthread_mutex_unlock(&sysevent_thread_lock);

  return 0;
} /* }}} int sysevent_read */

static int sysevent_shutdown(void) /* {{{ */
{
  DEBUG("sysevent plugin: Shutting down thread.");

  int status = stop_threads();
  int status2 = 0;

  if (sock != -1) {
    status2 = close(sock);
    if (status2 != 0) {
      ERROR("sysevent plugin: failed to close socket %d: %d (%s)", sock, status,
            STRERRNO);
    }

    sock = -1;
  }

  free(listen_ip);
  free(listen_port);

  for (int i = 0; i < buffer_length; i++) {
    free(ring.buffer[i]);
  }

  free(ring.buffer);
  free(ring.timestamp);

  if (status != 0)
    return status;
  else
    return status2;
} /* }}} int sysevent_shutdown */

void module_register(void) {
  plugin_register_complex_config("sysevent", sysevent_config);
  plugin_register_init("sysevent", sysevent_init);
  plugin_register_read("sysevent", sysevent_read);
  plugin_register_shutdown("sysevent", sysevent_shutdown);
} /* void module_register */
