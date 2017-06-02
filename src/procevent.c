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
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <libmnl/libmnl.h>
#include <linux/connector.h>
#include <linux/cn_proc.h>
#include <signal.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <dirent.h>

#define PROCEVENT_EXITED 0
#define PROCEVENT_FIELDS 3  // pid, status, extra
#define BUFSIZE 512
#define PROCDIR "/proc"

/*
 * Private data types
 */

typedef struct {
    int head;
    int tail;
    int maxLen;
    int ** buffer;
} circbuf_t;

struct processlist_s {
  char *process;

  uint32_t pid;

  struct processlist_s *next;
};
typedef struct processlist_s processlist_t;

/*
 * Private variables
 */

static int procevent_thread_loop = 0;
static int procevent_thread_error = 0;
static pthread_t procevent_thread_id;
static pthread_mutex_t procevent_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t procevent_cond = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t procevent_list_lock = PTHREAD_MUTEX_INITIALIZER;
static int nl_sock = -1;
static int buffer_length;
static _Bool fuzzy_names = 0;
static circbuf_t ring;
static processlist_t *processlist_head = NULL;

static const char *config_keys[] = { "BufferLength", "Process", "FuzzyNames" };
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

/*
 * Private functions
 */

static processlist_t * process_map_check(int pid, char * process)
{
  processlist_t *pl;

  pthread_mutex_lock(&procevent_list_lock);

  for (pl = processlist_head; pl != NULL; pl = pl->next)
  {
    int match_pid = 0;
    int match_process = 0;
    int match = 0;

    if (pid > 0)
    {
      if (pl->pid == pid)
        match_pid = 1;
    }

    if (process != NULL)
    {
      if (strcmp(pl->process, process) == 0)
        match_process = 1;
    }

    if (pid > 0 && process == NULL && match_pid == 1)
      match = 1;
    else if (pid < 0 && process != NULL && match_process == 1)
      match = 1;
    else if (pid > 0 && process != NULL && match_pid == 1 && match_process == 1)
      match = 1;

    if (match == 1)
    {
      pthread_mutex_unlock(&procevent_list_lock);
      return pl;
    }
  }

  pthread_mutex_unlock(&procevent_list_lock);

  return NULL;
}

static int process_map_refresh(void)
{
  DIR *proc;

  errno = 0;
  proc = opendir(PROCDIR);
  if (proc == NULL) {
    char errbuf[1024];
    ERROR("procevent plugin: fopen (%s): %s", PROCDIR,
          sstrerror(errno, errbuf, sizeof(errbuf)));
    return -1;
  }

  while (42) {
    struct dirent *dent;
    int len;
    char file[BUFSIZE];

    FILE *fh;
    char buffer[BUFSIZE];

    struct stat statbuf;

    int status;

    errno = 0;
    dent = readdir(proc);
    if (dent == NULL) {
      char errbuf[4096];

      if (errno == 0) /* end of directory */
        break;

      ERROR("procevent plugin: failed to read directory %s: %s", PROCDIR,
            sstrerror(errno, errbuf, sizeof(errbuf)));
      closedir(proc);
      return -1;
    }

    if (dent->d_name[0] == '.')
      continue;

    len = ssnprintf(file, sizeof(file), PROCDIR "/%s", dent->d_name);
    if ((len < 0) || (len >= BUFSIZE))
      continue;

    status = stat(file, &statbuf);
    if (status != 0) {
      char errbuf[4096];
      WARNING("procevent plugin: stat (%s) failed: %s", file,
              sstrerror(errno, errbuf, sizeof(errbuf)));
      continue;
    }

    if (!S_ISDIR(statbuf.st_mode))
      continue;

    len = ssnprintf(file, sizeof(file), PROCDIR "/%s/comm", dent->d_name);
    if ((len < 0) || (len >= BUFSIZE))
      continue;

    int not_number = 0;

    for (int i = 0; i < strlen(dent->d_name); i++)
    {
        if (!isdigit(dent->d_name[i]))
        {
          not_number = 1;
          break;
        }
    }

    if (not_number != 0)
      continue;

    if (NULL == (fh = fopen(file, "r"))) {
      char errbuf[1024];
      ERROR("procevent: cannot open '%s': %s", file,
            sstrerror(errno, errbuf, sizeof(errbuf)));
      return (-1);
    }

    fscanf(fh, "%[^\n]", buffer);
        
    //
    // Go through the processlist linked list and look for each process name
    // in /proc/<pid>/comm.  If found:
    // 1. If pl->pid is -1, then set pl->pid to <pid>
    // 2. If pl->pid is not -1, then another <process name> process was already
    //    found.  If <pid> == pl->pid, this is an old match, so just continue.
    //    If the <pid> is different, however,  make a new processlist_t and 
    //    associate <pid> with it (with the same process name as the existing).
    //

    pthread_mutex_lock(&procevent_list_lock);

    int this_pid = atoi(dent->d_name);
    processlist_t *pl;

    for (pl = processlist_head; pl != NULL; pl = pl->next)
    {
      if (strcmp(buffer, pl->process) == 0)
      {
        INFO("procevent plugin: process %d name match for %s", this_pid, pl->process);

        if (pl->pid == -1)
          pl->pid = this_pid;
        else if (pl->pid != this_pid) {
          processlist_t *pl;
          char *process;

          pl = malloc(sizeof(*pl));
          if (pl == NULL) {
            char errbuf[1024];
            ERROR("procevent plugin: malloc failed during process_map_refresh: %s",
                  sstrerror(errno, errbuf, sizeof(errbuf)));
            return (1);
          }

          process = strdup(pl->process);
          if (process == NULL) {
            char errbuf[1024];
            sfree(pl);
            ERROR("procevent plugin: strdup failed process_map_refresh: %s",
                  sstrerror(errno, errbuf, sizeof(errbuf)));
            return (1);
          }

          pl->process = process;
          pl->pid = this_pid;
          pl->next = processlist_head;
          processlist_head = pl;
        }

        // if this_pid is equal to pl->pid, then we've already
        // mapped this process
        break;
      }
    }

    pthread_mutex_unlock(&procevent_list_lock);

    if (fh != NULL) {
      fclose(fh);
      fh = NULL;
    }
  }

  closedir(proc);

  return 0;
}

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
        ERROR("procevent plugin: subscribing to netlink process events failed.");
        return -1;
    }

    return 0;
}

static int read_event()
{
    int status;
    int ret = 0;
    processlist_t * pl = NULL;
    int proc_status = -1;
    int proc_extra = -1;
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
          // printf("fork: parent tid=%d pid=%d -> child tid=%d pid=%d\n",
          //         nlcn_msg.proc_ev.event_data.fork.parent_pid,
          //         nlcn_msg.proc_ev.event_data.fork.parent_tgid,
          //         nlcn_msg.proc_ev.event_data.fork.child_pid,
          //         nlcn_msg.proc_ev.event_data.fork.child_tgid);
          break;
      case PROC_EVENT_EXEC:
          // printf("exec: tid=%d pid=%d\n",
          //         nlcn_msg.proc_ev.event_data.exec.process_pid,
          //         nlcn_msg.proc_ev.event_data.exec.process_tgid);
          break;
      case PROC_EVENT_UID:
          // printf("uid change: tid=%d pid=%d from %d to %d\n",
          //         nlcn_msg.proc_ev.event_data.id.process_pid,
          //         nlcn_msg.proc_ev.event_data.id.process_tgid,
          //         nlcn_msg.proc_ev.event_data.id.r.ruid,
          //         nlcn_msg.proc_ev.event_data.id.e.euid);
          break;
      case PROC_EVENT_GID:
          // printf("gid change: tid=%d pid=%d from %d to %d\n",
          //         nlcn_msg.proc_ev.event_data.id.process_pid,
          //         nlcn_msg.proc_ev.event_data.id.process_tgid,
          //         nlcn_msg.proc_ev.event_data.id.r.rgid,
          //         nlcn_msg.proc_ev.event_data.id.e.egid);
          break;
      case PROC_EVENT_EXIT:
          // 
          // Check whether we are interested in this process. 
          //

          pl = process_map_check(nlcn_msg.proc_ev.event_data.exit.process_pid, NULL);

          if (pl != NULL)
          {
            INFO("procevent plugin: pid %d EXIT event match for process %s", pl->pid, pl->process);

            proc_status = PROCEVENT_EXITED;
            proc_extra = nlcn_msg.proc_ev.event_data.exit.exit_code;
          }
          break;
      default:
          break;
    }

    // If we're interested in this event, then place the event in 
    // the ring buffer for consumption by the main polling thread.

    if (proc_status != -1 && pl != NULL)
    {
      pthread_mutex_unlock(&procevent_lock);

      int next = ring.head + 1;
      if (next >= ring.maxLen)
        next = 0;

      if (next == ring.tail)
      {
        WARNING("procevent plugin: ring buffer full");
      } else {

        struct timeval tv;

        gettimeofday(&tv, NULL);

        unsigned long long millisecondsSinceEpoch =
        (unsigned long long)(tv.tv_sec) * 1000 +
        (unsigned long long)(tv.tv_usec) / 1000;
        
        INFO("procevent plugin (%llu): Process %d (%s) status is now %s", millisecondsSinceEpoch, pl->pid, pl->process, (proc_status == PROCEVENT_EXITED ? "EXITED" : "STARTED"));

        ring.buffer[ring.head][0] = pl->pid;
        ring.buffer[ring.head][1] = proc_status;
        ring.buffer[ring.head][2] = proc_extra;

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

    usleep(1000);

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

  if (processlist_head == NULL) {
    NOTICE("procevent plugin: No processes have been configured.");
    return (-1);
  }

  ring.head = 0;
  ring.tail = 0;
  ring.maxLen = buffer_length;
  ring.buffer = (int **) malloc(buffer_length * sizeof(int *));

  for (int i = 0; i < buffer_length; i ++)
  {
    ring.buffer[i] = (int*) malloc(PROCEVENT_FIELDS * sizeof(int));
  }

  status = process_map_refresh();

  if (status == -1)
  {
    ERROR("procevent plugin: Initial process mapping failed.");
    return (-1);
  }

  return (start_thread());
} /* }}} int procevent_init */

static int procevent_config(const char *key, const char *value) /* {{{ */
{
  if (strcasecmp(key, "BufferLength") == 0) {
    buffer_length = atoi(value);
  } else if (strcasecmp(key, "Process") == 0) {

    processlist_t *pl;
    char *process;

    pl = malloc(sizeof(*pl));
    if (pl == NULL) {
      char errbuf[1024];
      ERROR("procevent plugin: malloc failed during procevent_config: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
      return (1);
    }

    process = strdup(value);
    if (process == NULL) {
      char errbuf[1024];
      sfree(pl);
      ERROR("procevent plugin: strdup failed procevent_config: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
      return (1);
    }

    pl->process = process;
    pl->pid = -1;
    pl->next = processlist_head;
    processlist_head = pl;
  } else if (strcasecmp(key, "FuzzyNames") == 0) {
    fuzzy_names = 1;
  } else {
    return (-1);
  }

  return (0);
} /* }}} int procevent_config */

static void submit(int pid, const char *type, /* {{{ */
                   gauge_t value, const char *process) {
  char pid_str[10];

  snprintf(pid_str, 10, "%d", pid);

  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "procevent", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, process, sizeof(vl.plugin_instance));
  sstrncpy(vl.type_instance, pid_str, sizeof(vl.type_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  struct timeval tv;

  gettimeofday(&tv, NULL);

  unsigned long long millisecondsSinceEpoch =
  (unsigned long long)(tv.tv_sec) * 1000 +
  (unsigned long long)(tv.tv_usec) / 1000;

  INFO("procevent plugin (%llu): dispatching state %d for PID %d (%s)", millisecondsSinceEpoch, (int) value, pid, process);

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

    processlist_t * pl = process_map_check(ring.buffer[ring.tail][0], NULL);

    submit(ring.buffer[ring.tail][0], "gauge", ring.buffer[ring.tail][1], (pl != NULL ? pl->process : NULL));

    ring.tail = next;
  }

  pthread_mutex_unlock(&procevent_lock);

  return (0);
} /* }}} int procevent_read */

static int procevent_shutdown(void) /* {{{ */
{
  //int status = 0;
  processlist_t *pl;

  INFO("procevent plugin: Shutting down thread.");
  if (stop_thread(1) < 0)
    return (-1);

  for (int i = 0; i < buffer_length; i ++)
  {
    free(ring.buffer[i]);
  }

  free(ring.buffer);

  pl = processlist_head;
  while (pl != NULL) {
    processlist_t *pl_next;

    pl_next = pl->next;

    sfree(pl->process);
    sfree(pl);

    pl = pl_next;
  }

  return (0);
} /* }}} int procevent_shutdown */

void module_register(void) {
  plugin_register_config("procevent", procevent_config, config_keys, config_keys_num);
  plugin_register_init("procevent", procevent_init);
  plugin_register_read("procevent", procevent_read);
  plugin_register_shutdown("procevent", procevent_shutdown);
} /* void module_register */