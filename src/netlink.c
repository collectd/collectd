/**
 * collectd - src/netlink.c
 * Copyright (C) 2007-2010  Florian octo Forster
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
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include <asm/types.h>
#include <sys/socket.h>

#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#if HAVE_LINUX_GEN_STATS_H
# include <linux/gen_stats.h>
#endif
#if HAVE_LINUX_PKT_SCHED_H
# include <linux/pkt_sched.h>
#endif

#if HAVE_LIBNETLINK_H
# include <libnetlink.h>
#elif HAVE_IPROUTE_LIBNETLINK_H
# include <iproute/libnetlink.h>
#elif HAVE_LINUX_LIBNETLINK_H
# include <linux/libnetlink.h>
#endif

typedef struct ir_ignorelist_s
{
  char *device;
  char *type;
  char *inst;
  struct ir_ignorelist_s *next;
} ir_ignorelist_t;

static int ir_ignorelist_invert = 1;
static ir_ignorelist_t *ir_ignorelist_head = NULL;

static struct rtnl_handle rth;

static char **iflist = NULL;
static size_t iflist_len = 0;

static const char *config_keys[] =
{
	"Interface",
	"VerboseInterface",
	"QDisc",
	"Class",
	"Filter",
	"IgnoreSelected"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int add_ignorelist (const char *dev, const char *type,
    const char *inst)
{
  ir_ignorelist_t *entry;

  entry = (ir_ignorelist_t *) malloc (sizeof (ir_ignorelist_t));
  if (entry == NULL)
    return (-1);

  memset (entry, '\0', sizeof (ir_ignorelist_t));

  if (strcasecmp (dev, "All") != 0)
  {
    entry->device = strdup (dev);
    if (entry->device == NULL)
    {
      sfree (entry);
      return (-1);
    }
  }

  entry->type = strdup (type);
  if (entry->type == NULL)
  {
    sfree (entry->device);
    sfree (entry);
    return (-1);
  }

  if (inst != NULL)
  {
    entry->inst = strdup (inst);
    if (entry->inst == NULL)
    {
      sfree (entry->type);
      sfree (entry->device);
      sfree (entry);
      return (-1);
    }
  }

  entry->next = ir_ignorelist_head;
  ir_ignorelist_head = entry;

  return (0);
} /* int add_ignorelist */

/* 
 * Checks wether a data set should be ignored. Returns `true' is the value
 * should be ignored, `false' otherwise.
 */
static int check_ignorelist (const char *dev,
    const char *type, const char *type_instance)
{
  ir_ignorelist_t *i;

  assert ((dev != NULL) && (type != NULL));

  if (ir_ignorelist_head == NULL)
    return (ir_ignorelist_invert ? 0 : 1);

  for (i = ir_ignorelist_head; i != NULL; i = i->next)
  {
    /* i->device == NULL  =>  match all devices */
    if ((i->device != NULL)
	&& (strcasecmp (i->device, dev) != 0))
      continue;

    if (strcasecmp (i->type, type) != 0)
      continue;

    if ((i->inst != NULL) && (type_instance != NULL)
	&& (strcasecmp (i->inst, type_instance) != 0))
      continue;

    DEBUG ("netlink plugin: check_ignorelist: "
	"(dev = %s; type = %s; inst = %s) matched "
	"(dev = %s; type = %s; inst = %s)",
	dev, type,
	type_instance == NULL ? "(nil)" : type_instance,
	i->device == NULL ? "(nil)" : i->device,
	i->type,
	i->inst == NULL ? "(nil)" : i->inst);

    return (ir_ignorelist_invert ? 0 : 1);
  } /* for i */

  return (ir_ignorelist_invert);
} /* int check_ignorelist */

static void submit_one (const char *dev, const char *type,
    const char *type_instance, derive_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].derive = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "netlink", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, dev, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));

  if (type_instance != NULL)
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void submit_one */

static void submit_two (const char *dev, const char *type,
    const char *type_instance,
    derive_t rx, derive_t tx)
{
  value_t values[2];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].derive = rx;
  values[1].derive = tx;

  vl.values = values;
  vl.values_len = 2;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "netlink", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, dev, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));

  if (type_instance != NULL)
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void submit_two */

static int link_filter (const struct sockaddr_nl __attribute__((unused)) *sa,
    struct nlmsghdr *nmh, void __attribute__((unused)) *args)
{
  struct ifinfomsg *msg;
  int msg_len;
  struct rtattr *attrs[IFLA_MAX + 1];
  struct rtnl_link_stats *stats;

  const char *dev;

  if (nmh->nlmsg_type != RTM_NEWLINK)
  {
    ERROR ("netlink plugin: link_filter: Don't know how to handle type %i.",
	nmh->nlmsg_type);
    return (-1);
  }

  msg = NLMSG_DATA (nmh);

  msg_len = nmh->nlmsg_len - sizeof (struct ifinfomsg);
  if (msg_len < 0)
  {
    ERROR ("netlink plugin: link_filter: msg_len = %i < 0;", msg_len);
    return (-1);
  }

  memset (attrs, '\0', sizeof (attrs));
  if (parse_rtattr (attrs, IFLA_MAX, IFLA_RTA (msg), msg_len) != 0)
  {
    ERROR ("netlink plugin: link_filter: parse_rtattr failed.");
    return (-1);
  }

  if (attrs[IFLA_IFNAME] == NULL)
  {
    ERROR ("netlink plugin: link_filter: attrs[IFLA_IFNAME] == NULL");
    return (-1);
  }
  dev = RTA_DATA (attrs[IFLA_IFNAME]);

  /* Update the `iflist'. It's used to know which interfaces exist and query
   * them later for qdiscs and classes. */
  if ((msg->ifi_index >= 0) && ((size_t) msg->ifi_index >= iflist_len))
  {
    char **temp;

    temp = (char **) realloc (iflist, (msg->ifi_index + 1) * sizeof (char *));
    if (temp == NULL)
    {
      ERROR ("netlink plugin: link_filter: realloc failed.");
      return (-1);
    }

    memset (temp + iflist_len, '\0',
	(msg->ifi_index + 1 - iflist_len) * sizeof (char *));
    iflist = temp;
    iflist_len = msg->ifi_index + 1;
  }
  if ((iflist[msg->ifi_index] == NULL)
      || (strcmp (iflist[msg->ifi_index], dev) != 0))
  {
    sfree (iflist[msg->ifi_index]);
    iflist[msg->ifi_index] = strdup (dev);
  }

  if (attrs[IFLA_STATS] == NULL)
  {
    DEBUG ("netlink plugin: link_filter: No statistics for interface %s.", dev);
    return (0);
  }
  stats = RTA_DATA (attrs[IFLA_STATS]);

  if (check_ignorelist (dev, "interface", NULL) == 0)
  {
    submit_two (dev, "if_octets", NULL, stats->rx_bytes, stats->tx_bytes);
    submit_two (dev, "if_packets", NULL, stats->rx_packets, stats->tx_packets);
    submit_two (dev, "if_errors", NULL, stats->rx_errors, stats->tx_errors);
  }
  else
  {
    DEBUG ("netlink plugin: Ignoring %s/interface.", dev);
  }

  if (check_ignorelist (dev, "if_detail", NULL) == 0)
  {
    submit_two (dev, "if_dropped", NULL, stats->rx_dropped, stats->tx_dropped);
    submit_one (dev, "if_multicast", NULL, stats->multicast);
    submit_one (dev, "if_collisions", NULL, stats->collisions);

    submit_one (dev, "if_rx_errors", "length", stats->rx_length_errors);
    submit_one (dev, "if_rx_errors", "over", stats->rx_over_errors);
    submit_one (dev, "if_rx_errors", "crc", stats->rx_crc_errors);
    submit_one (dev, "if_rx_errors", "frame", stats->rx_frame_errors);
    submit_one (dev, "if_rx_errors", "fifo", stats->rx_fifo_errors);
    submit_one (dev, "if_rx_errors", "missed", stats->rx_missed_errors);

    submit_one (dev, "if_tx_errors", "aborted", stats->tx_aborted_errors);
    submit_one (dev, "if_tx_errors", "carrier", stats->tx_carrier_errors);
    submit_one (dev, "if_tx_errors", "fifo", stats->tx_fifo_errors);
    submit_one (dev, "if_tx_errors", "heartbeat", stats->tx_heartbeat_errors);
    submit_one (dev, "if_tx_errors", "window", stats->tx_window_errors);
  }
  else
  {
    DEBUG ("netlink plugin: Ignoring %s/if_detail.", dev);
  }

  return (0);
} /* int link_filter */

static int qos_filter (const struct sockaddr_nl __attribute__((unused)) *sa,
    struct nlmsghdr *nmh, void *args)
{
  struct tcmsg *msg;
  int msg_len;
  struct rtattr *attrs[TCA_MAX + 1];

  int wanted_ifindex = *((int *) args);

  const char *dev;

  /* char *type_instance; */
  char *tc_type;
  char tc_inst[DATA_MAX_NAME_LEN];

  if (nmh->nlmsg_type == RTM_NEWQDISC)
    tc_type = "qdisc";
  else if (nmh->nlmsg_type == RTM_NEWTCLASS)
    tc_type = "class";
  else if (nmh->nlmsg_type == RTM_NEWTFILTER)
    tc_type = "filter";
  else
  {
    ERROR ("netlink plugin: qos_filter: Don't know how to handle type %i.",
	nmh->nlmsg_type);
    return (-1);
  }

  msg = NLMSG_DATA (nmh);

  msg_len = nmh->nlmsg_len - sizeof (struct tcmsg);
  if (msg_len < 0)
  {
    ERROR ("netlink plugin: qos_filter: msg_len = %i < 0;", msg_len);
    return (-1);
  }

  if (msg->tcm_ifindex != wanted_ifindex)
  {
    DEBUG ("netlink plugin: qos_filter: Got %s for interface #%i, "
	"but expected #%i.",
	tc_type, msg->tcm_ifindex, wanted_ifindex);
    return (0);
  }

  if ((msg->tcm_ifindex >= 0)
      && ((size_t) msg->tcm_ifindex >= iflist_len))
  {
    ERROR ("netlink plugin: qos_filter: msg->tcm_ifindex = %i "
	">= iflist_len = %zu",
	msg->tcm_ifindex, iflist_len);
    return (-1);
  }

  dev = iflist[msg->tcm_ifindex];
  if (dev == NULL)
  {
    ERROR ("netlink plugin: qos_filter: iflist[%i] == NULL",
	msg->tcm_ifindex);
    return (-1);
  }

  memset (attrs, '\0', sizeof (attrs));
  if (parse_rtattr (attrs, TCA_MAX, TCA_RTA (msg), msg_len) != 0)
  {
    ERROR ("netlink plugin: qos_filter: parse_rtattr failed.");
    return (-1);
  }

  if (attrs[TCA_KIND] == NULL)
  {
    ERROR ("netlink plugin: qos_filter: attrs[TCA_KIND] == NULL");
    return (-1);
  }

  { /* The the ID */
    uint32_t numberic_id;

    numberic_id = msg->tcm_handle;
    if (strcmp (tc_type, "filter") == 0)
      numberic_id = msg->tcm_parent;

    ssnprintf (tc_inst, sizeof (tc_inst), "%s-%x:%x",
	(const char *) RTA_DATA (attrs[TCA_KIND]),
	numberic_id >> 16,
	numberic_id & 0x0000FFFF);
  }

  DEBUG ("netlink plugin: qos_filter: got %s for %s (%i).",
      tc_type, dev, msg->tcm_ifindex);
  
  if (check_ignorelist (dev, tc_type, tc_inst))
    return (0);

#if HAVE_TCA_STATS2
  if (attrs[TCA_STATS2])
  {
    struct rtattr *attrs_stats[TCA_STATS_MAX + 1];

    memset (attrs_stats, '\0', sizeof (attrs_stats));
    parse_rtattr_nested (attrs_stats, TCA_STATS_MAX, attrs[TCA_STATS2]);

    if (attrs_stats[TCA_STATS_BASIC])
    {
      struct gnet_stats_basic bs;
      char type_instance[DATA_MAX_NAME_LEN];

      ssnprintf (type_instance, sizeof (type_instance), "%s-%s",
	  tc_type, tc_inst);

      memset (&bs, '\0', sizeof (bs));
      memcpy (&bs, RTA_DATA (attrs_stats[TCA_STATS_BASIC]),
	  MIN (RTA_PAYLOAD (attrs_stats[TCA_STATS_BASIC]), sizeof(bs)));

      submit_one (dev, "ipt_bytes", type_instance, bs.bytes);
      submit_one (dev, "ipt_packets", type_instance, bs.packets);
    }
  }
#endif /* TCA_STATS2 */
#if HAVE_TCA_STATS && HAVE_TCA_STATS2
  else
#endif
#if HAVE_TCA_STATS
  if (attrs[TCA_STATS] != NULL)
  {
    struct tc_stats ts;
    char type_instance[DATA_MAX_NAME_LEN];

    ssnprintf (type_instance, sizeof (type_instance), "%s-%s",
	tc_type, tc_inst);

    memset(&ts, '\0', sizeof (ts));
    memcpy(&ts, RTA_DATA (attrs[TCA_STATS]),
	MIN (RTA_PAYLOAD (attrs[TCA_STATS]), sizeof (ts)));

    submit_one (dev, "ipt_bytes", type_instance, ts.bytes);
    submit_one (dev, "ipt_packets", type_instance, ts.packets);
  }
#endif /* TCA_STATS */
#if HAVE_TCA_STATS || HAVE_TCA_STATS2
  else
#endif
  {
    DEBUG ("netlink plugin: qos_filter: Have neither TCA_STATS2 nor "
	"TCA_STATS.");
  }

  return (0);
} /* int qos_filter */

static int ir_config (const char *key, const char *value)
{
  char *new_val;
  char *fields[8];
  int fields_num;
  int status = 1;

  new_val = strdup (value);
  if (new_val == NULL)
    return (-1);

  fields_num = strsplit (new_val, fields, STATIC_ARRAY_SIZE (fields));
  if ((fields_num < 1) || (fields_num > 8))
  {
    sfree (new_val);
    return (-1);
  }

  if ((strcasecmp (key, "Interface") == 0)
      || (strcasecmp (key, "VerboseInterface") == 0))
  {
    if (fields_num != 1)
    {
      ERROR ("netlink plugin: Invalid number of fields for option "
	  "`%s'. Got %i, expected 1.", key, fields_num);
      status = -1;
    }
    else
    {
      add_ignorelist (fields[0], "interface", NULL);
      if (strcasecmp (key, "VerboseInterface") == 0)
	add_ignorelist (fields[0], "if_detail", NULL);
      status = 0;
    }
  }
  else if ((strcasecmp (key, "QDisc") == 0)
      || (strcasecmp (key, "Class") == 0)
      || (strcasecmp (key, "Filter") == 0))
  {
    if ((fields_num < 1) || (fields_num > 2))
    {
      ERROR ("netlink plugin: Invalid number of fields for option "
	  "`%s'. Got %i, expected 1 or 2.", key, fields_num);
      return (-1);
    }
    else
    {
      add_ignorelist (fields[0], key,
	  (fields_num == 2) ? fields[1] : NULL);
      status = 0;
    }
  }
  else if (strcasecmp (key, "IgnoreSelected") == 0)
  {
    if (fields_num != 1)
    {
      ERROR ("netlink plugin: Invalid number of fields for option "
	  "`IgnoreSelected'. Got %i, expected 1.", fields_num);
      status = -1;
    }
    else
    {
      if (IS_TRUE (fields[0]))
	ir_ignorelist_invert = 0;
      else
	ir_ignorelist_invert = 1;
      status = 0;
    }
  }

  sfree (new_val);

  return (status);
} /* int ir_config */

static int ir_init (void)
{
  memset (&rth, '\0', sizeof (rth));

  if (rtnl_open (&rth, 0) != 0)
  {
    ERROR ("netlink plugin: ir_init: rtnl_open failed.");
    return (-1);
  }

  return (0);
} /* int ir_init */

static int ir_read (void)
{
  struct ifinfomsg im;
  struct tcmsg tm;
  int ifindex;

  static const int type_id[] = { RTM_GETQDISC, RTM_GETTCLASS, RTM_GETTFILTER };
  static const char *type_name[] = { "qdisc", "class", "filter" };

  memset (&im, '\0', sizeof (im));
  im.ifi_type = AF_UNSPEC;

  if (rtnl_dump_request (&rth, RTM_GETLINK, &im, sizeof (im)) < 0)
  {
    ERROR ("netlink plugin: ir_read: rtnl_dump_request failed.");
    return (-1);
  }

  if (rtnl_dump_filter (&rth, link_filter, /* arg1 = */ NULL,
	NULL, NULL) != 0)
  {
    ERROR ("netlink plugin: ir_read: rtnl_dump_filter failed.");
    return (-1);
  }

  /* `link_filter' will update `iflist' which is used here to iterate over all
   * interfaces. */
  for (ifindex = 0; (size_t) ifindex < iflist_len; ifindex++)
  {
    size_t type_index;

    if (iflist[ifindex] == NULL)
      continue;

    for (type_index = 0; type_index < STATIC_ARRAY_SIZE (type_id); type_index++)
    {
      if (check_ignorelist (iflist[ifindex], type_name[type_index], NULL))
      {
	DEBUG ("netlink plugin: ir_read: check_ignorelist (%s, %s, (nil)) "
	    "== TRUE", iflist[ifindex], type_name[type_index]);
	continue;
      }

      DEBUG ("netlink plugin: ir_read: querying %s from %s (%i).",
	  type_name[type_index], iflist[ifindex], ifindex);

      memset (&tm, '\0', sizeof (tm));
      tm.tcm_family = AF_UNSPEC;
      tm.tcm_ifindex = ifindex;

      if (rtnl_dump_request (&rth, type_id[type_index], &tm, sizeof (tm)) < 0)
      {
	ERROR ("netlink plugin: ir_read: rtnl_dump_request failed.");
	continue;
      }

      if (rtnl_dump_filter (&rth, qos_filter, (void *) &ifindex,
	    NULL, NULL) != 0)
      {
	ERROR ("netlink plugin: ir_read: rtnl_dump_filter failed.");
	continue;
      }
    } /* for (type_index) */
  } /* for (if_index) */

  return (0);
} /* int ir_read */

static int ir_shutdown (void)
{
  if ((rth.fd != 0) || (rth.seq != 0) || (rth.dump != 0))
  {
    rtnl_close(&rth);
    memset (&rth, '\0', sizeof (rth));
  }
  
  return (0);
} /* int ir_shutdown */

void module_register (void)
{
  plugin_register_config ("netlink", ir_config, config_keys, config_keys_num);
  plugin_register_init ("netlink", ir_init);
  plugin_register_read ("netlink", ir_read);
  plugin_register_shutdown ("netlink", ir_shutdown);
} /* void module_register */

/*
 * vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
 */
