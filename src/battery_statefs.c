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

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <stdio.h>

#define STATEFS_ROOT "/run/state/namespaces/Battery/"
#define BUFFER_SIZE 512

static int submitted_this_run = 0;

static void battery_submit(const char *type, gauge_t value,
                           const char *type_instance) {
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "battery", sizeof(vl.plugin));
  /* statefs supports 1 battery at present */
  sstrncpy(vl.plugin_instance, "0", sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);

  submitted_this_run++;
}

static _Bool getvalue(const char *fname, gauge_t *value) {
  FILE *fh;
  char buffer[BUFFER_SIZE];

  if ((fh = fopen(fname, "r")) == NULL) {
    WARNING("battery plugin: cannot open StateFS file %s", fname);
    return (0);
  }

  if (fgets(buffer, STATIC_ARRAY_SIZE(buffer), fh) == NULL) {
    fclose(fh);
    return (0); // empty file
  }

  (*value) = atof(buffer);

  fclose(fh);

  return (1);
}

/* cannot be static, is referred to from battery.c */
int battery_read_statefs(void) {
  gauge_t value = NAN;

  submitted_this_run = 0;

  if (getvalue(STATEFS_ROOT "ChargePercentage", &value))
    battery_submit("charge", value, NULL);
  // Use capacity as a charge estimate if ChargePercentage is not available
  else if (getvalue(STATEFS_ROOT "Capacity", &value))
    battery_submit("charge", value, NULL);

  if (getvalue(STATEFS_ROOT "Current", &value))
    battery_submit("current", value * 1e-6, NULL); // from uA to A

  if (getvalue(STATEFS_ROOT "Energy", &value))
    battery_submit("energy_wh", value * 1e-6, NULL); // from uWh to Wh

  if (getvalue(STATEFS_ROOT "Power", &value))
    battery_submit("power", value * 1e-6, NULL); // from uW to W

  if (getvalue(STATEFS_ROOT "Temperature", &value))
    battery_submit("temperature", value * 0.1, NULL); // from 10xC to C

  if (getvalue(STATEFS_ROOT "TimeUntilFull", &value))
    battery_submit("duration", value, "full");

  if (getvalue(STATEFS_ROOT "TimeUntilLow", &value))
    battery_submit("duration", value, "low");

  if (getvalue(STATEFS_ROOT "Voltage", &value))
    battery_submit("voltage", value * 1e-6, NULL); // from uV to V

  if (submitted_this_run == 0) {
    ERROR("battery plugin: statefs backend: none of the statistics are "
          "available");
    return (-1);
  }

  return (0);
}
