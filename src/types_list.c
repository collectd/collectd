/**
 * collectd - src/types_list.c
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
#include "configfile.h"

static int parse_ds (data_source_t *dsrc, char *buf, size_t buf_len)
{
  char *dummy;
  char *saveptr;
  char *fields[8];
  int   fields_num;

  if (buf_len < 11)
  {
    ERROR ("parse_ds: (buf_len = %zu) < 11", buf_len);
    return (-1);
  }

  if (buf[buf_len - 1] == ',')
  {
    buf_len--;
    buf[buf_len] = '\0';
  }

  dummy = buf;
  saveptr = NULL;

  fields_num = 0;
  while (fields_num < 8)
  {
    if ((fields[fields_num] = strtok_r (dummy, ":", &saveptr)) == NULL)
      break;
    dummy = NULL;
    fields_num++;
  }

  if (fields_num != 4)
  {
    ERROR ("parse_ds: (fields_num = %i) != 4", fields_num);
    return (-1);
  }

  sstrncpy (dsrc->name, fields[0], sizeof (dsrc->name));

  if (strcasecmp (fields[1], "GAUGE") == 0)
    dsrc->type = DS_TYPE_GAUGE;
  else if (strcasecmp (fields[1], "COUNTER") == 0)
    dsrc->type = DS_TYPE_COUNTER;
  else if (strcasecmp (fields[1], "DERIVE") == 0)
    dsrc->type = DS_TYPE_DERIVE;
  else if (strcasecmp (fields[1], "ABSOLUTE") == 0)
    dsrc->type = DS_TYPE_ABSOLUTE;
  else
  {
    ERROR ("(fields[1] = %s) != (GAUGE || COUNTER || DERIVE || ABSOLUTE)", fields[1]);
    return (-1);
  }

  if (strcasecmp (fields[2], "U") == 0)
    dsrc->min = NAN;
  else
    dsrc->min = atof (fields[2]);

  if (strcasecmp (fields[3], "U") == 0)
    dsrc->max = NAN;
  else
    dsrc->max = atof (fields[3]);

  return (0);
} /* int parse_ds */

static void parse_line (char *buf)
{
  char  *fields[64];
  size_t fields_num;
  data_set_t *ds;
  int i;

  fields_num = strsplit (buf, fields, 64);
  if (fields_num < 2)
    return;

  /* Ignore lines which begin with a hash sign. */
  if (fields[0][0] == '#')
    return;

  ds = (data_set_t *) malloc (sizeof (data_set_t));
  if (ds == NULL)
    return;

  memset (ds, '\0', sizeof (data_set_t));

  sstrncpy (ds->type, fields[0], sizeof (ds->type));

  ds->ds_num = fields_num - 1;
  ds->ds = (data_source_t *) calloc (ds->ds_num, sizeof (data_source_t));
  if (ds->ds == NULL)
    return;

  for (i = 0; i < ds->ds_num; i++)
    if (parse_ds (ds->ds + i, fields[i + 1], strlen (fields[i + 1])) != 0)
    {
      sfree (ds->ds);
      ERROR ("types_list: parse_line: Cannot parse data source #%i "
	  "of data set %s", i, ds->type);
      return;
    }

  plugin_register_data_set (ds);

  sfree (ds->ds);
  sfree (ds);
} /* void parse_line */

static void parse_file (FILE *fh)
{
  char buf[4096];
  size_t buf_len;

  while (fgets (buf, sizeof (buf), fh) != NULL)
  {
    buf_len = strlen (buf);

    if (buf_len >= 4095)
    {
      NOTICE ("Skipping line with more than 4095 characters.");
      do
      {
	if (fgets (buf, sizeof (buf), fh) == NULL)
	  break;
	buf_len = strlen (buf);
      } while (buf_len >= 4095);
      continue;
    } /* if (buf_len >= 4095) */

    if ((buf_len == 0) || (buf[0] == '#'))
      continue;

    while ((buf_len > 0) && ((buf[buf_len - 1] == '\n')
	  || (buf[buf_len - 1] == '\n')))
      buf[--buf_len] = '\0';

    if (buf_len == 0)
      continue;

    parse_line (buf);
  } /* while (fgets) */
} /* void parse_file */

int read_types_list (const char *file)
{
  FILE *fh;

  if (file == NULL)
    return (-1);

  fh = fopen (file, "r");
  if (fh == NULL)
  {
    char errbuf[1024];
    fprintf (stderr, "Failed to open types database `%s': %s.\n",
	file, sstrerror (errno, errbuf, sizeof (errbuf)));
    ERROR ("Failed to open types database `%s': %s",
	file, sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  parse_file (fh);

  fclose (fh);
  fh = NULL;

  DEBUG ("Done parsing `%s'", file);

  return (0);
} /* int read_types_list */

/*
 * vim: shiftwidth=2:softtabstop=2:tabstop=8
 */
