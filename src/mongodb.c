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

static char *mc_user     = NULL;
static char *mc_password = NULL;
static char *mc_db       = NULL;
static char *mc_host     = NULL;
static int   mc_port     = MC_MONGO_DEF_PORT;

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

static int handle_field (bson *obj, const char *field, /* {{{ */
        int (*func) (bson_iterator *))
{
    bson_type type;
    bson subobj;
    bson_iterator i;
    bson_iterator j;
    int status = 0;

    type = bson_find (&i, obj, field);
    if (type != bson_object)
        return (EINVAL);

    bson_iterator_subobject (&i, &subobj);
    bson_iterator_init (&j, subobj.data);
    while (bson_iterator_next (&j))
    {
        status = (*func) (&j);
        if (status != 0)
            break;
    }
    bson_destroy (&subobj);

    return (status);
} /* }}} int handle_field */

static int handle_opcounters (bson_iterator *iter) /* {{{ */
{
    bson_type type;
    const char *key;
    derive_t value;

    type = bson_iterator_type (iter);
    if ((type != bson_long) && (type != bson_int))
        return (0);

    key = bson_iterator_key (iter);
    if (key == NULL)
        return (0);

    value = (derive_t) bson_iterator_long (iter);

    submit_derive ("total_operations", key, value);
    return (0);
} /* }}} int handle_opcounters */

static int handle_mem (bson_iterator *iter) /* {{{ */
{
    bson_type type;
    const char *key;
    gauge_t value;

    type = bson_iterator_type (iter);
    if ((type != bson_double) && (type != bson_long) && (type != bson_int))
        return (0);

    key = bson_iterator_key (iter);
    if (key == NULL)
        return (0);

    /* Is "virtual" really interesting?
     * What exactly does "mapped" mean? */
    if ((strcasecmp ("mapped", key) != 0)
            && (strcasecmp ("resident", key) != 0)
            && (strcasecmp ("virtual", key) != 0))
        return (0);

    value = (gauge_t) bson_iterator_double (iter);
    /* All values are in MByte */
    value *= 1048576.0;

    submit_gauge ("memory", key, value);
    return (0);
} /* }}} int handle_mem */

static int handle_connections (bson_iterator *iter) /* {{{ */
{
    bson_type type;
    const char *key;
    gauge_t value;

    type = bson_iterator_type (iter);
    if ((type != bson_double) && (type != bson_long) && (type != bson_int))
        return (0);

    key = bson_iterator_key (iter);
    if (key == NULL)
        return (0);

    if (strcmp ("current", key) != 0)
        return (0);

    value = (gauge_t) bson_iterator_double (iter);

    submit_gauge ("current_connections", NULL, value);
    return (0);
} /* }}} int handle_connections */

static int handle_lock (bson_iterator *iter) /* {{{ */
{
    bson_type type;
    const char *key;
    derive_t value;

    type = bson_iterator_type (iter);
    if ((type != bson_double) && (type != bson_long) && (type != bson_int))
        return (0);

    key = bson_iterator_key (iter);
    if (key == NULL)
        return (0);

    if (strcmp ("lockTime", key) != 0)
        return (0);

    value = (derive_t) bson_iterator_long (iter);
    /* The time is measured in microseconds (us). We convert it to
     * milliseconds (ms). */
    value = value / 1000;

    submit_derive ("total_time_in_ms", "lock_held", value);
    return (0);
} /* }}} int handle_lock */

static int handle_btree (bson *obj) /* {{{ */
{
    bson_iterator i;

    bson_iterator_init (&i, obj->data);
    while (bson_iterator_next (&i))
    {
        bson_type type;
        const char *key;
        gauge_t value;

        type = bson_iterator_type (&i);
        if ((type != bson_double) && (type != bson_long) && (type != bson_int))
            continue;

        key = bson_iterator_key (&i);
        if (key == NULL)
            continue;

        value = (gauge_t) bson_iterator_double (&i);

        if (strcmp ("hits", key) == 0)
            submit_gauge ("cache_result", "hit", value);
        else if (strcmp ("misses", key) != 0)
            submit_gauge ("cache_result", "miss", value);
    }

    return (0);
} /* }}} int handle_btree */

static int handle_index_counters (bson_iterator *iter) /* {{{ */
{
    bson_type type;
    const char *key;
    bson subobj;
    int status;

    type = bson_iterator_type (iter);
    if (type != bson_object)
        return (0);

    key = bson_iterator_key (iter);
    if (key == NULL)
        return (0);

    if (strcmp ("btree", key) != 0)
        return (0);

    bson_iterator_subobject (iter, &subobj);
    status = handle_btree (&subobj);
    bson_destroy (&subobj);

    return (status);
} /* }}} int handle_index_counters */

static int handle_dbstats (bson *obj)
{
    bson_iterator i;

    bson_iterator_init (&i, obj->data);
    while (bson_iterator_next (&i))
    {
        bson_type type;
        const char *key;
        gauge_t value;

        type = bson_iterator_type (&i);
        if ((type != bson_double) && (type != bson_long) && (type != bson_int))
            return (0);

        key = bson_iterator_key (&i);
        if (key == NULL)
            return (0);

        value = (gauge_t) bson_iterator_double (&i);

        /* counts */
        if (strcmp ("collections", key) == 0)
            submit_gauge ("gauge", "collections", value);
        else if (strcmp ("objects", key) == 0)
            submit_gauge ("gauge", "objects", value);
        else if (strcmp ("numExtents", key) == 0)
            submit_gauge ("gauge", "num_extents", value);
        else if (strcmp ("indexes", key) == 0)
            submit_gauge ("gauge", "indexes", value);
        /* sizes */
        else if (strcmp ("dataSize", key) == 0)
            submit_gauge ("bytes", "data", value);
        else if (strcmp ("storageSize", key) == 0)
            submit_gauge ("bytes", "storage", value);
        else if (strcmp ("indexSize", key) == 0)
            submit_gauge ("bytes", "index", value);
    }

    return (0);
} /* }}} int handle_dbstats */

static int do_stats (void) /* {{{ */
{
    bson obj;

    /* TODO:
     *
     *  change this to raw runCommand
     *       db.runCommand( { dbstats : 1 } );
     *       succeeds but is getting back all zeros !?!
     *  modify bson_print to print type 18
     *  repro problem w/o db name - show dbs doesn't work again
     *  why does db.admin.dbstats() work fine in shell?
     *  implement retries ? noticed that if db is unavailable, collectd dies
     */

    if( !mongo_simple_int_command(&mc_connection, mc_db, "dbstats", 1, &obj) ) {
        ERROR("Mongo: failed to call stats Host [%s] Port [%d] User [%s]",
            mc_host, mc_port, mc_user);
        return FAILURE;
    }

    handle_dbstats (&obj);

    bson_destroy (&obj);

    return (0);
} /* }}} int do_stats */

static int do_server_status (void) /* {{{ */
{
    bson obj;
    bson_bool_t status;

    status = mongo_simple_int_command (&mc_connection, /* db = */ mc_db,
            /* command = */ "serverStatus", /* arg = */ 1, /* out = */ &obj);
    if (!status)
    {
        ERROR("mongodb plugin: mongo_simple_int_command (%s:%i, serverStatus) "
                "failed.", mc_host, mc_port);
        return (-1);
    }

    handle_field (&obj, "opcounters",    handle_opcounters);
    handle_field (&obj, "mem",           handle_mem);
    handle_field (&obj, "connections",   handle_connections);
    handle_field (&obj, "globalLock",    handle_lock);
    handle_field (&obj, "indexCounters", handle_index_counters);

    bson_destroy(&obj);
    return (0);
} /* }}} int do_server_status */

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

void module_register(void) {
    plugin_register_config (MC_PLUGIN_NAME, mc_config,
            config_keys, config_keys_num);
    plugin_register_read (MC_PLUGIN_NAME, mc_read);
    plugin_register_init (MC_PLUGIN_NAME, mc_init);
    plugin_register_shutdown (MC_PLUGIN_NAME, mc_shutdown);
}

/* vim: set sw=4 sts=4 et fdm=marker : */
