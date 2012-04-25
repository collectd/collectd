/**
 * collectd - src/numa.c
 * Copyright (C) 2012  Florian Forster
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
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

#ifndef NUMA_ROOT_DIR
# define NUMA_ROOT_DIR "/sys/devices/system/node"
#endif

static int max_node = -1;

static void numa_dispatch_value (int node, /* {{{ */
    const char *type_instance, value_t v)
{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &v;
  vl.values_len = 1;

  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "numa", sizeof (vl.plugin));
  ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance), "node%i", node);
  sstrncpy (vl.type, "vmpage_action", sizeof (vl.type));
  sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* }}} void numa_dispatch_value */

static int numa_read_node (int node) /* {{{ */
{
  char path[PATH_MAX];
  FILE *fh;
  char buffer[128];
  int status;
  int success;

  ssnprintf (path, sizeof (path), NUMA_ROOT_DIR "/node%i/numastat", node);

  fh = fopen (path, "r");
  if (fh == NULL)
  {
    char errbuf[1024];
    ERROR ("numa plugin: Reading node %i failed: open(%s): %s",
        node, path, sstrerror (errno, errbuf, sizeof (errbuf)));
    return (-1);
  }

  success = 0;
  while (fgets (buffer, sizeof (buffer), fh) != NULL)
  {
    char *fields[4];
    value_t v;

    status = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
    if (status != 2)
    {
      WARNING ("numa plugin: Ignoring line with unexpected "
          "number of fields (node %i).", node);
      continue;
    }

    v.derive = 0;
    status = parse_value (fields[1], &v, DS_TYPE_DERIVE);
    if (status != 0)
      continue;

    numa_dispatch_value (node, fields[0], v);
    success++;
  }

  fclose (fh);
  return (success ? 0 : -1);
} /* }}} int numa_read_node */

static int numa_read (void) /* {{{ */
{
  int i;
  int status;
  int success;

  if (max_node < 0)
  {
    WARNING ("numa plugin: No NUMA nodes were detected.");
    return (-1);
  }

  success = 0;
  for (i = 0; i <= max_node; i++)
  {
    status = numa_read_node (i);
    if (status == 0)
      success++;
  }

  return (success ? 0 : -1);
} /* }}} int numa_read */

static int numa_init (void) /* {{{ */
{
  /* Determine the number of nodes on this machine. */
  while (42)
  {
    char path[PATH_MAX];
    struct stat statbuf;
    int status;

    ssnprintf (path, sizeof (path), NUMA_ROOT_DIR "/node%i", max_node + 1);
    memset (&statbuf, 0, sizeof (statbuf));

    status = stat (path, &statbuf);
    if (status == 0)
    {
      max_node++;
      continue;
    }
    else if (errno == ENOENT)
    {
      break;
    }
    else /* ((status != 0) && (errno != ENOENT)) */
    {
      char errbuf[1024];
      ERROR ("numa plugin: stat(%s) failed: %s", path,
          sstrerror (errno, errbuf, sizeof (errbuf)));
      return (-1);
    }
  }

  DEBUG ("numa plugin: Found %i nodes.", max_node + 1);
  return (0);
} /* }}} int numa_init */

void module_register (void)
{
  plugin_register_init ("numa", numa_init);
  plugin_register_read ("numa", numa_read);
} /* void module_register */

/* vim: set sw=2 sts=2 et : */
