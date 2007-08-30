/**
 * collectd - src/netlink.c
 * Copyright (C) 2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include <asm/types.h>
#include <sys/socket.h>
#include <iproute/libnetlink.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <linux/gen_stats.h>

#include <iproute/ll_map.h>

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

  if (ir_ignorelist_head == NULL)
    return (ir_ignorelist_invert ? 0 : 1);

  for (i = ir_ignorelist_head; i != NULL; i = i->next)
  {
    if ((strcasecmp (i->device, dev) != 0)
	|| (strcasecmp (i->type, type) != 0))
      continue;

    if ((i->inst != NULL)
	&& ((type_instance == NULL)
	  || (strcasecmp (i->inst, type_instance) != 0)))
      continue;

    return (ir_ignorelist_invert ? 0 : 1);
  } /* for i */

  return (ir_ignorelist_invert);
} /* int check_ignorelist */

static void submit_one (const char *dev, const char *type,
    const char *type_instance, counter_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].counter = value;

  vl.values = values;
  vl.values_len = 1;
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, "netlink");
  strncpy (vl.plugin_instance, dev, sizeof (vl.plugin_instance));

  if (type_instance != NULL)
    strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (type, &vl);
} /* void submit_one */

static void submit_two (const char *dev, const char *type,
    const char *type_instance,
    counter_t rx, counter_t tx)
{
  value_t values[2];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].counter = rx;
  values[1].counter = tx;

  vl.values = values;
  vl.values_len = 2;
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, "netlink");
  strncpy (vl.plugin_instance, dev, sizeof (vl.plugin_instance));

  if (type_instance != NULL)
    strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (type, &vl);
} /* void submit_two */

static int link_filter (const struct sockaddr_nl *sa, struct nlmsghdr *nmh,
    void *args)
{
  struct ifinfomsg *msg;
  int msg_len;
  struct rtattr *attrs[IFLA_MAX + 1];
  struct rtnl_link_stats *stats;

  const char *dev;

  if (nmh->nlmsg_type != RTM_NEWLINK)
  {
    ERROR ("netlink plugin: link_filter: Don't know how to handle type %i.\n",
	nmh->nlmsg_type);
    return (-1);
  }

  msg = NLMSG_DATA (nmh);

  msg_len = nmh->nlmsg_len - sizeof (struct ifinfomsg);
  if (msg_len < 0)
  {
    ERROR ("netlink plugin: link_filter: msg_len = %i < 0;\n", msg_len);
    return (-1);
  }

  memset (attrs, '\0', sizeof (attrs));
  if (parse_rtattr (attrs, IFLA_MAX, IFLA_RTA (msg), msg_len) != 0)
  {
    ERROR ("netlink plugin: link_filter: parse_rtattr failed.\n");
    return (-1);
  }

  if (attrs[IFLA_STATS] == NULL)
    return (-1);
  stats = RTA_DATA (attrs[IFLA_STATS]);

  if (attrs[IFLA_IFNAME] == NULL)
  {
    ERROR ("netlink plugin: link_filter: attrs[IFLA_IFNAME] == NULL\n");
    return (-1);
  }
  dev = RTA_DATA (attrs[IFLA_IFNAME]);

  if (check_ignorelist (dev, "interface", NULL) == 0)
  {
    submit_two (dev, "if_octets", NULL, stats->rx_bytes, stats->tx_bytes);
    submit_two (dev, "if_packets", NULL, stats->rx_bytes, stats->tx_bytes);
    submit_two (dev, "if_errors", NULL, stats->rx_bytes, stats->tx_bytes);
  }
  else
  {
    DEBUG ("netlink plugin: Ignoring %s/interface.", dev);
  }

  if (check_ignorelist (dev, "if_detail", NULL) == 0)
  {
    submit_two (dev, "if_dropped", NULL, stats->rx_bytes, stats->tx_bytes);
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

static int qos_filter (const struct sockaddr_nl *sa, struct nlmsghdr *nmh,
    void *args)
{
  struct tcmsg *msg;
  int msg_len;
  struct rtattr *attrs[TCA_MAX + 1];

  const char *dev;

  /* char *type_instance; */
  char *tc_type;
  char tc_inst[DATA_MAX_NAME_LEN];

  printf ("=== qos_filter ===\n");

  if (nmh->nlmsg_type == RTM_NEWQDISC)
    tc_type = "qdisc";
  else if (nmh->nlmsg_type == RTM_NEWTCLASS)
    tc_type = "class";
  else if (nmh->nlmsg_type == RTM_NEWTFILTER)
    tc_type = "filter";
  else
  {
    ERROR ("netlink plugin: qos_filter: Don't know how to handle type %i.\n",
	nmh->nlmsg_type);
    return (-1);
  }

  msg = NLMSG_DATA (nmh);

  msg_len = nmh->nlmsg_len - sizeof (struct tcmsg);
  if (msg_len < 0)
  {
    ERROR ("netlink plugin: qos_filter: msg_len = %i < 0;\n", msg_len);
    return (-1);
  }

  dev = ll_index_to_name (msg->tcm_ifindex);
  if (dev == NULL)
  {
    ERROR ("netlink plugin: qos_filter: ll_index_to_name (%i) failed.\n",
	msg->tcm_ifindex);
    return (-1);
  }

  memset (attrs, '\0', sizeof (attrs));
  if (parse_rtattr (attrs, TCA_MAX, TCA_RTA (msg), msg_len) != 0)
  {
    ERROR ("netlink plugin: qos_filter: parse_rtattr failed.\n");
    return (-1);
  }

  if (attrs[TCA_KIND] == NULL)
  {
    ERROR ("netlink plugin: qos_filter: attrs[TCA_KIND] == NULL\n");
    return (-1);
  }

  { /* The the ID */
    uint32_t numberic_id;

    numberic_id = msg->tcm_handle;
    if (strcmp (tc_type, "filter") == 0)
      numberic_id = msg->tcm_parent;

    snprintf (tc_inst, sizeof (tc_inst), "%s-%x:%x",
	(const char *) RTA_DATA (attrs[TCA_KIND]),
	numberic_id >> 16,
	numberic_id & 0x0000FFFF);
    tc_inst[sizeof (tc_inst) - 1] = '\0';
  }
  
  if (check_ignorelist (dev, tc_type, tc_inst))
    return (0);

  if (attrs[TCA_STATS2])
  {
    struct rtattr *attrs_stats[TCA_STATS_MAX + 1];

    memset (attrs_stats, '\0', sizeof (attrs_stats));
    parse_rtattr_nested (attrs_stats, TCA_STATS_MAX, attrs[TCA_STATS2]);

    if (attrs_stats[TCA_STATS_BASIC])
    {
      struct gnet_stats_basic bs;
      char type_instance[DATA_MAX_NAME_LEN];

      snprintf (type_instance, sizeof (type_instance), "%s-%s",
	  tc_type, tc_inst);
      type_instance[sizeof (type_instance) - 1] = '\0';

      memset (&bs, '\0', sizeof (bs));
      memcpy (&bs, RTA_DATA (attrs_stats[TCA_STATS_BASIC]),
	  MIN (RTA_PAYLOAD (attrs_stats[TCA_STATS_BASIC]), sizeof(bs)));

      submit_one (dev, "ipt_octets", type_instance, bs.bytes);
      submit_one (dev, "ipt_packets", type_instance, bs.packets);
    }
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
  else if (strcasecmp (key, "IgnoreSelected"))
  {
    if (fields_num != 1)
    {
      ERROR ("netlink plugin: Invalid number of fields for option "
	  "`IgnoreSelected'. Got %i, expected 1.", fields_num);
      status = -1;
    }
    else
    {
      if ((strcasecmp (fields[0], "yes") == 0)
	  || (strcasecmp (fields[0], "true") == 0)
	  || (strcasecmp (fields[0], "on") == 0))
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
    ERROR ("netlink plugin: print_stats: rtnl_open failed.\n");
    return (-1);
  }

  if (ll_init_map (&rth) != 0)
  {
    ERROR ("netlink plugin: print_stats: ll_init_map failed.\n");
    return (-1);
  }

  return (0);
} /* int ir_init */

static int ir_read (void)
{
  struct ifinfomsg im;
  struct tcmsg tm;

  memset (&im, '\0', sizeof (im));
  im.ifi_type = AF_UNSPEC;

  memset (&tm, '\0', sizeof (tm));
  tm.tcm_family = AF_UNSPEC;

  if (rtnl_dump_request (&rth, RTM_GETLINK, &im, sizeof (im)) < 0)
  {
    ERROR ("netlink plugin: print_stats: rtnl_dump_request failed.\n");
    return (-1);
  }

  if (rtnl_dump_filter (&rth, link_filter, /* arg1 = */ NULL,
	NULL, NULL) != 0)
  {
    ERROR ("netlink plugin: print_stats: rtnl_dump_filter failed.\n");
    return (-1);
  }

  /* Get QDisc stats */
  if (rtnl_dump_request (&rth, RTM_GETQDISC, &tm, sizeof (tm)) < 0)
  {
    ERROR ("netlink plugin: print_stats: rtnl_dump_request failed.\n");
    return (-1);
  }

  if (rtnl_dump_filter (&rth, qos_filter, /* arg1 = */ NULL,
	NULL, NULL) != 0)
  {
    ERROR ("netlink plugin: print_stats: rtnl_dump_filter failed.\n");
    return (-1);
  }

  /* Get Class stats */
  if (rtnl_dump_request (&rth, RTM_GETTCLASS, &tm, sizeof (tm)) < 0)
  {
    ERROR ("netlink plugin: print_stats: rtnl_dump_request failed.\n");
    return (-1);
  }

  if (rtnl_dump_filter (&rth, qos_filter, /* arg1 = */ NULL,
	NULL, NULL) != 0)
  {
    ERROR ("netlink plugin: print_stats: rtnl_dump_filter failed.\n");
    return (-1);
  }

  /* Get Filter stats */
  if (rtnl_dump_request (&rth, RTM_GETTFILTER, &tm, sizeof (tm)) < 0)
  {
    ERROR ("netlink plugin: print_stats: rtnl_dump_request failed.\n");
    return (-1);
  }

  if (rtnl_dump_filter (&rth, qos_filter, /* arg1 = */ NULL,
	NULL, NULL) != 0)
  {
    ERROR ("netlink plugin: print_stats: rtnl_dump_filter failed.\n");
    return (-1);
  }


  return (0);
} /* int print_stats */

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
