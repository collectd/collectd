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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "utils_format_atsd.h"

size_t strlcat(char *dst, const char *src, size_t siz) {
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

int format_value(char *ret, size_t ret_len, int ds_index, const data_set_t *ds,
                 const value_list_t *vl, const gauge_t *rates) {
  int status;
  size_t offset = 0;
  assert(0 == strcmp(ds->type, vl->type));

#define BUFFER_ADD(...)                                                        \
  do {                                                                         \
    status = snprintf(ret + offset, ret_len - offset, __VA_ARGS__);            \
    if (status < 1) {                                                          \
      return (-1);                                                             \
    } else if (((size_t)status) >= (ret_len - offset)) {                       \
      return (-1);                                                             \
    } else                                                                     \
      offset += ((size_t)status);                                              \
  } while (0)

  if (ds->ds[ds_index].type == DS_TYPE_GAUGE) {
    BUFFER_ADD(GAUGE_FORMAT, vl->values[ds_index].gauge);
  } else if (rates != NULL) {
    BUFFER_ADD("%f", rates[ds_index]);
  } else if (ds->ds[ds_index].type == DS_TYPE_COUNTER)
    BUFFER_ADD("%llu", vl->values[ds_index].counter);
  else if (ds->ds[ds_index].type == DS_TYPE_DERIVE)
    BUFFER_ADD("%" PRIi64, vl->values[ds_index].derive);
  else if (ds->ds[ds_index].type == DS_TYPE_ABSOLUTE)
    BUFFER_ADD("%" PRIu64, vl->values[ds_index].absolute);
  else {
    ERROR("gr_format_values plugin: Unknown data source type: %i",
          ds->ds[ds_index].type);
    return (-1);
  }
#undef BUFFER_ADD
  return (0);
}

static int starts_with(const char *pre, const char *str) {
  size_t lenpre = strlen(pre), lenstr = strlen(str);
  return lenstr < lenpre ? 0 : strncmp(pre, str, lenpre) == 0;
}

void check_entity(char *ret, const int ret_len, const char *entity,
                  const char *host_name, _Bool short_hostname) {

  char *host;
  char *c;
  char tmp[100];

  if ((strcasecmp("localhost", host_name) == 0 ||
       starts_with(host_name, "localhost."))) {
    gethostname(tmp, sizeof(tmp));
    host = strdup(tmp);
  } else {
    host = strdup(host_name);
  }
  if (short_hostname) {
    for (c = host; *c; c++) {
      if (*c == '.' && c != host) {
        *c = '\0';
        break;
      }
    }
  }

  if ((entity != NULL) && (strlen(entity) != 0) &&
      (strchr(entity, ' ') == NULL)) {
    sstrncpy(ret, entity, ret_len);
  } else {
    sstrncpy(ret, host, ret_len);
  }
  sfree(host);
}