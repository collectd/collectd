/**
 * collectd - src/epics.c
 * Copyright (C) 2024       Matwey V. Kornilov
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Matwey V. Kornilov <matwey.kornilov at gmail.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#include <cadef.h>

struct pv {
  char *name;

  chtype ch_type;
  chid id;
  evid eid;

  size_t family_index;
  union {
    struct {
      metric_list_t metric_list;
    } var;
    struct {
      char *label;
    } label;
  };

  bool is_active;
  bool is_label;
};

static struct {
  struct pv *pvs;
  size_t pvs_num;

  metric_family_t *families;
  size_t families_num;

  pthread_mutex_t lock;
  pthread_t thread_id;

  bool thread_loop;
} epics_plugin = {.lock = PTHREAD_MUTEX_INITIALIZER};

static void free_pvs(void) {
  for (size_t i = 0; i < epics_plugin.pvs_num; ++i) {
    struct pv *p = &epics_plugin.pvs[i];

    if (p->is_label) {
      free(p->label.label);
    } else {
      for (size_t j = 0; j < p->var.metric_list.num; ++j) {
        metric_reset(&p->var.metric_list.ptr[j]);
      }
      free(p->var.metric_list.ptr);
      p->var.metric_list.num = 0;
    }
    free(p->name);
  }

  free(epics_plugin.pvs);
  epics_plugin.pvs = NULL;
  epics_plugin.pvs_num = 0;
}

static void free_families(void) {
  for (size_t i = 0; i < epics_plugin.families_num; ++i) {
    metric_family_t *fam = &epics_plugin.families[i];

    free(fam->name);
    free(fam->help);
    free(fam->unit);
    metric_family_metric_reset(fam);
  }

  free(epics_plugin.families);
  epics_plugin.families = NULL;
  epics_plugin.families_num = 0;
}

static int printf_handler(const char *pformat, va_list args) {
#if COLLECT_DEBUG
  char msg[1024] = ""; // Size inherits from plugin_log()

  int status = vsnprintf(msg, sizeof(msg), pformat, args);

  if (status < 0) {
    return status;
  }

  msg[strcspn(msg, "\r\n")] = '\0';
  plugin_log(LOG_DEBUG, "%s", msg);

  return status;
#else
  return 0;
#endif
}

static int deduce_channel_type(metric_type_t metric_type) {
  if (metric_type == METRIC_TYPE_COUNTER) {
    return DBF_LONG;
  } else if (metric_type == METRIC_TYPE_GAUGE) {
    return DBR_DOUBLE;
  }

  return -1;
}

static void handle_var_event(struct pv *p, evargs args) {
  const metric_type_t metric_type = epics_plugin.families[p->family_index].type;
  const metric_list_t *metric_list = &p->var.metric_list;

  if (args.count != metric_list->num) {
    ERROR(
        "epics plugin: Unexpected channel element count %lu for channel \"%s\"",
        args.count, p->name);
    return;
  }

  pthread_mutex_lock(&epics_plugin.lock);

  if (metric_type == METRIC_TYPE_COUNTER && args.type == DBR_LONG) {
    const dbr_long_t *value = (const dbr_long_t *)args.dbr;

    for (size_t i = 0; i < metric_list->num; ++i) {
      metric_list->ptr[i].value.counter = value[i];
    }
  } else if (metric_type == DS_TYPE_GAUGE && args.type == DBR_DOUBLE) {
    const double *value = (const double *)args.dbr;

    for (size_t i = 0; i < metric_list->num; ++i) {
      metric_list->ptr[i].value.gauge = value[i];
    }
  } else {
    ERROR("epics plugin: Unexpected data type for channel type \"%s\"",
          dbr_type_to_text(args.type));
  }

  pthread_mutex_unlock(&epics_plugin.lock);
}

static void handle_label_event(struct pv *p, evargs args) {
  const char *value = (const char *)args.dbr;

  if (args.count != 1) {
    ERROR(
        "epics plugin: Unexpected channel element count %lu for channel \"%s\"",
        args.count, p->name);
    return;
  }

  pthread_mutex_lock(&epics_plugin.lock);

  free(p->label.label);

  p->label.label = strdup(value);
  if (p->label.label == NULL) {
    ERROR("epics plugin: Cannot allocate memory for \"%s\" value", p->name);

    goto error;
  }

error:
  pthread_mutex_unlock(&epics_plugin.lock);
}

static void event_handler(evargs args) {
  struct pv *p = (struct pv *)args.usr;

  if (args.status != ECA_NORMAL) {
    ERROR("epics plugin: Error %s at channel \"%s\"", ca_message(args.status),
          p->name);

    return;
  }

  if (p->is_label) {
    handle_label_event(p, args);
  } else {
    handle_var_event(p, args);
  }
}

static void handle_conn_up(struct pv *p) {
  if (p->eid) {
    INFO("epics plugin: Channel \"%s\" reconnected", p->name);

    p->is_active = true;

    return;
  }

  if (p->is_label) {
    p->ch_type = DBR_STRING;
  } else {
    p->ch_type =
        deduce_channel_type(epics_plugin.families[p->family_index].type);
    p->var.metric_list.num = ca_element_count(p->id);
    p->var.metric_list.ptr = calloc(p->var.metric_list.num, sizeof(metric_t));
    if (p->var.metric_list.ptr == NULL) {
      ERROR("epics plugin: Cannot allocate memory for %lu values or channel "
            "\"%s\"",
            p->var.metric_list.num, p->name);

      return;
    }
  }

  int ret =
      ca_create_subscription(p->ch_type, p->var.metric_list.num, p->id,
                             DBE_VALUE | DBE_ALARM, event_handler, p, &p->eid);
  if (ret != ECA_NORMAL) {
    ERROR("epics plugin: CA error %s occurred while trying to create "
          "subscription for channel \"%s\"",
          ca_message(ret), p->name);

    return;
  }

  p->is_active = true;
}

static void handle_conn_down(struct pv *p) {
  WARNING("epics plugin: Channel \"%s\" disconnected", p->name);

  p->is_active = false;
}

static void connection_handler(struct connection_handler_args args) {
  struct pv *p = (struct pv *)ca_puser(args.chid);

  switch (args.op) {
  case CA_OP_CONN_UP:
    handle_conn_up(p);
    break;
  case CA_OP_CONN_DOWN:
    handle_conn_down(p);
    break;
  }
}

static void *epics_thread(void *args) {
  long ret = ca_context_create(ca_disable_preemptive_callback);
  if (ret != ECA_NORMAL) {
    // FIXME: report error back to start_thread()
    ERROR("epics plugin: CA error %s occurred while trying to start channel "
          "access",
          ca_message(ret));
    return (void *)1;
  }

  ca_replace_printf_handler(&printf_handler);

  for (size_t i = 0; i < epics_plugin.pvs_num; ++i) {
    struct pv *p = &epics_plugin.pvs[i];

    ret = ca_create_channel(p->name, &connection_handler, p, 0, &p->id);
    if (ret != ECA_NORMAL) {
      ERROR("epics plugin: CA error %s occurred while trying to create channel "
            "\"%s\"",
            ca_message(ret), p->name);
      ret = 1;
      goto error;
    }
  }

  const double timeout = 2.0;
  while (epics_plugin.thread_loop != 0) {
    ca_pend_event(timeout);
  }

error:
  for (size_t i = 0; i < epics_plugin.pvs_num; ++i) {
    struct pv *p = &epics_plugin.pvs[i];

    if (p->eid) {
      ca_clear_subscription(p->eid);
      p->eid = 0;
    }
    ca_clear_channel(p->id);
  }

  ca_context_destroy();

  return (void *)ret;
}

static int start_thread(void) {
  pthread_mutex_lock(&epics_plugin.lock);

  int ret = 0;
  if (epics_plugin.thread_loop != 0) {
    goto epics_unlock;
  }

  epics_plugin.thread_loop = 1;
  ret = plugin_thread_create(&epics_plugin.thread_id, epics_thread, (void *)0,
                             "epics");
  if (ret != 0) {
    epics_plugin.thread_loop = 0;
    ERROR("epics plugin: Starting thread failed: %d", ret);

    goto epics_unlock;
  }

  // FIXME: wait untill ca_context_create success

epics_unlock:
  pthread_mutex_unlock(&epics_plugin.lock);

  return ret;
}

static int stop_thread(void) {
  pthread_mutex_lock(&epics_plugin.lock);

  epics_plugin.thread_loop = 0;

  pthread_mutex_unlock(&epics_plugin.lock);

  return pthread_join(epics_plugin.thread_id, NULL);
}

static bool epics_config_metric(oconfig_item_t *ci, struct pv *p,
                                size_t family_index) {
  if (cf_util_get_string(ci, &p->name) != 0 || p->name == NULL) {
    ERROR("epics plugin: Wrong metric configuration");

    return false;
  }

  p->family_index = family_index;
  p->is_label = false;

  return true;
}

static bool epics_config_label(oconfig_item_t *ci, struct pv *p,
                               size_t family_index) {
  if (cf_util_get_string(ci, &p->name) != 0 || p->name == NULL) {
    ERROR("epics plugin: Wrong label configuration");

    return false;
  }

  p->family_index = family_index;
  p->is_label = true;

  return true;
}

static bool epics_config_metric_family(oconfig_item_t *ci,
                                       size_t family_index) {
  metric_family_t *fam = &epics_plugin.families[family_index];

  if (cf_util_get_string(ci, &fam->name) != 0 || fam->name == NULL) {
    ERROR("epics plugin: Wrong metric family configuration");

    return false;
  }

  struct pv *pvs =
      realloc(epics_plugin.pvs,
              sizeof(struct pv) * (epics_plugin.pvs_num + ci->children_num));
  if (pvs == NULL) {
    ERROR("epics plugin: Cannot allocate memory for PV list");

    return false;
  }

  epics_plugin.pvs = pvs;
  memset(epics_plugin.pvs + epics_plugin.pvs_num, 0,
         sizeof(struct pv) * ci->children_num);
  for (oconfig_item_t *c = ci->children; c != ci->children + ci->children_num;
       ++c) {
    if (strcasecmp(c->key, "Type") == 0) {
      char *type = NULL;
      if (cf_util_get_string(c, &type) != 0 || type == NULL) {
        ERROR("epics plugin: Wrong metric family type");

        return false;
      }

      if (strcasecmp(type, "Gauge") == 0) {
        fam->type = METRIC_TYPE_GAUGE;
      } else if (strcasecmp(type, "Counter") == 0) {
        fam->type = METRIC_TYPE_COUNTER;
      } else {
        ERROR("epics plugin: Unknown metric family type \"%s\" for metric "
              "family \"%s\"",
              type, fam->name);
        free(type);

        return false;
      }

      free(type);
    } else if (strcasecmp(c->key, "Unit") == 0) {
      if (cf_util_get_string(c, &fam->unit) != 0 || fam->unit == NULL) {
        ERROR("epics plugin: Wrong metric family unit");

        return false;
      }
    } else if (strcasecmp(c->key, "Metric") == 0) {
      struct pv *p = &epics_plugin.pvs[epics_plugin.pvs_num];

      if (!epics_config_metric(c, p, family_index)) {
        return false;
      }

      ++epics_plugin.pvs_num;
    } else if (strcasecmp(c->key, "Label") == 0) {
      struct pv *p = &epics_plugin.pvs[epics_plugin.pvs_num];

      if (!epics_config_label(c, p, family_index)) {
        return false;
      }

      ++epics_plugin.pvs_num;
    } else {
      ERROR("epics plugin: Unknown configuration key \"%s\" for metric family "
            "\"%s\"",
            c->key, fam->name);

      return false;
    }
  }

  return true;
}

static int epics_config(oconfig_item_t *ci) {
  if (ci->children_num == 0) {
    ERROR("epics plugin: No metric families are specified");

    return -1;
  }

  metric_family_t *fams = realloc(
      epics_plugin.families,
      sizeof(metric_family_t) * (epics_plugin.families_num + ci->children_num));
  if (fams == NULL) {
    ERROR("epics plugin: Cannot allocate memory for metric family list");

    return -1;
  }

  epics_plugin.families = fams;
  memset(epics_plugin.families + epics_plugin.families_num, 0,
         sizeof(metric_family_t) * ci->children_num);
  for (oconfig_item_t *c = ci->children; c != ci->children + ci->children_num;
       ++c, ++epics_plugin.families_num) {
    if (strcasecmp(c->key, "Family") == 0) {
      if (!epics_config_metric_family(c, epics_plugin.families_num)) {
        goto error;
      }
    } else {
      ERROR("epics plugin: Unknown configuration key \"%s\" for plugin",
            c->key);

      goto error;
    }
  }

  return 0;

error:
  free_pvs();
  free_families();

  return -1;
}

static int epics_init(void) { return start_thread(); }

static int epics_shutdown(void) {
  stop_thread();
  free_pvs();
  free_families();

  return 0;
}

static int epics_read(void) {
  int ret = 0;
  label_set_t label_set;

  pthread_mutex_lock(&epics_plugin.lock);

  size_t offset = 0;
  for (size_t i = 0; i < epics_plugin.families_num; ++i) {
    metric_family_t *fam = &epics_plugin.families[i];

    label_set_reset(&label_set);
    for (struct pv *p = &epics_plugin.pvs[offset];
         p != epics_plugin.pvs + epics_plugin.pvs_num; ++p) {
      if (!p->is_active || !p->is_label) {
        continue;
      } else if (i != p->family_index) {
        break;
      }

      ret = label_set_update(&label_set, p->name, p->label.label);
      if (ret != 0) {
        ERROR("epics plugin: Cannot update label \"%s\" for metric family "
              "\"%s\": %s",
              p->name, fam->name, STRERROR(ret));

        goto error;
      }
    }

    for (struct pv *p = &epics_plugin.pvs[offset];
         p != epics_plugin.pvs + epics_plugin.pvs_num; ++p, ++offset) {

      if (!p->is_active || p->is_label) {
        continue;
      } else if (i != p->family_index) {
        break;
      }

      for (size_t j = 0; j < p->var.metric_list.num; ++j) {
        metric_t *metric = &p->var.metric_list.ptr[j];

        label_set_reset(&metric->label);
        ret = label_set_clone(&metric->label, label_set);
        if (ret != 0) {
          ERROR("epics plugin: Cannot clone label set for variable \"%s\" of "
                "metric family \"%s\": %s",
                p->name, fam->name, STRERROR(ret));

          goto error;
        }
      }

      ret = metric_family_append_list(fam, p->var.metric_list);
      if (ret != 0) {
        ERROR("epics plugin: Cannot append metric list for variable \"%s\" of "
              "metric family \"%s\": %s",
              p->name, fam->name, STRERROR(ret));

        goto error;
      }
    }

    ret = plugin_dispatch_metric_family(fam);
    if (ret != 0) {
      ERROR("epics plugin: Cannot dispatch metric family \"%s\": %s", fam->name,
            STRERROR(ret));
    }
    metric_family_metric_reset(fam);
  }

error:
  pthread_mutex_unlock(&epics_plugin.lock);
  label_set_reset(&label_set);

  return ret;
}

void module_register(void) {
  plugin_register_complex_config("epics", epics_config);
  plugin_register_read("epics", epics_read);
  plugin_register_init("epics", epics_init);
  plugin_register_shutdown("epics", epics_shutdown);
}
