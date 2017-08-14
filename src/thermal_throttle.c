/**
 * collectd - src/thermal_throttle.c
 * Copyright (C) 2017      notbaab
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
 *   notbaab <notbaab at gmail.com>
 **/

#include "common.h"
#include "plugin.h"
#include "collectd.h"

static int num_cpu = 0;

static int thermal_throttle_init() {
  int status;
  char filename[256];
  num_cpu = 0;

  while (1) {
    status = snprintf(filename, sizeof(filename),
                      "/sys/devices/system/cpu/cpu%d/thermal_throttle/"
                      "core_throttle_count",
                      num_cpu);

    if ((status < 1) || (unsigned int)status >= sizeof(filename)) {
      break;
    }

    if (access(filename, R_OK)) {
      break;
    }

    num_cpu++;
  }
  return 0;
}

static void thermal_throttle_submit(int cpu_num, value_t v_core,
                                    value_t v_package) {
  value_list_t v1 = VALUE_LIST_INIT;
  value_t values[] = {v_core, v_package};
  v1.values = values;
  v1.values_len = 2;

  sstrncpy(v1.plugin, "thermal_throttle", sizeof(v1.plugin));
  sstrncpy(v1.type, "thermal_throttle", sizeof(v1.type));
  snprintf(v1.type_instance, sizeof(v1.type_instance), "%i", cpu_num);

  plugin_dispatch_values(&v1);
}

static int thermal_throttle_read(void) {
  for (int i = 0; i < num_cpu; i++) {
    char filename[1024];
    snprintf(
        filename, sizeof(filename),
        "/sys/devices/system/cpu/cpu%d/thermal_throttle/core_throttle_count",
        i);

    value_t v_core;
    if (parse_value_file(filename, &v_core, DS_TYPE_COUNTER) != 0) {
      WARNING("thermal_throttle plugin: Reading \"%s\" failed.", filename);
      continue;
    }

    snprintf(
        filename, sizeof(filename),
        "/sys/devices/system/cpu/cpu%d/thermal_throttle/package_throttle_count",
        i);

    value_t v_package;
    if (parse_value_file(filename, &v_package, DS_TYPE_COUNTER) != 0) {
      WARNING("thermal_throttle plugin: Reading \"%s\" failed.", filename);
      continue;
    }

    thermal_throttle_submit(i, v_core, v_package);
  }

  return 0;
}

void module_register(void) {
  plugin_register_init("thermal_throttle", thermal_throttle_init);
  plugin_register_read("thermal_throttle", thermal_throttle_read);
}
