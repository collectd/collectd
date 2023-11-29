/**
 * collectd - src/df.c
 * Copyright (C) 2005-2009  Florian octo Forster
 * Copyright (C) 2009       Paul Sadauskas
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
 *   Paul Sadauskas <psadauskas at gmail.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"
#include "utils/mount/mount.h"

#if HAVE_STATVFS
#if HAVE_SYS_STATVFS_H
#include <sys/statvfs.h>
#endif
#define STATANYFS statvfs
#define STATANYFS_STR "statvfs"
#define BLOCKSIZE(s) ((s).f_frsize ? (s).f_frsize : (s).f_bsize)
#elif HAVE_STATFS
#if HAVE_SYS_STATFS_H
#include <sys/statfs.h>
#endif
#define STATANYFS statfs
#define STATANYFS_STR "statfs"
#define BLOCKSIZE(s) (s).f_bsize
#else
#error "No applicable input method."
#endif

static const char *config_keys[] = {
    "Device",         "MountPoint",       "FSType",
    "IgnoreSelected", "ReportByDevice",   "ReportInodes",
    "ValuesAbsolute", "ValuesPercentage", "LogOnce"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *il_device;
static ignorelist_t *il_mountpoint;
static ignorelist_t *il_fstype;
static ignorelist_t *il_errors;

static bool report_inodes;
static bool values_absolute = true;
static bool values_percentage;
static bool log_once;

static int df_init(void) {
  if (il_device == NULL)
    il_device = ignorelist_create(1);
  if (il_mountpoint == NULL)
    il_mountpoint = ignorelist_create(1);
  if (il_fstype == NULL)
    il_fstype = ignorelist_create(1);
  if (il_errors == NULL)
    il_errors = ignorelist_create(1);

  return 0;
}

static int df_config(const char *key, const char *value) {
  df_init();

  if (strcasecmp(key, "Device") == 0) {
    if (ignorelist_add(il_device, value))
      return 1;
    return 0;
  } else if (strcasecmp(key, "MountPoint") == 0) {
    if (ignorelist_add(il_mountpoint, value))
      return 1;
    return 0;
  } else if (strcasecmp(key, "FSType") == 0) {
    if (ignorelist_add(il_fstype, value))
      return 1;
    return 0;
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    if (IS_TRUE(value)) {
      ignorelist_set_invert(il_device, 0);
      ignorelist_set_invert(il_mountpoint, 0);
      ignorelist_set_invert(il_fstype, 0);
    } else {
      ignorelist_set_invert(il_device, 1);
      ignorelist_set_invert(il_mountpoint, 1);
      ignorelist_set_invert(il_fstype, 1);
    }
    return 0;
  } else if (strcasecmp(key, "ReportByDevice") == 0) {
    /* Not used anymore */
    return 0;
  } else if (strcasecmp(key, "ReportInodes") == 0) {
    if (IS_TRUE(value))
      report_inodes = true;
    else
      report_inodes = false;

    return 0;
  } else if (strcasecmp(key, "ValuesAbsolute") == 0) {
    if (IS_TRUE(value))
      values_absolute = true;
    else
      values_absolute = false;

    return 0;
  } else if (strcasecmp(key, "ValuesPercentage") == 0) {
    if (IS_TRUE(value))
      values_percentage = true;
    else
      values_percentage = false;

    return 0;
  } else if (strcasecmp(key, "LogOnce") == 0) {
    if (IS_TRUE(value))
      log_once = true;
    else
      log_once = false;

    return 0;
  }

  return -1;
}

static int df_read(void) {
#if HAVE_STATVFS
  struct statvfs statbuf;
#elif HAVE_STATFS
  struct statfs statbuf;
#endif
  metric_family_t fam_fs_free = {
      .name = "filesystem_free_bytes",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_reserved = {
      .name = "filesystem_reserved_bytes",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_used = {
      .name = "filesystem_used_bytes",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_free_pct = {
      .name = "filesystem_free_percent",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_reserved_pct = {
      .name = "filesystem_reserved_percent",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_used_pct = {
      .name = "filesystem_used_percent",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_inodes_free_pct = {
      .name = "filesystem_inodes_free_percent",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_inodes_reserved_pct = {
      .name = "filesystem_inodes_reserved_percent",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_inodes_used_pct = {
      .name = "filesystem_inodes_used_percent",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_inodes_free = {
      .name = "filesystem_inodes_free",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_inodes_reserved = {
      .name = "filesystem_inodes_reserved",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_fs_inodes_used = {
      .name = "filesystem_inodes_used",
      .type = METRIC_TYPE_GAUGE,
  };

  metric_family_t *fams[] = {&fam_fs_free,
                             &fam_fs_reserved,
                             &fam_fs_used,
                             &fam_fs_free_pct,
                             &fam_fs_reserved_pct,
                             &fam_fs_used_pct,
                             &fam_fs_inodes_free,
                             &fam_fs_inodes_reserved,
                             &fam_fs_inodes_used,
                             &fam_fs_inodes_free_pct,
                             &fam_fs_inodes_reserved_pct,
                             &fam_fs_inodes_used_pct,
                             NULL};

  int retval = 0;
  /* struct STATANYFS statbuf; */
  cu_mount_t *mnt_list;

  mnt_list = NULL;
  if (cu_mount_getlist(&mnt_list) == NULL) {
    ERROR("df plugin: cu_mount_getlist failed.");
    return -1;
  }

  for (cu_mount_t *mnt_ptr = mnt_list; mnt_ptr != NULL;
       mnt_ptr = mnt_ptr->next) {
    unsigned long long blocksize;
    uint64_t blk_free;
    uint64_t blk_reserved;
    uint64_t blk_used;

    char const *dev =
        (mnt_ptr->spec_device != NULL) ? mnt_ptr->spec_device : mnt_ptr->device;

    if (ignorelist_match(il_device, dev))
      continue;
    if (ignorelist_match(il_mountpoint, mnt_ptr->dir))
      continue;
    if (ignorelist_match(il_fstype, mnt_ptr->type))
      continue;

    if (STATANYFS(mnt_ptr->dir, &statbuf) < 0) {
      if (log_once == false || ignorelist_match(il_errors, mnt_ptr->dir) == 0) {
        if (log_once == true) {
          ignorelist_add(il_errors, mnt_ptr->dir);
        }
        ERROR(STATANYFS_STR "(%s) failed: %s", mnt_ptr->dir, STRERRNO);
      }
      continue;
    } else {
      if (log_once == true) {
        ignorelist_remove(il_errors, mnt_ptr->dir);
      }
    }

    if (!statbuf.f_blocks)
      continue;

    blocksize = BLOCKSIZE(statbuf);

/*
 * Sanity-check for the values in the struct
 */
/* Check for negative "available" byes. For example UFS can
 * report negative free space for user. Notice. blk_reserved
 * will start to diminish after this. */
#if HAVE_STATVFS
    /* Cast and temporary variable are needed to avoid
     * compiler warnings.
     * ((struct statvfs).f_bavail is unsigned (POSIX)) */
    int64_t signed_bavail = (int64_t)statbuf.f_bavail;
    if (signed_bavail < 0)
      statbuf.f_bavail = 0;
#elif HAVE_STATFS
    if (statbuf.f_bavail < 0)
      statbuf.f_bavail = 0;
#endif
    /* Make sure that f_blocks >= f_bfree >= f_bavail */
    if (statbuf.f_bfree < statbuf.f_bavail)
      statbuf.f_bfree = statbuf.f_bavail;
    if (statbuf.f_blocks < statbuf.f_bfree)
      statbuf.f_blocks = statbuf.f_bfree;

    blk_free = (uint64_t)statbuf.f_bavail;
    blk_reserved = (uint64_t)(statbuf.f_bfree - statbuf.f_bavail);
    blk_used = (uint64_t)(statbuf.f_blocks - statbuf.f_bfree);

    metric_t m = {0};
    metric_label_set(&m, "device", dev);
    metric_label_set(&m, "fstype", mnt_ptr->type);
    metric_label_set(&m, "mountpoint", mnt_ptr->dir);

    if (values_absolute) {
      m.value.gauge = (gauge_t)(blk_free * blocksize);
      metric_family_metric_append(&fam_fs_free, m);

      m.value.gauge = (gauge_t)(blk_reserved * blocksize);
      metric_family_metric_append(&fam_fs_reserved, m);

      m.value.gauge = (gauge_t)(blk_used * blocksize);
      metric_family_metric_append(&fam_fs_used, m);
    }

    if (values_percentage) {
      if (statbuf.f_blocks > 0) {
        m.value.gauge = (gauge_t)((float_t)(blk_free) / statbuf.f_blocks * 100);
        metric_family_metric_append(&fam_fs_free_pct, m);

        m.value.gauge =
            (gauge_t)((float_t)(blk_reserved) / statbuf.f_blocks * 100);
        metric_family_metric_append(&fam_fs_reserved_pct, m);

        m.value.gauge = (gauge_t)((float_t)(blk_used) / statbuf.f_blocks * 100);
        metric_family_metric_append(&fam_fs_used_pct, m);
      } else {
        metric_reset(&m);
        retval = -1;
        break;
      }
    }

    /* inode handling */
    if (report_inodes && statbuf.f_files != 0 && statbuf.f_ffree != 0) {
      uint64_t inode_free;
      uint64_t inode_reserved;
      uint64_t inode_used;

      /* Sanity-check for the values in the struct */
      if (statbuf.f_ffree < statbuf.f_favail)
        statbuf.f_ffree = statbuf.f_favail;
      if (statbuf.f_files < statbuf.f_ffree)
        statbuf.f_files = statbuf.f_ffree;

      inode_free = (uint64_t)statbuf.f_favail;
      inode_reserved = (uint64_t)(statbuf.f_ffree - statbuf.f_favail);
      inode_used = (uint64_t)(statbuf.f_files - statbuf.f_ffree);

      if (values_percentage) {
        if (statbuf.f_files > 0) {
          m.value.gauge =
              (gauge_t)((float_t)(inode_free) / statbuf.f_files * 100);
          metric_family_metric_append(&fam_fs_inodes_free_pct, m);

          m.value.gauge =
              (gauge_t)((float_t)(inode_reserved) / statbuf.f_files * 100);
          metric_family_metric_append(&fam_fs_inodes_reserved_pct, m);

          m.value.gauge =
              (gauge_t)((float_t)(inode_used) / statbuf.f_files * 100);
          metric_family_metric_append(&fam_fs_inodes_used_pct, m);
        } else {
          metric_reset(&m);
          retval = -1;
          break;
        }
      }
      if (values_absolute) {
        m.value.gauge = (gauge_t)inode_free;
        metric_family_metric_append(&fam_fs_inodes_free, m);

        m.value.gauge = (gauge_t)inode_reserved;
        metric_family_metric_append(&fam_fs_inodes_reserved, m);

        m.value.gauge = (gauge_t)inode_used;
        metric_family_metric_append(&fam_fs_inodes_used, m);
      }
    }

    metric_reset(&m);
  }

  cu_mount_freelist(mnt_list);

  for (size_t i = 0; fams[i] != NULL; i++) {
    if (fams[i]->metric.num > 0) {
      int status = plugin_dispatch_metric_family(fams[i]);
      if (status != 0) {
        ERROR("df: plugin_dispatch_metric_family failed: %s", STRERROR(status));
      }
      metric_family_metric_reset(fams[i]);
    }
  }

  return retval;
} /* int df_read */

void module_register(void) {
  plugin_register_config("df", df_config, config_keys, config_keys_num);
  plugin_register_init("df", df_init);
  plugin_register_read("df", df_read);
} /* void module_register */
