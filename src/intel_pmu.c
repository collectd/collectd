/**
 * collectd - src/intel_pmu.c
 *
 * Copyright(c) 2017-2020 Intel Corporation. All rights reserved.
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
 *   Kamil Wiatrowski <kamilx.wiatrowski@intel.com>
 **/

#include "collectd.h"
#include "utils/common/common.h"

#include "utils/config_cores/config_cores.h"

#include <jevents.h>
#include <jsession.h>

#define PMU_PLUGIN "intel_pmu"
#define CGROUPS_PER_ENT 2

struct intel_pmu_entity_s {
  char **hw_events;
  size_t hw_events_count;
  core_groups_list_t cores;
  size_t first_cgroup;
  size_t cgroups_count;
  bool copied;
  bool all_events;
  struct eventlist *event_list;
  user_data_t user_data;
  struct intel_pmu_entity_s *next;
};
typedef struct intel_pmu_entity_s intel_pmu_entity_t;

struct intel_pmu_ctx_s {
  char event_list_fn[PATH_MAX];
  bool dispatch_cloned_pmus;

  intel_pmu_entity_t *entl;
};
typedef struct intel_pmu_ctx_s intel_pmu_ctx_t;

static intel_pmu_ctx_t g_ctx;

#if COLLECT_DEBUG
static void pmu_dump_events(intel_pmu_entity_t *ent) {

  DEBUG(PMU_PLUGIN ": Events:");

  struct event *e;

  for (e = ent->event_list->eventlist; e; e = e->next) {
    DEBUG(PMU_PLUGIN ":   event       : %s", e->event);
    DEBUG(PMU_PLUGIN ":     group_lead: %d", e->group_leader);
    DEBUG(PMU_PLUGIN ":     in_group  : %d", e->ingroup);
    DEBUG(PMU_PLUGIN ":     end_group : %d", e->end_group);
    DEBUG(PMU_PLUGIN ":     type      : %d", e->attr.type);
    DEBUG(PMU_PLUGIN ":     config    : %#x", (unsigned)e->attr.config);
    DEBUG(PMU_PLUGIN ":     size      : %d", e->attr.size);
    if (e->attr.sample_period > 0)
      DEBUG(PMU_PLUGIN ":     period    : %lld", e->attr.sample_period);
    if (e->extra.decoded)
      DEBUG(PMU_PLUGIN ":     perf      : %s", e->extra.decoded);
    if (e->extra.name)
      DEBUG(PMU_PLUGIN ":     name      : %s", e->extra.name);
    DEBUG(PMU_PLUGIN ":     uncore    : %d", e->uncore);
  }
}

static void pmu_dump_config(void) {

  DEBUG(PMU_PLUGIN ": Config:");
  DEBUG(PMU_PLUGIN ":   AggregateUncorePMUs : %d", !g_ctx.dispatch_cloned_pmus);
  DEBUG(PMU_PLUGIN ":   event list file     : %s", g_ctx.event_list_fn);

  unsigned int i = 0;
  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL; ent = ent->next)
    for (size_t j = 0; j < ent->hw_events_count; j++) {
      DEBUG(PMU_PLUGIN ":   hardware_events[%u]  : %s", i++, ent->hw_events[j]);
    }
}

static void pmu_dump_cpu(void) {

  DEBUG(PMU_PLUGIN ": num cpus   : %d", g_ctx.entl->event_list->num_cpus);
  DEBUG(PMU_PLUGIN ": num sockets: %d", g_ctx.entl->event_list->num_sockets);
  for (size_t i = 0; i < g_ctx.entl->event_list->num_sockets; i++) {
    DEBUG(PMU_PLUGIN ":   socket [%" PRIsz "] core: %d", i,
          g_ctx.entl->event_list->socket_cpus[i]);
  }
}

static void pmu_dump_cgroups(intel_pmu_entity_t *ent) {

  DEBUG(PMU_PLUGIN ": Cores:");

  for (size_t i = 0; i < ent->cores.num_cgroups; i++) {
    core_group_t *cgroup = ent->cores.cgroups + i;
    const size_t cores_size = cgroup->num_cores * 4 + 1;
    char *cores = calloc(cores_size, sizeof(*cores));
    if (cores == NULL) {
      DEBUG(PMU_PLUGIN ": Failed to allocate string to list cores.");
      return;
    }
    for (size_t j = 0; j < cgroup->num_cores; j++)
      if (snprintf(cores + strlen(cores), cores_size - strlen(cores), " %d",
                   cgroup->cores[j]) < 0) {
        DEBUG(PMU_PLUGIN ": Failed to write list of cores to string.");
        sfree(cores);
        return;
      }

    DEBUG(PMU_PLUGIN ":   group[%" PRIsz "]", i);
    DEBUG(PMU_PLUGIN ":     description: %s", cgroup->desc);
    DEBUG(PMU_PLUGIN ":     cores count: %" PRIsz, cgroup->num_cores);
    DEBUG(PMU_PLUGIN ":     cores      :%s", cores);
    sfree(cores);
  }
}

#endif /* COLLECT_DEBUG */

static int pmu_validate_cgroups(core_group_t *cgroups, size_t len,
                                int max_cores) {
  /* i - group index, j - core index */
  for (size_t i = 0; i < len; i++) {
    for (size_t j = 0; j < cgroups[i].num_cores; j++) {
      int core = (int)cgroups[i].cores[j];

      /* Core index cannot exceed number of cores in system,
         note that max_cores include both online and offline CPUs. */
      if (core >= max_cores) {
        ERROR(PMU_PLUGIN ": Core %d is not valid, max core index: %d.", core,
              max_cores - 1);
        return -1;
      }
    }
    /* Check if cores are set in remaining groups */
    for (size_t k = i + 1; k < len; k++)
      if (config_cores_cmp_cgroups(&cgroups[i], &cgroups[k]) != 0) {
        ERROR(PMU_PLUGIN ": Same cores cannot be set in different groups.");
        return -1;
      }
  }
  return 0;
}

static int pmu_config_hw_events(oconfig_item_t *ci, intel_pmu_entity_t *ent) {

  if (strcasecmp("HardwareEvents", ci->key) != 0) {
    return -EINVAL;
  }

  if (ent->hw_events) {
    ERROR(PMU_PLUGIN ": Duplicate config for HardwareEvents.");
    return -EINVAL;
  }

  // check if all events has been requested
  for (int i = 0; i < ci->values_num; i++) {
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING(PMU_PLUGIN ": The %s option requires string arguments.", ci->key);
      continue;
    }

    if (strcasecmp(ci->values[i].value.string, "All") == 0) {
      INFO(PMU_PLUGIN ": Requested all events.");
      ent->all_events = true;
      return 0;
    }
  }

  ent->hw_events = calloc(ci->values_num, sizeof(*ent->hw_events));
  if (ent->hw_events == NULL) {
    ERROR(PMU_PLUGIN ": Failed to allocate hw events.");
    return -ENOMEM;
  }

  for (int i = 0; i < ci->values_num; i++) {
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING(PMU_PLUGIN ": The %s option requires string arguments.", ci->key);
      continue;
    }

    ent->hw_events[ent->hw_events_count] = strdup(ci->values[i].value.string);
    if (ent->hw_events[ent->hw_events_count] == NULL) {
      ERROR(PMU_PLUGIN ": Failed to allocate hw events entry.");
      return -ENOMEM;
    }

    ent->hw_events_count++;
  }

  return 0;
}

static int pmu_config(oconfig_item_t *ci) {

  DEBUG(PMU_PLUGIN ": %s:%d", __FUNCTION__, __LINE__);

  for (int i = 0; i < ci->children_num; i++) {
    int ret = 0;
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("EventList", child->key) == 0) {
      ret = cf_util_get_string_buffer(child, g_ctx.event_list_fn,
                                      sizeof(g_ctx.event_list_fn));
    } else if (strcasecmp("HardwareEvents", child->key) == 0) {
      intel_pmu_entity_t *ent = calloc(1, sizeof(*ent));
      if (ent == NULL) {
        ERROR(PMU_PLUGIN ": Failed to allocate pmu ent.");
        ret = -ENOMEM;
      } else {
        ret = pmu_config_hw_events(child, ent);
        ent->next = g_ctx.entl;
        g_ctx.entl = ent;
      }
    } else if (strcasecmp("Cores", child->key) == 0) {
      if (g_ctx.entl == NULL) {
        ERROR(PMU_PLUGIN
              ": `Cores` option is found before `HardwareEvents` was set.");
        ret = -1;
      } else if (g_ctx.entl->cores.num_cgroups != 0) {
        ERROR(PMU_PLUGIN
              ": Duplicated `Cores` option for single `HardwareEvents`.");
        ret = -1;
      } else {
        ret = config_cores_parse(child, &g_ctx.entl->cores);
      }
    } else if (strcasecmp("AggregateUncorePMUs", child->key) == 0) {
      bool aggregate = true;
      ret = cf_util_get_boolean(child, &aggregate);
      if (ret == 0)
        g_ctx.dispatch_cloned_pmus = !aggregate;
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

static void pmu_submit_counters(const char *cgroup, const char *event,
                                const char *pmu_name, bool multi_pmu,
                                counter_t scaled, counter_t raw,
                                counter_t enabled, counter_t running) {
  value_list_t vl = VALUE_LIST_INIT;

  value_t values[] = {{.counter = scaled},
                      {.counter = raw},
                      {.counter = enabled},
                      {.counter = running}};
  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);

  sstrncpy(vl.plugin, PMU_PLUGIN, sizeof(vl.plugin));
  if (pmu_name)
    ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s:%s", cgroup,
              pmu_name);
  else
    sstrncpy(vl.plugin_instance, cgroup, sizeof(vl.plugin_instance));

  sstrncpy(vl.type, "pmu_counter", sizeof(vl.type));
  sstrncpy(vl.type_instance, event, sizeof(vl.type_instance));

  DEBUG(PMU_PLUGIN ": %s/%s = %llu (%llu * %llu / %llu)", vl.type_instance,
        vl.plugin_instance, scaled, raw, enabled, running);

  plugin_dispatch_values(&vl);
}

static char *pmu_get_name(const struct event *e, const uint32_t *type) {

  if (type != NULL && (e->extra.pmus.gl_pathc > 0 || e->orig)) {
    const struct event *ce =
        e->extra.pmus.gl_pathc == 0 && e->orig ? e->orig : e;

    for (size_t i = 0; i < ce->extra.pmus.gl_pathc; i++) {
      char type_path[PATH_MAX];
      char buf[16];
      ssize_t len;
      unsigned int val = 0;
      ssnprintf(type_path, sizeof(type_path), "%s/type",
                ce->extra.pmus.gl_pathv[i]);
      int fd = open(type_path, O_RDONLY);
      if (fd < 0) {
        WARNING(PMU_PLUGIN ": failed to open `%s`.", type_path);
        continue;
      }

      if ((len = read(fd, buf, sizeof(buf) - 1)) <= 0) {
        WARNING(PMU_PLUGIN ": failed to read type for `%s`.",
                ce->extra.pmus.gl_pathv[i]);
        close(fd);
        continue;
      }
      buf[len] = '\0';

      if (sscanf(buf, "%u", &val) != 1) {
        WARNING(PMU_PLUGIN ": failed to read number from `%s`.", buf);
        close(fd);
        continue;
      }
      close(fd);

      if (*type == val) {
        char *name = NULL;
        char *pos = strrchr(ce->extra.pmus.gl_pathv[i], '/');
        if (pos)
          name = strdup(pos + 1);
        if (name == NULL)
          WARNING(PMU_PLUGIN ": Failed to get pmu name from path.");
        return name;
      }
    }
  } else if (e->extra.decoded) {
    char *name = NULL;
    char *pos = strchr(e->extra.decoded, '/');

    if (pos)
      name = strndup(e->extra.decoded, pos - e->extra.decoded);
    if (name == NULL)
      WARNING(PMU_PLUGIN ": Failed to get pmu name.");

    return name;
  }

  WARNING(PMU_PLUGIN ": No data for pmu name found.");
  return NULL;
}

static void pmu_dispatch_data(intel_pmu_entity_t *ent) {

  struct event *e;

  for (e = ent->event_list->eventlist; e; e = e->next) {
    const uint32_t *event_type = NULL;
    if (e->orig && !g_ctx.dispatch_cloned_pmus)
      continue;
    if ((e->extra.multi_pmu || e->orig) && g_ctx.dispatch_cloned_pmus)
      event_type = &e->attr.type;

    char *pmu_name = pmu_get_name(e, event_type);

    for (size_t i = 0; i < ent->cgroups_count; i++) {
      core_group_t *cgroup = ent->cores.cgroups + i + ent->first_cgroup;
      uint64_t cgroup_value = 0;
      uint64_t cgroup_value_raw = 0;
      uint64_t cgroup_time_enabled = 0;
      uint64_t cgroup_time_running = 0;
      int event_enabled_cgroup = 0;

      for (size_t j = 0; j < cgroup->num_cores; j++) {
        int core = (int)cgroup->cores[j];
        if (e->efd[core].fd < 0)
          continue;

        event_enabled_cgroup++;

        cgroup_value_raw += e->efd[core].val[0];
        cgroup_time_enabled += e->efd[core].val[1];
        cgroup_time_running += e->efd[core].val[2];

        /* If there are more events than counters, the kernel uses time
         * multiplexing. With multiplexing, at the end of the run,
         * the counter is scaled basing on total time enabled vs time running.
         * final_count = raw_count * time_enabled/time_running
         */
        if (e->extra.multi_pmu && !g_ctx.dispatch_cloned_pmus) {
          cgroup_value += event_scaled_value_sum(e, core);

          int num_clones = e->num_clones;
          for (struct event *ce = e->next; ce && num_clones > 0;
               ce = ce->next) {
            if (ce->orig == e) {
              cgroup_value_raw += ce->efd[core].val[0];
              cgroup_time_enabled += ce->efd[core].val[1];
              cgroup_time_running += ce->efd[core].val[2];
            }
          }
        } else {
          cgroup_value += event_scaled_value(e, core);
        }
      }

      if (event_enabled_cgroup > 0)
        /* dispatch per core group values */
        pmu_submit_counters(cgroup->desc, e->event, pmu_name,
                            e->extra.multi_pmu, cgroup_value, cgroup_value_raw,
                            cgroup_time_enabled, cgroup_time_running);
    }

    if (pmu_name)
      sfree(pmu_name);
  }
}

static int pmu_read(user_data_t *ud) {
  if (ud == NULL) {
    ERROR(PMU_PLUGIN ": ud is NULL! %s:%d", __FUNCTION__, __LINE__);
    return -1;
  }
  if (ud->data == NULL) {
    ERROR(PMU_PLUGIN ": ud->data is NULL! %s:%d", __FUNCTION__, __LINE__);
    return -1;
  }
  intel_pmu_entity_t *ent = (intel_pmu_entity_t *)ud->data;
  int ret;
  struct event *e;

  DEBUG(PMU_PLUGIN ": %s:%d", __FUNCTION__, __LINE__);

  /* read all events only for configured cores */
  for (e = ent->event_list->eventlist; e; e = e->next) {
    for (size_t i = 0; i < ent->cgroups_count; i++) {
      core_group_t *cgroup = ent->cores.cgroups + i + ent->first_cgroup;
      for (size_t j = 0; j < cgroup->num_cores; j++) {
        int core = (int)cgroup->cores[j];
        if (e->efd[core].fd < 0) {
          WARNING(PMU_PLUGIN ": Omitting event %s/%d.", e->event, core);
          continue;
        }

        ret = read_event(e, core);
        if (ret != 0) {
          ERROR(PMU_PLUGIN ": Failed to read value of %s/%d event.", e->event,
                core);
          return ret;
        }
      }
    }
  }

  pmu_dispatch_data(ent);

  return 0;
}

static int pmu_add_cloned_pmus(struct eventlist *el, struct event *e) {
  struct perf_event_attr attr = e->attr;
  int ret;

  while ((ret = jevent_next_pmu(&e->extra, &attr)) == 1) {
    /* Allocate memory for event struct that contains array of efd structs
       for all cores */
    struct event *ne =
        calloc(1, sizeof(struct event) + sizeof(struct efd) * el->num_cpus);
    if (ne == NULL) {
      return -ENOMEM;
    }
    for (size_t i = 0; i < el->num_cpus; i++)
      ne->efd[i].fd = -1;

    ne->attr = attr;
    ne->orig = e;
    ne->uncore = e->uncore;
    e->num_clones++;
    jevent_copy_extra(&ne->extra, &e->extra);

    ne->next = NULL;
    if (!el->eventlist)
      el->eventlist = ne;
    if (el->eventlist_last)
      el->eventlist_last->next = ne;
    el->eventlist_last = ne;
    ne->event = strdup(e->event);
  }

  if (ret < 0) {
    ERROR(PMU_PLUGIN ": Cannot find PMU for event %s", e->event);
    return ret;
  }

  return 0;
}

static int pmu_add_hw_events(struct eventlist *el, char **e, size_t count) {

  for (size_t i = 0; i < count; i++) {

    size_t group_events_count = 0;

    char *events = strdup(e[i]);
    if (events == NULL)
      return -1;

    bool group = strrchr(events, ',') != NULL ? true : false;

    char *s, *tmp = NULL;
    for (s = strtok_r(events, ",", &tmp); s; s = strtok_r(NULL, ",", &tmp)) {

      /* Allocate memory for event struct that contains array of efd structs
         for all cores */
      struct event *e =
          calloc(1, sizeof(struct event) + sizeof(struct efd) * el->num_cpus);
      if (e == NULL) {
        free(events);
        return -ENOMEM;
      }
      for (size_t j = 0; j < el->num_cpus; j++)
        e->efd[j].fd = -1;

      if (resolve_event_extra(s, &e->attr, &e->extra) != 0) {
        INFO(PMU_PLUGIN ": Cannot resolve %s", s);
        sfree(e);
        continue;
      }

      e->uncore = jevent_pmu_uncore(e->extra.decoded);

      /* Multiple events parsed in one entry */
      if (group) {
        if (e->extra.multi_pmu) {
          ERROR(PMU_PLUGIN ": Cannot handle multi pmu event %s in a group\n",
                s);
          jevent_free_extra(&e->extra);
          sfree(e);
          sfree(events);
          return -1;
        }
        if (group_events_count == 0)
          /* Mark first added event as group leader */
          e->group_leader = 1;

        e->ingroup = 1;
      }

      e->next = NULL;
      if (!el->eventlist)
        el->eventlist = e;
      if (el->eventlist_last)
        el->eventlist_last->next = e;
      el->eventlist_last = e;
      e->event = strdup(s);

      if (e->extra.multi_pmu && pmu_add_cloned_pmus(el, e) != 0) {
        sfree(events);
        return -1;
      }

      group_events_count++;
    }

    /* Multiple events parsed in one entry */
    if (group && group_events_count > 0) {
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

  free_eventlist(el);
}

static int pmu_setup_events(core_groups_list_t *cores, struct eventlist *el,
                            bool measure_all, int measure_pid) {
  struct event *e, *leader = NULL;
  int ret = -1;
  for (e = el->eventlist; e; e = e->next) {

    for (size_t i = 0; i < cores->num_cgroups; i++) {
      core_group_t *cgroup = cores->cgroups + i;
      for (size_t j = 0; j < cgroup->num_cores; j++) {
        int core = (int)cgroup->cores[j];

        if (e->uncore) {
          bool match = false;
          for (size_t k = 0; k < el->num_sockets; k++)
            if (el->socket_cpus[k] == core) {
              match = true;
              break;
            }
          if (!match)
            continue;
        }

        int res = setup_event(e, core, leader, measure_all, measure_pid);
        if (res < 0 && errno == EMFILE) {
          WARNING(PMU_PLUGIN
                  ": perf event '%s' is not available (cpu=%d). "
                  "Max number of open files reached for current process.",
                  e->event, core);
        } else if (res < 0) {
          WARNING(PMU_PLUGIN ": perf event '%s' is not available (cpu=%d).",
                  e->event, core);
        } else if (e->efd[core].fd < 0) {
          WARNING(PMU_PLUGIN ": max number of events "
                             "per group reached for event '%s' (cpu=%d).",
                  e->event, core);
        } else {
          /* success if at least one event was set */
          ret = 0;
        }
      }
    }

    if (e->group_leader)
      leader = e;
    if (e->end_group)
      leader = NULL;
  }

  return ret;
}

static int pmu_split_cores(intel_pmu_entity_t *ent) {
  if (ent->cores.num_cgroups <= CGROUPS_PER_ENT) {
    ent->cgroups_count = ent->cores.num_cgroups;
    return 0;
  }

  ent->cgroups_count = CGROUPS_PER_ENT;
  intel_pmu_entity_t *prev = ent;
  for (size_t i = CGROUPS_PER_ENT; i < ent->cores.num_cgroups;
       i += CGROUPS_PER_ENT) {
    intel_pmu_entity_t *entc = calloc(1, sizeof(*entc));
    if (entc == NULL) {
      ERROR(PMU_PLUGIN ": pmu_split_cores: Failed to allocate pmu ent.");
      return -ENOMEM;
    }

    /* make a shallow copy and mark it as copied to avoid double free */
    *entc = *prev;
    entc->copied = true;
    prev->next = entc;
    prev = entc;

    entc->first_cgroup = i;
    if (i + CGROUPS_PER_ENT > ent->cores.num_cgroups)
      entc->cgroups_count = ent->cores.num_cgroups - i;
  }

  return 0;
}

static int pmu_count_all_events(void *data, char *name, char *event,
                                char *desc) {
  intel_pmu_entity_t *ent = data;
  ent->hw_events_count++;
  return 0;
}

static int pmu_read_all_events(void *data, char *name, char *event,
                               char *desc) {
  static int event_counter = 0;
  intel_pmu_entity_t *ent = data;

  ent->hw_events[event_counter] = strdup(name);
  if (ent->hw_events[event_counter] == NULL) {
    ERROR(PMU_PLUGIN ": Failed to allocate hw events entry.");
    return -ENOMEM;
  }

  event_counter++;

  /* zeroing event_counter for next cores events */
  if (event_counter == ent->hw_events_count) {
    event_counter = 0;
  }

  return 0;
}

static int pmu_init(void) {
  int ret;

  DEBUG(PMU_PLUGIN ": %s:%d", __FUNCTION__, __LINE__);

  if (g_ctx.entl == NULL) {
    ERROR(PMU_PLUGIN ": No events were setup in configuration.");
    return -EINVAL;
  }

  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL; ent = ent->next) {
    ent->event_list = alloc_eventlist();
    if (ent->event_list == NULL) {
      ERROR(PMU_PLUGIN ": Failed to allocate event list.");
      return -ENOMEM;
    }
  }

  /* parse events names from JSON file */
  if (g_ctx.event_list_fn[0] == '\0')
    ret = read_events(NULL); // Let jevents choose default file
  else
    ret = read_events(g_ctx.event_list_fn);
  if (ret != 0) {
    ERROR(PMU_PLUGIN ": Failed to read event list file '%s'.",
          g_ctx.event_list_fn);
    return ret;
  }

  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL; ent = ent->next) {
    if (ent->cores.num_cgroups == 0) {
      ret = config_cores_default(ent->event_list->num_cpus, &ent->cores);
      if (ret != 0) {
        ERROR(PMU_PLUGIN ": Failed to set default core groups.");
        goto init_error;
      }
    } else {
      ret = pmu_validate_cgroups(ent->cores.cgroups, ent->cores.num_cgroups,
                                 ent->event_list->num_cpus);
      if (ret != 0) {
        ERROR(PMU_PLUGIN ": Invalid core groups configuration.");
        goto init_error;
      }
    }
  }

  /* write all events from provided EventList into hw_events */
  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL; ent = ent->next) {
    if (ent->all_events) {
      ret = walk_events(pmu_count_all_events, ent);
      if (ret != 0) {
        ERROR(PMU_PLUGIN ": Invalid core groups configuration.");
        goto init_error;
      }

      // allocating memory for all events
      ent->hw_events = calloc(ent->hw_events_count, sizeof(*ent->hw_events));
      if (ent->hw_events == NULL) {
        ERROR(PMU_PLUGIN ": Failed to allocate hw events.");
        return -ENOMEM;
      }

      ret = walk_events(pmu_read_all_events, ent);
      if (ret != 0) {
        ERROR(PMU_PLUGIN ": Invalid core groups configuration.");
        goto init_error;
      }
    }
  }

  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL; ent = ent->next) {
    if (ent->hw_events_count == 0) {
      ERROR(PMU_PLUGIN ": No events were setup in `HardwareEvents` option.");
      ret = -EINVAL;
      goto init_error;
    }

    ret = pmu_add_hw_events(ent->event_list, ent->hw_events,
                            ent->hw_events_count);
    if (ret != 0) {
      ERROR(PMU_PLUGIN ": Failed to add hardware events.");
      goto init_error;
    }
  }

#if COLLECT_DEBUG
  pmu_dump_cpu();
  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL; ent = ent->next) {
    pmu_dump_cgroups(ent);
    pmu_dump_events(ent);
  }
#endif

  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL; ent = ent->next) {
    if (ent->event_list->eventlist != NULL) {
      /* measure all processes */
      ret = pmu_setup_events(&ent->cores, ent->event_list, true, -1);
      if (ret != 0) {
        ERROR(PMU_PLUGIN ": Failed to setup perf events for the event list.");
        goto init_error;
      }
    } else {
      WARNING(PMU_PLUGIN
              ": Events list is empty. No events were setup for monitoring.");
      ret = -1;
      goto init_error;
    }
  }

  /* split list of cores for use in separate reading threads */
  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL;) {
    intel_pmu_entity_t *tmp = ent;
    ent = ent->next;
    ret = pmu_split_cores(tmp);
    if (ret != 0)
      goto init_error;
  }

  unsigned int i = 0;
  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL; ent = ent->next) {
    DEBUG(PMU_PLUGIN ": registering read callback [%u], first cgroup: %" PRIsz
                     ", count: %" PRIsz ".",
          i, ent->first_cgroup, ent->cgroups_count);
    char buf[64];
    ent->user_data.data = ent;
    ssnprintf(buf, sizeof(buf), PMU_PLUGIN "[%u]", i++);
    plugin_register_complex_read(NULL, buf, pmu_read, 0, &ent->user_data);
  }

  return 0;

init_error:

  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL;) {
    intel_pmu_entity_t *tmp = ent;
    ent = ent->next;

    if (tmp->copied) {
      sfree(tmp);
      continue;
    }

    pmu_free_events(tmp->event_list);
    tmp->event_list = NULL;
    for (size_t i = 0; i < tmp->hw_events_count; i++) {
      sfree(tmp->hw_events[i]);
    }
    sfree(tmp->hw_events);
    tmp->hw_events_count = 0;

    config_cores_cleanup(&tmp->cores);

    sfree(tmp);
  }
  g_ctx.entl = NULL;

  return ret;
}

static int pmu_shutdown(void) {

  DEBUG(PMU_PLUGIN ": %s:%d", __FUNCTION__, __LINE__);

  for (intel_pmu_entity_t *ent = g_ctx.entl; ent != NULL;) {
    intel_pmu_entity_t *tmp = ent;
    ent = ent->next;

    if (tmp->copied) {
      sfree(tmp);
      continue;
    }

    pmu_free_events(tmp->event_list);
    tmp->event_list = NULL;
    for (size_t i = 0; i < tmp->hw_events_count; i++) {
      sfree(tmp->hw_events[i]);
    }
    sfree(tmp->hw_events);
    tmp->hw_events_count = 0;

    config_cores_cleanup(&tmp->cores);

    sfree(tmp);
  }
  g_ctx.entl = NULL;

  return 0;
}

void module_register(void) {
  plugin_register_complex_config(PMU_PLUGIN, pmu_config);
  plugin_register_init(PMU_PLUGIN, pmu_init);
  plugin_register_shutdown(PMU_PLUGIN, pmu_shutdown);
}
