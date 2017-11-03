/**
 * collectd - src/intel_rdt.c
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
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

#include <pqos.h>

#define RDT_PLUGIN "intel_rdt"

#define RDT_MAX_SOCKETS 8
#define RDT_MAX_SOCKET_CORES 64
#define RDT_MAX_CORES (RDT_MAX_SOCKET_CORES * RDT_MAX_SOCKETS)

typedef enum {
  UNKNOWN = 0,
  CONFIGURATION_ERROR,
} rdt_config_status;

struct rdt_core_group_s {
  char *desc;
  size_t num_cores;
  unsigned *cores;
  enum pqos_mon_event events;
};
typedef struct rdt_core_group_s rdt_core_group_t;

struct rdt_ctx_s {
  rdt_core_group_t cgroups[RDT_MAX_CORES];
  struct pqos_mon_data *pgroups[RDT_MAX_CORES];
  size_t num_groups;
  const struct pqos_cpuinfo *pqos_cpu;
  const struct pqos_cap *pqos_cap;
  const struct pqos_capability *cap_mon;
};
typedef struct rdt_ctx_s rdt_ctx_t;

static rdt_ctx_t *g_rdt = NULL;

static rdt_config_status g_state = UNKNOWN;

static int isdup(const uint64_t *nums, size_t size, uint64_t val) {
  for (size_t i = 0; i < size; i++)
    if (nums[i] == val)
      return 1;
  return 0;
}

static int strtouint64(const char *s, uint64_t *n) {
  char *endptr = NULL;

  assert(s != NULL);
  assert(n != NULL);

  *n = strtoull(s, &endptr, 0);

  if (!(*s != '\0' && *endptr == '\0')) {
    DEBUG(RDT_PLUGIN ": Error converting '%s' to unsigned number.", s);
    return -EINVAL;
  }

  return 0;
}

/*
 * NAME
 *   strlisttonums
 *
 * DESCRIPTION
 *   Converts string of characters representing list of numbers into array of
 *   numbers. Allowed formats are:
 *     0,1,2,3
 *     0-10,20-18
 *     1,3,5-8,10,0x10-12
 *
 *   Numbers can be in decimal or hexadecimal format.
 *
 * PARAMETERS
 *   `s'         String representing list of unsigned numbers.
 *   `nums'      Array to put converted numeric values into.
 *   `max'       Maximum number of elements that nums can accommodate.
 *
 * RETURN VALUE
 *    Number of elements placed into nums.
 */
static size_t strlisttonums(char *s, uint64_t *nums, size_t max) {
  int ret;
  size_t index = 0;
  char *saveptr = NULL;

  if (s == NULL || nums == NULL || max == 0)
    return index;

  for (;;) {
    char *p = NULL;
    char *token = NULL;

    token = strtok_r(s, ",", &saveptr);
    if (token == NULL)
      break;

    s = NULL;

    while (isspace(*token))
      token++;
    if (*token == '\0')
      continue;

    p = strchr(token, '-');
    if (p != NULL) {
      uint64_t n, start, end;
      *p = '\0';
      ret = strtouint64(token, &start);
      if (ret < 0)
        return 0;
      ret = strtouint64(p + 1, &end);
      if (ret < 0)
        return 0;
      if (start > end) {
        return 0;
      }
      for (n = start; n <= end; n++) {
        if (!(isdup(nums, index, n))) {
          nums[index] = n;
          index++;
        }
        if (index >= max)
          return index;
      }
    } else {
      uint64_t val;

      ret = strtouint64(token, &val);
      if (ret < 0)
        return 0;

      if (!(isdup(nums, index, val))) {
        nums[index] = val;
        index++;
      }
      if (index >= max)
        return index;
    }
  }

  return index;
}

/*
 * NAME
 *   cgroup_cmp
 *
 * DESCRIPTION
 *   Function to compare cores in 2 core groups.
 *
 * PARAMETERS
 *   `cg_a'      Pointer to core group a.
 *   `cg_b'      Pointer to core group b.
 *
 * RETURN VALUE
 *    1 if both groups contain the same cores
 *    0 if none of their cores match
 *    -1 if some but not all cores match
 */
static int cgroup_cmp(const rdt_core_group_t *cg_a,
                      const rdt_core_group_t *cg_b) {
  int found = 0;

  assert(cg_a != NULL);
  assert(cg_b != NULL);

  const int sz_a = cg_a->num_cores;
  const int sz_b = cg_b->num_cores;
  const unsigned *tab_a = cg_a->cores;
  const unsigned *tab_b = cg_b->cores;

  for (int i = 0; i < sz_a; i++) {
    for (int j = 0; j < sz_b; j++)
      if (tab_a[i] == tab_b[j])
        found++;
  }
  /* if no cores are the same */
  if (!found)
    return 0;
  /* if group contains same cores */
  if (sz_a == sz_b && sz_b == found)
    return 1;
  /* if not all cores are the same */
  return -1;
}

static int cgroup_set(rdt_core_group_t *cg, char *desc, uint64_t *cores,
                      size_t num_cores) {
  assert(cg != NULL);
  assert(desc != NULL);
  assert(cores != NULL);
  assert(num_cores > 0);

  cg->cores = calloc(num_cores, sizeof(unsigned));
  if (cg->cores == NULL) {
    ERROR(RDT_PLUGIN ": Error allocating core group table");
    return -ENOMEM;
  }
  cg->num_cores = num_cores;
  cg->desc = strdup(desc);
  if (cg->desc == NULL) {
    ERROR(RDT_PLUGIN ": Error allocating core group description");
    sfree(cg->cores);
    return -ENOMEM;
  }

  for (size_t i = 0; i < num_cores; i++)
    cg->cores[i] = (unsigned)cores[i];

  return 0;
}

/*
 * NAME
 *   oconfig_to_cgroups
 *
 * DESCRIPTION
 *   Function to set the descriptions and cores for each core group.
 *   Takes a config option containing list of strings that are used to set
 *   core group values.
 *
 * PARAMETERS
 *   `item'        Config option containing core groups.
 *   `groups'      Table of core groups to set values in.
 *   `max_groups'  Maximum number of core groups allowed.
 *
 * RETURN VALUE
 *   On success, the number of core groups set up. On error, appropriate
 *   negative error value.
 */
static int oconfig_to_cgroups(oconfig_item_t *item, rdt_core_group_t *groups,
                              size_t max_groups) {
  int index = 0;

  assert(groups != NULL);
  assert(max_groups > 0);
  assert(item != NULL);

  for (int j = 0; j < item->values_num; j++) {
    int ret;
    size_t n;
    uint64_t cores[RDT_MAX_CORES] = {0};
    char value[DATA_MAX_NAME_LEN];

    if ((item->values[j].value.string == NULL) ||
        (strlen(item->values[j].value.string) == 0))
      continue;

    sstrncpy(value, item->values[j].value.string, sizeof(value));

    n = strlisttonums(value, cores, STATIC_ARRAY_SIZE(cores));
    if (n == 0) {
      ERROR(RDT_PLUGIN ": Error parsing core group (%s)",
            item->values[j].value.string);
      return -EINVAL;
    }

    /* set core group info */
    ret = cgroup_set(&groups[index], item->values[j].value.string, cores, n);
    if (ret < 0)
      return ret;

    index++;

    if (index >= max_groups) {
      WARNING(RDT_PLUGIN ": Too many core groups configured");
      return index;
    }
  }

  return index;
}

#if COLLECT_DEBUG
static void rdt_dump_cgroups(void) {
  char cores[RDT_MAX_CORES * 4];

  if (g_rdt == NULL)
    return;

  DEBUG(RDT_PLUGIN ": Core Groups Dump");
  DEBUG(RDT_PLUGIN ":  groups count: %" PRIsz, g_rdt->num_groups);

  for (int i = 0; i < g_rdt->num_groups; i++) {

    memset(cores, 0, sizeof(cores));
    for (int j = 0; j < g_rdt->cgroups[i].num_cores; j++) {
      snprintf(cores + strlen(cores), sizeof(cores) - strlen(cores) - 1, " %d",
               g_rdt->cgroups[i].cores[j]);
    }

    DEBUG(RDT_PLUGIN ":  group[%d]:", i);
    DEBUG(RDT_PLUGIN ":    description: %s", g_rdt->cgroups[i].desc);
    DEBUG(RDT_PLUGIN ":    cores: %s", cores);
    DEBUG(RDT_PLUGIN ":    events: 0x%X", g_rdt->cgroups[i].events);
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

    DEBUG(" [%s] %8u %10.1f %10.1f %10.1f", g_rdt->cgroups[i].desc,
          g_rdt->pgroups[i]->poll_ctx[0].rmid, llc, mbl, mbr);
  }
}
#endif /* COLLECT_DEBUG */

static void rdt_free_cgroups(void) {
  for (int i = 0; i < RDT_MAX_CORES; i++) {
    sfree(g_rdt->cgroups[i].desc);

    sfree(g_rdt->cgroups[i].cores);
    g_rdt->cgroups[i].num_cores = 0;

    sfree(g_rdt->pgroups[i]);
  }
}

static int rdt_default_cgroups(void) {
  int ret;

  /* configure each core in separate group */
  for (unsigned i = 0; i < g_rdt->pqos_cpu->num_cores; i++) {
    char desc[DATA_MAX_NAME_LEN];
    uint64_t core = i;

    snprintf(desc, sizeof(desc), "%d", g_rdt->pqos_cpu->cores[i].lcore);

    /* set core group info */
    ret = cgroup_set(&g_rdt->cgroups[i], desc, &core, 1);
    if (ret < 0)
      return ret;
  }

  return g_rdt->pqos_cpu->num_cores;
}

static int rdt_is_core_id_valid(int core_id) {

  for (int i = 0; i < g_rdt->pqos_cpu->num_cores; i++)
    if (core_id == g_rdt->pqos_cpu->cores[i].lcore)
      return 1;

  return 0;
}

static int rdt_config_cgroups(oconfig_item_t *item) {
  int n = 0;
  enum pqos_mon_event events = 0;

  if (item == NULL) {
    DEBUG(RDT_PLUGIN ": cgroups_config: Invalid argument.");
    return -EINVAL;
  }

  DEBUG(RDT_PLUGIN ": Core groups [%d]:", item->values_num);
  for (int j = 0; j < item->values_num; j++) {
    if (item->values[j].type != OCONFIG_TYPE_STRING) {
      ERROR(RDT_PLUGIN ": given core group value is not a string [idx=%d]", j);
      return -EINVAL;
    }
    DEBUG(RDT_PLUGIN ":  [%d]: %s", j, item->values[j].value.string);
  }

  n = oconfig_to_cgroups(item, g_rdt->cgroups, g_rdt->pqos_cpu->num_cores);
  if (n < 0) {
    rdt_free_cgroups();
    ERROR(RDT_PLUGIN ": Error parsing core groups configuration.");
    return -EINVAL;
  }

  /* validate configured core id values */
  for (int group_idx = 0; group_idx < n; group_idx++) {
    for (int core_idx = 0; core_idx < g_rdt->cgroups[group_idx].num_cores;
         core_idx++) {
      if (!rdt_is_core_id_valid(g_rdt->cgroups[group_idx].cores[core_idx])) {
        ERROR(RDT_PLUGIN ": Core group '%s' contains invalid core id '%d'",
              g_rdt->cgroups[group_idx].desc,
              (int)g_rdt->cgroups[group_idx].cores[core_idx]);
        rdt_free_cgroups();
        return -EINVAL;
      }
    }
  }

  if (n == 0) {
    /* create default core groups if "Cores" config option is empty */
    n = rdt_default_cgroups();
    if (n < 0) {
      rdt_free_cgroups();
      ERROR(RDT_PLUGIN ": Error creating default core groups configuration.");
      return n;
    }
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
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < i; j++) {
      int found = 0;
      found = cgroup_cmp(&g_rdt->cgroups[j], &g_rdt->cgroups[i]);
      if (found != 0) {
        rdt_free_cgroups();
        ERROR(RDT_PLUGIN ": Cannot monitor same cores in different groups.");
        return -EINVAL;
      }
    }

    g_rdt->cgroups[i].events = events;
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
    enum pqos_mon_event mbm_events =
        (PQOS_MON_EVENT_LMEM_BW | PQOS_MON_EVENT_TMEM_BW |
         PQOS_MON_EVENT_RMEM_BW);

    const struct pqos_event_values *pv = &g_rdt->pgroups[i]->values;

    /* Submit only monitored events data */

    if (g_rdt->cgroups[i].events & PQOS_MON_EVENT_L3_OCCUP)
      rdt_submit_gauge(g_rdt->cgroups[i].desc, "bytes", "llc", pv->llc);

    if (g_rdt->cgroups[i].events & PQOS_PERF_EVENT_IPC)
      rdt_submit_gauge(g_rdt->cgroups[i].desc, "ipc", NULL, pv->ipc);

    if (g_rdt->cgroups[i].events & mbm_events) {
      rdt_submit_derive(g_rdt->cgroups[i].desc, "memory_bandwidth", "local",
                        pv->mbm_local_delta);
      rdt_submit_derive(g_rdt->cgroups[i].desc, "memory_bandwidth", "remote",
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
    rdt_core_group_t *cg = &g_rdt->cgroups[i];

    ret = pqos_mon_start(cg->num_cores, cg->cores, cg->events, (void *)cg->desc,
                         g_rdt->pgroups[i]);

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
