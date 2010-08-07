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

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>

#define DEFAULT_SOCK LOCALSTATEDIR"/run/"PACKAGE_NAME"-unixsock"

extern char *optarg;

static int flush (
    const char *address,
    const char *plugin,
    const char *ident_str,
    int timeout)
{
  lcc_connection_t *connection;
  lcc_identifier_t ident;

  /* Pointer which is passed to lcc_flush.
   * Either a null pointer or it points to ident */
  lcc_identifier_t *identp;
  int status;

  connection = NULL;
  status = lcc_connect(address, &connection);
  if (status != 0) {
    fprintf (stderr, "ERROR: Connecting to daemon at %s failed: %s.\n",
        address, strerror (errno));
    return 1;
  }

  identp = NULL;
  if (ident_str != NULL && *ident_str != '\0') {
    status = lcc_string_to_identifier (connection, &ident, ident_str);
    if (status != 0) {
      fprintf (stderr, "ERROR: Creating and identifier failed: %s.\n",
          lcc_strerror(connection));
      LCC_DESTROY (connection);

      return 1;
    }
    identp = &ident;
  }

  status = lcc_flush (connection, plugin, identp, timeout);
  if (status != 0) {
    fprintf (stderr, "ERROR: Flushing failed: %s.\n",
        lcc_strerror (connection));
    LCC_DESTROY (connection);

    return 1;
  }

  LCC_DESTROY (connection);

  return 0;
}

static void exit_usage (const char *name, int status) {
  fprintf ((status == 0) ? stdout : stderr,
      "Usage: %s [options]\n\n"

      "Available options:\n"
      "  -s             Path to collectd's UNIX socket.\n"
      "                 Default: "DEFAULT_SOCK"\n"
      "  -p <plugin>    Plugin to be flushed.\n"
      "  -i <id>        Flush data identified by <id> only (see below).\n"
      "  -t <seconds>   Flush values older than this value only.\n"

      "\n  -h             Display this help and exit.\n"

      "\nIdentfiers:\n\n"

      "An identifier (as accepted by the -i option) has the following\n"
      "format:\n\n"

      "  [<hostname>/]<plugin>[-<plugin_instance>]/<type>[-<type_instance>]\n\n"

      "Hostname defaults to the local hostname if omitted (e.g., uptime/uptime).\n"
      "No error is returned if the specified identifier does not exist.\n"

      "\nExample:\n\n"

      "  collectd-flush -p rrdtool -i somehost/cpu-0/cpu-wait\n\n"

      "Flushes all CPU wait RRD values of the first CPU of the local host.\n"
      "I.e., writes all pending RRD updates of that data-source to disk.\n"
      , name);
  exit (status);
}

/*
 * Count how many occurences there are of a char in a string.
 */
static int charoccurences (const char *str, char chr) {
  int count = 0;
  while (*str != '\0') {
    if (*str == chr) {
      count++;
    }
    str++;
  }

  return count;
}

int main (int argc, char **argv) {
  char address[1024] = "unix:"DEFAULT_SOCK;
  char *plugin = NULL;
  char ident_str[1024] = "";
  int timeout = -1;
  char hostname[1024];

  while (42) {
    int c;

    c = getopt (argc, argv, "s:p:i:ht:");

    if (c == -1)
      break;

    switch (c) {
      case 's':
        snprintf (address, sizeof (address), "unix:%s", optarg);
        address[sizeof (address) - 1] = '\0';
        break;
      case 'p':
        plugin = optarg;
        break;
      case 'i':
        if (charoccurences (optarg, '/') == 1) {
          /* The user has omitted the hostname part of the identifier
           * (there is only one '/' in the identifier)
           * Let's add the local hostname */
          if (gethostname (hostname, sizeof (hostname)) != 0) {
            fprintf (stderr, "Could not get local hostname: %s", strerror (errno));
            return 1;
          }
          /* Make sure hostname is zero-terminated */
          hostname[sizeof (hostname) - 1] = '\0';
          snprintf (ident_str, sizeof (ident_str), "%s/%s", hostname, optarg);
          /* Make sure ident_str is zero terminated */
          ident_str[sizeof(ident_str) - 1] = '\0';
        } else {
          strncpy (ident_str, optarg, sizeof (ident_str));
          /* Make sure identifier is zero terminated */
          ident_str[sizeof (ident_str) - 1] = '\0';
        }
        break;
      case 't':
        timeout = atoi (optarg);
        break;
      case 'h':
        exit_usage (argv[0], 0);
        break;
      default:
        exit_usage (argv[0], 1);
    }
  }

  return flush(address, plugin, ident_str, timeout);
}

/* vim: set sw=2 ts=2 tw=78 expandtab : */

