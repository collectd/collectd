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

 Battery stats are collected from statefs Battery namespace. Reported
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
#define BFSZ 512

static int submitted_this_run = 0;

static void battery_submit (const char *type, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  
  values[0].gauge = value;
  
  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "statefs_battery", sizeof (vl.plugin));
  sstrncpy (vl.type, type, sizeof (vl.type));
  
  plugin_dispatch_values (&vl);

  submitted_this_run++;
}


static _Bool getvalue(const char *fname, gauge_t *value, char *buffer, size_t buffer_size)
{
  FILE *fh;
  if ((fh = fopen(fname, "r")) == NULL)
    return (0);

  if ( fgets(buffer, buffer_size, fh) == NULL )
    {
      fclose(fh);
      return (0); // empty file
    }

  (*value) = atof( buffer );

  fclose(fh);

  return (1);
}


static int battery_read (void)
{
  char buffer[BFSZ];
  gauge_t value = NAN;
  
  submitted_this_run = 0;
  

  if ( getvalue(STATEFS_ROOT "ChargePercentage", &value, buffer, BFSZ) )
    battery_submit( "charge", value );
  // Use capacity as a charge estimate if ChargePercentage is not available
  else if (getvalue(STATEFS_ROOT "Capacity", &value, buffer, BFSZ) )
    battery_submit( "charge", value );

  if ( getvalue(STATEFS_ROOT "Current", &value, buffer, BFSZ) )
    battery_submit( "current", value * 1e-6 ); // from uA to A

  if ( getvalue(STATEFS_ROOT "Energy", &value, buffer, BFSZ) )
    battery_submit( "energy", value * 1e-6 ); // from uWh to Wh

  if ( getvalue(STATEFS_ROOT "Power", &value, buffer, BFSZ) )
    battery_submit( "power_battery", value * 1e-6 ); // from uW to W
  
  if ( getvalue(STATEFS_ROOT "Temperature", &value, buffer, BFSZ) )
    battery_submit( "temperature", value * 0.1 ); // from 10xC to C
  
  if ( getvalue(STATEFS_ROOT "TimeUntilFull", &value, buffer, BFSZ) )
    battery_submit( "timefull", value );

  if ( getvalue(STATEFS_ROOT "TimeUntilLow", &value, buffer, BFSZ) )
    battery_submit( "timelow", value );
  
  if ( getvalue(STATEFS_ROOT "Voltage", &value, buffer, BFSZ) )
    battery_submit( "voltage", value * 1e-6 ); // from uV to V

  if ( submitted_this_run == 0 )
    {
      ERROR ("statefs_battery plugin: none of the statistics are available.");
      return (-1);
    }
  
  return (0);
}

void module_register (void)
{
  plugin_register_read ("statefs_battery", battery_read);
} /* void module_register */
