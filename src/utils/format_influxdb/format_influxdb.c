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

#define NEED_ESCAPE "\\ ,=\""
#define ESCAPE_CHAR '\\'

#define ERR_COMBINE(err, f)                                                    \
  do {                                                                         \
    err = err ? err : f;                                                       \
  } while (0)

static int format_metric_identity(strbuf_t *sb, metric_t const *m) {
  int err = strbuf_print_escaped(sb, m->family->name, NEED_ESCAPE, ESCAPE_CHAR);
  for (size_t j = 0; j < m->label.num; j++) {
    label_pair_t label = m->label.ptr[j];
    ERR_COMBINE(err, strbuf_printf(sb, ","));
    ERR_COMBINE(err,
                strbuf_print_escaped(sb, label.name, NEED_ESCAPE, ESCAPE_CHAR));
    ERR_COMBINE(err, strbuf_printf(sb, "="));
    ERR_COMBINE(err,
                strbuf_print_escaped(sb, label.value, "\\ ,=\"", ESCAPE_CHAR));
  }
  ERR_COMBINE(err, strbuf_printf(sb, " "));
  return err;
}

static int format_metric_rate(strbuf_t *sb, metric_t const *m) {
  gauge_t rate = 0;
  if (uc_get_rate(m, &rate) != 0) {
    WARNING("write_influxdb_udp plugin: uc_get_rate failed.");
    return EINVAL;
  }
  if (isnan(rate)) {
    return EAGAIN;
  }

  return strbuf_printf(sb, "value=" GAUGE_FORMAT, rate);
}

static int format_metric_value(strbuf_t *sb, metric_t const *m,
                               bool store_rate) {
  if (store_rate) {
    return format_metric_rate(sb, m);
  }

  switch (m->family->type) {
  case METRIC_TYPE_GAUGE:
    if (isnan(m->value.gauge)) {
      return EAGAIN;
    }
    return strbuf_printf(sb, "value=" GAUGE_FORMAT, m->value.gauge);
  case METRIC_TYPE_COUNTER:
    return strbuf_printf(sb, "value=%" PRIu64 "i", m->value.counter);
  case METRIC_TYPE_FPCOUNTER:
    return strbuf_printf(sb, "value=" GAUGE_FORMAT, m->value.fpcounter);
  case METRIC_TYPE_UNTYPED:
  }

  ERROR("format_influxdb plugin: invalid metric type: %d", m->family->type);
  return EINVAL;
}

static int format_metric_time(strbuf_t *sb, metric_t const *m) {
  return strbuf_printf(sb, " %" PRIu64 "\n", CDTIME_T_TO_MS(m->time));
}

int format_influxdb_point(strbuf_t *sb, metric_t const *m, bool store_rate) {
  int err = format_metric_identity(sb, m);
  if (err != 0) {
    return err;
  }

  err = format_metric_value(sb, m, store_rate);
  if (err == EAGAIN) {
    return 0;
  }
  if (err != 0) {
    return err;
  }

  return format_metric_time(sb, m);
} /* int write_influxdb_point */
