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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"       

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <fnmatch.h>

#define FC_RECURSIVE 1
#define FC_HIDDEN 2

struct fc_directory_conf_s
{
  char *path;
  char *instance;

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

static fc_directory_conf_t **directories = NULL;
static size_t directories_num = 0;

static void fc_submit_dir (const fc_directory_conf_t *dir)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = (gauge_t) dir->files_num;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE (values);
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "filecount", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, dir->instance, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, "files", sizeof (vl.type));

  plugin_dispatch_values (&vl);

  values[0].gauge = (gauge_t) dir->files_size;
  sstrncpy (vl.type, "bytes", sizeof (vl.type));

  plugin_dispatch_values (&vl);
} /* void fc_submit_dir */

/*
 * Config:
 * <Plugin filecount>
 *   <Directory /path/to/dir>
 *     Instance "foobar"
 *     Name "*.conf"
 *     MTime -3600
 *     Size "+10M"
 *   </Directory>
 * </Plugin>
 *
 * Collect:
 * - Number of files
 * - Total size
 */

static int fc_config_set_instance (fc_directory_conf_t *dir, const char *str)
{
  char buffer[1024];
  char *ptr;
  char *copy;

  sstrncpy (buffer, str, sizeof (buffer));
  for (ptr = buffer; *ptr != 0; ptr++)
    if (*ptr == '/')
      *ptr = '_';

  for (ptr = buffer; *ptr == '_'; ptr++)
    /* do nothing */;

  if (*ptr == 0)
    return (-1);

  copy = strdup (ptr);
  if (copy == NULL)
    return (-1);

  sfree (dir->instance);
  dir->instance = copy;

  return (0);
} /* int fc_config_set_instance */

static int fc_config_add_dir_instance (fc_directory_conf_t *dir,
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("filecount plugin: The `Instance' config option needs exactly "
        "one string argument.");
    return (-1);
  }

  return (fc_config_set_instance (dir, ci->values[0].value.string));
} /* int fc_config_add_dir_instance */

static int fc_config_add_dir_name (fc_directory_conf_t *dir,
    oconfig_item_t *ci)
{
  char *temp;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("filecount plugin: The `Name' config option needs exactly one "
        "string argument.");
    return (-1);
  }

  temp = strdup (ci->values[0].value.string);
  if (temp == NULL)
  {
    ERROR ("filecount plugin: strdup failed.");
    return (-1);
  }

  sfree (dir->name);
  dir->name = temp;

  return (0);
} /* int fc_config_add_dir_name */

static int fc_config_add_dir_mtime (fc_directory_conf_t *dir,
    oconfig_item_t *ci)
{
  char *endptr;
  double temp;

  if ((ci->values_num != 1)
      || ((ci->values[0].type != OCONFIG_TYPE_STRING)
        && (ci->values[0].type != OCONFIG_TYPE_NUMBER)))
  {
    WARNING ("filecount plugin: The `MTime' config option needs exactly one "
        "string or numeric argument.");
    return (-1);
  }

  if (ci->values[0].type == OCONFIG_TYPE_NUMBER)
  {
    dir->mtime = (int64_t) ci->values[0].value.number;
    return (0);
  }

  errno = 0;
  endptr = NULL;
  temp = strtod (ci->values[0].value.string, &endptr);
  if ((errno != 0) || (endptr == NULL)
      || (endptr == ci->values[0].value.string))
  {
    WARNING ("filecount plugin: Converting `%s' to a number failed.",
        ci->values[0].value.string);
    return (-1);
  }

  switch (*endptr)
  {
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
      WARNING ("filecount plugin: Invalid suffix for `MTime': `%c'", *endptr);
      return (-1);
  } /* switch (*endptr) */

  dir->mtime = (int64_t) temp;

  return (0);
} /* int fc_config_add_dir_mtime */

static int fc_config_add_dir_size (fc_directory_conf_t *dir,
    oconfig_item_t *ci)
{
  char *endptr;
  double temp;

  if ((ci->values_num != 1)
      || ((ci->values[0].type != OCONFIG_TYPE_STRING)
        && (ci->values[0].type != OCONFIG_TYPE_NUMBER)))
  {
    WARNING ("filecount plugin: The `Size' config option needs exactly one "
        "string or numeric argument.");
    return (-1);
  }

  if (ci->values[0].type == OCONFIG_TYPE_NUMBER)
  {
    dir->size = (int64_t) ci->values[0].value.number;
    return (0);
  }

  errno = 0;
  endptr = NULL;
  temp = strtod (ci->values[0].value.string, &endptr);
  if ((errno != 0) || (endptr == NULL)
      || (endptr == ci->values[0].value.string))
  {
    WARNING ("filecount plugin: Converting `%s' to a number failed.",
        ci->values[0].value.string);
    return (-1);
  }

  switch (*endptr)
  {
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
      WARNING ("filecount plugin: Invalid suffix for `Size': `%c'", *endptr);
      return (-1);
  } /* switch (*endptr) */

  dir->size = (int64_t) temp;

  return (0);
} /* int fc_config_add_dir_size */

static int fc_config_add_dir_option (fc_directory_conf_t *dir,
    oconfig_item_t *ci, int bit)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    WARNING ("filecount plugin: The `Recursive' config options needs exactly "
        "one boolean argument.");
    return (-1);
  }

  if (ci->values[0].value.boolean)
    dir->options |= bit;
  else
    dir->options &= ~bit;

  return (0);
} /* int fc_config_add_dir_option */

static int fc_config_add_dir (oconfig_item_t *ci)
{
  fc_directory_conf_t *dir;
  int status;
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("filecount plugin: `Directory' needs exactly one string "
        "argument.");
    return (-1);
  }

  /* Initialize `dir' */
  dir = (fc_directory_conf_t *) malloc (sizeof (*dir));
  if (dir == NULL)
  {
    ERROR ("filecount plugin: malloc failed.");
    return (-1);
  }
  memset (dir, 0, sizeof (*dir));

  dir->path = strdup (ci->values[0].value.string);
  if (dir->path == NULL)
  {
    ERROR ("filecount plugin: strdup failed.");
    return (-1);
  }

  fc_config_set_instance (dir, dir->path);

  dir->options = FC_RECURSIVE;

  dir->name = NULL;
  dir->mtime = 0;
  dir->size = 0;

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp ("Instance", option->key) == 0)
      status = fc_config_add_dir_instance (dir, option);
    else if (strcasecmp ("Name", option->key) == 0)
      status = fc_config_add_dir_name (dir, option);
    else if (strcasecmp ("MTime", option->key) == 0)
      status = fc_config_add_dir_mtime (dir, option);
    else if (strcasecmp ("Size", option->key) == 0)
      status = fc_config_add_dir_size (dir, option);
    else if (strcasecmp ("Recursive", option->key) == 0)
      status = fc_config_add_dir_option (dir, option, FC_RECURSIVE);
    else if (strcasecmp ("IncludeHidden", option->key) == 0)
      status = fc_config_add_dir_option (dir, option, FC_HIDDEN);
    else
    {
      WARNING ("filecount plugin: fc_config_add_dir: "
          "Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (ci->children) */

  if (status == 0)
  {
    fc_directory_conf_t **temp;

    temp = (fc_directory_conf_t **) realloc (directories,
        sizeof (*directories) * (directories_num + 1));
    if (temp == NULL)
    {
      ERROR ("filecount plugin: realloc failed.");
      status = -1;
    }
    else
    {
      directories = temp;
      directories[directories_num] = dir;
      directories_num++;
    }
  }

  if (status != 0)
  {
    sfree (dir->name);
    sfree (dir->instance);
    sfree (dir->path);
    sfree (dir);
    return (-1);
  }

  return (0);
} /* int fc_config_add_dir */

static int fc_config (oconfig_item_t *ci)
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp ("Directory", child->key) == 0)
      fc_config_add_dir (child);
    else
    {
      WARNING ("filecount plugin: Ignoring unknown config option `%s'.",
          child->key);
    }
  } /* for (ci->children) */

  return (0);
} /* int fc_config */

static int fc_init (void)
{
  if (directories_num < 1)
  {
    WARNING ("filecount plugin: No directories have been configured.");
    return (-1);
  }

  return (0);
} /* int fc_init */

static int fc_read_dir_callback (const char *dirname, const char *filename,
    void *user_data)
{
  fc_directory_conf_t *dir = user_data;
  char abs_path[PATH_MAX];
  struct stat statbuf;
  int status;

  if (dir == NULL)
    return (-1);

  ssnprintf (abs_path, sizeof (abs_path), "%s/%s", dirname, filename);

  status = lstat (abs_path, &statbuf);
  if (status != 0)
  {
    ERROR ("filecount plugin: stat (%s) failed.", abs_path);
    return (-1);
  }

  if (S_ISDIR (statbuf.st_mode) && (dir->options & FC_RECURSIVE))
  {
    status = walk_directory (abs_path, fc_read_dir_callback, dir,
        /* include hidden = */ (dir->options & FC_HIDDEN) ? 1 : 0);
    return (status);
  }
  else if (!S_ISREG (statbuf.st_mode))
  {
    return (0);
  }

  if (dir->name != NULL)
  {
    status = fnmatch (dir->name, filename, /* flags = */ 0);
    if (status != 0)
      return (0);
  }

  if (dir->mtime != 0)
  {
    time_t mtime = dir->now;

    if (dir->mtime < 0)
      mtime += dir->mtime;
    else
      mtime -= dir->mtime;

    DEBUG ("filecount plugin: Only collecting files that were touched %s %u.",
        (dir->mtime < 0) ? "after" : "before",
        (unsigned int) mtime);

    if (((dir->mtime < 0) && (statbuf.st_mtime < mtime))
        || ((dir->mtime > 0) && (statbuf.st_mtime > mtime)))
      return (0);
  }

  if (dir->size != 0)
  {
    off_t size;

    if (dir->size < 0)
      size = (off_t) ((-1) * dir->size);
    else
      size = (off_t) dir->size;

    if (((dir->size < 0) && (statbuf.st_size > size))
        || ((dir->size > 0) && (statbuf.st_size < size)))
      return (0);
  }

  dir->files_num++;
  dir->files_size += (uint64_t) statbuf.st_size;

  return (0);
} /* int fc_read_dir_callback */

static int fc_read_dir (fc_directory_conf_t *dir)
{
  int status;

  dir->files_num = 0;
  dir->files_size = 0;

  if (dir->mtime != 0)
    dir->now = time (NULL);
    
  status = walk_directory (dir->path, fc_read_dir_callback, dir,
      /* include hidden */ (dir->options & FC_HIDDEN) ? 1 : 0);
  if (status != 0)
  {
    WARNING ("filecount plugin: walk_directory (%s) failed.", dir->path);
    return (-1);
  }

  fc_submit_dir (dir);

  return (0);
} /* int fc_read_dir */

static int fc_read (void)
{
  size_t i;

  for (i = 0; i < directories_num; i++)
    fc_read_dir (directories[i]);

  return (0);
} /* int fc_read */

void module_register (void)
{
  plugin_register_complex_config ("filecount", fc_config);
  plugin_register_init ("filecount", fc_init);
  plugin_register_read ("filecount", fc_read);
} /* void module_register */

/*
 * vim: set sw=2 sts=2 et :
 */
