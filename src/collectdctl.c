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

#include "libcollectdclient/client.h"

#include <assert.h>

#include <errno.h>

#include <getopt.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>

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

      "\nIdentifiers:\n\n"

      "An identifier has the following format:\n\n"

      "  [<hostname>/]<plugin>[-<plugin_instance>]/<type>[-<type_instance>]\n\n"

      "Hostname defaults to the local hostname if omitted (e.g., uptime/uptime).\n"
      "No error is returned if the specified identifier does not exist.\n"

      "\nExample:\n\n"

      "  collectdctl flush plugin=rrdtool identifie=somehost/cpu-0/cpu-wait\n\n"

      "Flushes all CPU wait RRD values of the first CPU of the local host.\n"
      "I.e., writes all pending RRD updates of that data-source to disk.\n"

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
  if (status != 0)
    BAIL_OUT (-1);

  for (i = 0; i < ret_values_num; ++i)
    printf ("%s=%e\n", ret_values_names[i], ret_values[i]);
  BAIL_OUT (0);
} /* getval */

static int flush (lcc_connection_t *c, int argc, char **argv)
{
  lcc_identifier_t  ident;
  lcc_identifier_t *identp = NULL;

  char *plugin  = NULL;
  int   timeout = -1;

  int status;
  int i;

  assert (strcasecmp (argv[0], "flush") == 0);

  for (i = 1; i < argc; ++i) {
    char *key, *value;

    key   = argv[i];
    value = strchr (argv[i], (int)'=');

    if (! value) {
      fprintf (stderr, "ERROR: flush: Invalid option ``%s''.\n", argv[i]);
      return (-1);
    }

    *value = '\0';
    ++value;

    if (strcasecmp (key, "timeout") == 0) {
      char *endptr = NULL;

      timeout = strtol (value, &endptr, 0);

      if (endptr == value) {
        fprintf (stderr, "ERROR: Failed to parse timeout as number: %s.\n",
            value);
        return (-1);
      }
      else if ((endptr != NULL) && (*endptr != '\0')) {
        fprintf (stderr, "WARNING: Ignoring trailing garbage after timeout: "
            "%s.\n", endptr);
      }
    }
    else if (strcasecmp (key, "plugin") == 0) {
      plugin = value;
    }
    else if (strcasecmp (key, "identifier") == 0) {
      int status;

      memset (&ident, 0, sizeof (ident));
      status = parse_identifier (c, value, &ident);
      if (status != 0)
        return (status);
      identp = &ident;
    }
  }

  status = lcc_flush (c, plugin, identp, timeout);
  if (status != 0) {
    fprintf (stderr, "ERROR: Flushing failed: %s.\n",
        lcc_strerror (c));
    return (-1);
  }

  return 0;
} /* flush */

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

