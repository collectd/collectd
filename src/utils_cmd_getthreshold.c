/**
 * collectd - src/utils_cmd_getthreshold.c
 * Copyright (C) 2008,2009  Florian octo Forster
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

#include "utils_avltree.h"
#include "utils_threshold.h"
#include "utils_parse_option.h" /* for `parse_string' */
#include "utils_cmd_getthreshold.h"

#define print_to_socket(fh, ...) \
  if (fprintf (fh, __VA_ARGS__) < 0) { \
    char errbuf[1024]; \
    WARNING ("handle_getthreshold: failed to write to socket #%i: %s", \
        fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
    return -1; \
  }

int handle_getthreshold (FILE *fh, char *buffer)
{
  char *command;
  char *identifier;
  char *identifier_copy;

  char *host;
  char *plugin;
  char *plugin_instance;
  char *type;
  char *type_instance;

  value_list_t vl;
  threshold_t threshold;

  int   status;
  size_t i;

  if ((fh == NULL) || (buffer == NULL))
    return (-1);

  DEBUG ("utils_cmd_getthreshold: handle_getthreshold (fh = %p, buffer = %s);",
      (void *) fh, buffer);

  command = NULL;
  status = parse_string (&buffer, &command);
  if (status != 0)
  {
    print_to_socket (fh, "-1 Cannot parse command.\n");
    return (-1);
  }
  assert (command != NULL);

  if (strcasecmp ("GETTHRESHOLD", command) != 0)
  {
    print_to_socket (fh, "-1 Unexpected command: `%s'.\n", command);
    return (-1);
  }

  identifier = NULL;
  status = parse_string (&buffer, &identifier);
  if (status != 0)
  {
    print_to_socket (fh, "-1 Cannot parse identifier.\n");
    return (-1);
  }
  assert (identifier != NULL);

  if (*buffer != 0)
  {
    print_to_socket (fh, "-1 Garbage after end of command: %s\n", buffer);
    return (-1);
  }

  /* parse_identifier() modifies its first argument,
   * returning pointers into it */
  identifier_copy = sstrdup (identifier);

  status = parse_identifier (identifier_copy, &host,
      &plugin, &plugin_instance,
      &type, &type_instance);
  if (status != 0)
  {
    DEBUG ("handle_getthreshold: Cannot parse identifier `%s'.", identifier);
    print_to_socket (fh, "-1 Cannot parse identifier `%s'.\n", identifier);
    sfree (identifier_copy);
    return (-1);
  }

  memset (&vl, 0, sizeof (vl));
  sstrncpy (vl.host, host, sizeof (vl.host));
  sstrncpy (vl.plugin, plugin, sizeof (vl.plugin));
  if (plugin_instance != NULL)
    sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));
  if (type_instance != NULL)
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));
  sfree (identifier_copy);

  memset (&threshold, 0, sizeof (threshold));
  status = ut_search_threshold (&vl, &threshold);
  if (status == ENOENT)
  {
    print_to_socket (fh, "-1 No threshold found for identifier %s\n",
        identifier);
    return (0);
  }
  else if (status != 0)
  {
    print_to_socket (fh, "-1 Error while looking up threshold: %i\n",
        status);
    return (-1);
  }

  /* Lets count the number of lines we'll return. */
  i = 0;
  if (threshold.host[0] != 0)            i++;
  if (threshold.plugin[0] != 0)          i++;
  if (threshold.plugin_instance[0] != 0) i++;
  if (threshold.type[0] != 0)            i++;
  if (threshold.type_instance[0] != 0)   i++;
  if (threshold.data_source[0] != 0)     i++;
  if (!isnan (threshold.warning_min))    i++;
  if (!isnan (threshold.warning_max))    i++;
  if (!isnan (threshold.failure_min))    i++;
  if (!isnan (threshold.failure_max))    i++;
  if (threshold.hysteresis > 0.0)        i++;
  if (threshold.hits > 1)                i++;

  /* Print the response */
  print_to_socket (fh, "%zu Threshold found\n", i);

  if (threshold.host[0] != 0)
    print_to_socket (fh, "Host: %s\n", threshold.host)
  if (threshold.plugin[0] != 0)
    print_to_socket (fh, "Plugin: %s\n", threshold.plugin)
  if (threshold.plugin_instance[0] != 0)
    print_to_socket (fh, "Plugin Instance: %s\n", threshold.plugin_instance)
  if (threshold.type[0] != 0)
    print_to_socket (fh, "Type: %s\n", threshold.type)
  if (threshold.type_instance[0] != 0)
    print_to_socket (fh, "Type Instance: %s\n", threshold.type_instance)
  if (threshold.data_source[0] != 0)
    print_to_socket (fh, "Data Source: %s\n", threshold.data_source)
  if (!isnan (threshold.warning_min))
    print_to_socket (fh, "Warning Min: %g\n", threshold.warning_min)
  if (!isnan (threshold.warning_max))
    print_to_socket (fh, "Warning Max: %g\n", threshold.warning_max)
  if (!isnan (threshold.failure_min))
    print_to_socket (fh, "Failure Min: %g\n", threshold.failure_min)
  if (!isnan (threshold.failure_max))
    print_to_socket (fh, "Failure Max: %g\n", threshold.failure_max)
  if (threshold.hysteresis > 0.0)
    print_to_socket (fh, "Hysteresis: %g\n", threshold.hysteresis)
  if (threshold.hits > 1)
    print_to_socket (fh, "Hits: %i\n", threshold.hits)

  return (0);
} /* int handle_getthreshold */

/* vim: set sw=2 sts=2 ts=8 et : */
