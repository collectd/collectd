/**
 * collectd - src/load.c
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

static ros_connection_t *connection = NULL;

static char *conf_node = "router.example.com";
static char *conf_service = NULL;
static char *conf_username = "collectd";
static char *conf_password = "secret";

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

static int cr_read (void) /* {{{ */
{
  int status;

  if (connection == NULL)
  {
    connection = ros_connect (conf_node, conf_service,
	conf_username, conf_password);
    if (connection == NULL)
    {
      char errbuf[128];
      ERROR ("routeros plugin: ros_connect failed: %s",
	  sstrerror (errno, errbuf, sizeof (errbuf)));
      return (-1);
    }
  }
  assert (connection != NULL);

  status = ros_interface (connection, handle_interface,
      /* user data = */ NULL);
  if (status != 0)
  {
    char errbuf[128];
    ERROR ("routeros plugin: ros_interface failed: %s",
	sstrerror (status, errbuf, sizeof (errbuf)));
    ros_disconnect (connection);
    connection = NULL;
    return (-1);
  }

  status = ros_registration_table (connection, handle_regtable,
      /* user data = */ NULL);
  if (status != 0)
  {
    char errbuf[128];
    ERROR ("routeros plugin: ros_registration_table failed: %s",
	sstrerror (status, errbuf, sizeof (errbuf)));
    ros_disconnect (connection);
    connection = NULL;
    return (-1);
  }

  return (0);
} /* }}} int cr_read */

void module_register (void)
{
	plugin_register_read ("routeros", cr_read);
} /* void module_register */

/* vim: set sw=2 noet fdm=marker : */
