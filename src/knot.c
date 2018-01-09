/**
 * collectd - src/knot.c
 * Copyright (C) 2018       Julian Brost
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Julian Brost <julian at 0x4a42.net>
 **/

#define _DEFAULT_SOURCE
#define _BSD_SOURCE

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#include <libknot/libknot.h>

static const char *config_keys[] = {"Socket", "PerZoneStats", "Zone",
                                    "IgnoreSelected"};
static const int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
static const char *socket_path_default = "/run/knot/knot.sock";

static char *socket_path = NULL;
static _Bool per_zone_stats = 0;
static ignorelist_t *ignorelist = NULL;

static int knot_init_ignorelist(void) {
  if (ignorelist == NULL) {
    ignorelist = ignorelist_create(/* invert = */ 1);
    if (ignorelist == NULL) {
      ERROR("knot: creating ignore list failed");
      return 1;
    }
  }
  return 0;
}

static char *knot_normalize_zone_alloc(const char *z) {
  if (z == NULL) {
    return NULL;
  }
  char *zone = strdup(z);
  if (zone == NULL) {
    ERROR("knot: strdup failed to allocate memory");
    return NULL;
  }
  // trim trailing '.' except if it's the root zone
  size_t len = strlen(zone);
  if (len > 1 && zone[len - 1] == '.') {
    zone[len - 1] = '\0';
  }
  return zone;
}

static int knot_config(const char *key, const char *value) {
  if (knot_init_ignorelist() != 0) {
    return 1;
  }

  if (strcasecmp(key, "Socket") == 0) {
    socket_path = strdup(value);
    if (socket_path == NULL) {
      ERROR("knot: strdup failed to allocate memory");
      return 1;
    }
    return 0;
  } else if (strcasecmp(key, "PerZoneStats") == 0) {
    per_zone_stats = IS_TRUE(value);
    return 0;
  } else if (strcasecmp(key, "Zone") == 0) {
    char *zone = knot_normalize_zone_alloc(value);
    if (zone == NULL) {
      return 1;
    }
    ignorelist_add(ignorelist, zone);
    free(zone);
    return 0;
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    ignorelist_set_invert(ignorelist, !IS_TRUE(value));
    return 0;
  } else {
    return -1;
  }
}

static int knot_init(void) {
  if (knot_init_ignorelist() != 0) {
    return 1;
  }

  return 0;
}

static const struct {
  // matches
  const char *section;
  const char *item;

  // results
  int ds_type;
  const char *type;
  const char *type_instance;
} stats_map[] = {
    // section,   item,   ds_type,   type,   type_instance
    {"server", "zone-count", DS_TYPE_GAUGE, "count", "zones"},
    {"mod-stats", "request-protocol", DS_TYPE_DERIVE, "dns_request", NULL},
    {"mod-stats", "server-operation", DS_TYPE_DERIVE, "operations", NULL},
    {"mod-stats", "request-bytes", DS_TYPE_DERIVE, "if_rx_octets", NULL},
    {"mod-stats", "response-bytes", DS_TYPE_DERIVE, "if_tx_octets", NULL},
    {"mod-stats", "response-code", DS_TYPE_DERIVE, "dns_rcode", NULL},
    {"mod-stats", "query-type", DS_TYPE_DERIVE, "dns_qtype", NULL},
    // TODO: edns-presence flag-presence reply-nodata query-size reply-size?
};
static const int stats_map_num = STATIC_ARRAY_SIZE(stats_map);

static int knot_handle_value(const char *section, const char *item,
                             const char *id, const char *zone,
                             const char *value) {
  if (zone != NULL && ignorelist_match(ignorelist, zone) != 0) {
    return 0;
  }

  for (int i = 0; i < stats_map_num; i++) {
    if (strcmp(section, stats_map[i].section) != 0) {
      continue;
    } else if (strcmp(item, stats_map[i].item) != 0) {
      continue;
    }

    value_t values[1];
    if (parse_value(value, &values[0], stats_map[i].ds_type) != 0) {
      return 1;
    }

    const char *plugin_instance = zone;
    if (plugin_instance == NULL) {
      plugin_instance = "";
    }
    const char *type_instance = stats_map[i].type_instance;
    if (type_instance == NULL) {
      type_instance = id;
    }
    if (type_instance == NULL) {
      type_instance = "";
    }

    value_list_t vl = VALUE_LIST_INIT;
    vl.values = values;
    vl.values_len = 1;
    sstrncpy(vl.plugin, "knot", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
    sstrncpy(vl.type, stats_map[i].type, sizeof(vl.type));
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

    plugin_dispatch_values(&vl);
    break;
  }

  return 0;
}

static int knot_send_command(knot_ctl_t *ctl, const char *cmd,
                             const char *flags) {
  knot_ctl_data_t data = {[KNOT_CTL_IDX_CMD] = cmd,
                          [KNOT_CTL_IDX_FLAGS] = flags};

  knot_ctl_send(ctl, KNOT_CTL_TYPE_DATA, &data);
  knot_ctl_send(ctl, KNOT_CTL_TYPE_BLOCK, NULL);

  return 0;
}

static int knot_read_stats_result(knot_ctl_t *ctl) {
  while (true) {
    knot_ctl_type_t type;
    knot_ctl_data_t data;

    int ret = knot_ctl_receive(ctl, &type, &data);
    if (ret != KNOT_EOK) {
      ERROR("knot: cannot read from control socket: %s", knot_strerror(ret));
      return 1;
    }

    const char *error = data[KNOT_CTL_IDX_ERROR];
    const char *section = data[KNOT_CTL_IDX_SECTION];
    const char *item = data[KNOT_CTL_IDX_ITEM];
    const char *id = data[KNOT_CTL_IDX_ID];
    const char *zone = data[KNOT_CTL_IDX_ZONE];
    const char *value = data[KNOT_CTL_IDX_DATA];

    if (error != NULL) {
      ERROR("knot: received error: %s", error);
      return 1;
    }

    if (type == KNOT_CTL_TYPE_DATA || type == KNOT_CTL_TYPE_EXTRA) {
      if (section != NULL && value != NULL) {
        char *zone_norm = knot_normalize_zone_alloc(zone);
        knot_handle_value(section, item, id, zone_norm, value);
        free(zone_norm);
      }
    } else if (type == KNOT_CTL_TYPE_BLOCK) {
      return 0;
    } else {
      ERROR("knot: received unexpected message of type %d", type);
    }
  }
}

static int knot_read(void) {
  int result = 0;

  knot_ctl_t *ctl = knot_ctl_alloc();
  if (ctl == NULL) {
    ERROR("knot: cannot allocate control structure");
    result = 1;
    goto out;
  }

  result =
      knot_ctl_connect(ctl, socket_path ? socket_path : socket_path_default);
  if (result != KNOT_EOK) {
    ERROR("knot: cannot connect to control socket: %s", knot_strerror(result));
    result = 1;
    goto out_allocated;
  }

  result = knot_send_command(ctl, "stats", "F");
  if (result != 0) {
    goto out_connected;
  }

  result = knot_read_stats_result(ctl);
  if (result != 0) {
    goto out_connected;
  }

  if (per_zone_stats) {
    result = knot_send_command(ctl, "zone-stats", "F");
    if (result != 0) {
      goto out_connected;
    }

    result = knot_read_stats_result(ctl);
    if (result != 0) {
      goto out_connected;
    }
  }

out_connected:
  knot_ctl_close(ctl);
out_allocated:
  knot_ctl_free(ctl);
out:
  return result;
}

static int knot_shutdown(void) {
  ignorelist_free(ignorelist);
  free(socket_path);
  return 0;
}

void module_register(void) {
  plugin_register_config("knot", knot_config, config_keys, config_keys_num);
  plugin_register_init("knot", knot_init);
  plugin_register_read("knot", knot_read);
  plugin_register_shutdown("knot", knot_shutdown);
}
