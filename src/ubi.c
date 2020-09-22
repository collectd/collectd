/**
 * collectd - src/ubi.c
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
 */

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

#define PLUGIN_NAME "ubi"

#define SYS_PATH "/sys/class/ubi/"

/*
 * Device attributes
 */
#define DEV_BAD_COUNT                                                          \
  "bad_peb_count" // Count of bad physical eraseblocks on the underlying MTD
                  // device.
#define MAXIMUM_ERASE "max_ec" // Current maximum erase counter value

/*
 * The config key strings
 */
#define DEVICE_KEY "Device"
#define IGNORE_KEY "IgnoreSelected"

static const char *config_keys[] = {
    DEVICE_KEY,
    IGNORE_KEY,
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist = NULL;

/*
 * Private functions
 */
static int ubi_config(const char *key, const char *value) {
  if (ignorelist == NULL &&
      (ignorelist = ignorelist_create(/* invert = */ 1)) == NULL)
    return -1;

  if (strcasecmp(key, DEVICE_KEY) == 0)
    ignorelist_add(ignorelist, value);
  else if (strcasecmp(key, IGNORE_KEY) == 0)
    ignorelist_set_invert(ignorelist, IS_TRUE(value) ? 0 : 1);
  else
    return -1;

  return 0;
} /* int ubi_config */

static void ubi_submit(const char *dev_name, const char *type, gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  if (ignorelist_match(ignorelist, dev_name) != 0)
    return;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, PLUGIN_NAME, sizeof(vl.plugin));
  sstrncpy(vl.type_instance, dev_name, sizeof(vl.type_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  plugin_dispatch_values(&vl);
} /* void ubi_submit */

static int ubi_read_dev_attr(const char *dev_name, const char *attr) {
  FILE *f;
  int val;
  char
      str[sizeof(SYS_PATH) + strlen(dev_name) + sizeof("/") + strlen(attr) + 1];
  int n;

  snprintf(str, sizeof(str), SYS_PATH "%s/%s", dev_name, attr);

  if ((f = fopen(str, "r")) == NULL) {
    ERROR(PLUGIN_NAME ": cannot open [%s]", str);
    return -1;
  }

  n = fscanf(f, "%d", &val);
  fclose(f);

  if (n != 1) {
    ERROR(PLUGIN_NAME " : did not find an integer in %s", str);
    return -1;
  }

  ubi_submit(dev_name, attr, (gauge_t)val);

  return 0;
} /* int ubi_read_dev_attr */

static inline int ubi_read_dev_bad_count(const char *dev_name) {
  return ubi_read_dev_attr(dev_name, DEV_BAD_COUNT);
} /* int ubi_read_dev_bad_count */

static inline int ubi_read_max_ec(const char *dev_name) {
  return ubi_read_dev_attr(dev_name, MAXIMUM_ERASE);
} /* int ubi_read_max_ec */

static int ubi_read(void) {
  DIR *dir;
  struct dirent *dirent;

  if ((dir = opendir(SYS_PATH)) == NULL) {
    ERROR(PLUGIN_NAME " : cannot open dir " SYS_PATH);
    return -1;
  }

  while ((dirent = readdir(dir)) != NULL) {
    if (ignorelist_match(ignorelist, dirent->d_name))
      continue;

    ubi_read_dev_bad_count(dirent->d_name);
    ubi_read_max_ec(dirent->d_name);
  }

  closedir(dir);

  return 0;
} /* int ubi_read */

void module_register(void) {
  plugin_register_config(PLUGIN_NAME, ubi_config, config_keys, config_keys_num);
  plugin_register_read(PLUGIN_NAME, ubi_read);
} /* void module_register */
