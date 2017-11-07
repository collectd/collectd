/*-
 * collectd - src/hugepages.c
 * MIT License
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Jaroslav Safka <jaroslavx.safka@intel.com>
 *   Kim-Marie Jones <kim-marie.jones@intel.com>
 *   Florian Forster <octo at collectd.org>
 */

#include "collectd.h"

#include "common.h" /* auxiliary functions */
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */

static const char g_plugin_name[] = "hugepages";

static _Bool g_flag_rpt_numa = 1;
static _Bool g_flag_rpt_mm = 1;

static _Bool g_values_pages = 1;
static _Bool g_values_bytes = 0;
static _Bool g_values_percent = 0;

#define HP_HAVE_NR 0x01
#define HP_HAVE_SURPLUS 0x02
#define HP_HAVE_FREE 0x04
#define HP_HAVE_ALL 0x07

struct entry_info {
  char *d_name;
  const char *node;
  size_t page_size_kb;

  gauge_t nr;
  gauge_t surplus;
  gauge_t free;
  uint8_t flags;
};

static int hp_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("ReportPerNodeHP", child->key) == 0)
      cf_util_get_boolean(child, &g_flag_rpt_numa);
    else if (strcasecmp("ReportRootHP", child->key) == 0)
      cf_util_get_boolean(child, &g_flag_rpt_mm);
    else if (strcasecmp("ValuesPages", child->key) == 0)
      cf_util_get_boolean(child, &g_values_pages);
    else if (strcasecmp("ValuesBytes", child->key) == 0)
      cf_util_get_boolean(child, &g_values_bytes);
    else if (strcasecmp("ValuesPercentage", child->key) == 0)
      cf_util_get_boolean(child, &g_values_percent);
    else
      ERROR("%s: Invalid configuration option: \"%s\".", g_plugin_name,
            child->key);
  }

  return 0;
}

static void submit_hp(const struct entry_info *info) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = NAN};
  vl.values_len = 1;

  sstrncpy(vl.plugin, g_plugin_name, sizeof(vl.plugin));
  if (info->node) {
    snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s-%zuKb",
             info->node, info->page_size_kb);
  } else {
    snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%zuKb",
             info->page_size_kb);
  }

  /* ensure all metrics have the same timestamp */
  vl.time = cdtime();

  gauge_t free = info->free;
  gauge_t used = (info->nr + info->surplus) - info->free;

  if (g_values_pages) {
    sstrncpy(vl.type, "vmpage_number", sizeof(vl.type));
    plugin_dispatch_multivalue(&vl, /* store_percentage = */ 0, DS_TYPE_GAUGE,
                               "free", free, "used", used, NULL);
  }
  if (g_values_bytes) {
    gauge_t page_size = (gauge_t)(1024 * info->page_size_kb);
    sstrncpy(vl.type, "memory", sizeof(vl.type));
    plugin_dispatch_multivalue(&vl, /* store_percentage = */ 0, DS_TYPE_GAUGE,
                               "free", free * page_size, "used",
                               used * page_size, NULL);
  }
  if (g_values_percent) {
    sstrncpy(vl.type, "percent", sizeof(vl.type));
    plugin_dispatch_multivalue(&vl, /* store_percentage = */ 1, DS_TYPE_GAUGE,
                               "free", free, "used", used, NULL);
  }
}

static int read_hugepage_entry(const char *path, const char *entry,
                               void *e_info) {
  char path2[PATH_MAX];
  struct entry_info *info = e_info;
  double value;

  snprintf(path2, sizeof(path2), "%s/%s", path, entry);

  FILE *fh = fopen(path2, "rt");
  if (fh == NULL) {
    ERROR("%s: cannot open %s", g_plugin_name, path2);
    return -1;
  }

  if (fscanf(fh, "%lf", &value) != 1) {
    ERROR("%s: cannot parse file %s", g_plugin_name, path2);
    fclose(fh);
    return -1;
  }
  fclose(fh);

  if (strcmp(entry, "nr_hugepages") == 0) {
    info->nr = value;
    info->flags |= HP_HAVE_NR;
  } else if (strcmp(entry, "surplus_hugepages") == 0) {
    info->surplus = value;
    info->flags |= HP_HAVE_SURPLUS;
  } else if (strcmp(entry, "free_hugepages") == 0) {
    info->free = value;
    info->flags |= HP_HAVE_FREE;
  }

  if (info->flags != HP_HAVE_ALL) {
    return 0;
  }

  submit_hp(info);

  /* Reset flags so subsequent calls don't submit again. */
  info->flags = 0;
  return 0;
}

static int read_syshugepages(const char *path, const char *node) {
  static const char hugepages_dir[] = "hugepages-";
  DIR *dir;
  struct dirent *result;
  char path2[PATH_MAX];

  dir = opendir(path);
  if (dir == NULL) {
    ERROR("%s: cannot open directory %s", g_plugin_name, path);
    return -1;
  }

  /* read "hugepages-XXXXXkB" entries */
  while ((result = readdir(dir)) != NULL) {
    if (strncmp(result->d_name, hugepages_dir, sizeof(hugepages_dir) - 1)) {
      /* not node dir */
      errno = 0;
      continue;
    }

    long page_size = strtol(result->d_name + strlen(hugepages_dir),
                            /* endptr = */ NULL, /* base = */ 10);
    if (errno != 0) {
      ERROR("%s: failed to determine page size from directory name \"%s\": %s",
            g_plugin_name, result->d_name, STRERRNO);
      continue;
    }

    /* /sys/devices/system/node/node?/hugepages/ */
    snprintf(path2, sizeof(path2), "%s/%s", path, result->d_name);

    walk_directory(path2, read_hugepage_entry,
                   &(struct entry_info){
                       .d_name = result->d_name,
                       .node = node,
                       .page_size_kb = (size_t)page_size,
                   },
                   /* hidden = */ 0);
    errno = 0;
  }

  /* Check if NULL return from readdir() was an error */
  if (errno != 0) {
    ERROR("%s: readdir failed", g_plugin_name);
    closedir(dir);
    return -1;
  }

  closedir(dir);
  return 0;
}

static int read_nodes(void) {
  static const char sys_node[] = "/sys/devices/system/node";
  static const char node_string[] = "node";
  static const char sys_node_hugepages[] =
      "/sys/devices/system/node/%s/hugepages";
  DIR *dir;
  struct dirent *result;
  char path[PATH_MAX];

  dir = opendir(sys_node);
  if (dir == NULL) {
    ERROR("%s: cannot open directory %s", g_plugin_name, sys_node);
    return -1;
  }

  while ((result = readdir(dir)) != NULL) {
    if (strncmp(result->d_name, node_string, sizeof(node_string) - 1)) {
      /* not node dir */
      errno = 0;
      continue;
    }

    snprintf(path, sizeof(path), sys_node_hugepages, result->d_name);
    read_syshugepages(path, result->d_name);
    errno = 0;
  }

  /* Check if NULL return from readdir() was an error */
  if (errno != 0) {
    ERROR("%s: readdir failed", g_plugin_name);
    closedir(dir);
    return -1;
  }

  closedir(dir);
  return 0;
}

static int huge_read(void) {
  static const char sys_mm_hugepages[] = "/sys/kernel/mm/hugepages";

  if (g_flag_rpt_mm) {
    if (read_syshugepages(sys_mm_hugepages, "mm") != 0) {
      return -1;
    }
  }
  if (g_flag_rpt_numa) {
    if (read_nodes() != 0) {
      return -1;
    }
  }

  return 0;
}

void module_register(void) {
  plugin_register_complex_config(g_plugin_name, hp_config);
  plugin_register_read(g_plugin_name, huge_read);
}
