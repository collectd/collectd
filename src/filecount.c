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

  char *metric_files_size;
  char *metric_files_num;
  label_set_t labels;

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
  sfree(dir->metric_files_size);
  sfree(dir->metric_files_num);
  sfree(dir->name);

  label_set_reset(&dir->labels);

  sfree(dir);
} /* void fc_free_dir */

static void fc_submit_dir(char *name, metric_t *tmpl, gauge_t value) {
  metric_family_t fam = {
      .name = name,
      .type = METRIC_TYPE_GAUGE,
  };

  metric_family_append(&fam, NULL, NULL, (value_t){.gauge = value}, tmpl);

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR("filecount plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }

  metric_family_metric_reset(&fam);
} /* void fc_submit_dir */

/*
 * Config:
 * <Plugin filecount>
 *   <Directory /path/to/dir>
 *     MetricFilesSize "foo_bytes"
 *     MetricFilesNum "foo_files"
 *     Name "*.conf"
 *     MTime -3600
 *     Size "+10M"
 *     Recursive true
 *     IncludeHidden false
 *   </Directory>
 * </Plugin>
 *
 * Collect:
 * - Number of files
 * - Total size
 */

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

  int status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("MetricFilesSize", option->key) == 0)
      status = cf_util_get_string(option, &dir->metric_files_size);
    else if (strcasecmp("MetricFilesCount", option->key) == 0)
      status = cf_util_get_string(option, &dir->metric_files_num);
    else if (strcasecmp("Label", option->key) == 0)
      status = cf_util_get_label(option, &dir->labels);
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

  if (dir->metric_files_size == NULL && dir->metric_files_num == NULL) {
    WARNING("filecount plugin: Both `MetricFilesSize' and `MetricFilesCount' "
            "are disabled for '%s'. There's no metric to report.",
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

  metric_t m = {0};

  for (size_t i = 0; i < dir->labels.num; i++) {
    metric_label_set(&m, dir->labels.ptr[i].name, dir->labels.ptr[i].value);
  }

  if (dir->metric_files_num != NULL) {
    fc_submit_dir(dir->metric_files_num, &m, (gauge_t)dir->files_num);
  }
  if (dir->metric_files_size != NULL) {
    fc_submit_dir(dir->metric_files_size, &m, (gauge_t)dir->files_size);
  }

  metric_reset(&m);

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
