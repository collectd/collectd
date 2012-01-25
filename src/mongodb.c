/**
 * collectd - src/mongo.c
 * Copyright (C) 2010 Ryan Cox
 * Copyright (C) 2012 Florian Forster
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
 *   Ryan Cox <ryan.a.cox at gmail.com>
 *   Florian Forster <octo at collectd.org>
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

#define MC_MONGO_DEF_HOST "127.0.0.1"
#define MC_MONGO_DEF_DB "admin"

static char *mc_user     = NULL;
static char *mc_password = NULL;
static char *mc_db       = NULL;
static char *mc_host     = NULL;
static int   mc_port     = 0;

static mongo mc_connection;
static _Bool mc_have_connection = 0;

static const char *config_keys[] = {
    "User",
    "Password",
    "Database",
    "Host",
    "Port"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static void submit (const char *type, const char *instance, /* {{{ */
        value_t *values, size_t values_len)
{
    value_list_t v = VALUE_LIST_INIT;

    v.values = values;
    v.values_len = values_len;

    sstrncpy (v.host, hostname_g, sizeof(v.host));
    sstrncpy (v.plugin, "mongodb", sizeof(v.plugin));
    ssnprintf (v.plugin_instance, sizeof (v.plugin_instance), "%i", mc_port);
    sstrncpy (v.type, type, sizeof(v.type));

    if (instance != NULL)
        sstrncpy (v.type_instance, instance, sizeof (v.type_instance));

    plugin_dispatch_values (&v);
} /* }}} void submit */

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
    bson_iterator iter;
    bson_iterator subiter;
    int status = 0;

    type = bson_find (&iter, obj, field);
    if (type != BSON_OBJECT)
        return (EINVAL);

    bson_iterator_subiterator (&iter, &subiter);
    while (bson_iterator_more (&subiter))
    {
        (void) bson_iterator_next (&subiter);
        status = (*func) (&subiter);
        if (status != 0)
            break;
    }

    return (status);
} /* }}} int handle_field */

static int handle_opcounters (bson_iterator *iter) /* {{{ */
{
    bson_type type;
    const char *key;
    derive_t value;

    type = bson_iterator_type (iter);
    if ((type != BSON_INT) && (type != BSON_LONG))
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
    if ((type != BSON_DOUBLE) && (type != BSON_LONG) && (type != BSON_INT))
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
    if ((type != BSON_DOUBLE) && (type != BSON_LONG) && (type != BSON_INT))
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
    if ((type != BSON_DOUBLE) && (type != BSON_LONG) && (type != BSON_INT))
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

static int handle_btree (const bson *obj) /* {{{ */
{
    bson_iterator i;

    bson_iterator_init (&i, obj);
    while (bson_iterator_next (&i))
    {
        bson_type type;
        const char *key;
        gauge_t value;

        type = bson_iterator_type (&i);
        if ((type != BSON_DOUBLE) && (type != BSON_LONG) && (type != BSON_INT))
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
    if (type != BSON_OBJECT)
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

static int query_server_status (void) /* {{{ */
{
    bson result;
    int status;

    status = mongo_simple_int_command (&mc_connection,
            (mc_db != NULL) ? mc_db : MC_MONGO_DEF_DB,
            /* cmd = */ "serverStatus", /* arg = */ 1,
            &result);
    if (status != MONGO_OK)
    {
        ERROR ("mongodb plugin: Calling {\"serverStatus\": 1} failed: %s",
                mc_connection.errstr);
        return (-1);
    }

    handle_field (&result, "opcounters",    handle_opcounters);
    handle_field (&result, "mem",           handle_mem);
    handle_field (&result, "connections",   handle_connections);
    handle_field (&result, "globalLock",    handle_lock);
    handle_field (&result, "indexCounters", handle_index_counters);

    bson_destroy(&result);
    return (0);
} /* }}} int query_server_status */

static int handle_dbstats (const bson *obj) /* {{{ */
{
    bson_iterator i;

    bson_iterator_init (&i, obj);
    while (bson_iterator_next (&i))
    {
        bson_type type;
        const char *key;
        gauge_t value;

        type = bson_iterator_type (&i);
        if ((type != BSON_DOUBLE) && (type != BSON_LONG) && (type != BSON_INT))
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

static int query_dbstats (void) /* {{{ */
{
    bson result;
    int status;

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

    memset (&result, 0, sizeof (result));
    status = mongo_simple_int_command (&mc_connection,
            (mc_db != NULL) ? mc_db : MC_MONGO_DEF_DB,
            /* cmd = */ "dbstats", /* arg = */ 1,
            &result);
    if (status != MONGO_OK)
    {
        ERROR ("mongodb plugin: Calling {\"dbstats\": 1} failed: %s",
                mc_connection.errstr);
        return (-1);
    }

    handle_dbstats (&result);

    bson_destroy (&result);
    return (0);
} /* }}} int query_dbstats */

static int mc_read(void) /* {{{ */
{
    if (query_server_status () != 0)
        return (-1);

    if (query_dbstats () != 0)
        return (-1);

    return (0);
} /* }}} int mc_read */

static void mc_config_set (char **dest, const char *src ) /* {{{ */
{
    sfree(*dest);
    *dest = strdup (src);
} /* }}} void mc_config_set */

static int mc_config (const char *key, const char *value) /* {{{ */
{
    if (strcasecmp("Host", key) == 0)
        mc_config_set(&mc_host,value);
    else if (strcasecmp("Port", key) == 0)
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
    else if(strcasecmp("User", key) == 0)
        mc_config_set(&mc_user,value);
    else if(strcasecmp("Password", key) == 0)
        mc_config_set(&mc_password,value);
    else if(strcasecmp("Database", key) == 0)
        mc_config_set(&mc_db,value);
    else
    {
        ERROR ("mongodb plugin: Unknown config option: %s", key);
        return (-1);
    }

    return (0);
} /* }}} int mc_config */

static int mc_init (void) /* {{{ */
{
    int status;

    if (mc_have_connection)
        return (0);

    mongo_init (&mc_connection);

    status = mongo_connect (&mc_connection,
            (mc_host != NULL) ? mc_host : MC_MONGO_DEF_HOST,
            (mc_port > 0) ? mc_port : MONGO_DEFAULT_PORT);
    if (status != MONGO_OK)
    {
        ERROR ("mongo plugin: Connecting to %s:%i failed: %s",
                (mc_host != NULL) ? mc_host : MC_MONGO_DEF_HOST,
                (mc_port > 0) ? mc_port : MONGO_DEFAULT_PORT,
                mc_connection.errstr);
        return (-1);
    }

    mc_have_connection = 1;
    return (0);
} /* }}} int mc_init */

static int mc_shutdown(void) /* {{{ */
{
    if (mc_have_connection) {
        mongo_disconnect (&mc_connection);
        mongo_destroy (&mc_connection);
        mc_have_connection = 0;
    }

    sfree (mc_user);
    sfree (mc_password);
    sfree (mc_db);
    sfree (mc_host);

    return (0);
} /* }}} int mc_shutdown */

void module_register(void)
{
    plugin_register_config ("mongodb", mc_config,
            config_keys, config_keys_num);
    plugin_register_read ("mongodb", mc_read);
    plugin_register_init ("mongodb", mc_init);
    plugin_register_shutdown ("mongodb", mc_shutdown);
}

/* vim: set sw=4 sts=4 et fdm=marker : */
