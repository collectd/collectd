/**
 * collectd - src/memory.c
 * Copyright (C) 2005-2020  Florian octo Forster
 * Copyright (C) 2009       Simon Kuhnle
 * Copyright (C) 2009       Manuel Sanmartin
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
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
 *   Florian octo Forster <octo at collectd.org>
 *   Simon Kuhnle <simon at blarzwurst.de>
 *   Manuel Sanmartin
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#if (defined(HAVE_SYS_SYSCTL_H) && defined(HAVE_SYSCTLBYNAME)) ||              \
    defined(__OpenBSD__)
/* Implies BSD variant */
#include <sys/sysctl.h>
#endif
#ifdef HAVE_SYS_VMMETER_H
#include <sys/vmmeter.h>
#endif

#ifdef HAVE_MACH_KERN_RETURN_H
#include <mach/kern_return.h>
#endif
#ifdef HAVE_MACH_MACH_INIT_H
#include <mach/mach_init.h>
#endif
#ifdef HAVE_MACH_MACH_HOST_H
#include <mach/mach_host.h>
#endif
#ifdef HAVE_MACH_HOST_PRIV_H
#include <mach/host_priv.h>
#endif
#ifdef HAVE_MACH_VM_STATISTICS_H
#include <mach/vm_statistics.h>
#endif

#if HAVE_STATGRAB_H
#include <statgrab.h>
#endif

#if HAVE_PERFSTAT
#include <libperfstat.h>
#include <sys/protosw.h>
#endif /* HAVE_PERFSTAT */

static char const *const label_state = "system.memory.state";

typedef enum {
  COLLECTD_MEMORY_TYPE_USED,
  COLLECTD_MEMORY_TYPE_FREE,
  COLLECTD_MEMORY_TYPE_BUFFERS,
  COLLECTD_MEMORY_TYPE_CACHED,
  COLLECTD_MEMORY_TYPE_SLAB_TOTAL,
  COLLECTD_MEMORY_TYPE_SLAB_RECL,
  COLLECTD_MEMORY_TYPE_SLAB_UNRECL,
  COLLECTD_MEMORY_TYPE_WIRED,
  COLLECTD_MEMORY_TYPE_ACTIVE,
  COLLECTD_MEMORY_TYPE_INACTIVE,
  COLLECTD_MEMORY_TYPE_KERNEL,
  COLLECTD_MEMORY_TYPE_LOCKED,
  COLLECTD_MEMORY_TYPE_ARC,
  COLLECTD_MEMORY_TYPE_UNUSED,
  COLLECTD_MEMORY_TYPE_AVAILABLE,
  COLLECTD_MEMORY_TYPE_USER_WIRE,
  COLLECTD_MEMORY_TYPE_LAUNDRY,
  COLLECTD_MEMORY_TYPE_MAX, /* #states */
} memory_type_t;

static char const *memory_type_names[COLLECTD_MEMORY_TYPE_MAX] = {
    "used",
    "free",
    "buffers",
    "cached",
    "slab",
    "slab_reclaimable",
    "slab_unreclaimable",
    "wired",
    "active",
    "inactive",
    "kernel",
    "locked",
    "arc",
    "unusable",
    "available",
    "user_wire",
    "laundry",
};

/* vm_statistics_data_t */
#if HAVE_HOST_STATISTICS
static mach_port_t port_host;
static vm_size_t pagesize;
/* #endif HAVE_HOST_STATISTICS */

#elif HAVE_SYSCTLBYNAME
#if HAVE_SYSCTL && defined(KERNEL_NETBSD)
static int pagesize;
#include <unistd.h> /* getpagesize() */
#else
/* no global variables */
#endif
/* #endif HAVE_SYSCTLBYNAME */

#elif KERNEL_LINUX
/* no global variables */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
static int pagesize;
static kstat_t *ksp;
static kstat_t *ksz;
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SYSCTL && __OpenBSD__
/* OpenBSD variant does not have sysctlbyname */
static int pagesize;
/* #endif HAVE_SYSCTL && __OpenBSD__ */

#elif HAVE_LIBSTATGRAB
/* no global variables */
/* endif HAVE_LIBSTATGRAB */
#elif HAVE_PERFSTAT
static int pagesize;
/* endif HAVE_PERFSTAT */
#else
#error "No applicable input method."
#endif

#if KERNEL_NETBSD
#include <uvm/uvm_extern.h>
#endif

static bool values_absolute = true;
static bool values_percentage;

static int memory_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("ValuesAbsolute", child->key) == 0)
      cf_util_get_boolean(child, &values_absolute);
    else if (strcasecmp("ValuesPercentage", child->key) == 0)
      cf_util_get_boolean(child, &values_percentage);
    else
      ERROR("memory plugin: Invalid configuration option: \"%s\".", child->key);
  }

  return 0;
} /* }}} int memory_config */

static int memory_dispatch(gauge_t values[COLLECTD_MEMORY_TYPE_MAX]) {
  metric_family_t fam_absolute = {
      .name = "system.memory.usage",
      .help = "Reports memory in use by state",
      .unit = "By",
      .type = METRIC_TYPE_GAUGE,
  };
  gauge_t total = 0;

  for (size_t i = 0; i < COLLECTD_MEMORY_TYPE_MAX; i++) {
    if (isnan(values[i])) {
      continue;
    }

    total += values[i];

    if (values_absolute) {
      metric_family_append(&fam_absolute, label_state, memory_type_names[i],
                           (value_t){.gauge = values[i]}, NULL);
    }
  }

  int ret = 0;
  if (values_absolute) {
    int status = plugin_dispatch_metric_family(&fam_absolute);
    if (status != 0) {
      ERROR("memory plugin: plugin_dispatch_metric_family failed: %s",
            STRERROR(status));
    }
    ret = status;
  }
  metric_family_metric_reset(&fam_absolute);

  if (!values_percentage) {
    return ret;
  }

  if (total == 0) {
    return EINVAL;
  }

  metric_family_t fam_util = {
      .name = "system.memory.utilization",
      .help = "Reports memory in use by state",
      .unit = "1",
      .type = METRIC_TYPE_GAUGE,
  };
  for (size_t i = 0; i < COLLECTD_MEMORY_TYPE_MAX; i++) {
    if (isnan(values[i])) {
      continue;
    }

    metric_family_append(&fam_util, label_state, memory_type_names[i],
                         (value_t){.gauge = values[i] / total}, NULL);
  }

  int status = plugin_dispatch_metric_family(&fam_util);
  if (status != 0) {
    ERROR("memory plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
    ret = ret ? ret : status;
  }
  metric_family_metric_reset(&fam_util);

  return ret;
}

static int memory_init(void) {
#if HAVE_HOST_STATISTICS
  port_host = mach_host_self();
  host_page_size(port_host, &pagesize);
  /* #endif HAVE_HOST_STATISTICS */

#elif HAVE_SYSCTLBYNAME
#if HAVE_SYSCTL && defined(KERNEL_NETBSD)
  pagesize = getpagesize();
#else
/* no init stuff */
#endif /* HAVE_SYSCTL && defined(KERNEL_NETBSD) */
  /* #endif HAVE_SYSCTLBYNAME */

#elif defined(KERNEL_LINUX)
  /* no init stuff */
  /* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBKSTAT)
  /* getpagesize(3C) tells me this does not fail.. */
  pagesize = getpagesize();
  if (get_kstat(&ksp, "unix", 0, "system_pages") != 0) {
    ksp = NULL;
    return -1;
  }
  if (get_kstat(&ksz, "zfs", 0, "arcstats") != 0) {
    ksz = NULL;
    return -1;
  }

  /* #endif HAVE_LIBKSTAT */

#elif HAVE_SYSCTL && __OpenBSD__
  /* OpenBSD variant does not have sysctlbyname */
  pagesize = getpagesize();
  if (pagesize <= 0) {
    ERROR("memory plugin: Invalid pagesize: %i", pagesize);
    return -1;
  }
  /* #endif HAVE_SYSCTL && __OpenBSD__ */

#elif HAVE_LIBSTATGRAB
  /* no init stuff */
  /* #endif HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
  pagesize = getpagesize();
#endif /* HAVE_PERFSTAT */
  return 0;
} /* int memory_init */

static int memory_read_internal(gauge_t values[COLLECTD_MEMORY_TYPE_MAX]) {
#if HAVE_HOST_STATISTICS
  if (!port_host || !pagesize) {
    return EINVAL;
  }

  vm_statistics_data_t vm_data = {0};
  mach_msg_type_number_t vm_data_len = sizeof(vm_data) / sizeof(natural_t);
  kern_return_t status = host_statistics(port_host, HOST_VM_INFO,
                                         (host_info_t)&vm_data, &vm_data_len);
  if (status != KERN_SUCCESS) {
    ERROR("memory-plugin: host_statistics failed and returned the value %d",
          (int)status);
    return (int)status;
  }

  /*
   * From <http://docs.info.apple.com/article.html?artnum=107918>:
   *
   * Wired memory
   *   This information can't be cached to disk, so it must stay in RAM.
   *   The amount depends on what applications you are using.
   *
   * Active memory
   *   This information is currently in RAM and actively being used.
   *
   * Inactive memory
   *   This information is no longer being used and has been cached to
   *   disk, but it will remain in RAM until another application needs
   *   the space. Leaving this information in RAM is to your advantage if
   *   you (or a client of your computer) come back to it later.
   *
   * Free memory
   *   This memory is not being used.
   */

  values[COLLECTD_MEMORY_TYPE_WIRED] = (gauge_t)(vm_data.wire_count * pagesize);
  values[COLLECTD_MEMORY_TYPE_ACTIVE] =
      (gauge_t)(vm_data.active_count * pagesize);
  values[COLLECTD_MEMORY_TYPE_INACTIVE] =
      (gauge_t)(vm_data.inactive_count * pagesize);
  values[COLLECTD_MEMORY_TYPE_FREE] = (gauge_t)(vm_data.free_count * pagesize);
  /* #endif HAVE_HOST_STATISTICS */

#elif HAVE_SYSCTLBYNAME

#if HAVE_SYSCTL && defined(KERNEL_NETBSD)
  if (pagesize == 0) {
    return EINVAL;
  }

  int mib[] = {CTL_VM, VM_UVMEXP2};
  struct uvmexp_sysctl uvmexp = {0};
  size_t size = sizeof(uvmexp);
  if (sysctl(mib, STATIC_ARRAY_SIZE(mib), &uvmexp, &size, NULL, 0) < 0) {
    WARNING("memory plugin: sysctl failed: %s", STRERRNO);
    return errno;
  }

  values[COLLECTD_MEMORY_TYPE_WIRED] = (gauge_t)(uvmexp.wired * pagesize);
  values[COLLECTD_MEMORY_TYPE_ACTIVE] = (gauge_t)(uvmexp.active * pagesize);
  values[COLLECTD_MEMORY_TYPE_INACTIVE] = (gauge_t)(uvmexp.inactive * pagesize);
  values[COLLECTD_MEMORY_TYPE_FREE] = (gauge_t)(uvmexp.free * pagesize);

  int64_t accounted =
      uvmexp.wired + uvmexp.active + uvmexp.inactive + uvmexp.free;
  if (uvmexp.npages > accounted) {
    values[COLLECTD_MEMORY_TYPE_KERNEL] =
        (gauge_t)((uvmexp.npages - accounted) * pagesize);
  }
  /* #endif HAVE_SYSCTL && defined(KERNEL_NETBSD) */

#else  /* Other HAVE_SYSCTLBYNAME providers */
  /*
   * vm.stats.vm.v_page_size: 4096
   * vm.stats.vm.v_page_count: 246178
   * vm.stats.vm.v_free_count: 28760
   * vm.stats.vm.v_wire_count: 37526
   * vm.stats.vm.v_active_count: 55239
   * vm.stats.vm.v_inactive_count: 113730
   * vm.stats.vm.v_cache_count: 10809
   * vm.stats.vm.v_user_wire_count: 0
   * vm.stats.vm.v_laundry_count: 40394
   */
  struct {
    char const *sysctl_key;
    memory_type_t type;
  } metrics[] = {
      {"vm.stats.vm.v_page_size"},
      {"vm.stats.vm.v_free_count", COLLECTD_MEMORY_TYPE_FREE},
      {"vm.stats.vm.v_wire_count", COLLECTD_MEMORY_TYPE_WIRED},
      {"vm.stats.vm.v_active_count", COLLECTD_MEMORY_TYPE_ACTIVE},
      {"vm.stats.vm.v_inactive_count", COLLECTD_MEMORY_TYPE_INACTIVE},
      {"vm.stats.vm.v_cache_count", COLLECTD_MEMORY_TYPE_CACHED},
      {"vm.stats.vm.v_user_wire_count", COLLECTD_MEMORY_TYPE_USER_WIRE},
      {"vm.stats.vm.v_laundry_count", COLLECTD_MEMORY_TYPE_LAUNDRY},
  };

  gauge_t pagesize = 0;
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(metrics); i++) {
    long value = 0;
    size_t value_len = sizeof(value);

    int status = sysctlbyname(metrics[i].sysctl_key, (void *)&value, &value_len,
                              NULL, 0);
    if (status != 0) {
      WARNING("sysctlbyname(\"%s\") failed: %s", metrics[i].sysctl_key,
              STRERROR(status));
      continue;
    }

    if (i == 0) {
      pagesize = (gauge_t)value;
      continue;
    }

    values[metrics[i].type] = ((gauge_t)value) * pagesize;
  } /* for (sysctl_keys) */
#endif /* HAVE_SYSCTL && KERNEL_NETBSD */
  /* #endif HAVE_SYSCTLBYNAME */

#elif KERNEL_LINUX
  FILE *fh = fopen("/proc/meminfo", "r");
  if (fh == NULL) {
    int status = errno;
    ERROR("memory plugin: fopen(\"/proc/meminfo\") failed: %s", STRERRNO);
    return status;
  }

  gauge_t mem_total = 0;
  gauge_t mem_not_used = 0;

  char buffer[256];
  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    char *fields[4] = {NULL};
    int fields_num = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));
    if ((fields_num != 3) || (strcmp("kB", fields[2]) != 0)) {
      continue;
    }

    gauge_t v = 1024.0 * atof(fields[1]);
    if (!isfinite(v)) {
      continue;
    }

    if (strcmp(fields[0], "MemTotal:") == 0) {
      mem_total = v;
    } else if (strcmp(fields[0], "MemFree:") == 0) {
      values[COLLECTD_MEMORY_TYPE_FREE] = v;
      mem_not_used += v;
    } else if (strcmp(fields[0], "Buffers:") == 0) {
      values[COLLECTD_MEMORY_TYPE_BUFFERS] = v;
      mem_not_used += v;
    } else if (strcmp(fields[0], "Cached:") == 0) {
      values[COLLECTD_MEMORY_TYPE_CACHED] = v;
      mem_not_used += v;
    } else if (strcmp(fields[0], "Slab:") == 0) {
      values[COLLECTD_MEMORY_TYPE_SLAB_TOTAL] = v;
    } else if (strcmp(fields[0], "SReclaimable:") == 0) {
      values[COLLECTD_MEMORY_TYPE_SLAB_RECL] = v;
    } else if (strcmp(fields[0], "SUnreclaim:") == 0) {
      values[COLLECTD_MEMORY_TYPE_SLAB_UNRECL] = v;
    } else if (strcmp(fields[0], "MemAvailable:") == 0) {
      values[COLLECTD_MEMORY_TYPE_AVAILABLE] = v;
    }
  }

  if (fclose(fh)) {
    WARNING("memory plugin: fclose failed: %s", STRERRNO);
  }

  /* If SReclaimable (introduced in kernel 2.6.19) is available count it
   * (but not SUnreclaim) towards the unused memory.
   * If we do not have detailed slab info count the total as unused. */
  if (!isnan(values[COLLECTD_MEMORY_TYPE_SLAB_RECL])) {
    mem_not_used += values[COLLECTD_MEMORY_TYPE_SLAB_RECL];
  } else if (!isnan(values[COLLECTD_MEMORY_TYPE_SLAB_TOTAL])) {
    mem_not_used += values[COLLECTD_MEMORY_TYPE_SLAB_TOTAL];
  }

  if (isnan(mem_total) || (mem_total == 0) || (mem_total < mem_not_used)) {
    return EINVAL;
  }

  /* "used" is not explicitly reported. It is calculated as everything that is
   * not "not used", e.g. cached, buffers, ... */
  values[COLLECTD_MEMORY_TYPE_USED] = mem_total - mem_not_used;

  /* SReclaimable and SUnreclaim were introduced in kernel 2.6.19
   * They sum up to the value of Slab, which is available on older & newer
   * kernels. So SReclaimable/SUnreclaim are submitted if available, and Slab
   * if not. */
  if (!isnan(values[COLLECTD_MEMORY_TYPE_SLAB_RECL]) ||
      !isnan(values[COLLECTD_MEMORY_TYPE_SLAB_UNRECL])) {
    values[COLLECTD_MEMORY_TYPE_SLAB_TOTAL] = NAN;
  }
  /* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
  /* Most of the additions here were taken as-is from the k9toolkit from
   * Brendan Gregg and are subject to change I guess */
  if ((ksp == NULL) || (ksz == NULL)) {
    return EINVAL;
  }

  long long mem_used = get_kstat_value(ksp, "pagestotal");
  long long mem_free = get_kstat_value(ksp, "pagesfree");
  long long mem_lock = get_kstat_value(ksp, "pageslocked");
  long long mem_kern = 0;
  long long mem_unus = 0;
  long long arcsize = get_kstat_value(ksz, "size");

  long long pp_kernel = get_kstat_value(ksp, "pp_kernel");
  long long physmem = get_kstat_value(ksp, "physmem");
  long long availrmem = get_kstat_value(ksp, "availrmem");

  if ((mem_used < 0LL) || (mem_free < 0LL) || (mem_lock < 0LL)) {
    ERROR("memory plugin: one of used, free or locked is negative.");
    return -1;
  }

  mem_unus = physmem - mem_used;

  if (mem_used < (mem_free + mem_lock)) {
    /* source: http://wesunsolve.net/bugid/id/4909199
     * this seems to happen when swap space is small, e.g. 2G on a 32G system
     * we will make some assumptions here
     * educated solaris internals help welcome here */
    DEBUG("memory plugin: pages total is smaller than \"free\" "
          "+ \"locked\". This is probably due to small "
          "swap space");
    mem_free = availrmem;
    mem_used = 0;
  } else {
    mem_used -= mem_free + mem_lock;
  }

  /* mem_kern is accounted for in mem_lock */
  if (pp_kernel < mem_lock) {
    mem_kern = pp_kernel;
    mem_lock -= pp_kernel;
  } else {
    mem_kern = mem_lock;
    mem_lock = 0;
  }

  values[COLLECTD_MEMORY_TYPE_USED] = (gauge_t)(mem_used * pagesize);
  values[COLLECTD_MEMORY_TYPE_FREE] = (gauge_t)(mem_free * pagesize);
  values[COLLECTD_MEMORY_TYPE_LOCKED] = (gauge_t)(mem_lock * pagesize);
  values[COLLECTD_MEMORY_TYPE_KERNEL] =
      (gauge_t)((mem_kern * pagesize) - arcsize);
  values[COLLECTD_MEMORY_TYPE_UNUSED] = (gauge_t)(mem_unus * pagesize);
  values[COLLECTD_MEMORY_TYPE_ARC] = (gauge_t)arcsize;
  /* #endif HAVE_LIBKSTAT */

#elif HAVE_SYSCTL && __OpenBSD__
  /* OpenBSD variant does not have HAVE_SYSCTLBYNAME */
  if (pagesize == 0) {
    return EINVAL;
  }

  int mib[] = {CTL_VM, VM_METER};
  struct vmtotal vmtotal = {0};
  size_t size = sizeof(vmtotal);
  if (sysctl(mib, STATIC_ARRAY_SIZE(mib), &vmtotal, &size, NULL, 0) < 0) {
    ERROR("memory plugin: sysctl failed: %s", STRERRNO);
    return errno;
  }

  values[COLLECTD_MEMORY_TYPE_ACTIVE] = (gauge_t)(vmtotal.t_arm * pagesize);
  values[COLLECTD_MEMORY_TYPE_INACTIVE] =
      (gauge_t)((vmtotal.t_rm - vmtotal.t_arm) * pagesize);
  values[COLLECTD_MEMORY_TYPE_FREE] = (gauge_t)(vmtotal.t_free * pagesize);
  /* #endif HAVE_SYSCTL && __OpenBSD__ */

#elif HAVE_LIBSTATGRAB
  sg_mem_stats *ios = sg_get_mem_stats();
  if (ios == NULL) {
    return -1;
  }

  values[COLLECTD_MEMORY_TYPE_USED] = (gauge_t)ios->used;
  values[COLLECTD_MEMORY_TYPE_CACHED] = (gauge_t)ios->cache;
  values[COLLECTD_MEMORY_TYPE_FREE] = (gauge_t)ios->free;
  /* #endif HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
  perfstat_memory_total_t pmemory = {0};
  if (perfstat_memory_total(NULL, &pmemory, sizeof(pmemory), 1) < 0) {
    ERROR("memory plugin: perfstat_memory_total failed: %s", STRERRNO);
    return errno;
  }

  /* Unfortunately, the AIX documentation is not very clear on how these
   * numbers relate to one another. The only thing is states explicitly
   * is:
   *   real_total = real_process + real_free + numperm + real_system
   *
   * Another segmentation, which would be closer to the numbers reported
   * by the "svmon" utility, would be:
   *   real_total = real_free + real_inuse
   *   real_inuse = "active" + real_pinned + numperm
   */
  values[COLLECTD_MEMORY_TYPE_FREE] = (gauge_t)(pmemory.real_free * pagesize);
  values[COLLECTD_MEMORY_TYPE_CACHED] = (gauge_t)(pmemory.numperm * pagesize);
  values[COLLECTD_MEMORY_TYPE_KERNEL] =
      (gauge_t)(pmemory.real_system * pagesize);
  values[COLLECTD_MEMORY_TYPE_USED] =
      (gauge_t)(pmemory.real_process * pagesize);
#endif /* HAVE_PERFSTAT */

  return 0;
} /* }}} int memory_read_internal */

static int memory_read(void) /* {{{ */
{
  gauge_t values[COLLECTD_MEMORY_TYPE_MAX] = {0};
  for (size_t i = 0; i < COLLECTD_MEMORY_TYPE_MAX; i++) {
    values[i] = NAN;
  }

  int status = memory_read_internal(values);
  if (status != 0) {
    return status;
  }

  return memory_dispatch(values);
} /* }}} int memory_read */

void module_register(void) {
  plugin_register_complex_config("memory", memory_config);
  plugin_register_init("memory", memory_init);
  plugin_register_read("memory", memory_read);
} /* void module_register */
