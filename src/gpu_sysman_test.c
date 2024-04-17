/**
 * collectd - src/gpu_sysman_test.c
 *
 * Copyright(c) 2020-2024 Intel Corporation. All rights reserved.
 *
 * Licensed under the same terms and conditions as src/gpu_sysman.c.
 *
 * Authors:
 * - Eero Tamminen <eero.t.tamminen@intel.com>
 *
 * Testing for gpu_sysman.c Sysman API usage and error handling.
 *
 * Sysman API:
 * - https://spec.oneapi.com/level-zero/latest/sysman/PROG.html
 * - https://spec.oneapi.io/level-zero/latest/sysman/api.html
 *
 * Installing build dependencies:
 * - Fedora: sudo dnf install oneapi-level-zero-devel
 * - Debian: sudo apt install libze-dev
 *
 * Normal build / testing:
 * - build: make test_plugin_gpu_sysman
 * - test: make test_plugin_gpu_sysman.log
 *
 * Direct builds for whole-program analysis, coverage etc (for configured
 * sources, so that "config.h" + other generated includes are present):
 *	gcc -I. -Idaemon -DHAVE_CONFIG_H -DBUILD_STANDALONE --coverage -Werror \
 *	 -O3 -g -Wall -Wextra -Wpedantic -Wcast-align=strict -Wformat-security \
 *	 -Wnull-dereference -Wstrict-overflow=2 -Warray-bounds=2 \
 *	 -D_FORTIFY_SOURCE=2 -Wno-aggressive-loop-optimizations \
 *	 gpu_sysman_test.c -o test_plugin_gpu_sysman
 *
 * Running unit-units directly:
 *	./test_plugin_gpu_sysman
 *
 * Testing for memory leakage:
 *	valgrind --error-exitcode=1 --leak-check=full test_plugin_gpu_sysman
 *
 * Test coverage:
 *	./test_plugin_gpu_sysman
 *	gcov gpu_sysman_test.*
 * Untested lines:
 *	grep '###' gpu_sysman.c.gcov
 *
 * Note:
 * - Coverage of code lines is best when code is compiled using -O3 because
 *   it causes gcc to convert switch-cases to lookup tables.  Builds without
 *   optimizations have significantly lower coverage due to each (trivial
 *   and build-time verifiable) switch-case being considered separately
 *
 * Mock up functionality details:
 * - All functions return only a single property or metric item,
 *   until hitting earlier set call limit, after which they return error
 * - All metric property functions report them coming from subdevice 0
 *   (as non-subdevice cases can be tested on more easily available real HW)
 * - Except for device.prop.type, subdev type in metric property, and
 *   actual metric values in metric state structs, all struct members
 *   are zeroed
 * - After each query, memory free metric is decreased, all other metric
 *   values are increased
 *
 * Testing validates that:
 * - All registered config variables work and invalid config values are rejected
 * - All mocked up Sysman functions get called when no errors are returned and
 *   count of Sysman calls is always same for plugin init() and read() callbacks
 * - .pNext pointers in structs given to (most) Get functions are initialized
 * - Plugin dispatch API receives correct values for all metrics, both in
 *   single-sampling, and in multi-sampling configurations
 * - Every Sysman call failure during init or metrics queries is logged, and
 *   in case of metric queries, the corresponding metric is disabled, and
 *   this happens for all metrics and Sysman APIs they call
 * - Plugin init, shutdown and re-init works without problems
 */

#define SYSMAN_UNIT_TEST_BUILD 1
#include "gpu_sysman.c" /* test this */
#include "testing.h"

/* include metric functions + their dependencies directly, instead of
 * building & linking libcommon.a (like normal collectd builds do)?
 */
#ifdef BUILD_STANDALONE
/* utilities needed from collectd core */
#include "daemon/metric.c"
#include "daemon/resource.c"
#include "utils/common/common.c"
#include "utils/metadata/meta_data.c"
#include "utils/strbuf/strbuf.c"
#include "utils/utf8/utf8.c"
#endif

/* dummy for resource.c::default_resource_attributes() */
char *hostname_g = "hostname";

/* logging check bit, and per-phase logging bits enabling it */
#define VERBOSE_CALLS 1
#define VERBOSE_CALLS_INIT 2
#define VERBOSE_CALLS_INIT_LIMIT 4
#define VERBOSE_CALLS_METRICS 8
#define VERBOSE_CALLS_METRICS_LIMIT 16
#define VERBOSE_CALLS_METRICS_SAMPLED 32

/* logging check bit, and per-phase logging bits enabling it */
#define VERBOSE_METRICS 64
#define VERBOSE_METRICS_NORMAL 128
#define VERBOSE_METRICS_LIMIT 256
#define VERBOSE_METRICS_SAMPLED 512

static struct {
  /* bitmask of enabled verbosity areas */
  unsigned int verbose;

  /* to be able to count & limit Sysman API calls */
  unsigned int api_calls, api_limit;

  /* to verify that all mocked Level-Zero/Sysman functions get called */
  unsigned long callbits;

  /* how many errors & warnings have been logged */
  unsigned int warnings;

  /* how many messages have been logged regardless of log level */
  unsigned int messages;
} globs;

/* set verbosity mask call & metric logging bits based on calls & metrics
 * enabling bits */
static void set_verbose(unsigned int callmask, unsigned int metricmask) {
  if (globs.verbose & callmask) {
    globs.verbose |= VERBOSE_CALLS;
    fprintf(stderr, "Enabling call tracing...\n\n");
  } else {
    globs.verbose &= ~VERBOSE_CALLS;
  }
  if (globs.verbose & metricmask) {
    fprintf(stderr, "Enabling metrics value tracing...\n\n");
    globs.verbose |= VERBOSE_METRICS;
  } else {
    globs.verbose &= ~VERBOSE_METRICS;
  }
}

/* set given bit in the 'callbits' call type tracking bitmask
 * and increase 'api_calls' API call counter.
 *
 * return true if given call should be failed (call=limit)
 */
static bool call_limit(int callbit, const char *name) {
  globs.callbits |= 1ul << callbit;
  globs.api_calls++;

  if (globs.verbose & VERBOSE_CALLS) {
    fprintf(stderr, "CALL %d: %s()\n", globs.api_calls, name);
  }
  if (!globs.api_limit || globs.api_calls != globs.api_limit) {
    return false;
  }
  fprintf(stderr, "LIMIT @ %d: %s()\n", globs.api_calls, name);
  return true;
}

/* ------------------------------------------------------------------------- */
/* mock up level-zero init/driver/device handling API, called during gpu_init()
 */

/* mock up handle values to set & check against */
#define DRV_HANDLE ((zes_driver_handle_t)(0x123456))
#define DEV_HANDLE ((zes_device_handle_t)(0xecced))
#define VAL_HANDLE 0x0caffa00
#define HANDLE2VAL(handle) ((intptr_t)handle & ~0xff)
#define IDX2HANDLE(idx) (VAL_HANDLE | (idx & 0xff))
#define HANDLE2IDX(handle) ((intptr_t)handle & 0xff)

/* driver/device initialization status */
typedef enum {
  L0_NOT_INITIALIZED,
  L0_IS_INITIALIZED,
  L0_DRIVER_INITIALIZED,
  L0_DEVICE_INITIALIZED
} initialized_t;

static initialized_t initialized = L0_NOT_INITIALIZED;

ze_result_t zesInit(zes_init_flags_t flags) {
  if (call_limit(0, "zesInit"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (flags)
    return ZE_RESULT_ERROR_INVALID_ENUMERATION;
  initialized = L0_IS_INITIALIZED;
  return ZE_RESULT_SUCCESS;
}

ze_result_t zesDriverGet(uint32_t *count, zes_driver_handle_t *handles) {
  if (call_limit(1, "zesDriverGet"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (initialized < L0_IS_INITIALIZED)
    return ZE_RESULT_ERROR_UNINITIALIZED;
  if (!count)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  if (!*count) {
    *count = 1;
    return ZE_RESULT_SUCCESS;
  }
  *count = 1;
  if (!handles)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  initialized = L0_DRIVER_INITIALIZED;
  handles[0] = DRV_HANDLE;
  return ZE_RESULT_SUCCESS;
}

ze_result_t zesDeviceGet(zes_driver_handle_t drv, uint32_t *count,
                         zes_device_handle_t *handles) {
  if (call_limit(2, "zesDeviceGet"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (drv != DRV_HANDLE)
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  if (initialized < L0_DRIVER_INITIALIZED)
    return ZE_RESULT_ERROR_UNINITIALIZED;
  if (!count)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  if (!*count) {
    *count = 1;
    return ZE_RESULT_SUCCESS;
  }
  *count = 1;
  if (!handles)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  initialized = L0_DEVICE_INITIALIZED;
  handles[0] = DEV_HANDLE;
  return ZE_RESULT_SUCCESS;
}

/* mock up level-zero core device handling API, called during gpu_init() */

static ze_result_t dev_args_check(int callbit, const char *name,
                                  zes_device_handle_t dev, void *type) {
  if (call_limit(callbit, name))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (dev != DEV_HANDLE)
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  if (!type)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  if (initialized < L0_DEVICE_INITIALIZED)
    return ZE_RESULT_ERROR_UNINITIALIZED;
  return ZE_RESULT_SUCCESS;
}

/* mock up level-zero sysman device handling API, called during gpu_init() */

#define DEV_GET_SET_STRUCT(callbit, getname, structtype, setval)               \
  ze_result_t getname(zes_device_handle_t dev, structtype *to_zero) {          \
    ze_result_t ret = dev_args_check(callbit, #getname, dev, to_zero);         \
    if (ret == ZE_RESULT_SUCCESS) {                                            \
      assert(to_zero->pNext == NULL ||                                         \
             to_zero->stype == ZES_STRUCTURE_TYPE_DEVICE_PROPERTIES);          \
      memset(to_zero, 0, sizeof(*to_zero));                                    \
      setval;                                                                  \
    }                                                                          \
    return ret;                                                                \
  }

DEV_GET_SET_STRUCT(3, zesDeviceGetProperties, zes_device_properties_t, )
DEV_GET_SET_STRUCT(4, zesDevicePciGetProperties, zes_pci_properties_t, )
DEV_GET_SET_STRUCT(5, zesDeviceGetState, zes_device_state_t,
                   to_zero->reset = (ZES_RESET_REASON_FLAG_WEDGED |
                                     ZES_RESET_REASON_FLAG_REPAIR))
DEV_GET_SET_STRUCT(6, zesDeviceGetEccState, zes_device_ecc_properties_t,
                   to_zero->currentState = ZES_DEVICE_ECC_STATE_ENABLED)

/* mock up Sysman API metrics querying functions */

static ze_result_t metric_args_check(int callbit, const char *name,
                                     void *handle, void *type) {
  /* metric being unavailable on some HW / driver combination
   * is more likely for metric queries than device loss, so use
   * ZE_RESULT_ERROR_NOT_AVAILABLE rathen than ZE_RESULT_ERROR_DEVICE_LOST
   */
  if (call_limit(callbit, name))
    return ZE_RESULT_ERROR_NOT_AVAILABLE;
  if (HANDLE2VAL(handle) != VAL_HANDLE)
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  if (!type)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  if (initialized < L0_DEVICE_INITIALIZED)
    return ZE_RESULT_ERROR_UNINITIALIZED;
  return ZE_RESULT_SUCCESS;
}

#define METRIC_ADD_ENUM(callbit, handletype, enumname, all)                    \
  ze_result_t enumname(zes_device_handle_t dev, uint32_t *count,               \
                       handletype *handles) {                                  \
    ze_result_t ret = dev_args_check(callbit, #enumname, dev, count);          \
    if (ret != ZE_RESULT_SUCCESS)                                              \
      return ret;                                                              \
    if (!*count) {                                                             \
      *count = all;                                                            \
      return ZE_RESULT_SUCCESS;                                                \
    }                                                                          \
    if (*count > all) {                                                        \
      return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;                              \
    }                                                                          \
    if (!handles)                                                              \
      return ZE_RESULT_ERROR_INVALID_NULL_POINTER;                             \
    for (size_t i = 0; i < all; i++) {                                         \
      handles[i] = (handletype)IDX2HANDLE(i);                                  \
    }                                                                          \
    return ZE_RESULT_SUCCESS;                                                  \
  }

#define METRIC_ADD_PROP(callbit, handletype, propname, proptype, propvar, all) \
  ze_result_t propname(handletype handle, proptype *prop) {                    \
    ze_result_t ret = metric_args_check(callbit, #propname, handle, prop);     \
    if (ret == ZE_RESULT_SUCCESS) {                                            \
      assert(!prop->pNext);                                                    \
      size_t idx = HANDLE2IDX(handle);                                         \
      assert(idx < all);                                                       \
      *prop = *(&propvar + idx);                                               \
      prop->onSubdevice = true;                                                \
    }                                                                          \
    return ret;                                                                \
  }

static zes_firmware_properties_t fw_props[] = {
    {.name = "FW1", .version = "1"},
    {.name = "FW2", .version = "2"},
};

METRIC_ADD_ENUM(7, zes_firmware_handle_t, zesDeviceEnumFirmwares,
                STATIC_ARRAY_SIZE(fw_props))
METRIC_ADD_PROP(8, zes_firmware_handle_t, zesFirmwareGetProperties,
                zes_firmware_properties_t, fw_props[0],
                STATIC_ARRAY_SIZE(fw_props))

#define INIT_CALL_FUNCS 9
#define INIT_CALL_BITS (((uint64_t)1 << INIT_CALL_FUNCS) - 1)

/* ------------------------------------------------------------------------- */
/* mocked run-time functions called after gpu_init(), and their values */

#define COUNTER_START 100000 // 100ms
#define COUNTER_INC 20000    // 20ms
#define TIME_START 5000000   // 5s in us
#define TIME_INC 2000000     // 2s in us
#define COUNTER_MAX (2 * COUNTER_START + 20 * COUNTER_INC)

/* what should get reported as result of above */
#define COUNTER_RATIO ((double)COUNTER_INC / TIME_INC)
#define COUNTER_RATE (1.0e6 * COUNTER_INC / TIME_INC)
#define COUNTER_MAX_RATIO                                                      \
  (1.0e6 * COUNTER_INC / ((double)COUNTER_MAX * TIME_INC))

#define FREQ_LIMIT 1600
#define FREQ_INIT 300
#define FREQ_INC 50

#define MEMORY_SIZE (1024 * 1024 * 1024)
#define MEMORY_INIT (MEMORY_SIZE / 2) // so that both free & used get same value
#define MEMORY_INC (MEMORY_SIZE / 64)

#define POWER_LIMIT (2.0 * COUNTER_INC / TIME_INC) // in Watts

#define RAS_INIT 0
#define RAS_INC 1

#define TEMP_LIMIT 95
#define TEMP_INIT 10
#define TEMP_INC 5

/* Arguments:
 * - call bit
 * - metric enumaration function name
 * - its handle type
 * - zes*GetProperties() function name
 * - its property struct type
 * - global variable for initial prop values
 * - zes*GetState() function name
 * - its state struct type
 * - global variable for intial state values
 * - two increment operations for the global state variable members (or void)
 */
#define ADD_METRIC(callbit, enumname, handletype, propname, proptype, propvar, \
                   statename, statetype, statevar, stateinc1, stateinc2)       \
                                                                               \
  METRIC_ADD_ENUM(callbit, handletype, enumname, 1)                            \
                                                                               \
  METRIC_ADD_PROP(callbit + 1, handletype, propname, proptype, propvar, 1)     \
                                                                               \
  ze_result_t statename(handletype handle, statetype *state) {                 \
    ze_result_t ret =                                                          \
        metric_args_check(callbit + 2, #statename, handle, state);             \
    if (ret == ZE_RESULT_SUCCESS) {                                            \
      *state = statevar;                                                       \
      stateinc1;                                                               \
      stateinc2;                                                               \
    }                                                                          \
    return ret;                                                                \
  }

static zes_engine_properties_t engine_props;
static zes_engine_stats_t engine_stats = {.activeTime = COUNTER_START,
                                          .timestamp = TIME_START};

ADD_METRIC(0, zesDeviceEnumEngineGroups, zes_engine_handle_t,
           zesEngineGetProperties, zes_engine_properties_t, engine_props,
           zesEngineGetActivity, zes_engine_stats_t, engine_stats,
           engine_stats.activeTime += COUNTER_INC,
           engine_stats.timestamp += TIME_INC)

static zes_freq_properties_t freq_props = {.max = FREQ_LIMIT};
static zes_freq_state_t freq_state = {
    .throttleReasons = ZES_FREQ_THROTTLE_REASON_FLAG_CURRENT_LIMIT,
    .request = FREQ_INIT,
    .actual = FREQ_INIT};

ADD_METRIC(3, zesDeviceEnumFrequencyDomains, zes_freq_handle_t,
           zesFrequencyGetProperties, zes_freq_properties_t, freq_props,
           zesFrequencyGetState, zes_freq_state_t, freq_state,
           freq_state.request += 2 * FREQ_INC, freq_state.actual += FREQ_INC)

static zes_mem_properties_t mem_props;
static zes_mem_state_t mem_state = {.free = MEMORY_SIZE - MEMORY_INIT,
                                    .size = MEMORY_SIZE};

ADD_METRIC(6, zesDeviceEnumMemoryModules, zes_mem_handle_t,
           zesMemoryGetProperties, zes_mem_properties_t, mem_props,
           zesMemoryGetState, zes_mem_state_t, mem_state,
           mem_state.free -= MEMORY_INC, mem_state.health ^= ZES_MEM_HEALTH_OK)

static zes_power_properties_t power_props;
static zes_power_energy_counter_t power_counter = {.energy = COUNTER_START,
                                                   .timestamp = TIME_START};

ADD_METRIC(9, zesDeviceEnumPowerDomains, zes_pwr_handle_t,
           zesPowerGetProperties, zes_power_properties_t, power_props,
           zesPowerGetEnergyCounter, zes_power_energy_counter_t, power_counter,
           power_counter.energy += COUNTER_INC,
           power_counter.timestamp += TIME_INC)

static zes_temp_properties_t temp_props = {.maxTemperature = TEMP_LIMIT};
static double temperature = TEMP_INIT;
static int dummy;

ADD_METRIC(12, zesDeviceEnumTemperatureSensors, zes_temp_handle_t,
           zesTemperatureGetProperties, zes_temp_properties_t, temp_props,
           zesTemperatureGetState, double, temperature, temperature += TEMP_INC,
           dummy = 0)

static zes_ras_properties_t ras_props;

ADD_METRIC(15, zesDeviceEnumRasErrorSets, zes_ras_handle_t, zesRasGetProperties,
           zes_ras_properties_t, ras_props, zesRasGetDummyState, int,
           dummy, // dummy as state API differs from others
           dummy = 0, dummy = 0)

/* needed because there's an extra parameter */
ze_result_t zesRasGetState(zes_ras_handle_t handle, ze_bool_t clear,
                           zes_ras_state_t *state) {
  ze_result_t ret = metric_args_check(17, "zesRasGetState", handle, state);
  if (ret != ZE_RESULT_SUCCESS) {
    return ret;
  }
  if (clear) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
  }
  assert(!state->pNext);
  static uint64_t count = RAS_INIT;
  memset(state, 0, sizeof(zes_ras_state_t));
  /* props default to zeroes i.e. correctable error type,
   * so this needs to be a correctable category
   */
  state->category[ZES_RAS_ERROR_CAT_COMPUTE_ERRORS] = count;
  count += RAS_INC;
  return ZE_RESULT_SUCCESS;
}

ze_result_t zesFrequencyGetThrottleTime(zes_freq_handle_t handle,
                                        zes_freq_throttle_time_t *state) {
  ze_result_t ret =
      metric_args_check(18, "zesFrequencyGetThrottleTime", handle, state);
  if (ret != ZE_RESULT_SUCCESS) {
    return ret;
  }
  static zes_freq_throttle_time_t throttle = {.throttleTime = COUNTER_START,
                                              .timestamp = TIME_START};
  *state = throttle;
  throttle.timestamp += TIME_INC;
  throttle.throttleTime += COUNTER_INC;
  return ZE_RESULT_SUCCESS;
}

ze_result_t zesMemoryGetBandwidth(zes_mem_handle_t handle,
                                  zes_mem_bandwidth_t *state) {
  ze_result_t ret =
      metric_args_check(19, "zesMemoryGetBandwidth", handle, state);
  if (ret != ZE_RESULT_SUCCESS) {
    return ret;
  }
  static zes_mem_bandwidth_t bw = {.readCounter = 2 * COUNTER_START,
                                   .writeCounter = COUNTER_START,
                                   .maxBandwidth = COUNTER_MAX,
                                   .timestamp = TIME_START};
  *state = bw;
  bw.timestamp += TIME_INC;
  bw.readCounter += 2 * COUNTER_INC;
  bw.writeCounter += COUNTER_INC;
  return ZE_RESULT_SUCCESS;
}

ze_result_t zesPowerGetLimits(zes_pwr_handle_t handle,
                              zes_power_sustained_limit_t *sustained,
                              zes_power_burst_limit_t *burst,
                              zes_power_peak_limit_t *peak) {
  void *check = NULL; // something must be requested
  if (sustained) {
    check = sustained;
    sustained->enabled = true;
    sustained->interval = 2 * TIME_INC / 1000; // 2x to get this skipped
    sustained->power = 2 * 1000 * POWER_LIMIT; // mW
  }
  if (burst) {
    check = burst;
    burst->enabled = true;
    burst->power = 1000 * POWER_LIMIT;
  }
  if (peak) {
    check = NULL; // not supported
  }
  return metric_args_check(20, "zesPowerGetLimits", handle, check);
}

static zes_fabric_port_properties_t fabric_props = {
    .maxRxSpeed = {.width = 8, .bitRate = COUNTER_MAX},
    .maxTxSpeed = {.width = 8, .bitRate = COUNTER_MAX}};
static zes_fabric_port_state_t port_state = {
    .status = ZES_FABRIC_PORT_STATUS_HEALTHY};

/* .quality should be set only on degraded, .reasons on failed .status, this
 * increases them without changing status to increase coverage */
ADD_METRIC(21, zesDeviceEnumFabricPorts, zes_fabric_port_handle_t,
           zesFabricPortGetProperties, zes_fabric_port_properties_t,
           fabric_props, zesFabricPortGetState, zes_fabric_port_state_t,
           port_state, port_state.qualityIssues += 1,
           port_state.failureReasons += 1)

/* fabric ports have more functions than the other metrics */
ze_result_t zesFabricPortGetLinkType(zes_fabric_port_handle_t handle,
                                     zes_fabric_link_type_t *state) {
  ze_result_t ret =
      metric_args_check(24, "zesFabricPortGetLinkType", handle, state);
  if (ret == ZE_RESULT_SUCCESS) {
    static zes_fabric_link_type_t port = {.desc = "DummyLink"};
    *state = port;
  }
  return ret;
}

ze_result_t zesFabricPortGetConfig(zes_fabric_port_handle_t handle,
                                   zes_fabric_port_config_t *config) {
  ze_result_t ret =
      metric_args_check(25, "zesFabricPortGetConfig", handle, config);
  if (ret == ZE_RESULT_SUCCESS) {
    assert(!config->pNext);
    memset(config, 0, sizeof(*config));
  }
  return ret;
}

ze_result_t zesFabricPortGetThroughput(zes_fabric_port_handle_t handle,
                                       zes_fabric_port_throughput_t *state) {
  ze_result_t ret =
      metric_args_check(26, "zesFabricPortGetThroughput", handle, state);
  if (ret == ZE_RESULT_SUCCESS) {
    static zes_fabric_port_throughput_t bw = {.rxCounter = 2 * COUNTER_START,
                                              .txCounter = COUNTER_START,
                                              .timestamp = TIME_START};
    *state = bw;
    bw.timestamp += TIME_INC;
    bw.rxCounter += 2 * COUNTER_INC;
    bw.txCounter += COUNTER_INC;
  }
  return ret;
}

#define QUERY_CALL_FUNCS 27
#define QUERY_CALL_BITS (((uint64_t)1 << QUERY_CALL_FUNCS) - 1)

/* ------------------------------------------------------------------------- */
/* bitmask for the calls that happen only on successive query rounds:
 * - zesPowerGetLimits (20)
 * (due to them being inside 'old->timestamp' check)
 */
#define QUERY_MULTI_BITS (1 << 20)

/* ------------------------------------------------------------------------- */
/* mock up metrics reporting and validation */

typedef struct {
  const char *name;
  /* present also when multisampling */
  const bool multipresent;
  /* metric values are multisampled and present only when multisampling */
  const bool multisampled;
  const double value_init;
  const double value_inc;
  unsigned int count;
  double last;
} metrics_validation_t;

#define FREQ_RATIO_INIT ((double)(FREQ_INIT) / (FREQ_LIMIT))
#define FREQ_RATIO_INC ((double)(FREQ_INC) / (FREQ_LIMIT))

#define TEMP_RATIO_INIT ((double)(TEMP_INIT) / (TEMP_LIMIT))
#define TEMP_RATIO_INC ((double)(TEMP_INC) / (TEMP_LIMIT))

#define MEM_RATIO_INIT ((double)MEMORY_INIT / MEMORY_SIZE)
#define MEM_RATIO_INC ((double)MEMORY_INC / MEMORY_SIZE)

static metrics_validation_t valid_metrics[] = {
    /* gauge value changes */
    {"all_errors", true, false, RAS_INIT, RAS_INC, 0, 0.0},
    {"frequency_mhz/actual/current/gpu/min", true, true, FREQ_INIT, FREQ_INC, 0,
     0.0},
    {"frequency_mhz/actual/current/gpu/max", true, true, FREQ_INIT, FREQ_INC, 0,
     0.0},
    {"frequency_mhz/actual/current/gpu", false, false, FREQ_INIT, FREQ_INC, 0,
     0.0},
    {"frequency_mhz/request/current/gpu/min", true, true, FREQ_INIT,
     2 * FREQ_INC, 0, 0.0},
    {"frequency_mhz/request/current/gpu/max", true, true, FREQ_INIT,
     2 * FREQ_INC, 0, 0.0},
    {"frequency_mhz/request/current/gpu", false, false, FREQ_INIT, 2 * FREQ_INC,
     0, 0.0},
    {"frequency_ratio/actual/current/gpu/min", true, true, FREQ_RATIO_INIT,
     FREQ_RATIO_INC, 0, 0.0},
    {"frequency_ratio/actual/current/gpu/max", true, true, FREQ_RATIO_INIT,
     FREQ_RATIO_INC, 0, 0.0},
    {"frequency_ratio/actual/current/gpu", false, false, FREQ_RATIO_INIT,
     FREQ_RATIO_INC, 0, 0.0},
    {"frequency_ratio/request/current/gpu/min", true, true, FREQ_RATIO_INIT,
     2 * FREQ_RATIO_INC, 0, 0.0},
    {"frequency_ratio/request/current/gpu/max", true, true, FREQ_RATIO_INIT,
     2 * FREQ_RATIO_INC, 0, 0.0},
    {"frequency_ratio/request/current/gpu", false, false, FREQ_RATIO_INIT,
     2 * FREQ_RATIO_INC, 0, 0.0},
    {"memory_used_bytes/HBM/system/min", true, true, MEMORY_INIT, MEMORY_INC, 0,
     0.0},
    {"memory_used_bytes/HBM/system/max", true, true, MEMORY_INIT, MEMORY_INC, 0,
     0.0},
    {"memory_used_bytes/HBM/system", false, false, MEMORY_INIT, MEMORY_INC, 0,
     0.0},
    {"memory_usage_ratio/HBM/system/min", true, true, MEM_RATIO_INIT,
     MEM_RATIO_INC, 0, 0.0},
    {"memory_usage_ratio/HBM/system/max", true, true, MEM_RATIO_INIT,
     MEM_RATIO_INC, 0, 0.0},
    {"memory_usage_ratio/HBM/system", false, false, MEM_RATIO_INIT,
     MEM_RATIO_INC, 0, 0.0},
    {"temperature_celsius", true, false, TEMP_INIT, TEMP_INC, 0, 0.0},
    {"temperature_ratio", true, false, TEMP_RATIO_INIT, TEMP_RATIO_INC, 0, 0.0},

    /* while counters increase, per-time incremented value should stay same */
    {"energy_joules", true, false, COUNTER_START / 1e6, COUNTER_INC / 1e6, 0,
     0.0},
    {"engine_ratio/all", true, false, COUNTER_RATIO, 0, 0, 0.0},
    {"engine_use_seconds/all", true, false, COUNTER_START / 1e6,
     COUNTER_INC / 1e6, 0, 0.0},
    {"fabric_port_bytes/healthy/off/read", true, false, 2 * COUNTER_START,
     2 * COUNTER_INC, 0, 0.0},
    {"fabric_port_bytes/healthy/off/write", true, false, COUNTER_START,
     COUNTER_INC, 0, 0.0},
    {"fabric_port_bytes_per_second/healthy/off/read", true, false,
     2 * COUNTER_RATE, 0, 0, 0.0},
    {"fabric_port_bytes_per_second/healthy/off/write", true, false,
     COUNTER_RATE, 0, 0, 0.0},
    {"fabric_port_ratio/healthy/off/read", true, false, 2 * COUNTER_MAX_RATIO,
     0, 0, 0.0},
    {"fabric_port_ratio/healthy/off/write", true, false, COUNTER_MAX_RATIO, 0,
     0, 0.0},
    {"memory_bw_bytes/HBM/system/read", true, false, 2 * COUNTER_START,
     2 * COUNTER_INC, 0, 0.0},
    {"memory_bw_bytes/HBM/system/write", true, false, COUNTER_START,
     COUNTER_INC, 0, 0.0},
    {"memory_bw_bytes_per_second/HBM/system/read", true, false,
     2 * COUNTER_RATE, 0, 0, 0.0},
    {"memory_bw_bytes_per_second/HBM/system/write", true, false, COUNTER_RATE,
     0, 0, 0.0},
    {"memory_bw_ratio/HBM/system/read", true, false, 2 * COUNTER_MAX_RATIO, 0,
     0, 0.0},
    {"memory_bw_ratio/HBM/system/write", true, false, COUNTER_MAX_RATIO, 0, 0,
     0.0},
    {"power_ratio", true, false, COUNTER_INC / POWER_LIMIT / TIME_INC, 0, 0,
     0.0},
    {"power_watts", true, false, COUNTER_RATIO, 0, 0, 0.0},
    {"throttled_seconds/gpu", true, false, COUNTER_START / 1e6,
     COUNTER_INC / 1e6, 0, 0.0},
    {"throttled_ratio/gpu", true, false, COUNTER_RATIO, 0, 0, 0.0},
};

static int expect_double_eq(double expect, double actual) {
  /* WA for "unused-variable" warning on testing.h */
  fail_count__++;
  /* macro returns -1 on non-equality, continues if equal */
  EXPECT_EQ_DOUBLE(expect, actual);
  fail_count__--;
  return 0;
}

/* VALIDATE: reset tracked metrics values and return count of how many
 * metrics were not set since last reset.
 *
 * For non-zero 'base_rounds' parameter values, last metrics value
 * will be compared to expected value for that round, and if there's
 * a mismatch, error is logged and that metrics is also included to
 * returned count.
 *
 * If 'multisampled' is non-zero, rounds is increased by suitable
 * amount based on 'config.samples' value and metric 'multisample'
 * flag.
 */
static int validate_and_reset_saved_metrics(unsigned int base_rounds,
                                            unsigned int multisampled) {
  assert(config.samples > 0);
  int wrong = 0, missing = 0;
  for (int i = 0; i < (int)STATIC_ARRAY_SIZE(valid_metrics); i++) {
    metrics_validation_t *metric = &valid_metrics[i];
    if (!metric->count) {
      bool missed = false;
      if (multisampled) {
        if (metric->multipresent) {
          missed = true;
        }
      } else {
        if (!metric->multisampled) {
          missed = true;
        }
      }
      if (missed) {
        fprintf(stderr, "expected metric type '%s' not reported\n",
                metric->name);
        missing++;
      }
      continue;
    }
    /* verify metrics array above is correctly filled */
    if (multisampled && !metric->multipresent) {
      fprintf(stderr, "%s: %s / %s = %g (%d)\n", metric->name,
              metric->multipresent ? "multipresent" : "-",
              metric->multisampled ? "multisampled" : "-", metric->last,
              metric->count);
      abort();
    }

    double last = metric->last;
    metric->last = 0.0;
    metric->count = 0;
    if (!base_rounds) {
      /* no metric value checking requested */
      continue;
    }
    int incrounds = base_rounds - 1;
    if (multisampled && metric->multisampled) {
      /* min for increasing metrics is first value in given multisample round */
      if (metric->value_inc > 0.0 && strstr(metric->name, "/min")) {
        incrounds += multisampled - config.samples + 1;
      }
      /* max for decreasing metrics is first value in given multisample round */
      else if (metric->value_inc < 0.0 && strstr(metric->name, "/max")) {
        incrounds += multisampled - config.samples + 1;
      } else {
        /* for all others, it's the last value sampled */
        incrounds += multisampled;
      }
    } else {
      /* other metrics are sampled only at sample intervals */
      incrounds += multisampled / config.samples;
    }
    double expected = metric->value_init + incrounds * metric->value_inc;
    if (expect_double_eq(expected, last) != 0) {
      fprintf(
          stderr,
          "ERROR: expected %g, but got value %g for metric '%s' on round %d\n",
          expected, last, metric->name, incrounds);
      wrong++;
    } else if (globs.verbose & VERBOSE_METRICS) {
      fprintf(stderr, "round %d metric value verified for '%s' (%g)\n",
              incrounds, metric->name, expected);
    }
  }
  if (missing && (globs.verbose & VERBOSE_METRICS)) {
    fprintf(stderr, "%d metric(s) missing\n", missing);
  }
  return missing + wrong;
}

/* sort in reverse order so 'type' label comes first */
static int cmp_labels(const void *a, const void *b) {
  return strcmp(((const label_pair_t *)b)->name,
                ((const label_pair_t *)a)->name);
}

/* constructs metric name from metric family name and metric label values */
static void compose_name(char *buf, size_t bufsize, const char *name,
                         metric_t *metric) {
  label_pair_t *label = metric->label.ptr;
  size_t num = metric->label.num;
  assert(num && label);

  /* guarantee stable label ordering i.e. names */
  qsort(label, num, sizeof(*label), cmp_labels);

  /* compose names (metric family + metric label values) */
  size_t len = strlen(name);
  assert(len < bufsize);
  sstrncpy(buf, name, bufsize);
  for (size_t i = 0; i < num; i++) {
    const char *name = label[i].name;
    const char *value = label[i].value;
    assert(name && value);
    if (strcmp(name, "pci_bdf") == 0 || strcmp(name, "sub_dev") == 0 ||
        strcmp(name, "remote") == 0 || strcmp(name, "port") == 0 ||
        strcmp(name, "link") == 0 || strcmp(name, "model") == 0 ||
        strcmp(name, "issues") == 0) {
      /* do not add numeric IDs, HW labels, or issues to metric name */
      continue;
    }
    len += snprintf(buf + len, bufsize - len, "/%s", value);
  }
  assert(len < bufsize);
}

/* matches constructed metric names against validation array ones and
 * updates the values accordingly
 */
int plugin_dispatch_metric_family(metric_family_t const *fam) {
  assert(fam && fam->name && fam->metric.num && fam->metric.ptr);

  bool found = false;
  char name[128] = "\0";
  metric_t *metric = fam->metric.ptr;

  for (size_t m = 0; m < fam->metric.num; m++) {
    double value = metric2double(fam->type, metric[m].value);
    compose_name(name, sizeof(name), fam->name, &metric[m]);
    if (globs.verbose & VERBOSE_METRICS) {
      fprintf(stderr, "METRIC: %s: %g\n", name, value);
    }
    /* for now, ignore other errors than for all_errors */
    if (strstr(name, "errors") && !strstr(name, "all_errors")) {
      return 0;
    }
    for (int v = 0; v < (int)STATIC_ARRAY_SIZE(valid_metrics); v++) {
      metrics_validation_t *valid = &valid_metrics[v];
      if (strstr(name, valid->name)) {
        valid->last = value;
        valid->count++;
        found = true;
        break;
      }
    }
  }
  if (!found) {
    fprintf(stderr, "ERROR: found no '%s' metrics\n(e.g '%s')\n", fam->name,
            name);
    exit(1);
  }
  return 0;
}

/* ------------------------------------------------------------------------- */
/* mock up of collectd plugin API */

static struct {
  char *name;
  char **keys;
  unsigned int key_count;
  int (*config)(const char *key, const char *val);
  plugin_init_cb init;
  int (*read)(void);
  plugin_shutdown_cb shutdown;
} registry;

__attribute__((noinline)) cdtime_t plugin_get_interval(void) {
  return MS_TO_CDTIME_T(500);
}

int plugin_register_config(const char *name,
                           int (*callback)(const char *key, const char *val),
                           const char **keys, int keys_num) {
  assert(name && callback && keys && keys_num > 0);
  registry.name = sstrdup(name);
  registry.config = callback;

  registry.keys = scalloc(keys_num, sizeof(char *));
  for (int i = 0; i < keys_num; i++) {
    assert(keys[i]);
    registry.keys[i] = sstrdup(keys[i]);
  }
  registry.key_count = keys_num;
  return 0;
}
int plugin_register_init(const char *name, plugin_init_cb callback) {
  assert(name && callback);
  assert(strcmp(name, registry.name) == 0);
  registry.init = callback;
  return 0;
}
int plugin_register_read(const char *name, int (*callback)(void)) {
  assert(name && callback);
  assert(strcmp(name, registry.name) == 0);
  registry.read = callback;
  return 0;
}
int plugin_register_shutdown(const char *name, plugin_shutdown_cb callback) {
  assert(name && callback);
  assert(strcmp(name, registry.name) == 0);
  registry.shutdown = callback;
  return 0;
}

/* ------------------------------------------------------------------------- */
/* helper code partially copied from collectd (initially Copyright Florian
 * Foster) */

static const struct {
  int level;
  const char *name;
} log_levels[] = {{0, "???"},
                  {1, "???"},
                  {2, "???"},
                  {LOG_ERR, "ERROR"},
                  {LOG_WARNING, "WARN"},
                  {LOG_NOTICE, "NOTICE"},
                  {LOG_INFO, "INFO"},
                  {LOG_DEBUG, "DEBUG"}};

/* half based on daemon/plugin.c, for logging */
void plugin_log(int level, const char *format, ...) {
  assert(level >= LOG_ERR && level < (int)STATIC_ARRAY_SIZE(log_levels));
  if (level <= LOG_WARNING) {
    globs.warnings++;
  }
  globs.messages++;
  char msg[1024];
  va_list ap;
  va_start(ap, format);
  vsnprintf(msg, sizeof(msg), format, ap);
  va_end(ap);
  fprintf(stderr, "%s (%s)\n", msg, log_levels[level].name);
}

/* ------------------------------------------------------------------------- */
/* Dummies for unused collectd core functions, needed for linking
 * because "utils/common/common.c" refers both "daemon/utils_cache.c"
 * and "daemon/plugin.c" functionality.
 *
 * Therefore "libcommon.la" needs these too for linking to work.
 */

void daemon_log(__attribute__((unused)) int level,
                __attribute__((unused)) const char *format, ...) {
  assert(0);
}

int uc_get_rate(__attribute__((unused)) metric_t const *m,
                __attribute__((unused)) gauge_t *ret_value) {
  return ENOTSUP;
}

/* ------------------------------------------------------------------------- */
/* TEST: plugin setup & teardown */

static void plugin_register(void) {
  for (int i = 0; i < (int)STATIC_ARRAY_SIZE(log_levels); i++) {
    /* verify log levels match expected */
    assert(log_levels[i].level == i);
  }
  module_register();
  assert(registry.config && registry.init && registry.read &&
         registry.shutdown);
}

/* free test code registry struct allocs after config checks are done
 */
static void plugin_register_free(void) {
  for (unsigned int i = 0; i < registry.key_count; i++) {
    free(registry.keys[i]);
  }
  free(registry.keys);
  registry.keys = NULL;
  free(registry.name);
  registry.name = NULL;
}

/* ------------------------------------------------------------------------- */

/* TEST: config keys. 'check_nonbool' checks non-boolean config keys,
 * 'enable_metrics' enables quering of all metrics, and 'enable_logs' enables
 * all logs as part of testing. return 0 for success
 */
static int test_config_keys(bool check_nonbool, bool enable_metrics,
                            bool enable_logs) {
  /* tests for non-bool config keys */
  struct {
    const char *key;
    const char *value;
    bool success;
  } test[] = {
      {"MetricsOutput", "base", true},
      {"MetricsOutput", "rate", true},
      {"MetricsOutput", "RatiO", true},
      {"MetricsOutput", "RatiO/fooBAR", false},
      {"MetricsOutput", "1", false},
      {"MetricsOutput", "", false},
      {"Foobar", "Foobar", false},
      {"Samples", "999", false},
      {"Samples", "-1", false},
      {"Samples", "8", true},
      /* set back to default */
      {"MetricsOutput", "base:rate:ratio", true},
      {"Samples", "1", true},
  };
  unsigned int i, j;
  int ret, fails = 0;

  if (check_nonbool) {
    for (i = 0; i < STATIC_ARRAY_SIZE(test); i++) {
      ret = registry.config(test[i].key, test[i].value);
      if ((ret == 0) != test[i].success) {
        fprintf(stderr, "ERROR: unexpected config %s with '%s'='%s'\n",
                ret ? "fail" : "success", test[i].key, test[i].value);
        fails++;
      }
    }
  }

  /* make sure that also bool values work */
  struct {
    bool set_false;
    const char *prefix;
  } bool_checks[] = {{enable_metrics, "Disable"}, {!enable_logs, "Log"}};

  for (i = 0; i < registry.key_count; i++) {

    const char *prefix, *key = registry.keys[i];
    for (j = 0; j < STATIC_ARRAY_SIZE(bool_checks); j++) {
      prefix = bool_checks[j].prefix;

      if (strncmp(key, prefix, strlen(prefix))) {
        continue;
      }
      ret = registry.config(key, "true");
      if (bool_checks[j].set_false) {
        ret += registry.config(key, "false");
      }
      if (ret != 0) {
        fprintf(stderr, "ERROR: unexpected '%s' bool config set fail\n", key);
        fails++;
      }
    }
  }
  return fails;
}

/* ------------------------------------------------------------------------- */

/*
 * set all GPU metric disable flags in 'disabled' to 'value', update
 * bitmask of what was changed + set what's the full bitmask, and
 * return count of changed items
 */
static int get_reset_disabled(gpu_disable_t *disabled, bool value, int *mask,
                              int *all) {
  struct {
    const char *name;
    bool *flag;
  } flags[] = {{"engine", &disabled->engine},
               {"fabric", &disabled->fabric},
               {"frequency", &disabled->freq},
               {"memory", &disabled->mem},
               {"membw", &disabled->membw},
               {"power", &disabled->power},
               {"power_ratio", &disabled->power_ratio},
               {"errors", &disabled->ras},
               {"temperature", &disabled->temp},
               {"throttle", &disabled->throttle}};
  *all = 0;
  int count = 0;
  for (int i = 0; i < (int)STATIC_ARRAY_SIZE(flags); i++) {
    if (*(flags[i].flag) != value) {
      if (globs.verbose & VERBOSE_METRICS) {
        fprintf(stderr, "=> %s: %s\n", value ? "DISABLED" : "ENABLED",
                flags[i].name);
      }
      *(flags[i].flag) = value;
      *mask |= (1 << i);
      count++;
    }
    *all |= (1 << i);
  }
  return count;
}

/* TEST: metrics queries error handling, return 0 for success */
static int test_query_errors(unsigned int limit) {
  assert(gpu_count == 1);
  gpu_disable_t *disabled = &(gpus[0].disabled);

  /* enable all metrics */
  int fails, all, mask = 0;
  get_reset_disabled(disabled, false, &mask, &all);

  mask = fails = 0;
  for (; limit > 0; limit--) {
    int count;

    globs.warnings = 0;
    globs.api_calls = 0;
    globs.api_limit = limit;

    if (registry.read() != 0) {
      fprintf(stderr,
              "ERROR: metrics query failed completely with single call fail\n");
      fails++;
    }
    /* there were logged call failures? */
    if (globs.warnings == 0) {
      fprintf(stderr, "ERROR: no errors/warnings reported when call %d fails\n",
              limit);
      fails++;
    }
    /* enable all metrics again & check that exactly one metric type got
     * disabled? */
    count = get_reset_disabled(disabled, false, &mask, &all);
    if (count != 1) {
      fprintf(stderr, "ERROR: %d metric types disabled instead of 1\n", count);
      fails++;
    }
  }
  if (mask != all) {
    fprintf(stderr,
            "ERROR: all metric types were not disabled, expected %x, got %x\n",
            all, mask);
    fails++;
  }
  /* disable all metrics & check read fail */
  globs.warnings = 0;
  get_reset_disabled(disabled, true, &mask, &all);
  registry.read();
  if (registry.read() == 0) {
    fprintf(
        stderr,
        "ERROR: metrics query succceeded although all metrics were disabled\n");
    fails++;
  }
  globs.warnings = globs.api_limit = 0;
  return fails;
}

/* change sampling rate to given, implies plugin reset */
static void change_sampling_reset(const char *samples) {
  fprintf(stderr, "Setting 'Samples' to '%s' and reseting plugin\n", samples);
  assert(registry.shutdown() == 0);
  assert(atoi(samples) > 0);
  assert(registry.config("Samples", samples) == 0);
  assert(registry.init() == 0);
}

/* TEST: metrics queries with multiple samples, return number of fails */
static int test_multisampled_queries(unsigned int prev_rounds,
                                     unsigned int samples) {
  assert(samples > 1);
  /* first 'samples' rounds to prime counter metrics & count API calls */
  if (globs.verbose & VERBOSE_METRICS) {
    fprintf(stderr, "METRIC: first %d multisample rounds for query priming:\n",
            samples);
  }
  unsigned int i, calls_sampled = 0;
  for (i = 1; i <= samples; i++) {
    globs.api_calls = 0;
    assert(registry.read() == 0);
    assert(globs.warnings == 0);
    if (!calls_sampled) {
      calls_sampled = globs.api_calls;
    }
  }
  unsigned int calls_all = globs.api_calls;
  fprintf(stderr,
          "expect %d API calls for %dx multisampled metrics, >= %d for all\n",
          calls_sampled, samples, calls_all);

  /* additional 2x 'samples' rounds to verify the results */
  if (globs.verbose & VERBOSE_METRICS) {
    fprintf(stderr,
            "METRIC: additional %d+%d multisample rounds for verification:\n",
            samples, samples);
  }
  int fails = 0;
  for (/* i=samples */; i <= 3 * samples; i++) {
    globs.api_calls = 0;
    assert(registry.read() == 0);
    assert(globs.warnings == 0);
    /* verify same amount of calls on every run, separately for
     * the case when only sampled metrics are read, and when all are
     */
    if (i % samples > 0) {
      if (calls_sampled != globs.api_calls) {
        fprintf(stderr, "ERROR: expected %d API calls, got %d\n", calls_sampled,
                globs.api_calls);
        fails++;
      }
      continue;
    }
    /* number of calls may differ on multisampled rounds, so just
     * check that at least expected number of them is done
     */
    if (calls_all < calls_sampled || calls_all > globs.api_calls) {
      fprintf(stderr, "ERROR: expected >= %d (and > %d) API calls, got %d\n",
              calls_all, calls_sampled, globs.api_calls);
      fails++;
    }
    fails += validate_and_reset_saved_metrics(prev_rounds, i);
  }
  /* back to single sample */
  assert(registry.shutdown() == 0);
  assert(registry.config("Samples", "1") == 0);
  assert(registry.init() == 0);
  return fails;
}

/* TEST: error handling for Sysman calls during plugin init, return 0 for
 * success */
static int test_init_errors(unsigned int limit) {
  int fails = 0;
  for (; limit > 0; limit--) {
    globs.warnings = 0;
    globs.api_calls = 0;
    globs.api_limit = limit;

    if (registry.init() == 0) {
      fprintf(stderr, "ERROR: metrics init succeeded despite call %d failing\n",
              limit);
      fails++;
      if (registry.shutdown() != 0) {
        fprintf(stderr, "ERROR: plugin shutdown failed after init succeeded\n");
        fails++;
      }
    }
    if (globs.warnings == 0) {
      fprintf(stderr, "ERROR: no errors/warnings reported when call %d fails\n",
              limit);
      fails++;
    }
  }
  globs.warnings = globs.api_limit = 0;
  return fails;
}

/* ------------------------------------------------------------------------- */
/* options parsing, call count checks and main */

static void parse_options(int argc, const char **argv) {
  static const struct {
    const char *opt;
    unsigned int bit;
    const char *desc;
  } opts[] = {{"-ci", VERBOSE_CALLS_INIT, "Trace calls during metric inits"},
              {"-cil", VERBOSE_CALLS_INIT_LIMIT,
               "Trace calls during N call-limited init runs"},
              {"-cm", VERBOSE_CALLS_METRICS,
               "Trace calls during normal metric query runs"},
              {"-cms", VERBOSE_CALLS_METRICS_SAMPLED,
               "Trace calls during N sampled metric runs"},
              {"-cml", VERBOSE_CALLS_METRICS_LIMIT,
               "Trace calls during N call-limited metric runs"},
              {"-mn", VERBOSE_METRICS_NORMAL,
               "Log metric values in normal (samples=1) runs"},
              {"-ms", VERBOSE_METRICS_SAMPLED,
               "Log metric values in N sampled (samples>1) runs"},
              {"-ml", VERBOSE_METRICS_LIMIT,
               "Log metric values in N call-limited runs"}};
  int i, j, count = STATIC_ARRAY_SIZE(opts);

  for (i = 1; i < argc; i++) {
    for (j = 0; j < count; j++) {
      if (strcmp(argv[i], opts[j].opt) != 0) {
        continue;
      }
      globs.verbose |= opts[j].bit;
      break;
    }
    if (j >= count) {
      const char *basename = strrchr(argv[0], '/');
      fprintf(stderr, "\nUsage: %s [options]\n\nOptions:\n", basename);
      for (int j = 0; j < count; j++) {
        fprintf(stderr, "\t%s\t%s\n", opts[j].opt, opts[j].desc);
      }
      fprintf(stderr, "\n\t(Only Sysman API calls are traced.)\n");
      exit(1);
    }
  }
}

void check_call_counts(const char *type, unsigned long reqbits) {
  int count;
  bool reqbit, callbit;
  unsigned long callbits = globs.callbits;
  for (count = 0; reqbits | callbits;) {
    reqbit = reqbits & 1;
    callbit = callbits & 1;
    if (reqbit != callbit) {
      if (reqbit) {
        fprintf(stderr,
                "ERROR: call to Sysman API metric %s function %d missing\n",
                type, count);
      } else {
        fprintf(stderr,
                "ERROR: unexpected call to Sysman API metric %s function %d\n",
                type, count);
      }
      exit(1);
    } else if (callbit) {
      count++;
    }
    callbits >>= 1;
    reqbits >>= 1;
  }
  fprintf(stderr, "%d calls to expected %d Sysman metric %s functions\n",
          globs.api_calls, count, type);
}

int main(int argc, const char **argv) {
  parse_options(argc, argv);

  plugin_register();

  /* config & minimal init checks */

  set_verbose(VERBOSE_CALLS_INIT, 0);

  fprintf(stderr, "Default plugin config + 2*init + shutdown...\n");
  assert(registry.init() == 0);
  /* 2nd init call should be no-op with log message about that */
  globs.messages = 0;
  assert(registry.init() == 0);
  assert(globs.messages > 0);
  assert(registry.shutdown() == 0);
  fprintf(stderr, "default init/shutdown: PASS\n\n");

  /* check misc config options, enable all metrics & extra plugin logging */
  fprintf(stderr, "Misc config options checks...\n");
  globs.warnings = 0;
  assert(test_config_keys(true, true, true) == 0);
  assert(globs.warnings > 0);
  globs.warnings = 0;
  assert(registry.init() == 0);
  assert(registry.shutdown() == 0);
  /* more coverage by disabling only some of metrics at init */
  assert(registry.config("DisableEngine", "true") == 0);
  assert(registry.config("DisablePower", "true") == 0);
  /* for multi-sample init checks */
  assert(registry.config("Samples", "8") == 0);
  assert(registry.init() == 0);
  assert(registry.shutdown() == 0);
  assert(registry.config("Samples", "1") == 0);
  assert(globs.warnings == 0);
  fprintf(stderr, "misc config: PASS\n\n");

  /* init should fail when every metric is disabled */
  globs.warnings = 0;
  fprintf(stderr,
          "All metrics disabled + logs disabled&enabled + init/shutdown...\n");
  assert(test_config_keys(false, false, false) == 0);
  assert(registry.init() != 0);
  assert(globs.warnings > 0);
  /* undefined whether shutdown() returns fail or success after failed init */
  registry.shutdown();
  /* all metrics disabled with GPU info enabled */
  globs.warnings = 0;
  assert(registry.config("LogGpuInfo", "true") == 0);
  assert(registry.init() != 0);
  assert(globs.warnings > 0);
  /* undefined whether shutdown() returns fail or success after failed init */
  registry.shutdown();
  fprintf(stderr, "metrics disabled init/shutdown: PASS\n\n");

  /* config tests done, re-enable metrics */
  globs.warnings = 0;
  assert(test_config_keys(false, true, false) == 0);
  plugin_register_free();

  /* full init checks */

  /* make sure all Sysman functions are called at init */
  assert(registry.config("LogGpuInfo", "true") == 0);
  assert(globs.warnings == 0);

  fprintf(stderr,
          "Check whether init with GPU info does all Sysman calls...\n");
  globs.warnings = globs.api_calls = globs.callbits = 0;
  assert(registry.init() == 0);
  /* all Sysman metric init functions got called? */
  check_call_counts("init", INIT_CALL_BITS);
  assert(registry.shutdown() == 0);
  assert(globs.warnings == 0);
  fprintf(stderr, "full init: PASS\n\n");

  /* skip Sysman functions which failure isn't fatal for init */
  assert(registry.config("LogGpuInfo", "false") == 0);

  /* count relevant API calls */
  globs.warnings = globs.api_calls = 0;
  fprintf(stderr, "No init errors/warnings with GPU info disabled...\n");
  assert(registry.init() == 0);
  assert(registry.shutdown() == 0);
  assert(globs.warnings == 0);
  fprintf(stderr, "init warnings: PASS\n\n");

  set_verbose(VERBOSE_CALLS_INIT_LIMIT, 0);

  unsigned int api_calls = globs.api_calls;

  fprintf(stderr,
          "Error handling for each of %d relevant init Sysman calls...\n",
          api_calls);
  assert(test_init_errors(api_calls) == 0);
  /* undefined whether shutdown() returns fail or success after failed init */
  registry.shutdown();
  fprintf(stderr, "init error handling: PASS\n\n");

  /* metrics query & value checks */

  assert(registry.config("DisableSeparateErrors", "false") == 0);
  set_verbose(VERBOSE_CALLS_METRICS, VERBOSE_METRICS_NORMAL);
  assert(registry.config("LogMetrics", "true") == 0);
  assert(registry.init() == 0);

  fprintf(stderr, "Query all metrics for the first time, with separate errors "
                  "enabled...\n");
  globs.warnings = globs.api_calls = globs.callbits = 0;
  assert(registry.read() == 0);
  /* all Sysman metric query first round functions got successfully called? */
  check_call_counts("query", QUERY_CALL_BITS ^ QUERY_MULTI_BITS);
  assert(globs.warnings == 0);
  /* per-time counters do not report on first round */
  assert(validate_and_reset_saved_metrics(1, 0) > 0);
  assert(registry.config("LogMetrics", "false") == 0);
  fprintf(stderr, "metrics query round 1: PASS\n\n");

  api_calls = globs.api_calls;
  globs.api_calls = 0;

  fprintf(stderr, "Another query for per-timediff metric values + validation "
                  "for all values...\n");
  assert(registry.read() == 0);
  /* make sure second round calls all Sysman API functions */
  check_call_counts("query", QUERY_CALL_BITS);
  assert(globs.warnings == 0);
  /* second round may make additional calls */
  assert(globs.api_calls >= api_calls);
  /* make sure metrics values were correct and all metric types were now
   * reported */
  assert(validate_and_reset_saved_metrics(2, 0) == 0);
  fprintf(stderr, "metrics query round 2: PASS\n\n");

  /* just report total count of errors (should not affect calls) */
  assert(registry.config("DisableSeparateErrors", "true") == 0);

  api_calls = globs.api_calls;
  globs.api_calls = 0;

  fprintf(stderr, "One more query to verify increment handling, with only "
                  "error totals...\n");
  assert(registry.read() == 0);
  assert(globs.warnings == 0);
  assert(globs.api_calls == api_calls);
  /* make sure metrics values were correct and all metric types were reported */
  assert(validate_and_reset_saved_metrics(3, 0) == 0);
  fprintf(stderr, "metrics query round 3: PASS\n\n");

  /* queries with metrics sampling enabled */

  set_verbose(VERBOSE_CALLS_METRICS_SAMPLED, VERBOSE_METRICS_SAMPLED);
  fprintf(stderr, "Check metrics with >1 'Samples' sampling factor...\n");
  change_sampling_reset("8");
  assert(test_multisampled_queries(3, 8) == 0);
  fprintf(stderr, "metrics sampling: PASS\n\n");

  /* metrics error handling checks */

  set_verbose(VERBOSE_CALLS_METRICS_LIMIT, VERBOSE_METRICS_LIMIT);
  fprintf(stderr,
          "Test error handling separately for each of the %d query calls...\n",
          api_calls);
  /* disable multisampling & do one query round to guarantee
   * that all L0 calls are done on every read */
  change_sampling_reset("1");
  assert(registry.read() == 0);
  /* enable extra logging to increase coverage */
  assert(registry.config("LogGpuInfo", "true") == 0);
  /* verify metric error handling */
  assert(test_query_errors(api_calls) == 0);
  assert(registry.shutdown() == 0);
  fprintf(stderr, "metrics query error handling: PASS\n\n");

  fprintf(stderr, "=> SUCCESS, all tests PASSed!\n");
  return 0;
}
