/**
 * collectd - src/utils_format_json.c
 * Copyright (C) 2009-2015  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "utils_format_json.h"

#include "common.h"
#include "plugin.h"
#include "utils_cache.h"

#if HAVE_LIBYAJL
#include <yajl/yajl_common.h>
#include <yajl/yajl_gen.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif
#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
#define HAVE_YAJL_V2 1
#endif
#endif

static int json_escape_string(char *buffer, size_t buffer_size, /* {{{ */
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
  BUFFER_ADD('"');
  for (size_t src_pos = 0; string[src_pos] != 0; src_pos++) {
    if ((string[src_pos] == '"') || (string[src_pos] == '\\')) {
      BUFFER_ADD('\\');
      BUFFER_ADD(string[src_pos]);
    } else if (string[src_pos] <= 0x001F)
      BUFFER_ADD('?');
    else
      BUFFER_ADD(string[src_pos]);
  } /* for */
  BUFFER_ADD('"');
  buffer[dst_pos] = 0;

#undef BUFFER_ADD

  return 0;
} /* }}} int json_escape_string */

static int values_to_json(char *buffer, size_t buffer_size, /* {{{ */
                          const data_set_t *ds, const value_list_t *vl,
                          int store_rates) {
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

  BUFFER_ADD("[");
  for (size_t i = 0; i < ds->ds_num; i++) {
    if (i > 0)
      BUFFER_ADD(",");

    if (ds->ds[i].type == DS_TYPE_GAUGE) {
      if (isfinite(vl->values[i].gauge))
        BUFFER_ADD(JSON_GAUGE_FORMAT, vl->values[i].gauge);
      else
        BUFFER_ADD("null");
    } else if (store_rates) {
      if (rates == NULL)
        rates = uc_get_rate(ds, vl);
      if (rates == NULL) {
        WARNING("utils_format_json: uc_get_rate failed.");
        sfree(rates);
        return -1;
      }

      if (isfinite(rates[i]))
        BUFFER_ADD(JSON_GAUGE_FORMAT, rates[i]);
      else
        BUFFER_ADD("null");
    } else if (ds->ds[i].type == DS_TYPE_COUNTER)
      BUFFER_ADD("%" PRIu64, (uint64_t)vl->values[i].counter);
    else if (ds->ds[i].type == DS_TYPE_DERIVE)
      BUFFER_ADD("%" PRIi64, vl->values[i].derive);
    else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
      BUFFER_ADD("%" PRIu64, vl->values[i].absolute);
    else {
      ERROR("format_json: Unknown data source type: %i", ds->ds[i].type);
      sfree(rates);
      return -1;
    }
  } /* for ds->ds_num */
  BUFFER_ADD("]");

#undef BUFFER_ADD

  DEBUG("format_json: values_to_json: buffer = %s;", buffer);
  sfree(rates);
  return 0;
} /* }}} int values_to_json */

static int dstypes_to_json(char *buffer, size_t buffer_size, /* {{{ */
                           const data_set_t *ds) {
  size_t offset = 0;

  memset(buffer, 0, buffer_size);

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    int status;                                                                \
    status = snprintf(buffer + offset, buffer_size - offset, __VA_ARGS__);     \
    if (status < 1)                                                            \
      return -1;                                                               \
    else if (((size_t)status) >= (buffer_size - offset))                       \
      return -ENOMEM;                                                          \
    else                                                                       \
      offset += ((size_t)status);                                              \
  } while (0)

  BUFFER_ADD("[");
  for (size_t i = 0; i < ds->ds_num; i++) {
    if (i > 0)
      BUFFER_ADD(",");

    BUFFER_ADD("\"%s\"", DS_TYPE_TO_STRING(ds->ds[i].type));
  } /* for ds->ds_num */
  BUFFER_ADD("]");

#undef BUFFER_ADD

  DEBUG("format_json: dstypes_to_json: buffer = %s;", buffer);

  return 0;
} /* }}} int dstypes_to_json */

static int dsnames_to_json(char *buffer, size_t buffer_size, /* {{{ */
                           const data_set_t *ds) {
  size_t offset = 0;

  memset(buffer, 0, buffer_size);

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    int status;                                                                \
    status = snprintf(buffer + offset, buffer_size - offset, __VA_ARGS__);     \
    if (status < 1)                                                            \
      return -1;                                                               \
    else if (((size_t)status) >= (buffer_size - offset))                       \
      return -ENOMEM;                                                          \
    else                                                                       \
      offset += ((size_t)status);                                              \
  } while (0)

  BUFFER_ADD("[");
  for (size_t i = 0; i < ds->ds_num; i++) {
    if (i > 0)
      BUFFER_ADD(",");

    BUFFER_ADD("\"%s\"", ds->ds[i].name);
  } /* for ds->ds_num */
  BUFFER_ADD("]");

#undef BUFFER_ADD

  DEBUG("format_json: dsnames_to_json: buffer = %s;", buffer);

  return 0;
} /* }}} int dsnames_to_json */

static int meta_data_keys_to_json(char *buffer, size_t buffer_size, /* {{{ */
                                  meta_data_t *meta, char **keys,
                                  size_t keys_num) {
  size_t offset = 0;
  int status;

  buffer[0] = 0;

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

  for (size_t i = 0; i < keys_num; ++i) {
    int type;
    char *key = keys[i];

    type = meta_data_type(meta, key);
    if (type == MD_TYPE_STRING) {
      char *value = NULL;
      if (meta_data_get_string(meta, key, &value) == 0) {
        char temp[512] = "";

        status = json_escape_string(temp, sizeof(temp), value);
        sfree(value);
        if (status != 0)
          return status;

        BUFFER_ADD(",\"%s\":%s", key, temp);
      }
    } else if (type == MD_TYPE_SIGNED_INT) {
      int64_t value = 0;
      if (meta_data_get_signed_int(meta, key, &value) == 0)
        BUFFER_ADD(",\"%s\":%" PRIi64, key, value);
    } else if (type == MD_TYPE_UNSIGNED_INT) {
      uint64_t value = 0;
      if (meta_data_get_unsigned_int(meta, key, &value) == 0)
        BUFFER_ADD(",\"%s\":%" PRIu64, key, value);
    } else if (type == MD_TYPE_DOUBLE) {
      double value = 0.0;
      if (meta_data_get_double(meta, key, &value) == 0)
        BUFFER_ADD(",\"%s\":%f", key, value);
    } else if (type == MD_TYPE_BOOLEAN) {
      _Bool value = 0;
      if (meta_data_get_boolean(meta, key, &value) == 0)
        BUFFER_ADD(",\"%s\":%s", key, value ? "true" : "false");
    }
  } /* for (keys) */

  if (offset == 0)
    return ENOENT;

  buffer[0] = '{'; /* replace leading ',' */
  BUFFER_ADD("}");

#undef BUFFER_ADD

  return 0;
} /* }}} int meta_data_keys_to_json */

static int meta_data_to_json(char *buffer, size_t buffer_size, /* {{{ */
                             meta_data_t *meta) {
  char **keys = NULL;
  size_t keys_num;
  int status;

  if ((buffer == NULL) || (buffer_size == 0) || (meta == NULL))
    return EINVAL;

  status = meta_data_toc(meta, &keys);
  if (status <= 0)
    return status;
  keys_num = (size_t)status;

  status = meta_data_keys_to_json(buffer, buffer_size, meta, keys, keys_num);

  for (size_t i = 0; i < keys_num; ++i)
    sfree(keys[i]);
  sfree(keys);

  return status;
} /* }}} int meta_data_to_json */

static int value_list_to_json(char *buffer, size_t buffer_size, /* {{{ */
                              const data_set_t *ds, const value_list_t *vl,
                              int store_rates) {
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

  /* All value lists have a leading comma. The first one will be replaced with
   * a square bracket in `format_json_finalize'. */
  BUFFER_ADD(",{");

  status = values_to_json(temp, sizeof(temp), ds, vl, store_rates);
  if (status != 0)
    return status;
  BUFFER_ADD("\"values\":%s", temp);

  status = dstypes_to_json(temp, sizeof(temp), ds);
  if (status != 0)
    return status;
  BUFFER_ADD(",\"dstypes\":%s", temp);

  status = dsnames_to_json(temp, sizeof(temp), ds);
  if (status != 0)
    return status;
  BUFFER_ADD(",\"dsnames\":%s", temp);

  BUFFER_ADD(",\"time\":%.3f", CDTIME_T_TO_DOUBLE(vl->time));
  BUFFER_ADD(",\"interval\":%.3f", CDTIME_T_TO_DOUBLE(vl->interval));

#define BUFFER_ADD_KEYVAL(key, value)                                          \
  do {                                                                         \
    status = json_escape_string(temp, sizeof(temp), (value));                  \
    if (status != 0)                                                           \
      return status;                                                           \
    BUFFER_ADD(",\"%s\":%s", (key), temp);                                     \
  } while (0)

  BUFFER_ADD_KEYVAL("host", vl->host);
  BUFFER_ADD_KEYVAL("plugin", vl->plugin);
  BUFFER_ADD_KEYVAL("plugin_instance", vl->plugin_instance);
  BUFFER_ADD_KEYVAL("type", vl->type);
  BUFFER_ADD_KEYVAL("type_instance", vl->type_instance);

  if (vl->meta != NULL) {
    char meta_buffer[buffer_size];
    memset(meta_buffer, 0, sizeof(meta_buffer));
    status = meta_data_to_json(meta_buffer, sizeof(meta_buffer), vl->meta);
    if (status != 0)
      return status;

    BUFFER_ADD(",\"meta\":%s", meta_buffer);
  } /* if (vl->meta != NULL) */

  BUFFER_ADD("}");

#undef BUFFER_ADD_KEYVAL
#undef BUFFER_ADD

  DEBUG("format_json: value_list_to_json: buffer = %s;", buffer);

  return 0;
} /* }}} int value_list_to_json */

static int format_json_value_list_nocheck(char *buffer, /* {{{ */
                                          size_t *ret_buffer_fill,
                                          size_t *ret_buffer_free,
                                          const data_set_t *ds,
                                          const value_list_t *vl,
                                          int store_rates, size_t temp_size) {
  char temp[temp_size];
  int status;

  status = value_list_to_json(temp, sizeof(temp), ds, vl, store_rates);
  if (status != 0)
    return status;
  temp_size = strlen(temp);

  memcpy(buffer + (*ret_buffer_fill), temp, temp_size + 1);
  (*ret_buffer_fill) += temp_size;
  (*ret_buffer_free) -= temp_size;

  return 0;
} /* }}} int format_json_value_list_nocheck */

int format_json_initialize(char *buffer, /* {{{ */
                           size_t *ret_buffer_fill, size_t *ret_buffer_free) {
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
} /* }}} int format_json_initialize */

int format_json_finalize(char *buffer, /* {{{ */
                         size_t *ret_buffer_fill, size_t *ret_buffer_free) {
  size_t pos;

  if ((buffer == NULL) || (ret_buffer_fill == NULL) ||
      (ret_buffer_free == NULL))
    return -EINVAL;

  if (*ret_buffer_free < 2)
    return -ENOMEM;

  /* Replace the leading comma added in `value_list_to_json' with a square
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
} /* }}} int format_json_finalize */

int format_json_value_list(char *buffer, /* {{{ */
                           size_t *ret_buffer_fill, size_t *ret_buffer_free,
                           const data_set_t *ds, const value_list_t *vl,
                           int store_rates) {
  if ((buffer == NULL) || (ret_buffer_fill == NULL) ||
      (ret_buffer_free == NULL) || (ds == NULL) || (vl == NULL))
    return -EINVAL;

  if (*ret_buffer_free < 3)
    return -ENOMEM;

  return format_json_value_list_nocheck(buffer, ret_buffer_fill,
                                        ret_buffer_free, ds, vl, store_rates,
                                        (*ret_buffer_free) - 2);
} /* }}} int format_json_value_list */

#if HAVE_LIBYAJL
static int json_add_string(yajl_gen g, char const *str) /* {{{ */
{
  if (str == NULL)
    return (int)yajl_gen_null(g);

  return (int)yajl_gen_string(g, (const unsigned char *)str,
                              (unsigned int)strlen(str));
} /* }}} int json_add_string */

#define JSON_ADD(g, str)                                                       \
  do {                                                                         \
    yajl_gen_status status = json_add_string(g, str);                          \
    if (status != yajl_gen_status_ok) {                                        \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define JSON_ADDF(g, format, ...)                                              \
  do {                                                                         \
    char *str = ssnprintf_alloc(format, __VA_ARGS__);                          \
    yajl_gen_status status = json_add_string(g, str);                          \
    free(str);                                                                 \
    if (status != yajl_gen_status_ok) {                                        \
      return -1;                                                               \
    }                                                                          \
  } while (0)

#define CHECK_SUCCESS(cmd)                                                     \
  do {                                                                         \
    yajl_gen_status s = (cmd);                                                 \
    if (s != yajl_gen_status_ok) {                                             \
      return (int)s;                                                           \
    }                                                                          \
  } while (0)

static int format_json_meta(yajl_gen g, notification_meta_t *meta) /* {{{ */
{
  if (meta == NULL)
    return 0;

  JSON_ADD(g, meta->name);
  switch (meta->type) {
  case NM_TYPE_STRING:
    JSON_ADD(g, meta->nm_value.nm_string);
    break;
  case NM_TYPE_SIGNED_INT:
    JSON_ADDF(g, "%" PRIi64, meta->nm_value.nm_signed_int);
    break;
  case NM_TYPE_UNSIGNED_INT:
    JSON_ADDF(g, "%" PRIu64, meta->nm_value.nm_unsigned_int);
    break;
  case NM_TYPE_DOUBLE:
    JSON_ADDF(g, JSON_GAUGE_FORMAT, meta->nm_value.nm_double);
    break;
  case NM_TYPE_BOOLEAN:
    JSON_ADD(g, meta->nm_value.nm_boolean ? "true" : "false");
    break;
  case NM_TYPE_NESTED:
    CHECK_SUCCESS(yajl_gen_map_open(g));
    format_json_meta(g, meta->nm_value.nm_nested);
    CHECK_SUCCESS(yajl_gen_map_close(g));
    break;
  default:
    ERROR("format_json_meta: unknown meta data type %d (name \"%s\")",
          meta->type, meta->name);
    CHECK_SUCCESS(yajl_gen_null(g));
  }

  return format_json_meta(g, meta->next);
} /* }}} int format_json_meta */

static int format_time(yajl_gen g, cdtime_t t) /* {{{ */
{
  char buffer[RFC3339NANO_SIZE] = "";

  if (rfc3339nano(buffer, sizeof(buffer), t) != 0)
    return -1;

  JSON_ADD(g, buffer);
  return 0;
} /* }}} int format_time */

static int format_alert(yajl_gen g, notification_t const *n) /* {{{ */
{
  CHECK_SUCCESS(yajl_gen_array_open(g)); /* BEGIN array */
  CHECK_SUCCESS(yajl_gen_map_open(g));   /* BEGIN alert */

  /*
   * labels
   */
  JSON_ADD(g, "labels");
  CHECK_SUCCESS(yajl_gen_map_open(g)); /* BEGIN labels */

  JSON_ADD(g, "alertname");
  if (strncmp(n->plugin, n->type, strlen(n->plugin)) == 0)
    JSON_ADDF(g, "collectd_%s", n->type);
  else
    JSON_ADDF(g, "collectd_%s_%s", n->plugin, n->type);

  JSON_ADD(g, "instance");
  JSON_ADD(g, n->host);

  /* mangling of plugin instance and type instance into labels is copied from
   * the Prometheus collectd exporter. */
  if (strlen(n->plugin_instance) > 0) {
    JSON_ADD(g, n->plugin);
    JSON_ADD(g, n->plugin_instance);
  }
  if (strlen(n->type_instance) > 0) {
    if (strlen(n->plugin_instance) > 0)
      JSON_ADD(g, "type");
    else
      JSON_ADD(g, n->plugin);
    JSON_ADD(g, n->type_instance);
  }

  JSON_ADD(g, "severity");
  JSON_ADD(g,
           (n->severity == NOTIF_FAILURE)
               ? "FAILURE"
               : (n->severity == NOTIF_WARNING)
                     ? "WARNING"
                     : (n->severity == NOTIF_OKAY) ? "OKAY" : "UNKNOWN");

  JSON_ADD(g, "service");
  JSON_ADD(g, "collectd");

  CHECK_SUCCESS(yajl_gen_map_close(g)); /* END labels */

  /*
   * annotations
   */
  JSON_ADD(g, "annotations");
  CHECK_SUCCESS(yajl_gen_map_open(g)); /* BEGIN annotations */

  JSON_ADD(g, "summary");
  JSON_ADD(g, n->message);

  if (format_json_meta(g, n->meta) != 0) {
    return -1;
  }

  CHECK_SUCCESS(yajl_gen_map_close(g)); /* END annotations */

  JSON_ADD(g, "startsAt");
  if (format_time(g, n->time) != 0) {
    return -1;
  }

  CHECK_SUCCESS(yajl_gen_map_close(g));   /* END alert */
  CHECK_SUCCESS(yajl_gen_array_close(g)); /* END array */

  return 0;
} /* }}} format_alert */

/*
 * Format (prometheus/alertmanager v1):
 *
 * [{
 *   "labels": {
 *     "alertname": "collectd_cpu",
 *     "instance":  "host.example.com",
 *     "severity":  "FAILURE",
 *     "service":   "collectd",
 *     "cpu":       "0",
 *     "type":      "wait"
 *   },
 *   "annotations": {
 *     "summary": "...",
 *     // meta
 *   },
 *   "startsAt": <rfc3339 time>,
 *   "endsAt": <rfc3339 time>, // not used
 * }]
 */
int format_json_notification(char *buffer, size_t buffer_size, /* {{{ */
                             notification_t const *n) {
  yajl_gen g;
  unsigned char const *out;
#if HAVE_YAJL_V2
  size_t unused_out_len;
#else
  unsigned int unused_out_len;
#endif

  if ((buffer == NULL) || (n == NULL))
    return EINVAL;

#if HAVE_YAJL_V2
  g = yajl_gen_alloc(NULL);
  if (g == NULL)
    return -1;
#if COLLECT_DEBUG
  yajl_gen_config(g, yajl_gen_beautify, 1);
  yajl_gen_config(g, yajl_gen_validate_utf8, 1);
#endif

#else /* !HAVE_YAJL_V2 */
  yajl_gen_config conf = {0};
#if COLLECT_DEBUG
  conf.beautify = 1;
  conf.indentString = "  ";
#endif
  g = yajl_gen_alloc(&conf, NULL);
  if (g == NULL)
    return -1;
#endif

  if (format_alert(g, n) != 0) {
    yajl_gen_clear(g);
    yajl_gen_free(g);
    return -1;
  }

  /* copy to output buffer */
  if (yajl_gen_get_buf(g, &out, &unused_out_len) != yajl_gen_status_ok) {
    yajl_gen_clear(g);
    yajl_gen_free(g);
    return -1;
  }
  sstrncpy(buffer, (void *)out, buffer_size);

  yajl_gen_clear(g);
  yajl_gen_free(g);
  return 0;
} /* }}} format_json_notification */
#else
int format_json_notification(char *buffer, size_t buffer_size, /* {{{ */
                             notification_t const *n) {
  ERROR("format_json_notification: Not available (requires libyajl).");
  return ENOTSUP;
} /* }}} int format_json_notification */
#endif
