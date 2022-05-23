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

#define PLUGIN_NAME "mmc"

#define DEVICE_KEY "Device"
#define IGNORE_KEY "IgnoreSelected"

static const char *config_keys[] = {
    DEVICE_KEY,
    IGNORE_KEY,
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist = NULL;

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

static void mmc_submit(const char *dev_name, const char *type, gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, PLUGIN_NAME, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev_name, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  plugin_dispatch_values(&vl);
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

void module_register(void) {
  plugin_register_config(PLUGIN_NAME, mmc_config, config_keys, config_keys_num);
  plugin_register_read(PLUGIN_NAME, mmc_read);
} /* void module_register */
