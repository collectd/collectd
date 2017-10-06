/**
 * collectd - src/intel_pmu.c
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Serhiy Pshyk <serhiyx.pshyk@intel.com>
 **/

#include "collectd.h"
#include "common.h"

#include <jevents.h>
#include <jsession.h>

#define PMU_PLUGIN "intel_pmu"

#define HW_CACHE_READ_ACCESS                                                   \
  (((PERF_COUNT_HW_CACHE_OP_READ) << 8) |                                      \
   ((PERF_COUNT_HW_CACHE_RESULT_ACCESS) << 16))

#define HW_CACHE_WRITE_ACCESS                                                  \
  (((PERF_COUNT_HW_CACHE_OP_WRITE) << 8) |                                     \
   ((PERF_COUNT_HW_CACHE_RESULT_ACCESS) << 16))

#define HW_CACHE_PREFETCH_ACCESS                                               \
  (((PERF_COUNT_HW_CACHE_OP_PREFETCH) << 8) |                                  \
   ((PERF_COUNT_HW_CACHE_RESULT_ACCESS) << 16))

#define HW_CACHE_READ_MISS                                                     \
  (((PERF_COUNT_HW_CACHE_OP_READ) << 8) |                                      \
   ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))

#define HW_CACHE_WRITE_MISS                                                    \
  (((PERF_COUNT_HW_CACHE_OP_WRITE) << 8) |                                     \
   ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))

#define HW_CACHE_PREFETCH_MISS                                                 \
  (((PERF_COUNT_HW_CACHE_OP_PREFETCH) << 8) |                                  \
   ((PERF_COUNT_HW_CACHE_RESULT_MISS) << 16))

struct event_info {
  char *name;
  uint64_t config;
};
typedef struct event_info event_info_t;

struct intel_pmu_ctx_s {
  _Bool hw_cache_events;
  _Bool kernel_pmu_events;
  _Bool sw_events;
  char event_list_fn[PATH_MAX];
  char **hw_events;
  size_t hw_events_count;
  struct eventlist *event_list;
};
typedef struct intel_pmu_ctx_s intel_pmu_ctx_t;

event_info_t g_kernel_pmu_events[] = {
    {.name = "cpu-cycles", .config = PERF_COUNT_HW_CPU_CYCLES},
    {.name = "instructions", .config = PERF_COUNT_HW_INSTRUCTIONS},
    {.name = "cache-references", .config = PERF_COUNT_HW_CACHE_REFERENCES},
    {.name = "cache-misses", .config = PERF_COUNT_HW_CACHE_MISSES},
    {.name = "branches", .config = PERF_COUNT_HW_BRANCH_INSTRUCTIONS},
    {.name = "branch-misses", .config = PERF_COUNT_HW_BRANCH_MISSES},
    {.name = "bus-cycles", .config = PERF_COUNT_HW_BUS_CYCLES},
};

event_info_t g_hw_cache_events[] = {

    {.name = "L1-dcache-loads",
     .config = (PERF_COUNT_HW_CACHE_L1D | HW_CACHE_READ_ACCESS)},
    {.name = "L1-dcache-load-misses",
     .config = (PERF_COUNT_HW_CACHE_L1D | HW_CACHE_READ_MISS)},
    {.name = "L1-dcache-stores",
     .config = (PERF_COUNT_HW_CACHE_L1D | HW_CACHE_WRITE_ACCESS)},
    {.name = "L1-dcache-store-misses",
     .config = (PERF_COUNT_HW_CACHE_L1D | HW_CACHE_WRITE_MISS)},
    {.name = "L1-dcache-prefetches",
     .config = (PERF_COUNT_HW_CACHE_L1D | HW_CACHE_PREFETCH_ACCESS)},
    {.name = "L1-dcache-prefetch-misses",
     .config = (PERF_COUNT_HW_CACHE_L1D | HW_CACHE_PREFETCH_MISS)},

    {.name = "L1-icache-loads",
     .config = (PERF_COUNT_HW_CACHE_L1I | HW_CACHE_READ_ACCESS)},
    {.name = "L1-icache-load-misses",
     .config = (PERF_COUNT_HW_CACHE_L1I | HW_CACHE_READ_MISS)},
    {.name = "L1-icache-prefetches",
     .config = (PERF_COUNT_HW_CACHE_L1I | HW_CACHE_PREFETCH_ACCESS)},
    {.name = "L1-icache-prefetch-misses",
     .config = (PERF_COUNT_HW_CACHE_L1I | HW_CACHE_PREFETCH_MISS)},

    {.name = "LLC-loads",
     .config = (PERF_COUNT_HW_CACHE_LL | HW_CACHE_READ_ACCESS)},
    {.name = "LLC-load-misses",
     .config = (PERF_COUNT_HW_CACHE_LL | HW_CACHE_READ_MISS)},
    {.name = "LLC-stores",
     .config = (PERF_COUNT_HW_CACHE_LL | HW_CACHE_WRITE_ACCESS)},
    {.name = "LLC-store-misses",
     .config = (PERF_COUNT_HW_CACHE_LL | HW_CACHE_WRITE_MISS)},
    {.name = "LLC-prefetches",
     .config = (PERF_COUNT_HW_CACHE_LL | HW_CACHE_PREFETCH_ACCESS)},
    {.name = "LLC-prefetch-misses",
     .config = (PERF_COUNT_HW_CACHE_LL | HW_CACHE_PREFETCH_MISS)},

    {.name = "dTLB-loads",
     .config = (PERF_COUNT_HW_CACHE_DTLB | HW_CACHE_READ_ACCESS)},
    {.name = "dTLB-load-misses",
     .config = (PERF_COUNT_HW_CACHE_DTLB | HW_CACHE_READ_MISS)},
    {.name = "dTLB-stores",
     .config = (PERF_COUNT_HW_CACHE_DTLB | HW_CACHE_WRITE_ACCESS)},
    {.name = "dTLB-store-misses",
     .config = (PERF_COUNT_HW_CACHE_DTLB | HW_CACHE_WRITE_MISS)},
    {.name = "dTLB-prefetches",
     .config = (PERF_COUNT_HW_CACHE_DTLB | HW_CACHE_PREFETCH_ACCESS)},
    {.name = "dTLB-prefetch-misses",
     .config = (PERF_COUNT_HW_CACHE_DTLB | HW_CACHE_PREFETCH_MISS)},

    {.name = "iTLB-loads",
     .config = (PERF_COUNT_HW_CACHE_ITLB | HW_CACHE_READ_ACCESS)},
    {.name = "iTLB-load-misses",
     .config = (PERF_COUNT_HW_CACHE_ITLB | HW_CACHE_READ_MISS)},

    {.name = "branch-loads",
     .config = (PERF_COUNT_HW_CACHE_BPU | HW_CACHE_READ_ACCESS)},
    {.name = "branch-load-misses",
     .config = (PERF_COUNT_HW_CACHE_BPU | HW_CACHE_READ_MISS)},
};

event_info_t g_sw_events[] = {
    {.name = "cpu-clock", .config = PERF_COUNT_SW_CPU_CLOCK},

    {.name = "task-clock", .config = PERF_COUNT_SW_TASK_CLOCK},

    {.name = "context-switches", .config = PERF_COUNT_SW_CONTEXT_SWITCHES},

    {.name = "cpu-migrations", .config = PERF_COUNT_SW_CPU_MIGRATIONS},

    {.name = "page-faults", .config = PERF_COUNT_SW_PAGE_FAULTS},

    {.name = "minor-faults", .config = PERF_COUNT_SW_PAGE_FAULTS_MIN},

    {.name = "major-faults", .config = PERF_COUNT_SW_PAGE_FAULTS_MAJ},

    {.name = "alignment-faults", .config = PERF_COUNT_SW_ALIGNMENT_FAULTS},

    {.name = "emulation-faults", .config = PERF_COUNT_SW_EMULATION_FAULTS},
};

static intel_pmu_ctx_t g_ctx;

#if COLLECT_DEBUG
static void pmu_dump_events() {

  DEBUG(PMU_PLUGIN ": Events:");

  struct event *e;

  for (e = g_ctx.event_list->eventlist; e; e = e->next) {
    DEBUG(PMU_PLUGIN ":   event       : %s", e->event);
    DEBUG(PMU_PLUGIN ":     group_lead: %d", e->group_leader);
    DEBUG(PMU_PLUGIN ":     end_group : %d", e->end_group);
    DEBUG(PMU_PLUGIN ":     type      : %#x", e->attr.type);
    DEBUG(PMU_PLUGIN ":     config    : %#x", (unsigned)e->attr.config);
    DEBUG(PMU_PLUGIN ":     size      : %d", e->attr.size);
  }
}

static void pmu_dump_config(void) {

  DEBUG(PMU_PLUGIN ": Config:");
  DEBUG(PMU_PLUGIN ":   hw_cache_events   : %d", g_ctx.hw_cache_events);
  DEBUG(PMU_PLUGIN ":   kernel_pmu_events : %d", g_ctx.kernel_pmu_events);
  DEBUG(PMU_PLUGIN ":   software_events   : %d", g_ctx.sw_events);

  for (size_t i = 0; i < g_ctx.hw_events_count; i++) {
    DEBUG(PMU_PLUGIN ":   hardware_events[%zu]: %s", i, g_ctx.hw_events[i]);
  }
}

#endif /* COLLECT_DEBUG */

static int pmu_config_hw_events(oconfig_item_t *ci) {

  if (strcasecmp("HardwareEvents", ci->key) != 0) {
    return -EINVAL;
  }

  g_ctx.hw_events = calloc(ci->values_num, sizeof(char *));
  if (g_ctx.hw_events == NULL) {
    ERROR(PMU_PLUGIN ": Failed to allocate hw events.");
    return -ENOMEM;
  }

  for (int i = 0; i < ci->values_num; i++) {
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING(PMU_PLUGIN ": The %s option requires string arguments.", ci->key);
      continue;
    }

    g_ctx.hw_events[g_ctx.hw_events_count] = strdup(ci->values[i].value.string);
    if (g_ctx.hw_events[g_ctx.hw_events_count] == NULL) {
      ERROR(PMU_PLUGIN ": Failed to allocate hw events entry.");
      return -ENOMEM;
    }

    g_ctx.hw_events_count++;
  }

  return 0;
}

static int pmu_config(oconfig_item_t *ci) {

  DEBUG(PMU_PLUGIN ": %s:%d", __FUNCTION__, __LINE__);

  for (int i = 0; i < ci->children_num; i++) {
    int ret = 0;
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("ReportHardwareCacheEvents", child->key) == 0) {
      ret = cf_util_get_boolean(child, &g_ctx.hw_cache_events);
    } else if (strcasecmp("ReportKernelPMUEvents", child->key) == 0) {
      ret = cf_util_get_boolean(child, &g_ctx.kernel_pmu_events);
    } else if (strcasecmp("EventList", child->key) == 0) {
      ret = cf_util_get_string_buffer(child, g_ctx.event_list_fn,
                                      sizeof(g_ctx.event_list_fn));
    } else if (strcasecmp("HardwareEvents", child->key) == 0) {
      ret = pmu_config_hw_events(child);
    } else if (strcasecmp("ReportSoftwareEvents", child->key) == 0) {
      ret = cf_util_get_boolean(child, &g_ctx.sw_events);
    } else {
      ERROR(PMU_PLUGIN ": Unknown configuration parameter \"%s\".", child->key);
      ret = -1;
    }

    if (ret != 0) {
      DEBUG(PMU_PLUGIN ": %s:%d ret=%d", __FUNCTION__, __LINE__, ret);
      return ret;
    }
  }

#if COLLECT_DEBUG
  pmu_dump_config();
#endif

  return 0;
}

static void pmu_submit_counter(int cpu, char *event, counter_t value,
                               meta_data_t *meta) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.counter = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, PMU_PLUGIN, sizeof(vl.plugin));
  if (cpu == -1) {
    snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "all");
  } else {
    vl.meta = meta;
    snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%d", cpu);
  }
  sstrncpy(vl.type, "counter", sizeof(vl.type));
  sstrncpy(vl.type_instance, event, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

meta_data_t *pmu_meta_data_create(const struct efd *efd) {
  meta_data_t *meta = NULL;

  /* create meta data only if value was scaled */
  if (efd->val[1] == efd->val[2] || !efd->val[2]) {
    return NULL;
  }

  meta = meta_data_create();
  if (meta == NULL) {
    ERROR(PMU_PLUGIN ": meta_data_create failed.");
    return NULL;
  }

  meta_data_add_unsigned_int(meta, "intel_pmu:raw_count", efd->val[0]);
  meta_data_add_unsigned_int(meta, "intel_pmu:time_enabled", efd->val[1]);
  meta_data_add_unsigned_int(meta, "intel_pmu:time_running", efd->val[2]);

  return meta;
}

static void pmu_dispatch_data(void) {

  struct event *e;

  for (e = g_ctx.event_list->eventlist; e; e = e->next) {
    uint64_t all_value = 0;
    int event_enabled = 0;
    for (int i = 0; i < g_ctx.event_list->num_cpus; i++) {

      if (e->efd[i].fd < 0)
        continue;

      event_enabled++;

      /* If there are more events than counters, the kernel uses time
       * multiplexing. With multiplexing, at the end of the run,
       * the counter is scaled basing on total time enabled vs time running.
       * final_count = raw_count * time_enabled/time_running
       */
      uint64_t value = event_scaled_value(e, i);
      all_value += value;

      /* get meta data with information about scaling */
      meta_data_t *meta = pmu_meta_data_create(&e->efd[i]);

      /* dispatch per CPU value */
      pmu_submit_counter(i, e->event, value, meta);

      meta_data_destroy(meta);
    }

    if (event_enabled > 0) {
      DEBUG(PMU_PLUGIN ": %-20s %'10lu", e->event, all_value);
      /* dispatch all CPU value */
      pmu_submit_counter(-1, e->event, all_value, NULL);
    }
  }
}

static int pmu_read(__attribute__((unused)) user_data_t *ud) {
  int ret;

  DEBUG(PMU_PLUGIN ": %s:%d", __FUNCTION__, __LINE__);

  ret = read_all_events(g_ctx.event_list);
  if (ret != 0) {
    ERROR(PMU_PLUGIN ": Failed to read values of all events.");
    return ret;
  }

  pmu_dispatch_data();

  return 0;
}

static int pmu_add_events(struct eventlist *el, uint32_t type,
                          event_info_t *events, size_t count) {

  for (size_t i = 0; i < count; i++) {
    /* Allocate memory for event struct that contains array of efd structs
       for all cores */
    struct event *e =
        calloc(sizeof(struct event) + sizeof(struct efd) * el->num_cpus, 1);
    if (e == NULL) {
      ERROR(PMU_PLUGIN ": Failed to allocate event structure");
      return -ENOMEM;
    }

    e->attr.type = type;
    e->attr.config = events[i].config;
    e->attr.size = PERF_ATTR_SIZE_VER0;
    if (!el->eventlist)
      el->eventlist = e;
    if (el->eventlist_last)
      el->eventlist_last->next = e;
    el->eventlist_last = e;
    e->event = strdup(events[i].name);
  }

  return 0;
}

static int pmu_add_hw_events(struct eventlist *el, char **e, size_t count) {

  for (size_t i = 0; i < count; i++) {

    size_t group_events_count = 0;

    char *events = strdup(e[i]);
    if (!events)
      return -1;

    char *s, *tmp;
    for (s = strtok_r(events, ",", &tmp); s; s = strtok_r(NULL, ",", &tmp)) {

      /* Multiple events parsed in one entry */
      if (group_events_count == 1) {
        /* Mark previously added event as group leader */
        el->eventlist_last->group_leader = 1;
      }

      /* Allocate memory for event struct that contains array of efd structs
         for all cores */
      struct event *e =
          calloc(sizeof(struct event) + sizeof(struct efd) * el->num_cpus, 1);
      if (e == NULL) {
        free(events);
        return -ENOMEM;
      }

      if (resolve_event(s, &e->attr) == 0) {
        e->next = NULL;
        if (!el->eventlist)
          el->eventlist = e;
        if (el->eventlist_last)
          el->eventlist_last->next = e;
        el->eventlist_last = e;
        e->event = strdup(s);
      } else {
        DEBUG(PMU_PLUGIN ": Cannot resolve %s", s);
        sfree(e);
      }

      group_events_count++;
    }

    /* Multiple events parsed in one entry */
    if (group_events_count > 1) {
      /* Mark last added event as group end */
      el->eventlist_last->end_group = 1;
    }

    free(events);
  }

  return 0;
}

static void pmu_free_events(struct eventlist *el) {

  if (el == NULL)
    return;

  struct event *e = el->eventlist;

  while (e) {
    struct event *next = e->next;
    sfree(e);
    e = next;
  }

  el->eventlist = NULL;
}

static int pmu_setup_events(struct eventlist *el, bool measure_all,
                            int measure_pid) {
  struct event *e, *leader = NULL;
  int ret = -1;

  for (e = el->eventlist; e; e = e->next) {

    for (int i = 0; i < el->num_cpus; i++) {
      if (setup_event(e, i, leader, measure_all, measure_pid) < 0) {
        WARNING(PMU_PLUGIN ": perf event '%s' is not available (cpu=%d).",
                e->event, i);
      } else {
        /* success if at least one event was set */
        ret = 0;
      }
    }

    if (e->group_leader)
      leader = e;
    if (e->end_group)
      leader = NULL;
  }

  return ret;
}

static int pmu_init(void) {
  int ret;

  DEBUG(PMU_PLUGIN ": %s:%d", __FUNCTION__, __LINE__);

  g_ctx.event_list = alloc_eventlist();
  if (g_ctx.event_list == NULL) {
    ERROR(PMU_PLUGIN ": Failed to allocate event list.");
    return -ENOMEM;
  }

  if (g_ctx.hw_cache_events) {
    ret =
        pmu_add_events(g_ctx.event_list, PERF_TYPE_HW_CACHE, g_hw_cache_events,
                       STATIC_ARRAY_SIZE(g_hw_cache_events));
    if (ret != 0) {
      ERROR(PMU_PLUGIN ": Failed to add hw cache events.");
      goto init_error;
    }
  }

  if (g_ctx.kernel_pmu_events) {
    ret = pmu_add_events(g_ctx.event_list, PERF_TYPE_HARDWARE,
                         g_kernel_pmu_events,
                         STATIC_ARRAY_SIZE(g_kernel_pmu_events));
    if (ret != 0) {
      ERROR(PMU_PLUGIN ": Failed to add kernel PMU events.");
      goto init_error;
    }
  }

  /* parse events names if config option is present and is not empty */
  if (g_ctx.hw_events_count) {

    ret = read_events(g_ctx.event_list_fn);
    if (ret != 0) {
      ERROR(PMU_PLUGIN ": Failed to read event list file '%s'.",
            g_ctx.event_list_fn);
      return ret;
    }

    ret = pmu_add_hw_events(g_ctx.event_list, g_ctx.hw_events,
                            g_ctx.hw_events_count);
    if (ret != 0) {
      ERROR(PMU_PLUGIN ": Failed to add hardware events.");
      goto init_error;
    }
  }

  if (g_ctx.sw_events) {
    ret = pmu_add_events(g_ctx.event_list, PERF_TYPE_SOFTWARE, g_sw_events,
                         STATIC_ARRAY_SIZE(g_sw_events));
    if (ret != 0) {
      ERROR(PMU_PLUGIN ": Failed to add software events.");
      goto init_error;
    }
  }

#if COLLECT_DEBUG
  pmu_dump_events();
#endif

  if (g_ctx.event_list->eventlist != NULL) {
    /* measure all processes */
    ret = pmu_setup_events(g_ctx.event_list, true, -1);
    if (ret != 0) {
      ERROR(PMU_PLUGIN ": Failed to setup perf events for the event list.");
      goto init_error;
    }
  } else {
    WARNING(PMU_PLUGIN
            ": Events list is empty. No events were setup for monitoring.");
  }

  return 0;

init_error:

  pmu_free_events(g_ctx.event_list);
  sfree(g_ctx.event_list);
  for (size_t i = 0; i < g_ctx.hw_events_count; i++) {
    sfree(g_ctx.hw_events[i]);
  }
  sfree(g_ctx.hw_events);
  g_ctx.hw_events_count = 0;

  return ret;
}

static int pmu_shutdown(void) {

  DEBUG(PMU_PLUGIN ": %s:%d", __FUNCTION__, __LINE__);

  pmu_free_events(g_ctx.event_list);
  sfree(g_ctx.event_list);
  for (size_t i = 0; i < g_ctx.hw_events_count; i++) {
    sfree(g_ctx.hw_events[i]);
  }
  sfree(g_ctx.hw_events);
  g_ctx.hw_events_count = 0;

  return 0;
}

void module_register(void) {
  plugin_register_init(PMU_PLUGIN, pmu_init);
  plugin_register_complex_config(PMU_PLUGIN, pmu_config);
  plugin_register_complex_read(NULL, PMU_PLUGIN, pmu_read, 0, NULL);
  plugin_register_shutdown(PMU_PLUGIN, pmu_shutdown);
}
