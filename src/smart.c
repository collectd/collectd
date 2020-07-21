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
 *   Maciej Fijalkowski <maciej.fijalkowski@intel.com>
 *   Bartlomiej Kotlowski <bartlomiej.kotlowski@intel.com>
 *   Slawomir Strehlau <slawomir.strehlau@intel.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#include <atasmart.h>
#include <libudev.h>
#include <sys/ioctl.h>

#include "intel-nvme.h"
#include "nvme.h"

#ifdef HAVE_SYS_CAPABILITY_H
#include <sys/capability.h>
#endif

#define O_RDWR 02
#define NVME_SMART_CDW10 0x00800002
#define SHIFT_BYTE_LEFT 256
struct nvme_admin_cmd {
  __u8 opcode;
  __u8 rsvd1[3];
  __u32 nsid;
  __u8 rsvd2[16];
  __u64 addr;
  __u8 rsvd3[4];
  __u32 data_len;
  __u32 cdw10;
  __u32 cdw11;
  __u8 rsvd4[24];
};

#define NVME_IOCTL_ADMIN_CMD _IOWR('N', 0x41, struct nvme_admin_cmd)

static const char *config_keys[] = {"Disk", "IgnoreSelected", "IgnoreSleepMode",
                                    "UseSerial"};

static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *ignorelist, *ignorelist_by_serial;
static int ignore_sleep_mode;
static int use_serial;
static int invert_ignorelist;

static int smart_config(const char *key, const char *value) {
  if (ignorelist == NULL)
    ignorelist = ignorelist_create(/* invert = */ 1);
  if (ignorelist == NULL)
    return 1;

  if (strcasecmp("Disk", key) == 0) {
    ignorelist_add(ignorelist, value);
  } else if (strcasecmp("IgnoreSelected", key) == 0) {
    invert_ignorelist = 1;
    if (IS_TRUE(value))
      invert_ignorelist = 0;
    ignorelist_set_invert(ignorelist, invert_ignorelist);
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

static int create_ignorelist_by_serial(ignorelist_t *il) {

  struct udev *handle_udev;
  struct udev_enumerate *enumerate;
  struct udev_list_entry *devices, *dev_list_entry;
  struct udev_device *dev;

  if (ignorelist_by_serial == NULL)
    ignorelist_by_serial = ignorelist_create(invert_ignorelist);
  if (ignorelist_by_serial == NULL)
    return 1;

  if (invert_ignorelist == 0) {
    ignorelist_set_invert(ignorelist, 1);
  }

  // Use udev to get a list of disks
  handle_udev = udev_new();
  if (!handle_udev) {
    ERROR("smart plugin: unable to initialize udev.");
    return 1;
  }
  enumerate = udev_enumerate_new(handle_udev);
  if (enumerate == NULL) {
    ERROR("fail udev_enumerate_new");
    return 1;
  }
  udev_enumerate_add_match_subsystem(enumerate, "block");
  udev_enumerate_add_match_property(enumerate, "DEVTYPE", "disk");
  udev_enumerate_scan_devices(enumerate);
  devices = udev_enumerate_get_list_entry(enumerate);
  if (devices == NULL) {
    ERROR("udev returned an empty list deviecs");
    return 1;
  }
  udev_list_entry_foreach(dev_list_entry, devices) {
    const char *path, *devpath, *serial, *name;
    path = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(handle_udev, path);
    devpath = udev_device_get_devnode(dev);
    serial = udev_device_get_property_value(dev, "ID_SERIAL_SHORT");
    name = strrchr(devpath, '/');
    if (name != NULL) {
      if (name[0] == '/')
        name++;

      if (ignorelist_match(ignorelist, name) == 0 && serial != NULL) {
        ignorelist_add(ignorelist_by_serial, serial);
      }
    }
  }

  if (invert_ignorelist == 0) {
    ignorelist_set_invert(ignorelist, 1);
  }
  return 0;
}

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
    ssnprintf(notif.message, sizeof(notif.message),
              "attribute %s is below allowed threshold (%d < %d)", a->name,
              a->current_value, a->threshold);
    plugin_dispatch_notification(&notif);
  }
}

static inline double compute_field(__u8 *data) {
  double sum = 0;
  double add = 0;

  for (int i = 0; i < 16; i++) {
    add = data[15 - i];
    for (int j = i + 1; j < 16; j++) {
      add *= SHIFT_BYTE_LEFT;
    }
    sum += add;
  }
  return sum;
}

static inline double int48_to_double(__u8 *data) {
  double sum = 0;
  double add = 0;

  for (int i = 0; i < 6; i++) {
    add = data[5 - i];
    for (int j = i + 1; j < 6; j++) {
      add *= SHIFT_BYTE_LEFT;
    }
    sum += add;
  }
  return sum;
}

/**
 * There is a bunch of metrics that are 16 bytes long and need to be
 * converted into single double value, so they can be dispatched
 */
#define NVME_METRIC_16B(metric)                                                \
  { "nvme_" #metric, offsetof(union nvme_smart_log, data.metric), "" }

static inline uint16_t le16_to_cpu(__le16 x) {
  return le16toh((__force __u16)x);
}

struct nvme_metric_16b {
  char *label;
  unsigned int offset;
  char *type_inst;
} nvme_metrics[] = {
    NVME_METRIC_16B(data_units_read),    NVME_METRIC_16B(data_units_written),
    NVME_METRIC_16B(host_commands_read), NVME_METRIC_16B(host_commands_written),
    NVME_METRIC_16B(ctrl_busy_time),     NVME_METRIC_16B(power_cycles),
    NVME_METRIC_16B(power_on_hours),     NVME_METRIC_16B(unsafe_shutdowns),
    NVME_METRIC_16B(media_errors),       NVME_METRIC_16B(num_err_log_entries),
};

static void smart_nvme_submit_16b(char const *name, __u8 *raw) {
  int i = 0;

  for (; i < STATIC_ARRAY_SIZE(nvme_metrics); i++) {
    DEBUG("%s : %f", nvme_metrics[i].label,
          compute_field(&raw[nvme_metrics[i].offset]));
    smart_submit(name, nvme_metrics[i].label, nvme_metrics[i].type_inst,
                 compute_field(&raw[nvme_metrics[i].offset]));
  }
}

static int get_vendor_id(const char *dev, char const *name) {

  int fd, err;
  __le16 vid;

  fd = open(dev, O_RDWR);
  if (fd < 0) {
    ERROR("open failed with %s\n", strerror(errno));
    return fd;
  }

  err = ioctl(fd, NVME_IOCTL_ADMIN_CMD,
              &(struct nvme_admin_cmd){.opcode = NVME_ADMIN_IDENTIFY,
                                       .nsid = NVME_NSID_ALL,
                                       .addr = (unsigned long)&vid,
                                       .data_len = sizeof(vid),
                                       .cdw10 = 1,
                                       .cdw11 = 0});

  if (err < 0) {
    ERROR("ioctl for NVME_IOCTL_ADMIN_CMD failed with %s\n", strerror(errno));
    close(fd);
    return err;
  }

  close(fd);
  return (int)le16_to_cpu(vid);
}

static int smart_read_nvme_disk(const char *dev, char const *name) {
  union nvme_smart_log smart_log = {};
  int fd, status;

  fd = open(dev, O_RDWR);
  if (fd < 0) {
    ERROR("open failed with %s\n", strerror(errno));
    return fd;
  }

  /**
   * Prepare Get Log Page command
   * Fill following fields (see NVMe 1.4 spec, section 5.14.1)
   * - Number of DWORDS (bits 27:16) - the struct that will be passed for
   *   filling has 512 bytes which gives 128 (0x80) DWORDS
   * - Log Page Indentifier (bits 7:0) - for SMART the id is 0x02
   */

  status = ioctl(fd, NVME_IOCTL_ADMIN_CMD,
                 &(struct nvme_admin_cmd){.opcode = NVME_ADMIN_GET_LOG_PAGE,
                                          .nsid = NVME_NSID_ALL,
                                          .addr = (unsigned long)&smart_log,
                                          .data_len = sizeof(smart_log),
                                          .cdw10 = NVME_SMART_CDW10});
  if (status < 0) {
    ERROR("ioctl for NVME_IOCTL_ADMIN_CMD failed with %s\n", strerror(errno));
    close(fd);
    return status;
  } else {
    smart_submit(name, "nvme_critical_warning", "",
                 (double)smart_log.data.critical_warning);
    smart_submit(name, "nvme_temperature", "",
                 ((double)(smart_log.data.temperature[1] << 8) +
                  smart_log.data.temperature[0] - 273));
    smart_submit(name, "nvme_avail_spare", "",
                 (double)smart_log.data.avail_spare);
    smart_submit(name, "nvme_avail_spare_thresh", "",
                 (double)smart_log.data.spare_thresh);
    smart_submit(name, "nvme_percent_used", "",
                 (double)smart_log.data.percent_used);
    smart_submit(name, "nvme_endu_grp_crit_warn_sumry", "",
                 (double)smart_log.data.endu_grp_crit_warn_sumry);
    smart_submit(name, "nvme_warning_temp_time", "",
                 (double)smart_log.data.warning_temp_time);
    smart_submit(name, "nvme_critical_comp_time", "",
                 (double)smart_log.data.critical_comp_time);
    smart_submit(name, "nvme_temp_sensor", "sensor_1",
                 (double)smart_log.data.temp_sensor[0] - 273);
    smart_submit(name, "nvme_temp_sensor", "sensor_2",
                 (double)smart_log.data.temp_sensor[1] - 273);
    smart_submit(name, "nvme_temp_sensor", "sensor_3",
                 (double)smart_log.data.temp_sensor[2] - 273);
    smart_submit(name, "nvme_temp_sensor", "sensor_4",
                 (double)smart_log.data.temp_sensor[3] - 273);
    smart_submit(name, "nvme_temp_sensor", "sensor_5",
                 (double)smart_log.data.temp_sensor[4] - 273);
    smart_submit(name, "nvme_temp_sensor", "sensor_6",
                 (double)smart_log.data.temp_sensor[5] - 273);
    smart_submit(name, "nvme_temp_sensor", "sensor_7",
                 (double)smart_log.data.temp_sensor[6] - 273);
    smart_submit(name, "nvme_temp_sensor", "sensor_8",
                 (double)smart_log.data.temp_sensor[7] - 273);
    smart_submit(name, "nvme_thermal_mgmt_temp1_transition_count", "",
                 (double)smart_log.data.thm_temp1_trans_count);
    smart_submit(name, "nvme_thermal_mgmt_temp1_total_time", "",
                 (double)smart_log.data.thm_temp1_total_time);
    smart_submit(name, "nvme_thermal_mgmt_temp2_transition_count", "",
                 (double)smart_log.data.thm_temp2_trans_count);
    smart_submit(name, "nvme_thermal_mgmt_temp2_total_time", "",
                 (double)smart_log.data.thm_temp2_total_time);
    smart_nvme_submit_16b(name, smart_log.raw);
  }

  close(fd);
  return 0;
}

static int smart_read_nvme_intel_disk(const char *dev, char const *name) {

  DEBUG("name = %s", name);
  DEBUG("dev = %s", dev);

  struct nvme_additional_smart_log intel_smart_log;
  int fd, status;
  fd = open(dev, O_RDWR);
  if (fd < 0) {
    ERROR("open failed with %s\n", strerror(errno));
    return fd;
  }

  /**
   * Prepare Get Log Page command
   * - Additional SMART Attributes (Log Identfiter CAh)
   */

  status =
      ioctl(fd, NVME_IOCTL_ADMIN_CMD,
            &(struct nvme_admin_cmd){.opcode = NVME_ADMIN_GET_LOG_PAGE,
                                     .nsid = NVME_NSID_ALL,
                                     .addr = (unsigned long)&intel_smart_log,
                                     .data_len = sizeof(intel_smart_log),
                                     .cdw10 = NVME_SMART_INTEL_CDW10});
  if (status < 0) {
    ERROR("ioctl for NVME_IOCTL_ADMIN_CMD failed with %s\n", strerror(errno));
    close(fd);
    return status;
  } else {

    smart_submit(name, "nvme_program_fail_count", "norm",
                 (double)intel_smart_log.program_fail_cnt.norm);
    smart_submit(name, "nvme_program_fail_count", "raw",
                 int48_to_double(intel_smart_log.program_fail_cnt.raw));
    smart_submit(name, "nvme_erase_fail_count", "norm",
                 (double)intel_smart_log.erase_fail_cnt.norm);
    smart_submit(name, "nvme_erase_fail_count", "raw",
                 int48_to_double(intel_smart_log.program_fail_cnt.raw));
    smart_submit(name, "nvme_wear_leveling", "norm",
                 (double)intel_smart_log.wear_leveling_cnt.norm);
    smart_submit(
        name, "nvme_wear_leveling", "min",
        (double)le16_to_cpu(intel_smart_log.wear_leveling_cnt.wear_level.min));
    smart_submit(
        name, "nvme_wear_leveling", "max",
        (double)le16_to_cpu(intel_smart_log.wear_leveling_cnt.wear_level.max));
    smart_submit(
        name, "nvme_wear_leveling", "avg",
        (double)le16_to_cpu(intel_smart_log.wear_leveling_cnt.wear_level.avg));
    smart_submit(name, "nvme_end_to_end_error_detection_count", "norm",
                 (double)intel_smart_log.e2e_err_cnt.norm);
    smart_submit(name, "nvme_end_to_end_error_detection_count", "raw",
                 int48_to_double(intel_smart_log.e2e_err_cnt.raw));
    smart_submit(name, "nvme_crc_error_count", "norm",
                 (double)intel_smart_log.crc_err_cnt.norm);
    smart_submit(name, "nvme_crc_error_count", "raw",
                 int48_to_double(intel_smart_log.crc_err_cnt.raw));
    smart_submit(name, "nvme_timed_workload_media_wear", "norm",
                 (double)intel_smart_log.timed_workload_media_wear.norm);
    smart_submit(
        name, "nvme_timed_workload_media_wear", "raw",
        int48_to_double(intel_smart_log.timed_workload_media_wear.raw));
    smart_submit(name, "nvme_timed_workload_host_reads", "norm",
                 (double)intel_smart_log.timed_workload_host_reads.norm);
    smart_submit(
        name, "nvme_timed_workload_host_reads", "raw",
        int48_to_double(intel_smart_log.timed_workload_host_reads.raw));
    smart_submit(name, "nvme_timed_workload_timer", "norm",
                 (double)intel_smart_log.timed_workload_timer.norm);
    smart_submit(name, "nvme_timed_workload_timer", "raw",
                 int48_to_double(intel_smart_log.timed_workload_timer.raw));
    smart_submit(name, "nvme_thermal_throttle_status", "norm",
                 (double)intel_smart_log.thermal_throttle_status.norm);
    smart_submit(
        name, "nvme_thermal_throttle_status", "pct",
        (double)intel_smart_log.thermal_throttle_status.thermal_throttle.pct);
    smart_submit(
        name, "nvme_thermal_throttle_status", "count",
        (double)intel_smart_log.thermal_throttle_status.thermal_throttle.count);
    smart_submit(name, "nvme_retry_buffer_overflow_count", "norm",
                 (double)intel_smart_log.retry_buffer_overflow_cnt.norm);
    smart_submit(
        name, "nvme_retry_buffer_overflow_count", "raw",
        int48_to_double(intel_smart_log.retry_buffer_overflow_cnt.raw));
    smart_submit(name, "nvme_pll_lock_loss_count", "norm",
                 (double)intel_smart_log.pll_lock_loss_cnt.norm);
    smart_submit(name, "nvme_pll_lock_loss_count", "raw",
                 int48_to_double(intel_smart_log.pll_lock_loss_cnt.raw));
    smart_submit(name, "nvme_nand_bytes_written", "norm",
                 (double)intel_smart_log.host_bytes_written.norm);
    smart_submit(name, "nvme_nand_bytes_written", "raw",
                 int48_to_double(intel_smart_log.host_bytes_written.raw));
    smart_submit(name, "nvme_host_bytes_written", "norm",
                 (double)intel_smart_log.host_bytes_written.norm);
    smart_submit(name, "nvme_host_bytes_written", "raw",
                 int48_to_double(intel_smart_log.host_bytes_written.raw));
  }

  close(fd);
  return 0;
}

static void smart_read_sata_disk(SkDisk *d, char const *name) {
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
  int err;

  if (use_serial && serial) {
    name = serial;
  } else {
    name = strrchr(dev, '/');
    if (!name)
      return;
    name++;
  }

  if (use_serial) {
    if (ignorelist_match(ignorelist_by_serial, name) != 0) {
      DEBUG("smart plugin: ignoring %s. Name = %s", dev, name);
      return;
    }
  } else {
    if (ignorelist_match(ignorelist, name) != 0) {
      DEBUG("smart plugin: ignoring %s. Name = %s", dev, name);
      return;
    }
  }

  DEBUG("smart plugin: checking SMART status of %s.", dev);

  if (strstr(dev, "nvme")) {
    err = smart_read_nvme_disk(dev, name);
    if (err) {
      ERROR("smart plugin: smart_read_nvme_disk failed, %d", err);
    } else {
      switch (get_vendor_id(dev, name)) {
      case INTEL_VENDOR_ID:
        err = smart_read_nvme_intel_disk(dev, name);
        if (err) {
          ERROR("smart plugin: smart_read_nvme_intel_disk failed, %d", err);
        }
        break;

      default:
        DEBUG("No support vendor specific attributes");
        break;
      }
    }

  } else {

    if (sk_disk_open(dev, &d) < 0) {
      ERROR("smart plugin: unable to open %s.", dev);
      return;
    }
    smart_read_sata_disk(d, name);
    sk_disk_free(d);
  }
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
  if (enumerate == NULL) {
    ERROR("fail udev_enumerate_new");
    return -1;
  }
  udev_enumerate_add_match_subsystem(enumerate, "block");
  udev_enumerate_add_match_property(enumerate, "DEVTYPE", "disk");
  udev_enumerate_scan_devices(enumerate);
  devices = udev_enumerate_get_list_entry(enumerate);
  if (devices == NULL) {
    ERROR("udev returned an empty list deviecs");
    return -1;
  }
  udev_list_entry_foreach(dev_list_entry, devices) {
    const char *path, *devpath, *serial;
    path = udev_list_entry_get_name(dev_list_entry);
    dev = udev_device_new_from_syspath(handle_udev, path);
    devpath = udev_device_get_devnode(dev);
    serial = udev_device_get_property_value(dev, "ID_SERIAL_SHORT");

    /* Query status with libatasmart */
    smart_handle_disk(devpath, serial);
    udev_device_unref(dev);
  }

  udev_enumerate_unref(enumerate);
  udev_unref(handle_udev);

  return 0;
} /* int smart_read */

static int smart_init(void) {
  int err;
  if (use_serial) {
    err = create_ignorelist_by_serial(ignorelist);
    if (err != 0) {
      ERROR("Enable to create ignorelist_by_serial");
      return 1;
    }
  }

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
