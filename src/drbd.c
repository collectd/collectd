/**
 * collectd - src/drbd.c
 * Copyright (C) 2014  Tim Laszlo
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
 *   Tim Laszlo <tim.laszlo at gmail.com>
 **/

/*
 See: http://www.drbd.org/users-guide/ch-admin.html#s-performance-indicators

 version: 8.3.11 (api:88/proto:86-96)
 srcversion: 71955441799F513ACA6DA60
  0: cs:Connected ro:Primary/Secondary ds:UpToDate/UpToDate B r-----
         ns:64363752 nr:0 dw:357799284 dr:846902273 al:34987022 bm:18062 lo:0 \
                                                pe:0 ua:0 ap:0 ep:1 wo:f oos:0
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"

static const char *drbd_stats = "/proc/drbd";
static const char *drbd_names[] = {
    "network_send",   /* ns (network send) */
    "network_recv",   /* nr (network receive) */
    "disk_write",     /* dw (disk write) */
    "disk_read",      /* dr (disk read) */
    "activity_log",   /* al (activity log) */
    "bitmap",         /* bm (bit map) */
    "local_count",    /* lo (local count) */
    "pending",        /* pe (pending) */
    "unacknowledged", /* ua (unacknowledged) */
    "app pending",    /* ap (application pending) */
    "epochs",         /* ep (epochs) */
    NULL,             /* wo (write order) */
    "oos"             /* oos (out of sync) */
};
static size_t drbd_names_num = STATIC_ARRAY_SIZE(drbd_names);

static int drbd_init(void) { return 0; }

static int drbd_submit_fields(long int resource, char **fields,
                              size_t fields_num) {
  char plugin_instance[DATA_MAX_NAME_LEN];
  value_t values[fields_num];
  value_list_t vl = VALUE_LIST_INIT;

  if (resource < 0) {
    WARNING("drbd plugin: Unable to parse resource");
    return EINVAL;
  }

  if (fields_num != drbd_names_num) {
    WARNING("drbd plugin: Wrong number of fields for "
            "r%ld statistics. Expected %" PRIsz ", got %" PRIsz ".",
            resource, drbd_names_num, fields_num);
    return EINVAL;
  }

  snprintf(plugin_instance, sizeof(plugin_instance), "r%ld", resource);

  for (size_t i = 0; i < drbd_names_num; i++) {
    char *data;
    /* skip non numeric wo */
    if (strncmp(fields[i], "wo", 2) == 0)
      continue;
    data = strchr(fields[i], ':');
    if (data == NULL)
      return EINVAL;
    (void)parse_value(++data, &values[i], DS_TYPE_DERIVE);
  }

  vl.values_len = 1;
  sstrncpy(vl.plugin, "drbd", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "drbd_resource", sizeof(vl.type));

  for (size_t i = 0; i < fields_num; i++) {
    if (drbd_names[i] == NULL)
      continue;
    vl.values = values + i;
    sstrncpy(vl.type_instance, drbd_names[i], sizeof(vl.type_instance));
    plugin_dispatch_values(&vl);
  }

  return 0;
} /* drbd_submit_fields */

static int drbd_read(void) {
  FILE *fh;
  char buffer[256];

  long int resource = -1;
  char *fields[16];
  int fields_num = 0;

  fh = fopen(drbd_stats, "r");
  if (fh == NULL) {
    WARNING("drbd plugin: Unable to open %s", drbd_stats);
    return EINVAL;
  }

  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    fields_num = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));

    /* ignore headers (first two iterations) */
    if ((strcmp(fields[0], "version:") == 0) ||
        (strcmp(fields[0], "srcversion:") == 0) ||
        (strcmp(fields[0], "GIT-hash:") == 0)) {
      continue;
    }

    if (isdigit(fields[0][0])) {
      /* parse the resource line, next loop iteration
         will submit values for this resource */
      resource = strtol(fields[0], NULL, 10);
    } else {
      /* handle stats data for the resource defined in the
         previous iteration */
      drbd_submit_fields(resource, fields, fields_num);
    }
  } /* while (fgets) */

  fclose(fh);
  return 0;
} /* void drbd_read */

void module_register(void) {
  plugin_register_init("drbd", drbd_init);
  plugin_register_read("drbd", drbd_read);
} /* void module_register */
