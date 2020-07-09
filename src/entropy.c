/**
 * collectd - src/entropy.c
 * Copyright (C) 2007       Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

static void entropy_submit(value_t);
static int entropy_read(void);

#if !KERNEL_LINUX && !KERNEL_NETBSD
#error "No applicable input method."
#endif

#if KERNEL_LINUX
#define ENTROPY_FILE "/proc/sys/kernel/random/entropy_avail"

static int entropy_read(void) {
  value_t v;
  if (parse_value_file(ENTROPY_FILE, &v, DS_TYPE_GAUGE) != 0) {
    ERROR("entropy plugin: Reading \"" ENTROPY_FILE "\" failed.");
    return -1;
  }

  entropy_submit(v);
  return 0;
}
#endif /* KERNEL_LINUX */

#if KERNEL_NETBSD
/* Provide a NetBSD implementation, partial from rndctl.c */

/*
 * Improved to keep the /dev/urandom open, since there's a consumption
 * of entropy from /dev/random for every open of /dev/urandom, and this
 * will end up opening /dev/urandom lots of times.
 */

#include <sys/ioctl.h>
#include <sys/param.h>
#include <sys/rnd.h>
#include <sys/types.h>
#if HAVE_SYS_RNDIO_H
#include <sys/rndio.h>
#endif
#include <paths.h>

static int entropy_read(void) {
  value_t v;
  rndpoolstat_t rs;
  static int fd;
  char buf[30];

  if (fd == 0) {
    fd = open(_PATH_URANDOM, O_RDONLY, 0644);
    if (fd < 0) {
      fd = 0;
      return -1;
    }
  }

  if (ioctl(fd, RNDGETPOOLSTAT, &rs) < 0) {
    (void)close(fd);
    fd = 0; /* signal a reopening on next attempt */
    return -1;
  }
  snprintf(buf, sizeof(buf), "%ju", (uintmax_t)rs.curentropy);
  if (parse_value(buf, &v, DS_TYPE_GAUGE) != 0) {
    ERROR("entropy plugin: Parsing \"%s\" failed.", buf);
    return (-1);
  }

  entropy_submit(v);

  return 0;
}

#endif /* KERNEL_NETBSD */

static void entropy_submit(value_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &value;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "entropy", sizeof(vl.plugin));
  sstrncpy(vl.type, "entropy", sizeof(vl.type));

  plugin_dispatch_values(&vl);
}

void module_register(void) {
  plugin_register_read("entropy", entropy_read);
} /* void module_register */
