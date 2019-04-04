/**
 * collectd - src/intel_rdt.c
 *
 * Copyright(c) 2016-2019 Intel Corporation. All rights reserved.
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
 *   Starzyk, Mateusz <mateuszx.starzyk@intel.com>
 *   Wojciech Andralojc <wojciechx.andralojc@intel.com>
 *   Michał Aleksiński <michalx.aleksinski@intel.com>
 **/

#include "collectd.h"
#include "utils/common/common.h"
#include "utils/config_cores/config_cores.h"
#include "utils/proc_pids/proc_pids.h"
#include <pqos.h>

#define RDT_PLUGIN "intel_rdt"

/* libpqos v2.0 or newer is required for process monitoring*/
#undef LIBPQOS2
#if defined(PQOS_VERSION) && PQOS_VERSION >= 20000
#define LIBPQOS2
#endif

#define RDT_PLUGIN "intel_rdt"

#define RDT_MAX_SOCKETS 8
#define RDT_MAX_SOCKET_CORES 64
#define RDT_MAX_CORES (RDT_MAX_SOCKET_CORES * RDT_MAX_SOCKETS)

#ifdef LIBPQOS2
/*
 * Process name inside comm file is limited to 16 chars.
 * More info here: http://man7.org/linux/man-pages/man5/proc.5.html
 */
#define RDT_MAX_NAMES_GROUPS 64
#define RDT_PROC_PATH "/proc"
#endif /* LIBPQOS2 */

typedef enum {
  UNKNOWN = 0,
  CONFIGURATION_ERROR,
} rdt_config_status;

#ifdef LIBPQOS2
struct rdt_name_group_s {
  char *desc;
  size_t num_names;
  char **names;
  proc_pids_t **proc_pids;
  size_t monitored_pids_count;
  enum pqos_mon_event events;
};
typedef struct rdt_name_group_s rdt_name_group_t;
#endif /* LIBPQOS2 */

struct rdt_ctx_s {
  core_groups_list_t cores;
  enum pqos_mon_event events[RDT_MAX_CORES];
  struct pqos_mon_data *pcgroups[RDT_MAX_CORES];
#ifdef LIBPQOS2
  rdt_name_group_t ngroups[RDT_MAX_NAMES_GROUPS];
  struct pqos_mon_data *pngroups[RDT_MAX_NAMES_GROUPS];
  size_t num_ngroups;
  proc_pids_t **proc_pids;
  size_t num_proc_pids;
#endif /* LIBPQOS2 */
  const struct pqos_cpuinfo *pqos_cpu;
  const struct pqos_cap *pqos_cap;
  const struct pqos_capability *cap_mon;
};
typedef struct rdt_ctx_s rdt_ctx_t;

static rdt_ctx_t *g_rdt;

static rdt_config_status g_state = UNKNOWN;

static int g_interface = -1;

static void rdt_submit_derive(const char *cgroup, const char *type,
                              const char *type_instance, derive_t value) {
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

static void rdt_submit_gauge(const char *cgroup, const char *type,
                             const char *type_instance, gauge_t value) {
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

#if COLLECT_DEBUG
static void rdt_dump_cgroups(void) {
  char cores[RDT_MAX_CORES * 4];

  if (g_rdt == NULL)
    return;

  DEBUG(RDT_PLUGIN ": Core Groups Dump");
  DEBUG(RDT_PLUGIN ":  groups count: %" PRIsz, g_rdt->cores.num_cgroups);

  for (size_t i = 0; i < g_rdt->cores.num_cgroups; i++) {
    core_group_t *cgroup = g_rdt->cores.cgroups + i;

    memset(cores, 0, sizeof(cores));
    for (size_t j = 0; j < cgroup->num_cores; j++) {
      snprintf(cores + strlen(cores), sizeof(cores) - strlen(cores) - 1, " %d",
               cgroup->cores[j]);
    }

    DEBUG(RDT_PLUGIN ":  group[%zu]:", i);
    DEBUG(RDT_PLUGIN ":    description: %s", cgroup->desc);
    DEBUG(RDT_PLUGIN ":    cores: %s", cores);
    DEBUG(RDT_PLUGIN ":    events: 0x%X", g_rdt->events[i]);
  }

  return;
}

#ifdef LIBPQOS2
static void rdt_dump_ngroups(void) {

  char names[DATA_MAX_NAME_LEN];

  if (g_rdt == NULL)
    return;

  DEBUG(RDT_PLUGIN ": Process Names Groups Dump");
  DEBUG(RDT_PLUGIN ":  groups count: %" PRIsz, g_rdt->num_ngroups);

  for (size_t i = 0; i < g_rdt->num_ngroups; i++) {
    memset(names, 0, sizeof(names));
    for (size_t j = 0; j < g_rdt->ngroups[i].num_names; j++)
      snprintf(names + strlen(names), sizeof(names) - strlen(names) - 1, " %s",
               g_rdt->ngroups[i].names[j]);

    DEBUG(RDT_PLUGIN ":  group[%d]:", (int)i);
    DEBUG(RDT_PLUGIN ":    description: %s", g_rdt->ngroups[i].desc);
    DEBUG(RDT_PLUGIN ":    process names:%s", names);
    DEBUG(RDT_PLUGIN ":    events: 0x%X", g_rdt->ngroups[i].events);
  }

  return;
}
#endif /* LIBPQOS2 */

static inline double bytes_to_kb(const double bytes) { return bytes / 1024.0; }

static inline double bytes_to_mb(const double bytes) {
  return bytes / (1024.0 * 1024.0);
}

static void rdt_dump_cores_data(void) {
/*
 * CORE - monitored group of cores
 * RMID - Resource Monitoring ID associated with the monitored group
 *        This is not available for monitoring with resource control
 * LLC - last level cache occupancy
 * MBL - local memory bandwidth
 * MBR - remote memory bandwidth
 */
#ifdef LIBPQOS2
  if (g_interface == PQOS_INTER_OS_RESCTRL_MON) {
    DEBUG(RDT_PLUGIN ":  CORE     LLC[KB]   MBL[MB]    MBR[MB]");
  } else {
    DEBUG(RDT_PLUGIN ":  CORE     RMID    LLC[KB]   MBL[MB]    MBR[MB]");
  }
#else
  DEBUG(RDT_PLUGIN ":  CORE     RMID    LLC[KB]   MBL[MB]    MBR[MB]");
#endif /* LIBPQOS2 */

  for (int i = 0; i < g_rdt->cores.num_cgroups; i++) {
    const struct pqos_event_values *pv = &g_rdt->pcgroups[i]->values;

    double llc = bytes_to_kb(pv->llc);
    double mbr = bytes_to_mb(pv->mbm_remote_delta);
    double mbl = bytes_to_mb(pv->mbm_local_delta);
#ifdef LIBPQOS2
    if (g_interface == PQOS_INTER_OS_RESCTRL_MON) {
      DEBUG(RDT_PLUGIN ": [%s] %10.1f %10.1f %10.1f",
            g_rdt->cores.cgroups[i].desc, llc, mbl, mbr);
    } else {
      DEBUG(RDT_PLUGIN ": [%s] %8u %10.1f %10.1f %10.1f",
            g_rdt->cores.cgroups[i].desc, g_rdt->pcgroups[i]->poll_ctx[0].rmid,
            llc, mbl, mbr);
    }
#else
    DEBUG(RDT_PLUGIN ": [%s] %8u %10.1f %10.1f %10.1f",
          g_rdt->cores.cgroups[i].desc, g_rdt->pcgroups[i]->poll_ctx[0].rmid,
          llc, mbl, mbr);
#endif /* LIBPQOS2 */
  }
}

#ifdef LIBPQOS2
static void rdt_dump_pids_data(void) {
  /*
   * NAME - monitored group of processes
   * PIDs - list of PID numbers in the NAME group
   * LLC - last level cache occupancy
   * MBL - local memory bandwidth
   * MBR - remote memory bandwidth
   */

  DEBUG(RDT_PLUGIN ":  NAME     PIDs");
  char pids[DATA_MAX_NAME_LEN];
  for (size_t i = 0; i < g_rdt->num_ngroups; ++i) {
    memset(pids, 0, sizeof(pids));
    for (size_t j = 0; j < g_rdt->ngroups[i].num_names; ++j) {
      pids_list_t *list = g_rdt->ngroups[i].proc_pids[j]->curr;
      for (size_t k = 0; k < list->size; k++)
        snprintf(pids + strlen(pids), sizeof(pids) - strlen(pids) - 1, " %u",
                 list->pids[k]);
    }
    DEBUG(RDT_PLUGIN ":  [%s] %s", g_rdt->ngroups[i].desc, pids);
  }

  DEBUG(RDT_PLUGIN ":  NAME    LLC[KB]   MBL[MB]    MBR[MB]");
  for (size_t i = 0; i < g_rdt->num_ngroups; i++) {

    const struct pqos_event_values *pv = &g_rdt->pngroups[i]->values;

    double llc = bytes_to_kb(pv->llc);
    double mbr = bytes_to_mb(pv->mbm_remote_delta);
    double mbl = bytes_to_mb(pv->mbm_local_delta);

    DEBUG(RDT_PLUGIN ":  [%s] %10.1f %10.1f %10.1f", g_rdt->ngroups[i].desc,
          llc, mbl, mbr);
  }
}
#endif /* LIBPQOS2 */
#endif /* COLLECT_DEBUG */

#ifdef LIBPQOS2
static int isdupstr(const char *names[], const size_t size, const char *name) {
  for (size_t i = 0; i < size; i++)
    if (strncmp(names[i], name, (size_t)MAX_PROC_NAME_LEN) == 0)
      return 1;

  return 0;
}

/*
 * NAME
 *   strlisttoarray
 *
 * DESCRIPTION
 *   Converts string representing list of strings into array of strings.
 *   Allowed format is:
 *     name,name1,name2,name3
 *
 * PARAMETERS
 *   `str_list'  String representing list of strings.
 *   `names'     Array to put extracted strings into.
 *   `names_num' Variable to put number of extracted strings.
 *
 * RETURN VALUE
 *    Number of elements placed into names.
 */
static int strlisttoarray(char *str_list, char ***names, size_t *names_num) {
  char *saveptr = NULL;

  if (str_list == NULL || names == NULL)
    return -EINVAL;

  if (strstr(str_list, ",,")) {
    /* strtok ignores empty words between separators.
     * This condition handles that by rejecting strings
     * with consecutive seprators */
    ERROR(RDT_PLUGIN ": Empty process name");
    return -EINVAL;
  }

  for (;;) {
    char *token = strtok_r(str_list, ",", &saveptr);
    if (token == NULL)
      break;

    str_list = NULL;

    while (isspace(*token))
      token++;

    if (*token == '\0')
      continue;

    if ((isdupstr((const char **)*names, *names_num, token))) {
      ERROR(RDT_PLUGIN ": Duplicated process name \'%s\' in group \'%s\'",
            token, str_list);
      return -EINVAL;
    } else {
      if (0 != strarray_add(names, names_num, token)) {
        ERROR(RDT_PLUGIN ": Error allocating process name string");
        return -ENOMEM;
      }
    }
  }

  return 0;
}

/*
 * NAME
 *   ngroup_cmp
 *
 * DESCRIPTION
 *   Function to compare names in two name groups.
 *
 * PARAMETERS
 *   `ng_a'      Pointer to name group a.
 *   `ng_b'      Pointer to name group b.
 *
 * RETURN VALUE
 *    1 if both groups contain the same names
 *    0 if none of their names match
 *    -1 if some but not all names match
 */
static int ngroup_cmp(const rdt_name_group_t *ng_a,
                      const rdt_name_group_t *ng_b) {
  unsigned found = 0;

  assert(ng_a != NULL);
  assert(ng_b != NULL);

  const size_t sz_a = (unsigned)ng_a->num_names;
  const size_t sz_b = (unsigned)ng_b->num_names;
  const char **tab_a = (const char **)ng_a->names;
  const char **tab_b = (const char **)ng_b->names;

  for (size_t i = 0; i < sz_a; i++) {
    for (size_t j = 0; j < sz_b; j++)
      if (strncmp(tab_a[i], tab_b[j], (size_t)MAX_PROC_NAME_LEN) == 0)
        found++;
  }
  /* if no names are the same */
  if (!found)
    return 0;
  /* if group contains same names */
  if (sz_a == sz_b && sz_b == (size_t)found)
    return 1;
  /* if not all names are the same */
  return -1;
}

/*
 * NAME
 *   oconfig_to_ngroups
 *
 * DESCRIPTION
 *   Function to set the descriptions and names for each process names group.
 *   Takes a config option containing list of strings that are used to set
 *   process group values.
 *
 * PARAMETERS
 *   `item'        Config option containing process names groups.
 *   `groups'      Table of process name groups to set values in.
 *   `max_groups'  Maximum number of process name groups allowed.
 *
 * RETURN VALUE
 *   On success, the number of name groups set up. On error, appropriate
 *   negative error value.
 */
static int oconfig_to_ngroups(const oconfig_item_t *item,
                              rdt_name_group_t *groups,
                              const size_t max_groups) {
  int index = 0;

  assert(groups != NULL);
  assert(max_groups > 0);
  assert(item != NULL);

  for (int j = 0; j < item->values_num; j++) {
    int ret;
    char value[DATA_MAX_NAME_LEN];

    if ((item->values[j].value.string == NULL) ||
        (strlen(item->values[j].value.string) == 0)) {
      ERROR(RDT_PLUGIN ": Error - empty group");
      return -EINVAL;
    }

    sstrncpy(value, item->values[j].value.string, sizeof(value));

    ret = strlisttoarray(value, &groups[index].names, &groups[index].num_names);
    if (ret != 0 || groups[index].num_names == 0) {
      ERROR(RDT_PLUGIN ": Error parsing process names group (%s)",
            item->values[j].value.string);
      return -EINVAL;
    }

    /* set group description info */
    groups[index].desc = sstrdup(item->values[j].value.string);
    if (groups[index].desc == NULL) {
      ERROR(RDT_PLUGIN ": Error allocating name group description");
      return -ENOMEM;
    }

    groups[index].proc_pids = NULL;
    groups[index].monitored_pids_count = 0;

    index++;

    if (index >= (const int)max_groups) {
      WARNING(RDT_PLUGIN ": Too many process names groups configured");
      return index;
    }
  }

  return index;
}

/*
 * NAME
 *   rdt_free_ngroups
 *
 * DESCRIPTION
 *   Function to deallocate memory allocated for name groups.
 *
 * PARAMETERS
 *   `rdt'       Pointer to rdt context
 */
static void rdt_free_ngroups(rdt_ctx_t *rdt) {
  for (int i = 0; i < RDT_MAX_NAMES_GROUPS; i++) {
    if (rdt->ngroups[i].desc)
      DEBUG(RDT_PLUGIN ": Freeing pids \'%s\' group\'s data...",
            rdt->ngroups[i].desc);
    sfree(rdt->ngroups[i].desc);
    strarray_free(rdt->ngroups[i].names, rdt->ngroups[i].num_names);

    if (rdt->ngroups[i].proc_pids)
      proc_pids_free(rdt->ngroups[i].proc_pids, rdt->ngroups[i].num_names);

    rdt->ngroups[i].num_names = 0;
    sfree(rdt->pngroups[i]);
  }
  if (rdt->proc_pids)
    sfree(rdt->proc_pids);

  rdt->num_ngroups = 0;
}

/*
 * NAME
 *   rdt_config_ngroups
 *
 * DESCRIPTION
 *   Reads name groups configuration.
 *
 * PARAMETERS
 *   `rdt`       Pointer to rdt context
 *   `item'      Config option containing process names groups.
 *
 * RETURN VALUE
 *  0 on success. Negative number on error.
 */
static int rdt_config_ngroups(rdt_ctx_t *rdt, const oconfig_item_t *item) {
  int n = 0;
  enum pqos_mon_event events = 0;

  if (item == NULL) {
    DEBUG(RDT_PLUGIN ": ngroups_config: Invalid argument.");
    return -EINVAL;
  }

  DEBUG(RDT_PLUGIN ": Process names groups [%d]:", item->values_num);
  for (int j = 0; j < item->values_num; j++) {
    if (item->values[j].type != OCONFIG_TYPE_STRING) {
      ERROR(RDT_PLUGIN
            ": given process names group value is not a string [idx=%d]",
            j);
      return -EINVAL;
    }
    DEBUG(RDT_PLUGIN ":  [%d]: %s", j, item->values[j].value.string);
  }

  n = oconfig_to_ngroups(item, rdt->ngroups, RDT_MAX_NAMES_GROUPS);
  if (n < 0) {
    rdt_free_ngroups(rdt);
    ERROR(RDT_PLUGIN ": Error parsing process name groups configuration.");
    return -EINVAL;
  }

  /* validate configured process name values */
  for (int group_idx = 0; group_idx < n; group_idx++) {
    DEBUG(RDT_PLUGIN ":  checking group [%d]: %s", group_idx,
          rdt->ngroups[group_idx].desc);
    for (size_t name_idx = 0; name_idx < rdt->ngroups[group_idx].num_names;
         name_idx++) {
      DEBUG(RDT_PLUGIN ":    checking process name [%zu]: %s", name_idx,
            rdt->ngroups[group_idx].names[name_idx]);
      if (!proc_pids_is_name_valid(rdt->ngroups[group_idx].names[name_idx])) {
        ERROR(RDT_PLUGIN ": Process name group '%s' contains invalid name '%s'",
              rdt->ngroups[group_idx].desc,
              rdt->ngroups[group_idx].names[name_idx]);
        rdt_free_ngroups(rdt);
        return -EINVAL;
      }
    }
  }

  if (n == 0) {
    ERROR(RDT_PLUGIN ": Empty process name groups configured.");
    return -EINVAL;
  }

  /* Get all available events on this platform */
  for (unsigned i = 0; i < rdt->cap_mon->u.mon->num_events; i++)
    events |= rdt->cap_mon->u.mon->events[i].type;

  events &= ~(PQOS_PERF_EVENT_LLC_MISS);

  DEBUG(RDT_PLUGIN ": Available events to monitor: %#x", events);

  rdt->num_ngroups = n;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < i; j++) {
      int found = ngroup_cmp(&rdt->ngroups[j], &rdt->ngroups[i]);
      if (found != 0) {
        rdt_free_ngroups(rdt);
        ERROR(RDT_PLUGIN
              ": Cannot monitor same process name in different groups.");
        return -EINVAL;
      }
    }

    rdt->ngroups[i].events = events;
    rdt->pngroups[i] = calloc(1, sizeof(*rdt->pngroups[i]));
    if (rdt->pngroups[i] == NULL) {
      rdt_free_ngroups(rdt);
      ERROR(RDT_PLUGIN
            ": Failed to allocate memory for process name monitoring data.");
      return -ENOMEM;
    }
  }

  return 0;
}

/*
 * NAME
 *   rdt_refresh_ngroup
 *
 * DESCRIPTION
 *   Refresh pids monitored by name group.
 *
 * PARAMETERS
 *   `ngroup`         Pointer to name group.
 *   `group_mon_data' PQoS monitoring context.
 *
 * RETURN VALUE
 *  0 on success. Negative number on error.
 */
static int rdt_refresh_ngroup(rdt_name_group_t *ngroup,
                              struct pqos_mon_data *group_mon_data) {

  int result = 0;

  if (NULL == ngroup)
    return -1;

  if (NULL == ngroup->proc_pids) {
    ERROR(RDT_PLUGIN
          ": rdt_refresh_ngroup: \'%s\' uninitialized process pids array.",
          ngroup->desc);

    return -1;
  }

  DEBUG(RDT_PLUGIN ": rdt_refresh_ngroup: \'%s\' process names group.",
        ngroup->desc);

  proc_pids_t **proc_pids = ngroup->proc_pids;
  pids_list_t added_pids;
  pids_list_t removed_pids;

  memset(&added_pids, 0, sizeof(added_pids));
  memset(&removed_pids, 0, sizeof(removed_pids));

  for (size_t i = 0; i < ngroup->num_names; ++i) {
    int diff_result = pids_list_diff(proc_pids[i], &added_pids, &removed_pids);
    if (0 != diff_result) {
      ERROR(RDT_PLUGIN
            ": rdt_refresh_ngroup: \'%s\'. Error [%d] during PID diff.",
            ngroup->desc, diff_result);
      result = -1;
      goto cleanup;
    }
  }

  DEBUG(RDT_PLUGIN ": rdt_refresh_ngroup: \'%s\' process names group, added: "
                   "%u, removed: %u.",
        ngroup->desc, (unsigned)added_pids.size, (unsigned)removed_pids.size);

  if (added_pids.size > 0) {

    /* no pids are monitored for this group yet: start monitoring */
    if (0 == ngroup->monitored_pids_count) {

      int start_result =
          pqos_mon_start_pids(added_pids.size, added_pids.pids, ngroup->events,
                              (void *)ngroup->desc, group_mon_data);
      if (PQOS_RETVAL_OK == start_result) {
        ngroup->monitored_pids_count = added_pids.size;
      } else {
        ERROR(RDT_PLUGIN ": rdt_refresh_ngroup: \'%s\'. Error [%d] while "
                         "STARTING pids monitoring",
              ngroup->desc, start_result);
        result = -1;
        goto pqos_error_recovery;
      }

    } else {

      int add_result =
          pqos_mon_add_pids(added_pids.size, added_pids.pids, group_mon_data);
      if (PQOS_RETVAL_OK == add_result)
        ngroup->monitored_pids_count += added_pids.size;
      else {
        ERROR(RDT_PLUGIN
              ": rdt_refresh_ngroup: \'%s\'. Error [%d] while ADDING pids.",
              ngroup->desc, add_result);
        result = -1;
        goto pqos_error_recovery;
      }
    }
  }

  if (removed_pids.size > 0) {

    /* all pids are removed: stop monitoring */
    if (removed_pids.size == ngroup->monitored_pids_count) {
      /* all pids for this group are lost: stop monitoring */
      int stop_result = pqos_mon_stop(group_mon_data);
      if (PQOS_RETVAL_OK != stop_result) {
        ERROR(RDT_PLUGIN ": rdt_refresh_ngroup: \'%s\'. Error [%d] while "
                         "STOPPING monitoring",
              ngroup->desc, stop_result);
        result = -1;
        goto pqos_error_recovery;
      }
      ngroup->monitored_pids_count = 0;
    } else {
      int remove_result = pqos_mon_remove_pids(
          removed_pids.size, removed_pids.pids, group_mon_data);
      if (PQOS_RETVAL_OK == remove_result) {
        ngroup->monitored_pids_count -= removed_pids.size;
      } else {
        ERROR(RDT_PLUGIN
              ": rdt_refresh_ngroup: \'%s\'. Error [%d] while REMOVING pids.",
              ngroup->desc, remove_result);
        result = -1;
        goto pqos_error_recovery;
      }
    }
  }

  goto cleanup;

pqos_error_recovery:
  /* Why?
   * Resources might be temporary unavailable.
   *
   * How?
   * Collectd will halt the reading thread for this
   * plugin if it returns an error.
   * Consecutive errors will be increasing the read period
   * up to 1 day interval.
   * On pqos error stop monitoring current group
   * and reset the proc_pids array
   * monitoring will be restarted on next collectd read cycle
   */
  DEBUG(RDT_PLUGIN ": rdt_refresh_ngroup: \'%s\' group RESET after error.",
        ngroup->desc);
  pqos_mon_stop(group_mon_data);
  for (size_t i = 0; i < ngroup->num_names; ++i)
    if (ngroup->proc_pids[i]->curr)
      ngroup->proc_pids[i]->curr->size = 0;

  ngroup->monitored_pids_count = 0;

cleanup:
  pids_list_clear(&added_pids);
  pids_list_clear(&removed_pids);

  return result;
}

/*
 * NAME
 *   read_pids_data
 *
 * DESCRIPTION
 *   Poll monitoring statistics for name groups
 *
 * RETURN VALUE
 *  0 on success. Negative number on error.
 */
static int read_pids_data() {

  if (0 == g_rdt->num_ngroups) {
    DEBUG(RDT_PLUGIN ": read_pids_data: not configured - PIDs read skipped");
    return 0;
  }

  DEBUG(RDT_PLUGIN ": read_pids_data: Scanning active groups");
  struct pqos_mon_data *active_groups[RDT_MAX_NAMES_GROUPS] = {0};
  size_t active_group_idx = 0;
  for (size_t pngroups_idx = 0;
       pngroups_idx < STATIC_ARRAY_SIZE(g_rdt->pngroups); ++pngroups_idx)
    if (0 != g_rdt->ngroups[pngroups_idx].monitored_pids_count)
      active_groups[active_group_idx++] = g_rdt->pngroups[pngroups_idx];

  int ret = 0;

  if (0 == active_group_idx) {
    DEBUG(RDT_PLUGIN ": read_pids_data: no active groups - PIDs read skipped");
    goto groups_refresh;
  }

  DEBUG(RDT_PLUGIN ": read_pids_data: PIDs data polling");

  int poll_result = pqos_mon_poll(active_groups, active_group_idx);
  if (poll_result != PQOS_RETVAL_OK) {
    ERROR(RDT_PLUGIN ": read_pids_data: Failed to poll monitoring data for "
                     "pids. Error [%d].",
          poll_result);
    ret = -poll_result;
    goto groups_refresh;
  }

  for (size_t i = 0; i < g_rdt->num_ngroups; i++) {
    enum pqos_mon_event mbm_events =
        (PQOS_MON_EVENT_LMEM_BW | PQOS_MON_EVENT_TMEM_BW |
         PQOS_MON_EVENT_RMEM_BW);

    if (g_rdt->pngroups[i] == NULL ||
        g_rdt->ngroups[i].monitored_pids_count == 0)
      continue;

    const struct pqos_event_values *pv = &g_rdt->pngroups[i]->values;

    /* Submit only monitored events data */

    if (g_rdt->ngroups[i].events & PQOS_MON_EVENT_L3_OCCUP)
      rdt_submit_gauge(g_rdt->ngroups[i].desc, "bytes", "llc", pv->llc);

    if (g_rdt->ngroups[i].events & PQOS_PERF_EVENT_IPC)
      rdt_submit_gauge(g_rdt->ngroups[i].desc, "ipc", NULL, pv->ipc);

    if (g_rdt->ngroups[i].events & mbm_events) {
      rdt_submit_derive(g_rdt->ngroups[i].desc, "memory_bandwidth", "local",
                        pv->mbm_local_delta);
      rdt_submit_derive(g_rdt->ngroups[i].desc, "memory_bandwidth", "remote",
                        pv->mbm_remote_delta);
    }
  }

#if COLLECT_DEBUG
  rdt_dump_pids_data();
#endif /* COLLECT_DEBUG */

groups_refresh:
  ret = proc_pids_update(RDT_PROC_PATH, g_rdt->proc_pids, g_rdt->num_proc_pids);
  if (0 != ret) {
    ERROR(RDT_PLUGIN ": Initial update of proc pids failed");
    return ret;
  }

  for (size_t i = 0; i < g_rdt->num_ngroups; i++) {
    int refresh_result =
        rdt_refresh_ngroup(&(g_rdt->ngroups[i]), g_rdt->pngroups[i]);

    if (0 != refresh_result) {
      ERROR(RDT_PLUGIN ": read_pids_data: NGroup %zu refresh failed. Error: %d",
            i, refresh_result);
      if (0 == ret) {
        /* refresh error will be escalated only if there were no
         * errors before.
         */
        ret = refresh_result;
      }
    }
  }

  assert(ret <= 0);
  return ret;
}

/*
 * NAME
 *   rdt_init_pids_monitoring
 *
 * DESCRIPTION
 *   Initialize pids monitoring for all name groups
 */
static void rdt_init_pids_monitoring() {
  for (size_t group_idx = 0; group_idx < g_rdt->num_ngroups; group_idx++) {
    /*
     * Each group must have not-null proc_pids array.
     * Initial refresh is not mandatory for proper
     * PIDs statistics detection.
     */
    rdt_name_group_t *ng = &g_rdt->ngroups[group_idx];
    int init_result =
        proc_pids_init((const char **)ng->names, ng->num_names, &ng->proc_pids);
    if (0 != init_result) {
      ERROR(RDT_PLUGIN
            ": Initialization of proc_pids for group %zu failed. Error: %d",
            group_idx, init_result);
      continue;
    }

    /* update global proc_pids table */
    proc_pids_t **proc_pids = realloc(g_rdt->proc_pids,
                                      (g_rdt->num_proc_pids + ng->num_names) *
                                          sizeof(*g_rdt->proc_pids));
    if (NULL == proc_pids) {
      ERROR(RDT_PLUGIN ": Alloc error\n");
      continue;
    }

    for (size_t i = 0; i < ng->num_names; i++)
      proc_pids[g_rdt->num_proc_pids + i] = ng->proc_pids[i];

    g_rdt->proc_pids = proc_pids;
    g_rdt->num_proc_pids += ng->num_names;
  }

  if (g_rdt->num_ngroups > 0) {
    int update_result =
        proc_pids_update(RDT_PROC_PATH, g_rdt->proc_pids, g_rdt->num_proc_pids);
    if (0 != update_result)
      ERROR(RDT_PLUGIN ": Initial update of proc pids failed");
  }

  for (size_t group_idx = 0; group_idx < g_rdt->num_ngroups; group_idx++) {
    int refresh_result = rdt_refresh_ngroup(&(g_rdt->ngroups[group_idx]),
                                            g_rdt->pngroups[group_idx]);
    if (0 != refresh_result)
      ERROR(RDT_PLUGIN ": Initial refresh of group %zu failed. Error: %d",
            group_idx, refresh_result);
  }
}
#endif /* LIBPQOS2 */
/*
 * NAME
 *   rdt_free_cgroups
 *
 * DESCRIPTION
 *   Function to deallocate memory allocated for core groups.
 */
static void rdt_free_cgroups(void) {
  config_cores_cleanup(&g_rdt->cores);
  for (int i = 0; i < RDT_MAX_CORES; i++) {
    sfree(g_rdt->pcgroups[i]);
  }
  g_rdt->cores.num_cgroups = 0;
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
    cgroup->cores = calloc(1, sizeof(*cgroup->cores));
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

static int rdt_is_core_id_valid(unsigned int core_id) {

  for (unsigned int i = 0; i < g_rdt->pqos_cpu->num_cores; i++)
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
      if (!rdt_is_core_id_valid(cgroup->cores[core_idx])) {
        ERROR(RDT_PLUGIN ": Core group '%s' contains invalid core id '%u'",
              cgroup->desc, cgroup->cores[core_idx]);
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
  for (unsigned int i = 0; i < g_rdt->cap_mon->u.mon->num_events; i++)
    events |= g_rdt->cap_mon->u.mon->events[i].type;

  events &= ~(PQOS_PERF_EVENT_LLC_MISS);

  DEBUG(RDT_PLUGIN ": Number of cores in the system: %u",
        g_rdt->pqos_cpu->num_cores);
  DEBUG(RDT_PLUGIN ": Available events to monitor: %#x", events);

  g_rdt->cores.num_cgroups = n;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < i; j++) {
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
    g_rdt->pcgroups[i] = calloc(1, sizeof(*g_rdt->pcgroups[i]));
    if (g_rdt->pcgroups[i] == NULL) {
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
                             .verbose = 0,
#ifdef LIBPQOS2
                             .interface = PQOS_INTER_OS_RESCTRL_MON};
  DEBUG(RDT_PLUGIN ": Initializing PQoS with RESCTRL interface");
#else
                             .interface = PQOS_INTER_MSR};
  DEBUG(RDT_PLUGIN ": Initializing PQoS with MSR interface");
#endif

  ret = pqos_init(&pqos);
  DEBUG(RDT_PLUGIN ": PQoS initialization result: [%d]", ret);

#ifdef LIBPQOS2
  if (ret == PQOS_RETVAL_INTER) {
    pqos.interface = PQOS_INTER_MSR;
    DEBUG(RDT_PLUGIN ": Initializing PQoS with MSR interface");
    ret = pqos_init(&pqos);
    DEBUG(RDT_PLUGIN ": PQoS initialization result: [%d]", ret);
  }
#endif

  if (ret != PQOS_RETVAL_OK) {
    ERROR(RDT_PLUGIN ": Error initializing PQoS library!");
    goto rdt_preinit_error1;
  }

  g_interface = pqos.interface;

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
    return 0;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strncasecmp("Cores", child->key, (size_t)strlen("Cores")) == 0) {
      if (g_rdt->cores.num_cgroups > 0) {
        ERROR(RDT_PLUGIN
              ": Configuration parameter \"%s\" can be used only once.",
              child->key);
        g_state = CONFIGURATION_ERROR;
      } else if (rdt_config_cgroups(child) != 0)
        g_state = CONFIGURATION_ERROR;

      if (g_state == CONFIGURATION_ERROR)
        /* if we return -1 at this point collectd
           reports a failure in configuration and
           aborts
         */
        return 0;

#if COLLECT_DEBUG
      rdt_dump_cgroups();
#endif /* COLLECT_DEBUG */
    } else if (strncasecmp("Processes", child->key,
                           (size_t)strlen("Processes")) == 0) {
#ifdef LIBPQOS2
      if (g_interface != PQOS_INTER_OS_RESCTRL_MON) {
        ERROR(RDT_PLUGIN ": Configuration parameter \"%s\" not supported. "
                         "Resctrl monitoring is needed for PIDs monitoring.",
              child->key);
        g_state = CONFIGURATION_ERROR;
      }

      else if (g_rdt->num_ngroups > 0) {
        ERROR(RDT_PLUGIN
              ": Configuration parameter \"%s\" can be used only once.",
              child->key);
        g_state = CONFIGURATION_ERROR;
      }

      else if (rdt_config_ngroups(g_rdt, child) != 0)
        g_state = CONFIGURATION_ERROR;

      if (g_state == CONFIGURATION_ERROR)
        /* if we return -1 at this point collectd
           reports a failure in configuration and
           aborts
         */
        return 0;

#if COLLECT_DEBUG
      rdt_dump_ngroups();
#endif /* COLLECT_DEBUG */
#else  /* !LIBPQOS2 */
      ERROR(RDT_PLUGIN ": Configuration parameter \"%s\" not supported, please "
                       "recompile collectd with libpqos version 2.0 or newer.",
            child->key);
#endif /* LIBPQOS2 */
    } else {
      ERROR(RDT_PLUGIN ": Unknown configuration parameter \"%s\".", child->key);
    }
  }

  return 0;
}

static int read_cores_data() {

  if (0 == g_rdt->cores.num_cgroups) {
    DEBUG(RDT_PLUGIN ": read_cores_data: not configured - Cores read skipped");
    return 0;
  }
  DEBUG(RDT_PLUGIN ": read_cores_data: Cores data poll");

  int ret =
      pqos_mon_poll(&g_rdt->pcgroups[0], (unsigned)g_rdt->cores.num_cgroups);
  if (ret != PQOS_RETVAL_OK) {
    ERROR(RDT_PLUGIN ": read_cores_data: Failed to poll monitoring data for "
                     "cores. Error [%d].",
          ret);
    return -1;
  }

  for (size_t i = 0; i < g_rdt->cores.num_cgroups; i++) {
    core_group_t *cgroup = g_rdt->cores.cgroups + i;
    enum pqos_mon_event mbm_events =
        (PQOS_MON_EVENT_LMEM_BW | PQOS_MON_EVENT_TMEM_BW |
         PQOS_MON_EVENT_RMEM_BW);

    const struct pqos_event_values *pv = &g_rdt->pcgroups[i]->values;

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

#if COLLECT_DEBUG
  rdt_dump_cores_data();
#endif /* COLLECT_DEBUG */

  return 0;
}

static int rdt_read(__attribute__((unused)) user_data_t *ud) {

  if (g_rdt == NULL) {
    ERROR(RDT_PLUGIN ": rdt_read: plugin not initialized.");
    return -EINVAL;
  }

  int cores_read_result = read_cores_data();

#ifdef LIBPQOS2
  int pids_read_result = read_pids_data();
#endif /* LIBPQOS2 */

  if (0 != cores_read_result)
    return cores_read_result;

#ifdef LIBPQOS2
  if (0 != pids_read_result)
    return pids_read_result;
#endif /* LIBPQOS2 */

  return 0;
}

static void rdt_init_cores_monitoring() {
  for (size_t i = 0; i < g_rdt->cores.num_cgroups; i++) {
    core_group_t *cg = g_rdt->cores.cgroups + i;

    int mon_start_result =
        pqos_mon_start(cg->num_cores, cg->cores, g_rdt->events[i],
                       (void *)cg->desc, g_rdt->pcgroups[i]);

    if (mon_start_result != PQOS_RETVAL_OK)
      ERROR(RDT_PLUGIN
            ": Error starting cores monitoring group %s (pqos status=%d)",
            cg->desc, mon_start_result);
  }
}

static int rdt_init(void) {

  if (g_state == CONFIGURATION_ERROR) {
    if (g_rdt != NULL) {
      if (g_rdt->cores.num_cgroups > 0)
        rdt_free_cgroups();
#ifdef LIBPQOS2
      if (g_rdt->num_ngroups > 0)
        rdt_free_ngroups(g_rdt);
#endif
    }
    return -1;
  }

  int rdt_preinint_result = rdt_preinit();
  if (rdt_preinint_result != 0)
    return rdt_preinint_result;

  rdt_init_cores_monitoring();
#ifdef LIBPQOS2
  rdt_init_pids_monitoring();
#endif /* LIBPQOS2 */

  return 0;
}

static int rdt_shutdown(void) {
  int ret;

  DEBUG(RDT_PLUGIN ": rdt_shutdown.");

  if (g_rdt == NULL)
    return 0;

  /* Stop monitoring cores */
  for (size_t i = 0; i < g_rdt->cores.num_cgroups; i++) {
    pqos_mon_stop(g_rdt->pcgroups[i]);
  }

/* Stop pids monitoring */
#ifdef LIBPQOS2
  for (size_t i = 0; i < g_rdt->num_ngroups; i++)
    pqos_mon_stop(g_rdt->pngroups[i]);
#endif

  ret = pqos_fini();
  if (ret != PQOS_RETVAL_OK)
    ERROR(RDT_PLUGIN ": Error shutting down PQoS library.");
  rdt_free_cgroups();
#ifdef LIBPQOS2
  rdt_free_ngroups(g_rdt);
#endif /* LIBPQOS2 */
  sfree(g_rdt);

  return 0;
}

void module_register(void) {
  plugin_register_init(RDT_PLUGIN, rdt_init);
  plugin_register_complex_config(RDT_PLUGIN, rdt_config);
  plugin_register_complex_read(NULL, RDT_PLUGIN, rdt_read, 0, NULL);
  plugin_register_shutdown(RDT_PLUGIN, rdt_shutdown);
}
