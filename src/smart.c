/**
 * collectd - src/smart.c
 * Copyright (C) 2014       Vincent Bernat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Vincent Bernat <vbe at exoscale.ch>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#include <atasmart.h>
#include <libudev.h>

#ifdef HAVE_SYS_CAPABILITY_H
#include <sys/capability.h>
#endif

static const char *config_keys[] = {"Disk", "IgnoreSelected", "IgnoreSleepMode",
                                    "UseSerial"};

static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist;
static int ignore_sleep_mode;
static int use_serial;

static int smart_config(const char *key, const char *value) {
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
  } else if (strcasecmp("IgnoreSleepMode", key) == 0) {
    if (IS_TRUE(value))
      ignore_sleep_mode = 1;
  } else if (strcasecmp("UseSerial", key) == 0) {
    if (IS_TRUE(value))
      use_serial = 1;
  } else {
    return -1;
  }

  return 0;
} /* int smart_config */

static void smart_submit(const char *dev, const char *type,
                         const char *type_inst, double value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "smart", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void handle_attribute(SkDisk *d, const SkSmartAttributeParsedData *a,
                             void *userdata) {
  char const *name = userdata;

  if (!a->current_value_valid || !a->worst_value_valid)
    return;

  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {
      {.gauge = a->current_value},
      {.gauge = a->worst_value},
      {.gauge = a->threshold_valid ? a->threshold : 0},
      {.gauge = a->pretty_value},
  };

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.plugin, "smart", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, name, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "smart_attribute", sizeof(vl.type));
  sstrncpy(vl.type_instance, a->name, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);

  if (a->threshold_valid && a->current_value <= a->threshold) {
    notification_t notif = {NOTIF_WARNING,     cdtime(), "",  "", "smart", "",
                            "smart_attribute", "",       NULL};
    sstrncpy(notif.host, hostname_g, sizeof(notif.host));
    sstrncpy(notif.plugin_instance, name, sizeof(notif.plugin_instance));
    sstrncpy(notif.type_instance, a->name, sizeof(notif.type_instance));
    snprintf(notif.message, sizeof(notif.message),
             "attribute %s is below allowed threshold (%d < %d)", a->name,
             a->current_value, a->threshold);
    plugin_dispatch_notification(&notif);
  }
}

static void smart_read_disk(SkDisk *d, char const *name) {
  SkBool available = FALSE;
  if (sk_disk_identify_is_available(d, &available) < 0 || !available) {
    DEBUG("smart plugin: disk %s cannot be identified.", name);
    return;
  }
  if (sk_disk_smart_is_available(d, &available) < 0 || !available) {
    DEBUG("smart plugin: disk %s has no SMART support.", name);
    return;
  }
  if (!ignore_sleep_mode) {
    SkBool awake = FALSE;
    if (sk_disk_check_sleep_mode(d, &awake) < 0 || !awake) {
      DEBUG("smart plugin: disk %s is sleeping.", name);
      return;
    }
  }
  if (sk_disk_smart_read_data(d) < 0) {
    ERROR("smart plugin: unable to get SMART data for disk %s.", name);
    return;
  }

  if (sk_disk_smart_parse(d, &(SkSmartParsedData const *){NULL}) < 0) {
    ERROR("smart plugin: unable to parse SMART data for disk %s.", name);
    return;
  }

  /* Get some specific values */
  uint64_t value;
  if (sk_disk_smart_get_power_on(d, &value) >= 0)
    smart_submit(name, "smart_poweron", "", ((gauge_t)value) / 1000.);
  else
    DEBUG("smart plugin: unable to get milliseconds since power on for %s.",
          name);

  if (sk_disk_smart_get_power_cycle(d, &value) >= 0)
    smart_submit(name, "smart_powercycles", "", (gauge_t)value);
  else
    DEBUG("smart plugin: unable to get number of power cycles for %s.", name);

  if (sk_disk_smart_get_bad(d, &value) >= 0)
    smart_submit(name, "smart_badsectors", "", (gauge_t)value);
  else
    DEBUG("smart plugin: unable to get number of bad sectors for %s.", name);

  if (sk_disk_smart_get_temperature(d, &value) >= 0)
    smart_submit(name, "smart_temperature", "",
                 ((gauge_t)value) / 1000. - 273.15);
  else
    DEBUG("smart plugin: unable to get temperature for %s.", name);

  /* Grab all attributes */
  if (sk_disk_smart_parse_attributes(d, handle_attribute, (void *)name) < 0) {
    ERROR("smart plugin: unable to handle SMART attributes for %s.", name);
  }
}

static void smart_handle_disk(const char *dev, const char *serial) {
  SkDisk *d = NULL;
  const char *name;

  if (use_serial && serial) {
    name = serial;
  } else {
    name = strrchr(dev, '/');
    if (!name)
      return;
    name++;
  }
  if (ignorelist_match(ignorelist, name) != 0) {
    DEBUG("smart plugin: ignoring %s.", dev);
    return;
  }

  DEBUG("smart plugin: checking SMART status of %s.", dev);
  if (sk_disk_open(dev, &d) < 0) {
    ERROR("smart plugin: unable to open %s.", dev);
    return;
  }

  smart_read_disk(d, name);
  sk_disk_free(d);
}

static int smart_read(void) {
  struct udev *handle_udev;
  struct udev_enumerate *enumerate;
  struct udev_list_entry *devices, *dev_list_entry;
  struct udev_device *dev;

  /* Use udev to get a list of disks */
  handle_udev = udev_new();
  if (!handle_udev) {
    ERROR("smart plugin: unable to initialize udev.");
    return -1;
  }
  enumerate = udev_enumerate_new(handle_udev);
  udev_enumerate_add_match_subsystem(enumerate, "block");
  udev_enumerate_add_match_property(enumerate, "DEVTYPE", "disk");
  udev_enumerate_scan_devices(enumerate);
  devices = udev_enumerate_get_list_entry(enumerate);
  udev_list_entry_foreach(dev_list_entry, devices) {
    const char *path, *devpath, *serial;
    path = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(handle_udev, path);
    devpath = udev_device_get_devnode(dev);
    serial = udev_device_get_property_value(dev, "ID_SERIAL");

    /* Query status with libatasmart */
    smart_handle_disk(devpath, serial);
    udev_device_unref(dev);
  }

  udev_enumerate_unref(enumerate);
  udev_unref(handle_udev);

  return 0;
} /* int smart_read */

static int smart_init(void) {
#if defined(HAVE_SYS_CAPABILITY_H) && defined(CAP_SYS_RAWIO)
  if (check_capability(CAP_SYS_RAWIO) != 0) {
    if (getuid() == 0)
      WARNING("smart plugin: Running collectd as root, but the "
              "CAP_SYS_RAWIO capability is missing. The plugin's read "
              "function will probably fail. Is your init system dropping "
              "capabilities?");
    else
      WARNING("smart plugin: collectd doesn't have the CAP_SYS_RAWIO "
              "capability. If you don't want to run collectd as root, try "
              "running \"setcap cap_sys_rawio=ep\" on the collectd binary.");
  }
#endif
  return 0;
} /* int smart_init */

void module_register(void) {
  plugin_register_config("smart", smart_config, config_keys, config_keys_num);
  plugin_register_init("smart", smart_init);
  plugin_register_read("smart", smart_read);
} /* void module_register */
