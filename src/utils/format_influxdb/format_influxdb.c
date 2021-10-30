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
 *   multiple Server directives by:
 *   Paul (systemcrash) <newtwen thatfunny_at_symbol gmail.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils_cache.h"

#include "utils/format_influxdb/format_influxdb.h"

static int format_influxdb_escape_string(char *buffer, size_t buffer_size,
                                         const char *string) {

  if ((buffer == NULL) || (string == NULL))
    return -EINVAL;

  if (buffer_size < 3)
    return -ENOMEM;

  int dst_pos = 0;

#define BUFFER_ADD(c)                                                          \
  do {                                                                         \
    if (dst_pos >= (buffer_size - 1)) {                                        \
      buffer[buffer_size - 1] = '\0';                                          \
      return -ENOMEM;                                                          \
    }                                                                          \
    buffer[dst_pos] = (c);                                                     \
    dst_pos++;                                                                 \
  } while (0)

  /* Escape special characters */
  for (int src_pos = 0; string[src_pos] != 0; src_pos++) {
    if ((string[src_pos] == '\\') || (string[src_pos] == ' ') ||
        (string[src_pos] == ',') || (string[src_pos] == '=') ||
        (string[src_pos] == '"')) {
      BUFFER_ADD('\\');
      BUFFER_ADD(string[src_pos]);
    } else
      BUFFER_ADD(string[src_pos]);
  } /* for */
  buffer[dst_pos] = 0;

#undef BUFFER_ADD

  return dst_pos;
} /* int format_influxdb_escape_string */

int format_influxdb_value_list(
    char *buffer, int buffer_len, const data_set_t *ds, const value_list_t *vl,
    bool store_rates, format_influxdb_time_precision_t time_precision) {
  int status;
  int offset = 0;
  gauge_t *rates = NULL;
  bool have_values = false;

  assert(0 == strcmp(ds->type, vl->type));

#define BUFFER_ADD_ESCAPE(...)                                                 \
  do {                                                                         \
    status = format_influxdb_escape_string(buffer + offset,                    \
                                           buffer_len - offset, __VA_ARGS__);  \
    if (status < 0)                                                            \
      return status;                                                           \
    offset += status;                                                          \
  } while (0)

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    status = snprintf(buffer + offset, buffer_len - offset, __VA_ARGS__);      \
    if ((status < 0) || (status >= (buffer_len - offset))) {                   \
      sfree(rates);                                                            \
      return -ENOMEM;                                                          \
    }                                                                          \
    offset += status;                                                          \
  } while (0)

  BUFFER_ADD_ESCAPE(vl->plugin);
  BUFFER_ADD(",host=");
  BUFFER_ADD_ESCAPE(vl->host);
  if (strcmp(vl->plugin_instance, "") != 0) {
    BUFFER_ADD(",instance=");
    BUFFER_ADD_ESCAPE(vl->plugin_instance);
  }
  if (strcmp(vl->type, "") != 0) {
    BUFFER_ADD(",type=");
    BUFFER_ADD_ESCAPE(vl->type);
  }
  if (strcmp(vl->type_instance, "") != 0) {
    BUFFER_ADD(",type_instance=");
    BUFFER_ADD_ESCAPE(vl->type_instance);
  }

  BUFFER_ADD(" ");
  for (size_t i = 0; i < ds->ds_num; i++) {
    if ((ds->ds[i].type != DS_TYPE_COUNTER) &&
        (ds->ds[i].type != DS_TYPE_GAUGE) &&
        (ds->ds[i].type != DS_TYPE_DERIVE) &&
        (ds->ds[i].type != DS_TYPE_ABSOLUTE)) {
      sfree(rates);
      return -EINVAL;
    }

    if (ds->ds[i].type == DS_TYPE_GAUGE) {
      if (isnan(vl->values[i].gauge))
        continue;
      if (have_values)
        BUFFER_ADD(",");
      BUFFER_ADD("%s=%lf", ds->ds[i].name, vl->values[i].gauge);
      have_values = true;
    } else if (store_rates) {
      if (rates == NULL)
        rates = uc_get_rate(ds, vl);
      if (rates == NULL) {
        WARNING("format_influxdb: "
                "uc_get_rate failed.");
        return -EINVAL;
      }
      if (isnan(rates[i]))
        continue;
      if (have_values)
        BUFFER_ADD(",");
      BUFFER_ADD("%s=%lf", ds->ds[i].name, rates[i]);
      have_values = true;
    } else if (ds->ds[i].type == DS_TYPE_COUNTER) {
      if (have_values)
        BUFFER_ADD(",");
      BUFFER_ADD("%s=%" PRIu64 "i", ds->ds[i].name,
                 (uint64_t)vl->values[i].counter);
      have_values = true;
    } else if (ds->ds[i].type == DS_TYPE_DERIVE) {
      if (have_values)
        BUFFER_ADD(",");
      BUFFER_ADD("%s=%" PRIi64 "i", ds->ds[i].name, vl->values[i].derive);
      have_values = true;
    } else if (ds->ds[i].type == DS_TYPE_ABSOLUTE) {
      if (have_values)
        BUFFER_ADD(",");
      BUFFER_ADD("%s=%" PRIu64 "i", ds->ds[i].name, vl->values[i].absolute);
      have_values = true;
    }

  } /* for ds->ds_num */
  sfree(rates);

  if (!have_values)
    return 0;

  uint64_t influxdb_time = 0;
  switch (time_precision) {
  case NS:
    influxdb_time = CDTIME_T_TO_NS(vl->time);
    break;
  case US:
    influxdb_time = CDTIME_T_TO_US(vl->time);
    break;
  case MS:
    influxdb_time = CDTIME_T_TO_MS(vl->time);
    break;
  }

  BUFFER_ADD(" %" PRIu64 "\n", influxdb_time);

#undef BUFFER_ADD_ESCAPE
#undef BUFFER_ADD

  return offset;
} /* int format_influxdb_value_list */
