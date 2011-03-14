/**
 * collectd - src/collectdctl-show.c
 * Copyright (C) 2011 Florian Forster
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
 *   Florian "octo" Forster <octo at collectd.org>
 **/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <assert.h>
#include <errno.h>

#if NAN_STATIC_DEFAULT
# include <math.h>
/* #endif NAN_STATIC_DEFAULT*/
#elif NAN_STATIC_ISOC
# ifndef __USE_ISOC99
#  define DISABLE_ISOC99 1
#  define __USE_ISOC99 1
# endif /* !defined(__USE_ISOC99) */
# include <math.h>
# if DISABLE_ISOC99
#  undef DISABLE_ISOC99
#  undef __USE_ISOC99
# endif /* DISABLE_ISOC99 */
/* #endif NAN_STATIC_ISOC */
#elif NAN_ZERO_ZERO
# include <math.h>
# ifdef NAN
#  undef NAN
# endif
# define NAN (0.0 / 0.0)
# ifndef isnan
#  define isnan(f) ((f) != (f))
# endif /* !defined(isnan) */
# ifndef isfinite
#  define isfinite(f) (((f) - (f)) == 0.0)
# endif
# ifndef isinf
#  define isinf(f) (!isfinite(f) && !isnan(f))
# endif
#endif /* NAN_ZERO_ZERO */

#include "libcollectdclient/client.h"

#define AGGR_TYPE_COUNT 0
#define AGGR_TYPE_MIN   1
#define AGGR_TYPE_MAX   2
#define AGGR_TYPE_AVG   3
#define AGGR_TYPE_SUM   4
#define AGGR_TYPE_SDEV  5

/*
 * Data structures
 */
struct aggregation_group_s
{
  char *name;

  int num;
  double min;
  double max;
  double sum;
  double sum_of_squares;
};
typedef struct aggregation_group_s aggregation_group_t;

/*
 * Global variables
 */
static lcc_identifier_t *selector;

static int *aggregation_types = NULL;
static size_t aggregation_types_num = 0;

static aggregation_group_t *aggregation_groups = NULL;
static size_t aggregation_groups_num = 0;

/*
 * Private functions
 */
static int parse_aggr_type (const char *type) /* {{{ */
{
  if (type == NULL)
    return (-1);
  else if (strcasecmp ("count", type) == 0)
    return (AGGR_TYPE_COUNT);
  else if ((strcasecmp ("min", type) == 0)
      || (strcasecmp ("minimum", type) == 0))
    return (AGGR_TYPE_MIN);
  else if ((strcasecmp ("max", type) == 0)
      || (strcasecmp ("maximum", type) == 0))
    return (AGGR_TYPE_MAX);
  else if ((strcasecmp ("avg", type) == 0)
      || (strcasecmp ("average", type) == 0))
    return (AGGR_TYPE_AVG);
  else if (strcasecmp ("sum", type) == 0)
    return (AGGR_TYPE_SUM);
  else if ((strcasecmp ("sdev", type) == 0)
      || (strcasecmp ("stddev", type) == 0))
    return (AGGR_TYPE_SDEV);
  else
    return (-1);
} /* }}} int parse_aggr_type */

static const char *aggr_type_to_string (int type) /* {{{ */
{
  switch (type)
  {
    case AGGR_TYPE_COUNT: return ("Count");
    case AGGR_TYPE_MIN:   return ("Min");
    case AGGR_TYPE_MAX:   return ("Max");
    case AGGR_TYPE_AVG:   return ("Average");
    case AGGR_TYPE_SUM:   return ("Sum");
    case AGGR_TYPE_SDEV:  return ("Std. Dev.");
  }

  return ("UNKNOWN");
} /* }}} const char *aggr_type_to_string */

static int aggregation_type_add (const char *str_type) /* {{{ */
{
  int type;
  int *tmp;
  size_t i;

  type = parse_aggr_type (str_type);
  if (type < 0)
  {
    fprintf (stderr, "ERROR: \"%s\" is not a known aggregation function.\n",
        str_type);
    return (type);
  }

  /* Check for duplicate definitions */
  for (i = 0; i < aggregation_types_num; i++)
  {
    if (aggregation_types[i] == type)
    {
      fprintf (stderr, "ERROR: Multiple aggregations with type \"%s\" "
          "defined.\n", str_type);
      return (EEXIST);
    }
  }

  tmp = realloc (aggregation_types,
      (aggregation_types_num + 1) * sizeof (*aggregation_types));
  if (tmp == NULL)
    return (ENOMEM);
  aggregation_types = tmp;
  aggregation_types[aggregation_types_num] = type;
  aggregation_types_num++;

  return (0);
} /* }}} int aggregation_type_add */

static int group_name_from_ident (const lcc_identifier_t *selector, /* {{{ */
    const lcc_identifier_t *identifier,
    char *buffer, size_t buffer_size)
{
  if ((selector == NULL)
      || (identifier == NULL)
      || (buffer == NULL) || (buffer_size < 2))
    return (EINVAL);

  /* Check if there is no "grouping" wildcard. If there isn't, return "all" as
   * the default value. */
  if ((strcmp ("+", selector->host) != 0)
      && (strcmp ("+", selector->plugin) != 0)
      && (strcmp ("+", selector->plugin_instance) != 0)
      && (strcmp ("+", selector->type) != 0)
      && (strcmp ("+", selector->type_instance) != 0))
  {
    /* There is no wildcard at all => use the identifier. */
    if ((strcmp ("*", selector->host) != 0)
        && (strcmp ("*", selector->plugin) != 0)
        && (strcmp ("*", selector->plugin_instance) != 0)
        && (strcmp ("*", selector->type) != 0)
        && (strcmp ("*", selector->type_instance) != 0))
      lcc_identifier_to_string (/* connection = */ NULL,
          buffer, buffer_size, identifier);
    else /* there's wildcards but no grouping */
      strncpy (buffer, "all", buffer_size);
    buffer[buffer_size - 1] = 0;
    return (0);
  }

  memset (buffer, 0, buffer_size);

#define COPY_FIELD(field) do {                                               \
  if (strcmp ("+", selector->field) != 0)                                    \
    break;                                                                   \
  if (buffer[0] == 0)                                                        \
    strncpy (buffer, identifier->field, buffer_size);                        \
  else                                                                       \
  {                                                                          \
    char tmp[buffer_size];                                                   \
    snprintf (tmp, buffer_size, "%s/%s", buffer, identifier->field);         \
    memcpy (buffer, tmp, buffer_size);                                       \
  }                                                                          \
  buffer[buffer_size - 1] = 0;                                               \
} while (0)

  COPY_FIELD (host);
  COPY_FIELD (plugin);
  COPY_FIELD (plugin_instance);
  COPY_FIELD (type);
  COPY_FIELD (type_instance);

#undef COPY_FIELD

  return (0);
} /* }}} int group_name_from_ident */

static _Bool ident_matches_selector (const lcc_identifier_t *selector, /* {{{ */
    const lcc_identifier_t *identifier)
{
  if ((selector == NULL) || (identifier == NULL))
    return (0);

  if ((strcmp (identifier->host, selector->host) != 0)
      && (strcmp ("*", selector->host) != 0)
      && (strcmp ("+", selector->host) != 0))
    return (0);

  if ((strcmp (identifier->plugin, selector->plugin) != 0)
      && (strcmp ("*", selector->plugin) != 0)
      && (strcmp ("+", selector->plugin) != 0))
    return (0);

  if ((strcmp (identifier->plugin_instance, selector->plugin_instance) != 0)
      && (strcmp ("*", selector->plugin_instance) != 0)
      && (strcmp ("+", selector->plugin_instance) != 0))
    return (0);

  if ((strcmp (identifier->type, selector->type) != 0)
      && (strcmp ("*", selector->type) != 0)
      && (strcmp ("+", selector->type) != 0))
    return (0);

  if ((strcmp (identifier->type_instance, selector->type_instance) != 0)
      && (strcmp ("*", selector->type_instance) != 0)
      && (strcmp ("+", selector->type_instance) != 0))
    return (0);

  return (1);
} /* }}} _Bool ident_matches_selector */

static aggregation_group_t *aggregation_get_group ( const lcc_identifier_t *identifier) /* {{{ */
{
  char group_name[LCC_NAME_LEN];
  aggregation_group_t *g;
  size_t i;
  int status;

  if (identifier == NULL)
    return (NULL);

  status = group_name_from_ident (selector, identifier,
      group_name, sizeof (group_name));
  if (status != 0)
    return (NULL);

  for (i = 0; i < aggregation_groups_num; i++)
    if (strcmp (group_name, aggregation_groups[i].name) == 0)
      return (aggregation_groups + i);

  g = realloc (aggregation_groups,
      (aggregation_groups_num + 1) * sizeof (*aggregation_groups));
  if (g == NULL)
    return (NULL);
  aggregation_groups = g;
  g = aggregation_groups + aggregation_groups_num;

  memset (g, 0, sizeof (*g));
  g->name = strdup (group_name);
  if (g->name == NULL)
    return (NULL);

  g->min = NAN;
  g->max = NAN;
  g->sum = NAN;
  g->sum_of_squares = NAN;

  aggregation_groups_num++;
  return (g);
} /* }}} aggregation_group_t *aggregation_get_group */

static int aggregation_add_value (const lcc_identifier_t *identifier, /* {{{ */
    double value)
{
  aggregation_group_t *g;

  if (identifier == NULL)
    return (EINVAL);

  g = aggregation_get_group (identifier);
  if (g == NULL)
    return (-1);

  if (g->num == 0)
  {
    g->min = value;
    g->max = value;
    g->sum = value;
    g->sum_of_squares = value * value;
    g->num = 1;
    return (0);
  }

  if (isnan (value))
    return (0);

  if (isnan (g->min) || (g->min > value))
    g->min = value;

  if (isnan (g->max) || (g->max < value))
    g->max = value;

  if (isnan (g->sum))
    g->sum = value;
  else
    g->sum += value;

  if (isnan (g->sum_of_squares))
    g->sum_of_squares = value * value;
  else
    g->sum_of_squares += value * value;

  g->num++;

  return (0);
} /* }}} int aggregation_add_value */

static int read_data (lcc_connection_t *c) /* {{{ */
{
  lcc_identifier_t *ret_ident     = NULL;
  size_t            ret_ident_num = 0;

  int status;
  size_t i;

  status = lcc_listval (c, &ret_ident, &ret_ident_num);
  if (status != 0)
  {
    fprintf (stderr, "ERROR: lcc_listval: %s\n", lcc_strerror (c));
    return (-1);
  }
  assert ((ret_ident != NULL) || (ret_ident_num == 0));

  /* Iterate over all returned identifiers and figure out which ones are
   * interesting, i.e. match a selector in an aggregation. */
  for (i = 0; i < ret_ident_num; ++i)
  {
    size_t   ret_values_num   = 0;
    gauge_t *ret_values       = NULL;

    if (!ident_matches_selector (selector, ret_ident + i))
      continue;

    status = lcc_getval (c, ret_ident + i,
        &ret_values_num, &ret_values, /* values_names = */ NULL);
    if (status != 0)
    {
      fprintf (stderr, "ERROR: lcc_getval: %s\n", lcc_strerror (c));
      continue;
    }
    assert (ret_values != NULL);

    /* FIXME: What to do with multiple data sources values? */
    aggregation_add_value (ret_ident + i, ret_values[0]);

    free (ret_values);
  } /* for (ret_ident) */

  free (ret_ident);

  return (0);
} /* }}} int read_data */

static int print_horizontal_line (int name_len_max) /* {{{ */
{
  int i;
  size_t j;

  printf ("+-");

  for (i = 0; i < name_len_max; i++)
    printf ("-");

  printf ("-+");

  for (j = 0; j < aggregation_types_num; j++)
    printf ("------------+");

  printf ("\n");

  return (0);
} /* }}} int print_horizontal_line */

static int write_data (void) /* {{{ */
{
  int name_len_max = 4;
  size_t i;

  for (i = 0; i < aggregation_groups_num; i++)
  {
    int name_len = (int) strlen (aggregation_groups[i].name);
    if (name_len_max < name_len)
      name_len_max = name_len;
  }

  print_horizontal_line (name_len_max);
  printf ("! %-*s !", name_len_max, "Name");
  for (i = 0; i < aggregation_types_num; i++)
    printf (" %10s !", aggr_type_to_string (aggregation_types[i]));
  printf ("\n");
  print_horizontal_line (name_len_max);

  for (i = 0; i < aggregation_groups_num; i++)
  {
    size_t j;

    aggregation_group_t *g = aggregation_groups + i;

    printf ("! %-*s !", name_len_max, g->name);

    for (j = 0; j < aggregation_types_num; j++)
    {
      int type = aggregation_types[j];
      double value = NAN;

      if (type == AGGR_TYPE_COUNT)
        value = (double) g->num;
      else if (type == AGGR_TYPE_MIN)
        value = g->min;
      else if (type == AGGR_TYPE_MAX)
        value = g->max;
      else if (type == AGGR_TYPE_SUM)
        value = g->sum;
      else if ((type == AGGR_TYPE_AVG)
          && (g->num > 0))
        value = g->sum / ((double) g->num);
      else if (type == AGGR_TYPE_SDEV)
      {
        if (g->num == 1)
          value = 0.0;
        else if (g->num > 1)
          value = sqrt (
              (
               g->sum_of_squares
               - ((g->sum * g->sum) / ((double) g->num))
              )
              / ((double) (g->num - 1)));
      }

      printf (" %10g !", value);
    }

    printf ("\n");
  }

  print_horizontal_line (name_len_max);

  return (0);
} /* }}} int write_data */

__attribute__((noreturn))
static void exit_usage (int status) /* {{{ */
{
  printf ("Usage: collectdctl show <selector> <aggregation> "
          "[<aggregation> ...]\n"
          "\n"
          "Selector:\n"
          "  A selector is an identifier, where each part may be replaced "
          "with either\n"
          "  \"*\" or \"+\".\n"
          "\n"
          "Aggregation:\n"
          "  count\n"
          "  min\n"
          "  max\n"
          "  avg\n"
          "\n");
  exit (status);
} /* }}} void exit_usage */

int show (lcc_connection_t *c, int argc, char **argv) /* {{{ */
{
  lcc_identifier_t tmp;
  int status;
  int i;
  size_t j;

  if (argc < 3)
    exit_usage (EXIT_FAILURE);

  memset (&tmp, 0, sizeof (tmp));
  status = lcc_string_to_identifier (c, &tmp, argv[1]);
  if (status != 0)
    return (status);
  selector = &tmp;

  for (i = 2; i < argc; i++)
    aggregation_type_add (argv[i]);

  status = read_data (c);
  if (status != 0)
    return (status);

  status = write_data ();
  if (status != 0)
    return (status);

  for (j = 0; j < aggregation_groups_num; j++)
    free (aggregation_groups[j].name);
  free (aggregation_groups);
  free (aggregation_types);

  return (0);
} /* }}} int show */

/* vim: set sw=2 ts=2 tw=78 expandtab fdm=marker : */
