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
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#include <atasmart.h>
#include <libudev.h>

static const char *config_keys[] =
{
  "Disk",
  "IgnoreSelected"
};

static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *ignorelist = NULL;

static int smart_config (const char *key, const char *value)
{
  if (ignorelist == NULL)
    ignorelist = ignorelist_create (/* invert = */ 1);
  if (ignorelist == NULL)
    return (1);

  if (strcasecmp ("Disk", key) == 0)
  {
    ignorelist_add (ignorelist, value);
  }
  else if (strcasecmp ("IgnoreSelected", key) == 0)
  {
    int invert = 1;
    if (IS_TRUE (value))
      invert = 0;
    ignorelist_set_invert (ignorelist, invert);
  }
  else
  {
    return (-1);
  }

  return (0);
} /* int smart_config */

static void smart_submit (const char *dev, char *type, char *type_inst, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "smart", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, dev, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_inst, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static void smart_handle_disk_attribute(SkDisk *d, const SkSmartAttributeParsedData *a,
                                        void* userdata)
{
  const char *dev = userdata;
  value_t values[4];
  value_list_t vl = VALUE_LIST_INIT;

  if (!a->current_value_valid || !a->worst_value_valid) return;
  values[0].gauge = a->current_value;
  values[1].gauge = a->worst_value;
  values[2].gauge = a->threshold_valid?a->threshold:0;
  values[3].gauge = a->pretty_value;

  vl.values = values;
  vl.values_len = 4;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "smart", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, dev, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, "smart_attribute", sizeof (vl.type));
  sstrncpy (vl.type_instance, a->name, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);

  if (a->threshold_valid && a->current_value <= a->threshold)
  {
    notification_t notif = { NOTIF_WARNING,
                             cdtime (),
                             "",
                             "",
                             "smart", "",
                             "smart_attribute",
                             "",
                             NULL };
    sstrncpy (notif.host, hostname_g, sizeof (notif.host));
    sstrncpy (notif.plugin_instance, dev, sizeof (notif.plugin_instance));
    sstrncpy (notif.type_instance, a->name, sizeof (notif.type_instance));
    ssnprintf (notif.message, sizeof (notif.message),
               "attribute %s is below allowed threshold (%d < %d)",
               a->name, a->current_value, a->threshold);
    plugin_dispatch_notification (&notif);
  }
}

static void smart_handle_disk (const char *dev)
{
  SkDisk *d = NULL;
  SkBool awake = FALSE;
  SkBool available = FALSE;
  const char *shortname;
  const SkSmartParsedData *spd;
  uint64_t poweron, powercycles, badsectors, temperature;

  shortname = strrchr(dev, '/');
  if (!shortname) return;
  shortname++;
  if (ignorelist_match (ignorelist, shortname) != 0) {
    DEBUG ("smart plugin: ignoring %s.", dev);
    return;
  }

  DEBUG ("smart plugin: checking SMART status of %s.",
         dev);

  if (sk_disk_open (dev, &d) < 0)
  {
    ERROR ("smart plugin: unable to open %s.", dev);
    return;
  }
  if (sk_disk_identify_is_available (d, &available) < 0 || !available)
  {
    DEBUG ("smart plugin: disk %s cannot be identified.", dev);
    goto end;
  }
  if (sk_disk_smart_is_available (d, &available) < 0 || !available)
  {
    DEBUG ("smart plugin: disk %s has no SMART support.", dev);
    goto end;
  }
  if (sk_disk_check_sleep_mode (d, &awake) < 0 || !awake)
  {
    DEBUG ("smart plugin: disk %s is sleeping.", dev);
    goto end;
  }
  if (sk_disk_smart_read_data (d) < 0)
  {
    ERROR ("smart plugin: unable to get SMART data for disk %s.", dev);
    goto end;
  }
  if (sk_disk_smart_parse (d, &spd) < 0)
  {
    ERROR ("smart plugin: unable to parse SMART data for disk %s.", dev);
    goto end;
  }

  /* Get some specific values */
  if (sk_disk_smart_get_power_on (d, &poweron) < 0)
  {
    WARNING ("smart plugin: unable to get milliseconds since power on for %s.",
             dev);
  }
  else
    smart_submit (shortname, "smart_poweron", "", poweron / 1000.);

  if (sk_disk_smart_get_power_cycle (d, &powercycles) < 0)
  {
    WARNING ("smart plugin: unable to get number of power cycles for %s.",
             dev);
  }
  else
    smart_submit (shortname, "smart_powercycles", "", powercycles);

  if (sk_disk_smart_get_bad (d, &badsectors) < 0)
  {
    WARNING ("smart plugin: unable to get number of bad sectors for %s.",
             dev);
  }
  else
    smart_submit (shortname, "smart_badsectors", "", badsectors);

  if (sk_disk_smart_get_temperature (d, &temperature) < 0)
  {
    WARNING ("smart plugin: unable to get temperature for %s.",
             dev);
  }
  else
    smart_submit (shortname, "smart_temperature", "", temperature / 1000. - 273.15);

  /* Grab all attributes */
  if (sk_disk_smart_parse_attributes(d, smart_handle_disk_attribute,
                                     (char *)shortname) < 0)
  {
    ERROR ("smart plugin: unable to handle SMART attributes for %s.",
           dev);
  }

end:
  sk_disk_free(d);
}

static int smart_read (void)
{
  struct udev *handle_udev;
  struct udev_enumerate *enumerate;
  struct udev_list_entry *devices, *dev_list_entry;
  struct udev_device *dev;

  /* Use udev to get a list of disks */
  handle_udev = udev_new();
  if (!handle_udev)
  {
    ERROR ("smart plugin: unable to initialize udev.");
    return (-1);
  }
  enumerate = udev_enumerate_new (handle_udev);
  udev_enumerate_add_match_subsystem (enumerate, "block");
  udev_enumerate_add_match_property (enumerate, "DEVTYPE", "disk");
  udev_enumerate_scan_devices (enumerate);
  devices = udev_enumerate_get_list_entry (enumerate);
  udev_list_entry_foreach (dev_list_entry, devices)
  {
    const char *path, *devpath;
    path = udev_list_entry_get_name (dev_list_entry);
    dev = udev_device_new_from_syspath (handle_udev, path);
    devpath = udev_device_get_devnode (dev);

    /* Query status with libatasmart */
    smart_handle_disk (devpath);
  }

  udev_enumerate_unref (enumerate);
  udev_unref (handle_udev);

  return (0);
} /* int smart_read */

void module_register (void)
{
  plugin_register_config ("smart", smart_config,
                          config_keys, config_keys_num);
  plugin_register_read ("smart", smart_read);
} /* void module_register */
