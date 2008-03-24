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
    fprintf (fh, "-1 Wrong number of fields: Got %i, expected 1.\n",
	fields_num);
    fflush (fh);
    return (-1);
  }

  status = uc_get_names (&names, &times, &number);
  if (status != 0)
  {
    DEBUG ("command listval: uc_get_names failed with status %i", status);
    fprintf (fh, "-1 uc_get_names failed.\n");
    fflush (fh);
    return (-1);
  }

  fprintf (fh, "%i Values found\n", (int) number);
  for (i = 0; i < number; i++)
    fprintf (fh, "%u %s\n", (unsigned int) times[i], names[i]);
  fflush (fh);

  return (0);
} /* int handle_listval */

/* vim: set sw=2 sts=2 ts=8 : */
