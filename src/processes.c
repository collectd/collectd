/**
 * collectd - src/processes.c
 * Copyright (C) 2005       Lyonel Vincent
 * Copyright (C) 2006-2017  Florian octo Forster
 * Copyright (C) 2008       Oleg King
 * Copyright (C) 2009       Sebastian Harl
 * Copyright (C) 2009       Andrés J. Díaz
 * Copyright (C) 2009       Manuel Sanmartin
 * Copyright (C) 2010       Clément Stenac
 * Copyright (C) 2012       Cosmin Ioiart
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
 *   Lyonel Vincent <lyonel at ezix.org>
 *   Florian octo Forster <octo at collectd.org>
 *   Oleg King <king2 at kaluga.ru>
 *   Sebastian Harl <sh at tokkee.org>
 *   Andrés J. Díaz <ajdiaz at connectical.com>
 *   Manuel Sanmartin
 *   Clément Stenac <clement.stenac at diwi.org>
 *   Cosmin Ioiart <cioiart at gmail.com>
 *   Pavel Rochnyack <pavel2000 at ngs.ru>
 *   Wilfried Goesgens <dothebart at citadel.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#if HAVE_LIBTASKSTATS
#include "utils/taskstats/taskstats.h"
#include "utils_complain.h"
#endif

/* Include header files for the mach system, if they exist.. */
#if HAVE_THREAD_INFO
#if HAVE_MACH_MACH_INIT_H
#include <mach/mach_init.h>
#endif
#if HAVE_MACH_HOST_PRIV_H
#include <mach/host_priv.h>
#endif
#if HAVE_MACH_MACH_ERROR_H
#include <mach/mach_error.h>
#endif
#if HAVE_MACH_MACH_HOST_H
#include <mach/mach_host.h>
#endif
#if HAVE_MACH_MACH_PORT_H
#include <mach/mach_port.h>
#endif
#if HAVE_MACH_MACH_TYPES_H
#include <mach/mach_types.h>
#endif
#if HAVE_MACH_MESSAGE_H
#include <mach/message.h>
#endif
#if HAVE_MACH_PROCESSOR_SET_H
#include <mach/processor_set.h>
#endif
#if HAVE_MACH_TASK_H
#include <mach/task.h>
#endif
#if HAVE_MACH_THREAD_ACT_H
#include <mach/thread_act.h>
#endif
#if HAVE_MACH_VM_REGION_H
#include <mach/vm_region.h>
#endif
#if HAVE_MACH_VM_MAP_H
#include <mach/vm_map.h>
#endif
#if HAVE_MACH_VM_PROT_H
#include <mach/vm_prot.h>
#endif
#if HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
/* no-op */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKVM_GETPROCS &&                                                  \
    (HAVE_STRUCT_KINFO_PROC_FREEBSD || HAVE_STRUCT_KINFO_PROC_OPENBSD ||       \
     HAVE_STRUCT_KINFO_PROC2_NETBSD)
#include <kvm.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#if defined(__FreeBSD__) || defined(__DragonFly__)
#include <sys/user.h>
#endif
/* #endif HAVE_LIBKVM_GETPROCS && (HAVE_STRUCT_KINFO_PROC_FREEBSD ||
 * HAVE_STRUCT_KINFO_PROC_OPENBSD || HAVE_STRUCT_KINFO_PROC2_NETBSD) */

#elif HAVE_PROCINFO_H
#include <procinfo.h>
#include <sys/types.h>

#define MAXPROCENTRY 32
#define MAXTHRDENTRY 16
#define MAXARGLN 1024
/* #endif HAVE_PROCINFO_H */

#elif KERNEL_SOLARIS
/* Hack: Avoid #error when building a 32-bit binary with
 * _FILE_OFFSET_BITS=64. There is a reason for this #error, as one
 * of the structures in <sys/procfs.h> uses an off_t, but that
 * isn't relevant to our usage of procfs. */
#if !defined(_LP64) && _FILE_OFFSET_BITS == 64
#define SAVE_FOB_64
#undef _FILE_OFFSET_BITS
#endif

#include <procfs.h>

#ifdef SAVE_FOB_64
#define _FILE_OFFSET_BITS 64
#undef SAVE_FOB_64
#endif

#include <dirent.h>
#include <sys/user.h>

#ifndef MAXCOMLEN
#define MAXCOMLEN 16
#endif

/* #endif KERNEL_SOLARIS */

#else
#error "No applicable input method."
#endif

#if HAVE_REGEX_H
#include <regex.h>
#endif

#if HAVE_KSTAT_H
#include <kstat.h>
#endif

#ifdef HAVE_SYS_CAPABILITY_H
#include <sys/capability.h>
#endif

#ifndef CMDLINE_BUFFER_SIZE
#if defined(ARG_MAX) && (ARG_MAX < 4096)
#define CMDLINE_BUFFER_SIZE ARG_MAX
#else
#define CMDLINE_BUFFER_SIZE 4096
#endif
#endif

#define PROCSTAT_NAME_LEN 256
/* process_entry_t represents the process data read from the operating system.
 */
typedef struct {
  unsigned long id;
  char name[PROCSTAT_NAME_LEN];
  // The time the process started after system boot.
  // Value is in jiffies.
  unsigned long long starttime;
  char const *command_line; // static, do not free

  unsigned long num_proc;
  unsigned long num_lwp;
  unsigned long num_fd;
  unsigned long num_maps;
  unsigned long vmem_size;
  unsigned long vmem_rss;
  unsigned long vmem_data;
  unsigned long vmem_code;
  unsigned long stack_size;

  derive_t vmem_minflt_counter;
  derive_t vmem_majflt_counter;

  fpcounter_t cpu_user_counter;
  fpcounter_t cpu_system_counter;

  /* io data */
  derive_t io_rchar;
  derive_t io_wchar;
  derive_t io_syscr;
  derive_t io_syscw;
  derive_t io_diskr;
  derive_t io_diskw;
  bool has_io;

  derive_t cswitch_vol;
  derive_t cswitch_invol;
  bool has_cswitch;

#if HAVE_LIBTASKSTATS
  ts_delay_t delay;
#endif
  bool has_delay;

  bool has_fd;

  bool has_maps;
} process_entry_t;

/* procstat_entry_t represents a process/thread cached in between reads. */
typedef struct procstat_entry_s {
  unsigned long id;
  unsigned char age;
  // The time the process started after system boot.
  // Value is in jiffies.
  unsigned long long starttime;
  bool seen;

  char *command_line;

  gauge_t num_lwp;
  gauge_t num_fd;
  gauge_t num_maps;
  gauge_t vmem_size;
  gauge_t vmem_rss;
  gauge_t vmem_data;
  gauge_t vmem_code;
  gauge_t stack_size;

  derive_t vmem_minflt_counter;
  derive_t vmem_majflt_counter;

  fpcounter_t cpu_user_counter;
  fpcounter_t cpu_system_counter;
  value_to_rate_state_t cpu_user_state;
  value_to_rate_state_t cpu_system_state;
  gauge_t cpu_user_rate;
  gauge_t cpu_system_rate;

  /* io data */
  derive_t io_rchar;
  derive_t io_wchar;
  derive_t io_syscr;
  derive_t io_syscw;
  derive_t io_diskr;
  derive_t io_diskw;

  derive_t cswitch_vol;
  derive_t cswitch_invol;

  /* Linux Delay Accounting. Unit is ns/s. */
  bool has_delay;
  derive_t delay_cpu;
  derive_t delay_blkio;
  derive_t delay_swapin;
  derive_t delay_freepages;

  struct procstat_entry_s *next;
} procstat_entry_t;

/* procstat_t represents one process that was configured to be monitored. */
typedef struct procstat {
  char name[PROCSTAT_NAME_LEN];
#if HAVE_REGEX_H
  regex_t *re;
#endif

  derive_t vmem_minflt_counter;
  derive_t vmem_majflt_counter;

  /* io data */
  derive_t io_rchar;
  derive_t io_wchar;
  derive_t io_syscr;
  derive_t io_syscw;
  derive_t io_diskr;
  derive_t io_diskw;

  derive_t cswitch_vol;
  derive_t cswitch_invol;

  bool report_fd_num;
  bool report_maps_num;
  bool report_ctx_switch;
  bool report_delay;

  struct procstat *next;
  procstat_entry_t *instances;
} procstat_t;

static procstat_t *list_head_g;

static bool want_init = true;
static bool report_ctx_switch;
static bool report_fd_num;
static bool report_maps_num;
static bool report_delay;
static bool report_sys_ctxt_switch;

#if HAVE_THREAD_INFO
static mach_port_t port_host_self;
static mach_port_t port_task_self;

static processor_set_name_array_t pset_list;
static mach_msg_type_number_t pset_list_len;
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
static long pagesize_g;
static void ps_fill_details(const procstat_t *ps, process_entry_t *entry);
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKVM_GETPROCS &&                                                  \
    (HAVE_STRUCT_KINFO_PROC_FREEBSD || HAVE_STRUCT_KINFO_PROC_OPENBSD ||       \
     HAVE_STRUCT_KINFO_PROC2_NETBSD)
static int pagesize;

#if KERNEL_NETBSD
int maxslp;
#endif

/* #endif HAVE_LIBKVM_GETPROCS && (HAVE_STRUCT_KINFO_PROC_FREEBSD ||
 * HAVE_STRUCT_KINFO_PROC_OPENBSD || HAVE_STRUCT_KINFO_PROC2_NETBSD) */

#elif HAVE_PROCINFO_H
static struct procentry64 procentry[MAXPROCENTRY];
static struct thrdentry64 thrdentry[MAXTHRDENTRY];
static int pagesize;

#ifndef _AIXVERSION_610
int getprocs64(void *procsinfo, int sizproc, void *fdsinfo, int sizfd,
               pid_t *index, int count);
int getthrds64(pid_t, void *, int, tid64_t *, int);
#endif
int getargs(void *processBuffer, int bufferLen, char *argsBuffer, int argsLen);
#endif /* HAVE_PROCINFO_H */

#if HAVE_LIBTASKSTATS
static ts_t *taskstats_handle;
#endif

static char const *const state_label = "system.processes.status";

typedef enum {
  // required
  STATE_RUNNING,
  STATE_SLEEPING,
  STATE_STOPPED,
  STATE_DEFUNCT,
  // custom
  STATE_BLOCKED,
  STATE_DAEMON,
  STATE_DETACHED,
  STATE_IDLE,
  STATE_ONPROC,
  STATE_ORPHAN,
  STATE_PAGING,
  STATE_SYSTEM,
  STATE_WAIT,
  // #states
  STATE_MAX,
} process_state_t;

static char const *state_names[STATE_MAX] = {
    // required
    [STATE_RUNNING] = "running",
    [STATE_SLEEPING] = "sleeping",
    [STATE_STOPPED] = "stopped",
    [STATE_DEFUNCT] = "defunct",
    // custom
    [STATE_BLOCKED] = "blocked",
    [STATE_DAEMON] = "daemon",
    [STATE_DETACHED] = "detached",
    [STATE_IDLE] = "idle",
    [STATE_ONPROC] = "onproc",
    [STATE_ORPHAN] = "orphan",
    [STATE_PAGING] = "paging",
    [STATE_SYSTEM] = "system",
    [STATE_WAIT] = "wait",
};

/* put name of process from config to list_head_g tree
 * list_head_g is a list of 'procstat_t' structs with
 * processes names we want to watch */
static procstat_t *ps_list_register(const char *name, const char *regexp) {
  procstat_t *new;
  procstat_t *ptr;
  int status;

  new = calloc(1, sizeof(*new));
  if (new == NULL) {
    ERROR("processes plugin: ps_list_register: calloc failed.");
    return NULL;
  }
  sstrncpy(new->name, name, sizeof(new->name));

  new->io_rchar = -1;
  new->io_wchar = -1;
  new->io_syscr = -1;
  new->io_syscw = -1;
  new->io_diskr = -1;
  new->io_diskw = -1;
  new->cswitch_vol = -1;
  new->cswitch_invol = -1;

  new->report_fd_num = report_fd_num;
  new->report_maps_num = report_maps_num;
  new->report_ctx_switch = report_ctx_switch;
  new->report_delay = report_delay;

#if HAVE_REGEX_H
  if (regexp != NULL) {
    DEBUG("ProcessMatch: adding \"%s\" as criteria to process %s.", regexp,
          name);
    new->re = malloc(sizeof(*new->re));
    if (new->re == NULL) {
      ERROR("processes plugin: ps_list_register: malloc failed.");
      sfree(new);
      return NULL;
    }

    status = regcomp(new->re, regexp, REG_EXTENDED | REG_NOSUB);
    if (status != 0) {
      DEBUG("ProcessMatch: compiling the regular expression \"%s\" failed.",
            regexp);
      sfree(new->re);
      sfree(new);
      return NULL;
    }
  }
#else
  if (regexp != NULL) {
    ERROR("processes plugin: ps_list_register: "
          "Regular expression \"%s\" found in config "
          "file, but support for regular expressions "
          "has been disabled at compile time.",
          regexp);
    sfree(new);
    return NULL;
  }
#endif

  for (ptr = list_head_g; ptr != NULL; ptr = ptr->next) {
    if (strcmp(ptr->name, name) == 0) {
      WARNING("processes plugin: You have configured more "
              "than one `Process' or "
              "`ProcessMatch' with the same name. "
              "All but the first setting will be "
              "ignored.");
#if HAVE_REGEX_H
      sfree(new->re);
#endif
      sfree(new);
      return NULL;
    }

    if (ptr->next == NULL)
      break;
  }

  if (ptr == NULL)
    list_head_g = new;
  else
    ptr->next = new;

  return new;
} /* void ps_list_register */

/* try to match name against entry, returns 1 if success */
static bool ps_list_match(const char *name, const char *cmdline,
                          procstat_t *ps) {
#if HAVE_REGEX_H
  if (ps->re != NULL) {
    const char *str = cmdline;
    if ((str == NULL) || (str[0] == 0))
      str = name;

    assert(str != NULL);

    int status = regexec(ps->re, str,
                         /* nmatch = */ 0,
                         /* pmatch = */ NULL,
                         /* eflags = */ 0);
    return status == 0 ? true : false;
  }
#endif
  if (strcmp(ps->name, name) == 0) {
    return true;
  }

  return false;
} /* int ps_list_match */

static void ps_update_counter(derive_t *group_counter, derive_t *curr_counter,
                              derive_t new_counter) {
  unsigned long curr_value;

  if (want_init) {
    *curr_counter = new_counter;
    return;
  }

  if (new_counter < *curr_counter)
    curr_value = new_counter + (ULONG_MAX - *curr_counter);
  else
    curr_value = new_counter - *curr_counter;

  if (*group_counter == -1)
    *group_counter = 0;

  *curr_counter = new_counter;
  *group_counter += curr_value;
}

static procstat_entry_t *
find_or_allocate_procstat_entry(procstat_t *ps, unsigned long id,
                                unsigned long long starttime) {
  procstat_entry_t *tail = NULL;
  for (procstat_entry_t *pse = ps->instances; pse != NULL; pse = pse->next) {
    if (pse->id == id && pse->starttime == starttime) {
      return pse;
    }

    if (pse->next == NULL) {
      tail = pse;
      break;
    }
  }

  procstat_entry_t *new = calloc(1, sizeof(*new));
  if (new == NULL) {
    return NULL;
  }
  new->id = id;
  new->starttime = starttime;

  if (tail == NULL)
    ps->instances = new;
  else
    tail->next = new;

  return new;
}

/* add process entry to 'instances' of process 'name' (or update it) */
static void ps_add_entry_to_procstat(procstat_t *ps, char const *cmdline,
                                     process_entry_t *entry) {
  procstat_entry_t *pse =
      find_or_allocate_procstat_entry(ps, entry->id, entry->starttime);
  if (pse == NULL) {
    ERROR("processes plugin: find_or_allocate_procstat_entry failed: %s",
          STRERRNO);
    return;
  }

  pse->seen = true;

  if (pse->command_line == NULL && cmdline != NULL) {
    pse->command_line = strdup(cmdline);
  }

#if KERNEL_LINUX
  ps_fill_details(ps, entry);
#endif

  pse->has_delay = entry->has_delay;
#if HAVE_LIBTASKSTATS
  pse->delay_cpu = (derive_t)entry->delay.cpu_ns;
  pse->delay_blkio = (derive_t)entry->delay.blkio_ns;
  pse->delay_swapin = (derive_t)entry->delay.swapin_ns;
  pse->delay_freepages = (derive_t)entry->delay.freepages_ns;
#endif

  pse->num_lwp = (gauge_t)entry->num_lwp;
  pse->num_fd = (gauge_t)entry->num_fd;
  pse->num_maps = (gauge_t)entry->num_maps;
  pse->vmem_size = (gauge_t)entry->vmem_size;
  pse->vmem_rss = (gauge_t)entry->vmem_rss;
  pse->vmem_data = (gauge_t)entry->vmem_data;
  pse->vmem_code = (gauge_t)entry->vmem_code;
  pse->stack_size = (gauge_t)entry->stack_size;

  cdtime_t now = cdtime();

  pse->cpu_user_counter = entry->cpu_user_counter;
  value_to_rate(&pse->cpu_user_rate,
                (value_t){.fpcounter = entry->cpu_user_counter},
                METRIC_TYPE_FPCOUNTER, now, &pse->cpu_user_state);
  pse->cpu_system_counter = entry->cpu_system_counter;
  value_to_rate(&pse->cpu_system_rate,
                (value_t){.fpcounter = entry->cpu_system_counter},
                METRIC_TYPE_FPCOUNTER, now, &pse->cpu_system_state);

  if ((entry->io_rchar != -1) && (entry->io_wchar != -1)) {
    ps_update_counter(&ps->io_rchar, &pse->io_rchar, entry->io_rchar);
    ps_update_counter(&ps->io_wchar, &pse->io_wchar, entry->io_wchar);
  }

  if ((entry->io_syscr != -1) && (entry->io_syscw != -1)) {
    ps_update_counter(&ps->io_syscr, &pse->io_syscr, entry->io_syscr);
    ps_update_counter(&ps->io_syscw, &pse->io_syscw, entry->io_syscw);
  }

  if ((entry->io_diskr != -1) && (entry->io_diskw != -1)) {
    ps_update_counter(&ps->io_diskr, &pse->io_diskr, entry->io_diskr);
    ps_update_counter(&ps->io_diskw, &pse->io_diskw, entry->io_diskw);
  }

  if ((entry->cswitch_vol != -1) && (entry->cswitch_invol != -1)) {
    ps_update_counter(&ps->cswitch_vol, &pse->cswitch_vol, entry->cswitch_vol);
    ps_update_counter(&ps->cswitch_invol, &pse->cswitch_invol,
                      entry->cswitch_invol);
  }

  ps_update_counter(&ps->vmem_minflt_counter, &pse->vmem_minflt_counter,
                    entry->vmem_minflt_counter);
  ps_update_counter(&ps->vmem_majflt_counter, &pse->vmem_majflt_counter,
                    entry->vmem_majflt_counter);
}

#if KERNEL_LINUX
static int ps_get_cmdline(long pid, char const *name, char *buf,
                          size_t buf_len) {
  if ((pid < 1) || (NULL == buf) || (buf_len < 2))
    return EINVAL;

  char file[PATH_MAX];
  snprintf(file, sizeof(file), "/proc/%ld/cmdline", pid);
  ssize_t status = read_text_file_contents(file, buf, buf_len);
  if (status < 0) {
    ERROR("process plugin: Reading \"%s\" failed: %s", file, STRERRNO);
    return (int)status;
  }
  if (status == 0) {
    /* cmdline not available; e.g. kernel thread, zombie */
    if (NULL == name) {
      return EINVAL;
    }

    snprintf(buf, buf_len, "[%s]", name);
    return 0;
  }
  assert(status > 0);
  /* n is the number of bytes in the buffer, including the final null byte. */
  size_t n = (size_t)status;

  /* remove trailing whitespace */
  while (n > 0 && (isspace(buf[n - 1]) || buf[n - 1] == 0)) {
    n--;
    buf[n] = 0;
  }

  /* arguments are separated by '\0' in /proc/<pid>/cmdline */
  for (size_t i = 0; i < n; i++) {
    if (buf[i] == 0) {
      buf[i] = ' ';
    }
  }
  return 0;
} /* char *ps_get_cmdline (...) */
#endif

#if KERNEL_SOLARIS
static char *ps_get_cmdline(long pid,
                            char const *name __attribute__((unused)), /* {{{ */
                            char *buffer, size_t buffer_size) {
  char path[PATH_MAX] = {0};
  snprintf(path, sizeof(path), "/proc/%li/psinfo", pid);

  psinfo_t info = {0};
  ssize_t status = read_file_contents(path, &info, sizeof(info));
  if ((status < 0) || (((size_t)status) != sizeof(info))) {
    ERROR("processes plugin: Unexpected return value while reading \"%s\": "
          "Returned %zd but expected %zu.",
          path, status, buffer_size);
    return NULL;
  }

  sstrncpy(buffer, info.pr_psargs, buffer_size);

  return buffer;
} /* }}} int ps_get_cmdline */
#endif

/* add process entry to 'instances' of process 'name' (or refresh it) */
static void ps_list_add(char const *name, process_entry_t *entry) {
  if (entry->id == 0 || list_head_g == NULL) {
    return;
  }

  char const *cmdline = entry->command_line;

#if KERNEL_LINUX || KERNEL_SOLARIS
  /* On Linux and Solaris the command line is read from a file. Only do this if
   * list_head_g is not empty. */
  char clbuf[CMDLINE_BUFFER_SIZE];
  int err = ps_get_cmdline(entry->id, name, clbuf, sizeof(clbuf));
  if (err != 0) {
    ERROR("processes plugin: ps_get_cmdline(%lu) failed: %s", entry->id,
          STRERROR(err));
  } else {
    cmdline = clbuf;
  }
#endif

  for (procstat_t *ps = list_head_g; ps != NULL; ps = ps->next) {
    if (!ps_list_match(name, entry->command_line, ps)) {
      continue;
    }

    ps_add_entry_to_procstat(ps, cmdline, entry);
    return;
  }
}

/* remove old entries from instances of processes in list_head_g */
static void ps_list_reset(void) {
  for (procstat_t *ps = list_head_g; ps != NULL; ps = ps->next) {
    procstat_entry_t *pse = ps->instances;
    procstat_entry_t *pse_prev = NULL;
    while (pse != NULL) {
      pse->num_lwp = NAN;
      pse->num_fd = NAN;
      pse->num_maps = NAN;
      pse->vmem_size = NAN;
      pse->vmem_rss = NAN;
      pse->vmem_data = NAN;
      pse->vmem_code = NAN;
      pse->stack_size = NAN;

      pse->cpu_user_counter = 0;
      pse->cpu_system_counter = 0;
      pse->cpu_user_rate = NAN;
      pse->cpu_system_rate = NAN;

      pse->has_delay = false;
      pse->delay_cpu = 0;
      pse->delay_blkio = 0;
      pse->delay_swapin = 0;
      pse->delay_freepages = 0;

      if (pse->seen) {
        pse->seen = false;
        pse_prev = pse;
        pse = pse->next;
        continue;
      }

      DEBUG("Removing this procstat entry cause it's too old: "
            "id = %lu; name = %s;",
            pse->id, ps->name);

      if (pse_prev == NULL) {
        ps->instances = pse->next;
        free(pse->command_line);
        free(pse);
        pse = ps->instances;
      } else {
        pse_prev->next = pse->next;
        free(pse->command_line);
        free(pse);
        pse = pse_prev->next;
      }
    } /* while (pse != NULL) */
  }   /* for (ps = list_head_g; ps != NULL; ps = ps->next) */
}

static void ps_tune_instance(oconfig_item_t *ci, procstat_t *ps) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *c = ci->children + i;

    if (strcasecmp(c->key, "CollectContextSwitch") == 0)
      cf_util_get_boolean(c, &ps->report_ctx_switch);
    else if (strcasecmp(c->key, "CollectFileDescriptor") == 0)
      cf_util_get_boolean(c, &ps->report_fd_num);
    else if (strcasecmp(c->key, "CollectMemoryMaps") == 0)
      cf_util_get_boolean(c, &ps->report_maps_num);
    else if (strcasecmp(c->key, "CollectDelayAccounting") == 0) {
#if HAVE_LIBTASKSTATS
      cf_util_get_boolean(c, &ps->report_delay);
#else
      WARNING("processes plugin: The plugin has been compiled without support "
              "for the \"CollectDelayAccounting\" option.");
#endif
    } else {
      ERROR("processes plugin: Option \"%s\" not allowed here.", c->key);
    }
  } /* for (ci->children) */
} /* void ps_tune_instance */

/* put all pre-defined 'Process' names from config to list_head_g tree */
static int ps_config(oconfig_item_t *ci) {
#if KERNEL_LINUX
  const size_t max_procname_len = 15;
#elif KERNEL_SOLARIS || KERNEL_FREEBSD
  const size_t max_procname_len = MAXCOMLEN - 1;
#endif

  procstat_t *ps;

  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *c = ci->children + i;

    if (strcasecmp(c->key, "Process") == 0) {
      if ((c->values_num != 1) || (OCONFIG_TYPE_STRING != c->values[0].type)) {
        ERROR("processes plugin: `Process' expects exactly "
              "one string argument (got %i).",
              c->values_num);
        continue;
      }

#if KERNEL_LINUX || KERNEL_SOLARIS || KERNEL_FREEBSD
      if (strlen(c->values[0].value.string) > max_procname_len) {
        WARNING("processes plugin: this platform has a %" PRIsz
                " character limit "
                "to process names. The `Process \"%s\"' option will "
                "not work as expected.",
                max_procname_len, c->values[0].value.string);
      }
#endif

      ps = ps_list_register(c->values[0].value.string, NULL);

      if (c->children_num != 0 && ps != NULL)
        ps_tune_instance(c, ps);
    } else if (strcasecmp(c->key, "ProcessMatch") == 0) {
      if ((c->values_num != 2) || (OCONFIG_TYPE_STRING != c->values[0].type) ||
          (OCONFIG_TYPE_STRING != c->values[1].type)) {
        ERROR("processes plugin: `ProcessMatch' needs exactly "
              "two string arguments (got %i).",
              c->values_num);
        continue;
      }

      ps = ps_list_register(c->values[0].value.string,
                            c->values[1].value.string);

      if (c->children_num != 0 && ps != NULL)
        ps_tune_instance(c, ps);
    } else if (strcasecmp(c->key, "CollectContextSwitch") == 0) {
      cf_util_get_boolean(c, &report_ctx_switch);
    } else if (strcasecmp(c->key, "CollectFileDescriptor") == 0) {
      cf_util_get_boolean(c, &report_fd_num);
    } else if (strcasecmp(c->key, "CollectMemoryMaps") == 0) {
      cf_util_get_boolean(c, &report_maps_num);
    } else if (strcasecmp(c->key, "CollectDelayAccounting") == 0) {
#if HAVE_LIBTASKSTATS
      cf_util_get_boolean(c, &report_delay);
#else
      WARNING("processes plugin: The plugin has been compiled without support "
              "for the \"CollectDelayAccounting\" option.");
#endif
    } else if (strcasecmp(c->key, "CollectSystemContextSwitch") == 0) {
      cf_util_get_boolean(c, &report_sys_ctxt_switch);
    } else {
      ERROR("processes plugin: The `%s' configuration option is not "
            "understood and will be ignored.",
            c->key);
      continue;
    }
  }

  return 0;
}

static int ps_init(void) {
#if HAVE_THREAD_INFO
  kern_return_t status;

  port_host_self = mach_host_self();
  port_task_self = mach_task_self();

  if (pset_list != NULL) {
    vm_deallocate(port_task_self, (vm_address_t)pset_list,
                  pset_list_len * sizeof(processor_set_t));
    pset_list = NULL;
    pset_list_len = 0;
  }

  if ((status = host_processor_sets(port_host_self, &pset_list,
                                    &pset_list_len)) != KERN_SUCCESS) {
    ERROR("host_processor_sets failed: %s\n", mach_error_string(status));
    pset_list = NULL;
    pset_list_len = 0;
    return -1;
  }
  /* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
  pagesize_g = sysconf(_SC_PAGESIZE);

#if HAVE_LIBTASKSTATS
  if (taskstats_handle == NULL) {
    taskstats_handle = ts_create();
    if (taskstats_handle == NULL) {
      WARNING("processes plugin: Creating taskstats handle failed.");
    }
  }
#endif
  /* #endif KERNEL_LINUX */

#elif HAVE_LIBKVM_GETPROCS &&                                                  \
    (HAVE_STRUCT_KINFO_PROC_FREEBSD || HAVE_STRUCT_KINFO_PROC_OPENBSD ||       \
     HAVE_STRUCT_KINFO_PROC2_NETBSD)
#if KERNEL_NETBSD
  int mib[2];
  size_t size;

  mib[0] = CTL_VM;
  mib[1] = VM_MAXSLP;
  size = sizeof(maxslp);
  if (sysctl(mib, 2, &maxslp, &size, NULL, 0) == -1)
    maxslp = 20; /* reasonable default? */
#endif

  pagesize = getpagesize();
  /* #endif HAVE_LIBKVM_GETPROCS && (HAVE_STRUCT_KINFO_PROC_FREEBSD ||
   * HAVE_STRUCT_KINFO_PROC_OPENBSD || HAVE_STRUCT_KINFO_PROC2_NETBSD) */

#elif HAVE_PROCINFO_H
  pagesize = getpagesize();
#endif /* HAVE_PROCINFO_H */

  return 0;
} /* int ps_init */

static int process_resource(procstat_t const *ps, procstat_entry_t const *pse,
                            label_set_t *ret_resource) {
  bool have_id = false;

#if HAVE_REGEX_H
  if (ps->re == NULL && strlen(ps->name) > 0) {
    label_set_add(ret_resource, "process.executable.name", ps->name);
    have_id = true;
  }
#else
  if (strlen(ps->name) > 0) {
    label_set_add(ret_resource, "process.executable.name", ps->name);
    have_id = true;
  }
#endif

  if (pse->command_line != NULL) {
    label_set_add(ret_resource, "process.command_line", pse->command_line);
    have_id = true;
  }

  if (pse->id != 0) {
    char pid[64] = "";
    ssnprintf(pid, sizeof(pid), "%lu", pse->id);

    label_set_add(ret_resource, "process.pid", pid);
    have_id = true;
  }

  return have_id ? 0 : -1;
}

static void ps_dispatch_cpu(label_set_t resource, procstat_entry_t const *pse) {
  metric_family_t fam_cpu_time = {
      .name = "process.cpu.time",
      .help = "Total CPU seconds broken down by different states.",
      .unit = "s",
      .type = METRIC_TYPE_FPCOUNTER,
      .resource = resource,
  };
  metric_family_append(&fam_cpu_time, "state", "user",
                       (value_t){.derive = pse->cpu_user_counter}, NULL);
  metric_family_append(&fam_cpu_time, "state", "system",
                       (value_t){.derive = pse->cpu_system_counter}, NULL);
  plugin_dispatch_metric_family(&fam_cpu_time);
  metric_family_metric_reset(&fam_cpu_time);

  if (isnan(pse->cpu_user_rate) && isnan(pse->cpu_system_rate)) {
    return;
  }

  gauge_t cpus = (gauge_t)sysconf(_SC_NPROCESSORS_ONLN);
  gauge_t factor = 1.0 / (cpus * 1000000.0);

  metric_family_t fam_cpu_util = {
      .name = "process.cpu.utilization",
      .help =
          "Difference in process.cpu.time since the last measurement, divided "
          "by the elapsed time and number of CPUs available to the process.",
      .unit = "1",
      .type = METRIC_TYPE_GAUGE,
      .resource = resource,
  };
  metric_family_append(&fam_cpu_util, "state", "user",
                       (value_t){.gauge = pse->cpu_user_rate * factor}, NULL);
  metric_family_append(&fam_cpu_util, "state", "system",
                       (value_t){.gauge = pse->cpu_system_rate * factor}, NULL);
  plugin_dispatch_metric_family(&fam_cpu_util);
  metric_family_metric_reset(&fam_cpu_util);
}

static void ps_dispatch_delay(label_set_t resource,
                              procstat_entry_t const *pse) {
  metric_family_t fam_delay = {
      .name = "process.delay.time",
      .help = "Time the process spend waiting for external components.",
      .unit = "ns",
      .type = METRIC_TYPE_COUNTER,
      .resource = resource,
  };
  struct {
    char *type_label;
    value_t value;
  } metrics[] = {
      {
          .type_label = "cpu",
          .value.derive = pse->delay_cpu,
      },
      {
          .type_label = "blkio",
          .value.derive = pse->delay_blkio,
      },
      {
          .type_label = "swapin",
          .value.derive = pse->delay_swapin,
      },
      {
          .type_label = "freepages",
          .value.derive = pse->delay_freepages,
      },
  };
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(metrics); i++) {
    metric_family_append(&fam_delay, "type", metrics[i].type_label,
                         metrics[i].value, NULL);
  }
  plugin_dispatch_metric_family(&fam_delay);
  metric_family_metric_reset(&fam_delay);
}

static void ps_dispatch_procstat_entry(procstat_t const *ps,
                                       procstat_entry_t const *pse) {
  label_set_t resource = {0};
  int status = process_resource(ps, pse, &resource);
  if (status != 0) {
    ERROR("processes plugin: Creating resource attributes failed.");
  } else {
    strbuf_t buf = STRBUF_CREATE;
    label_set_format(&buf, resource);
    DEBUG("processes plugin: resource = %s", buf.ptr);
    STRBUF_DESTROY(buf);
  }

  ps_dispatch_cpu(resource, pse);

  metric_family_t fam_rss = {
      .name = "process.memory.usage",
      .help = "The amount of physical memory in use (RSS).",
      .unit = "By",
      .type = METRIC_TYPE_GAUGE,
      .resource = resource,
  };
  metric_family_metric_append(&fam_rss, (metric_t){
                                            .value.gauge = pse->vmem_rss,
                                        });
  plugin_dispatch_metric_family(&fam_rss);
  metric_family_metric_reset(&fam_rss);

  metric_family_t fam_vmem = {
      .name = "process.memory.virtual",
      .help = "The amount of committed virtual memory.",
      .unit = "By",
      .type = METRIC_TYPE_GAUGE,
      .resource = resource,
  };
  metric_family_metric_append(&fam_vmem, (metric_t){
                                             .value.gauge = pse->vmem_size,
                                         });
  plugin_dispatch_metric_family(&fam_vmem);
  metric_family_metric_reset(&fam_vmem);

  metric_family_t fam_stack = {
      .name = "process.memory.stack",
      .help = "The size of the process' stack.",
      .unit = "By",
      .type = METRIC_TYPE_GAUGE,
      .resource = resource,
  };
  metric_family_metric_append(&fam_stack, (metric_t){
                                              .value.gauge = pse->stack_size,
                                          });
  plugin_dispatch_metric_family(&fam_stack);
  metric_family_metric_reset(&fam_stack);

  if (pse->io_rchar != -1 && pse->io_wchar != -1) {
    metric_family_t fam_disk = {
        .name = "process.io",
        .help = "The amount of data transferred by the process.",
        .unit = "By",
        .type = METRIC_TYPE_COUNTER,
        .resource = resource,
    };
    metric_family_append(&fam_disk, "direction", "read",
                         (value_t){.derive = pse->io_rchar}, NULL);
    metric_family_append(&fam_disk, "direction", "write",
                         (value_t){.derive = pse->io_wchar}, NULL);
    plugin_dispatch_metric_family(&fam_disk);
    metric_family_metric_reset(&fam_disk);
  }

  if (pse->io_syscr != -1 && pse->io_syscw != -1) {
    metric_family_t fam_disk = {
        .name = "process.operations",
        .help = "The number of I/O operations performed by the process.",
        .unit = "{operation}",
        .type = METRIC_TYPE_COUNTER,
        .resource = resource,
    };
    metric_family_append(&fam_disk, "direction", "read",
                         (value_t){.derive = pse->io_syscr}, NULL);
    metric_family_append(&fam_disk, "direction", "write",
                         (value_t){.derive = pse->io_syscw}, NULL);
    plugin_dispatch_metric_family(&fam_disk);
    metric_family_metric_reset(&fam_disk);
  }

  if (pse->io_diskr != -1 && pse->io_diskw != -1) {
    metric_family_t fam_disk = {
        .name = "process.disk.io",
        .help = "Disk bytes transferred.",
        .unit = "By",
        .type = METRIC_TYPE_COUNTER,
        .resource = resource,
    };
    metric_family_append(&fam_disk, "direction", "read",
                         (value_t){.derive = pse->io_diskr}, NULL);
    metric_family_append(&fam_disk, "direction", "write",
                         (value_t){.derive = pse->io_diskw}, NULL);
    plugin_dispatch_metric_family(&fam_disk);
    metric_family_metric_reset(&fam_disk);
  }

  metric_family_t fam_threads = {
      .name = "process.threads",
      .help = "Process threads count.",
      .unit = "{thread}",
      .type = METRIC_TYPE_GAUGE,
      .resource = resource,
  };
  metric_family_metric_append(&fam_threads, (metric_t){
                                                .value.gauge = pse->num_lwp,
                                            });
  plugin_dispatch_metric_family(&fam_threads);
  metric_family_metric_reset(&fam_threads);

  metric_family_t fam_fd = {
      .name = "process.open_file_descriptors",
      .help = "Number of file descriptors in use by the process.",
      .unit = "{count}",
      .type = METRIC_TYPE_GAUGE,
      .resource = resource,
  };
  metric_family_metric_append(&fam_fd, (metric_t){
                                           .value.gauge = pse->num_fd,
                                       });
  plugin_dispatch_metric_family(&fam_fd);
  metric_family_metric_reset(&fam_fd);

  if (pse->cswitch_vol != -1 && pse->cswitch_invol != -1) {
    metric_family_t fam_cswitch = {
        .name = "process.context_switches",
        .help = "Number of times the process has been context switched.",
        .unit = "{count}",
        .type = METRIC_TYPE_COUNTER,
        .resource = resource,
    };
    metric_family_append(&fam_cswitch, "type", "voluntary",
                         (value_t){.derive = pse->cswitch_vol}, NULL);
    metric_family_append(&fam_cswitch, "type", "involuntary",
                         (value_t){.derive = pse->cswitch_invol}, NULL);
    plugin_dispatch_metric_family(&fam_cswitch);
    metric_family_metric_reset(&fam_cswitch);
  }

  if (pse->vmem_majflt_counter != -1 && pse->vmem_minflt_counter != -1) {
    metric_family_t fam_pgfault = {
        .name = "process.paging.faults",
        .help = "Number of page faults the process has made.",
        .unit = "{fault}",
        .type = METRIC_TYPE_COUNTER,
        .resource = resource,
    };
    metric_family_append(&fam_pgfault, "type", "major",
                         (value_t){.derive = pse->vmem_majflt_counter}, NULL);
    metric_family_append(&fam_pgfault, "type", "minor",
                         (value_t){.derive = pse->vmem_minflt_counter}, NULL);
    plugin_dispatch_metric_family(&fam_pgfault);
    metric_family_metric_reset(&fam_pgfault);
  }

  if (pse->has_delay) {
    ps_dispatch_delay(resource, pse);
  }
}

/* submit info about specific process (e.g.: memory taken, cpu usage, etc..)
 */
static void ps_submit_proc_list(procstat_t *ps) {
  for (procstat_entry_t *pse = ps->instances; pse != NULL; pse = pse->next) {
    ps_dispatch_procstat_entry(ps, pse);
  }

#if 0
  sstrncpy(vl.type, "ps_data", sizeof(vl.type));
  vl.values[0].gauge = pse->vmem_data;
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ps_code", sizeof(vl.type));
  vl.values[0].gauge = pse->vmem_code;
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ps_count", sizeof(vl.type));
  vl.values[0].gauge = ps->num_proc;
  vl.values[1].gauge = pse->num_lwp;
  vl.values_len = 2;
  plugin_dispatch_values(&vl);
#endif
} /* void ps_submit_proc_list */

#if KERNEL_LINUX || KERNEL_SOLARIS
static int dispatch_fork_rate(counter_t value) {
  metric_family_t fam = {
      .name = "system.processes.created",
      .help = "Total number of processes created over uptime of the host",
      .unit = "{process}",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_metric_append(&fam, (metric_t){
                                        .value = {.counter = value},
                                    });
  int err = plugin_dispatch_metric_family(&fam);
  metric_family_metric_reset(&fam);
  return err;
}
#endif /* KERNEL_LINUX || KERNEL_SOLARIS*/

/* ------- additional functions for KERNEL_LINUX/HAVE_THREAD_INFO ------- */
#if KERNEL_LINUX
static int ps_read_tasks_status(process_entry_t *ps) {
  char dirname[64];
  DIR *dh;
  char filename[64];
  FILE *fh;
  struct dirent *ent;
  derive_t cswitch_vol = 0;
  derive_t cswitch_invol = 0;
  char buffer[1024];
  char *fields[8];
  int numfields;

  snprintf(dirname, sizeof(dirname), "/proc/%li/task", ps->id);

  if ((dh = opendir(dirname)) == NULL) {
    DEBUG("Failed to open directory `%s'", dirname);
    return -1;
  }

  while ((ent = readdir(dh)) != NULL) {
    char *tpid;

    if (!isdigit((int)ent->d_name[0]))
      continue;

    tpid = ent->d_name;

    int r = snprintf(filename, sizeof(filename), "/proc/%li/task/%s/status",
                     ps->id, tpid);
    if ((size_t)r >= sizeof(filename)) {
      DEBUG("Filename too long: `%s'", filename);
      continue;
    }

    if ((fh = fopen(filename, "r")) == NULL) {
      DEBUG("Failed to open file `%s'", filename);
      continue;
    }

    while (fgets(buffer, sizeof(buffer), fh) != NULL) {
      derive_t tmp;
      char *endptr;

      if (strncmp(buffer, "voluntary_ctxt_switches", 23) != 0 &&
          strncmp(buffer, "nonvoluntary_ctxt_switches", 26) != 0)
        continue;

      numfields = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));

      if (numfields < 2)
        continue;

      errno = 0;
      endptr = NULL;
      tmp = (derive_t)strtoll(fields[1], &endptr, /* base = */ 10);
      if ((errno == 0) && (endptr != fields[1])) {
        if (strncmp(buffer, "voluntary_ctxt_switches", 23) == 0) {
          cswitch_vol += tmp;
        } else if (strncmp(buffer, "nonvoluntary_ctxt_switches", 26) == 0) {
          cswitch_invol += tmp;
        }
      }
    } /* while (fgets) */

    if (fclose(fh)) {
      WARNING("processes: fclose: %s", STRERRNO);
    }
  }
  closedir(dh);

  ps->cswitch_vol = cswitch_vol;
  ps->cswitch_invol = cswitch_invol;

  return 0;
} /* int *ps_read_tasks_status */

/* Read data from /proc/pid/status */
static int ps_read_status(long pid, process_entry_t *ps) {
  FILE *fh;
  char buffer[1024];
  char filename[64];
  unsigned long lib = 0;
  unsigned long exe = 0;
  unsigned long data = 0;
  unsigned long threads = 0;
  char *fields[8];
  int numfields;

  snprintf(filename, sizeof(filename), "/proc/%li/status", pid);
  if ((fh = fopen(filename, "r")) == NULL)
    return -1;

  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    unsigned long tmp;
    char *endptr;

    if (strncmp(buffer, "Vm", 2) != 0 && strncmp(buffer, "Threads", 7) != 0)
      continue;

    numfields = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));

    if (numfields < 2)
      continue;

    errno = 0;
    endptr = NULL;
    tmp = strtoul(fields[1], &endptr, /* base = */ 10);
    if ((errno == 0) && (endptr != fields[1])) {
      if (strncmp(buffer, "VmData", 6) == 0) {
        data = tmp;
      } else if (strncmp(buffer, "VmLib", 5) == 0) {
        lib = tmp;
      } else if (strncmp(buffer, "VmExe", 5) == 0) {
        exe = tmp;
      } else if (strncmp(buffer, "Threads", 7) == 0) {
        threads = tmp;
      }
    }
  } /* while (fgets) */

  if (fclose(fh)) {
    WARNING("processes: fclose: %s", STRERRNO);
  }

  ps->vmem_data = data * 1024;
  ps->vmem_code = (exe + lib) * 1024;
  if (threads != 0)
    ps->num_lwp = threads;

  return 0;
} /* int *ps_read_status */

static int ps_read_io(process_entry_t *ps) {
  FILE *fh;
  char buffer[1024];
  char filename[64];

  char *fields[8];
  int numfields;

  snprintf(filename, sizeof(filename), "/proc/%li/io", ps->id);
  if ((fh = fopen(filename, "r")) == NULL) {
    DEBUG("ps_read_io: Failed to open file `%s'", filename);
    return -1;
  }

  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    derive_t *val = NULL;
    long long tmp;
    char *endptr;

    if (strncasecmp(buffer, "rchar:", 6) == 0)
      val = &(ps->io_rchar);
    else if (strncasecmp(buffer, "wchar:", 6) == 0)
      val = &(ps->io_wchar);
    else if (strncasecmp(buffer, "syscr:", 6) == 0)
      val = &(ps->io_syscr);
    else if (strncasecmp(buffer, "syscw:", 6) == 0)
      val = &(ps->io_syscw);
    else if (strncasecmp(buffer, "read_bytes:", 11) == 0)
      val = &(ps->io_diskr);
    else if (strncasecmp(buffer, "write_bytes:", 12) == 0)
      val = &(ps->io_diskw);
    else
      continue;

    numfields = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));

    if (numfields < 2)
      continue;

    errno = 0;
    endptr = NULL;
    tmp = strtoll(fields[1], &endptr, /* base = */ 10);
    if ((errno != 0) || (endptr == fields[1]))
      *val = -1;
    else
      *val = (derive_t)tmp;
  } /* while (fgets) */

  if (fclose(fh)) {
    WARNING("processes: fclose: %s", STRERRNO);
  }
  return 0;
} /* int ps_read_io (...) */

static int ps_count_maps(pid_t pid) {
  FILE *fh;
  char buffer[1024];
  char filename[64];
  int count = 0;

  snprintf(filename, sizeof(filename), "/proc/%d/maps", pid);
  if ((fh = fopen(filename, "r")) == NULL) {
    DEBUG("ps_count_maps: Failed to open file `%s'", filename);
    return -1;
  }

  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    if (strchr(buffer, '\n')) {
      count++;
    }
  } /* while (fgets) */

  if (fclose(fh)) {
    WARNING("processes: fclose: %s", STRERRNO);
  }
  return count;
} /* int ps_count_maps (...) */

static int ps_count_fd(int pid) {
  char dirname[64];
  DIR *dh;
  struct dirent *ent;
  int count = 0;

  snprintf(dirname, sizeof(dirname), "/proc/%i/fd", pid);

  if ((dh = opendir(dirname)) == NULL) {
    DEBUG("Failed to open directory `%s'", dirname);
    return -1;
  }
  while ((ent = readdir(dh)) != NULL) {
    if (!isdigit((int)ent->d_name[0]))
      continue;
    else
      count++;
  }
  closedir(dh);

  return (count >= 1) ? count : 1;
} /* int ps_count_fd (pid) */

#if HAVE_LIBTASKSTATS
static int ps_delay(process_entry_t *ps) {
  if (taskstats_handle == NULL) {
    return ENOTCONN;
  }

  int status = ts_delay_by_tgid(taskstats_handle, (uint32_t)ps->id, &ps->delay);
  if (status == EPERM) {
    static c_complain_t c;
#if defined(HAVE_SYS_CAPABILITY_H) && defined(CAP_NET_ADMIN)
    if (check_capability(CAP_NET_ADMIN) != 0) {
      if (getuid() == 0) {
        c_complain(
            LOG_ERR, &c,
            "processes plugin: Reading Delay Accounting metric failed: %s. "
            "collectd is running as root, but missing the CAP_NET_ADMIN "
            "capability. The most common cause for this is that the init "
            "system is dropping capabilities.",
            STRERROR(status));
      } else {
        c_complain(
            LOG_ERR, &c,
            "processes plugin: Reading Delay Accounting metric failed: %s. "
            "collectd is not running as root and missing the CAP_NET_ADMIN "
            "capability. Either run collectd as root or grant it the "
            "CAP_NET_ADMIN capability using \"setcap cap_net_admin=ep " PREFIX
            "/sbin/collectd\".",
            STRERROR(status));
      }
    } else {
      ERROR("processes plugin: ts_delay_by_tgid failed: %s. The CAP_NET_ADMIN "
            "capability is available (I checked), so this error is utterly "
            "unexpected.",
            STRERROR(status));
    }
#else
    c_complain(LOG_ERR, &c,
               "processes plugin: Reading Delay Accounting metric failed: %s. "
               "Reading Delay Accounting metrics requires root privileges.",
               STRERROR(status));
#endif
    return status;
  } else if (status != 0) {
    ERROR("processes plugin: ts_delay_by_tgid failed: %s", STRERROR(status));
    return status;
  }

  return 0;
}
#endif

static void ps_fill_details(const procstat_t *ps, process_entry_t *entry) {
  if (entry->has_io == false) {
    ps_read_io(entry);
    entry->has_io = true;
  }

  if (ps->report_ctx_switch) {
    if (entry->has_cswitch == false) {
      ps_read_tasks_status(entry);
      entry->has_cswitch = true;
    }
  }

  if (ps->report_maps_num) {
    int num_maps;
    if (entry->has_maps == false && (num_maps = ps_count_maps(entry->id)) > 0) {
      entry->num_maps = num_maps;
    }
    entry->has_maps = true;
  }

  if (ps->report_fd_num) {
    int num_fd;
    if (entry->has_fd == false && (num_fd = ps_count_fd(entry->id)) > 0) {
      entry->num_fd = num_fd;
    }
    entry->has_fd = true;
  }

#if HAVE_LIBTASKSTATS
  if (ps->report_delay && !entry->has_delay) {
    if (ps_delay(entry) == 0) {
      entry->has_delay = true;
    }
  }
#endif
} /* void ps_fill_details (...) */

/* ps_read_process reads process counters on Linux. */
static int ps_read_process(long pid, process_entry_t *ps, char *state) {
  char filename[64];
  char buffer[1024];

  char *fields[64];
  char fields_len;

  size_t buffer_len;

  char *buffer_ptr;
  size_t name_start_pos;
  size_t name_end_pos;
  size_t name_len;

  long long unsigned vmem_size;
  long long unsigned vmem_rss;
  long long unsigned stack_size;

  ssize_t status;

  snprintf(filename, sizeof(filename), "/proc/%li/stat", pid);

  status = read_text_file_contents(filename, buffer, sizeof(buffer));
  if (status <= 0)
    return -1;
  buffer_len = (size_t)status;

  /* The name of the process is enclosed in parens. Since the name can
   * contain parens itself, spaces, numbers and pretty much everything
   * else, use these to determine the process name. We don't use
   * strchr(3) and strrchr(3) to avoid pointer arithmetic which would
   * otherwise be required to determine name_len. */
  name_start_pos = 0;
  while (name_start_pos < buffer_len && buffer[name_start_pos] != '(')
    name_start_pos++;

  name_end_pos = buffer_len - 1;
  while (name_end_pos > 0 && buffer[name_end_pos] != ')')
    name_end_pos--;

  /* Either '(' or ')' is not found or they are in the wrong order.
   * Anyway, something weird that shouldn't happen ever. */
  if (name_start_pos >= name_end_pos) {
    ERROR("processes plugin: name_start_pos = %" PRIsz
          " >= name_end_pos = %" PRIsz,
          name_start_pos, name_end_pos);
    return -1;
  }

  name_len = (name_end_pos - name_start_pos) - 1;
  if (name_len >= sizeof(ps->name))
    name_len = sizeof(ps->name) - 1;

  sstrncpy(ps->name, &buffer[name_start_pos + 1], name_len + 1);

  if ((buffer_len - name_end_pos) < 2)
    return -1;
  buffer_ptr = &buffer[name_end_pos + 2];

  fields_len = strsplit(buffer_ptr, fields, STATIC_ARRAY_SIZE(fields));
  if (fields_len < 22) {
    DEBUG("processes plugin: ps_read_process (pid = %li):"
          " `%s' has only %i fields..",
          pid, filename, fields_len);
    return -1;
  }

  *state = fields[0][0];

  if (*state == 'Z') {
    ps->num_lwp = 0;
    ps->num_proc = 0;
  } else {
    ps->num_lwp = strtoul(fields[17], /* endptr = */ NULL, /* base = */ 10);
    if ((ps_read_status(pid, ps)) != 0) {
      /* No VMem data */
      ps->vmem_data = -1;
      ps->vmem_code = -1;
      DEBUG("ps_read_process: did not get vmem data for pid %li", pid);
    }
    if (ps->num_lwp == 0)
      ps->num_lwp = 1;
    ps->num_proc = 1;
  }

  /* Leave the rest at zero if this is only a zombi */
  if (ps->num_proc == 0) {
    DEBUG("processes plugin: This is only a zombie: pid = %li; "
          "name = %s;",
          pid, ps->name);
    return 0;
  }

  long long cpu_user_counter = atoll(fields[11]);
  long long cpu_system_counter = atoll(fields[12]);
  vmem_size = atoll(fields[20]);
  vmem_rss = atoll(fields[21]);
  ps->vmem_minflt_counter = atol(fields[7]);
  ps->vmem_majflt_counter = atol(fields[9]);

  {
    unsigned long long stack_start = atoll(fields[25]);
    unsigned long long stack_ptr = atoll(fields[26]);

    stack_size = (stack_start > stack_ptr) ? stack_start - stack_ptr
                                           : stack_ptr - stack_start;
  }

  fpcounter_t clock_ticks = (fpcounter_t)sysconf(_SC_CLK_TCK);

  ps->cpu_user_counter = ((fpcounter_t)cpu_user_counter) / clock_ticks;
  ps->cpu_system_counter = ((fpcounter_t)cpu_system_counter) / clock_ticks;
  ps->vmem_size = (unsigned long)vmem_size;
  ps->vmem_rss = (unsigned long)(vmem_rss * pagesize_g);
  ps->stack_size = (unsigned long)stack_size;

  /* no data by default. May be filled by ps_fill_details () */
  ps->io_rchar = -1;
  ps->io_wchar = -1;
  ps->io_syscr = -1;
  ps->io_syscw = -1;
  ps->io_diskr = -1;
  ps->io_diskw = -1;

  ps->cswitch_vol = -1;
  ps->cswitch_invol = -1;
  ps->starttime = strtoull(fields[19], NULL, 10);

  /* success */
  return 0;
} /* int ps_read_process (...) */

static int procs_running(const char *buffer) {
  char id[] = "procs_running "; /* white space terminated */

  /* the data contains :
   * the literal string 'procs_running',
   * a whitespace
   * the number of running processes.
   * The parser does include the white-space character.
   */
  char *running = strstr(buffer, id);
  if (!running) {
    WARNING("'procs_running ' not found in /proc/stat");
    return -1;
  }
  running += strlen(id);

  char *endptr = NULL;
  long result = strtol(running, &endptr, 10);
  if ((*running != '\0') && ((*endptr == '\0') || (*endptr == '\n'))) {
    return result;
  }

  return -1;
}

static int read_fork_rate(const char *buffer) {
  char const *prefix = "processes ";

  char *processes = strstr(buffer, prefix);
  if (!processes) {
    ERROR("processes plugin: \"processes \" not found in /proc/stat");
    return ENOENT;
  }

  char *fields[2] = {0};
  const int expected = STATIC_ARRAY_SIZE(fields);
  int fields_num = strsplit(processes, fields, expected);
  if (fields_num != expected) {
    return EINVAL;
  }

  value_t value = {0};
  int status = parse_value(fields[1], &value, METRIC_TYPE_COUNTER);
  if (status != 0)
    return -1;

  return dispatch_fork_rate(value.counter);
}

static int dispatch_context_switch(counter_t value) {
  metric_family_t fam = {
      .name = "system.process.context_switches",
      .help = "Total number of context switches performed on the system.",
      .unit = "{count}",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_metric_append(&fam, (metric_t){
                                        .value = {.counter = value},
                                    });
  int err = plugin_dispatch_metric_family(&fam);
  metric_family_metric_reset(&fam);
  return err;
}

static int read_sys_ctxt_switch(const char *buffer) {
  char const *prefix = "ctxt ";

  char *processes = strstr(buffer, prefix);
  if (!processes) {
    ERROR("processes plugin: \"ctxt \" not found in /proc/stat");
    return ENOENT;
  }

  char *fields[2] = {0};
  const int expected = STATIC_ARRAY_SIZE(fields);
  int fields_num = strsplit(processes, fields, expected);
  if (fields_num != expected) {
    return EINVAL;
  }

  value_t value = {0};
  int status = parse_value(fields[1], &value, METRIC_TYPE_COUNTER);
  if (status != 0)
    return -1;

  return dispatch_context_switch(value.counter);
}
#endif /*KERNEL_LINUX */

#if KERNEL_SOLARIS

/*
 * Reads process information on the Solaris OS. The information comes mainly
 * from
 * /proc/PID/status, /proc/PID/psinfo and /proc/PID/usage
 * The values for input and ouput chars are calculated "by hand"
 * Added a few "solaris" specific process states as well
 */
static int ps_read_process(long pid, process_entry_t *ps, char *state) {
  char filename[64];
  char f_psinfo[64], f_usage[64];
  char *buffer;

  pstatus_t *myStatus;
  psinfo_t *myInfo;
  prusage_t *myUsage;

  snprintf(filename, sizeof(filename), "/proc/%li/status", pid);
  snprintf(f_psinfo, sizeof(f_psinfo), "/proc/%li/psinfo", pid);
  snprintf(f_usage, sizeof(f_usage), "/proc/%li/usage", pid);

  buffer = scalloc(1, sizeof(pstatus_t));
  read_file_contents(filename, buffer, sizeof(pstatus_t));
  myStatus = (pstatus_t *)buffer;

  buffer = scalloc(1, sizeof(psinfo_t));
  read_file_contents(f_psinfo, buffer, sizeof(psinfo_t));
  myInfo = (psinfo_t *)buffer;

  buffer = scalloc(1, sizeof(prusage_t));
  read_file_contents(f_usage, buffer, sizeof(prusage_t));
  myUsage = (prusage_t *)buffer;

  sstrncpy(ps->name, myInfo->pr_fname, sizeof(myInfo->pr_fname));
  ps->num_lwp = myStatus->pr_nlwp;
  if (myInfo->pr_wstat != 0) {
    ps->num_proc = 0;
    ps->num_lwp = 0;
    *state = (char)'Z';

    sfree(myStatus);
    sfree(myInfo);
    sfree(myUsage);
    return 0;
  } else {
    ps->num_proc = 1;
    ps->num_lwp = myInfo->pr_nlwp;
  }

  /* Convert system time and user time from nanoseconds to seconds. */
  ps->cpu_system_counter = ((fpcounter_t)myStatus->pr_stime.tv_nsec) / 1e9;
  ps->cpu_user_counter = ((fpcounter_t)myStatus->pr_utime.tv_nsec) / 1e9;

  /*
   * Convert rssize from KB to bytes to be consistent w/ the linux module
   */
  ps->vmem_rss = myInfo->pr_rssize * 1024;
  ps->vmem_size = myInfo->pr_size * 1024;
  ps->vmem_minflt_counter = myUsage->pr_minf;
  ps->vmem_majflt_counter = myUsage->pr_majf;

  /*
   * TODO: Data and code segment calculations for Solaris
   */

  ps->vmem_data = -1;
  ps->vmem_code = -1;
  ps->stack_size = myStatus->pr_stksize;

  /*
   * TODO: File descriptor count for Solaris
   */
  ps->num_fd = 0;

  /* Number of memory mappings */
  ps->num_maps = 0;

  /*
   * Calculating input/ouput chars
   * Formula used is total chars / total blocks => chars/block
   * then convert input/output blocks to chars
   */
  ulong_t tot_chars = myUsage->pr_ioch;
  ulong_t tot_blocks = myUsage->pr_inblk + myUsage->pr_oublk;
  ulong_t chars_per_block = 1;
  if (tot_blocks != 0)
    chars_per_block = tot_chars / tot_blocks;
  ps->io_rchar = myUsage->pr_inblk * chars_per_block;
  ps->io_wchar = myUsage->pr_oublk * chars_per_block;
  ps->io_syscr = myUsage->pr_sysc;
  ps->io_syscw = myUsage->pr_sysc;
  ps->io_diskr = -1;
  ps->io_diskw = -1;

  /*
   * TODO: context switch counters for Solaris
   */
  ps->cswitch_vol = -1;
  ps->cswitch_invol = -1;

  /*
   * TODO: Find way of setting BLOCKED and PAGING status
   */

  *state = (char)'R';
  if (myStatus->pr_flags & PR_ASLEEP)
    *state = (char)'S';
  else if (myStatus->pr_flags & PR_STOPPED)
    *state = (char)'T';
  else if (myStatus->pr_flags & PR_DETACH)
    *state = (char)'E';
  else if (myStatus->pr_flags & PR_DAEMON)
    *state = (char)'A';
  else if (myStatus->pr_flags & PR_ISSYS)
    *state = (char)'Y';
  else if (myStatus->pr_flags & PR_ORPHAN)
    *state = (char)'O';

  sfree(myStatus);
  sfree(myInfo);
  sfree(myUsage);

  return 0;
}

/*
 * Reads the number of threads created since the last reboot. On Solaris these
 * are retrieved from kstat (module cpu, name sys, class misc, stat nthreads).
 * The result is the sum for all the threads created on each cpu
 */
static int read_fork_rate(void) {
  extern kstat_ctl_t *kc;
  counter_t sum = 0;

  if (kc == NULL)
    return -1;

  for (kstat_t *ksp_chain = kc->kc_chain; ksp_chain != NULL;
       ksp_chain = ksp_chain->ks_next) {
    if ((strcmp(ksp_chain->ks_module, "cpu") == 0) &&
        (strcmp(ksp_chain->ks_name, "sys") == 0) &&
        (strcmp(ksp_chain->ks_class, "misc") == 0)) {
      long long tmp;

      kstat_read(kc, ksp_chain, NULL);

      tmp = get_kstat_value(ksp_chain, "nthreads");
      if (tmp != -1LL)
        sum += tmp;
    }
  }

  return dispatch_fork_rate(sum);
}
#endif /* KERNEL_SOLARIS */

#if HAVE_THREAD_INFO
static int mach_get_task_name(task_t t, int *pid, char *name,
                              size_t name_max_len) {
  int mib[4];

  struct kinfo_proc kp;
  size_t kp_size;

  mib[0] = CTL_KERN;
  mib[1] = KERN_PROC;
  mib[2] = KERN_PROC_PID;

  if (pid_for_task(t, pid) != KERN_SUCCESS)
    return -1;
  mib[3] = *pid;

  kp_size = sizeof(kp);
  if (sysctl(mib, 4, &kp, &kp_size, NULL, 0) != 0)
    return -1;

  if (name_max_len > (MAXCOMLEN + 1))
    name_max_len = MAXCOMLEN + 1;

  strncpy(name, kp.kp_proc.p_comm, name_max_len - 1);
  name[name_max_len - 1] = '\0';

  DEBUG("pid = %i; name = %s;", *pid, name);

  /* We don't do the special handling for `p_comm == "LaunchCFMApp"' as
   * `top' does it, because it is a lot of work and only used when
   * debugging. -octo */

  return 0;
}
#endif /* HAVE_THREAD_INFO */
/* end of additional functions for KERNEL_LINUX/HAVE_THREAD_INFO */

/* do actual readings from kernel */
#if HAVE_THREAD_INFO
static int ps_read_thread_info(gauge_t process_count[static STATE_MAX]) {
  kern_return_t status;

  processor_set_t port_pset_priv;

  task_array_t task_list;
  mach_msg_type_number_t task_list_len;

  int task_pid;
  char task_name[MAXCOMLEN + 1];

  thread_act_array_t thread_list;
  mach_msg_type_number_t thread_list_len;
  thread_basic_info_data_t thread_data;
  mach_msg_type_number_t thread_data_len;

  process_count[STATE_BLOCKED] = 0;
  process_count[STATE_DEFUNCT] = 0;
  process_count[STATE_RUNNING] = 0;
  process_count[STATE_SLEEPING] = 0;
  process_count[STATE_STOPPED] = 0;

  procstat_t *ps;
  process_entry_t pse;

  ps_list_reset();

  /*
   * The Mach-concept is a little different from the traditional UNIX
   * concept: All the work is done in threads. Threads are contained in
   * `tasks'. Therefore, `task status' doesn't make much sense, since
   * it's actually a `thread status'.
   * Tasks are assigned to sets of processors, so that's where you go to
   * get a list.
   */
  for (mach_msg_type_number_t pset = 0; pset < pset_list_len; pset++) {
    if ((status = host_processor_set_priv(port_host_self, pset_list[pset],
                                          &port_pset_priv)) != KERN_SUCCESS) {
      ERROR("host_processor_set_priv failed: %s\n", mach_error_string(status));
      continue;
    }

    if ((status = processor_set_tasks(port_pset_priv, &task_list,
                                      &task_list_len)) != KERN_SUCCESS) {
      ERROR("processor_set_tasks failed: %s\n", mach_error_string(status));
      mach_port_deallocate(port_task_self, port_pset_priv);
      continue;
    }

    for (mach_msg_type_number_t task = 0; task < task_list_len; task++) {
      ps = NULL;
      if (mach_get_task_name(task_list[task], &task_pid, task_name,
                             PROCSTAT_NAME_LEN) == 0) {
        /* search for at least one match */
        for (ps = list_head_g; ps != NULL; ps = ps->next)
          /* FIXME: cmdline should be here instead of NULL */
          if (ps_list_match(task_name, NULL, ps) == 1)
            break;
      }

      /* Collect more detailed statistics for this process */
      if (ps != NULL) {
        task_basic_info_data_t task_basic_info;
        mach_msg_type_number_t task_basic_info_len;
        task_events_info_data_t task_events_info;
        mach_msg_type_number_t task_events_info_len;
        task_absolutetime_info_data_t task_absolutetime_info;
        mach_msg_type_number_t task_absolutetime_info_len;

        memset(&pse, '\0', sizeof(pse));
        pse.id = task_pid;

        task_basic_info_len = TASK_BASIC_INFO_COUNT;
        status = task_info(task_list[task], TASK_BASIC_INFO,
                           (task_info_t)&task_basic_info, &task_basic_info_len);
        if (status != KERN_SUCCESS) {
          ERROR("task_info failed: %s", mach_error_string(status));
          continue; /* with next thread_list */
        }

        task_events_info_len = TASK_EVENTS_INFO_COUNT;
        status =
            task_info(task_list[task], TASK_EVENTS_INFO,
                      (task_info_t)&task_events_info, &task_events_info_len);
        if (status != KERN_SUCCESS) {
          ERROR("task_info failed: %s", mach_error_string(status));
          continue; /* with next thread_list */
        }

        task_absolutetime_info_len = TASK_ABSOLUTETIME_INFO_COUNT;
        status = task_info(task_list[task], TASK_ABSOLUTETIME_INFO,
                           (task_info_t)&task_absolutetime_info,
                           &task_absolutetime_info_len);
        if (status != KERN_SUCCESS) {
          ERROR("task_info failed: %s", mach_error_string(status));
          continue; /* with next thread_list */
        }

        /* Command line not implemented */
        pse.command_line = NULL;

        pse.num_proc++;
        pse.vmem_size = task_basic_info.virtual_size;
        pse.vmem_rss = task_basic_info.resident_size;
        /* Does not seem to be easily exposed */
        pse.vmem_data = 0;
        pse.vmem_code = 0;

        pse.io_rchar = -1;
        pse.io_wchar = -1;
        pse.io_syscr = -1;
        pse.io_syscw = -1;
        pse.io_diskr = -1;
        pse.io_diskw = -1;

        /* File descriptor count not implemented */
        pse.num_fd = 0;

        /* Number of memory mappings */
        pse.num_maps = 0;

        pse.vmem_minflt_counter = task_events_info.cow_faults;
        pse.vmem_majflt_counter = task_events_info.faults;

        pse.cpu_user_counter =
            ((fpcounter_t)task_absolutetime_info.total_user) / 1e9;
        pse.cpu_system_counter =
            ((fpcounter_t)task_absolutetime_info.total_system) / 1e9;

        /* context switch counters not implemented */
        pse.cswitch_vol = -1;
        pse.cswitch_invol = -1;
      }

      status = task_threads(task_list[task], &thread_list, &thread_list_len);
      if (status != KERN_SUCCESS) {
        /* Apple's `top' treats this case a zombie. It
         * makes sense to some extend: A `zombie'
         * thread is nonsense, since the task/process
         * is dead. */
        process_count[STATE_DEFUNCT]++;
        DEBUG("task_threads failed: %s", mach_error_string(status));
        if (task_list[task] != port_task_self)
          mach_port_deallocate(port_task_self, task_list[task]);
        continue; /* with next task_list */
      }

      for (mach_msg_type_number_t thread = 0; thread < thread_list_len;
           thread++) {
        thread_data_len = THREAD_BASIC_INFO_COUNT;
        status = thread_info(thread_list[thread], THREAD_BASIC_INFO,
                             (thread_info_t)&thread_data, &thread_data_len);
        if (status != KERN_SUCCESS) {
          ERROR("thread_info failed: %s", mach_error_string(status));
          if (task_list[task] != port_task_self)
            mach_port_deallocate(port_task_self, thread_list[thread]);
          continue; /* with next thread_list */
        }

        if (ps != NULL)
          pse.num_lwp++;

        switch (thread_data.run_state) {
        case TH_STATE_RUNNING:
          process_count[STATE_RUNNING]++;
          break;
        case TH_STATE_STOPPED:
        /* What exactly is `halted'? */
        case TH_STATE_HALTED:
          process_count[STATE_STOPPED]++;
          break;
        case TH_STATE_WAITING:
          process_count[STATE_SLEEPING]++;
          break;
        case TH_STATE_UNINTERRUPTIBLE:
          process_count[STATE_BLOCKED]++;
          break;
        /* There is no `zombie' case here,
         * since there are no zombie-threads.
         * There's only zombie tasks, which are
         * handled above. */
        default:
          WARNING("Unknown thread status: %i", thread_data.run_state);
          break;
        } /* switch (thread_data.run_state) */

        if (task_list[task] != port_task_self) {
          status = mach_port_deallocate(port_task_self, thread_list[thread]);
          if (status != KERN_SUCCESS)
            ERROR("mach_port_deallocate failed: %s", mach_error_string(status));
        }
      } /* for (thread_list) */

      if ((status = vm_deallocate(port_task_self, (vm_address_t)thread_list,
                                  thread_list_len * sizeof(thread_act_t))) !=
          KERN_SUCCESS) {
        ERROR("vm_deallocate failed: %s", mach_error_string(status));
      }
      thread_list = NULL;
      thread_list_len = 0;

      /* Only deallocate the task port, if it isn't our own.
       * Don't know what would happen in that case, but this
       * is what Apple's top does.. ;) */
      if (task_list[task] != port_task_self) {
        status = mach_port_deallocate(port_task_self, task_list[task]);
        if (status != KERN_SUCCESS)
          ERROR("mach_port_deallocate failed: %s", mach_error_string(status));
      }

      if (ps != NULL)
        ps_list_add(task_name, &pse);
    } /* for (task_list) */

    if ((status = vm_deallocate(port_task_self, (vm_address_t)task_list,
                                task_list_len * sizeof(task_t))) !=
        KERN_SUCCESS) {
      ERROR("vm_deallocate failed: %s", mach_error_string(status));
    }
    task_list = NULL;
    task_list_len = 0;

    if ((status = mach_port_deallocate(port_task_self, port_pset_priv)) !=
        KERN_SUCCESS) {
      ERROR("mach_port_deallocate failed: %s", mach_error_string(status));
    }
  } /* for (pset_list) */

  for (ps = list_head_g; ps != NULL; ps = ps->next)
    ps_submit_proc_list(ps);

  return 0;
}

#elif KERNEL_LINUX
static int ps_read_linux(gauge_t process_count[static STATE_MAX]) {
  process_count[STATE_RUNNING] = 0;
  process_count[STATE_SLEEPING] = 0;
  process_count[STATE_DEFUNCT] = 0;
  process_count[STATE_STOPPED] = 0;
  process_count[STATE_PAGING] = 0;
  process_count[STATE_BLOCKED] = 0;

  ps_list_reset();

  DIR *proc = opendir("/proc");
  if (proc == NULL) {
    ERROR("Cannot open `/proc': %s", STRERRNO);
    return -1;
  }

  struct dirent *ent;
  while ((ent = readdir(proc)) != NULL) {
    if (!isdigit(ent->d_name[0]))
      continue;

    long pid = atol(ent->d_name);
    if (pid < 1) {
      continue;
    }

    process_entry_t pse = {
        .id = pid,
    };
    char state = 0;

    int status = ps_read_process(pid, &pse, &state);
    if (status != 0) {
      DEBUG("ps_read_process(%ld) failed: %d", pid, status);
      continue;
    }

    switch (state) {
    case 'R':
      /* ignored. We use procs_running() instead below. */
      break;
    case 'S':
      process_count[STATE_SLEEPING]++;
      break;
    case 'D':
      process_count[STATE_BLOCKED]++;
      break;
    case 'Z':
      process_count[STATE_DEFUNCT]++;
      break;
    case 'T':
      process_count[STATE_STOPPED]++;
      break;
    case 'W':
      process_count[STATE_PAGING]++;
      break;
    }

    ps_list_add(pse.name, &pse);
  }

  closedir(proc);

  char buffer[4096] = {0};
  ssize_t n = read_text_file_contents("/proc/stat", buffer, sizeof(buffer) - 1);
  if (n <= 0) {
    ERROR("processes plugin: reading \"/proc/stat\" failed.");
    return -1;
  }

  /* get procs_running from /proc/stat
   * scanning /proc/stat AND computing other process stats takes too much
   * time. Consequently, the number of running processes based on the
   * occurences of 'R' as character indicating the running state is typically
   * zero. Due to processes are actually changing state during the evaluation
   * of it's stat(s). The 'procs_running' number in /proc/stat on the other
   * hand is more accurate, and can be retrieved in a single 'read' call. */
  process_count[STATE_RUNNING] = (gauge_t)procs_running(buffer);

  for (procstat_t *ps = list_head_g; ps != NULL; ps = ps->next)
    ps_submit_proc_list(ps);

  read_fork_rate(buffer);
  if (report_sys_ctxt_switch) {
    read_sys_ctxt_switch(buffer);
  }

  return 0;
}

#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD
static int ps_read_freebsd(gauge_t process_count[static STATE_MAX]) {
  process_count[STATE_RUNNING] = 0;
  process_count[STATE_SLEEPING] = 0;
  process_count[STATE_DEFUNCT] = 0;
  process_count[STATE_STOPPED] = 0;
  process_count[STATE_BLOCKED] = 0;
  process_count[STATE_IDLE] = 0;
  process_count[STATE_WAIT] = 0;

  kvm_t *kd;
  char errbuf[_POSIX2_LINE_MAX];
  struct kinfo_proc *procs; /* array of processes */
  struct kinfo_proc *proc_ptr = NULL;
  int count; /* returns number of processes */

  ps_list_reset();

  /* Open the kvm interface, get a descriptor */
  kd = kvm_openfiles(NULL, "/dev/null", NULL, 0, errbuf);
  if (kd == NULL) {
    ERROR("processes plugin: Cannot open kvm interface: %s", errbuf);
    return 0;
  }

  /* Get the list of processes. */
  procs = kvm_getprocs(kd, KERN_PROC_ALL, 0, &count);
  if (procs == NULL) {
    ERROR("processes plugin: Cannot get kvm processes list: %s",
          kvm_geterr(kd));
    kvm_close(kd);
    return 0;
  }

  /* Iterate through the processes in kinfo_proc */
  for (int i = 0; i < count; i++) {
    /* Create only one process list entry per _process_, i.e.
     * filter out threads (duplicate PID entries). */
    if ((proc_ptr == NULL) || (proc_ptr->ki_pid != procs[i].ki_pid)) {
      char cmdline[CMDLINE_BUFFER_SIZE] = "";

      process_entry_t pse = {
          .id = procs[i].ki_pid,
      };

      proc_ptr = &(procs[i]);
      /* Don't probe system processes and processes without arguments */
      if (((procs[i].ki_flag & P_SYSTEM) == 0) && (procs[i].ki_args != NULL)) {
        char **argv;
        int argc;
        int status;

        /* retrieve the arguments */
        argv = kvm_getargv(kd, proc_ptr, /* nchr = */ 0);
        argc = 0;
        if ((argv != NULL) && (argv[0] != NULL)) {
          while (argv[argc] != NULL)
            argc++;

          status = strjoin(cmdline, sizeof(cmdline), argv, argc, " ");
          if (status < 0) {
            WARNING("processes plugin: Command line did not fit into buffer.");
          } else {
            pse.command_line = cmdline;
          }
        }
      } /* if (process has argument list) */

      pse.num_proc = 1;
      pse.num_lwp = procs[i].ki_numthreads;

      pse.vmem_size = procs[i].ki_size;
      pse.vmem_rss = procs[i].ki_rssize * pagesize;
      pse.vmem_data = procs[i].ki_dsize * pagesize;
      pse.vmem_code = procs[i].ki_tsize * pagesize;
      pse.stack_size = procs[i].ki_ssize * pagesize;
      pse.vmem_minflt_counter = procs[i].ki_rusage.ru_minflt;
      pse.vmem_majflt_counter = procs[i].ki_rusage.ru_majflt;

      pse.cpu_user_counter = 0;
      pse.cpu_system_counter = 0;
      /*
       * The u-area might be swapped out, and we can't get
       * at it because we have a crashdump and no swap.
       * If it's here fill in these fields, otherwise, just
       * leave them 0.
       */
      if (procs[i].ki_flag & P_INMEM) {
        pse.cpu_user_counter =
            ((fpcounter_t)procs[i].ki_rusage.ru_utime.tv_sec) +
            ((fpcounter_t)procs[i].ki_rusage.ru_utime.tv_usec) / 1e6;
        pse.cpu_system_counter =
            ((fpcounter_t)procs[i].ki_rusage.ru_stime.tv_sec) +
            ((fpcounter_t)procs[i].ki_rusage.ru_stime.tv_usec) / 1e6;
      }

      /* no I/O data */
      pse.io_rchar = -1;
      pse.io_wchar = -1;
      pse.io_syscr = -1;
      pse.io_syscw = -1;
      pse.io_diskr = -1;
      pse.io_diskw = -1;

      /* file descriptor count not implemented */
      pse.num_fd = 0;

      /* Number of memory mappings */
      pse.num_maps = 0;

      /* context switch counters not implemented */
      pse.cswitch_vol = -1;
      pse.cswitch_invol = -1;

      ps_list_add(procs[i].ki_comm, &pse);

      switch (procs[i].ki_stat) {
      case SSTOP:
        process_count[STATE_STOPPED]++;
        break;
      case SSLEEP:
        process_count[STATE_SLEEPING]++;
        break;
      case SRUN:
        process_count[STATE_RUNNING]++;
        break;
      case SIDL:
        process_count[STATE_IDLE]++;
        break;
      case SWAIT:
        process_count[STATE_WAIT]++;
        break;
      case SLOCK:
        process_count[STATE_BLOCKED]++;
        break;
      case SZOMB:
        process_count[STATE_DEFUNCT]++;
        break;
      }
    } /* if ((proc_ptr == NULL) || (proc_ptr->ki_pid != procs[i].ki_pid)) */
  }

  kvm_close(kd);

  for (procstat_t *ps = list_head_g; ps != NULL; ps = ps->next)
    ps_submit_proc_list(ps);

  return 0;
}

#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC2_NETBSD
static int ps_read_netbsd(gauge_t process_count[static STATE_MAX]) {
  process_count[STATE_RUNNING] = 0;
  process_count[STATE_SLEEPING] = 0;
  process_count[STATE_DEFUNCT] = 0;
  process_count[STATE_STOPPED] = 0;
  process_count[STATE_BLOCKED] = 0;
  process_count[STATE_IDLE] = 0;

  kvm_t *kd;
  char errbuf[_POSIX2_LINE_MAX];
  struct kinfo_proc2 *procs; /* array of processes */
  struct kinfo_proc2 *proc_ptr = NULL;
  struct kinfo_proc2 *p;
  int count; /* returns number of processes */
  int i;
  int l, nlwps;
  struct kinfo_lwp *kl;

  procstat_t *ps_ptr;

  ps_list_reset();

  /* Open the kvm interface, get a descriptor */
  kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, errbuf);
  if (kd == NULL) {
    ERROR("processes plugin: Cannot open kvm interface: %s", errbuf);
    return (0);
  }

  /* Get the list of processes. */
  procs =
      kvm_getproc2(kd, KERN_PROC_ALL, 0, sizeof(struct kinfo_proc2), &count);
  if (procs == NULL) {
    ERROR("processes plugin: Cannot get kvm processes list: %s",
          kvm_geterr(kd));
    kvm_close(kd);
    return (0);
  }

  /* Iterate through the processes in kinfo_proc */
  for (i = 0; i < count; i++) {
    /* Create only one process list entry per _process_, i.e.
     * filter out threads (duplicate PID entries). */
    if ((proc_ptr == NULL) || (proc_ptr->p_pid != procs[i].p_pid)) {
      char cmdline[CMDLINE_BUFFER_SIZE] = "";

      process_entry_t pse = {
          .id = procs[i].p_pid,
      };

      proc_ptr = &(procs[i]);
      /* Don't probe system processes and processes without arguments */
      if (((procs[i].p_flag & P_SYSTEM) == 0) && (procs[i].p_comm[0] != 0)) {
        char **argv;
        int argc;
        int status;

        /* retrieve the arguments */
        argv = kvm_getargv2(kd, proc_ptr, 0);
        argc = 0;
        if ((argv != NULL) && (argv[0] != NULL)) {
          while (argv[argc] != NULL)
            argc++;

          status = strjoin(cmdline, sizeof(cmdline), argv, argc, " ");
          if (status < 0) {
            WARNING("processes plugin: Command line did not fit into buffer.");
          } else {
            pse.command_line = cmdline;
          }
        }
      } /* if (process has argument list) */

      pse.num_proc = 1;
      pse.num_lwp = procs[i].p_nlwps;

      pse.vmem_size = procs[i].p_uru_maxrss * pagesize;
      pse.vmem_rss = procs[i].p_vm_rssize * pagesize;
      pse.vmem_data = procs[i].p_vm_dsize * pagesize;
      pse.vmem_code = procs[i].p_vm_tsize * pagesize;
      pse.stack_size = procs[i].p_vm_ssize * pagesize;
      pse.vmem_minflt_counter = procs[i].p_uru_minflt;
      pse.vmem_majflt_counter = procs[i].p_uru_majflt;

      pse.cpu_user_counter = 0;
      pse.cpu_system_counter = 0;
      /* context switch counters not implemented */
      pse.cswitch_vol = -1;
      pse.cswitch_invol = -1;

      /*
       * The u-area might be swapped out, and we can't get
       * at it because we have a crashdump and no swap.
       * If it's here fill in these fields, otherwise, just
       * leave them 0.
       */
      if (procs[i].p_flag & P_INMEM) {
        pse.cpu_user_counter = ((fpcounter_t)procs[i].p_uutime_sec) +
                               ((fpcounter_t)procs[i].p_uutime_usec) / 1e6;
        pse.cpu_system_counter = ((fpcounter_t)procs[i].p_ustime_sec) +
                                 ((fpcounter_t)procs[i].p_ustime_usec) / 1e6;
      }

      /* no I/O data */
      pse.io_rchar = -1;
      pse.io_wchar = -1;
      pse.io_syscr = procs[i].p_uru_inblock;
      pse.io_syscw = procs[i].p_uru_oublock;

      /* file descriptor count not implemented */
      pse.num_fd = 0;

      /* Number of memory mappings */
      pse.num_maps = 0;

      /* context switch counters not implemented */
      pse.cswitch_vol = -1;
      pse.cswitch_invol = -1;

      ps_list_add(procs[i].p_comm, &pse);
    } /* if ((proc_ptr == NULL) || (proc_ptr->ki_pid != procs[i].ki_pid)) */

    /* system processes' LWPs end up in "running" state */
    if ((procs[i].p_flag & P_SYSTEM) != 0)
      continue;

    switch (procs[i].p_realstat) {
    case SSTOP:
    case SACTIVE:
    case SIDL:
      p = &(procs[i]);
      /* get info about LWPs */
      kl = kvm_getlwps(kd, p->p_pid, (u_long)p->p_paddr,
                       sizeof(struct kinfo_lwp), &nlwps);

      for (l = 0; kl && l < nlwps; l++) {
        switch (kl[l].l_stat) {
        case LSONPROC:
        case LSRUN:
          process_count[STATE_RUNNING]++;
          break;
        case LSSLEEP:
          if (kl[l].l_flag & L_SINTR) {
            if (kl[l].l_slptime > maxslp)
              process_count[STATE_IDLE]++;
            else
              process_count[STATE_SLEEPING]++;
          } else
            process_count[STATE_BLOCKED]++;
          break;
        case LSSTOP:
          process_count[STATE_STOPPED]++;
          break;
        case LSIDL:
          process_count[STATE_IDLE]++;
          break;
        }
      }
      break;
    case SZOMB:
    case SDYING:
    case SDEAD:
      process_count[STATE_DEFUNCT]++;
      break;
    }
  }

  kvm_close(kd);

  for (ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
    ps_submit_proc_list(ps_ptr);

  return 0;
}

#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_OPENBSD
static int ps_read_openbsd(gauge_t process_count[static STATE_MAX]) {
  process_count[STATE_RUNNING] = 0;
  process_count[STATE_SLEEPING] = 0;
  process_count[STATE_DEFUNCT] = 0;
  process_count[STATE_STOPPED] = 0;
  process_count[STATE_ONPROC] = 0;
  process_count[STATE_IDLE] = 0;

  kvm_t *kd;
  char errbuf[1024];
  struct kinfo_proc *procs; /* array of processes */
  struct kinfo_proc *proc_ptr = NULL;
  int count; /* returns number of processes */

  ps_list_reset();

  /* Open the kvm interface, get a descriptor */
  kd = kvm_openfiles(NULL, NULL, NULL, KVM_NO_FILES, errbuf);
  if (kd == NULL) {
    ERROR("processes plugin: Cannot open kvm interface: %s", errbuf);
    return 0;
  }

  /* Get the list of processes. */
  procs = kvm_getprocs(kd, KERN_PROC_ALL, 0, sizeof(struct kinfo_proc), &count);
  if (procs == NULL) {
    ERROR("processes plugin: Cannot get kvm processes list: %s",
          kvm_geterr(kd));
    kvm_close(kd);
    return 0;
  }

  /* Iterate through the processes in kinfo_proc */
  for (int i = 0; i < count; i++) {
    /* Create only one process list entry per _process_, i.e.
     * filter out threads (duplicate PID entries). */
    if ((proc_ptr == NULL) || (proc_ptr->p_pid != procs[i].p_pid)) {
      char cmdline[CMDLINE_BUFFER_SIZE] = "";

      process_entry_t pse = {
          .id = procs[i].p_pid,
      };

      proc_ptr = &(procs[i]);
      /* Don't probe zombie processes  */
      if (!P_ZOMBIE(proc_ptr)) {
        char **argv;
        int argc;
        int status;

        /* retrieve the arguments */
        argv = kvm_getargv(kd, proc_ptr, /* nchr = */ 0);
        argc = 0;
        if ((argv != NULL) && (argv[0] != NULL)) {
          while (argv[argc] != NULL)
            argc++;

          status = strjoin(cmdline, sizeof(cmdline), argv, argc, " ");
          if (status < 0) {
            WARNING("processes plugin: Command line did not fit into buffer.");
          } else {
            pse.command_line = cmdline;
          }
        }
      } /* if (process has argument list) */

      pse.num_proc = 1;
      pse.num_lwp = 1; /* XXX: accumulate p_tid values for a single p_pid ? */

      pse.vmem_rss = procs[i].p_vm_rssize * pagesize;
      pse.vmem_data = procs[i].p_vm_dsize * pagesize;
      pse.vmem_code = procs[i].p_vm_tsize * pagesize;
      pse.stack_size = procs[i].p_vm_ssize * pagesize;
      pse.vmem_size = pse.stack_size + pse.vmem_code + pse.vmem_data;
      pse.vmem_minflt_counter = procs[i].p_uru_minflt;
      pse.vmem_majflt_counter = procs[i].p_uru_majflt;

      pse.cpu_user_counter = ((fpcounter_t)procs[i].p_uutime_sec) +
                             ((fpcounter_t)procs[i].p_uutime_usec) / 1e6;
      pse.cpu_system_counter = ((fpcounter_t)procs[i].p_ustime_sec) +
                               ((fpcounter_t)procs[i].p_ustime_usec) / 1e6;

      /* no I/O data */
      pse.io_rchar = -1;
      pse.io_wchar = -1;
      pse.io_syscr = -1;
      pse.io_syscw = -1;
      pse.io_diskr = -1;
      pse.io_diskw = -1;

      /* file descriptor count not implemented */
      pse.num_fd = 0;

      /* Number of memory mappings */
      pse.num_maps = 0;

      /* context switch counters not implemented */
      pse.cswitch_vol = -1;
      pse.cswitch_invol = -1;

      ps_list_add(procs[i].p_comm, &pse);

      switch (procs[i].p_stat) {
      case SSTOP:
        process_count[STATE_STOPPED]++;
        break;
      case SSLEEP:
        process_count[STATE_SLEEPING]++;
        break;
      case SRUN:
        process_count[STATE_RUNNING]++;
        break;
      case SIDL:
        process_count[STATE_IDLE]++;
        break;
      case SONPROC:
        process_count[STATE_ONPROC]++;
        break;
      case SDEAD:
      case SZOMB:
        process_count[STATE_DEFUNCT]++;
        break;
      }
    } /* if ((proc_ptr == NULL) || (proc_ptr->p_pid != procs[i].p_pid)) */
  }

  kvm_close(kd);

  for (procstat_t *ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
    ps_submit_proc_list(ps_ptr);

  return 0;
}

#elif HAVE_PROCINFO_H
static int ps_read_aix(gauge_t process_count[static STATE_MAX]) {
  process_count[STATE_RUNNING] = 0;
  process_count[STATE_SLEEPING] = 0;
  process_count[STATE_DEFUNCT] = 0;
  process_count[STATE_STOPPED] = 0;
  process_count[STATE_PAGING] = 0;
  process_count[STATE_BLOCKED] = 0;

  pid_t pindex = 0;
  int nprocs;

  ps_list_reset();
  while ((nprocs = getprocs64(procentry, sizeof(struct procentry64),
                              /* fdsinfo = */ NULL, sizeof(struct fdsinfo64),
                              &pindex, MAXPROCENTRY)) > 0) {
    for (int i = 0; i < nprocs; i++) {
      if (procentry[i].pi_state == SNONE)
        continue;
      /* if (procentry[i].pi_state == SZOMB)  FIXME */

      char *cmdline = NULL;
      char arglist[MAXARGLN + 1];
      if (procentry[i].pi_flags & SKPROC) {
        if (procentry[i].pi_pid == 0) {
          cmdline = "swapper";
        } else {
          cmdline = procentry[i].pi_comm;
        }
      } else {
        if (getargs(&procentry[i], sizeof(struct procentry64), arglist,
                    MAXARGLN) >= 0) {
          for (int i = 0; i < MAXARGLN; i++) {
            if (arglist[n] == 0 && arglist[n + 1] == 0) {
              break;
            }
            if (arglist[n] == 0) {
              arglist[n] = ' ';
            }
          }
          cmdline = argslist;
        }
      }

      process_entry_t pse = {
          .id = procentry[i].pi_pid,
          .command_line = cargs,
          .num_lwp = procentry[i].pi_thcount,
          .num_proc = 1,
      };

      tid64_t thindex = 0;
      int nthreads;
      while ((nthreads = getthrds64(procentry[i].pi_pid, thrdentry,
                                    sizeof(struct thrdentry64), &thindex,
                                    MAXTHRDENTRY)) > 0) {
        for (int j = 0; j < nthreads; j++) {
          switch (thrdentry[j].ti_state) {
          /* case TSNONE: break; */
          case TSIDL:
            process_count[STATE_BLOCKED]++;
            break; /* FIXME is really blocked */
          case TSRUN:
            process_count[STATE_RUNNING]++;
            break;
          case TSSLEEP:
            process_count[STATE_SLEEPING]++;
            break;
          case TSSWAP:
            process_count[STATE_PAGING]++;
            break;
          case TSSTOP:
            process_count[STATE_STOPPED]++;
            break;
          case TSZOMB:
            process_count[STATE_DEFUNCT]++;
            break;
          }
        }
        if (nthreads < MAXTHRDENTRY)
          break;
      }

      /* Yes, the .tv_usec field below really does contain nanoseconds. From the
       * AIX docs:
       *
       * The ProcessBuffer parameter of getprocs subroutine contains two struct
       * rusage fields named pi_ru and pi_cru. Each of these fields contains two
       * struct timeval fields named ru_utime and ru_stime. The tv_usec field in
       * both of the struct timeval contain nanoseconds instead of microseconds.
       */
      pse.cpu_user_counter =
          ((fpcounter_t)procentry[i].pi_ru.ru_utime.tv_sec) +
          ((fpcounter_t)procentry[i].pi_ru.ru_utime.tv_usec) / 1e9;
      pse.cpu_system_counter =
          ((fpcounter_t)procentry[i].pi_ru.ru_stime.tv_sec) +
          ((fpcounter_t)procentry[i].pi_ru.ru_stime.tv_usec) / 1e9;

      pse.vmem_minflt_counter = procentry[i].pi_minflt;
      pse.vmem_majflt_counter = procentry[i].pi_majflt;

      pse.vmem_size = procentry[i].pi_tsize + procentry[i].pi_dvm * pagesize;
      pse.vmem_rss = (procentry[i].pi_drss + procentry[i].pi_trss) * pagesize;
      /* Not supported/implemented */
      pse.vmem_data = 0;
      pse.vmem_code = 0;
      pse.stack_size = 0;

      pse.io_rchar = -1;
      pse.io_wchar = -1;
      pse.io_syscr = -1;
      pse.io_syscw = -1;
      pse.io_diskr = -1;
      pse.io_diskw = -1;

      pse.num_fd = 0;
      pse.num_maps = 0;

      pse.cswitch_vol = -1;
      pse.cswitch_invol = -1;

      ps_list_add(cmdline, cargs, &pse);
    } /* for (i = 0 .. nprocs) */

    if (nprocs < MAXPROCENTRY)
      break;
  } /* while (getprocs64() > 0) */

  for (procstat_t *ps = list_head_g; ps != NULL; ps = ps->next)
    ps_submit_proc_list(ps);

  return 0;
}

#elif KERNEL_SOLARIS
static int ps_read_solaris(gauge_t process_count[static STATE_MAX]) {
  /*
   * The Solaris section adds a few more process states and removes some
   * process states compared to linux. Most notably there is no "PAGING"
   * and "BLOCKED" state for a process.  The rest is similar to the linux
   * code.
   */
  process_count[STATE_RUNNING] = 0;
  process_count[STATE_SLEEPING] = 0;
  process_count[STATE_DEFUNCT] = 0;
  process_count[STATE_STOPPED] = 0;
  process_count[STATE_DETACHED] = 0;
  process_count[STATE_DAEMON] = 0;
  process_count[STATE_SYSTEM] = 0;
  process_count[STATE_ORPHAN] = 0;

  struct dirent *ent;
  DIR *proc;

  int status;
  char state;

  ps_list_reset();

  proc = opendir("/proc");
  if (proc == NULL)
    return -1;

  while ((ent = readdir(proc)) != NULL) {
    long pid;
    process_entry_t pse;
    char *endptr;

    if (!isdigit((int)ent->d_name[0]))
      continue;

    pid = strtol(ent->d_name, &endptr, 10);
    if (*endptr != 0) /* value didn't completely parse as a number */
      continue;

    memset(&pse, 0, sizeof(pse));
    pse.id = pid;

    status = ps_read_process(pid, &pse, &state);
    if (status != 0) {
      DEBUG("ps_read_process failed: %i", status);
      continue;
    }

    switch (state) {
    case 'R':
      process_count[STATE_RUNNING]++;
      break;
    case 'S':
      process_count[STATE_SLEEPING]++;
      break;
    case 'E':
      process_count[STATE_DETACHED]++;
      break;
    case 'Z':
      process_count[STATE_DEFUNCT]++;
      break;
    case 'T':
      process_count[STATE_STOPPED]++;
      break;
    case 'A':
      process_count[STATE_DAEMON]++;
      break;
    case 'Y':
      process_count[STATE_SYSTEM]++;
      break;
    case 'O':
      process_count[STATE_ORPHAN]++;
      break;
    }

    ps_list_add(pse.name, &pse);
  } /* while(readdir) */
  closedir(proc);

  for (procstat_t *ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
    ps_submit_proc_list(ps_ptr);

  read_fork_rate();

  return 0;
}
#endif /* KERNEL_SOLARIS */

static int ps_read(void) {
  gauge_t process_count[STATE_MAX];
  for (process_state_t s = 0; s < STATE_MAX; s++) {
    process_count[s] = NAN;
  }

  int status = 0;
#if HAVE_THREAD_INFO
  status = ps_read_thread_info(process_count);
#elif KERNEL_LINUX
  status = ps_read_linux(process_count);
#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD
  status = ps_read_freebsd(process_count);
#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC2_NETBSD
  status = ps_read_netbsd(process_count);
#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_OPENBSD
  status = ps_read_openbsd(process_count);
#elif HAVE_PROCINFO_H
  status = ps_read_aix(process_count);
#elif KERNEL_SOLARIS
  status = ps_read_solaris(process_count);
#endif
  if (status != 0) {
    return status;
  }

  metric_family_t fam = {
      .name = "system.processes.count",
      .help = "Total number of processes in each state",
      .unit = "{process}",
      .type = METRIC_TYPE_GAUGE,
  };

  for (process_state_t s = 0; s < STATE_MAX; s++) {
    if (isnan(process_count[s])) {
      continue;
    }

    value_t v = {.gauge = process_count[s]};
    metric_family_append(&fam, state_label, state_names[s], v, NULL);
  }

  plugin_dispatch_metric_family(&fam);

  metric_family_metric_reset(&fam);
  want_init = false;
  return 0;
} /* int ps_read */

void module_register(void) {
  plugin_register_complex_config("processes", ps_config);
  plugin_register_init("processes", ps_init);
  plugin_register_read("processes", ps_read);
} /* void module_register */
