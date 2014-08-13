/**
 * collectd - src/utils_format_opentsdb.c
 * Copyright (C) 2014  Anand Karthik Tumuluru
 * Copyright (C) 2012  Thomas Meson
 * Copyright (C) 2012  Florian octo Forster
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
 *   Anand Karthik Tumuluru <anand.karthik at flipkart.com>
 *   Thomas Meson <zllak at hycik.org>
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include "utils_format_opentsdb.h"
#include "utils_cache.h"
#include "utils_parse_option.h"

#define OPENTSDB_FORBIDDEN " \t\"\\:!/()\n\r"

/* Utils functions to format data sets in opentsdb format.
 * Largely taken from utils_format_graphite.c as it contains similar constructs */

static int opentsdb_format_values (char *ret, size_t ret_len,
		int ds_num, const data_set_t *ds, const value_list_t *vl,
		gauge_t const *rates)
{
	size_t offset = 0;
	int status;

	assert (0 == strcmp (ds->type, vl->type));

	memset (ret, 0, ret_len);

#define BUFFER_ADD(...) do { \
	status = ssnprintf (ret + offset, ret_len - offset, \
			__VA_ARGS__); \
	if (status < 1) \
	{ \
		return (-1); \
	} \
	else if (((size_t) status) >= (ret_len - offset)) \
	{ \
		return (-1); \
	} \
	else \
	offset += ((size_t) status); \
} while (0)

if (ds->ds[ds_num].type == DS_TYPE_GAUGE)
	BUFFER_ADD ("%f", vl->values[ds_num].gauge);
else if (rates != NULL)
	BUFFER_ADD ("%f", rates[ds_num]);
else if (ds->ds[ds_num].type == DS_TYPE_COUNTER)
	BUFFER_ADD ("%llu", vl->values[ds_num].counter);
else if (ds->ds[ds_num].type == DS_TYPE_DERIVE)
	BUFFER_ADD ("%"PRIi64, vl->values[ds_num].derive);
else if (ds->ds[ds_num].type == DS_TYPE_ABSOLUTE)
	BUFFER_ADD ("%"PRIu64, vl->values[ds_num].absolute);
	else
{
	ERROR ("opentsdb_format_values plugin: Unknown data source type: %i",
			ds->ds[ds_num].type);
	return (-1);
}

#undef BUFFER_ADD

return (0);
}

static void opentsdb_copy_escape_part (char *dst, const char *src, size_t dst_len,
		char escape_char)
{
	size_t i;

	memset (dst, 0, dst_len);

	if (src == NULL)
		return;

	for (i = 0; i < dst_len; i++)
	{
		if (src[i] == 0)
		{
			dst[i] = 0;
			break;
		}

		if ((src[i] == '.')
				|| isspace ((int) src[i])
				|| iscntrl ((int) src[i]))
			dst[i] = escape_char;
		else
			dst[i] = src[i];
	}
}

char* get_service_tags_from_host(char const *host){
    return "";
}

static int opentsdb_format_tags (char *ret, int ret_len,
		value_list_t const *vl,
		char const *tags,
		char const escape_char,
		unsigned int flags)
{

	DEBUG ("formatting tags with %s",tags);
	char n_plugin[DATA_MAX_NAME_LEN];
	char n_plugin_instance[DATA_MAX_NAME_LEN];

	char tmp_plugin[2 * DATA_MAX_NAME_LEN + 1];

    if (tags == NULL){
	    if (flags & OPENTSDB_INFER_SERVICE_TAGS){ 
            tags = get_service_tags_from_host(vl->host);
        } 
        else{ 
		    tags = "";
        }
    
    }

	opentsdb_copy_escape_part (n_plugin, vl->plugin,
			sizeof (n_plugin), escape_char);
	opentsdb_copy_escape_part (n_plugin_instance, vl->plugin_instance,
			sizeof (n_plugin_instance), escape_char);

	if (n_plugin_instance[0] != '\0'){
		ssnprintf (tmp_plugin, sizeof (tmp_plugin), "%s%c%s",
				n_plugin,
				'-',
				n_plugin_instance); 
    	ssnprintf (ret, ret_len, "host=%s plugin=%s %s",
	    		vl->host, tmp_plugin, tags);
    }
	else
    	ssnprintf (ret, ret_len, "host=%s %s",
	    		vl->host, tags);

	return (0);
}

static int opentsdb_format_name (char *ret, int ret_len,
		value_list_t const *vl,
		char const *prefix,
		char const escape_char,
		unsigned int flags)
{
	//char n_host[DATA_MAX_NAME_LEN];
	char n_plugin[DATA_MAX_NAME_LEN];
	char n_type[DATA_MAX_NAME_LEN];
	char n_type_instance[DATA_MAX_NAME_LEN];

	char tmp_plugin[2 * DATA_MAX_NAME_LEN + 1];
	char tmp_type_instance[2 * DATA_MAX_NAME_LEN + 1];
	char tmp_type[2 * DATA_MAX_NAME_LEN + 1];


	opentsdb_copy_escape_part (n_plugin, vl->plugin,
			sizeof (n_plugin), escape_char);
	opentsdb_copy_escape_part (n_type, vl->type,
			sizeof (n_type), escape_char);
	opentsdb_copy_escape_part (n_type_instance, vl->type_instance,
			sizeof (n_type_instance), escape_char);

	sstrncpy (tmp_plugin, n_plugin, sizeof (tmp_plugin));

	sstrncpy (tmp_type, n_type, sizeof (tmp_type));
	
    sstrncpy (tmp_type_instance, n_type_instance, sizeof (tmp_type_instance));

    if(prefix == NULL){
      if(tmp_type_instance[0] == '\0'){
        if (strcasecmp (tmp_plugin, tmp_type) == 0){
	      ssnprintf (ret, ret_len, "%s", tmp_plugin);
        }
        else{
	      ssnprintf (ret, ret_len, "%s.%s", tmp_plugin, tmp_type); 
        }
      }   
      else{
        if (strcasecmp (tmp_plugin, tmp_type) == 0){
	      ssnprintf (ret, ret_len, "%s.%s", tmp_plugin, tmp_type_instance);
        }
        else{
	      ssnprintf (ret, ret_len, "%s.%s.%s", tmp_plugin, tmp_type, tmp_type_instance); 
        }
      }
    }else{ 
      if(tmp_type_instance[0] == '\0'){
        if (strcasecmp (tmp_plugin, tmp_type) == 0){
	      ssnprintf (ret, ret_len, "%s.%s", prefix, tmp_plugin);
        }
        else{
	      ssnprintf (ret, ret_len, "%s.%s.%s", prefix, tmp_plugin, tmp_type); 
        }
      }   
      else{
        if (strcasecmp (tmp_plugin, tmp_type) == 0){
	      ssnprintf (ret, ret_len, "%s.%s.%s",
		    	prefix, tmp_plugin, tmp_type_instance);
        }
        else{
	      ssnprintf (ret, ret_len, "%s.%s.%s.%s",
		    	prefix, tmp_plugin, tmp_type, tmp_type_instance);
        }
      }
    }


	return (0);
}

static void escape_opentsdb_string (char *buffer, char escape_char)
{
	char *head;

	assert (strchr(OPENTSDB_FORBIDDEN, escape_char) == NULL);

	for (head = buffer + strcspn(buffer, OPENTSDB_FORBIDDEN);
			*head != '\0';
			head += strcspn(head, OPENTSDB_FORBIDDEN))
		*head = escape_char;
}

int format_opentsdb (char *buffer, size_t buffer_size,
		data_set_t const *ds, value_list_t const *vl,
		char const *prefix, char const *tags, char const escape_char,
		unsigned int flags)
{
	int status = 0;
	int i;
	int buffer_pos = 0;

	gauge_t *rates = NULL;
	if (flags & OPENTSDB_STORE_RATES)
		rates = uc_get_rate (ds, vl);

	for (i = 0; i < ds->ds_num; i++)
	{
		char        key[10*DATA_MAX_NAME_LEN];
		char        final_tags[512];
		char        values[512];
		size_t      message_len;
		char        message[1024];


		/* Copy the identifier to `key' and escape it. */
		status = opentsdb_format_name (key, sizeof (key), vl,
				prefix, escape_char, flags);
		if (status != 0)
		{
			ERROR ("format_opentsdb: error with opentsdb_format_name");
			sfree (rates);
			return (status);
		}

		escape_opentsdb_string (key, escape_char);
		/* Convert the values to an ASCII representation and put that into
		 * `values'. */
		status = opentsdb_format_values (values, sizeof (values), i, ds, vl, rates);
		if (status != 0)
		{
			ERROR ("format_opentsdb: error with opentsdb_format_values");
			sfree (rates);
			return (status);
		}

		/* Copy the identifier to `tags' and escape it. */
		status = opentsdb_format_tags (final_tags, sizeof (final_tags), vl,
			        tags, escape_char, flags);
		if (status != 0)
		{
			ERROR ("format_opentsdb: error with opentsdb_format_tags");
			sfree (rates);
			return (status);
		}

		/* Compute the opentsdb command */
		message_len = (size_t) ssnprintf (message, sizeof (message),
				"put %s %u %s %s\r\n",
				key,
				(unsigned int) CDTIME_T_TO_TIME_T (vl->time), 
				values,
				final_tags);
		if (message_len >= sizeof (message)) {
			ERROR ("format_opentsdb: message buffer too small: "
					"Need %zu bytes.", message_len + 1);
			sfree (rates);
			return (-ENOMEM);
		}

		/* Append it in case we got multiple data set */
		if ((buffer_pos + message_len) >= buffer_size)
		{
			ERROR ("format_opentsdb: target buffer too small");
			sfree (rates);
			return (-ENOMEM);
		}
		memcpy((void *) (buffer + buffer_pos), message, message_len);
		buffer_pos += message_len;
	}
	sfree (rates);
	return (status);
} /* int format_opentsdb */

/* vim: set sw=2 sts=2 et fdm=marker : */
