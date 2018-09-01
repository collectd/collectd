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
#include "utils_avltree.h"
#include "utils_ignorelist.h"
#include "utils_mount.h"

#include <btrfs/ioctl.h>

#define PLUGIN_NAME "btrfs"
static const char *config_keys[] = {"Path"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
static char *conf_btrfs_path;
static c_avl_tree_t *conf_list_btrfs_paths = NULL;

static int btrfs_avl_compare(const void *key, const void *value) { return 1; };

static int btrfs_config(const char *key, const char *value) {

  // create linked-list
  if (conf_list_btrfs_paths == NULL) {
    conf_list_btrfs_paths = c_avl_create(btrfs_avl_compare);
  }

  if (strcasecmp("Path", key) == 0) {

    int ret = 0;

    // get btrfs-path
    conf_btrfs_path = strdup(value);
    if (conf_btrfs_path == NULL) {
      return 1;
    }

    ret = c_avl_insert(conf_list_btrfs_paths, conf_btrfs_path, conf_btrfs_path);
    if (ret < 0) {
      ERROR("[btrfs] ERROR: c_avl_insert\n");
      exit(-1);
    }

    DEBUG("[btrfs] Set path: %s \n", conf_btrfs_path);
  }

  return 0;
} /* int disk_config */

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
  snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s#%s", folder,
           error);
  sstrncpy(vl.type, "count", sizeof(vl.type));

  // send it
  plugin_dispatch_values(&vl);
}

static int btrfs_submit_read_stats(char *path) {

  // vars
  char *btrfs_path = NULL;
  int fd = 0;
  int ret = 0;

  // copy to temporary
  btrfs_path = strdup(path);

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
  char *path = NULL;

  // iterate over config-paths
  c_avl_iterator_t *iterator = c_avl_get_iterator(conf_list_btrfs_paths);
  ret = c_avl_iterator_next(iterator, (void **)&path, (void **)&path);
  while (ret == 0) {
    btrfs_submit_read_stats(path);
    ret = c_avl_iterator_next(iterator, (void **)&path, (void **)&path);
  }
  c_avl_iterator_destroy(iterator);

  return 0;
}

void module_register(void) {
  plugin_register_config("btrfs", btrfs_config, config_keys, config_keys_num);
  plugin_register_read("btrfs", btrfs_read);
}
