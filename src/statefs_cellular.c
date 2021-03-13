/**
 * collectd - src/statefs_cellular.c
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

 Cellular stats are collected from statefs Cellular namespace. Reported data
 units are as follows:
 
 signal_quality %

 Type instance is used to indicate the used network technology 

 Provider at
 https://git.merproject.org/mer-core/statefs-providers/blob/master/src/ofono/provider_ofono.cpp
 
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <string.h>

#define STATEFS_ROOT "/run/state/namespaces/Cellular/"
#define BFSZ 512

static void cellular_submit (const char *type, const char *type_instance, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  
  values[0].gauge = value;
  
  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "statefs_cellular", sizeof (vl.plugin));
  sstrncpy (vl.type, type, sizeof (vl.type));
  sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));
  
  plugin_dispatch_values (&vl);
}


static _Bool getvalue(const char *fname, gauge_t *value, char *buffer, size_t buffer_size)
{
  FILE *fh;
  if ((fh = fopen (fname, "r")) == NULL)
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


static int cellular_read (void)
{
  FILE *fh;
  
  char buffer[BFSZ];
  char technology[BFSZ];
  gauge_t value = NAN;

  if ((fh = fopen(STATEFS_ROOT "Technology", "r")) == NULL)
    {
      ERROR ("statefs_cellular plugin: technology file unavailable.");
      return (-1);
    }

  if ( fgets(technology, BFSZ, fh) == NULL ||
       strncmp("unknown", technology, BFSZ) == 0 )
    {
      fclose(fh);
      return (0); // empty file or unconnected
    }
  fclose(fh);
  
  if ( getvalue(STATEFS_ROOT "SignalStrength", &value, buffer, BFSZ) )
    {
      cellular_submit( "signal_strength", technology, value );
    }
  else
    {
      ERROR ("statefs_cellular plugin: statistics is unavailable.");
      return (-1);
    }
  
  return (0);
}

void module_register (void)
{
  plugin_register_read ("statefs_cellular", cellular_read);
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
