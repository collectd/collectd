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
 */

#include "collectd.h"
#include "common.h" /* auxiliary functions */
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */

static const char g_plugin_name[] = "hugepages";

static _Bool g_flag_rpt_numa = 1;
static _Bool g_flag_rpt_mm = 1;

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
    else
      ERROR("%s: Invalid configuration option: \"%s\".", g_plugin_name,
            child->key);
  }

  return (0);
}

static void submit_hp(const char *plug_inst, const char *type_instance,
                      gauge_t free_value, gauge_t used_value) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {
    { .gauge = free_value },
    { .gauge = used_value },
  };

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE (values);
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, g_plugin_name, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plug_inst, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "hugepages", sizeof(vl.type));

  if (type_instance != NULL) {
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));
  }

  DEBUG("submit_hp pl_inst:%s, inst_type %s, free=%lf, used=%lf", plug_inst,
        type_instance, free_value, used_value);

  plugin_dispatch_values(&vl);
}

static int read_hugepage_entry(const char *path, const char *entry,
                               void *e_info) {
  char path2[PATH_MAX];
  char type_instance[PATH_MAX];
  struct entry_info *info = e_info;
  double value;

  ssnprintf(path2, sizeof(path2), "%s/%s", path, entry);

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

  ssnprintf(type_instance, sizeof(type_instance), "free_used-%zukB",
            info->page_size_kb);
  submit_hp(info->node, type_instance, info->free,
            (info->nr + info->surplus) - info->free);

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
      char errbuf[1024];
      ERROR("%s: failed to determine page size from directory name \"%s\": %s",
            g_plugin_name, result->d_name,
            sstrerror(errno, errbuf, sizeof(errbuf)));
      continue;
    }

    /* /sys/devices/system/node/node?/hugepages/ */
    ssnprintf(path2, sizeof(path2), "%s/%s", path, result->d_name);

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

    ssnprintf(path, sizeof(path), sys_node_hugepages, result->d_name);
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
