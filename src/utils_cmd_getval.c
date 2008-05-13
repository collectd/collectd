/**
 * collectd - src/utils_cms_getval.c
 * Copyright (C) 2008  Florian octo Forster
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

#include "utils_cache.h"

#define print_to_socket(fh, ...) \
  if (fprintf (fh, __VA_ARGS__) < 0) { \
    char errbuf[1024]; \
    WARNING ("handle_getval: failed to write to socket #%i: %s", \
	fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
    return -1; \
  }

int handle_getval (FILE *fh, char **fields, int fields_num)
{
  char *hostname;
  char *plugin;
  char *plugin_instance;
  char *type;
  char *type_instance;
  gauge_t *values;
  size_t values_num;

  char *identifier_copy;

  const data_set_t *ds;

  int   status;
  int   i;

  if (fields_num != 2)
  {
    DEBUG ("unixsock plugin: Wrong number of fields: %i", fields_num);
    print_to_socket (fh, "-1 Wrong number of fields: Got %i, expected 2.\n",
	fields_num);
    return (-1);
  }
  DEBUG ("unixsock plugin: Got query for `%s'", fields[1]);

  if (strlen (fields[1]) < strlen ("h/p/t"))
  {
    print_to_socket (fh, "-1 Invalied identifier, %s\n", fields[1]);
    return (-1);
  }

  /* parse_identifier() modifies its first argument,
   * returning pointers into it */
  identifier_copy = sstrdup (fields[1]);

  status = parse_identifier (identifier_copy, &hostname,
      &plugin, &plugin_instance,
      &type, &type_instance);
  if (status != 0)
  {
    DEBUG ("unixsock plugin: Cannot parse `%s'", fields[1]);
    print_to_socket (fh, "-1 Cannot parse identifier.\n");
    sfree (identifier_copy);
    return (-1);
  }

  ds = plugin_get_ds (type);
  if (ds == NULL)
  {
    DEBUG ("unixsock plugin: plugin_get_ds (%s) == NULL;", type);
    print_to_socket (fh, "-1 Type `%s' is unknown.\n", type);
    sfree (identifier_copy);
    return (-1);
  }

  values = NULL;
  values_num = 0;
  status = uc_get_rate_by_name (fields[1], &values, &values_num);
  if (status != 0)
  {
    print_to_socket (fh, "-1 No such value\n");
    sfree (identifier_copy);
    return (-1);
  }

  if (ds->ds_num != values_num)
  {
    ERROR ("ds[%s]->ds_num = %i, "
	"but uc_get_rate_by_name returned %i values.",
	ds->type, ds->ds_num, values_num);
    print_to_socket (fh, "-1 Error reading value from cache.\n");
    sfree (values);
    sfree (identifier_copy);
    return (-1);
  }

  print_to_socket (fh, "%u Value%s found\n", (unsigned int) values_num,
      (values_num == 1) ? "" : "s");
  for (i = 0; i < values_num; i++)
  {
    print_to_socket (fh, "%s=", ds->ds[i].name);
    if (isnan (values[i]))
    {
      print_to_socket (fh, "NaN\n");
    }
    else
    {
      print_to_socket (fh, "%12e\n", values[i]);
    }
  }

  sfree (values);
  sfree (identifier_copy);

  return (0);
} /* int handle_getval */

/* vim: set sw=2 sts=2 ts=8 : */
