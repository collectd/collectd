/**
 * collectd - src/synproxy.c
 * Copyright (C) 2017 Marek Becka
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
 *   Marek Becka <https://github.com/marekbecka>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

#define SYNPROXY_FIELDS 6

static const char *synproxy_stat_path = "/proc/net/stat/synproxy";

static void synproxy_submit(value_t *results) {
  metric_family_t fams[SYNPROXY_FIELDS - 1] = {
      {
          .name = "synproxy_connections_syn_received_total",
          .type = METRIC_TYPE_COUNTER,
      },
      {
          .name = "synproxy_cookies_invalid_total",
          .type = METRIC_TYPE_COUNTER,
      },
      {
          .name = "synproxy_cookies_valid_total",
          .type = METRIC_TYPE_COUNTER,
      },
      {
          .name = "synproxy_cookies_retransmission_total",
          .type = METRIC_TYPE_COUNTER,
      },
      {
          .name = "synproxy_connections_reopened_total",
          .type = METRIC_TYPE_COUNTER,
      },
  };

  metric_t m = {0};
  for (size_t n = 0; n < (SYNPROXY_FIELDS - 1); n++) {
    /* 1st column (entries) is hardcoded to 0 in kernel code */
    m.value.counter = results[n + 1].counter;
    metric_family_metric_append(&fams[n], m);

    int status = plugin_dispatch_metric_family(&fams[n]);
    if (status != 0) {
      ERROR("synproxy plugin: plugin_dispatch_metric_family failed: %s",
            STRERROR(status));
    }
    metric_family_metric_reset(&fams[n]);
  }
}

static int synproxy_read(void) {
  char buf[1024];
  value_t results[SYNPROXY_FIELDS];
  int is_header = 1, status = 0;

  FILE *fh = fopen(synproxy_stat_path, "r");
  if (fh == NULL) {
    ERROR("synproxy plugin: unable to open %s", synproxy_stat_path);
    return -1;
  }

  memset(results, 0, sizeof(results));

  while (fgets(buf, sizeof(buf), fh) != NULL) {
    char *fields[SYNPROXY_FIELDS], *endprt;

    if (is_header) {
      is_header = 0;
      continue;
    }

    int numfields = strsplit(buf, fields, STATIC_ARRAY_SIZE(fields));
    if (numfields != SYNPROXY_FIELDS) {
      ERROR("synproxy plugin: unexpected number of columns in %s",
            synproxy_stat_path);
      status = -1;
      break;
    }

    /* 1st column (entries) is hardcoded to 0 in kernel code */
    for (size_t n = 1; n < SYNPROXY_FIELDS; n++) {
      char *endptr = NULL;
      errno = 0;

      results[n].counter += strtoull(fields[n], &endprt, 16);
      if ((endptr == fields[n]) || errno != 0) {
        ERROR("synproxy plugin: unable to parse value: %s", fields[n]);
        fclose(fh);
        return -1;
      }
    }
  }

  fclose(fh);

  if (status == 0) {
    synproxy_submit(results);
  }

  return status;
}

void module_register(void) {
  plugin_register_read("synproxy", synproxy_read);
} /* void module_register */
