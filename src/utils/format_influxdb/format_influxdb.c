/**
 * collectd - src/utils_format_influxdb.c
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2009       Aman Gupta
 * Copyright (C) 2019       Carlos Peon Costa
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
 *   Florian octo Forster <octo at collectd.org>
 *   Aman Gupta <aman at tmm1.net>
 *   Carlos Peon Costa <carlospeon at gmail.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils_cache.h"

#include "utils/format_influxdb/format_influxdb.h"

int format_influxdb_point(strbuf_t *sb, metric_t metric, bool store_rates) {
  bool have_values = false;

#define BUFFER_ADD_ESCAPE(...)                                                 \
  do {                                                                         \
    int status = strbuf_print_escaped(sb, __VA_ARGS__, "\\ ,=\"", '\\');       \
    if (status != 0)                                                           \
      return status;                                                           \
  } while (0)

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    int status = strbuf_printf(sb, __VA_ARGS__);                               \
    if (status != 0)                                                           \
      return status;                                                           \
  } while (0)

  have_values = false;
  BUFFER_ADD_ESCAPE(metric.family->name);
  for (size_t j = 0; j < metric.label.num; j++) {
    label_pair_t label = metric.label.ptr[j];
    BUFFER_ADD(",");
    BUFFER_ADD_ESCAPE(label.name);
    BUFFER_ADD("=");
    BUFFER_ADD_ESCAPE(label.value);
  }
  BUFFER_ADD(" ");

  if (store_rates && (metric.family->type == METRIC_TYPE_COUNTER)) {
    gauge_t rate;
    if (uc_get_rate(&metric, &rate) != 0) {
      WARNING("write_influxdb_udp plugin: "
              "uc_get_rate failed.");
      return EINVAL;
    }
    if (!isnan(rate)) {
      BUFFER_ADD("value=" GAUGE_FORMAT, rate);
      have_values = true;
    }
  } else {
    switch (metric.family->type) {
    case METRIC_TYPE_GAUGE:
    case METRIC_TYPE_UNTYPED:
      if (!isnan(metric.value.gauge)) {
        BUFFER_ADD("value=" GAUGE_FORMAT, metric.value.gauge);
        have_values = true;
      }
      break;
    case METRIC_TYPE_COUNTER:
      BUFFER_ADD("value=%" PRIi64 "i", metric.value.counter);
      have_values = true;
      break;
    default:
      WARNING("write_influxdb_udp plugin: "
              "unknown family type.");
      return EINVAL;
      break;
    }
  }

  if (!have_values)
    return 0;

  BUFFER_ADD(" %" PRIu64 "\n", CDTIME_T_TO_MS(metric.time));

#undef BUFFER_ADD_ESCAPE
#undef BUFFER_ADD

  return 0;
} /* int write_influxdb_point */
