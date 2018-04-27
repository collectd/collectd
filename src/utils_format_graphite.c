/**
 * collectd - src/utils_format_graphite.c
 * Copyright (C) 2012  Thomas Meson
 * Copyright (C) 2012  Florian octo Forster
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
 *   Thomas Meson <zllak at hycik.org>
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include "utils_cache.h"
#include "utils_format_graphite.h"

#define GRAPHITE_FORBIDDEN " \t\"\\:!/()\n\r"

/* Utils functions to format data sets in graphite format.
 * Largely taken from write_graphite.c as it remains the same formatting */

static int gr_format_values(char *ret, size_t ret_len, int ds_num,
                            const data_set_t *ds, const value_list_t *vl,
                            gauge_t const *rates) {
  size_t offset = 0;
  int status;

  assert(0 == strcmp(ds->type, vl->type));

  memset(ret, 0, ret_len);

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    status = snprintf(ret + offset, ret_len - offset, __VA_ARGS__);            \
    if (status < 1) {                                                          \
      return -1;                                                               \
    } else if (((size_t)status) >= (ret_len - offset)) {                       \
      return -1;                                                               \
    } else                                                                     \
      offset += ((size_t)status);                                              \
  } while (0)

  if (ds->ds[ds_num].type == DS_TYPE_GAUGE)
    BUFFER_ADD(GAUGE_FORMAT, vl->values[ds_num].gauge);
  else if (rates != NULL)
    BUFFER_ADD("%f", rates[ds_num]);
  else if (ds->ds[ds_num].type == DS_TYPE_COUNTER)
    BUFFER_ADD("%" PRIu64, (uint64_t)vl->values[ds_num].counter);
  else if (ds->ds[ds_num].type == DS_TYPE_DERIVE)
    BUFFER_ADD("%" PRIi64, vl->values[ds_num].derive);
  else if (ds->ds[ds_num].type == DS_TYPE_ABSOLUTE)
    BUFFER_ADD("%" PRIu64, vl->values[ds_num].absolute);
  else {
    ERROR("gr_format_values plugin: Unknown data source type: %i",
          ds->ds[ds_num].type);
    return -1;
  }

#undef BUFFER_ADD

  return 0;
}

static void gr_copy_escape_part(char *dst, const char *src, size_t dst_len,
                                char escape_char, _Bool preserve_separator) {
  memset(dst, 0, dst_len);

  if (src == NULL)
    return;

  for (size_t i = 0; i < dst_len; i++) {
    if (src[i] == 0) {
      dst[i] = 0;
      break;
    }

    if ((!preserve_separator && (src[i] == '.')) || isspace((int)src[i]) ||
        iscntrl((int)src[i]))
      dst[i] = escape_char;
    else
      dst[i] = src[i];
  }
}

static int gr_format_name(char *ret, int ret_len, value_list_t const *vl,
                          char const *ds_name, char const *prefix,
                          char const *postfix, char const escape_char,
                          unsigned int flags) {
  char n_host[DATA_MAX_NAME_LEN];
  char n_plugin[DATA_MAX_NAME_LEN];
  char n_plugin_instance[DATA_MAX_NAME_LEN];
  char n_type[DATA_MAX_NAME_LEN];
  char n_type_instance[DATA_MAX_NAME_LEN];

  char tmp_plugin[2 * DATA_MAX_NAME_LEN + 1];
  char tmp_type[2 * DATA_MAX_NAME_LEN + 1];

  if (prefix == NULL)
    prefix = "";

  if (postfix == NULL)
    postfix = "";

  _Bool preserve_separator = (flags & GRAPHITE_PRESERVE_SEPARATOR) ? 1 : 0;

  gr_copy_escape_part(n_host, vl->host, sizeof(n_host), escape_char,
                      preserve_separator);
  gr_copy_escape_part(n_plugin, vl->plugin, sizeof(n_plugin), escape_char,
                      preserve_separator);
  gr_copy_escape_part(n_plugin_instance, vl->plugin_instance,
                      sizeof(n_plugin_instance), escape_char,
                      preserve_separator);
  gr_copy_escape_part(n_type, vl->type, sizeof(n_type), escape_char,
                      preserve_separator);
  gr_copy_escape_part(n_type_instance, vl->type_instance,
                      sizeof(n_type_instance), escape_char, preserve_separator);

  if (n_plugin_instance[0] != '\0')
    snprintf(tmp_plugin, sizeof(tmp_plugin), "%s%c%s", n_plugin,
             (flags & GRAPHITE_SEPARATE_INSTANCES) ? '.' : '-',
             n_plugin_instance);
  else
    sstrncpy(tmp_plugin, n_plugin, sizeof(tmp_plugin));

  if (n_type_instance[0] != '\0') {
    if ((flags & GRAPHITE_DROP_DUPE_FIELDS) && strcmp(n_plugin, n_type) == 0)
      sstrncpy(tmp_type, n_type_instance, sizeof(tmp_type));
    else
      snprintf(tmp_type, sizeof(tmp_type), "%s%c%s", n_type,
               (flags & GRAPHITE_SEPARATE_INSTANCES) ? '.' : '-',
               n_type_instance);
  } else
    sstrncpy(tmp_type, n_type, sizeof(tmp_type));

  /* Assert always_append_ds -> ds_name */
  assert(!(flags & GRAPHITE_ALWAYS_APPEND_DS) || (ds_name != NULL));
  if (ds_name != NULL) {
    if ((flags & GRAPHITE_DROP_DUPE_FIELDS) &&
        strcmp(tmp_plugin, tmp_type) == 0)
      snprintf(ret, ret_len, "%s%s%s.%s.%s", prefix, n_host, postfix,
               tmp_plugin, ds_name);
    else
      snprintf(ret, ret_len, "%s%s%s.%s.%s.%s", prefix, n_host, postfix,
               tmp_plugin, tmp_type, ds_name);
  } else
    snprintf(ret, ret_len, "%s%s%s.%s.%s", prefix, n_host, postfix, tmp_plugin,
             tmp_type);

  return 0;
}

static void escape_graphite_string(char *buffer, char escape_char) {
  assert(strchr(GRAPHITE_FORBIDDEN, escape_char) == NULL);

  for (char *head = buffer + strcspn(buffer, GRAPHITE_FORBIDDEN); *head != '\0';
       head += strcspn(head, GRAPHITE_FORBIDDEN))
    *head = escape_char;
}

int format_graphite(char *buffer, size_t buffer_size, data_set_t const *ds,
                    value_list_t const *vl, char const *prefix,
                    char const *postfix, char const escape_char,
                    unsigned int flags) {
  int status = 0;
  int buffer_pos = 0;

  gauge_t *rates = NULL;
  if (flags & GRAPHITE_STORE_RATES) {
    rates = uc_get_rate(ds, vl);
    if (rates == NULL) {
      ERROR("format_graphite: error with uc_get_rate");
      return -1;
    }
  }

  for (size_t i = 0; i < ds->ds_num; i++) {
    char const *ds_name = NULL;
    char key[10 * DATA_MAX_NAME_LEN];
    char values[512];
    size_t message_len;
    char message[1024];

    if ((flags & GRAPHITE_ALWAYS_APPEND_DS) || (ds->ds_num > 1))
      ds_name = ds->ds[i].name;

    /* Copy the identifier to `key' and escape it. */
    status = gr_format_name(key, sizeof(key), vl, ds_name, prefix, postfix,
                            escape_char, flags);
    if (status != 0) {
      ERROR("format_graphite: error with gr_format_name");
      sfree(rates);
      return status;
    }

    escape_graphite_string(key, escape_char);
    /* Convert the values to an ASCII representation and put that into
     * `values'. */
    status = gr_format_values(values, sizeof(values), i, ds, vl, rates);
    if (status != 0) {
      ERROR("format_graphite: error with gr_format_values");
      sfree(rates);
      return status;
    }

    /* Compute the graphite command */
    message_len =
        (size_t)snprintf(message, sizeof(message), "%s %s %u\r\n", key, values,
                         (unsigned int)CDTIME_T_TO_TIME_T(vl->time));
    if (message_len >= sizeof(message)) {
      ERROR("format_graphite: message buffer too small: "
            "Need %" PRIsz " bytes.",
            message_len + 1);
      sfree(rates);
      return -ENOMEM;
    }

    /* Append it in case we got multiple data set */
    if ((buffer_pos + message_len) >= buffer_size) {
      ERROR("format_graphite: target buffer too small");
      sfree(rates);
      return -ENOMEM;
    }
    memcpy((void *)(buffer + buffer_pos), message, message_len);
    buffer_pos += message_len;
    buffer[buffer_pos] = '\0';
  }
  sfree(rates);
  return status;
} /* int format_graphite */
