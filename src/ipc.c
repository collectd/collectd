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
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#if KERNEL_LINUX
  /* X/OPEN tells us to use <sys/{types,ipc,sem}.h> for semctl() */
  /* X/OPEN tells us to use <sys/{types,ipc,msg}.h> for msgctl() */
  /* X/OPEN tells us to use <sys/{types,ipc,shm}.h> for shmctl() */
# include <sys/types.h>
# include <sys/ipc.h>
# include <sys/sem.h>
# include <sys/msg.h>
# include <sys/shm.h>

  /* For older kernels the same holds for the defines below */
# ifndef MSG_STAT
#  define MSG_STAT    11
#  define MSG_INFO    12
# endif

# ifndef SHM_STAT
#   define SHM_STAT        13
#   define SHM_INFO        14
    struct shm_info {
        int used_ids;
        ulong shm_tot;      /* total allocated shm */
        ulong shm_rss;      /* total resident shm */
        ulong shm_swp;      /* total swapped shm */
        ulong swap_attempts;
        ulong swap_successes;
    };
# endif

# ifndef SEM_STAT
#  define SEM_STAT    18
#  define SEM_INFO    19
# endif

  /* The last arg of semctl is a union semun, but where is it defined?
     X/OPEN tells us to define it ourselves, but until recently
     Linux include files would also define it. */
# if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
    /* union semun is defined by including <sys/sem.h> */
# else
    /* according to X/OPEN we have to define it ourselves */
    union semun {
      int val;
      struct semid_ds *buf;
      unsigned short *array;
      struct seminfo *__buf;
    };
# endif
static long pagesize_g;
/* #endif  KERNEL_LINUX */
#elif KERNEL_AIX
# include <sys/ipc_info.h>
/* #endif KERNEL_AIX */
#else
# error "No applicable input method."
#endif

__attribute__ ((nonnull(1)))
static void ipc_submit_g (const char *type, gauge_t value) /* {{{ */
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "ipc", sizeof (vl.plugin));
  sstrncpy (vl.type, type, sizeof (vl.type));

  plugin_dispatch_values (&vl);
} /* }}} */

#if KERNEL_AIX
static caddr_t ipc_get_info (cid_t cid, int cmd, int version, int stsize, int *nmemb) /* {{{ */
{
  int size = 0;
  caddr_t buff = NULL;

  if (get_ipc_info(cid, cmd, version, buff, &size) < 0)
  {
    if (errno != ENOSPC) {
      char errbuf[1024];
      WARNING ("ipc plugin: get_ipc_info: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
      return (NULL);
    }
  }

  if (size == 0)
    return NULL;

  if (size % stsize) {
    ERROR ("ipc plugin: ipc_get_info: missmatch struct size and buffer size");
    return (NULL);
  }

  *nmemb = size / stsize;

  buff = (caddr_t)malloc (size);
  if (buff == NULL)  {
    ERROR ("ipc plugin: ipc_get_info malloc failed.");
    return (NULL);
  }

  if (get_ipc_info(cid, cmd, version, buff, &size) < 0)
  {
    char errbuf[1024];
    WARNING ("ipc plugin: get_ipc_info: %s",
      sstrerror (errno, errbuf, sizeof (errbuf)));
    free(buff);
    return (NULL);
  }

  return buff;
} /* }}} */
#endif /* KERNEL_AIX */

static int ipc_read_sem (void) /* {{{ */
{
#if KERNEL_LINUX
  struct seminfo seminfo;
  union semun arg;

  arg.array = (ushort *) (void *) &seminfo;

  if ( semctl(0, 0, SEM_INFO, arg) < 0 )
  {
    ERROR("Kernel is not configured for semaphores");
    return (-1);
  }

  ipc_submit_g("sem_used_arrays", seminfo.semusz);
  ipc_submit_g("sem_used", seminfo.semaem);

/* #endif KERNEL_LINUX */
#elif KERNEL_AIX
  ipcinfo_sem_t *ipcinfo_sem;
  unsigned short sem_nsems=0;
  unsigned short sems=0;
  int i,n;

  ipcinfo_sem = (ipcinfo_sem_t *)ipc_get_info(0,
    GET_IPCINFO_SEM_ALL, IPCINFO_SEM_VERSION, sizeof(ipcinfo_sem_t), &n);
  if (ipcinfo_sem == NULL)
    return -1;

  for (i=0; i<n; i++) {
    sem_nsems += ipcinfo_sem[i].sem_nsems;
    sems++;
  }
  free(ipcinfo_sem);

  ipc_submit_g("sem_used_arrays", sem_nsems);
  ipc_submit_g("sem_used", sems);
#endif /* KERNEL_AIX */

  return (0);
}
/* }}} */

static int ipc_read_shm (void) /* {{{ */
{
#if KERNEL_LINUX
  struct shm_info shm_info;
  pagesize_g = sysconf(_SC_PAGESIZE);

  if ( shmctl(0, SHM_INFO, (struct shmid_ds *) (void *) &shm_info) < 0 )
  {
    ERROR("Kernel is not configured for shared memory");
    return (-1);
  }
  ipc_submit_g("shm_segments", shm_info.used_ids);
  ipc_submit_g("shm_bytes_total", shm_info.shm_tot * pagesize_g);
  ipc_submit_g("shm_bytes_rss", shm_info.shm_rss * pagesize_g);
  ipc_submit_g("shm_bytes_swapped", shm_info.shm_swp * pagesize_g);
/* #endif KERNEL_LINUX */
#elif KERNEL_AIX
  ipcinfo_shm_t *ipcinfo_shm;
  ipcinfo_shm_t *pshm;
  unsigned int shm_segments=0;
  size64_t shm_bytes=0;
  int i,n;

  ipcinfo_shm = (ipcinfo_shm_t *)ipc_get_info(0,
    GET_IPCINFO_SHM_ALL, IPCINFO_SHM_VERSION, sizeof(ipcinfo_shm_t), &n);
  if (ipcinfo_shm == NULL)
    return -1;

  for (i=0, pshm=ipcinfo_shm; i<n; i++, pshm++) {
    shm_segments++;
    shm_bytes += pshm->shm_segsz;
  }
  free(ipcinfo_shm);

  ipc_submit_g("shm_segments", shm_segments);
  ipc_submit_g("shm_bytes_total", shm_bytes);

#endif /* KERNEL_AIX */
  return (0);
}
/* }}} */

static int ipc_read_msg (void) /* {{{ */
{
#if KERNEL_LINUX
  struct msginfo msginfo;

  if ( msgctl(0, MSG_INFO, (struct msqid_ds *) (void *) &msginfo) < 0 )
  {
    ERROR("Kernel is not configured for message queues");
    return (-1);
  }
  ipc_submit_g("msg_alloc_queues", msginfo.msgmni);
  ipc_submit_g("msg_used_headers", msginfo.msgmap);
  ipc_submit_g("msg_used_space", msginfo.msgtql);
/* #endif KERNEL_LINUX */
#elif KERNEL_AIX
  ipcinfo_msg_t *ipcinfo_msg;
  uint32_t msg_used_space=0;
  uint32_t msg_alloc_queues=0;
  msgqnum32_t msg_qnum=0;
  int i,n;

  ipcinfo_msg = (ipcinfo_msg_t *)ipc_get_info(0,
    GET_IPCINFO_MSG_ALL, IPCINFO_MSG_VERSION, sizeof(ipcinfo_msg_t), &n);
  if (ipcinfo_msg == NULL)
    return -1;

  for (i=0; i<n; i++) {
    msg_alloc_queues++;
    msg_used_space += ipcinfo_msg[i].msg_cbytes;
    msg_qnum += ipcinfo_msg[i].msg_qnum;
  }
  free(ipcinfo_msg);

  ipc_submit_g("msg_alloc_queues", msg_alloc_queues);
  ipc_submit_g("msg_used_headers", msg_qnum);
  ipc_submit_g("msg_used_space", msg_used_space);
#endif /* KERNEL_AIX */
  return (0);
}
/* }}} */

static int ipc_read (void) /* {{{ */
{
  int x = 0;
  x |= ipc_read_shm();
  x |= ipc_read_sem();
  x |= ipc_read_msg();

  return (x);
}
/* }}} */

#ifdef KERNEL_LINUX
static int ipc_init (void) /* {{{ */
{
  pagesize_g = sysconf(_SC_PAGESIZE);
  return (0);
}
/* }}} */
#endif /* KERNEL_LINUX */

void module_register (void) /* {{{ */
{
#ifdef KERNEL_LINUX
  plugin_register_init ("ipc", ipc_init);
#endif
  plugin_register_read ("ipc", ipc_read);
}
/* }}} */

/* vim: set sw=2 sts=2 et fdm=marker : */
