/**
 * collectd - src/utils_format_kairosdb.c
 * Copyright (C) 2016       Aurelien beorn Rougemont
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
 *   Aurelien beorn Rougemont <beorn at gandi dot net>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include "utils_cache.h"
#include "utils_format_kairosdb.h"

/* This is the KAIROSDB format for write_http output
 *
 * Target format
 * [
 *   {
 *     "name":"collectd.vmem"
 *     "datapoints":
 *       [
 *         [1453897164060, 97.000000]
 *       ],
 *      "tags":
 *        {
 *          "host": "fqdn.domain.tld",
 *          "plugin_instance": "vmpage_number",
 *          "type": "kernel_stack",
 *          "ds": "value"
 *          ""
 *        }
 *   }
 * ]
 */

static int kairosdb_escape_string(char *buffer, size_t buffer_size, /* {{{ */
                                  const char *string) {
  size_t dst_pos;

  if ((buffer == NULL) || (string == NULL))
    return -EINVAL;

  if (buffer_size < 3)
    return -ENOMEM;

  dst_pos = 0;

#define BUFFER_ADD(c)                                                          \
  do {                                                                         \
    if (dst_pos >= (buffer_size - 1)) {                                        \
      buffer[buffer_size - 1] = 0;                                             \
      return -ENOMEM;                                                          \
    }                                                                          \
    buffer[dst_pos] = (c);                                                     \
    dst_pos++;                                                                 \
  } while (0)

  /* Escape special characters */
  /* authorize -_. and alpha num but also escapes " */
  BUFFER_ADD('"');
  for (size_t src_pos = 0; string[src_pos] != 0; src_pos++) {
    if (isalnum(string[src_pos]) || 0x2d == string[src_pos] ||
        0x2e == string[src_pos] || 0x5f == string[src_pos])
      BUFFER_ADD(tolower(string[src_pos]));
  } /* for */
  BUFFER_ADD('"');
  buffer[dst_pos] = 0;

#undef BUFFER_ADD

  return 0;
} /* }}} int kairosdb_escape_string */

static int values_to_kairosdb(char *buffer, size_t buffer_size, /* {{{ */
                              const data_set_t *ds, const value_list_t *vl,
                              int store_rates, size_t ds_idx) {
  size_t offset = 0;
  gauge_t *rates = NULL;

  memset(buffer, 0, buffer_size);

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    int status;                                                                \
    status = snprintf(buffer + offset, buffer_size - offset, __VA_ARGS__);     \
    if (status < 1) {                                                          \
      sfree(rates);                                                            \
      return -1;                                                               \
    } else if (((size_t)status) >= (buffer_size - offset)) {                   \
      sfree(rates);                                                            \
      return -ENOMEM;                                                          \
    } else                                                                     \
      offset += ((size_t)status);                                              \
  } while (0)

  if (ds->ds[ds_idx].type == DS_TYPE_GAUGE) {
    if (isfinite(vl->values[ds_idx].gauge)) {
      BUFFER_ADD("[[");
      BUFFER_ADD("%" PRIu64, CDTIME_T_TO_MS(vl->time));
      BUFFER_ADD(",");
      BUFFER_ADD(JSON_GAUGE_FORMAT, vl->values[ds_idx].gauge);
    } else {
      DEBUG("utils_format_kairosdb: invalid vl->values[ds_idx].gauge for "
            "%s|%s|%s|%s|%s",
            vl->plugin, vl->plugin_instance, vl->type, vl->type_instance,
            ds->ds[ds_idx].name);
      return -1;
    }
  } else if (store_rates) {
    if (rates == NULL)
      rates = uc_get_rate(ds, vl);
    if (rates == NULL) {
      WARNING("utils_format_kairosdb: uc_get_rate failed for %s|%s|%s|%s|%s",
              vl->plugin, vl->plugin_instance, vl->type, vl->type_instance,
              ds->ds[ds_idx].name);

      return -1;
    }

    if (isfinite(rates[ds_idx])) {
      BUFFER_ADD("[[");
      BUFFER_ADD("%" PRIu64, CDTIME_T_TO_MS(vl->time));
      BUFFER_ADD(",");
      BUFFER_ADD(JSON_GAUGE_FORMAT, rates[ds_idx]);
    } else {
      WARNING("utils_format_kairosdb: invalid rates[ds_idx] for %s|%s|%s|%s|%s",
              vl->plugin, vl->plugin_instance, vl->type, vl->type_instance,
              ds->ds[ds_idx].name);
      sfree(rates);
      return -1;
    }
  } else if (ds->ds[ds_idx].type == DS_TYPE_COUNTER) {
    BUFFER_ADD("[[");
    BUFFER_ADD("%" PRIu64, CDTIME_T_TO_MS(vl->time));
    BUFFER_ADD(",");
    BUFFER_ADD("%" PRIu64, (uint64_t)vl->values[ds_idx].counter);
  } else if (ds->ds[ds_idx].type == DS_TYPE_DERIVE) {
    BUFFER_ADD("[[");
    BUFFER_ADD("%" PRIu64, CDTIME_T_TO_MS(vl->time));
    BUFFER_ADD(",");
    BUFFER_ADD("%" PRIi64, vl->values[ds_idx].derive);
  } else if (ds->ds[ds_idx].type == DS_TYPE_ABSOLUTE) {
    BUFFER_ADD("[[");
    BUFFER_ADD("%" PRIu64, CDTIME_T_TO_MS(vl->time));
    BUFFER_ADD(",");
    BUFFER_ADD("%" PRIu64, vl->values[ds_idx].absolute);
  } else {
    ERROR("format_kairosdb: Unknown data source type: %i", ds->ds[ds_idx].type);
    sfree(rates);
    return -1;
  }
  BUFFER_ADD("]]");

#undef BUFFER_ADD

  DEBUG("format_kairosdb: values_to_kairosdb: buffer = %s;", buffer);
  sfree(rates);
  return 0;
} /* }}} int values_to_kairosdb */

static int value_list_to_kairosdb(char *buffer, size_t buffer_size, /* {{{ */
                                  const data_set_t *ds, const value_list_t *vl,
                                  int store_rates,
                                  char const *const *http_attrs,
                                  size_t http_attrs_num, int data_ttl,
                                  char const *metrics_prefix) {
  char temp[512];
  size_t offset = 0;
  int status;

  memset(buffer, 0, buffer_size);

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    status = snprintf(buffer + offset, buffer_size - offset, __VA_ARGS__);     \
    if (status < 1)                                                            \
      return -1;                                                               \
    else if (((size_t)status) >= (buffer_size - offset))                       \
      return -ENOMEM;                                                          \
    else                                                                       \
      offset += ((size_t)status);                                              \
  } while (0)

#define BUFFER_ADD_KEYVAL(key, value)                                          \
  do {                                                                         \
    status = kairosdb_escape_string(temp, sizeof(temp), (value));              \
    if (status != 0)                                                           \
      return status;                                                           \
    BUFFER_ADD(",\"%s\": %s", (key), temp);                                    \
  } while (0)

  for (size_t i = 0; i < ds->ds_num; i++) {
    /* All value lists have a leading comma. The first one will be replaced with
     * a square bracket in `format_kairosdb_finalize'. */
    BUFFER_ADD(",{\"name\":\"");

    if (metrics_prefix != NULL) {
      BUFFER_ADD("%s.", metrics_prefix);
    }

    BUFFER_ADD("%s", vl->plugin);

    status = values_to_kairosdb(temp, sizeof(temp), ds, vl, store_rates, i);
    if (status != 0)
      return status;

    BUFFER_ADD("\", \"datapoints\": %s", temp);

    /*
     * Now adds meta data to metric as tags
     */

    memset(temp, 0, sizeof(temp));

    if (data_ttl != 0)
      BUFFER_ADD(", \"ttl\": %i", data_ttl);

    BUFFER_ADD(", \"tags\":\{");

    BUFFER_ADD("\"host\": \"%s\"", vl->host);
    for (size_t j = 0; j < http_attrs_num; j += 2) {
      BUFFER_ADD(", \"%s\":", http_attrs[j]);
      BUFFER_ADD(" \"%s\"", http_attrs[j + 1]);
    }

    if (strlen(vl->plugin_instance))
      BUFFER_ADD_KEYVAL("plugin_instance", vl->plugin_instance);
    BUFFER_ADD_KEYVAL("type", vl->type);
    if (strlen(vl->type_instance))
      BUFFER_ADD_KEYVAL("type_instance", vl->type_instance);
    if (ds->ds_num != 1)
      BUFFER_ADD_KEYVAL("ds", ds->ds[i].name);
    BUFFER_ADD("}}");
  } /* for ds->ds_num */

#undef BUFFER_ADD_KEYVAL
#undef BUFFER_ADD

  DEBUG("format_kairosdb: value_list_to_kairosdb: buffer = %s;", buffer);

  return 0;
} /* }}} int value_list_to_kairosdb */

static int format_kairosdb_value_list_nocheck(
    char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free, const data_set_t *ds,
    const value_list_t *vl, int store_rates, size_t temp_size,
    char const *const *http_attrs, size_t http_attrs_num, int data_ttl,
    char const *metrics_prefix) {
  char temp[temp_size];
  int status;

  status = value_list_to_kairosdb(temp, sizeof(temp), ds, vl, store_rates,
                                  http_attrs, http_attrs_num, data_ttl,
                                  metrics_prefix);
  if (status != 0)
    return status;
  temp_size = strlen(temp);

  memcpy(buffer + (*ret_buffer_fill), temp, temp_size + 1);
  (*ret_buffer_fill) += temp_size;
  (*ret_buffer_free) -= temp_size;

  return 0;
} /* }}} int format_kairosdb_value_list_nocheck */

int format_kairosdb_initialize(char *buffer, /* {{{ */
                               size_t *ret_buffer_fill,
                               size_t *ret_buffer_free) {
  size_t buffer_fill;
  size_t buffer_free;

  if ((buffer == NULL) || (ret_buffer_fill == NULL) ||
      (ret_buffer_free == NULL))
    return -EINVAL;

  buffer_fill = *ret_buffer_fill;
  buffer_free = *ret_buffer_free;

  buffer_free = buffer_fill + buffer_free;
  buffer_fill = 0;

  if (buffer_free < 3)
    return -ENOMEM;

  memset(buffer, 0, buffer_free);
  *ret_buffer_fill = buffer_fill;
  *ret_buffer_free = buffer_free;

  return 0;
} /* }}} int format_kairosdb_initialize */

int format_kairosdb_finalize(char *buffer, /* {{{ */
                             size_t *ret_buffer_fill, size_t *ret_buffer_free) {
  size_t pos;

  if ((buffer == NULL) || (ret_buffer_fill == NULL) ||
      (ret_buffer_free == NULL))
    return -EINVAL;

  if (*ret_buffer_free < 2)
    return -ENOMEM;

  /* Replace the leading comma added in `value_list_to_kairosdb' with a square
   * bracket. */
  if (buffer[0] != ',')
    return -EINVAL;
  buffer[0] = '[';

  pos = *ret_buffer_fill;
  buffer[pos] = ']';
  buffer[pos + 1] = 0;

  (*ret_buffer_fill)++;
  (*ret_buffer_free)--;

  return 0;
} /* }}} int format_kairosdb_finalize */

int format_kairosdb_value_list(char *buffer, /* {{{ */
                               size_t *ret_buffer_fill, size_t *ret_buffer_free,
                               const data_set_t *ds, const value_list_t *vl,
                               int store_rates, char const *const *http_attrs,
                               size_t http_attrs_num, int data_ttl,
                               char const *metrics_prefix) {
  if ((buffer == NULL) || (ret_buffer_fill == NULL) ||
      (ret_buffer_free == NULL) || (ds == NULL) || (vl == NULL))
    return -EINVAL;

  if (*ret_buffer_free < 3)
    return -ENOMEM;

  return format_kairosdb_value_list_nocheck(
      buffer, ret_buffer_fill, ret_buffer_free, ds, vl, store_rates,
      (*ret_buffer_free) - 2, http_attrs, http_attrs_num, data_ttl,
      metrics_prefix);
} /* }}} int format_kairosdb_value_list */

/* vim: set sw=2 sts=2 et fdm=marker : */
