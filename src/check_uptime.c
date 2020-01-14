/**
 * collectd - src/check_uptime.c
 * Copyright (C) 2007-2019  Florian Forster
 * Copyright (C) 2019  Pavel V. Rochnyack
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
 * Author:
 *   Florian octo Forster <octo at collectd.org>
 *   Pavel Rochnyak <pavel2000 ngs.ru>
 **/

#include "collectd.h"
#include "plugin.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils_cache.h"

/* Types are registered only in `config` phase, so access is not protected by
 * locks */
c_avl_tree_t *types_tree = NULL;

static int format_uptime(unsigned long uptime_sec, char *buf, size_t bufsize) {

  unsigned int uptime_days = uptime_sec / 24 / 3600;
  uptime_sec -= uptime_days * 24 * 3600;
  unsigned int uptime_hours = uptime_sec / 3600;
  uptime_sec -= uptime_hours * 3600;
  unsigned int uptime_mins = uptime_sec / 60;
  uptime_sec -= uptime_mins * 60;

  int ret = 0;
  if (uptime_days) {
    ret += snprintf(buf + ret, bufsize - ret, " %u day(s)", uptime_days);
  }
  if (uptime_days || uptime_hours) {
    ret += snprintf(buf + ret, bufsize - ret, " %u hour(s)", uptime_hours);
  }
  if (uptime_days || uptime_hours || uptime_mins) {
    ret += snprintf(buf + ret, bufsize - ret, " %u min", uptime_mins);
  }
  ret += snprintf(buf + ret, bufsize - ret, " %lu sec.", uptime_sec);
  return ret;
}

static int cu_notify(enum cache_event_type_e event_type, const value_list_t *vl,
                     gauge_t old_uptime, gauge_t new_uptime) {
  notification_t n;
  NOTIFICATION_INIT_VL(&n, vl);

  int status;
  char *buf = n.message;
  size_t bufsize = sizeof(n.message);

  n.time = vl->time;

  const char *service = "Service";
  if (strcmp(vl->plugin, "uptime") == 0)
    service = "Host";

  switch (event_type) {
  case CE_VALUE_NEW:
    n.severity = NOTIF_OKAY;
    status = snprintf(buf, bufsize, "%s is running.", service);
    buf += status;
    bufsize -= status;
    break;
  case CE_VALUE_UPDATE:
    n.severity = NOTIF_WARNING;
    status = snprintf(buf, bufsize, "%s just restarted.", service);
    buf += status;
    bufsize -= status;
    break;
  case CE_VALUE_EXPIRED:
    n.severity = NOTIF_FAILURE;
    status = snprintf(buf, bufsize, "%s is unreachable.", service);
    buf += status;
    bufsize -= status;
    break;
  }

  if (!isnan(old_uptime)) {
    status = snprintf(buf, bufsize, " Uptime was:");
    buf += status;
    bufsize -= status;

    status = format_uptime(old_uptime, buf, bufsize);
    buf += status;
    bufsize -= status;

    plugin_notification_meta_add_double(&n, "LastValue", old_uptime);
  }

  if (!isnan(new_uptime)) {
    status = snprintf(buf, bufsize, " Uptime now:");
    buf += status;
    bufsize -= status;

    status = format_uptime(new_uptime, buf, bufsize);
    buf += status;
    bufsize -= status;

    plugin_notification_meta_add_double(&n, "CurrentValue", new_uptime);
  }

  plugin_dispatch_notification(&n);

  plugin_notification_meta_free(n.meta);
  return 0;
}

static int cu_cache_event(cache_event_t *event,
                          __attribute__((unused)) user_data_t *ud) {
  gauge_t values_history[2];

  /* For CE_VALUE_EXPIRED */
  int ret;
  value_t *values;
  size_t values_num;
  gauge_t old_uptime = NAN;

  switch (event->type) {
  case CE_VALUE_NEW:
    DEBUG("check_uptime: CE_VALUE_NEW, %s", event->value_list_name);
    if (c_avl_get(types_tree, event->value_list->type, NULL) == 0) {
      event->ret = 1;
      assert(event->value_list->values_len > 0);
      cu_notify(CE_VALUE_NEW, event->value_list, NAN /* old */,
                event->value_list->values[0].gauge /* new */);
    }
    break;
  case CE_VALUE_UPDATE:
    DEBUG("check_uptime: CE_VALUE_UPDATE, %s", event->value_list_name);
    if (uc_get_history_by_name(event->value_list_name, values_history, 2, 1)) {
      ERROR("check_uptime plugin: Failed to get value history for %s.",
            event->value_list_name);
    } else {
      if (!isnan(values_history[0]) && !isnan(values_history[1]) &&
          values_history[0] < values_history[1]) {
        cu_notify(CE_VALUE_UPDATE, event->value_list,
                  values_history[1] /* old */, values_history[0] /* new */);
      }
    }
    break;
  case CE_VALUE_EXPIRED:
    DEBUG("check_uptime: CE_VALUE_EXPIRED, %s", event->value_list_name);
    ret = uc_get_value_by_name(event->value_list_name, &values, &values_num);
    if (ret == 0) {
      old_uptime = values[0].gauge;
      sfree(values);
    }

    cu_notify(CE_VALUE_EXPIRED, event->value_list, old_uptime, NAN /* new */);
    break;
  }
  return 0;
}

static int cu_config(oconfig_item_t *ci) {
  if (types_tree == NULL) {
    types_tree = c_avl_create((int (*)(const void *, const void *))strcmp);
    if (types_tree == NULL) {
      ERROR("check_uptime plugin: c_avl_create failed.");
      return -1;
    }
  }

  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Type", child->key) == 0) {
      if ((child->values_num != 1) ||
          (child->values[0].type != OCONFIG_TYPE_STRING)) {
        WARNING("check_uptime plugin: The `Type' option needs exactly one "
                "string argument.");
        return -1;
      }
      char *type = child->values[0].value.string;

      if (c_avl_get(types_tree, type, NULL) == 0) {
        ERROR("check_uptime plugin: Type `%s' already added.", type);
        return -1;
      }

      char *type_copy = strdup(type);
      if (type_copy == NULL) {
        ERROR("check_uptime plugin: strdup failed.");
        return -1;
      }

      int status = c_avl_insert(types_tree, type_copy, NULL);
      if (status != 0) {
        ERROR("check_uptime plugin: c_avl_insert failed.");
        sfree(type_copy);
        return -1;
      }
    } else
      WARNING("check_uptime plugin: Ignore unknown config option `%s'.",
              child->key);
  }

  return 0;
}

static int cu_init(void) {
  if (types_tree == NULL) {
    types_tree = c_avl_create((int (*)(const void *, const void *))strcmp);
    if (types_tree == NULL) {
      ERROR("check_uptime plugin: c_avl_create failed.");
      return -1;
    }
    /* Default configuration */
    char *type = strdup("uptime");
    if (type == NULL) {
      ERROR("check_uptime plugin: strdup failed.");
      return -1;
    }
    int status = c_avl_insert(types_tree, type, NULL);
    if (status != 0) {
      ERROR("check_uptime plugin: c_avl_insert failed.");
      sfree(type);
      return -1;
    }
  }

  int ret = 0;
  char *type;
  void *val;
  c_avl_iterator_t *iter = c_avl_get_iterator(types_tree);
  while (c_avl_iterator_next(iter, (void *)&type, (void *)&val) == 0) {
    data_set_t const *ds = plugin_get_ds(type);
    if (ds == NULL) {
      ERROR("check_uptime plugin: Failed to look up type \"%s\".", type);
      ret = -1;
      continue;
    }
    if (ds->ds_num != 1) {
      ERROR("check_uptime plugin: The type \"%s\" has %" PRIsz " data sources. "
            "Only types with a single GAUGE data source are supported.",
            ds->type, ds->ds_num);
      ret = -1;
      continue;
    }
    if (ds->ds[0].type != DS_TYPE_GAUGE) {
      ERROR("check_uptime plugin: The type \"%s\" has wrong data source type. "
            "Only types with a single GAUGE data source are supported.",
            ds->type);
      ret = -1;
      continue;
    }
  }
  c_avl_iterator_destroy(iter);

  if (ret == 0)
    plugin_register_cache_event("check_uptime", cu_cache_event, NULL);

  return ret;
}

void module_register(void) {
  plugin_register_complex_config("check_uptime", cu_config);
  plugin_register_init("check_uptime", cu_init);
}
