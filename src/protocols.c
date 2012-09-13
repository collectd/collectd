/**
 * collectd - src/protocols.c
 * Copyright (C) 2009,2010  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#if !KERNEL_LINUX
# error "No applicable input method."
#endif

#define SNMP_FILE "/proc/net/snmp"
#define NETSTAT_FILE "/proc/net/netstat"

/*
 * Global variables
 */
static const char *config_keys[] =
{
  "Value",
  "IgnoreSelected",
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *values_list = NULL;

/* 
 * Functions
 */
static void submit (const char *protocol_name,
    const char *str_key, const char *str_value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  int status;

  status = parse_value (str_value, values, DS_TYPE_DERIVE);
  if (status != 0)
  {
    ERROR ("protocols plugin: Parsing string as integer failed: %s",
        str_value);
    return;
  }

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "protocols", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, protocol_name, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, "protocol_counter", sizeof (vl.type));
  sstrncpy (vl.type_instance, str_key, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void submit */

static int read_file (const char *path)
{
  FILE *fh;
  char key_buffer[4096];
  char value_buffer[4096];
  char *key_ptr;
  char *value_ptr;
  char *key_fields[256];
  char *value_fields[256];
  int key_fields_num;
  int value_fields_num;
  int status;
  int i;

  fh = fopen (path, "r");
  if (fh == NULL)
  {
    ERROR ("protocols plugin: fopen (%s) failed: %s.",
        path, sstrerror (errno, key_buffer, sizeof (key_buffer)));
    return (-1);
  }

  status = -1;
  while (42)
  {
    clearerr (fh);
    key_ptr = fgets (key_buffer, sizeof (key_buffer), fh);
    if (key_ptr == NULL)
    {
      if (feof (fh) != 0)
      {
        status = 0;
        break;
      }
      else if (ferror (fh) != 0)
      {
        ERROR ("protocols plugin: Reading from %s failed.", path);
        break;
      }
      else
      {
        ERROR ("protocols plugin: fgets failed for an unknown reason.");
        break;
      }
    } /* if (key_ptr == NULL) */

    value_ptr = fgets (value_buffer, sizeof (value_buffer), fh);
    if (value_ptr == NULL)
    {
      ERROR ("protocols plugin: read_file (%s): Could not read values line.",
          path);
      break;
    }

    key_ptr = strchr (key_buffer, ':');
    if (key_ptr == NULL)
    {
      ERROR ("protocols plugin: Could not find protocol name in keys line.");
      break;
    }
    *key_ptr = 0;
    key_ptr++;

    value_ptr = strchr (value_buffer, ':');
    if (value_ptr == NULL)
    {
      ERROR ("protocols plugin: Could not find protocol name "
          "in values line.");
      break;
    }
    *value_ptr = 0;
    value_ptr++;

    if (strcmp (key_buffer, value_buffer) != 0)
    {
      ERROR ("protocols plugin: Protocol names in keys and values lines "
          "don't match: `%s' vs. `%s'.",
          key_buffer, value_buffer);
      break;
    }


    key_fields_num = strsplit (key_ptr,
        key_fields, STATIC_ARRAY_SIZE (key_fields));
    value_fields_num = strsplit (value_ptr,
        value_fields, STATIC_ARRAY_SIZE (value_fields));

    if (key_fields_num != value_fields_num)
    {
      ERROR ("protocols plugin: Number of fields in keys and values lines "
          "don't match: %i vs %i.",
          key_fields_num, value_fields_num);
      break;
    }

    for (i = 0; i < key_fields_num; i++)
    {
      if (values_list != NULL)
      {
        char match_name[2 * DATA_MAX_NAME_LEN];

        ssnprintf (match_name, sizeof (match_name), "%s:%s",
            key_buffer, key_fields[i]);

        if (ignorelist_match (values_list, match_name))
          continue;
      } /* if (values_list != NULL) */

      submit (key_buffer, key_fields[i], value_fields[i]);
    } /* for (i = 0; i < key_fields_num; i++) */
  } /* while (42) */

  fclose (fh);

  return (status);
} /* int read_file */

static int protocols_read (void)
{
  int status;
  int success = 0;

  status = read_file (SNMP_FILE);
  if (status == 0)
    success++;

  status = read_file (NETSTAT_FILE);
  if (status == 0)
    success++;

  if (success == 0)
    return (-1);

  return (0);
} /* int protocols_read */

static int protocols_config (const char *key, const char *value)
{
  if (values_list == NULL)
    values_list = ignorelist_create (/* invert = */ 1);

  if (strcasecmp (key, "Value") == 0)
  {
    ignorelist_add (values_list, value);
  }
  else if (strcasecmp (key, "IgnoreSelected") == 0)
  {
    int invert = 1;
    if (IS_TRUE (value))
      invert = 0;
    ignorelist_set_invert (values_list, invert);
  }
  else
  {
    return (-1);
  }

  return (0);
} /* int protocols_config */

void module_register (void)
{
  plugin_register_config ("protocols", protocols_config,
      config_keys, config_keys_num);
  plugin_register_read ("protocols", protocols_read);
} /* void module_register */

/* vim: set sw=2 sts=2 et : */
