/**
 * collectd - src/utils_cms_listval.c
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

#include "utils_cmd_listval.h"
#include "utils_cache.h"

#define print_to_socket(fh, ...) \
  if (fprintf (fh, __VA_ARGS__) < 0) { \
    char errbuf[1024]; \
    WARNING ("handle_listval: failed to write to socket #%i: %s", \
	fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
    return -1; \
  }

int handle_listval (FILE *fh, char **fields, int fields_num)
{
  char **names = NULL;
  time_t *times = NULL;
  size_t number = 0;
  size_t i;
  int status;

  if (fields_num != 1)
  {
    DEBUG ("command listval: us_handle_listval: Wrong number of fields: %i",
	fields_num);
    print_to_socket (fh, "-1 Wrong number of fields: Got %i, expected 1.\n",
	fields_num);
    return (-1);
  }

  status = uc_get_names (&names, &times, &number);
  if (status != 0)
  {
    DEBUG ("command listval: uc_get_names failed with status %i", status);
    print_to_socket (fh, "-1 uc_get_names failed.\n");
    return (-1);
  }

  print_to_socket (fh, "%i Value%s found\n",
      (int) number, (number == 1) ? "" : "s");
  for (i = 0; i < number; i++)
    print_to_socket (fh, "%u %s\n", (unsigned int) times[i], names[i]);

  return (0);
} /* int handle_listval */

/* vim: set sw=2 sts=2 ts=8 : */
