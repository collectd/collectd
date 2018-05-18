/**
 * collectd - src/intel_rdt.c
 *
 * Copyright(c) 2016-2018 Intel Corporation. All rights reserved.
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

#include "utils_config_cores.h"

#include <pqos.h>

#define RDT_PLUGIN "intel_rdt"

#define RDT_MAX_SOCKETS 8
#define RDT_MAX_SOCKET_CORES 64
#define RDT_MAX_CORES (RDT_MAX_SOCKET_CORES * RDT_MAX_SOCKETS)

typedef enum {
  UNKNOWN = 0,
  CONFIGURATION_ERROR,
} rdt_config_status;

struct rdt_ctx_s {
  core_groups_list_t cores;
  enum pqos_mon_event events[RDT_MAX_CORES];
  struct pqos_mon_data *pgroups[RDT_MAX_CORES];
  size_t num_groups;
  const struct pqos_cpuinfo *pqos_cpu;
  const struct pqos_cap *pqos_cap;
  const struct pqos_capability *cap_mon;
};
typedef struct rdt_ctx_s rdt_ctx_t;

static rdt_ctx_t *g_rdt = NULL;

static rdt_config_status g_state = UNKNOWN;

#if COLLECT_DEBUG
static void rdt_dump_cgroups(void) {
  char cores[RDT_MAX_CORES * 4];

  if (g_rdt == NULL)
    return;

  DEBUG(RDT_PLUGIN ": Core Groups Dump");
  DEBUG(RDT_PLUGIN ":  groups count: %zu", g_rdt->num_groups);

  for (int i = 0; i < g_rdt->num_groups; i++) {
    core_group_t *cgroup = g_rdt->cores.cgroups + i;

    memset(cores, 0, sizeof(cores));
    for (int j = 0; j < cgroup->num_cores; j++) {
      snprintf(cores + strlen(cores), sizeof(cores) - strlen(cores) - 1, " %d",
               cgroup->cores[j]);
    }

    DEBUG(RDT_PLUGIN ":  group[%d]:", i);
    DEBUG(RDT_PLUGIN ":    description: %s", cgroup->desc);
    DEBUG(RDT_PLUGIN ":    cores: %s", cores);
    DEBUG(RDT_PLUGIN ":    events: 0x%X", g_rdt->events[i]);
  }

  return;
}

static inline double bytes_to_kb(const double bytes) { return bytes / 1024.0; }

static inline double bytes_to_mb(const double bytes) {
  return bytes / (1024.0 * 1024.0);
}

static void rdt_dump_data(void) {
  /*
   * CORE - monitored group of cores
   * RMID - Resource Monitoring ID associated with the monitored group
   * LLC - last level cache occupancy
   * MBL - local memory bandwidth
   * MBR - remote memory bandwidth
   */
  DEBUG("  CORE     RMID    LLC[KB]   MBL[MB]    MBR[MB]");
  for (int i = 0; i < g_rdt->num_groups; i++) {

    const struct pqos_event_values *pv = &g_rdt->pgroups[i]->values;

    double llc = bytes_to_kb(pv->llc);
    double mbr = bytes_to_mb(pv->mbm_remote_delta);
    double mbl = bytes_to_mb(pv->mbm_local_delta);

    DEBUG(" [%s] %8u %10.1f %10.1f %10.1f", g_rdt->cores.cgroups[i].desc,
          g_rdt->pgroups[i]->poll_ctx[0].rmid, llc, mbl, mbr);
  }
}
#endif /* COLLECT_DEBUG */

static void rdt_free_cgroups(void) {
  config_cores_cleanup(&g_rdt->cores);
  for (int i = 0; i < RDT_MAX_CORES; i++) {
    sfree(g_rdt->pgroups[i]);
  }
}

static int rdt_default_cgroups(void) {
  unsigned num_cores = g_rdt->pqos_cpu->num_cores;

  g_rdt->cores.cgroups = calloc(num_cores, sizeof(*(g_rdt->cores.cgroups)));
  if (g_rdt->cores.cgroups == NULL) {
    ERROR(RDT_PLUGIN ": Error allocating core groups array");
    return -ENOMEM;
  }
  g_rdt->cores.num_cgroups = num_cores;

  /* configure each core in separate group */
  for (unsigned i = 0; i < num_cores; i++) {
    core_group_t *cgroup = g_rdt->cores.cgroups + i;
    char desc[DATA_MAX_NAME_LEN];

    /* set core group info */
    cgroup->cores = calloc(1, sizeof(*(cgroup->cores)));
    if (cgroup->cores == NULL) {
      ERROR(RDT_PLUGIN ": Error allocating cores array");
      rdt_free_cgroups();
      return -ENOMEM;
    }
    cgroup->num_cores = 1;
    cgroup->cores[0] = i;

    snprintf(desc, sizeof(desc), "%d", g_rdt->pqos_cpu->cores[i].lcore);
    cgroup->desc = strdup(desc);
    if (cgroup->desc == NULL) {
      ERROR(RDT_PLUGIN ": Error allocating core group description");
      rdt_free_cgroups();
      return -ENOMEM;
    }
  }

  return num_cores;
}

static int rdt_is_core_id_valid(int core_id) {

  for (int i = 0; i < g_rdt->pqos_cpu->num_cores; i++)
    if (core_id == g_rdt->pqos_cpu->cores[i].lcore)
      return 1;

  return 0;
}

static int rdt_config_cgroups(oconfig_item_t *item) {
  size_t n = 0;
  enum pqos_mon_event events = 0;

  if (config_cores_parse(item, &g_rdt->cores) < 0) {
    rdt_free_cgroups();
    ERROR(RDT_PLUGIN ": Error parsing core groups configuration.");
    return -EINVAL;
  }
  n = g_rdt->cores.num_cgroups;

  /* validate configured core id values */
  for (size_t group_idx = 0; group_idx < n; group_idx++) {
    core_group_t *cgroup = g_rdt->cores.cgroups + group_idx;
    for (size_t core_idx = 0; core_idx < cgroup->num_cores; core_idx++) {
      if (!rdt_is_core_id_valid((int)cgroup->cores[core_idx])) {
        ERROR(RDT_PLUGIN ": Core group '%s' contains invalid core id '%d'",
              cgroup->desc, (int)cgroup->cores[core_idx]);
        rdt_free_cgroups();
        return -EINVAL;
      }
    }
  }

  if (n == 0) {
    /* create default core groups if "Cores" config option is empty */
    int ret = rdt_default_cgroups();
    if (ret < 0) {
      rdt_free_cgroups();
      ERROR(RDT_PLUGIN ": Error creating default core groups configuration.");
      return ret;
    }
    n = (size_t)ret;
    INFO(RDT_PLUGIN
         ": No core groups configured. Default core groups created.");
  }

  /* Get all available events on this platform */
  for (int i = 0; i < g_rdt->cap_mon->u.mon->num_events; i++)
    events |= g_rdt->cap_mon->u.mon->events[i].type;

  events &= ~(PQOS_PERF_EVENT_LLC_MISS);

  DEBUG(RDT_PLUGIN ": Number of cores in the system: %u",
        g_rdt->pqos_cpu->num_cores);
  DEBUG(RDT_PLUGIN ": Available events to monitor: %#x", events);

  g_rdt->num_groups = n;
  for (size_t i = 0; i < n; i++) {
    for (size_t j = 0; j < i; j++) {
      int found = 0;
      found = config_cores_cmp_cgroups(&g_rdt->cores.cgroups[j],
                                       &g_rdt->cores.cgroups[i]);
      if (found != 0) {
        rdt_free_cgroups();
        ERROR(RDT_PLUGIN ": Cannot monitor same cores in different groups.");
        return -EINVAL;
      }
    }

    g_rdt->events[i] = events;
    g_rdt->pgroups[i] = calloc(1, sizeof(*g_rdt->pgroups[i]));
    if (g_rdt->pgroups[i] == NULL) {
      rdt_free_cgroups();
      ERROR(RDT_PLUGIN ": Failed to allocate memory for monitoring data.");
      return -ENOMEM;
    }
  }

  return 0;
}

static void rdt_pqos_log(void *context, const size_t size, const char *msg) {
  DEBUG(RDT_PLUGIN ": %s", msg);
}

static int rdt_preinit(void) {
  int ret;

  if (g_rdt != NULL) {
    /* already initialized if config callback was called before init callback */
    return 0;
  }

  g_rdt = calloc(1, sizeof(*g_rdt));
  if (g_rdt == NULL) {
    ERROR(RDT_PLUGIN ": Failed to allocate memory for rdt context.");
    return -ENOMEM;
  }

  struct pqos_config pqos = {.fd_log = -1,
                             .callback_log = rdt_pqos_log,
                             .context_log = NULL,
                             .verbose = 0};

  ret = pqos_init(&pqos);
  if (ret != PQOS_RETVAL_OK) {
    ERROR(RDT_PLUGIN ": Error initializing PQoS library!");
    goto rdt_preinit_error1;
  }

  ret = pqos_cap_get(&g_rdt->pqos_cap, &g_rdt->pqos_cpu);
  if (ret != PQOS_RETVAL_OK) {
    ERROR(RDT_PLUGIN ": Error retrieving PQoS capabilities.");
    goto rdt_preinit_error2;
  }

  ret = pqos_cap_get_type(g_rdt->pqos_cap, PQOS_CAP_TYPE_MON, &g_rdt->cap_mon);
  if (ret == PQOS_RETVAL_PARAM) {
    ERROR(RDT_PLUGIN ": Error retrieving monitoring capabilities.");
    goto rdt_preinit_error2;
  }

  if (g_rdt->cap_mon == NULL) {
    ERROR(
        RDT_PLUGIN
        ": Monitoring capability not detected. Nothing to do for the plugin.");
    goto rdt_preinit_error2;
  }

  /* Reset pqos monitoring groups registers */
  pqos_mon_reset();

  return 0;

rdt_preinit_error2:
  pqos_fini();

rdt_preinit_error1:

  sfree(g_rdt);

  return -1;
}

static int rdt_config(oconfig_item_t *ci) {
  if (rdt_preinit() != 0) {
    g_state = CONFIGURATION_ERROR;
    /* if we return -1 at this point collectd
      reports a failure in configuration and
      aborts
    */
    return (0);
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Cores", child->key) == 0) {
      if (rdt_config_cgroups(child) != 0) {
        g_state = CONFIGURATION_ERROR;
        /* if we return -1 at this point collectd
           reports a failure in configuration and
           aborts
         */
        return (0);
      }

#if COLLECT_DEBUG
      rdt_dump_cgroups();
#endif /* COLLECT_DEBUG */
    } else {
      ERROR(RDT_PLUGIN ": Unknown configuration parameter \"%s\".", child->key);
    }
  }

  return 0;
}

static void rdt_submit_derive(char *cgroup, char *type, char *type_instance,
                              derive_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.derive = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, RDT_PLUGIN, sizeof(vl.plugin));
  snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s", cgroup);
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void rdt_submit_gauge(char *cgroup, char *type, char *type_instance,
                             gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, RDT_PLUGIN, sizeof(vl.plugin));
  snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s", cgroup);
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static int rdt_read(__attribute__((unused)) user_data_t *ud) {
  int ret;

  if (g_rdt == NULL) {
    ERROR(RDT_PLUGIN ": rdt_read: plugin not initialized.");
    return -EINVAL;
  }

  ret = pqos_mon_poll(&g_rdt->pgroups[0], (unsigned)g_rdt->num_groups);
  if (ret != PQOS_RETVAL_OK) {
    ERROR(RDT_PLUGIN ": Failed to poll monitoring data.");
    return -1;
  }

#if COLLECT_DEBUG
  rdt_dump_data();
#endif /* COLLECT_DEBUG */

  for (int i = 0; i < g_rdt->num_groups; i++) {
    core_group_t *cgroup = g_rdt->cores.cgroups + i;

    enum pqos_mon_event mbm_events =
        (PQOS_MON_EVENT_LMEM_BW | PQOS_MON_EVENT_TMEM_BW |
         PQOS_MON_EVENT_RMEM_BW);

    const struct pqos_event_values *pv = &g_rdt->pgroups[i]->values;

    /* Submit only monitored events data */

    if (g_rdt->events[i] & PQOS_MON_EVENT_L3_OCCUP)
      rdt_submit_gauge(cgroup->desc, "bytes", "llc", pv->llc);

    if (g_rdt->events[i] & PQOS_PERF_EVENT_IPC)
      rdt_submit_gauge(cgroup->desc, "ipc", NULL, pv->ipc);

    if (g_rdt->events[i] & mbm_events) {
      rdt_submit_derive(cgroup->desc, "memory_bandwidth", "local",
                        pv->mbm_local_delta);
      rdt_submit_derive(cgroup->desc, "memory_bandwidth", "remote",
                        pv->mbm_remote_delta);
    }
  }

  return 0;
}

static int rdt_init(void) {
  int ret;

  if (g_state == CONFIGURATION_ERROR)
    return -1;

  ret = rdt_preinit();
  if (ret != 0)
    return ret;

  /* Start monitoring */
  for (int i = 0; i < g_rdt->num_groups; i++) {
    core_group_t *cg = g_rdt->cores.cgroups + i;

    ret = pqos_mon_start(cg->num_cores, cg->cores, g_rdt->events[i],
                         (void *)cg->desc, g_rdt->pgroups[i]);

    if (ret != PQOS_RETVAL_OK)
      ERROR(RDT_PLUGIN ": Error starting monitoring group %s (pqos status=%d)",
            cg->desc, ret);
  }

  return 0;
}

static int rdt_shutdown(void) {
  int ret;

  DEBUG(RDT_PLUGIN ": rdt_shutdown.");

  if (g_rdt == NULL)
    return 0;

  /* Stop monitoring */
  for (int i = 0; i < g_rdt->num_groups; i++) {
    pqos_mon_stop(g_rdt->pgroups[i]);
  }

  ret = pqos_fini();
  if (ret != PQOS_RETVAL_OK)
    ERROR(RDT_PLUGIN ": Error shutting down PQoS library.");

  rdt_free_cgroups();
  sfree(g_rdt);

  return 0;
}

void module_register(void) {
  plugin_register_init(RDT_PLUGIN, rdt_init);
  plugin_register_complex_config(RDT_PLUGIN, rdt_config);
  plugin_register_complex_read(NULL, RDT_PLUGIN, rdt_read, 0, NULL);
  plugin_register_shutdown(RDT_PLUGIN, rdt_shutdown);
}
