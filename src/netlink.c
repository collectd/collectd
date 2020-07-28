/**
 * collectd - src/netlink.c
 * Copyright (C) 2007-2010  Florian octo Forster
 * Copyright (C) 2008-2012  Sebastian Harl
 * Copyright (C) 2013       Andreas Henriksson
 * Copyright (C) 2013       Marc Fournier
 * Copyright (C) 2020       Intel Corporation
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
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 *   Andreas Henriksson <andreas at fatal.se>
 *   Marc Fournier <marc.fournier at camptocamp.com>
 *   Kamil Wiatrowski <kamilx.wiatrowski at intel.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#if HAVE_REGEX_H
#include <regex.h>
#endif

#include <asm/types.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#if HAVE_LINUX_GEN_STATS_H
#include <linux/gen_stats.h>
#endif
#if HAVE_LINUX_PKT_SCHED_H
#include <linux/pkt_sched.h>
#endif

#include <glob.h>
#include <libmnl/libmnl.h>

#define NETLINK_VF_DEFAULT_BUF_SIZE_KB 16

struct ir_link_stats_storage_s {

  uint64_t rx_packets;
  uint64_t tx_packets;
  uint64_t rx_bytes;
  uint64_t tx_bytes;
  uint64_t rx_errors;
  uint64_t tx_errors;

  uint64_t rx_dropped;
  uint64_t tx_dropped;
  uint64_t multicast;
  uint64_t collisions;
  uint64_t rx_nohandler;

  uint64_t rx_length_errors;
  uint64_t rx_over_errors;
  uint64_t rx_crc_errors;
  uint64_t rx_frame_errors;
  uint64_t rx_fifo_errors;
  uint64_t rx_missed_errors;

  uint64_t tx_aborted_errors;
  uint64_t tx_carrier_errors;
  uint64_t tx_fifo_errors;
  uint64_t tx_heartbeat_errors;
  uint64_t tx_window_errors;
};

union ir_link_stats_u {
  struct rtnl_link_stats *stats32;
#ifdef HAVE_RTNL_LINK_STATS64
  struct rtnl_link_stats64 *stats64;
#endif
};

#ifdef HAVE_IFLA_VF_STATS
typedef struct vf_stats_s {
  struct ifla_vf_mac *vf_mac;
  uint32_t vlan;
  uint32_t qos;
  uint32_t spoofcheck;
  uint32_t link_state;
  uint32_t txrate;
  uint32_t min_txrate;
  uint32_t max_txrate;
  uint32_t rss_query_en;
  uint32_t trust;

  uint64_t rx_packets;
  uint64_t tx_packets;
  uint64_t rx_bytes;
  uint64_t tx_bytes;
  uint64_t broadcast;
  uint64_t multicast;
#ifdef HAVE_IFLA_VF_STATS_RX_DROPPED
  uint64_t rx_dropped;
#endif
#ifdef HAVE_IFLA_VF_STATS_TX_DROPPED
  uint64_t tx_dropped;
#endif
} vf_stats_t;
#endif

typedef struct ir_ignorelist_s {
  char *device;
#if HAVE_REGEX_H
  regex_t *rdevice; /* regular expression device identification */
#endif
  char *type;
  char *inst;
  struct ir_ignorelist_s *next;
} ir_ignorelist_t;

struct qos_stats {
  struct gnet_stats_basic *bs;
  struct gnet_stats_queue *qs;
};

static int ir_ignorelist_invert = 1;
static ir_ignorelist_t *ir_ignorelist_head;

static struct mnl_socket *nl;

static char **iflist;
static size_t iflist_len;

static bool collect_vf_stats = false;
static size_t nl_socket_buffer_size = NETLINK_VF_DEFAULT_BUF_SIZE_KB * 1024;

static const char *config_keys[] = {
    "Interface", "VerboseInterface", "QDisc",         "Class",
    "Filter",    "IgnoreSelected",   "CollectVFStats"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int add_ignorelist(const char *dev, const char *type, const char *inst) {
  ir_ignorelist_t *entry;

  entry = calloc(1, sizeof(*entry));
  if (entry == NULL)
    return -1;

#if HAVE_REGEX_H
  size_t len = strlen(dev);
  /* regex string is enclosed in "/.../" */
  if ((len > 2) && (dev[0] == '/') && dev[len - 1] == '/') {
    char *copy = strdup(dev + 1);
    if (copy == NULL) {
      sfree(entry);
      return -1;
    }
    copy[strlen(copy) - 1] = '\0';

    regex_t *re = calloc(1, sizeof(*re));
    if (re == NULL) {
      sfree(entry);
      sfree(copy);
      return -1;
    }

    int status = regcomp(re, copy, REG_EXTENDED);
    if (status != 0) {
      char errbuf[1024];
      (void)regerror(status, re, errbuf, sizeof(errbuf));
      ERROR("netlink plugin: add_ignorelist: regcomp for %s failed: %s", dev,
            errbuf);
      regfree(re);
      sfree(entry);
      sfree(copy);
      sfree(re);
      return -1;
    }

    entry->rdevice = re;
    sfree(copy);
  } else
#endif
      if (strcasecmp(dev, "All") != 0) {
    entry->device = strdup(dev);
    if (entry->device == NULL) {
      sfree(entry);
      return -1;
    }
  }

  entry->type = strdup(type);
  if (entry->type == NULL) {
    sfree(entry->device);
#if HAVE_REGEX_H
    sfree(entry->rdevice);
#endif
    sfree(entry);
    return -1;
  }

  if (inst != NULL) {
    entry->inst = strdup(inst);
    if (entry->inst == NULL) {
      sfree(entry->type);
      sfree(entry->device);
#if HAVE_REGEX_H
      sfree(entry->rdevice);
#endif
      sfree(entry);
      return -1;
    }
  }

  entry->next = ir_ignorelist_head;
  ir_ignorelist_head = entry;

  return 0;
} /* int add_ignorelist */

/*
 * Checks wether a data set should be ignored. Returns `true' is the value
 * should be ignored, `false' otherwise.
 */
static int check_ignorelist(const char *dev, const char *type,
                            const char *type_instance) {
  assert((dev != NULL) && (type != NULL));

  if (ir_ignorelist_head == NULL)
    return ir_ignorelist_invert ? 0 : 1;

  for (ir_ignorelist_t *i = ir_ignorelist_head; i != NULL; i = i->next) {
#if HAVE_REGEX_H
    if (i->rdevice != NULL) {
      if (regexec(i->rdevice, dev, 0, NULL, 0) != REG_NOERROR)
        continue;
    } else
#endif
        /* i->device == NULL  =>  match all devices */
        if ((i->device != NULL) && (strcasecmp(i->device, dev) != 0))
      continue;

    if (strcasecmp(i->type, type) != 0)
      continue;

    if ((i->inst != NULL) && (type_instance != NULL) &&
        (strcasecmp(i->inst, type_instance) != 0))
      continue;

#if COLLECT_DEBUG
#if HAVE_REGEX_H
    const char *device = i->device == NULL
                             ? (i->rdevice != NULL ? "(regexp)" : "(nil)")
                             : i->device;
#else
    const char *device = i->device == NULL ? "(nil)" : i->device;
#endif
    DEBUG("netlink plugin: check_ignorelist: "
          "(dev = %s; type = %s; inst = %s) matched "
          "(dev = %s; type = %s; inst = %s)",
          dev, type, type_instance == NULL ? "(nil)" : type_instance, device,
          i->type, i->inst == NULL ? "(nil)" : i->inst);
#endif

    return ir_ignorelist_invert ? 0 : 1;
  } /* for i */

  return ir_ignorelist_invert;
} /* int check_ignorelist */

#ifdef HAVE_IFLA_VF_STATS
static void submit_one_gauge(const char *dev, const char *type,
                             const char *type_instance, gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "netlink", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void submit_one_gauge */
#endif

static void submit_one(const char *dev, const char *type,
                       const char *type_instance, derive_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.derive = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "netlink", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void submit_one */

static void submit_two(const char *dev, const char *type,
                       const char *type_instance, derive_t rx, derive_t tx) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {
      {.derive = rx},
      {.derive = tx},
  };

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.plugin, "netlink", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, dev, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void submit_two */

static int update_iflist(struct ifinfomsg *msg, const char *dev) {
  /* Update the `iflist'. It's used to know which interfaces exist and query
   * them later for qdiscs and classes. */
  if ((msg->ifi_index >= 0) && ((size_t)msg->ifi_index >= iflist_len)) {
    char **temp;

    temp = realloc(iflist, (msg->ifi_index + 1) * sizeof(char *));
    if (temp == NULL) {
      ERROR("netlink plugin: update_iflist: realloc failed.");
      return -1;
    }

    memset(temp + iflist_len, '\0',
           (msg->ifi_index + 1 - iflist_len) * sizeof(char *));
    iflist = temp;
    iflist_len = msg->ifi_index + 1;
  }
  if ((iflist[msg->ifi_index] == NULL) ||
      (strcmp(iflist[msg->ifi_index], dev) != 0)) {
    sfree(iflist[msg->ifi_index]);
    iflist[msg->ifi_index] = strdup(dev);
  }

  return 0;
} /* int update_iflist */

static void check_ignorelist_and_submit(const char *dev,
                                        struct ir_link_stats_storage_s *stats) {

  if (check_ignorelist(dev, "interface", NULL) == 0) {
    submit_two(dev, "if_octets", NULL, stats->rx_bytes, stats->tx_bytes);
    submit_two(dev, "if_packets", NULL, stats->rx_packets, stats->tx_packets);
    submit_two(dev, "if_errors", NULL, stats->rx_errors, stats->tx_errors);
  } else {
    DEBUG("netlink plugin: Ignoring %s/interface.", dev);
  }

  if (check_ignorelist(dev, "if_detail", NULL) == 0) {
    submit_two(dev, "if_dropped", NULL, stats->rx_dropped, stats->tx_dropped);
    submit_one(dev, "if_multicast", NULL, stats->multicast);
    submit_one(dev, "if_collisions", NULL, stats->collisions);
#if defined(HAVE_STRUCT_RTNL_LINK_STATS_RX_NOHANDLER) ||                       \
    defined(HAVE_STRUCT_RTNL_LINK_STATS64_RX_NOHANDLER)
    submit_one(dev, "if_rx_nohandler", NULL, stats->rx_nohandler);
#endif

    submit_one(dev, "if_rx_errors", "length", stats->rx_length_errors);
    submit_one(dev, "if_rx_errors", "over", stats->rx_over_errors);
    submit_one(dev, "if_rx_errors", "crc", stats->rx_crc_errors);
    submit_one(dev, "if_rx_errors", "frame", stats->rx_frame_errors);
    submit_one(dev, "if_rx_errors", "fifo", stats->rx_fifo_errors);
    submit_one(dev, "if_rx_errors", "missed", stats->rx_missed_errors);

    submit_one(dev, "if_tx_errors", "aborted", stats->tx_aborted_errors);
    submit_one(dev, "if_tx_errors", "carrier", stats->tx_carrier_errors);
    submit_one(dev, "if_tx_errors", "fifo", stats->tx_fifo_errors);
    submit_one(dev, "if_tx_errors", "heartbeat", stats->tx_heartbeat_errors);
    submit_one(dev, "if_tx_errors", "window", stats->tx_window_errors);
  } else {
    DEBUG("netlink plugin: Ignoring %s/if_detail.", dev);
  }

} /* void check_ignorelist_and_submit */

#define COPY_RTNL_LINK_VALUE(dst_stats, src_stats, value_name)                 \
  (dst_stats)->value_name = (src_stats)->value_name

#define COPY_RTNL_LINK_STATS(dst_stats, src_stats)                             \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_packets);                      \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, tx_packets);                      \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_bytes);                        \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, tx_bytes);                        \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_errors);                       \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, tx_errors);                       \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_dropped);                      \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, tx_dropped);                      \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, multicast);                       \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, collisions);                      \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_length_errors);                \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_over_errors);                  \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_crc_errors);                   \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_frame_errors);                 \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_fifo_errors);                  \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, rx_missed_errors);                \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, tx_aborted_errors);               \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, tx_carrier_errors);               \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, tx_fifo_errors);                  \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, tx_heartbeat_errors);             \
  COPY_RTNL_LINK_VALUE(dst_stats, src_stats, tx_window_errors)

#ifdef HAVE_RTNL_LINK_STATS64
static void check_ignorelist_and_submit64(const char *dev,
                                          struct rtnl_link_stats64 *stats) {
  struct ir_link_stats_storage_s s;

  COPY_RTNL_LINK_STATS(&s, stats);
#ifdef HAVE_STRUCT_RTNL_LINK_STATS64_RX_NOHANDLER
  COPY_RTNL_LINK_VALUE(&s, stats, rx_nohandler);
#endif

  check_ignorelist_and_submit(dev, &s);
}
#endif

static void check_ignorelist_and_submit32(const char *dev,
                                          struct rtnl_link_stats *stats) {
  struct ir_link_stats_storage_s s;

  COPY_RTNL_LINK_STATS(&s, stats);
#ifdef HAVE_STRUCT_RTNL_LINK_STATS_RX_NOHANDLER
  COPY_RTNL_LINK_VALUE(&s, stats, rx_nohandler);
#endif

  check_ignorelist_and_submit(dev, &s);
}

#ifdef HAVE_IFLA_VF_STATS
static void vf_info_submit(const char *dev, vf_stats_t *vf_stats) {
  if (vf_stats->vf_mac == NULL) {
    ERROR("netlink plugin: vf_info_submit: failed to get VF macaddress, "
          "skipping VF for interface %s",
          dev);
    return;
  }
  uint8_t *mac = vf_stats->vf_mac->mac;
  uint32_t vf_num = vf_stats->vf_mac->vf;
  char instance[512];
  ssnprintf(instance, sizeof(instance), "%s_vf%u_%02x:%02x:%02x:%02x:%02x:%02x",
            dev, vf_num, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  DEBUG("netlink plugin: vf_info_submit: plugin_instance - %s", instance);

  submit_one_gauge(instance, "vf_link_info", "number", vf_num);
  submit_one_gauge(instance, "vf_link_info", "vlan", vf_stats->vlan);
  submit_one_gauge(instance, "vf_link_info", "qos", vf_stats->qos);
  submit_one_gauge(instance, "vf_link_info", "spoofcheck",
                   vf_stats->spoofcheck);
  submit_one_gauge(instance, "vf_link_info", "link_state",
                   vf_stats->link_state);
  submit_one_gauge(instance, "vf_link_info", "tx_rate", vf_stats->txrate);
  submit_one_gauge(instance, "vf_link_info", "min_tx_rate",
                   vf_stats->min_txrate);
  submit_one_gauge(instance, "vf_link_info", "max_tx_rate",
                   vf_stats->max_txrate);
  submit_one_gauge(instance, "vf_link_info", "rss_query_en",
                   vf_stats->rss_query_en);
  submit_one_gauge(instance, "vf_link_info", "trust", vf_stats->trust);

  submit_one(instance, "vf_broadcast", NULL, vf_stats->broadcast);
  submit_one(instance, "vf_multicast", NULL, vf_stats->multicast);
  submit_two(instance, "vf_packets", NULL, vf_stats->rx_packets,
             vf_stats->tx_packets);
  submit_two(instance, "vf_bytes", NULL, vf_stats->rx_bytes,
             vf_stats->tx_bytes);
#if defined(HAVE_IFLA_VF_STATS_RX_DROPPED) &&                                  \
    defined(HAVE_IFLA_VF_STATS_TX_DROPPED)
  submit_two(instance, "vf_dropped", NULL, vf_stats->rx_dropped,
             vf_stats->tx_dropped);
#endif
} /* void vf_info_submit */

#define IFCOPY_VF_STAT_VALUE(attr, name, type_name)                            \
  do {                                                                         \
    if (mnl_attr_get_type(attr) == type_name) {                                \
      if (mnl_attr_validate(attr, MNL_TYPE_U64) < 0) {                         \
        ERROR("netlink plugin: vf_info_attr_cb: " #type_name                   \
              " mnl_attr_validate failed.");                                   \
        return MNL_CB_ERROR;                                                   \
      }                                                                        \
      vf_stats->name = mnl_attr_get_u64(attr);                                 \
    }                                                                          \
  } while (0)

static int vf_info_attr_cb(const struct nlattr *attr, void *args) {
  vf_stats_t *vf_stats = (vf_stats_t *)args;

  /* skip unsupported attribute */
  if (mnl_attr_type_valid(attr, IFLA_VF_MAX) < 0) {
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == IFLA_VF_MAC) {
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*vf_stats->vf_mac)) <
        0) {
      ERROR("netlink plugin: vf_info_attr_cb: IFLA_VF_MAC mnl_attr_validate2 "
            "failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }

    vf_stats->vf_mac = (struct ifla_vf_mac *)mnl_attr_get_payload(attr);
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == IFLA_VF_VLAN) {
    struct ifla_vf_vlan *vf_vlan;
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*vf_vlan)) < 0) {
      ERROR("netlink plugin: vf_info_attr_cb: IFLA_VF_VLAN mnl_attr_validate2 "
            "failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }

    vf_vlan = (struct ifla_vf_vlan *)mnl_attr_get_payload(attr);
    vf_stats->vlan = vf_vlan->vlan;
    vf_stats->qos = vf_vlan->qos;
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == IFLA_VF_TX_RATE) {
    struct ifla_vf_tx_rate *vf_txrate;
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*vf_txrate)) < 0) {
      ERROR("netlink plugin: vf_info_attr_cb: IFLA_VF_TX_RATE "
            "mnl_attr_validate2 failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }

    vf_txrate = (struct ifla_vf_tx_rate *)mnl_attr_get_payload(attr);
    vf_stats->txrate = vf_txrate->rate;
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == IFLA_VF_SPOOFCHK) {
    struct ifla_vf_spoofchk *vf_spoofchk;
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*vf_spoofchk)) < 0) {
      ERROR("netlink plugin: vf_info_attr_cb: IFLA_VF_SPOOFCHK "
            "mnl_attr_validate2 failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }

    vf_spoofchk = (struct ifla_vf_spoofchk *)mnl_attr_get_payload(attr);
    vf_stats->spoofcheck = vf_spoofchk->setting;
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == IFLA_VF_LINK_STATE) {
    struct ifla_vf_link_state *vf_link_state;
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*vf_link_state)) < 0) {
      ERROR("netlink plugin: vf_info_attr_cb: IFLA_VF_LINK_STATE "
            "mnl_attr_validate2 failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }

    vf_link_state = (struct ifla_vf_link_state *)mnl_attr_get_payload(attr);
    vf_stats->link_state = vf_link_state->link_state;
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == IFLA_VF_RATE) {
    struct ifla_vf_rate *vf_rate;
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*vf_rate)) < 0) {
      ERROR("netlink plugin: vf_info_attr_cb: IFLA_VF_RATE mnl_attr_validate2 "
            "failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }

    vf_rate = (struct ifla_vf_rate *)mnl_attr_get_payload(attr);
    vf_stats->min_txrate = vf_rate->min_tx_rate;
    vf_stats->max_txrate = vf_rate->max_tx_rate;
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == IFLA_VF_RSS_QUERY_EN) {
    struct ifla_vf_rss_query_en *vf_rss_query_en;
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*vf_rss_query_en)) <
        0) {
      ERROR("netlink plugin: vf_info_attr_cb: IFLA_VF_RSS_QUERY_EN "
            "mnl_attr_validate2 "
            "failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }

    vf_rss_query_en = (struct ifla_vf_rss_query_en *)mnl_attr_get_payload(attr);
    vf_stats->rss_query_en = vf_rss_query_en->setting;
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == IFLA_VF_TRUST) {
    struct ifla_vf_trust *vf_trust;
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*vf_trust)) < 0) {
      ERROR("netlink plugin: vf_info_attr_cb: IFLA_VF_TRUST mnl_attr_validate2 "
            "failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }

    vf_trust = (struct ifla_vf_trust *)mnl_attr_get_payload(attr);
    vf_stats->trust = vf_trust->setting;
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == IFLA_VF_STATS) {
    if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) {
      ERROR("netlink plugin: vf_info_attr_cb: IFLA_VF_STATS mnl_attr_validate "
            "failed.");
      return MNL_CB_ERROR;
    }

    struct nlattr *nested;
    mnl_attr_for_each_nested(nested, attr) {
      IFCOPY_VF_STAT_VALUE(nested, rx_packets, IFLA_VF_STATS_RX_PACKETS);
      IFCOPY_VF_STAT_VALUE(nested, tx_packets, IFLA_VF_STATS_TX_PACKETS);
      IFCOPY_VF_STAT_VALUE(nested, rx_bytes, IFLA_VF_STATS_RX_BYTES);
      IFCOPY_VF_STAT_VALUE(nested, tx_bytes, IFLA_VF_STATS_TX_BYTES);
      IFCOPY_VF_STAT_VALUE(nested, broadcast, IFLA_VF_STATS_BROADCAST);
      IFCOPY_VF_STAT_VALUE(nested, multicast, IFLA_VF_STATS_MULTICAST);
#ifdef HAVE_IFLA_VF_STATS_RX_DROPPED
      IFCOPY_VF_STAT_VALUE(nested, rx_dropped, IFLA_VF_STATS_RX_DROPPED);
#endif
#ifdef HAVE_IFLA_VF_STATS_TX_DROPPED
      IFCOPY_VF_STAT_VALUE(nested, tx_dropped, IFLA_VF_STATS_TX_DROPPED);
#endif
    }
    return MNL_CB_OK;
  }

  return MNL_CB_OK;
} /* int vf_info_attr_cb */
#endif /* HAVE_IFLA_VF_STATS */

static int link_filter_cb(const struct nlmsghdr *nlh,
                          void *args __attribute__((unused))) {
  struct ifinfomsg *ifm = mnl_nlmsg_get_payload(nlh);
  struct nlattr *attr;
  const char *dev = NULL;
  union ir_link_stats_u stats;
#ifdef HAVE_IFLA_VF_STATS
  uint32_t num_vfs = 0;
#endif
  bool stats_done = false;

  if (nlh->nlmsg_type != RTM_NEWLINK) {
    ERROR("netlink plugin: link_filter_cb: Don't know how to handle type %i.",
          nlh->nlmsg_type);
    return MNL_CB_ERROR;
  }

  /* Scan attribute list for device name. */
  mnl_attr_for_each(attr, nlh, sizeof(*ifm)) {
    if (mnl_attr_get_type(attr) != IFLA_IFNAME)
      continue;

    if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0) {
      ERROR("netlink plugin: link_filter_cb: IFLA_IFNAME mnl_attr_validate "
            "failed.");
      return MNL_CB_ERROR;
    }

    dev = mnl_attr_get_str(attr);
    if (update_iflist(ifm, dev) < 0)
      return MNL_CB_ERROR;
    break;
  }

  if (dev == NULL) {
    ERROR("netlink plugin: link_filter_cb: dev == NULL");
    return MNL_CB_ERROR;
  }

  if (check_ignorelist(dev, "interface", NULL) != 0 &&
      check_ignorelist(dev, "if_detail", NULL) != 0) {
    DEBUG("netlink plugin: link_filter_cb: Ignoring %s/interface.", dev);
    DEBUG("netlink plugin: link_filter_cb: Ignoring %s/if_detail.", dev);
    return MNL_CB_OK;
  }

#ifdef HAVE_IFLA_VF_STATS
  if (collect_vf_stats) {
    mnl_attr_for_each(attr, nlh, sizeof(*ifm)) {
      if (mnl_attr_get_type(attr) != IFLA_NUM_VF)
        continue;

      if (mnl_attr_validate(attr, MNL_TYPE_U32) < 0) {
        ERROR("netlink plugin: link_filter_cb: IFLA_NUM_VF mnl_attr_validate "
              "failed.");
        return MNL_CB_ERROR;
      }

      num_vfs = mnl_attr_get_u32(attr);
      break;
    }
  }
#endif

#ifdef HAVE_RTNL_LINK_STATS64
  mnl_attr_for_each(attr, nlh, sizeof(*ifm)) {
    if (mnl_attr_get_type(attr) != IFLA_STATS64)
      continue;

    uint16_t attr_len = mnl_attr_get_payload_len(attr);
    if (attr_len < sizeof(*stats.stats64)) {
      ERROR("netlink plugin: link_filter_cb: IFLA_STATS64 attribute has "
            "insufficient data.");
      return MNL_CB_ERROR;
    }
    stats.stats64 = mnl_attr_get_payload(attr);

    check_ignorelist_and_submit64(dev, stats.stats64);

    stats_done = true;
    break;
  }
#endif
  if (stats_done == false) {
    mnl_attr_for_each(attr, nlh, sizeof(*ifm)) {
      if (mnl_attr_get_type(attr) != IFLA_STATS)
        continue;

      uint16_t attr_len = mnl_attr_get_payload_len(attr);
      if (attr_len < sizeof(*stats.stats32)) {
        ERROR("netlink plugin: link_filter_cb: IFLA_STATS attribute has "
              "insufficient data.");
        return MNL_CB_ERROR;
      }
      stats.stats32 = mnl_attr_get_payload(attr);

      check_ignorelist_and_submit32(dev, stats.stats32);

      stats_done = true;
      break;
    }
  }

#if COLLECT_DEBUG
  if (stats_done == false)
    DEBUG("netlink plugin: link_filter: No statistics for interface %s.", dev);
#endif

#ifdef HAVE_IFLA_VF_STATS
  if (num_vfs == 0)
    return MNL_CB_OK;

  /* Get VFINFO list. */
  mnl_attr_for_each(attr, nlh, sizeof(*ifm)) {
    if (mnl_attr_get_type(attr) != IFLA_VFINFO_LIST)
      continue;

    if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) {
      ERROR("netlink plugin: link_filter_cb: IFLA_VFINFO_LIST "
            "mnl_attr_validate failed.");
      return MNL_CB_ERROR;
    }

    struct nlattr *nested;
    mnl_attr_for_each_nested(nested, attr) {
      if (mnl_attr_get_type(nested) != IFLA_VF_INFO) {
        continue;
      }

      if (mnl_attr_validate(nested, MNL_TYPE_NESTED) < 0) {
        ERROR("netlink plugin: link_filter_cb: IFLA_VF_INFO mnl_attr_validate "
              "failed.");
        return MNL_CB_ERROR;
      }

      vf_stats_t vf_stats = {0};
      if (mnl_attr_parse_nested(nested, vf_info_attr_cb, &vf_stats) ==
          MNL_CB_ERROR)
        return MNL_CB_ERROR;

      vf_info_submit(dev, &vf_stats);
    }
    break;
  }
#endif

  return MNL_CB_OK;
} /* int link_filter_cb */

#if HAVE_TCA_STATS2
static int qos_attr_cb(const struct nlattr *attr, void *data) {
  struct qos_stats *q_stats = (struct qos_stats *)data;

  /* skip unsupported attribute in user-space */
  if (mnl_attr_type_valid(attr, TCA_STATS_MAX) < 0)
    return MNL_CB_OK;

  if (mnl_attr_get_type(attr) == TCA_STATS_BASIC) {
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*q_stats->bs)) < 0) {
      ERROR("netlink plugin: qos_attr_cb: TCA_STATS_BASIC mnl_attr_validate2 "
            "failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }
    q_stats->bs = mnl_attr_get_payload(attr);
    return MNL_CB_OK;
  }

  if (mnl_attr_get_type(attr) == TCA_STATS_QUEUE) {
    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*q_stats->qs)) < 0) {
      ERROR("netlink plugin: qos_attr_cb: TCA_STATS_QUEUE mnl_attr_validate2 "
            "failed.");
      return MNL_CB_ERROR;
    }
    q_stats->qs = mnl_attr_get_payload(attr);
    return MNL_CB_OK;
  }

  return MNL_CB_OK;
} /* qos_attr_cb */
#endif

static int qos_filter_cb(const struct nlmsghdr *nlh, void *args) {
  struct tcmsg *tm = mnl_nlmsg_get_payload(nlh);
  struct nlattr *attr;

  int wanted_ifindex = *((int *)args);

  const char *dev;
  const char *kind = NULL;

  /* char *type_instance; */
  const char *tc_type;
  char tc_inst[DATA_MAX_NAME_LEN];

  bool stats_submitted = false;

  if (nlh->nlmsg_type == RTM_NEWQDISC)
    tc_type = "qdisc";
  else if (nlh->nlmsg_type == RTM_NEWTCLASS)
    tc_type = "class";
  else if (nlh->nlmsg_type == RTM_NEWTFILTER)
    tc_type = "filter";
  else {
    ERROR("netlink plugin: qos_filter_cb: Don't know how to handle type %i.",
          nlh->nlmsg_type);
    return MNL_CB_ERROR;
  }

  if (tm->tcm_ifindex != wanted_ifindex) {
    DEBUG("netlink plugin: qos_filter_cb: Got %s for interface #%i, "
          "but expected #%i.",
          tc_type, tm->tcm_ifindex, wanted_ifindex);
    return MNL_CB_OK;
  }

  if ((tm->tcm_ifindex >= 0) && ((size_t)tm->tcm_ifindex >= iflist_len)) {
    ERROR("netlink plugin: qos_filter_cb: tm->tcm_ifindex = %i "
          ">= iflist_len = %" PRIsz,
          tm->tcm_ifindex, iflist_len);
    return MNL_CB_ERROR;
  }

  dev = iflist[tm->tcm_ifindex];
  if (dev == NULL) {
    ERROR("netlink plugin: qos_filter_cb: iflist[%i] == NULL", tm->tcm_ifindex);
    return MNL_CB_ERROR;
  }

  mnl_attr_for_each(attr, nlh, sizeof(*tm)) {
    if (mnl_attr_get_type(attr) != TCA_KIND)
      continue;

    if (mnl_attr_validate(attr, MNL_TYPE_STRING) < 0) {
      ERROR(
          "netlink plugin: qos_filter_cb: TCA_KIND mnl_attr_validate failed.");
      return MNL_CB_ERROR;
    }

    kind = mnl_attr_get_str(attr);
    break;
  }

  if (kind == NULL) {
    ERROR("netlink plugin: qos_filter_cb: kind == NULL");
    return -1;
  }

  { /* The ID */
    uint32_t numberic_id;

    numberic_id = tm->tcm_handle;
    if (strcmp(tc_type, "filter") == 0)
      numberic_id = tm->tcm_parent;

    ssnprintf(tc_inst, sizeof(tc_inst), "%s-%x:%x", kind, numberic_id >> 16,
              numberic_id & 0x0000FFFF);
  }

  DEBUG("netlink plugin: qos_filter_cb: got %s for %s (%i).", tc_type, dev,
        tm->tcm_ifindex);

  if (check_ignorelist(dev, tc_type, tc_inst))
    return MNL_CB_OK;

#if HAVE_TCA_STATS2
  mnl_attr_for_each(attr, nlh, sizeof(*tm)) {
    struct qos_stats q_stats;

    memset(&q_stats, 0x0, sizeof(q_stats));

    if (mnl_attr_get_type(attr) != TCA_STATS2)
      continue;

    if (mnl_attr_validate(attr, MNL_TYPE_NESTED) < 0) {
      ERROR("netlink plugin: qos_filter_cb: TCA_STATS2 mnl_attr_validate "
            "failed.");
      return MNL_CB_ERROR;
    }

    mnl_attr_parse_nested(attr, qos_attr_cb, &q_stats);

    if (q_stats.bs != NULL || q_stats.qs != NULL) {
      char type_instance[DATA_MAX_NAME_LEN];

      stats_submitted = true;

      int r = ssnprintf(type_instance, sizeof(type_instance), "%s-%s", tc_type,
                        tc_inst);
      if ((size_t)r >= sizeof(type_instance)) {
        ERROR("netlink plugin: type_instance truncated to %zu bytes, need %d",
              sizeof(type_instance), r);
        return MNL_CB_ERROR;
      }

      if (q_stats.bs != NULL) {
        submit_one(dev, "ipt_bytes", type_instance, q_stats.bs->bytes);
        submit_one(dev, "ipt_packets", type_instance, q_stats.bs->packets);
      }
      if (q_stats.qs != NULL) {
        submit_one(dev, "if_tx_dropped", type_instance, q_stats.qs->drops);
      }
    }

    break;
  }
#endif /* TCA_STATS2 */

#if HAVE_TCA_STATS
  mnl_attr_for_each(attr, nlh, sizeof(*tm)) {
    struct tc_stats *ts = NULL;

    if (mnl_attr_get_type(attr) != TCA_STATS)
      continue;

    if (mnl_attr_validate2(attr, MNL_TYPE_UNSPEC, sizeof(*ts)) < 0) {
      ERROR("netlink plugin: qos_filter_cb: TCA_STATS mnl_attr_validate2 "
            "failed: %s",
            STRERRNO);
      return MNL_CB_ERROR;
    }
    ts = mnl_attr_get_payload(attr);

    if (!stats_submitted && ts != NULL) {
      char type_instance[DATA_MAX_NAME_LEN];

      int r = ssnprintf(type_instance, sizeof(type_instance), "%s-%s", tc_type,
                        tc_inst);
      if ((size_t)r >= sizeof(type_instance)) {
        ERROR("netlink plugin: type_instance truncated to %zu bytes, need %d",
              sizeof(type_instance), r);
        return MNL_CB_ERROR;
      }

      submit_one(dev, "ipt_bytes", type_instance, ts->bytes);
      submit_one(dev, "ipt_packets", type_instance, ts->packets);
    }

    break;
  }

#endif /* TCA_STATS */

#if !(HAVE_TCA_STATS && HAVE_TCA_STATS2)
  DEBUG("netlink plugin: qos_filter_cb: Have neither TCA_STATS2 nor "
        "TCA_STATS.");
#endif

  return MNL_CB_OK;
} /* int qos_filter_cb */

static size_t ir_get_buffer_size() {
  if (collect_vf_stats == false) {
    return MNL_SOCKET_BUFFER_SIZE;
  }

  glob_t g;
  unsigned int max_num = 0;
  if (glob("/sys/class/net/*/device/sriov_totalvfs", GLOB_NOSORT, NULL, &g)) {
    ERROR("netlink plugin: ir_get_buffer_size: glob failed");
    /* using default value */
    return NETLINK_VF_DEFAULT_BUF_SIZE_KB * 1024;
  }

  for (size_t i = 0; i < g.gl_pathc; i++) {
    char buf[16];
    ssize_t len;
    int num = 0;
    int fd = open(g.gl_pathv[i], O_RDONLY);
    if (fd < 0) {
      WARNING("netlink plugin: ir_get_buffer_size: failed to open `%s.`",
              g.gl_pathv[i]);
      continue;
    }

    if ((len = read(fd, buf, sizeof(buf) - 1)) <= 0) {
      WARNING("netlink plugin: ir_get_buffer_size: failed to read `%s.`",
              g.gl_pathv[i]);
      close(fd);
      continue;
    }
    buf[len] = '\0';

    if (sscanf(buf, "%d", &num) != 1) {
      WARNING("netlink plugin: ir_get_buffer_size: failed to read number from "
              "`%s.`",
              buf);
      close(fd);
      continue;
    }

    if (num > max_num)
      max_num = num;

    close(fd);
  }
  globfree(&g);
  DEBUG("netlink plugin: ir_get_buffer_size: max sriov_totalvfs = %u", max_num);

  unsigned int mp = NETLINK_VF_DEFAULT_BUF_SIZE_KB;
  /* allign to power of two, buffer size should be at least totalvfs/2 kb */
  while (mp < max_num / 2)
    mp *= 2;

  return mp * 1024;
}

static int ir_config(const char *key, const char *value) {
  char *new_val;
  char *fields[8];
  int fields_num;
  int status = 1;

  new_val = strdup(value);
  if (new_val == NULL)
    return -1;

  fields_num = strsplit(new_val, fields, STATIC_ARRAY_SIZE(fields));
  if ((fields_num < 1) || (fields_num > 8)) {
    sfree(new_val);
    return -1;
  }

  if ((strcasecmp(key, "Interface") == 0) ||
      (strcasecmp(key, "VerboseInterface") == 0)) {
    if (fields_num != 1) {
      ERROR("netlink plugin: Invalid number of fields for option "
            "`%s'. Got %i, expected 1.",
            key, fields_num);
      status = -1;
    } else {
      status = add_ignorelist(fields[0], "interface", NULL);
      if (strcasecmp(key, "VerboseInterface") == 0)
        status += add_ignorelist(fields[0], "if_detail", NULL);
    }
  } else if ((strcasecmp(key, "QDisc") == 0) ||
             (strcasecmp(key, "Class") == 0) ||
             (strcasecmp(key, "Filter") == 0)) {
    if (fields_num > 2) {
      ERROR("netlink plugin: Invalid number of fields for option "
            "`%s'. Got %i, expected 1 or 2.",
            key, fields_num);
      return -1;
    } else {
      status =
          add_ignorelist(fields[0], key, (fields_num == 2) ? fields[1] : NULL);
    }
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    if (fields_num != 1) {
      ERROR("netlink plugin: Invalid number of fields for option "
            "`IgnoreSelected'. Got %i, expected 1.",
            fields_num);
      status = -1;
    } else {
      if (IS_TRUE(fields[0]))
        ir_ignorelist_invert = 0;
      else
        ir_ignorelist_invert = 1;
      status = 0;
    }
  } else if (strcasecmp(key, "CollectVFStats") == 0) {
    if (fields_num != 1) {
      ERROR("netlink plugin: Invalid number of fields for option "
            "`%s'. Got %i, expected 1.",
            key, fields_num);
      status = -1;
    } else {
#ifdef HAVE_IFLA_VF_STATS
      if (IS_TRUE(fields[0]))
        collect_vf_stats = true;
      else
        collect_vf_stats = false;
#else
      WARNING("netlink plugin: VF statistics not supported on this system.");
#endif
      status = 0;
    }
  }

  sfree(new_val);

  return status;
} /* int ir_config */

static int ir_init(void) {
  nl = mnl_socket_open(NETLINK_ROUTE);
  if (nl == NULL) {
    ERROR("netlink plugin: ir_init: mnl_socket_open failed.");
    return -1;
  }

  if (mnl_socket_bind(nl, 0, MNL_SOCKET_AUTOPID) < 0) {
    ERROR("netlink plugin: ir_init: mnl_socket_bind failed.");
    return -1;
  }

  nl_socket_buffer_size = ir_get_buffer_size();
  INFO("netlink plugin: ir_init: buffer size = %zu", nl_socket_buffer_size);

  return 0;
} /* int ir_init */

static int ir_read(void) {
  char buf[nl_socket_buffer_size];
  struct nlmsghdr *nlh;
  struct rtgenmsg *rt;
  int ret;
  unsigned int seq, portid;

  static const int type_id[] = {RTM_GETQDISC, RTM_GETTCLASS, RTM_GETTFILTER};
  static const char *type_name[] = {"qdisc", "class", "filter"};

  portid = mnl_socket_get_portid(nl);

  nlh = mnl_nlmsg_put_header(buf);
  nlh->nlmsg_type = RTM_GETLINK;
  nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
  nlh->nlmsg_seq = seq = time(NULL);
  rt = mnl_nlmsg_put_extra_header(nlh, sizeof(*rt));
  rt->rtgen_family = AF_PACKET;

#ifdef HAVE_IFLA_VF_STATS
  if (collect_vf_stats &&
      mnl_attr_put_u32_check(nlh, sizeof(buf), IFLA_EXT_MASK,
                             RTEXT_FILTER_VF) == 0) {
    ERROR("netlink plugin: FAILED to set RTEXT_FILTER_VF");
    return -1;
  }
#endif

  if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
    ERROR("netlink plugin: ir_read: rtnl_wilddump_request failed.");
    return -1;
  }

  ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
  while (ret > 0) {
    ret = mnl_cb_run(buf, ret, seq, portid, link_filter_cb, NULL);
    if (ret <= MNL_CB_STOP)
      break;
    ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
  }
  if (ret < 0) {
    ERROR("netlink plugin: ir_read: mnl_socket_recvfrom failed: %s", STRERRNO);
    return (-1);
  }

  /* `link_filter_cb' will update `iflist' which is used here to iterate
   * over all interfaces. */
  for (size_t ifindex = 1; ifindex < iflist_len; ifindex++) {
    struct tcmsg *tm;

    if (iflist[ifindex] == NULL)
      continue;

    for (size_t type_index = 0; type_index < STATIC_ARRAY_SIZE(type_id);
         type_index++) {
      if (check_ignorelist(iflist[ifindex], type_name[type_index], NULL)) {
        DEBUG("netlink plugin: ir_read: check_ignorelist (%s, %s, (nil)) "
              "== TRUE",
              iflist[ifindex], type_name[type_index]);
        continue;
      }

      DEBUG("netlink plugin: ir_read: querying %s from %s (%" PRIsz ").",
            type_name[type_index], iflist[ifindex], ifindex);

      nlh = mnl_nlmsg_put_header(buf);
      nlh->nlmsg_type = type_id[type_index];
      nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
      nlh->nlmsg_seq = seq = time(NULL);
      tm = mnl_nlmsg_put_extra_header(nlh, sizeof(*tm));
      tm->tcm_family = AF_PACKET;
      tm->tcm_ifindex = ifindex;

      if (mnl_socket_sendto(nl, nlh, nlh->nlmsg_len) < 0) {
        ERROR("netlink plugin: ir_read: mnl_socket_sendto failed.");
        continue;
      }

      ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
      while (ret > 0) {
        ret = mnl_cb_run(buf, ret, seq, portid, qos_filter_cb, &ifindex);
        if (ret <= MNL_CB_STOP)
          break;
        ret = mnl_socket_recvfrom(nl, buf, sizeof(buf));
      }
      if (ret < 0) {
        ERROR("netlink plugin: ir_read: mnl_socket_recvfrom failed: %s",
              STRERRNO);
        continue;
      }
    } /* for (type_index) */
  }   /* for (if_index) */

  return 0;
} /* int ir_read */

static int ir_shutdown(void) {
  if (nl) {
    mnl_socket_close(nl);
    nl = NULL;
  }

  ir_ignorelist_t *next = NULL;
  for (ir_ignorelist_t *i = ir_ignorelist_head; i != NULL; i = next) {
    next = i->next;
#if HAVE_REGEX_H
    if (i->rdevice != NULL) {
      regfree(i->rdevice);
      sfree(i->rdevice);
    }
#endif
    sfree(i->inst);
    sfree(i->type);
    sfree(i->device);
    sfree(i);
  }
  ir_ignorelist_head = NULL;

  return 0;
} /* int ir_shutdown */

void module_register(void) {
  plugin_register_config("netlink", ir_config, config_keys, config_keys_num);
  plugin_register_init("netlink", ir_init);
  plugin_register_read("netlink", ir_read);
  plugin_register_shutdown("netlink", ir_shutdown);
} /* void module_register */
