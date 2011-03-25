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

struct data_formatter_s
{
  char *name;

  /*
   * title line: name, aggr type 1, ...
   */
  char *title_name_fmt;
  char *title_type_fmt;

  /*
   * aggregation group: group name, value 1, ...
   */
  char *group_name_fmt;
  char *group_val_fmt;

  /*
   * separators
   */
  char *field_sep;
  char *rec_sep;

  /*
   * lines, etc.
   */
  int (*print_header) (int);
  int (*print_line) (int);
  int (*print_footer) (int);
};
typedef struct data_formatter_s data_formatter_t;

/*
 * Global variables
 */
/* Selection */
static const char *re_host = NULL;
static const char *re_plugin = NULL;
static const char *re_plugin_instance = NULL;
static const char *re_type = NULL;
static const char *re_type_instance = NULL;

/* Grouping */
static uint16_t grouping = 0;

/* Aggregation */
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

static int group_name_from_ident (const lcc_identifier_t *identifier, /* {{{ */
    char *buffer, size_t buffer_size)
{
  if ((identifier == NULL)
      || (buffer == NULL) || (buffer_size < 2))
    return (EINVAL);

  if (grouping == 0)
  {
    lcc_identifier_to_string (/* connection = */ NULL,
                              buffer, buffer_size, identifier);
    buffer[buffer_size - 1] = 0;
    return (0);
  }

  memset (buffer, 0, buffer_size);

#define COPY_FIELD(field,index) do {                                         \
  if ((grouping & (1 << index)) != 0)                                        \
  {                                                                          \
    if (buffer[0] == 0)                                                      \
      strncpy (buffer, identifier->field, buffer_size);                      \
    else                                                                     \
    {                                                                        \
      char tmp[buffer_size];                                                 \
          snprintf (tmp, buffer_size, "%s/%s", buffer, identifier->field);   \
          memcpy (buffer, tmp, buffer_size);                                 \
    }                                                                        \
    buffer[buffer_size - 1] = 0;                                             \
  }                                                                          \
} while (0)

  COPY_FIELD (host, 0);
  COPY_FIELD (plugin, 1);
  COPY_FIELD (plugin_instance, 2);
  COPY_FIELD (type, 3);
  COPY_FIELD (type_instance, 4);

#undef COPY_FIELD

  return (0);
} /* }}} int group_name_from_ident */

static aggregation_group_t *aggregation_get_group ( const lcc_identifier_t *identifier) /* {{{ */
{
  char group_name[LCC_NAME_LEN];
  aggregation_group_t *g;
  size_t i;
  int status;

  if (identifier == NULL)
    return (NULL);

  status = group_name_from_ident (identifier,
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

  status = lcc_listval_with_selection (c,
                                       re_host,
                                       re_plugin,
                                       re_plugin_instance,
                                       re_type,
                                       re_type_instance,
                                       &ret_ident, &ret_ident_num);
  if (status != 0)
  {
    fprintf (stderr, "ERROR: lcc_listval_with_selection: %s\n",
             lcc_strerror (c));
    return (-1);
  }
  assert ((ret_ident != NULL) || (ret_ident_num == 0));

  /* Iterate over all returned identifiers and figure out which ones are
   * interesting, i.e. match a selector in an aggregation. */
  for (i = 0; i < ret_ident_num; ++i)
  {
    size_t   ret_values_num   = 0;
    gauge_t *ret_values       = NULL;

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

/* Formatting */
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
  if (aggregation_types_num == 0)
    printf ("------------+");

  printf ("\n");

  return (0);
} /* }}} int print_horizontal_line */

static int print_header_latex (int name_len_max) /* {{{ */
{
  size_t i;

  printf ("\\begin{tabular}{| l |");

  for (i = 0; i < aggregation_types_num; i++)
    printf (" r |");
  if (aggregation_types_num == 0)
    printf (" r |");

  printf ("}\n"
      "\\hline\n");

  return (0);
} /* }}} int print_header_latex */

static int print_horizontal_line_latex (int __attribute__((unused)) n) /* {{{ */
{
  printf ("\\hline\n");

  return (0);
} /* }}} int print_horizontal_line_latex */

static int print_footer_latex (int __attribute__((unused)) n) /* {{{ */
{
  printf ("\\hline\n"
      "\\end{tabular}\n");

  return (0);
} /* }}} int print_footer_latex */

/* Formatting configuration {{{ */
data_formatter_t formatter_list[] = {
  {
    "table",

    /* title line */
    "! %-*s",
    " %10s",

    /* aggr group */
    "! %-*s",
    " %10g",

    /* separators */
    " !",
    " !\n",

    print_horizontal_line,
    print_horizontal_line,
    print_horizontal_line
  },
  {
    "latex",

    /* title line */
    "{\\itshape %-*s}",
    " {\\itshape %10s}",

    /* aggr group */
    "%-*s",
    " %10g",

    /* separators */
    " &",
    " \\\\\n",

    print_header_latex,
    print_horizontal_line_latex,
    print_footer_latex
  }
};
static size_t formatter_list_len = sizeof (formatter_list) / sizeof (formatter_list[0]);

static data_formatter_t *formatter = &formatter_list[0];
/* }}} */

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

  formatter->print_header (name_len_max);
  printf (formatter->title_name_fmt, name_len_max, "Name");
  printf ("%s", formatter->field_sep);
  for (i = 0; i < aggregation_types_num; i++)
  {
    printf (formatter->title_type_fmt, aggr_type_to_string (aggregation_types[i]));
    if (i < aggregation_types_num - 1)
      printf ("%s", formatter->field_sep);
  }

  if (aggregation_types_num == 0)
    printf (formatter->title_type_fmt, "Value");

  printf ("%s", formatter->rec_sep);
  formatter->print_line (name_len_max);

  for (i = 0; i < aggregation_groups_num; i++)
  {
    size_t j;

    aggregation_group_t *g = aggregation_groups + i;

    printf (formatter->group_name_fmt, name_len_max, g->name);
    printf ("%s", formatter->field_sep);

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

      printf (formatter->group_val_fmt, value);
      if (j < aggregation_types_num - 1)
        printf ("%s", formatter->field_sep);
    }
    if (aggregation_types_num == 0)
    {
      /* g->num may be zero if the value is NAN. */
      assert (g->num < 2);
      printf (formatter->group_val_fmt, g->min);
    }

    printf ("%s", formatter->rec_sep);
  }

  formatter->print_footer (name_len_max);

  return (0);
} /* }}} int write_data */

__attribute__((noreturn))
static void exit_usage (int status) /* {{{ */
{
  printf ("Usage: collectdctl show [<Selection>] [<Aggregation> <Grouping>]\n"
          "\n"
          "Selection:\n"
          "\n"
          "  host=<regex>                      Regex for the host name.\n"
          "  plugin=<regex>                    Regex for the plugin.\n"
          "  plugin_instance=<regex>           Regex for the plugin instance.\n"
          "  type=<regex>                      Regex for the type.\n"
          "  type_instance=<regex>             Regex for the type instance.\n"
          "\n"
          "Aggregation:\n"
          "\n"
          "  aggregate=<aggr>[,<aggr>[...]]    List of aggregations to use when\n"
          "                                    combining multiple values.\n"
          "                                    Valid aggregations are:\n"
          "                                    count, min, max, avg, sum, stddev\n"
          "\n"
          "Grouping:\n"
          "\n"
          "  group=<field>[,<field>[...]]      List of fields to group by.\n"
          "                                    Valid fields are:\n"
          "                                    host, plugin, plugin_instance,\n"
          "                                    type, type_instance\n"
          "\n");
  exit (status);
} /* }}} void exit_usage */

static int parse_aggregate (const char *aggr) /* {{{ */
{
  char *aggr_copy;
  char *dummy;
  char *a;

  aggr_copy = strdup (aggr);
  if (aggr_copy == NULL)
    return (ENOMEM);

  free (aggregation_types);
  aggregation_types = NULL;
  aggregation_types_num = 0;

  dummy = aggr_copy;
  while ((a = strtok (dummy, ",")) != NULL)
  {
    int status;

    dummy = NULL;

    status = aggregation_type_add (a);
    if (status != 0)
      exit_usage (EXIT_FAILURE);
  } /* while (strtok) */

  free (aggr_copy);

  return (0);
} /* }}} int parse_group */

static int parse_group (const char *group) /* {{{ */
{
  char *group_copy;
  char *dummy;
  char *g;

  group_copy = strdup (group);
  if (group_copy == NULL)
    return (ENOMEM);

  grouping = 0;

  dummy = group_copy;
  while ((g = strtok (dummy, ",")) != NULL)
  {
    int pos = 0;

    dummy = NULL;

    if (strcasecmp ("host", g) == 0)
      pos = 0;
    else if (strcasecmp ("plugin", g) == 0)
      pos = 1;
    else if ((strcasecmp ("plugin_instance", g) == 0)
        || (strcasecmp ("plugininstance", g) == 0)
        || (strcasecmp ("pinst", g) == 0))
      pos = 2;
    else if (strcasecmp ("type", g) == 0)
      pos = 3;
    else if ((strcasecmp ("type_instance", g) == 0)
        || (strcasecmp ("typeinstance", g) == 0)
        || (strcasecmp ("tinst", g) == 0))
      pos = 4;
    else
    {
      fprintf (stderr, "Unknown grouping field: \"%s\"\n", g);
      exit_usage (EXIT_FAILURE);
    }

    grouping |= 1 << pos;
  } /* while (strtok) */

  free (group_copy);

  return (0);
} /* }}} int parse_group */

static int parse_format (const char *name) /* {{{ */
{
  int i;

  for (i = 0; i < formatter_list_len; ++i)
  {
    if (strcasecmp (formatter_list[i].name, name) == 0)
    {
      formatter = formatter_list + i;
      break;
    }
  }

  if (i >= formatter_list_len) {
    fprintf (stderr, "Unknown format: \"%s\"\n", name);
    exit_usage (EXIT_FAILURE);
  }

  return (0);
} /* }}} int parse_format */

static int parse_arg (const char *arg) /* {{{ */
{
  if (arg == NULL)
    return (EINVAL);
  else if (strncasecmp ("host=", arg, strlen ("host=")) == 0)
    re_host = arg + strlen ("host=");
  else if (strncasecmp ("plugin=", arg, strlen ("plugin=")) == 0)
    re_plugin = arg + strlen ("plugin=");
  else if (strncasecmp ("plugin_instance=", arg, strlen ("plugin_instance=")) == 0)
    re_plugin_instance = arg + strlen ("plugin_instance=");
  else if (strncasecmp ("type=", arg, strlen ("type=")) == 0)
    re_type = arg + strlen ("type=");
  else if (strncasecmp ("type_instance=", arg, strlen ("type_instance=")) == 0)
    re_type_instance = arg + strlen ("type_instance=");

  /* Grouping */
  else if (strncasecmp ("group=", arg, strlen ("group=")) == 0)
    return (parse_group (arg + strlen ("group=")));

  /* Aggregations */
  else if (strncasecmp ("aggregate=", arg, strlen ("aggregate=")) == 0)
    return (parse_aggregate (arg + strlen ("aggregate=")));

  /* Some alternative spellings to make it easier to guess a working argument
   * name: */
  else if (strncasecmp ("hostname=", arg, strlen ("hostname=")) == 0)
    re_host = arg + strlen ("hostname=");
  else if (strncasecmp ("plugininstance=", arg, strlen ("plugininstance=")) == 0)
    re_plugin_instance = arg + strlen ("plugininstance=");
  else if (strncasecmp ("typeinstance=", arg, strlen ("typeinstance=")) == 0)
    re_type_instance = arg + strlen ("typeinstance=");
  else if (strncasecmp ("pinst=", arg, strlen ("pinst=")) == 0)
    re_plugin_instance = arg + strlen ("pinst=");
  else if (strncasecmp ("tinst=", arg, strlen ("tinst=")) == 0)
    re_type_instance = arg + strlen ("tinst=");
  else if (strncasecmp ("aggr=", arg, strlen ("aggr=")) == 0)
    return (parse_aggregate (arg + strlen ("aggr=")));

  /* Formatting */
  else if (strncasecmp ("format=", arg, strlen ("format=")) == 0)
    return (parse_format (arg + strlen ("format=")));

  /* Don't know what that is ... */
  else
  {
    fprintf (stderr, "Unknown argument: \"%s\"\n", arg);
    exit_usage (EXIT_FAILURE);
  }

  return (0);
} /* }}} int parse_arg */

int show (lcc_connection_t *c, int argc, char **argv) /* {{{ */
{
  int status;
  int i;
  size_t j;

  for (i = 1; i < argc; i++)
  {
    status = parse_arg (argv[i]);
    /* parse_arg calls exit_usage() on error. */
    assert (status == 0);
  }

  if ((grouping == 0) && (aggregation_types_num > 0))
  {
    fprintf (stderr, "One or more aggregations were specified, but no fields "
        "were selected for grouping values. Please use the ""\"group=...\" "
        "option.\n");
    exit_usage (EXIT_FAILURE);
  }
  else if ((grouping != 0) && (aggregation_types_num == 0))
  {
    fprintf (stderr, "One or more fields were specified for grouping but no "
        "aggregation was given. Please use the \"aggregate=...\" option.\n");
    exit_usage (EXIT_FAILURE);
  }

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
