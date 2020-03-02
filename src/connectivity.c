/**
 * collectd - src/connectivity.c
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
 *     Aneesh Puttur <aputtur at redhat.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"
#include "utils_complain.h"

#include <asm/types.h>
#include <errno.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <libmnl/libmnl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>

#include <yajl/yajl_common.h>
#include <yajl/yajl_gen.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif
#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
#define HAVE_YAJL_V2 1
#endif

#define MYPROTO NETLINK_ROUTE

#define LINK_STATE_DOWN 0
#define LINK_STATE_UP 1
#define LINK_STATE_UNKNOWN 2

#define CONNECTIVITY_DOMAIN_FIELD "domain"
#define CONNECTIVITY_DOMAIN_VALUE "stateChange"
#define CONNECTIVITY_EVENT_ID_FIELD "eventId"
#define CONNECTIVITY_EVENT_NAME_FIELD "eventName"
#define CONNECTIVITY_EVENT_NAME_DOWN_VALUE "down"
#define CONNECTIVITY_EVENT_NAME_UP_VALUE "up"
#define CONNECTIVITY_LAST_EPOCH_MICROSEC_FIELD "lastEpochMicrosec"
#define CONNECTIVITY_PRIORITY_FIELD "priority"
#define CONNECTIVITY_PRIORITY_VALUE "high"
#define CONNECTIVITY_REPORTING_ENTITY_NAME_FIELD "reportingEntityName"
#define CONNECTIVITY_REPORTING_ENTITY_NAME_VALUE "collectd connectivity plugin"
#define CONNECTIVITY_SEQUENCE_FIELD "sequence"
#define CONNECTIVITY_SEQUENCE_VALUE "0"
#define CONNECTIVITY_SOURCE_NAME_FIELD "sourceName"
#define CONNECTIVITY_START_EPOCH_MICROSEC_FIELD "startEpochMicrosec"
#define CONNECTIVITY_VERSION_FIELD "version"
#define CONNECTIVITY_VERSION_VALUE "1.0"

#define CONNECTIVITY_NEW_STATE_FIELD "newState"
#define CONNECTIVITY_NEW_STATE_FIELD_DOWN_VALUE "outOfService"
#define CONNECTIVITY_NEW_STATE_FIELD_UP_VALUE "inService"
#define CONNECTIVITY_OLD_STATE_FIELD "oldState"
#define CONNECTIVITY_OLD_STATE_FIELD_DOWN_VALUE "outOfService"
#define CONNECTIVITY_OLD_STATE_FIELD_UP_VALUE "inService"
#define CONNECTIVITY_STATE_CHANGE_FIELDS_FIELD "stateChangeFields"
#define CONNECTIVITY_STATE_CHANGE_FIELDS_VERSION_FIELD                         \
  "stateChangeFieldsVersion"
#define CONNECTIVITY_STATE_CHANGE_FIELDS_VERSION_VALUE "1.0"
#define CONNECTIVITY_STATE_INTERFACE_FIELD "stateInterface"

/*
 * Private data types
 */

struct interface_list_s {
  char *interface;

  uint32_t status;
  uint32_t prev_status;
  uint32_t sent;
  cdtime_t timestamp;

  struct interface_list_s *next;
};
typedef struct interface_list_s interface_list_t;

/*
 * Private variables
 */

static ignorelist_t *ignorelist = NULL;

static interface_list_t *interface_list_head = NULL;
static int monitor_all_interfaces = 1;

static int connectivity_netlink_thread_loop = 0;
static int connectivity_netlink_thread_error = 0;
static pthread_t connectivity_netlink_thread_id;
static int connectivity_dequeue_thread_loop = 0;
static pthread_t connectivity_dequeue_thread_id;
static pthread_mutex_t connectivity_threads_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t connectivity_data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t connectivity_cond = PTHREAD_COND_INITIALIZER;
static int nl_sock = -1;
static int event_id = 0;
static int statuses_to_send = 0;

static const char *config_keys[] = {"Interface", "IgnoreSelected"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

/*
 * Private functions
 */

static int gen_message_payload(int state, int old_state, const char *interface,
                               cdtime_t timestamp, char **buf) {
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
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_DOMAIN_FIELD,
                      strlen(CONNECTIVITY_DOMAIN_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_DOMAIN_VALUE,
                      strlen(CONNECTIVITY_DOMAIN_VALUE)) != yajl_gen_status_ok)
    goto err;

  // eventId
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_EVENT_ID_FIELD,
                      strlen(CONNECTIVITY_EVENT_ID_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  event_id = event_id + 1;
  if (snprintf(json_str, sizeof(json_str), "%d", event_id) < 0) {
    goto err;
  }

  if (yajl_gen_number(g, json_str, strlen(json_str)) != yajl_gen_status_ok) {
    goto err;
  }

  // eventName
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_EVENT_NAME_FIELD,
                      strlen(CONNECTIVITY_EVENT_NAME_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (snprintf(json_str, sizeof(json_str), "interface %s %s", interface,
               (state == 0 ? CONNECTIVITY_EVENT_NAME_DOWN_VALUE
                           : CONNECTIVITY_EVENT_NAME_UP_VALUE)) < 0) {
    goto err;
  }

  if (yajl_gen_string(g, (u_char *)json_str, strlen(json_str)) !=
      yajl_gen_status_ok) {
    goto err;
  }

  // lastEpochMicrosec
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_LAST_EPOCH_MICROSEC_FIELD,
                      strlen(CONNECTIVITY_LAST_EPOCH_MICROSEC_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (snprintf(json_str, sizeof(json_str), "%" PRIu64,
               CDTIME_T_TO_US(cdtime())) < 0) {
    goto err;
  }

  if (yajl_gen_number(g, json_str, strlen(json_str)) != yajl_gen_status_ok) {
    goto err;
  }

  // priority
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_PRIORITY_FIELD,
                      strlen(CONNECTIVITY_PRIORITY_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_PRIORITY_VALUE,
                      strlen(CONNECTIVITY_PRIORITY_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // reportingEntityName
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_REPORTING_ENTITY_NAME_FIELD,
                      strlen(CONNECTIVITY_REPORTING_ENTITY_NAME_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_REPORTING_ENTITY_NAME_VALUE,
                      strlen(CONNECTIVITY_REPORTING_ENTITY_NAME_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // sequence
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_SEQUENCE_FIELD,
                      strlen(CONNECTIVITY_SEQUENCE_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_number(g, CONNECTIVITY_SEQUENCE_VALUE,
                      strlen(CONNECTIVITY_SEQUENCE_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // sourceName
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_SOURCE_NAME_FIELD,
                      strlen(CONNECTIVITY_SOURCE_NAME_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)interface, strlen(interface)) !=
      yajl_gen_status_ok)
    goto err;

  // startEpochMicrosec
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_START_EPOCH_MICROSEC_FIELD,
                      strlen(CONNECTIVITY_START_EPOCH_MICROSEC_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (snprintf(json_str, sizeof(json_str), "%" PRIu64,
               CDTIME_T_TO_US(timestamp)) < 0) {
    goto err;
  }

  if (yajl_gen_number(g, json_str, strlen(json_str)) != yajl_gen_status_ok) {
    goto err;
  }

  // version
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_VERSION_FIELD,
                      strlen(CONNECTIVITY_VERSION_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_number(g, CONNECTIVITY_VERSION_VALUE,
                      strlen(CONNECTIVITY_VERSION_VALUE)) != yajl_gen_status_ok)
    goto err;

  // *** END common event header ***

  // *** BEGIN state change fields ***

  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_STATE_CHANGE_FIELDS_FIELD,
                      strlen(CONNECTIVITY_STATE_CHANGE_FIELDS_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_map_open(g) != yajl_gen_status_ok)
    goto err;

  // newState
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_NEW_STATE_FIELD,
                      strlen(CONNECTIVITY_NEW_STATE_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  int new_state_len =
      (state == 0 ? strlen(CONNECTIVITY_NEW_STATE_FIELD_DOWN_VALUE)
                  : strlen(CONNECTIVITY_NEW_STATE_FIELD_UP_VALUE));

  if (yajl_gen_string(g,
                      (u_char *)(state == 0
                                     ? CONNECTIVITY_NEW_STATE_FIELD_DOWN_VALUE
                                     : CONNECTIVITY_NEW_STATE_FIELD_UP_VALUE),
                      new_state_len) != yajl_gen_status_ok)
    goto err;

  // oldState
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_OLD_STATE_FIELD,
                      strlen(CONNECTIVITY_OLD_STATE_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  int old_state_len =
      (old_state == 0 ? strlen(CONNECTIVITY_OLD_STATE_FIELD_DOWN_VALUE)
                      : strlen(CONNECTIVITY_OLD_STATE_FIELD_UP_VALUE));

  if (yajl_gen_string(g,
                      (u_char *)(old_state == 0
                                     ? CONNECTIVITY_OLD_STATE_FIELD_DOWN_VALUE
                                     : CONNECTIVITY_OLD_STATE_FIELD_UP_VALUE),
                      old_state_len) != yajl_gen_status_ok)
    goto err;

  // stateChangeFieldsVersion
  if (yajl_gen_string(g,
                      (u_char *)CONNECTIVITY_STATE_CHANGE_FIELDS_VERSION_FIELD,
                      strlen(CONNECTIVITY_STATE_CHANGE_FIELDS_VERSION_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_number(g, CONNECTIVITY_STATE_CHANGE_FIELDS_VERSION_VALUE,
                      strlen(CONNECTIVITY_STATE_CHANGE_FIELDS_VERSION_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // stateInterface
  if (yajl_gen_string(g, (u_char *)CONNECTIVITY_STATE_INTERFACE_FIELD,
                      strlen(CONNECTIVITY_STATE_INTERFACE_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)interface, strlen(interface)) !=
      yajl_gen_status_ok)
    goto err;

  // close state change and header fields
  if (yajl_gen_map_close(g) != yajl_gen_status_ok ||
      yajl_gen_map_close(g) != yajl_gen_status_ok)
    goto err;

  // *** END state change fields ***

  if (yajl_gen_get_buf(g, &buf2, &len) != yajl_gen_status_ok)
    goto err;

  *buf = strdup((char *)buf2);

  if (*buf == NULL) {
    ERROR("connectivity plugin: strdup failed during gen_message_payload: %s",
          STRERRNO);
    goto err;
  }

  yajl_gen_free(g);

  return 0;

err:
  yajl_gen_free(g);
  ERROR("connectivity plugin: gen_message_payload failed to generate JSON");
  return -1;
}

static interface_list_t *add_interface(const char *interface, int status,
                                       int prev_status) {
  interface_list_t *il = calloc(1, sizeof(*il));

  if (il == NULL) {
    ERROR("connectivity plugin: calloc failed during add_interface: %s",
          STRERRNO);
    return NULL;
  }

  char *interface2 = strdup(interface);
  if (interface2 == NULL) {
    sfree(il);
    ERROR("connectivity plugin: strdup failed during add_interface: %s",
          STRERRNO);
    return NULL;
  }

  il->interface = interface2;
  il->status = status;
  il->prev_status = prev_status;
  il->timestamp = cdtime();
  il->sent = 0;
  il->next = interface_list_head;
  interface_list_head = il;

  DEBUG("connectivity plugin: added interface %s", interface2);

  return il;
}

static int connectivity_link_state(struct nlmsghdr *msg) {
  pthread_mutex_lock(&connectivity_data_lock);

  struct nlattr *attr;
  struct ifinfomsg *ifi = mnl_nlmsg_get_payload(msg);

  /* Scan attribute list for device name. */
  mnl_attr_for_each(attr, msg, sizeof(*ifi)) {
    if (mnl_attr_get_type(attr) != IFLA_IFNAME)
      continue;

    if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0) {
      ERROR("connectivity plugin: connectivity_link_state: IFLA_IFNAME "
            "mnl_attr_validate "
            "failed.");
      pthread_mutex_unlock(&connectivity_data_lock);
      return MNL_CB_ERROR;
    }

    const char *dev = mnl_attr_get_str(attr);

    // Check the list of interfaces we should monitor, if we've chosen
    // a subset.  If we don't care about this one, abort.
    if (ignorelist_match(ignorelist, dev) != 0) {
      DEBUG("connectivity plugin: Ignoring link state change for unmonitored "
            "interface: %s",
            dev);
      break;
    }

    interface_list_t *il = NULL;

    for (il = interface_list_head; il != NULL; il = il->next)
      if (strcmp(dev, il->interface) == 0)
        break;

    if (il == NULL) {
      // We haven't encountered this interface yet, so add it to the linked list
      il = add_interface(dev, LINK_STATE_UNKNOWN, LINK_STATE_UNKNOWN);

      if (il == NULL) {
        ERROR("connectivity plugin: unable to add interface %s during "
              "connectivity_link_state",
              dev);
        return MNL_CB_ERROR;
      }
    }

    uint32_t prev_status = il->status;
    il->status =
        ((ifi->ifi_flags & IFF_RUNNING) ? LINK_STATE_UP : LINK_STATE_DOWN);
    il->timestamp = cdtime();

    // If the new status is different than the previous status,
    // store the previous status and set sent to zero, and set the
    // global flag to indicate there are statuses to dispatch
    if (il->status != prev_status) {
      il->prev_status = prev_status;
      il->sent = 0;
      statuses_to_send = 1;
    }

    DEBUG("connectivity plugin (%llu): Interface %s status is now %s",
          (unsigned long long)il->timestamp, dev,
          ((ifi->ifi_flags & IFF_RUNNING) ? "UP" : "DOWN"));

    // no need to loop again, we found the interface name attr
    // (otherwise the first if-statement in the loop would
    // have moved us on with 'continue')
    break;
  }

  pthread_mutex_unlock(&connectivity_data_lock);

  return 0;
}

static int msg_handler(struct nlmsghdr *msg) {
  // We are only interested in RTM_NEWLINK messages
  if (msg->nlmsg_type != RTM_NEWLINK) {
    return 0;
  }
  return connectivity_link_state(msg);
}

static int read_event(int (*msg_handler)(struct nlmsghdr *)) {
  int ret = 0;
  int recv_flags = MSG_DONTWAIT;

  if (nl_sock == -1 || msg_handler == NULL)
    return EINVAL;

  while (42) {
    pthread_mutex_lock(&connectivity_threads_lock);

    if (connectivity_netlink_thread_loop <= 0) {
      pthread_mutex_unlock(&connectivity_threads_lock);
      return ret;
    }

    pthread_mutex_unlock(&connectivity_threads_lock);

    char buf[4096];
    int status = recv(nl_sock, buf, sizeof(buf), recv_flags);

    if (status < 0) {

      // If there were no more messages to drain from the socket,
      // then signal the dequeue thread and allow it to dispatch
      // any saved interface status changes.  Then continue, but
      // block and wait for new messages
      if (errno == EWOULDBLOCK || errno == EAGAIN) {
        pthread_cond_signal(&connectivity_cond);

        recv_flags = 0;
        continue;
      }

      if (errno == EINTR) {
        // Interrupt, so just continue and try again
        continue;
      }

      /* Anything else is an error */
      ERROR("connectivity plugin: read_event: Error recv: %d", status);
      return status;
    }

    // Message received successfully, so we'll stop blocking on the
    // receive call for now (until we get a "would block" error, which
    // will be handled above)
    recv_flags = MSG_DONTWAIT;

    if (status == 0) {
      DEBUG("connectivity plugin: read_event: EOF");
    }

    /* We need to handle more than one message per 'recvmsg' */
    for (struct nlmsghdr *h = (struct nlmsghdr *)buf;
         NLMSG_OK(h, (unsigned int)status); h = NLMSG_NEXT(h, status)) {
      /* Finish reading */
      if (h->nlmsg_type == NLMSG_DONE)
        return ret;

      /* Message is some kind of error */
      if (h->nlmsg_type == NLMSG_ERROR) {
        struct nlmsgerr *l_err = (struct nlmsgerr *)NLMSG_DATA(h);
        ERROR("connectivity plugin: read_event: Message is an error: %d",
              l_err->error);
        return -1; // Error
      }

      /* Call message handler */
      if (msg_handler) {
        ret = (*msg_handler)(h);
        if (ret < 0) {
          ERROR("connectivity plugin: read_event: Message handler error %d",
                ret);
          return ret;
        }
      } else {
        ERROR("connectivity plugin: read_event: Error NULL message handler");
        return -1;
      }
    }
  }

  return ret;
}

static void connectivity_dispatch_notification(const char *interface,
                                               gauge_t value, gauge_t old_value,
                                               cdtime_t timestamp) {

  notification_t n = {
      .severity = (value == LINK_STATE_UP ? NOTIF_OKAY : NOTIF_FAILURE),
      .time = cdtime(),
      .plugin = "connectivity",
      .type = "gauge",
      .type_instance = "interface_status",
  };

  sstrncpy(n.host, hostname_g, sizeof(n.host));
  sstrncpy(n.plugin_instance, interface, sizeof(n.plugin_instance));

  char *buf = NULL;

  gen_message_payload(value, old_value, interface, timestamp, &buf);

  int status = plugin_notification_meta_add_string(&n, "ves", buf);

  if (status < 0) {
    sfree(buf);
    ERROR("connectivity plugin: unable to set notification VES metadata: %s",
          STRERRNO);
    return;
  }

  DEBUG("connectivity plugin: notification VES metadata: %s",
        n.meta->nm_value.nm_string);

  DEBUG("connectivity plugin: dispatching state %d for interface %s",
        (int)value, interface);

  plugin_dispatch_notification(&n);
  plugin_notification_meta_free(n.meta);

  // strdup'd in gen_message_payload
  if (buf != NULL)
    sfree(buf);
}

// NOTE: Caller MUST hold connectivity_data_lock when calling this function
static void send_interface_status() {
  for (interface_list_t *il = interface_list_head; il != NULL;
       il = il->next) /* {{{ */
  {
    uint32_t status = il->status;
    uint32_t prev_status = il->prev_status;
    uint32_t sent = il->sent;

    if (status != prev_status && sent == 0) {
      connectivity_dispatch_notification(il->interface, status, prev_status,
                                         il->timestamp);
      il->sent = 1;
    }
  } /* }}} for (il = interface_list_head; il != NULL; il = il->next) */

  statuses_to_send = 0;
}

static void read_interface_status() /* {{{ */
{
  pthread_mutex_lock(&connectivity_data_lock);

  // If we don't have any interface statuses to dispatch,
  // then we wait until signalled
  if (!statuses_to_send)
    pthread_cond_wait(&connectivity_cond, &connectivity_data_lock);

  send_interface_status();

  pthread_mutex_unlock(&connectivity_data_lock);
} /* }}} int *read_interface_status */

static void *connectivity_netlink_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&connectivity_threads_lock);

  while (connectivity_netlink_thread_loop > 0) {
    pthread_mutex_unlock(&connectivity_threads_lock);

    int status = read_event(msg_handler);

    pthread_mutex_lock(&connectivity_threads_lock);

    if (status < 0) {
      connectivity_netlink_thread_error = 1;
      break;
    }
  } /* while (connectivity_netlink_thread_loop > 0) */

  pthread_mutex_unlock(&connectivity_threads_lock);

  return (void *)0;
} /* }}} void *connectivity_netlink_thread */

static void *connectivity_dequeue_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&connectivity_threads_lock);

  while (connectivity_dequeue_thread_loop > 0) {
    pthread_mutex_unlock(&connectivity_threads_lock);

    read_interface_status();

    pthread_mutex_lock(&connectivity_threads_lock);
  } /* while (connectivity_dequeue_thread_loop > 0) */

  pthread_mutex_unlock(&connectivity_threads_lock);

  return ((void *)0);
} /* }}} void *connectivity_dequeue_thread */

static int nl_connect() {
  struct sockaddr_nl sa_nl = {
      .nl_family = AF_NETLINK,
      .nl_groups = RTMGRP_LINK,
      .nl_pid = getpid(),
  };

  nl_sock = socket(AF_NETLINK, SOCK_DGRAM, NETLINK_ROUTE);
  if (nl_sock == -1) {
    ERROR("connectivity plugin: socket open failed: %s", STRERRNO);
    return -1;
  }

  int rc = bind(nl_sock, (struct sockaddr *)&sa_nl, sizeof(sa_nl));
  if (rc == -1) {
    ERROR("connectivity plugin: socket bind failed: %s", STRERRNO);
    close(nl_sock);
    nl_sock = -1;
    return -1;
  }

  return 0;
}

static int start_netlink_thread(void) /* {{{ */
{
  pthread_mutex_lock(&connectivity_threads_lock);

  if (connectivity_netlink_thread_loop != 0) {
    pthread_mutex_unlock(&connectivity_threads_lock);
    return 0;
  }

  connectivity_netlink_thread_loop = 1;
  connectivity_netlink_thread_error = 0;

  int status;

  if (nl_sock == -1) {
    status = nl_connect();

    if (status != 0) {
      pthread_mutex_unlock(&connectivity_threads_lock);
      return status;
    }
  }

  status = plugin_thread_create(&connectivity_netlink_thread_id,
                                connectivity_netlink_thread,
                                /* arg = */ (void *)0, "connectivity");
  if (status != 0) {
    connectivity_netlink_thread_loop = 0;
    ERROR("connectivity plugin: Starting thread failed.");
    pthread_mutex_unlock(&connectivity_threads_lock);

    int status2 = close(nl_sock);

    if (status2 != 0) {
      ERROR("connectivity plugin: failed to close socket %d: %d (%s)", nl_sock,
            status2, STRERRNO);
    }

    nl_sock = -1;

    return -1;
  }

  pthread_mutex_unlock(&connectivity_threads_lock);

  return status;
}

static int start_dequeue_thread(void) /* {{{ */
{
  pthread_mutex_lock(&connectivity_threads_lock);

  if (connectivity_dequeue_thread_loop != 0) {
    pthread_mutex_unlock(&connectivity_threads_lock);
    return 0;
  }

  connectivity_dequeue_thread_loop = 1;

  int status = plugin_thread_create(&connectivity_dequeue_thread_id,
                                    connectivity_dequeue_thread,
                                    /* arg = */ (void *)0, "connectivity");
  if (status != 0) {
    connectivity_dequeue_thread_loop = 0;
    ERROR("connectivity plugin: Starting dequeue thread failed.");
    pthread_mutex_unlock(&connectivity_threads_lock);
    return -1;
  }

  pthread_mutex_unlock(&connectivity_threads_lock);

  return status;
} /* }}} int start_dequeue_thread */

static int start_threads(void) /* {{{ */
{
  int status = start_netlink_thread();
  int status2 = start_dequeue_thread();

  if (status != 0)
    return status;
  else
    return status2;
} /* }}} int start_threads */

static int stop_netlink_thread(int shutdown) /* {{{ */
{
  int socket_status;

  if (nl_sock != -1) {
    socket_status = close(nl_sock);
    if (socket_status != 0) {
      ERROR("connectivity plugin: failed to close socket %d: %d (%s)", nl_sock,
            socket_status, STRERRNO);
    }

    nl_sock = -1;
  } else
    socket_status = 0;

  pthread_mutex_lock(&connectivity_threads_lock);

  if (connectivity_netlink_thread_loop == 0) {
    pthread_mutex_unlock(&connectivity_threads_lock);
    // Thread has already been terminated, nothing more to attempt
    return socket_status;
  }

  // Set thread termination status
  connectivity_netlink_thread_loop = 0;
  pthread_mutex_unlock(&connectivity_threads_lock);

  // Let threads waiting on access to the interface list know to move
  // on such that they'll see the thread's termination status
  pthread_cond_broadcast(&connectivity_cond);

  int thread_status;

  if (shutdown == 1) {
    // Since the thread is blocking, calling pthread_join
    // doesn't actually succeed in stopping it.  It will stick around
    // until a NETLINK message is received on the socket (at which
    // it will realize that "connectivity_netlink_thread_loop" is 0 and will
    // break out of the read loop and be allowed to die).  This is
    // fine when the process isn't supposed to be exiting, but in
    // the case of a process shutdown, we don't want to have an
    // idle thread hanging around.  Calling pthread_cancel here in
    // the case of a shutdown is just assures that the thread is
    // gone and that the process has been fully terminated.

    DEBUG("connectivity plugin: Canceling netlink thread for process shutdown");

    thread_status = pthread_cancel(connectivity_netlink_thread_id);

    if (thread_status != 0 && thread_status != ESRCH) {
      ERROR("connectivity plugin: Unable to cancel netlink thread: %d",
            thread_status);
      thread_status = -1;
    } else
      thread_status = 0;
  } else {
    thread_status =
        pthread_join(connectivity_netlink_thread_id, /* return = */ NULL);
    if (thread_status != 0 && thread_status != ESRCH) {
      ERROR("connectivity plugin: Stopping netlink thread failed: %d",
            thread_status);
      thread_status = -1;
    } else
      thread_status = 0;
  }

  pthread_mutex_lock(&connectivity_threads_lock);
  memset(&connectivity_netlink_thread_id, 0,
         sizeof(connectivity_netlink_thread_id));
  connectivity_netlink_thread_error = 0;
  pthread_mutex_unlock(&connectivity_threads_lock);

  DEBUG("connectivity plugin: Finished requesting stop of netlink thread");

  if (socket_status != 0)
    return socket_status;
  else
    return thread_status;
}

static int stop_dequeue_thread() /* {{{ */
{
  pthread_mutex_lock(&connectivity_threads_lock);

  if (connectivity_dequeue_thread_loop == 0) {
    pthread_mutex_unlock(&connectivity_threads_lock);
    return -1;
  }

  // Set thread termination status
  connectivity_dequeue_thread_loop = 0;
  pthread_mutex_unlock(&connectivity_threads_lock);

  // Let threads waiting on access to the interface list know to move
  // on such that they'll see the threads termination status
  pthread_cond_broadcast(&connectivity_cond);

  // Calling pthread_cancel here just assures that the thread is
  // gone and that the process has been fully terminated.

  DEBUG("connectivity plugin: Canceling dequeue thread for process shutdown");

  int status = pthread_cancel(connectivity_dequeue_thread_id);

  if (status != 0 && status != ESRCH) {
    ERROR("connectivity plugin: Unable to cancel dequeue thread: %d", status);
    status = -1;
  } else
    status = 0;

  pthread_mutex_lock(&connectivity_threads_lock);
  memset(&connectivity_dequeue_thread_id, 0,
         sizeof(connectivity_dequeue_thread_id));
  pthread_mutex_unlock(&connectivity_threads_lock);

  DEBUG("connectivity plugin: Finished requesting stop of dequeue thread");

  return status;
} /* }}} int stop_dequeue_thread */

static int stop_threads() /* {{{ */
{
  int status = stop_netlink_thread(1);
  int status2 = stop_dequeue_thread();

  if (status != 0)
    return status;
  else
    return status2;
} /* }}} int stop_threads */

static int connectivity_init(void) /* {{{ */
{
  if (monitor_all_interfaces) {
    NOTICE("connectivity plugin: No interfaces have been selected, so all will "
           "be monitored");
  }

  return start_threads();
} /* }}} int connectivity_init */

static int connectivity_config(const char *key, const char *value) /* {{{ */
{
  if (ignorelist == NULL) {
    ignorelist = ignorelist_create(/* invert = */ 1);

    if (ignorelist == NULL)
      return -1;
  }

  if (strcasecmp(key, "Interface") == 0) {
    ignorelist_add(ignorelist, value);
    monitor_all_interfaces = 0;
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    int invert = 1;
    if (IS_TRUE(value))
      invert = 0;
    ignorelist_set_invert(ignorelist, invert);
  } else {
    return -1;
  }

  return 0;
} /* }}} int connectivity_config */

static int connectivity_read(void) /* {{{ */
{
  pthread_mutex_lock(&connectivity_threads_lock);

  if (connectivity_netlink_thread_error != 0) {

    pthread_mutex_unlock(&connectivity_threads_lock);

    ERROR("connectivity plugin: The netlink thread had a problem. Restarting "
          "it.");

    stop_netlink_thread(0);

    for (interface_list_t *il = interface_list_head; il != NULL;
         il = il->next) {
      il->status = LINK_STATE_UNKNOWN;
      il->prev_status = LINK_STATE_UNKNOWN;
      il->sent = 0;
    }

    start_netlink_thread();

    return -1;
  } /* if (connectivity_netlink_thread_error != 0) */

  pthread_mutex_unlock(&connectivity_threads_lock);

  return 0;
} /* }}} int connectivity_read */

static int connectivity_shutdown(void) /* {{{ */
{
  DEBUG("connectivity plugin: Shutting down thread.");

  int status = stop_threads();

  interface_list_t *il = interface_list_head;
  while (il != NULL) {
    interface_list_t *il_next;

    il_next = il->next;

    sfree(il->interface);
    sfree(il);

    il = il_next;
  }

  ignorelist_free(ignorelist);

  return status;
} /* }}} int connectivity_shutdown */

void module_register(void) {
  plugin_register_config("connectivity", connectivity_config, config_keys,
                         config_keys_num);
  plugin_register_init("connectivity", connectivity_init);
  plugin_register_read("connectivity", connectivity_read);
  plugin_register_shutdown("connectivity", connectivity_shutdown);
} /* void module_register */
