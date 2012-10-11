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
#include "jsonrpc.h"
#include <json/json.h>
#define OUTPUT_PREFIX_JSONRPC_CB_PERFWATCHER "JSONRPC plugin (perfwatcher) : "

int jsonrpc_cb_pw_get_status (struct json_object *params, struct json_object *result, const char **errorstring) {
		struct json_object *obj;
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



		/* Parse the params */
		if(!json_object_is_type (params, json_type_object)) {
				return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
		}
		/* Params : get the "timeout" */
		if(NULL == (obj = json_object_object_get(params, "timeout"))) {
				return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
		}
		if(!json_object_is_type (obj, json_type_int)) {
				return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
		}
		errno = 0;
		timeout = json_object_get_int(obj);
		if(errno != 0) {
				return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
		}
		/* Params : get the "server" array
		 * and fill the server tree and the servers_status array
		 */
		if(NULL == (server_array = json_object_object_get(params, "server"))) {
				return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
		}
		if(!json_object_is_type (server_array, json_type_array)) {
				return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
		}

		if(NULL == (servers = c_avl_create((int (*) (const void *, const void *)) strcmp))) {
				return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}
		al = json_object_get_array(server_array);
		assert(NULL != al);
		array_len = json_object_array_length (server_array);
		if(NULL == (servers_status = malloc(array_len * sizeof(*servers_status)))) {
				c_avl_destroy(servers);
				return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}
		for(i=0; i<array_len; i++) {
				struct json_object *element;
				const char *str;
				servers_status[i] = 0;
				element = json_object_array_get_idx(server_array, i);
				assert(NULL != element);
				if(!json_object_is_type (element, json_type_string)) {
						c_avl_destroy(servers);
						free(servers_status);
						return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
				}
				if(NULL == (str = json_object_get_string(element))) {
						c_avl_destroy(servers);
						free(servers_status);
						return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);

				}
				c_avl_insert(servers, (void*)str, (void*)NULL+i);
		}
		/* Get the names */
		status = jsonrpc_local_uc_get_names (&names, &times, &number);
		if (status != 0)
		{
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_PERFWATCHER "uc_get_names failed with status %i", status);
				c_avl_destroy(servers);
				free(servers_status);
				return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}

		/* Parse the cache and update the servers_status array*/
		for (i = 0; i < number; i++) {
				int j;
				gauge_t t;

				t = times[i];
				for(j=0; names[i][j] && names[i][j] != '/'; j++);
				names[i][j] = '\0';

				if(0 == c_avl_get(servers, names[i], &idx)) {
						assert((unsigned long)idx >= 0);
						assert((unsigned long)idx < array_len);
						if(times[i] > servers_status[(unsigned long)idx]) servers_status[(unsigned long)idx] = times[i];
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
				c_avl_destroy(servers);
				free(servers_status);
				return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}

		/* Append the values to the array */
		avl_iter = c_avl_get_iterator(servers);
		while (c_avl_iterator_next (avl_iter, (void *) &key, (void *) &idx) == 0) {
				if(servers_status[(unsigned long)idx] == 0) {
						obj =  json_object_new_string("unknown");
				} else if(servers_status[(unsigned long)idx] > now_before_timeout) {
						obj =  json_object_new_string("up");
				} else {
						obj =  json_object_new_string("down");
				}


				if(NULL == obj) {
						DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json string");
						c_avl_iterator_destroy(avl_iter);
						json_object_put(result_servers_object);
						c_avl_destroy(servers);
						free(servers_status);
						return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
				}
				json_object_object_add(result_servers_object, key, obj);
		}
		c_avl_iterator_destroy(avl_iter);

		/* Last : add the "result" to the result object */
		json_object_object_add(result, "result", result_servers_object);
		c_avl_destroy(servers);
		free(servers_status);

		return(0);
}


#define free_avl_tree_keys(tree) do {                                             \
			c_avl_iterator_t *it;                                                 \
			it = c_avl_get_iterator(tree);                                        \
			while (c_avl_iterator_next (it, (void *) &key, (void *) &useless_var) == 0) { \
					free(key);                                                    \
			}                                                                     \
			c_avl_iterator_destroy(it);                                           \
	} while(0)

int jsonrpc_cb_pw_get_metric (struct json_object *params, struct json_object *result, const char **errorstring) {
		struct json_object *result_metrics_array;
		c_avl_tree_t *servers;
		c_avl_tree_t *metrics;

		struct array_list *al;
		int array_len;
		int i;
		char **names = NULL;
		cdtime_t *times = NULL;
		size_t number = 0;
		int status;
		c_avl_iterator_t *avl_iter;
		char *key;
		void *useless_var;

		/* Parse the params */
		if(!json_object_is_type (params, json_type_array)) {
				return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
		}

		if(NULL == (servers = c_avl_create((int (*) (const void *, const void *)) strcmp))) {
				return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}
		if(NULL == (metrics = c_avl_create((int (*) (const void *, const void *)) strcmp))) {
				c_avl_destroy(servers);
				return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}
		al = json_object_get_array(params);
		assert(NULL != al);
		array_len = json_object_array_length (params);
		for(i=0; i<array_len; i++) {
				struct json_object *element;
				const char *str;
				element = json_object_array_get_idx(params, i);
				assert(NULL != element);
				if(!json_object_is_type (element, json_type_string)) {
						c_avl_destroy(servers);
						c_avl_destroy(metrics);
						return (JSONRPC_ERROR_CODE_32602_INVALID_PARAMS);
				}
				if(NULL == (str = json_object_get_string(element))) {
						c_avl_destroy(servers);
						c_avl_destroy(metrics);
						return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);

				}
				c_avl_insert(servers, (void*)str, (void*)NULL);
		}
		/* Get the names */
		status = jsonrpc_local_uc_get_names (&names, &times, &number);
		if (status != 0)
		{
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_PERFWATCHER "uc_get_names failed with status %i", status);
				c_avl_destroy(servers);
				c_avl_destroy(metrics);
				return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}

		/* Parse the cache and update the metrics list */
		for (i = 0; i < number; i++) {
				int j;

				for(j=0; names[i][j] && names[i][j] != '/'; j++);
				assert(names[i][j] != '\0');
				names[i][j] = '\0';

				if(
								(0 == c_avl_get(servers, names[i], NULL)) /* if the name is in the list */
								&& (0 != c_avl_get(metrics, names[i]+j+1, NULL)) /* and if the metric is NOT already known */
				  ) {
						char *m;
						if(NULL == (m = strdup(names[i]+j+1))) {
								c_avl_destroy(servers);
								free_avl_tree_keys(metrics);
								c_avl_destroy(metrics);
								for(j=i; i<number; j++) {
										sfree(names[j]);
										names[j] = NULL;
								}
								sfree(names);
								sfree(times);
								return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);

						}
						c_avl_insert(metrics, (void*)m, (void*)NULL);
				}
				sfree(names[i]);
				names[i] = NULL;
		}
		sfree(names);
		sfree(times);

		/* Check the servers and build the result array */
		if(NULL == (result_metrics_array = json_object_new_array())) {
				DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json array");
				c_avl_destroy(servers);
				free_avl_tree_keys(metrics);
				c_avl_destroy(metrics);
				return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
		}

		/* Append the values to the array */
		avl_iter = c_avl_get_iterator(metrics);
		while (c_avl_iterator_next (avl_iter, (void *) &key, (void *) &useless_var) == 0) {
				struct json_object *obj;

				if(NULL == (obj =  json_object_new_string(key))) {
						DEBUG (OUTPUT_PREFIX_JSONRPC_CB_BASE "Could not create a json string");
						c_avl_iterator_destroy(avl_iter);
						json_object_put(result_metrics_array);
						c_avl_destroy(servers);
						free_avl_tree_keys(metrics);
						c_avl_destroy(metrics);
						return (JSONRPC_ERROR_CODE_32603_INTERNAL_ERROR);
				}
			json_object_array_add(result_metrics_array,obj);
		}
		c_avl_iterator_destroy(avl_iter);

		/* Last : add the "result" to the result object */
		json_object_object_add(result, "result", result_metrics_array);
		c_avl_destroy(servers);
		free_avl_tree_keys(metrics);
		c_avl_destroy(metrics);

		return(0);
}

