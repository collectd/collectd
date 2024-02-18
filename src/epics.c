/**
 * collectd - src/epics.c
 * Copyright (C) 2022       Matwey V. Kornilov
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

  union {
    value_list_t value;
    char *label;
  };

  bool is_active;
  bool is_label;
};

static struct {
  struct pv *pvs;
  int pvs_num;

  pthread_mutex_t lock;
  pthread_t thread_id;

  bool thread_loop;
} epics_plugin = {.lock = PTHREAD_MUTEX_INITIALIZER};

static void free_pvs() {
  for (int i = 0; i < epics_plugin.pvs_num; ++i) {
    struct pv *p = &epics_plugin.pvs[i];

    if (p->is_label) {
      free(p->label);
    } else if (p->value.values) {
      free(p->value.values);
    }
    free(p->name);
  }

  free(epics_plugin.pvs);
  epics_plugin.pvs = NULL;
  epics_plugin.pvs_num = 0;
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

static int deduce_channel_type(int ch_type, int ds_type) {
  if (ds_type == DS_TYPE_COUNTER &&
      (ch_type == DBR_SHORT || ch_type == DBR_LONG)) {
    return DBR_LONG;
  } else if (ds_type == DS_TYPE_GAUGE &&
             (ch_type == DBR_FLOAT || ch_type == DBR_DOUBLE ||
              ch_type == DBR_ENUM)) {
    return DBR_DOUBLE;
  } else if (ds_type == DS_TYPE_DERIVE &&
             (ch_type == DBR_SHORT || ch_type == DBR_LONG)) {
    return DBR_LONG;
  } else if (ds_type == DS_TYPE_ABSOLUTE &&
             (ch_type == DBR_SHORT || ch_type == DBR_LONG)) {
    return DBR_LONG;
  }

  return -1;
}

static void handle_var_event(struct pv *p, evargs args) {
  const data_set_t *ds = plugin_get_ds(p->value.type);
  const size_t values_len = p->value.values_len;
  value_t *values = p->value.values;

  if (ds == NULL) {
    ERROR("epics plugin: Unknown type \"%s\" for channel \"%s\". See "
          "types.db(5) for details.",
          p->value.type, p->name);

    return;
  }

  const int ds_type = ds->ds[0].type;

  if (args.count != values_len) {
    ERROR(
        "epics plugin: Unexpected channel element count %lu for channel \"%s\"",
        args.count, p->name);
    return;
  }

  pthread_mutex_lock(&epics_plugin.lock);

  if (ds_type == DS_TYPE_COUNTER && args.type == DBR_LONG) {
    const long *value = (const long *)args.dbr;

    for (size_t i = 0; i < values_len; ++i) {
      values[i].counter = value[i];
    }
  } else if (ds_type == DS_TYPE_DERIVE && args.type == DBR_LONG) {
    const long *value = (const long *)args.dbr;

    for (size_t i = 0; i < values_len; ++i) {
      values[i].derive = value[i];
    }
  } else if (ds_type == DS_TYPE_ABSOLUTE && args.type == DBR_LONG) {
    const long *value = (const long *)args.dbr;

    for (size_t i = 0; i < values_len; ++i) {
      values[i].absolute = value[i];
    }
  } else if (ds_type == DS_TYPE_GAUGE && args.type == DBR_DOUBLE) {
    const double *value = (const double *)args.dbr;

    for (size_t i = 0; i < values_len; ++i) {
      values[i].gauge = value[i];
    }
  } else {
    WARNING("epics plugin: Unexpected data type \"%s\" for channel type \"%s\"",
            DS_TYPE_TO_STRING(ds_type), dbf_type_to_text(args.type));
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

  free(p->label);

  p->label = strdup(value);
  if (p->label == NULL) {
    ERROR("epics plugin: Cannot allocate memory for \"%s\" value", p->name);
    // fall-through
  }

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

    p->is_active = 1;

    return;
  }

  if (p->is_label) {
    p->ch_type = DBR_STRING;
  } else {
    const data_set_t *ds = plugin_get_ds(p->value.type);

    if (ds == NULL) {
      ERROR("epics plugin: Unknown type \"%s\" for channel \"%s\". See "
            "types.db(5) for details.",
            p->value.type, p->name);

      return;
    }

    p->ch_type = deduce_channel_type(ca_field_type(p->id), ds->ds[0].type);
    if (p->ch_type < 0) {
      ERROR("epics plugin: Variable type \"%s\" doesn't match channel type "
            "\"%s\" for channel \"%s\"",
            DS_TYPE_TO_STRING(ds->ds[0].type), dbf_type_to_text(p->ch_type),
            p->name);

      return;
    }

    if (ca_element_count(p->id) != ds->ds_num) {
      ERROR("epics plugin: Variable element number %lu doesn't match channel "
            "element count %lu for channel \"%s\"",
            ds->ds_num, ca_element_count(p->id), p->name);

      return;
    }

    p->value.values_len = ds->ds_num;
    p->value.values = calloc(p->value.values_len, sizeof(value_t));
    if (p->value.values == NULL) {
      ERROR("epics plugin: Cannot allocate memory for %lu values or channel "
            "\"%s\"",
            ds->ds_num, p->name);

      return;
    }
  }

  int ret =
      ca_create_subscription(p->ch_type, ca_element_count(p->id), p->id,
                             DBE_VALUE | DBE_ALARM, event_handler, p, &p->eid);
  if (ret != ECA_NORMAL) {
    ERROR("epics plugin: CA error %s occurred while trying to create "
          "subscription for channel \"%s\"",
          ca_message(ret), p->name);

    return;
  }

  p->is_active = 1;
}

static void handle_conn_down(struct pv *p) {
  WARNING("epics plugin: Channel \"%s\" disconnected", p->name);

  p->is_active = 0;
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

  for (int i = 0; i < epics_plugin.pvs_num; ++i) {
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
  for (int i = 0; i < epics_plugin.pvs_num; ++i) {
    struct pv *p = &epics_plugin.pvs[i];

    if (p->eid) {
      ca_clear_subscription(p->eid);
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

static int epics_config_variable(oconfig_item_t *ci, struct pv *p) {

  if (cf_util_get_string(ci, &p->name) != 0 || p->name == NULL) {
    ERROR("epics plugin: Wrong variable configuration");

    return -1;
  }

  for (oconfig_item_t *c = ci->children; c != ci->children + ci->children_num;
       ++c) {
    if (strcasecmp(c->key, "Type") == 0) {
      if (cf_util_get_string_buffer(c, p->value.type, sizeof(p->value.type)) !=
          0) {

        return -1;
      }

      sstrncpy(p->value.type_instance, p->name, sizeof(p->value.type_instance));
    } else {
      ERROR(
          "epics plugin: Unknown configuration key \"%s\" for variable \"%s\"",
          c->key, p->name);

      return -1;
    }
  }

  sstrncpy(p->value.plugin, "epics", sizeof(p->value.plugin));

  return 0;
}

static int epics_config_label(oconfig_item_t *ci, struct pv *p) {
  if (cf_util_get_string(ci, &p->name) != 0 || p->name == NULL) {
    ERROR("epics plugin: Wrong label configuration");

    return -1;
  }

  p->is_label = 1;

  return 0;
}

static int epics_config(oconfig_item_t *ci) {
  if (ci->children_num == 0) {
    ERROR("epics plugin: No variables are specified");

    return -1;
  }

  struct pv *pvs =
      realloc(epics_plugin.pvs,
              sizeof(struct pv) * (epics_plugin.pvs_num + ci->children_num));
  if (pvs == NULL) {
    ERROR("epics plugin: Cannot allocate memory for PV list");

    return -1;
  }

  epics_plugin.pvs = pvs;
  memset(epics_plugin.pvs + epics_plugin.pvs_num, 0,
         sizeof(struct pv) * ci->children_num);
  for (oconfig_item_t *c = ci->children; c != ci->children + ci->children_num;
       ++c, ++epics_plugin.pvs_num) {
    struct pv *p = &epics_plugin.pvs[epics_plugin.pvs_num];

    if (strcasecmp(c->key, "Variable") == 0) {
      if (epics_config_variable(c, p) != 0)
        goto error;
    } else if (strcasecmp(c->key, "Label") == 0) {
      if (epics_config_label(c, p) != 0)
        goto error;
    } else {
      ERROR("epics plugin: Unknown configuration key \"%s\"", ci->key);
      goto error;
    }
  }

  return 0;

error:
  free_pvs();

  return -1;
}

static int epics_init(void) { return start_thread(); }

static int epics_shutdown(void) {
  stop_thread();
  free_pvs();

  return 0;
}

static int epics_read(void) {
  int ret = 0;

  meta_data_t *md = meta_data_create();
  if (md == NULL) {
    ERROR("epics plugin: Cannot allocate memory for meta data");

    return -1;
  }

  pthread_mutex_lock(&epics_plugin.lock);

  cdtime_t time = cdtime();

  for (int i = 0; i < epics_plugin.pvs_num; ++i) {
    struct pv *p = &epics_plugin.pvs[i];

    if (!p->is_active || !p->is_label) {
      continue;
    }

    ret = meta_data_add_string(md, p->name, p->label);
    if (ret != 0) {
      ERROR("epics plugin: Cannot add value for meta \"%s\"", p->name);

      goto error;
    }
  }

  for (int i = 0; i < epics_plugin.pvs_num; ++i) {
    struct pv *p = &epics_plugin.pvs[i];

    if (!p->is_active || p->is_label) {
      continue;
    }

    p->value.time = time;
    p->value.meta = md;

    ret = plugin_dispatch_values(&p->value);
    if (ret != 0) {
      ERROR("epics plugin: Cannot dispatch values for \"%s\"", p->name);

      goto error;
    }
  }

  ret = 0;
error:
  pthread_mutex_unlock(&epics_plugin.lock);
  meta_data_destroy(md);

  return ret;
}

void module_register(void) {
  plugin_register_complex_config("epics", epics_config);
  plugin_register_read("epics", epics_read);
  plugin_register_init("epics", epics_init);
  plugin_register_shutdown("epics", epics_shutdown);
}
