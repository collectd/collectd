/**
 * collectd - src/tcpconns.c
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
 * Author:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

#define PORT_COLLECT_LOCAL  0x01
#define PORT_COLLECT_REMOTE 0x02
#define PORT_IS_LISTENING   0x04

typedef struct port_entry_s
{
  uint16_t port;
  uint16_t flags;
  uint32_t count_local;
  uint32_t count_remote;
  struct port_entry_s *next;
} port_entry_t;

static const char *config_keys[] =
{
  "ListeningPorts",
  "LocalPort",
  "RemotePort"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int port_collect_listening = 0;
static port_entry_t *port_list_head = NULL;

static void conn_submit_port_entry (port_entry_t *pe)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = 1;
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, "tcpconns");
  snprintf (vl.type_instance, sizeof (vl.type_instance), "%hu", pe->port);
  vl.type_instance[sizeof (vl.type_instance) - 1] = '\0';

  if (((port_collect_listening != 0) && (pe->flags & PORT_IS_LISTENING))
      || (pe->flags & PORT_COLLECT_LOCAL))
  {
    values[0].gauge = pe->count_local;
    strcpy (vl.plugin_instance, "local");
    plugin_dispatch_values ("tcp_connections", &vl);
  }
  if (pe->flags & PORT_COLLECT_REMOTE)
  {
    values[0].gauge = pe->count_remote;
    strcpy (vl.plugin_instance, "remote");
    plugin_dispatch_values ("tcp_connections", &vl);
  }
} /* void conn_submit */

static void conn_submit_all (void)
{
  port_entry_t *pe;

  for (pe = port_list_head; pe != NULL; pe = pe->next)
    conn_submit_port_entry (pe);
} /* void conn_submit_all */

static port_entry_t *conn_get_port_entry (uint16_t port, int create)
{
  port_entry_t *ret;

  ret = port_list_head;
  while (ret != NULL)
  {
    if (ret->port == port)
      break;
    ret = ret->next;
  }

  if ((ret == NULL) && (create != 0))
  {
    ret = (port_entry_t *) malloc (sizeof (port_entry_t));
    if (ret == NULL)
      return (NULL);
    memset (ret, '\0', sizeof (port_entry_t));

    ret->port = port;
    ret->next = port_list_head;
    port_list_head = ret;
  }

  return (ret);
} /* port_entry_t *conn_get_port_entry */

static void conn_reset_port_entry (void)
{
  port_entry_t *prev = NULL;
  port_entry_t *pe = port_list_head;

  while (pe != NULL)
  {
    /* If this entry was created while reading the files (ant not when handling
     * the configuration) remove it now. */
    if ((pe->flags & (PORT_COLLECT_LOCAL | PORT_COLLECT_REMOTE)) == 0)
    {
      port_entry_t *next = pe->next;

      DEBUG ("tcpconns plugin: Removing temporary entry "
	  "for listening port %hu", pe->port);

      if (prev == NULL)
	port_list_head = next;
      else
	prev->next = next;

      sfree (pe);
      pe = next;

      continue;
    }

    pe->count_local = 0;
    pe->count_remote = 0;

    pe->flags &= ~PORT_IS_LISTENING;

    pe = pe->next;
  }
} /* void conn_reset_port_entry */

static int conn_handle_ports (uint16_t port_local, uint16_t port_remote, uint8_t state)
{
  /* Listening sockets */
  if ((state == 0x0a) && (port_collect_listening != 0))
  {
    port_entry_t *pe;

    DEBUG ("tcpconns plugin: Adding listening port %hu", port_local);

    pe = conn_get_port_entry (port_local, 1 /* create */);
    if (pe != NULL)
    {
      pe->count_local++;
      pe->flags |= PORT_IS_LISTENING;
    }
  }
  /* Established connections */
  else if (state == 0x01)
  {
    port_entry_t *pe;

    DEBUG ("tcpconns plugin: Established connection %hu <-> %hu",
	port_local, port_remote);
    
    pe = conn_get_port_entry (port_local, 0 /* no create */);
    if ((pe != NULL) && (pe->flags & PORT_COLLECT_LOCAL))
      pe->count_local++;

    pe = conn_get_port_entry (port_remote, 0 /* no create */);
    if ((pe != NULL) && (pe->flags & PORT_COLLECT_REMOTE))
      pe->count_remote++;
  }
  else
  {
    DEBUG ("tcpconns plugin: Ignoring unknown state 0x%x", state);
  }

  return (0);
} /* int conn_handle_ports */

static int conn_handle_line (char *buffer)
{
  char *fields[32];
  int   fields_len;

  char *endptr;

  char *port_local_str;
  char *port_remote_str;
  uint16_t port_local;
  uint16_t port_remote;

  uint8_t state;

  int buffer_len = strlen (buffer);

  while ((buffer_len > 0) && (buffer[buffer_len - 1] < 32))
    buffer[--buffer_len] = '\0';
  if (buffer_len <= 0)
    return (-1);

  fields_len = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
  if (fields_len < 12)
  {
    DEBUG ("tcpconns plugin: Got %i fields, expected at least 12.", fields_len);
    return (-1);
  }

  port_local_str  = strchr (fields[1], ':');
  port_remote_str = strchr (fields[2], ':');

  if ((port_local_str == NULL) || (port_remote_str == NULL))
    return (-1);
  port_local_str++;
  port_remote_str++;
  if ((*port_local_str == '\0') || (*port_remote_str == '\0'))
    return (-1);

  endptr = NULL;
  port_local = (uint16_t) strtol (port_local_str, &endptr, 16);
  if ((endptr == NULL) || (*endptr != '\0'))
    return (-1);

  endptr = NULL;
  port_remote = (uint16_t) strtol (port_remote_str, &endptr, 16);
  if ((endptr == NULL) || (*endptr != '\0'))
    return (-1);

  endptr = NULL;
  state = (uint8_t) strtol (fields[3], &endptr, 16);
  if ((endptr == NULL) || (*endptr != '\0'))
    return (-1);

  return (conn_handle_ports (port_local, port_remote, state));
} /* int conn_handle_line */

static int conn_read_file (const char *file)
{
  FILE *fh;
  char buffer[1024];

  fh = fopen (file, "r");
  if (fh == NULL)
  {
    char errbuf[1024];
    ERROR ("tcpconns plugin: fopen (%s) failed: %s",
	file, sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  while (fgets (buffer, sizeof (buffer), fh) != NULL)
  {
    conn_handle_line (buffer);
  } /* while (fgets) */

  fclose (fh);

  return (0);
} /* int conn_read_file */

static int conn_config (const char *key, const char *value)
{
  if (strcasecmp (key, "ListeningPorts") == 0)
  {
    if ((strcasecmp (value, "Yes") == 0)
	|| (strcasecmp (value, "True") == 0)
	|| (strcasecmp (value, "On") == 0))
      port_collect_listening = 1;
    else
      port_collect_listening = 0;
  }
  else if ((strcasecmp (key, "LocalPort") == 0)
      || (strcasecmp (key, "RemotePort") == 0))
  {
      port_entry_t *pe;
      int port = atoi (value);

      if ((port < 1) || (port > 65535))
      {
	ERROR ("tcpconns plugin: Invalid port: %i", port);
	return (1);
      }

      pe = conn_get_port_entry ((uint16_t) port, 1 /* create */);
      if (pe == NULL)
      {
	ERROR ("tcpconns plugin: conn_get_port_entry failed.");
	return (1);
      }

      if (strcasecmp (key, "LocalPort") == 0)
	pe->flags |= PORT_COLLECT_LOCAL;
      else
	pe->flags |= PORT_COLLECT_REMOTE;
  }
  else
  {
    return (-1);
  }

  return (0);
} /* int conn_config */

static int conn_init (void)
{
  if (port_list_head == NULL)
    port_collect_listening = 1;

  return (0);
} /* int conn_init */

static int conn_read (void)
{
  conn_reset_port_entry ();

  conn_read_file ("/proc/net/tcp");
  conn_read_file ("/proc/net/tcp6");

  conn_submit_all ();

  return (0);
} /* int conn_read */

void module_register (void)
{
	plugin_register_config ("tcpconns", conn_config,
			config_keys, config_keys_num);
	plugin_register_init ("tcpconns", conn_init);
	plugin_register_read ("tcpconns", conn_read);
} /* void module_register */

/*
 * vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
 */
