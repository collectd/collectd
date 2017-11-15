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

#define PART_END -1
#define PART_STR 0
#define PART_VL_PLUGIN 1
#define PART_VL_PLUGIN_INSTANCE 2
#define PART_VL_TYPE 3
#define PART_VL_TYPE_INSTANCE 4
#define PART_IS_RAW 5
#define PART_DS_NAME 6

/* Max number of part in name pattern */
#define MAX_NAME_PARTS 6

typedef struct {
    int part_type;
    char *str_value;
} name_part_t;

typedef struct {
    name_part_t name_parts[MAX_NAME_PARTS];
} name_rule_t;

typedef void (*transform_func_t)(char *value);

#define STRING(__str_val) {.part_type = PART_STR, .str_value = __str_val}
#define PLUGIN {.part_type = PART_VL_PLUGIN}
#define PLUGIN_INSTANCE {.part_type = PART_VL_PLUGIN_INSTANCE}
#define TYPE {.part_type = PART_VL_TYPE}
#define TYPE_INSTANCE {.part_type = PART_VL_TYPE_INSTANCE}
#define DATA_SOURCE {.part_type = PART_DS_NAME}
#define IS_RAW {.part_type = PART_IS_RAW}
#define END {.part_type = PART_END}

#define NAME_PATTERN(...) {.name_parts = {__VA_ARGS__, END}}
#define NAME_PATTERN_PTR(...) &(name_rule_t) NAME_PATTERN (__VA_ARGS__)

typedef struct tag_key_val_s {
  char *key;
  char *val;
  struct tag_key_val_s *next;
} tag_key_val_t;

typedef struct series_s {
  char entity[6 * DATA_MAX_NAME_LEN];
  char metric[6 * DATA_MAX_NAME_LEN];
  char formatted_value[MAX_VALUE_LEN];
  tag_key_val_t *metric_tags;
  tag_key_val_t *series_tags;
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

  /* Find the end of dst and adjust bytes left but dobreak;n't go past end */
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
  } else if (format->rates != NULL) {
    return format->rates[format->index];
  } else if (format->ds->ds[format->index].type == DS_TYPE_COUNTER) {
    return format->vl->values[format->index].counter;
  } else if (format->ds->ds[format->index].type == DS_TYPE_DERIVE) {
    return format->vl->values[format->index].derive;
  } else if (format->ds->ds[format->index].type == DS_TYPE_ABSOLUTE) {
    return format->vl->values[format->index].absolute;
  }
  return -1;
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
  } else if (format->rates != NULL) {
    BUFFER_ADD("%.15g", format->rates[format->index]);
  } else if (format->ds->ds[format->index].type == DS_TYPE_COUNTER) {
    BUFFER_ADD("%llu", format->vl->values[format->index].counter);
  } else if (format->ds->ds[format->index].type == DS_TYPE_DERIVE) {
    BUFFER_ADD("%" PRIi64, format->vl->values[format->index].derive);
  } else if (format->ds->ds[format->index].type == DS_TYPE_ABSOLUTE) {
    BUFFER_ADD("%" PRIu64, format->vl->values[format->index].absolute);
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
        break;\

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

static void format_metric_name(char *buffer, size_t len, format_info_t *format, name_rule_t *rule) {
  name_part_t name_part;
  size_t i;

  memset(buffer, 0, len);
  metric_name_append(buffer, format->prefix, len);
  for (i = 0; rule->name_parts[i].part_type != PART_END; i++) {
    name_part = rule->name_parts[i];
    switch (name_part.part_type) {
      case PART_STR:
        metric_name_append(buffer, name_part.str_value, len);
        break;
      case PART_VL_PLUGIN:
        metric_name_append(buffer, format->vl->plugin, len);
        break;
      case PART_VL_TYPE:
        metric_name_append(buffer, format->vl->type, len);
        break;
      case PART_VL_TYPE_INSTANCE:
        metric_name_append(buffer, format->vl->type_instance, len);
        break;
      case PART_IS_RAW:
        if (format->ds->ds[format->index].type != DS_TYPE_GAUGE && format->rates == NULL) {
          metric_name_append(buffer, "raw", len);
        }
        break;
      case PART_DS_NAME:
        if (strcasecmp(format->ds->ds[format->index].name, "value") != 0) {
          metric_name_append(buffer, format->ds->ds[format->index].name, len);
        }
        break;
      default:
        ERROR("utils_format_atsd: unknown metric format part type");
    }
  }
}

static void invert_percent(char *value) {
  char tmp[MAX_VALUE_LEN];
  snprintf(tmp, sizeof(tmp), "%.15g", (100.0 - atof(value)));
  strncpy(value, tmp, MAX_VALUE_LEN);
}

static int format_series(series_t *series, format_info_t *format,
                         name_rule_t *name_rule, _Bool add_instance_tag,
                         transform_func_t transform) {
  int ret;

  series->metric_tags = NULL;
  series->series_tags = NULL;
  series->time = CDTIME_T_TO_MS(format->vl->time);
  strncpy(series->entity, format->entity, sizeof(series->entity));

  format_metric_name(series->metric, sizeof(series->metric), format, name_rule);
  format_value(series->formatted_value, sizeof(series->formatted_value), format);

  if (transform != NULL) {
    transform(series->formatted_value);
  }

  if (*format->vl->plugin != '\0') {
    ret = add_tag(&series->series_tags, "plugin", format->vl->plugin);
  }
  if (*format->vl->plugin_instance != '\0') {
    ret = add_tag(&series->series_tags, "plugin_instance", format->vl->plugin_instance);
    if (add_instance_tag) {
      ret = add_tag(&series->series_tags, "instance", format->vl->plugin_instance);
    }
  }
  if (*format->vl->type != '\0') {
    ret = add_tag(&series->series_tags, "type", format->vl->type);
  }
  if (*format->vl->type_instance != '\0') {
    ret = add_tag(&series->series_tags, "type_instance", format->vl->type_instance);
  }
  ret = add_tag(&series->series_tags, "data_source", format->ds->ds[format->index].name);

  add_tag(&series->metric_tags, "data_type",
      DS_TYPE_TO_STRING(format->ds->ds[format->index].type));

  return ret;
}

size_t derive_series(series_t *series_buffer, format_info_t *format) {
  char *key, *value, *strtok_ctx, *tmp;
  _Bool preserve_original = true;
  size_t count = 0;

  if (format->rates != NULL &&
      strcasecmp(format->vl->plugin, "cpu") == 0 &&
      strcasecmp(format->vl->type_instance, "idle") == 0) {
    format_series(series_buffer, format,
                  &(name_rule_t) NAME_PATTERN(PLUGIN, TYPE, STRING("busy")),
                  true, invert_percent);
    count++;
    series_buffer++;
  } else if (strcasecmp(format->vl->plugin, "df") == 0 &&
             strcasecmp(format->vl->type, "percent_bytes") == 0 &&
             strcasecmp(format->vl->type_instance, "free") == 0) {
    format_series(series_buffer, format,
                  NAME_PATTERN_PTR(PLUGIN, TYPE, STRING("used_reserved")),
                  true, invert_percent);
    count++;
    series_buffer++;
  } else if (strcasecmp(format->vl->plugin, "exec") == 0) {
    format_series(series_buffer, format,
                  NAME_PATTERN_PTR(PLUGIN_INSTANCE, IS_RAW),
                  false, NULL);

    if (strchr(format->vl->type_instance, ';')) {
      tmp = strdup(format->vl->type_instance);
      while ((key = strtok_r(tmp, ";", &strtok_ctx)) != NULL) {
        value = strchr(key, '=');
        if (value) {
          *value++ = '\0';
          add_tag(&series_buffer->series_tags, key, value);
        }
      }
      free(tmp);
    } else {
      add_tag(&series_buffer->series_tags, "instance", format->vl->type_instance);
    }

    preserve_original = false;
  }

  if (preserve_original) {
    format_series(series_buffer, format,
                  NAME_PATTERN_PTR(PLUGIN, TYPE, TYPE_INSTANCE, DATA_SOURCE, IS_RAW),
                  true, NULL);
    count++;
    series_buffer++;
  }

  return count;
}

size_t format_tags(char *buffer, size_t buffer_len, tag_key_val_t **tags) {
  char escape_buffer[6 * DATA_MAX_NAME_LEN];
  size_t written;

  written = 0;

  for (; *tags; remove_tag(tags)) {
    escape_atsd_string(escape_buffer, (*tags)->key, sizeof escape_buffer);
    written += snprintf(buffer + written, buffer_len - written, " t:\"%s\"=",
                        escape_buffer);

    escape_atsd_string(escape_buffer, (*tags)->val, sizeof escape_buffer);
    written += snprintf(buffer + written, buffer_len - written, "\"%s\"",
                        escape_buffer);
  }

  return written;
}

size_t format_series_command(char *buffer, size_t buffer_len, series_t *series) {
  char escape_buffer[6 * DATA_MAX_NAME_LEN];
  size_t written;

  written = 0;

  written += snprintf(buffer, buffer_len, "series");
  escape_atsd_string(escape_buffer, series->entity, sizeof escape_buffer);
  written += snprintf(buffer + written, buffer_len - written, " e:\"%s\"",
                      escape_buffer);
  escape_atsd_string(escape_buffer, series->metric, sizeof escape_buffer);
  written += snprintf(buffer + written, buffer_len - written, " m:\"%s\"=%s",
                      escape_buffer, series->formatted_value);
  written += format_tags(buffer + written, buffer_len - written, &series->series_tags);
  written += snprintf(buffer + written, buffer_len - written, " ms:%" PRIu64,
                      series->time);
  written += snprintf(buffer + written, buffer_len - written, " \n");

  return written;
}

size_t format_metric_command(char *buffer, size_t buffer_len, series_t *series) {
  char escape_buffer[6 * DATA_MAX_NAME_LEN];
  size_t written;

  written = 0;

  written += snprintf(buffer, buffer_len, "metric");
  escape_atsd_string(escape_buffer, series->metric, sizeof escape_buffer);
  written += snprintf(buffer + written, buffer_len - written, " m:\"%s\"",
                      escape_buffer);
  written += format_tags(buffer + written, buffer_len - written, &series->metric_tags);
  written += snprintf(buffer + written, buffer_len - written, " \n");

  return written;
}

int format_atsd_command(format_info_t *format, _Bool append_metrics) {
  size_t series_count, i, written;
  series_t series_buffer[MAX_DERIVED_SERIES];

  series_count = derive_series(series_buffer, format);

  memset(format->buffer, 0, format->buffer_len);
  written = 0;
  for (i = 0; i < series_count; i++) {
    if (append_metrics) {
      written += format_metric_command(format->buffer + written, format->buffer_len - written,
                                       &series_buffer[i]);
    }

    written += format_series_command(format->buffer + written, format->buffer_len - written,
                              &series_buffer[i]);
  }

  return 0;
}
