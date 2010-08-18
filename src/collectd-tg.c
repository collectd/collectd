/**
 * collectd - src/collectd-tg.c
 * Copyright (C) 2010  Florian octo Forster
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
 *   Florian Forster <ff at octo.it>
 **/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "utils_heap.h"

#include "libcollectdclient/collectd/client.h"
#include "libcollectdclient/collectd/network.h"
#include "libcollectdclient/collectd/network_buffer.h"

static int conf_num_hosts = 1000;
static int conf_num_plugins = 20;
static int conf_num_values = 100000;

static lcc_network_t *net;

static c_heap_t *values_heap = NULL;

static int compare_time (const void *v0, const void *v1) /* {{{ */
{
  lcc_value_list_t * const *vl0 = v0;
  lcc_value_list_t * const *vl1 = v1;

  if ((*vl0)->time < (*vl1)->time)
    return (-1);
  else if ((*vl0)->time > (*vl1)->time)
    return (1);
  else
    return (lcc_identifier_compare (&(*vl0)->identifier, /* Ouch, somebody */
          &(*vl1)->identifier));          /* is going to hate me for this. */
} /* }}} int compare_time */

static int get_boundet_random (int min, int max) /* {{{ */
{
  int range;

  if (min >= max)
    return (-1);
  if (min == (max - 1))
    return (min);

  range = max - min;

  return (min + ((int) (((double) range) * ((double) random ()) / (((double) RAND_MAX) + 1.0))));
} /* }}} int get_boundet_random */

#if 0
static int dump_network_buffer (void) /* {{{ */
{
  char buffer[LCC_NETWORK_BUFFER_SIZE_DEFAULT];
  size_t buffer_size;
  int status;
  size_t offset;

  memset (buffer, 0, sizeof (buffer));
  buffer_size = sizeof (buffer);

  status = lcc_network_buffer_get (nb, buffer, &buffer_size);
  if (status != 0)
  {
    fprintf (stderr, "lcc_network_buffer_get failed with status %i.\n",
        status);
    return (status);
  }

  if (buffer_size > sizeof (buffer))
    buffer_size = sizeof (buffer);

  for (offset = 0; offset < buffer_size; offset += 16)
  {
    size_t i;

    for (i = 0; (i < 16) && ((offset + i) < buffer_size); i++)
    {
      uint8_t v = (uint8_t) buffer[offset + i];
      printf ("%02"PRIx8" ", v);
    }
    for (; i < 16; i++)
      printf ("   ");
    printf ("   ");
    for (i = 0; (i < 16) && ((offset + i) < buffer_size); i++)
    {
      uint8_t v = (uint8_t) buffer[offset + i];
      if ((v >= 32) && (v < 128))
        printf ("%c", (int) buffer[offset + i]);
      else
        printf (".");
    }
    printf ("\n");
  }

  return (0);
} /* }}} int dump_network_buffer */
#endif

static lcc_value_list_t *create_value_list (void) /* {{{ */
{
  lcc_value_list_t *vl;
  int host_num;

  vl = malloc (sizeof (*vl));
  if (vl == NULL)
  {
    fprintf (stderr, "malloc failed.\n");
    return (NULL);
  }
  memset (vl, 0, sizeof (*vl));

  vl->values = calloc (/* nmemb = */ 1, sizeof (*vl->values));
  if (vl->values == NULL)
  {
    fprintf (stderr, "calloc failed.\n");
    free (vl);
    return (NULL);
  }

  vl->values_types = calloc (/* nmemb = */ 1, sizeof (*vl->values_types));
  if (vl->values_types == NULL)
  {
    fprintf (stderr, "calloc failed.\n");
    free (vl->values);
    free (vl);
    return (NULL);
  }

  vl->values_len = 1;

  host_num = get_boundet_random (0, conf_num_hosts);

  vl->interval = 10;
  vl->time = time (NULL) - (host_num % vl->interval);

  if (get_boundet_random (0, 2) == 0)
    vl->values_types[0] = LCC_TYPE_GAUGE;
  else
    vl->values_types[0] = LCC_TYPE_DERIVE;

  snprintf (vl->identifier.host, sizeof (vl->identifier.host),
      "host%04i", host_num);
  snprintf (vl->identifier.plugin, sizeof (vl->identifier.plugin),
      "plugin%03i", get_boundet_random (0, conf_num_plugins));
  strncpy (vl->identifier.type,
      (vl->values_types[0] == LCC_TYPE_GAUGE) ? "gauge" : "derive",
      sizeof (vl->identifier.type));
  snprintf (vl->identifier.type_instance, sizeof (vl->identifier.type_instance),
      "ti%li", random ());

  return (vl);
} /* }}} int create_value_list */

static void destroy_value_list (lcc_value_list_t *vl) /* {{{ */
{
  if (vl == NULL)
    return;

  free (vl->values);
  free (vl->values_types);
  free (vl);
} /* }}} void destroy_value_list */

static int send_value (lcc_value_list_t *vl) /* {{{ */
{
  int status;

  if (vl->values_types[0] == LCC_TYPE_GAUGE)
    vl->values[0].gauge = 100.0 * ((gauge_t) random ()) / (((gauge_t) RAND_MAX) + 1.0);
  else
    vl->values[0].derive += get_boundet_random (0, 100);

  status = lcc_network_values_send (net, vl);
  if (status != 0)
    fprintf (stderr, "lcc_network_values_send failed with status %i.\n", status);

  vl->time += vl->interval;

  return (0);
} /* }}} int send_value */

int main (int argc, char **argv) /* {{{ */
{
  int i;

  values_heap = c_heap_create (compare_time);
  if (values_heap == NULL)
  {
    fprintf (stderr, "c_heap_create failed.\n");
    exit (EXIT_FAILURE);
  }

  net = lcc_network_create ();
  if (net == NULL)
  {
    fprintf (stderr, "lcc_network_create failed.\n");
    exit (EXIT_FAILURE);
  }
  else
  {
    lcc_server_t *srv;
    
    srv = lcc_server_create (net, NET_DEFAULT_V6_ADDR, NET_DEFAULT_PORT);
    if (srv == NULL)
    {
      fprintf (stderr, "lcc_server_create failed.\n");
      exit (EXIT_FAILURE);
    }

    lcc_server_set_ttl (srv, 42);
#if 0
    lcc_server_set_security_level (srv, ENCRYPT,
        "admin", "password1");
#endif
  }

  fprintf (stdout, "Creating %i values ... ", conf_num_values);
  fflush (stdout);
  for (i = 0; i < conf_num_values; i++)
  {
    lcc_value_list_t *vl;

    vl = create_value_list ();
    if (vl == NULL)
    {
      fprintf (stderr, "create_value_list failed.\n");
      exit (EXIT_FAILURE);
    }

    c_heap_insert (values_heap, vl);
  }
  fprintf (stdout, "done\n");

  while (42)
  {
    lcc_value_list_t *vl = c_heap_get_root (values_heap);

    if (vl == NULL)
      break;

    send_value (vl);
    destroy_value_list (vl);
  }

  c_heap_destroy (values_heap);

  lcc_network_destroy (net);
  exit (EXIT_SUCCESS);
  return (0);
} /* }}} int main */

/* vim: set sw=2 sts=2 et fdm=marker : */
