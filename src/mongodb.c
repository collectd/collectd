/**
 * collectd - src/mongo.c
 * Copyright (C) 2010 Ryan Cox
 * Copyright (C) 2012 Florian Forster
 * Copyright (C) 2013 John (J5) Palmieri
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
 *   John (J5) Palmieri <j5 at stackdriver.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_llist.h"

#if HAVE_STDINT_H
# define MONGO_HAVE_STDINT 1
#else
# define MONGO_USE_LONG_LONG_INT 1
#endif
#include <mongo.h>
#include <bson.h>
#include <libmongoc/src/env.h>

#define MC_MONGO_DEF_HOST "127.0.0.1"
#define MC_MONGO_DEF_DB "admin"


/* FIXME: use autoconf to determine if bson_iterator_subobject_init or
          the older bson_iterator_subobject should be used.  Right now
          you need to use the unreleased mongo c driver out of git.
*/

# define _bson_subobject_destroy(obj) bson_destroy(obj)

/* TODO: Flesh this all out a bit more for use with non-auto-discover
 *       setups
 */
struct mongo_db_s /* {{{ */
{
    char *name;
    char *user;
    char *password;
    mongo connection;
};

typedef struct mongo_db_s mongo_db_t; /* }}} */

struct mongo_config_s /* {{{ */
{
    char *user;
    char *password;
    char *host;
    char *name;
    int   port;

    char *set_name;
    mongo connection;

    _Bool is_primary;
    _Bool run_dbstats;
    _Bool auto_discover;

    _Bool prefer_secondary_query;
    char *secondary_query_host;
    int   secondary_query_port;

    llist_t *db_llist;
};
typedef struct mongo_config_s mongo_config_t; /* }}} */

mongo_config_t *mc = NULL;

static const char *config_keys[] = {
    "User",
    "Password",
    "Database",
    "Host",
    "Port",
    "AllowSecondaryQuery", /* XXX DEPRECATED */
    "PreferSecondaryQuery"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static void mc_config_set (char **dest, const char *src ) /* {{{ */
{
    sfree(*dest);
    *dest = strdup (src);
} /* }}} void mc_config_set */

/*
 * This should be upstreamed into the mongo-c-driver
 *   connect and authenticate while returning the output of is_master command
 */
static MONGO_EXPORT int _mongo_client_complex (mongo *conn , const char *host, int port, const char *user, const char *pass, bson *is_master_out) { /* {{{ */
    int status;

    mongo_init( conn );

    conn->primary = (mongo_host_port*) bson_malloc (sizeof (mongo_host_port));
    snprintf( conn->primary->host, MAXHOSTNAMELEN, "%s", host);
    conn->primary->port = port;
    conn->primary->next = NULL;

    if( mongo_env_socket_connect (conn, host, port) != MONGO_OK )
        return MONGO_ERROR;

    if (user!=NULL && pass!=NULL) {
        status = mongo_cmd_authenticate (conn, "admin", user, pass);
        if (status != MONGO_OK)
        {
            ERROR ("mongo plugin: Authenticating to %s:%i failed: %s",
                    host,
                    port,
                    conn->errstr);
            return (-1);
        }
    }

    if (is_master_out != NULL)
        mongo_cmd_ismaster (conn, is_master_out);

    return MONGO_OK;
} /* }}} int _mongo_client_complex */

/*
 *  Check connection and connect if needed
 */
static int mc_connect (mongo *conn, const char *host, int port, const char *user, const char *pass, bson *is_master_out) { /* {{{ */
    int status;

    status = mongo_check_connection(conn);
    if (status == MONGO_ERROR) {
        mongo_disconnect(conn);
    } else {
        // we are still connected
        if (is_master_out != NULL)
            mongo_cmd_ismaster (conn, is_master_out);
        return MONGO_OK;
    }

    status = _mongo_client_complex (conn,
            (host != NULL) ? host : MC_MONGO_DEF_HOST,
            (port > 0) ? port : MONGO_DEFAULT_PORT,
            user,
            pass,
            is_master_out);
    if (status != MONGO_OK)
    {
        ERROR ("mongo plugin: Connecting to %s:%i failed: %s",
                (host != NULL) ? host : MC_MONGO_DEF_HOST,
                (port > 0) ? port : MONGO_DEFAULT_PORT,
                conn->errstr);
        return (-1);
    }

    return (0);
} /* }}} int mc_connect */

static void submit (const char *type, const char *instance, /* {{{ */
        value_t *values, size_t values_len,
        mongo_db_t *db)
{
    value_list_t v = VALUE_LIST_INIT;

    v.values = values;
    v.values_len = values_len;

    sstrncpy (v.host, hostname_g, sizeof(v.host));
    sstrncpy (v.plugin, "mongodb", sizeof(v.plugin));
    if (db != NULL) {
        ssnprintf (v.plugin_instance, sizeof (v.plugin_instance), "%s", db->name);
    }
    sstrncpy (v.type, type, sizeof(v.type));

    if (instance != NULL)
        sstrncpy (v.type_instance, instance, sizeof (v.type_instance));

    plugin_dispatch_values (&v);
} /* }}} void submit */

static void submit_gauge (const char *type, const char *instance, /* {{{ */
        gauge_t gauge, mongo_db_t *db)
{
    value_t v;

    v.gauge = gauge;
    submit(type, instance, &v, /* values_len = */ 1, db);
} /* }}} void submit_gauge */

static void submit_derive (const char *type, const char *instance, /* {{{ */
        derive_t derive)
{
    value_t v;

    v.derive = derive;
    submit(type, instance, &v, /* values_len = */ 1, NULL);
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

    submit_gauge ("memory", key, value, NULL);
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

    submit_gauge ("current_connections", NULL, value, NULL);
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

    submit_derive ("total_time_in_ms", "global_lock_held", value);
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
            submit_gauge ("cache_result", "hit", value, NULL);
        else if (strcmp ("misses", key) != 0)
            submit_gauge ("cache_result", "miss", value, NULL);
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

    bson_iterator_subobject_init (iter, &subobj, 0);
    status = handle_btree (&subobj);
    _bson_subobject_destroy(&subobj);

    return (status);
} /* }}} int handle_index_counters */

static int query_server_status (void) /* {{{ */
{
    bson result;
    int status;

    status = mongo_simple_int_command (&(mc->connection),
            "admin",
            /* cmd = */ "serverStatus", /* arg = */ 1,
            &result);
    if (status != MONGO_OK)
    {
        ERROR ("mongodb plugin: Calling {\"serverStatus\": 1} failed: %s",
                mc->connection.errstr);
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

static int handle_dbstats (mongo_db_t *db, const bson *obj) /* {{{ */
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

        /* counts */
        if (strcmp ("collections", key) == 0)
            submit_gauge ("gauge", "collections", value, db);
        else if (strcmp ("objects", key) == 0)
            submit_gauge ("gauge", "objects", value, db);
        else if (strcmp ("numExtents", key) == 0)
            submit_gauge ("gauge", "num_extents", value, db);
        else if (strcmp ("indexes", key) == 0)
            submit_gauge ("gauge", "indexes", value, db);
        /* sizes */
        else if (strcmp ("dataSize", key) == 0)
            submit_gauge ("bytes", "data", value, db);
        else if (strcmp ("storageSize", key) == 0)
            submit_gauge ("bytes", "storage", value, db);
        else if (strcmp ("indexSize", key) == 0)
            submit_gauge ("bytes", "index", value, db);
    }

    return (0);
} /* }}} int handle_dbstats */

static int mc_db_stats_read_cb (user_data_t *ud) /* {{{ */
{
    bson result;
    bson b;
    int status;
    mongo_db_t *db = (mongo_db_t *) ud->data;
    const char *host = mc->host;
    int port = mc->port;

    if (mc->prefer_secondary_query && mc->secondary_query_host != NULL) {
        host = mc->secondary_query_host;
        port = mc->secondary_query_port;
    }

    if (mc_connect(&(db->connection),
                   host,
                   port,
                   (db->user != NULL) ? db->user : mc->user,
                   (db->password != NULL) ? db->password : mc->password,
                   NULL) != 0) {
        return (-1);
    }

    bson_init (&result);
    bson_init (&b);
    bson_append_int (&b, "dbStats", 1);
    bson_append_int (&b, "scale", 1);
    bson_finish (&b);
    status = mongo_run_command (&(db->connection), db->name, &b, &result);
    bson_destroy (&b);
    if (status != MONGO_OK)
    {
        ERROR ("mongodb plugin: Calling {\"dbstats\": 1} failed: %s",
                mc->connection.errstr);
        return (-1);
    }

    handle_dbstats (db, &result);

    bson_destroy (&result);
    return (0);
} /* }}} int query_dbstats */

static void mc_unregister_and_free_ghost_dbs(llist_t *ghost_dbs, _Bool free_list) {
    llentry_t *e_this;
    llentry_t *e_next;

    if (ghost_dbs == NULL)
        return;

    for (e_this = llist_head(ghost_dbs); e_this != NULL; e_this = e_next)
    {
        e_next = e_this->next;
        plugin_unregister_read (e_this->key);
        llentry_destroy (e_this);
    }

    if (free_list)
        free (ghost_dbs);
}

static void free_db_userdata(void *data) {
    mongo_db_t *db = (mongo_db_t *) data;
    sfree (db->name);
    sfree (db->user);
    sfree (db->password);
    mongo_destroy (&(db->connection));
    free (db);
}

static int setup_dbs(void) /* {{{ */
{
    bson out;
    bson_iterator it, it_dbs;
    llist_t *active_db_llist = llist_create ();
    llist_t *old_db_llist = mc->db_llist;
    mongo_db_t *db;

    if (mongo_simple_int_command( &(mc->connection), "admin", "listDatabases", 1, &out ) != MONGO_OK)
        return MONGO_ERROR;

    bson_find (&it, &out, "databases");
    bson_iterator_subiterator (&it, &it_dbs);
    while (bson_iterator_next (&it_dbs)) {
        llentry_t *entry;
        bson sub;
        const char *db_name;
        char cb_name[DATA_MAX_NAME_LEN];
        bson_iterator sub_it;

        bson_iterator_subobject_init (&it_dbs, &sub, 0);
        if (bson_find(&sub_it, &sub, "name")) {
            db_name = bson_iterator_string (&sub_it);
        } else {
            // shouldn't happen
            goto cont;
        }

        ssnprintf (cb_name, sizeof (cb_name), "mongo-%s", db_name);
        entry = llist_search (old_db_llist, cb_name);

        if (entry == NULL) {
            user_data_t ud;
            char *new_cb_name = strdup (cb_name);
            if (new_cb_name == NULL)
                 goto oom;

            entry = llentry_create (new_cb_name, NULL);
            if (entry == NULL)
                goto oom;

            DEBUG ("mongodb plugin: Registering new read callback: %s",
                   cb_name);

            db = malloc (sizeof (mongo_db_t));
            if (db == NULL)
                goto oom;

            memset (db, 0, sizeof (mongo_db_t));
            db->name = strdup (db_name);
            if (db->name == NULL)
                goto oom;

            memset (&ud, 0, sizeof (ud));
            ud.data = (void *) db;
            ud.free_func = free_db_userdata;

            if (plugin_register_complex_read (NULL, cb_name, mc_db_stats_read_cb, NULL, &ud) != 0) {
                free_db_userdata (db);
                goto cont;
            }
        } else {
            llist_remove (old_db_llist, entry);
        }

        llist_append (active_db_llist, entry);
cont:
        _bson_subobject_destroy (&sub);
    }
    bson_destroy (&out);

    mc->db_llist = active_db_llist;

    /* clean up any dbs that were removed */
    mc_unregister_and_free_ghost_dbs (old_db_llist, 1);
    return (0);

oom:
    bson_destroy (&out);
    ERROR ("mongodb plugin: OOM configuring dbs");
    return MONGO_ERROR;
}
/* }}} setup_dbs */

static void mc_split_host_port (const char *full_host, char **host_in, int *port_in) /* {{{ */
{
    char *delimit_c;
    delimit_c = strrchr(full_host, ':');
    if (delimit_c == NULL) {
        /* this is ulikely but use a default port if no port is given */
        *port_in = 27017;
        *host_in = strdup (full_host);
    } else {
        /* grab the port from the right side of the delimiter */
        sscanf (delimit_c, ":%d", port_in);

        /* dup the string up to the delimit pointer */
        *host_in = strndup (full_host, (size_t)(delimit_c - full_host));
    }
} /* }}} void mc_split_host_port */

static void mc_config_secondary (bson *is_master) /* {{{ */
{
    bson_iterator it;
    bson_iterator hosts_it;
    const char *primary;
    mc->secondary_query_host = NULL;


    if (bson_find (&it, is_master, "primary")) {
        primary = bson_iterator_string (&it);
    } else {
        WARNING("mongodb plugin: failed to find the primary db from call to isMaster");
        return;
    }

    /* grab the first address that is not the primary */
    if (bson_find (&it, is_master, "hosts")) {
       bson_iterator_subiterator (&it, &hosts_it);
       while (bson_iterator_next (&hosts_it)) {
           const char *host;
           host = bson_iterator_string (&hosts_it);
           if (strcmp (host, primary) != 0) {
               mc_split_host_port (host, &mc->secondary_query_host, &mc->secondary_query_port);
               break;
           }
       }
    } else {
        WARNING("mongodb plugin: failed to find list of hosts from call to isMaster");
    }
} /* }}} void mc_config_secondary */

static int mc_setup_read(void) /* {{{ */
{
    bson is_master_out;
    bson_iterator it;
    int max_bson_size = MONGO_DEFAULT_MAX_BSON_SIZE;

    if (mc_connect(&(mc->connection), mc->host, mc->port, mc->user, mc->password, &is_master_out) != 0) {
        return (-1);
    }

    if( bson_find( &it, &is_master_out, "ismaster" ) )
        mc->is_primary = bson_iterator_bool( &it );
    if( bson_find( &it, &is_master_out, "maxBsonObjectSize" ) )
        max_bson_size = bson_iterator_int( &it );
    if( bson_find( &it, &is_master_out, "setName" ) )
        mc_config_set(&(mc->set_name), bson_iterator_string( &it ));
    mc->connection.max_bson_size = max_bson_size;

    if (mc->prefer_secondary_query) {
        mc_config_secondary (&is_master_out);
    }

    bson_destroy (&is_master_out);

    /* NOTE Disabling this to allow collection of stats from secondary nodes. */
    /* Only the primary node sends back stats for now
     * though it may query a secondary node for queries that are
     * intensive
     */
    //if (!mc->is_primary) {
    //    /* unregister any db reads */
    //    mc_unregister_and_free_ghost_dbs (mc->db_llist, 0);
    //    return (0);
    //}

    if (setup_dbs () != 0)
        return (-1);

    if (query_server_status () != 0)
        return (-1);

    return (0);
} /* }}} int mc_setup_read */

static int mc_init_config_struct (void) {
    if (mc == NULL) {
        mc = (mongo_config_t *) malloc (sizeof(mongo_config_t));
        if (mc == NULL) {
            ERROR ("mongodb plugin: malloc failed for configuration data.");
            return (-1);
        }
        memset (mc, 0, sizeof (mongo_config_t));
        /* autodiscover on by default */
        mc->auto_discover = 1;
        mc->db_llist = llist_create();
        if (mc->db_llist == NULL) {
            ERROR ("OOM trying to allocate the db list");
            return (-1);
        }

        mongo_init(&(mc->connection));
    }
    return 0;
}

static int mc_config (const char *key, const char *value) /* {{{ */
{

    if (mc_init_config_struct() != 0)
        return -1;

    if (strcasecmp("Host", key) == 0)
        mc_config_set(&(mc->host),value);
    else if (strcasecmp("Port", key) == 0)
    {
        int tmp;

        tmp = service_name_to_port_number (value);
        if (tmp > 0)
            mc->port = tmp;
        else
        {
            ERROR("mongodb plugin: failed to parse Port value: %s", value);
            return (-1);
        }
    }
    else if (strcasecmp("User", key) == 0)
        mc_config_set(&mc->user, value);
    else if (strcasecmp("Password", key) == 0)
        mc_config_set(&mc->password, value);
    else if ((strcasecmp("PreferSecondaryQuery", key) == 0)
            || (strcasecmp("AllowSecondaryQuery", key) == 0)) /* XXX DEPRECATED */
    {
        if (strcasecmp("AllowSecondaryQuery", key) == 0)
            WARNING("mongodb plugin: config option 'AllowSecondaryQuery' is deprecated. use 'PreferSecondaryQuery' instead.");

        if (strcasecmp("true", value) == 0 || strcasecmp("1", value) == 0)
            mc->prefer_secondary_query = 1;
        else
            mc->prefer_secondary_query = 0;
    }
    else
    {
        ERROR ("mongodb plugin: Unknown config option: %s", key);
        return (-1);
    }

    return (0);
} /* }}} int mc_config */

static int mc_shutdown(void) /* {{{ */
{
    mongo_destroy (&(mc->connection));

    sfree (mc->user);
    sfree (mc->password);
    sfree (mc->host);
    sfree (mc->secondary_query_host);

    llist_destroy(mc->db_llist);


    free (mc);
    return (0);
} /* }}} int mc_shutdown */

void module_register(void)
{
    plugin_register_config ("mongodb", mc_config,
            config_keys, config_keys_num);

    /* the setup read checks if we are a master node and sets up database reads accordingly */
    plugin_register_read ("mongodb", mc_setup_read);
    plugin_register_shutdown ("mongodb", mc_shutdown);
}

/* vim: set sw=4 sts=4 et fdm=marker : */
