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
 */

#include "collectd.h"
#include "common.h" /* auxiliary functions */
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */

#include <stdio.h>
#include <string.h>
#include <dirent.h>

static int huge_read (void);
static int huge_config_callback (const char *key, const char *val);

static const char PLUGIN_NAME[] = "hugepages";
static const char SYS_NODE[] = "/sys/devices/system/node";
static const char NODE[] = "node";
static const char HUGEPAGES_DIR[] = "hugepages";
static const char SYS_NODE_HUGEPAGES[] = "/sys/devices/system/node/%s/hugepages";
static const char SYS_MM_HUGEPAGES[] = "/sys/kernel/mm/hugepages";
static const char CONFIG_NAME[] = "hugepages";
static const char CFG_ENA_NUMA[] = "EnableNuma";
static const char CFG_ENA_MM[] = "EnableMM";

static const char *CONFIG_KEYS[] = {
  CFG_ENA_NUMA,
  CFG_ENA_MM,
};
static const size_t CONFIG_KEYS_NUM = sizeof(CONFIG_KEYS)/sizeof(*CONFIG_NAME);
static int g_config_ena_numa = 1;
static int g_config_ena_mm = 1;

static int huge_config_callback (const char *key, const char *val)
{
  INFO("HugePages config key='%s', val='%s'", key, val);

  if (0 == strcasecmp(key, CFG_ENA_NUMA)) {
    g_config_ena_numa = IS_TRUE(val);
    return 0;
  }
  if (0 == strcasecmp(key, CFG_ENA_MM)) {
    g_config_ena_mm = IS_TRUE(val);
    return 0;
  }

  return -1;
}

static void submit_one (const char *plug_inst, const char *type,
  const char *type_instance, derive_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].derive = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, PLUGIN_NAME, sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, plug_inst, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));

  if (type_instance != NULL) {
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));
  }

  DEBUG("submit_one pl_inst:%s, inst_type %s, type %s, val=%lu",
      plug_inst, type_instance, type, value);

  plugin_dispatch_values (&vl);
}

static int read_hugepage_entry(const char *path, const char* entry,
    const char* plinst, const char* hpsize)
{
  char path2[512];
  long value = 0;
  snprintf(path2, sizeof(path2), "%s/%s", path, entry);

  FILE *fh = fopen(path2, "rt");
  if (NULL == fh) {
    ERROR("Cannot open %s", path2);
    return -1;
  }

  if (fscanf(fh, "%ld", &value) !=1) {
    ERROR("Cannot parse file %s", path2);
    fclose(fh);
    return -1;
  }

  submit_one (plinst, entry, hpsize, value);

  fclose(fh);
  return 0;
}

static int read_syshugepage_dir(const char* path, const char* dirhpsize,
    const char* node)
{
  DIR       *dir = NULL;
  struct dirent *entry = NULL;
  struct dirent *result = NULL;
  size_t name_max = 0;
  size_t len = 0;

  dir = opendir(path);
  if (NULL == dir) {
    ERROR("Cannot open directory %s", path);
    return -1;
  }

  name_max = pathconf(path, _PC_NAME_MAX);
  if (name_max == -1) {    /* Limit not defined, or error */
    name_max = 255;     /* Take a guess */
  }

  len = offsetof(struct dirent, d_name) + name_max + 1;
  entry = malloc(len);
  if (entry == NULL) {
    ERROR("Malloc returned NULL");
    return -1;
  }

  while (0 == readdir_r(dir, entry, &result)) {
    if (NULL == result) {
      /* end of dir */
      break;
    }
    if (result->d_name[0] == '.') {
      /* not interesting "." and ".." */
      continue;
    }

    read_hugepage_entry(path, result->d_name, node, dirhpsize);
  }

  free(entry);
  closedir(dir);


  return 0;
}

static int read_syshugepages(const char* path, const char* node)
{
  DIR       *dir = NULL;
  struct dirent *entry = NULL;
  struct dirent *result = NULL;
  size_t name_max = 0;
  size_t len = 0;
  char path2[255];

  dir = opendir(path);
  if (NULL == dir) {
    ERROR("Cannot open directory %s", path);
    return -1;
  }

  name_max = pathconf(path, _PC_NAME_MAX);
  if (name_max == -1) {    /* Limit not defined, or error */
    name_max = 255;     /* Take a guess */
  }
  len = offsetof(struct dirent, d_name) + name_max + 1;
  entry = malloc(len);
  if (entry == NULL) {
    ERROR("Malloc returned NULL");
    return -1;
  }

  while (0 == readdir_r(dir, entry, &result)) {
    /* read "hugepages-XXXXXkB" entries */
    if (NULL == result) {
      /* end of dir */
      break;
    }

    if (strncmp(result->d_name, HUGEPAGES_DIR, sizeof(HUGEPAGES_DIR)-1)) {
      /* not node dir */
      continue;
    }

    /* /sys/devices/system/node/node?/hugepages/ */
    snprintf(path2, sizeof(path2), "%s/%s", path, result->d_name);
    read_syshugepage_dir(path2, result->d_name, node);
  }

  free(entry);
  closedir(dir);

  return 0;
}

static int read_nodes(void)
{
  DIR       *dir = NULL;
  struct dirent *entry = NULL;
  struct dirent *result = NULL;
  size_t name_max = 0;
  size_t len = 0;
  char path[255];

  dir = opendir(SYS_NODE);
  if (NULL == dir) {
    ERROR("Cannot open directory %s", SYS_NODE);
    return -1;
  }

  name_max = pathconf(SYS_NODE, _PC_NAME_MAX);
  if (name_max == -1) {    /* Limit not defined, or error */
    name_max = 255;     /* Take a guess */
  }
  len = offsetof(struct dirent, d_name) + name_max + 1;
  entry = malloc(len);
  if (entry == NULL) {
    ERROR("Malloc returned NULL");
    return -1;
  }

  while (0 == readdir_r(dir, entry, &result)) {
    if (NULL == result) {
      /* end of dir */
      break;
    }

    if (strncmp(result->d_name, NODE, sizeof(NODE)-1)) {
      /* not node dir */
      continue;
    }

    snprintf(path, sizeof(path), SYS_NODE_HUGEPAGES, result->d_name);
    read_syshugepages(path, result->d_name);
  }

  free(entry);
  closedir(dir);

  return 0;
}


static int huge_read (void)
{
  if (g_config_ena_mm) {
    read_syshugepages(SYS_MM_HUGEPAGES, "mm");
  }
  if (g_config_ena_numa) {
    read_nodes();
  }

  return 0;
}

void module_register (void)
{
	plugin_register_config(CONFIG_NAME, huge_config_callback, CONFIG_KEYS,
                         CONFIG_KEYS_NUM);
  plugin_register_read (PLUGIN_NAME, huge_read);
}

