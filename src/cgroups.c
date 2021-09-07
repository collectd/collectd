/**
 * collectd - src/cgroups.c
 * Copyright (C) 2011  Michael Stapelberg
 * Copyright (C) 2013  Florian Forster
 * Copyright (C) 2015  Thomas Weißschuh, Amadeus Germany GmbH
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the license is applicable.
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
 *   Michael Stapelberg <michael at stapelberg.de>
 *   Florian Forster <octo at collectd.org>
 *   Thomas Weißschuh <thomas.weissschuh at de.amadeus.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"
#include "utils/mount/mount.h"

struct controller_settings {
  bool found;
  char *controller;
  dirwalk_callback_f callback;
};

static char const *config_keys[] = {"CGroup", "IgnoreSelected"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *il_cgroup;

__attribute__((nonnull(1, 2, 3))) static void
cgroups_submit_one(char const *type, char const *plugin_instance,
                   char const *type_instance, value_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &value;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "cgroups", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void cgroups_submit_one */

__attribute__((nonnull(1, 2, 3, 4))) static int
read_cgroups_table(const char *type, const char *dirname,
                   const char *cgroup_name, const char *table_name, int ds_type,
                   void *user_data) {
  char abs_path[PATH_MAX];
  struct stat statbuf;
  char buf[1024];
  int status;

  FILE *fh;

  if (ignorelist_match(il_cgroup, cgroup_name))
    return 0;

  snprintf(abs_path, sizeof(abs_path), "%s/%s", dirname, cgroup_name);

  status = lstat(abs_path, &statbuf);
  if (status != 0) {
    ERROR("cgroups plugin: stat (\"%s\") failed.", abs_path);
    return -1;
  }

  /* We are only interested in directories, so skip everything else. */
  if (!S_ISDIR(statbuf.st_mode))
    return 0;

  snprintf(abs_path, sizeof(abs_path), "%s/%s/%s", dirname, cgroup_name,
           table_name);
  fh = fopen(abs_path, "r");
  if (fh == NULL) {
    ERROR("cgroups plugin: fopen (\"%s\") failed: %s", abs_path, STRERRNO);
    return -1;
  }

  while (fgets(buf, sizeof(buf), fh) != NULL) {
    char *fields[8];
    int numfields = 0;
    char *key;
    size_t key_len;
    value_t value;

    /* Expected format:
     *
     *   user: 12345
     *   system: 23456
     *
     * Or:
     *
     *   user 12345
     *   system 23456
     */
    strstripnewline(buf);
    numfields = strsplit(buf, fields, STATIC_ARRAY_SIZE(fields));
    if (numfields != 2)
      continue;

    key = fields[0];
    key_len = strlen(key);
    if (key_len < 2)
      continue;

    /* Strip colon off the first column, if found */
    if (key[key_len - 1] == ':')
      key[key_len - 1] = '\0';

    status = parse_value(fields[1], &value, ds_type);
    if (status != 0)
      continue;

    cgroups_submit_one(type, cgroup_name, key, value);
  }

  fclose(fh);
  return 0;
} /* int read_cgroups_table */

/*
 * This callback reads the user/system CPU time for each cgroup.
 */
__attribute__((nonnull(1, 2))) static int
read_cpuacct_procs(const char *dirname, char const *cgroup_name,
                   void *user_data) {
  return read_cgroups_table("cpu", dirname, cgroup_name, "cpuacct.stat",
                            DS_TYPE_DERIVE, user_data);
} /* int read_cpuacct_procs */

__attribute__((nonnull(1, 2))) static int
read_memory_procs(const char *dirname, char const *cgroup_name,
                  void *user_data) {
  return read_cgroups_table("memory", dirname, cgroup_name, "memory.stat",
                            DS_TYPE_GAUGE, user_data);
} /* int read_memory_procs */

/*
 * This callback reads the memory statistics for each cgroup.
 */
__attribute__((nonnull(1, 2, 3))) static int
read_cgroups_root(const char *dirname, const char *filename,
                  dirwalk_callback_f callback, void *user_data) {
  char abs_path[PATH_MAX];
  struct stat statbuf;
  int status;

  snprintf(abs_path, sizeof(abs_path), "%s/%s", dirname, filename);

  status = lstat(abs_path, &statbuf);
  if (status != 0) {
    ERROR("cgroups plugin: stat (%s) failed.", abs_path);
    return -1;
  }

  if (S_ISDIR(statbuf.st_mode)) {
    status = walk_directory(abs_path, callback,
                            /* user_data = */ NULL,
                            /* include_hidden = */ 0);
    return status;
  }

  return 0;
} /* int read_cgroups_root */

/*
 * Gets called for every file/folder in /sys/fs/cgroup/cpu,cpuacct (or
 * wherever cpuacct is mounted on the system). Calls walk_directory with the
 * read_cpuacct_procs callback on every folder it finds, such as "system".
 */
__attribute__((nonnull(1, 2))) static int
read_cpuacct_root(const char *dirname, const char *filename, void *user_data) {
  return read_cgroups_root(dirname, filename, read_cpuacct_procs, user_data);
} /* int read_cpuacct_root */

/*
 * Gets called for every file/folder in /sys/fs/cgroup/memory (or
 * wherever memory is mounted on the system). Calls walk_directory with the
 * read_memory_procs callback on every folder it finds, such as "system".
 */
__attribute__((nonnull(1, 2))) static int
read_memory_root(const char *dirname, const char *filename, void *user_data) {
  return read_cgroups_root(dirname, filename, read_memory_procs, user_data);
} /* int read_memory_root */

static int cgroups_init(void) {
  if (il_cgroup == NULL)
    il_cgroup = ignorelist_create(1);

  return 0;
} /* int cgroups_init */

static int cgroups_shutdown(void) {
  ignorelist_free(il_cgroup);

  return 0;
} /* int cgroups_shutdown */

static int cgroups_config(const char *key, const char *value) {
  cgroups_init();

  if (strcasecmp(key, "CGroup") == 0) {
    if (ignorelist_add(il_cgroup, value))
      return 1;
    return 0;
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    if (IS_TRUE(value))
      ignorelist_set_invert(il_cgroup, 0);
    else
      ignorelist_set_invert(il_cgroup, 1);
    return 0;
  }

  return -1;
} /* int cgroups_config */

static void cgroups_walk_mountpoint(cu_mount_t *mnt_ptr,
                                    struct controller_settings *settings,
                                    bool check_mountoption) {
  if (1 == settings->found)
    return;

  if (check_mountoption &&
      !cu_mount_checkoption(mnt_ptr->options, settings->controller, 1))
    return;

  walk_directory(mnt_ptr->dir, settings->callback,
                 /* user_data = */ NULL,
                 /* include_hidden = */ 0);
  settings->found = 1;
} /* void cgroups_walk_mountpoint */

static int cgroups_read(void) {
  cu_mount_t *mnt_list;
  cu_mount_t *mnt_ptr;
  struct controller_settings settings[] = {
      {0, "cpuacct", read_cpuacct_root},
      {0, "memory", read_memory_root},
  };
  unsigned int i;
  bool found_any = 0;

  mnt_list = NULL;
  if (cu_mount_getlist(&mnt_list) == NULL) {
    ERROR("cgroups plugin: cu_mount_getlist failed.");
    return -1;
  }

  for (mnt_ptr = mnt_list; mnt_ptr != NULL; mnt_ptr = mnt_ptr->next) {
    if (strcmp(mnt_ptr->type, "cgroup") != 0 &&
        strcmp(mnt_ptr->type, "cgroup2"))
      continue;

    for (i = 0; i < STATIC_ARRAY_SIZE(settings); i++)
      cgroups_walk_mountpoint(mnt_ptr, &settings[i],
                              strcmp(mnt_ptr->type, "cgroup2"));
  }

  cu_mount_freelist(mnt_list);

  for (i = 0; i < STATIC_ARRAY_SIZE(settings); i++) {
    found_any |= settings[i].found;
    if (0 == settings[i].found)
      WARNING("cgroups plugin: Unable to find cgroup "
              "mount-point with the \"%s\" option or cgroup2 mount-point.",
              settings[i].controller);
  }

  return found_any ? 0 : -1;
} /* int cgroup_read */

void module_register(void) {
  plugin_register_config("cgroups", cgroups_config, config_keys,
                         config_keys_num);
  plugin_register_init("cgroups", cgroups_init);
  plugin_register_shutdown("cgroups", cgroups_shutdown);
  plugin_register_read("cgroups", cgroups_read);
} /* void module_register */
