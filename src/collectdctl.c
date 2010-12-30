/**
 * collectd - src/collectdctl.c
 * Copyright (C) 2010 Håkon J Dugstad Johnsen
 * Copyright (C) 2010 Sebastian Harl
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
 *   Håkon J Dugstad Johnsen <hakon-dugstad.johnsen at telenor.com>
 *   Sebastian "tokkee" Harl <sh@tokkee.org>
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

#define DEFAULT_SOCK LOCALSTATEDIR"/run/"PACKAGE_NAME"-unixsock"

extern char *optarg;
extern int   optind;

static void exit_usage (const char *name, int status) {
  fprintf ((status == 0) ? stdout : stderr,
      "Usage: %s [options] <command> [cmd options]\n\n"

      "Available options:\n"
      "  -s       Path to collectd's UNIX socket.\n"
      "           Default: "DEFAULT_SOCK"\n"

      "\n  -h       Display this help and exit.\n"

      "\nAvailable commands:\n\n"

      " * getval <identifier>\n"
      " * flush [timeout=<seconds>] [plugin=<name>] [identifier=<id>]\n"
      " * listval\n"
      " * putval <identifier> [interval=<seconds>] <value-list(s)>\n"

      "\nIdentifiers:\n\n"

      "An identifier has the following format:\n\n"

      "  [<hostname>/]<plugin>[-<plugin_instance>]/<type>[-<type_instance>]\n\n"

      "Hostname defaults to the local hostname if omitted (e.g., uptime/uptime).\n"
      "No error is returned if the specified identifier does not exist.\n"

      "\n"PACKAGE" "VERSION", http://collectd.org/\n"
      "by Florian octo Forster <octo@verplant.org>\n"
      "for contributions see `AUTHORS'\n"
      , name);
  exit (status);
}

/* Count the number of occurrences of the character 'chr'
 * in the specified string. */
static int count_chars (const char *str, char chr) {
  int count = 0;

  while (*str != '\0') {
    if (*str == chr) {
      count++;
    }
    str++;
  }

  return count;
} /* count_chars */

static int array_grow (void **array, int *array_len, size_t elem_size)
{
  void *tmp;

  assert ((array != NULL) && (array_len != NULL));

  tmp = realloc (*array, (*array_len + 1) * elem_size);
  if (tmp == NULL) {
    fprintf (stderr, "ERROR: Failed to allocate memory.\n");
    return (-1);
  }

  *array = tmp;
  ++(*array_len);
  return (0);
} /* array_grow */

static int parse_identifier (lcc_connection_t *c,
    const char *value, lcc_identifier_t *ident)
{
  char hostname[1024];
  char ident_str[1024] = "";
  int  n_slashes;

  int status;

  n_slashes = count_chars (value, '/');
  if (n_slashes == 1) {
    /* The user has omitted the hostname part of the identifier
     * (there is only one '/' in the identifier)
     * Let's add the local hostname */
    if (gethostname (hostname, sizeof (hostname)) != 0) {
      fprintf (stderr, "ERROR: Failed to get local hostname: %s",
          strerror (errno));
      return (-1);
    }
    hostname[sizeof (hostname) - 1] = '\0';

    snprintf (ident_str, sizeof (ident_str), "%s/%s", hostname, value);
    ident_str[sizeof(ident_str) - 1] = '\0';
  }
  else {
    strncpy (ident_str, value, sizeof (ident_str));
    ident_str[sizeof (ident_str) - 1] = '\0';
  }

  status = lcc_string_to_identifier (c, ident, ident_str);
  if (status != 0) {
    fprintf (stderr, "ERROR: Failed to parse identifier ``%s'': %s.\n",
        ident_str, lcc_strerror(c));
    return (-1);
  }
  return (0);
} /* parse_identifier */

static int getval (lcc_connection_t *c, int argc, char **argv)
{
  lcc_identifier_t ident;

  size_t   ret_values_num   = 0;
  gauge_t *ret_values       = NULL;
  char   **ret_values_names = NULL;

  int status;
  size_t i;

  assert (strcasecmp (argv[0], "getval") == 0);

  if (argc != 2) {
    fprintf (stderr, "ERROR: getval: Missing identifier.\n");
    return (-1);
  }

  memset (&ident, 0, sizeof (ident));
  status = parse_identifier (c, argv[1], &ident);
  if (status != 0)
    return (status);

#define BAIL_OUT(s) \
  do { \
    if (ret_values != NULL) \
      free (ret_values); \
    if (ret_values_names != NULL) { \
      for (i = 0; i < ret_values_num; ++i) \
        free (ret_values_names[i]); \
      free (ret_values_names); \
    } \
    ret_values_num = 0; \
    return (s); \
  } while (0)

  status = lcc_getval (c, &ident,
      &ret_values_num, &ret_values, &ret_values_names);
  if (status != 0) {
    fprintf (stderr, "ERROR: %s\n", lcc_strerror (c));
    BAIL_OUT (-1);
  }

  for (i = 0; i < ret_values_num; ++i)
    printf ("%s=%e\n", ret_values_names[i], ret_values[i]);
  BAIL_OUT (0);
#undef BAIL_OUT
} /* getval */

static int flush (lcc_connection_t *c, int argc, char **argv)
{
  int timeout = -1;

  lcc_identifier_t *identifiers = NULL;
  int identifiers_num = 0;

  char **plugins = NULL;
  int plugins_num = 0;

  int status;
  int i;

  assert (strcasecmp (argv[0], "flush") == 0);

#define BAIL_OUT(s) \
  do { \
    if (identifiers != NULL) \
      free (identifiers); \
    identifiers_num = 0; \
    if (plugins != NULL) \
      free (plugins); \
    plugins_num = 0; \
    return (s); \
  } while (0)

  for (i = 1; i < argc; ++i) {
    char *key, *value;

    key   = argv[i];
    value = strchr (argv[i], (int)'=');

    if (! value) {
      fprintf (stderr, "ERROR: flush: Invalid option ``%s''.\n", argv[i]);
      BAIL_OUT (-1);
    }

    *value = '\0';
    ++value;

    if (strcasecmp (key, "timeout") == 0) {
      char *endptr = NULL;

      timeout = (int) strtol (value, &endptr, 0);

      if (endptr == value) {
        fprintf (stderr, "ERROR: Failed to parse timeout as number: %s.\n",
            value);
        BAIL_OUT (-1);
      }
      else if ((endptr != NULL) && (*endptr != '\0')) {
        fprintf (stderr, "WARNING: Ignoring trailing garbage after timeout: "
            "%s.\n", endptr);
      }
    }
    else if (strcasecmp (key, "plugin") == 0) {
      status = array_grow ((void *)&plugins, &plugins_num,
          sizeof (*plugins));
      if (status != 0)
        BAIL_OUT (status);

      plugins[plugins_num - 1] = value;
    }
    else if (strcasecmp (key, "identifier") == 0) {
      status = array_grow ((void *)&identifiers, &identifiers_num,
          sizeof (*identifiers));
      if (status != 0)
        BAIL_OUT (status);

      memset (identifiers + (identifiers_num - 1), 0, sizeof (*identifiers));
      status = parse_identifier (c, value,
          identifiers + (identifiers_num - 1));
      if (status != 0)
        BAIL_OUT (status);
    }
    else {
      fprintf (stderr, "ERROR: flush: Unknown option `%s'.\n", key);
      BAIL_OUT (-1);
    }
  }

  if (plugins_num == 0) {
    status = array_grow ((void *)&plugins, &plugins_num, sizeof (*plugins));
    if (status != 0)
      BAIL_OUT (status);

    assert (plugins_num == 1);
    plugins[0] = NULL;
  }

  for (i = 0; i < plugins_num; ++i) {
    if (identifiers_num == 0) {
      status = lcc_flush (c, plugins[i], NULL, timeout);
      if (status != 0)
        fprintf (stderr, "ERROR: Failed to flush plugin `%s': %s.\n",
            (plugins[i] == NULL) ? "(all)" : plugins[i], lcc_strerror (c));
    }
    else {
      int j;

      for (j = 0; j < identifiers_num; ++j) {
        status = lcc_flush (c, plugins[i], identifiers + j, timeout);
        if (status != 0) {
          char id[1024];

          lcc_identifier_to_string (c, id, sizeof (id), identifiers + j);
          fprintf (stderr, "ERROR: Failed to flush plugin `%s', "
              "identifier `%s': %s.\n",
              (plugins[i] == NULL) ? "(all)" : plugins[i],
              id, lcc_strerror (c));
        }
      }
    }
  }

  BAIL_OUT (0);
#undef BAIL_OUT
} /* flush */

static int listval (lcc_connection_t *c, int argc, char **argv)
{
  lcc_identifier_t *ret_ident     = NULL;
  size_t            ret_ident_num = 0;

  int status;
  size_t i;

  assert (strcasecmp (argv[0], "listval") == 0);

  if (argc != 1) {
    fprintf (stderr, "ERROR: listval: Does not accept any arguments.\n");
    return (-1);
  }

#define BAIL_OUT(s) \
  do { \
    if (ret_ident != NULL) \
      free (ret_ident); \
    ret_ident_num = 0; \
    return (s); \
  } while (0)

  status = lcc_listval (c, &ret_ident, &ret_ident_num);
  if (status != 0) {
    fprintf (stderr, "ERROR: %s\n", lcc_strerror (c));
    BAIL_OUT (status);
  }

  for (i = 0; i < ret_ident_num; ++i) {
    char id[1024];

    status = lcc_identifier_to_string (c, id, sizeof (id), ret_ident + i);
    if (status != 0) {
      fprintf (stderr, "ERROR: listval: Failed to convert returned "
          "identifier to a string: %s\n", lcc_strerror (c));
      continue;
    }

    printf ("%s\n", id);
  }
  BAIL_OUT (0);
#undef BAIL_OUT
} /* listval */

static int putval (lcc_connection_t *c, int argc, char **argv)
{
  lcc_value_list_t vl = LCC_VALUE_LIST_INIT;

  /* 64 ought to be enough for anybody ;-) */
  value_t values[64];
  int     values_types[64];
  size_t  values_len = 0;

  int status;
  int i;

  assert (strcasecmp (argv[0], "putval") == 0);

  if (argc < 3) {
    fprintf (stderr, "ERROR: putval: Missing identifier "
        "and/or value list.\n");
    return (-1);
  }

  vl.values       = values;
  vl.values_types = values_types;

  status = parse_identifier (c, argv[1], &vl.identifier);
  if (status != 0)
    return (status);

  for (i = 2; i < argc; ++i) {
    char *tmp;

    tmp = strchr (argv[i], (int)'=');

    if (tmp != NULL) { /* option */
      char *key   = argv[i];
      char *value = tmp;

      *value = '\0';
      ++value;

      if (strcasecmp (key, "interval") == 0) {
        char *endptr;

        vl.interval = strtol (value, &endptr, 0);

        if (endptr == value) {
          fprintf (stderr, "ERROR: Failed to parse interval as number: %s.\n",
              value);
          return (-1);
        }
        else if ((endptr != NULL) && (*endptr != '\0')) {
          fprintf (stderr, "WARNING: Ignoring trailing garbage after "
              "interval: %s.\n", endptr);
        }
      }
      else {
        fprintf (stderr, "ERROR: putval: Unknown option `%s'.\n", key);
        return (-1);
      }
    }
    else { /* value list */
      char *value;

      tmp = strchr (argv[i], (int)':');

      if (tmp == NULL) {
        fprintf (stderr, "ERROR: putval: Invalid value list: %s.\n",
            argv[i]);
        return (-1);
      }

      *tmp = '\0';
      ++tmp;

      if (strcasecmp (argv[i], "N") == 0) {
        vl.time = 0;
      }
      else {
        char *endptr;

        vl.time = strtol (argv[i], &endptr, 0);

        if (endptr == argv[i]) {
          fprintf (stderr, "ERROR: Failed to parse time as number: %s.\n",
              argv[i]);
          return (-1);
        }
        else if ((endptr != NULL) && (*endptr != '\0')) {
          fprintf (stderr, "ERROR: Garbage after time: %s.\n", endptr);
          return (-1);
        }
      }

      values_len = 0;
      value = tmp;
      while (value != 0) {
        char *dot, *endptr;

        tmp = strchr (argv[i], (int)':');

        if (tmp != NULL) {
          *tmp = '\0';
          ++tmp;
        }

        /* This is a bit of a hack, but parsing types.db just does not make
         * much sense imho -- the server might have different types defined
         * anyway. Also, lcc uses the type information for formatting the
         * number only, so the real meaning does not matter. -tokkee */
        dot = strchr (value, (int)'.');
        endptr = NULL;
        if (strcasecmp (value, "U") == 0) {
          values[values_len].gauge = NAN;
          values_types[values_len] = LCC_TYPE_GAUGE;
        }
        else if (dot) { /* floating point value */
          values[values_len].gauge = strtod (value, &endptr);
          values_types[values_len] = LCC_TYPE_GAUGE;
        }
        else { /* integer */
          values[values_len].counter = strtol (value, &endptr, 0);
          values_types[values_len] = LCC_TYPE_COUNTER;
        }
        ++values_len;

        if (endptr == value) {
          fprintf (stderr, "ERROR: Failed to parse value as number: %s.\n",
              argv[i]);
          return (-1);
        }
        else if ((endptr != NULL) && (*endptr != '\0')) {
          fprintf (stderr, "ERROR: Garbage after value: %s.\n", endptr);
          return (-1);
        }

        value = tmp;
      }

      assert (values_len >= 1);
      vl.values_len = values_len;

      status = lcc_putval (c, &vl);
      if (status != 0) {
        fprintf (stderr, "ERROR: %s\n", lcc_strerror (c));
        return (-1);
      }
    }
  }

  if (values_len == 0) {
    fprintf (stderr, "ERROR: putval: Missing value list(s).\n");
    return (-1);
  }
  return (0);
} /* putval */

int main (int argc, char **argv) {
  char address[1024] = "unix:"DEFAULT_SOCK;

  lcc_connection_t *c;

  int status;

  while (42) {
    int c;

    c = getopt (argc, argv, "s:h");

    if (c == -1)
      break;

    switch (c) {
      case 's':
        snprintf (address, sizeof (address), "unix:%s", optarg);
        address[sizeof (address) - 1] = '\0';
        break;
      case 'h':
        exit_usage (argv[0], 0);
        break;
      default:
        exit_usage (argv[0], 1);
    }
  }

  if (optind >= argc) {
    fprintf (stderr, "%s: missing command\n", argv[0]);
    exit_usage (argv[0], 1);
  }

  c = NULL;
  status = lcc_connect (address, &c);
  if (status != 0) {
    fprintf (stderr, "ERROR: Failed to connect to daemon at %s: %s.\n",
        address, strerror (errno));
    return (1);
  }

  if (strcasecmp (argv[optind], "getval") == 0)
    status = getval (c, argc - optind, argv + optind);
  else if (strcasecmp (argv[optind], "flush") == 0)
    status = flush (c, argc - optind, argv + optind);
  else if (strcasecmp (argv[optind], "listval") == 0)
    status = listval (c, argc - optind, argv + optind);
  else if (strcasecmp (argv[optind], "putval") == 0)
    status = putval (c, argc - optind, argv + optind);
  else {
    fprintf (stderr, "%s: invalid command: %s\n", argv[0], argv[optind]);
    return (1);
  }

  LCC_DESTROY (c);

  if (status != 0)
    return (status);
  return (0);
} /* main */

/* vim: set sw=2 ts=2 tw=78 expandtab : */

