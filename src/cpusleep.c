/**
 * collectd - src/cpusleep.c
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

 CPU sleep is reported in milliseconds / s. For that, derive type was
 selected and the time difference between BOOT and MONOTONIC clocks
 fed to RRD
 
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include <time.h>

static void cpusleep_submit (derive_t cpu_sleep)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  
  values[0].derive = (derive_t) cpu_sleep;
  
  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "cpusleep", sizeof (vl.plugin));
  sstrncpy (vl.type, "cpusleep", sizeof (vl.type));
  
  plugin_dispatch_values (&vl);
}

static int cpusleep_read (void)
{
  struct timespec b, m;
  if ( clock_gettime(CLOCK_BOOTTIME, &b ) < 0 )
    {
      ERROR("cpusleep plugin: clock_boottime failed");
      return (-1);
    }

  if ( clock_gettime(CLOCK_MONOTONIC, &m ) < 0 )
    {
      ERROR("cpusleep plugin: clock_monotonic failed");
      return (-1);
    }

  double db = b.tv_sec + 1e-9 * b.tv_nsec;
  double dm = m.tv_sec + 1e-9 * m.tv_nsec;

  // to avoid false positives in counter overflow due to reboot,
  // derive is used
  derive_t sleep = (derive_t) ((db-dm) * 1000);  

  cpusleep_submit (sleep);

  return 0;
}

void module_register (void)
{
  plugin_register_read ("cpusleep", cpusleep_read);
} /* void module_register */
