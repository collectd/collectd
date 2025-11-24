/**
 * collectd - src/mesh.c
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
 *   Pierre Lebleu <pierre.lebleu at pile-engineering.com>
 */

#include "collectd.h"
#include <inttypes.h>

#include "plugin.h"
#include "utils/common/common.h"

#if !KERNEL_LINUX
#error "This module only supports the Linux implementation of ieee8021"
#endif

#define PLUGIN_NAME "mesh"

#define SYS_PATH "/sys/kernel/debug/ieee80211/"

/*
 * Device attributes
 */
#define ESTAB_PLINKS "estab_plinks" // The number of established peer links
#define DROPPED_FRAMES_CONGESTION                                              \
  "dropped_frames_congestion" // The number of dropped frames due to congestion
#define DROPPED_FRAMES_NO_ROUTE                                                \
  "dropped_frames_no_route" // The number of dropped frames due to unrouteable
#define DROPPED_FRAMES_TTL                                                     \
  "dropped_frames_ttl" // The number of dropped frames due to TTL
#define FORWARDED_FRAMES "fwded_frames" // The number of forwarded frames
#define FORWARDED_UNICAST                                                      \
  "fwded_unicast" // The number of unicast forwarded frames
#define FORWARDED_MCAST                                                        \
  "fwded_mcast" // The number of multicast forwarded frames

/*
 * Metric labels
 */
#define LABEL_NAME "name"
#define LABEL_REASON "reason"
#define LABEL_TYPE "type"

/*
 * The config key strings
 */
#define PHY_DEVICE_KEY "PhysicalDevice"
#define NET_DEVICE_KEY "NetworkDevice"

#define PHY_NAME_LEN 32
#define NET_NAME_LEN 32

struct mesh_list {
  char phy_name[PHY_NAME_LEN];
  char net_name[NET_NAME_LEN];
  struct mesh_list *next;
}; /* mesh_list */
typedef struct mesh_list mesh_list_t;

typedef enum {
  MESH_FAM_DROPPED,
  MESH_FAM_FORWARDED,
  MESH_FAM_PEERS,
  MESH_FAM_MAX,
} mesh_family_t;

static metric_family_t g_families[MESH_FAM_MAX] = {
    [MESH_FAM_DROPPED] =
        {
            .name = "hw.network.packets.dropped",
            .help = "",
            .unit = "{packet}",
            .type = METRIC_TYPE_COUNTER,
        },
    [MESH_FAM_FORWARDED] =
        {
            .name = "hw.network.packets.forwarded",
            .help = "",
            .unit = "{packet}",
            .type = METRIC_TYPE_COUNTER,
        },
    [MESH_FAM_PEERS] =
        {
            .name = "hw.network.peers",
            .help = "",
            .unit = "{packet}",
            .type = METRIC_TYPE_GAUGE,
        },
};

static mesh_list_t *g_mesh_list;

/*
 * Private functions
 */
static int mesh_list_add(const oconfig_item_t *ci) {
  mesh_list_t *new_mesh;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING(PLUGIN_NAME " plugin: `" PHY_DEVICE_KEY "' blocks need exactly"
                        " one string argument.");
    return EINVAL;
  }

  if ((new_mesh = calloc(1, sizeof(mesh_list_t))) == NULL) {
    ERROR(PLUGIN_NAME " plugin: Couldn't allocate memory.");
    return ENOMEM;
  }

  sstrncpy(new_mesh->phy_name, ci->values[0].value.string,
           sizeof(new_mesh->phy_name));

  for (int i = 0; i < ci->children_num; i++) {
    const oconfig_item_t *child = ci->children + i;

    if (strcasecmp(NET_DEVICE_KEY, child->key) == 0) {
      if ((child->values_num != 1) ||
          (ci->values[0].type != OCONFIG_TYPE_STRING)) {
        WARNING(PLUGIN_NAME " plugin: `" NET_DEVICE_KEY "' blocks need exactly"
                            " one string argument.");
        free(new_mesh);
        return EINVAL;
      }
      sstrncpy(new_mesh->net_name, child->values[0].value.string,
               sizeof(new_mesh->net_name));
    } else {
      WARNING(PLUGIN_NAME " plugin: ignoring unknown option %s", child->key);
    }
  }

  new_mesh->next = g_mesh_list;
  g_mesh_list = new_mesh;

  return 0;
} /* int mesh_list_add */

static int mesh_config(oconfig_item_t *ci) {
  int ret = 0;

  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp(PHY_DEVICE_KEY, child->key) == 0) {
      if ((ret = mesh_list_add(child)) != 0)
        break;
    } else {
      WARNING(PLUGIN_NAME " plugin: ignoring unknown option %s", child->key);
    }
  }

  return ret;
} /* int mesh_config */

static int mesh_submit(const mesh_list_t *ml, const mesh_family_t type,
                       metric_t m) {
  metric_family_t *fam = &g_families[type];
  int ret;

  metric_label_set(&m, LABEL_NAME, ml->net_name);

  if ((ret = metric_family_metric_append(fam, m)) != 0) {
    ERROR(PLUGIN_NAME " plugin: metric_family_append failed for %s: %s",
          fam->name, STRERROR(ret));
    return ret;
  }

  if ((ret = plugin_dispatch_metric_family(fam)) != 0) {
    ERROR(PLUGIN_NAME
          " plugin: plugin_dispatch_metric_family failed for %s: %s",
          fam->name, STRERROR(ret));
  }

  metric_family_metric_reset(fam);

  return ret;
} /* int mesh_submit */

static int mesh_read_gauge_attr_from_file(const char *fname, gauge_t *value) {
  FILE *f;
  int n;

  if ((f = fopen(fname, "r")) == NULL) {
    ERROR(PLUGIN_NAME " plugin: cannot open `%s'", fname);
    return EACCES;
  }

  n = fscanf(f, "%lf", value);
  fclose(f);

  if (n != 1) {
    ERROR(PLUGIN_NAME " plugin: did not find a double in %s", fname);
    return EINVAL;
  }

  return 0;
} /* int mesh_read_gauge_attr_from_file */

static int mesh_read_derive_attr_from_file(const char *fname, derive_t *value) {
  FILE *f;
  int n;

  if ((f = fopen(fname, "r")) == NULL) {
    ERROR(PLUGIN_NAME " plugin: cannot open `%s'", fname);
    return EACCES;
  }

  n = fscanf(f, "%" PRId64, value);
  fclose(f);

  if (n != 1) {
    ERROR(PLUGIN_NAME " plugin: did not find a int64_t in %s", fname);
    return EINVAL;
  }

  return 0;
} /* int mesh_read_derive_attr_from_file */

static int mesh_read_gauge_attr(const mesh_list_t *m, const char *attr,
                                gauge_t *value) {
  char str[sizeof(SYS_PATH) + strlen(m->phy_name) + sizeof("/netdev:") +
           strlen(m->net_name) + sizeof("/") + strlen(attr) + 1];

  snprintf(str, sizeof(str), SYS_PATH "%s/netdev:%s/%s", m->phy_name,
           m->net_name, attr);

  return mesh_read_gauge_attr_from_file(str, value);
} /* int mesh_read_gauge_attr */

static inline int mesh_read_estab_plinks(const mesh_list_t *ml) {
  int ret;
  metric_t m;

  DEBUG(PLUGIN_NAME " plugin: Reading attribute " ESTAB_PLINKS " for device %s",
        ml->net_name);

  if ((ret = mesh_read_gauge_attr(ml, ESTAB_PLINKS, &m.value.gauge)) != 0) {
    ERROR(PLUGIN_NAME " plugin: Unable to read " ESTAB_PLINKS);
    return ret;
  }

  return mesh_submit(ml, MESH_FAM_PEERS, m);
} /* int mesh_read_estab_plinks */

static int mesh_read_stats_attr(const mesh_list_t *ml, const char *attr,
                                derive_t *value) {
  char str[sizeof(SYS_PATH) + strlen(ml->phy_name) + sizeof("/netdev:") +
           strlen(ml->net_name) + sizeof("/mesh_stats/") + strlen(attr) + 1];

  snprintf(str, sizeof(str), SYS_PATH "%s/netdev:%s/mesh_stats/%s",
           ml->phy_name, ml->net_name, attr);

  return mesh_read_derive_attr_from_file(str, value);
} /* int mesh_read_stats_attr */

static int mesh_read_dropped_frames_congestion(const mesh_list_t *ml) {
  int ret;
  metric_t m;

  DEBUG(PLUGIN_NAME " plugin: Reading attribute " DROPPED_FRAMES_CONGESTION
                    " for device %s",
        ml->net_name);

  ret = mesh_read_stats_attr(ml, DROPPED_FRAMES_CONGESTION, &m.value.derive);

  if (ret != 0) {
    ERROR(PLUGIN_NAME " plugin: Unable to read " DROPPED_FRAMES_CONGESTION);
    return ret;
  }

  metric_label_set(&m, LABEL_REASON, "congestion");

  return mesh_submit(ml, MESH_FAM_DROPPED, m);
} /* int mesh_read_dropped_frames_congestion */

static int mesh_read_dropped_frames_no_route(const mesh_list_t *ml) {
  int ret;
  metric_t m;

  DEBUG(PLUGIN_NAME " plugin: Reading attribute " DROPPED_FRAMES_NO_ROUTE
                    " for device %s",
        ml->net_name);

  ret = mesh_read_stats_attr(ml, DROPPED_FRAMES_NO_ROUTE, &m.value.derive);

  if (ret != 0) {
    ERROR(PLUGIN_NAME " plugin: Unable to read " DROPPED_FRAMES_NO_ROUTE);
    return ret;
  }

  metric_label_set(&m, LABEL_REASON, "no_route");

  return mesh_submit(ml, MESH_FAM_DROPPED, m);
} /* int mesh_read_dropped_frames_no_route */

static int mesh_read_dropped_frames_ttl(const mesh_list_t *ml) {
  int ret;
  metric_t m;

  DEBUG(PLUGIN_NAME " plugin: Reading attribute " DROPPED_FRAMES_TTL
                    " for device %s",
        ml->net_name);

  ret = mesh_read_stats_attr(ml, DROPPED_FRAMES_TTL, &m.value.derive);

  if (ret != 0) {
    ERROR(PLUGIN_NAME " plugin: Unable to read " DROPPED_FRAMES_TTL);
    return ret;
  }

  metric_label_set(&m, LABEL_REASON, "ttl");

  return mesh_submit(ml, MESH_FAM_DROPPED, m);
} /* int mesh_read_dropped_frames_ttl */

static int mesh_read_forwarded_frames(const mesh_list_t *ml) {
  int ret;
  metric_t m;

  DEBUG(PLUGIN_NAME " plugin: Reading attribute " FORWARDED_FRAMES
                    " for device %s",
        ml->net_name);

  ret = mesh_read_stats_attr(ml, FORWARDED_FRAMES, &m.value.derive);

  if (ret != 0) {
    ERROR(PLUGIN_NAME " plugin: Unable to read " FORWARDED_FRAMES);
    return ret;
  }

  return mesh_submit(ml, MESH_FAM_FORWARDED, m);
} /* int mesh_read_forwarded_frames */

static int mesh_read_forwarded_unicast(const mesh_list_t *ml) {
  int ret;
  metric_t m;

  DEBUG(PLUGIN_NAME " plugin: Reading attribute " FORWARDED_UNICAST
                    " for device %s",
        ml->net_name);

  ret = mesh_read_stats_attr(ml, FORWARDED_UNICAST, &m.value.derive);

  if (ret != 0) {
    ERROR(PLUGIN_NAME " plugin: Unable to read " FORWARDED_UNICAST);
    return ret;
  }

  metric_label_set(&m, LABEL_TYPE, "unicast");

  return mesh_submit(ml, MESH_FAM_FORWARDED, m);
} /* int mesh_read_forwarded_unicast */

static int mesh_read_forwarded_mcast(const mesh_list_t *ml) {
  int ret;
  metric_t m;

  DEBUG(PLUGIN_NAME " plugin: Reading attribute " FORWARDED_MCAST
                    " for device %s",
        ml->net_name);

  ret = mesh_read_stats_attr(ml, FORWARDED_MCAST, &m.value.derive);

  if (ret != 0) {
    ERROR(PLUGIN_NAME " plugin: Unable to read " FORWARDED_MCAST);
    return ret;
  }

  metric_label_set(&m, LABEL_TYPE, "multicast");

  return mesh_submit(ml, MESH_FAM_FORWARDED, m);
} /* int mesh_read_forwarded_mcast */

static int mesh_read(void) {
  typedef int (*mesh_reader_t)(const mesh_list_t *);
  static const mesh_reader_t mesh_readers[] = {
      mesh_read_estab_plinks,
      mesh_read_dropped_frames_congestion,
      mesh_read_dropped_frames_no_route,
      mesh_read_dropped_frames_ttl,
      mesh_read_forwarded_frames,
      mesh_read_forwarded_unicast,
      mesh_read_forwarded_mcast,
  };
  int ret = 0;

  for (const mesh_list_t *m = g_mesh_list; m != NULL; m = m->next) {
    for (int i = 0; i < STATIC_ARRAY_SIZE(mesh_readers); i++) {
      ret = mesh_readers[i](m);

      if (ret != 0)
        break;
    }
  }

  return ret;
} /* int mesh_read */

static int mesh_shutdown(void) {
  while (g_mesh_list != NULL) {
    mesh_list_t *next = g_mesh_list->next;
    free(g_mesh_list);
    g_mesh_list = next;
  }

  return 0;
} /* int mesh_shutdown */

void module_register(void) {
  plugin_register_complex_config(PLUGIN_NAME, mesh_config);
  plugin_register_read(PLUGIN_NAME, mesh_read);
  plugin_register_shutdown(PLUGIN_NAME, mesh_shutdown);
} /* void module_register */
