/**
 * collectd - src/btrfs.c
 * Copyright (C) 2018       Martin Langlotz
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
 *   Martin Langlotz < stackshadow at evilbrain . de >
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"
#include "utils_llist.h"
#include "utils_mount.h"

#include <btrfs/ioctl.h>

#define PLUGIN_NAME "btrfs"
static const char *config_keys[] = {"RefreshMounts"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
static char btrfs_is_init = 0;
static char btrfs_conf_refresh_always = 0;
static llist_t *llist_btrfs_paths = NULL;

static int btrfs_mountlist_create() {

  if (llist_btrfs_paths != NULL) {
    llentry_t *e = llist_head(llist_btrfs_paths);
    while (e != NULL) {
      sfree(e->key);
      e = e->next;
    }
    llist_destroy(llist_btrfs_paths);
  }

  llist_btrfs_paths = llist_create();
  if (llist_btrfs_paths == NULL) {
    return -1;
  }

  return 0;
}

static int btrfs_mountlist_read() {

  FILE *file_mounts = setmntent("/proc/mounts", "r");
  if (file_mounts == NULL) {
    return -1;
  }

  struct mntent *mount_entry = getmntent(file_mounts);
  while (mount_entry != NULL) {

    if (strncmp(mount_entry->mnt_type, "btrfs", 5) == 0) {
      char *mnt_dir = strdup(mount_entry->mnt_dir);
      llist_append(llist_btrfs_paths, llentry_create(mnt_dir, NULL));
    }

    mount_entry = getmntent(file_mounts);
  }

  endmntent(file_mounts);
  return 0;
}

static int btrfs_init() {

  if (btrfs_is_init == 1)
    return 0;

  int ret = btrfs_mountlist_create();
  if (ret < 0) {
    return -1;
  }

  ret = btrfs_mountlist_read();
  if (ret < 0) {
    return -1;
  }

  btrfs_is_init = 1;
  return 0;
}

static int btrfs_config(const char *key, const char *value) {

  int ret = -1;

  ret = btrfs_init();
  if (ret < 0) {
    return -1;
  }

  if (strcasecmp("RefreshMounts", key) == 0) {

    if (strcasecmp("on", value) == 0) {
      btrfs_conf_refresh_always = 1;
      DEBUG("[btrfs] Enable refresh on every read \n");
    } else {
      btrfs_conf_refresh_always = 0;
    }
  }

  return 0;
}

static void btrfs_submit_value(const char *folder, const char *error,
                               gauge_t value) {

  // value
  value_t tmp_value = {.gauge = value};
  value_list_t vl = VALUE_LIST_INIT;

  // create value-list
  vl.values = &tmp_value;
  vl.values_len = 1;

  // create value
  sstrncpy(vl.plugin, PLUGIN_NAME, sizeof(vl.plugin));
  snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s", folder);
  snprintf(vl.type_instance, sizeof(vl.type_instance), "%s", error);
  sstrncpy(vl.type, "count", sizeof(vl.type));

  // send it
  plugin_dispatch_values(&vl);
}

static int btrfs_submit_read_stats(char *mount_path) {

  // vars
  char *btrfs_path = NULL;
  int fd = 0;
  int ret = 0;

  // copy to temporary
  btrfs_path = strdup(mount_path);

  // open
  DIR *dirstream = opendir(btrfs_path);
  if (dirstream == NULL) {
    ERROR("[btrfs] ERROR: open on %s failed %s\n", btrfs_path, strerror(errno));
    ret = -1;
    goto onerr;
  }

  // get fd
  fd = dirfd(dirstream);
  if (fd < 0) {
    ERROR("[btrfs] ERROR: open on %s failed: %s\n", btrfs_path,
          strerror(errno));
    ret = -1;
    goto onerr;
  }

  // get fs info
  struct btrfs_ioctl_fs_info_args fs_args = {0};
  ret = ioctl(fd, BTRFS_IOC_FS_INFO, &fs_args);
  if (ret < 0) {
    ERROR("[btrfs] ERROR: ioctl(BTRFS_IOC_FS_INFO) on %s failed: %s\n",
          btrfs_path, strerror(errno));
    goto onerr;
  }

  // get device stats
  struct btrfs_ioctl_get_dev_stats dev_stats_args = {0};
  dev_stats_args.devid = fs_args.max_id;
  dev_stats_args.nr_items = BTRFS_DEV_STAT_VALUES_MAX;
  dev_stats_args.flags = 0;

  ret = ioctl(fd, BTRFS_IOC_GET_DEV_STATS, &dev_stats_args);
  if (ret < 0) {
    ERROR("[btrfs] ERROR: ioctl(BTRFS_IOC_GET_DEV_STATS) on %s failed: %s\n",
          btrfs_path, strerror(errno));
    goto onerr;
  }

  // replace / with _
  escape_slashes(btrfs_path, strlen(btrfs_path) * sizeof(char));

  btrfs_submit_value(btrfs_path, "err-write",
                     dev_stats_args.values[BTRFS_DEV_STAT_WRITE_ERRS]);
  btrfs_submit_value(btrfs_path, "err-read",
                     dev_stats_args.values[BTRFS_DEV_STAT_READ_ERRS]);
  btrfs_submit_value(btrfs_path, "err-flush",
                     dev_stats_args.values[BTRFS_DEV_STAT_FLUSH_ERRS]);
  btrfs_submit_value(btrfs_path, "err-corrupt",
                     dev_stats_args.values[BTRFS_DEV_STAT_CORRUPTION_ERRS]);
  btrfs_submit_value(btrfs_path, "err-generate",
                     dev_stats_args.values[BTRFS_DEV_STAT_GENERATION_ERRS]);

  gauge_t sum = 0;
  sum += dev_stats_args.values[BTRFS_DEV_STAT_WRITE_ERRS];
  sum += dev_stats_args.values[BTRFS_DEV_STAT_READ_ERRS];
  sum += dev_stats_args.values[BTRFS_DEV_STAT_FLUSH_ERRS];
  sum += dev_stats_args.values[BTRFS_DEV_STAT_CORRUPTION_ERRS];
  sum += dev_stats_args.values[BTRFS_DEV_STAT_GENERATION_ERRS];
  btrfs_submit_value(btrfs_path, "err", sum);

onerr:
  // cleanup
  closedir(dirstream);
  close(fd);
  free(btrfs_path);

  return ret;
}

static int btrfs_read(void) {

  // vars
  int ret = 0;

  ret = btrfs_init();
  if (ret < 0) {
    return -1;
  }

  if (btrfs_conf_refresh_always == 1) {
    DEBUG("[btrfs] Refresh mounts..\n");

    ret = btrfs_mountlist_create();
    if (ret < 0) {
      return -1;
    }

    btrfs_mountlist_read();
  }

  for (llentry_t *e = llist_head(llist_btrfs_paths); e != NULL; e = e->next) {
    btrfs_submit_read_stats(e->key);
  }

  return 0;
}

static int btrfs_shutdown(void) {

  if (llist_btrfs_paths != NULL) {
    llentry_t *e = llist_head(llist_btrfs_paths);
    while (e != NULL) {
      sfree(e->key);
      e = e->next;
    }
    llist_destroy(llist_btrfs_paths);
  }

  return 0;
} /* static int powerdns_shutdown */

void module_register(void) {
  plugin_register_config(PLUGIN_NAME, btrfs_config, config_keys,
                         config_keys_num);
  plugin_register_read(PLUGIN_NAME, btrfs_read);
  plugin_register_shutdown(PLUGIN_NAME, btrfs_shutdown);
}
