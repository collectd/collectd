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

#include "common.h"
#include "plugin.h"

#if HAVE_LIBTASKSTATS
#include "utils_complain.h"
#include "utils_taskstats.h"
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
#if HAVE_LINUX_CONFIG_H
#include <linux/config.h>
#endif
#ifndef CONFIG_HZ
#define CONFIG_HZ 100
#endif
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKVM_GETPROCS &&                                                  \
    (HAVE_STRUCT_KINFO_PROC_FREEBSD || HAVE_STRUCT_KINFO_PROC_OPENBSD)
#include <kvm.h>
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/sysctl.h>
#include <sys/user.h>
/* #endif HAVE_LIBKVM_GETPROCS && (HAVE_STRUCT_KINFO_PROC_FREEBSD ||
 * HAVE_STRUCT_KINFO_PROC_OPENBSD) */

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
typedef struct process_entry_s {
  unsigned long id;
  char name[PROCSTAT_NAME_LEN];

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

  derive_t cpu_user_counter;
  derive_t cpu_system_counter;

  /* io data */
  derive_t io_rchar;
  derive_t io_wchar;
  derive_t io_syscr;
  derive_t io_syscw;
  derive_t io_diskr;
  derive_t io_diskw;
  _Bool has_io;

  derive_t cswitch_vol;
  derive_t cswitch_invol;
  _Bool has_cswitch;

#if HAVE_LIBTASKSTATS
  ts_delay_t delay;
#endif
  _Bool has_delay;

  _Bool has_fd;

  _Bool has_maps;
} process_entry_t;

typedef struct procstat_entry_s {
  unsigned long id;
  unsigned long age;

  derive_t vmem_minflt_counter;
  derive_t vmem_majflt_counter;

  derive_t cpu_user_counter;
  derive_t cpu_system_counter;

  /* io data */
  derive_t io_rchar;
  derive_t io_wchar;
  derive_t io_syscr;
  derive_t io_syscw;
  derive_t io_diskr;
  derive_t io_diskw;

  derive_t cswitch_vol;
  derive_t cswitch_invol;

#if HAVE_LIBTASKSTATS
  value_to_rate_state_t delay_cpu;
  value_to_rate_state_t delay_blkio;
  value_to_rate_state_t delay_swapin;
  value_to_rate_state_t delay_freepages;
#endif

  struct procstat_entry_s *next;
} procstat_entry_t;

typedef struct procstat {
  char name[PROCSTAT_NAME_LEN];
#if HAVE_REGEX_H
  regex_t *re;
#endif

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

  derive_t cpu_user_counter;
  derive_t cpu_system_counter;

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
  gauge_t delay_cpu;
  gauge_t delay_blkio;
  gauge_t delay_swapin;
  gauge_t delay_freepages;

  _Bool report_fd_num;
  _Bool report_maps_num;
  _Bool report_ctx_switch;
  _Bool report_delay;

  struct procstat *next;
  struct procstat_entry_s *instances;
} procstat_t;

static procstat_t *list_head_g = NULL;

static _Bool want_init = 1;
static _Bool report_ctx_switch = 0;
static _Bool report_fd_num = 0;
static _Bool report_maps_num = 0;
static _Bool report_delay = 0;

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
    (HAVE_STRUCT_KINFO_PROC_FREEBSD || HAVE_STRUCT_KINFO_PROC_OPENBSD)
static int pagesize;
/* #endif HAVE_LIBKVM_GETPROCS && (HAVE_STRUCT_KINFO_PROC_FREEBSD ||
 * HAVE_STRUCT_KINFO_PROC_OPENBSD) */

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
static ts_t *taskstats_handle = NULL;
#endif

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
static int ps_list_match(const char *name, const char *cmdline,
                         procstat_t *ps) {
#if HAVE_REGEX_H
  if (ps->re != NULL) {
    int status;
    const char *str;

    str = cmdline;
    if ((str == NULL) || (str[0] == 0))
      str = name;

    assert(str != NULL);

    status = regexec(ps->re, str,
                     /* nmatch = */ 0,
                     /* pmatch = */ NULL,
                     /* eflags = */ 0);
    if (status == 0)
      return 1;
  } else
#endif
      if (strcmp(ps->name, name) == 0)
    return 1;

  return 0;
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

#if HAVE_LIBTASKSTATS
static void ps_update_delay_one(gauge_t *out_rate_sum,
                                value_to_rate_state_t *state, uint64_t cnt,
                                cdtime_t t) {
  gauge_t rate = NAN;
  int status = value_to_rate(&rate, (value_t){.counter = (counter_t)cnt},
                             DS_TYPE_COUNTER, t, state);
  if ((status != 0) || isnan(rate)) {
    return;
  }

  if (isnan(*out_rate_sum)) {
    *out_rate_sum = rate;
  } else {
    *out_rate_sum += rate;
  }
}

static void ps_update_delay(procstat_t *out, procstat_entry_t *prev,
                            process_entry_t *curr) {
  cdtime_t now = cdtime();

  ps_update_delay_one(&out->delay_cpu, &prev->delay_cpu, curr->delay.cpu_ns,
                      now);
  ps_update_delay_one(&out->delay_blkio, &prev->delay_blkio,
                      curr->delay.blkio_ns, now);
  ps_update_delay_one(&out->delay_swapin, &prev->delay_swapin,
                      curr->delay.swapin_ns, now);
  ps_update_delay_one(&out->delay_freepages, &prev->delay_freepages,
                      curr->delay.freepages_ns, now);
}
#endif

/* add process entry to 'instances' of process 'name' (or refresh it) */
static void ps_list_add(const char *name, const char *cmdline,
                        process_entry_t *entry) {
  procstat_entry_t *pse;

  if (entry->id == 0)
    return;

  for (procstat_t *ps = list_head_g; ps != NULL; ps = ps->next) {
    if ((ps_list_match(name, cmdline, ps)) == 0)
      continue;

#if KERNEL_LINUX
    ps_fill_details(ps, entry);
#endif

    for (pse = ps->instances; pse != NULL; pse = pse->next)
      if ((pse->id == entry->id) || (pse->next == NULL))
        break;

    if ((pse == NULL) || (pse->id != entry->id)) {
      procstat_entry_t *new;

      new = calloc(1, sizeof(*new));
      if (new == NULL)
        return;
      new->id = entry->id;

      if (pse == NULL)
        ps->instances = new;
      else
        pse->next = new;

      pse = new;
    }

    pse->age = 0;

    ps->num_proc += entry->num_proc;
    ps->num_lwp += entry->num_lwp;
    ps->num_fd += entry->num_fd;
    ps->num_maps += entry->num_maps;
    ps->vmem_size += entry->vmem_size;
    ps->vmem_rss += entry->vmem_rss;
    ps->vmem_data += entry->vmem_data;
    ps->vmem_code += entry->vmem_code;
    ps->stack_size += entry->stack_size;

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
      ps_update_counter(&ps->cswitch_vol, &pse->cswitch_vol,
                        entry->cswitch_vol);
      ps_update_counter(&ps->cswitch_invol, &pse->cswitch_invol,
                        entry->cswitch_invol);
    }

    ps_update_counter(&ps->vmem_minflt_counter, &pse->vmem_minflt_counter,
                      entry->vmem_minflt_counter);
    ps_update_counter(&ps->vmem_majflt_counter, &pse->vmem_majflt_counter,
                      entry->vmem_majflt_counter);

    ps_update_counter(&ps->cpu_user_counter, &pse->cpu_user_counter,
                      entry->cpu_user_counter);
    ps_update_counter(&ps->cpu_system_counter, &pse->cpu_system_counter,
                      entry->cpu_system_counter);

#if HAVE_LIBTASKSTATS
    ps_update_delay(ps, pse, entry);
#endif
  }
}

/* remove old entries from instances of processes in list_head_g */
static void ps_list_reset(void) {
  procstat_entry_t *pse;
  procstat_entry_t *pse_prev;

  for (procstat_t *ps = list_head_g; ps != NULL; ps = ps->next) {
    ps->num_proc = 0;
    ps->num_lwp = 0;
    ps->num_fd = 0;
    ps->num_maps = 0;
    ps->vmem_size = 0;
    ps->vmem_rss = 0;
    ps->vmem_data = 0;
    ps->vmem_code = 0;
    ps->stack_size = 0;

    ps->delay_cpu = NAN;
    ps->delay_blkio = NAN;
    ps->delay_swapin = NAN;
    ps->delay_freepages = NAN;

    pse_prev = NULL;
    pse = ps->instances;
    while (pse != NULL) {
      if (pse->age > 10) {
        DEBUG("Removing this procstat entry cause it's too old: "
              "id = %lu; name = %s;",
              pse->id, ps->name);

        if (pse_prev == NULL) {
          ps->instances = pse->next;
          free(pse);
          pse = ps->instances;
        } else {
          pse_prev->next = pse->next;
          free(pse);
          pse = pse_prev->next;
        }
      } else {
        pse->age++;
        pse_prev = pse;
        pse = pse->next;
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
        WARNING("processes plugin: this platform has a %zu character limit "
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
  DEBUG("pagesize_g = %li; CONFIG_HZ = %i;", pagesize_g, CONFIG_HZ);

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
    (HAVE_STRUCT_KINFO_PROC_FREEBSD || HAVE_STRUCT_KINFO_PROC_OPENBSD)
  pagesize = getpagesize();
/* #endif HAVE_LIBKVM_GETPROCS && (HAVE_STRUCT_KINFO_PROC_FREEBSD ||
 * HAVE_STRUCT_KINFO_PROC_OPENBSD) */

#elif HAVE_PROCINFO_H
  pagesize = getpagesize();
#endif /* HAVE_PROCINFO_H */

  return 0;
} /* int ps_init */

/* submit global state (e.g.: qty of zombies, running, etc..) */
static void ps_submit_state(const char *state, double value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "processes", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, "", sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "ps_state", sizeof(vl.type));
  sstrncpy(vl.type_instance, state, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

/* submit info about specific process (e.g.: memory taken, cpu usage, etc..) */
static void ps_submit_proc_list(procstat_t *ps) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[2];

  vl.values = values;
  sstrncpy(vl.plugin, "processes", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, ps->name, sizeof(vl.plugin_instance));

  sstrncpy(vl.type, "ps_vm", sizeof(vl.type));
  vl.values[0].gauge = ps->vmem_size;
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ps_rss", sizeof(vl.type));
  vl.values[0].gauge = ps->vmem_rss;
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ps_data", sizeof(vl.type));
  vl.values[0].gauge = ps->vmem_data;
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ps_code", sizeof(vl.type));
  vl.values[0].gauge = ps->vmem_code;
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ps_stacksize", sizeof(vl.type));
  vl.values[0].gauge = ps->stack_size;
  vl.values_len = 1;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ps_cputime", sizeof(vl.type));
  vl.values[0].derive = ps->cpu_user_counter;
  vl.values[1].derive = ps->cpu_system_counter;
  vl.values_len = 2;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ps_count", sizeof(vl.type));
  vl.values[0].gauge = ps->num_proc;
  vl.values[1].gauge = ps->num_lwp;
  vl.values_len = 2;
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type, "ps_pagefaults", sizeof(vl.type));
  vl.values[0].derive = ps->vmem_minflt_counter;
  vl.values[1].derive = ps->vmem_majflt_counter;
  vl.values_len = 2;
  plugin_dispatch_values(&vl);

  if ((ps->io_rchar != -1) && (ps->io_wchar != -1)) {
    sstrncpy(vl.type, "io_octets", sizeof(vl.type));
    vl.values[0].derive = ps->io_rchar;
    vl.values[1].derive = ps->io_wchar;
    vl.values_len = 2;
    plugin_dispatch_values(&vl);
  }

  if ((ps->io_syscr != -1) && (ps->io_syscw != -1)) {
    sstrncpy(vl.type, "io_ops", sizeof(vl.type));
    vl.values[0].derive = ps->io_syscr;
    vl.values[1].derive = ps->io_syscw;
    vl.values_len = 2;
    plugin_dispatch_values(&vl);
  }

  if ((ps->io_diskr != -1) && (ps->io_diskw != -1)) {
    sstrncpy(vl.type, "disk_octets", sizeof(vl.type));
    vl.values[0].derive = ps->io_diskr;
    vl.values[1].derive = ps->io_diskw;
    vl.values_len = 2;
    plugin_dispatch_values(&vl);
  }

  if (ps->num_fd > 0) {
    sstrncpy(vl.type, "file_handles", sizeof(vl.type));
    vl.values[0].gauge = ps->num_fd;
    vl.values_len = 1;
    plugin_dispatch_values(&vl);
  }

  if (ps->num_maps > 0) {
    sstrncpy(vl.type, "file_handles", sizeof(vl.type));
    sstrncpy(vl.type_instance, "mapped", sizeof(vl.type_instance));
    vl.values[0].gauge = ps->num_maps;
    vl.values_len = 1;
    plugin_dispatch_values(&vl);
  }

  if ((ps->cswitch_vol != -1) && (ps->cswitch_invol != -1)) {
    sstrncpy(vl.type, "contextswitch", sizeof(vl.type));
    sstrncpy(vl.type_instance, "voluntary", sizeof(vl.type_instance));
    vl.values[0].derive = ps->cswitch_vol;
    vl.values_len = 1;
    plugin_dispatch_values(&vl);

    sstrncpy(vl.type, "contextswitch", sizeof(vl.type));
    sstrncpy(vl.type_instance, "involuntary", sizeof(vl.type_instance));
    vl.values[0].derive = ps->cswitch_invol;
    vl.values_len = 1;
    plugin_dispatch_values(&vl);
  }

  /* The ps->delay_* metrics are in nanoseconds per second. Convert to seconds
   * per second. */
  gauge_t const delay_factor = 1000000000.0;

  struct {
    char *type_instance;
    gauge_t rate_ns;
  } delay_metrics[] = {
      {"delay-cpu", ps->delay_cpu},
      {"delay-blkio", ps->delay_blkio},
      {"delay-swapin", ps->delay_swapin},
      {"delay-freepages", ps->delay_freepages},
  };
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(delay_metrics); i++) {
    if (isnan(delay_metrics[i].rate_ns)) {
      continue;
    }
    sstrncpy(vl.type, "delay_rate", sizeof(vl.type));
    sstrncpy(vl.type_instance, delay_metrics[i].type_instance,
             sizeof(vl.type_instance));
    vl.values[0].gauge = delay_metrics[i].rate_ns * delay_factor;
    vl.values_len = 1;
    plugin_dispatch_values(&vl);
  }

  DEBUG(
      "name = %s; num_proc = %lu; num_lwp = %lu; num_fd = %lu; num_maps = %lu; "
      "vmem_size = %lu; vmem_rss = %lu; vmem_data = %lu; "
      "vmem_code = %lu; "
      "vmem_minflt_counter = %" PRIi64 "; vmem_majflt_counter = %" PRIi64 "; "
      "cpu_user_counter = %" PRIi64 "; cpu_system_counter = %" PRIi64 "; "
      "io_rchar = %" PRIi64 "; io_wchar = %" PRIi64 "; "
      "io_syscr = %" PRIi64 "; io_syscw = %" PRIi64 "; "
      "io_diskr = %" PRIi64 "; io_diskw = %" PRIi64 "; "
      "cswitch_vol = %" PRIi64 "; cswitch_invol = %" PRIi64 "; "
      "delay_cpu = %g; delay_blkio = %g; "
      "delay_swapin = %g; delay_freepages = %g;",
      ps->name, ps->num_proc, ps->num_lwp, ps->num_fd, ps->num_maps,
      ps->vmem_size, ps->vmem_rss, ps->vmem_data, ps->vmem_code,
      ps->vmem_minflt_counter, ps->vmem_majflt_counter, ps->cpu_user_counter,
      ps->cpu_system_counter, ps->io_rchar, ps->io_wchar, ps->io_syscr,
      ps->io_syscw, ps->io_diskr, ps->io_diskw, ps->cswitch_vol,
      ps->cswitch_invol, ps->delay_cpu, ps->delay_blkio, ps->delay_swapin,
      ps->delay_freepages);

} /* void ps_submit_proc_list */

#if KERNEL_LINUX || KERNEL_SOLARIS
static void ps_submit_fork_rate(derive_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.derive = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "processes", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, "", sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "fork_rate", sizeof(vl.type));
  sstrncpy(vl.type_instance, "", sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
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

    if (snprintf(filename, sizeof(filename), "/proc/%li/task/%s/status", ps->id,
                 tpid) >= sizeof(filename)) {
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
  if (entry->has_io == 0) {
    ps_read_io(entry);
    entry->has_io = 1;
  }

  if (ps->report_ctx_switch) {
    if (entry->has_cswitch == 0) {
      ps_read_tasks_status(entry);
      entry->has_cswitch = 1;
    }
  }

  if (ps->report_maps_num) {
    int num_maps;
    if (entry->has_maps == 0 && (num_maps = ps_count_maps(entry->id)) > 0) {
      entry->num_maps = num_maps;
    }
    entry->has_maps = 1;
  }

  if (ps->report_fd_num) {
    int num_fd;
    if (entry->has_fd == 0 && (num_fd = ps_count_fd(entry->id)) > 0) {
      entry->num_fd = num_fd;
    }
    entry->has_fd = 1;
  }

#if HAVE_LIBTASKSTATS
  if (ps->report_delay && !entry->has_delay) {
    if (ps_delay(entry) == 0) {
      entry->has_delay = 1;
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

  derive_t cpu_user_counter;
  derive_t cpu_system_counter;
  long long unsigned vmem_size;
  long long unsigned vmem_rss;
  long long unsigned stack_size;

  ssize_t status;

  snprintf(filename, sizeof(filename), "/proc/%li/stat", pid);

  status = read_file_contents(filename, buffer, sizeof(buffer) - 1);
  if (status <= 0)
    return -1;
  buffer_len = (size_t)status;
  buffer[buffer_len] = 0;

  /* The name of the process is enclosed in parens. Since the name can
   * contain parens itself, spaces, numbers and pretty much everything
   * else, use these to determine the process name. We don't use
   * strchr(3) and strrchr(3) to avoid pointer arithmetic which would
   * otherwise be required to determine name_len. */
  name_start_pos = 0;
  while (name_start_pos < buffer_len && buffer[name_start_pos] != '(')
    name_start_pos++;

  name_end_pos = buffer_len;
  while (name_end_pos > 0 && buffer[name_end_pos] != ')')
    name_end_pos--;

  /* Either '(' or ')' is not found or they are in the wrong order.
   * Anyway, something weird that shouldn't happen ever. */
  if (name_start_pos >= name_end_pos) {
    ERROR("processes plugin: name_start_pos = %zu >= name_end_pos = %zu",
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

  cpu_user_counter = atoll(fields[11]);
  cpu_system_counter = atoll(fields[12]);
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

  /* Convert jiffies to useconds */
  cpu_user_counter = cpu_user_counter * 1000000 / CONFIG_HZ;
  cpu_system_counter = cpu_system_counter * 1000000 / CONFIG_HZ;
  vmem_rss = vmem_rss * pagesize_g;

  ps->cpu_user_counter = cpu_user_counter;
  ps->cpu_system_counter = cpu_system_counter;
  ps->vmem_size = (unsigned long)vmem_size;
  ps->vmem_rss = (unsigned long)vmem_rss;
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

  /* success */
  return 0;
} /* int ps_read_process (...) */

static char *ps_get_cmdline(long pid, char *name, char *buf, size_t buf_len) {
  char *buf_ptr;
  size_t len;

  char file[PATH_MAX];
  int fd;

  size_t n;

  if ((pid < 1) || (NULL == buf) || (buf_len < 2))
    return NULL;

  snprintf(file, sizeof(file), "/proc/%li/cmdline", pid);

  errno = 0;
  fd = open(file, O_RDONLY);
  if (fd < 0) {
    /* ENOENT means the process exited while we were handling it.
     * Don't complain about this, it only fills the logs. */
    if (errno != ENOENT)
      WARNING("processes plugin: Failed to open `%s': %s.", file, STRERRNO);
    return NULL;
  }

  buf_ptr = buf;
  len = buf_len;

  n = 0;

  while (42) {
    ssize_t status;

    status = read(fd, (void *)buf_ptr, len);

    if (status < 0) {

      if ((EAGAIN == errno) || (EINTR == errno))
        continue;

      WARNING("processes plugin: Failed to read from `%s': %s.", file,
              STRERRNO);
      close(fd);
      return NULL;
    }

    n += status;

    if (status == 0)
      break;

    buf_ptr += status;
    len -= status;

    if (len == 0)
      break;
  }

  close(fd);

  if (0 == n) {
    /* cmdline not available; e.g. kernel thread, zombie */
    if (NULL == name)
      return NULL;

    snprintf(buf, buf_len, "[%s]", name);
    return buf;
  }

  assert(n <= buf_len);

  if (n == buf_len)
    --n;
  buf[n] = '\0';

  --n;
  /* remove trailing whitespace */
  while ((n > 0) && (isspace(buf[n]) || ('\0' == buf[n]))) {
    buf[n] = '\0';
    --n;
  }

  /* arguments are separated by '\0' in /proc/<pid>/cmdline */
  while (n > 0) {
    if ('\0' == buf[n])
      buf[n] = ' ';
    --n;
  }
  return buf;
} /* char *ps_get_cmdline (...) */

static int read_fork_rate(void) {
  FILE *proc_stat;
  char buffer[1024];
  value_t value;
  _Bool value_valid = 0;

  proc_stat = fopen("/proc/stat", "r");
  if (proc_stat == NULL) {
    ERROR("processes plugin: fopen (/proc/stat) failed: %s", STRERRNO);
    return -1;
  }

  while (fgets(buffer, sizeof(buffer), proc_stat) != NULL) {
    int status;
    char *fields[3];
    int fields_num;

    fields_num = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));
    if (fields_num != 2)
      continue;

    if (strcmp("processes", fields[0]) != 0)
      continue;

    status = parse_value(fields[1], &value, DS_TYPE_DERIVE);
    if (status == 0)
      value_valid = 1;

    break;
  }
  fclose(proc_stat);

  if (!value_valid)
    return -1;

  ps_submit_fork_rate(value.derive);
  return 0;
}
#endif /*KERNEL_LINUX */

#if KERNEL_SOLARIS
static char *ps_get_cmdline(long pid,
                            char *name __attribute__((unused)), /* {{{ */
                            char *buffer, size_t buffer_size) {
  char path[PATH_MAX];
  psinfo_t info;
  ssize_t status;

  snprintf(path, sizeof(path), "/proc/%li/psinfo", pid);

  status = read_file_contents(path, (void *)&info, sizeof(info));
  if ((status < 0) || (((size_t)status) != sizeof(info))) {
    ERROR("processes plugin: Unexpected return value "
          "while reading \"%s\": "
          "Returned %zd but expected %zu.",
          path, status, buffer_size);
    return NULL;
  }

  info.pr_psargs[sizeof(info.pr_psargs) - 1] = 0;
  sstrncpy(buffer, info.pr_psargs, buffer_size);

  return buffer;
} /* }}} int ps_get_cmdline */

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

  buffer = calloc(1, sizeof(pstatus_t));
  read_file_contents(filename, buffer, sizeof(pstatus_t));
  myStatus = (pstatus_t *)buffer;

  buffer = calloc(1, sizeof(psinfo_t));
  read_file_contents(f_psinfo, buffer, sizeof(psinfo_t));
  myInfo = (psinfo_t *)buffer;

  buffer = calloc(1, sizeof(prusage_t));
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

  /*
   * Convert system time and user time from nanoseconds to microseconds
   * for compatibility with the linux module
   */
  ps->cpu_system_counter = myStatus->pr_stime.tv_nsec / 1000;
  ps->cpu_user_counter = myStatus->pr_utime.tv_nsec / 1000;

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
  derive_t result = 0;

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
        result += tmp;
    }
  }

  ps_submit_fork_rate(result);
  return 0;
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
static int ps_read(void) {
#if HAVE_THREAD_INFO
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

  int running = 0;
  int sleeping = 0;
  int zombies = 0;
  int stopped = 0;
  int blocked = 0;

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

        pse.cpu_user_counter = task_absolutetime_info.total_user;
        pse.cpu_system_counter = task_absolutetime_info.total_system;

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
        zombies++;
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
          running++;
          break;
        case TH_STATE_STOPPED:
        /* What exactly is `halted'? */
        case TH_STATE_HALTED:
          stopped++;
          break;
        case TH_STATE_WAITING:
          sleeping++;
          break;
        case TH_STATE_UNINTERRUPTIBLE:
          blocked++;
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
        /* FIXME: cmdline should be here instead of NULL */
        ps_list_add(task_name, NULL, &pse);
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

  ps_submit_state("running", running);
  ps_submit_state("sleeping", sleeping);
  ps_submit_state("zombies", zombies);
  ps_submit_state("stopped", stopped);
  ps_submit_state("blocked", blocked);

  for (ps = list_head_g; ps != NULL; ps = ps->next)
    ps_submit_proc_list(ps);
/* #endif HAVE_THREAD_INFO */

#elif KERNEL_LINUX
  int running = 0;
  int sleeping = 0;
  int zombies = 0;
  int stopped = 0;
  int paging = 0;
  int blocked = 0;

  struct dirent *ent;
  DIR *proc;
  long pid;

  char cmdline[CMDLINE_BUFFER_SIZE];

  int status;
  process_entry_t pse;
  char state;

  running = sleeping = zombies = stopped = paging = blocked = 0;
  ps_list_reset();

  if ((proc = opendir("/proc")) == NULL) {
    ERROR("Cannot open `/proc': %s", STRERRNO);
    return -1;
  }

  while ((ent = readdir(proc)) != NULL) {
    if (!isdigit(ent->d_name[0]))
      continue;

    if ((pid = atol(ent->d_name)) < 1)
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
      running++;
      break;
    case 'S':
      sleeping++;
      break;
    case 'D':
      blocked++;
      break;
    case 'Z':
      zombies++;
      break;
    case 'T':
      stopped++;
      break;
    case 'W':
      paging++;
      break;
    }

    ps_list_add(pse.name,
                ps_get_cmdline(pid, pse.name, cmdline, sizeof(cmdline)), &pse);
  }

  closedir(proc);

  ps_submit_state("running", running);
  ps_submit_state("sleeping", sleeping);
  ps_submit_state("zombies", zombies);
  ps_submit_state("stopped", stopped);
  ps_submit_state("paging", paging);
  ps_submit_state("blocked", blocked);

  for (procstat_t *ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
    ps_submit_proc_list(ps_ptr);

  read_fork_rate();
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD
  int running = 0;
  int sleeping = 0;
  int zombies = 0;
  int stopped = 0;
  int blocked = 0;
  int idle = 0;
  int wait = 0;

  kvm_t *kd;
  char errbuf[_POSIX2_LINE_MAX];
  struct kinfo_proc *procs; /* array of processes */
  struct kinfo_proc *proc_ptr = NULL;
  int count; /* returns number of processes */

  process_entry_t pse;

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
      _Bool have_cmdline = 0;

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
          if (status < 0)
            WARNING("processes plugin: Command line did not fit into buffer.");
          else
            have_cmdline = 1;
        }
      } /* if (process has argument list) */

      memset(&pse, 0, sizeof(pse));
      pse.id = procs[i].ki_pid;

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
        pse.cpu_user_counter = procs[i].ki_rusage.ru_utime.tv_usec +
                               (1000000lu * procs[i].ki_rusage.ru_utime.tv_sec);
        pse.cpu_system_counter =
            procs[i].ki_rusage.ru_stime.tv_usec +
            (1000000lu * procs[i].ki_rusage.ru_stime.tv_sec);
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

      ps_list_add(procs[i].ki_comm, have_cmdline ? cmdline : NULL, &pse);

      switch (procs[i].ki_stat) {
      case SSTOP:
        stopped++;
        break;
      case SSLEEP:
        sleeping++;
        break;
      case SRUN:
        running++;
        break;
      case SIDL:
        idle++;
        break;
      case SWAIT:
        wait++;
        break;
      case SLOCK:
        blocked++;
        break;
      case SZOMB:
        zombies++;
        break;
      }
    } /* if ((proc_ptr == NULL) || (proc_ptr->ki_pid != procs[i].ki_pid)) */
  }

  kvm_close(kd);

  ps_submit_state("running", running);
  ps_submit_state("sleeping", sleeping);
  ps_submit_state("zombies", zombies);
  ps_submit_state("stopped", stopped);
  ps_submit_state("blocked", blocked);
  ps_submit_state("idle", idle);
  ps_submit_state("wait", wait);

  for (procstat_t *ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
    ps_submit_proc_list(ps_ptr);
/* #endif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_FREEBSD */

#elif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_OPENBSD
  int running = 0;
  int sleeping = 0;
  int zombies = 0;
  int stopped = 0;
  int onproc = 0;
  int idle = 0;
  int dead = 0;

  kvm_t *kd;
  char errbuf[1024];
  struct kinfo_proc *procs; /* array of processes */
  struct kinfo_proc *proc_ptr = NULL;
  int count; /* returns number of processes */

  process_entry_t pse;

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
      _Bool have_cmdline = 0;

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
          if (status < 0)
            WARNING("processes plugin: Command line did not fit into buffer.");
          else
            have_cmdline = 1;
        }
      } /* if (process has argument list) */

      memset(&pse, 0, sizeof(pse));
      pse.id = procs[i].p_pid;

      pse.num_proc = 1;
      pse.num_lwp = 1; /* XXX: accumulate p_tid values for a single p_pid ? */

      pse.vmem_rss = procs[i].p_vm_rssize * pagesize;
      pse.vmem_data = procs[i].p_vm_dsize * pagesize;
      pse.vmem_code = procs[i].p_vm_tsize * pagesize;
      pse.stack_size = procs[i].p_vm_ssize * pagesize;
      pse.vmem_size = pse.stack_size + pse.vmem_code + pse.vmem_data;
      pse.vmem_minflt_counter = procs[i].p_uru_minflt;
      pse.vmem_majflt_counter = procs[i].p_uru_majflt;

      pse.cpu_user_counter =
          procs[i].p_uutime_usec + (1000000lu * procs[i].p_uutime_sec);
      pse.cpu_system_counter =
          procs[i].p_ustime_usec + (1000000lu * procs[i].p_ustime_sec);

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

      ps_list_add(procs[i].p_comm, have_cmdline ? cmdline : NULL, &pse);

      switch (procs[i].p_stat) {
      case SSTOP:
        stopped++;
        break;
      case SSLEEP:
        sleeping++;
        break;
      case SRUN:
        running++;
        break;
      case SIDL:
        idle++;
        break;
      case SONPROC:
        onproc++;
        break;
      case SDEAD:
        dead++;
        break;
      case SZOMB:
        zombies++;
        break;
      }
    } /* if ((proc_ptr == NULL) || (proc_ptr->p_pid != procs[i].p_pid)) */
  }

  kvm_close(kd);

  ps_submit_state("running", running);
  ps_submit_state("sleeping", sleeping);
  ps_submit_state("zombies", zombies);
  ps_submit_state("stopped", stopped);
  ps_submit_state("onproc", onproc);
  ps_submit_state("idle", idle);
  ps_submit_state("dead", dead);

  for (procstat_t *ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
    ps_submit_proc_list(ps_ptr);
/* #endif HAVE_LIBKVM_GETPROCS && HAVE_STRUCT_KINFO_PROC_OPENBSD */

#elif HAVE_PROCINFO_H
  /* AIX */
  int running = 0;
  int sleeping = 0;
  int zombies = 0;
  int stopped = 0;
  int paging = 0;
  int blocked = 0;

  pid_t pindex = 0;
  int nprocs;

  process_entry_t pse;

  ps_list_reset();
  while ((nprocs = getprocs64(procentry, sizeof(struct procentry64),
                              /* fdsinfo = */ NULL, sizeof(struct fdsinfo64),
                              &pindex, MAXPROCENTRY)) > 0) {
    for (int i = 0; i < nprocs; i++) {
      tid64_t thindex;
      int nthreads;
      char arglist[MAXARGLN + 1];
      char *cargs;
      char *cmdline;

      if (procentry[i].pi_state == SNONE)
        continue;
      /* if (procentry[i].pi_state == SZOMB)  FIXME */

      cmdline = procentry[i].pi_comm;
      cargs = procentry[i].pi_comm;
      if (procentry[i].pi_flags & SKPROC) {
        if (procentry[i].pi_pid == 0)
          cmdline = "swapper";
        cargs = cmdline;
      } else {
        if (getargs(&procentry[i], sizeof(struct procentry64), arglist,
                    MAXARGLN) >= 0) {
          int n;

          n = -1;
          while (++n < MAXARGLN) {
            if (arglist[n] == '\0') {
              if (arglist[n + 1] == '\0')
                break;
              arglist[n] = ' ';
            }
          }
          cargs = arglist;
        }
      }

      memset(&pse, 0, sizeof(pse));

      pse.id = procentry[i].pi_pid;
      pse.num_lwp = procentry[i].pi_thcount;
      pse.num_proc = 1;

      thindex = 0;
      while ((nthreads = getthrds64(procentry[i].pi_pid, thrdentry,
                                    sizeof(struct thrdentry64), &thindex,
                                    MAXTHRDENTRY)) > 0) {
        int j;

        for (j = 0; j < nthreads; j++) {
          switch (thrdentry[j].ti_state) {
          /* case TSNONE: break; */
          case TSIDL:
            blocked++;
            break; /* FIXME is really blocked */
          case TSRUN:
            running++;
            break;
          case TSSLEEP:
            sleeping++;
            break;
          case TSSWAP:
            paging++;
            break;
          case TSSTOP:
            stopped++;
            break;
          case TSZOMB:
            zombies++;
            break;
          }
        }
        if (nthreads < MAXTHRDENTRY)
          break;
      }

      /* tv_usec is nanosec ??? */
      pse.cpu_user_counter = procentry[i].pi_ru.ru_utime.tv_sec * 1000000 +
                             procentry[i].pi_ru.ru_utime.tv_usec / 1000;

      /* tv_usec is nanosec ??? */
      pse.cpu_system_counter = procentry[i].pi_ru.ru_stime.tv_sec * 1000000 +
                               procentry[i].pi_ru.ru_stime.tv_usec / 1000;

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
  ps_submit_state("running", running);
  ps_submit_state("sleeping", sleeping);
  ps_submit_state("zombies", zombies);
  ps_submit_state("stopped", stopped);
  ps_submit_state("paging", paging);
  ps_submit_state("blocked", blocked);

  for (procstat_t *ps = list_head_g; ps != NULL; ps = ps->next)
    ps_submit_proc_list(ps);
/* #endif HAVE_PROCINFO_H */

#elif KERNEL_SOLARIS
  /*
   * The Solaris section adds a few more process states and removes some
   * process states compared to linux. Most notably there is no "PAGING"
   * and "BLOCKED" state for a process.  The rest is similar to the linux
   * code.
   */
  int running = 0;
  int sleeping = 0;
  int zombies = 0;
  int stopped = 0;
  int detached = 0;
  int daemon = 0;
  int system = 0;
  int orphan = 0;

  struct dirent *ent;
  DIR *proc;

  int status;
  char state;

  char cmdline[PRARGSZ];

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
      running++;
      break;
    case 'S':
      sleeping++;
      break;
    case 'E':
      detached++;
      break;
    case 'Z':
      zombies++;
      break;
    case 'T':
      stopped++;
      break;
    case 'A':
      daemon++;
      break;
    case 'Y':
      system++;
      break;
    case 'O':
      orphan++;
      break;
    }

    ps_list_add(pse.name,
                ps_get_cmdline(pid, pse.name, cmdline, sizeof(cmdline)), &pse);
  } /* while(readdir) */
  closedir(proc);

  ps_submit_state("running", running);
  ps_submit_state("sleeping", sleeping);
  ps_submit_state("zombies", zombies);
  ps_submit_state("stopped", stopped);
  ps_submit_state("detached", detached);
  ps_submit_state("daemon", daemon);
  ps_submit_state("system", system);
  ps_submit_state("orphan", orphan);

  for (procstat_t *ps_ptr = list_head_g; ps_ptr != NULL; ps_ptr = ps_ptr->next)
    ps_submit_proc_list(ps_ptr);

  read_fork_rate();
#endif /* KERNEL_SOLARIS */

  want_init = 0;

  return 0;
} /* int ps_read */

void module_register(void) {
  plugin_register_complex_config("processes", ps_config);
  plugin_register_init("processes", ps_init);
  plugin_register_read("processes", ps_read);
} /* void module_register */
