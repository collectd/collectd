/**
 * collectd - src/thermal_throttle.c
 * Copyright (C) 2017      notbaab
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

static void thermal_throttle_submit(int cpu_num, value_t v_value,
                                    char type_instance[]) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values = &v_value;
  vl.values_len = 1;

  sstrncpy(vl.plugin, "thermal_throttles", sizeof(vl.plugin));
  sstrncpy(vl.type, "total_throttles", sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type));
  snprintf(vl.plugin_instance, sizeof(vl.type_instance), "cpu%d", cpu_num);

  plugin_dispatch_values(&vl);
}

static int thermal_throttle_read(void) {
  int core_num = 0;

  while (1) {
    char filename[128];
    int status = snprintf(filename, sizeof(filename),
                          "/sys/devices/system/cpu/cpu%d/thermal_throttle/"
                          "core_throttle_count",
                          core_num);

    if ((status < 1) || (unsigned int)status >= sizeof(filename)) {
      break;
    }

    if (access(filename, R_OK)) {
      break;
    }

    value_t v_core;
    if (parse_value_file(filename, &v_core, DS_TYPE_COUNTER) != 0) {
      WARNING("thermal_throttle plugin: Reading \"%s\" failed.", filename);
      continue;
    }

    thermal_throttle_submit(core_num, v_core, "core");

    snprintf(
        filename, sizeof(filename),
        "/sys/devices/system/cpu/cpu%d/thermal_throttle/package_throttle_count",
        core_num);

    value_t v_package;

    if (parse_value_file(filename, &v_package, DS_TYPE_COUNTER) != 0) {
      WARNING("thermal_throttle plugin: Reading \"%s\" failed.", filename);
      continue;
    }

    thermal_throttle_submit(core_num, v_package, "package");
    core_num++;
  }

  return 0;
}

void module_register(void) {
  plugin_register_read("thermal_throttle", thermal_throttle_read);
}
