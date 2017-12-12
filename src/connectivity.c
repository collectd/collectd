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

#include "common.h"
#include "plugin.h"
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

#define MYPROTO NETLINK_ROUTE

/*
 * Private data types
 */
struct interfacelist_s {
  char *interface;

  uint32_t status;
  uint32_t prev_status;
  uint32_t sent;
  uint32_t sec;
  uint32_t usec;

  struct interfacelist_s *next;
};
typedef struct interfacelist_s interfacelist_t;

/*
 * Private variables
 */
static interfacelist_t *interfacelist_head = NULL;

static int connectivity_thread_loop = 0;
static int connectivity_thread_error = 0;
static pthread_t connectivity_thread_id;
static pthread_mutex_t connectivity_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t connectivity_cond = PTHREAD_COND_INITIALIZER;
static struct mnl_socket *sock;

static const char *config_keys[] = {"Interface"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

/*
 * Private functions
 */

static int connectivity_link_state(struct nlmsghdr *msg) {
  int retval = 0;
  struct ifinfomsg *ifi = mnl_nlmsg_get_payload(msg);
  struct nlattr *attr;
  const char *dev = NULL;

  pthread_mutex_lock(&connectivity_lock);

  interfacelist_t *il;

  /* Scan attribute list for device name. */
  mnl_attr_for_each(attr, msg, sizeof(*ifi)) {
    if (mnl_attr_get_type(attr) != IFLA_IFNAME)
      continue;

    if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0) {
      ERROR("connectivity plugin: connectivity_link_state: IFLA_IFNAME "
            "mnl_attr_validate "
            "failed.");
      pthread_mutex_unlock(&connectivity_lock);
      return MNL_CB_ERROR;
    }

    dev = mnl_attr_get_str(attr);

    for (il = interfacelist_head; il != NULL; il = il->next)
      if (strcmp(dev, il->interface) == 0)
        break;

    if (il == NULL) {
      INFO("connectivity plugin: Ignoring link state change for unmonitored "
           "interface: %s",
           dev);
    } else {
      uint32_t prev_status;
      struct timeval tv;

      gettimeofday(&tv, NULL);

      unsigned long long millisecondsSinceEpoch =
          (unsigned long long)(tv.tv_sec) * 1000 +
          (unsigned long long)(tv.tv_usec) / 1000;

      INFO("connectivity plugin (%llu): Interface %s status is now %s",
           millisecondsSinceEpoch, dev,
           ((ifi->ifi_flags & IFF_RUNNING) ? "UP" : "DOWN"));
      prev_status = il->status;
      il->status = ((ifi->ifi_flags & IFF_RUNNING) ? 1 : 0);
      il->sec = tv.tv_sec;
      il->usec = tv.tv_usec;
      // If the new status is different than the previous status,
      // store the previous status and set sent to zero
      if (il->status != prev_status) {
        il->prev_status = prev_status;
        il->sent = 0;
      }
    }

    // no need to loop again, we found the interface name
    // (otherwise the first if-statement in the loop would
    // have moved us on with 'continue')
    break;
  }

  pthread_mutex_unlock(&connectivity_lock);

  return retval;
}

static int msg_handler(struct nlmsghdr *msg) {
  switch (msg->nlmsg_type) {
  case RTM_NEWADDR:
    break;
  case RTM_DELADDR:
    break;
  case RTM_NEWROUTE:
    break;
  case RTM_DELROUTE:
    break;
  case RTM_NEWLINK:
    connectivity_link_state(msg);
    break;
  case RTM_DELLINK:
    break;
  default:
    ERROR("connectivity plugin: msg_handler: Unknown netlink nlmsg_type %d\n",
          msg->nlmsg_type);
    break;
  }
  return 0;
}

static int read_event(struct mnl_socket *nl,
                      int (*msg_handler)(struct nlmsghdr *)) {
  int status;
  int ret = 0;
  char buf[4096];
  struct nlmsghdr *h;

  if (nl == NULL)
    return ret;

  status = mnl_socket_recvfrom(nl, buf, sizeof(buf));

  if (status < 0) {
    /* Socket non-blocking so bail out once we have read everything */
    if (errno == EWOULDBLOCK || errno == EAGAIN)
      return ret;

    /* Anything else is an error */
    ERROR("connectivity plugin: read_event: Error mnl_socket_recvfrom: %d\n",
          status);
    return status;
  }

  if (status == 0) {
    DEBUG("connectivity plugin: read_event: EOF\n");
  }

  /* We need to handle more than one message per 'recvmsg' */
  for (h = (struct nlmsghdr *)buf; NLMSG_OK(h, (unsigned int)status);
       h = NLMSG_NEXT(h, status)) {
    /* Finish reading */
    if (h->nlmsg_type == NLMSG_DONE)
      return ret;

    /* Message is some kind of error */
    if (h->nlmsg_type == NLMSG_ERROR) {
      ERROR("connectivity plugin: read_event: Message is an error - decode "
            "TBD\n");
      return -1; // Error
    }

    /* Call message handler */
    if (msg_handler) {
      ret = (*msg_handler)(h);
      if (ret < 0) {
        ERROR("connectivity plugin: read_event: Message handler error %d\n",
              ret);
        return ret;
      }
    } else {
      ERROR("connectivity plugin: read_event: Error NULL message handler\n");
      return -1;
    }
  }

  return ret;
}

static void *connectivity_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&connectivity_lock);

  while (connectivity_thread_loop > 0) {
    int status;

    pthread_mutex_unlock(&connectivity_lock);

    status = read_event(sock, msg_handler);

    pthread_mutex_lock(&connectivity_lock);

    if (status < 0) {
      connectivity_thread_error = 1;
      break;
    }

    if (connectivity_thread_loop <= 0)
      break;
  } /* while (connectivity_thread_loop > 0) */

  pthread_mutex_unlock(&connectivity_lock);

  return ((void *)0);
} /* }}} void *connectivity_thread */

static int start_thread(void) /* {{{ */
{
  int status;

  pthread_mutex_lock(&connectivity_lock);

  if (connectivity_thread_loop != 0) {
    pthread_mutex_unlock(&connectivity_lock);
    return (0);
  }

  connectivity_thread_loop = 1;
  connectivity_thread_error = 0;

  if (sock == NULL) {
    sock = mnl_socket_open(NETLINK_ROUTE);
    if (sock == NULL) {
      ERROR(
          "connectivity plugin: connectivity_thread: mnl_socket_open failed.");
      pthread_mutex_unlock(&connectivity_lock);
      return (-1);
    }

    if (mnl_socket_bind(sock, RTMGRP_LINK, MNL_SOCKET_AUTOPID) < 0) {
      ERROR(
          "connectivity plugin: connectivity_thread: mnl_socket_bind failed.");
      pthread_mutex_unlock(&connectivity_lock);
      return (1);
    }
  }

  status = plugin_thread_create(&connectivity_thread_id, /* attr = */ NULL,
                                connectivity_thread,
                                /* arg = */ (void *)0, "connectivity");
  if (status != 0) {
    connectivity_thread_loop = 0;
    ERROR("connectivity plugin: Starting thread failed.");
    pthread_mutex_unlock(&connectivity_lock);
    mnl_socket_close(sock);
    return (-1);
  }

  pthread_mutex_unlock(&connectivity_lock);
  return (0);
} /* }}} int start_thread */

static int stop_thread(int shutdown) /* {{{ */
{
  int status;

  if (sock != NULL)
    mnl_socket_close(sock);

  pthread_mutex_lock(&connectivity_lock);

  if (connectivity_thread_loop == 0) {
    pthread_mutex_unlock(&connectivity_lock);
    return (-1);
  }

  connectivity_thread_loop = 0;
  pthread_cond_broadcast(&connectivity_cond);
  pthread_mutex_unlock(&connectivity_lock);

  if (shutdown == 1) {
    // Since the thread is blocking, calling pthread_join
    // doesn't actually succeed in stopping it.  It will stick around
    // until a NETLINK message is received on the socket (at which
    // it will realize that "connectivity_thread_loop" is 0 and will
    // break out of the read loop and be allowed to die).  This is
    // fine when the process isn't supposed to be exiting, but in
    // the case of a process shutdown, we don't want to have an
    // idle thread hanging around.  Calling pthread_cancel here in
    // the case of a shutdown is just assures that the thread is
    // gone and that the process has been fully terminated.

    INFO("connectivity plugin: Canceling thread for process shutdown");

    status = pthread_cancel(connectivity_thread_id);

    if (status != 0) {
      ERROR("connectivity plugin: Unable to cancel thread: %d", status);
      status = -1;
    }
  } else {
    status = pthread_join(connectivity_thread_id, /* return = */ NULL);
    if (status != 0) {
      ERROR("connectivity plugin: Stopping thread failed.");
      status = -1;
    }
  }

  pthread_mutex_lock(&connectivity_lock);
  memset(&connectivity_thread_id, 0, sizeof(connectivity_thread_id));
  connectivity_thread_error = 0;
  pthread_mutex_unlock(&connectivity_lock);

  INFO("connectivity plugin: Finished requesting stop of thread");

  return (status);
} /* }}} int stop_thread */

static int connectivity_init(void) /* {{{ */
{
  if (interfacelist_head == NULL) {
    NOTICE("connectivity plugin: No interfaces have been configured.");
    return (-1);
  }

  return (start_thread());
} /* }}} int connectivity_init */

static int connectivity_config(const char *key, const char *value) /* {{{ */
{
  if (strcasecmp(key, "Interface") == 0) {
    interfacelist_t *il;
    char *interface;

    il = malloc(sizeof(*il));
    if (il == NULL) {
      char errbuf[1024];
      ERROR("connectivity plugin: malloc failed during connectivity_config: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
      return (1);
    }

    interface = strdup(value);
    if (interface == NULL) {
      char errbuf[1024];
      sfree(il);
      ERROR("connectivity plugin: strdup failed connectivity_config: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
      return (1);
    }

    il->interface = interface;
    il->status = 2; // "unknown"
    il->prev_status = 2;
    il->sent = 0;
    il->next = interfacelist_head;
    interfacelist_head = il;

  } else {
    return (-1);
  }

  return (0);
} /* }}} int connectivity_config */

static void submit(const char *interface, const char *type, /* {{{ */
                   gauge_t value, uint32_t sec, uint32_t usec) {
  value_list_t vl = VALUE_LIST_INIT;
  char hostname[1024];
  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "connectivity", sizeof(vl.plugin));
  sstrncpy(vl.type_instance, interface, sizeof(vl.type_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  // Create metadata to store JSON key-values
  meta_data_t *meta = meta_data_create();

  vl.meta = meta;
  // For latency measurement
  struct timeval tv;
  gettimeofday(&tv, NULL);
  gethostname(hostname, sizeof(hostname));
  char strSec[11];
  char struSec[11];
  snprintf(strSec, sizeof strSec, "%" PRIu32, sec);
  snprintf(struSec, sizeof struSec, "%" PRIu32, usec);
  if (value == 1) {
    meta_data_add_string(meta, "condition", "interface_up");
    meta_data_add_string(meta, "entity", interface);
    meta_data_add_string(meta, "source", hostname);
    meta_data_add_string(meta, "sec", strSec);
    meta_data_add_string(meta, "usec", struSec);
    meta_data_add_string(meta, "dest", "interface_down");
  } else {
    meta_data_add_string(meta, "condition", "interface_down");
    meta_data_add_string(meta, "entity", interface);
    meta_data_add_string(meta, "source", hostname);
    meta_data_add_string(meta, "sec", strSec);
    meta_data_add_string(meta, "usec", struSec);
    meta_data_add_string(meta, "dest", "interface_up");
  }

  plugin_dispatch_values(&vl);
} /* }}} void interface_submit */

static int connectivity_read(void) /* {{{ */
{
  if (connectivity_thread_error != 0) {
    ERROR("connectivity plugin: The interface thread had a problem. Restarting "
          "it.");

    stop_thread(0);

    for (interfacelist_t *il = interfacelist_head; il != NULL; il = il->next) {
      il->status = 2; // signifies "unknown"
      il->prev_status = 2;
      il->sent = 0;
    }

    start_thread();

    return (-1);
  } /* if (connectivity_thread_error != 0) */

  for (interfacelist_t *il = interfacelist_head; il != NULL;
       il = il->next) /* {{{ */
  {
    uint32_t status;
    uint32_t prev_status;
    uint32_t sent;

    /* Locking here works, because the structure of the linked list is only
     * changed during configure and shutdown. */
    pthread_mutex_lock(&connectivity_lock);

    status = il->status;
    prev_status = il->prev_status;
    sent = il->sent;

    if (status != prev_status && sent == 0) {
      submit(il->interface, "gauge", status, il->sec, il->usec);

      il->sent = 1;
    }

    pthread_mutex_unlock(&connectivity_lock);
  } /* }}} for (il = interfacelist_head; il != NULL; il = il->next) */

  return (0);
} /* }}} int connectivity_read */

static int connectivity_shutdown(void) /* {{{ */
{
  interfacelist_t *il;

  INFO("connectivity plugin: Shutting down thread.");
  if (stop_thread(1) < 0)
    return (-1);

  il = interfacelist_head;
  while (il != NULL) {
    interfacelist_t *il_next;

    il_next = il->next;

    sfree(il->interface);
    sfree(il);

    il = il_next;
  }

  return (0);
} /* }}} int connectivity_shutdown */

void module_register(void) {
  plugin_register_config("connectivity", connectivity_config, config_keys,
                         config_keys_num);
  plugin_register_init("connectivity", connectivity_init);
  plugin_register_read("connectivity", connectivity_read);
  plugin_register_shutdown("connectivity", connectivity_shutdown);
} /* void module_register */
