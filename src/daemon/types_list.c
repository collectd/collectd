/**
 * collectd - src/types_list.c
 * Copyright (C) 2007       Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"

#include "plugin.h"
#include "configfile.h"
#include "types_list.h"
#include "utils_avltree.h"

static c_avl_tree_t *data_sets = NULL;

const data_set_t *get_dataset (const char *type)
{
  data_set_t *ds = NULL;
  if (data_sets == NULL)
  {
    ERROR ("collectd: No data sets registered. "
          "Could the types database be read? "
          "Check your `TypesDB' setting!");
    return (NULL);
  }

  c_avl_get (data_sets, type, (void *) &ds);
  return ds;
}

void free_datasets ()
{
  void *key;
  void *value;

  if (data_sets == NULL)
    return;

  while (c_avl_pick (data_sets, &key, &value) == 0)
  {
    data_set_t *ds = value;
    /* key is a pointer to ds->type */

    sfree (ds->ds);
    sfree (ds);
  }

  c_avl_destroy (data_sets);
  data_sets = NULL;
} /* void free_datasets */

int merge_dataset (data_set_t *ds)
{
  data_set_t *current = NULL;

  if (data_sets == NULL)
    return (-1);

  c_avl_get (data_sets, ds->type, (void *) &current);

  if (current != NULL)
  {
    size_t i;
    int match = 1;

    if (current->ds_num != ds->ds_num)
    {
      NOTICE ("New version of DS `%s' has different datasources number. "
              "Dataset not updated.", ds->type);

      sfree (ds->ds);
      sfree (ds);
      return (-1);
    }

#define double_is_equal(a,b) ((isnan(a) && isnan(b)) || (!isnan(a) && !isnan(b) && (a == b)))
    for (i = 0; i < ds->ds_num; i++)
    {
      if (!double_is_equal(current->ds[i].min, ds->ds[i].min) ||
          !double_is_equal(current->ds[i].max, ds->ds[i].max) ||
          (current->ds[i].type != ds->ds[i].type) ||
          (strcmp(current->ds[i].name, ds->ds[i].name) != 0))
      {
        match = 0;
        break;
      }
    }
#undef double_is_equal

    if (match == 1)
    {
      sfree (ds->ds);
      sfree (ds);
      return (0);
    }

    NOTICE ("Updating DS `%s' with another version.", ds->type);

    for (i = 0; i < ds->ds_num; i++)
    {
      current->ds[i].min  = ds->ds[i].min;
      current->ds[i].max  = ds->ds[i].max;
      current->ds[i].type = ds->ds[i].type;

      sstrncpy (current->ds[i].name, ds->ds[i].name, sizeof (current->ds[i].name));
    }

    sfree (ds->ds);
    sfree (ds);
    return (0);
  } /* current != NULL */

  if (c_avl_insert (data_sets, (void *) ds->type, (void *) ds) != 0)
  {
    ERROR("merge_dataset: c_avl_insert() failed.");
    sfree (ds->ds);
    sfree (ds);
    return (-1);
  }

  return (0);
} /* int merge_dataset */

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
  size_t i;

  fields_num = strsplit (buf, fields, 64);
  if (fields_num < 2)
    return;

  /* Ignore lines which begin with a hash sign. */
  if (fields[0][0] == '#')
    return;

  ds = calloc (1, sizeof (*ds));
  if (ds == NULL)
    return;

  sstrncpy (ds->type, fields[0], sizeof (ds->type));

  ds->ds_num = fields_num - 1;
  ds->ds = (data_source_t *) calloc (ds->ds_num, sizeof (data_source_t));
  if (ds->ds == NULL)
  {
    sfree (ds);
    return;
  }

  for (i = 0; i < ds->ds_num; i++)
    if (parse_ds (ds->ds + i, fields[i + 1], strlen (fields[i + 1])) != 0)
    {
      ERROR ("types_list: parse_line: Cannot parse data source #%zu "
             "of data set %s", i, ds->type);
      sfree (ds->ds);
      sfree (ds);
      return;
    }

  merge_dataset(ds);
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
	  || (buf[buf_len - 1] == '\r')))
      buf[--buf_len] = '\0';

    if (buf_len == 0)
      continue;

    parse_line (buf);
  } /* while (fgets) */
} /* void parse_file */

static int read_types (const char *file)
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
} /* static int read_types */

int reload_typesdb (void) {
  size_t i;

  assert (conf_typesdb_num != 0);

  if (data_sets == NULL)
  {
    data_sets = c_avl_create ((int (*) (const void *, const void *)) strcmp);
    if (data_sets == NULL)
    {
      ERROR ("configfile: c_avl_create failed.");
      return (-1);
    }
  }

  for (i = 0; i < conf_typesdb_num; i++)
  {
    if (read_types (conf_typesdb[i]) != 0)
      return (-1);
  }

  return (0);
} /* int reload_typesdb */

/*
 * vim: shiftwidth=2:softtabstop=2:tabstop=8
 */
