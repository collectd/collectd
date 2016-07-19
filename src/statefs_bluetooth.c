/**
 * collectd - src/statefs_bluetooth.c
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

 Bluetooth stats are collected from statefs Bluetooth namespace. Reported
 stats are either 0 or 1
 
 Provider at
 https://git.merproject.org/mer-core/statefs-providers/tree/master/src/bluez
 
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <stdio.h>

#define STATEFS_ROOT "/run/state/namespaces/Bluetooth/"
#define BFSZ 512

static int submitted_this_run = 0;

static void bluetooth_submit (const char *type, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  
  values[0].gauge = value;
  
  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "statefs_bluetooth", sizeof (vl.plugin));
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


static int bluetooth_read (void)
{
  char buffer[BFSZ];
  gauge_t value = NAN;
  
  submitted_this_run = 0;
  

  if ( getvalue(STATEFS_ROOT "Connected", &value, buffer, BFSZ) )
    bluetooth_submit( "bluetooth_connected", value );

  if ( getvalue(STATEFS_ROOT "Enabled", &value, buffer, BFSZ) )
    bluetooth_submit( "bluetooth_enabled", value );

  if ( getvalue(STATEFS_ROOT "Visible", &value, buffer, BFSZ) )
    bluetooth_submit( "bluetooth_visible", value );

  if ( submitted_this_run == 0 )
    {
      ERROR ("statefs_bluetooth plugin: none of the statistics are available.");
      return (-1);
    }
  
  return (0);
}

void module_register (void)
{
  plugin_register_read ("statefs_bluetooth", bluetooth_read);
} /* void module_register */


/*
 * Local variables:
 *  c-file-style: "gnu"
 *  indent-tabs-mode: nil
 *  c-indent-level: 4
 *  c-basic-offset: 2
 *  tab-width: 4
 * End:
 */
