/**
 * collectd - src/buddyinfo.c
 * Copyright (C) 2019       Asaf Kahlon
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
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
 *   Asaf Kahlon <asafka7 at gmail.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

#include <unistd.h>

#define MAX_ORDER 11
#define BUDDYINFO_FIELDS                                                       \
  MAX_ORDER + 4 // "Node" + node_num + "zone" + Name + (MAX_ORDER entries)
#define NUM_OF_KB(pagesize, order) ((pagesize) / 1024) * (1 << (order))

static const char *config_keys[] = {"Zone"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist;

static int buddyinfo_config(const char *key, const char *value) {
  if (ignorelist == NULL) {
    ignorelist = ignorelist_create(1);
    if (ignorelist == NULL) {
      ERROR("buddyinfo plugin: ignorelist_create failed");
      return -ENOMEM;
    }
  }

  if (strcasecmp(key, "Zone") == 0) {
    if (ignorelist_add(ignorelist, value)) {
      ERROR("buddyinfo plugin: cannot add value to ignorelist");
      return -1;
    }
  } else {
    ERROR("buddyinfo plugin: invalid option: %s", key);
    return -1;
  }

  return 0;
}

static int buddyinfo_read(void) {
  FILE *fh;
  char buffer[1024], pagesize_kb[8], node_name[16];
  char *dummy, *zone;
  char *fields[BUDDYINFO_FIELDS];
  int node_num, numfields, pagesize = getpagesize();
  metric_family_t fam = {
      .name = "buddyinfo_freepages",
      .type = METRIC_TYPE_GAUGE,
  };

  if ((fh = fopen("/proc/buddyinfo", "r")) == NULL) {
    WARNING("buddyinfo plugin: fopen: %s", STRERRNO);
    return -1;
  }

  metric_t m = {0};

  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    if (!(dummy = strstr(buffer, "Node")))
      continue;

    numfields = strsplit(dummy, fields, BUDDYINFO_FIELDS);
    if (numfields != BUDDYINFO_FIELDS) {
      WARNING("line %s doesn't contain %d orders, skipping...", buffer,
              MAX_ORDER);
      continue;
    }

    node_num = atoi(fields[1]);
    zone = fields[3];

    if (ignorelist_match(ignorelist, zone) != 0)
      continue;

    ssnprintf(node_name, sizeof(node_name), "%d", node_num);
    metric_label_set(&m, "node", node_name);
    metric_label_set(&m, "zone", zone);

    for (int i = 1; i <= MAX_ORDER; i++) {
      ssnprintf(pagesize_kb, sizeof(pagesize_kb), "%d",
                NUM_OF_KB(pagesize, i - 1));
      metric_label_set(&m, "pagesize_kb", pagesize_kb);

      m.value.gauge = atoi(fields[i + 3]);
      metric_family_metric_append(&fam, m);
    }
  }

  metric_reset(&m);

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR("buddyinfo plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }

  metric_family_metric_reset(&fam);

  fclose(fh);
  return 0;
}

static int buddyinfo_shutdown(void) {
  ignorelist_free(ignorelist);

  return 0;
}

void module_register(void) {

  plugin_register_config("buddyinfo", buddyinfo_config, config_keys,
                         config_keys_num);
  plugin_register_read("buddyinfo", buddyinfo_read);
  plugin_register_shutdown("buddy", buddyinfo_shutdown);
}
