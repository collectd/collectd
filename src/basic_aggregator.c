/**
 * collectd - src/basic_aggregator.c
 * Copyright (C) 2012       Cyril Feraudet
 * Copyright (C) 2012       Yves Mettier
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Cyril Feraudet <cyril at feraudet.com>
 *   Yves Mettier <ymettier at free.fr>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include "utils_cache.h"
#include "utils_parse_option.h"
#include "utils_avltree.h"

#include <pthread.h>

#define PLUGIN_NAME_PREFIX "AGGREGATOR "
#define OUTPUT_PREFIX_STRING "basic_aggregator plugin: "

/* constants for dynamic array of values
 * -> VALUELIST_NB_INITIAL_SIZE is the initial size of the array
 * -> Every time the array is resized, its size is incremented of VALUELIST_NB_INCREMENT
 */
#define VALUELIST_NB_INITIAL_SIZE 1024
#define VALUELIST_NB_INCREMENT 1024

/*
 * Plugin description
 * ------------------
 * 1. Read the configuration and define the aggregators.
 * Each aggregator definition will be stored in a aggregator_definition_t structure.
 * All those aggregator_definition_t are put in an avl_tree called... aggregator !
 * Register a read callback for them.
 *
 * 2. Register one more callback : instances_of_types_update.
 * This callback will scan the cache tree and keep a list of types (for example "cpu") 
 * and all its instances (for example "idle", "wait"...)
 * This list is stored into a c_avl_tree_t *instances_of_types_tree.
 * Each node of instances_of_types_tree is (key="type", value=instances_list_t*).
 * The values (instances_list_t) contains a NULL terminated string array (char **) : the type instances.
 *
 * 3.1 The read callback runs each aggregator by iterating on the aggregator avl_tree.
 * For each aggregator agg, run basic_aggregator_read(agg)
 *
 * 3.2 Each aggregator start initializing a c_avl_tree_t that contains the aggregated values.
 * In other words, each time the basic_aggregator_read() is called, it starts with an empty tree.
 *
 * 3.3 For each type (if "type") or instance of type (if "alltypesof" - get the list thanks
 * to the instances_of_types_update callback), call basic_aggregator_update_aggregator().
 *
 * 3.4 basic_aggregator_update_aggregator() finds ou what type and instance to update.
 * Then for that instance, and for each ds, do the aggregation operations (sum...)
 *
 * 4. At the end of basic_aggregator_read(), call basic_aggregator_submit_resultvalue().
 * This function scans the tree with the aggregated values and dispatch the results.
 *
 */

static char           *configuration_filename = NULL;
static time_t          configuration_filename_date = 0;
static c_avl_tree_t   *aggregator = NULL;
static c_avl_tree_t   *instances_of_types_tree = NULL;
static pthread_mutex_t instances_of_types_mutex = PTHREAD_MUTEX_INITIALIZER;

typedef enum {
	operation_SUM,
	operation_AVG,
	nb_enum_in_aggregator_operation_e
} aggregator_operation_e;

static const char *config_keys[] =
{
    "Aggregators_config_file"
};

static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/* 
 * Aggregators are defined as one read callback per aggregator.
 * Each aggregator has its own definition inside a aggregator_definition_t.
 */
typedef struct aggregator_definition_s {
	char *resultvalue; /* the result value (the type instance will be ignored if any) */
	aggregator_operation_e operation[nb_enum_in_aggregator_operation_e];
						/* what operations to do (sum, avg... */
	char **type_list;	/* List of types to aggregate. For an element, NULL means no type, but not the end of the list. */
	char *is_alltypesof; /* Flag to know if it's a litteral type or if we should get all its instances */
	int type_list_size; /* size of type_list array. Note : this is not the number of types */
} aggregator_definition_t;

typedef struct value_and_nb_s {
	gauge_t val;
	int nb;
} value_and_nb_t;

typedef struct instances_list_s {
	char **instance; /* NULL terminated array */
	int nb;          /* Size of the array (may be bigger that its number of elements) */
} instances_list_t;

/* Not used yet 
typedef struct mysql_database_s
{
//	char *instance;
	char *identifier;
	char *host;
	char *user;
	char *passwd;
	char *database;
	int   port;
//	_Bool replace;
//	c_avl_tree_t *host_tree, *plugin_tree, *type_tree, *dataset_tree; 
//	MYSQL *conn;
//	MYSQL_BIND data_bind[8], notif_bind[8];
//	MYSQL_STMT *data_stmt, *notif_stmt;
//	pthread_mutex_t mutexdb;
//	pthread_mutex_t mutexhost_tree, mutexplugin_tree;
//	pthread_mutex_t mutextype_tree, mutexdataset_tree;
//	char data_query[1024];
} mysql_database_t; 
*/

void print_aggregator_definitions (aggregator_definition_t *agg) {
		int i;
		WARNING (OUTPUT_PREFIX_STRING "DEBUG DEBUG DEGUG");
		WARNING (OUTPUT_PREFIX_STRING "Config structure");
		WARNING (OUTPUT_PREFIX_STRING "resultvalue = '%s'", agg->resultvalue?agg->resultvalue:"**not defined**");
		WARNING (OUTPUT_PREFIX_STRING "operation SUM = '%d' ", agg->operation[operation_SUM]);
		WARNING (OUTPUT_PREFIX_STRING "operation AVG = '%d' ", agg->operation[operation_AVG]);
		if(agg->type_list) {
			for(i=0; i<agg->type_list_size; i++) {
				if(agg->type_list[i]) WARNING (OUTPUT_PREFIX_STRING "   value = '%s' (is_alltypesof=%d)", agg->type_list[i], agg->is_alltypesof[i]);
			}
		} else {
			WARNING (OUTPUT_PREFIX_STRING "  no defined values");
		}
		WARNING (OUTPUT_PREFIX_STRING "DEBUG DEBUG DEGUG");

}

void
free_data_tree (c_avl_tree_t *t) {
	void *key;
	void *value;
	void *tkey;
	c_avl_tree_t *tt;
	assert(t != NULL);

	while (c_avl_pick (t, &tkey, (void *)&tt) == 0) {
		free(tkey);
		while (c_avl_pick (tt, &key, &value) == 0) {
			free (key);
			free (value);
		}
		c_avl_destroy (tt);
	}
	c_avl_destroy (t);
}

void instances_of_types_tree_print(void) {
	c_avl_iterator_t *iter;
	char *k;
	instances_list_t *v;
	int n=0;

	INFO(OUTPUT_PREFIX_STRING "INSTANCES");
	pthread_mutex_lock (&instances_of_types_mutex);
	iter = c_avl_get_iterator (instances_of_types_tree);
	while (c_avl_iterator_next (iter, (void *) &k, (void *) &v) == 0)
	{
		int i;
			INFO(OUTPUT_PREFIX_STRING "INSTANCES of %s", k);
		for(i=0; v->instance[i]; i++) {
			INFO(OUTPUT_PREFIX_STRING "INSTANCES :        %s", v->instance[i]);
		}
		n++;
	} /* while (c_avl_iterator_next) */
	c_avl_iterator_destroy (iter);
	pthread_mutex_unlock (&instances_of_types_mutex);
	INFO(OUTPUT_PREFIX_STRING "INSTANCES nb=%d",n);
}

static int
instances_of_types_tree_update (void) {
	char **names = NULL;
	cdtime_t *times = NULL;
	size_t number = 0;
	int i;
	int status = 0;

	if(NULL == instances_of_types_tree) {
		instances_of_types_tree = c_avl_create ((void *) strcmp);
	}

	status = uc_get_names (&names, &times, &number);
	if (status != 0)
	{
			size_t j; 
			DEBUG (OUTPUT_PREFIX_STRING "uc_get_names failed with status %i", status);
			for (j = 0; j < number; j++) { 
					sfree(names[j]); 
			} 
			sfree(names); 
			sfree(times); 
			return(status);
	}

	if(number == 0) {
		return(0);
	}
	pthread_mutex_lock (&instances_of_types_mutex);
	for(i=0; i<number; i++) {
		char *type;
		int l1,l2;
		int pos =0;
		instances_list_t *v;
		char *type_instance;
		int type_instance_is_missing = 1;

		for(l1=0; names[i][l1] && (pos < 2); l1++) {
			if(names[i][l1] == '/') pos++;
		}
		if(names[i][l1] == '\0') {
			sfree(names[i]);
			continue;
		}
		l2 = l1;
		while(names[i][l2] && (names[i][l2] != '-')) l2++;
		if(names[i][l2] == '\0') {
			sfree(names[i]);
			continue;
		}
		type = names[i]+l1;
		names[i][l2] = '\0';
		type_instance = names[i]+l2+1;


		if(0 == c_avl_get(instances_of_types_tree, type,(void*)&v)) {
			int i;
			for(i=0; v->instance[i]; i++) {
				if(0 == strcmp(v->instance[i], type_instance)) {
					type_instance_is_missing = 0;
					break;
				}
			}
			if(type_instance_is_missing) {
				if(i >= v->nb) {
					v->nb += 128;
					if(NULL == (v->instance = realloc(v->instance, v->nb*sizeof(*(v->instance))))) {
						ERROR(OUTPUT_PREFIX_STRING "Could not allocate memory");
						pthread_mutex_unlock (&instances_of_types_mutex);
						return(-1);
					}
					}
				v->instance[i+1] = NULL;
				if(NULL == (v->instance[i] = sstrdup(type_instance))) {
					ERROR(OUTPUT_PREFIX_STRING "Could not allocate memory");
					pthread_mutex_unlock (&instances_of_types_mutex);
					return(-1);
				}
			}
		} else {
			char *k;
			if(NULL == (v = malloc(sizeof(*v)))) {
				ERROR(OUTPUT_PREFIX_STRING "Could not allocate memory");
				pthread_mutex_unlock (&instances_of_types_mutex);
				return(-1);
			}
			v->nb = 128;
			if(NULL == (v->instance = malloc(v->nb*sizeof(*(v->instance))))) {
				ERROR(OUTPUT_PREFIX_STRING "Could not allocate memory");
				sfree(v);
				pthread_mutex_unlock (&instances_of_types_mutex);
				return(-1);
			}
			if(NULL == (k = sstrdup(type))) {
				ERROR(OUTPUT_PREFIX_STRING "Could not allocate memory");
				sfree(v);
				pthread_mutex_unlock (&instances_of_types_mutex);
				return(-1);
			}
			if(NULL == (v->instance[0] = sstrdup(type_instance))) {
				ERROR(OUTPUT_PREFIX_STRING "Could not allocate memory");
				sfree(v);
				pthread_mutex_unlock (&instances_of_types_mutex);
				return(-1);
			}
			v->instance[1] = NULL;
			if(0 != c_avl_insert(instances_of_types_tree, k,v)) {
				ERROR (OUTPUT_PREFIX_STRING "Could insert data into AVL tree");
				sfree(v->instance[0]);
				sfree(v);
				pthread_mutex_unlock (&instances_of_types_mutex);
				return(-1);
			}
		}
		sfree(names[i]); 
	}
	pthread_mutex_unlock (&instances_of_types_mutex);
	sfree(names); 
	sfree(times); 
	return(0);
}

static int basic_aggregator_submit_resultvalue (
	const aggregator_definition_t *agg,
	c_avl_tree_t *ds_data
	)
{
	value_t *values;
	value_list_t vl = VALUE_LIST_INIT;
	const data_set_t *ds;
	char *identifier;
	char *hostname;
	char *plugin;
	char *plugin_instance;
	char *type;
	char *type_instance;
	int  status;
	int i;
	int submit_ok;
	aggregator_operation_e operation;
	c_avl_iterator_t *iter;
	char *key_instance;
	c_avl_tree_t *ds_tree;

	identifier = sstrdup (agg->resultvalue);

	status = parse_identifier (identifier, &hostname,
					&plugin, &plugin_instance,
					&type, &type_instance);
	if (status != 0)
	{
		ERROR (OUTPUT_PREFIX_STRING "Cannot parse value `%s'.", agg->resultvalue);
		sfree (identifier);
		return (-1);
	}

	ds = plugin_get_ds (type);
	if (ds == NULL)
	{
		ERROR (OUTPUT_PREFIX_STRING "plugin_get_ds (%s) == NULL;", type);
		sfree (identifier);
		return (-1);
	}
	
	if(NULL == (values = malloc(ds->ds_num* sizeof(*values)))) {
		ERROR (OUTPUT_PREFIX_STRING "Could not allocate memory");
		sfree (identifier);
		return (-1);
	}
	
	iter = c_avl_get_iterator (ds_data);
	while (c_avl_iterator_next (iter, (void *) &key_instance, (void *) &ds_tree) == 0) {
	
		for (operation=0; operation < nb_enum_in_aggregator_operation_e; operation++) {
			if(0 == agg->operation[operation]) continue;
			memset(values, '\0', ds->ds_num*sizeof(*values));
			submit_ok = 1;
			for(i=0; i<ds->ds_num; i++) {
				value_and_nb_t *v;
				if(0 == c_avl_get(ds_tree, ds->ds[i].name,(void*)&v)) {
					switch(operation) {
						case operation_SUM : values[i].gauge = v->val; break;
						case operation_AVG : values[i].gauge = v->val/v->nb; break;
						default : assert (11 == 12); /* this should never happen */
					}
				} else {
						submit_ok = 0;
				}
	
			}
			if(submit_ok) {
				int l = 0;
				int pos = 0;
	
				vl.values = values;
				vl.values_len = ds->ds_num;
				
				if(plugin_instance) {
					l = strlen(plugin_instance);
				}
				if(l) {
					sstrncpy (vl.plugin_instance, plugin_instance, l+1);
					pos += l;
					sstrncpy (vl.plugin_instance + pos, "_", DATA_MAX_NAME_LEN - pos);
					pos += 1;
				}
				switch(operation) {
						case operation_SUM : sstrncpy (vl.plugin_instance + pos, "sum", DATA_MAX_NAME_LEN - pos); break;
						case operation_AVG : sstrncpy (vl.plugin_instance + pos, "avg", DATA_MAX_NAME_LEN - pos); break;
						default : assert (11 == 12); /* this should never happen */
				}
	
				sstrncpy (vl.host, hostname, sizeof (vl.host));
				sstrncpy (vl.plugin, plugin, sizeof (vl.plugin));
				sstrncpy (vl.type, type, sizeof (vl.type));
				if(key_instance[0]) sstrncpy (vl.type_instance, key_instance, sizeof (vl.type_instance));
	
#ifdef DEBUG_THIS
INFO(OUTPUT_PREFIX_STRING "DEBUG : dispatch '%s/%s-%s/%s%s%s'", vl.host, vl.plugin, vl.plugin_instance, vl.type,
	                                                               (vl.type_instance && vl.type_instance[0])?"-":"",
																   vl.type_instance?vl.type_instance:"(null)");
#endif
#ifdef DEBUG_THIS
do {
	int i;
	if(strcmp(vl.host, "aggregator_2")) break;
	for(i=0; i<vl.values_len; i++) {
INFO(OUTPUT_PREFIX_STRING "DEBUG : dispatch '%s/%s-%s/%s%s%s DS '%s'=%12e", vl.host, vl.plugin, vl.plugin_instance, vl.type,
	                                                               (vl.type_instance && vl.type_instance[0])?"-":"",
																   vl.type_instance?vl.type_instance:"(null)",
																   ds->ds[i].name, values[i].gauge
																   );
	}
} while(0);
#endif
				plugin_dispatch_values (&vl);
			}
		}
	} /* while (c_avl_iterator_next) */
	c_avl_iterator_destroy (iter);
	sfree (identifier);
	return(0);
} /* void basic_aggregator_submit_resultvalue */

static int
basic_aggregator_update_aggregator(char *identifier, c_avl_tree_t *ds_data, aggregator_definition_t *agg) {
		char *identifier_copy;
		char *hostname;
		char *plugin;
		char *plugin_instance;
		char *type;
		char *type_instance;
		gauge_t *values;
		size_t values_num;
		int status;
		size_t i;
		const data_set_t *ds;

		/* parse_identifier() modifies its first argument,
		 * returning pointers into it */
		identifier_copy = sstrdup (identifier);

		status = parse_identifier (identifier_copy, &hostname,
						&plugin, &plugin_instance,
						&type, &type_instance);
		if (status != 0)
		{
			WARNING (OUTPUT_PREFIX_STRING "Cannot parse value `%s'.", identifier);
			WARNING (OUTPUT_PREFIX_STRING "Value `%s' is removed from the aggregator '%s'.", identifier, agg->resultvalue);
			sfree (identifier_copy);
			return(1);
		}

		ds = plugin_get_ds (type);
		if (ds == NULL)
		{
			WARNING (OUTPUT_PREFIX_STRING "plugin_get_ds (%s) == NULL;", type);
			WARNING (OUTPUT_PREFIX_STRING "Value `%s' is removed from the aggregator '%s'.", identifier, agg->resultvalue);
			sfree (identifier_copy);
			return(1);
		}

		values = NULL;
		values_num = 0;
		status = uc_get_rate_by_name (identifier, &values, &values_num);
		if (status != 0)
		{
			DEBUG(OUTPUT_PREFIX_STRING "uc_get_rate_by_name failed for %s", identifier);
			sfree (identifier_copy);
			return(2);
		}

		if ((size_t) ds->ds_num != values_num)
		{
			ERROR ("ds[%s]->ds_num = %i, "
							"but uc_get_rate_by_name returned %u values.",
							ds->type, ds->ds_num, (unsigned int) values_num);
			sfree (values);
			sfree (identifier_copy);
			return (-1);
		}

		for (i = 0; i < values_num; i++)
		{
			if ( ! isnan (values[i]))
			{
				char *k;
				c_avl_tree_t *t;
				value_and_nb_t *v;

				if(0 != c_avl_get(ds_data, type_instance?type_instance:"",(void*)&t)) {
					if(NULL == (t = c_avl_create((void *) strcmp))) {
						ERROR (OUTPUT_PREFIX_STRING "Could not allocate memory for tree");
						sfree (identifier_copy);
						return(-1);

					}
					if(NULL == (k=strdup(type_instance?type_instance:""))) {
						ERROR (OUTPUT_PREFIX_STRING "Could not allocate memory");
						c_avl_destroy(t);
						sfree (identifier_copy);
						return(-1);
					}
					if(0 != c_avl_insert(ds_data, k,t)) {
						ERROR (OUTPUT_PREFIX_STRING "Could insert data into AVL tree");
						c_avl_destroy(t);
						sfree (identifier_copy);
						return(-1);
					}
				}
				if(0 != c_avl_get(t, ds->ds[i].name,(void*)&v)) {
					if(NULL == (k=strdup(ds->ds[i].name))) {
						ERROR (OUTPUT_PREFIX_STRING "Could not allocate memory");
						sfree (identifier_copy);
						return(-1);
					}
					if(NULL == (v=malloc(sizeof(*v)))) {
						ERROR (OUTPUT_PREFIX_STRING "Could not allocate memory");
						sfree (identifier_copy);
						return(-1);
					}
					v->val=0;
					v->nb=0;
					if(0 != c_avl_insert(t, k,v)) {
						ERROR (OUTPUT_PREFIX_STRING "Could insert data into AVL tree");
						sfree (identifier_copy);
						return(-1);
					}
				}
				v->val += values[i];
				v->nb +=1;
#ifdef DEBUG_THIS
				if(values[i] > 1000) {

INFO(OUTPUT_PREFIX_STRING "DEBUG : ATTENTION '%s/%s-%s/%s%s%s DS '%s'=%12e", hostname, plugin, plugin_instance, type,
	                                                               (type_instance && type_instance[0])?"-":"",
																   type_instance?type_instance:"(null)",
																   ds->ds[i].name, values[i]
																   );
				}
#endif

			}
		}

		sfree (values);
		sfree (identifier_copy);
		return(status);
}

static int
basic_aggregator_config_aggregator_get_all_instances_of_type(char ***type_instances, char*type) {
	char **names = NULL;
	char *subtype;
	int l;
	int pos = 0;
	instances_list_t *v;
	int i;
	int typelen;
	*type_instances = NULL;

	for(l=0; type[l] && (pos < 2); l++) {
		if(type[l] == '/') pos++;
	}
	if(type[l] == '\0') {
		return(-1);
	}
	subtype = type+l;

	pthread_mutex_lock (&instances_of_types_mutex);
	if(0 != c_avl_get(instances_of_types_tree, subtype,(void*)&v)) {
		pthread_mutex_unlock (&instances_of_types_mutex);
		return(0);
	}
	for(i=0; v->instance[i]; i++); /* Count how many instances the type has */
	if(NULL == (names = malloc((i+1)*sizeof(*type_instances)))) {
		ERROR(OUTPUT_PREFIX_STRING "Could not allocate memory");
		pthread_mutex_unlock (&instances_of_types_mutex);
		return(-1);
	}
	typelen = strlen(type);
	for(i=0; v->instance[i]; i++) {
		int li;
		li = strlen(v->instance[i]);
		if(NULL == (names[i] = malloc((typelen+2+li)*sizeof(**names)))) {
			int j;
			ERROR(OUTPUT_PREFIX_STRING "Could not allocate memory");
			for(j=0; j<i; j++) free(names[j]);
			free(names);
			pthread_mutex_unlock (&instances_of_types_mutex);
			return(-1);
		}
		memcpy(names[i], type, typelen);
		names[i][typelen] = '-';
		memcpy(names[i]+typelen+1, v->instance[i],li+1);
	}
	names[i] = NULL;
	pthread_mutex_unlock (&instances_of_types_mutex);
	*type_instances = names;
		
	return(0);
}


static int
basic_aggregator_read (aggregator_definition_t *agg) {
	int aggid;
	c_avl_tree_t *ds_data;
	int   status = 0;

	ds_data = c_avl_create ((void *) strcmp);

	for(aggid=0; aggid < agg->type_list_size; aggid++) {
		if(NULL == agg->type_list[aggid]) continue;

		if(agg->is_alltypesof[aggid]) {
			char **names;
			int i;
			if(0 == basic_aggregator_config_aggregator_get_all_instances_of_type(&names, agg->type_list[aggid])) {
				if(names) {
					for(i=0; names[i]; i++) {
						status = basic_aggregator_update_aggregator(names[i], ds_data, agg);
						sfree(names[i]);
						if(status != 0) continue;
					}
					sfree(names);
				}
			}
		} else {
			status = basic_aggregator_update_aggregator(agg->type_list[aggid], ds_data, agg);
		}

		if(status == 1) {
			free(agg->type_list[aggid]);
			agg->type_list[aggid]=NULL;
		}
		if(status > 0) continue;
		if(status < 0) return(status);
	}

	if(0 != basic_aggregator_submit_resultvalue(agg, ds_data)) {
		return(-1);

	}

  /* Free the tree */
	free_data_tree(ds_data);
	return (0);
}

static int
basic_aggregator_config_aggregator_append_type(aggregator_definition_t *agg, char*value, char is_alltypesof) {
	char *str;
	int i;

	if(NULL == value) return(-1);
	if(NULL == (str=strdup(value))) return(-1);

	if(NULL == agg->type_list) {
		/* list does not exist. Initialize it */
		if(NULL == (agg->type_list = malloc(VALUELIST_NB_INITIAL_SIZE*sizeof(*(agg->type_list))))) {
			free(str);
			return(-1);
		}
		if(NULL == (agg->is_alltypesof = malloc(VALUELIST_NB_INITIAL_SIZE*sizeof(*(agg->is_alltypesof))))) {
			free(str);
			return(-1);
		}
		memset(agg->type_list, '\0', VALUELIST_NB_INITIAL_SIZE*sizeof(*(agg->type_list)));
		memset(agg->is_alltypesof, '\0', VALUELIST_NB_INITIAL_SIZE*sizeof(*(agg->is_alltypesof)));
		agg->type_list_size = VALUELIST_NB_INITIAL_SIZE;
	}
	for(i=0; i<agg->type_list_size; i++) {
		if(NULL == agg->type_list[i]) break;
	}
	if(i == agg->type_list_size) {
		/* array is too small. Resize it */
		if(NULL == (agg->type_list = realloc(agg->type_list, (VALUELIST_NB_INCREMENT+agg->type_list_size)*sizeof(*(agg->type_list))))) {
			free(str);
			return(-1);
		}
		if(NULL == (agg->is_alltypesof = realloc(agg->is_alltypesof, (VALUELIST_NB_INCREMENT+agg->type_list_size)*sizeof(*(agg->is_alltypesof))))) {
			free(str);
			return(-1);
		}
		memset(agg->type_list+agg->type_list_size, '\0', VALUELIST_NB_INCREMENT*sizeof(*(agg->type_list)));
		memset(agg->type_list+agg->type_list_size, '\0', VALUELIST_NB_INCREMENT*sizeof(*(agg->is_alltypesof)));
		agg->type_list_size += VALUELIST_NB_INCREMENT;
	}
	agg->type_list[i] = str;
	agg->is_alltypesof[i] = is_alltypesof;
	return(0);
}

static int
basic_aggregator_config_aggregator_add_data_resultvalue (aggregator_definition_t *agg, oconfig_item_t *ci) {

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING (OUTPUT_PREFIX_STRING "`resultvalue' needs exactly one string argument.");
		return (-1);
	}

	if(agg->resultvalue) {
		WARNING (OUTPUT_PREFIX_STRING "`resultvalue' defined twice.");
		return (-1);

	}

	agg->resultvalue = strdup (ci->values[0].value.string);
	if(NULL == agg->resultvalue) 
		return (-1);

	return (0);
}

static int
basic_aggregator_config_aggregator_add_data_operation (aggregator_definition_t *agg, oconfig_item_t *ci) {
	int i;

	if (ci->values_num < 1) {
		WARNING (OUTPUT_PREFIX_STRING "`operation' needs string arguments.");
		return (-1);
	}

	for(i=0; i<ci->values_num; i++) {
	
		if (ci->values[i].type != OCONFIG_TYPE_STRING)
		{
			WARNING (OUTPUT_PREFIX_STRING "`operation' needs string arguments.");
			return (-1);
		}
		if(!strcmp(ci->values[i].value.string, "SUM")) {
			agg->operation[operation_SUM] = 1;
		} else if(!strcmp(ci->values[i].value.string, "AVG")) {
			agg->operation[operation_AVG] = 1;
		} else {
			WARNING (OUTPUT_PREFIX_STRING "'%s' for `operation' is not a known value.", ci->values[i].value.string);
			return (-1);
		}
	}

	return (0);
}

static int
basic_aggregator_config_aggregator_add_data_valuelist_manual (aggregator_definition_t *agg, oconfig_item_t *ci) {
	int i;
	int status = 0;

	for(i=0; i<ci->children_num; i++) {
		oconfig_item_t *child = ci->children + i;
		if (strcasecmp ("type", child->key) == 0) {
			if (child->values[0].type != OCONFIG_TYPE_STRING) {
				WARNING (OUTPUT_PREFIX_STRING "'type' of 'valuelist' needs exactly one string argument.");
				status = -1;
			} else {
					basic_aggregator_config_aggregator_append_type(agg, child->values[0].value.string,'\0');
			}
		} else if (strcasecmp ("alltypesof", child->key) == 0) {
			if (child->values[0].type != OCONFIG_TYPE_STRING) {
				WARNING (OUTPUT_PREFIX_STRING "'type' of 'valuelist' needs exactly one string argument.");
				status = -1;
			} else {
					basic_aggregator_config_aggregator_append_type(agg, child->values[0].value.string,'\1');
			}
		} else {
			WARNING (OUTPUT_PREFIX_STRING "Option '%s' not allowed for valuelist.",
					child->key);
			status = -1;
		}
	
	}
	return (status);
}

static int
basic_aggregator_config_check (aggregator_definition_t *agg)
{
	int check=0;
	int op=0;
	int i;

	if(NULL == agg->resultvalue) {
		check=-1;
		ERROR (OUTPUT_PREFIX_STRING "resultvalue is not defined");
	}
	for(i=0; i<nb_enum_in_aggregator_operation_e; i++) {
		op |= agg->operation[i];
	}
	if(0 == op) {
		check=-1;
		ERROR (OUTPUT_PREFIX_STRING "aggregator '%s' : no operation (SUM, AVG...) defined", agg->resultvalue);
	}
	if(NULL == agg->type_list) {
		ERROR (OUTPUT_PREFIX_STRING "aggregator '%s' : no defined values", agg->resultvalue);
		check=-1;
	}
	return(check);
}

static void
aggregator_definition_free(aggregator_definition_t *agg, short free_result_value) {
		int i;
		if(agg->resultvalue && free_result_value) free(agg->resultvalue);
		if(agg->type_list) {
				for(i=0; i<agg->type_list_size; i++) {
						if(agg->type_list[i]) free(agg->type_list[i]);
				}
				free(agg->type_list);
		}
		if(agg->is_alltypesof) free(agg->is_alltypesof);
		free(agg);
}

aggregator_definition_t *
basic_aggregator_config_aggregator (oconfig_item_t *ci) {
	aggregator_definition_t *agg;
	int i;
	int status = 0;

	if(NULL == (agg = calloc(1, sizeof(aggregator_definition_t)))) return(NULL);
	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("resultvalue", child->key) == 0) {
			status = basic_aggregator_config_aggregator_add_data_resultvalue(agg, child);
		} else if (strcasecmp ("operation", child->key) == 0) {
			status = basic_aggregator_config_aggregator_add_data_operation(agg, child);
		} else if (strcasecmp ("valuelist", child->key) == 0) {
			if ((child->values_num < 1) || (child->values[0].type != OCONFIG_TYPE_STRING)) {
				WARNING (OUTPUT_PREFIX_STRING "`valuelist' needs a type as its first argument.");
				status = -1;
			} else {
				if (strcasecmp ("manual", child->values[0].value.string) == 0) {
					status = basic_aggregator_config_aggregator_add_data_valuelist_manual(agg, child);
				} else if (strcasecmp ("mysql", child->values[0].value.string) == 0) {
				} else {
					WARNING (OUTPUT_PREFIX_STRING "'%s' is not a known type for `valuelist'.", ci->values[0].value.string);
					status = -1;
				}
			}

		} else {
			WARNING (OUTPUT_PREFIX_STRING "Option \"%s\" not allowed here.",
					child->key);
			status = -1;
		}

		if(0 != status) break;
	}

	if(0 != basic_aggregator_config_check(agg))
		return(NULL);

	if(0 != status) {
		if(agg) aggregator_definition_free(agg,1);
		return(NULL);
	} 
	return(agg);
}

/* Not used yet
static int
basic_aggregator_config_mysql_database (oconfig_item_t *ci)
{
	mysql_database_t *db;
	int status = 0;
	int i;

	if ((ci->values_num != 1)
	    || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING (OUTPUT_PREFIX_STRING "The `Database' block "
			 "needs exactly one string argument.");
		return (-1);
	}

	db = (mysql_database_t *) malloc (sizeof (*db));
	if (db == NULL)
	{
		ERROR (OUTPUT_PREFIX_STRING "malloc failed.");
		return (-1);
	}
	memset (db, '\0', sizeof (*db));
	db->host		= NULL;
	db->user		= NULL;
	db->passwd		= NULL;
	db->database	= NULL;
	db->port		= 0;
//	db->replace		= NULL;

//	status = cf_util_get_string (ci, &db->instance);
//	if (status != 0)
//	{
//	    sfree (db);
//		return (status);
//	}
//	assert (db->instance != NULL);
	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;
		if (strcasecmp ("Identifier", child->key) == 0)
			status = cf_util_get_string (child, &db->identifier);
		else if (strcasecmp ("Host", child->key) == 0)
			status = cf_util_get_string (child, &db->host);
		else if (strcasecmp ("User", child->key) == 0)
			status = cf_util_get_string (child, &db->user);
		else if (strcasecmp ("Passwd", child->key) == 0)
			status = cf_util_get_string (child, &db->passwd);
		else if (strcasecmp ("Port", child->key) == 0)
		{
			status = cf_util_get_port_number (child);
			if (status > 0)
			{
				db->port = status;
				status = 0;
			}
		}
		else if (strcasecmp ("Database", child->key) == 0)
			status = cf_util_get_string (child, &db->database);
//		else if (strcasecmp ("Replace", child->key) == 0)
//		    status = cf_util_get_boolean (child, &db->replace);
		else
        {
            WARNING (OUTPUT_PREFIX_STRING "Option `%s' not allowed here.", child->key);
            status = -1;
        }

        if (status != 0)
            break;
	}

	if (status == 0)
	{
		INFO (OUTPUT_PREFIX_STRING "Mysql : identifier = '%s'", db->identifier);
		INFO (OUTPUT_PREFIX_STRING "Mysql : host = '%s'", db->host);
		INFO (OUTPUT_PREFIX_STRING "Mysql : user = '%s'", db->user);
		INFO (OUTPUT_PREFIX_STRING "Mysql : passwd = '%s'", db->passwd);
		INFO (OUTPUT_PREFIX_STRING "Mysql : port = '%d'", db->port);
		INFO (OUTPUT_PREFIX_STRING "Mysql : database = '%s'", db->database);
	}
	return (status);
}
*/

static int
basic_aggregator_read_config_file_and_update_aggregator_definitions(char *filename) {
		oconfig_item_t *ci;
		int status = 0;
		int i;
		int nb_aggregators = 0;
		char *k;
		aggregator_definition_t *agg;

		ci = oconfig_parse_file (filename);
		if(NULL == ci) {
				WARNING (OUTPUT_PREFIX_STRING "Failed to read default config ('%s').", filename);
				return(-1);
		}

		/* Free the aggregator tree : it will be reconfigured from scratch from the file */
		if(NULL != aggregator) {
				while (c_avl_pick (aggregator, (void *)&k, (void *)&agg) == 0) {
						aggregator_definition_free(agg,1);
				}
				c_avl_destroy (aggregator);
		}
		aggregator = c_avl_create((void *) strcmp);
		if(NULL == aggregator) return (-1);

		/* Parse the configuration file */
		for (i = 0; i < ci->children_num; i++)
		{
				oconfig_item_t *child = ci->children + i;

				if (strcasecmp ("aggregator", child->key) == 0) {
						agg = basic_aggregator_config_aggregator (child);
						if(agg) {
								int r;
								r = c_avl_insert(aggregator, agg->resultvalue, agg);
								if(r != 0) {
										ERROR (OUTPUT_PREFIX_STRING "Could not insert aggregator '%s' in the list of aggregators (duplicate ?)",  agg->resultvalue);
										aggregator_definition_free(agg,1);
								} else {
										nb_aggregators++;
								}
						}
				} else if (strcasecmp ("database", child->key) == 0) {
						if ((child->values_num < 1) || (child->values[0].type != OCONFIG_TYPE_STRING)) {
								WARNING (OUTPUT_PREFIX_STRING "`database' needs 1 string arguments.");
								status = -1;
						} else {
								if (strcasecmp ("mysql", child->values[0].value.string) == 0) {
										/* Not implemented yet
										   basic_aggregator_config_mysql_database (child);
										   */
								} else if (strcasecmp ("postgresql", child->values[0].value.string) == 0) {

										/* Not supported yet */
								} else {
										WARNING (OUTPUT_PREFIX_STRING "'%s' is not a known type for `database'.", ci->values[0].value.string);
										status = -1;
								}
						}
				} else
						WARNING (OUTPUT_PREFIX_STRING "Option \"%s\" not allowed here.",
										child->key);
		}

		INFO(OUTPUT_PREFIX_STRING "Registered %d aggregators", nb_aggregators);
		return(status);
}

static int
basic_aggregator_config (const char *key, const char *value)
{
		int status=0;
		if (strcasecmp ("Aggregators_config_file", key) == 0)
		{
				if(configuration_filename) sfree (configuration_filename);
				configuration_filename = sstrdup (value);
		}
		else
		{
				ERROR (OUTPUT_PREFIX_STRING "Unknown config option: %s", key);
				return (-1);
		}

		if(NULL == configuration_filename) {
				ERROR (OUTPUT_PREFIX_STRING "No configuration filename 'Aggregators_config_file' was set in the collectd config file");
				return(-1);
		}

		return (status);
}

static int
basic_aggregator_read_all_aggregators (void) {
		c_avl_iterator_t *iter;
		char *k;
		aggregator_definition_t *agg;
		int status;
		struct stat buf;
		short update_config = 1;

		/* Check if we should reread the configuration file */
		errno=0;
		if(-1 == stat(configuration_filename, &buf)) {
				ERROR (OUTPUT_PREFIX_STRING "Cannot stat configuration file '%s' (errno=%d)", configuration_filename,errno);
				return(-1);
		}
		if(buf.st_mtime > configuration_filename_date) {
				configuration_filename_date = buf.st_mtime;
		} else {
				update_config = 0;
		}

		/* Reread the configuration file if needed */
		if(1 == update_config) {
				status = basic_aggregator_read_config_file_and_update_aggregator_definitions(configuration_filename);
				if(status != 0) return -1;
		}

		/* Aggregate for all the aggregators */
		iter = c_avl_get_iterator (aggregator);
		while (c_avl_iterator_next (iter, (void *) &k, (void *) &agg) == 0)
		{
				basic_aggregator_read(agg);
		} /* while (c_avl_iterator_next) */
		c_avl_iterator_destroy (iter);

		return(0);
}


void module_register (void)
{
	plugin_register_config ("basic_aggregator", basic_aggregator_config,
            config_keys, config_keys_num);
	plugin_register_read ("instances_of_types_update", instances_of_types_tree_update);
	plugin_register_read ("basic_aggregator_read_all_aggregators", basic_aggregator_read_all_aggregators);
}
