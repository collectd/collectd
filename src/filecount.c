/**
 * collectd - src/filecount.c
 * Copyright (C) 2008  Alessandro Iurlano
 * Copyright (C) 2008  Florian octo Forster
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
 *   Alessandro Iurlano <alessandro.iurlano at gmail.com>
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <dirent.h>
#include <fcntl.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <sys/types.h>

#define FC_RECURSIVE 1
#define FC_HIDDEN 2
#define FC_REGULAR 4

struct fc_directory_conf_s {
  char *path;
  char *plugin_name;
  char *instance;
  char *files_size_type;
  char *files_num_type;
  char *type_instance;

  int options;

  /* Data counters */
  uint64_t files_num;
  uint64_t files_size;

  /* Selectors */
  char *name;
  int64_t mtime;
  int64_t size;

  /* Helper for the recursive functions */
  time_t now;
};
typedef struct fc_directory_conf_s fc_directory_conf_t;

static fc_directory_conf_t **directories;
static size_t directories_num;

static void fc_free_dir(fc_directory_conf_t *dir) {
  sfree(dir->path);
  sfree(dir->plugin_name);
  sfree(dir->instance);
  sfree(dir->files_size_type);
  sfree(dir->files_num_type);
  sfree(dir->type_instance);
  sfree(dir->name);

  sfree(dir);
} /* void fc_free_dir */

static void fc_submit_dir(const fc_directory_conf_t *dir) {
  value_list_t vl = VALUE_LIST_INIT;

  sstrncpy(vl.plugin, dir->plugin_name, sizeof(vl.plugin));
  if (dir->instance != NULL)
    sstrncpy(vl.plugin_instance, dir->instance, sizeof(vl.plugin_instance));
  if (dir->type_instance != NULL)
    sstrncpy(vl.type_instance, dir->type_instance, sizeof(vl.type_instance));

  vl.values_len = 1;

  if (dir->files_num_type != NULL) {
    vl.values = &(value_t){.gauge = (gauge_t)dir->files_num};
    sstrncpy(vl.type, dir->files_num_type, sizeof(vl.type));
    plugin_dispatch_values(&vl);
  }

  if (dir->files_size_type != NULL) {
    vl.values = &(value_t){.gauge = (gauge_t)dir->files_size};
    sstrncpy(vl.type, dir->files_size_type, sizeof(vl.type));
    plugin_dispatch_values(&vl);
  }
} /* void fc_submit_dir */

/*
 * Config:
 * <Plugin filecount>
 *   <Directory /path/to/dir>
 *     Plugin "foo"
 *     Instance "foobar"
 *     Name "*.conf"
 *     MTime -3600
 *     Size "+10M"
 *     Recursive true
 *     IncludeHidden false
 *     FilesSizeType "bytes"
 *     FilesCountType "files"
 *     TypeInstance "instance"
 *   </Directory>
 * </Plugin>
 *
 * Collect:
 * - Number of files
 * - Total size
 */

static int fc_config_set_instance(fc_directory_conf_t *dir, const char *str) {
  char buffer[1024];
  char *ptr;

  sstrncpy(buffer, str, sizeof(buffer));
  for (ptr = buffer; *ptr != 0; ptr++)
    if (*ptr == '/')
      *ptr = '_';

  for (ptr = buffer; *ptr == '_'; ptr++)
    /* do nothing */;

  char *copy = strdup(ptr);
  if (copy == NULL)
    return -1;

  sfree(dir->instance);
  dir->instance = copy;

  return 0;
} /* int fc_config_set_instance */

static int fc_config_add_dir_instance(fc_directory_conf_t *dir,
                                      oconfig_item_t *ci) {
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("filecount plugin: The `Instance' config option needs exactly "
            "one string argument.");
    return -1;
  }

  return fc_config_set_instance(dir, ci->values[0].value.string);
} /* int fc_config_add_dir_instance */

static int fc_config_add_dir_mtime(fc_directory_conf_t *dir,
                                   oconfig_item_t *ci) {
  if ((ci->values_num != 1) || ((ci->values[0].type != OCONFIG_TYPE_STRING) &&
                                (ci->values[0].type != OCONFIG_TYPE_NUMBER))) {
    WARNING("filecount plugin: The `MTime' config option needs exactly one "
            "string or numeric argument.");
    return -1;
  }

  if (ci->values[0].type == OCONFIG_TYPE_NUMBER) {
    dir->mtime = (int64_t)ci->values[0].value.number;
    return 0;
  }

  errno = 0;
  char *endptr = NULL;
  double temp = strtod(ci->values[0].value.string, &endptr);
  if ((errno != 0) || (endptr == NULL) ||
      (endptr == ci->values[0].value.string)) {
    WARNING("filecount plugin: Converting `%s' to a number failed.",
            ci->values[0].value.string);
    return -1;
  }

  switch (*endptr) {
  case 0:
  case 's':
  case 'S':
    break;

  case 'm':
  case 'M':
    temp *= 60;
    break;

  case 'h':
  case 'H':
    temp *= 3600;
    break;

  case 'd':
  case 'D':
    temp *= 86400;
    break;

  case 'w':
  case 'W':
    temp *= 7 * 86400;
    break;

  case 'y':
  case 'Y':
    temp *= 31557600; /* == 365.25 * 86400 */
    break;

  default:
    WARNING("filecount plugin: Invalid suffix for `MTime': `%c'", *endptr);
    return -1;
  } /* switch (*endptr) */

  dir->mtime = (int64_t)temp;

  return 0;
} /* int fc_config_add_dir_mtime */

static int fc_config_add_dir_size(fc_directory_conf_t *dir,
                                  oconfig_item_t *ci) {
  if ((ci->values_num != 1) || ((ci->values[0].type != OCONFIG_TYPE_STRING) &&
                                (ci->values[0].type != OCONFIG_TYPE_NUMBER))) {
    WARNING("filecount plugin: The `Size' config option needs exactly one "
            "string or numeric argument.");
    return -1;
  }

  if (ci->values[0].type == OCONFIG_TYPE_NUMBER) {
    dir->size = (int64_t)ci->values[0].value.number;
    return 0;
  }

  errno = 0;
  char *endptr = NULL;
  double temp = strtod(ci->values[0].value.string, &endptr);
  if ((errno != 0) || (endptr == NULL) ||
      (endptr == ci->values[0].value.string)) {
    WARNING("filecount plugin: Converting `%s' to a number failed.",
            ci->values[0].value.string);
    return -1;
  }

  switch (*endptr) {
  case 0:
  case 'b':
  case 'B':
    break;

  case 'k':
  case 'K':
    temp *= 1000.0;
    break;

  case 'm':
  case 'M':
    temp *= 1000.0 * 1000.0;
    break;

  case 'g':
  case 'G':
    temp *= 1000.0 * 1000.0 * 1000.0;
    break;

  case 't':
  case 'T':
    temp *= 1000.0 * 1000.0 * 1000.0 * 1000.0;
    break;

  case 'p':
  case 'P':
    temp *= 1000.0 * 1000.0 * 1000.0 * 1000.0 * 1000.0;
    break;

  default:
    WARNING("filecount plugin: Invalid suffix for `Size': `%c'", *endptr);
    return -1;
  } /* switch (*endptr) */

  dir->size = (int64_t)temp;

  return 0;
} /* int fc_config_add_dir_size */

static int fc_config_add_dir_option(fc_directory_conf_t *dir,
                                    oconfig_item_t *ci, int bit) {
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN)) {
    WARNING("filecount plugin: The `Recursive' config options needs exactly "
            "one boolean argument.");
    return -1;
  }

  if (ci->values[0].value.boolean)
    dir->options |= bit;
  else
    dir->options &= ~bit;

  return 0;
} /* int fc_config_add_dir_option */

static int fc_config_add_dir(oconfig_item_t *ci) {
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("filecount plugin: `Directory' needs exactly one string "
            "argument.");
    return -1;
  }

  /* Initialize `dir' */
  fc_directory_conf_t *dir = calloc(1, sizeof(*dir));
  if (dir == NULL) {
    ERROR("filecount plugin: calloc failed.");
    return -1;
  }

  dir->path = strdup(ci->values[0].value.string);
  if (dir->path == NULL) {
    ERROR("filecount plugin: strdup failed.");
    fc_free_dir(dir);
    return -1;
  }

  dir->options = FC_RECURSIVE | FC_REGULAR;

  dir->name = NULL;
  dir->plugin_name = strdup("filecount");
  dir->instance = NULL;
  dir->type_instance = NULL;
  dir->mtime = 0;
  dir->size = 0;

  dir->files_size_type = strdup("bytes");
  dir->files_num_type = strdup("files");

  if (dir->plugin_name == NULL || dir->files_size_type == NULL ||
      dir->files_num_type == NULL) {
    ERROR("filecount plugin: strdup failed.");
    fc_free_dir(dir);
    return -1;
  }

  int status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Plugin", option->key) == 0)
      status = cf_util_get_string(option, &dir->plugin_name);
    else if (strcasecmp("Instance", option->key) == 0)
      status = fc_config_add_dir_instance(dir, option);
    else if (strcasecmp("Name", option->key) == 0)
      status = cf_util_get_string(option, &dir->name);
    else if (strcasecmp("MTime", option->key) == 0)
      status = fc_config_add_dir_mtime(dir, option);
    else if (strcasecmp("Size", option->key) == 0)
      status = fc_config_add_dir_size(dir, option);
    else if (strcasecmp("Recursive", option->key) == 0)
      status = fc_config_add_dir_option(dir, option, FC_RECURSIVE);
    else if (strcasecmp("IncludeHidden", option->key) == 0)
      status = fc_config_add_dir_option(dir, option, FC_HIDDEN);
    else if (strcasecmp("RegularOnly", option->key) == 0)
      status = fc_config_add_dir_option(dir, option, FC_REGULAR);
    else if (strcasecmp("FilesSizeType", option->key) == 0)
      status = cf_util_get_string(option, &dir->files_size_type);
    else if (strcasecmp("FilesCountType", option->key) == 0)
      status = cf_util_get_string(option, &dir->files_num_type);
    else if (strcasecmp("TypeInstance", option->key) == 0)
      status = cf_util_get_string(option, &dir->type_instance);
    else {
      WARNING("filecount plugin: fc_config_add_dir: "
              "Option `%s' not allowed here.",
              option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (ci->children) */

  if (status != 0) {
    fc_free_dir(dir);
    return -1;
  }

  /* Set default plugin instance */
  if (dir->instance == NULL) {
    fc_config_set_instance(dir, dir->path);
    if (dir->instance == NULL || strlen(dir->instance) == 0) {
      ERROR("filecount plugin: failed to build plugin instance name.");
      fc_free_dir(dir);
      return -1;
    }
  }

  /* Handle disabled types */
  if (strlen(dir->instance) == 0)
    sfree(dir->instance);

  if (strlen(dir->files_size_type) == 0)
    sfree(dir->files_size_type);

  if (strlen(dir->files_num_type) == 0)
    sfree(dir->files_num_type);

  if (dir->files_size_type == NULL && dir->files_num_type == NULL) {
    WARNING("filecount plugin: Both `FilesSizeType' and `FilesCountType ' "
            "are disabled for '%s'. There's no types to report.",
            dir->path);
    fc_free_dir(dir);
    return -1;
  }

  /* Ready to add it to list */
  fc_directory_conf_t **temp =
      realloc(directories, sizeof(*directories) * (directories_num + 1));
  if (temp == NULL) {
    ERROR("filecount plugin: realloc failed.");
    fc_free_dir(dir);
    return -1;
  }

  directories = temp;
  directories[directories_num] = dir;
  directories_num++;

  return 0;
} /* int fc_config_add_dir */

static int fc_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Directory", child->key) == 0)
      fc_config_add_dir(child);
    else {
      WARNING("filecount plugin: Ignoring unknown config option `%s'.",
              child->key);
    }
  } /* for (ci->children) */

  return 0;
} /* int fc_config */

static int fc_init(void) {
  if (directories_num < 1) {
    WARNING("filecount plugin: No directories have been configured.");
    return -1;
  }

  return 0;
} /* int fc_init */

static int fc_read_dir_callback(const char *dirname, const char *filename,
                                void *user_data) {
  fc_directory_conf_t *dir = user_data;
  char abs_path[PATH_MAX];
  struct stat statbuf;

  if (dir == NULL)
    return -1;

  snprintf(abs_path, sizeof(abs_path), "%s/%s", dirname, filename);

  int status = lstat(abs_path, &statbuf);
  if (status != 0) {
    ERROR("filecount plugin: stat (%s) failed.", abs_path);
    return -1;
  }

  if (S_ISDIR(statbuf.st_mode) && (dir->options & FC_RECURSIVE)) {
    status = walk_directory(
        abs_path, fc_read_dir_callback, dir,
        /* include hidden = */ (dir->options & FC_HIDDEN) ? 1 : 0);
    return status;
  } else if ((dir->options & FC_REGULAR) && !S_ISREG(statbuf.st_mode)) {
    return 0;
  }

  if (dir->name != NULL) {
    status = fnmatch(dir->name, filename, /* flags = */ 0);
    if (status != 0)
      return 0;
  }

  if (!S_ISREG(statbuf.st_mode)) {
    dir->files_num++;
    return 0;
  }

  if (dir->mtime != 0) {
    time_t mtime = dir->now;

    if (dir->mtime < 0)
      mtime += dir->mtime;
    else
      mtime -= dir->mtime;

    DEBUG("filecount plugin: Only collecting files that were touched %s %u.",
          (dir->mtime < 0) ? "after" : "before", (unsigned int)mtime);

    if (((dir->mtime < 0) && (statbuf.st_mtime < mtime)) ||
        ((dir->mtime > 0) && (statbuf.st_mtime > mtime)))
      return 0;
  }

  if (dir->size != 0) {
    off_t size;

    if (dir->size < 0)
      size = (off_t)((-1) * dir->size);
    else
      size = (off_t)dir->size;

    if (((dir->size < 0) && (statbuf.st_size > size)) ||
        ((dir->size > 0) && (statbuf.st_size < size)))
      return 0;
  }

  dir->files_num++;
  dir->files_size += (uint64_t)statbuf.st_size;

  return 0;
} /* int fc_read_dir_callback */

static int fc_read_dir(fc_directory_conf_t *dir) {
  dir->files_num = 0;
  dir->files_size = 0;

  if (dir->mtime != 0)
    dir->now = time(NULL);

  int status =
      walk_directory(dir->path, fc_read_dir_callback, dir,
                     /* include hidden */ (dir->options & FC_HIDDEN) ? 1 : 0);
  if (status != 0) {
    WARNING("filecount plugin: walk_directory (%s) failed.", dir->path);
    return -1;
  }

  fc_submit_dir(dir);

  return 0;
} /* int fc_read_dir */

static int fc_read(void) {
  for (size_t i = 0; i < directories_num; i++)
    fc_read_dir(directories[i]);

  return 0;
} /* int fc_read */

void module_register(void) {
  plugin_register_complex_config("filecount", fc_config);
  plugin_register_init("filecount", fc_init);
  plugin_register_read("filecount", fc_read);
} /* void module_register */
