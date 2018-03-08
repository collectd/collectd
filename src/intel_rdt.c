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
 *   Starzyk, Mateusz <mateuszx.starzyk@intel.com>
 *   Wojciech Andralojc <wojciechx.andralojc@intel.com>
 **/

#include "collectd.h"
#include "utils/common/common.h"
#include "utils/config_cores/config_cores.h"
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
#define RDT_MAX_NAME_LEN 16
#define RDT_MAX_NAMES_GROUPS 64

#define RDT_PROC_PATH "/proc"
#endif /* LIBPQOS2 */

typedef enum {
  UNKNOWN = 0,
  CONFIGURATION_ERROR,
} rdt_config_status;

#ifdef LIBPQOS2
/* Helper typedef for process name array
 * Extra 1 char is added for string null termination.
 */
typedef char proc_comm_t[RDT_MAX_NAME_LEN + 1];

/* Linked one-way list of pids. */
typedef struct pids_list_s {
  pid_t pid;
  struct pids_list_s *next;
} pids_list_t;

/* Holds process name and list of pids assigned to that name */
typedef struct proc_pids_s {
  proc_comm_t proccess_name;
  pids_list_t *pids;
} proc_pids_t;

struct rdt_name_group_s {
  char *desc;
  size_t num_names;
  char **names;
  proc_pids_t *proc_pids_array;
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
#endif /* LIBPQOS2 */
  const struct pqos_cpuinfo *pqos_cpu;
  const struct pqos_cap *pqos_cap;
  const struct pqos_capability *cap_mon;
};
typedef struct rdt_ctx_s rdt_ctx_t;

static rdt_ctx_t *g_rdt;

static rdt_config_status g_state = UNKNOWN;

#ifdef LIBPQOS2
static int isdupstr(const char *names[], const size_t size, const char *name) {
  for (size_t i = 0; i < size; i++)
    if (strncmp(names[i], name, (size_t)RDT_MAX_NAME_LEN) == 0)
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

  for (;;) {
    char *token = strtok_r(str_list, ",", &saveptr);
    if (token == NULL)
      break;

    str_list = NULL;

    while (isspace(*token))
      token++;

    if (*token == '\0')
      continue;

    if (!(isdupstr((const char **)*names, *names_num, token)))
      if (0 != strarray_add(names, names_num, token)) {
        ERROR(RDT_PLUGIN ": Error allocating process name string");
        return -ENOMEM;
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
      if (strncmp(tab_a[i], tab_b[j], (size_t)RDT_MAX_NAME_LEN) == 0)
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
        (strlen(item->values[j].value.string) == 0))
      continue;

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

    groups[index].proc_pids_array = NULL;

    index++;

    if (index >= (const int)max_groups) {
      WARNING(RDT_PLUGIN ": Too many process names groups configured");
      return index;
    }
  }

  return index;
}
#endif /* LIBPQOS2 */

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

static void rdt_dump_data(void) {
  /*
   * CORE - monitored group of cores
   * NAME - monitored group of processes
   * RMID - Resource Monitoring ID associated with the monitored group
   * LLC - last level cache occupancy
   * MBL - local memory bandwidth
   * MBR - remote memory bandwidth
   */
  DEBUG("  CORE     RMID    LLC[KB]   MBL[MB]    MBR[MB]");
  for (size_t i = 0; i < g_rdt->cores.num_cgroups; i++) {

    const struct pqos_event_values *pv = &g_rdt->pcgroups[i]->values;

    double llc = bytes_to_kb(pv->llc);
    double mbr = bytes_to_mb(pv->mbm_remote_delta);
    double mbl = bytes_to_mb(pv->mbm_local_delta);

    DEBUG(" [%s] %8u %10.1f %10.1f %10.1f", g_rdt->cores.cgroups[i].desc,
          g_rdt->pcgroups[i]->poll_ctx[0].rmid, llc, mbl, mbr);
  }

#ifdef LIBPQOS2
  DEBUG("  NAME     PIDs");
  char pids[DATA_MAX_NAME_LEN];
  for (size_t i = 0; i < g_rdt->num_ngroups; ++i) {
    memset(pids, 0, sizeof(pids));
    for (size_t j = 0; j < g_rdt->ngroups[i].num_names; ++j) {
      pids_list_t *list = g_rdt->ngroups[i].proc_pids_array[j].pids;
      while (list != NULL) {
        snprintf(pids + strlen(pids), sizeof(pids) - strlen(pids) - 1, " %u",
                 list->pid);
        list = list->next;
      }
    }
    DEBUG(" [%s] %s", g_rdt->ngroups[i].desc, pids);
  }

  DEBUG("  NAME     RMID    LLC[KB]   MBL[MB]    MBR[MB]");
  for (size_t i = 0; i < g_rdt->num_ngroups; i++) {

    if (g_rdt->pngroups[i]->poll_ctx == NULL)
      continue;

    const struct pqos_event_values *pv = &g_rdt->pngroups[i]->values;

    double llc = bytes_to_kb(pv->llc);
    double mbr = bytes_to_mb(pv->mbm_remote_delta);
    double mbl = bytes_to_mb(pv->mbm_local_delta);

    DEBUG(" [%s] %8u %10.1f %10.1f %10.1f", g_rdt->ngroups[i].desc,
          g_rdt->pngroups[i]->poll_ctx[0].rmid, llc, mbl, mbr);
  }
#endif /* LIBPQOS2 */
}
#endif /* COLLECT_DEBUG */

static void rdt_free_cgroups(void) {
  config_cores_cleanup(&g_rdt->cores);
  for (int i = 0; i < RDT_MAX_CORES; i++) {
    sfree(g_rdt->pcgroups[i]);
  }
}

#ifdef LIBPQOS2
static int pids_list_free(pids_list_t *list) {
  assert(list);

  pids_list_t *current = list;
  while (current != NULL) {
    pids_list_t *previous = current;
    current = current->next;
    sfree(previous);
  }
  return 0;
}

static void rdt_free_ngroups(void) {
  for (int i = 0; i < RDT_MAX_NAMES_GROUPS; i++) {
    DEBUG(RDT_PLUGIN ": Freeing \'%s\' group\'s data...",
          g_rdt->ngroups[i].desc);
    sfree(g_rdt->ngroups[i].desc);
    strarray_free(g_rdt->ngroups[i].names, g_rdt->ngroups[i].num_names);

    if (g_rdt->ngroups[i].proc_pids_array) {
      for (size_t j = 0; j < g_rdt->ngroups[i].num_names; ++j) {
        if (NULL == g_rdt->ngroups[i].proc_pids_array[j].pids)
          continue;
        pids_list_free(g_rdt->ngroups[i].proc_pids_array[j].pids);
      }

      sfree(g_rdt->ngroups[i].proc_pids_array);
    }

    g_rdt->ngroups[i].num_names = 0;
    sfree(g_rdt->pngroups[i]);
  }
}
#endif /* LIBPQOS2 */

static int rdt_default_cgroups(void) {
  unsigned num_cores = g_rdt->pqos_cpu->num_cores;

  g_rdt->cores.cgroups = calloc(num_cores, sizeof(*g_rdt->cores.cgroups));
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

#ifdef LIBPQOS2
static int rdt_is_proc_name_valid(const char *name) {

  if (name != NULL) {
    unsigned len = strlen(name);
    if (len > 0 && len <= RDT_MAX_NAME_LEN)
      return 1;
    else {
      DEBUG(RDT_PLUGIN
            ": Process name \'%s\' is too long. Max supported len is %d chars.",
            name, RDT_MAX_NAME_LEN);
    }
  }

  return 0;
}
#endif /* LIBPQOS2 */

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

#ifdef LIBPQOS2
static int rdt_config_ngroups(const oconfig_item_t *item) {
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

  n = oconfig_to_ngroups(item, g_rdt->ngroups, RDT_MAX_NAMES_GROUPS);
  if (n < 0) {
    rdt_free_ngroups();
    ERROR(RDT_PLUGIN ": Error parsing process name groups configuration.");
    return -EINVAL;
  }

  /* validate configured process name values */
  for (int group_idx = 0; group_idx < n; group_idx++) {
    for (size_t name_idx = 0; name_idx < g_rdt->ngroups[group_idx].num_names;
         name_idx++) {
      if (!rdt_is_proc_name_valid(g_rdt->ngroups[group_idx].names[name_idx])) {
        ERROR(RDT_PLUGIN ": Process name group '%s' contains invalid name '%s'",
              g_rdt->ngroups[group_idx].desc,
              g_rdt->ngroups[group_idx].names[name_idx]);
        rdt_free_ngroups();
        return -EINVAL;
      }
    }
  }

  if (n == 0) {
    ERROR(RDT_PLUGIN ": Empty process name groups configured.");
    return -EINVAL;
  }

  /* Get all available events on this platform */
  for (unsigned i = 0; i < g_rdt->cap_mon->u.mon->num_events; i++)
    events |= g_rdt->cap_mon->u.mon->events[i].type;

  events &= ~(PQOS_PERF_EVENT_LLC_MISS);

  DEBUG(RDT_PLUGIN ": Available events to monitor: %#x", events);

  g_rdt->num_ngroups = n;
  for (int i = 0; i < n; i++) {
    for (int j = 0; j < i; j++) {
      int found = ngroup_cmp(&g_rdt->ngroups[j], &g_rdt->ngroups[i]);
      if (found != 0) {
        rdt_free_ngroups();
        ERROR(RDT_PLUGIN
              ": Cannot monitor same process name in different groups.");
        return -EINVAL;
      }
    }

    g_rdt->ngroups[i].events = events;
    g_rdt->pngroups[i] = calloc(1, sizeof(*g_rdt->pngroups[i]));
    if (g_rdt->pngroups[i] == NULL) {
      rdt_free_ngroups();
      ERROR(RDT_PLUGIN
            ": Failed to allocate memory for process name monitoring data.");
      return -ENOMEM;
    }
  }

  return 0;
}

/*
 * NAME
 *   pids_list_add_pid
 *
 * DESCRIPTION
 *   Adds pid at the end of the pids list.
 *   Allocates memory for new pid element, it is up to user to free it.
 *
 * PARAMETERS
 *   `list'     Head of target pids_list.
 *   `pid'      Pid to be added.
 *
 * RETURN VALUE
 *   On success, returns 0.
 *   -1 on memory allocation error.
 */
static int pids_list_add_pid(pids_list_t **list, const pid_t pid) {
  assert(list);

  pids_list_t *new_element = calloc(1, sizeof(*new_element));

  if (new_element == NULL) {
    ERROR(RDT_PLUGIN ": Alloc error\n");
    return -1;
  }
  new_element->pid = pid;
  new_element->next = NULL;

  pids_list_t **current = list;
  while (*current != NULL) {
    current = &((*current)->next);
  }
  *current = new_element;
  return 0;
}

/*
 * NAME
 *   pids_list_contains_pid
 *
 * DESCRIPTION
 *   Tests if pids list contains specific pid.
 *
 * PARAMETERS
 *   `list'     Head of pids_list.
 *   `pid'      Pid to be searched for.
 *
 * RETURN VALUE
 *   If PID found in list, returns 1,
 *   Otherwise returns 0.
 */
static int pids_list_contains_pid(pids_list_t *list, const pid_t pid) {
  assert(list);

  pids_list_t *current = list;
  while (current != NULL) {
    if (current->pid == pid)
      return 1;
    current = current->next;
  }
  return 0;
}

/*
 * NAME
 *   pids_list_add_pids_list
 *
 * DESCRIPTION
 *   Adds pids list at the end of the pids list.
 *   Allocates memory for new pid elements, it is up to user to free it.
 *   Increases dst_num by a number of added PIDs.
 *
 * PARAMETERS
 *   `dst'      Head of target PIDs list.
 *   `src'      Head of source PIDs list.
 *   `dst_num'  Variable to be increased by a number of appended PIDs.
 *
 * RETURN VALUE
 *   On success, returns 0.
 *   -1 on memory allocation error.
 */
static int pids_list_add_pids_list(pids_list_t **dst, pids_list_t *src,
                                   size_t *dst_num) {
  assert(dst);
  assert(src);
  assert(dst_num);

  pids_list_t *current = src;
  int ret;

  while (current != NULL) {
    ret = pids_list_add_pid(dst, current->pid);
    if (0 != ret)
      return ret;

    ++(*dst_num);
    current = current->next;
  }

  return 0;
}

/*
 * NAME
 *   read_proc_name
 *
 * DESCRIPTION
 *   Reads process name from given pid directory.
 *   Strips new-line character (\n).
 *
 * PARAMETERS
 *   `procfs_path` Path to systems proc directory (e.g. /proc)
 *   `pid_entry'   Dirent for PID directory
 *   `name'        Output buffer for process name, recommended proc_comm.
 *   `out_size'    Output buffer size, recommended sizeof(proc_comm)
 *
 * RETURN VALUE
 *   On success, the number of read bytes (includes stripped \n).
 *   -1 on file open error
 */
static int read_proc_name(const char *procfs_path,
                          const struct dirent *pid_entry, char *name,
                          const size_t out_size) {
  assert(procfs_path);
  assert(pid_entry);
  assert(name);
  assert(out_size);
  memset(name, 0, out_size);

  const char *comm_file_name = "comm";

  char *path = ssnprintf_alloc("%s/%s/%s", procfs_path, pid_entry->d_name,
                               comm_file_name);

  FILE *f = fopen(path, "r");
  if (f == NULL) {
    ERROR(RDT_PLUGIN ": Failed to open comm file, error: %d\n", errno);
    sfree(path);
    return -1;
  }
  size_t read_length = fread(name, sizeof(char), out_size, f);
  fclose(f);
  sfree(path);
  /* strip new line ending */
  char *newline = strchr(name, '\n');
  if (newline) {
    *newline = '\0';
  }

  return read_length;
}

/*
 * NAME
 *   get_pid_number
 *
 * DESCRIPTION
 *   Gets pid number for given /proc/pid directory entry or
 *   returns error if input directory does not hold PID information.
 *
 * PARAMETERS
 *   `entry'    Dirent for PID directory
 *   `pid'      PID number to be filled
 *
 * RETURN VALUE
 *   0 on success. Negative number on error:
 *   -1: given entry is not a directory
 *   -2: PID conversion error
 */
static int get_pid_number(struct dirent *entry, pid_t *pid) {
  char *tmp_end; /* used for strtoul error check*/

  if (pid == NULL || entry == NULL)
    return -1;

  if (entry->d_type != DT_DIR)
    return -1;

  /* trying to get pid number from directory name*/
  *pid = strtoul(entry->d_name, &tmp_end, 10);
  if (*tmp_end != '\0') {
    return -2; /* conversion failed, not proc-pid */
  }
  /* all checks passed, marking as success */
  return 0;
}

/*
 * NAME
 *   fetch_pids_for_procs
 *
 * DESCRIPTION
 *   Finds PIDs matching given process's names.
 *   Searches all PID directories in /proc fs and
 *   allocates memory for proc_pids structs, it is up to user to free it.
 *   Output array will have same element count as input array.
 *
 * PARAMETERS
 *   `procfs_path'      Path to systems proc directory (e.g. /proc)
 *   `procs'            Array of null-terminated strings with
 *                      process' names to search for
 *   `procs_size'       procs array element count
 *   `proc_pids_array'  Address of pointer, under which new
 *                      array of proc_pids will be allocated. Must be NULL.
 *
 * RETURN VALUE
 *   0 on success. Negative number on error:
 *   -1: could not open /proc dir
 */
__attribute__((unused)) /* TODO: remove this attribute when PID monitoring is
                           implemented */
static int
fetch_pids_for_procs(const char *procfs_path, const char **procs_names_array,
                     const size_t procs_names_array_size,
                     proc_pids_t **proc_pids_array) {
  assert(procfs_path);
  assert(procs_names_array);
  assert(procs_names_array_size);
  assert(proc_pids_array);
  assert(NULL == *proc_pids_array);

  DIR *proc_dir = opendir(procfs_path);
  if (proc_dir == NULL) {
    ERROR(RDT_PLUGIN ": Could not open %s directory, error: %d", procfs_path,
          errno);
    return -1;
  }

  /* Copy procs names to output array. Initialize pids list with NULL value. */
  (*proc_pids_array) =
      calloc(procs_names_array_size, sizeof(**proc_pids_array));
  for (size_t i = 0; i < procs_names_array_size; ++i) {
    sstrncpy((*proc_pids_array)[i].proccess_name, procs_names_array[i],
             STATIC_ARRAY_SIZE((*proc_pids_array)[i].proccess_name));
    (*proc_pids_array)[i].pids = NULL;
  }

  /* Go through procfs and find PIDS and their comms */
  struct dirent *entry;
  while ((entry = readdir(proc_dir)) != NULL) {

    pid_t pid;
    int pid_conversion = get_pid_number(entry, &pid);
    if (pid_conversion < 0)
      continue;

    proc_comm_t comm;
    int read_result =
        read_proc_name(procfs_path, entry, comm, sizeof(proc_comm_t));
    if (read_result <= 0) {
      ERROR(RDT_PLUGIN ": Comm file skipped. Read result: %d", read_result);
      continue;
    }

    /* Try to find comm in input procs array (proc_pids_array has same names) */
    for (size_t i = 0; i < procs_names_array_size; ++i) {
      if (0 == strncmp(comm, (*proc_pids_array)[i].proccess_name,
                       STATIC_ARRAY_SIZE(comm)))
        pids_list_add_pid(&((*proc_pids_array)[i].pids), pid);
    }
  }

  int close_result = closedir(proc_dir);
  if (0 != close_result) {
    ERROR(RDT_PLUGIN ": failed to close %s directory, error: %d", procfs_path,
          errno);
    return -1;
  }
  return 0;
}
#endif /* LIBPQOS2 */

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

    if (strncasecmp("Cores", child->key, (size_t)strlen("Cores")) == 0) {
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
    } else if (strncasecmp("Processes", child->key,
                           (size_t)strlen("Processes")) == 0) {
#ifdef LIBPQOS2
      if (rdt_config_ngroups(child) != 0) {
        g_state = CONFIGURATION_ERROR;
        /* if we return -1 at this point collectd
           reports a failure in configuration and
           aborts
         */
        return (0);
      }

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

#ifdef LIBPQOS2
static int rdt_pid_list_diff(pids_list_t *prev, pids_list_t *curr,
                             pids_list_t **added, size_t *added_num,
                             pids_list_t **removed, size_t *removed_num) {
  assert(prev || curr);
  assert(added);
  assert(removed);

  if (NULL == prev) {
    /* append all PIDs from curr to added*/
    return pids_list_add_pids_list(added, curr, added_num);
  } else if (NULL == curr) {
    /* append all PIDs from prev to removed*/
    return pids_list_add_pids_list(removed, prev, removed_num);
  }

  pids_list_t *item = prev;
  while (item != NULL) {
    if (0 == pids_list_contains_pid(curr, item->pid)) {
      pids_list_add_pid(removed, item->pid);
      ++(*removed_num);
    }
    item = item->next;
  }

  item = curr;
  while (item != NULL) {
    if (0 == pids_list_contains_pid(prev, item->pid)) {
      pids_list_add_pid(added, item->pid);
      ++(*added_num);
    }
    item = item->next;
  }

  return 0;
}

static int rdt_refresh_ngroup(rdt_name_group_t *ngroup) {
  if (NULL == ngroup)
    return -1;

  DEBUG(RDT_PLUGIN ": rdt_refresh_ngroup: \'%s\' process names group.",
        ngroup->desc);

  proc_pids_t *pids_array_prev = ngroup->proc_pids_array;
  proc_pids_t *pids_array_curr = NULL;

  int fetch_result =
      fetch_pids_for_procs(RDT_PROC_PATH, (const char **)ngroup->names,
                           ngroup->num_names, &pids_array_curr);

  if (0 != fetch_result) {
    ERROR(RDT_PLUGIN ": rdt_refresh_ngroup: failed to fetch PIDs for \'%s\' "
                     "process names group.",
          ngroup->desc);
    return fetch_result;
  }

  if (NULL == pids_array_prev) {
    /*no PIDs info yet, just save current one for next iteration*/
    ngroup->proc_pids_array = pids_array_curr;
    return 0;
  }

  pids_list_t *added = NULL;
  size_t added_num = 0;

  pids_list_t *removed = NULL;
  size_t removed_num = 0;

  for (size_t i = 0; i < ngroup->num_names; ++i) {
    if (NULL == pids_array_prev[i].pids && NULL == pids_array_curr[i].pids)
      continue;
    rdt_pid_list_diff(pids_array_prev[i].pids, pids_array_curr[i].pids, &added,
                      &added_num, &removed, &removed_num);
  }

  DEBUG(RDT_PLUGIN ": rdt_refresh_ngroup: \'%s\' process names group, added: "
                   "%u, removed: %u.",
        ngroup->desc, (unsigned)added_num, (unsigned)removed_num);

  if (added_num != 0 || removed_num != 0) {
    ngroup->proc_pids_array = pids_array_curr;

    /*call pqos add and remove functions here*/
  }

  /*free prev PID lists, only if new was saved in ngroup struct*/
  if (pids_array_prev && pids_array_prev != ngroup->proc_pids_array) {
    for (size_t i = 0; i < ngroup->num_names; ++i) {
      if (NULL == pids_array_prev[i].pids)
        continue;
      pids_list_free(pids_array_prev[i].pids);
    }

    sfree(pids_array_prev);
  }

  return 0;
}
#endif /* LIBPQOS2 */

static int rdt_read(__attribute__((unused)) user_data_t *ud) {
  int ret;

  if (g_rdt == NULL) {
    ERROR(RDT_PLUGIN ": rdt_read: plugin not initialized.");
    return -EINVAL;
  }

  ret = pqos_mon_poll(&g_rdt->pcgroups[0], (unsigned)g_rdt->cores.num_cgroups);
  if (ret != PQOS_RETVAL_OK) {
    ERROR(RDT_PLUGIN ": Failed to poll monitoring data.");
    return -1;
  }

#ifdef LIBPQOS2
  for (size_t i = 0; i < g_rdt->num_ngroups; i++)
    rdt_refresh_ngroup(&g_rdt->ngroups[i]);
#endif /* LIBPQOS2 */

#if COLLECT_DEBUG
  rdt_dump_data();
#endif /* COLLECT_DEBUG */

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
  for (size_t i = 0; i < g_rdt->cores.num_cgroups; i++) {
    core_group_t *cg = g_rdt->cores.cgroups + i;

    ret = pqos_mon_start(cg->num_cores, cg->cores, g_rdt->events[i],
                         (void *)cg->desc, g_rdt->pcgroups[i]);

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
  for (size_t i = 0; i < g_rdt->cores.num_cgroups; i++) {
    pqos_mon_stop(g_rdt->pcgroups[i]);
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
