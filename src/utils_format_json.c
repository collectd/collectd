/**
 * collectd - src/utils_format_json.c
 * Copyright (C) 2009  Florian octo Forster
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
#include "plugin.h"
#include "common.h"

#include "utils_cache.h"
#include "utils_format_json.h"

static int escape_string (char *buffer, size_t buffer_size, /* {{{ */
    const char *string)
{
  size_t src_pos;
  size_t dst_pos;

  if ((buffer == NULL) || (string == NULL))
    return (-EINVAL);

  if (buffer_size < 3)
    return (-ENOMEM);

  dst_pos = 0;

#define BUFFER_ADD(c) do { \
  if (dst_pos >= (buffer_size - 1)) { \
    buffer[buffer_size - 1] = 0; \
    return (-ENOMEM); \
  } \
  buffer[dst_pos] = (c); \
  dst_pos++; \
} while (0)

  /* Escape special characters */
  BUFFER_ADD ('"');
  for (src_pos = 0; string[src_pos] != 0; src_pos++)
  {
    if ((string[src_pos] == '"')
        || (string[src_pos] == '\\'))
    {
      BUFFER_ADD ('\\');
      BUFFER_ADD (string[src_pos]);
    }
    else if (string[src_pos] <= 0x001F)
      BUFFER_ADD ('?');
    else
      BUFFER_ADD (string[src_pos]);
  } /* for */
  BUFFER_ADD ('"');
  buffer[dst_pos] = 0;

#undef BUFFER_ADD

  return (0);
} /* }}} int buffer_add_string */

static int values_to_json (char *buffer, size_t buffer_size, /* {{{ */
                const data_set_t *ds, const value_list_t *vl, int store_rates)
{
  size_t offset = 0;
  int i;
  gauge_t *rates = NULL;

  memset (buffer, 0, buffer_size);

#define BUFFER_ADD(...) do { \
  int status; \
  status = ssnprintf (buffer + offset, buffer_size - offset, \
      __VA_ARGS__); \
  if (status < 1) \
  { \
    sfree(rates); \
    return (-1); \
  } \
  else if (((size_t) status) >= (buffer_size - offset)) \
  { \
    sfree(rates); \
    return (-ENOMEM); \
  } \
  else \
    offset += ((size_t) status); \
} while (0)

  BUFFER_ADD ("[");
  for (i = 0; i < ds->ds_num; i++)
  {
    if (i > 0)
      BUFFER_ADD (",");

    if (ds->ds[i].type == DS_TYPE_GAUGE)
    {
      if(isfinite (vl->values[i].gauge))
        BUFFER_ADD ("%g", vl->values[i].gauge);
      else
        BUFFER_ADD ("null");
    }
    else if (store_rates)
    {
      if (rates == NULL)
        rates = uc_get_rate (ds, vl);
      if (rates == NULL)
      {
        WARNING ("utils_format_json: uc_get_rate failed.");
        sfree(rates);
        return (-1);
      }

      if(isfinite (rates[i]))
        BUFFER_ADD ("%g", rates[i]);
      else
        BUFFER_ADD ("null");
    }
    else if (ds->ds[i].type == DS_TYPE_COUNTER)
      BUFFER_ADD ("%llu", vl->values[i].counter);
    else if (ds->ds[i].type == DS_TYPE_DERIVE)
      BUFFER_ADD ("%"PRIi64, vl->values[i].derive);
    else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
      BUFFER_ADD ("%"PRIu64, vl->values[i].absolute);
    else
    {
      ERROR ("format_json: Unknown data source type: %i",
          ds->ds[i].type);
      sfree (rates);
      return (-1);
    }
  } /* for ds->ds_num */
  BUFFER_ADD ("]");

#undef BUFFER_ADD

  DEBUG ("format_json: values_to_json: buffer = %s;", buffer);
  sfree(rates);
  return (0);
} /* }}} int values_to_json */

static int dstypes_to_json (char *buffer, size_t buffer_size, /* {{{ */
                const data_set_t *ds, const value_list_t *vl)
{
  size_t offset = 0;
  int i;

  memset (buffer, 0, buffer_size);

#define BUFFER_ADD(...) do { \
  int status; \
  status = ssnprintf (buffer + offset, buffer_size - offset, \
      __VA_ARGS__); \
  if (status < 1) \
    return (-1); \
  else if (((size_t) status) >= (buffer_size - offset)) \
    return (-ENOMEM); \
  else \
    offset += ((size_t) status); \
} while (0)

  BUFFER_ADD ("[");
  for (i = 0; i < ds->ds_num; i++)
  {
    if (i > 0)
      BUFFER_ADD (",");

    BUFFER_ADD ("\"%s\"", DS_TYPE_TO_STRING (ds->ds[i].type));
  } /* for ds->ds_num */
  BUFFER_ADD ("]");

#undef BUFFER_ADD

  DEBUG ("format_json: dstypes_to_json: buffer = %s;", buffer);

  return (0);
} /* }}} int dstypes_to_json */

static int dsnames_to_json (char *buffer, size_t buffer_size, /* {{{ */
                const data_set_t *ds, const value_list_t *vl)
{
  size_t offset = 0;
  int i;

  memset (buffer, 0, buffer_size);

#define BUFFER_ADD(...) do { \
  int status; \
  status = ssnprintf (buffer + offset, buffer_size - offset, \
      __VA_ARGS__); \
  if (status < 1) \
    return (-1); \
  else if (((size_t) status) >= (buffer_size - offset)) \
    return (-ENOMEM); \
  else \
    offset += ((size_t) status); \
} while (0)

  BUFFER_ADD ("[");
  for (i = 0; i < ds->ds_num; i++)
  {
    if (i > 0)
      BUFFER_ADD (",");

    BUFFER_ADD ("\"%s\"", ds->ds[i].name);
  } /* for ds->ds_num */
  BUFFER_ADD ("]");

#undef BUFFER_ADD

  DEBUG ("format_json: dsnames_to_json: buffer = %s;", buffer);

  return (0);
} /* }}} int dsnames_to_json */

static int value_list_to_json (char *buffer, size_t buffer_size, /* {{{ */
                const data_set_t *ds, const value_list_t *vl, int store_rates)
{
  char temp[512];
  size_t offset = 0;
  int status;

  memset (buffer, 0, buffer_size);

#define BUFFER_ADD(...) do { \
  status = ssnprintf (buffer + offset, buffer_size - offset, \
      __VA_ARGS__); \
  if (status < 1) \
    return (-1); \
  else if (((size_t) status) >= (buffer_size - offset)) \
    return (-ENOMEM); \
  else \
    offset += ((size_t) status); \
} while (0)

  /* All value lists have a leading comma. The first one will be replaced with
   * a square bracket in `format_json_finalize'. */
  BUFFER_ADD (",{");

  status = values_to_json (temp, sizeof (temp), ds, vl, store_rates);
  if (status != 0)
    return (status);
  BUFFER_ADD ("\"values\":%s", temp);

  status = dstypes_to_json (temp, sizeof (temp), ds, vl);
  if (status != 0)
    return (status);
  BUFFER_ADD (",\"dstypes\":%s", temp);

  status = dsnames_to_json (temp, sizeof (temp), ds, vl);
  if (status != 0)
    return (status);
  BUFFER_ADD (",\"dsnames\":%s", temp);

  BUFFER_ADD (",\"time\":%.3f", CDTIME_T_TO_DOUBLE (vl->time));
  BUFFER_ADD (",\"interval\":%.3f", CDTIME_T_TO_DOUBLE (vl->interval));

#define BUFFER_ADD_KEYVAL(key, value) do { \
  status = escape_string (temp, sizeof (temp), (value)); \
  if (status != 0) \
    return (status); \
  BUFFER_ADD (",\"%s\":%s", (key), temp); \
} while (0)

  BUFFER_ADD_KEYVAL ("host", vl->host);
  BUFFER_ADD_KEYVAL ("plugin", vl->plugin);
  BUFFER_ADD_KEYVAL ("plugin_instance", vl->plugin_instance);
  BUFFER_ADD_KEYVAL ("type", vl->type);
  BUFFER_ADD_KEYVAL ("type_instance", vl->type_instance);

  BUFFER_ADD ("}");

#undef BUFFER_ADD_KEYVAL
#undef BUFFER_ADD

  DEBUG ("format_json: value_list_to_json: buffer = %s;", buffer);

  return (0);
} /* }}} int value_list_to_json */

static int format_json_value_list_nocheck (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl,
    int store_rates, size_t temp_size)
{
  char temp[temp_size];
  int status;

  status = value_list_to_json (temp, sizeof (temp), ds, vl, store_rates);
  if (status != 0)
    return (status);
  temp_size = strlen (temp);

  memcpy (buffer + (*ret_buffer_fill), temp, temp_size + 1);
  (*ret_buffer_fill) += temp_size;
  (*ret_buffer_free) -= temp_size;

  return (0);
} /* }}} int format_json_value_list_nocheck */

int format_json_initialize (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free)
{
  size_t buffer_fill;
  size_t buffer_free;

  if ((buffer == NULL) || (ret_buffer_fill == NULL) || (ret_buffer_free == NULL))
    return (-EINVAL);

  buffer_fill = *ret_buffer_fill;
  buffer_free = *ret_buffer_free;

  buffer_free = buffer_fill + buffer_free;
  buffer_fill = 0;

  if (buffer_free < 3)
    return (-ENOMEM);

  memset (buffer, 0, buffer_free);
  *ret_buffer_fill = buffer_fill;
  *ret_buffer_free = buffer_free;

  return (0);
} /* }}} int format_json_initialize */

int format_json_finalize (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free)
{
  size_t pos;

  if ((buffer == NULL) || (ret_buffer_fill == NULL) || (ret_buffer_free == NULL))
    return (-EINVAL);

  if (*ret_buffer_free < 2)
    return (-ENOMEM);

  /* Replace the leading comma added in `value_list_to_json' with a square
   * bracket. */
  if (buffer[0] != ',')
    return (-EINVAL);
  buffer[0] = '[';

  pos = *ret_buffer_fill;
  buffer[pos] = ']';
  buffer[pos+1] = 0;

  (*ret_buffer_fill)++;
  (*ret_buffer_free)--;

  return (0);
} /* }}} int format_json_finalize */

int format_json_value_list (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl, int store_rates)
{
  if ((buffer == NULL)
      || (ret_buffer_fill == NULL) || (ret_buffer_free == NULL)
      || (ds == NULL) || (vl == NULL))
    return (-EINVAL);

  if (*ret_buffer_free < 3)
    return (-ENOMEM);

  return (format_json_value_list_nocheck (buffer,
        ret_buffer_fill, ret_buffer_free, ds, vl,
        store_rates, (*ret_buffer_free) - 2));
} /* }}} int format_json_value_list */

/* vim: set sw=2 sts=2 et fdm=marker : */
