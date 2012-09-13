/**
 * collectd - src/nut.c
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
#include "common.h"
#include "plugin.h"

#include <pthread.h>
#include <upsclient.h>

#if HAVE_UPSCONN_T
typedef UPSCONN_t collectd_upsconn_t;
#elif HAVE_UPSCONN
typedef UPSCONN collectd_upsconn_t;
#else
# error "Unable to determine the UPS connection type."
#endif

struct nut_ups_s;
typedef struct nut_ups_s nut_ups_t;
struct nut_ups_s
{
  collectd_upsconn_t *conn;
  char      *upsname;
  char      *hostname;
  int        port;
  nut_ups_t *next;
};

static nut_ups_t *upslist_head = NULL;

static pthread_mutex_t read_lock = PTHREAD_MUTEX_INITIALIZER;
static int read_busy = 0;

static const char *config_keys[] =
{
  "UPS"
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static void free_nut_ups_t (nut_ups_t *ups)
{
  if (ups->conn != NULL)
  {
    upscli_disconnect (ups->conn);
    sfree (ups->conn);
  }
  sfree (ups->hostname);
  sfree (ups->upsname);
  sfree (ups);
} /* void free_nut_ups_t */

static int nut_add_ups (const char *name)
{
  nut_ups_t *ups;
  int status;

  DEBUG ("nut plugin: nut_add_ups (name = %s);", name);

  ups = (nut_ups_t *) malloc (sizeof (nut_ups_t));
  if (ups == NULL)
  {
    ERROR ("nut plugin: nut_add_ups: malloc failed.");
    return (1);
  }
  memset (ups, '\0', sizeof (nut_ups_t));

  status = upscli_splitname (name, &ups->upsname, &ups->hostname,
      &ups->port);
  if (status != 0)
  {
    ERROR ("nut plugin: nut_add_ups: upscli_splitname (%s) failed.", name);
    free_nut_ups_t (ups);
    return (1);
  }

  if (upslist_head == NULL)
    upslist_head = ups;
  else
  {
    nut_ups_t *last = upslist_head;
    while (last->next != NULL)
      last = last->next;
    last->next = ups;
  }

  return (0);
} /* int nut_add_ups */

static int nut_config (const char *key, const char *value)
{
  if (strcasecmp (key, "UPS") == 0)
    return (nut_add_ups (value));
  else
    return (-1);
} /* int nut_config */

static void nut_submit (nut_ups_t *ups, const char *type,
    const char *type_instance, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE (values);
  sstrncpy (vl.host,
      (strcasecmp (ups->hostname, "localhost") == 0)
      ? hostname_g
      : ups->hostname,
      sizeof (vl.host));
  sstrncpy (vl.plugin, "nut", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, ups->upsname, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));
  sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void nut_submit */

static int nut_read_one (nut_ups_t *ups)
{
  const char *query[3] = {"VAR", ups->upsname, NULL};
  unsigned int query_num = 2;
  char **answer;
  unsigned int answer_num;
  int status;

  /* (Re-)Connect if we have no connection */
  if (ups->conn == NULL)
  {
    ups->conn = (collectd_upsconn_t *) malloc (sizeof (collectd_upsconn_t));
    if (ups->conn == NULL)
    {
      ERROR ("nut plugin: malloc failed.");
      return (-1);
    }

    status = upscli_connect (ups->conn, ups->hostname, ups->port,
	UPSCLI_CONN_TRYSSL);
    if (status != 0)
    {
      ERROR ("nut plugin: nut_read_one: upscli_connect (%s, %i) failed: %s",
	  ups->hostname, ups->port, upscli_strerror (ups->conn));
      sfree (ups->conn);
      return (-1);
    }

    INFO ("nut plugin: Connection to (%s, %i) established.",
	ups->hostname, ups->port);
  } /* if (ups->conn == NULL) */

  /* nut plugin: nut_read_one: upscli_list_start (adpos) failed: Protocol
   * error */
  status = upscli_list_start (ups->conn, query_num, query);
  if (status != 0)
  {
    ERROR ("nut plugin: nut_read_one: upscli_list_start (%s) failed: %s",
	ups->upsname, upscli_strerror (ups->conn));
    upscli_disconnect (ups->conn);
    sfree (ups->conn);
    return (-1);
  }

  while ((status = upscli_list_next (ups->conn, query_num, query,
	  &answer_num, &answer)) == 1)
  {
    char  *key;
    double value;

    if (answer_num < 4)
      continue;

    key = answer[2];
    value = atof (answer[3]);

    if (strncmp ("ambient.", key, 8) == 0)
    {
      if (strcmp ("ambient.humidity", key) == 0)
	nut_submit (ups, "humidity", "ambient", value);
      else if (strcmp ("ambient.temperature", key) == 0)
	nut_submit (ups, "temperature", "ambient", value);
    }
    else if (strncmp ("battery.", key, 8) == 0)
    {
      if (strcmp ("battery.charge", key) == 0)
	nut_submit (ups, "percent", "charge", value);
      else if (strcmp ("battery.current", key) == 0)
	nut_submit (ups, "current", "battery", value);
      else if (strcmp ("battery.runtime", key) == 0)
	nut_submit (ups, "timeleft", "battery", value);
      else if (strcmp ("battery.temperature", key) == 0)
	nut_submit (ups, "temperature", "battery", value);
      else if (strcmp ("battery.voltage", key) == 0)
	nut_submit (ups, "voltage", "battery", value);
    }
    else if (strncmp ("input.", key, 6) == 0)
    {
      if (strcmp ("input.frequency", key) == 0)
	nut_submit (ups, "frequency", "input", value);
      else if (strcmp ("input.voltage", key) == 0)
	nut_submit (ups, "voltage", "input", value);
    }
    else if (strncmp ("output.", key, 7) == 0)
    {
      if (strcmp ("output.current", key) == 0)
	nut_submit (ups, "current", "output", value);
      else if (strcmp ("output.frequency", key) == 0)
	nut_submit (ups, "frequency", "output", value);
      else if (strcmp ("output.voltage", key) == 0)
	nut_submit (ups, "voltage", "output", value);
    }
    else if (strncmp ("ups.", key, 4) == 0)
    {
      if (strcmp ("ups.load", key) == 0)
	nut_submit (ups, "percent", "load", value);
      else if (strcmp ("ups.power", key) == 0)
	nut_submit (ups, "power", "ups", value);
      else if (strcmp ("ups.temperature", key) == 0)
	nut_submit (ups, "temperature", "ups", value);
    }
  } /* while (upscli_list_next) */

  return (0);
} /* int nut_read_one */

static int nut_read (void)
{
  nut_ups_t *ups;
  int success = 0;

  pthread_mutex_lock (&read_lock);
  success = read_busy;
  read_busy = 1;
  pthread_mutex_unlock (&read_lock);

  if (success != 0)
    return (0);

  for (ups = upslist_head; ups != NULL; ups = ups->next)
    if (nut_read_one (ups) == 0)
      success++;

  pthread_mutex_lock (&read_lock);
  read_busy = 0;
  pthread_mutex_unlock (&read_lock);

  return ((success != 0) ? 0 : -1);
} /* int nut_read */

static int nut_shutdown (void)
{
  nut_ups_t *this;
  nut_ups_t *next;

  this = upslist_head;
  while (this != NULL)
  {
    next = this->next;
    free_nut_ups_t (this);
    this = next;
  }

  return (0);
} /* int nut_shutdown */

void module_register (void)
{
  plugin_register_config ("nut", nut_config, config_keys, config_keys_num);
  plugin_register_read ("nut", nut_read);
  plugin_register_shutdown ("nut", nut_shutdown);
} /* void module_register */

/* vim: set sw=2 ts=8 sts=2 tw=78 : */
