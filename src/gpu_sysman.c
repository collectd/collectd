/**
 * collectd - src/gpu_sysman.c
 *
 * Copyright(c) 2020-2021 Intel Corporation. All rights reserved.
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
 * See: https://spec.oneapi.com/level-zero/latest/sysman/PROG.html
 *
 * Error handling:
 * - All allocation checking is done with asserts, so plugin will abort
 *   if any allocation fails
 * - All Sysman API call errors are logged
 * - Sysman errors do not cause plugin initialization failure if even
 *   one GPU device is available with PCI ID
 * - Sysman errors in metrics queries cause just given metric to be
 *   disabled (for given GPU)
 *
 * Testing/validation:
 * - See gpu_sysman_test.c
 */
#ifdef TEST_BUILD
#define KERNEL_LINUX 1
#define FP_LAYOUT_NEED_NOTHING 1
#endif

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <level_zero/ze_api.h>
#include <level_zero/zes_api.h>

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#define PLUGIN_NAME "gpu_sysman"
#define MAX_GPU_NAME 32 /* PREFIX + PCI-BNF */
#define NAME_PREFIX "GPU-"

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
  bool freq;
  bool mem;
  bool membw;
  bool power;
  bool ras;
  bool ras_separate;
  bool temp;
  bool throttle;
} gpu_disable_t;

/* handles for the GPU devices discovered by Sysman library */
typedef struct {
  char *name;
  /* number of types for metrics without allocs */
  uint32_t ras_count;
  uint32_t temp_count;
  /* number of types for each counter metric */
  uint32_t engine_count;
  uint32_t membw_count;
  uint32_t power_count;
  uint32_t throttle_count;
  /* number of types for each sampled metric */
  uint32_t frequency_count;
  uint32_t memory_count;
  /* previous values for counters */
  zes_engine_stats_t *engine;
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

static gpu_device_t *gpus;
static uint32_t gpu_count;
static struct {
  bool gpuinfo;
  gpu_disable_t disabled;
  uint32_t samples;
} config;

/* Sysman GPU plugin config options (defines to ease catching typos) */
#define KEY_DISABLE_ENGINE "DisableEngine"
#define KEY_DISABLE_FREQ "DisableFrequency"
#define KEY_DISABLE_MEM "DisableMemory"
#define KEY_DISABLE_MEMBW "DisableMemoryBandwidth"
#define KEY_DISABLE_POWER "DisablePower"
#define KEY_DISABLE_RAS "DisableErrors"
#define KEY_DISABLE_RAS_SEPARATE "DisableSeparateErrors"
#define KEY_DISABLE_TEMP "DisableTemperature"
#define KEY_DISABLE_THROTTLE "DisableThrottleTime"

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
  mem = malloc(config.samples * sizeof(void *));
  assert(mem);
  for (i = 0; i < config.samples; i++) {
    mem[i] = calloc(count, size);
    assert(mem[i]);
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
    FREE_GPU_ARRAY(i, membw);
    FREE_GPU_ARRAY(i, power);
    FREE_GPU_ARRAY(i, throttle);
    /* and similar for sampling arrays */
    FREE_GPU_SAMPLING_ARRAYS(i, frequency);
    FREE_GPU_SAMPLING_ARRAYS(i, memory);
    /* zero rest of counters & free name */
    gpus[i].ras_count = 0;
    gpus[i].temp_count = 0;
    free(gpus[i].name);
    gpus[i].name = NULL;
  }
#undef FREE_GPU_SAMPLING_ARRAYS
#undef FREE_GPU_ARRAY
  free(gpus);
  gpus = NULL;
  return RET_OK;
}

/* show plugin GPU metrics config options, return RET_OK
 * if at least some metric is enabled, otherwise error code
 */
static int gpu_config_check(void) {
  if (config.gpuinfo) {
    INFO("Sysman '" KEY_SAMPLES "': %d", config.samples);
    INFO("Disabled metrics:");
  }
  struct {
    const char *name;
    bool value;
  } options[] = {{KEY_DISABLE_ENGINE, config.disabled.engine},
                 {KEY_DISABLE_FREQ, config.disabled.freq},
                 {KEY_DISABLE_MEM, config.disabled.mem},
                 {KEY_DISABLE_MEMBW, config.disabled.membw},
                 {KEY_DISABLE_POWER, config.disabled.power},
                 {KEY_DISABLE_RAS, config.disabled.ras},
                 {KEY_DISABLE_RAS_SEPARATE, config.disabled.ras_separate},
                 {KEY_DISABLE_TEMP, config.disabled.temp},
                 {KEY_DISABLE_THROTTLE, config.disabled.throttle}};
  unsigned int i, disabled = 0;
  for (i = 0; i < STATIC_ARRAY_SIZE(options); i++) {
    if (options[i].value) {
      if (config.gpuinfo) {
        INFO("- %s", options[i].name);
      }
      disabled++;
    }
  }
  if (disabled >= STATIC_ARRAY_SIZE(options)) {
    ERROR(PLUGIN_NAME ": all metrics disabled");
    return RET_NO_METRICS;
  }
  if (config.gpuinfo) {
    if (disabled) {
      INFO("=> %d disabled metrics", disabled);
    } else {
      INFO("- no disabled metrics");
    }
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
  strcpy(buf, prefix);
  while (len-- > 0) {
    sprintf(buf + offset, "%02x", *byte++);
    offset += 2;
  }
  INFO("%s", buf);
}

/* Log Sysman API provided info for given GPU if logging is enabled
 * and return name for it: "GPU-<idx> <PCI BDF>"
 */
static char *gpu_info(int idx, zes_device_handle_t dev) {
  char *name = malloc(MAX_GPU_NAME);
  assert(name);

  zes_pci_properties_t pci;
  ze_result_t ret = zesDevicePciGetProperties(dev, &pci);
  if (ret == ZE_RESULT_SUCCESS) {
    const zes_pci_address_t *addr = &pci.address;
    snprintf(name, MAX_GPU_NAME, NAME_PREFIX "%04x:%02x:%02x.%x", addr->domain,
             addr->bus, addr->device, addr->function);
  } else {
    ERROR(PLUGIN_NAME ": failed to get GPU %d PCI device properties => 0x%x",
          idx, ret);
    free(name);
    return NULL;
  }
  if (!config.gpuinfo) {
    return name;
  }

  INFO("Level-Zero Sysman API GPU %d info", idx);
  INFO("==================================");

  INFO("PCI info:");
  if (ret == ZE_RESULT_SUCCESS) {
    /* https://wiki.xen.org/wiki/Bus:Device.Function_(BDF)_Notation */
    INFO("- PCI B/D/F:  %s", name + sizeof(NAME_PREFIX));
    const zes_pci_speed_t *speed = &pci.maxSpeed;
    INFO("- PCI gen:    %d", speed->gen);
    INFO("- PCI width:  %d", speed->width);
    double max = speed->maxBandwidth / (double)(1024 * 1024 * 1024);
    INFO("- max BW:     %.2f GiB/s (all lines)", max);
  } else {
    INFO("- unavailable");
  }

  INFO("HW state:");
  zes_device_state_t state;
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
    WARNING(PLUGIN_NAME ": failed to get GPU %d device state => 0x%x", idx,
            ret);
  }

  INFO("HW identification:");
  zes_device_properties_t props;
  if (ret = zesDeviceGetProperties(dev, &props), ret == ZE_RESULT_SUCCESS) {
    const ze_device_properties_t *core = &props.core;
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
    WARNING(PLUGIN_NAME ": failed to get GPU %d device properties => 0x%x", idx,
            ret);
  }

  /* HW info for all memories */
  uint32_t i, mem_count = 0;
  ze_device_handle_t mdev = (ze_device_handle_t)dev;
  if (zeDeviceGetMemoryProperties(mdev, &mem_count, NULL) !=
      ZE_RESULT_SUCCESS) {
    WARNING(PLUGIN_NAME ": failed to get memory properties count");
    return name;
  }
  ze_device_memory_properties_t *mems =
      calloc(mem_count, sizeof(ze_device_memory_properties_t));
  assert(mems);
  if (zeDeviceGetMemoryProperties(mdev, &mem_count, mems) !=
      ZE_RESULT_SUCCESS) {
    WARNING(PLUGIN_NAME ": failed to get %d memory properties", mem_count);
    free(mems);
    return name;
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
  return name;
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
    if (zeDeviceGet(drivers[drv_idx], &dev_count, NULL) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get device count for driver %d", drv_idx);
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
  gpus = calloc(*scan_count, sizeof(gpu_device_t));
  assert(gpus);

  uint32_t ignored = 0, count = 0;
  int retval = RET_NO_GPUS;
  char *name;

  for (uint32_t drv_idx = 0; drv_idx < driver_count; drv_idx++) {
    uint32_t dev_count = 0;
    if (zeDeviceGet(drivers[drv_idx], &dev_count, NULL) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get device count for driver %d", drv_idx);
      retval = RET_ZE_DEVICE_GET_FAIL;
      continue;
    }
    ze_device_handle_t *devs = calloc(dev_count, sizeof(ze_device_handle_t));
    assert(devs);
    if (zeDeviceGet(drivers[drv_idx], &dev_count, devs) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get %d devices for driver %d", dev_count,
            drv_idx);
      free(devs);
      devs = NULL;
      retval = RET_ZE_DEVICE_GET_FAIL;
      continue;
    }
    /* Get all GPU devices for the driver */
    for (uint32_t dev_idx = 0; dev_idx < dev_count; dev_idx++) {
      ze_device_properties_t props;
      if (zeDeviceGetProperties(devs[dev_idx], &props) != ZE_RESULT_SUCCESS) {
        ERROR(PLUGIN_NAME ": failed to get driver %d device %d properties",
              drv_idx, dev_idx);
        retval = RET_ZE_DEVICE_PROPS_FAIL;
        continue;
      }
      assert(ZE_DEVICE_TYPE_GPU == props.type);
      if (count >= *scan_count) {
        ignored++;
        continue;
      }
      gpus[count].handle = (zes_device_handle_t)devs[dev_idx];
      name = gpu_info(count, devs[dev_idx]);
      if (!name) {
        ignored++;
        continue;
      }
      gpus[count].name = name;
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
  setenv("ZES_ENABLE_SYSMAN", "1", 1);
  if (zeInit(ZE_INIT_FLAG_GPU_ONLY) != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": Level Zero API init failed");
    return RET_ZE_INIT_FAIL;
  }
  /* Discover all the drivers */
  uint32_t driver_count = 0;
  if (zeDriverGet(&driver_count, NULL) != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get L0 GPU drivers count");
    return RET_ZE_DRIVER_GET_FAIL;
  }
  if (!driver_count) {
    ERROR(PLUGIN_NAME ": no drivers found with Level-Zero Sysman API");
    return RET_NO_DRIVERS;
  }
  ze_driver_handle_t *drivers =
      calloc(driver_count, sizeof(ze_driver_handle_t));
  assert(drivers);
  if (zeDriverGet(&driver_count, drivers) != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d L0 drivers", driver_count);
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

/* Dispatch given value to collectd */
static void gpu_submit(const char *name, const char *type, const char *valname,
                       double value) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, PLUGIN_NAME, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, name, sizeof(vl.plugin_instance));
  sstrncpy(vl.type_instance, valname, sizeof(vl.type_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  plugin_dispatch_values(&vl);
}

/* Report error set types, return true for success */
static bool gpu_ras(gpu_device_t *gpu) {
  uint32_t i, ras_count = 0;
  zes_device_handle_t dev = gpu->handle;
  if ((zesDeviceEnumRasErrorSets(dev, &ras_count, NULL) != ZE_RESULT_SUCCESS)) {
    ERROR(PLUGIN_NAME ": failed to get RAS error sets count");
    return false;
  }
  zes_ras_handle_t *ras = calloc(ras_count, sizeof(zes_ras_handle_t));
  assert(ras);
  if (zesDeviceEnumRasErrorSets(dev, &ras_count, ras) != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d RAS error sets", ras_count);
    free(ras);
    return false;
  }
  if (gpu->ras_count != ras_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d RAS error sets", ras_count);
    gpu->ras_count = ras_count;
  }

  bool ok = false;
  for (i = 0; i < ras_count; i++) {
    zes_ras_properties_t props;
    if (zesRasGetProperties(ras[i], &props) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get RAS set %d properties", i);
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
    char prefix[32];
    if (props.onSubdevice) {
      snprintf(prefix, sizeof(prefix), "subdev%02d-%s", props.subdeviceId,
               type);
    } else {
      snprintf(prefix, sizeof(prefix), "device-%s", type);
    }

    zes_ras_state_t values;
    const bool clear = false;
    if (zesRasGetState(ras[i], clear, &values) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get RAS set %d (%s) state", i, prefix);
      ok = false;
      break;
    }
    char vname[64];
    uint64_t value, total = 0;
    const char *gpuname = gpu->name;
    for (int cat_idx = 0; cat_idx < ZES_MAX_RAS_ERROR_CATEGORY_COUNT;
         cat_idx++) {
      value = values.category[cat_idx];
      total += value;
      if (gpu->disabled.ras_separate) {
        continue;
      }
      const char *catname;
      switch (cat_idx) {
      case ZES_RAS_ERROR_CAT_RESET:
        catname = "resets";
        break;
      case ZES_RAS_ERROR_CAT_PROGRAMMING_ERRORS:
        catname = "programming-errors";
        break;
      case ZES_RAS_ERROR_CAT_DRIVER_ERRORS:
        catname = "driver-errors";
        break;
      case ZES_RAS_ERROR_CAT_COMPUTE_ERRORS:
        catname = "compute-errors";
        break;
      case ZES_RAS_ERROR_CAT_NON_COMPUTE_ERRORS:
        catname = "non-compute-errors";
        break;
      case ZES_RAS_ERROR_CAT_CACHE_ERRORS:
        catname = "cache-errors";
        break;
      case ZES_RAS_ERROR_CAT_DISPLAY_ERRORS:
        catname = "display-errors";
        break;
      default:
        catname = "unknown";
      }
      snprintf(vname, sizeof(vname), "%s-%s", prefix, catname);
      gpu_submit(gpuname, "error_count", vname, value);
    }
    snprintf(vname, sizeof(vname), "%s-total", prefix);
    gpu_submit(gpuname, "error_count", vname, total);
    ok = true;
  }
  free(ras);
  return ok;
}

static bool set_mem_prefix(zes_mem_handle_t mem, char *prefix, size_t max) {
  zes_mem_properties_t props;
  if (zesMemoryGetProperties(mem, &props) != ZE_RESULT_SUCCESS) {
    return false;
  }
  const char *location;
  switch (props.location) {
  case ZES_MEM_LOC_SYSTEM:
    location = "system";
    break;
  case ZES_MEM_LOC_DEVICE:
    /* device-device would be confusing in metrics name
     * -> use kernel "local" memory naming instead
     */
    location = "local";
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
  if (props.onSubdevice) {
    snprintf(prefix, max, "subdev%02d-%s-%s", props.subdeviceId, location,
             type);
  } else {
    snprintf(prefix, max, "device-%s-%s", location, type);
  }
  return true;
}

/* Report memory usage for memory modules, return true for success.
 *
 * See gpu_read() on 'cache_idx' usage.
 */
static bool gpu_mems(gpu_device_t *gpu, unsigned int cache_idx) {
  uint32_t i, mem_count = 0;
  zes_device_handle_t dev = gpu->handle;
  if ((zesDeviceEnumMemoryModules(dev, &mem_count, NULL) !=
       ZE_RESULT_SUCCESS)) {
    ERROR(PLUGIN_NAME ": failed to get memory modules count");
    return false;
  }
  zes_mem_handle_t *mems = calloc(mem_count, sizeof(zes_mem_handle_t));
  assert(mems);
  if (zesDeviceEnumMemoryModules(dev, &mem_count, mems) != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d memory modules", mem_count);
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

  bool ok = false;
  for (i = 0; i < mem_count; i++) {
    /* fetch memory samples */
    if (zesMemoryGetState(mems[i], &(gpu->memory[cache_idx][i])) !=
        ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get memory module %d state", i);
      ok = false;
      break;
    }
    ok = true;
    if (cache_idx > 0) {
      continue;
    }
    /* process samples */
    char prefix[32];
    if (!set_mem_prefix(mems[i], prefix, sizeof(prefix))) {
      ERROR(PLUGIN_NAME ": failed to get memory module %d properties", i);
      ok = false;
      break;
    }
    const char *gpuname = gpu->name;
    char vname[64];

    const uint64_t mem_size = gpu->memory[0][i].size;
    if (config.samples < 2) {
      const uint64_t mem_free = gpu->memory[0][i].free;
      /* Sysman reports just memory size & free amounts => calculate used */
      snprintf(vname, sizeof(vname), "%s-used", prefix);
      gpu_submit(gpuname, "memory", vname, mem_size - mem_free);

      snprintf(vname, sizeof(vname), "%s-free", prefix);
      gpu_submit(gpuname, "memory", vname, mem_free);
    } else {
      /* find min & max values for memory free from
       * (the configured number of) samples
       */
      uint64_t free_min = (uint64_t)1024 * 1024 * 1024 * 1024;
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
      snprintf(vname, sizeof(vname), "%s-used-min", prefix);
      gpu_submit(gpuname, "memory", vname, mem_size - free_max);
      snprintf(vname, sizeof(vname), "%s-used-max", prefix);
      gpu_submit(gpuname, "memory", vname, mem_size - free_min);

      snprintf(vname, sizeof(vname), "%s-free-min", prefix);
      gpu_submit(gpuname, "memory", vname, free_min);
      snprintf(vname, sizeof(vname), "%s-free-max", prefix);
      gpu_submit(gpuname, "memory", vname, free_max);
    }
  }
  free(mems);
  return ok;
}

/* Report memory modules bandwidth usage, return true for success.
 */
static bool gpu_mems_bw(gpu_device_t *gpu) {
  uint32_t i, mem_count = 0;
  zes_device_handle_t dev = gpu->handle;
  if ((zesDeviceEnumMemoryModules(dev, &mem_count, NULL) !=
       ZE_RESULT_SUCCESS)) {
    ERROR(PLUGIN_NAME ": failed to get memory (BW) modules count");
    return false;
  }
  zes_mem_handle_t *mems = calloc(mem_count, sizeof(zes_mem_handle_t));
  assert(mems);
  if (zesDeviceEnumMemoryModules(dev, &mem_count, mems) != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d memory (BW) modules", mem_count);
    free(mems);
    return false;
  }

  if (gpu->membw_count != mem_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d memory (BW) modules", mem_count);
    if (gpu->membw) {
      free(gpu->membw);
    }
    gpu->membw = calloc(mem_count, sizeof(zes_mem_bandwidth_t));
    gpu->membw_count = mem_count;
    assert(gpu->membw);
  }

  bool ok = false;
  for (i = 0; i < mem_count; i++) {
    char prefix[32];
    if (!set_mem_prefix(mems[i], prefix, sizeof(prefix))) {
      ERROR(PLUGIN_NAME ": failed to get memory module %d properties", i);
      ok = false;
      break;
    }
    ze_result_t ret;
    zes_mem_bandwidth_t bw;
    if (ret = zesMemoryGetBandwidth(mems[i], &bw), ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME
            ": failed to get memory module %d (%s) bandwidth => 0x%x",
            i, prefix, ret);
      ok = false;
      break;
    }
    zes_mem_bandwidth_t *old = &gpu->membw[i];
    if (old->maxBandwidth) {
      /* https://spec.oneapi.com/level-zero/latest/sysman/api.html#_CPPv419zes_mem_bandwidth_t
       */
      uint64_t writes = bw.writeCounter - old->writeCounter;
      uint64_t reads = bw.readCounter - old->readCounter;
      uint64_t timediff = bw.timestamp - old->timestamp;
      double factor = 100.0 * 1.0e6 / (bw.maxBandwidth * timediff);
      char vname[64];

      snprintf(vname, sizeof(vname), "%s-membw-writes", prefix);
      gpu_submit(gpu->name, "percent", vname, factor * writes);

      snprintf(vname, sizeof(vname), "%s-membw-reads", prefix);
      gpu_submit(gpu->name, "percent", vname, factor * reads);
    }
    *old = bw;
    ok = true;
  }
  free(mems);
  return ok;
}

/* set frequency metric prefix based on its properties, return true for success
 */
static bool set_freq_prefix(zes_freq_handle_t freq, uint32_t i, char *prefix,
                            size_t max) {
  zes_freq_properties_t props;
  if (zesFrequencyGetProperties(freq, &props) != ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get frequency domain %d properties", i);
    return false;
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
  if (props.onSubdevice) {
    snprintf(prefix, max, "subdev%02d-%s", props.subdeviceId, type);
  } else {
    snprintf(prefix, max, "device-%s", type);
  }
  return true;
}

/* Report frequency domains request & actual frequency, return true for success
 *
 * See gpu_read() on 'cache_idx' usage.
 */
static bool gpu_freqs(gpu_device_t *gpu, unsigned int cache_idx) {
  uint32_t i, freq_count = 0;
  zes_device_handle_t dev = gpu->handle;
  if ((zesDeviceEnumFrequencyDomains(dev, &freq_count, NULL) !=
       ZE_RESULT_SUCCESS)) {
    ERROR(PLUGIN_NAME ": failed to get frequency domains count");
    return false;
  }
  zes_freq_handle_t *freqs = calloc(freq_count, sizeof(zes_freq_handle_t));
  assert(freqs);
  if (zesDeviceEnumFrequencyDomains(dev, &freq_count, freqs) !=
      ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d frequency domains", freq_count);
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

  bool ok = false;
  for (i = 0; i < freq_count; i++) {
    /* fetch freq samples */
    if (zesFrequencyGetState(freqs[i], &(gpu->frequency[cache_idx][i])) !=
        ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get frequency domain %d state", i);
      ok = false;
      break;
    }
    ok = true;
    if (cache_idx > 0) {
      continue;
    }
    /* process samples */
    char prefix[32];
    if (!set_freq_prefix(freqs[i], i, prefix, sizeof(prefix))) {
      ok = false;
      break;
    }
    const char *gpuname = gpu->name;
    bool freq_ok = false;
    char vname[64];
    double value;

    if (config.samples < 2) {
      /* negative value = unsupported:
       * https://spec.oneapi.com/level-zero/latest/sysman/api.html#_CPPv416zes_freq_state_t
       */
      value = gpu->frequency[0][i].request;
      if (value >= 0) {
        snprintf(vname, sizeof(vname), "%s-request", prefix);
        gpu_submit(gpuname, "frequency", vname, value);
        freq_ok = true;
      }
      value = gpu->frequency[0][i].actual;
      if (value >= 0) {
        snprintf(vname, sizeof(vname), "%s-actual", prefix);
        gpu_submit(gpuname, "frequency", vname, value);
        freq_ok = true;
      }
    } else {
      /* find min & max values for actual frequency & its request
       * from (the configured number of) samples
       */
      double req_min = 1.0e12, req_max = -1.0e12;
      double act_min = 1.0e12, act_max = -1.0e12;
      for (uint32_t j = 0; j < config.samples; j++) {
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
      if (req_max >= 0.0) {
        snprintf(vname, sizeof(vname), "%s-request-min", prefix);
        gpu_submit(gpuname, "frequency", vname, req_min);
        snprintf(vname, sizeof(vname), "%s-request-max", prefix);
        gpu_submit(gpuname, "frequency", vname, req_max);
        freq_ok = true;
      }
      if (act_max >= 0.0) {
        snprintf(vname, sizeof(vname), "%s-actual-min", prefix);
        gpu_submit(gpuname, "frequency", vname, act_min);
        snprintf(vname, sizeof(vname), "%s-actual-max", prefix);
        gpu_submit(gpuname, "frequency", vname, act_max);
        freq_ok = true;
      }
    }
    if (!freq_ok) {
      ERROR(PLUGIN_NAME ": neither requests nor actual frequencies supported "
                        "for domain %d (%s)",
            i, prefix);
      ok = false;
      break;
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
  if ((zesDeviceEnumFrequencyDomains(dev, &freq_count, NULL) !=
       ZE_RESULT_SUCCESS)) {
    ERROR(PLUGIN_NAME ": failed to get frequency (throttling) domains count");
    return false;
  }
  zes_freq_handle_t *freqs = calloc(freq_count, sizeof(zes_freq_handle_t));
  assert(freqs);
  if (zesDeviceEnumFrequencyDomains(dev, &freq_count, freqs) !=
      ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d frequency (throttling) domains",
          freq_count);
    free(freqs);
    return false;
  }

  if (gpu->throttle_count != freq_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d frequency (throttling) domains",
         freq_count);
    if (gpu->throttle) {
      free(gpu->throttle);
    }
    gpu->throttle = calloc(freq_count, sizeof(zes_freq_throttle_time_t));
    gpu->throttle_count = freq_count;
    assert(gpu->throttle);
  }

  bool ok = false;
  for (i = 0; i < freq_count; i++) {
    char prefix[32];
    if (!set_freq_prefix(freqs[i], i, prefix, sizeof(prefix))) {
      ok = false;
      break;
    }
    ze_result_t ret;
    zes_freq_throttle_time_t throttle;
    if (ret = zesFrequencyGetThrottleTime(freqs[i], &throttle),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME
            ": failed to get frequency domain %d (%s) throttle time => 0x%x",
            i, prefix, ret);
      ok = false;
      break;
    }
    zes_freq_throttle_time_t *old = &gpu->throttle[i];
    if (old->timestamp) {
      char vname[64];
      /* in micro seconds */
      double throttled = (throttle.throttleTime - old->throttleTime) /
                         (double)(throttle.timestamp - old->timestamp);
      snprintf(vname, sizeof(vname), "%s-throttled", prefix);
      gpu_submit(gpu->name, "percent", vname, 100.0 * throttled);
    }
    *old = throttle;
    ok = true;
  }
  free(freqs);
  return ok;
}

/* Report relevant temperature sensor values, return true for success */
static bool gpu_temps(gpu_device_t *gpu) {
  uint32_t i, temp_count = 0;
  zes_device_handle_t dev = gpu->handle;
  if ((zesDeviceEnumTemperatureSensors(dev, &temp_count, NULL) !=
       ZE_RESULT_SUCCESS)) {
    ERROR(PLUGIN_NAME ": failed to get temperature sensors count");
    return false;
  }
  zes_temp_handle_t *temps = calloc(temp_count, sizeof(zes_temp_handle_t));
  assert(temps);
  if (zesDeviceEnumTemperatureSensors(dev, &temp_count, temps) !=
      ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d temperature sensors", temp_count);
    free(temps);
    return false;
  }
  if (gpu->temp_count != temp_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d temperature sensors", temp_count);
    gpu->temp_count = temp_count;
  }

  bool ok = false;
  for (i = 0; i < temp_count; i++) {
    zes_temp_properties_t props;
    if (zesTemperatureGetProperties(temps[i], &props) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get temperature sensor %d properties", i);
      ok = false;
      break;
    }
    const char *type;
    switch (props.type) {
    case ZES_TEMP_SENSORS_GLOBAL:
      type = "global-max";
      break;
    case ZES_TEMP_SENSORS_GPU:
      type = "gpu-max";
      break;
    case ZES_TEMP_SENSORS_MEMORY:
      type = "memory-max";
      break;
    case ZES_TEMP_SENSORS_GLOBAL_MIN:
      type = "global-min";
      break;
    case ZES_TEMP_SENSORS_GPU_MIN:
      type = "gpu-min";
      break;
    case ZES_TEMP_SENSORS_MEMORY_MIN:
      type = "gpu-max";
      break;
    default:
      type = "unknown";
    }
    char vname[32];
    if (props.onSubdevice) {
      snprintf(vname, sizeof(vname), "subdev%02d-%s", props.subdeviceId, type);
    } else {
      snprintf(vname, sizeof(vname), "device-%s", type);
    }
    double value;
    if (zesTemperatureGetState(temps[i], &value) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get temperature sensor %d (%s) state", i,
            vname);
      ok = false;
      break;
    }
    gpu_submit(gpu->name, "temperature", vname, value);
    ok = true;
  }
  free(temps);
  return ok;
}

/* Report power usage for relevant domains, return true for success */
static bool gpu_powers(gpu_device_t *gpu) {
  uint32_t i, power_count = 0;
  zes_device_handle_t dev = gpu->handle;
  if ((zesDeviceEnumPowerDomains(dev, &power_count, NULL) !=
       ZE_RESULT_SUCCESS)) {
    ERROR(PLUGIN_NAME ": failed to get power domains count");
    return false;
  }
  zes_pwr_handle_t *powers = calloc(power_count, sizeof(zes_pwr_handle_t));
  assert(powers);
  if (zesDeviceEnumPowerDomains(dev, &power_count, powers) !=
      ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d power domains", power_count);
    free(powers);
    return false;
  }

  if (gpu->power_count != power_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d power domains", power_count);
    if (gpu->power) {
      free(gpu->power);
    }
    gpu->power = calloc(power_count, sizeof(zes_power_energy_counter_t));
    gpu->power_count = power_count;
    assert(gpu->power);
  }

  bool ok = false;
  for (i = 0; i < power_count; i++) {
    zes_power_properties_t props;
    if (zesPowerGetProperties(powers[i], &props) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get power domain %d properties", i);
      ok = false;
      break;
    }
    zes_power_energy_counter_t counter;
    if (zesPowerGetEnergyCounter(powers[i], &counter) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get power domain %d energy counter", i);
      ok = false;
      break;
    }
    zes_power_energy_counter_t *old = &gpu->power[i];
    if (old->timestamp) {
      char vname[64];
      if (props.onSubdevice) {
        snprintf(vname, sizeof(vname), "subdev%02d-watts", props.subdeviceId);
      } else {
        strcpy(vname, "device-watts");
      }
      /* microJoules / microSeconds */
      double watts = (double)(counter.energy - old->energy) /
                     (counter.timestamp - old->timestamp);
      gpu_submit(gpu->name, "power", vname, watts);
    }
    *old = counter;
    ok = true;
  }
  free(powers);
  return ok;
}

/* Report engine activity in relevant groups, return true for success */
static bool gpu_engines(gpu_device_t *gpu) {
  uint32_t i, engine_count = 0;
  zes_device_handle_t dev = gpu->handle;
  if ((zesDeviceEnumEngineGroups(dev, &engine_count, NULL) !=
       ZE_RESULT_SUCCESS)) {
    ERROR(PLUGIN_NAME ": failed to get engine groups count");
    return false;
  }
  zes_engine_handle_t *engines =
      calloc(engine_count, sizeof(zes_engine_handle_t));
  assert(engines);
  if (zesDeviceEnumEngineGroups(dev, &engine_count, engines) !=
      ZE_RESULT_SUCCESS) {
    ERROR(PLUGIN_NAME ": failed to get %d engine groups", engine_count);
    free(engines);
    return false;
  }

  if (gpu->engine_count != engine_count) {
    INFO(PLUGIN_NAME ": Sysman reports %d engine groups", engine_count);
    if (gpu->engine) {
      free(gpu->engine);
    }
    gpu->engine = calloc(engine_count, sizeof(zes_engine_stats_t));
    gpu->engine_count = engine_count;
    assert(gpu->engine);
  }

  bool ok = false;
  for (i = 0; i < engine_count; i++) {
    zes_engine_properties_t props;
    if (zesEngineGetProperties(engines[i], &props) != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get engine group %d properties", i);
      ok = false;
      break;
    }
    char location[32];
    if (props.onSubdevice) {
      snprintf(location, sizeof(location), "subdev%02d", props.subdeviceId);
    } else {
      strcpy(location, "device");
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
    char vname[64];
    if (all) {
      snprintf(vname, sizeof(vname), "%s-engines-%s", location, type);
    } else {
      /* include engine index as there can be multiple engines of same type */
      snprintf(vname, sizeof(vname), "%s-engine%02d-%s", location, i, type);
    }
    ze_result_t ret;
    zes_engine_stats_t stats;
    if (ret = zesEngineGetActivity(engines[i], &stats),
        ret != ZE_RESULT_SUCCESS) {
      ERROR(PLUGIN_NAME ": failed to get engine %d (%s) group activity => 0x%x",
            i, vname, ret);
      ok = false;
      break;
    }
    zes_engine_stats_t *old = &gpu->engine[i];
    if (old->timestamp) {
      double util = (double)(stats.activeTime - old->activeTime) /
                    (stats.timestamp - old->timestamp);
      gpu_submit(gpu->name, "percent", vname, 100.0 * util);
    }
    *old = stats;
    ok = true;
  }
  free(engines);
  return ok;
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
    if (disabled->engine && disabled->mem && disabled->freq &&
        disabled->membw && disabled->power && disabled->ras && disabled->temp &&
        disabled->throttle) {
      /* all metrics missing -> disable use of that GPU */
      ERROR(PLUGIN_NAME ": No metrics from GPU-%d, disabling its querying", i);
      disabled->all = true;
    } else {
      retval = RET_OK;
    }
  }
  return retval;
}

static int gpu_config_parse(const char *key, const char *value) {
  /* all metrics are enabled by default, but user can disable them */
  if (strcasecmp(key, KEY_DISABLE_ENGINE) == 0) {
    config.disabled.engine = IS_TRUE(value);
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
      KEY_DISABLE_ENGINE,       KEY_DISABLE_FREQ,  KEY_DISABLE_MEM,
      KEY_DISABLE_MEMBW,        KEY_DISABLE_POWER, KEY_DISABLE_RAS,
      KEY_DISABLE_RAS_SEPARATE, KEY_DISABLE_TEMP,  KEY_DISABLE_THROTTLE,
      KEY_LOG_GPU_INFO,         KEY_SAMPLES};
  const int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

  plugin_register_config(PLUGIN_NAME, gpu_config_parse, config_keys,
                         config_keys_num);
  plugin_register_init(PLUGIN_NAME, gpu_init);
  plugin_register_read(PLUGIN_NAME, gpu_read);
  plugin_register_shutdown(PLUGIN_NAME, gpu_config_free);
} /* void module_register */