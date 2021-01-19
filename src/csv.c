/**
 * collectd - src/csv.c
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2009       Doug MacEachern
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
 *   Doug MacEachern <dougm@hyperic.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils_cache.h"

/*
 * Private variables
 */
static const char *config_keys[] = {"DataDir", "StoreRates"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static char *datadir;
static int store_rates;
static int use_stdio;

static int metric_to_string(char *buffer, int buffer_len, metric_t const *m) {
  int offset;
  int status;

  if (buffer == NULL || m == NULL) {
    return -1;
  }

  memset(buffer, '\0', buffer_len);

  status = snprintf(buffer, buffer_len, "%.3f", CDTIME_T_TO_DOUBLE(m->time));

  if ((status < 1) || (status >= buffer_len))
    return -1;

  offset = status;

  if ((m->family->type != METRIC_TYPE_COUNTER) &&
      (m->family->type != METRIC_TYPE_DISTRIBUTION) &&
      (m->family->type != METRIC_TYPE_GAUGE) &&
      (m->family->type != METRIC_TYPE_UNTYPED)) {
    return -1;
  }

  if (m->family->type == METRIC_TYPE_GAUGE ||
      m->family->type == METRIC_TYPE_UNTYPED) {
    status =
        snprintf(buffer + offset, buffer_len - offset, ",%lf", m->value.gauge);
  } else if (store_rates != 0 || m->family->type == METRIC_TYPE_DISTRIBUTION) {
    gauge_t rate;
    status = uc_get_rate(m, &rate);
    if (status != 0) {
      WARNING("csv plugin: uc_get_rate failed.");
      return -1;
    }
    status = snprintf(buffer + offset, buffer_len - offset, ",%lf", rate);
  } else if (m->family->type == METRIC_TYPE_COUNTER) {
    status = snprintf(buffer + offset, buffer_len - offset, ",%" PRIu64,
                      m->value.counter);
  }

  if ((status < 1) || (status >= (buffer_len - offset))) {
    return -1;
  }

  return 0;
} /* int metric_to_string */

static int metric_to_filename(char *buffer, size_t buffer_size,
                              metric_t const *m) {
  int status;
  char *ptr = buffer;
  size_t ptr_size = buffer_size;
  time_t now;
  struct tm struct_tm;

  if (datadir != NULL) {
    size_t len = strlen(datadir) + 1;

    if (len >= ptr_size)
      return ENOBUFS;

    memcpy(ptr, datadir, len);
    ptr[len - 1] = '/';
    ptr_size -= len;
    ptr += len;
  }

  char *plugin = NULL;
  char *plugin_instance = NULL;
  char *type = NULL;
  char *type_instance = NULL;

  for (size_t j = 0; j < m->label.num; ++j) {
    label_pair_t *l = m->label.ptr + j;

    if (plugin == NULL) {
      plugin = strdup(l->name);
      plugin_instance = strdup(l->value);
    } else if (type == NULL) {
      type = strdup(l->name);
      type_instance = strdup(l->value);
    }
  }

  status = format_name(ptr, ptr_size, NULL, plugin, plugin_instance, type,
                       type_instance);

  sfree(plugin);
  sfree(plugin_instance);
  sfree(type);
  sfree(type_instance);

  if (status != 0)
    return status;

  /* Skip all the time formatting stuff when printing to STDOUT or
   * STDERR. */
  if (use_stdio)
    return 0;

  ptr_size -= strlen(ptr);
  ptr += strlen(ptr);

  /* "-2013-07-12" => 11 bytes */
  if (ptr_size < 12) {
    ERROR("csv plugin: Buffer too small.");
    return ENOMEM;
  }

  /* TODO: Find a way to minimize the calls to `localtime_r',
   * since they are pretty expensive.. */
  now = time(NULL);
  if (localtime_r(&now, &struct_tm) == NULL) {
    ERROR("csv plugin: localtime_r failed");
    return -1;
  }

  status = strftime(ptr, ptr_size, "-%Y-%m-%d", &struct_tm);
  if (status == 0) /* yep, it returns zero on error. */
  {
    ERROR("csv plugin: strftime failed");
    return -1;
  }

  return 0;
} /* int metric_to_filename */

static int csv_create_file(const char *filename, metric_family_t const *fam) {
  FILE *csv;

  if (check_create_dir(filename))
    return -1;

  csv = fopen(filename, "w");
  if (csv == NULL) {
    ERROR("csv plugin: fopen (%s) failed: %s", filename, STRERRNO);
    return -1;
  }

  fprintf(csv, "epoch");
  fprintf(csv, ",%s", fam->name);

  fprintf(csv, "\n");
  fclose(csv);

  return 0;
} /* int csv_create_file */

static int csv_config(const char *key, const char *value) {
  if (strcasecmp("DataDir", key) == 0) {
    if (datadir != NULL) {
      free(datadir);
      datadir = NULL;
    }
    if (strcasecmp("stdout", value) == 0) {
      use_stdio = 1;
      return 0;
    } else if (strcasecmp("stderr", value) == 0) {
      use_stdio = 2;
      return 0;
    }
    datadir = strdup(value);
    if (datadir != NULL) {
      size_t len = strlen(datadir);
      while ((len > 0) && (datadir[len - 1] == '/')) {
        len--;
        datadir[len] = '\0';
      }
      if (len == 0) {
        free(datadir);
        datadir = NULL;
      }
    }
  } else if (strcasecmp("StoreRates", key) == 0) {
    if (IS_TRUE(value))
      store_rates = 1;
    else
      store_rates = 0;
  } else {
    return -1;
  }
  return 0;
} /* int csv_config */

static int csv_write(metric_family_t const *fam,
                     user_data_t __attribute__((unused)) * user_data) {
  struct stat statbuf;
  char filename[512];
  char values[4096];
  FILE *csv;
  int csv_fd;
  struct flock fl = {0};
  int status;

  if (fam == NULL)
    return -1;

  for (size_t i = 0; i < fam->metric.num; ++i) {
    metric_t const *m = fam->metric.ptr + i;
    status = metric_to_filename(filename, sizeof(filename), m);
    if (status != 0)
      return -1;

    DEBUG("csv plugin: csv_write: filename = %s;", filename);

    if (metric_to_string(values, sizeof(values), m) != 0)
      return -1;

    if (use_stdio) {
      escape_string(filename, sizeof(filename));

      /* Replace commas by colons for PUTVAL compatible output. */
      for (size_t i = 0; i < sizeof(values); i++) {
        if (values[i] == 0)
          break;
        else if (values[i] == ',')
          values[i] = ':';
      }

      fprintf(use_stdio == 1 ? stdout : stderr, "PUTVAL %s interval=%.3f %s\n",
              filename, CDTIME_T_TO_DOUBLE(m->interval), values);
      return 0;
    }

    if (stat(filename, &statbuf) == -1) {
      if (errno == ENOENT) {
        if (csv_create_file(filename, fam))
          return -1;
      } else {
        ERROR("stat(%s) failed: %s", filename, STRERRNO);
        return -1;
      }
    } else if (!S_ISREG(statbuf.st_mode)) {
      ERROR("stat(%s): Not a regular file!", filename);
      return -1;
    }

    csv = fopen(filename, "a");
    if (csv == NULL) {
      ERROR("csv plugin: fopen (%s) failed: %s", filename, STRERRNO);
      return -1;
    }
    csv_fd = fileno(csv);

    fl.l_pid = getpid();
    fl.l_type = F_WRLCK;
    fl.l_whence = SEEK_SET;

    status = fcntl(csv_fd, F_SETLK, &fl);
    if (status != 0) {
      ERROR("csv plugin: flock (%s) failed: %s", filename, STRERRNO);
      fclose(csv);
      return -1;
    }

    fprintf(csv, "%s\n", values);

    /* The lock is implicitely released. I we don't release it explicitely
     * because the `FILE *' may need to flush a cache first */
    fclose(csv);
  }
  return 0;
} /* int csv_write */

void module_register(void) {
  plugin_register_config("csv", csv_config, config_keys, config_keys_num);
  plugin_register_write("csv", csv_write, /* user_data = */ NULL);
} /* void module_register */
