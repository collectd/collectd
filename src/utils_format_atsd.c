/**
 * collectd - src/utils_format_atsd.c
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
 **/

#include "common.h"
#include "plugin.h"
#include "collectd.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils_format_atsd.h"

typedef struct tag_key_val_s {
  char *key;
  char *val;
  struct tag_key_val_s *next;
} tag_key_val_t;

typedef struct series_s {
  char entity[6 * DATA_MAX_NAME_LEN];
  char metric[6 * DATA_MAX_NAME_LEN];
  char formatted_value[MAX_VALUE_LEN];
  tag_key_val_t *tags;
  uint64_t time;
} series_t;

typedef struct value_index_t {
  size_t index;
  value_list_t *vl;
  data_set_t *ds;
} value_index_s;

static int add_tag(tag_key_val_t **tags, const char *key, const char *val) {
  tag_key_val_t *tag = malloc(sizeof(tag_key_val_t));
  if (tag == NULL) {
    ERROR("atsd_write: out of memory");
    return -1;
  }

  tag->key = strdup(key);
  tag->val = strdup(val);
  tag->next = *tags;
  *tags = tag;

  return 0;
}

static void remove_tag(tag_key_val_t **tags) {
  tag_key_val_t *tag;
  if (*tags != NULL) {
    tag = *tags;
    *tags = tag->next;
    sfree(tag->key);
    sfree(tag->val);
    sfree(tag);
  }
}

static size_t strlcat(char *dst, const char *src, size_t siz) {
  char *d = dst;
  const char *s = src;
  size_t n = siz;
  size_t dlen;

  /* Find the end of dst and adjust bytes left but don't go past end */
  while (n-- != 0 && *d != '\0')
    d++;
  dlen = d - dst;
  n = siz - dlen;

  if (n == 0)
    return (dlen + strlen(s));
  while (*s != '\0') {
    if (n != 1) {
      *d++ = *s;
      n--;
    }
    s++;
  }
  *d = '\0';

  return (dlen + (s - src)); /* count does not include NUL */
}

char *escape_atsd_string(char *dst_buf, const char *src_buf, size_t n) {
  char tmp_buf[6 * DATA_MAX_NAME_LEN];
  const char *s;
  char *t;
  size_t k;

  k = 0;
  s = src_buf;
  t = tmp_buf;

  if (n > sizeof(tmp_buf))
    n = sizeof(tmp_buf);
  while (k < n && *s) {
    if (*s == '"') {
      *t = '"';
      t++;
    }
    *t = *s;
    t++;
    s++;
    k++;
  }
  *t = '\0';

  strncpy(dst_buf, tmp_buf, n);
  return dst_buf;
}

double get_value(format_info_t *format) {
  if (format->ds->ds[format->index].type == DS_TYPE_GAUGE) {
    return format->vl->values[format->index].gauge;
  } else {
    return format->rates[format->index];
  }
}

int format_value(char *ret, size_t ret_len, format_info_t *format) {
  size_t offset = 0;
  int status;

  assert(0 == strcmp(format->ds->type, format->vl->type));

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

  if (format->ds->ds[format->index].type == DS_TYPE_GAUGE) {
    BUFFER_ADD(GAUGE_FORMAT, format->vl->values[format->index].gauge);
  } else {
    BUFFER_ADD("%.15g", format->rates[format->index]);
  }

#undef BUFFER_ADD

  return 0;
}

static int starts_with(const char *pre, const char *str) {
  size_t lenpre = strlen(pre), lenstr = strlen(str);
  return lenstr < lenpre ? 0 : strncmp(pre, str, lenpre) == 0;
}

int format_entity(char *ret, const int ret_len, const char *entity,
                  const char *host_name, _Bool short_hostname) {
  char *host, *c;
  char buf[HOST_NAME_MAX];
  const char *e;
  _Bool use_entity = true;

  if (entity != NULL) {
    for (e = entity; *e; e++) {
      if (*e == ' ') {
        use_entity = false;
        break;
      }
    }
  } else {
    use_entity = false;
  }

  if (use_entity) {
    sstrncpy(ret, entity, ret_len);
  } else {
    if (strcasecmp("localhost", host_name) == 0 ||
        starts_with(host_name, "localhost.")) {
      gethostname(buf, sizeof buf);
      host = strdup(buf);
    } else {
      host = strdup(host_name);
    }

    if (short_hostname) {
      c = strchr(host + 1, '.');
      if (c != NULL && *c == '.')
        *c = '\0';
    }

    sstrncpy(ret, host, ret_len);
    sfree(host);
  }

  return 0;
}

static void metric_name_append(char *metric_name, const char *str, size_t n) {
  if (*str != '\0') {
    if (*metric_name != '\0')
      strlcat(metric_name, ".", n);
    strlcat(metric_name, str, n);
  }
}

static int format_metric_default(series_t *series, format_info_t *format) {
  int ret;

  memset(series->metric, 0, sizeof(series->metric));
  metric_name_append(series->metric, format->prefix, sizeof(series->metric));
  metric_name_append(series->metric, format->vl->plugin, sizeof(series->metric));
  metric_name_append(series->metric, format->vl->type, sizeof(series->metric));
  metric_name_append(series->metric, format->vl->type_instance, sizeof(series->metric));
  if (strcasecmp(format->ds->ds[format->index].name, "value") != 0) {
    metric_name_append(series->metric, format->ds->ds[format->index].name,
                       sizeof(series->metric));
  }

  if (*format->vl->plugin != '\0') {
    ret = add_tag(&series->tags, "plugin", format->vl->plugin);
  }
  if (*format->vl->plugin_instance != '\0') {
    ret = add_tag(&series->tags, "plugin_instance", format->vl->plugin_instance);
    ret = add_tag(&series->tags, "instance", format->vl->plugin_instance);
  }
  if (*format->vl->type != '\0') {
    ret = add_tag(&series->tags, "type", format->vl->type);
  }
  if (*format->vl->type_instance != '\0') {
    ret = add_tag(&series->tags, "type_instance", format->vl->type_instance);
  }
  ret = add_tag(&series->tags, "data_source", format->ds->ds[format->index].name);

  return ret;
}

void init_series(series_t *series, const char *entity, const value_list_t *vl) {
  series->tags = NULL;
  series->time = CDTIME_T_TO_MS(vl->time);
  strncpy(series->entity, entity, sizeof(series->entity));
}

size_t derive_series(series_t *series_buffer, format_info_t *format) {
  char tmp_value[MAX_VALUE_LEN];
  char *key, *value, *strtok_ctx, *tmp;
  size_t count = 0;

  init_series(series_buffer, format->entity, format->vl);
  format_metric_default(series_buffer, format);
  format_value(series_buffer->formatted_value,
               sizeof(series_buffer->formatted_value), format);

  count++;
  series_buffer++;

  if (strcasecmp(format->vl->plugin, "cpu") == 0 &&
      strcasecmp(format->vl->type_instance, "idle") == 0) {
    init_series(series_buffer, format->entity, format->vl);
    snprintf(series_buffer->metric, sizeof(series_buffer->metric),
             "%s.cpu.%s.busy", format->prefix, format->vl->type);
    if (*format->vl->plugin_instance != '\0')
      add_tag(&series_buffer->tags, "instance", format->vl->plugin_instance);

    format_value(tmp_value, sizeof(tmp_value), format);
    snprintf(series_buffer->formatted_value,
             sizeof(series_buffer->formatted_value), "%g",
             (100.0 - atof(tmp_value)));

    count++;
    series_buffer++;
  } else if (strcasecmp(format->vl->plugin, "df") == 0 &&
             strcasecmp(format->vl->type, "percent_bytes") == 0 &&
             strcasecmp(format->vl->type_instance, "free") == 0) {
    init_series(series_buffer, format->entity, format->vl);
    snprintf(series_buffer->metric, sizeof(series_buffer->metric),
             "%s.df.percent_bytes.used_reserved", format->prefix);
    if (*format->vl->plugin_instance != '\0')
      add_tag(&series_buffer->tags, "instance", format->vl->plugin_instance);

    format_value(tmp_value, sizeof(tmp_value), format);
    snprintf(series_buffer->formatted_value,
             sizeof(series_buffer->formatted_value), "%g",
             (100.0 - atof(tmp_value)));

    count++;
    series_buffer++;
  } else if (strcasecmp(format->vl->plugin, "exec") == 0) {
    init_series(series_buffer, format->entity, format->vl);
    snprintf(series_buffer->metric, sizeof(series_buffer->metric), "%s.%s",
             format->prefix, format->vl->plugin_instance);
    format_value(series_buffer->formatted_value,
                 sizeof(series_buffer->formatted_value), format);

    if (strchr(format->vl->type_instance, ';')) {
      tmp = strdup(format->vl->type_instance);
      while ((key = strtok_r(tmp, ";", &strtok_ctx)) != NULL) {
        value = strchr(key, '=');
        if (value) {
          *value++ = '\0';
          add_tag(&series_buffer->tags, key, value);
        }
      }
      free(tmp);
    } else {
      add_tag(&series_buffer->tags, "instance", format->vl->type_instance);
    }
  }

  return count;
}

size_t format_command(char *buffer, size_t buffer_len, series_t *series) {
  char escape_buffer[6 * DATA_MAX_NAME_LEN];
  size_t written;

  memset(buffer, 0, buffer_len);
  written = 0;

  written += snprintf(buffer, buffer_len, "series");

  escape_atsd_string(escape_buffer, series->entity, sizeof escape_buffer);
  written += snprintf(buffer + written, buffer_len - written, " e:\"%s\"",
                      escape_buffer);

  escape_atsd_string(escape_buffer, series->metric, sizeof escape_buffer);
  written += snprintf(buffer + written, buffer_len - written, " m:\"%s\"=%s",
                      escape_buffer, series->formatted_value);

  for (; series->tags; remove_tag(&series->tags)) {
    escape_atsd_string(escape_buffer, series->tags->key, sizeof escape_buffer);
    written += snprintf(buffer + written, buffer_len - written, " t:\"%s\"=",
                        escape_buffer);

    escape_atsd_string(escape_buffer, series->tags->val, sizeof escape_buffer);
    written += snprintf(buffer + written, buffer_len - written, "\"%s\"",
                        escape_buffer);
  }

  written += snprintf(buffer + written, buffer_len - written, " ms:%" PRIu64,
                      series->time);
  written += snprintf(buffer + written, buffer_len - written, " \n");

  return written;
}

int format_atsd_command(format_info_t *format) {
  size_t series_count, i, written;
  series_t series_buffer[MAX_DERIVED_SERIES];

  series_count =
      derive_series(series_buffer, format);

  memset(format->buffer, 0, format->buffer_len);
  written = 0;
  for (i = 0; i < series_count; i++) {
    written += format_command(format->buffer + written, format->buffer_len - written,
                              &series_buffer[i]);
  }

  return 0;
}
