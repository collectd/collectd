/**
 * collectd - src/mongo.c
 * Copyright (C) 2010 Ryan Cox 
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
 *   Ryan Cox <ryan.a.cox@gmail.com> 
 **/

#include "collectd.h"
#include "common.h" 
#include "plugin.h" 

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

#if HAVE_STDINT_H
# define MONGO_HAVE_STDINT 1
#else
# define MONGO_USE_LONG_LONG_INT 1
#endif
#include <mongo.h>

#define MC_PLUGIN_NAME "mongo"
#define MC_MONGO_DEF_HOST "127.0.0.1"
#define MC_MONGO_DEF_PORT 27017 
#define MC_MONGO_DEF_DB "admin"
#define MC_MIN_PORT 1
#define MC_MAX_PORT 65535 
#define SUCCESS 0 
#define FAILURE -1 

static char *mc_user = NULL;
static char *mc_password = NULL;
static char *mc_db = NULL; 
static char *mc_host = NULL;
static int  mc_port = MC_MONGO_DEF_PORT;

static mongo_connection mc_connection;
static _Bool mc_have_connection = 0;

static const char *config_keys[] = {
	"User",
	"Password",
	"Database",
	"Host",
	"Port"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static void submit (const char *type, const char *instance,
        value_t *values, size_t values_len)
{
    value_list_t v = VALUE_LIST_INIT;

    v.values = values;
    v.values_len = values_len;

    sstrncpy (v.host, hostname_g, sizeof(v.host));
    sstrncpy (v.plugin, MC_PLUGIN_NAME, sizeof(v.plugin));
    ssnprintf (v.plugin_instance, sizeof (v.plugin_instance), "%i", mc_port);
    sstrncpy (v.type, type, sizeof(v.type));

    if (instance != NULL)
        sstrncpy (v.type_instance, instance, sizeof (v.type_instance)); 

    plugin_dispatch_values (&v);
}

static void submit_gauge (const char *type, const char *instance, /* {{{ */
        gauge_t gauge)
{
	value_t v;

	v.gauge = gauge;
	submit(type, instance, &v, /* values_len = */ 1);
} /* }}} void submit_gauge */

static void submit_derive (const char *type, const char *instance, /* {{{ */
        derive_t derive)
{
	value_t v;

	v.derive = derive;
	submit(type, instance, &v, /* values_len = */ 1);
} /* }}} void submit_derive */

static int handle_opcounters (bson *obj) /* {{{ */
{
    bson_type type;
    bson subobj;
    bson_iterator i;
    bson_iterator j;

    type = bson_find (&i, obj, "opcounters");
    if ((type != bson_object) && (type != bson_array))
        return (EINVAL);

    bson_iterator_subobject (&i, &subobj);

    bson_iterator_init (&j, subobj.data);
    while (bson_iterator_next (&j))
    {
        const char *key;
        derive_t value;

        type = bson_iterator_type (&j);
        if ((type != bson_long) && (type != bson_int))
            continue;

        key = bson_iterator_key (&j);
        if (key == NULL)
            continue;

        value = (derive_t) bson_iterator_long (&j);

        submit_derive ("total_operations", key, value);
    }
    bson_destroy (&subobj);

    return (0);
} /* }}} int handle_opcounters */

static void handle_mem(bson* obj) {
    INFO("handle mem");
    bson_iterator it;
    if(bson_find(&it, obj, "mem")) {
        bson subobj;
        bson_iterator_subobject(&it, &subobj);
        bson_iterator it2;
        bson_iterator_init(&it2, subobj.data);
        int64_t resident = 0;
        int64_t virtual = 0;
        int64_t mapped = 0;
        while(bson_iterator_next(&it2)) {
            if(strcmp(bson_iterator_key(&it2),"resident") == 0) {
                resident = bson_iterator_long(&it2);
            }
            if(strcmp(bson_iterator_key(&it2),"virtual") == 0) {
                virtual = bson_iterator_long(&it2);
            }
            if(strcmp(bson_iterator_key(&it2),"mapped") == 0) {
                mapped = bson_iterator_long(&it2);
            }
        }
        value_t values[3];
        values[0].gauge = resident;
        values[1].gauge = virtual;
        values[2].gauge = mapped;
        submit_gauge("memory", "resident", resident );
        submit_gauge("memory", "virtual", virtual );
        submit_gauge("memory", "mapped", mapped );
        bson_destroy(&subobj);
    }
}

static void handle_connections(bson* obj) {
    INFO("handle connections");
    bson_iterator it;
    if(bson_find(&it, obj, "connections")) {
    bson subobj;
        bson_iterator_subobject(&it, &subobj);
        bson_iterator it2;
        bson_iterator_init(&it2, subobj.data);
        while(bson_iterator_next(&it2)) {
            if(strcmp(bson_iterator_key(&it2),"current") == 0) {
                submit_gauge("connections", "connections", bson_iterator_int(&it2));
                break;
            }
        }
        bson_destroy(&subobj);
    }
}

static void handle_lock(bson* obj) {
    INFO("handle lock");
    bson_iterator it;
    if(bson_find(&it, obj, "globalLock")) {
        bson subobj;
        bson_iterator_subobject(&it, &subobj);
        bson_iterator it2;
        bson_iterator_init(&it2, subobj.data);
        while(bson_iterator_next(&it2)) {
            if(strcmp(bson_iterator_key(&it2),"ratio") == 0) {
                submit_gauge("percent", "lock_ratio", bson_iterator_double(&it2));
            }
        }
        bson_destroy(&subobj);
    }
}

static void handle_index_counters(bson* obj) {
    INFO("handle index counters");
    bson_iterator icit;
    if(bson_find(&icit, obj, "indexCounters")) {
        bson oic;
        bson_iterator_subobject(&icit, &oic);
        if(bson_find(&icit, &oic, "btree")) {
            bson obt;
            bson_iterator_subobject(&icit, &obt);
            bson_iterator bit;
            bson_iterator_init(&bit, oic.data);
            int accesses; 
            int misses;
            double ratio;

            while(bson_iterator_next(&bit)) {
                if(strcmp(bson_iterator_key(&bit),"accesses") == 0) {
                    accesses = bson_iterator_int(&bit); 
                }
                if(strcmp(bson_iterator_key(&bit),"misses") == 0) {
                    misses = bson_iterator_int(&bit); 
                }
            }

            ratio = NAN;
            if (misses <= accesses)
                ratio = ((double) misses) / ((double) accesses);
            submit_gauge("cache_ratio", "cache_misses", ratio );

            bson_destroy(&obt);
        }
        bson_destroy(&oic);
    }
}

static void handle_stats_counts(bson* obj) {

    INFO("handle stats counts");
    bson_iterator it;
    bson_iterator_init(&it, obj->data);
    int64_t collections = 0;
    int64_t objects = 0;
    int64_t numExtents = 0;
    int64_t indexes = 0;

    while(bson_iterator_next(&it)) {
        if(strcmp(bson_iterator_key(&it),"collections") == 0) {
            collections = bson_iterator_long(&it); 
        }
        if(strcmp(bson_iterator_key(&it),"objects") == 0) {
            objects = bson_iterator_long(&it); 
        }
        if(strcmp(bson_iterator_key(&it),"numExtents") == 0) {
            numExtents = bson_iterator_long(&it); 
        }
        if(strcmp(bson_iterator_key(&it),"indexes") == 0) {
            indexes = bson_iterator_long(&it); 
        }
    }

    submit_gauge("counter","object_count",objects);

    submit_gauge("counter", "collections",collections); 
    submit_gauge("counter", "num_extents",numExtents); 
    submit_gauge("counter", "indexes",indexes); 
}

static void handle_stats_sizes(bson* obj) {
    bson_iterator it;
    bson_iterator_init(&it, obj->data);
    int64_t storageSize = 0;
    int64_t dataSize = 0;
    int64_t indexSize = 0;
    while(bson_iterator_next(&it)) {
        if(strcmp(bson_iterator_key(&it),"storageSize") == 0) {
            storageSize = bson_iterator_long(&it); 
        }
        if(strcmp(bson_iterator_key(&it),"dataSize") == 0) {
            dataSize = bson_iterator_long(&it); 
        }
        if(strcmp(bson_iterator_key(&it),"indexSize") == 0) {
            indexSize = bson_iterator_long(&it); 
        }
    }

    submit_gauge ("file_size", "storage", storageSize);
    submit_gauge ("file_size", "index", indexSize);
    submit_gauge ("file_size", "data", dataSize);
}

static int do_stats(void) {
    bson obj;

    /* TODO: 

        change this to raw runCommand
             db.runCommand( { dbstats : 1 } ); 
             succeeds but is getting back all zeros !?!
        modify bson_print to print type 18
        repro problem w/o db name - show dbs doesn't work again
        why does db.admin.dbstats() work fine in shell?
        implement retries ? noticed that if db is unavailable, collectd dies
    */

    if( !mongo_simple_int_command(&mc_connection, mc_db, "dbstats", 1, &obj) ) {
        ERROR("Mongo: failed to call stats Host [%s] Port [%d] User [%s]", 
            mc_host, mc_port, mc_user);
        return FAILURE;
    }
    
    bson_print(&obj);
    
    handle_stats_sizes(&obj);
    handle_stats_counts(&obj);


    bson_destroy(&obj);

    return SUCCESS;
}


static int do_server_status(void) {
    bson obj;

    if( !mongo_simple_int_command(&mc_connection, mc_db, "serverStatus", 1, &obj) ) {
        ERROR("Mongo: failed to call serverStatus Host [%s] Port [%d] User [%s]", 
            mc_host, mc_port, mc_user);
        return FAILURE;
    }

    bson_print(&obj);

    handle_opcounters(&obj);
    handle_mem(&obj);
    handle_connections(&obj);
    handle_index_counters(&obj);
    handle_lock(&obj);

    bson_destroy(&obj);
    return SUCCESS;
}

static int mc_read(void) {
    DEBUG("Mongo: mongo driver read"); 

    if(do_server_status() != SUCCESS) {
        ERROR("Mongo: do server status failed"); 
        return FAILURE;
    }

    if(do_stats() != SUCCESS) {
        ERROR("Mongo: do stats status failed"); 
        return FAILURE;
    }

    return SUCCESS;
}


static void config_set(char** dest, const char* src ) {
        if( *dest ) {
            sfree(*dest);
        }
		*dest= malloc(strlen(src)+1);
        sstrncpy(*dest,src,strlen(src)+1);
}

static int mc_config(const char *key, const char *value)
{ 
    DEBUG("Mongo: config key [%s] value [%s]", key, value); 

    if(strcasecmp("Host", key) == 0) {
        config_set(&mc_host,value);
    }
    else if(strcasecmp("Port", key) == 0)
    {
        int tmp;
        
        tmp = service_name_to_port_number (value);
        if (tmp > 0)
            mc_port = tmp;
        else
        {
            ERROR("mongodb plugin: failed to parse Port value: %s", value);
            return (-1);
        }
    }
    else if(strcasecmp("User", key) == 0) {
        config_set(&mc_user,value);
    }
    else if(strcasecmp("Password", key) == 0) {
        config_set(&mc_password,value);
    }
    else if(strcasecmp("Database", key) == 0) {
        config_set(&mc_db,value);
    }
    else
    {
        ERROR ("mongodb plugin: Unknown config option: %s", key);
        return (-1);
    }

    return SUCCESS;
} 

static int mc_init(void)
{
    if (mc_have_connection)
        return (0);

    DEBUG("mongo driver initializing"); 
    if( !mc_host) {
        DEBUG("Mongo: Host not specified. Using default [%s]",MC_MONGO_DEF_HOST); 
        config_set(&mc_host, MC_MONGO_DEF_HOST);
    }

    if( !mc_db) {
        DEBUG("Mongo: Database not specified. Using default [%s]",MC_MONGO_DEF_DB); 
        config_set(&mc_db, MC_MONGO_DEF_DB);
    }

    mongo_connection_options opts;
    sstrncpy(opts.host, mc_host, sizeof(opts.host));
    opts.port = mc_port; 

    if(mongo_connect(&mc_connection, &opts )){
        ERROR("Mongo: driver failed to connect. Host [%s] Port [%d] User [%s]", 
                mc_host, mc_port, mc_user);
        return FAILURE;     
    }

    mc_have_connection = 1;
    return SUCCESS;
} 

static int mc_shutdown(void)
{
    DEBUG("Mongo: driver shutting down"); 

    if (mc_have_connection) {
        mongo_disconnect (&mc_connection);
        mongo_destroy (&mc_connection);
        mc_have_connection = 0;
    }

    sfree(mc_user);
    sfree(mc_password);
    sfree(mc_db);
    sfree(mc_host);

    return (0);
}


void module_register(void)  {       
    plugin_register_config (MC_PLUGIN_NAME, mc_config,
            config_keys, config_keys_num);
    plugin_register_read (MC_PLUGIN_NAME, mc_read);
    plugin_register_init (MC_PLUGIN_NAME, mc_init);
    plugin_register_shutdown (MC_PLUGIN_NAME, mc_shutdown);
}

/* vim: set sw=4 sts=4 et fdm=marker : */
