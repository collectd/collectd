/**
 * collectd - src/xencpu.c
 * Copyright (C) 2016       Pavel Rochnyak
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
 *   Pavel Rochnyak <pavel2000 ngs.ru>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <xenctrl.h>

#ifdef XENCTRL_HAS_XC_INTERFACE

// Xen-4.1+
#define XC_INTERFACE_INIT_ARGS NULL, NULL, 0
xc_interface *xc_handle;

#else /* XENCTRL_HAS_XC_INTERFACE */

// For xen-3.4/xen-4.0
#include <string.h>
#define xc_strerror(xc_interface, errcode) strerror(errcode)
#define XC_INTERFACE_INIT_ARGS
typedef int xc_interface;
xc_interface xc_handle = 0;

#endif /* XENCTRL_HAS_XC_INTERFACE */

uint32_t num_cpus = 0;
xc_cpuinfo_t *cpu_info;
static value_to_rate_state_t *cpu_states;

static int xencpu_init(void) {
  xc_handle = xc_interface_open(XC_INTERFACE_INIT_ARGS);
  if (!xc_handle) {
    ERROR("xencpu: xc_interface_open() failed");
    return (-1);
  }

  xc_physinfo_t *physinfo;

  physinfo = calloc(1, sizeof(xc_physinfo_t));
  if (physinfo == NULL) {
    ERROR("xencpu plugin: calloc() for physinfo failed.");
    xc_interface_close(xc_handle);
    return (ENOMEM);
  }

  if (xc_physinfo(xc_handle, physinfo) < 0) {
    ERROR("xencpu plugin: xc_physinfo() failed");
    xc_interface_close(xc_handle);
    free(physinfo);
    return (-1);
  }

  num_cpus = physinfo->nr_cpus;
  free(physinfo);

  INFO("xencpu plugin: Found %" PRIu32 " processors.", num_cpus);

  cpu_info = calloc(num_cpus, sizeof(xc_cpuinfo_t));
  if (cpu_info == NULL) {
    ERROR("xencpu plugin: calloc() for num_cpus failed.");
    xc_interface_close(xc_handle);
    return (ENOMEM);
  }

  cpu_states = calloc(num_cpus, sizeof(value_to_rate_state_t));
  if (cpu_states == NULL) {
    ERROR("xencpu plugin: calloc() for cpu_states failed.");
    xc_interface_close(xc_handle);
    free(cpu_info);
    return (ENOMEM);
  }

  return (0);
} /* static int xencpu_init */

static int xencpu_shutdown(void) {
  free(cpu_states);
  free(cpu_info);
  xc_interface_close(xc_handle);

  return 0;
} /* static int xencpu_shutdown */

static void submit_value(int cpu_num, gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, "xencpu", sizeof(vl.plugin));
  sstrncpy(vl.type, "percent", sizeof(vl.type));
  sstrncpy(vl.type_instance, "load", sizeof(vl.type_instance));

  if (cpu_num >= 0) {
    ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%i", cpu_num);
  }
  plugin_dispatch_values(&vl);
} /* static void submit_value */

static int xencpu_read(void) {
  cdtime_t now = cdtime();

  int rc, nr_cpus;

  rc = xc_getcpuinfo(xc_handle, num_cpus, cpu_info, &nr_cpus);
  if (rc < 0) {
    ERROR("xencpu: xc_getcpuinfo() Failed: %d %s\n", rc,
          xc_strerror(xc_handle, errno));
    return (-1);
  }

  int status;
  for (int cpu = 0; cpu < nr_cpus; cpu++) {
    gauge_t rate = NAN;

    status = value_to_rate(&rate, (value_t){.derive = cpu_info[cpu].idletime},
                           DS_TYPE_DERIVE, now, &cpu_states[cpu]);
    if (status == 0) {
      submit_value(cpu, 100 - rate / 10000000);
    }
  }

  return (0);
} /* static int xencpu_read */

void module_register(void) {
  plugin_register_init("xencpu", xencpu_init);
  plugin_register_read("xencpu", xencpu_read);
  plugin_register_shutdown("xencpu", xencpu_shutdown);
} /* void module_register */
