/**
 * collectd - src/procevent.c
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
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_complain.h"

#include <pthread.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <net/if.h>
#include <netinet/in.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <libmnl/libmnl.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#define MYPROTO NETLINK_ROUTE

/*
 * Private data types
 */

typedef struct {
    int head;
    int tail;
    int maxLen;
    int ** buffer;
} circbuf_t;

/*
 * Private variables
 */

static int procevent_thread_loop = 0;
static int procevent_thread_error = 0;
static pthread_t procevent_thread_id;
static pthread_mutex_t procevent_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t procevent_cond = PTHREAD_COND_INITIALIZER;
static int nl_sock = -1;
static int buffer_length;
static circbuf_t ring;

static const char *config_keys[] = { "BufferLength" };
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

/*
 * Private functions
 */

static int nl_connect()
{
    int rc;
    struct sockaddr_nl sa_nl;

    nl_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
    if (nl_sock == -1) {
        ERROR("procevent plugin: socket open failed.");
        return -1;
    }

    sa_nl.nl_family = AF_NETLINK;
    sa_nl.nl_groups = CN_IDX_PROC;
    sa_nl.nl_pid = getpid();

    rc = bind(nl_sock, (struct sockaddr *)&sa_nl, sizeof(sa_nl));
    if (rc == -1) {
        ERROR("procevent plugin: socket bind failed.");
        close(nl_sock);
        return -1;
    }

    return 0;
}

static int set_proc_ev_listen(bool enable)
{
    int rc;
    struct __attribute__ ((aligned(NLMSG_ALIGNTO))) {
        struct nlmsghdr nl_hdr;
        struct __attribute__ ((__packed__)) {
            struct cn_msg cn_msg;
            enum proc_cn_mcast_op cn_mcast;
        };
    } nlcn_msg;

    memset(&nlcn_msg, 0, sizeof(nlcn_msg));
    nlcn_msg.nl_hdr.nlmsg_len = sizeof(nlcn_msg);
    nlcn_msg.nl_hdr.nlmsg_pid = getpid();
    nlcn_msg.nl_hdr.nlmsg_type = NLMSG_DONE;

    nlcn_msg.cn_msg.id.idx = CN_IDX_PROC;
    nlcn_msg.cn_msg.id.val = CN_VAL_PROC;
    nlcn_msg.cn_msg.len = sizeof(enum proc_cn_mcast_op);

    nlcn_msg.cn_mcast = enable ? PROC_CN_MCAST_LISTEN : PROC_CN_MCAST_IGNORE;

    rc = send(nl_sock, &nlcn_msg, sizeof(nlcn_msg), 0);
    if (rc == -1) {
        ERROR("procevent plugin: netlink process event subscribe failed.");
        return -1;
    }

    return 0;
}

static int read_event()
{
    int status;
    int ret = 0;
    int proc_status = -1;
    struct __attribute__ ((aligned(NLMSG_ALIGNTO))) {
        struct nlmsghdr nl_hdr;
        struct __attribute__ ((__packed__)) {
            struct cn_msg cn_msg;
            struct proc_event proc_ev;
        };
    } nlcn_msg;

    if (nl_sock == -1)
      return ret;

    status = recv(nl_sock, &nlcn_msg, sizeof(nlcn_msg), 0);

    if (status == 0) {
        return 0;
    } else if (status == -1) {
        if (errno != EINTR)
        {
          INFO("procevent plugin: socket receive error: %d", errno);
          return -1;
        }
    }

    switch (nlcn_msg.proc_ev.what) {
      case PROC_EVENT_NONE:
          //printf("set mcast listen ok\n");
          break;
      case PROC_EVENT_FORK:
          printf("fork: parent tid=%d pid=%d -> child tid=%d pid=%d\n",
                  nlcn_msg.proc_ev.event_data.fork.parent_pid,
                  nlcn_msg.proc_ev.event_data.fork.parent_tgid,
                  nlcn_msg.proc_ev.event_data.fork.child_pid,
                  nlcn_msg.proc_ev.event_data.fork.child_tgid);
          break;
      case PROC_EVENT_EXEC:
          printf("exec: tid=%d pid=%d\n",
                  nlcn_msg.proc_ev.event_data.exec.process_pid,
                  nlcn_msg.proc_ev.event_data.exec.process_tgid);
          break;
      case PROC_EVENT_UID:
          printf("uid change: tid=%d pid=%d from %d to %d\n",
                  nlcn_msg.proc_ev.event_data.id.process_pid,
                  nlcn_msg.proc_ev.event_data.id.process_tgid,
                  nlcn_msg.proc_ev.event_data.id.r.ruid,
                  nlcn_msg.proc_ev.event_data.id.e.euid);
          break;
      case PROC_EVENT_GID:
          printf("gid change: tid=%d pid=%d from %d to %d\n",
                  nlcn_msg.proc_ev.event_data.id.process_pid,
                  nlcn_msg.proc_ev.event_data.id.process_tgid,
                  nlcn_msg.proc_ev.event_data.id.r.rgid,
                  nlcn_msg.proc_ev.event_data.id.e.egid);
          break;
      case PROC_EVENT_EXIT:
          printf("exit: tid=%d pid=%d exit_code=%d\n",
                  nlcn_msg.proc_ev.event_data.exit.process_pid,
                  nlcn_msg.proc_ev.event_data.exit.process_tgid,
                  nlcn_msg.proc_ev.event_data.exit.exit_code);
          break;
      default:
          printf("unhandled proc event\n");
          break;
    }

    // If we're interested in this event...

    if (proc_status != -1)
    {
      pthread_mutex_unlock(&procevent_lock);

      int next = ring.head + 1;
      if (next >= ring.maxLen)
        next = 0;

      if (next == ring.tail)
      {
        WARNING("procevent plugin: ring buffer full");
      } else {

        // TODO: put data in buffer

        struct timeval tv;

        gettimeofday(&tv, NULL);

        unsigned long long millisecondsSinceEpoch =
        (unsigned long long)(tv.tv_sec) * 1000 +
        (unsigned long long)(tv.tv_usec) / 1000;
        
        INFO("procevent plugin (%llu): Process %d status is now %s", millisecondsSinceEpoch, nlcn_msg.proc_ev.event_data.id.process_tgid, "DEAD");

        // TODO
        //strncpy(ring.buffer[ring.head], buffer, sizeof(buffer));
        ring.head = next;
      }

      pthread_mutex_unlock(&procevent_lock);
    }

    return ret;
}

static void *procevent_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&procevent_lock);

  while (procevent_thread_loop > 0) 
  {
    int status;

    pthread_mutex_unlock(&procevent_lock);

    status = read_event();
    
    pthread_mutex_lock(&procevent_lock);

    if (status < 0)
    {
      procevent_thread_error = 1;
      break;
    }
    
    if (procevent_thread_loop <= 0)
      break;
  } /* while (procevent_thread_loop > 0) */

  pthread_mutex_unlock(&procevent_lock);

  return ((void *)0);
} /* }}} void *procevent_thread */

static int start_thread(void) /* {{{ */
{
  int status;

  pthread_mutex_lock(&procevent_lock);

  if (procevent_thread_loop != 0) {
    pthread_mutex_unlock(&procevent_lock);
    return (0);
  }

  procevent_thread_loop = 1;
  procevent_thread_error = 0;

  status = plugin_thread_create(&procevent_thread_id, /* attr = */ NULL, procevent_thread,
                                /* arg = */ (void *)0, "procevent");
  if (status != 0) {
    procevent_thread_loop = 0;
    ERROR("procevent plugin: Starting thread failed.");
    pthread_mutex_unlock(&procevent_lock);
    return (-1);
  }

  pthread_mutex_unlock(&procevent_lock);
  return (0);
} /* }}} int start_thread */

static int stop_thread(int shutdown) /* {{{ */
{
  int status;

  pthread_mutex_lock(&procevent_lock);

  if (procevent_thread_loop == 0) {
    pthread_mutex_unlock(&procevent_lock);
    return (-1);
  }

  procevent_thread_loop = 0;
  pthread_cond_broadcast(&procevent_cond);
  pthread_mutex_unlock(&procevent_lock);

  if (shutdown == 1)
  {
    // Calling pthread_cancel here in 
    // the case of a shutdown just assures that the thread is 
    // gone and that the process has been fully terminated.

    INFO("procevent plugin: Canceling thread for process shutdown");

    status = pthread_cancel(procevent_thread_id);

    if (status != 0)
    {
      ERROR("procevent plugin: Unable to cancel thread: %d", status);
      status = -1;
    }
  } else {
    status = pthread_join(procevent_thread_id, /* return = */ NULL);
    if (status != 0) {
      ERROR("procevent plugin: Stopping thread failed.");
      status = -1;
    }
  }

  pthread_mutex_lock(&procevent_lock);
  memset(&procevent_thread_id, 0, sizeof(procevent_thread_id));
  procevent_thread_error = 0;
  pthread_mutex_unlock(&procevent_lock);

  INFO("procevent plugin: Finished requesting stop of thread");

  return (status);
} /* }}} int stop_thread */

static int procevent_init(void) /* {{{ */
{
  int status;

  ring.head = 0;
  ring.tail = 0;
  ring.maxLen = buffer_length;
  ring.buffer = (int **) malloc(buffer_length * sizeof(int *));

  for (int i = 0; i < buffer_length; i ++)
  {
    ring.buffer[i] = (int*) malloc(buffer_length * sizeof(int));
  }

  if (nl_sock == -1)
  {
    status = nl_connect();

    if (status != 0)
      return status;

    status = set_proc_ev_listen(true);
    if (status == -1) 
        return status;
  }

  INFO("procevent plugin: socket created and bound");

  return (start_thread());
} /* }}} int procevent_init */

static int procevent_config(const char *key, const char *value) /* {{{ */
{
  if (strcasecmp(key, "BufferLength") == 0) {
    buffer_length = atoi(value);
  } else {
    return (-1);
  }

  return (0);
} /* }}} int procevent_config */

static void submit(int pid, const char *type, /* {{{ */
                   gauge_t value) {
  char pid_str[10];

  snprintf(pid_str, 10, "%d", pid);

  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "procevent", sizeof(vl.plugin));
  sstrncpy(vl.type_instance, pid_str, sizeof(vl.type_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  struct timeval tv;

  gettimeofday(&tv, NULL);

  unsigned long long millisecondsSinceEpoch =
  (unsigned long long)(tv.tv_sec) * 1000 +
  (unsigned long long)(tv.tv_usec) / 1000;

  INFO("procevent plugin (%llu): dispatching state %d for PID %d", millisecondsSinceEpoch, (int) value, pid);

  plugin_dispatch_values(&vl);
} /* }}} void interface_submit */

static int procevent_read(void) /* {{{ */
{
  if (procevent_thread_error != 0) {
    ERROR("procevent plugin: The interface thread had a problem. Restarting it.");

    stop_thread(0);

    start_thread();

    return (-1);
  } /* if (procevent_thread_error != 0) */

  pthread_mutex_lock(&procevent_lock);

  while (ring.head != ring.tail)
  {
    int next = ring.tail + 1;

    if (next >= ring.maxLen)
      next = 0;

    INFO("procevent plugin: reading %d - %d", ring.buffer[ring.tail][0], ring.buffer[ring.tail][1]);

    submit(ring.buffer[ring.tail][0], "gauge", ring.buffer[ring.tail][1]);

    ring.tail = next;
  }

  pthread_mutex_unlock(&procevent_lock);

  return (0);
} /* }}} int procevent_read */

static int procevent_shutdown(void) /* {{{ */
{
  int status = 0;

  INFO("procevent plugin: Shutting down thread.");
  if (stop_thread(1) < 0)
    return (-1);

  if (nl_sock != -1)
  {
    status = close(nl_sock);
    if (status != 0)
    {
      ERROR("procevent plugin: failed to close socket %d: %d (%s)", nl_sock, status, strerror(errno));
      return (-1);
    } else
      nl_sock = -1;
  }

  for (int i = 0; i < buffer_length; i ++)
  {
    free(ring.buffer[i]);
  }

  free(ring.buffer);

  return (0);
} /* }}} int procevent_shutdown */

void module_register(void) {
  plugin_register_config("procevent", procevent_config, config_keys, config_keys_num);
  plugin_register_init("procevent", procevent_init);
  plugin_register_read("procevent", procevent_read);
  plugin_register_shutdown("procevent", procevent_shutdown);
} /* void module_register */