/**
 * collectd - src/qmail.c
 * Copyright (C) 2008  Alessandro Iurlano
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
 * Author:
 *   Alessandro Iurlano <alessandro.iurlano at gmail.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"       

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>

#define DEFAULT_BASE_DIR "/var/qmail"

static char *qmail_base_dir;

static const char *config_keys[] =
{
  "QmailDir"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static void qmail_submit (const char *plugin_instance, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE (values);
  vl.time = time (NULL);
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.type, "gauge", sizeof (vl.type));
  sstrncpy (vl.plugin, "qmail", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));

  plugin_dispatch_values (&vl);
} /* void qmail_submit */

static int count_files_in_subtree (const char *path, int depth)
{
  DIR *dh;
  struct dirent *de;
  int status;

  char **subdirs;
  size_t subdirs_num;

  int count;
  int i;

  dh = opendir (path);
  if (dh == NULL)
  {
    ERROR ("qmail plugin: opendir (%s) failed.", path);
    return (-1);
  }

  subdirs = NULL;
  subdirs_num = 0;

  count = 0;
  while ((de = readdir (dh)) != NULL)
  {
    char abs_path[4096];
    struct stat statbuf;

    ssnprintf (abs_path, sizeof (abs_path), "%s/%s", path, de->d_name);

    status = lstat (abs_path, &statbuf);
    if (status != 0)
    {
      ERROR ("qmail plugin: stat (%s) failed.", abs_path);
      continue;
    }

    if (S_ISREG (statbuf.st_mode))
    {
      count++;
    }
    else if (S_ISDIR (statbuf.st_mode))
    {
      char **temp;

      temp = (char **) realloc (subdirs, sizeof (char *) * (subdirs_num + 1));
      if (temp == NULL)
      {
        ERROR ("qmail plugin: realloc failed.");
        continue;
      }
      subdirs = temp;

      subdirs[subdirs_num] = strdup (abs_path);
      if (subdirs[subdirs_num] == NULL)
      {
        ERROR ("qmail plugin: strdup failed.");
        continue;
      }
      subdirs_num++;
    }
  }

  closedir (dh);
  dh = NULL;

  if (depth > 0)
  {
    for (i = 0; i < subdirs_num; i++)
    {
      status = count_files_in_subtree (subdirs[i], depth - 1);
      if (status > 0)
        count += status;
    }
  }

  for (i = 0; i < subdirs_num; i++)
  {
    sfree (subdirs[i]);
  }
  sfree (subdirs);

  return (count);
} /* int count_files_in_subtree */

static int read_queue_length (const char *queue_name, const char *path)
{
  int64_t num_files;

  num_files = count_files_in_subtree (path, /* depth = */ 1);
  if (num_files < 0)
  {
    ERROR ("qmail plugin: Counting files in `%s' failed.", path);
    return (-1);
  }

  qmail_submit (queue_name, (gauge_t) num_files);
  return (0);
} /* int read_queue_length */

static int queue_len_read (void)
{
  char path[4096];
  int success;
  int status;

  success = 0;
  
  ssnprintf (path, sizeof (path), "%s/queue/mess",
      (qmail_base_dir != NULL)
      ? qmail_base_dir
      : DEFAULT_BASE_DIR);

  status = read_queue_length ("messages", path);
  if (status == 0)
    success++;

  ssnprintf (path, sizeof (path), "%s/queue/todo",
      (qmail_base_dir != NULL)
      ? qmail_base_dir
      : DEFAULT_BASE_DIR);

  status = read_queue_length ("todo", path);
  if (status == 0)
    success++;

  if (success > 0)
    return 0;
  return (-1);
} /* int queue_len_read */

static int qmail_config (const char *key, const char *val)
{
  if (strcasecmp ("QmailDir", key) == 0)
  {
    size_t qmail_base_dir_len;

    sfree (qmail_base_dir);
    qmail_base_dir = strdup(val);
    if (qmail_base_dir == NULL)
    {
      ERROR ("qmail plugin: strdup failed.");
      return (1);
    }

    qmail_base_dir_len = strlen (qmail_base_dir);
    while ((qmail_base_dir_len > 0)
        && (qmail_base_dir[qmail_base_dir_len - 1] == '/'))
    {
      qmail_base_dir[qmail_base_dir_len - 1] = 0;
      qmail_base_dir_len--;
    }

    if (qmail_base_dir_len == 0)
    {
      ERROR ("qmail plugin: QmailDir is invalid.");
      sfree (qmail_base_dir);
      qmail_base_dir = NULL;
      return (1);
    }
  }
  else
  {
    return (-1);
  }

  return (0);
} /* int qmail_config */

void module_register (void)
{
  plugin_register_config ("qmail", qmail_config,
      config_keys, config_keys_num);
  plugin_register_read ("qmail", queue_len_read);
} /* void module_register */

/*
 * vim: set sw=2 sts=2 et :
 */
