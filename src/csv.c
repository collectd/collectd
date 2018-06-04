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

#include "common.h"
#include "plugin.h"
#include "utils_cache.h"

/*
 * Private variables
 */
static const char *config_keys[] = {"DataDir", "StoreRates"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static char *datadir;
static int store_rates;
static int use_stdio;

static int value_list_to_string(char *buffer, int buffer_len,
                                const data_set_t *ds, const value_list_t *vl) {
  int offset;
  int status;
  gauge_t *rates = NULL;

  assert(0 == strcmp(ds->type, vl->type));

  memset(buffer, '\0', buffer_len);

  status = snprintf(buffer, buffer_len, "%.3f", CDTIME_T_TO_DOUBLE(vl->time));
  if ((status < 1) || (status >= buffer_len))
    return -1;
  offset = status;

  for (size_t i = 0; i < ds->ds_num; i++) {
    if ((ds->ds[i].type != DS_TYPE_COUNTER) &&
        (ds->ds[i].type != DS_TYPE_GAUGE) &&
        (ds->ds[i].type != DS_TYPE_DERIVE) &&
        (ds->ds[i].type != DS_TYPE_ABSOLUTE)) {
      sfree(rates);
      return -1;
    }

    if (ds->ds[i].type == DS_TYPE_GAUGE) {
      status = snprintf(buffer + offset, buffer_len - offset, ",%lf",
                        vl->values[i].gauge);
    } else if (store_rates != 0) {
      if (rates == NULL)
        rates = uc_get_rate(ds, vl);
      if (rates == NULL) {
        WARNING("csv plugin: "
                "uc_get_rate failed.");
        return -1;
      }
      status = snprintf(buffer + offset, buffer_len - offset, ",%lf", rates[i]);
    } else if (ds->ds[i].type == DS_TYPE_COUNTER) {
      status = snprintf(buffer + offset, buffer_len - offset, ",%" PRIu64,
                        (uint64_t)vl->values[i].counter);
    } else if (ds->ds[i].type == DS_TYPE_DERIVE) {
      status = snprintf(buffer + offset, buffer_len - offset, ",%" PRIi64,
                        vl->values[i].derive);
    } else if (ds->ds[i].type == DS_TYPE_ABSOLUTE) {
      status = snprintf(buffer + offset, buffer_len - offset, ",%" PRIu64,
                        vl->values[i].absolute);
    }

    if ((status < 1) || (status >= (buffer_len - offset))) {
      sfree(rates);
      return -1;
    }

    offset += status;
  } /* for ds->ds_num */

  sfree(rates);
  return 0;
} /* int value_list_to_string */

static int value_list_to_filename(char *buffer, size_t buffer_size,
                                  value_list_t const *vl) {
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

  status = FORMAT_VL(ptr, ptr_size, vl);
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
} /* int value_list_to_filename */

static int csv_create_file(const char *filename, const data_set_t *ds) {
  FILE *csv;

  if (check_create_dir(filename))
    return -1;

  csv = fopen(filename, "w");
  if (csv == NULL) {
    ERROR("csv plugin: fopen (%s) failed: %s", filename, STRERRNO);
    return -1;
  }

  fprintf(csv, "epoch");
  for (size_t i = 0; i < ds->ds_num; i++)
    fprintf(csv, ",%s", ds->ds[i].name);

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

static int csv_write(const data_set_t *ds, const value_list_t *vl,
                     user_data_t __attribute__((unused)) * user_data) {
  struct stat statbuf;
  char filename[512];
  char values[4096];
  FILE *csv;
  int csv_fd;
  struct flock fl = {0};
  int status;

  if (0 != strcmp(ds->type, vl->type)) {
    ERROR("csv plugin: DS type does not match value list type");
    return -1;
  }

  status = value_list_to_filename(filename, sizeof(filename), vl);
  if (status != 0)
    return -1;

  DEBUG("csv plugin: csv_write: filename = %s;", filename);

  if (value_list_to_string(values, sizeof(values), ds, vl) != 0)
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
            filename, CDTIME_T_TO_DOUBLE(vl->interval), values);
    return 0;
  }

  if (stat(filename, &statbuf) == -1) {
    if (errno == ENOENT) {
      if (csv_create_file(filename, ds))
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

  return 0;
} /* int csv_write */

void module_register(void) {
  plugin_register_config("csv", csv_config, config_keys, config_keys_num);
  plugin_register_write("csv", csv_write, /* user_data = */ NULL);
} /* void module_register */
