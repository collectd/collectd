/**
 * collectd - src/suspend.c
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

 Suspend attempts reported by Linux kernel. Reported data units are counts/s.

 Type instance is used to indicate the count: successful, failed, and
 failed type

 Info at https://www.kernel.org/doc/Documentation/power/basic-pm-debugging.txt

 **/

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#include <stdlib.h>
#include <string.h>

#define SUSPEND_STATS "/sys/kernel/debug/suspend_stats"
#define BFSZ 1024

static int suspend_init(void) {
  if (access(SUSPEND_STATS, R_OK) == 0) // everything is fine, continue
    return (0);

  // either debugFS is not mounted or permissions are not allowing to
  // see suspend stats
  INFO("suspend plugin: cannot read %s, unregistered plugin", SUSPEND_STATS);
  plugin_unregister_read("suspend");

  return (0);
}

static void suspend_submit(const char *type, const char *type_instance,
                           derive_t value) {
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].derive = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "suspend", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static int suspend_read(void) {
  FILE *fh;

  char buffer[BFSZ];
  derive_t value;
  char *dummy;
  char *fields[2];
  int numfields;
  size_t count = 0;

  if ((fh = fopen(SUSPEND_STATS, "r")) == NULL) {
    ERROR("suspend plugin: %s unavailable or inaccessible", SUSPEND_STATS);
    return (-1);
  }

  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    if (!(dummy = strchr(buffer, ':')))
      continue;
    dummy[0] = ' ';

    numfields = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));

    if (numfields != 2) // unsupported line, move on
      continue;

    if (strcmp("success", fields[0]) == 0 || strcmp("fail", fields[0]) == 0 ||
        strncmp("failed_", fields[0], 7) == 0) {
      value = atoll(fields[1]);
      suspend_submit("attempts", fields[0], value);
      ++count;
    }
  }

  fclose(fh);

  if (count == 0) {
    ERROR("suspend plugin: statistics is unavailable.");
    return (-1);
  }

  return (0);
}

void module_register(void) {
  plugin_register_init("suspend", suspend_init);
  plugin_register_read("suspend", suspend_read);
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
