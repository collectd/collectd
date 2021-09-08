/**
 * collectd - src/ipc.c, based on src/memcached.c
 * Copyright (C) 2010       Andres J. Diaz <ajdiaz@connectical.com>
 * Copyright (C) 2010       Manuel L. Sanmartin <manuel.luis@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Andres J. Diaz <ajdiaz@connectical.com>
 *   Manuel L. Sanmartin <manuel.luis@gmail>
 **/

/* Many of this code is based on busybox ipc implementation, which is:
 *   (C) Rodney Radford <rradford@mindspring.com> and distributed under GPLv2.
 */

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#if KERNEL_LINUX
/* _GNU_SOURCE is needed for struct shm_info.used_ids on musl libc */
#define _GNU_SOURCE

/* X/OPEN tells us to use <sys/{types,ipc,sem}.h> for semctl() */
/* X/OPEN tells us to use <sys/{types,ipc,msg}.h> for msgctl() */
/* X/OPEN tells us to use <sys/{types,ipc,shm}.h> for shmctl() */
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>

/* For older kernels the same holds for the defines below */
#ifndef MSG_STAT
#define MSG_STAT 11
#define MSG_INFO 12
#endif

#ifndef SHM_STAT
#define SHM_STAT 13
#define SHM_INFO 14
struct shm_info {
  int used_ids;
  ulong shm_tot; /* total allocated shm */
  ulong shm_rss; /* total resident shm */
  ulong shm_swp; /* total swapped shm */
  ulong swap_attempts;
  ulong swap_successes;
};
#endif

#ifndef SEM_STAT
#define SEM_STAT 18
#define SEM_INFO 19
#endif

/* The last arg of semctl is a union semun, but where is it defined?
   X/OPEN tells us to define it ourselves, but until recently
   Linux include files would also define it. */
#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
/* union semun is defined by including <sys/sem.h> */
#else
/* according to X/OPEN we have to define it ourselves */
union semun {
  int val;
  struct semid_ds *buf;
  unsigned short *array;
  struct seminfo *__buf;
};
#endif
static long pagesize_g;
/* #endif  KERNEL_LINUX */
#elif KERNEL_AIX
#include <sys/ipc_info.h>
/* #endif KERNEL_AIX */
#else
#error "No applicable input method."
#endif

enum {
  FAM_IPC_SEM_ARRAYS = 0,
  FAM_IPC_SEM_SEMAPHORES,
  FAM_IPC_SHM_SEGMENTS,
  FAM_IPC_SHM_TOTAL,
  FAM_IPC_SHM_RSS,
  FAM_IPC_SHM_SWAPPED,
  FAM_IPC_MSG_QUEUES,
  FAM_IPC_MSG_MESSAGES,
  FAM_IPC_MSG_BYTES,
  FAM_IPC_MAX,
};

#if KERNEL_LINUX
static int ipc_read_sem(metric_family_t *fams) /* {{{ */
{
  struct seminfo seminfo;
  union semun arg;
  int status;

  arg.array = (void *)&seminfo;

  status = semctl(/* id = */ 0, /* num = */ 0, SEM_INFO, arg);
  if (status == -1) {
    ERROR("ipc plugin: semctl(2) failed: %s. "
          "Maybe the kernel is not configured for semaphores?",
          STRERRNO);
    return -1;
  }

  metric_family_metric_append(&fams[FAM_IPC_SEM_ARRAYS],
                              (metric_t){
                                  .value.gauge = seminfo.semusz,
                              });
  metric_family_metric_append(&fams[FAM_IPC_SEM_SEMAPHORES],
                              (metric_t){
                                  .value.gauge = seminfo.semaem,
                              });

  return 0;
} /* }}} int ipc_read_sem */

static int ipc_read_shm(metric_family_t *fams) /* {{{ */
{
  struct shm_info shm_info;
  int status;

  status = shmctl(/* id = */ 0, SHM_INFO, (void *)&shm_info);
  if (status == -1) {
    ERROR("ipc plugin: shmctl(2) failed: %s. "
          "Maybe the kernel is not configured for shared memory?",
          STRERRNO);
    return -1;
  }

  metric_family_metric_append(&fams[FAM_IPC_SHM_SEGMENTS],
                              (metric_t){
                                  .value.gauge = shm_info.used_ids,
                              });
  metric_family_metric_append(&fams[FAM_IPC_SHM_TOTAL],
                              (metric_t){
                                  .value.gauge = shm_info.shm_tot * pagesize_g,
                              });
  metric_family_metric_append(&fams[FAM_IPC_SHM_RSS],
                              (metric_t){
                                  .value.gauge = shm_info.shm_rss * pagesize_g,
                              });
  metric_family_metric_append(&fams[FAM_IPC_SHM_SWAPPED],
                              (metric_t){
                                  .value.gauge = shm_info.shm_swp * pagesize_g,
                              });

  return 0;
}
/* }}} int ipc_read_shm */

static int ipc_read_msg(metric_family_t *fams) /* {{{ */
{
  struct msginfo msginfo;

  if (msgctl(0, MSG_INFO, (struct msqid_ds *)(void *)&msginfo) < 0) {
    ERROR("Kernel is not configured for message queues");
    return -1;
  }

  metric_family_metric_append(&fams[FAM_IPC_MSG_QUEUES],
                              (metric_t){
                                  .value.gauge = msginfo.msgpool,
                              });
  metric_family_metric_append(&fams[FAM_IPC_MSG_MESSAGES],
                              (metric_t){
                                  .value.gauge = msginfo.msgmap,
                              });
  metric_family_metric_append(&fams[FAM_IPC_MSG_BYTES],
                              (metric_t){
                                  .value.gauge = msginfo.msgtql,
                              });

  return 0;
}
/* }}} int ipc_read_msg */

static int ipc_init(void) /* {{{ */
{
  pagesize_g = sysconf(_SC_PAGESIZE);
  return 0;
}
/* }}} */
/* #endif KERNEL_LINUX */

#elif KERNEL_AIX
static caddr_t ipc_get_info(cid_t cid, int cmd, int version, int stsize,
                            int *nmemb) /* {{{ */
{
  int size = 0;
  caddr_t buff = NULL;

  if (get_ipc_info(cid, cmd, version, buff, &size) < 0) {
    if (errno != ENOSPC) {
      WARNING("ipc plugin: get_ipc_info: %s", STRERRNO);
      return NULL;
    }
  }

  if (size == 0)
    return NULL;

  if (size % stsize) {
    ERROR("ipc plugin: ipc_get_info: missmatch struct size and buffer size");
    return NULL;
  }

  *nmemb = size / stsize;

  buff = malloc(size);
  if (buff == NULL) {
    ERROR("ipc plugin: ipc_get_info malloc failed.");
    return NULL;
  }

  if (get_ipc_info(cid, cmd, version, buff, &size) < 0) {
    WARNING("ipc plugin: get_ipc_info: %s", STRERRNO);
    free(buff);
    return NULL;
  }

  return buff;
} /* }}} */

static int ipc_read_sem(metric_family_t *fams) /* {{{ */
{
  ipcinfo_sem_t *ipcinfo_sem;
  unsigned short sem_nsems = 0;
  unsigned short sems = 0;
  int n;

  ipcinfo_sem = (ipcinfo_sem_t *)ipc_get_info(
      0, GET_IPCINFO_SEM_ALL, IPCINFO_SEM_VERSION, sizeof(ipcinfo_sem_t), &n);
  if (ipcinfo_sem == NULL)
    return -1;

  for (int i = 0; i < n; i++) {
    sem_nsems += ipcinfo_sem[i].sem_nsems;
    sems++;
  }
  free(ipcinfo_sem);

  metric_family_metric_append(&fams[FAM_IPC_SEM_ARRAYS],
                              (metric_t){
                                  .value.gauge = sem_nsems,
                              });
  metric_family_metric_append(&fams[FAM_IPC_SEM_SEMAPHORES],
                              (metric_t){
                                  .value.gauge = sems,
                              });

  return 0;
} /* }}} int ipc_read_sem */

static int ipc_read_shm(metric_family_t *fams) /* {{{ */
{
  ipcinfo_shm_t *ipcinfo_shm;
  ipcinfo_shm_t *pshm;
  unsigned int shm_segments = 0;
  size64_t shm_bytes = 0;
  int i, n;

  ipcinfo_shm = (ipcinfo_shm_t *)ipc_get_info(
      0, GET_IPCINFO_SHM_ALL, IPCINFO_SHM_VERSION, sizeof(ipcinfo_shm_t), &n);
  if (ipcinfo_shm == NULL)
    return -1;

  for (i = 0, pshm = ipcinfo_shm; i < n; i++, pshm++) {
    shm_segments++;
    shm_bytes += pshm->shm_segsz;
  }
  free(ipcinfo_shm);

  metric_family_metric_append(&fams[FAM_IPC_SHM_SEGMENTS],
                              (metric_t){
                                  .value.gauge = shm_segments,
                              });
  metric_family_metric_append(&fams[FAM_IPC_SHM_TOTAL],
                              (metric_t){
                                  .value.gauge = shm_bytes,
                              });

  return 0;
}
/* }}} int ipc_read_shm */

static int ipc_read_msg(metric_family_t *fams) /* {{{ */
{
  ipcinfo_msg_t *ipcinfo_msg;
  uint32_t msg_used_space = 0;
  uint32_t msg_alloc_queues = 0;
  msgqnum32_t msg_qnum = 0;
  int n;

  ipcinfo_msg = (ipcinfo_msg_t *)ipc_get_info(
      0, GET_IPCINFO_MSG_ALL, IPCINFO_MSG_VERSION, sizeof(ipcinfo_msg_t), &n);
  if (ipcinfo_msg == NULL)
    return -1;

  for (int i = 0; i < n; i++) {
    msg_alloc_queues++;
    msg_used_space += ipcinfo_msg[i].msg_cbytes;
    msg_qnum += ipcinfo_msg[i].msg_qnum;
  }
  free(ipcinfo_msg);

  metric_family_metric_append(&fams[FAM_IPC_MSG_QUEUES],
                              (metric_t){
                                  .value.gauge = msg_alloc_queues,
                              });
  metric_family_metric_append(&fams[FAM_IPC_MSG_MESSAGES],
                              (metric_t){
                                  .value.gauge = msg_qnum,
                              });
  metric_family_metric_append(&fams[FAM_IPC_MSG_BYTES],
                              (metric_t){
                                  .value.gauge = msg_used_space,
                              });

  return 0;
}
/* }}} */
#endif /* KERNEL_AIX */

static int ipc_read(void) /* {{{ */
{
  int status = 0;
  metric_family_t fams[FAM_IPC_MAX] = {
      [FAM_IPC_SEM_ARRAYS] =
          {
              .name = "ipc_sem_arrays",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_IPC_SEM_SEMAPHORES] =
          {
              .name = "ipc_sem_semaphores",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_IPC_SHM_SEGMENTS] =
          {
              .name = "ipc_shm_segments",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_IPC_SHM_TOTAL] =
          {
              .name = "ipc_shm_total_bytes",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_IPC_SHM_RSS] =
          {
              .name = "ipc_shm_rss_bytes",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_IPC_SHM_SWAPPED] =
          {
              .name = "ipc_shm_swapped_bytes",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_IPC_MSG_QUEUES] =
          {
              .name = "ipc_msg_queues",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_IPC_MSG_MESSAGES] =
          {
              .name = "ipc_msg_messages",
              .type = METRIC_TYPE_GAUGE,
          },
      [FAM_IPC_MSG_BYTES] =
          {
              .name = "ipc_msg_bytes",
              .type = METRIC_TYPE_GAUGE,
          },
  };

  status |= ipc_read_shm(fams);
  status |= ipc_read_sem(fams);
  status |= ipc_read_msg(fams);

  for (size_t i = 0; i < FAM_IPC_MAX; i++) {
    if (fams[i].metric.num > 0) {
      int status = plugin_dispatch_metric_family(&fams[i]);
      if (status != 0) {
        ERROR("ipc plugin: plugin_dispatch_metric_family failed: %s",
              STRERROR(status));
      }
      metric_family_metric_reset(&fams[i]);
    }
  }

  return status;
}
/* }}} */

void module_register(void) /* {{{ */
{
#ifdef KERNEL_LINUX
  plugin_register_init("ipc", ipc_init);
#endif
  plugin_register_read("ipc", ipc_read);
}
/* }}} */
