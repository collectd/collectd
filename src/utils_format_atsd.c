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

int format_value(char *ret, size_t ret_len, size_t index, const data_set_t *ds,
                 const value_list_t *vl, gauge_t *rates) {
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

  if (ds->ds[index].type == DS_TYPE_GAUGE) {
    BUFFER_ADD(GAUGE_FORMAT, vl->values[index].gauge);
  } else {
    BUFFER_ADD("%f", rates[index]);
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

static int metric_cpu_format(char *metric_name, size_t metric_n, tag_key_val_t **tags,
                             const char *prefix, size_t index, const data_set_t *ds, const value_list_t *vl) {
  int ret;

  metric_name[0] = '\0';
  metric_name_append(metric_name, prefix, metric_n);
  metric_name_append(metric_name, "cpu", metric_n);
  if (strcmp(vl->type_instance, "idle") == 0) {
    metric_name_append(metric_name, "busy", metric_n);
  } else {
    metric_name_append(metric_name, vl->type_instance, metric_n);
  }

  if (*vl->plugin_instance != '\0')
      ret = add_tag(tags, "instance", vl->plugin_instance);

  return ret;
}

static int metric_inrerface_format(char *metric_name, size_t metric_n, tag_key_val_t **tags,
                                   const char *prefix, size_t index, const data_set_t *ds, const value_list_t *vl) {
  int ret;

  metric_name[0] = '\0';
  metric_name_append(metric_name, prefix, metric_n);
  metric_name_append(metric_name, "interface", metric_n);
  metric_name_append(metric_name, vl->type, metric_n);

  if (strcasecmp(ds->ds[index].name, "rx") == 0) {
    metric_name_append(metric_name, "received", metric_n);
  } else if (strcasecmp(ds->ds[index].name, "tx") == 0) {
    metric_name_append(metric_name, "sent", metric_n);
  }

  return ret;
}

static int metric_df_format(char *metric_name, size_t metric_n, tag_key_val_t **tags,
                            const char *prefix, size_t index, const data_set_t *ds, const value_list_t *vl) {
  char path_buffer[1024], *c;

  metric_name[0] = '\0';
  metric_name_append(metric_name, prefix, metric_n);
  metric_name_append(metric_name, "df", metric_n);

  if (strcasecmp(vl->type, "df_inodes") == 0) {
    metric_name_append(metric_name, "inodes", metric_n);
    metric_name_append(metric_name, vl->type_instance, metric_n);
  } else if (strcasecmp(vl->type, "df_complex") == 0) {
    metric_name_append(metric_name, "space", metric_n);
    metric_name_append(metric_name, vl->type_instance, metric_n);
  } else if (strcasecmp(vl->type, "percent_bytes") == 0) {
    metric_name_append(metric_name, "space", metric_n);
    if (strcasecmp(vl->type_instance, "free") == 0) {
      metric_name_append(metric_name, "used-reserved", metric_n);
    } else {
      metric_name_append(metric_name, vl->type_instance, metric_n);
    }
    metric_name_append(metric_name, "percent", metric_n);
  } else if (strcasecmp(vl->type, "percent_inodes") == 0) {
    metric_name_append(metric_name, "inodes", metric_n);
    metric_name_append(metric_name, vl->type_instance, metric_n);
    metric_name_append(metric_name, "percent", metric_n);
  }

  path_buffer[0] = '/';
  path_buffer[1] = '\0';
  if (strcasecmp(vl->plugin_instance, "root") != 0) {
    strlcat(path_buffer, vl->plugin_instance, sizeof path_buffer);
    for (c = path_buffer; *c; c++)
      if (*c == '-')
        *c = '/';
  }

  return add_tag(tags, "instance", path_buffer);
}

static int metric_load_format(char *metric_name, size_t metric_n, tag_key_val_t **tags,
                              const char *prefix, size_t index, const data_set_t *ds, const value_list_t *vl) {
  int ret;

  metric_name[0] = '\0';
  metric_name_append(metric_name, "load", metric_n);
  metric_name_append(metric_name, "loadavg", metric_n);

  if (strcasecmp(ds->ds[index].name, "shortterm") == 0) {
    metric_name_append(metric_name, "1m", metric_n);
  } else if (strcasecmp(ds->ds[index].name, "midterm") == 0) {
    metric_name_append(metric_name, "5m", metric_n);
  } else if (strcasecmp(ds->ds[index].name, "longterm") == 0) {
    metric_name_append(metric_name, "15m", metric_n);
  }

  return ret;
}

static int metric_aggregation_format(char *metric_name, size_t metric_n, tag_key_val_t **tags,
                                     const char *prefix, size_t index, const data_set_t *ds, const value_list_t *vl) {
  char tmp_buffer[2 * DATA_MAX_NAME_LEN], *s;
  int ret;

  tmp_buffer[0] = '\0';
  strlcat(tmp_buffer, vl->type, sizeof tmp_buffer);
  strlcat(tmp_buffer, "-", sizeof tmp_buffer);

  s = strstr(vl->plugin_instance, tmp_buffer);

  metric_name[0] = '\0';
  metric_name_append(metric_name, "load", metric_n);
  metric_name_append(metric_name, "loadavg", metric_n);

  return ret;
}

static int metric_default_format(char *metric_name, size_t metric_n, tag_key_val_t **tags,
                                 const char *prefix, size_t index, const data_set_t *ds, const value_list_t *vl) {
  int ret;

  metric_name[0] = '\0';
  metric_name_append(metric_name, prefix, metric_n);
  metric_name_append(metric_name, vl->plugin, metric_n);
  metric_name_append(metric_name, vl->type, metric_n);
  metric_name_append(metric_name, vl->type_instance, metric_n);
  if (strcasecmp(ds->ds[index].name, "value") != 0)
    metric_name_append(metric_name, ds->ds[index].name, metric_n);

  if (*vl->plugin_instance != '\0')
    ret = add_tag(tags, "instance", vl->plugin_instance);

  return ret;
}

int format_atsd_command(char *buffer, size_t buffer_len, const char *entity,
                        const char *prefix, size_t index, const data_set_t *ds,
                        const value_list_t *vl, gauge_t *rates) {
  int status;
  char metric_name[6 * DATA_MAX_NAME_LEN];
  char escape_buffer[6 * DATA_MAX_NAME_LEN];
  char value_str[128];
  tag_key_val_t *tags;
  size_t written;

  status = format_value(value_str, sizeof(value_str), index, ds, vl, rates);
  if (status != 0)
    return status;

  tags = NULL;
  metric_default_format(metric_name, sizeof metric_name, &tags, prefix, index, ds, vl);

  memset(buffer, 0, buffer_len);

  escape_atsd_string(escape_buffer, entity, sizeof escape_buffer);
  escape_atsd_string(metric_name, metric_name, sizeof metric_name);

  written = 0;
  written += snprintf(buffer, buffer_len, "series e:\"%s\" ms:%" PRIu64 " m:\"%s\"=%s",
                      escape_buffer, CDTIME_T_TO_MS(vl->time), metric_name, value_str);

  for (; tags; remove_tag(&tags)) {
    escape_atsd_string(escape_buffer, tags->key, sizeof escape_buffer);
    written += snprintf(buffer + written, buffer_len - written,
                        " t:\"%s\"=", escape_buffer);

    escape_atsd_string(escape_buffer, tags->val, sizeof escape_buffer);
    written += snprintf(buffer + written, buffer_len - written,
                        "\"%s\"", escape_buffer);
  }

  if (buffer_len > written)
    snprintf(buffer + written, buffer_len - written, " \n");

  return 0;
}
