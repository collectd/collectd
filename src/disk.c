/**
 * collectd - src/disk.c
 * Copyright (C) 2005-2012  Florian octo Forster
 * Copyright (C) 2009       Manuel Sanmartin
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
 *   Manuel Sanmartin
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#if HAVE_MACH_MACH_TYPES_H
#include <mach/mach_types.h>
#endif
#if HAVE_MACH_MACH_INIT_H
#include <mach/mach_init.h>
#endif
#if HAVE_MACH_MACH_ERROR_H
#include <mach/mach_error.h>
#endif
#if HAVE_MACH_MACH_PORT_H
#include <mach/mach_port.h>
#endif
#if HAVE_COREFOUNDATION_COREFOUNDATION_H
#include <CoreFoundation/CoreFoundation.h>
#endif
#if HAVE_IOKIT_IOKITLIB_H
#include <IOKit/IOKitLib.h>
#endif
#if HAVE_IOKIT_IOTYPES_H
#include <IOKit/IOTypes.h>
#endif
#if HAVE_IOKIT_STORAGE_IOBLOCKSTORAGEDRIVER_H
#include <IOKit/storage/IOBlockStorageDriver.h>
#endif
#if HAVE_IOKIT_IOBSD_H
#include <IOKit/IOBSD.h>
#endif
#if KERNEL_FREEBSD
#include <devstat.h>
#include <libgeom.h>
#endif

#ifndef UINT_MAX
#define UINT_MAX 4294967295U
#endif

#if HAVE_STATGRAB_H
#include <statgrab.h>
#endif

#if HAVE_PERFSTAT
#ifndef _AIXVERSION_610
#include <sys/systemcfg.h>
#endif
#include <libperfstat.h>
#include <sys/protosw.h>
#endif

static char const *const device_label = "system.device";
static char const *const direction_label = "disk.io.direction";

static char const *const read_direction = "read";
static char const *const write_direction = "write";

#if (MAC_OS_X_VERSION_MIN_REQUIRED < 120000) // Before macOS 12 Monterey
#define IOMainPort IOMasterPort
#endif

#if HAVE_IOKIT_IOKITLIB_H
static mach_port_t io_main_port = MACH_PORT_NULL;
/* This defaults to false for backwards compatibility. Please fix in the next
 * major version. */
static bool use_bsd_name;
/* #endif HAVE_IOKIT_IOKITLIB_H */

#elif KERNEL_LINUX
typedef struct diskstats {
  char *name;

  /* This overflows in roughly 1361 years */
  unsigned int poll_count;

  derive_t read_sectors;
  derive_t write_sectors;

  derive_t read_bytes;
  derive_t write_bytes;

  derive_t read_ops;
  derive_t write_ops;
  derive_t read_time_us;
  derive_t write_time_us;

  derive_t avg_read_time;
  derive_t avg_write_time;

  derive_t io_time;

  bool has_merged;
  bool has_in_progress;
  bool has_io_time;

  struct diskstats *next;
} diskstats_t;

static diskstats_t *disklist;
/* #endif KERNEL_LINUX */
#elif KERNEL_FREEBSD
static struct gmesh geom_tree;
/* #endif KERNEL_FREEBSD */

#elif HAVE_LIBKSTAT
#if HAVE_KSTAT_H
#include <kstat.h>
#endif
#define MAX_NUMDISK 1024
extern kstat_ctl_t *kc;
static kstat_t *ksp[MAX_NUMDISK];
static int numdisk;
/* #endif HAVE_LIBKSTAT */

#elif defined(HAVE_LIBSTATGRAB)
/* #endif HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
static perfstat_disk_t *stat_disk;
static int numdisk;
static int pnumdisk;
/* #endif HAVE_PERFSTAT */

#elif HAVE_SYSCTL && KERNEL_NETBSD

#include <sys/iostat.h>
#include <sys/sysctl.h>

static struct io_sysctl *drives = NULL;
static size_t ndrive = 0;

/* #endif HAVE_SYSCTL && KERNEL_NETBSD */

#else
#error "No applicable input method."
#endif

#if HAVE_LIBUDEV_H
#include <libudev.h>

static char *conf_udev_name_attr;
static struct udev *handle_udev;
#endif

static const char *config_keys[] = {"Disk", "UseBSDName", "IgnoreSelected",
                                    "UdevNameAttr"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist;

static int disk_config(const char *key, const char *value) {
  if (ignorelist == NULL)
    ignorelist = ignorelist_create(/* invert = */ 1);
  if (ignorelist == NULL)
    return 1;

  if (strcasecmp("Disk", key) == 0) {
    ignorelist_add(ignorelist, value);
  } else if (strcasecmp("IgnoreSelected", key) == 0) {
    int invert = 1;
    if (IS_TRUE(value))
      invert = 0;
    ignorelist_set_invert(ignorelist, invert);
  } else if (strcasecmp("UseBSDName", key) == 0) {
#if HAVE_IOKIT_IOKITLIB_H
    use_bsd_name = IS_TRUE(value);
#else
    WARNING("disk plugin: The \"UseBSDName\" option is only supported "
            "on Mach / Mac OS X and will be ignored.");
#endif
  } else if (strcasecmp("UdevNameAttr", key) == 0) {
#if HAVE_LIBUDEV_H
    if (conf_udev_name_attr != NULL) {
      free(conf_udev_name_attr);
      conf_udev_name_attr = NULL;
    }
    if ((conf_udev_name_attr = strdup(value)) == NULL)
      return 1;
#else
    WARNING("disk plugin: The \"UdevNameAttr\" option is only supported "
            "if collectd is built with libudev support");
#endif
  } else {
    return -1;
  }

  return 0;
} /* int disk_config */

static int disk_init(void) {
#if HAVE_IOKIT_IOKITLIB_H
  kern_return_t status;

  if (io_main_port != MACH_PORT_NULL) {
    mach_port_deallocate(mach_task_self(), io_main_port);
    io_main_port = MACH_PORT_NULL;
  }

  status = IOMainPort(MACH_PORT_NULL, &io_main_port);
  if (status != kIOReturnSuccess) {
    ERROR("IOMainPort failed: %s", mach_error_string(status));
    io_main_port = MACH_PORT_NULL;
    return -1;
  }
  /* #endif HAVE_IOKIT_IOKITLIB_H */

#elif KERNEL_LINUX
#if HAVE_LIBUDEV_H
  if (conf_udev_name_attr != NULL) {
    handle_udev = udev_new();
    if (handle_udev == NULL) {
      ERROR("disk plugin: udev_new() failed!");
      return -1;
    }
  }
#endif /* HAVE_LIBUDEV_H */
  /* #endif KERNEL_LINUX */

#elif KERNEL_FREEBSD
  int rv;

  rv = geom_gettree(&geom_tree);
  if (rv != 0) {
    ERROR("geom_gettree() failed, returned %d", rv);
    return -1;
  }
  rv = geom_stats_open();
  if (rv != 0) {
    ERROR("geom_stats_open() failed, returned %d", rv);
    return -1;
  }
  /* #endif KERNEL_FREEBSD */

#elif HAVE_LIBKSTAT
  kstat_t *ksp_chain;

  numdisk = 0;

  if (kc == NULL)
    return -1;

  for (numdisk = 0, ksp_chain = kc->kc_chain;
       (numdisk < MAX_NUMDISK) && (ksp_chain != NULL);
       ksp_chain = ksp_chain->ks_next) {
    if (strncmp(ksp_chain->ks_class, "disk", 4) &&
        strncmp(ksp_chain->ks_class, "partition", 9))
      continue;
    if (ksp_chain->ks_type != KSTAT_TYPE_IO)
      continue;
    ksp[numdisk++] = ksp_chain;
  }
/* #endif HAVE_LIBKSTAT */
#elif HAVE_SYSCTL && KERNEL_NETBSD
  int mib[3];
  size_t size;

  /* figure out number of drives */
  mib[0] = CTL_HW;
  mib[1] = HW_IOSTATS;
  mib[2] = sizeof(struct io_sysctl);
  if (sysctl(mib, 3, NULL, &size, NULL, 0) == -1) {
    ERROR("disk plugin: sysctl for ndrives failed");
    return -1;
  }
  ndrive = size / sizeof(struct io_sysctl);

  if (size == 0) {
    ERROR("disk plugin: no drives found");
    return -1;
  }
  drives = (struct io_sysctl *)malloc(size);
  if (drives == NULL) {
    ERROR("disk plugin: memory allocation failure");
    return -1;
  }
#endif /* HAVE_SYSCTL && KERNEL_NETBSD */

  return 0;
} /* int disk_init */

static int disk_shutdown(void) {
#if KERNEL_LINUX
#if HAVE_LIBUDEV_H
  if (handle_udev != NULL)
    udev_unref(handle_udev);
#endif /* HAVE_LIBUDEV_H */
#endif /* KERNEL_LINUX */
  return 0;
} /* int disk_shutdown */

#if KERNEL_LINUX
static counter_t disk_calc_time_incr(counter_t delta_time,
                                     counter_t delta_ops) {
  double interval = CDTIME_T_TO_DOUBLE(plugin_get_interval());
  double avg_time = ((double)delta_time) / ((double)delta_ops);
  double avg_time_incr = interval * avg_time;

  return (counter_t)(avg_time_incr + .5);
}
#endif

#if HAVE_LIBUDEV_H
/**
 * Attempt to provide an rename disk instance from an assigned udev attribute.
 *
 * On success, it returns a strduped char* to the desired attribute value.
 * Otherwise it returns NULL.
 */

static char *disk_udev_attr_name(struct udev *udev, char *disk_name,
                                 const char *attr) {
  struct udev_device *dev;
  const char *prop;
  char *output = NULL;

  dev = udev_device_new_from_subsystem_sysname(udev, "block", disk_name);
  if (dev != NULL) {
    prop = udev_device_get_property_value(dev, attr);
    if (prop) {
      output = strdup(prop);
      DEBUG("disk plugin: renaming %s => %s", disk_name, output);
    }
    udev_device_unref(dev);
  }
  return output;
}
#endif

#if HAVE_IOKIT_IOKITLIB_H
static signed long long dict_get_value(CFDictionaryRef dict, const char *key) {
  signed long long val_int;
  CFNumberRef val_obj;
  CFStringRef key_obj;

  /* `key_obj' needs to be released. */
  key_obj = CFStringCreateWithCString(kCFAllocatorDefault, key,
                                      kCFStringEncodingASCII);
  if (key_obj == NULL) {
    DEBUG("CFStringCreateWithCString (%s) failed.", key);
    return -1LL;
  }

  /* get => we don't need to release (== free) the object */
  val_obj = (CFNumberRef)CFDictionaryGetValue(dict, key_obj);

  CFRelease(key_obj);

  if (val_obj == NULL) {
    DEBUG("CFDictionaryGetValue (%s) failed.", key);
    return -1LL;
  }

  if (!CFNumberGetValue(val_obj, kCFNumberSInt64Type, &val_int)) {
    DEBUG("CFNumberGetValue (%s) failed.", key);
    return -1LL;
  }

  return val_int;
}
#endif /* HAVE_IOKIT_IOKITLIB_H */

static int disk_read(void) {
  metric_family_t fam_io = {
      .name = "system.disk.io",
      .help = "Bytes read from and written to disk.",
      .unit = "By",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_t fam_ops = {
      .name = "system.disk.operations",
      .help = "Read and write operations performed by the disk. Operations are "
              "reported separately even if they were merged into one operation "
              "by the I/O subsystem.",
      .unit = "{operation}",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_t fam_ops_time = {
      .name = "system.disk.operation_time",
      .help = "Sum of the time each operation took to complete",
      .unit = "us",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_t fam_merged = {
      .name = "system.disk.merged",
      .help = "I/O operations that were merged into reduce load on the disk.",
      .unit = "{operation}",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_t fam_disk_io_time = {
      .name = "system.disk.io_time",
      .help = "Time disk spent activated",
      .unit = "us",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_t fam_disk_io_weighted_time = {
      .name = "system.disk.weighted_io_time",
      .help = "This metric is incremented at each I/O start, I/O completion, "
              "or I/O merge by the number of I/Os in progress, times the "
              "number of milliseconds spent doing I/O since the last update of "
              "this field. This can provide an easy measure of both I/O "
              "completion time and the backlog that may be accumulating.",
      .unit = "ms",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_family_t fam_disk_pending_operations = {
      .name = "system.disk.pending_operations",
      .help = "Number of I/O operations currently in progress.",
      .unit = "{operation}",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_utilization = {
      .name = "system.disk.utilization",
      .help = "The ratio of time the device had one or more transactions "
              "outstanding.",
      .unit = "1",
      .type = METRIC_TYPE_GAUGE,
  };

  metric_family_t *fams[] = {
      &fam_io,
      &fam_ops,
      &fam_ops_time,
      &fam_merged,
      &fam_disk_io_time,
      &fam_disk_io_weighted_time,
      &fam_disk_pending_operations,
      &fam_utilization,
      NULL,
  };

#if HAVE_IOKIT_IOKITLIB_H
  io_registry_entry_t disk;
  io_registry_entry_t disk_child;
  io_iterator_t disk_list;
  CFMutableDictionaryRef props_dict, child_dict;
  CFDictionaryRef stats_dict;
  CFStringRef tmp_cf_string_ref;
  kern_return_t status;

  signed long long read_ops, read_byt;
  signed long long write_ops, write_byt;

  int disk_major, disk_minor;
  char disk_name[DATA_MAX_NAME_LEN];
  char child_disk_name_bsd[DATA_MAX_NAME_LEN],
      props_disk_name_bsd[DATA_MAX_NAME_LEN];

  /* Get the list of all disk objects. */
  if (IOServiceGetMatchingServices(
          io_main_port, IOServiceMatching(kIOBlockStorageDriverClass),
          &disk_list) != kIOReturnSuccess) {
    ERROR("disk plugin: IOServiceGetMatchingServices failed.");
    return -1;
  }

  while ((disk = IOIteratorNext(disk_list)) != 0) {
    props_dict = NULL;
    stats_dict = NULL;
    child_dict = NULL;

    /* get child of disk entry and corresponding property dictionary */
    if ((status = IORegistryEntryGetChildEntry(
             disk, kIOServicePlane, &disk_child)) != kIOReturnSuccess) {
      /* This fails for example for DVD/CD drives, which we want to ignore
       * anyway */
      DEBUG("IORegistryEntryGetChildEntry (disk) failed: 0x%08x", status);
      IOObjectRelease(disk);
      continue;
    }
    if (IORegistryEntryCreateCFProperties(
            disk_child, (CFMutableDictionaryRef *)&child_dict,
            kCFAllocatorDefault, kNilOptions) != kIOReturnSuccess ||
        child_dict == NULL) {
      ERROR("disk plugin: IORegistryEntryCreateCFProperties (disk_child) "
            "failed.");
      IOObjectRelease(disk_child);
      IOObjectRelease(disk);
      continue;
    }

    /* extract name and major/minor numbers */
    memset(child_disk_name_bsd, 0, sizeof(child_disk_name_bsd));
    tmp_cf_string_ref =
        (CFStringRef)CFDictionaryGetValue(child_dict, CFSTR(kIOBSDNameKey));
    if (tmp_cf_string_ref) {
      assert(CFGetTypeID(tmp_cf_string_ref) == CFStringGetTypeID());
      CFStringGetCString(tmp_cf_string_ref, child_disk_name_bsd,
                         sizeof(child_disk_name_bsd), kCFStringEncodingUTF8);
    }
    disk_major = (int)dict_get_value(child_dict, kIOBSDMajorKey);
    disk_minor = (int)dict_get_value(child_dict, kIOBSDMinorKey);
    DEBUG("disk plugin: child_disk_name_bsd=\"%s\" major=%d minor=%d",
          child_disk_name_bsd, disk_major, disk_minor);
    CFRelease(child_dict);
    IOObjectRelease(disk_child);

    /* get property dictionary of the disk entry itself */
    if (IORegistryEntryCreateCFProperties(
            disk, (CFMutableDictionaryRef *)&props_dict, kCFAllocatorDefault,
            kNilOptions) != kIOReturnSuccess ||
        props_dict == NULL) {
      ERROR("disk-plugin: IORegistryEntryCreateCFProperties failed.");
      IOObjectRelease(disk);
      continue;
    }

    /* extract name and stats dictionary */
    memset(props_disk_name_bsd, 0, sizeof(props_disk_name_bsd));
    tmp_cf_string_ref =
        (CFStringRef)CFDictionaryGetValue(props_dict, CFSTR(kIOBSDNameKey));
    if (tmp_cf_string_ref) {
      assert(CFGetTypeID(tmp_cf_string_ref) == CFStringGetTypeID());
      CFStringGetCString(tmp_cf_string_ref, props_disk_name_bsd,
                         sizeof(props_disk_name_bsd), kCFStringEncodingUTF8);
    }
    stats_dict = (CFDictionaryRef)CFDictionaryGetValue(
        props_dict, CFSTR(kIOBlockStorageDriverStatisticsKey));
    if (stats_dict == NULL) {
      ERROR("disk plugin: CFDictionaryGetValue (%s) failed.",
            kIOBlockStorageDriverStatisticsKey);
      CFRelease(props_dict);
      IOObjectRelease(disk);
      continue;
    }
    DEBUG("disk plugin: props_disk_name_bsd=\"%s\"", props_disk_name_bsd);

    /* choose name */
    if (use_bsd_name) {
      if (child_disk_name_bsd[0] != 0)
        sstrncpy(disk_name, child_disk_name_bsd, sizeof(disk_name));
      else if (props_disk_name_bsd[0] != 0)
        sstrncpy(disk_name, props_disk_name_bsd, sizeof(disk_name));
      else {
        ERROR("disk plugin: can't find bsd disk name.");
        ssnprintf(disk_name, sizeof(disk_name), "%i-%i", disk_major,
                  disk_minor);
      }
    } else
      ssnprintf(disk_name, sizeof(disk_name), "%i-%i", disk_major, disk_minor);

    DEBUG("disk plugin: disk_name = \"%s\"", disk_name);

    /* check the name against ignore list */
    if (ignorelist_match(ignorelist, disk_name) != 0) {
      CFRelease(props_dict);
      IOObjectRelease(disk);
      continue;
    }

    /* extract the stats */
    read_ops =
        dict_get_value(stats_dict, kIOBlockStorageDriverStatisticsReadsKey);
    read_byt =
        dict_get_value(stats_dict, kIOBlockStorageDriverStatisticsBytesReadKey);
    long long read_time_ns = dict_get_value(
        stats_dict, kIOBlockStorageDriverStatisticsTotalReadTimeKey);
    write_ops =
        dict_get_value(stats_dict, kIOBlockStorageDriverStatisticsWritesKey);
    write_byt = dict_get_value(stats_dict,
                               kIOBlockStorageDriverStatisticsBytesWrittenKey);
    long long write_time_ns = dict_get_value(
        stats_dict, kIOBlockStorageDriverStatisticsTotalWriteTimeKey);
    CFRelease(props_dict);
    IOObjectRelease(disk);

    /* and submit */
    metric_t m = {0};
    metric_label_set(&m, device_label, disk_name);
    if ((read_byt != -1LL) || (write_byt != -1LL)) {
      metric_family_append(&fam_io, direction_label, read_direction,
                           (value_t){.counter = read_byt}, &m);
      metric_family_append(&fam_io, direction_label, write_direction,
                           (value_t){.counter = write_byt}, &m);
    }
    if ((read_ops != -1LL) || (write_ops != -1LL)) {
      metric_family_append(&fam_ops, direction_label, read_direction,
                           (value_t){.counter = read_ops}, &m);
      metric_family_append(&fam_ops, direction_label, write_direction,
                           (value_t){.counter = write_ops}, &m);
    }
    if ((read_time_ns != -1LL) || (write_time_ns != -1LL)) {
      metric_family_append(&fam_ops_time, direction_label, read_direction,
                           (value_t){.derive = read_time_ns / 1000}, &m);
      metric_family_append(&fam_ops_time, direction_label, write_direction,
                           (value_t){.derive = write_time_ns / 1000}, &m);
    }
    metric_reset(&m);
  }
  IOObjectRelease(disk_list);
  /* #endif HAVE_IOKIT_IOKITLIB_H */

#elif KERNEL_FREEBSD
  int retry, dirty;

  void *snap = NULL;
  struct devstat *snap_iter;

  struct gident *geom_id;

  const char *disk_name;

  for (retry = 0, dirty = 1; retry < 5 && dirty == 1; retry++) {
    if (snap != NULL)
      geom_stats_snapshot_free(snap);

    /* Get a fresh copy of stats snapshot */
    snap = geom_stats_snapshot_get();
    if (snap == NULL) {
      ERROR("disk plugin: geom_stats_snapshot_get() failed.");
      return -1;
    }

    /* Check if we have dirty read from this snapshot */
    dirty = 0;
    geom_stats_snapshot_reset(snap);
    while ((snap_iter = geom_stats_snapshot_next(snap)) != NULL) {
      if (snap_iter->id == NULL)
        continue;
      geom_id = geom_lookupid(&geom_tree, snap_iter->id);

      /* New device? refresh GEOM tree */
      if (geom_id == NULL) {
        geom_deletetree(&geom_tree);
        if (geom_gettree(&geom_tree) != 0) {
          ERROR("disk plugin: geom_gettree() failed");
          geom_stats_snapshot_free(snap);
          return -1;
        }
        geom_id = geom_lookupid(&geom_tree, snap_iter->id);
      }
      /*
       * This should be rare: the device come right before we take the
       * snapshot and went away right after it.  We will handle this
       * case later, so don't mark dirty but silently ignore it.
       */
      if (geom_id == NULL)
        continue;

      /* Only collect PROVIDER data */
      if (geom_id->lg_what != ISPROVIDER)
        continue;

      /* Only collect data when rank is 1 (physical devices) */
      if (((struct gprovider *)(geom_id->lg_ptr))->lg_geom->lg_rank != 1)
        continue;

      /* Check if this is a dirty read quit for another try */
      if (snap_iter->sequence0 != snap_iter->sequence1) {
        dirty = 1;
        break;
      }
    }
  }

  /* Reset iterator */
  geom_stats_snapshot_reset(snap);
  for (;;) {
    snap_iter = geom_stats_snapshot_next(snap);
    if (snap_iter == NULL)
      break;

    if (snap_iter->id == NULL)
      continue;
    geom_id = geom_lookupid(&geom_tree, snap_iter->id);
    if (geom_id == NULL)
      continue;
    if (geom_id->lg_what != ISPROVIDER)
      continue;
    if (((struct gprovider *)(geom_id->lg_ptr))->lg_geom->lg_rank != 1)
      continue;
    /* Skip dirty reads, if present */
    if (dirty && (snap_iter->sequence0 != snap_iter->sequence1))
      continue;

    disk_name = ((struct gprovider *)geom_id->lg_ptr)->lg_name;

    if (ignorelist_match(ignorelist, disk_name) != 0)
      continue;

    metric_t m = {0};
    metric_label_set(&m, device_label, disk_name);

    if ((snap_iter->bytes[DEVSTAT_READ] != 0) ||
        (snap_iter->bytes[DEVSTAT_WRITE] != 0)) {
      metric_family_append(
          &fam_io, direction_label, read_direction,
          (value_t){.counter = (counter_t)snap_iter->bytes[DEVSTAT_READ]}, &m);
      metric_family_append(
          &fam_io, direction_label, write_direction,
          (value_t){.counter = (counter_t)snap_iter->bytes[DEVSTAT_WRITE]}, &m);
    }

    if ((snap_iter->operations[DEVSTAT_READ] != 0) ||
        (snap_iter->operations[DEVSTAT_WRITE] != 0)) {
      metric_family_append(
          &fam_ops, direction_label, read_direction,
          (value_t){.counter = (counter_t)snap_iter->operations[DEVSTAT_READ]},
          &m);
      metric_family_append(
          &fam_ops, direction_label, write_direction,
          (value_t){.counter = (counter_t)snap_iter->operations[DEVSTAT_WRITE]},
          &m);
    }

    long double read_time_s =
        devstat_compute_etime(&snap_iter->duration[DEVSTAT_READ], NULL);
    long double write_time_s =
        devstat_compute_etime(&snap_iter->duration[DEVSTAT_WRITE], NULL);
    if ((read_time_s != 0) || (write_time_s != 0)) {
      metric_family_append(
          &fam_ops_time, direction_label, read_direction,
          (value_t){.derive = (derive_t)(read_time_s * 1000000.0)}, &m);
      metric_family_append(
          &fam_ops_time, direction_label, write_direction,
          (value_t){.derive = (derive_t)(write_time_s * 1000000.0)}, &m);
    }

    long double busy_time = 0, utilization = 0, total_duration = 0;
    uint64_t queue_length = 0;
    if (devstat_compute_statistics(
            snap_iter, NULL, 1.0, DSM_TOTAL_BUSY_TIME, &busy_time, DSM_BUSY_PCT,
            &utilization, DSM_TOTAL_DURATION, &total_duration, DSM_QUEUE_LENGTH,
            &queue_length, DSM_NONE) != 0) {
      WARNING("%s", devstat_errbuf);
    } else {
      m.value.counter = (counter_t)(1000000.0 * busy_time);
      metric_family_metric_append(&fam_disk_io_time, m);

      m.value.gauge = (gauge_t)utilization;
      metric_family_metric_append(&fam_utilization, m);

      m.value.counter = (counter_t)total_duration;
      metric_family_metric_append(&fam_disk_io_weighted_time, m);

      m.value.gauge = (gauge_t)queue_length;
      metric_family_metric_append(&fam_disk_pending_operations, m);
    }
    metric_reset(&m);
  }
  geom_stats_snapshot_free(snap);

#elif KERNEL_LINUX
  FILE *fh;
  char buffer[1024];

  char *fields[32];
  static unsigned int poll_count = 0;

  derive_t read_sectors = 0;
  derive_t write_sectors = 0;

  derive_t read_ops = 0;
  derive_t read_merged = 0;
  derive_t read_time_us = 0;
  derive_t write_ops = 0;
  derive_t write_merged = 0;
  derive_t write_time_us = 0;
  gauge_t in_progress = NAN;
  derive_t io_time_ms = 0;
  derive_t weighted_time = 0;
  derive_t diff_io_time_ms = 0;
  int is_disk = 0;

  diskstats_t *ds, *pre_ds;

  if ((fh = fopen("/proc/diskstats", "r")) == NULL) {
    ERROR("disk plugin: fopen(\"/proc/diskstats\"): %s", STRERRNO);
    return -1;
  }

  poll_count++;
  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    int numfields = strsplit(buffer, fields, 32);

    /* need either 7 fields (partition) or at least 14 fields */
    if ((numfields != 7) && (numfields < 14))
      continue;

    char *disk_name = fields[2];

    for (ds = disklist, pre_ds = disklist; ds != NULL;
         pre_ds = ds, ds = ds->next)
      if (strcmp(disk_name, ds->name) == 0)
        break;

    if (ds == NULL) {
      if ((ds = calloc(1, sizeof(*ds))) == NULL)
        continue;

      if ((ds->name = strdup(disk_name)) == NULL) {
        free(ds);
        continue;
      }

      if (pre_ds == NULL)
        disklist = ds;
      else
        pre_ds->next = ds;
    }

    is_disk = 0;
    if (numfields == 7) {
      /* Kernel 2.6, Partition */
      read_ops = atoll(fields[3]);
      read_sectors = atoll(fields[4]);
      write_ops = atoll(fields[5]);
      write_sectors = atoll(fields[6]);
    } else {
      assert(numfields >= 14);
      read_ops = atoll(fields[3]);
      write_ops = atoll(fields[7]);

      read_sectors = atoll(fields[5]);
      write_sectors = atoll(fields[9]);

      is_disk = 1;
      read_merged = atoll(fields[4]);
      read_time_us = atoll(fields[6]);
      write_merged = atoll(fields[8]);
      write_time_us = atoll(fields[10]);

      in_progress = atof(fields[11]);

      io_time_ms = atof(fields[12]);
      weighted_time = atof(fields[13]);
    }

    {
      derive_t diff_read_sectors;
      derive_t diff_write_sectors;

      /* If the counter wraps around, it's only 32 bits.. */
      if (read_sectors < ds->read_sectors)
        diff_read_sectors = 1 + read_sectors + (UINT_MAX - ds->read_sectors);
      else
        diff_read_sectors = read_sectors - ds->read_sectors;
      if (write_sectors < ds->write_sectors)
        diff_write_sectors = 1 + write_sectors + (UINT_MAX - ds->write_sectors);
      else
        diff_write_sectors = write_sectors - ds->write_sectors;

      ds->read_bytes += 512 * diff_read_sectors;
      ds->write_bytes += 512 * diff_write_sectors;
      ds->read_sectors = read_sectors;
      ds->write_sectors = write_sectors;
    }

    /* Calculate the average time an io-op needs to complete */
    if (is_disk) {
      derive_t diff_read_ops;
      derive_t diff_write_ops;
      derive_t diff_read_time;
      derive_t diff_write_time;

      if (read_ops < ds->read_ops)
        diff_read_ops = 1 + read_ops + (UINT_MAX - ds->read_ops);
      else
        diff_read_ops = read_ops - ds->read_ops;
      DEBUG("disk plugin: disk_name = %s; read_ops = %" PRIi64 "; "
            "ds->read_ops = %" PRIi64 "; diff_read_ops = %" PRIi64 ";",
            disk_name, read_ops, ds->read_ops, diff_read_ops);

      if (write_ops < ds->write_ops)
        diff_write_ops = 1 + write_ops + (UINT_MAX - ds->write_ops);
      else
        diff_write_ops = write_ops - ds->write_ops;

      if (read_time_us < ds->read_time_us)
        diff_read_time = 1 + read_time_us + (UINT_MAX - ds->read_time_us);
      else
        diff_read_time = read_time_us - ds->read_time_us;

      if (write_time_us < ds->write_time_us)
        diff_write_time = 1 + write_time_us + (UINT_MAX - ds->write_time_us);
      else
        diff_write_time = write_time_us - ds->write_time_us;

      if (io_time_ms < ds->io_time)
        diff_io_time_ms = 1 + io_time_ms + (UINT_MAX - ds->io_time);
      else
        diff_io_time_ms = io_time_ms - ds->io_time;

      if (diff_read_ops != 0)
        ds->avg_read_time += disk_calc_time_incr(diff_read_time, diff_read_ops);
      if (diff_write_ops != 0)
        ds->avg_write_time +=
            disk_calc_time_incr(diff_write_time, diff_write_ops);

      ds->read_ops = read_ops;
      ds->read_time_us = read_time_us;
      ds->write_ops = write_ops;
      ds->write_time_us = write_time_us;
      ds->io_time = io_time_ms;

      if (read_merged || write_merged)
        ds->has_merged = true;

      if (in_progress)
        ds->has_in_progress = true;

      if (io_time_ms)
        ds->has_io_time = true;

    } /* if (is_disk) */

    /* Skip first cycle for newly-added disk */
    if (ds->poll_count == 0) {
      DEBUG("disk plugin: (ds->poll_count = 0) => Skipping.");
      ds->poll_count = poll_count;
      continue;
    }
    ds->poll_count = poll_count;

    if ((read_ops == 0) && (write_ops == 0)) {
      DEBUG("disk plugin: ((read_ops == 0) && "
            "(write_ops == 0)); => Not writing.");
      continue;
    }

    char *output_name = disk_name;

#if HAVE_LIBUDEV_H
    char *alt_name = NULL;
    if (conf_udev_name_attr != NULL) {
      alt_name =
          disk_udev_attr_name(handle_udev, disk_name, conf_udev_name_attr);
      if (alt_name != NULL)
        output_name = alt_name;
    }
#endif

    if (ignorelist_match(ignorelist, output_name) != 0) {
#if HAVE_LIBUDEV_H
      /* release udev-based alternate name, if allocated */
      sfree(alt_name);
#endif
      continue;
    }
    metric_t m = {0};
    metric_label_set(&m, device_label, output_name);

    if ((ds->read_bytes != 0) || (ds->write_bytes != 0)) {
      metric_family_append(&fam_io, direction_label, read_direction,
                           (value_t){.counter = (counter_t)ds->read_bytes}, &m);
      metric_family_append(&fam_io, direction_label, write_direction,
                           (value_t){.counter = (counter_t)ds->write_bytes},
                           &m);
    }

    if ((ds->read_ops != 0) || (ds->write_ops != 0)) {
      metric_family_append(&fam_ops, direction_label, read_direction,
                           (value_t){.counter = (counter_t)ds->read_ops}, &m);
      metric_family_append(&fam_ops, direction_label, write_direction,
                           (value_t){.counter = (counter_t)ds->write_ops}, &m);
    }

    if ((ds->read_time_us != 0) || (ds->write_time_us != 0)) {
      metric_family_append(&fam_ops_time, direction_label, read_direction,
                           (value_t){.derive = ds->read_time_us}, &m);
      metric_family_append(&fam_ops_time, direction_label, write_direction,
                           (value_t){.derive = ds->write_time_us}, &m);
    }

    if (is_disk) {
      if (ds->has_merged) {
        metric_family_append(&fam_merged, direction_label, read_direction,
                             (value_t){.counter = (counter_t)read_merged}, &m);
        metric_family_append(&fam_merged, direction_label, write_direction,
                             (value_t){.counter = (counter_t)write_merged}, &m);
      }
      if (ds->has_in_progress) {
        m.value.gauge = in_progress;
        metric_family_metric_append(&fam_disk_pending_operations, m);
      }
      if (ds->has_io_time) {
        m.value.derive = 1000 * io_time_ms;
        metric_family_metric_append(&fam_disk_io_time, m);
      }
      m.value.counter = (counter_t)weighted_time;
      metric_family_metric_append(&fam_disk_io_weighted_time, m);

      long interval_ms = CDTIME_T_TO_MS(plugin_get_interval());
      m.value.gauge = ((gauge_t)diff_io_time_ms) / ((gauge_t)interval_ms);
      metric_family_metric_append(&fam_utilization, m);
    } /* if (is_disk) */

    metric_reset(&m);

#if HAVE_LIBUDEV_H
    /* release udev-based alternate name, if allocated */
    sfree(alt_name);
#endif
  } /* while (fgets (buffer, sizeof (buffer), fh) != NULL) */

  /* Remove disks that have disappeared from diskstats */
  for (ds = disklist, pre_ds = disklist; ds != NULL;) {
    /* Disk exists */
    if (ds->poll_count == poll_count) {
      pre_ds = ds;
      ds = ds->next;
      continue;
    }

    /* Disk is missing, remove it */
    diskstats_t *missing_ds = ds;
    if (ds == disklist) {
      pre_ds = disklist = ds->next;
    } else {
      pre_ds->next = ds->next;
    }
    ds = ds->next;

    DEBUG("disk plugin: Disk %s disappeared.", missing_ds->name);
    free(missing_ds->name);
    free(missing_ds);
  }
  fclose(fh);
  /* #endif defined(KERNEL_LINUX) */

#elif HAVE_LIBKSTAT
#if HAVE_KSTAT_IO_T_WRITES && HAVE_KSTAT_IO_T_NWRITES && HAVE_KSTAT_IO_T_WTIME
#define KIO_ROCTETS reads
#define KIO_WOCTETS writes
#define KIO_ROPS nreads
#define KIO_WOPS nwrites
#define KIO_RTIME rtime
#define KIO_WTIME wtime
#elif HAVE_KSTAT_IO_T_NWRITTEN && HAVE_KSTAT_IO_T_WRITES &&                    \
    HAVE_KSTAT_IO_T_WTIME
#define KIO_ROCTETS nread
#define KIO_WOCTETS nwritten
#define KIO_ROPS reads
#define KIO_WOPS writes
#define KIO_RTIME rtime
#define KIO_WTIME wtime
#else
#error "kstat_io_t does not have the required members"
#endif
  static kstat_io_t kio;

  if (kc == NULL)
    return -1;

  for (int i = 0; i < numdisk; i++) {
    if (kstat_read(kc, ksp[i], &kio) == -1) {
      continue;
    }
    if ((strncmp(ksp[i]->ks_class, "disk", 4) != 0) &&
        (strncmp(ksp[i]->ks_class, "partition", 9) != 0)) {
      continue;
    }
    if (ignorelist_match(ignorelist, ksp[i]->ks_name) != 0) {
      continue;
    }

    metric_t m = {0};
    metric_label_set(&m, device_label, ksp[i]->ks_name);

    metric_family_append(&fam_io, direction_label, read_direction,
                         (value_t){.counter = kio.KIO_ROCTETS}, &m);
    metric_family_append(&fam_io, direction_label, write_direction,
                         (value_t){.counter = kio.KIO_WOCTETS}, &m);

    metric_family_append(&fam_ops, direction_label, read_direction,
                         (value_t){.counter = kio.KIO_ROPS}, &m);
    metric_family_append(&fam_ops, direction_label, write_direction,
                         (value_t){.counter = kio.KIO_WOPS}, &m);

    metric_reset(&m);
  }
  /* #endif defined(HAVE_LIBKSTAT) */

#elif defined(HAVE_LIBSTATGRAB)
  sg_disk_io_stats *ds;
#if HAVE_LIBSTATGRAB_0_90
  size_t disks;
#else
  int disks;
#endif

  if ((ds = sg_get_disk_io_stats(&disks)) == NULL)
    return 0;

  for (int counter = 0; counter < disks; counter++) {
    if (ignorelist_match(ignorelist, ds->disk_name) != 0) {
      ds++;
      continue;
    }
    metric_t m = {0};
    metric_label_set(&m, device_label, ds->disk_name);

    metric_family_append(&fam_io, direction_label, read_direction,
                         (value_t){.counter = ds->read_bytes}, &m);
    metric_family_append(&fam_io, direction_label, write_direction,
                         (value_t){.counter = ds->write_bytes}, &m);

    metric_reset(&m);
    ds++;
  }
  /* #endif defined(HAVE_LIBSTATGRAB) */

#elif defined(HAVE_PERFSTAT)
  if ((numdisk = perfstat_disk(NULL, NULL, sizeof(perfstat_disk_t), 0)) < 0) {
    WARNING("disk plugin: perfstat_disk: %s", STRERRNO);
    return -1;
  }

  if (numdisk != pnumdisk || stat_disk == NULL) {
    free(stat_disk);
    stat_disk = calloc(numdisk, sizeof(*stat_disk));
  }
  pnumdisk = numdisk;

  perfstat_id_t firstpath = {
      .name = "",
  };
  int rnumdisk =
      perfstat_disk(&firstpath, stat_disk, sizeof(perfstat_disk_t), numdisk);
  if (rnumdisk < 0) {
    WARNING("disk plugin: perfstat_disk : %s", STRERRNO);
    return -1;
  }

  for (int i = 0; i < rnumdisk; i++) {
    if (ignorelist_match(ignorelist, stat_disk[i].name) != 0)
      continue;
    metric_t m = {0};
    metric_label_set(&m, device_label, stat_disk[i].name);

    metric_family_append(&fam_io, direction_label, read_direction,
                         (value_t){.counter = (counter_t)(stat_disk[i].rblks *
                                                          stat_disk[i].bsize)},
                         &m);
    metric_family_append(&fam_io, direction_label, write_direction,
                         (value_t){.counter = (counter_t)(stat_disk[i].wblks *
                                                          stat_disk[i].bsize)},
                         &m);

    metric_family_append(&fam_ops, direction_label, read_direction,
                         (value_t){.counter = (counter_t)stat_disk[i].xrate},
                         &m);
    metric_family_append(&fam_ops, direction_label, write_direction,
                         (value_t){.counter = (counter_t)(stat_disk[i].xfers -
                                                          stat_disk[i].xrate)},
                         &m);

    double xintfrac = ((double)_system_configuration.Xint) /
                      ((double)_system_configuration.Xfrac);

    derive_t read_time_ns = stat_disk[i].rserv * _system_configuration.Xint /
                            _system_configuration.Xfrac;
    derive_t write_time_ns = stat_disk[i].wserv * _system_configuration.Xint /
                             _system_configuration.Xfrac;
    metric_family_append(&fam_ops_time, direction_label, read_direction,
                         (value_t){.derive = read_time_ns / 1000}, &m);
    metric_family_append(&fam_ops_time, direction_label, write_direction,
                         (value_t){.derive = write_time_ns / 1000, &m);

      metric_reset(&m);
  }
/* #endif defined(HAVE_PERFSTAT) */
#elif HAVE_SYSCTL && KERNEL_NETBSD
  int mib[3];
  size_t size, i, nndrive;

  /* figure out number of drives */
  mib[0] = CTL_HW;
  mib[1] = HW_IOSTATS;
  mib[2] = sizeof(struct io_sysctl);
  if (sysctl(mib, 3, NULL, &size, NULL, 0) == -1) {
    ERROR("disk plugin: sysctl for ndrives failed");
    return -1;
  }
  nndrive = size / sizeof(struct io_sysctl);

  if (size == 0) {
    ERROR("disk plugin: no drives found");
    return -1;
  }
  /* number of drives changed, reallocate buffer */
  if (nndrive != ndrive) {
    drives = (struct io_sysctl *)realloc(drives, size);
    if (drives == NULL) {
      ERROR("disk plugin: memory allocation failure");
      return -1;
    }
    ndrive = nndrive;
  }

  /* get stats for all drives */
  mib[0] = CTL_HW;
  mib[1] = HW_IOSTATS;
  mib[2] = sizeof(struct io_sysctl);
  if (sysctl(mib, 3, drives, &size, NULL, 0) == -1) {
    ERROR("disk plugin: sysctl for drive stats failed");
    return -1;
  }

  for (i = 0; i < ndrive; i++) {
    if (drives[i].type != IOSTAT_DISK)
      continue;
    if (ignorelist_match(ignorelist, drives[i].name))
      continue;

    metric_t m = {0};
    metric_label_set(&m, device_label, drives[i].name);

    metric_family_append(&fam_io, direction_label, read_direction,
                         (value_t){.counter = drives[i].rbytes}, &m);
    metric_family_append(&fam_io, direction_label, write_direction,
                         (value_t){.counter = drives[i].wbytes}, &m);

    metric_family_append(&fam_ops, direction_label, read_direction,
                         (value_t){.counter = drives[i].rxfer}, &m);
    metric_family_append(&fam_ops, direction_label, write_direction,
                         (value_t){.counter = drives[i].wxfer}, &m);

    m.value.derive = ((derive_t)drives[i].time_sec * 1000000) +
                     ((derive_t)drives[i].time_usec);
    metric_family_metric_append(&fam_disk_io_time, m);

    metric_reset(&m);
  }
#endif /* HAVE_SYSCTL && KERNEL_NETBSD */

  for (size_t i = 0; fams[i] != NULL; i++) {
    if (fams[i]->metric.num == 0) {
      continue;
    }

    int status = plugin_dispatch_metric_family(fams[i]);
    if (status != 0) {
      ERROR("disk: plugin_dispatch_metric_family failed: %s", STRERROR(status));
    }
    metric_family_metric_reset(fams[i]);
  }

  return 0;
} /* int disk_read */

void module_register(void) {
  plugin_register_config("disk", disk_config, config_keys, config_keys_num);
  plugin_register_init("disk", disk_init);
  plugin_register_shutdown("disk", disk_shutdown);
  plugin_register_read("disk", disk_read);
} /* void module_register */
