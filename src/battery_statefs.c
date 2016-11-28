/**
 * collectd - src/statefs_battery.c
 * Copyright (C) 2016 rinigus
 *
 *
The MIT License (MIT)

Permission is hereby granted, free of charge, to any person obtaining
a copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

 * Authors:
 *   rinigus <http://github.com/rinigus>

 Battery stats are collected from StateFS Battery namespace. Reported
 units are as follows:

 capacity %
 charge %
 current A
 energy Wh
 power W
 temperature C
 timefull and timelow seconds
 voltage V

 Provider at
 https://git.merproject.org/mer-core/statefs-providers/blob/master/src/power_udev/provider_power_udev.cpp

 **/

#include "common.h"
#include "plugin.h"
#include "collectd.h"

#include <stdio.h>

#define STATEFS_ROOT "/run/state/namespaces/Battery/"

static void battery_submit(const char *type, gauge_t value,
                           const char *type_instance) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "battery", sizeof(vl.plugin));
  /* statefs supports 1 battery at present */
  sstrncpy(vl.plugin_instance, "0", sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);
}

/* cannot be static, is referred to from battery.c */
int battery_read_statefs(void) {
  value_t v;
  int success = 0;

  if (parse_value_file(STATEFS_ROOT "ChargePercentage", &v, DS_TYPE_GAUGE) ==
      0) {
    battery_submit("charge", v.gauge, NULL);
    success++;
  } else if (parse_value_file(STATEFS_ROOT "Capacity", &v, DS_TYPE_GAUGE) ==
             0) {
    // Use capacity as a charge estimate if ChargePercentage is not available
    battery_submit("charge", v.gauge, NULL);
    success++;
  } else {
    WARNING("battery plugin: Neither \"" STATEFS_ROOT "ChargePercentage\" "
            "nor \"" STATEFS_ROOT "Capacity\" could be read.");
  }

  struct {
    char *path;
    char *type;
    char *type_instance;
    gauge_t factor;
  } metrics[] = {
      {STATEFS_ROOT "Current", "current", NULL, 1e-6},        // from uA to A
      {STATEFS_ROOT "Energy", "energy_wh", NULL, 1e-6},       // from uWh to Wh
      {STATEFS_ROOT "Power", "power", NULL, 1e-6},            // from uW to W
      {STATEFS_ROOT "Temperature", "temperature", NULL, 0.1}, // from 10xC to C
      {STATEFS_ROOT "TimeUntilFull", "duration", "full", 1.0},
      {STATEFS_ROOT "TimeUntilLow", "duration", "low", 1.0},
      {STATEFS_ROOT "Voltage", "voltage", NULL, 1e-6}, // from uV to V
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(metrics); i++) {
    if (parse_value_file(metrics[i].path, &v, DS_TYPE_GAUGE) != 0) {
      WARNING("battery plugin: Reading \"%s\" failed.", metrics[i].path);
      continue;
    }

    battery_submit(metrics[i].type, v.gauge * metrics[i].factor,
                   metrics[i].type_instance);
    success++;
  }

  if (success == 0) {
    ERROR("battery plugin: statefs backend: none of the statistics are "
          "available");
    return (-1);
  }

  return (0);
}
