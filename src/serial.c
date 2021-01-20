/**
 * collectd - src/serial.c
 * Copyright (C) 2005,2006  David Bacher
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   David Bacher <drbacher at gmail.com>
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

static int serial_read(void) {
  FILE *fh;
  char buffer[1024];
  metric_family_t fam_serial_read = {
      .name = "serial_read_bytes_total",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_t fam_serial_write = {
      .name = "serial_write_bytes_total",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_t *fams[] = {&fam_serial_read, &fam_serial_write, NULL};

  /* there are a variety of names for the serial device */
  if ((fh = fopen("/proc/tty/driver/serial", "r")) == NULL &&
      (fh = fopen("/proc/tty/driver/ttyS", "r")) == NULL) {
    WARNING("serial: fopen: %s", STRERRNO);
    return -1;
  }

  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    derive_t rx = 0;
    derive_t tx = 0;
    bool have_rx = false;
    bool have_tx = false;
    size_t len;

    char *fields[16];
    int numfields;

    numfields = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));
    if (numfields < 6)
      continue;

    /*
     * 0: uart:16550A port:000003F8 irq:4 tx:0 rx:0
     * 1: uart:16550A port:000002F8 irq:3 tx:0 rx:0
     */
    len = strlen(fields[0]);
    if (len < 2)
      continue;
    if (fields[0][len - 1] != ':')
      continue;
    fields[0][len - 1] = '\0';

    for (int i = 1; i < numfields; i++) {
      len = strlen(fields[i]);
      if (len < 4)
        continue;

      if (strncmp(fields[i], "tx:", 3) == 0) {
        if (strtoderive(fields[i] + 3, &tx) == 0)
          have_tx = true;
      } else if (strncmp(fields[i], "rx:", 3) == 0) {
        if (strtoderive(fields[i] + 3, &rx) == 0)
          have_rx = true;
      }
    }

    if (have_rx && have_tx) {
      metric_t m = {0};
      metric_label_set(&m, "line", fields[0]);

      m.value.counter = rx;
      metric_family_metric_append(&fam_serial_read, m);

      m.value.counter = tx;
      metric_family_metric_append(&fam_serial_write, m);

      metric_reset(&m);
    }
  }

  for (size_t i = 0; fams[i] != NULL; i++) {
    if (fams[i]->metric.num > 0) {
      int status = plugin_dispatch_metric_family(fams[i]);
      if (status != 0) {
        ERROR("serial plugin: plugin_dispatch_metric_family failed: %s",
              STRERROR(status));
      }
      metric_family_metric_reset(fams[i]);
    }
  }

  fclose(fh);
  return 0;
} /* int serial_read */

void module_register(void) {
  plugin_register_read("serial", serial_read);
} /* void module_register */
