/**
 * collectd - src/powerdns.c
 * Copyright (C) 2007-2008  C-Ware, Inc.
 * Copyright (C) 2008       Florian Forster
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
 *   Luke Heberling <lukeh at c-ware.com>
 *   Florian Forster <octo at verplant.org>
 *
 * DESCRIPTION
 *      Queries a PowerDNS control socket for statistics
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_llist.h"

#include <sys/stat.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <malloc.h>

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX sizeof (((struct sockaddr_un *)0)->sun_path)
#endif
#define FUNC_ERROR(func) ERROR ("powerdns plugin: `%s' failed\n", func)

#define SERVER_SOCKET  "/var/run/pdns.controlsocket"
#define SERVER_COMMAND "SHOW *"

#define RECURSOR_SOCKET  "/var/run/pdns_recursor.controlsocket"
#define RECURSOR_COMMAND "get all-outqueries answers0-1 answers100-1000 answers10-100 answers1-10 answers-slow cache-entries cache-hits cache-misses chain-resends client-parse-errors concurrent-queries dlg-only-drops ipv6-outqueries negcache-entries noerror-answers nsset-invalidations nsspeeds-entries nxdomain-answers outgoing-timeouts qa-latency questions resource-limits server-parse-errors servfail-answers spoof-prevents sys-msec tcp-client-overflow tcp-outqueries tcp-questions throttled-out throttled-outqueries throttle-entries unauthorized-tcp unauthorized-udp unexpected-packets unreachables user-msec"

struct list_item_s;
typedef struct list_item_s list_item_t;

struct list_item_s
{
  int (*func) (list_item_t *item);
  char *instance;
  char *command;
  struct sockaddr_un sockaddr;
  int socktype;
};

static llist_t *list = NULL;

static void submit (const char *plugin_instance, const char *type, const char *value)
{
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[1];
  const data_set_t *ds;
  float f;
  long l;

  ERROR ("powerdns plugin: submit: TODO: Translate the passed-in `key' to a reasonable type (and type_instance).");

  ds = plugin_get_ds (type);
  if (ds == NULL)
  {
    ERROR( "%s: DS %s not defined\n", "powerdns", type );
    return;
  }

  errno = 0;
  if (ds->ds->type == DS_TYPE_GAUGE)
  {
    f = atof(value);
    if (errno != 0)
    {
      ERROR ("%s: atof failed (%s->%s)", "powerdns", type, value);
      return;
    }
    else
    {
      values[0].gauge = f<0?-f:f;
    }
  }
  else
  {
    l = atol(value);
    if (errno != 0)
    {
      ERROR ("%s: atol failed (%s->%s)", "powerdns", type, value);
      return;
    }
    else
    {
      values[0].counter = l < 0 ? -l : l;
    }
  }

  vl.values = values;
  vl.values_len = 1;
  vl.time = time (NULL);
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "powerdns", sizeof (vl.plugin));
  sstrncpy (vl.type_instance, "", sizeof (vl.type_instance));
  sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));

  plugin_dispatch_values (type, &vl);
} /* static void submit */

static int powerdns_get_data (list_item_t *item, char **ret_buffer,
    size_t *ret_buffer_size)
{
  int sd;
  int status;

  char temp[1024];
  char *buffer = NULL;
  size_t buffer_size = 0;

  sd = socket (AF_UNIX, item->socktype, 0);
  if (sd < 0)
  {
    FUNC_ERROR ("socket");
    return (-1);
  }

  status = connect (sd, (struct sockaddr *) &item->sockaddr,
      sizeof(item->sockaddr));
  if (status != 0)
  {
    FUNC_ERROR ("connect");
    close (sd);
    return (-1);
  }

  status = send (sd, item->command, strlen (item->command), 0);
  if (status < 0)
  {
    FUNC_ERROR ("send");
    close (sd);
    return (-1);
  }

  while (42)
  {
    char *buffer_new;

    status = recv (sd, temp, sizeof (temp), 0);
    if (status < 0)
    {
      FUNC_ERROR ("recv");
      break;
    }
    else if (status == 0)
      break;

    buffer_new = (char *) realloc (buffer, buffer_size + status);
    if (buffer_new == NULL)
    {
      FUNC_ERROR ("realloc");
      status = -1;
      break;
    }
    buffer = buffer_new;

    memcpy (buffer + buffer_size, temp, status);
    buffer_size += status;
  }
  close (sd);
  sd = -1;

  if (status < 0)
  {
    sfree (buffer);
  }
  else
  {
    *ret_buffer = buffer;
    *ret_buffer_size = buffer_size;
  }

  return (status);
} /* int powerdns_get_data */

static int powerdns_read_server (list_item_t *item)
{
  char *buffer = NULL;
  size_t buffer_size = 0;
  int status;

  char *dummy;
  char *saveptr;

  char *key;
  char *value;

  status = powerdns_get_data (item, &buffer, &buffer_size);
  if (status != 0)
    return (-1);

  dummy = buffer;
  saveptr = NULL;
  while ((key = strtok_r (dummy, ",", &saveptr)) != NULL)
  {
    dummy = NULL;

    value = strchr (key, '=');
    if (value == NULL)
      break;

    *value = '\0';
    value++;

    if (value[0] == '\0')
      continue;

    submit (item->instance, key, value);
  } /* while (strtok_r) */

  sfree (buffer);

  return (0);
} /* int powerdns_read_server */

static int powerdns_read_recursor (list_item_t *item)
{
  char *buffer = NULL;
  size_t buffer_size = 0;
  int status;

  char *dummy;

  char *keys_list;
  char *key;
  char *key_saveptr;
  char *value;
  char *value_saveptr;

  status = powerdns_get_data (item, &buffer, &buffer_size);
  if (status != 0)
    return (-1);

  keys_list = strdup (item->command);
  if (keys_list == NULL)
  {
    FUNC_ERROR ("strdup");
    sfree (buffer);
    return (-1);
  }

  key_saveptr = NULL;
  value_saveptr = NULL;

  /* Skip the `get' at the beginning */
  strtok_r (keys_list, " \t", &key_saveptr);

  dummy = buffer;
  while ((value = strtok_r (dummy, " \t\n\r", &value_saveptr)) != NULL)
  {
    dummy = NULL;

    key = strtok_r (NULL, " \t", &key_saveptr);
    if (key == NULL)
      break;

    submit (item->instance, key, value);
  } /* while (strtok_r) */

  sfree (buffer);
  sfree (keys_list);

  return (0);
} /* int powerdns_read_recursor */

static int powerdns_config_add_string (const char *name, char **dest,
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("powerdns plugin: `%s' needs exactly one string argument.",
	name);
    return (-1);
  }

  sfree (*dest);
  *dest = strdup (ci->values[0].value.string);
  if (*dest == NULL)
    return (-1);

  return (0);
} /* int ctail_config_add_string */

static int powerdns_config_add_server (oconfig_item_t *ci)
{
  char *socket_temp;

  list_item_t *item;
  int status;
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("powerdns plugin: `%s' needs exactly one string argument.",
	ci->key);
    return (-1);
  }

  item = (list_item_t *) malloc (sizeof (list_item_t));
  if (item == NULL)
  {
    ERROR ("powerdns plugin: malloc failed.");
    return (-1);
  }
  memset (item, '\0', sizeof (list_item_t));

  item->instance = strdup (ci->values[0].value.string);
  if (item->instance == NULL)
  {
    ERROR ("powerdns plugin: strdup failed.");
    sfree (item);
    return (-1);
  }

  /*
   * Set default values for the members of list_item_t
   */
  if (strcasecmp ("Server", ci->key) == 0)
  {
    item->func = powerdns_read_server;
    item->command = strdup (SERVER_COMMAND);
    item->socktype = SOCK_STREAM;
    socket_temp = strdup (SERVER_SOCKET);
  }
  else if (strcasecmp ("Recursor", ci->key) == 0)
  {
    item->func = powerdns_read_recursor;
    item->command = strdup (RECURSOR_COMMAND);
    item->socktype = SOCK_DGRAM;
    socket_temp = strdup (RECURSOR_SOCKET);
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Command", option->key) == 0)
      status = powerdns_config_add_string ("Command", &item->command, option);
    else if (strcasecmp ("Socket", option->key) == 0)
      status = powerdns_config_add_string ("Socket", &socket_temp, option);
    else
    {
      ERROR ("powerdns plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  while (status == 0)
  {
    llentry_t *e;

    if (socket_temp == NULL)
    {
      ERROR ("powerdns plugin: socket_temp == NULL.");
      status = -1;
      break;
    }

    if (item->command == NULL)
    {
      ERROR ("powerdns plugin: item->command == NULL.");
      status = -1;
      break;
    }

    item->sockaddr.sun_family = AF_UNIX;
    sstrncpy (item->sockaddr.sun_path, socket_temp, UNIX_PATH_MAX);

    e = llentry_create (item->instance, item);
    if (e == NULL)
    {
      ERROR ("powerdns plugin: llentry_create failed.");
      status = -1;
      break;
    }
    llist_append (list, e);

    break;
  }

  if (status != 0)
  {
    sfree (item);
    return (-1);
  }

  return (0);
} /* int powerdns_config_add_server */

static int powerdns_config (oconfig_item_t *ci)
{
  int i;

  if (list == NULL)
  {
    list = llist_create ();

    if (list == NULL)
    {
      ERROR ("powerdns plugin: `llist_create' failed.");
      return (-1);
    }
  }

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if ((strcasecmp ("Server", ci->key) == 0)
	|| (strcasecmp ("Recursor", ci->key) == 0))
      powerdns_config_add_server (option);
    else
    {
      ERROR ("powerdns plugin: Option `%s' not allowed here.", option->key);
    }
  } /* for (i = 0; i < ci->children_num; i++) */

  return (0);
} /* int powerdns_config */

static int powerdns_read (void)
{
  llentry_t *e;

  for (e = llist_head (list); e != NULL; e = e->next)
  {
    list_item_t *item = e->value;
    item->func (item);
  }

  return (0);
} /* static int powerdns_read */

static int powerdns_shutdown (void)
{
  llentry_t *e;

  if (list == NULL)
    return (0);

  for (e = llist_head (list); e != NULL; e = e->next)
  {
    list_item_t *item = (list_item_t *) e->value;
    e->value = NULL;

    sfree (item->instance);
    sfree (item->command);
    sfree (item);
  }

  llist_destroy (list);
  list = NULL;

  return (0);
} /* static int powerdns_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("powerdns", powerdns_config);
  plugin_register_read ("powerdns", powerdns_read);
  plugin_register_shutdown ("powerdns", powerdns_shutdown );
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 : */
