/**
 * collectd - src/powerdns.c
 * Copyright (C) 2007-2008  C-Ware, Inc.
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
 *
 * DESCRIPTION
 *      Queries a PowerDNS control socket for statistics
 *
 **/

#include "collectd.h"
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

#define BUFFER_SIZE 1000

#define FUNC_ERROR(func) ERROR ("%s: `%s' failed\n", "powerdns", func)

#define COMMAND_SERVER "SHOW *"
#define COMMAND_RECURSOR "get all-outqueries answers0-1 answers100-1000 answers10-100 answers1-10 answers-slow cache-entries cache-hits cache-misses chain-resends client-parse-errors concurrent-queries dlg-only-drops ipv6-outqueries negcache-entries noerror-answers nsset-invalidations nsspeeds-entries nxdomain-answers outgoing-timeouts qa-latency questions resource-limits server-parse-errors servfail-answers spoof-prevents sys-msec tcp-client-overflow tcp-outqueries tcp-questions throttled-out throttled-outqueries throttle-entries unauthorized-tcp unauthorized-udp unexpected-packets unreachables user-msec"

typedef void item_func (void*);
typedef ssize_t io_func (int, void*, size_t, int);

struct list_item_s
{
  item_func *func;
  char *instance;
  char *command;
  struct sockaddr_un remote;
  struct sockaddr_un local;
};
typedef struct list_item_s list_item_t;

static llist_t *list = NULL;

static void submit (const char *instance, const char *name, const char *value)
{
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[1];
  const data_set_t *ds;
  float f;
  long l;

  ds = plugin_get_ds (name);
  if (ds == NULL)
  {
    ERROR( "%s: DS %s not defined\n", "powerdns", name );
    return;
  }

  errno = 0;
  if (ds->ds->type == DS_TYPE_GAUGE)
  {
    f = atof(value);
    if (errno != 0)
    {
      ERROR ("%s: atof failed (%s->%s)", "powerdns", name, value);
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
      ERROR ("%s: atol failed (%s->%s)", "powerdns", name, value);
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
  strncpy (vl.host, hostname_g, sizeof (vl.host));
  strncpy (vl.plugin, "powerdns", sizeof (vl.plugin));
  strncpy (vl.type_instance, "", sizeof (vl.type_instance));
  strncpy (vl.plugin_instance,instance, sizeof (vl.plugin_instance));

  plugin_dispatch_values (name, &vl);
} /* static void submit */

static int io (io_func *func, int fd, char* buf, int buflen)
{
  int bytes = 0;
  int cc = 1;
  for (; buflen > 0 && (cc = func (fd, buf, buflen, 0)) > 0; 
      buf += cc, bytes += cc, buflen -= cc)
    ;

  return bytes;
} /* static int io */

static void powerdns_read_server (list_item_t *item)
{
  int bytes;
  int sck;
  char *name_token,*value_token,*pos;
  char *buffer;
  char *delims = ",=";

  if ((sck = socket(AF_UNIX, SOCK_STREAM, 0)) < 0)
  {
    FUNC_ERROR ("socket");
    return;
  }

  if (connect( sck,(struct sockaddr *) &item->remote, 
	sizeof(item->remote)) == -1)
  {
    FUNC_ERROR( "connect" );
    close (sck);
    return;
  }

  buffer = malloc (BUFFER_SIZE + 1);
  if (buffer == NULL)
  {
    FUNC_ERROR ("malloc");
    close (sck);
    return;
  }
  strncpy (buffer, 
      item->command == NULL ? COMMAND_SERVER : item->command,
      BUFFER_SIZE);
  buffer[BUFFER_SIZE] = '\0';

  if (io ((io_func*) &send, sck, buffer, strlen(buffer)) < strlen(buffer))
  {
    FUNC_ERROR ("send");
    free (buffer);
    close (sck);
    return;
  }

  bytes = io ((io_func*) &recv, sck, buffer, BUFFER_SIZE);
  if (bytes < 1)
  {
    FUNC_ERROR ("recv");
    free (buffer);
    close (sck);
    return;
  }

  close(sck);

  buffer[bytes] = '\0';

  for (name_token = strtok_r (buffer, delims, &pos),
      value_token = strtok_r (NULL, delims, &pos);
      name_token != NULL && value_token != NULL;
      name_token = strtok_r (NULL, delims, &pos ),
      value_token = strtok_r (NULL, delims, &pos) )
    submit (item->instance, name_token, value_token);

  free (buffer);
  return;
} /* static void powerdns_read_server */

static void powerdns_read_recursor (list_item_t *item) {
  int sck,tmp,bytes;
  char *ptr;
  char *name_token, *name_pos;
  char *value_token, *value_pos;
  char *send_buffer;
  char *recv_buffer;
  char *delims = " \n";	

  for (ptr = item->local.sun_path
      + strlen(item->local.sun_path) - 1;
      ptr > item->local.sun_path && *ptr != '/'; --ptr)
    ;

  if (ptr <= item->local.sun_path)
  {
    ERROR("%s: Bad path %s\n", "powerdns", item->local.sun_path);
    return;
  }

  *ptr='\0';
  strncat (item->local.sun_path, "/lsockXXXXXX",
      sizeof (item->local.sun_path) - strlen (item->local.sun_path));

  if ((sck = socket(AF_UNIX, SOCK_DGRAM, 0)) < 0) {
    FUNC_ERROR ("socket");
    return;
  }

  tmp = 1;
  if (setsockopt (sck, SOL_SOCKET, SO_REUSEADDR, &tmp, sizeof(tmp)) < 0)
  {
    FUNC_ERROR ("setsockopt");
    close (sck);
    return;
  }

  if ((tmp=mkstemp(item->local.sun_path))< 0)
  {
    FUNC_ERROR ("mkstemp");
    close (sck);
    return;
  }
  close (tmp);

  if (unlink(item->local.sun_path) < 0 && errno != ENOENT)
  {
    FUNC_ERROR ("unlink");
    close (sck);
    return;
  }

  if (bind(sck, (struct sockaddr*)&item->local, sizeof(item->local)) < 0)
  {
    FUNC_ERROR ("bind");
    close (sck);
    unlink (item->local.sun_path);
    return;
  }

  if (chmod(item->local.sun_path,0666) < 0)
  {
    FUNC_ERROR ("chmod");
    close (sck);
    unlink (item->local.sun_path);
    return;
  }

  if (connect (sck,(struct sockaddr *) &item->remote, sizeof(item->remote)) == -1)
  {
    FUNC_ERROR ("connect");
    close (sck);
    unlink (item->local.sun_path);
    return;
  }

  send_buffer = strdup (item->command == NULL ? COMMAND_RECURSOR : item->command);
  if (send_buffer == NULL)
  {
    FUNC_ERROR ("strdup");
    close (sck);
    unlink (item->local.sun_path);
    return;
  }

  if (io((io_func*)&send, sck, send_buffer, strlen (send_buffer)) < strlen (send_buffer))
  {
    FUNC_ERROR ("send");
    close (sck);
    unlink (item->local.sun_path);
    free (send_buffer);
    return;
  }

  recv_buffer = malloc (BUFFER_SIZE + 1);
  if (recv_buffer == NULL)
  {
    FUNC_ERROR ("malloc");
    close (sck);
    unlink (item->local.sun_path);
    free (send_buffer);
    return;
  }

  bytes = recv (sck, recv_buffer, BUFFER_SIZE, 0);
  if (bytes < 1) {
    FUNC_ERROR ("recv");
    close (sck);
    unlink (item->local.sun_path);
    free (send_buffer);
    free (recv_buffer);
    return;
  }
  recv_buffer[bytes]='\0';

  close (sck);
  unlink (item->local.sun_path);

  for( name_token = strtok_r (send_buffer, delims, &name_pos),
      name_token = strtok_r (NULL, delims, &name_pos),
      value_token = strtok_r (recv_buffer, delims, &value_pos);
      name_token != NULL && value_token != NULL;
      name_token = strtok_r (NULL, delims, &name_pos),
      value_token = strtok_r (NULL, delims, &value_pos) )
    submit (item->instance, name_token, value_token);

  free (send_buffer);
  free (recv_buffer);
  return;

} /* static void powerdns_read_recursor */

static int powerdns_term() {
  llentry_t *e_this;
  llentry_t *e_next;
  list_item_t *item;

  if (list != NULL)
  {
    for (e_this = llist_head(list); e_this != NULL; e_this = e_next)
    {
      item = e_this->value;
      free (item->instance);

      if (item->command != COMMAND_SERVER &&
	  item->command != COMMAND_RECURSOR)
	free (item->command);

      free (item);

      e_next = e_this->next;
    }

    llist_destroy (list);
    list = NULL;
  }

  return (0);
} /* static int powerdns_term */

static int powerdns_config (oconfig_item_t *ci)
{
  oconfig_item_t *gchild;
  int gchildren;

  oconfig_item_t *child = ci->children;
  int children = ci->children_num;

  llentry_t *entry;
  list_item_t *item;

  if (list == NULL && (list = llist_create()) == NULL )
  {
    ERROR ("powerdns plugin: `llist_create' failed.");
    return 1;
  }		

  for (; children; --children, ++child)
  {
    item = malloc (sizeof (list_item_t));
    if (item == NULL)
    {
      ERROR ("powerdns plugin: `malloc' failed.");
      return 1;
    }

    if (strcmp (child->key, "Server") == 0)
    {
      item->func = (item_func*)&powerdns_read_server;
      item->command = COMMAND_SERVER;
    }
    else if (strcmp (child->key, "Recursor") == 0)
    {
      item->func = (item_func*)&powerdns_read_recursor;
      item->command = COMMAND_RECURSOR;
    }
    else
    {
      WARNING ("powerdns plugin: Ignoring unknown"
	  " config option `%s'.", child->key);
      free (item);
      continue;
    }

    if ((child->values_num != 1) ||
	(child->values[0].type != OCONFIG_TYPE_STRING))
    {
      WARNING ("powerdns plugin: `%s' needs exactly"
	  " one string argument.", child->key);
      free (item);
      continue;
    }

    if (llist_search (list, child->values[0].value.string) != NULL)
    {
      ERROR ("powerdns plugin: multiple instances for %s",
	  child->values[0].value.string);
      free (item);
      return 1;
    }

    item->instance = strdup (child->values[0].value.string);
    if (item->instance == NULL)
    {
      ERROR ("powerdns plugin: `strdup' failed.");
      free (item);
      return 1;
    }

    entry = llentry_create (item->instance, item);
    if (entry == NULL)
    {
      ERROR ("powerdns plugin: `llentry_create' failed.");
      free (item->instance);
      free (item);
      return 1;
    }

    item->remote.sun_family = ~AF_UNIX;

    gchild = child->children;
    gchildren = child->children_num;

    for (; gchildren; --gchildren, ++gchild)
    {
      if (strcmp (gchild->key, "Socket") == 0)
      {
	if (gchild->values_num != 1 || 
	    gchild->values[0].type != OCONFIG_TYPE_STRING)
	{
	  WARNING ("powerdns plugin: config option `%s'"
	      " should have exactly one string value.",
	      gchild->key);
	  continue;
	}
	if (item->remote.sun_family == AF_UNIX)
	{
	  WARNING ("powerdns plugin: ignoring extraneous"
	      " `%s' config option.", gchild->key);
	  continue;
	}
	item->remote.sun_family = item->local.sun_family = AF_UNIX;
	strncpy (item->remote.sun_path, gchild->values[0].value.string,
	    sizeof (item->remote.sun_path));
	strncpy (item->local.sun_path, gchild->values[0].value.string,
	    sizeof (item->remote.sun_path));
      }
      else if (strcmp (gchild->key, "Command") == 0)
      {
	if (gchild->values_num != 1 
	    || gchild->values[0].type != OCONFIG_TYPE_NUMBER)
	{
	  WARNING ("powerdns plugin: config option `%s'"
	      " should have exactly one string value.",
	      gchild->key);
	  continue;
	}
	if (item->command != COMMAND_RECURSOR &&
	    item->command != COMMAND_SERVER)
	{
	  WARNING ("powerdns plugin: ignoring extraneous"
	      " `%s' config option.", gchild->key);
	  continue;
	}
	item->command = strdup (gchild->values[0].value.string);
	if (item->command == NULL)
	{
	  ERROR ("powerdns plugin: `strdup' failed.");
	  llentry_destroy (entry);
	  free (item->instance);
	  free (item);
	  return 1;
	}
      }
      else
      {
	WARNING ("powerdns plugin: Ignoring unknown config option"
	    " `%s'.", gchild->key);
	continue;
      }

      if (gchild->children_num)
      {
	WARNING ("powerdns plugin: config option `%s' should not"
	    " have children.", gchild->key);
      }
    }


    if (item->remote.sun_family != AF_UNIX)
    {
      if (item->func == (item_func*)&powerdns_read_server)
      {
	item->remote.sun_family = item->local.sun_family = AF_UNIX;
	strncpy (item->remote.sun_path, "/var/run/pdns.controlsocket",
	    sizeof (item->remote.sun_path));
	strncpy (item->local.sun_path, "/var/run/pdns.controlsocket",
	    sizeof (item->remote.sun_path));
      }
      else
      {
	item->remote.sun_family = item->local.sun_family = AF_UNIX;
	strncpy (item->remote.sun_path, "/var/run/pdns_recursor.controlsocket",
	    sizeof (item->remote.sun_path));
	strncpy (item->local.sun_path, "/var/run/pdns_recursor.controlsocket",
	    sizeof (item->remote.sun_path));
      }
    }

    llist_append (list, entry);
  }

  return 0;
} /* static int powerdns_config */

static int powerdns_read(void)
{
  llentry_t *e_this;
  list_item_t *item;

  for (e_this = llist_head(list); e_this != NULL; e_this = e_this->next)
  {
    item = e_this->value;
    item->func(item);
  }

  return (0);
} /* static int powerdns_read */

void module_register (void)
{
  plugin_register_complex_config ("powerdns", powerdns_config);
  plugin_register_read ("powerdns", powerdns_read);
  plugin_register_shutdown ("powerdns", powerdns_term );
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 : */
