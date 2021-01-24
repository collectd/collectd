/**
 * collectd - src/numa.c
 * Copyright (C) 2012       Florian Forster
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

#include "plugin.h"
#include "utils/common/common.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

#ifndef NUMA_ROOT_DIR
#define NUMA_ROOT_DIR "/sys/devices/system/node"
#endif

static int max_node = -1;

enum {
  FAM_NUMA_HIT = 0,
  FAM_NUMA_MISS,
  FAM_NUMA_FOREIGN,
  FAM_NUMA_LOCAL_NODE,
  FAM_NUMA_OTHER_NODE,
  FAM_NUMA_INTERLEAVE_HIT,
  FAM_NUMA_MAX
};

static int numa_read_node(metric_family_t *fams, int node) /* {{{ */
{
  char path[PATH_MAX];
  FILE *fh;
  char buffer[128];
  int status;
  char node_buffer[21];

  snprintf(path, sizeof(path), NUMA_ROOT_DIR "/node%i/numastat", node);

  fh = fopen(path, "r");
  if (fh == NULL) {
    ERROR("numa plugin: Reading node %i failed: open(%s): %s", node, path,
          STRERRNO);
    return -1;
  }

  int success = 0;
  while (fgets(buffer, sizeof(buffer), fh) != NULL) {
    char *fields[4];
    value_t v;

    status = strsplit(buffer, fields, STATIC_ARRAY_SIZE(fields));
    if (status != 2) {
      WARNING("numa plugin: Ignoring line with unexpected "
              "number of fields (node %i).",
              node);
      continue;
    }

    v.counter = 0;
    status = parse_value(fields[1], &v, DS_TYPE_COUNTER);
    if (status != 0)
      continue;

    metric_t m = {0};
    m.value.counter = v.counter;
    snprintf(node_buffer, sizeof(node_buffer), "%i", node);
    metric_label_set(&m, "node", node_buffer);

    if (!strcmp(fields[0], "numa_hit")) {
      metric_family_metric_append(&fams[FAM_NUMA_HIT], m);
      success++;
    } else if (!strcmp(fields[0], "numa_miss")) {
      metric_family_metric_append(&fams[FAM_NUMA_MISS], m);
      success++;
    } else if (!strcmp(fields[0], "numa_foreign")) {
      metric_family_metric_append(&fams[FAM_NUMA_FOREIGN], m);
      success++;
    } else if (!strcmp(fields[0], "local_node")) {
      metric_family_metric_append(&fams[FAM_NUMA_LOCAL_NODE], m);
      success++;
    } else if (!strcmp(fields[0], "other_node")) {
      metric_family_metric_append(&fams[FAM_NUMA_OTHER_NODE], m);
      success++;
    } else if (!strcmp(fields[0], "interleave_hit")) {
      metric_family_metric_append(&fams[FAM_NUMA_INTERLEAVE_HIT], m);
      success++;
    }

    metric_reset(&m);
  }

  fclose(fh);
  return success ? 0 : -1;
} /* }}} int numa_read_node */

static int numa_read(void) /* {{{ */
{
  metric_family_t fams[FAM_NUMA_MAX] = {
      [FAM_NUMA_HIT] =
          {
              .name = "numa_hit_total",
              .type = METRIC_TYPE_COUNTER,
          },
      [FAM_NUMA_MISS] =
          {
              .name = "numa_miss_total",
              .type = METRIC_TYPE_COUNTER,
          },
      [FAM_NUMA_FOREIGN] =
          {
              .name = "numa_foreign_total",
              .type = METRIC_TYPE_COUNTER,
          },
      [FAM_NUMA_LOCAL_NODE] =
          {
              .name = "numa_local_node_total",
              .type = METRIC_TYPE_COUNTER,
          },
      [FAM_NUMA_OTHER_NODE] =
          {
              .name = "numa_other_node_total",
              .type = METRIC_TYPE_COUNTER,
          },
      [FAM_NUMA_INTERLEAVE_HIT] =
          {
              .name = "numa_interleave_hit_total",
              .type = METRIC_TYPE_COUNTER,
          },
  };

  if (max_node < 0) {
    WARNING("numa plugin: No NUMA nodes were detected.");
    return -1;
  }

  int success = 0;
  for (int i = 0; i <= max_node; i++) {
    int status = numa_read_node(fams, i);
    if (status == 0)
      success++;
  }

  if (success != 0) {
    for (size_t i = 0; i < FAM_NUMA_MAX; i++) {
      if (fams[i].metric.num > 0) {
        int status = plugin_dispatch_metric_family(&fams[i]);
        if (status != 0) {
          ERROR("numa plugin: plugin_dispatch_metric_family failed: %s",
                STRERROR(status));
        }
        metric_family_metric_reset(&fams[i]);
      }
    }
  }

  return success ? 0 : -1;
} /* }}} int numa_read */

static int numa_init(void) /* {{{ */
{
  /* Determine the number of nodes on this machine. */
  while (42) {
    char path[PATH_MAX];
    struct stat statbuf = {0};
    int status;

    snprintf(path, sizeof(path), NUMA_ROOT_DIR "/node%i", max_node + 1);

    status = stat(path, &statbuf);
    if (status == 0) {
      max_node++;
      continue;
    } else if (errno == ENOENT) {
      break;
    } else /* ((status != 0) && (errno != ENOENT)) */
    {
      ERROR("numa plugin: stat(%s) failed: %s", path, STRERRNO);
      return -1;
    }
  }

  DEBUG("numa plugin: Found %i nodes.", max_node + 1);
  return 0;
} /* }}} int numa_init */

void module_register(void) {
  plugin_register_init("numa", numa_init);
  plugin_register_read("numa", numa_read);
} /* void module_register */
