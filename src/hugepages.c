/*-
 * collectd - src/hugepages.c
 * MIT License
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
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
static const char g_cfg_rpt_numa[] = "ReportPerNodeHP";
static const char g_cfg_rpt_mm[] = "ReportRootHP";

static const char *g_config_keys[] = {
  g_cfg_rpt_numa,
  g_cfg_rpt_mm,
};
static size_t g_config_keys_num = STATIC_ARRAY_SIZE(g_config_keys);
static int g_flag_rpt_numa = 1;
static int g_flag_rpt_mm = 1;

struct entry_info {
  char *d_name;
  const char *node;
};

static int huge_config_callback(const char *key, const char *val)
{
  DEBUG("%s: HugePages config key='%s', val='%s'", g_plugin_name, key, val);

  if (strcasecmp(key, g_cfg_rpt_numa) == 0) {
    g_flag_rpt_numa = IS_TRUE(val);
    return 0;
  }
  if (strcasecmp(key, g_cfg_rpt_mm) == 0) {
    g_flag_rpt_mm = IS_TRUE(val);
    return 0;
  }

  return -1;
}

static void submit_hp(const char *plug_inst, const char *type,
  const char *type_instance, gauge_t free_value, gauge_t used_value)
{
  value_t values[2];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = free_value;
  values[1].gauge = used_value;

  vl.values = values;
  vl.values_len = 2;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, g_plugin_name, sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, plug_inst, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));

  if (type_instance != NULL) {
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));
  }

  DEBUG("submit_hp pl_inst:%s, inst_type %s, type %s, free=%lf, used=%lf",
      plug_inst, type_instance, type, free_value, used_value);

  plugin_dispatch_values (&vl);
}

static int read_hugepage_entry(const char *path, const char *entry,
    void *e_info)
{
  char path2[PATH_MAX];
  static const char type[] = "hugepages";
  static const char partial_type_inst[] = "free_used";
  char type_instance[PATH_MAX];
  char *strin;
  struct entry_info *hpsize_plinst = e_info;
  static int flag = 0;
  static double used_hp = 0;
  static double free_hp = 0;
  double value;

  ssnprintf(path2, sizeof(path2), "%s/%s", path, entry);

  FILE *fh = fopen(path2, "rt");
  if (fh == NULL) {
    ERROR("%s: cannot open %s", g_plugin_name, path2);
    return -1;
  }

  if (fscanf(fh, "%lf", &value) !=1) {
    ERROR("%s: cannot parse file %s", g_plugin_name, path2);
    fclose(fh);
    return -1;
  }

  if (strcmp(entry, "nr_hugepages") == 0) {
    used_hp += value;
    flag++;
  } else if (strcmp(entry, "surplus_hugepages") == 0) {
    used_hp += value;
    flag++;
  } else if (strcmp(entry, "free_hugepages") == 0) {
    used_hp -= value;
    free_hp = value;
    flag++;
  }

  if (flag == 3) {
    /* Can now submit "used" and "free" values.
     * 0x2D is the ASCII "-" character, after which the string
     *   contains "<size>kB"
     * The string passed as param 3 to submit_hp is of the format:
     *   <type>-<partial_type_inst>-<size>kB
     */
    strin = strchr(hpsize_plinst->d_name, 0x2D);
    if (strin != NULL) {
      ssnprintf(type_instance, sizeof(type_instance), "%s%s", partial_type_inst, strin);
    } else {
      ssnprintf(type_instance, sizeof(type_instance), "%s%s", partial_type_inst,
          hpsize_plinst->d_name);
    }
    submit_hp(hpsize_plinst->node, type, type_instance, free_hp, used_hp);

    /* Reset for next time */
    flag = 0;
    used_hp = 0;
    free_hp = 0;
  }

  fclose(fh);
  return 0;
}

static int read_syshugepages(const char* path, const char* node)
{
  static const char hugepages_dir[] = "hugepages";
  DIR *dir;
  struct dirent *result;
  char path2[PATH_MAX];
  struct entry_info e_info;
  long lim;

  dir = opendir(path);
  if (dir == NULL) {
    ERROR("%s: cannot open directory %s", g_plugin_name, path);
    return -1;
  }

  errno = 0;
  if ((lim = pathconf(path, _PC_NAME_MAX)) == -1) {
    /* Limit not defined if errno == 0, otherwise error */
    if (errno != 0) {
      ERROR("%s: pathconf failed", g_plugin_name);
      closedir(dir);
      return -1;
    } else {
      lim = PATH_MAX;
    }
  }

  /* read "hugepages-XXXXXkB" entries */
  while ((result = readdir(dir)) != NULL) {
    if (strncmp(result->d_name, hugepages_dir, sizeof(hugepages_dir)-1)) {
      /* not node dir */
      errno = 0;
      continue;
    }

    /* /sys/devices/system/node/node?/hugepages/ */
    ssnprintf(path2, (size_t) lim, "%s/%s", path, result->d_name);

    e_info.d_name = result->d_name;
    e_info.node = node;
    walk_directory(path2, read_hugepage_entry, &e_info, 0);
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

static int read_nodes(void)
{
  static const char sys_node[] = "/sys/devices/system/node";
  static const char node_string[] = "node";
  static const char sys_node_hugepages[] = "/sys/devices/system/node/%s/hugepages";
  DIR *dir;
  struct dirent *result;
  char path[PATH_MAX];
  long lim;

  dir = opendir(sys_node);
  if (dir == NULL) {
    ERROR("%s: cannot open directory %s", g_plugin_name, sys_node);
    return -1;
  }

  errno = 0;
  if ((lim = pathconf(sys_node, _PC_NAME_MAX)) == -1) {
    /* Limit not defined if errno == 0, otherwise error */
    if (errno != 0) {
      ERROR("%s: pathconf failed", g_plugin_name);
      closedir(dir);
      return -1;
    } else {
      lim = PATH_MAX;
    }
  }

  while ((result = readdir(dir)) != NULL) {
    if (strncmp(result->d_name, node_string, sizeof(node_string)-1)) {
      /* not node dir */
      errno = 0;
      continue;
    }

    ssnprintf(path, (size_t) lim, sys_node_hugepages, result->d_name);
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


static int huge_read(void)
{
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

void module_register(void)
{
  plugin_register_config(g_plugin_name, huge_config_callback, g_config_keys,
      g_config_keys_num);
  plugin_register_read(g_plugin_name, huge_read);
}

