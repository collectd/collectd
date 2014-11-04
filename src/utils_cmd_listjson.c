/**
 * collectd - src/utils_cmd_listjson.c
 * Copyright (C) 2008       Florian octo Forster
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

#include "utils_cmd_listjson.h"
#include "utils_cache.h"
#include "utils_parse_option.h"


#define getjson_print_to_socket(fh, ...) \
  if (fprintf (fh, __VA_ARGS__) < 0) { \
    char errbuf[1024]; \
    WARNING ("handle_getjson: failed to write to socket #%i: %s", \
        fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
    return -1; \
  }

#define free_everything_and_return(status) do { \
    size_t j; \
    for (j = 0; j < number; j++) { \
      sfree(names[j]); \
      names[j] = NULL; \
    } \
    sfree(names); \
    sfree(times); \
    return (status); \
  } while (0)

#define print_to_socket(fh, ...) \
  if (fprintf (fh, __VA_ARGS__) < 0) { \
    char errbuf[1024]; \
    WARNING ("handle_listjson: failed to write to socket #%i: %s", \
	fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf))); \
    free_everything_and_return (-1); \
  }


int handle_getjson (FILE *fh, char *buffer)
{
  char *identifier;
  char *identifier_copy;

  char *hostname;
  char *plugin;
  char *plugin_instance;
  char *type;
  char *type_instance;
  gauge_t *values;
  size_t values_num;

  const data_set_t *ds;

  int   status;
  size_t i;

  if ((fh == NULL) || (buffer == NULL))
    return (-1);

  identifier = NULL;
  status = parse_string (&buffer, &identifier);
  if (status != 0)
  {
    getjson_print_to_socket (fh, "-1 Cannot parse identifier.\n");
    return (-1);
  }
  assert (identifier != NULL);

  if (*buffer != 0)
  {
    getjson_print_to_socket (fh, "-1 Garbage after end of command: %s\n", buffer);
    return (-1);
  }

  /* parse_identifier() modifies its first argument,
   * returning pointers into it */
  identifier_copy = sstrdup (identifier);

  status = parse_identifier (identifier_copy, &hostname,
      &plugin, &plugin_instance,
      &type, &type_instance);
  if (status != 0)
  {
    DEBUG ("handle_getjson: Cannot parse identifier `%s'.", identifier);
    getjson_print_to_socket (fh, "-1 Cannot parse identifier `%s'.\n", identifier);
    sfree (identifier_copy);
    return (-1);
  }

  ds = plugin_get_ds (type);
  if (ds == NULL)
  {
    DEBUG ("handle_getjson: plugin_get_ds (%s) == NULL;", type);
    getjson_print_to_socket (fh, "-1 Type `%s' is unknown.\n", type);
    sfree (identifier_copy);
    return (-1);
  }

  values = NULL;
  values_num = 0;
  status = uc_get_rate_by_name (identifier, &values, &values_num);
  if (status != 0)
  {
    getjson_print_to_socket (fh, "-1 No such value\n");
    sfree (identifier_copy);
    return (-1);
  }

  if ((size_t) ds->ds_num != values_num)
  {
    ERROR ("ds[%s]->ds_num = %i, "
	"but uc_get_rate_by_name returned %u values.",
	ds->type, ds->ds_num, (unsigned int) values_num);
    getjson_print_to_socket (fh, "-1 Error reading value from cache.\n");
    sfree (values);
    sfree (identifier_copy);
    return (-1);
  }

  getjson_print_to_socket (fh, ": ")
  if (values_num == 1)
  {
    if (isnan (values[0]))
    {
      getjson_print_to_socket (fh, "null");
    }
    else
    {
      getjson_print_to_socket (fh, "%12e", values[0]);
    }
  }
  else
  {
    getjson_print_to_socket (fh, "{\n")
    for (i = 0; i < values_num; i++)
    {
      if (i > 0)
      {
        getjson_print_to_socket (fh, ",\n");
      }
      getjson_print_to_socket (fh, "  \"%s\": ", ds->ds[i].name);
      if (isnan (values[i]))
      {
        getjson_print_to_socket (fh, "null");
      }
      else
      {
        getjson_print_to_socket (fh, "%12e", values[i]);
      }
    }
    getjson_print_to_socket (fh, "\n}")
  }

  sfree (values);
  sfree (identifier_copy);

  return (0);
} /* int handle_getjson */


int handle_listjson (FILE *fh,_Bool strip_hostnames)
{
  char **names = NULL;
  char *name = NULL;
  cdtime_t *times = NULL;
  size_t number = 0;
  size_t i;
  int status;

  DEBUG ("utils_cmd_listjson: handle_listjson (fh = %p);", (void *) fh);

  status = uc_get_names (&names, &times, &number);
  if (status != 0)
  {
    DEBUG ("command listjson: uc_get_names failed with status %i", status);
    print_to_socket (fh, "-1 uc_get_names failed.\n");
    free_everything_and_return (-1);
  }

  print_to_socket (fh, "{\n");
  for (i = 0; i < number; i++)
  {
    if (i > 0)
    {
      print_to_socket (fh, ",\n");
    }
    name = names[i]
    if (strip_hostnames)
    {
      char *s;
      s = strchr(names[i],'/');
      if (s != NULL)
        name = s + 1;
    }
    print_to_socket (fh, "\"%s\"", name);
    handle_getjson (fh, names[i]);
  }
  print_to_socket (fh, "\n}\n");

  free_everything_and_return (0);
} /* int handle_listjson */


/* vim: set sw=2 sts=2 ts=8 : */
