#include "daemon/collectd.h"
#include "daemon/common.h"
#include "daemon/plugin.h"

#include <nvml.h>
#include <stdint.h>
#include <stdio.h>

#define MAX_DEVNAME_LEN 256
#define PLUGIN_NAME "gpu_nvml"

static nvmlReturn_t nv_status = NVML_SUCCESS;
static char *nv_errline = "";

#define TRY_CATCH(f, catch)                                                    \
  if ((nv_status = f) != NVML_SUCCESS) {                                       \
    nv_errline = #f;                                                           \
    goto catch;                                                                \
  }
#define TRY(f) TRY_CATCH(f, catch)
#define WRAPGAUGE(x) ((value_t){.gauge = (gauge_t)(x)})

static const char *config_keys[] = {
    "GPUIndex",
    "IgnoreSelected",
};
static const unsigned int n_config_keys = STATIC_ARRAY_SIZE(config_keys);

static uint64_t conf_match_mask = 0;
static bool conf_mask_is_exclude = 0;

static int nvml_config(const char *key, const char *value) {

  unsigned long device_ix;
  char *eptr;

  if (strcasecmp(key, config_keys[0]) == 0) {
    device_ix = strtoul(value, &eptr, 10);
    if (eptr == value) {
      return -1;
    }
    if (device_ix > 64) {
      return -2;
    }
    conf_match_mask |= (1 << device_ix);
  } else if (strcasecmp(key, config_keys[1])) {
    if
      IS_TRUE(value) { conf_mask_is_exclude = 1; }
  } else {
    return -10;
  }

  return 0;
}

static int nvml_init(void) {
  TRY(nvmlInit());
  return 0;

  catch : ERROR("NVML init failed with %d", nv_status);
  return -1;
}

static int nvml_shutdown(void) {
  TRY(nvmlShutdown())
  return 0;

  catch : ERROR("NVML shutdown failed with %d", nv_status);
  return -1;
}

static void nvml_submit(const char *plugin_instance, const char *type,
                        const char *type_instance, value_t nvml) {

  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &nvml;
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

  nvmlDevice_t dev;
  char dev_name[MAX_DEVNAME_LEN + 1];
  unsigned int fan_speed;
  nvmlUtilization_t utilization;
  nvmlMemory_t meminfo;
  unsigned int core_temp;

  for (int ix = 0; ix < device_count; ix++) {

    int is_match = ((1 << ix) & conf_match_mask) || (conf_match_mask == 0);
    if (conf_mask_is_exclude == !!is_match) {
      continue;
    }

    TRY(nvmlDeviceGetHandleByIndex(ix, &dev));

    dev_name[0] = '\0';
    TRY(nvmlDeviceGetName(dev, dev_name, MAX_DEVNAME_LEN));

    TRY(nvmlDeviceGetMemoryInfo(dev, &meminfo))
    TRY(nvmlDeviceGetUtilizationRates(dev, &utilization))
    TRY(nvmlDeviceGetFanSpeed(dev, &fan_speed))
    TRY(nvmlDeviceGetTemperature(dev, NVML_TEMPERATURE_GPU, &core_temp))

    double pct_mem_used = 100. * (double)meminfo.used / meminfo.total;

    nvml_submit(dev_name, "percent", "GPU", WRAPGAUGE(pct_mem_used));
    nvml_submit(dev_name, "percent", "GPU", WRAPGAUGE(utilization.gpu));
    nvml_submit(dev_name, "fanspeed", "GPU", WRAPGAUGE(fan_speed));
    nvml_submit(dev_name, "temperature", "GPU", WRAPGAUGE(core_temp));
    continue;

    catch : WARNING("NVML call \"%s\" failed with code %d!", nv_errline,
                    nv_status);
    continue;
  }

  return 0;

catch_nocount:
  ERROR("Failed to enumerate NVIDIA GPUs (\"%s\" returned %d)", nv_errline,
        nv_status);
  return -1;
}

void module_register(void) {
  plugin_register_init(PLUGIN_NAME, nvml_init);
  plugin_register_config(PLUGIN_NAME, nvml_config, config_keys, n_config_keys);
  plugin_register_read(PLUGIN_NAME, nvml_read);
  plugin_register_shutdown(PLUGIN_NAME, nvml_shutdown);
}
