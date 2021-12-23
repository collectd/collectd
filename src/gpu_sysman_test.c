/**
 * collectd - src/gpu_sysman_test.c
 *
 * Copyright(c) 2020-2021 Intel Corporation. All rights reserved.
 *
 * Licensed under the same terms and conditions as src/gpu_sysman.c.
 *
 * Authors:
 * - Eero Tamminen <eero.t.tamminen@intel.com>
 *
 * Testing for gpu_sysman.c Sysman API and its error handling.
 *
 * See: https://spec.oneapi.com/level-zero/latest/sysman/PROG.html
 *
 * Building unit-tests:
 *   gcc -DTEST_BUILD -I. -Idaemon -I/path/to/level-zero \
 *       -O3 -g --coverage -Werror -Wall -Wextra -Wformat-security \
 *       gpu_sysman_test.c -o test_plugin_gpu_sysman
 *
 * Running unit-units:
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
 * - Code lines run coverage is best with code compiled using -O3 because
 *   it causes gcc to convert switch-cases to lookup tables.  Builds without
 *   optimizations have significantly lower coverage due to each (trivial
 *   and build-time verifiable) switch-case being considered separately
 *
 *
 * Mock up functionality details:
 * - All functions return only a single property or metric item,
 *   until hitting earlier set call limit, after which they return error
 * - All metric property functions report them coming from subdevice 0
 *   (as non-subdevice cases can be tested on more easily available real HW)
 * - Except for device.prop.type, subdev type in metric property, and
 *   actual metric values in metric state structs, all other struct members
 *   are zeroed
 * - Memory free metric is decreased, all other metric values are increased
 *   after each query
 *
 * Testing validates that:
 * - All registered config variables work and invalid config values are rejected
 * - All mocked up Sysman functions get called when no errors are returned and
 *   count of Sysman calls is always same for plugin init() and read() callbacks
 * - Plugin dispatch API receives correct values for all metrics both in
 *   single-sampling and multi-sampling configurations
 * - Single Sysman call failing during init or metrics queries causes logging
 *   of the failure, and in case of metric queries, disabling of the (only)
 *   relevant metric, and that working for all metrics and Sysman APIs they call
 * - Plugin init, shutdown and re-init works without problems
 */

#include "gpu_sysman.c" /* test this */

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
  unsigned int callbits;

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
  globs.callbits |= 1u << callbit;
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
#define DRV_HANDLE ((ze_driver_handle_t)(0x123456))
#define DEV_HANDLE ((ze_device_handle_t)(0xecced))
#define VAL_HANDLE 0xcaffa

ze_result_t zeInit(ze_init_flags_t flags) {
  if (call_limit(0, "zeInit"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (flags && flags != ZE_INIT_FLAG_GPU_ONLY) {
    return ZE_RESULT_ERROR_INVALID_ENUMERATION;
  }
  return ZE_RESULT_SUCCESS;
}

ze_result_t zeDriverGet(uint32_t *count, ze_driver_handle_t *handles) {
  if (call_limit(1, "zeDriverGet"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (!count)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  if (!*count) {
    *count = 1;
    return ZE_RESULT_SUCCESS;
  }
  if (*count != 1)
    return ZE_RESULT_ERROR_INVALID_SIZE;
  if (!handles)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  handles[0] = DRV_HANDLE;
  return ZE_RESULT_SUCCESS;
}

ze_result_t zeDeviceGet(ze_driver_handle_t drv, uint32_t *count,
                        ze_device_handle_t *handles) {
  if (call_limit(2, "zeDeviceGet"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (drv != DRV_HANDLE)
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  if (!count)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  if (!*count) {
    *count = 1;
    return ZE_RESULT_SUCCESS;
  }
  if (*count != 1)
    return ZE_RESULT_ERROR_INVALID_SIZE;
  if (!handles)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  handles[0] = DEV_HANDLE;
  return ZE_RESULT_SUCCESS;
}

ze_result_t zeDeviceGetProperties(ze_device_handle_t dev,
                                  ze_device_properties_t *props) {
  if (call_limit(3, "zeDeviceGetProperties"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (dev != DEV_HANDLE)
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  if (!props)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  memset(props, 0, sizeof(*props));
  props->type = ZE_DEVICE_TYPE_GPU;
  return ZE_RESULT_SUCCESS;
}

ze_result_t zeDeviceGetMemoryProperties(ze_device_handle_t dev, uint32_t *count,
                                        ze_device_memory_properties_t *props) {
  if (call_limit(4, "zeDeviceGetMemoryProperties"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (dev != DEV_HANDLE)
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  if (!count)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  if (!*count) {
    *count = 1;
    return ZE_RESULT_SUCCESS;
  }
  if (*count != 1)
    return ZE_RESULT_ERROR_INVALID_SIZE;
  if (!props)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  memset(props, 0, sizeof(*props));
  return ZE_RESULT_SUCCESS;
}

/* mock up level-zero sysman device handling API, called during gpu_init() */

ze_result_t zesDeviceGetProperties(zes_device_handle_t dev,
                                   zes_device_properties_t *props) {
  if (call_limit(5, "zesDeviceGetProperties"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (dev != DEV_HANDLE)
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  if (!props)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  memset(props, 0, sizeof(*props));
  return ZE_RESULT_SUCCESS;
}

ze_result_t zesDevicePciGetProperties(zes_device_handle_t dev,
                                      zes_pci_properties_t *props) {
  if (call_limit(6, "zesDevicePciGetProperties"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (dev != DEV_HANDLE)
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  if (!props)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  memset(props, 0, sizeof(*props));
  return ZE_RESULT_SUCCESS;
}

ze_result_t zesDeviceGetState(zes_device_handle_t dev,
                              zes_device_state_t *state) {
  if (call_limit(7, "zesDeviceGetState"))
    return ZE_RESULT_ERROR_DEVICE_LOST;
  if (dev != DEV_HANDLE)
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  if (!state)
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  memset(state, 0, sizeof(*state));
  return ZE_RESULT_SUCCESS;
}

#define INIT_CALL_FUNCS 8
#define INIT_CALL_BITS (((uint64_t)1 << INIT_CALL_FUNCS) - 1)

/* ------------------------------------------------------------------------- */
/* mock up Sysman API metrics querying functions */

#define COUNTER_START 100000 // 100ms
#define COUNTER_INC 20000    // 20ms
#define TIME_START 5000000   // 5s in us
#define TIME_INC 1000000     // 1s in us
#define COUNTER_MAX TIME_INC

/* what should get reported as result of above */
#define COUNTER_RATIO ((double)COUNTER_INC / TIME_INC)

#define FREQ_INIT 300
#define FREQ_INC 50

#define MEMORY_SIZE (1024 * 1024 * 1024)
#define MEMORY_INIT (MEMORY_SIZE / 2) // so that both free & used get same value
#define MEMORY_INC (MEMORY_SIZE / 64)

#define RAS_INIT 0
#define RAS_INC 1

#define TEMP_INIT 10
#define TEMP_INC 5

/* Call bit, metric enumaration function name, its handle type,
 * corresponding zes*GetProperties() function name, its property struct type,
 * corresponding zes*GetState() function name, its state struct type, global
 * variable for intial state values, two increment operations for the global
 * state variable members (or void)
 */
#define ADD_METRIC(callbit, getname, handletype, propname, proptype,           \
                   statename, statetype, statevar, stateinc1, stateinc2)       \
  ze_result_t getname(zes_device_handle_t dev, uint32_t *count,                \
                      handletype *handles) {                                   \
    if (call_limit(callbit, #getname))                                         \
      return ZE_RESULT_ERROR_NOT_AVAILABLE;                                    \
    if (dev != DEV_HANDLE)                                                     \
      return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;                              \
    if (!count)                                                                \
      return ZE_RESULT_ERROR_INVALID_NULL_POINTER;                             \
    if (!*count) {                                                             \
      *count = 1;                                                              \
      return ZE_RESULT_SUCCESS;                                                \
    }                                                                          \
    if (*count != 1)                                                           \
      return ZE_RESULT_ERROR_INVALID_SIZE;                                     \
    if (!handles)                                                              \
      return ZE_RESULT_ERROR_INVALID_NULL_POINTER;                             \
    handles[0] = (handletype)VAL_HANDLE;                                       \
    return ZE_RESULT_SUCCESS;                                                  \
  }                                                                            \
  ze_result_t propname(handletype handle, proptype *prop) {                    \
    proptype value = {.onSubdevice = true};                                    \
    if (call_limit(callbit + 1, #propname))                                    \
      return ZE_RESULT_ERROR_NOT_AVAILABLE;                                    \
    if (handle != (handletype)VAL_HANDLE)                                      \
      return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;                              \
    if (!prop)                                                                 \
      return ZE_RESULT_ERROR_INVALID_NULL_POINTER;                             \
    *prop = value;                                                             \
    return ZE_RESULT_SUCCESS;                                                  \
  }                                                                            \
  ze_result_t statename(handletype handle, statetype *state) {                 \
    if (call_limit(callbit + 2, #statename))                                   \
      return ZE_RESULT_ERROR_NOT_AVAILABLE;                                    \
    if (handle != (handletype)VAL_HANDLE)                                      \
      return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;                              \
    if (!state)                                                                \
      return ZE_RESULT_ERROR_INVALID_NULL_POINTER;                             \
    *state = statevar;                                                         \
    stateinc1;                                                                 \
    stateinc2;                                                                 \
    return ZE_RESULT_SUCCESS;                                                  \
  }

static zes_engine_stats_t engine_stats = {.activeTime = COUNTER_START,
                                          .timestamp = TIME_START};

ADD_METRIC(0, zesDeviceEnumEngineGroups, zes_engine_handle_t,
           zesEngineGetProperties, zes_engine_properties_t,
           zesEngineGetActivity, zes_engine_stats_t, engine_stats,
           engine_stats.activeTime += COUNTER_INC,
           engine_stats.timestamp += TIME_INC);

static zes_freq_state_t freq_state = {.request = FREQ_INIT,
                                      .actual = FREQ_INIT};

ADD_METRIC(3, zesDeviceEnumFrequencyDomains, zes_freq_handle_t,
           zesFrequencyGetProperties, zes_freq_properties_t,
           zesFrequencyGetState, zes_freq_state_t, freq_state,
           freq_state.request += 2 * FREQ_INC, freq_state.actual += FREQ_INC);

static zes_mem_state_t mem_state = {.free = MEMORY_SIZE - MEMORY_INIT,
                                    .size = MEMORY_SIZE};

ADD_METRIC(6, zesDeviceEnumMemoryModules, zes_mem_handle_t,
           zesMemoryGetProperties, zes_mem_properties_t, zesMemoryGetState,
           zes_mem_state_t, mem_state, mem_state.free -= MEMORY_INC,
           mem_state.health ^= ZES_MEM_HEALTH_OK);

static zes_power_energy_counter_t power_counter = {.energy = COUNTER_START,
                                                   .timestamp = TIME_START};

ADD_METRIC(9, zesDeviceEnumPowerDomains, zes_pwr_handle_t,
           zesPowerGetProperties, zes_power_properties_t,
           zesPowerGetEnergyCounter, zes_power_energy_counter_t, power_counter,
           power_counter.energy += COUNTER_INC,
           power_counter.timestamp += TIME_INC);

static int dummy;
static double temperature = TEMP_INIT;

ADD_METRIC(12, zesDeviceEnumTemperatureSensors, zes_temp_handle_t,
           zesTemperatureGetProperties, zes_temp_properties_t,
           zesTemperatureGetState, double, temperature, temperature += TEMP_INC,
           dummy = 0);

ADD_METRIC(15, zesDeviceEnumRasErrorSets, zes_ras_handle_t, zesRasGetProperties,
           zes_ras_properties_t, zesRasGetDummy, int,
           dummy, // dummy as state API differs from others
           dummy = 0, dummy = 0);

ze_result_t zesRasGetState(zes_ras_handle_t handle, ze_bool_t clear,
                           zes_ras_state_t *state) {
  if (call_limit(17, "zesRasGetState")) {
    return ZE_RESULT_ERROR_NOT_AVAILABLE;
  }
  if (handle != (zes_ras_handle_t)VAL_HANDLE) {
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  }
  if (clear) {
    return ZE_RESULT_ERROR_UNSUPPORTED_FEATURE;
  }
  if (!state) {
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
  }
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
  if (call_limit(18, "zesFrequencyGetThrottleTime")) {
    return ZE_RESULT_ERROR_NOT_AVAILABLE;
  }
  if (handle != (zes_freq_handle_t)VAL_HANDLE) {
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  }
  if (!state) {
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
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
  if (call_limit(19, "zesMemoryGetBandwidth")) {
    return ZE_RESULT_ERROR_NOT_AVAILABLE;
  }
  if (handle != (zes_mem_handle_t)VAL_HANDLE) {
    return ZE_RESULT_ERROR_INVALID_NULL_HANDLE;
  }
  if (!state) {
    return ZE_RESULT_ERROR_INVALID_NULL_POINTER;
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

#define QUERY_CALL_FUNCS 20
#define QUERY_CALL_BITS (((uint64_t)1 << QUERY_CALL_FUNCS) - 1)

/* ------------------------------------------------------------------------- */
/* mock up metrics reporting and validation */

typedef struct {
  const char *name;
  /* present also when multisampling */
  const bool multipresent;
  /* metric values are multisampled and present only when multisampling */
  const bool multisampled;
  const double value_init;
  const int value_inc;
  unsigned int count;
  double last;
} metrics_validation_t;

static metrics_validation_t valid_metrics[] = {
    {"all_errors_total", true, false, RAS_INIT, RAS_INC, 0, 0.0},
    {"frequency_mhz/actual/", false, false, FREQ_INIT, FREQ_INC, 0, 0.0},
    {"frequency_mhz/actual_min", true, true, FREQ_INIT, FREQ_INC, 0, 0.0},
    {"frequency_mhz/actual_max", true, true, FREQ_INIT, FREQ_INC, 0, 0.0},
    {"frequency_mhz/request/", false, false, FREQ_INIT, 2 * FREQ_INC, 0, 0.0},
    {"frequency_mhz/request_min", true, true, FREQ_INIT, 2 * FREQ_INC, 0, 0.0},
    {"frequency_mhz/request_max", true, true, FREQ_INIT, 2 * FREQ_INC, 0, 0.0},
    {"memory_bytes/free/", false, false, MEMORY_INIT, -MEMORY_INC, 0, 0.0},
    {"memory_bytes/free_min", true, true, MEMORY_INIT, -MEMORY_INC, 0, 0.0},
    {"memory_bytes/free_max", true, true, MEMORY_INIT, -MEMORY_INC, 0, 0.0},
    {"memory_bytes/used/", false, false, MEMORY_INIT, +MEMORY_INC, 0, 0.0},
    {"memory_bytes/used_min", true, true, MEMORY_INIT, +MEMORY_INC, 0, 0.0},
    {"memory_bytes/used_max", true, true, MEMORY_INIT, +MEMORY_INC, 0, 0.0},
    {"temperature_celsius", true, false, TEMP_INIT, TEMP_INC, 0, 0.0},

    /* while counters increase, per-time incremented value should stay same */
    {"engine_ratio/all", true, false, COUNTER_RATIO, 0, 0, 0.0},
    {"throttling_ratio/gpu", true, false, COUNTER_RATIO, 0, 0, 0.0},
    {"memory_bw_ratio/HBM/system/read", true, false, 2 * COUNTER_RATIO, 0, 0,
     0.0},
    {"memory_bw_ratio/HBM/system/write", true, false, COUNTER_RATIO, 0, 0, 0.0},
    {"power_watts", true, false, COUNTER_RATIO, 0, 0, 0.0},
};

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
    assert(metric->multipresent || !multisampled);

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
      if (metric->value_inc > 0 && strstr(metric->name, "_min")) {
        incrounds += multisampled - config.samples + 1;
      }
      /* max for decreasing metrics is first value in given multisample round */
      else if (metric->value_inc < 0 && strstr(metric->name, "_max")) {
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
    if (last != expected) {
      fprintf(
          stderr,
          "ERROR: expected %g, but got %g value for metric '%s' on round %d\n",
          expected, last, metric->name, incrounds);
      wrong++;
    } else if (globs.verbose & VERBOSE_METRICS) {
      fprintf(stderr, "round %d metric value verified for '%s' (%.2f)\n",
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
  return strcmp(((label_pair_t *)b)->name, ((label_pair_t *)a)->name);
}

/* constructs metric name from metric family name and metric label values */
static void compose_name(char *buf, size_t size, const char *name,
                         metric_t *metric) {
  label_pair_t *label = metric->label.ptr;
  size_t num = metric->label.num;
  assert(num && label);

  /* guarantee stable label ordering i.e. names */
  qsort(label, num, sizeof(*label), cmp_labels);

  /* compose names (metric family + metric label values) */
  size_t len = strlen(name);
  assert(len < size);
  strcpy(buf, name);
  for (size_t i = 0; i < num; i++) {
    const char *name = label[i].name;
    const char *value = label[i].value;
    assert(name && value);
    if (strcmp(name, "pci_bdf") == 0 || strcmp(name, "sub_dev") == 0) {
      /* do not add device PCI ID / sub device IDs to metric name */
      continue;
    }
    len += snprintf(buf + len, sizeof(buf) - len, "/%s", value);
  }
  assert(len < size);
}

static double get_value(metric_type_t type, value_t value) {
  switch (type) {
  case METRIC_TYPE_COUNTER:
    return value.counter;
    break;
  case METRIC_TYPE_GAUGE:
    return value.gauge;
    break;
  default:
    assert(0);
  }
}

/* matches constructed metric names against validation array ones and
 * updates the values accordingly
 */
int plugin_dispatch_metric_family(metric_family_t const *fam) {
  assert(fam && fam->name);
  if (!fam->metric.num) {
    if (globs.verbose & VERBOSE_METRICS) {
      fprintf(stderr, "metric family '%s' with no metrics\n", fam->name);
    }
    assert(!fam->metric.ptr);
    return 0;
  }
  assert(fam->metric.ptr);

  char name[128];
  bool found = false;
  metric_t *metric = fam->metric.ptr;

  for (size_t m = 0; m < fam->metric.num; m++) {
    double value = get_value(fam->type, metric[m].value);
    compose_name(name, sizeof(name), fam->name, &metric[m]);
    if (globs.verbose & VERBOSE_METRICS) {
      fprintf(stderr, "METRIC: %s: %.2f\n", name, value);
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
      }
    }
  }
  assert(found);
  return 0;
}

#define MAX_LABELS 8

/* mock function uses just one large enough metrics array (for testing)
 * instead of increasing it one-by-one, like the real collectd metrics
 * code does
 */
int metric_label_set(metric_t *m, char const *name, char const *value) {
  assert(m && name);
  size_t num = m->label.num;
  label_pair_t *pair = m->label.ptr;
  if (num) {
    assert(num < MAX_LABELS);
    assert(pair);
  } else {
    assert(!pair);
    pair = calloc(MAX_LABELS, sizeof(*pair));
    m->label.ptr = pair;
    assert(pair);
  }
  int i;
  for (i = 0; i < MAX_LABELS; i++) {
    if (!pair[i].name) {
      /* not found -> new label */
      pair[i].name = strdup(name);
      m->label.num++;
      break;
    }
    if (strcmp(name, pair[i].name) == 0) {
      break;
    }
  }
  assert(value); /* removing label with NULL 'value' is not supported */
  free(pair[i].value);
  pair[i].value = strdup(value);
  return 0;
}

int metric_reset(metric_t *m) {
  assert(m);
  size_t num = m->label.num;
  label_pair_t *pair = m->label.ptr;
  if (!num) {
    assert(!pair);
    return 0;
  }
  assert(pair);
  for (int i = 0; i < MAX_LABELS; i++) {
    if (!pair[i].name) {
      break;
    }
    free(pair[i].name);
    free(pair[i].value);
    pair[i].value = pair[i].name = NULL;
    num--;
  }
  assert(!num);
  free(pair);
  m->label.ptr = NULL;
  m->label.num = 0;
  return 0;
}

#define MAX_METRICS 8

/* mock function uses just one large enough metrics array (for testing)
 * instead of increasing it one-by-one, like the real collectd metrics
 * code does
 */
int metric_family_metric_append(metric_family_t *fam, metric_t m) {
  assert(fam);
  size_t num = fam->metric.num;
  metric_t *metric = fam->metric.ptr;
  if (num) {
    assert(num < MAX_METRICS);
    assert(metric);
  } else {
    assert(!metric);
    metric = calloc(MAX_METRICS, sizeof(*metric));
    fam->metric.ptr = metric;
    assert(metric);
  }
  /* copy metric and pointers to its labels */
  metric[num] = m;
  label_pair_t *src = m.label.ptr;
  if (src) {
    /* alloc max size as labels can be added also to family metrics copies */
    label_pair_t *dst = calloc(MAX_LABELS, sizeof(*src));
    metric[num].label.ptr = dst;
    assert(dst);
    for (size_t i = 0; i < m.label.num; i++) {
      dst[i].name = strdup(src[i].name);
      dst[i].value = strdup(src[i].value);
    }
  }
  fam->metric.num++;
  m.family = fam;
  return 0;
}

int metric_family_metric_reset(metric_family_t *fam) {
  metric_t *metric = fam->metric.ptr;
  for (size_t m = 0; m < fam->metric.num; m++) {
    label_pair_t *pair = metric[m].label.ptr;
    for (size_t i = 0; i < metric[m].label.num; i++) {
      free(pair[i].name);
      free(pair[i].value);
    }
    free(pair);
    metric[m].label.ptr = NULL;
    metric[m].label.num = 0;
  }
  free(fam->metric.ptr);
  fam->metric.ptr = NULL;
  fam->metric.num = 0;
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

int plugin_register_config(const char *name,
                           int (*callback)(const char *key, const char *val),
                           const char **keys, int keys_num) {
  assert(name && callback && keys && keys_num > 0);
  registry.name = strdup(name);
  registry.config = callback;

  registry.keys = calloc(keys_num, sizeof(char *));
  assert(registry.keys);
  for (int i = 0; i < keys_num; i++) {
    assert(keys[i]);
    registry.keys[i] = strdup(keys[i]);
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
/* helper code copied from collectd */

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
  msg[sizeof(msg) - 1] = '\0';
  va_end(ap);
  fprintf(stderr, "%s (%s)\n", msg, log_levels[level].name);
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
  struct {
    bool set_false;
    const char *prefix;
  } bool_checks[] = {{enable_metrics, "Disable"}, {!enable_logs, "Log"}};
  /* tests for non-bool config keys */
  struct {
    const char *key;
    const char *value;
    bool success;
  } test[] = {
      {"Foobar", "Foobar", false}, {"Samples", "999", false},
      {"Samples", "-1", false},    {"Samples", "8", true},
      {"Samples", "1", true}, /* set back to default */
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
  for (i = 0; i < registry.key_count; i++) {

    const char *prefix, *key = registry.keys[i];
    for (j = 0; j < 2; j++) {
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
 * set all GPU metrics Disable* flags to 'value', update bitmask of
 * what was changed + set what's the full bitmask, and return count
 * of changed items
 */
static int get_reset_disabled(gpu_disable_t *disabled, bool value, int *mask,
                              int *all) {
  struct {
    const char *name;
    bool *flag;
  } flags[] = {
      {"engine", &disabled->engine},    {"frequency", &disabled->freq},
      {"memory", &disabled->mem},       {"membw", &disabled->membw},
      {"power", &disabled->power},      {"errors", &disabled->ras},
      {"temperature", &disabled->temp}, {"throttle", &disabled->throttle}};
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

/* TEST: metrics queries with multiple samples */
static int test_multisampled_queries(unsigned int prev_rounds,
                                     const char *samples_str,
                                     unsigned int samples) {
  /* change to multiple samples */
  assert(registry.shutdown() == 0);
  assert(samples > 1 && atoi(samples_str) == (int)samples);
  assert(registry.config("Samples", samples_str) == 0);
  assert(registry.init() == 0);

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
          "expect %d API calls for %dx multisampled metrics, %d for all\n",
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
    if (calls_all < calls_sampled || calls_all != globs.api_calls) {
      fprintf(stderr, "ERROR: expected %d (> %d) API calls, got %d\n",
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
/* options parsing & main */

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
  /* more coverage by disabling only some of metrics at init */
  globs.warnings = 0;
  assert(registry.config("DisablePower", "true") == 0);
  assert(registry.init() == 0);
  assert(registry.shutdown() == 0);
  assert(globs.warnings == 0);
  fprintf(stderr, "misc config: PASS\n\n");

  /* init should fail when every metric is disabled */
  globs.warnings = 0;
  fprintf(stderr, "All metrics & logs disabled + init/shutdown...\n");
  assert(test_config_keys(false, false, false) == 0);
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
  assert(globs.callbits == INIT_CALL_BITS);
  fprintf(stderr, "%d calls to all %d Sysman metric init functions\n",
          globs.api_calls, INIT_CALL_FUNCS);
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
  assert(registry.init() == 0);

  fprintf(stderr, "Query all metrics for the first time, with separate errors "
                  "enabled...\n");
  globs.warnings = globs.api_calls = globs.callbits = 0;
  assert(registry.read() == 0);
  /* all Sysman metric query functions got successfully called? */
  assert(globs.callbits == QUERY_CALL_BITS);
  assert(globs.warnings == 0);
  fprintf(stderr, "%d calls to all %d Sysman metric query functions\n",
          globs.api_calls, QUERY_CALL_FUNCS);
  /* per-time counters do not report on first round */
  assert(validate_and_reset_saved_metrics(1, 0) > 0);
  fprintf(stderr, "metrics query round 1: PASS\n\n");

  api_calls = globs.api_calls;

  fprintf(stderr, "Another query for per-timediff metric values + validation "
                  "for all values...\n");
  globs.api_calls = 0;
  assert(registry.read() == 0);
  /* make sure second round does (successfully) same (amount of) calls */
  assert(globs.warnings == 0);
  assert(globs.api_calls == api_calls);
  /* make sure metrics values were correct and all metric types were now
   * reported */
  assert(validate_and_reset_saved_metrics(2, 0) == 0);
  fprintf(stderr, "metrics query round 2: PASS\n\n");

  /* just report total count of errors (should not affect calls) */
  assert(registry.config("DisableSeparateErrors", "true") == 0);

  fprintf(stderr, "One more query to verify increment handling, with only "
                  "error totals...\n");
  globs.api_calls = 0;
  assert(registry.read() == 0);
  assert(globs.warnings == 0);
  assert(globs.api_calls == api_calls);
  /* make sure metrics values were correct and all metric types were reported */
  assert(validate_and_reset_saved_metrics(3, 0) == 0);
  fprintf(stderr, "metrics query round 3: PASS\n\n");

  /* queries with metrics sampling enabled */

  set_verbose(VERBOSE_CALLS_METRICS_SAMPLED, VERBOSE_METRICS_SAMPLED);
  fprintf(stderr, "Check metrics with >1 'Samples' sampling factor...\n");
  assert(test_multisampled_queries(3, "8", 8) == 0);
  fprintf(stderr, "metrics sampling: PASS\n\n");

  /* metrics error handling checks */

  set_verbose(VERBOSE_CALLS_METRICS_LIMIT, VERBOSE_METRICS_LIMIT);
  fprintf(stderr,
          "Test error handling separately for each of the %d query calls...\n",
          api_calls);
  assert(test_query_errors(api_calls) == 0);
  assert(registry.shutdown() == 0);
  fprintf(stderr, "metrics query error handling: PASS\n\n");

  fprintf(stderr, "=> SUCCESS, all tests PASSed!\n");
  return 0;
}
