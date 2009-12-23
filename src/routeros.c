/**
 * collectd - src/routeros.c
 * Copyright (C) 2009  Florian octo Forster
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
#include "common.h"
#include "plugin.h"

#include <routeros_api.h>

struct cr_data_s
{
  ros_connection_t *connection;

  char *node;
  char *service;
  char *username;
  char *password;

  _Bool collect_interface;
  _Bool collect_regtable;
  _Bool collect_cpu_load;
  _Bool collect_memory;
  _Bool collect_df;
  _Bool collect_disk;
};
typedef struct cr_data_s cr_data_t;

static void cr_submit_io (const char *type, const char *type_instance, /* {{{ */
    counter_t rx, counter_t tx)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = rx;
	values[1].counter = tx;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "routeros", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* }}} void cr_submit_io */

static void submit_interface (const ros_interface_t *i) /* {{{ */
{
  if (i == NULL)
    return;

  if (!i->running)
  {
    submit_interface (i->next);
    return;
  }

  cr_submit_io ("if_packets", i->name,
      (counter_t) i->rx_packets, (counter_t) i->tx_packets);
  cr_submit_io ("if_octets", i->name,
      (counter_t) i->rx_bytes, (counter_t) i->tx_bytes);
  cr_submit_io ("if_errors", i->name,
      (counter_t) i->rx_errors, (counter_t) i->tx_errors);
  cr_submit_io ("if_dropped", i->name,
      (counter_t) i->rx_drops, (counter_t) i->tx_drops);

  submit_interface (i->next);
} /* }}} void submit_interface */

static int handle_interface (__attribute__((unused)) ros_connection_t *c, /* {{{ */
    const ros_interface_t *i, __attribute__((unused)) void *user_data)
{
  submit_interface (i);
  return (0);
} /* }}} int handle_interface */

static void cr_submit_gauge (const char *type, /* {{{ */
    const char *type_instance, gauge_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "routeros", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* }}} void cr_submit_gauge */

#if ROS_VERSION >= ROS_VERSION_ENCODE(1, 1, 0)
static void cr_submit_counter (const char *type, /* {{{ */
    const char *type_instance, counter_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = value;

	vl.values = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host)); /* FIXME */
	sstrncpy (vl.plugin, "routeros", sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* }}} void cr_submit_gauge */
#endif

static void submit_regtable (const ros_registration_table_t *r) /* {{{ */
{
  char type_instance[DATA_MAX_NAME_LEN];

  if (r == NULL)
    return;

  /*** RX ***/
  ssnprintf (type_instance, sizeof (type_instance), "%s-%s-rx",
      r->interface, r->radio_name);
  cr_submit_gauge ("bitrate", type_instance,
      (gauge_t) (1000000.0 * r->rx_rate));
  cr_submit_gauge ("signal_power", type_instance,
      (gauge_t) r->rx_signal_strength);
  cr_submit_gauge ("signal_quality", type_instance,
      (gauge_t) r->rx_ccq);

  /*** TX ***/
  ssnprintf (type_instance, sizeof (type_instance), "%s-%s-tx",
      r->interface, r->radio_name);
  cr_submit_gauge ("bitrate", type_instance,
      (gauge_t) (1000000.0 * r->tx_rate));
  cr_submit_gauge ("signal_power", type_instance,
      (gauge_t) r->tx_signal_strength);
  cr_submit_gauge ("signal_quality", type_instance,
      (gauge_t) r->tx_ccq);

  /*** RX / TX ***/
  ssnprintf (type_instance, sizeof (type_instance), "%s-%s",
      r->interface, r->radio_name);
  cr_submit_io ("if_octets", type_instance,
      (counter_t) r->rx_bytes, (counter_t) r->tx_bytes);
  cr_submit_gauge ("snr", type_instance, (gauge_t) r->signal_to_noise);

  submit_regtable (r->next);
} /* }}} void submit_regtable */

static int handle_regtable (__attribute__((unused)) ros_connection_t *c, /* {{{ */
    const ros_registration_table_t *r,
    __attribute__((unused)) void *user_data)
{
  submit_regtable (r);
  return (0);
} /* }}} int handle_regtable */

#if ROS_VERSION >= ROS_VERSION_ENCODE(1, 1, 0) /* FIXME */
static int handle_system_resource (__attribute__((unused)) ros_connection_t *c, /* {{{ */
        const ros_system_resource_t *r,
	__attribute__((unused)) void *user_data)
{
  cr_data_t *rd;

  if ((r == NULL) || (user_data == NULL))
    return (EINVAL);
  rd = user_data;

  if (rd->collect_cpu_load)
    cr_submit_gauge ("gauge", "cpu_load", (gauge_t) r->cpu_load);

  if (rd->collect_memory)
  {
    cr_submit_gauge ("memory", "used",
	(gauge_t) (r->total_memory - r->free_memory));
    cr_submit_gauge ("memory", "free", (gauge_t) r->free_memory);
  }

  if (rd->collect_df)
  {
    cr_submit_gauge ("df_complex", "used",
	(gauge_t) (r->total_memory - r->free_memory));
    cr_submit_gauge ("df_complex", "free", (gauge_t) r->free_memory);
  }

  if (rd->collect_disk)
  {
    cr_submit_counter ("counter", "secors_written", (counter_t) r->write_sect_total);
    cr_submit_gauge ("gauge", "bad_blocks", (gauge_t) r->bad_blocks);
  }

  return (0);
} /* }}} int handle_system_resource */
#endif

static int cr_read (user_data_t *user_data) /* {{{ */
{
  int status;
  cr_data_t *rd;

  if (user_data == NULL)
    return (EINVAL);

  rd = user_data->data;
  if (rd == NULL)
    return (EINVAL);

  if (rd->connection == NULL)
  {
    rd->connection = ros_connect (rd->node, rd->service,
	rd->username, rd->password);
    if (rd->connection == NULL)
    {
      char errbuf[128];
      ERROR ("routeros plugin: ros_connect failed: %s",
	  sstrerror (errno, errbuf, sizeof (errbuf)));
      return (-1);
    }
  }
  assert (rd->connection != NULL);

  if (rd->collect_interface)
  {
    status = ros_interface (rd->connection, handle_interface,
	/* user data = */ NULL);
    if (status != 0)
    {
      char errbuf[128];
      ERROR ("routeros plugin: ros_interface failed: %s",
	  sstrerror (status, errbuf, sizeof (errbuf)));
      ros_disconnect (rd->connection);
      rd->connection = NULL;
      return (-1);
    }
  }

  if (rd->collect_regtable)
  {
    status = ros_registration_table (rd->connection, handle_regtable,
	/* user data = */ NULL);
    if (status != 0)
    {
      char errbuf[128];
      ERROR ("routeros plugin: ros_registration_table failed: %s",
	  sstrerror (status, errbuf, sizeof (errbuf)));
      ros_disconnect (rd->connection);
      rd->connection = NULL;
      return (-1);
    }
  }

#if ROS_VERSION >= ROS_VERSION_ENCODE(1, 1, 0) /* FIXME */
  if (rd->collect_cpu_load
      || rd->collect_memory
      || rd->collect_df
      || rd->collect_disk)
  {
    status = ros_system_resource (rd->connection, handle_system_resource,
	/* user data = */ rd);
    if (status != 0)
    {
      char errbuf[128];
      ERROR ("routeros plugin: ros_system_resource failed: %s",
	  sstrerror (status, errbuf, sizeof (errbuf)));
      ros_disconnect (rd->connection);
      rd->connection = NULL;
      return (-1);
    }
  }
#endif

  return (0);
} /* }}} int cr_read */

static void cr_free_data (cr_data_t *ptr) /* {{{ */
{
  if (ptr == NULL)
    return;

  ros_disconnect (ptr->connection);
  ptr->connection = NULL;

  sfree (ptr->node);
  sfree (ptr->service);
  sfree (ptr->username);
  sfree (ptr->password);

  sfree (ptr);
} /* }}} void cr_free_data */

static int cr_config_router (oconfig_item_t *ci) /* {{{ */
{
  cr_data_t *router_data;
  char read_name[128];
  user_data_t user_data;
  int status;
  int i;

  router_data = malloc (sizeof (*router_data));
  if (router_data == NULL)
    return (-1);
  memset (router_data, 0, sizeof (router_data));
  router_data->connection = NULL;
  router_data->node = NULL;
  router_data->service = NULL;
  router_data->username = NULL;
  router_data->password = NULL;

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Host", child->key) == 0)
      status = cf_util_get_string (child, &router_data->node);
    else if (strcasecmp ("Port", child->key) == 0)
      status = cf_util_get_string (child, &router_data->service);
    else if (strcasecmp ("User", child->key) == 0)
      status = cf_util_get_string (child, &router_data->username);
    else if (strcasecmp ("Password", child->key) == 0)
      status = cf_util_get_string (child, &router_data->password);
    else if (strcasecmp ("CollectInterface", child->key) == 0)
      cf_util_get_boolean (child, &router_data->collect_interface);
    else if (strcasecmp ("CollectRegistrationTable", child->key) == 0)
      cf_util_get_boolean (child, &router_data->collect_regtable);
#if ROS_VERSION >= ROS_VERSION_ENCODE(1, 1, 0) /* FIXME */
    else if (strcasecmp ("CollectCPULoad", child->key) == 0)
      cf_util_get_boolean (child, &router_data->collect_cpu_load);
    else if (strcasecmp ("CollectMemory", child->key) == 0)
      cf_util_get_boolean (child, &router_data->collect_memory);
    else if (strcasecmp ("CollectDF", child->key) == 0)
      cf_util_get_boolean (child, &router_data->collect_df);
    else if (strcasecmp ("CollectDisk", child->key) == 0)
      cf_util_get_boolean (child, &router_data->collect_disk);
#endif
    else
    {
      WARNING ("routeros plugin: Unknown config option `%s'.", child->key);
    }

    if (status != 0)
      break;
  }

  if (status == 0)
  {
    if (router_data->node == NULL)
    {
      ERROR ("routeros plugin: No `Host' option within a `Router' block. "
	  "Where should I connect to?");
      status = -1;
    }

    if (router_data->password == NULL)
    {
      ERROR ("routeros plugin: No `Password' option within a `Router' block. "
	  "How should I authenticate?");
      status = -1;
    }

    if (!router_data->collect_interface
	&& !router_data->collect_regtable)
    {
      ERROR ("routeros plugin: No `Collect*' option within a `Router' block. "
	  "What statistics should I collect?");
      status = -1;
    }
  }

  if ((status == 0) && (router_data->username == NULL))
  {
    router_data->username = sstrdup ("admin");
    if (router_data->username == NULL)
    {
      ERROR ("routeros plugin: sstrdup failed.");
      status = -1;
    }
  }

  ssnprintf (read_name, sizeof (read_name), "routeros/%s", router_data->node);
  user_data.data = router_data;
  user_data.free_func = (void *) cr_free_data;
  if (status == 0)
    status = plugin_register_complex_read (read_name, cr_read,
	/* interval = */ NULL, &user_data);

  if (status != 0)
    cr_free_data (router_data);

  return (status);
} /* }}} int cr_config_router */

static int cr_config (oconfig_item_t *ci)
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Router", child->key) == 0)
      cr_config_router (child);
    else
    {
      WARNING ("routeros plugin: Unknown config option `%s'.", child->key);
    }
  }

  return (0);
} /* }}} int cr_config */

void module_register (void)
{
  plugin_register_complex_config ("routeros", cr_config);
} /* void module_register */

/* vim: set sw=2 noet fdm=marker : */
