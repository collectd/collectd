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
 *   Manoj Srivastava <srivasta at google.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"

#include "utils/format_kairosdb/format_kairosdb.h"
#include "utils_cache.h"

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
      buffer[buffer_size - 1] = '\0';                                          \
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

int format_kairosdb_metric(char *buffer_p, size_t *ret_buffer_fill, /* {{{ */
                           size_t *ret_buffer_free, const metric_t *metric_p,
                           int store_rates, char const *const *http_attrs,
                           size_t http_attrs_num, int data_ttl,
                           char const *metrics_prefix) {
  char temp[1024];
  gauge_t rate = -1;
  int status = 0;

  if ((buffer_p == NULL) || (ret_buffer_fill == NULL) ||
      (ret_buffer_free == NULL) || (metric_p == NULL) ||
      (metric_p->identity == NULL) || (metric_p->ds == NULL))
    return -EINVAL;

  if (*ret_buffer_free < 3)
    return -ENOMEM;

  /* Respect high water marks and fere size */
  char *buffer = buffer_p + (*ret_buffer_fill);
  size_t buffer_size = *ret_buffer_free;
  size_t offset = 0;

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
    BUFFER_ADD(",\"%s\":%s", (key), temp);                                     \
  } while (0)

  /* Designed to be called multiple times, as when adding metrics from a
     metrics_lisat_t object. When finalizing, the initial leading comma
     will be replaced by a [ */
  BUFFER_ADD(",{");
  BUFFER_ADD("\"name\":\"");
  if (metrics_prefix != NULL) {
    BUFFER_ADD("%s.", metrics_prefix);
  }
  BUFFER_ADD("%s\",", metric_p->identity->name);

  BUFFER_ADD("\"datapoints\":");
  if (metric_p->value_type == DS_TYPE_GAUGE) {
    if (isfinite(metric_p->value.gauge))
      BUFFER_ADD(JSON_GAUGE_FORMAT, metric_p->value.gauge);
    else
      BUFFER_ADD("null");
  } else if (store_rates) {
    if (rate == -1)
      status = uc_get_rate(metric_p, &rate);
    if (status != 0) {
      WARNING("utils_format_json: uc_get_rate failed.");
      buffer_p[*ret_buffer_fill] = '0';
      return -1;
    }

    if (isfinite(rate))
      BUFFER_ADD(JSON_GAUGE_FORMAT, rate);
    else
      BUFFER_ADD("null");
  } else if (metric_p->value_type == DS_TYPE_COUNTER)
    BUFFER_ADD("%" PRIu64, (uint64_t)metric_p->value.counter);
  else if (metric_p->value_type == DS_TYPE_DERIVE)
    BUFFER_ADD("%" PRIi64, metric_p->value.derive);
  else if (metric_p->value_type == DS_TYPE_ABSOLUTE)
    BUFFER_ADD("%" PRIu64, metric_p->value.absolute);
  else {
    ERROR("format_json: Unknown data source type: %i", metric_p->value_type);
    buffer_p[*ret_buffer_fill] = '0';
    return -1;
  }
  /*
   * Now add meta data to metric as tags
   */
  if (data_ttl != 0)
    BUFFER_ADD(", \"ttl\": %i", data_ttl);

  BUFFER_ADD(", \"tags\":{");

  BUFFER_ADD(",\"time\":%.3f", CDTIME_T_TO_DOUBLE(metric_p->time));
  BUFFER_ADD(",\"interval\":%.3f", CDTIME_T_TO_DOUBLE(metric_p->interval));
  BUFFER_ADD_KEYVAL("plugin", metric_p->plugin);
  BUFFER_ADD_KEYVAL("type", metric_p->type);
  BUFFER_ADD_KEYVAL("dsname", metric_p->ds->name);
  BUFFER_ADD_KEYVAL("dstype", DS_TYPE_TO_STRING(metric_p->value_type));
  for (size_t j = 0; j < http_attrs_num; j += 2) {
    BUFFER_ADD(", \"%s\":", http_attrs[j]);
    BUFFER_ADD(" \"%s\"", http_attrs[j + 1]);
  }

  if (metric_p->identity->root_p != NULL) {
    c_avl_iterator_t *iter_p = c_avl_get_iterator(metric_p->identity->root_p);
    if (iter_p != NULL) {
      char *key_p = NULL;
      char *value_p = NULL;
      while ((c_avl_iterator_next(iter_p, (void **)&key_p,
                                  (void **)&value_p)) == 0) {
        if ((key_p != NULL) && (value_p != NULL)) {
          BUFFER_ADD_KEYVAL(key_p, value_p);
        }
      }
      c_avl_iterator_destroy(iter_p);
    }
  }

  BUFFER_ADD("}}");

#undef BUFFER_ADD_KEYVAL
#undef BUFFER_ADD

  /* Update hihg water mark and free size */
  (*ret_buffer_fill) += offset;
  (*ret_buffer_free) -= offset;

  return 0;
} /* }}} */
