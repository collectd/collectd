/*
Copyright 2018 Evgeny Naumov

Permission is hereby granted, free of charge, to any person obtaining a copy of
this software and associated documentation files (the "Software"), to deal in
the Software without restriction, including without limitation the rights to
use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
of the Software, and to permit persons to whom the Software is furnished to do
so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include "daemon/collectd.h"
#include "daemon/plugin.h"
#include "utils/common/common.h"

#include <nvml.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_DEVNAME_LEN 256
#define PLUGIN_NAME "gpu_nvidia"

static nvmlReturn_t nv_status = NVML_SUCCESS;
static char *nv_errline = "";

#define TRY_CATCH(f, catch)                                                    \
  if ((nv_status = f) != NVML_SUCCESS) {                                       \
    nv_errline = #f;                                                           \
    goto catch;                                                                \
  }

#define TRY_CATCH_OPTIONAL(f, catch)                                           \
  if ((nv_status = f) != NVML_SUCCESS &&                                       \
      nv_status != NVML_ERROR_NOT_SUPPORTED) {                                 \
    nv_errline = #f;                                                           \
    goto catch;                                                                \
  }

#define TRY(f) TRY_CATCH(f, catch)
#define TRYOPT(f) TRY_CATCH_OPTIONAL(f, catch)

#define KEY_GPUINDEX "GPUIndex"
#define KEY_IGNORESELECTED "IgnoreSelected"

static const char *config_keys[] = {
    KEY_GPUINDEX, KEY_IGNORESELECTED,
};
static const unsigned int n_config_keys = STATIC_ARRAY_SIZE(config_keys);

// This is a bitflag, necessitating the (extremely conservative) assumption
// that there are no more than 64 GPUs on this system.
static uint64_t conf_match_mask = 0;
static bool conf_mask_is_exclude = 0;

static int nvml_config(const char *key, const char *value) {

  if (strcasecmp(key, KEY_GPUINDEX) == 0) {
    char *eptr;
    unsigned long device_ix = strtoul(value, &eptr, 10);
    if (eptr == value) {
      ERROR(PLUGIN_NAME ": Failed to parse GPUIndex value \"%s\"", value);
      return -1;
    }
    if (device_ix >= 64) {
      ERROR(PLUGIN_NAME
            ": At most 64 GPUs (0 <= GPUIndex < 64) are supported!");
      return -2;
    }
    conf_match_mask |= (1 << device_ix);
  } else if (strcasecmp(key, KEY_IGNORESELECTED)) {
    conf_mask_is_exclude = IS_TRUE(value);
  } else {
    ERROR(PLUGIN_NAME ": Unrecognized config option %s", key);
    return -10;
  }

  return 0;
}

static int nvml_init(void) {
  TRY(nvmlInit());
  return 0;

  catch : ERROR(PLUGIN_NAME ": NVML init failed with %d", nv_status);
  return -1;
}

static int nvml_shutdown(void) {
  TRY(nvmlShutdown())
  return 0;

  catch : ERROR(PLUGIN_NAME ": NVML shutdown failed with %d", nv_status);
  return -1;
}

static void nvml_submit_gauge(const char *plugin_instance, const char *type,
                              const char *type_instance, gauge_t nvml) {

  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = nvml};
  vl.values_len = 1;

  sstrncpy(vl.plugin, PLUGIN_NAME, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));

  sstrncpy(vl.type, type, sizeof(vl.type));

  if (type_instance != NULL) {
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));
  }

  plugin_dispatch_values(&vl);
}

static int nvml_read(void) {

  unsigned int device_count;
  TRY_CATCH(nvmlDeviceGetCount(&device_count), catch_nocount);

  if (device_count > 64) {
    device_count = 64;
  }

  for (unsigned int ix = 0; ix < device_count; ix++) {

    unsigned int is_match =
        ((1 << ix) & conf_match_mask) || (conf_match_mask == 0);
    if (conf_mask_is_exclude == !!is_match) {
      continue;
    }

    nvmlDevice_t dev;
    TRY(nvmlDeviceGetHandleByIndex(ix, &dev));

    char dev_name[MAX_DEVNAME_LEN + 1] = {0};
    TRY(nvmlDeviceGetName(dev, dev_name, sizeof(dev_name) - 1));

    // Try to be as lenient as possible with the variety of devices that are
    // out there, ignoring any NOT_SUPPORTED errors gently.
    nvmlMemory_t meminfo;
    TRYOPT(nvmlDeviceGetMemoryInfo(dev, &meminfo))
    if (nv_status == NVML_SUCCESS) {
      nvml_submit_gauge(dev_name, "memory", "used", meminfo.used);
      nvml_submit_gauge(dev_name, "memory", "free", meminfo.free);
    }

    nvmlUtilization_t utilization;
    TRYOPT(nvmlDeviceGetUtilizationRates(dev, &utilization))
    if (nv_status == NVML_SUCCESS)
      nvml_submit_gauge(dev_name, "percent", "gpu_used", utilization.gpu);

    unsigned int fan_speed;
    TRYOPT(nvmlDeviceGetFanSpeed(dev, &fan_speed))
    if (nv_status == NVML_SUCCESS)
      nvml_submit_gauge(dev_name, "fanspeed", NULL, fan_speed);

    unsigned int core_temp;
    TRYOPT(nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &core_temp))
    if (nv_status == NVML_SUCCESS)
      nvml_submit_gauge(dev_name, "temperature", "core", core_temp);

    unsigned int sm_clk_mhz;
    TRYOPT(nvmlDeviceGetClockInfo(dev, NVML_CLOCK_SM, &sm_clk_mhz))
    if (nv_status == NVML_SUCCESS)
      nvml_submit_gauge(dev_name, "frequency", "multiprocessor",
                        1e6 * sm_clk_mhz);

    unsigned int mem_clk_mhz;
    TRYOPT(nvmlDeviceGetClockInfo(dev, NVML_CLOCK_MEM, &mem_clk_mhz))
    if (nv_status == NVML_SUCCESS)
      nvml_submit_gauge(dev_name, "frequency", "memory", 1e6 * mem_clk_mhz);

    unsigned int power_mW;
    TRYOPT(nvmlDeviceGetPowerUsage(dev, &power_mW))
    if (nv_status == NVML_SUCCESS)
      nvml_submit_gauge(dev_name, "power", NULL, 1e-3 * power_mW);

    continue;

    // Failures here indicate transient errors or removal of GPU. In either
    // case it will either be resolved or the GPU will no longer be enumerated
    // the next time round.
    catch : WARNING(PLUGIN_NAME
                    ": NVML call \"%s\" failed (%d) on dev at index %d!",
                    nv_errline, nv_status, ix);
    continue;
  }

  return 0;

// Failures here indicate serious misconfiguration; we bail out totally.
catch_nocount:
  ERROR(PLUGIN_NAME ": Failed to enumerate NVIDIA GPUs (\"%s\" returned %d)",
        nv_errline, nv_status);
  return -1;
}

void module_register(void) {
  plugin_register_init(PLUGIN_NAME, nvml_init);
  plugin_register_config(PLUGIN_NAME, nvml_config, config_keys, n_config_keys);
  plugin_register_read(PLUGIN_NAME, nvml_read);
  plugin_register_shutdown(PLUGIN_NAME, nvml_shutdown);
}
