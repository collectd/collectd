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
 *     Andrew Bays <abays at redhat.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"
#include "utils_complain.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <dirent.h>
#include <linux/cn_proc.h>
#include <linux/connector.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <yajl/yajl_common.h>
#include <yajl/yajl_gen.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif
#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
#define HAVE_YAJL_V2 1
#endif

#define PROCEVENT_EXITED 0
#define PROCEVENT_STARTED 1
#define PROCEVENT_FIELDS 3 // pid, status, timestamp
#define BUFSIZE 512
#define PROCDIR "/proc"
#define RBUF_PROC_ID_INDEX 0
#define RBUF_PROC_STATUS_INDEX 1
#define RBUF_TIME_INDEX 2

#define PROCEVENT_DOMAIN_FIELD "domain"
#define PROCEVENT_DOMAIN_VALUE "fault"
#define PROCEVENT_EVENT_ID_FIELD "eventId"
#define PROCEVENT_EVENT_NAME_FIELD "eventName"
#define PROCEVENT_EVENT_NAME_DOWN_VALUE "down"
#define PROCEVENT_EVENT_NAME_UP_VALUE "up"
#define PROCEVENT_LAST_EPOCH_MICROSEC_FIELD "lastEpochMicrosec"
#define PROCEVENT_PRIORITY_FIELD "priority"
#define PROCEVENT_PRIORITY_VALUE "high"
#define PROCEVENT_REPORTING_ENTITY_NAME_FIELD "reportingEntityName"
#define PROCEVENT_REPORTING_ENTITY_NAME_VALUE "collectd procevent plugin"
#define PROCEVENT_SEQUENCE_FIELD "sequence"
#define PROCEVENT_SEQUENCE_VALUE "0"
#define PROCEVENT_SOURCE_NAME_FIELD "sourceName"
#define PROCEVENT_START_EPOCH_MICROSEC_FIELD "startEpochMicrosec"
#define PROCEVENT_VERSION_FIELD "version"
#define PROCEVENT_VERSION_VALUE "1.0"

#define PROCEVENT_ALARM_CONDITION_FIELD "alarmCondition"
#define PROCEVENT_ALARM_INTERFACE_A_FIELD "alarmInterfaceA"
#define PROCEVENT_EVENT_SEVERITY_FIELD "eventSeverity"
#define PROCEVENT_EVENT_SEVERITY_CRITICAL_VALUE "CRITICAL"
#define PROCEVENT_EVENT_SEVERITY_NORMAL_VALUE "NORMAL"
#define PROCEVENT_EVENT_SOURCE_TYPE_FIELD "eventSourceType"
#define PROCEVENT_EVENT_SOURCE_TYPE_VALUE "process"
#define PROCEVENT_FAULT_FIELDS_FIELD "faultFields"
#define PROCEVENT_FAULT_FIELDS_VERSION_FIELD "faultFieldsVersion"
#define PROCEVENT_FAULT_FIELDS_VERSION_VALUE "1.0"
#define PROCEVENT_SPECIFIC_PROBLEM_FIELD "specificProblem"
#define PROCEVENT_SPECIFIC_PROBLEM_DOWN_VALUE "down"
#define PROCEVENT_SPECIFIC_PROBLEM_UP_VALUE "up"
#define PROCEVENT_VF_STATUS_FIELD "vfStatus"
#define PROCEVENT_VF_STATUS_CRITICAL_VALUE "Ready to terminate"
#define PROCEVENT_VF_STATUS_NORMAL_VALUE "Active"

/*
 * Private data types
 */

typedef struct {
  int head;
  int tail;
  int maxLen;
  cdtime_t **buffer;
} circbuf_t;

struct processlist_s {
  char *process;

  long pid;
  int32_t last_status;

  struct processlist_s *next;
};
typedef struct processlist_s processlist_t;

/*
 * Private variables
 */
static ignorelist_t *ignorelist = NULL;

static int procevent_netlink_thread_loop = 0;
static int procevent_netlink_thread_error = 0;
static pthread_t procevent_netlink_thread_id;
static int procevent_dequeue_thread_loop = 0;
static pthread_t procevent_dequeue_thread_id;
static pthread_mutex_t procevent_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t procevent_data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t procevent_cond = PTHREAD_COND_INITIALIZER;
static int nl_sock = -1;
static int buffer_length;
static circbuf_t ring;
static processlist_t *processlist_head = NULL;
static int event_id = 0;

static const char *config_keys[] = {"BufferLength", "Process", "ProcessRegex"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

/*
 * Private functions
 */

static int gen_message_payload(int state, long pid, char *process,
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
  if (yajl_gen_string(g, (u_char *)PROCEVENT_DOMAIN_FIELD,
                      strlen(PROCEVENT_DOMAIN_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)PROCEVENT_DOMAIN_VALUE,
                      strlen(PROCEVENT_DOMAIN_VALUE)) != yajl_gen_status_ok)
    goto err;

  // eventId
  if (yajl_gen_string(g, (u_char *)PROCEVENT_EVENT_ID_FIELD,
                      strlen(PROCEVENT_EVENT_ID_FIELD)) != yajl_gen_status_ok)
    goto err;

  event_id = event_id + 1;
  if (snprintf(json_str, sizeof(json_str), "%d", event_id) < 0) {
    goto err;
  }

  if (yajl_gen_number(g, json_str, strlen(json_str)) != yajl_gen_status_ok) {
    goto err;
  }

  // eventName
  if (yajl_gen_string(g, (u_char *)PROCEVENT_EVENT_NAME_FIELD,
                      strlen(PROCEVENT_EVENT_NAME_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (snprintf(json_str, sizeof(json_str), "process %s (%ld) %s", process, pid,
               (state == 0 ? PROCEVENT_EVENT_NAME_DOWN_VALUE
                           : PROCEVENT_EVENT_NAME_UP_VALUE)) < 0) {
    goto err;
  }

  if (yajl_gen_string(g, (u_char *)json_str, strlen(json_str)) !=
      yajl_gen_status_ok) {
    goto err;
  }

  // lastEpochMicrosec
  if (yajl_gen_string(g, (u_char *)PROCEVENT_LAST_EPOCH_MICROSEC_FIELD,
                      strlen(PROCEVENT_LAST_EPOCH_MICROSEC_FIELD)) !=
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
  if (yajl_gen_string(g, (u_char *)PROCEVENT_PRIORITY_FIELD,
                      strlen(PROCEVENT_PRIORITY_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)PROCEVENT_PRIORITY_VALUE,
                      strlen(PROCEVENT_PRIORITY_VALUE)) != yajl_gen_status_ok)
    goto err;

  // reportingEntityName
  if (yajl_gen_string(g, (u_char *)PROCEVENT_REPORTING_ENTITY_NAME_FIELD,
                      strlen(PROCEVENT_REPORTING_ENTITY_NAME_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)PROCEVENT_REPORTING_ENTITY_NAME_VALUE,
                      strlen(PROCEVENT_REPORTING_ENTITY_NAME_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // sequence
  if (yajl_gen_string(g, (u_char *)PROCEVENT_SEQUENCE_FIELD,
                      strlen(PROCEVENT_SEQUENCE_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_number(g, PROCEVENT_SEQUENCE_VALUE,
                      strlen(PROCEVENT_SEQUENCE_VALUE)) != yajl_gen_status_ok)
    goto err;

  // sourceName
  if (yajl_gen_string(g, (u_char *)PROCEVENT_SOURCE_NAME_FIELD,
                      strlen(PROCEVENT_SOURCE_NAME_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)process, strlen(process)) !=
      yajl_gen_status_ok)
    goto err;

  // startEpochMicrosec
  if (yajl_gen_string(g, (u_char *)PROCEVENT_START_EPOCH_MICROSEC_FIELD,
                      strlen(PROCEVENT_START_EPOCH_MICROSEC_FIELD)) !=
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
  if (yajl_gen_string(g, (u_char *)PROCEVENT_VERSION_FIELD,
                      strlen(PROCEVENT_VERSION_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_number(g, PROCEVENT_VERSION_VALUE,
                      strlen(PROCEVENT_VERSION_VALUE)) != yajl_gen_status_ok)
    goto err;

  // *** END common event header ***

  // *** BEGIN fault fields ***

  if (yajl_gen_string(g, (u_char *)PROCEVENT_FAULT_FIELDS_FIELD,
                      strlen(PROCEVENT_FAULT_FIELDS_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_map_open(g) != yajl_gen_status_ok)
    goto err;

  // alarmCondition
  if (yajl_gen_string(g, (u_char *)PROCEVENT_ALARM_CONDITION_FIELD,
                      strlen(PROCEVENT_ALARM_CONDITION_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (snprintf(json_str, sizeof(json_str), "process %s (%ld) state change",
               process, pid) < 0) {
    goto err;
  }

  if (yajl_gen_string(g, (u_char *)json_str, strlen(json_str)) !=
      yajl_gen_status_ok) {
    goto err;
  }

  // alarmInterfaceA
  if (yajl_gen_string(g, (u_char *)PROCEVENT_ALARM_INTERFACE_A_FIELD,
                      strlen(PROCEVENT_ALARM_INTERFACE_A_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)process, strlen(process)) !=
      yajl_gen_status_ok)
    goto err;

  // eventSeverity
  if (yajl_gen_string(g, (u_char *)PROCEVENT_EVENT_SEVERITY_FIELD,
                      strlen(PROCEVENT_EVENT_SEVERITY_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(
          g,
          (u_char *)(state == 0 ? PROCEVENT_EVENT_SEVERITY_CRITICAL_VALUE
                                : PROCEVENT_EVENT_SEVERITY_NORMAL_VALUE),
          strlen((state == 0 ? PROCEVENT_EVENT_SEVERITY_CRITICAL_VALUE
                             : PROCEVENT_EVENT_SEVERITY_NORMAL_VALUE))) !=
      yajl_gen_status_ok)
    goto err;

  // eventSourceType
  if (yajl_gen_string(g, (u_char *)PROCEVENT_EVENT_SOURCE_TYPE_FIELD,
                      strlen(PROCEVENT_EVENT_SOURCE_TYPE_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(g, (u_char *)PROCEVENT_EVENT_SOURCE_TYPE_VALUE,
                      strlen(PROCEVENT_EVENT_SOURCE_TYPE_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // faultFieldsVersion
  if (yajl_gen_string(g, (u_char *)PROCEVENT_FAULT_FIELDS_VERSION_FIELD,
                      strlen(PROCEVENT_FAULT_FIELDS_VERSION_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (yajl_gen_number(g, PROCEVENT_FAULT_FIELDS_VERSION_VALUE,
                      strlen(PROCEVENT_FAULT_FIELDS_VERSION_VALUE)) !=
      yajl_gen_status_ok)
    goto err;

  // specificProblem
  if (yajl_gen_string(g, (u_char *)PROCEVENT_SPECIFIC_PROBLEM_FIELD,
                      strlen(PROCEVENT_SPECIFIC_PROBLEM_FIELD)) !=
      yajl_gen_status_ok)
    goto err;

  if (snprintf(json_str, sizeof(json_str), "process %s (%ld) %s", process, pid,
               (state == 0 ? PROCEVENT_SPECIFIC_PROBLEM_DOWN_VALUE
                           : PROCEVENT_SPECIFIC_PROBLEM_UP_VALUE)) < 0) {
    goto err;
  }

  if (yajl_gen_string(g, (u_char *)json_str, strlen(json_str)) !=
      yajl_gen_status_ok) {
    goto err;
  }

  // vfStatus
  if (yajl_gen_string(g, (u_char *)PROCEVENT_VF_STATUS_FIELD,
                      strlen(PROCEVENT_VF_STATUS_FIELD)) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_string(
          g,
          (u_char *)(state == 0 ? PROCEVENT_VF_STATUS_CRITICAL_VALUE
                                : PROCEVENT_VF_STATUS_NORMAL_VALUE),
          strlen((state == 0 ? PROCEVENT_VF_STATUS_CRITICAL_VALUE
                             : PROCEVENT_VF_STATUS_NORMAL_VALUE))) !=
      yajl_gen_status_ok)
    goto err;

  // *** END fault fields ***

  // close fault and header fields
  if (yajl_gen_map_close(g) != yajl_gen_status_ok ||
      yajl_gen_map_close(g) != yajl_gen_status_ok)
    goto err;

  if (yajl_gen_get_buf(g, &buf2, &len) != yajl_gen_status_ok)
    goto err;

  *buf = strdup((char *)buf2);

  if (*buf == NULL) {
    ERROR("procevent plugin: strdup failed during gen_message_payload: %s",
          STRERRNO);
    goto err;
  }

  yajl_gen_free(g);

  return 0;

err:
  yajl_gen_free(g);
  ERROR("procevent plugin: gen_message_payload failed to generate JSON");
  return -1;
}

// Does /proc/<pid>/comm contain a process name we are interested in?
// NOTE: Caller MUST hold procevent_data_lock when calling this function
static processlist_t *process_check(long pid) {
  char file[BUFSIZE];

  int len = snprintf(file, sizeof(file), PROCDIR "/%ld/comm", pid);

  if ((len < 0) || (len >= BUFSIZE)) {
    WARNING("procevent process_check: process name too large");
    return NULL;
  }

  FILE *fh;

  if (NULL == (fh = fopen(file, "r"))) {
    // No /proc/<pid>/comm for this pid, just ignore
    DEBUG("procevent plugin: no comm file available for pid %ld", pid);
    return NULL;
  }

  char buffer[BUFSIZE];
  int retval = fscanf(fh, "%[^\n]", buffer);

  if (retval < 0) {
    WARNING("procevent process_check: unable to read comm file for pid %ld",
            pid);
    fclose(fh);
    return NULL;
  }

  // Now that we have the process name in the buffer, check if we are
  // even interested in it
  if (ignorelist_match(ignorelist, buffer) != 0) {
    DEBUG("procevent process_check: ignoring process %s (%ld)", buffer, pid);
    fclose(fh);
    return NULL;
  }

  if (fh != NULL) {
    fclose(fh);
    fh = NULL;
  }

  //
  // Go through the processlist linked list and look for the process name
  // in /proc/<pid>/comm.  If found:
  // 1. If pl->pid is -1, then set pl->pid to <pid> (and return that object)
  // 2. If pl->pid is not -1, then another <process name> process was already
  //    found.  If <pid> == pl->pid, this is an old match, so do nothing.
  //    If the <pid> is different, however, make a new processlist_t and
  //    associate <pid> with it (with the same process name as the existing).
  //

  processlist_t *match = NULL;

  for (processlist_t *pl = processlist_head; pl != NULL; pl = pl->next) {

    int is_match = (strcmp(buffer, pl->process) == 0 ? 1 : 0);

    if (is_match == 1) {
      DEBUG("procevent plugin: process %ld name match for %s", pid, buffer);

      if (pl->pid == pid) {
        // this is a match, and we've already stored the exact pid/name combo
        DEBUG("procevent plugin: found exact match with name %s, PID %ld for "
              "incoming PID %ld",
              pl->process, pl->pid, pid);
        match = pl;
        break;
      } else if (pl->pid == -1) {
        // this is a match, and we've found a candidate processlist_t to store
        // this new pid/name combo
        DEBUG("procevent plugin: reusing pl object with PID %ld for incoming "
              "PID %ld",
              pl->pid, pid);
        pl->pid = pid;
        match = pl;
        break;
      } else if (pl->pid != -1) {
        // this is a match, but another instance of this process has already
        // claimed this pid/name combo,
        // so keep looking
        DEBUG("procevent plugin: found pl object with matching name for "
              "incoming PID %ld, but object is in use by PID %ld",
              pid, pl->pid);
        match = pl;
        continue;
      }
    }
  }

  if (match == NULL ||
      (match != NULL && match->pid != -1 && match->pid != pid)) {
    // if there wasn't an existing match, OR
    // if there was a match but the associated processlist_t object already
    // contained a pid/name combo,
    // then make a new one and add it to the linked list

    DEBUG("procevent plugin: allocating new processlist_t object for PID %ld "
          "(%s)",
          pid, buffer);

    processlist_t *pl2 = calloc(1, sizeof(*pl2));
    if (pl2 == NULL) {
      ERROR("procevent plugin: calloc failed during process_check: %s",
            STRERRNO);
      return NULL;
    }

    char *process = strdup(buffer);
    if (process == NULL) {
      sfree(pl2);
      ERROR("procevent plugin: strdup failed during process_check: %s",
            STRERRNO);
      return NULL;
    }

    pl2->process = process;
    pl2->pid = pid;
    pl2->next = processlist_head;
    processlist_head = pl2;

    match = pl2;
  }

  return match;
}

// Does our map have this PID or name?
// NOTE: Caller MUST hold procevent_data_lock when calling this function
static processlist_t *process_map_check(long pid, char *process) {
  for (processlist_t *pl = processlist_head; pl != NULL; pl = pl->next) {
    int match_pid = 0;

    if (pid > 0) {
      if (pl->pid == pid)
        match_pid = 1;
    }

    int match_process = 0;

    if (process != NULL) {
      if (strcmp(pl->process, process) == 0)
        match_process = 1;
    }

    int match = 0;

    if ((pid > 0 && process == NULL && match_pid == 1) ||
        (pid < 0 && process != NULL && match_process == 1) ||
        (pid > 0 && process != NULL && match_pid == 1 && match_process == 1)) {
      match = 1;
    }

    if (match == 1) {
      return pl;
    }
  }

  return NULL;
}

static int process_map_refresh(void) {
  errno = 0;
  DIR *proc = opendir(PROCDIR);

  if (proc == NULL) {
    ERROR("procevent plugin: fopen (%s): %s", PROCDIR, STRERRNO);
    return -1;
  }

  while (42) {
    errno = 0;
    struct dirent *dent = readdir(proc);
    if (dent == NULL) {
      if (errno == 0) /* end of directory */
        break;

      ERROR("procevent plugin: failed to read directory %s: %s", PROCDIR,
            STRERRNO);
      closedir(proc);
      return -1;
    }

    if (dent->d_name[0] == '.')
      continue;

    char file[BUFSIZE];

    int len = snprintf(file, sizeof(file), PROCDIR "/%s", dent->d_name);
    if ((len < 0) || (len >= BUFSIZE))
      continue;

    struct stat statbuf;

    int status = stat(file, &statbuf);
    if (status != 0) {
      WARNING("procevent plugin: stat (%s) failed: %s", file, STRERRNO);
      continue;
    }

    if (!S_ISDIR(statbuf.st_mode))
      continue;

    len = snprintf(file, sizeof(file), PROCDIR "/%s/comm", dent->d_name);
    if ((len < 0) || (len >= BUFSIZE))
      continue;

    int not_number = 0;

    for (int i = 0; i < strlen(dent->d_name); i++) {
      if (!isdigit(dent->d_name[i])) {
        not_number = 1;
        break;
      }
    }

    if (not_number != 0)
      continue;

    // Check if we need to store this pid/name combo in our processlist_t linked
    // list
    int this_pid = atoi(dent->d_name);
    pthread_mutex_lock(&procevent_data_lock);
    processlist_t *pl = process_check(this_pid);
    pthread_mutex_unlock(&procevent_data_lock);

    if (pl != NULL)
      DEBUG("procevent plugin: process map refreshed for PID %d and name %s",
            this_pid, pl->process);
  }

  closedir(proc);

  return 0;
}

static int nl_connect() {
  struct sockaddr_nl sa_nl = {
      .nl_family = AF_NETLINK,
      .nl_groups = CN_IDX_PROC,
      .nl_pid = getpid(),
  };

  nl_sock = socket(PF_NETLINK, SOCK_DGRAM, NETLINK_CONNECTOR);
  if (nl_sock == -1) {
    ERROR("procevent plugin: socket open failed: %d", errno);
    return -1;
  }

  int rc = bind(nl_sock, (struct sockaddr *)&sa_nl, sizeof(sa_nl));
  if (rc == -1) {
    ERROR("procevent plugin: socket bind failed: %d", errno);
    close(nl_sock);
    nl_sock = -1;
    return -1;
  }

  return 0;
}

static int set_proc_ev_listen(bool enable) {
  struct __attribute__((aligned(NLMSG_ALIGNTO))) {
    struct nlmsghdr nl_hdr;
    struct __attribute__((__packed__)) {
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

  int rc = send(nl_sock, &nlcn_msg, sizeof(nlcn_msg), 0);
  if (rc == -1) {
    ERROR("procevent plugin: subscribing to netlink process events failed: %d",
          errno);
    return -1;
  }

  return 0;
}

// Read from netlink socket and write to ring buffer
static int read_event() {
  int recv_flags = MSG_DONTWAIT;
  struct __attribute__((aligned(NLMSG_ALIGNTO))) {
    struct nlmsghdr nl_hdr;
    struct __attribute__((__packed__)) {
      struct cn_msg cn_msg;
      struct proc_event proc_ev;
    };
  } nlcn_msg;

  if (nl_sock == -1)
    return 0;

  while (42) {
    pthread_mutex_lock(&procevent_thread_lock);

    if (procevent_netlink_thread_loop <= 0) {
      pthread_mutex_unlock(&procevent_thread_lock);
      return 0;
    }

    pthread_mutex_unlock(&procevent_thread_lock);

    int status = recv(nl_sock, &nlcn_msg, sizeof(nlcn_msg), recv_flags);

    if (status == 0) {
      return 0;
    } else if (status < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        pthread_mutex_lock(&procevent_data_lock);

        // There was nothing more to receive for now, so...
        // If ring head does not equal ring tail, then there is data
        // in the ring buffer for the dequeue thread to read, so
        // signal it
        if (ring.head != ring.tail)
          pthread_cond_signal(&procevent_cond);

        pthread_mutex_unlock(&procevent_data_lock);

        // Since there was nothing to receive, set recv to block and
        // try again
        recv_flags = 0;
        continue;
      } else if (errno != EINTR) {
        ERROR("procevent plugin: socket receive error: %d", errno);
        return -1;
      } else {
        // Interrupt, so just continue and try again
        continue;
      }
    }

    // We successfully received a message, so don't block on the next
    // read in case there are more (and if there aren't, it will be
    // handled above in the EWOULDBLOCK error-checking)
    recv_flags = MSG_DONTWAIT;

    int proc_id = -1;
    int proc_status = -1;

    switch (nlcn_msg.proc_ev.what) {
    case PROC_EVENT_EXEC:
      proc_status = PROCEVENT_STARTED;
      proc_id = nlcn_msg.proc_ev.event_data.exec.process_pid;
      break;
    case PROC_EVENT_EXIT:
      proc_id = nlcn_msg.proc_ev.event_data.exit.process_pid;
      proc_status = PROCEVENT_EXITED;
      break;
    default:
      // Otherwise not of interest
      break;
    }

    // If we're interested in this process status event, place the event
    // in the ring buffer for consumption by the dequeue (dispatch) thread.

    if (proc_status != -1) {
      pthread_mutex_lock(&procevent_data_lock);

      int next = ring.head + 1;
      if (next >= ring.maxLen)
        next = 0;

      if (next == ring.tail) {
        // Buffer is full, signal the dequeue thread to process the buffer
        // and clean it out, and then sleep
        WARNING("procevent plugin: ring buffer full");

        pthread_cond_signal(&procevent_cond);
        pthread_mutex_unlock(&procevent_data_lock);

        usleep(1000);
        continue;
      } else {
        DEBUG("procevent plugin: Process %d status is now %s at %llu", proc_id,
              (proc_status == PROCEVENT_EXITED ? "EXITED" : "STARTED"),
              (unsigned long long)cdtime());

        ring.buffer[ring.head][RBUF_PROC_ID_INDEX] = proc_id;
        ring.buffer[ring.head][RBUF_PROC_STATUS_INDEX] = proc_status;
        ring.buffer[ring.head][RBUF_TIME_INDEX] = cdtime();

        ring.head = next;
      }

      pthread_mutex_unlock(&procevent_data_lock);
    }
  }

  return 0;
}

static void procevent_dispatch_notification(long pid, gauge_t value,
                                            char *process, cdtime_t timestamp) {

  notification_t n = {
      .severity = (value == 1 ? NOTIF_OKAY : NOTIF_FAILURE),
      .time = cdtime(),
      .plugin = "procevent",
      .type = "gauge",
      .type_instance = "process_status",
  };

  sstrncpy(n.host, hostname_g, sizeof(n.host));
  sstrncpy(n.plugin_instance, process, sizeof(n.plugin_instance));

  char *buf = NULL;
  gen_message_payload(value, pid, process, timestamp, &buf);

  int status = plugin_notification_meta_add_string(&n, "ves", buf);

  if (status < 0) {
    sfree(buf);
    ERROR("procevent plugin: unable to set notification VES metadata: %s",
          STRERRNO);
    return;
  }

  DEBUG("procevent plugin: notification VES metadata: %s",
        n.meta->nm_value.nm_string);

  DEBUG("procevent plugin: dispatching state %d for PID %ld (%s)", (int)value,
        pid, process);

  plugin_dispatch_notification(&n);
  plugin_notification_meta_free(n.meta);

  // strdup'd in gen_message_payload
  if (buf != NULL)
    sfree(buf);
}

// Read from ring buffer and dispatch to write plugins
static void read_ring_buffer() {
  pthread_mutex_lock(&procevent_data_lock);

  // If there's currently nothing to read from the buffer,
  // then wait
  if (ring.head == ring.tail)
    pthread_cond_wait(&procevent_cond, &procevent_data_lock);

  while (ring.head != ring.tail) {
    int next = ring.tail + 1;

    if (next >= ring.maxLen)
      next = 0;

    if (ring.buffer[ring.tail][RBUF_PROC_STATUS_INDEX] == PROCEVENT_EXITED) {
      processlist_t *pl = process_map_check(ring.buffer[ring.tail][0], NULL);

      if (pl != NULL) {
        // This process is of interest to us, so publish its EXITED status
        procevent_dispatch_notification(
            ring.buffer[ring.tail][RBUF_PROC_ID_INDEX],
            ring.buffer[ring.tail][RBUF_PROC_STATUS_INDEX], pl->process,
            ring.buffer[ring.tail][RBUF_TIME_INDEX]);
        DEBUG(
            "procevent plugin: PID %ld (%s) EXITED, removing PID from process "
            "list",
            pl->pid, pl->process);
        pl->pid = -1;
        pl->last_status = -1;
      }
    } else if (ring.buffer[ring.tail][RBUF_PROC_STATUS_INDEX] ==
               PROCEVENT_STARTED) {
      // a new process has started, so check if we should monitor it
      processlist_t *pl = process_check(ring.buffer[ring.tail][0]);

      // If we had already seen this process name and pid combo before,
      // and the last message was a "process started" message, don't send
      // the notfication again

      if (pl != NULL && pl->last_status != PROCEVENT_STARTED) {
        // This process is of interest to us, so publish its STARTED status
        procevent_dispatch_notification(
            ring.buffer[ring.tail][RBUF_PROC_ID_INDEX],
            ring.buffer[ring.tail][RBUF_PROC_STATUS_INDEX], pl->process,
            ring.buffer[ring.tail][RBUF_TIME_INDEX]);

        pl->last_status = PROCEVENT_STARTED;

        DEBUG("procevent plugin: PID %ld (%s) STARTED, adding PID to process "
              "list",
              pl->pid, pl->process);
      }
    }

    ring.tail = next;
  }

  pthread_mutex_unlock(&procevent_data_lock);
}

// Entry point for thread responsible for listening
// to netlink socket and writing data to ring buffer
static void *procevent_netlink_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&procevent_thread_lock);

  while (procevent_netlink_thread_loop > 0) {
    pthread_mutex_unlock(&procevent_thread_lock);

    int status = read_event();

    pthread_mutex_lock(&procevent_thread_lock);

    if (status < 0) {
      procevent_netlink_thread_error = 1;
      break;
    }
  } /* while (procevent_netlink_thread_loop > 0) */

  pthread_mutex_unlock(&procevent_thread_lock);

  return (void *)0;
} /* }}} void *procevent_netlink_thread */

// Entry point for thread responsible for reading from
// ring buffer and dispatching notifications
static void *procevent_dequeue_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&procevent_thread_lock);

  while (procevent_dequeue_thread_loop > 0) {
    pthread_mutex_unlock(&procevent_thread_lock);

    read_ring_buffer();

    pthread_mutex_lock(&procevent_thread_lock);
  } /* while (procevent_dequeue_thread_loop > 0) */

  pthread_mutex_unlock(&procevent_thread_lock);

  return (void *)0;
} /* }}} void *procevent_dequeue_thread */

static int start_netlink_thread(void) /* {{{ */
{
  pthread_mutex_lock(&procevent_thread_lock);

  if (procevent_netlink_thread_loop != 0) {
    pthread_mutex_unlock(&procevent_thread_lock);
    return 0;
  }

  int status;

  if (nl_sock == -1) {
    status = nl_connect();

    if (status != 0) {
      pthread_mutex_unlock(&procevent_thread_lock);
      return status;
    }

    status = set_proc_ev_listen(true);
    if (status == -1) {
      pthread_mutex_unlock(&procevent_thread_lock);
      return status;
    }
  }

  DEBUG("procevent plugin: socket created and bound");

  procevent_netlink_thread_loop = 1;
  procevent_netlink_thread_error = 0;

  status = plugin_thread_create(&procevent_netlink_thread_id,
                                procevent_netlink_thread,
                                /* arg = */ (void *)0, "procevent");
  if (status != 0) {
    procevent_netlink_thread_loop = 0;
    ERROR("procevent plugin: Starting netlink thread failed.");
    pthread_mutex_unlock(&procevent_thread_lock);

    int status2 = close(nl_sock);

    if (status2 != 0) {
      ERROR("procevent plugin: failed to close socket %d: %d (%s)", nl_sock,
            status2, STRERRNO);
    }

    nl_sock = -1;

    return -1;
  }

  pthread_mutex_unlock(&procevent_thread_lock);

  return status;
} /* }}} int start_netlink_thread */

static int start_dequeue_thread(void) /* {{{ */
{
  pthread_mutex_lock(&procevent_thread_lock);

  if (procevent_dequeue_thread_loop != 0) {
    pthread_mutex_unlock(&procevent_thread_lock);
    return 0;
  }

  procevent_dequeue_thread_loop = 1;

  int status = plugin_thread_create(&procevent_dequeue_thread_id,
                                    procevent_dequeue_thread,
                                    /* arg = */ (void *)0, "procevent");
  if (status != 0) {
    procevent_dequeue_thread_loop = 0;
    ERROR("procevent plugin: Starting dequeue thread failed.");
    pthread_mutex_unlock(&procevent_thread_lock);
    return -1;
  }

  pthread_mutex_unlock(&procevent_thread_lock);

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
      ERROR("procevent plugin: failed to close socket %d: %d (%s)", nl_sock,
            socket_status, strerror(errno));
    }

    nl_sock = -1;
  } else
    socket_status = 0;

  pthread_mutex_lock(&procevent_thread_lock);

  if (procevent_netlink_thread_loop == 0) {
    pthread_mutex_unlock(&procevent_thread_lock);
    return -1;
  }

  // Set thread termination status
  procevent_netlink_thread_loop = 0;
  pthread_mutex_unlock(&procevent_thread_lock);

  // Let threads waiting on access to the data know to move
  // on such that they'll see the thread's termination status
  pthread_cond_broadcast(&procevent_cond);

  int thread_status;

  if (shutdown == 1) {
    // Calling pthread_cancel here in
    // the case of a shutdown just assures that the thread is
    // gone and that the process has been fully terminated.

    DEBUG("procevent plugin: Canceling netlink thread for process shutdown");

    thread_status = pthread_cancel(procevent_netlink_thread_id);

    if (thread_status != 0 && thread_status != ESRCH) {
      ERROR("procevent plugin: Unable to cancel netlink thread: %d",
            thread_status);
      thread_status = -1;
    } else
      thread_status = 0;
  } else {
    thread_status =
        pthread_join(procevent_netlink_thread_id, /* return = */ NULL);
    if (thread_status != 0 && thread_status != ESRCH) {
      ERROR("procevent plugin: Stopping netlink thread failed.");
      thread_status = -1;
    } else
      thread_status = 0;
  }

  pthread_mutex_lock(&procevent_thread_lock);
  memset(&procevent_netlink_thread_id, 0, sizeof(procevent_netlink_thread_id));
  procevent_netlink_thread_error = 0;
  pthread_mutex_unlock(&procevent_thread_lock);

  DEBUG("procevent plugin: Finished requesting stop of netlink thread");

  if (socket_status != 0)
    return socket_status;
  else
    return thread_status;
} /* }}} int stop_netlink_thread */

static int stop_dequeue_thread() /* {{{ */
{
  pthread_mutex_lock(&procevent_thread_lock);

  if (procevent_dequeue_thread_loop == 0) {
    pthread_mutex_unlock(&procevent_thread_lock);
    return -1;
  }

  procevent_dequeue_thread_loop = 0;
  pthread_mutex_unlock(&procevent_thread_lock);

  pthread_cond_broadcast(&procevent_cond);

  // Calling pthread_cancel here just assures that the thread is
  // gone and that the process has been fully terminated.

  DEBUG("procevent plugin: Canceling dequeue thread for process shutdown");

  int status = pthread_cancel(procevent_dequeue_thread_id);

  if (status != 0 && status != ESRCH) {
    ERROR("procevent plugin: Unable to cancel dequeue thread: %d", status);
    status = -1;
  } else
    status = 0;

  pthread_mutex_lock(&procevent_thread_lock);
  memset(&procevent_dequeue_thread_id, 0, sizeof(procevent_dequeue_thread_id));
  pthread_mutex_unlock(&procevent_thread_lock);

  DEBUG("procevent plugin: Finished requesting stop of dequeue thread");

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

static int procevent_init(void) /* {{{ */
{
  ring.head = 0;
  ring.tail = 0;
  ring.maxLen = buffer_length;
  ring.buffer = (cdtime_t **)calloc(buffer_length, sizeof(cdtime_t *));

  for (int i = 0; i < buffer_length; i++) {
    ring.buffer[i] = (cdtime_t *)calloc(PROCEVENT_FIELDS, sizeof(cdtime_t));
  }

  int status = process_map_refresh();

  if (status == -1) {
    ERROR("procevent plugin: Initial process mapping failed.");
    return -1;
  }

  if (ignorelist == NULL) {
    NOTICE("procevent plugin: No processes have been configured.");
    return -1;
  }

  return start_threads();
} /* }}} int procevent_init */

static int procevent_config(const char *key, const char *value) /* {{{ */
{
  if (ignorelist == NULL)
    ignorelist = ignorelist_create(/* invert = */ 1);

  if (ignorelist == NULL) {
    return -1;
  }

  if (strcasecmp(key, "BufferLength") == 0) {
    buffer_length = atoi(value);
  } else if (strcasecmp(key, "Process") == 0) {
    ignorelist_add(ignorelist, value);
  } else if (strcasecmp(key, "ProcessRegex") == 0) {
#if HAVE_REGEX_H
    int status = ignorelist_add(ignorelist, value);

    if (status != 0) {
      ERROR("procevent plugin: invalid regular expression: %s", value);
      return 1;
    }
#else
    WARNING("procevent plugin: The plugin has been compiled without support "
            "for the \"ProcessRegex\" option.");
#endif
  } else {
    return -1;
  }

  return 0;
} /* }}} int procevent_config */

static int procevent_read(void) /* {{{ */
{
  pthread_mutex_lock(&procevent_thread_lock);

  if (procevent_netlink_thread_error != 0) {

    pthread_mutex_unlock(&procevent_thread_lock);

    ERROR("procevent plugin: The netlink thread had a problem. Restarting it.");

    stop_netlink_thread(0);

    start_netlink_thread();

    return -1;
  } /* if (procevent_netlink_thread_error != 0) */

  pthread_mutex_unlock(&procevent_thread_lock);

  return 0;
} /* }}} int procevent_read */

static int procevent_shutdown(void) /* {{{ */
{
  DEBUG("procevent plugin: Shutting down threads.");

  int status = stop_threads();

  for (int i = 0; i < buffer_length; i++) {
    free(ring.buffer[i]);
  }

  free(ring.buffer);

  processlist_t *pl = processlist_head;
  while (pl != NULL) {
    processlist_t *pl_next;

    pl_next = pl->next;

    sfree(pl->process);
    sfree(pl);

    pl = pl_next;
  }

  ignorelist_free(ignorelist);

  return status;
} /* }}} int procevent_shutdown */

void module_register(void) {
  plugin_register_config("procevent", procevent_config, config_keys,
                         config_keys_num);
  plugin_register_init("procevent", procevent_init);
  plugin_register_read("procevent", procevent_read);
  plugin_register_shutdown("procevent", procevent_shutdown);
} /* void module_register */
