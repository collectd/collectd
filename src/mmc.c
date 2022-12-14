/*
 * collectd - src/mmc.c
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
 * Author:
 *   Florian Eckert <fe@dev.tdt.de>
 *
 */

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

#include <libudev.h>
#include <linux/major.h>
#include <linux/mmc/ioctl.h>
#include <sys/ioctl.h>

#define PLUGIN_NAME "mmc"

#define DEVICE_KEY "Device"
#define IGNORE_KEY "IgnoreSelected"

#define MMC_BLOCK_SIZE 512

static const char *config_keys[] = {
    DEVICE_KEY,
    IGNORE_KEY,
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist = NULL;

// Cache of open file descriptors for /dev/mmcblk? block devices.
// The purpose of caching the file descriptors instead of open()/close()ing for
// every read is to prevent the generation of udev events for every close().
typedef struct dev_cache_entry_s {
  char *path;
  int fd;
  struct dev_cache_entry_s *next;
} dev_cache_entry_t;

static pthread_mutex_t block_dev_cache_lock = PTHREAD_MUTEX_INITIALIZER;
static dev_cache_entry_t *block_dev_cache = NULL;

static int mmc_config(const char *key, const char *value) {
  if (ignorelist == NULL)
    ignorelist = ignorelist_create(1);

  if (ignorelist == NULL) {
    ERROR(PLUGIN_NAME ": Ignorelist_create failed");
    return -ENOMEM;
  }

  if (strcasecmp(key, DEVICE_KEY) == 0) {
    if (ignorelist_add(ignorelist, value)) {
      ERROR(PLUGIN_NAME ": Cannot add value to ignorelist");
      return -1;
    }
  } else if (strcasecmp(key, IGNORE_KEY) == 0)
    ignorelist_set_invert(ignorelist, IS_TRUE(value) ? 0 : 1);
  else {
    ERROR(PLUGIN_NAME ": Invalid option %s", key);
    return -1;
  }

  return 0;
}

static void mmc_submit(const char *dev_name, char *type, gauge_t value) {
  metric_family_t fam = {
      .name = type,
      .type = METRIC_TYPE_GAUGE,
  };

  metric_t m = {
      .family = &fam,
      .value = (value_t){.gauge = value},
  };

  metric_label_set(&m, "device", dev_name);
  metric_family_metric_append(&fam, m);

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR(PLUGIN_NAME ": plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }

  metric_reset(&m);
  metric_family_metric_reset(&fam);
}

static int mmc_read_manfid(struct udev_device *mmc_dev, int *value) {
  const char *attr = udev_device_get_sysattr_value(mmc_dev, "manfid");

  if (attr == NULL) {
    WARNING(PLUGIN_NAME "(%s): Unable to read manufacturer identifier (manfid)",
            udev_device_get_sysname(mmc_dev));
    return EXIT_FAILURE;
  }

  *value = (int)strtol(attr, NULL, 0);
  return 0;
}

static int mmc_read_oemid(struct udev_device *mmc_dev, int *value) {
  const char *attr = udev_device_get_sysattr_value(mmc_dev, "oemid");

  if (attr == NULL) {
    WARNING(PLUGIN_NAME "(%s): Unable to read original equipment manufacturer "
                        "identifier (oemid)",
            udev_device_get_sysname(mmc_dev));
    return EXIT_FAILURE;
  }

  *value = (int)strtol(attr, NULL, 0);
  return 0;
}
enum mmc_manfid {
  MANUFACTUR_MICRON = 0x13,
  MANUFACTUR_SANDISK = 0x45,
  MANUFACTUR_SWISSBIT = 0x5d,
};

enum mmc_oemid_swissbit {
  OEMID_SWISSBIT_1 = 21314, // 0x5342
};

static int mmc_read_emmc_generic(struct udev_device *mmc_dev) {
  const char *dev_name, *attr_life_time, *attr_pre_eol;
  uint8_t life_time_a, life_time_b, pre_eol;
  int res = EXIT_FAILURE;

  dev_name = udev_device_get_sysname(mmc_dev);
  attr_life_time = udev_device_get_sysattr_value(mmc_dev, "life_time");
  attr_pre_eol = udev_device_get_sysattr_value(mmc_dev, "pre_eol_info");

  // write generic eMMC 5.0 lifetime estimates
  if (attr_life_time != NULL) {
    if (sscanf(attr_life_time, "%hhx %hhx", &life_time_a, &life_time_b) == 2) {
      mmc_submit(dev_name, "mmc_life_time_est_typ_a", (gauge_t)life_time_a);
      mmc_submit(dev_name, "mmc_life_time_est_typ_b", (gauge_t)life_time_b);
      res = EXIT_SUCCESS;
    }
  }

  // write generic eMMC 5.0 pre_eol estimate
  if (attr_pre_eol != NULL) {
    if (sscanf(attr_pre_eol, "%hhx", &pre_eol) == 1) {
      mmc_submit(dev_name, "mmc_pre_eol_info", (gauge_t)pre_eol);
      res = EXIT_SUCCESS;
    }
  }

  return res;
}

// mmc_open_block_dev open file descriptor for device at path "dev_path" or
// return a fd from a previous invocation.
// A caller must hold the block_dev_cache_lock and keep it locked while using
// the returned fd to prevent races between ioctls.
static int mmc_open_block_dev(const char *dev_name, const char *dev_path) {
  if (dev_path == NULL) {
    INFO(PLUGIN_NAME "(%s) failed to find block device", dev_name);
    return -1;
  }

  // Check if we have already opened this block device before.
  // The purpose of this file descriptor caching is to prevent the generation
  // of periodic udev events.
  // Why does udev generate an event whenever a block device opened for
  // writing is closed? Because this usually happens when a device is
  // partitioned or mkfs is called (e.g. an actual change to the dev).
  // This is however not what we do. We only need O_RDWR to send special MMC
  // commands which do not modify the content of the device, so we don't want
  // to generate the events.
  for (dev_cache_entry_t *piv = block_dev_cache; piv != NULL; piv = piv->next) {
    if (strcmp(dev_path, piv->path) == 0) {
      return piv->fd;
    }
  }

  // This dev_path was not opened before. Open it now:
  int block_fd = open(dev_path, O_RDWR);
  if (block_fd < 0) {
    INFO(PLUGIN_NAME "(%s) failed to open block device (%s): (%s)", dev_name,
         dev_path, strerror(errno));
    return -1;
  }

  // And add it to the cache of already openend block devices:
  dev_cache_entry_t *cache_entry = calloc(1, sizeof(*cache_entry));
  if (cache_entry == NULL) {
    ERROR(PLUGIN_NAME "(%s) failed to allocate memory (%s)", dev_name,
          strerror(errno));
    return -1;
  }

  cache_entry->path = strdup(dev_path);
  if (cache_entry->path == NULL) {
    ERROR(PLUGIN_NAME "(%s) failed to copy path string (%s)", dev_name,
          strerror(errno));

    free(cache_entry);
    return -1;
  }

  cache_entry->fd = block_fd;
  cache_entry->next = block_dev_cache;
  block_dev_cache = cache_entry;

  return cache_entry->fd;
}

// mmc_close_block_dev close a file descriptor returned by mmc_open_block_dev.
// A caller must hold the block_dev_cache_lock.
static int mmc_close_block_dev(int fd) {
  for (dev_cache_entry_t **elem_ptr = &block_dev_cache; *elem_ptr != NULL;
       elem_ptr = &(*elem_ptr)->next) {
    dev_cache_entry_t *elem = *elem_ptr;

    if (elem->fd == fd) {
      // Unhook the element from the linked list by overwriting the pointer
      // that pointed to it. This could be &block_dev_cache or the &->next
      // pointer of the previous element.
      *elem_ptr = elem->next;

      free(elem->path);
      free(elem);

      return close(fd);
    }
  }

  return -1;
}

// The flags are what emmcparm uses and translate to (include/linux/mmc/core.h):
// MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE | MMC_CMD_ADTC |
// MMC_RSP_SPI_S1
#define MICRON_CMD56_FLAGS 0x00b5
#define MICRON_CMD56ARG_BAD_BLOCKS 0x11
#define MICRON_CMD56ARG_ERASES_SLC 0x23
#define MICRON_CMD56ARG_ERASES_MLC 0x25

static int mmc_micron_cmd56(int block_fd, uint32_t arg, uint16_t *val1,
                            uint16_t *val2, uint16_t *val3) {
  uint16_t cmd_data[MMC_BLOCK_SIZE / sizeof(uint16_t)];
  struct mmc_ioc_cmd cmd = {
      .opcode = 56,
      .arg = arg,
      .flags = MICRON_CMD56_FLAGS,
      .blksz = sizeof(cmd_data),
      .blocks = 1,
  };

  mmc_ioc_cmd_set_data(cmd, cmd_data);

  if (ioctl(block_fd, MMC_IOC_CMD, &cmd) < 0) {
    return EXIT_FAILURE;
  }

  *val1 = be16toh(cmd_data[0]);
  *val2 = be16toh(cmd_data[1]);
  *val3 = be16toh(cmd_data[2]);

  return EXIT_SUCCESS;
}

static int mmc_read_micron(struct udev_device *mmc_dev,
                           struct udev_device *block_dev) {
  uint16_t bb_initial, bb_runtime, bb_remaining, er_slc_min, er_slc_max,
      er_slc_avg, er_mlc_min, er_mlc_max, er_mlc_avg;
  const char *dev_name, *dev_path;
  int block_fd;
  gauge_t bb_total;

  dev_name = udev_device_get_sysname(mmc_dev);
  dev_path = udev_device_get_devnode(block_dev);

  pthread_mutex_lock(&block_dev_cache_lock);
  block_fd = mmc_open_block_dev(dev_name, dev_path);
  if (block_fd < 0) {
    return EXIT_FAILURE;
  }

  if (mmc_micron_cmd56(block_fd, MICRON_CMD56ARG_BAD_BLOCKS, &bb_initial,
                       &bb_runtime, &bb_remaining) != EXIT_SUCCESS) {
    INFO(PLUGIN_NAME "(%s) failed to send ioctl to %s: %s", dev_name, dev_path,
         strerror(errno));
    mmc_close_block_dev(block_fd);
    pthread_mutex_unlock(&block_dev_cache_lock);
    return EXIT_FAILURE;
  }

  if (mmc_micron_cmd56(block_fd, MICRON_CMD56ARG_ERASES_SLC, &er_slc_min,
                       &er_slc_max, &er_slc_avg) != EXIT_SUCCESS) {
    INFO(PLUGIN_NAME "(%s) failed to send ioctl to %s: %s", dev_name, dev_path,
         strerror(errno));
    mmc_close_block_dev(block_fd);
    pthread_mutex_unlock(&block_dev_cache_lock);
    return EXIT_FAILURE;
  }

  if (mmc_micron_cmd56(block_fd, MICRON_CMD56ARG_ERASES_MLC, &er_mlc_min,
                       &er_mlc_max, &er_mlc_avg) != EXIT_SUCCESS) {
    INFO(PLUGIN_NAME "(%s) failed to send ioctl to %s: %s", dev_name, dev_path,
         strerror(errno));
    mmc_close_block_dev(block_fd);
    pthread_mutex_unlock(&block_dev_cache_lock);
    return EXIT_FAILURE;
  }

  pthread_mutex_unlock(&block_dev_cache_lock);

  bb_total = (gauge_t)(bb_initial) + (gauge_t)(bb_runtime);

  mmc_submit(dev_name, "mmc_bad_blocks", bb_total);
  mmc_submit(dev_name, "mmc_spare_blocks", (gauge_t)(bb_remaining));

  mmc_submit(dev_name, "mmc_erases_slc_min", (gauge_t)(er_slc_min));
  mmc_submit(dev_name, "mmc_erases_slc_max", (gauge_t)(er_slc_max));
  mmc_submit(dev_name, "mmc_erases_slc_avg", (gauge_t)(er_slc_avg));

  mmc_submit(dev_name, "mmc_erases_mlc_min", (gauge_t)(er_mlc_min));
  mmc_submit(dev_name, "mmc_erases_mlc_max", (gauge_t)(er_mlc_max));
  mmc_submit(dev_name, "mmc_erases_mlc_avg", (gauge_t)(er_mlc_avg));

  return EXIT_SUCCESS;
}

// Copy what worked for the micron eMMC but allow busy response in report mode
// enable flags (MMC_RSP_PRESENT | MMC_RSP_CRC | MMC_RSP_OPCODE | MMC_CMD_ADTC |
// MMC_RSP_BUSY | MMC_RSP_SPI_S1 | MMC_RSP_SPI_BUSY) The _ARG is a magic value
// from the datasheet
#define SANDISK_CMD_EN_REPORT_MODE_FLAGS 0x04bd
#define SANDISK_CMD_EN_REPORT_MODE_OP 62
#define SANDIKS_CMD_EN_REPORT_MODE_ARG 0x96C9D71C

#define SANDISK_CMD_READ_REPORT_FLAGS 0x00b5
#define SANDISK_CMD_READ_REPORT_OP 63
#define SANDISK_CMD_READ_REPORT_ARG 0

// Fields in the Device Report / Advanced Health Status structure
#define SANDISK_FIELDS_POWER_UPS 25
#define SANDISK_FIELDS_TEMP_CUR 41

#define SANDISK_FIELDS_BB_INITIAL 6
#define SANDISK_FIELDS_BB_RUNTIME_MLC 9
#define SANDISK_FIELDS_BB_RUNTIME_SLC 36
#define SANDISK_FIELDS_BB_RUNTIME_SYS 7

#define SANDISK_FIELDS_ER_MLC_AVG 2
#define SANDISK_FIELDS_ER_MLC_MIN 31
#define SANDISK_FIELDS_ER_MLC_MAX 28

#define SANDISK_FIELDS_ER_SLC_AVG 34
#define SANDISK_FIELDS_ER_SLC_MIN 33
#define SANDISK_FIELDS_ER_SLC_MAX 32

#define SANDISK_FIELDS_ER_SYS_AVG 0
#define SANDISK_FIELDS_ER_SYS_MIN 29
#define SANDISK_FIELDS_ER_SYS_MAX 26

static int mmc_read_sandisk(struct udev_device *mmc_dev,
                            struct udev_device *block_dev) {
  uint32_t cmd_data[MMC_BLOCK_SIZE / sizeof(uint32_t)];

  struct mmc_ioc_cmd cmd_en_report_mode = {
      .opcode = SANDISK_CMD_EN_REPORT_MODE_OP,
      .arg = SANDIKS_CMD_EN_REPORT_MODE_ARG,
      .flags = SANDISK_CMD_EN_REPORT_MODE_FLAGS,
  };
  struct mmc_ioc_cmd cmd_read_report = {
      .opcode = SANDISK_CMD_READ_REPORT_OP,
      .arg = SANDISK_CMD_READ_REPORT_ARG,
      .flags = SANDISK_CMD_READ_REPORT_FLAGS,
      .blksz = sizeof(cmd_data),
      .blocks = 1,
  };

  const char *dev_name = udev_device_get_sysname(mmc_dev);
  const char *dev_path = udev_device_get_devnode(block_dev);

  pthread_mutex_lock(&block_dev_cache_lock);
  int block_fd = mmc_open_block_dev(dev_name, dev_path);
  if (block_fd < 0) {
    return EXIT_FAILURE;
  }

  mmc_ioc_cmd_set_data(cmd_read_report, cmd_data);

  if (ioctl(block_fd, MMC_IOC_CMD, &cmd_en_report_mode) < 0) {
    mmc_close_block_dev(block_fd);
    pthread_mutex_unlock(&block_dev_cache_lock);
    INFO(PLUGIN_NAME
         "(%s) failed to send enable report mode MMC ioctl to %s: %s",
         dev_name, dev_path, strerror(errno));
    return EXIT_FAILURE;
  }

  if (ioctl(block_fd, MMC_IOC_CMD, &cmd_read_report) < 0) {
    mmc_close_block_dev(block_fd);
    pthread_mutex_unlock(&block_dev_cache_lock);
    INFO(PLUGIN_NAME "(%s) failed to send read_report MMC ioctl to %s: %s",
         dev_name, dev_path, strerror(errno));
    return EXIT_FAILURE;
  }

  pthread_mutex_unlock(&block_dev_cache_lock);

  gauge_t bb_total = le32toh(cmd_data[SANDISK_FIELDS_BB_INITIAL]) +
                     le32toh(cmd_data[SANDISK_FIELDS_BB_RUNTIME_MLC]) +
                     le32toh(cmd_data[SANDISK_FIELDS_BB_RUNTIME_SLC]) +
                     le32toh(cmd_data[SANDISK_FIELDS_BB_RUNTIME_SYS]);

  mmc_submit(dev_name, "mmc_bad_blocks", bb_total);

  mmc_submit(dev_name, "mmc_power_cycles",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_POWER_UPS]));
  mmc_submit(dev_name, "temperature",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_TEMP_CUR]));

  mmc_submit(dev_name, "mmc_erases_mlc_avg",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_ER_MLC_AVG]));
  mmc_submit(dev_name, "mmc_erases_mlc_max",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_ER_MLC_MAX]));
  mmc_submit(dev_name, "mmc_erases_mlc_min",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_ER_MLC_MIN]));

  mmc_submit(dev_name, "mmc_erases_slc_avg",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_ER_SLC_AVG]));
  mmc_submit(dev_name, "mmc_erases_slc_max",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_ER_SLC_MAX]));
  mmc_submit(dev_name, "mmc_erases_slc_min",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_ER_SLC_MIN]));

  mmc_submit(dev_name, "mmc_erases_sys_avg",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_ER_SYS_AVG]));
  mmc_submit(dev_name, "mmc_erases_sys_max",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_ER_SYS_MAX]));
  mmc_submit(dev_name, "mmc_erases_sys_min",
             (gauge_t)le32toh(cmd_data[SANDISK_FIELDS_ER_SYS_MIN]));

  return EXIT_SUCCESS;
}

// Size of string buffer with '\0'
#define SWISSBIT_LENGTH_SPARE_BLOCKS 3
#define SWISSBIT_LENGTH_BLOCK_ERASES 13
#define SWISSBIT_LENGTH_POWER_ON 9

#define SWISSBIT_SSR_START_SPARE_BLOCKS 66
#define SWISSBIT_SSR_START_BLOCK_ERASES 92
#define SWISSBIT_SSR_START_POWER_ON 112

static int mmc_read_ssr_swissbit(struct udev_device *mmc_dev) {
  const char *dev_name, *attr;
  int oemid;
  int value;
  int length;
  char bad_blocks[SWISSBIT_LENGTH_SPARE_BLOCKS];
  char block_erases[SWISSBIT_LENGTH_BLOCK_ERASES];
  char power_on[SWISSBIT_LENGTH_POWER_ON];

  dev_name = udev_device_get_sysname(mmc_dev);

  if (mmc_read_oemid(mmc_dev, &oemid) != 0) {
    return EXIT_FAILURE;
  }

  if (oemid != OEMID_SWISSBIT_1) {
    INFO(PLUGIN_NAME
         "(%s): The mmc device is not suppored by this plugin (oemid: 0x%x)",
         dev_name, oemid);
    return EXIT_FAILURE;
  }

  attr = udev_device_get_sysattr_value(mmc_dev, "ssr");

  if (attr == NULL) {
    return EXIT_FAILURE;
  }

  /*
   * Since the register is read out as a byte stream, it is 128 bytes long.
   * One char represents a half byte (nibble).
   *
   */
  length = strlen(attr);
  DEBUG(PLUGIN_NAME ": %d byte read from SSR register", length);
  if (length < 128) {
    INFO(PLUGIN_NAME "(%s): The SSR register is not 128 byte long", dev_name);
    return EXIT_FAILURE;
  }

  DEBUG(PLUGIN_NAME "(%s): [ssr]=%s", dev_name, attr);

  /* write mmc_bad_blocks */
  sstrncpy(bad_blocks, &attr[SWISSBIT_SSR_START_SPARE_BLOCKS],
           sizeof(bad_blocks) - 1);
  bad_blocks[sizeof(bad_blocks) - 1] = '\0';
  value = (int)strtol(bad_blocks, NULL, 16);
  /* convert to more common bad blocks information */
  value = abs(value - 100);
  DEBUG(PLUGIN_NAME "(%s): [bad_blocks] str=%s int=%d", dev_name, bad_blocks,
        value);
  mmc_submit(dev_name, "mmc_bad_blocks", value);

  /* write mmc_block_erases */
  sstrncpy(block_erases, &attr[SWISSBIT_SSR_START_BLOCK_ERASES],
           sizeof(block_erases) - 1);
  block_erases[sizeof(block_erases) - 1] = '\0';
  value = (int)strtol(block_erases, NULL, 16);
  DEBUG(PLUGIN_NAME "(%s): [block_erases] str=%s int=%d", dev_name,
        block_erases, value);
  mmc_submit(dev_name, "mmc_block_erases", (gauge_t)value);

  /* write mmc_power_cycles */
  sstrncpy(power_on, &attr[SWISSBIT_SSR_START_POWER_ON], sizeof(power_on) - 1);
  power_on[sizeof(power_on) - 1] = '\0';
  value = (int)strtol(power_on, NULL, 16);
  DEBUG(PLUGIN_NAME "(%s): [power_on] str=%s int=%d", dev_name, power_on,
        value);
  mmc_submit(dev_name, "mmc_power_cycles", (gauge_t)value);

  return EXIT_SUCCESS;
}

static int mmc_read(void) {
  const char *path, *driver, *dev_name;
  struct udev *handle_udev;
  struct udev_enumerate *enumerate;
  struct udev_list_entry *devices, *dev_list_entry;
  struct udev_device *block_dev, *mmc_dev;
  int manfid;
  bool have_stats;

  handle_udev = udev_new();
  if (!handle_udev) {
    ERROR(PLUGIN_NAME ": unable to initialize udev for device enumeration");
    return -1;
  }

  enumerate = udev_enumerate_new(handle_udev);
  if (enumerate == NULL) {
    ERROR(PLUGIN_NAME ": udev_enumerate_new failed");
    return -1;
  }

  udev_enumerate_add_match_subsystem(enumerate, "block");

  if (udev_enumerate_scan_devices(enumerate) < 0) {
    WARNING(PLUGIN_NAME ": udev scan devices failed");
    return -1;
  }

  devices = udev_enumerate_get_list_entry(enumerate);
  if (devices == NULL) {
    WARNING(PLUGIN_NAME ": udev did not return any block devices");
    return -1;
  }

  // Iterate through all block devices in the system
  udev_list_entry_foreach(dev_list_entry, devices) {
    path = udev_list_entry_get_name(dev_list_entry);
    block_dev = udev_device_new_from_syspath(handle_udev, path);

    // Get the parent of the block device.
    // Note that _get_parent() just gives us its reference to the parent device
    // and does not increment the reference count, so mmc_dev should not be
    // _unrefed.
    mmc_dev = udev_device_get_parent(block_dev);
    if (!mmc_dev) {
      udev_device_unref(block_dev);
      continue;
    }

    // Select only block devices that have a mmcblk device as first parent.
    // This selects e.g. /dev/mmcblk1, but not /dev/mmcblk1p* or
    // /dev/mmcblk1boot* and especially not /dev/sda, /dev/vda ....
    driver = udev_device_get_driver(mmc_dev);
    if (driver == NULL || strcmp(driver, "mmcblk") != 0) {
      udev_device_unref(block_dev);
      continue;
    }

    // Check if Device name (Something like "mmc2:0001") matches an entry in the
    // ignore list
    dev_name = udev_device_get_sysname(mmc_dev);
    if (ignorelist_match(ignorelist, dev_name)) {
      udev_device_unref(block_dev);
      continue;
    }

    // Read generic health metrics that should be available for all eMMC 5.0+
    // devices.
    have_stats = (mmc_read_emmc_generic(mmc_dev) == EXIT_SUCCESS);

    // Read more datailed vendor-specific health info
    if (mmc_read_manfid(mmc_dev, &manfid) == EXIT_SUCCESS) {
      switch (manfid) {
      case MANUFACTUR_MICRON:
        have_stats |= (mmc_read_micron(mmc_dev, block_dev) == EXIT_FAILURE);
        break;
      case MANUFACTUR_SANDISK:
        have_stats |= (mmc_read_sandisk(mmc_dev, block_dev) == EXIT_FAILURE);
        break;
      case MANUFACTUR_SWISSBIT:
        have_stats |= (mmc_read_ssr_swissbit(mmc_dev) == EXIT_SUCCESS);
        break;
      }
    }

    // Print a warning if no info at all could be collected for a device
    if (!have_stats) {
      INFO(PLUGIN_NAME "(%s): Could not collect any info for device", dev_name);
    }

    udev_device_unref(block_dev);
  }

  udev_enumerate_unref(enumerate);
  udev_unref(handle_udev);

  return 0;
} /* int mmc_read */

int mmc_shutdown(void) {
  // Close the file descriptors we have accumulated
  pthread_mutex_lock(&block_dev_cache_lock);
  while (block_dev_cache != NULL) {
    mmc_close_block_dev(block_dev_cache->fd);
  }
  pthread_mutex_unlock(&block_dev_cache_lock);

  return 0;
} /* int mmc_shutdown */

void module_register(void) {
  plugin_register_config(PLUGIN_NAME, mmc_config, config_keys, config_keys_num);
  plugin_register_read(PLUGIN_NAME, mmc_read);
  plugin_register_shutdown(PLUGIN_NAME, mmc_shutdown);
} /* void module_register */
