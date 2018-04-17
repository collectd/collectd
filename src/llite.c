/**
 * collectd - src/llite.c
 * Copyright (C) 2015 Battelle Memorial Institute
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
 *   Gary Skouson <gary.skouson at pnnl.gov>
 **/

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"
#include "collectd.h"

#define LLITEDIR "/proc/fs/lustre/llite"

static const char *config_keys[] = {"Filesystem", "IgnoreSelected"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *llite_fs = NULL;

static int llite_config(const char *key, const char *value) {
  plugin_log(LOG_INFO, "llite: Configuring with %s : %s", key, value);
  if (llite_fs == NULL)
    llite_fs = ignorelist_create(0);
  if (llite_fs == NULL)
    return (1);

  if (strcasecmp("Filesystem", key) == 0) {
    ignorelist_add(llite_fs, value);
  } else if (strcasecmp("Ignoreselected", key) == 0) {
    int invert = 1;
    if (IS_TRUE(value))
      invert = 0;
    ignorelist_set_invert(llite_fs, invert);
  } else {
    return (-1);
  }
  return (0);
}

static void submit(const char *instance, const char *type,
                   const char *type_inst, const unsigned long long val) {
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  values[0].derive = val;

  vl.values = values;
  vl.values_len = 1;

  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "llite", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);
}

static int llite_process_fs(const char *fs, const char *name) {
  FILE *stats;
  char buffer[1024];
  char fs_name[1024];
  char bw[] = "total_bytes";
  char ops[] = "operations";
  snprintf(fs_name, sizeof(fs_name), "%s/%s/stats", LLITEDIR, fs);
  stats = fopen(fs_name, "r");
  if (stats == NULL) {
    ERROR("llite plugin: Can't open Lustre stats '%s'", fs_name);
    return (-1);
  }
  while (fgets(buffer, sizeof(buffer), stats) != NULL) {
    char *fields[10];
    int fields_num;
    char *key;
    char *endptr = NULL;
    unsigned long long value;

    fields_num = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));

    if (strcmp("snapshot_time", fields[0]) == 0) {
      continue;
    } else if (strcmp("read_bytes", fields[0]) == 0) {
      key = "read_nr";
      value = strtoll(fields[1], &endptr, 10);
      submit(name, ops, key, value);
      key = "read_bytes";
      value = strtoll(fields[6], &endptr, 10);
      submit(name, bw, key, value);
    } else if (strcmp("write_bytes", fields[0]) == 0) {
      key = "write_nr";
      value = strtoll(fields[1], &endptr, 10);
      submit(name, ops, key, value);
      key = "write_bytes";
      value = strtoll(fields[6], &endptr, 10);
      submit(name, bw, key, value);
    } else if (strcmp("osc_read", fields[0]) == 0) {
      key = "osc_read_nr";
      value = strtoll(fields[1], &endptr, 10);
      submit(name, ops, key, value);
      key = "osc_read";
      value = strtoll(fields[6], &endptr, 10);
      submit(name, bw, key, value);
    } else if (strcmp("osc_write", fields[0]) == 0) {
      key = "osc_write_nr";
      value = strtoll(fields[1], &endptr, 10);
      submit(name, ops, key, value);
      key = "osc_write";
      value = strtoll(fields[6], &endptr, 10);
      submit(name, bw, key, value);
    } else if (fields_num > 1) {
      value = strtoll(fields[1], &endptr, 10);
      submit(name, ops, fields[0], value);
    }
  }
  fclose(stats);
  return (0);
}

static int llite_read(void) {
  int status;
  DIR *dir;
  struct dirent *ent;
  char testDir[FILENAME_MAX];
  char *chkdir;
  char *saveptr;

  if ((dir = opendir(LLITEDIR)) == NULL) {
    ERROR("llite plugin: Can't open %s.", LLITEDIR);
    return (-1);
  }
  while ((ent = readdir(dir)) != NULL) {
    if ((strcmp(".", ent->d_name) == 0) || (strcmp("..", ent->d_name) == 0))
      continue;
    if (strncpy(testDir, ent->d_name, FILENAME_MAX) &&
        (chkdir = strtok_r(testDir, "-", &saveptr))) {
      if (ignorelist_match(llite_fs, chkdir))
        continue;
      if ((status = llite_process_fs(ent->d_name, chkdir)) != 0)
        return (status);
    }
  }
  closedir(dir);
  return (0);
}

void module_register(void) {
  plugin_register_config("llite", llite_config, config_keys, config_keys_num);
  plugin_register_read("llite", llite_read);
}
