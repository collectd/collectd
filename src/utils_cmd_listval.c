/**
 * collectd - src/utils_cms_listval.c
 * Copyright (C) 2008-2011  Florian Forster
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
 *   Florian "octo" Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <regex.h>

#include "utils_cmd_listval.h"
#include "utils_cache.h"
#include "utils_parse_option.h"

/* Not very nice, but oh so handy ... */
#define FREE_EVERYTHING_AND_RETURN(status) do { \
    size_t j; \
    for (j = 0; j < number; j++) { \
      sfree(names[j]); \
      names[j] = NULL; \
    } \
    sfree(names); \
    sfree(times); \
    if (have_re_host)            { regfree (&re_host);            } \
    if (have_re_plugin)          { regfree (&re_plugin);          } \
    if (have_re_plugin_instance) { regfree (&re_plugin_instance); } \
    if (have_re_type)            { regfree (&re_type);            } \
    if (have_re_type_instance)   { regfree (&re_type_instance);   } \
    return (status); \
  } while (0)

#define print_to_socket(fh, ...) \
  if (fprintf (fh, __VA_ARGS__) < 0) { \
    char errbuf[1024]; \
    WARNING ("handle_listval: failed to write to socket #%i: %s", \
        fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
    FREE_EVERYTHING_AND_RETURN (-1); \
  }

int handle_listval (FILE *fh, char *buffer)
{
  char *command;
  char **names = NULL;
  cdtime_t *times = NULL;
  size_t number = 0;
  size_t i;
  int status;

  regex_t re_host;
  regex_t re_plugin;
  regex_t re_plugin_instance;
  regex_t re_type;
  regex_t re_type_instance;
  _Bool have_re_host = 0;
  _Bool have_re_plugin = 0;
  _Bool have_re_plugin_instance = 0;
  _Bool have_re_type = 0;
  _Bool have_re_type_instance = 0;

  DEBUG ("utils_cmd_listval: handle_listval (fh = %p, buffer = %s);",
      (void *) fh, buffer);

  command = NULL;
  status = parse_string (&buffer, &command);
  if (status != 0)
  {
    print_to_socket (fh, "-1 Cannot parse command.\n");
    FREE_EVERYTHING_AND_RETURN (-1);
  }
  assert (command != NULL);

  if (strcasecmp ("LISTVAL", command) != 0)
  {
    print_to_socket (fh, "-1 Unexpected command: `%s'.\n", command);
    FREE_EVERYTHING_AND_RETURN (-1);
  }

  /* Parse the options which may still be contained in the buffer. Valid
   * options are "host", "plugin", "plugin_instance", "type" and
   * "type_instance"; each option takes a regular expression as argument which
   * is used to filter the returned identifiers. */
  while (*buffer != 0)
  {
    char *opt_key;
    char *opt_value;
    regex_t *re;
    _Bool *have_re;

    opt_key = NULL;
    opt_value = NULL;
    status = parse_option (&buffer, &opt_key, &opt_value);
    if (status != 0)
    {
      print_to_socket (fh, "-1 Parsing options failed.\n");
      FREE_EVERYTHING_AND_RETURN (-1);
    }

    if (strcasecmp ("host", opt_key) == 0)
    {
      re = &re_host;
      have_re = &have_re_host;
    }
    else if (strcasecmp ("plugin", opt_key) == 0)
    {
      re = &re_plugin;
      have_re = &have_re_plugin;
    }
    else if (strcasecmp ("plugin_instance", opt_key) == 0)
    {
      re = &re_plugin_instance;
      have_re = &have_re_plugin_instance;
    }
    else if (strcasecmp ("type", opt_key) == 0)
    {
      re = &re_type;
      have_re = &have_re_type;
    }
    else if (strcasecmp ("type_instance", opt_key) == 0)
    {
      re = &re_type_instance;
      have_re = &have_re_type_instance;
    }
    else
    {
      print_to_socket (fh, "-1 Unknown option: %s\n", opt_key);
      FREE_EVERYTHING_AND_RETURN (-1);
    }

    /* Free a previous regular expression */
    if (*have_re)
    {
      NOTICE ("listval command: More than one match for part \"%s\". "
          "Only the last regular expression will be used to search "
          "for matching value lists!",
          opt_key);
      regfree (re);
      *have_re = 0;
    }

    /* Compile the regular expression. */
    status = regcomp (re, opt_value, REG_EXTENDED | REG_NOSUB);
    if (status != 0)
    {
      char errbuf[1024];
      regerror (status, re, errbuf, sizeof (errbuf));
      errbuf[sizeof (errbuf) - 1] = 0;
      print_to_socket (fh, "-1 Compiling %s regex failed: %s\n",
          opt_key, errbuf);
      FREE_EVERYTHING_AND_RETURN (-1);
    }
    *have_re = 1;
  } /* while (*buffer != 0) */

  /* Get a list of values from the cache. */
  status = uc_get_names (&names, &times, &number);
  if (status != 0)
  {
    DEBUG ("command listval: uc_get_names failed with status %i", status);
    print_to_socket (fh, "-1 uc_get_names failed.\n");
    FREE_EVERYTHING_AND_RETURN (-1);
  }

  /* If no regex has been specified, take the easy way out. This will avoid a
   * lot of pointless if-blocks. */
  if (!have_re_host
      && !have_re_plugin
      && !have_re_plugin_instance
      && !have_re_type
      && !have_re_type_instance)
  {
    print_to_socket (fh, "%i Value%s found\n",
        (int) number, (number == 1) ? "" : "s");
    for (i = 0; i < number; i++)
      print_to_socket (fh, "%.3f %s\n", CDTIME_T_TO_DOUBLE (times[i]),
          names[i]);
  }
  else /* At least one regular expression is present. */
  {
    char *matching_names[number];
    cdtime_t matching_times[number];
    size_t matching_number = 0;

    /* We need to figure out how many values we're going to return for the
     * status line first. We save all matched values in the above arrays to
     * avoid doing the matching twice. */
    for (i = 0; i < number; i++)
    {
      value_list_t vl = VALUE_LIST_INIT;

      status = parse_identifier_vl (names[i], &vl);
      if (status != 0)
        continue;

      /* If a regex exists and doesn't match, ignore this value and continue
       * with the next one. */
      if (have_re_host && (regexec (&re_host,
              /* string = */ vl.host,
              /* nmatch = */ 0,
              /* pmatch = */ NULL,
              /* flags  = */ 0) == REG_NOMATCH))
        continue;
      if (have_re_plugin && (regexec (&re_plugin,
              /* string = */ vl.plugin,
              /* nmatch = */ 0,
              /* pmatch = */ NULL,
              /* flags  = */ 0) == REG_NOMATCH))
        continue;
      if (have_re_plugin_instance && (regexec (&re_plugin_instance,
              /* string = */ vl.plugin_instance,
              /* nmatch = */ 0,
              /* pmatch = */ NULL,
              /* flags  = */ 0) == REG_NOMATCH))
        continue;
      if (have_re_type && (regexec (&re_type,
              /* string = */ vl.type,
              /* nmatch = */ 0,
              /* pmatch = */ NULL,
              /* flags  = */ 0) == REG_NOMATCH))
        continue;
      if (have_re_type_instance && (regexec (&re_type_instance,
              /* string = */ vl.type_instance,
              /* nmatch = */ 0,
              /* pmatch = */ NULL,
              /* flags  = */ 0) == REG_NOMATCH))
        continue;

      matching_names[matching_number] = names[i];
      matching_times[matching_number] = times[i];
      matching_number++;
    }

    print_to_socket (fh, "%zu Matching value%s\n",
        matching_number, (matching_number == 1) ? "" : "s");
    for (i = 0; i < matching_number; i++)
      print_to_socket (fh, "%.3f %s\n",
          CDTIME_T_TO_DOUBLE (matching_times[i]),
          matching_names[i]);
  }

  FREE_EVERYTHING_AND_RETURN (0);
} /* int handle_listval */

/* vim: set sw=2 sts=2 ts=8 et : */
