/**
 * collectd - src/conntrack.c
 * Copyright (C) 2009  Tomasz Pala
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Tomasz Pala <gotar at pld-linux.org>
 * based on entropy.c by:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

#define CONNTRACK_FILE "/proc/sys/net/netfilter/nf_conntrack_count"
#define CONNTRACK_MAX_FILE "/proc/sys/net/netfilter/nf_conntrack_max"
#define CONNTRACK_FILE_OLD "/proc/sys/net/ipv4/netfilter/ip_conntrack_count"
#define CONNTRACK_MAX_FILE_OLD "/proc/sys/net/ipv4/netfilter/ip_conntrack_max"

static const char *config_keys[] = {"OldFiles"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
/*
    Each table/chain combo that will be queried goes into this list
*/

static int old_files;

static int conntrack_config(const char *key, const char *value) {
  if (strcmp(key, "OldFiles") == 0)
    old_files = 1;

  return 0;
}

static void conntrack_submit(char *fam_name, gauge_t value) {
  metric_family_t fam = {
      .name = fam_name,
      .type = METRIC_TYPE_GAUGE,
  };

  metric_family_metric_append(&fam, (metric_t){
                                        .value.gauge = value,
                                    });

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR("conntrack plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }

  metric_family_metric_reset(&fam);
} /* static void conntrack_submit */

static int conntrack_read(void) {
  value_t conntrack, conntrack_max, conntrack_pct;

  char const *path = old_files ? CONNTRACK_FILE_OLD : CONNTRACK_FILE;
  if (parse_value_file(path, &conntrack, DS_TYPE_GAUGE) != 0) {
    ERROR("conntrack plugin: Reading \"%s\" failed.", path);
    return -1;
  }

  path = old_files ? CONNTRACK_MAX_FILE_OLD : CONNTRACK_MAX_FILE;
  if (parse_value_file(path, &conntrack_max, DS_TYPE_GAUGE) != 0) {
    ERROR("conntrack plugin: Reading \"%s\" failed.", path);
    return -1;
  }

  conntrack_pct.gauge = (conntrack.gauge / conntrack_max.gauge) * 100;

  conntrack_submit("conntrack_used", conntrack.gauge);
  conntrack_submit("conntrack_max", conntrack_max.gauge);
  conntrack_submit("conntrack_used_percent", conntrack_pct.gauge);

  return 0;
} /* static int conntrack_read */

void module_register(void) {
  plugin_register_config("conntrack", conntrack_config, config_keys,
                         config_keys_num);
  plugin_register_read("conntrack", conntrack_read);
} /* void module_register */
