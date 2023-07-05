/**
 * collectd - src/gpu_sysman.c
 *
 * Copyright(c) 2020-2023 Intel Corporation. All rights reserved.
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
 * - Eero Tamminen <eero.t.tamminen@intel.com>
 *
 * See:
 * - https://spec.oneapi.com/level-zero/latest/sysman/PROG.html
 * - https://spec.oneapi.io/level-zero/latest/sysman/api.html
 *
 * Error handling:
 * - Allocations are done using collectd scalloc(), smalloc() and sstrdup()
 *   helpers which log an error and exit on allocation failures
 * - All Sysman API call errors are logged
 * - Sysman errors cause plugin initialization failure only when
 *   no GPU devices (with PCI ID) are available
 * - Sysman errors in metric queries cause just given metric to be
 *    disabled for given GPU
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <level_zero/ze_api.h>
#include <level_zero/zes_api.h>

/* whether to add "dev_file" label to metrics for Kubernetes Intel GPU plugin,
 * needs (POSIX.1-2001) basename() + glob() and (POSIX.1-2008) getline()
 * functions.
 */
#define ADD_DEV_FILE 1
#if ADD_DEV_FILE
#include <glob.h>
#include <libgen.h>
#endif

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#define PLUGIN_NAME "gpu_sysman"
#define METRIC_PREFIX "collectd_" PLUGIN_NAME "_"

/* collectd plugin API callback finished OK */
#define RET_OK 0
/* plugin specific callback error return values */
#define RET_NO_METRICS -1
#define RET_INVALID_CONFIG -2
#define RET_ZE_INIT_FAIL -3
#define RET_NO_DRIVERS -4
#define RET_ZE_DRIVER_GET_FAIL -5
#define RET_ZE_DEVICE_GET_FAIL -6
#define RET_ZE_DEVICE_PROPS_FAIL -7
#define RET_NO_GPUS -9

/* GPU metrics to disable */
typedef struct {
  bool all; /* no metrics from whole GPU */
  bool engine;
  bool engine_single;
  bool fabric;
  bool freq;
  bool mem;
  bool membw;
  bool power;
  bool power_ratio; // needs extra Sysman data compared to power
  bool ras;
  bool ras_separate;
  bool temp;
  bool throttle;
} gpu_disable_t;

/* handles for the GPU devices discovered by Sysman library */
typedef struct {
  /* GPU info for metric labels */
  char *pci_bdf;  // required
  char *pci_dev;  // if GpuInfo
  char *dev_file; // if ADD_DEV_FILE
  /* number of types for metrics without allocs */
  uint32_t ras_count;
  uint32_t temp_count;
  /* number of types for each counter metric */
  uint32_t engine_count;
  uint32_t fabric_count;
  uint32_t membw_count;
  uint32_t power_count;
  uint32_t throttle_count;
  /* number of types for each sampled metric */
  uint32_t frequency_count;
  uint32_t memory_count;
  /* previous values for counters, must have matching <name>_count */
  zes_engine_stats_t *engine;
  zes_fabric_port_throughput_t *fabric;
  zes_mem_bandwidth_t *membw;
  zes_power_energy_counter_t *power;
  zes_freq_throttle_time_t *throttle;
  /* types * samples sized array of values, used for aggregate outputs */
  zes_freq_state_t **frequency;
  zes_mem_state_t **memory;
  /* GPU  specific disable flags */
  gpu_disable_t disabled;
  zes_device_handle_t handle;
  /* report counter */
  uint64_t counter;
} gpu_device_t;

typedef enum {
  OUTPUT_COUNTER = (1 << 0),
  OUTPUT_RATE = (1 << 1),
  OUTPUT_RATIO = (1 << 2),
  OUTPUT_ALL = (OUTPUT_COUNTER | OUTPUT_RATE | OUTPUT_RATIO)
} output_t;

static const struct {
  const char *name;
  output_t value;
} metrics_output[] = {{"counter", OUTPUT_COUNTER},
                      {"rate", OUTPUT_RATE},
                      {"ratio", OUTPUT_RATIO}};

static gpu_device_t *gpus;
static uint32_t gpu_count;
static struct {
  bool gpuinfo;
  gpu_disable_t disabled;
  output_t output;
  uint32_t samples;
} config;

/* Sysman GPU plugin config options (defines to ease catching typos) */
#define KEY_DISABLE_ENGINE "DisableEngine"
#define KEY_DISABLE_ENGINE_SINGLE "DisableEngineSingle"
#define KEY_DISABLE_FABRIC "DisableFabric"
#define KEY_DISABLE_FREQ "DisableFrequency"
#define KEY_DISABLE_MEM "DisableMemory"
#define KEY_DISABLE_MEMBW "DisableMemoryBandwidth"
#define KEY_DISABLE_POWER "DisablePower"
#define KEY_DISABLE_RAS "DisableErrors"
#define KEY_DISABLE_RAS_SEPARATE "DisableSeparateErrors"
#define KEY_DISABLE_TEMP "DisableTemperature"
#define KEY_DISABLE_THROTTLE "DisableThrottleTime"

#define KEY_METRICS_OUTPUT "MetricsOutput"
#define KEY_LOG_GPU_INFO "LogGpuInfo"
#define KEY_SAMPLES "Samples"
#define MAX_SAMPLES 64

/* Free array of arrays allocated with gpu_subarray_realloc().
 *
 * config.samples must not have changed since allocation, because
 * that determines the number of allocated subarrays
 */
static bool gpu_subarray_free(void **mem) {
  uint32_t i;
  if (!mem) {
    return false;
  }
  for (i = 0; i < config.samples; i++) {
    free(mem[i]);
    mem[i] = NULL;
  }
  free(mem);
  return true;
}

/* Allocate 'config.samples' sized array of 'count' sized arrays having 'size'
 * sized items.  If given array is already allocated, it and its subarrays
 * is freed first
 */
static void **gpu_subarray_realloc(void **mem, int count, int size) {
  uint32_t i;
  gpu_subarray_free(mem);
  mem = smalloc(config.samples * sizeof(void *));
  for (i = 0; i < config.samples; i++) {
    // (s)calloc used so pointers in structs are initialized to
    // NULLs for Sysman metric state/property Get calls
    mem[i] = scalloc(count, size);
  }
  return mem;
}

/* Free GPU allocations and zero counters
 *
 * Return RET_OK for shutdown callback success
 */
static int gpu_config_free(void) {
#define FREE_GPU_ARRAY(i, member)                                              \
  if (gpus[i].member) {                                                        \
    free(gpus[i].member);                                                      \
    gpus[i].member##_count = 0;                                                \
    gpus[i].member = NULL;                                                     \
  }
#define FREE_GPU_SAMPLING_ARRAYS(i, member)                                    \
  if (gpus[i].member) {                                                        \
    gpu_subarray_free((void **)gpus[i].member);                                \
    gpus[i].member##_count = 0;                                                \
    gpus[i].member = NULL;                                                     \
  }
  if (!gpus) {
    /* gpu_init() should have failed with no GPUs, so no need for this */
    WARNING(PLUGIN_NAME
            ": gpu_config_free() (shutdown) called with no GPUs initialized");
    return RET_NO_GPUS;
  }
  for (uint32_t i = 0; i < gpu_count; i++) {
    /* free previous values for counters & zero their counts */
    FREE_GPU_ARRAY(i, engine);
    FREE_GPU_ARRAY(i, fabric);
    FREE_GPU_ARRAY(i, membw);
    FREE_GPU_ARRAY(i, power);
    FREE_GPU_ARRAY(i, throttle);
    /* and similar for sampling arrays */
    FREE_GPU_SAMPLING_ARRAYS(i, frequency);
    FREE_GPU_SAMPLING_ARRAYS(i, memory);
    /* zero rest of counters & free name */
    gpus[i].ras_count = 0;
    gpus[i].temp_count = 0;
    free(gpus[i].pci_bdf);
    gpus[i].pci_bdf = NULL;
    free(gpus[i].pci_dev);
    gpus[i].pci_dev = NULL;
    free(gpus[i].dev_file);
    gpus[i].dev_file = NULL;
  }
#undef FREE_GPU_SAMPLING_ARRAYS
#undef FREE_GPU_ARRAY
  free(gpus);
  gpus = NULL;
  return RET_OK;
}

/* list GPU metric options that can get disabled at run-time */
static unsigned int list_gpu_metrics(const gpu_disable_t *disabled) {
  struct {
    bool disabled;
    bool submetric;
    const char *name;
  } names[] = {{disabled->engine, false, "Engine"},
               {disabled->fabric, false, "Fabric port"},
               {disabled->freq, false, "Frequency"},
               {disabled->mem, false, "Memory"},
               {disabled->membw, false, "Memory BW"},
               {disabled->power, false, "Power"},
               {disabled->power_ratio, true, "Power ratio"},
               {disabled->ras, false, "RAS/errors"},
               {disabled->temp, false, "Temperature"},
               {disabled->throttle, false, "Throttle time"}};

  unsigned int i;

  if (config.gpuinfo) {
    unsigned int disabled = 0;
    INFO("Disabled metrics / submetrics:");
    for (i = 0; i < STATIC_ARRAY_SIZE(names); i++) {
      if (names[i].disabled) {
        INFO("- %s%s", names[i].name, names[i].submetric ? " (submetric)" : "");
        disabled++;
      }
    }
    if (!disabled) {
      INFO("- none");
    }
  }
  if (config.gpuinfo) {
    INFO("Enabled metrics:");
  }
  unsigned int enabled = 0;
  for (i = 0; i < STATIC_ARRAY_SIZE(names); i++) {
    if (!(names[i].disabled || names[i].submetric)) {
      if (config.gpuinfo) {
        INFO("- %s", names[i].name);
      }
      enabled++;
    }
  }
  if (config.gpuinfo && !enabled) {
    INFO("- none");
  }
  return enabled;
}

/* show plugin GPU metrics config options, return RET_OK
 * if at least some metric is enabled, otherwise error code
 */
static int gpu_config_check(void) {
  if (!config.output) {
    config.output = OUTPUT_ALL;
  }

  if (config.gpuinfo) {
    double interval = CDTIME_T_TO_DOUBLE(plugin_get_interval());
    INFO("\nPlugin settings for '" PLUGIN_NAME "':");
    INFO("- " KEY_SAMPLES ": %d", config.samples);
    if (config.samples > 1) {
      INFO("- internal sampling interval: %.2f", interval);
      INFO("- query / aggregation submit interval: %.2f",
           config.samples * interval);
    } else {
      INFO("- query / submit interval: %.2f", interval);
    }

    unsigned i;
    INFO("'" KEY_METRICS_OUTPUT "' variants:");
    for (i = 0; i < STATIC_ARRAY_SIZE(metrics_output); i++) {
      if (config.output & metrics_output[i].value) {
        INFO("- %s", metrics_output[i].name);
      }
    }

    struct {
      const char *name;
      bool value;
    } options[] = {{KEY_DISABLE_ENGINE_SINGLE, config.disabled.engine_single},
                   {KEY_DISABLE_RAS_SEPARATE, config.disabled.ras_separate}};
    INFO("Metric detail options:");
    for (i = 0; i < STATIC_ARRAY_SIZE(options); i++) {
      INFO("- %s: %s", options[i].name, options[i].value ? "true" : "false");
    }
  }

  if (!list_gpu_metrics(&config.disabled)) {
    ERROR(PLUGIN_NAME ": all metrics disabled");
    return RET_NO_METRICS;
  }
  return RET_OK;
}

/* Set GPU specific flags to initial global configuration values
 * for each GPU.  Allocations of metrics arrays are done when metrics
 * are queried for the first time (not here), and re-allocated if
 * number of types for given metric changes.
 *
 * Return RET_OK if config is OK, (negative) error value otherwise
 */
static int gpu_config_init(unsigned int count) {
  if (!config.samples) {
    config.samples = 1;
  }
  if (gpu_config_check()) {
    gpu_config_free();
    return RET_NO_METRICS;
  }
  unsigned int i;
  for (i = 0; i < count; i++) {
    gpus[i].disabled = config.disabled;
    gpus[i].counter = 0;
  }
  gpu_count = count;
  return RET_OK;
}

/* log given UUID (without dashes):
 * https://en.wikipedia.org/wiki/Universally_unique_identifier
 */
static void log_uuid(const char *prefix, const uint8_t *byte, int len) {
  int offset = strlen(prefix);
  char buf[offset + 2 * len + 1];
  sstrncpy(buf, prefix, sizeof(buf));
  while (len-- > 0) {
    sprintf(buf + offset, "%02x", *byte++);
    offset += 2;
  }
  INFO("%s", buf);
}

/* If GPU info setting is enabled, log Sysman API provided info for
 * given GPU, and set PCI device ID to 'pci_dev'.  On success, return
 * true and set GPU PCI address to 'pci_bdf' as string in BDF notation:
 * https://wiki.xen.org/wiki/Bus:Device.Function_(BDF)_Notation
 */
static bool gpu_info(zes_device_handle_t dev, char **pci_bdf, char **pci_dev) {
  char buf[32];

  *pci_bdf = *pci_dev = NULL;
  zes_pci_properties_t pci = {.pNext = NULL};
  ze_result_t ret = zesDevicePciGetProperties(dev, &pci);
  if (ret == ZE_RESULT_SUCCESS) {
    const zes_pci_address_t *addr = &pci.address;
    snprintf(buf, sizeof(buf), "%04x:%02x:%02x.%x", addr->domain, addr->bus,
             addr->device, addr->function);
  } else {
    ERROR(PLUGIN_NAME ": failed to get GPU PCI device properties => 0x%x", ret);
    return false;
  }
  *pci_bdf = sstrdup(buf);
  if (!config.gpuinfo) {
    return true;
  }

  INFO("Level-Zero Sysman API GPU info");
  INFO("==============================");

  INFO("PCI info:");
  if (ret == ZE_RESULT_SUCCESS) {
    INFO("- PCI B/D/F:  %s", *pci_bdf);
    const zes_pci_speed_t *speed = &pci.maxSpeed;
    INFO("- PCI gen:    %d", speed->gen);
    INFO("- PCI width:  %d", speed->width);
    double max = speed->maxBandwidth / (double)(1024 * 1024 * 1024);
    INFO("- max BW:     %.2f GiB/s (all lines)", max);
  } else {
    INFO("- unavailable");
  }

  INFO("HW state:");
  zes_device_state_t state = {.pNext = NULL};
  /* Note: there's also zesDevicePciGetState() for PCI link status */
  if (ret = zesDeviceGetState(dev, &state), ret == ZE_RESULT_SUCCESS) {
    INFO("- repaired: %s",
         (state.repaired == ZES_REPAIR_STATUS_PERFORMED) ? "yes" : "no");
    if (state.reset != 0) {
      INFO("- device RESET required");
      if (state.reset & ZES_RESET_REASON_FLAG_WEDGED) {
        INFO(" - HW is wedged");
      }
      if (state.reset & ZES_RESET_REASON_FLAG_REPAIR) {
        INFO(" - HW needs to complete repairs");
      }
    } else {
      INFO("- no RESET required");
    }
  } else {
    INFO("- unavailable");
    WARNING(PLUGIN_NAME ": failed to get GPU device state => 0x%x", ret);
  }

  const char *eccstate = "unavailable";
  zes_device_ecc_properties_t ecc = {.pNext = NULL};
  if (zesDeviceGetEccState(dev, &ecc) == ZE_RESULT_SUCCESS) {
    switch (ecc.currentState) {
    case ZES_DEVICE_ECC_STATE_ENABLED:
      eccstate = "enabled";
      break;
    case ZES_DEVICE_ECC_STATE_DISABLED:
      eccstate = "disabled";
      break;
    default:
      break;
    }
  }
  INFO("- ECC state: %s", eccstate);

  INFO("HW identification:");
  zes_device_properties_t props = {.pNext = NULL};
  if (ret = zesDeviceGetProperties(dev, &props), ret == ZE_RESULT_SUCCESS) {
    const ze_device_properties_t *core = &props.core;
    snprintf(buf, sizeof(buf), "0x%x", core->deviceId);
    *pci_dev = sstrdup(buf); // used only if present
    INFO("- name:       %s", core->name);
    INFO("- vendor ID:  0x%x", core->vendorId);
    INFO("- device ID:  0x%x", core->deviceId);
    log_uuid("- UUID:       0x", core->uuid.id, sizeof(core->uuid.id));
    INFO("- serial#:    %s", props.serialNumber);
    INFO("- board#:     %s", props.boardNumber);
    INFO("- brand:      %s", props.brandName);
    INFO("- model:      %s", props.modelName);
    INFO("- vendor:     %s", props.vendorName);

    INFO("UMD/KMD driver info:");
    INFO("- version:    %s", props.driverVersion);
    INFO("- max alloc:  %lu MiB", core->maxMemAllocSize / (1024 * 1024));

    INFO("HW info:");
    INFO("- # sub devs: %u", props.numSubdevices);
    INFO("- core clock: %u", core->coreClockRate);
    INFO("- EUs:        %u", core->numEUsPerSubslice *
                                 core->numSubslicesPerSlice * core->numSlices);
  } else {
    INFO("- unavailable");
    WARNING(PLUGIN_NAME ": failed to get GPU device properties => 0x%x", ret);
  }

  /* HW info for all memories */
  uint32_t i, mem_count = 0;
  ze_device_handle_t mdev = (ze_device_handle_t)dev;
  if (ret = zeDeviceGetMemoryProperties(mdev, &mem_count, NULL),
      ret != ZE_RESULT_SUCCESS) {
    WARNING(PLUGIN_NAME ": failed to get memory properties count => 0x%x", ret);
    return true;
  }
  ze_device_memory_properties_t *mems = scalloc(mem_count, sizeof(*mems));
  if (ret = zeDeviceGetMemoryProperties(mdev, &mem_count, mems),
      ret != ZE_RESULT_SUCCESS) {
    WARNING(PLUGIN_NAME ": failed to get %d memory properties => 0x%x",
            mem_count, ret);
    free(mems);
    return true;
  }
  for (i = 0; i < mem_count; i++) {
    const char *memname = mems[i].name;
    if (!(memname && *memname)) {
      memname = "Unknown";
    }
    INFO("Memory - %s:", memname);
    INFO("- size:       %lu MiB", mems[i].totalSize / (1024 * 1024));
    INFO("- bus width:  %u", mems[i].maxBusWidth);
    INFO("- max clock:  %u", mems[i].maxClockRate);
  }
  free(mems);
  return true;
}

/* Add (given) BDF string and device file name to GPU struct for metric labels.
 *
 * Return false if (required) BDF string is missing, true otherwise.
 */
static bool add_gpu_labels(gpu_device_t *gpu, zes_device_handle_t dev) {
  assert(gpu);
  char *pci_bdf, *pci_dev;
  if (!gpu_info(dev, &pci_bdf, &pci_dev) || !pci_bdf) {
    return false;
  }
  gpu->pci_bdf = pci_bdf;
  gpu->pci_dev = pci_dev;
  /*
   * scan devfs and sysfs to find primary GPU device file node matching
   * given BDF, and if one is found, use that as device file name.
   *
   * NOTE: scanning can log only INFO messages, because ERRORs and WARNINGs
   * would FAIL unit test that are run as part of build, if build environment
   * has no GPU access.
   */
#if ADD_DEV_FILE
#define BDF_LINE "PCI_SLOT_NAME="
#define DEVFS_GLOB "/dev/dri/card*"
  glob_t devfs;
  if (glob(DEVFS_GLOB, 0, NULL, &devfs) != 0) {
    INFO(PLUGIN_NAME ": device <-> BDF mapping, no matches for: " DEVFS_GLOB);
    globfree(&devfs);
    return true;
  }
  const size_t prefix_size = strlen(BDF_LINE);
  for (size_t i = 0; i < devfs.gl_pathc; i++) {
    char path[PATH_MAX], *dev_file;
    dev_file = basename(devfs.gl_pathv[i]);

    FILE *fp;
    snprintf(path, sizeof(path), "/sys/class/drm/%s/device/uevent", dev_file);
    if (!(fp = fopen(path, "r"))) {
      INFO(PLUGIN_NAME ": device <-> BDF mapping, file missing: %s", path);
      continue;
    }
    ssize_t nread;
    size_t len = 0;
    char *line = NULL;
    while ((nread = getline(&line, &len, fp)) > 0) {
      if (strncmp(line, BDF_LINE, prefix_size) != 0) {
        continue;
      }
      line[nread - 1] = '\0'; // remove newline
      if (strcmp(line + prefix_size, pci_bdf) == 0) {
        INFO(PLUGIN_NAME ": %s <-> %s", dev_file, pci_bdf);
        gpu->dev_file = sstrdup(dev_file);
        break;
      }
    }
    free(line);
    fclose(fp);
    if (gpu->dev_file) {
      break;
    }
  }
  globfree(&devfs);
#undef DEVFS_GLOB
#undef BDF_LINE
#endif
  return true;
}

/* Scan how many GPU devices Sysman reports in total, and set 'scan_count'
 * accordingly
 *
 * Return RET_OK for success, or (negative) error value if any of the device
 * count queries fails
 */
static int gpu_scan(ze_driver_handle_t *drivers, uint32_t driver_count,
                    uint32_t *scan_count) {
  assert(!gpus);
  *scan_count = 0;
  for (uint32_t drv_idx = 0; drv_idx < driver_count; drv_idx++) {

    uint32_t dev_count = 0;
    ze_result_t ret = zeDeviceGet(drivers[drv_idx], &dev_count, NULL);
    if (ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get device count for driver %d => 0x%x",
            drv_idx, ret);
      return RET_ZE_DEVICE_GET_FAIL;
    }
    if (config.gpuinfo) {
      INFO("driver %d: %d devices", drv_idx, dev_count);
    }
    *scan_count += dev_count;
  }
  if (!*scan_count) {
    ERROR(PLUGIN_NAME ": scan for GPU devices failed");
    return RET_NO_GPUS;
  }
  if (config.gpuinfo) {
    INFO("scan: %d GPUs in total from %d L0 drivers", *scan_count,
         driver_count);
  }
  return RET_OK;
}

/* Allocate 'scan_count' GPU structs to 'gpus' and fetch Sysman handle & name
 * for them.
 *
 * Counts of still found & ignored GPUs are set to 'scan_count' and
 * 'scan_ignored' arguments before returning.
 *
 * Return RET_OK for success if at least one GPU device info fetch succeeded,
 * otherwise (negative) error value for last error encountered
 */
static int gpu_fetch(ze_driver_handle_t *drivers, uint32_t driver_count,
                     uint32_t *scan_count, uint32_t *scan_ignored) {
  assert(!gpus);
  assert(*scan_count > 0);
  gpus = scalloc(*scan_count, sizeof(*gpus));

  uint32_t ignored = 0, count = 0;
  int retval = RET_NO_GPUS;
  ze_result_t ret;

  for (uint32_t drv_idx = 0; drv_idx < driver_count; drv_idx++) {
    uint32_t dev_count = 0;
    if (ret = zeDeviceGet(drivers[drv_idx], &dev_count, NULL),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get device count for driver %d => 0x%x",
            drv_idx, ret);
      retval = RET_ZE_DEVICE_GET_FAIL;
      continue;
    }
    ze_device_handle_t *devs;
    devs = scalloc(dev_count, sizeof(*devs));
    if (ret = zeDeviceGet(drivers[drv_idx], &dev_count, devs),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get %d devices for driver %d => 0x%x",
            dev_count, drv_idx, ret);
      free(devs);
      devs = NULL;
      retval = RET_ZE_DEVICE_GET_FAIL;
      continue;
    }
    /* Get all GPU devices for the driver */
    for (uint32_t dev_idx = 0; dev_idx < dev_count; dev_idx++) {
      ze_device_properties_t props = {.pNext = NULL};
      if (ret = zeDeviceGetProperties(devs[dev_idx], &props),
          ret != ZE_RESULT_SUCCESS) {
        ERROR(PLUGIN_NAME
              ": failed to get driver %d device %d properties => 0x%x",
              drv_idx, dev_idx, ret);
        retval = RET_ZE_DEVICE_PROPS_FAIL;
        continue;
      }
      assert(ZE_DEVICE_TYPE_GPU == props.type);
      if (count >= *scan_count) {
        ignored++;
        continue;
      }
      gpus[count].handle = (zes_device_handle_t)devs[dev_idx];
      if (!add_gpu_labels(&(gpus[count]), devs[dev_idx])) {
        ERROR(PLUGIN_NAME ": failed to get driver %d device %d information",
              drv_idx, dev_idx);
        ignored++;
        continue;
      }
      count++;
    }
    free(devs);
    devs = NULL;
  }
  if (count > 0) {
    retval = RET_OK;
    if (config.gpuinfo) {
      INFO("fetch: %d/%d GPUs in total from %d L0 drivers", count, *scan_count,
           driver_count);
    }
  } else {
    ERROR(PLUGIN_NAME ": fetch for GPU devices failed");
    gpu_config_free();
  }
  *scan_ignored = ignored;
  *scan_count = count;
  return retval;
}

/* Scan Sysman for GPU devices
 * Return RET_OK for success, (negative) error value otherwise
 */
static int gpu_init(void) {
  if (gpus) {
    NOTICE(PLUGIN_NAME ": skipping extra gpu_init() call");
    return RET_OK;
  }
  ze_result_t ret;
  setenv("ZES_ENABLE_SYSMAN", "1", 1);
  if (ret = zeInit(ZE_INIT_FLAG_GPU_ONLY), ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": Level Zero API init failed => 0x%x", ret);
    return RET_ZE_INIT_FAIL;
  }
  /* Discover all the drivers */
  uint32_t driver_count = 0;
  if (ret = zeDriverGet(&driver_count, NULL), ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get L0 GPU drivers count => 0x%x", ret);
    return RET_ZE_DRIVER_GET_FAIL;
  }
  if (!driver_count) {
    ERROR(PLUGIN_NAME ": no drivers found with Level-Zero Sysman API");
    return RET_NO_DRIVERS;
  }
  ze_driver_handle_t *drivers;
  drivers = scalloc(driver_count, sizeof(*drivers));
  if (ret = zeDriverGet(&driver_count, drivers), ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d L0 drivers => 0x%x", driver_count,
          ret);
    free(drivers);
    return RET_ZE_DRIVER_GET_FAIL;
  }
  /* scan number of Sysman provided GPUs... */
  int fail;
  uint32_t count;
  if ((fail = gpu_scan(drivers, driver_count, &count)) < 0) {
    free(drivers);
    return fail;
  }
  uint32_t ignored = 0, scanned = count;
  if (count) {
    /* ...and allocate & fetch data for them */
    if ((fail = gpu_fetch(drivers, driver_count, &count, &ignored)) < 0) {
      free(drivers);
      return fail;
    }
  }
  free(drivers);
  if (scanned > count) {
    WARNING(PLUGIN_NAME ": %d GPUs disappeared after first scan",
            scanned - count);
  }
  if (ignored) {
    WARNING(PLUGIN_NAME ": %d GPUs appeared after first scan (are ignored)",
            ignored);
  }
  if (!count) {
    ERROR(PLUGIN_NAME ": no GPU devices found with Level-Zero Sysman API");
    return RET_NO_GPUS;
  }
  return gpu_config_init(count);
}

/* Add device labels to all metrics in given metric family and submit family to
 * collectd.  Resets metric family after dispatch */
static void gpu_submit(gpu_device_t *gpu, metric_family_t *fam) {
  metric_t *m = fam->metric.ptr;
  for (size_t i = 0; i < fam->metric.num; i++) {
    metric_label_set(m + i, "pci_bdf", gpu->pci_bdf);
    if (gpu->dev_file) {
      metric_label_set(m + i, "dev_file", gpu->dev_file);
    }
    if (gpu->pci_dev) {
      metric_label_set(m + i, "pci_dev", gpu->pci_dev);
    }
  }
  int status = plugin_dispatch_metric_family(fam);
  if (status != 0) {
    ERROR(PLUGIN_NAME ": gpu_submit(%s, %s) failed: %s", gpu->pci_bdf,
          fam->name, strerror(status));
  }
  metric_family_metric_reset(fam);
}

/* because of family name change, each RAS metric needs to be submitted +
 * reseted separately */
static void ras_submit(gpu_device_t *gpu, const char *name, const char *help,
                       const char *type, const char *subdev, double value) {
  metric_family_t fam = {
      .type = METRIC_TYPE_COUNTER,
      /*
       * String literals are const, so they are passed as such to
       * here, but .name & .help members are not, so casts are
       * necessary.
       *
       * Note that same casts happen implicitly when string
       * literals are assigned directly to these members, GCC
       * just does not warn about that unless "-Write-strings"
       * warning is enabled, which is NOT part of even "-Wall
       * -Wextra".
       *
       * This cast is safe as long as metric_family_free() is not
       * called on these families (which is the case).
       */
      .name = (char *)name,
      .help = (char *)help,
  };
  metric_t m = {0};

  m.value.counter = value;
  if (type) {
    metric_label_set(&m, "type", type);
  }
  if (subdev) {
    metric_label_set(&m, "sub_dev", subdev);
  }
  metric_family_metric_append(&fam, m);
  metric_reset(&m);
  gpu_submit(gpu, &fam);
}

/* Report error set types, return true for success */
static bool gpu_ras(gpu_device_t *gpu) {
  uint32_t i, ras_count = 0;
  zes_device_handle_t dev = gpu->handle;
  ze_result_t ret = zesDeviceEnumRasErrorSets(dev, &ras_count, NULL);
  if (ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get RAS error sets count => 0x%x", ret);
    return false;
  }
  zes_ras_handle_t *ras;
  ras = scalloc(ras_count, sizeof(*ras));
  if (ret = zesDeviceEnumRasErrorSets(dev, &ras_count, ras),
      ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d RAS error sets => 0x%x", ras_count,
          ret);
    free(ras);
    return false;
  }
  if (gpu->ras_count != ras_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d RAS error sets", ras_count);
    gpu->ras_count = ras_count;
  }

  bool ok = false;
  for (i = 0; i < ras_count; i++) {
    zes_ras_properties_t props = {.pNext = NULL};
    if (ret = zesRasGetProperties(ras[i], &props), ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get RAS set %d properties => 0x%x", i,
            ret);
      ok = false;
      break;
    }
    const char *type;
    switch (props.type) {
    case ZES_RAS_ERROR_TYPE_CORRECTABLE:
      type = "correctable";
      break;
    case ZES_RAS_ERROR_TYPE_UNCORRECTABLE:
      type = "uncorrectable";
      break;
    default:
      type = "unknown";
    }
    char buf[8];
    const char *subdev = NULL;
    if (props.onSubdevice) {
      snprintf(buf, sizeof(buf), "%d", props.subdeviceId);
      subdev = buf;
    }
    const bool clear = false;
    zes_ras_state_t values = {.pNext = NULL};
    if (ret = zesRasGetState(ras[i], clear, &values),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get RAS set %d (%s) state => 0x%x", i,
            type, ret);
      ok = false;
      break;
    }

    bool correctable;
    uint64_t value, total = 0;
    const char *catname, *help;
    for (int cat_idx = 0; cat_idx < ZES_MAX_RAS_ERROR_CATEGORY_COUNT;
         cat_idx++) {
      value = values.category[cat_idx];
      total += value;
      if (gpu->disabled.ras_separate) {
        continue;
      }
      correctable = true;
      switch (cat_idx) {
        // categories which are not correctable, see:
        // https://spec.oneapi.io/level-zero/latest/sysman/PROG.html#querying-ras-errors
      case ZES_RAS_ERROR_CAT_RESET:
        help = "Total count of HW accelerator resets attempted by the driver";
        catname = METRIC_PREFIX "resets_total";
        correctable = false;
        break;
      case ZES_RAS_ERROR_CAT_PROGRAMMING_ERRORS:
        help =
            "Total count of (non-correctable) HW exceptions generated by the "
            "way workloads program the HW";
        catname = METRIC_PREFIX "programming_errors_total";
        correctable = false;
        break;
      case ZES_RAS_ERROR_CAT_DRIVER_ERRORS:
        help =
            "total count of (non-correctable) low-level driver communication "
            "errors";
        catname = METRIC_PREFIX "driver_errors_total";
        correctable = false;
        break;
        // categories which can have both correctable and uncorrectable errors
      case ZES_RAS_ERROR_CAT_COMPUTE_ERRORS:
        help = "Total count of errors that have occurred in the (shader) "
               "accelerator HW";
        catname = METRIC_PREFIX "compute_errors_total";
        break;
      case ZES_RAS_ERROR_CAT_NON_COMPUTE_ERRORS:
        help = "Total count of errors that have occurred in the fixed-function "
               "accelerator HW";
        catname = METRIC_PREFIX "fixed_function_errors_total";
        break;
      case ZES_RAS_ERROR_CAT_CACHE_ERRORS:
        help = "Total count of ECC errors that have occurred in the on-chip "
               "caches";
        catname = METRIC_PREFIX "cache_errors_total";
        break;
      case ZES_RAS_ERROR_CAT_DISPLAY_ERRORS:
        help = "Total count of ECC errors that have occurred in the display";
        catname = METRIC_PREFIX "display_errors_total";
        break;
      default:
        help = "Total count of errors in unsupported categories";
        catname = METRIC_PREFIX "unknown_errors_total";
      }
      if (correctable) {
        ras_submit(gpu, catname, help, type, subdev, value);
      } else if (props.type == ZES_RAS_ERROR_TYPE_UNCORRECTABLE) {
        ras_submit(gpu, catname, help, NULL, subdev, value);
      }
    }
    catname = METRIC_PREFIX "all_errors_total";
    help = "Total count of errors in all categories";
    ras_submit(gpu, catname, help, type, subdev, total);
    ok = true;
  }
  free(ras);
  return ok;
}

static void metric_set_subdev(metric_t *m, bool onsub, uint32_t subid) {
  if (onsub) {
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", subid);
    metric_label_set(m, "sub_dev", buf);
  }
}

/* set memory metric labels based on its properties, return ZE_RESULT_SUCCESS
 * for success
 */
static ze_result_t set_mem_labels(zes_mem_handle_t mem, metric_t *metric) {
  zes_mem_properties_t props = {.pNext = NULL};
  ze_result_t ret = zesMemoryGetProperties(mem, &props);
  if (ret != ZE_RESULT_SUCCESS) {
    return ret;
  }
  const char *location;
  switch (props.location) {
  case ZES_MEM_LOC_SYSTEM:
    location = "system";
    break;
  case ZES_MEM_LOC_DEVICE:
    location = "device";
    break;
  default:
    location = "unknown";
  }
  const char *type;
  switch (props.type) {
  case ZES_MEM_TYPE_HBM:
    type = "HBM";
    break;
  case ZES_MEM_TYPE_DDR:
    type = "DDR";
    break;
  case ZES_MEM_TYPE_DDR3:
    type = "DDR3";
    break;
  case ZES_MEM_TYPE_DDR4:
    type = "DDR4";
    break;
  case ZES_MEM_TYPE_DDR5:
    type = "DDR5";
    break;
  case ZES_MEM_TYPE_LPDDR:
    type = "LPDDR";
    break;
  case ZES_MEM_TYPE_LPDDR3:
    type = "LPDDR3";
    break;
  case ZES_MEM_TYPE_LPDDR4:
    type = "LPDDR4";
    break;
  case ZES_MEM_TYPE_LPDDR5:
    type = "LPDDR5";
    break;
  case ZES_MEM_TYPE_GDDR4:
    type = "GDDR4";
    break;
  case ZES_MEM_TYPE_GDDR5:
    type = "GDDR5";
    break;
  case ZES_MEM_TYPE_GDDR5X:
    type = "GDDR5X";
    break;
  case ZES_MEM_TYPE_GDDR6:
    type = "GDDR6";
    break;
  case ZES_MEM_TYPE_GDDR6X:
    type = "GDDR6X";
    break;
  case ZES_MEM_TYPE_GDDR7:
    type = "GDDR7";
    break;
  case ZES_MEM_TYPE_SRAM:
    type = "SRAM";
    break;
  case ZES_MEM_TYPE_L1:
    type = "L1";
    break;
  case ZES_MEM_TYPE_L3:
    type = "L3";
    break;
  case ZES_MEM_TYPE_GRF:
    type = "GRF";
    break;
  case ZES_MEM_TYPE_SLM:
    type = "SLM";
    break;
  default:
    type = "unknown";
  }
  metric_label_set(metric, "type", type);
  metric_label_set(metric, "location", location);
  metric_set_subdev(metric, props.onSubdevice, props.subdeviceId);
  return ZE_RESULT_SUCCESS;
}

/* Report memory usage for memory modules, return true for success.
 *
 * See gpu_read() on 'cache_idx' usage.
 */
static bool gpu_mems(gpu_device_t *gpu, unsigned int cache_idx) {
  uint32_t i, mem_count = 0;
  zes_device_handle_t dev = gpu->handle;
  ze_result_t ret = zesDeviceEnumMemoryModules(dev, &mem_count, NULL);
  if (ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get memory modules count => 0x%x", ret);
    return false;
  }
  zes_mem_handle_t *mems;
  mems = scalloc(mem_count, sizeof(*mems));
  if (ret = zesDeviceEnumMemoryModules(dev, &mem_count, mems),
      ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d memory modules => 0x%x", mem_count,
          ret);
    free(mems);
    return false;
  }

  if (gpu->memory_count != mem_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d memory modules", mem_count);
    gpu->memory = (zes_mem_state_t **)gpu_subarray_realloc(
        (void **)gpu->memory, mem_count, sizeof(gpu->memory[0][0]));
    gpu->memory_count = mem_count;
    assert(gpu->memory);
  }

  metric_family_t fam_bytes = {
      .help = "Sampled memory usage (in bytes)",
      .name = METRIC_PREFIX "memory_used_bytes",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_ratio = {
      .help = "Sampled memory usage ratio (0-1)",
      .name = METRIC_PREFIX "memory_usage_ratio",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_t metric = {0};

  bool reported_ratio = false, reported = false, ok = false;
  for (i = 0; i < mem_count; i++) {
    /* fetch memory samples */
    if (ret = zesMemoryGetState(mems[i], &(gpu->memory[cache_idx][i])),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get memory module %d state => 0x%x", i,
            ret);
      ok = false;
      break;
    }
    ok = true;
    if (cache_idx > 0) {
      continue;
    }
    const uint64_t mem_size = gpu->memory[0][i].size;
    if (!mem_size) {
      ERROR(PLUGIN_NAME ": invalid (zero) memory module %d size", i);
      ok = false;
      break;
    }
    /* process samples */
    if (ret = set_mem_labels(mems[i], &metric), ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get memory module %d properties => 0x%x",
            i, ret);
      ok = false;
      break;
    }
    /* get health status from last i.e. zeroeth sample */
    zes_mem_health_t value = gpu->memory[0][i].health;
    if (value != ZES_MEM_HEALTH_UNKNOWN) {
      const char *health;
      switch (value) {
      case ZES_MEM_HEALTH_OK:
        health = "ok";
        break;
      case ZES_MEM_HEALTH_DEGRADED:
        health = "degraded";
        break;
      case ZES_MEM_HEALTH_CRITICAL:
        health = "critical";
        break;
      case ZES_MEM_HEALTH_REPLACE:
        health = "replace";
        break;
      default:
        health = "unknown";
      }
      metric_label_set(&metric, "health", health);
    }
    double mem_used;
    if (config.samples < 2) {
      const uint64_t mem_free = gpu->memory[0][i].free;
      /* Sysman reports just memory size & free amounts => calculate used */
      mem_used = mem_size - mem_free;
      metric.value.gauge = mem_used;
      metric_family_metric_append(&fam_bytes, metric);
      if (config.output & OUTPUT_RATIO) {
        metric.value.gauge = mem_used / mem_size;
        metric_family_metric_append(&fam_ratio, metric);
        reported_ratio = true;
      }
      reported = true;
    } else {
      /* find min & max values for memory free from
       * (the configured number of) samples
       */
      uint64_t free_min = (uint64_t)0xffffffff;
      uint64_t free_max = 0, mem_free;
      for (uint32_t j = 0; j < config.samples; j++) {
        mem_free = gpu->memory[j][i].free;
        if (mem_free < free_min) {
          free_min = mem_free;
        }
        if (mem_free > free_max) {
          free_max = mem_free;
        }
      }
      /* smallest used amount of memory within interval */
      mem_used = mem_size - free_max;
      metric.value.gauge = mem_used;
      metric_label_set(&metric, "function", "min");
      metric_family_metric_append(&fam_bytes, metric);
      if (config.output & OUTPUT_RATIO) {
        metric.value.gauge = mem_used / mem_size;
        metric_family_metric_append(&fam_ratio, metric);
        reported_ratio = true;
      }
      /* largest used amount of memory within interval */
      mem_used = mem_size - free_min;
      metric.value.gauge = mem_used;
      metric_label_set(&metric, "function", "max");
      metric_family_metric_append(&fam_bytes, metric);
      if (config.output & OUTPUT_RATIO) {
        metric.value.gauge = mem_used / mem_size;
        metric_family_metric_append(&fam_ratio, metric);
        reported_ratio = true;
      }
      reported = true;
    }
    metric_reset(&metric);
  }
  if (reported) {
    gpu_submit(gpu, &fam_bytes);
    if (reported_ratio) {
      gpu_submit(gpu, &fam_ratio);
    }
  }
  free(mems);
  return ok;
}

static void add_bw_gauges(metric_t *metric, metric_family_t *fam, double reads,
                          double writes) {
  metric->value.gauge = reads;
  metric_label_set(metric, "direction", "read");
  metric_family_metric_append(fam, *metric);

  metric->value.gauge = writes;
  metric_label_set(metric, "direction", "write");
  metric_family_metric_append(fam, *metric);
}

/* Report memory modules bandwidth usage, return true for success.
 */
static bool gpu_mems_bw(gpu_device_t *gpu) {
  uint32_t i, mem_count = 0;
  zes_device_handle_t dev = gpu->handle;
  ze_result_t ret = zesDeviceEnumMemoryModules(dev, &mem_count, NULL);
  if (ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get memory (BW) modules count => 0x%x", ret);
    return false;
  }
  zes_mem_handle_t *mems;
  mems = scalloc(mem_count, sizeof(*mems));
  if (ret = zesDeviceEnumMemoryModules(dev, &mem_count, mems),
      ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d memory (BW) modules => 0x%x",
          mem_count, ret);
    free(mems);
    return false;
  }

  if (gpu->membw_count != mem_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d memory (BW) modules", mem_count);
    if (gpu->membw) {
      free(gpu->membw);
    }
    gpu->membw = scalloc(mem_count, sizeof(*gpu->membw));
    gpu->membw_count = mem_count;
  }

  metric_family_t fam_ratio = {
      .help = "Average memory bandwidth usage ratio (0-1) over query interval",
      .name = METRIC_PREFIX "memory_bw_ratio",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_rate = {
      .help = "Memory bandwidth usage rate (in bytes per second)",
      .name = METRIC_PREFIX "memory_bw_bytes_per_second",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_counter = {
      .help = "Memory bandwidth usage total (in bytes)",
      .name = METRIC_PREFIX "memory_bw_bytes_total",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_t metric = {0};

  bool reported_rate = false, reported_ratio = false, reported_counter = false;

  bool ok = false;
  for (i = 0; i < mem_count; i++) {
    zes_mem_bandwidth_t bw;
    if (ret = zesMemoryGetBandwidth(mems[i], &bw), ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get memory module %d bandwidth => 0x%x", i,
            ret);
      ok = false;
      break;
    }
    if (ret = set_mem_labels(mems[i], &metric), ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get memory module %d properties => 0x%x",
            i, ret);
      ok = false;
      break;
    }
    if (config.output & OUTPUT_COUNTER) {
      metric.value.counter = bw.writeCounter;
      metric_label_set(&metric, "direction", "write");
      metric_family_metric_append(&fam_counter, metric);

      metric.value.counter = bw.readCounter;
      metric_label_set(&metric, "direction", "read");
      metric_family_metric_append(&fam_counter, metric);
      reported_counter = true;
    }
    zes_mem_bandwidth_t *old = &gpu->membw[i];
    if (old->timestamp && bw.timestamp > old->timestamp &&
        (config.output & (OUTPUT_RATIO | OUTPUT_RATE))) {
      /* https://spec.oneapi.com/level-zero/latest/sysman/api.html#_CPPv419zes_mem_bandwidth_t
       */
      uint64_t writes = bw.writeCounter - old->writeCounter;
      uint64_t reads = bw.readCounter - old->readCounter;
      uint64_t timediff = bw.timestamp - old->timestamp;

      if (config.output & OUTPUT_RATE) {
        double factor = 1.0e6 / timediff;
        add_bw_gauges(&metric, &fam_rate, factor * reads, factor * writes);
        reported_rate = true;
      }
      if ((config.output & OUTPUT_RATIO) && old->maxBandwidth) {
        double factor = 1.0e6 / (old->maxBandwidth * timediff);
        add_bw_gauges(&metric, &fam_ratio, factor * reads, factor * writes);
        reported_ratio = true;
      }
    }
    metric_reset(&metric);
    *old = bw;
    ok = true;
  }
  if (reported_ratio) {
    gpu_submit(gpu, &fam_ratio);
  }
  if (reported_rate) {
    gpu_submit(gpu, &fam_rate);
  }
  if (reported_counter) {
    gpu_submit(gpu, &fam_counter);
  }
  free(mems);
  return ok;
}

/* set frequency metric labels based on its properties and maxfreq for non-NULL
 * pointer, return ZE_RESULT_SUCCESS for success
 */
static ze_result_t set_freq_labels(zes_freq_handle_t freq, metric_t *metric,
                                   double *maxfreq) {
  zes_freq_properties_t props = {.pNext = NULL};
  ze_result_t ret = zesFrequencyGetProperties(freq, &props);
  if (ret != ZE_RESULT_SUCCESS) {
    return ret;
  }
  if (maxfreq) {
    *maxfreq = props.max;
  }
  const char *type;
  switch (props.type) {
  case ZES_FREQ_DOMAIN_GPU:
    type = "gpu";
    break;
  case ZES_FREQ_DOMAIN_MEMORY:
    type = "memory";
    break;
  default:
    type = "unknown";
  }
  metric_label_set(metric, "location", type);
  metric_set_subdev(metric, props.onSubdevice, props.subdeviceId);
  return ZE_RESULT_SUCCESS;
}

/* set label explaining frequency throttling reason(s) */
static void set_freq_throttled_label(metric_t *metric,
                                     zes_freq_throttle_reason_flags_t reasons) {
  static const struct {
    zes_freq_throttle_reason_flags_t flag;
    const char *reason;
  } flags[] = {
      {ZES_FREQ_THROTTLE_REASON_FLAG_AVE_PWR_CAP, "average-power"},
      {ZES_FREQ_THROTTLE_REASON_FLAG_BURST_PWR_CAP, "burst-power"},
      {ZES_FREQ_THROTTLE_REASON_FLAG_CURRENT_LIMIT, "current"},
      {ZES_FREQ_THROTTLE_REASON_FLAG_THERMAL_LIMIT, "temperature"},
      {ZES_FREQ_THROTTLE_REASON_FLAG_PSU_ALERT, "PSU-alert"},
      {ZES_FREQ_THROTTLE_REASON_FLAG_SW_RANGE, "SW-freq-range"},
      {ZES_FREQ_THROTTLE_REASON_FLAG_HW_RANGE, "HW-freq-range"},
  };
  bool found = false;
  const char *reason = NULL;
  for (unsigned int i = 0; i < STATIC_ARRAY_SIZE(flags); i++) {
    if (reasons & flags[i].flag) {
      if (found) {
        reason = "many";
        break;
      }
      reason = flags[i].reason;
      found = true;
    }
  }
  if (reasons) {
    if (!found) {
      reason = "unknown";
    }
    metric_label_set(metric, "throttled_by", reason);
  }
}

/* Report frequency domains request & actual frequency, return true for success
 *
 * See gpu_read() on 'cache_idx' usage.
 */
static bool gpu_freqs(gpu_device_t *gpu, unsigned int cache_idx) {
  uint32_t i, freq_count = 0;
  zes_device_handle_t dev = gpu->handle;
  ze_result_t ret = zesDeviceEnumFrequencyDomains(dev, &freq_count, NULL);
  if (ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get frequency domains count => 0x%x", ret);
    return false;
  }
  zes_freq_handle_t *freqs;
  freqs = scalloc(freq_count, sizeof(*freqs));
  if (ret = zesDeviceEnumFrequencyDomains(dev, &freq_count, freqs),
      ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d frequency domains => 0x%x",
          freq_count, ret);
    free(freqs);
    return false;
  }

  if (gpu->frequency_count != freq_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d frequency domains", freq_count);
    gpu->frequency = (zes_freq_state_t **)gpu_subarray_realloc(
        (void **)gpu->frequency, freq_count, sizeof(gpu->frequency[0][0]));
    gpu->frequency_count = freq_count;
    assert(gpu->frequency);
  }

  metric_family_t fam_freq = {
      .help = "Sampled HW frequency (in MHz)",
      .name = METRIC_PREFIX "frequency_mhz",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_ratio = {
      .help = "Sampled HW frequency ratio vs (non-overclocked) max frequency",
      .name = METRIC_PREFIX "frequency_ratio",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_t metric = {0};

  bool reported_ratio = false, reported = false, ok = false;
  for (i = 0; i < freq_count; i++) {
    /* fetch freq samples */
    if (ret = zesFrequencyGetState(freqs[i], &(gpu->frequency[cache_idx][i])),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get frequency domain %d state => 0x%x", i,
            ret);
      ok = false;
      break;
    }
    ok = true;
    if (cache_idx > 0) {
      continue;
    }
    /* process samples */
    double maxfreq;
    if (ret = set_freq_labels(freqs[i], &metric, &maxfreq),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME
            ": failed to get frequency domain %d properties => 0x%x",
            i, ret);
      ok = false;
      break;
    }
    double value;

    if (config.samples < 2) {
      set_freq_throttled_label(&metric, gpu->frequency[0][i].throttleReasons);
      /* negative value = unsupported:
       * https://spec.oneapi.com/level-zero/latest/sysman/api.html#_CPPv416zes_freq_state_t
       */
      value = gpu->frequency[0][i].request;
      if (value >= 0) {
        metric.value.gauge = value;
        metric_label_set(&metric, "type", "request");
        metric_family_metric_append(&fam_freq, metric);
        if ((config.output & OUTPUT_RATIO) && maxfreq > 0) {
          metric.value.gauge = value / maxfreq;
          metric_family_metric_append(&fam_ratio, metric);
          reported_ratio = true;
        }
        reported = true;
      }
      value = gpu->frequency[0][i].actual;
      if (value >= 0) {
        metric.value.gauge = value;
        metric_label_set(&metric, "type", "actual");
        metric_family_metric_append(&fam_freq, metric);
        if ((config.output & OUTPUT_RATIO) && maxfreq > 0) {
          metric.value.gauge = value / maxfreq;
          metric_family_metric_append(&fam_ratio, metric);
          reported_ratio = true;
        }
        reported = true;
      }
    } else {
      /* find min & max values for actual frequency & its request
       * from (the configured number of) samples
       */
      double req_min = 1.0e12, req_max = -1.0e12;
      double act_min = 1.0e12, act_max = -1.0e12;
      zes_freq_throttle_reason_flags_t reasons = 0;
      for (uint32_t j = 0; j < config.samples; j++) {
        reasons |= gpu->frequency[j][i].throttleReasons;
        value = gpu->frequency[j][i].request;
        if (value < req_min) {
          req_min = value;
        }
        if (value > req_max) {
          req_max = value;
        }
        value = gpu->frequency[j][i].actual;
        if (value < act_min) {
          act_min = value;
        }
        if (value > act_max) {
          act_max = value;
        }
      }
      set_freq_throttled_label(&metric, reasons);
      if (req_max >= 0.0) {
        metric.value.gauge = req_min;
        metric_label_set(&metric, "type", "request");
        metric_label_set(&metric, "function", "min");
        metric_family_metric_append(&fam_freq, metric);
        if ((config.output & OUTPUT_RATIO) && maxfreq > 0) {
          metric.value.gauge = req_min / maxfreq;
          metric_family_metric_append(&fam_ratio, metric);
          reported_ratio = true;
        }
        metric.value.gauge = req_max;
        metric_label_set(&metric, "function", "max");
        metric_family_metric_append(&fam_freq, metric);
        if ((config.output & OUTPUT_RATIO) && maxfreq > 0) {
          metric.value.gauge = req_max / maxfreq;
          metric_family_metric_append(&fam_ratio, metric);
          reported_ratio = true;
        }
        reported = true;
      }
      if (act_max >= 0.0) {
        metric.value.gauge = act_min;
        metric_label_set(&metric, "type", "actual");
        metric_label_set(&metric, "function", "min");
        metric_family_metric_append(&fam_freq, metric);
        if ((config.output & OUTPUT_RATIO) && maxfreq > 0) {
          metric.value.gauge = act_min / maxfreq;
          metric_family_metric_append(&fam_ratio, metric);
          reported_ratio = true;
        }
        metric.value.gauge = act_max;
        metric_label_set(&metric, "function", "max");
        metric_family_metric_append(&fam_freq, metric);
        if ((config.output & OUTPUT_RATIO) && maxfreq > 0) {
          metric.value.gauge = act_max / maxfreq;
          metric_family_metric_append(&fam_ratio, metric);
          reported_ratio = true;
        }
        reported = true;
      }
    }
    metric_reset(&metric);
    if (!reported) {
      ERROR(PLUGIN_NAME ": neither requests nor actual frequencies supported "
                        "for domain %d",
            i);
      ok = false;
      break;
    }
  }
  if (reported) {
    gpu_submit(gpu, &fam_freq);
    if (reported_ratio) {
      gpu_submit(gpu, &fam_ratio);
    }
  }
  free(freqs);
  return ok;
}

/* Report throttling time, return true for success
 */
static bool gpu_freqs_throttle(gpu_device_t *gpu) {
  uint32_t i, freq_count = 0;
  zes_device_handle_t dev = gpu->handle;
  ze_result_t ret = zesDeviceEnumFrequencyDomains(dev, &freq_count, NULL);
  if (ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME
          ": failed to get frequency (throttling) domains count => 0x%x",
          ret);
    return false;
  }
  zes_freq_handle_t *freqs;
  freqs = scalloc(freq_count, sizeof(*freqs));
  if (ret = zesDeviceEnumFrequencyDomains(dev, &freq_count, freqs),
      ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME
          ": failed to get %d frequency (throttling) domains => 0x%x",
          freq_count, ret);
    free(freqs);
    return false;
  }

  if (gpu->throttle_count != freq_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d frequency (throttling) domains",
         freq_count);
    if (gpu->throttle) {
      free(gpu->throttle);
    }
    gpu->throttle = scalloc(freq_count, sizeof(*gpu->throttle));
    gpu->throttle_count = freq_count;
  }
  if (!(config.output & (OUTPUT_COUNTER | OUTPUT_RATIO))) {
    ERROR(PLUGIN_NAME ": no throttle-time output variants selected");
    free(freqs);
    return false;
  }

  metric_family_t fam_ratio = {
      .help =
          "Ratio (0-1) of HW frequency being throttled during query interval",
      .name = METRIC_PREFIX "throttled_ratio",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_counter = {
      .help = "Total time HW frequency has been throttled (in microseconds)",
      .name = METRIC_PREFIX "throttled_usecs_total",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_t metric = {0};

  bool reported_ratio = false, reported_counter = false, ok = false;
  for (i = 0; i < freq_count; i++) {
    zes_freq_throttle_time_t throttle;
    if (ret = zesFrequencyGetThrottleTime(freqs[i], &throttle),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME
            ": failed to get frequency domain %d throttle time => 0x%x",
            i, ret);
      ok = false;
      break;
    }
    if (ret = set_freq_labels(freqs[i], &metric, NULL),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME
            ": failed to get frequency domain %d properties => 0x%x",
            i, ret);
      ok = false;
      break;
    }
    if (config.output & OUTPUT_COUNTER) {
      /* cannot convert microsecs to secs as counters are integers */
      metric.value.counter = throttle.throttleTime;
      metric_family_metric_append(&fam_counter, metric);
      reported_counter = true;
    }
    zes_freq_throttle_time_t *old = &gpu->throttle[i];
    if (old->timestamp && throttle.timestamp > old->timestamp &&
        (config.output & OUTPUT_RATIO)) {
      /* micro seconds => throttle ratio */
      metric.value.gauge = (throttle.throttleTime - old->throttleTime) /
                           (double)(throttle.timestamp - old->timestamp);
      metric_family_metric_append(&fam_ratio, metric);
      reported_ratio = true;
    }
    metric_reset(&metric);
    *old = throttle;
    ok = true;
  }
  if (reported_ratio) {
    gpu_submit(gpu, &fam_ratio);
  }
  if (reported_counter) {
    gpu_submit(gpu, &fam_counter);
  }
  free(freqs);
  return ok;
}

/* Report relevant temperature sensor values, return true for success */
static bool gpu_temps(gpu_device_t *gpu) {
  uint32_t i, temp_count = 0;
  zes_device_handle_t dev = gpu->handle;
  ze_result_t ret = zesDeviceEnumTemperatureSensors(dev, &temp_count, NULL);
  if (ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get temperature sensors count => 0x%x", ret);
    return false;
  }
  zes_temp_handle_t *temps;
  temps = scalloc(temp_count, sizeof(*temps));
  if (ret = zesDeviceEnumTemperatureSensors(dev, &temp_count, temps),
      ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d temperature sensors => 0x%x",
          temp_count, ret);
    free(temps);
    return false;
  }
  if (gpu->temp_count != temp_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d temperature sensors", temp_count);
    gpu->temp_count = temp_count;
  }

  metric_family_t fam_temp = {
      .help = "Temperature sensor value (in Celsius) when queried",
      .name = METRIC_PREFIX "temperature_celsius",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_ratio = {
      .help = "Temperature sensor value ratio to its max value when queried",
      .name = METRIC_PREFIX "temperature_ratio",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_t metric = {0};

  bool reported_ratio = false, ok = false;
  for (i = 0; i < temp_count; i++) {
    zes_temp_properties_t props = {.pNext = NULL};
    if (ret = zesTemperatureGetProperties(temps[i], &props),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME
            ": failed to get temperature sensor %d properties => 0x%x",
            i, ret);
      ok = false;
      break;
    }
    const char *type;
    /*
     * https://spec.oneapi.io/level-zero/latest/sysman/PROG.html#querying-temperature
     */
    switch (props.type) {
    /* max temperatures */
    case ZES_TEMP_SENSORS_GLOBAL:
      type = "global-max";
      break;
    case ZES_TEMP_SENSORS_GPU:
      type = "gpu-max";
      break;
    case ZES_TEMP_SENSORS_MEMORY:
      type = "memory-max";
      break;
    /* min temperatures */
    case ZES_TEMP_SENSORS_GLOBAL_MIN:
      type = "global-min";
      break;
    case ZES_TEMP_SENSORS_GPU_MIN:
      type = "gpu-min";
      break;
    case ZES_TEMP_SENSORS_MEMORY_MIN:
      type = "memory-min";
      break;
    default:
      type = "unknown";
    }

    double value;
    if (ret = zesTemperatureGetState(temps[i], &value),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME
            ": failed to get temperature sensor %d (%s) state => 0x%x",
            i, type, ret);
      ok = false;
      break;
    }
    metric.value.gauge = value;
    metric_label_set(&metric, "location", type);
    metric_set_subdev(&metric, props.onSubdevice, props.subdeviceId);
    metric_family_metric_append(&fam_temp, metric);

    if (props.maxTemperature > 0 && (config.output & OUTPUT_RATIO)) {
      metric.value.gauge = value / props.maxTemperature;
      metric_family_metric_append(&fam_ratio, metric);
      reported_ratio = true;
    }
    metric_reset(&metric);
    ok = true;
  }
  if (ok) {
    gpu_submit(gpu, &fam_temp);
    if (reported_ratio) {
      gpu_submit(gpu, &fam_ratio);
    }
  }
  free(temps);
  return ok;
}

/* status / health labels */
static void add_fabric_state_labels(metric_t *metric,
                                    zes_fabric_port_state_t *state) {
  const char *status;
  switch (state->status) {
  case ZES_FABRIC_PORT_STATUS_UNKNOWN:
    status = "unknown";
    break;
  case ZES_FABRIC_PORT_STATUS_HEALTHY:
    status = "healthy";
    break;
  case ZES_FABRIC_PORT_STATUS_DEGRADED:
    status = "degraded";
    break;
  case ZES_FABRIC_PORT_STATUS_FAILED:
    status = "failed";
    break;
  case ZES_FABRIC_PORT_STATUS_DISABLED:
    status = "disabled";
    break;
  default:
    status = "unsupported";
  }
  metric_label_set(metric, "status", status);

  const char *issues = NULL;
  switch (state->qualityIssues) {
  case 0:
    break;
  case ZES_FABRIC_PORT_QUAL_ISSUE_FLAG_LINK_ERRORS:
    issues = "link";
    break;
  case ZES_FABRIC_PORT_QUAL_ISSUE_FLAG_SPEED:
    issues = "speed";
    break;
  default:
    issues = "link+speed";
  }
  switch (state->failureReasons) {
  case 0:
    break;
  case ZES_FABRIC_PORT_FAILURE_FLAG_FAILED:
    issues = "failure";
    break;
  case ZES_FABRIC_PORT_FAILURE_FLAG_TRAINING_TIMEOUT:
    issues = "training";
    break;
  case ZES_FABRIC_PORT_FAILURE_FLAG_FLAPPING:
    issues = "flapping";
    break;
  default:
    issues = "multiple";
  }
  if (issues) {
    metric_label_set(metric, "issues", issues);
  }
}

/* Report metrics for relevant fabric ports, return true for success */
static bool gpu_fabrics(gpu_device_t *gpu) {
  uint32_t i, port_count = 0;
  zes_device_handle_t dev = gpu->handle;
  ze_result_t ret = zesDeviceEnumFabricPorts(dev, &port_count, NULL);
  if (ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get fabric port count => 0x%x", ret);
    return false;
  }
  zes_fabric_port_handle_t *ports;
  ports = scalloc(port_count, sizeof(*ports));
  if (ret = zesDeviceEnumFabricPorts(dev, &port_count, ports),
      ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d fabric ports => 0x%x", port_count,
          ret);
    free(ports);
    return false;
  }
  if (gpu->fabric_count != port_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d fabric ports", port_count);
    if (gpu->fabric) {
      free(gpu->fabric);
    }
    gpu->fabric = scalloc(port_count, sizeof(*gpu->fabric));
    gpu->fabric_count = port_count;
  }

  metric_family_t fam_ratio = {
      .help =
          "Average fabric port bandwidth usage ratio (0-1) over query interval",
      .name = METRIC_PREFIX "fabric_port_ratio",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_rate = {
      .help = "Fabric port throughput rate (in bytes per second)",
      .name = METRIC_PREFIX "fabric_port_bytes_per_second",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_counter = {
      .help = "Fabric port throughput total (in bytes)",
      .name = METRIC_PREFIX "fabric_port_bytes_total",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_t metric = {0};

  bool reported_rate = false, reported_ratio = false, reported_counter = false;

  bool ok = false;
  for (i = 0; i < port_count; i++) {

    /* fetch all information before allocing labels */

    zes_fabric_port_state_t state = {.pNext = NULL};
    if (ret = zesFabricPortGetState(ports[i], &state),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get fabric port %d state => 0x%x", i, ret);
      ok = false;
      break;
    }
    zes_fabric_port_properties_t props = {.pNext = NULL};
    if (ret = zesFabricPortGetProperties(ports[i], &props),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get fabric port %d properties => 0x%x", i,
            ret);
      ok = false;
      break;
    }
    zes_fabric_port_config_t conf = {.pNext = NULL};
    if (ret = zesFabricPortGetConfig(ports[i], &conf),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get fabric port %d config => 0x%x", i,
            ret);
      ok = false;
      break;
    }
    zes_fabric_port_throughput_t bw;
    if (ret = zesFabricPortGetThroughput(ports[i], &bw),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get fabric port %d throughput => 0x%x", i,
            ret);
      ok = false;
      break;
    }
    zes_fabric_link_type_t link;
    if (ret = zesFabricPortGetLinkType(ports[i], &link),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get fabric port %d link type => 0x%x", i,
            ret);
      ok = false;
      break;
    }

    /* port setting / identity setting labels */

    link.desc[sizeof(link.desc) - 1] = '\0';
    metric_label_set(&metric, "link", link.desc);
    metric_label_set(&metric, "enabled", conf.enabled ? "on" : "off");
    metric_label_set(&metric, "beaconing", conf.beaconing ? "on" : "off");

    props.model[sizeof(props.model) - 1] = '\0';
    metric_label_set(&metric, "model", props.model);
    metric_set_subdev(&metric, props.onSubdevice, props.subdeviceId);

    /* topology labels */

    char buf[32];
    zes_fabric_port_id_t *pid = &props.portId;
    snprintf(buf, sizeof(buf), "%08x.%08x.%02x", pid->fabricId, pid->attachId,
             pid->portNumber);
    metric_label_set(&metric, "port", buf);

    pid = &state.remotePortId;
    snprintf(buf, sizeof(buf), "%08x.%08x.%02x", pid->fabricId, pid->attachId,
             pid->portNumber);
    metric_label_set(&metric, "remote", buf);

    /* status / health labels */

    add_fabric_state_labels(&metric, &state);

    /* add counters with direction labels */

    if (config.output & OUTPUT_COUNTER) {
      metric.value.counter = bw.txCounter;
      metric_label_set(&metric, "direction", "write");
      metric_family_metric_append(&fam_counter, metric);

      metric.value.counter = bw.rxCounter;
      metric_label_set(&metric, "direction", "read");
      metric_family_metric_append(&fam_counter, metric);
      reported_counter = true;
    }

    /* add rate + ratio gauges with direction labels */

    zes_fabric_port_throughput_t *old = &gpu->fabric[i];
    if (old->timestamp && bw.timestamp > old->timestamp &&
        (config.output & (OUTPUT_RATIO | OUTPUT_RATE))) {
      /* https://spec.oneapi.io/level-zero/latest/sysman/api.html#zes-fabric-port-throughput-t
       */
      uint64_t writes = bw.txCounter - old->txCounter;
      uint64_t reads = bw.rxCounter - old->rxCounter;
      uint64_t timediff = bw.timestamp - old->timestamp;

      if (config.output & OUTPUT_RATE) {
        double factor = 1.0e6 / timediff;
        add_bw_gauges(&metric, &fam_rate, factor * reads, factor * writes);
        reported_rate = true;
      }
      if (config.output & OUTPUT_RATIO) {
        int64_t maxr = props.maxRxSpeed.bitRate * props.maxRxSpeed.width / 8;
        int64_t maxw = props.maxTxSpeed.bitRate * props.maxTxSpeed.width / 8;
        if (maxr > 0 && maxw > 0) {
          double rfactor = 1.0e6 / (maxr * timediff);
          double wfactor = 1.0e6 / (maxw * timediff);
          add_bw_gauges(&metric, &fam_ratio, rfactor * reads, wfactor * writes);
          reported_ratio = true;
        }
      }
    }
    metric_reset(&metric);
    *old = bw;
    ok = true;
  }
  if (reported_ratio) {
    gpu_submit(gpu, &fam_ratio);
  }
  if (reported_rate) {
    gpu_submit(gpu, &fam_rate);
  }
  if (reported_counter) {
    gpu_submit(gpu, &fam_counter);
  }
  free(ports);
  return ok;
}

/* Report power usage for relevant domains, return true for success */
static bool gpu_powers(gpu_device_t *gpu) {
  uint32_t i, power_count = 0;
  zes_device_handle_t dev = gpu->handle;
  ze_result_t ret = zesDeviceEnumPowerDomains(dev, &power_count, NULL);
  if (ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get power domains count => 0x%x", ret);
    return false;
  }
  zes_pwr_handle_t *powers;
  powers = scalloc(power_count, sizeof(*powers));
  if (ret = zesDeviceEnumPowerDomains(dev, &power_count, powers),
      ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d power domains => 0x%x", power_count,
          ret);
    free(powers);
    return false;
  }

  if (gpu->power_count != power_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d power domains", power_count);
    if (gpu->power) {
      free(gpu->power);
    }
    gpu->power = scalloc(power_count, sizeof(*gpu->power));
    gpu->power_count = power_count;
  }

  metric_family_t fam_ratio = {
      .help = "Ratio of average power usage vs sustained or burst "
              "power limit",
      .name = METRIC_PREFIX "power_ratio",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_power = {
      .help = "Average power usage (in Watts) over query interval",
      .name = METRIC_PREFIX "power_watts",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_energy = {
      .help = "Total energy consumption since boot (in microjoules)",
      .name = METRIC_PREFIX "energy_ujoules_total",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_t metric = {0};

  ze_result_t limit_ret = ZE_RESULT_SUCCESS;
  bool reported_ratio = false, reported_power = false, reported_energy = false;
  bool ratio_fail = false;
  bool ok = false;

  for (i = 0; i < power_count; i++) {
    zes_power_properties_t props = {.pNext = NULL};
    if (ret = zesPowerGetProperties(powers[i], &props),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get power domain %d properties => 0x%x", i,
            ret);
      ok = false;
      break;
    }
    zes_power_energy_counter_t counter;
    if (ret = zesPowerGetEnergyCounter(powers[i], &counter),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME
            ": failed to get power domain %d energy counter => 0x%x",
            i, ret);
      ok = false;
      break;
    }
    metric_set_subdev(&metric, props.onSubdevice, props.subdeviceId);
    if (config.output & OUTPUT_COUNTER) {
      metric.value.counter = counter.energy;
      metric_family_metric_append(&fam_energy, metric);
      reported_energy = true;
    }
    zes_power_energy_counter_t *old = &gpu->power[i];
    if (old->timestamp && counter.timestamp > old->timestamp &&
        (config.output & (OUTPUT_RATIO | OUTPUT_RATE))) {

      uint64_t energy_diff = counter.energy - old->energy;
      double time_diff = counter.timestamp - old->timestamp;

      if (config.output & OUTPUT_RATE) {
        /* microJoules / microSeconds => watts */
        metric.value.gauge = energy_diff / time_diff;
        metric_family_metric_append(&fam_power, metric);
        reported_power = true;
      }
      if ((config.output & OUTPUT_RATIO) && !gpu->disabled.power_ratio) {
        zes_power_burst_limit_t burst;
        zes_power_sustained_limit_t sustain;
        /* TODO: future spec version deprecates zesPowerGetLimits():
         *        https://github.com/oneapi-src/level-zero-spec/issues/12
         * Switch to querying list of limits after Sysman plugin starts
         * requiring that spec version / loader.
         */
        if (limit_ret = zesPowerGetLimits(powers[i], &sustain, &burst, NULL),
            limit_ret == ZE_RESULT_SUCCESS) {
          const char *name;
          int32_t limit = 0;
          /* Multiply by 1000, as sustain interval is in ms & power in mJ/s,
           * whereas energy is in uJ and its timestamp in us:
           * https://spec.oneapi.io/level-zero/latest/sysman/api.html#zes-power-energy-counter-t
           */
          if (sustain.enabled &&
              (time_diff >= 1000 * sustain.interval || !burst.enabled)) {
            name = "sustained";
            limit = sustain.power;
          } else if (burst.enabled) {
            name = "burst";
            limit = burst.power;
          }
          if (limit > 0) {
            metric_label_set(&metric, "limit", name);
            metric.value.gauge = 1000 * energy_diff / (limit * time_diff);
            metric_family_metric_append(&fam_ratio, metric);
            reported_ratio = true;
          } else {
            ratio_fail = true;
          }
        } else {
          ratio_fail = true;
        }
      }
    }
    metric_reset(&metric);
    *old = counter;
    ok = true;
  }
  if (reported_energy) {
    gpu_submit(gpu, &fam_energy);
  }
  if (reported_power) {
    gpu_submit(gpu, &fam_power);
  }
  if (reported_ratio) {
    gpu_submit(gpu, &fam_ratio);
  } else if (ratio_fail) {
    gpu->disabled.power_ratio = true;
    if (ok) {
      WARNING(PLUGIN_NAME ": failed to get power limit(s) "
                          "for any of the %d domain(s), last error = 0x%x",
              power_count, limit_ret);
    }
  }
  free(powers);
  return ok;
}

/* Report engine activity in relevant groups, return true for success */
static bool gpu_engines(gpu_device_t *gpu) {
  uint32_t i, engine_count = 0;
  zes_device_handle_t dev = gpu->handle;
  ze_result_t ret = zesDeviceEnumEngineGroups(dev, &engine_count, NULL);
  if (ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get engine groups count => 0x%x", ret);
    return false;
  }
  zes_engine_handle_t *engines;
  engines = scalloc(engine_count, sizeof(*engines));
  if (ret = zesDeviceEnumEngineGroups(dev, &engine_count, engines),
      ret != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d engine groups => 0x%x", engine_count,
          ret);
    free(engines);
    return false;
  }

  if (gpu->engine_count != engine_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d engine groups", engine_count);
    if (gpu->engine) {
      free(gpu->engine);
    }
    gpu->engine = scalloc(engine_count, sizeof(*gpu->engine));
    gpu->engine_count = engine_count;
  }
  if (!(config.output & (OUTPUT_COUNTER | OUTPUT_RATIO))) {
    ERROR(PLUGIN_NAME ": no engine output variants selected");
    free(engines);
    return false;
  }

  metric_family_t fam_ratio = {
      .help = "Average GPU engine / group utilization ratio (0-1) over query "
              "interval",
      .name = METRIC_PREFIX "engine_ratio",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_t fam_counter = {
      .help = "GPU engine / group execution time (activity) total (in "
              "microseconds)",
      .name = METRIC_PREFIX "engine_use_usecs_total",
      .type = METRIC_TYPE_COUNTER,
  };
  metric_t metric = {0};

  int type_idx[16] = {0};
  bool reported_ratio = false, reported_counter = false, ok = false;
  for (i = 0; i < engine_count; i++) {
    zes_engine_properties_t props = {.pNext = NULL};
    if (ret = zesEngineGetProperties(engines[i], &props),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get engine group %d properties => 0x%x", i,
            ret);
      ok = false;
      break;
    }
    bool all = false;
    const char *type;
    switch (props.type) {
    case ZES_ENGINE_GROUP_ALL:
      type = "all";
      all = true;
      break;
      /* multiple engines */
    case ZES_ENGINE_GROUP_COMPUTE_ALL:
      type = "compute";
      all = true;
      break;
    case ZES_ENGINE_GROUP_MEDIA_ALL:
      type = "media";
      all = true;
      break;
    case ZES_ENGINE_GROUP_COPY_ALL:
      type = "copy";
      all = true;
      break;
      /* individual engines */
    case ZES_ENGINE_GROUP_COMPUTE_SINGLE:
      type = "compute";
      break;
    case ZES_ENGINE_GROUP_MEDIA_DECODE_SINGLE:
      type = "decode";
      break;
    case ZES_ENGINE_GROUP_MEDIA_ENCODE_SINGLE:
      type = "encode";
      break;
    case ZES_ENGINE_GROUP_COPY_SINGLE:
      type = "copy";
      break;
    case ZES_ENGINE_GROUP_RENDER_SINGLE:
      type = "render";
      break;

    /* Following defines require at least Level-Zero relase v1.1 */
    case ZES_ENGINE_GROUP_RENDER_ALL:
      type = "render";
      all = true;
      break;
    case ZES_ENGINE_GROUP_3D_ALL:
      type = "3d";
      all = true;
      break;
    case ZES_ENGINE_GROUP_3D_RENDER_COMPUTE_ALL:
      type = "3d-render-compute";
      all = true;
      break;
    case ZES_ENGINE_GROUP_MEDIA_ENHANCEMENT_SINGLE:
      type = "enhance";
      break;
    case ZES_ENGINE_GROUP_3D_SINGLE:
      type = "3d";
      break;

    default:
      type = "unknown";
    }
    const char *vname;
    char buf[32];
    if (all) {
      vname = type;
    } else {
      if (gpu->disabled.engine_single) {
        continue;
      }
      assert(props.type < sizeof(type_idx));
      /* include engine index as there can be multiple engines of same type */
      snprintf(buf, sizeof(buf), "%s-%03d", type, type_idx[props.type]);
      type_idx[props.type]++;
      vname = buf;
    }
    zes_engine_stats_t stats;
    if (ret = zesEngineGetActivity(engines[i], &stats),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get engine %d (%s) group activity => 0x%x",
            i, vname, ret);
      ok = false;
      break;
    }
    metric_set_subdev(&metric, props.onSubdevice, props.subdeviceId);
    metric_label_set(&metric, "type", vname);
    if (config.output & OUTPUT_COUNTER) {
      metric.value.counter = stats.activeTime;
      metric_family_metric_append(&fam_counter, metric);
      reported_counter = true;
    }
    zes_engine_stats_t *old = &gpu->engine[i];
    if (old->timestamp && stats.timestamp > old->timestamp &&
        (config.output & OUTPUT_RATIO)) {
      metric.value.gauge = (double)(stats.activeTime - old->activeTime) /
                           (stats.timestamp - old->timestamp);
      metric_family_metric_append(&fam_ratio, metric);
      reported_ratio = true;
    }
    metric_reset(&metric);
    *old = stats;
    ok = true;
  }
  if (reported_ratio) {
    gpu_submit(gpu, &fam_ratio);
  }
  if (reported_counter) {
    gpu_submit(gpu, &fam_counter);
  }
  free(engines);
  return ok;
}

static void check_gpu_metrics(uint32_t gpu, const gpu_disable_t *initial,
                              const gpu_disable_t *disabled) {
  if (!config.gpuinfo) {
    return;
  }
  if (!memcmp(initial, disabled, sizeof(*initial))) {
    return;
  }
  INFO(PLUGIN_NAME ": GPU-%d metric reporting change", gpu);
  list_gpu_metrics(disabled);
}

static int gpu_read(void) {
  /* no metrics yet */
  int retval = RET_NO_METRICS;
  /* go through all GPUs */
  for (uint32_t i = 0; i < gpu_count; i++) {
    gpu_device_t *gpu = &gpus[i];
    gpu_disable_t *disabled = &gpu->disabled;
    if (disabled->all) {
      continue;
    }
    gpu_disable_t initial = *disabled;

    if (!gpu->counter) {
      INFO(PLUGIN_NAME ": GPU-%d queries:", i);
    }
    /* 'cache_idx' is high frequency sampling aggregation counter.
     *
     * Functions needing that should use gpu_subarray_realloc() to
     * allocate 'config.samples' sized array of metric value arrays,
     * and use 'cache_idx' as index to that array.
     *
     * 'cache_idx' goes down to zero, so that functions themselves
     * need to care less about config.samples value.  But when it
     * does reache zero, function should process 'config.samples'
     * amount of cached items and provide aggregated metrics of
     * them to gpu_submit().
     */
    unsigned int cache_idx =
        (config.samples - 1) - gpu->counter % config.samples;
    /* get potentially high-frequency metrics data (aggregate metrics sent when
     * counter=0)
     */
    if (!disabled->freq && !gpu_freqs(gpu, cache_idx)) {
      WARNING(PLUGIN_NAME
              ": GPU-%d frequency query fail / no domains => disabled",
              i);
      disabled->freq = true;
    }
    if (!disabled->mem && !gpu_mems(gpu, cache_idx)) {
      WARNING(PLUGIN_NAME ": GPU-%d memory query fail / no modules => disabled",
              i);
      disabled->mem = true;
    }
    /* rest of the metrics are read only when the high frequency
     * counter goes down to zero
     */
    gpu->counter++;
    if (cache_idx > 0) {
      if (!disabled->all) {
        /* there are still valid counters at least for this GPU */
        retval = RET_OK;
      }
      check_gpu_metrics(i, &initial, disabled);
      continue;
    }

    /* process lower frequency counters */
    if (config.samples > 1 && gpu->counter <= config.samples) {
      INFO(PLUGIN_NAME ": GPU-%d queries:", i);
    }
    /* get lower frequency metrics */
    if (!disabled->engine && !gpu_engines(gpu)) {
      WARNING(PLUGIN_NAME ": GPU-%d engine query fail / no groups => disabled",
              i);
      disabled->engine = true;
    }
    if (!disabled->fabric && !gpu_fabrics(gpu)) {
      WARNING(PLUGIN_NAME
              ": GPU-%d fabric query fail / no fabric ports => disabled",
              i);
      disabled->fabric = true;
    }
    if (!disabled->membw && !gpu_mems_bw(gpu)) {
      WARNING(PLUGIN_NAME ": GPU-%d mem BW query fail / no modules => disabled",
              i);
      gpu->disabled.membw = true;
    }
    if (!disabled->power && !gpu_powers(gpu)) {
      WARNING(PLUGIN_NAME ": GPU-%d power query fail / no domains => disabled",
              i);
      disabled->power = true;
    }
    if (!disabled->ras && !gpu_ras(gpu)) {
      WARNING(PLUGIN_NAME ": GPU-%d errors query fail / no sets => disabled",
              i);
      disabled->ras = true;
    }
    if (!disabled->temp && !gpu_temps(gpu)) {
      WARNING(PLUGIN_NAME
              ": GPU-%d temperature query fail / no sensors => disabled",
              i);
      disabled->temp = true;
    }
    if (!disabled->throttle && !gpu_freqs_throttle(gpu)) {
      WARNING(PLUGIN_NAME
              ": GPU-%d throttle time query fail / no domains => disabled",
              i);
      gpu->disabled.throttle = true;
    }
    if (disabled->engine && disabled->fabric && disabled->freq &&
        disabled->mem && disabled->membw && disabled->power && disabled->ras &&
        disabled->temp && disabled->throttle) {
      /* all metrics missing -> disable use of that GPU */
      ERROR(PLUGIN_NAME ": No metrics from GPU-%d, disabling its querying", i);
      disabled->all = true;
    } else {
      check_gpu_metrics(i, &initial, disabled);
      retval = RET_OK;
    }
  }
  return retval;
}

static int gpu_config_parse(const char *key, const char *value) {
  /* all metrics are enabled by default, but user can disable them */
  if (strcasecmp(key, KEY_DISABLE_ENGINE) == 0) {
    config.disabled.engine = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_ENGINE_SINGLE) == 0) {
    config.disabled.engine_single = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_FABRIC) == 0) {
    config.disabled.fabric = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_FREQ) == 0) {
    config.disabled.freq = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_MEM) == 0) {
    config.disabled.mem = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_MEMBW) == 0) {
    config.disabled.membw = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_POWER) == 0) {
    config.disabled.power = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_RAS) == 0) {
    config.disabled.ras = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_RAS_SEPARATE) == 0) {
    config.disabled.ras_separate = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_TEMP) == 0) {
    config.disabled.temp = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_DISABLE_THROTTLE) == 0) {
    config.disabled.throttle = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_LOG_GPU_INFO) == 0) {
    config.gpuinfo = IS_TRUE(value);
  } else if (strcasecmp(key, KEY_METRICS_OUTPUT) == 0) {
    config.output = 0;
    static const char delim[] = ",:/ ";
    char *save, *flag, *flags = sstrdup(value);
    for (flag = strtok_r(flags, delim, &save); flag;
         flag = strtok_r(NULL, delim, &save)) {
      bool found = false;
      for (unsigned i = 0; i < STATIC_ARRAY_SIZE(metrics_output); i++) {
        if (strcasecmp(flag, metrics_output[i].name) == 0) {
          config.output |= metrics_output[i].value;
          found = true;
          break;
        }
      }
      if (!found) {
        free(flags);
        ERROR(PLUGIN_NAME ": Invalid '%s' config key value '%s'", key, value);
        return RET_INVALID_CONFIG;
      }
    }
    free(flags);
    if (!config.output) {
      ERROR(PLUGIN_NAME ": Invalid '%s' config key value '%s'", key, value);
      return RET_INVALID_CONFIG;
    }
  } else if (strcasecmp(key, KEY_SAMPLES) == 0) {
    /* because collectd converts config values to floating point strings,
     * this can't use strtol() to check that value is integer, so simply
     * just take the integer part
     */
    int samples = atoi(value);
    if (samples < 1 || samples > MAX_SAMPLES) {
      ERROR(PLUGIN_NAME ": Invalid " KEY_SAMPLES " value '%s'", value);
      return RET_INVALID_CONFIG;
    }
    /* number of samples cannot be changed without freeing per-GPU
     * metrics cache arrays & members, zeroing metric counters and
     * GPU cache index counter.  However, this parse function should
     * be called only before gpu structures have been initialized, so
     * just assert here
     */
    assert(gpus == NULL);
    config.samples = samples;
  } else {
    ERROR(PLUGIN_NAME ": Invalid '%s' config key", key);
    return RET_INVALID_CONFIG;
  }
  return RET_OK;
}

void module_register(void) {
  /* NOTE: key strings *must* be static */
  static const char *config_keys[] = {
      KEY_DISABLE_ENGINE,       KEY_DISABLE_ENGINE_SINGLE,
      KEY_DISABLE_FABRIC,       KEY_DISABLE_FREQ,
      KEY_DISABLE_MEM,          KEY_DISABLE_MEMBW,
      KEY_DISABLE_POWER,        KEY_DISABLE_RAS,
      KEY_DISABLE_RAS_SEPARATE, KEY_DISABLE_TEMP,
      KEY_DISABLE_THROTTLE,     KEY_METRICS_OUTPUT,
      KEY_LOG_GPU_INFO,         KEY_SAMPLES};
  const int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

  plugin_register_config(PLUGIN_NAME, gpu_config_parse, config_keys,
                         config_keys_num);
  plugin_register_init(PLUGIN_NAME, gpu_init);
  plugin_register_read(PLUGIN_NAME, gpu_read);
  plugin_register_shutdown(PLUGIN_NAME, gpu_config_free);
} /* void module_register */
