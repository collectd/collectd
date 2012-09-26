/**
 * collectd - src/jsonrpc_cb_perfwatcher.c
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

#include "utils_avltree.h"
#include "utils_cache.h"
#include "common.h"
#include "plugin.h"
#include <json/json.h>
#define OUTPUT_PREFIX_JSONRPC_CB_PERFWATCHER "JSONRPC plugin (perfwatcher) : "

int jsonrpc_cb_pw_get_status (struct json_object *params, struct json_object *result, const char **errorstring) {
		struct json_object *obj;
		struct json_object *resultobject;
		struct json_object *result_servers_object;
		c_avl_tree_t *servers;
		cdtime_t *servers_status;
		cdtime_t now_before_timeout;
		int timeout;
		struct array_list *al;
		struct json_object *server_array;
		int array_len;
		int i;
		char **names = NULL;
		cdtime_t *times = NULL;
		size_t number = 0;
		int status;
		c_avl_iterator_t *avl_iter;
		char *key;
		void *idx;



		/* Create the result object */
		if(NULL == (resultobject = json_object_new_object())) {
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json object");
				return (-32603);
		}
		/* Parse the params */
		if(!json_object_is_type (params, json_type_object)) {
				json_object_put(resultobject);
				return (-32602);
		}
		/* Params : get the "timeout" */
		if(NULL == (obj = json_object_object_get(params, "timeout"))) {
				json_object_put(resultobject);
				return (-32602);
		}
		if(!json_object_is_type (obj, json_type_int)) {
				json_object_put(resultobject);
				return (-32602);
		}
		errno = 0;
		timeout = json_object_get_int(obj);
		if(errno != 0) {
				json_object_put(resultobject);
				return (-32602);
		}
		/* Params : get the "server" array
		 * and fill the server tree and the servers_status array
		 */
		if(NULL == (server_array = json_object_object_get(params, "server"))) {
				json_object_put(resultobject);
				return (-32602);
		}
		if(!json_object_is_type (server_array, json_type_array)) {
				json_object_put(resultobject);
				return (-32602);
		}

		if(NULL == (servers = c_avl_create((int (*) (const void *, const void *)) strcmp))) {
				json_object_put(resultobject);
				return (-32603);
		}
		al = json_object_get_array(server_array);
		assert(NULL != al);
		array_len = json_object_array_length (server_array);
		if(NULL == (servers_status = malloc(array_len * sizeof(*servers_status)))) {
				json_object_put(resultobject);
				c_avl_destroy(servers);
				return (-32603);
		}
		for(i=0; i<array_len; i++) {
				struct json_object *element;
				const char *str;
				servers_status[i] = 0;
				element = json_object_array_get_idx(server_array, i);
				assert(NULL != element);
				if(!json_object_is_type (element, json_type_string)) {
						json_object_put(resultobject);
						c_avl_destroy(servers);
						free(servers_status);
						return (-32602);
				}
				if(NULL == (str = json_object_get_string(element))) {
						json_object_put(resultobject);
						c_avl_destroy(servers);
						free(servers_status);
						return (-32603);

				}
				c_avl_insert(servers, (void*)str, (void*)NULL+i);
		}
		/* Get the names */
		status = uc_get_names (&names, &times, &number);
		if (status != 0)
		{
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_PERFWATCHER "uc_get_names failed with status %i", status);
				json_object_put(resultobject);
				c_avl_destroy(servers);
				free(servers_status);
				return (-32603);
		}

		/* Parse the cache and update the servers_status array*/
		for (i = 0; i < number; i++) {
				int j;
				gauge_t t;

				t = times[i];
				for(j=0; names[i][j] && names[i][j] != '/'; j++);
				names[i][j] = '\0';

				if(0 == c_avl_get(servers, names[i], &idx)) {
						assert((long)idx > 0);
						assert((long)idx < array_len);
						if(times[i] > servers_status[(long)idx]) servers_status[(long)idx] = times[i];
				}
				sfree(names[i]);
				names[i] = NULL;
		}
		sfree(names);
		sfree(times);

		/* What time is it ? */
		now_before_timeout = cdtime();
		now_before_timeout -= (TIME_T_TO_CDTIME_T(timeout));


		/* Check the servers and build the result array */
		if(NULL == (result_servers_object = json_object_new_object())) {
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json array");
				json_object_put(resultobject);
				c_avl_destroy(servers);
				free(servers_status);
				return (-32603);
		}

		/* Append the values to the array */
		avl_iter = c_avl_get_iterator(servers);
		while (c_avl_iterator_next (avl_iter, (void *) &key, (void *) &idx) == 0) {
				if(servers_status[(long)idx] == 0) {
						obj =  json_object_new_string("unknown");
				} else if(servers_status[(long)idx] > now_before_timeout) {
						obj =  json_object_new_string("up");
				} else {
						obj =  json_object_new_string("down");
				}


				if(NULL == obj) {
						DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json string");
						c_avl_iterator_destroy(avl_iter);
						json_object_put(result_servers_object);
						json_object_put(resultobject);
						c_avl_destroy(servers);
						free(servers_status);
						return (-32603);
				}
				json_object_object_add(result_servers_object, key, obj);
		}

		/* Last : add the "result" to the result object */
		json_object_object_add(result, "result", result_servers_object);
		c_avl_destroy(servers);
		free(servers_status);

		return(0);
}

