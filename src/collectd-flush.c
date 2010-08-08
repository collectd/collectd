/**
 * collectd-flush - src/collectd-flush.c
 * Copyright (C) 2010 Håkon J Dugstad Johnsen
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

      " * flush [timeout=<seconds>] [plugin=<name>] [identifier=<id>]\n"

      "\nIdentifiers:\n\n"

      "An identifier has the following format:\n\n"

      "  [<hostname>/]<plugin>[-<plugin_instance>]/<type>[-<type_instance>]\n\n"

      "Hostname defaults to the local hostname if omitted (e.g., uptime/uptime).\n"
      "No error is returned if the specified identifier does not exist.\n"

      "\nExample:\n\n"

      "  collectd-flush flush plugin=rrdtool identifie=somehost/cpu-0/cpu-wait\n\n"

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

static int flush (const char *address, int argc, char **argv)
{
  lcc_connection_t *connection;

  lcc_identifier_t  ident;
  lcc_identifier_t *identp = NULL;

  char *plugin  = NULL;
  int   timeout = -1;

  int status;
  int i;

  assert (strcasecmp (argv[0], "flush") == 0);

  connection = NULL;
  status = lcc_connect (address, &connection);
  if (status != 0) {
    fprintf (stderr, "ERROR: Failed to connect to daemon at %s: %s.\n",
        address, strerror (errno));
    return (1);
  }

  for (i = 1; i < argc; ++i) {
    char *key, *value;

    key   = argv[i];
    value = strchr (argv[i], (int)'=');

    if (! value) {
      fprintf (stderr, "ERROR: Invalid option ``%s''.\n", argv[i]);
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
      char hostname[1024];
      char ident_str[1024] = "";
      int  n_slashes;

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

      status = lcc_string_to_identifier (connection, &ident, ident_str);
      if (status != 0) {
        fprintf (stderr, "ERROR: Failed to parse identifier ``%s'': %s.\n",
            ident_str, lcc_strerror(connection));
        LCC_DESTROY (connection);
        return (-1);
      }
      identp = &ident;
    }
  }

  status = lcc_flush (connection, plugin, identp, timeout);
  if (status != 0) {
    fprintf (stderr, "ERROR: Flushing failed: %s.\n",
        lcc_strerror (connection));
    LCC_DESTROY (connection);
    return (-1);
  }

  LCC_DESTROY (connection);

  return 0;
} /* flush */

int main (int argc, char **argv) {
  char address[1024] = "unix:"DEFAULT_SOCK;

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

  if (strcasecmp (argv[optind], "flush") == 0)
    status = flush (address, argc - optind, argv + optind);
  else {
    fprintf (stderr, "%s: invalid command: %s\n", argv[0], argv[optind]);
    return (1);
  }

  if (status != 0)
    return (status);
  return (0);
} /* main */

/* vim: set sw=2 ts=2 tw=78 expandtab : */

