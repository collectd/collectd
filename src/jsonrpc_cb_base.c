/**
 * collectd - src/jsonrpc_cb_base.c
 * Copyright (C) 2012 Yves Mettier, Cyril Feraudet
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
 *   Yves Mettier <ymettier at free dot fr>
 *   Cyril Feraudet <cyril at feraudet dot com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_cache.h"
#include "jsonrpc.h"

#include <json/json.h>

#define OUTPUT_PREFIX_JSONRPC_CB_BASE "JSONRPC plugin (base) : "

static const char *jsonrpc_error_32001_listval_failed = "-1 uc_get_names failed.";

#define jsonrpc_free_everything_and_return(status) do { \
    size_t j; \
    for (j = 0; j < number; j++) { \
      sfree(names[j]); \
      names[j] = NULL; \
    } \
    sfree(names); \
    sfree(times); \
    return (status); \
  } while (0)


int jsonrpc_cb_listval (struct json_object *params, struct json_object *result, const char **errorstring) {
		struct json_object *obj;
		struct json_object *array;
		struct json_object *resultobject;
		char **names = NULL;
		cdtime_t *times = NULL;
		size_t number = 0;
		int status;
		int i;

		*errorstring = NULL;

/* Get the names */
		status = jsonrpc_local_uc_get_names (&names, &times, &number);
		if (status != 0)
		{
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "uc_get_names failed with status %i", status);
				*errorstring = jsonrpc_error_32001_listval_failed;
				jsonrpc_free_everything_and_return (-32001);
		}

/* Create the result object */
		if(NULL == (resultobject = json_object_new_object())) {
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json object");
				jsonrpc_free_everything_and_return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}

/* Insert the nb of values in the result object */
		if(NULL == (obj = json_object_new_int((int)number))) {
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json object");
				json_object_put(resultobject);
				jsonrpc_free_everything_and_return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}
		json_object_object_add(resultobject, "nb", obj);

/* Create the array of values */
		if(NULL == (array = json_object_new_array())) {
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json array");
				json_object_put(resultobject);
				jsonrpc_free_everything_and_return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}

/* Append the values to the array */
		for (i = 0; i < number; i++) {
				struct json_object * obj_array;

		if(NULL == (obj_array = json_object_new_array())) {
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json array");
						json_object_put(array);
				json_object_put(resultobject);
				jsonrpc_free_everything_and_return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}
				if(NULL == (obj = json_object_new_double(CDTIME_T_TO_DOUBLE (times[i])))) {
						DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json object");
						json_object_put(array);
						json_object_put(obj_array);
						json_object_put(resultobject);
						jsonrpc_free_everything_and_return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
				}
			json_object_array_add(obj_array,obj);
				if(NULL == (obj = json_object_new_string(names[i]))) {
						DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json object");
						json_object_put(array);
						json_object_put(obj_array);
						json_object_put(resultobject);
						jsonrpc_free_everything_and_return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
				}
			json_object_array_add(obj_array,obj);
			json_object_array_add(array,obj_array);
		}
		json_object_object_add(resultobject, "values", array);

/* Last : add the "result" to the result object */
		json_object_object_add(result, "result", resultobject);

		jsonrpc_free_everything_and_return (0);

		return(0);
}

static struct json_object *get_value_for_type(const char*identifier) {
		struct json_object *json_result = NULL;

		char *identifier_copy;

		char *hostname;
		char *plugin;
		char *plugin_instance;
		char *type;
		char *type_instance;
		gauge_t *values;
		size_t values_num;

		const data_set_t *ds;

		int   status;
		size_t i;

		identifier_copy = sstrdup (identifier);

		status = parse_identifier (identifier_copy, &hostname,
						&plugin, &plugin_instance,
						&type, &type_instance);
		if (status != 0)
		{
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Cannot parse identifier `%s'.", identifier);
				sfree (identifier_copy);
				return(NULL);
		}

		ds = plugin_get_ds (type);
		if (ds == NULL)
		{
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "plugin_get_ds (%s) == NULL;", type);
				sfree (identifier_copy);
				return(NULL);
		}

		values = NULL;
		values_num = 0;
		status = uc_get_rate_by_name (identifier, &values, &values_num);
		if (status != 0)
		{
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "uc_get_rate_by_name(%s,...) : No such value", identifier);
				sfree (identifier_copy);
				return (NULL);
		}

		if ((size_t) ds->ds_num != values_num)
		{
				ERROR (OUTPUT_PREFIX_JSONRPC_CB_BASE "ds[%s]->ds_num = %i, "
								"but uc_get_rate_by_name returned %u values.",
								ds->type, ds->ds_num, (unsigned int) values_num);
				sfree (values);
				sfree (identifier_copy);
				return (NULL);
		}

		if(NULL == (json_result= json_object_new_object())) {
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json object");
				sfree (values);
				sfree (identifier_copy);
				return(NULL);
		}
		for (i = 0; i < values_num; i++) {
				struct json_object *obj;

				if (isnan (values[i]))
				{
						if(NULL == (obj = json_object_new_string("NaN"))) {
								DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json object");
								json_object_put(json_result);
								sfree (values);
								sfree (identifier_copy);
								return(NULL);
						}
				}
				else
				{
						if(NULL == (obj = json_object_new_double(values[i]))) {
								DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json object");
								json_object_put(json_result);
								sfree (values);
								sfree (identifier_copy);
								return(NULL);
						}
				}

				json_object_object_add(json_result,  ds->ds[i].name, obj);
		}

		sfree (values);
		sfree (identifier_copy);

		return(json_result);
}

int jsonrpc_cb_getval (struct json_object *params, struct json_object *result, const char **errorstring) {
		struct json_object *resultobject = NULL;

		*errorstring = NULL;

		/* Create the result object */
		if(NULL == (resultobject = json_object_new_object())) {
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json object");
				return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}
		/* Parse the params */
		if(json_object_is_type (params, json_type_array)) {
				struct array_list *al;
				int array_len;
				int i;

				al = json_object_get_array(params);
				assert(NULL != al);
				array_len = json_object_array_length (params);
				for(i=0; i<array_len; i++) {
						struct json_object *element;
						element = json_object_array_get_idx(params, i);
						assert(NULL != element);
						if(json_object_is_type (element, json_type_string)) {
								const char *str;
								struct json_object *obj = NULL; /* if any error occur, append a NULL object */
								if(NULL != (str = json_object_get_string(element)))
										obj = get_value_for_type(str);
								json_object_object_add(resultobject, str, obj);
						} else {
								json_object_put(resultobject);
								return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
						}
				}
		} else if(json_object_is_type (params, json_type_object)) {
				struct json_object *element; struct lh_entry *entry; 
				/* warning : loop from json_object.h (0.9) file because the json_object_foreach does not work without C>=99 */
				for(entry = json_object_get_object(params)->head; (entry ? (element = (struct json_object*)entry->v, entry) : 0); entry = entry->next) {
						if(json_object_is_type (element, json_type_string)) {
								const char *str;
								struct json_object *obj = NULL; /* if any error occur, append a NULL object */
								if(NULL != (str = json_object_get_string(element)))
										obj = get_value_for_type(str);
								json_object_object_add(resultobject, str, obj);
						} else {
								json_object_put(resultobject);
								return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
						}
				}
		} else if(json_object_is_type (params, json_type_string)) {
			const char *str;
			struct json_object *obj = NULL; /* if any error occur, append a NULL object */
			if(NULL != (str = json_object_get_string(params)))
					obj = get_value_for_type(str);
			json_object_object_add(resultobject, str, obj);
		} else {
				json_object_put(resultobject);
				return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
		}

		/* Last : add the "result" to the result object */
		json_object_object_add(result, "result", resultobject);

		return(0);
}

