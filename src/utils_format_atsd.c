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

/* strlcat based on OpenBSDs strlcat */
/*----------------------------------------------------------*/
/*
 * Appends src to string dst of size siz (unlike strncat, siz is the
 * full size of dst, not space left).  At most siz-1 characters
 * will be copied.  Always NUL terminates (unless siz <= strlen(dst)).
 * Returns strlen(src) + MIN(siz, strlen(initial dst)).
 * If retval >= siz, truncation occurred.
 */
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
      for (c = host + 1; *c; c++) {
        if (*c == '.') {
          *c = '\0';
          break;
        }
      }
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

int format_atsd_command(char *buffer, size_t buffer_len, const char *entity,
                        const char *prefix, size_t index, const data_set_t *ds,
                        const value_list_t *vl, gauge_t *rates) {
  int status;
  char metric_name[6 * DATA_MAX_NAME_LEN];
  char value_str[128];
  size_t written;

  status = format_value(value_str, sizeof(value_str), index, ds, vl, rates);
  if (status != 0)
    return status;

  metric_name[0] = '\0';
  metric_name_append(metric_name, prefix, sizeof metric_name);
  metric_name_append(metric_name, vl->plugin, sizeof metric_name);
  metric_name_append(metric_name, vl->type, sizeof metric_name);
  metric_name_append(metric_name, vl->type_instance, sizeof metric_name);
  if (strcasecmp(ds->ds[index].name, "value") != 0)
    metric_name_append(metric_name, ds->ds[index].name, sizeof metric_name);

  memset(buffer, 0, buffer_len);

  written = 0;
  written += snprintf(buffer, buffer_len, "series e:%s ms:%" PRIu64 " m:%s=%s",
                      entity, CDTIME_T_TO_MS(vl->time), metric_name, value_str);
  if (*vl->plugin_instance != 0 && buffer_len > written)
    written += snprintf(buffer + written, buffer_len - written,
                        " t:instance=%s", vl->plugin_instance);
  if (*vl->plugin_instance != 0 && buffer_len > written)
    snprintf(buffer + written, buffer_len - written, " \n");

  return 0;
}
