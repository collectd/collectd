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

#define PLUGIN_NAME "mmc"
#define SYS_PATH "/sys/bus/mmc/devices/"

#define DEVICE_KEY "Device"
#define IGNORE_KEY "IgnoreSelected"

static const char *config_keys[] = {
    DEVICE_KEY,
    IGNORE_KEY,
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

#define MMC_MANUFACTOR "manfid"
#define MMC_OEM_ID "oemid"
#define MMC_SSR "ssr"
#define MMC_LIFE_TIME "life_time"
#define MMC_PRE_EOL_INFO "pre_eol_info"

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

static int mmc_read_dev_attr(const char *dev_name, const char *file_name,
                             char *buffer, int size) {
  FILE *fh;
  char str[sizeof(SYS_PATH) + strlen(dev_name) + sizeof("/") +
           strlen(file_name) + 1];
  int length;

  snprintf(str, sizeof(str), SYS_PATH "%s/%s", dev_name, file_name);
  fh = fopen(str, "r");

  if (fh == NULL) {
    ERROR(PLUGIN_NAME "(%s): Cannot open file [%s]", dev_name, str);
    return EXIT_FAILURE;
  }

  DEBUG(PLUGIN_NAME "(%s): try to read [%s]", dev_name, str);
  if (fgets(buffer, size, fh) == NULL) {
    ERROR(PLUGIN_NAME "(%s): Unable to read file [%s] (%s)", dev_name, str,
          STRERRNO);
    fclose(fh);
    return EXIT_FAILURE;
  }
  fclose(fh);

  /* Remove trailing whitespace for sysfs attr read */
  length = strlen(buffer);
  DEBUG(PLUGIN_NAME "(%s): Read %d characters [%s]", dev_name, length, str);
  if (buffer > 0)
    buffer[length - 1] = '\0';

  return 0;
}

static int mmc_read_manfid(const char *dev_name, int *value) {
  char buffer[4096];

  if (mmc_read_dev_attr(dev_name, MMC_MANUFACTOR, buffer, sizeof(buffer)) ==
      0) {
    *value = (int)strtol(buffer, NULL, 0);
    DEBUG(PLUGIN_NAME "(%s): [%s]=%s (%d)", dev_name, MMC_MANUFACTOR, buffer,
          *value);
    return 0;
  }

  WARNING(PLUGIN_NAME "(%s): Unable to read manufacturer identifier (manfid)",
          dev_name);
  return EXIT_FAILURE;
}

static int mmc_read_oemid(const char *dev_name, int *value) {
  char buffer[4096];

  if (mmc_read_dev_attr(dev_name, MMC_OEM_ID, buffer, sizeof(buffer)) == 0) {
    *value = (int)strtol(buffer, NULL, 0);
    DEBUG(PLUGIN_NAME "(%s): [%s]=%s (%d)", dev_name, MMC_OEM_ID, buffer,
          *value);
    return 0;
  }

  WARNING(
      PLUGIN_NAME
      "(%s): Unable to read original equipment manufacturer identifier (oemid)",
      dev_name);
  return EXIT_FAILURE;
}

#define MMC_POWER_CYCLES "mmc_power_cycles"
#define MMC_BLOCK_ERASES "mmc_block_erases"
#define MMC_BAD_BLOCKS "mmc_bad_blocks"
#define MMC_LTE_A "mmc_life_time_est_typ_a"
#define MMC_LTE_B "mmc_life_time_est_typ_b"
#define MMC_EOL_INFO "mmc_pre_eol_info"

static int mmc_read_emmc_generic(const char *dev_name) {
  char buffer[4096];
  uint8_t life_time_a, life_time_b;
  uint8_t pre_eol;
  int res = EXIT_FAILURE;

  /* write generic eMMC 5.0 lifetime estimates / health reports */
  if (mmc_read_dev_attr(dev_name, MMC_LIFE_TIME, buffer, sizeof(buffer)) == 0) {
    if (sscanf(buffer, "%hhx %hhx", &life_time_a, &life_time_b) == 2) {
      mmc_submit(dev_name, MMC_LTE_A, (gauge_t)life_time_a);
      mmc_submit(dev_name, MMC_LTE_B, (gauge_t)life_time_b);
      res = EXIT_SUCCESS;
    }
  }

  if (mmc_read_dev_attr(dev_name, MMC_PRE_EOL_INFO, buffer, sizeof(buffer)) ==
      0) {
    if (sscanf(buffer, "%hhx", &pre_eol) == 1) {
      mmc_submit(dev_name, MMC_EOL_INFO, (gauge_t)pre_eol);
      res = EXIT_SUCCESS;
    }
  }

  return res;
}

enum mmc_manfid {
  MANUFACTUR_SWISSBIT = 93, // 0x5d
};

enum mmc_oemid_swissbit {
  OEMID_SWISSBIT_1 = 21314, // 0x5342
};

// Size of string buffer with '\0'
#define SWISSBIT_LENGTH_SPARE_BLOCKS 3
#define SWISSBIT_LENGTH_BLOCK_ERASES 13
#define SWISSBIT_LENGTH_POWER_ON 9

#define SWISSBIT_SSR_START_SPARE_BLOCKS 66
#define SWISSBIT_SSR_START_BLOCK_ERASES 92
#define SWISSBIT_SSR_START_POWER_ON 112

static int mmc_read_ssr_swissbit(const char *dev_name) {
  char buffer[4096];
  int oemid;
  int value;
  int length;
  char bad_blocks[SWISSBIT_LENGTH_SPARE_BLOCKS];
  char block_erases[SWISSBIT_LENGTH_BLOCK_ERASES];
  char power_on[SWISSBIT_LENGTH_POWER_ON];

  if (mmc_read_oemid(dev_name, &oemid) != 0) {
    return EXIT_FAILURE;
  }

  if (oemid != OEMID_SWISSBIT_1) {
    INFO(PLUGIN_NAME
         "(%s): The mmc device is not suppored by this plugin (oemid: 0x%x)",
         dev_name, oemid);
    return EXIT_FAILURE;
  }

  if (mmc_read_dev_attr(dev_name, MMC_SSR, buffer, sizeof(buffer)) != 0) {
    return EXIT_FAILURE;
  }

  /*
   * Since the register is read out as a byte stream, it is 128 bytes long.
   * One char represents a half byte (nibble).
   *
   */
  length = strlen(buffer);
  DEBUG(PLUGIN_NAME ": %d byte read from SSR register", length);
  if (length < 128) {
    INFO(PLUGIN_NAME "(%s): The SSR register is not 128 byte long", dev_name);
    return EXIT_FAILURE;
  }
  DEBUG(PLUGIN_NAME "(%s): [%s]=%s", dev_name, MMC_SSR, buffer);

  /* write MMC_BAD_BLOCKS */
  sstrncpy(bad_blocks, &buffer[SWISSBIT_SSR_START_SPARE_BLOCKS],
           sizeof(bad_blocks) - 1);
  bad_blocks[sizeof(bad_blocks) - 1] = '\0';
  value = (int)strtol(bad_blocks, NULL, 16);
  /* convert to more common bad blocks information */
  value = abs(value - 100);
  DEBUG(PLUGIN_NAME "(%s): [bad_blocks] str=%s int=%d", dev_name, bad_blocks,
        value);
  mmc_submit(dev_name, MMC_BAD_BLOCKS, (gauge_t)value);

  /* write MMC_BLOCK_ERASES */
  sstrncpy(block_erases, &buffer[SWISSBIT_SSR_START_BLOCK_ERASES],
           sizeof(block_erases) - 1);
  block_erases[sizeof(block_erases) - 1] = '\0';
  value = (int)strtol(block_erases, NULL, 16);
  DEBUG(PLUGIN_NAME "(%s): [block_erases] str=%s int=%d", dev_name,
        block_erases, value);
  mmc_submit(dev_name, MMC_BLOCK_ERASES, (gauge_t)value);

  /* write MMC_POWER_CYCLES */
  sstrncpy(power_on, &buffer[SWISSBIT_SSR_START_POWER_ON],
           sizeof(power_on) - 1);
  power_on[sizeof(power_on) - 1] = '\0';
  value = (int)strtol(power_on, NULL, 16);
  DEBUG(PLUGIN_NAME "(%s): [power_on] str=%s int=%d", dev_name, power_on,
        value);
  mmc_submit(dev_name, MMC_POWER_CYCLES, (gauge_t)value);

  return 0;
}

static int mmc_read(void) {
  DIR *dir;
  struct dirent *dirent;
  int manfid;
  bool have_stats;

  if ((dir = opendir(SYS_PATH)) == NULL) {
    ERROR(PLUGIN_NAME ": Cannot open directory [%s]", SYS_PATH);
    return -1;
  }

  while ((dirent = readdir(dir)) != NULL) {
    have_stats = false;

    if (dirent->d_name[0] == '.')
      continue;

    if (ignorelist_match(ignorelist, dirent->d_name))
      continue;

    if (mmc_read_manfid(dirent->d_name, &manfid) != 0)
      continue;

    DEBUG(PLUGIN_NAME "(%s): manfid=%d", dirent->d_name, manfid);

    if (mmc_read_emmc_generic(dirent->d_name) == EXIT_SUCCESS)
      have_stats = true;

    switch (manfid) {
    case MANUFACTUR_SWISSBIT:
      mmc_read_ssr_swissbit(dirent->d_name);
      have_stats = true;
      break;
    }

    if (!have_stats) {
      INFO(PLUGIN_NAME "(%s): Could not collect any info for manufactur id %d",
           dirent->d_name, manfid);
    }
  }

  closedir(dir);

  return 0;
}

void module_register(void) {
  plugin_register_config(PLUGIN_NAME, mmc_config, config_keys, config_keys_num);
  plugin_register_read(PLUGIN_NAME, mmc_read);
} /* void module_register */
