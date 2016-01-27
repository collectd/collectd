/**
 * collectd - src/utils_format_kairosdb.c
 * Copyright (C) 2016       Aurelien beorn Rougemont
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Aurelien beorn Rougemont <beorn at gandi dot net>
 **/

#include <ctype.h>

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include "utils_cache.h"
#include "utils_format_kairosdb.h"

/* This is the KAIROSDB format for write_http output
 *
 * Target format
 * [
 *   {
 *     "name":"collectd.vmem"
 *     "datapoints":
 *       [
 *         [1453897164060, 97.000000]
 *       ],
 *      "tags":
 *        {
 *          "host": "fqdn.domain.tld",
 *          "plugin_instance": "vmpage_number",
 *          "type": "kernel_stack",
 *          "ds": "value"
 *          ""
 *        }
 *   }
 * ]
 */

static int kairosdb_escape_string (char *buffer, size_t buffer_size, /* {{{ */
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
  /* authorize -_. and alpha num but also escapes " */
  BUFFER_ADD ('"');
  for (src_pos = 0; string[src_pos] != 0; src_pos++)
  {
    if (isalnum(string[src_pos]) ||
      0x2d == string[src_pos] ||
      0x2e == string[src_pos] ||
      0x5f == string[src_pos])
        BUFFER_ADD (tolower(string[src_pos]));
  } /* for */
  BUFFER_ADD ('"');
  buffer[dst_pos] = 0;

#undef BUFFER_ADD

  return (0);
} /* }}} int kairosdb_escape_string */

static int values_to_kairosdb (char *buffer, size_t buffer_size, /* {{{ */
                const data_set_t *ds, const value_list_t *vl, int store_rates,
                int ds_idx)
{
  size_t offset = 0;
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

  BUFFER_ADD ("[[");
  BUFFER_ADD ("%"PRIu64, CDTIME_T_TO_MS (vl->time));
  BUFFER_ADD (",");
  if (ds->ds[ds_idx].type == DS_TYPE_GAUGE)
  {
    if(isfinite (vl->values[ds_idx].gauge))
      BUFFER_ADD (JSON_GAUGE_FORMAT, vl->values[ds_idx].gauge);
    else
      BUFFER_ADD ("null");
  }
  else if (store_rates)
  {
    if (rates == NULL)
      rates = uc_get_rate (ds, vl);
    if (rates == NULL)
    {
      WARNING ("utils_format_kairosdb: uc_get_rate failed.");
      sfree(rates);
      return (-1);
    }

    if(isfinite (rates[ds_idx]))
      BUFFER_ADD (JSON_GAUGE_FORMAT, rates[ds_idx]);
    else
      BUFFER_ADD ("null");
  }
  else if (ds->ds[ds_idx].type == DS_TYPE_COUNTER)
    BUFFER_ADD ("%llu", vl->values[ds_idx].counter);
  else if (ds->ds[ds_idx].type == DS_TYPE_DERIVE)
    BUFFER_ADD ("%"PRIi64, vl->values[ds_idx].derive);
  else if (ds->ds[ds_idx].type == DS_TYPE_ABSOLUTE)
    BUFFER_ADD ("%"PRIu64, vl->values[ds_idx].absolute);
  else
  {
    ERROR ("format_kairosdb: Unknown data source type: %i",
        ds->ds[ds_idx].type);
    sfree (rates);
    return (-1);
  }
  BUFFER_ADD ("]]");

#undef BUFFER_ADD

  DEBUG ("format_kairosdb: values_to_kairosdb: buffer = %s;", buffer);
  sfree(rates);
  return (0);
} /* }}} int values_to_kairosdb */

static int meta_data_to_kairosdb (char *buffer, size_t buffer_size, /* {{{ */
    meta_data_t *meta)
{
  size_t offset = 0;
  char **keys = NULL;
  int keys_num;
  int status;
  int i;

  buffer[0] = 0;

  if (meta == NULL)
    return (EINVAL);

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

  keys_num = meta_data_toc (meta, &keys);
  if (keys_num == 0)
  {
    sfree (keys);
    return (0);
  }

  for (i = 0; i < keys_num; ++i)
  {
    int type;
    char *key = keys[i];

    type = meta_data_type (meta, key);
    if (type == MD_TYPE_STRING)
    {
      char *value = NULL;
      if (meta_data_get_string (meta, key, &value) == 0)
      {
        char temp[512] = "";
        kairosdb_escape_string (temp, sizeof (temp), value);
        sfree (value);
        BUFFER_ADD (",\"%s\":%s", key, temp);
      }
    }
    else if (type == MD_TYPE_SIGNED_INT)
    {
      int64_t value = 0;
      if (meta_data_get_signed_int (meta, key, &value) == 0)
        BUFFER_ADD (",\"%s\":%"PRIi64, key, value);
    }
    else if (type == MD_TYPE_UNSIGNED_INT)
    {
      uint64_t value = 0;
      if (meta_data_get_unsigned_int (meta, key, &value) == 0)
        BUFFER_ADD (",\"%s\":%"PRIu64, key, value);
    }
    else if (type == MD_TYPE_DOUBLE)
    {
      double value = 0.0;
      if (meta_data_get_double (meta, key, &value) == 0)
        BUFFER_ADD (",\"%s\":%f", key, value);
    }
    else if (type == MD_TYPE_BOOLEAN)
    {
      _Bool value = 0;
      if (meta_data_get_boolean (meta, key, &value) == 0)
      {
        /*
         * XXX: This should be handled through a core tag plugin someday
         * if tag is network:received drop it
         */
        if (! ((strlen(key) == 16) && (strncmp(key, "network:received", 16) == 0)))
        {
          BUFFER_ADD (",\"%s\":%s", key, value ? "true" : "false");
        }
      }
    }
    sfree (key);
  } /* for (keys) */
  sfree (keys);

  if (offset <= 0)
    return (ENOENT);

  buffer[0] = '{'; /* replace leading ',' */
  BUFFER_ADD ("}");

#undef BUFFER_ADD

  return (0);
} /* }}} int meta_data_to_kairosdb */

static int value_list_to_kairosdb (char *buffer, size_t buffer_size, /* {{{ */
                const data_set_t *ds, const value_list_t *vl, int store_rates)
{
  char temp[512];
  size_t offset = 0;
  int status;
  int i=0;

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

  for (i = 0; i < ds->ds_num; i++)
  {
    /* All value lists have a leading comma. The first one will be replaced with
     * a square bracket in `format_kairosdb_finalize'. */
    BUFFER_ADD (",{");

    BUFFER_ADD ("\"name\":\"collectd");

    if (strlen(vl->plugin))
    {
      BUFFER_ADD (".%s", vl->plugin);
    }
    if (strlen(vl->plugin_instance))
    {
      meta_data_add_string(vl->meta, "plugin_instance", vl->plugin_instance);
    }
    if (strlen(vl->type))
    {
      meta_data_add_string(vl->meta, "type", vl->type);
    }
    if (strlen(vl->type_instance))
    {
      meta_data_add_string(vl->meta, "type_instance", vl->type_instance);
    }
    if (ds->ds_num > 0)
    {
      meta_data_add_string(vl->meta, "ds", ds->ds[i].name);
    }

    status = values_to_kairosdb (temp, sizeof (temp), ds, vl, store_rates, i);
    if (status != 0)
      return (status);

    BUFFER_ADD ("\", \"datapoints\": %s", temp);

#define BUFFER_ADD_KEYVAL(key, value) do { \
  status = kairosdb_escape_string (temp, sizeof (temp), (value)); \
  if (status != 0) \
    return (status); \
  BUFFER_ADD (",\"%s\": %s", (key), temp); \
} while (0)

    /* Now adds meta data to metrics using fqdn
     *
     */
    meta_data_add_string(vl->meta, "host", vl->host);

    if (vl->meta != NULL)
    {
      char meta_buffer[buffer_size];
      memset (meta_buffer, 0, sizeof (meta_buffer));
      status = meta_data_to_kairosdb (meta_buffer, sizeof (meta_buffer), vl->meta);
      if (status != 0)
        return (status);

      BUFFER_ADD (",\"tags\": %s", meta_buffer);
    } /* if (vl->meta != NULL) */

    BUFFER_ADD ("}");
  } /* for ds->ds_num */

#undef BUFFER_ADD_KEYVAL
#undef BUFFER_ADD

  DEBUG ("format_kairosdb: value_list_to_kairosdb: buffer = %s;", buffer);

  return (0);
} /* }}} int value_list_to_kairosdb */

static int format_kairosdb_value_list_nocheck (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl,
    int store_rates, size_t temp_size)
{
  char temp[temp_size];
  int status;

  status = value_list_to_kairosdb (temp, sizeof (temp), ds, vl, store_rates);
  if (status != 0)
    return (status);
  temp_size = strlen (temp);

  memcpy (buffer + (*ret_buffer_fill), temp, temp_size + 1);
  (*ret_buffer_fill) += temp_size;
  (*ret_buffer_free) -= temp_size;

  return (0);
} /* }}} int format_kairosdb_value_list_nocheck */

int format_kairosdb_initialize (char *buffer, /* {{{ */
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
} /* }}} int format_kairosdb_initialize */

int format_kairosdb_finalize (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free)
{
  size_t pos;

  if ((buffer == NULL) || (ret_buffer_fill == NULL) || (ret_buffer_free == NULL))
    return (-EINVAL);

  if (*ret_buffer_free < 2)
    return (-ENOMEM);

  /* Replace the leading comma added in `value_list_to_kairosdb' with a square
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
} /* }}} int format_kairosdb_finalize */

int format_kairosdb_value_list (char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl, int store_rates)
{
  if ((buffer == NULL)
      || (ret_buffer_fill == NULL) || (ret_buffer_free == NULL)
      || (ds == NULL) || (vl == NULL))
    return (-EINVAL);

  if (*ret_buffer_free < 3)
    return (-ENOMEM);

  return (format_kairosdb_value_list_nocheck (buffer,
        ret_buffer_fill, ret_buffer_free, ds, vl,
        store_rates, (*ret_buffer_free) - 2));
} /* }}} int format_kairosdb_value_list */

/* vim: set sw=2 sts=2 et fdm=marker : */
