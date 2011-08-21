/**
 * collectd - src/mysql.c
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Mirko Buffoni
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2009       Sebastian tokkee Harl
 * Copyright (C) 2009       Rodolphe Quiédeville
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
 *   Florian octo Forster <octo at collectd.org>
 *   Mirko Buffoni <briareos at eswat.org>
 *   Doug MacEachern <dougm at hyperic.com>
 *   Sebastian tokkee Harl <sh at tokkee.org>
 *   Rodolphe Quiédeville <rquiedeville at bearstech.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#ifdef HAVE_MYSQL_H
#include <mysql.h>
#elif defined(HAVE_MYSQL_MYSQL_H)
#include <mysql/mysql.h>
#endif

/* TODO: Understand `Select_*' and possibly do that stuff as well.. */

struct mysql_database_s /* {{{ */
{
	char *instance;
	char *host;
	char *user;
	char *pass;
	char *database;
	char *socket;
	int   port;

	_Bool master_stats;
	_Bool slave_stats;

	_Bool slave_notif;
	_Bool slave_io_running;
	_Bool slave_sql_running;

	MYSQL *con;
	int    state;
};
typedef struct mysql_database_s mysql_database_t; /* }}} */

static int mysql_read (user_data_t *ud);

static void mysql_database_free (void *arg) /* {{{ */
{
	mysql_database_t *db;

	DEBUG ("mysql plugin: mysql_database_free (arg = %p);", arg);

	db = (mysql_database_t *) arg;

	if (db == NULL)
		return;

	if (db->con != NULL)
		mysql_close (db->con);

	sfree (db->host);
	sfree (db->user);
	sfree (db->pass);
	sfree (db->socket);
	sfree (db->instance);
	sfree (db->database);
	sfree (db);
} /* }}} void mysql_database_free */

/* Configuration handling functions {{{
 *
 * <Plugin mysql>
 *   <Database "plugin_instance1">
 *     Host "localhost"
 *     Port 22000
 *     ...
 *   </Database>
 * </Plugin>
 */
static int mysql_config_database (oconfig_item_t *ci) /* {{{ */
{
	mysql_database_t *db;
	int status = 0;
	int i;

	if ((ci->values_num != 1)
	    || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		WARNING ("mysql plugin: The `Database' block "
			 "needs exactly one string argument.");
		return (-1);
	}

	db = (mysql_database_t *) malloc (sizeof (*db));
	if (db == NULL)
	{
		ERROR ("mysql plugin: malloc failed.");
		return (-1);
	}
	memset (db, 0, sizeof (*db));

	/* initialize all the pointers */
	db->host     = NULL;
	db->user     = NULL;
	db->pass     = NULL;
	db->database = NULL;
	db->socket   = NULL;
	db->con      = NULL;

	/* trigger a notification, if it's not running */
	db->slave_io_running  = 1;
	db->slave_sql_running = 1;

	status = cf_util_get_string (ci, &db->instance);
	if (status != 0)
	{
		sfree (db);
		return (status);
	}
	assert (db->instance != NULL);

	/* Fill the `mysql_database_t' structure.. */
	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Host", child->key) == 0)
			status = cf_util_get_string (child, &db->host);
		else if (strcasecmp ("User", child->key) == 0)
			status = cf_util_get_string (child, &db->user);
		else if (strcasecmp ("Password", child->key) == 0)
			status = cf_util_get_string (child, &db->pass);
		else if (strcasecmp ("Port", child->key) == 0)
		{
			status = cf_util_get_port_number (child);
			if (status > 0)
			{
				db->port = status;
				status = 0;
			}
		}
		else if (strcasecmp ("Socket", child->key) == 0)
			status = cf_util_get_string (child, &db->socket);
		else if (strcasecmp ("Database", child->key) == 0)
			status = cf_util_get_string (child, &db->database);
		else if (strcasecmp ("MasterStats", child->key) == 0)
			status = cf_util_get_boolean (child, &db->master_stats);
		else if (strcasecmp ("SlaveStats", child->key) == 0)
			status = cf_util_get_boolean (child, &db->slave_stats);
		else if (strcasecmp ("SlaveNotifications", child->key) == 0)
			status = cf_util_get_boolean (child, &db->slave_notif);
		else
		{
			WARNING ("mysql plugin: Option `%s' not allowed here.", child->key);
			status = -1;
		}

		if (status != 0)
			break;
	}

	/* If all went well, register this database for reading */
	if (status == 0)
	{
		user_data_t ud;
		char cb_name[DATA_MAX_NAME_LEN];

		DEBUG ("mysql plugin: Registering new read callback: %s",
				(db->database != NULL) ? db->database : "<default>");

		memset (&ud, 0, sizeof (ud));
		ud.data = (void *) db;
		ud.free_func = mysql_database_free;

		if (db->database != NULL)
			ssnprintf (cb_name, sizeof (cb_name), "mysql-%s",
					db->database);
		else
			sstrncpy (cb_name, "mysql", sizeof (cb_name));

		plugin_register_complex_read (/* group = */ NULL, cb_name,
					      mysql_read,
					      /* interval = */ NULL, &ud);
	}
	else
	{
		mysql_database_free (db);
		return (-1);
	}

	return (0);
} /* }}} int mysql_config_database */

static int mysql_config (oconfig_item_t *ci) /* {{{ */
{
	int i;

	if (ci == NULL)
		return (EINVAL);

	/* Fill the `mysql_database_t' structure.. */
	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Database", child->key) == 0)
			mysql_config_database (child);
		else
			WARNING ("mysql plugin: Option \"%s\" not allowed here.",
					child->key);
	}

	return (0);
} /* }}} int mysql_config */

/* }}} End of configuration handling functions */

static MYSQL *getconnection (mysql_database_t *db)
{
	if (db->state != 0)
	{
		int err;
		if ((err = mysql_ping (db->con)) != 0)
		{
			/* Assured by "mysql_config_database" */
			assert (db->instance != NULL);
			WARNING ("mysql_ping failed for instance \"%s\": %s",
					db->instance,
					mysql_error (db->con));
			db->state = 0;
		}
		else
		{
			db->state = 1;
			return (db->con);
		}
	}

	if ((db->con = mysql_init (db->con)) == NULL)
	{
		ERROR ("mysql_init failed: %s", mysql_error (db->con));
		db->state = 0;
		return (NULL);
	}

	if (mysql_real_connect (db->con, db->host, db->user, db->pass,
				db->database, db->port, db->socket, 0) == NULL)
	{
		ERROR ("mysql plugin: Failed to connect to database %s "
				"at server %s: %s",
				(db->database != NULL) ? db->database : "<none>",
				(db->host != NULL) ? db->host : "localhost",
				mysql_error (db->con));
		db->state = 0;
		return (NULL);
	}
	else
	{
		INFO ("mysql plugin: Successfully connected to database %s "
				"at server %s (server version: %s, protocol version: %d)",
				(db->database != NULL) ? db->database : "<none>",
				mysql_get_host_info (db->con),
				mysql_get_server_info (db->con),
				mysql_get_proto_info (db->con));
		db->state = 1;
		return (db->con);
	}
} /* static MYSQL *getconnection (mysql_database_t *db) */

static void set_host (mysql_database_t *db, char *buf, size_t buflen)
{
	if ((db->host == NULL)
			|| (strcmp ("", db->host) == 0)
			|| (strcmp ("localhost", db->host) == 0))
		sstrncpy (buf, hostname_g, buflen);
	else
		sstrncpy (buf, db->host, buflen);
} /* void set_host */

static void submit (const char *type, const char *type_instance,
		value_t *values, size_t values_len, mysql_database_t *db)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values     = values;
	vl.values_len = values_len;

	set_host (db, vl.host, sizeof (vl.host));

	sstrncpy (vl.plugin, "mysql", sizeof (vl.plugin));

	/* Assured by "mysql_config_database" */
	assert (db->instance != NULL);
	sstrncpy (vl.plugin_instance, db->instance, sizeof (vl.plugin_instance));

	sstrncpy (vl.type, type, sizeof (vl.type));
	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* submit */

static void counter_submit (const char *type, const char *type_instance,
		derive_t value, mysql_database_t *db)
{
	value_t values[1];

	values[0].derive = value;
	submit (type, type_instance, values, STATIC_ARRAY_SIZE (values), db);
} /* void counter_submit */

static void gauge_submit (const char *type, const char *type_instance,
		gauge_t value, mysql_database_t *db)
{
	value_t values[1];

	values[0].gauge = value;
	submit (type, type_instance, values, STATIC_ARRAY_SIZE (values), db);
} /* void gauge_submit */

static void derive_submit (const char *type, const char *type_instance,
		derive_t value, mysql_database_t *db)
{
	value_t values[1];

	values[0].derive = value;
	submit (type, type_instance, values, STATIC_ARRAY_SIZE (values), db);
} /* void derive_submit */

static void traffic_submit (derive_t rx, derive_t tx, mysql_database_t *db)
{
	value_t values[2];

	values[0].derive = rx;
	values[1].derive = tx;

	submit ("mysql_octets", NULL, values, STATIC_ARRAY_SIZE (values), db);
} /* void traffic_submit */

static MYSQL_RES *exec_query (MYSQL *con, const char *query)
{
	MYSQL_RES *res;

	int query_len = strlen (query);

	if (mysql_real_query (con, query, query_len))
	{
		ERROR ("mysql plugin: Failed to execute query: %s",
				mysql_error (con));
		INFO ("mysql plugin: SQL query was: %s", query);
		return (NULL);
	}

	res = mysql_store_result (con);
	if (res == NULL)
	{
		ERROR ("mysql plugin: Failed to store query result: %s",
				mysql_error (con));
		INFO ("mysql plugin: SQL query was: %s", query);
		return (NULL);
	}

	return (res);
} /* exec_query */

static int mysql_read_master_stats (mysql_database_t *db, MYSQL *con)
{
	MYSQL_RES *res;
	MYSQL_ROW  row;

	char *query;
	int   field_num;
	unsigned long long position;

	query = "SHOW MASTER STATUS";

	res = exec_query (con, query);
	if (res == NULL)
		return (-1);

	row = mysql_fetch_row (res);
	if (row == NULL)
	{
		ERROR ("mysql plugin: Failed to get master statistics: "
				"`%s' did not return any rows.", query);
		return (-1);
	}

	field_num = mysql_num_fields (res);
	if (field_num < 2)
	{
		ERROR ("mysql plugin: Failed to get master statistics: "
				"`%s' returned less than two columns.", query);
		return (-1);
	}

	position = atoll (row[1]);
	counter_submit ("mysql_log_position", "master-bin", position, db);

	row = mysql_fetch_row (res);
	if (row != NULL)
		WARNING ("mysql plugin: `%s' returned more than one row - "
				"ignoring further results.", query);

	mysql_free_result (res);

	return (0);
} /* mysql_read_master_stats */

static int mysql_read_slave_stats (mysql_database_t *db, MYSQL *con)
{
	MYSQL_RES *res;
	MYSQL_ROW  row;

	char *query;
	int   field_num;

	/* WTF? libmysqlclient does not seem to provide any means to
	 * translate a column name to a column index ... :-/ */
	const int READ_MASTER_LOG_POS_IDX   = 6;
	const int SLAVE_IO_RUNNING_IDX      = 10;
	const int SLAVE_SQL_RUNNING_IDX     = 11;
	const int EXEC_MASTER_LOG_POS_IDX   = 21;
	const int SECONDS_BEHIND_MASTER_IDX = 32;

	query = "SHOW SLAVE STATUS";

	res = exec_query (con, query);
	if (res == NULL)
		return (-1);

	row = mysql_fetch_row (res);
	if (row == NULL)
	{
		ERROR ("mysql plugin: Failed to get slave statistics: "
				"`%s' did not return any rows.", query);
		return (-1);
	}

	field_num = mysql_num_fields (res);
	if (field_num < 33)
	{
		ERROR ("mysql plugin: Failed to get slave statistics: "
				"`%s' returned less than 33 columns.", query);
		return (-1);
	}

	if (db->slave_stats)
	{
		unsigned long long counter;
		double gauge;

		counter = atoll (row[READ_MASTER_LOG_POS_IDX]);
		counter_submit ("mysql_log_position", "slave-read", counter, db);

		counter = atoll (row[EXEC_MASTER_LOG_POS_IDX]);
		counter_submit ("mysql_log_position", "slave-exec", counter, db);

		if (row[SECONDS_BEHIND_MASTER_IDX] != NULL)
		{
			gauge = atof (row[SECONDS_BEHIND_MASTER_IDX]);
			gauge_submit ("time_offset", NULL, gauge, db);
		}
	}

	if (db->slave_notif)
	{
		notification_t n = { 0, cdtime (), "", "",
			"mysql", "", "time_offset", "", NULL };

		char *io, *sql;

		io  = row[SLAVE_IO_RUNNING_IDX];
		sql = row[SLAVE_SQL_RUNNING_IDX];

		set_host (db, n.host, sizeof (n.host));

		/* Assured by "mysql_config_database" */
		assert (db->instance != NULL);
		sstrncpy (n.plugin_instance, db->instance, sizeof (n.plugin_instance));

		if (((io == NULL) || (strcasecmp (io, "yes") != 0))
				&& (db->slave_io_running))
		{
			n.severity = NOTIF_WARNING;
			ssnprintf (n.message, sizeof (n.message),
					"slave I/O thread not started or not connected to master");
			plugin_dispatch_notification (&n);
			db->slave_io_running = 0;
		}
		else if (((io != NULL) && (strcasecmp (io, "yes") == 0))
				&& (! db->slave_io_running))
		{
			n.severity = NOTIF_OKAY;
			ssnprintf (n.message, sizeof (n.message),
					"slave I/O thread started and connected to master");
			plugin_dispatch_notification (&n);
			db->slave_io_running = 1;
		}

		if (((sql == NULL) || (strcasecmp (sql, "yes") != 0))
				&& (db->slave_sql_running))
		{
			n.severity = NOTIF_WARNING;
			ssnprintf (n.message, sizeof (n.message),
					"slave SQL thread not started");
			plugin_dispatch_notification (&n);
			db->slave_sql_running = 0;
		}
		else if (((sql != NULL) && (strcasecmp (sql, "yes") == 0))
				&& (! db->slave_sql_running))
		{
			n.severity = NOTIF_OKAY;
			ssnprintf (n.message, sizeof (n.message),
					"slave SQL thread started");
			plugin_dispatch_notification (&n);
			db->slave_sql_running = 0;
		}
	}

	row = mysql_fetch_row (res);
	if (row != NULL)
		WARNING ("mysql plugin: `%s' returned more than one row - "
				"ignoring further results.", query);

	mysql_free_result (res);

	return (0);
} /* mysql_read_slave_stats */

static int mysql_read (user_data_t *ud)
{
	mysql_database_t *db;
	MYSQL     *con;
	MYSQL_RES *res;
	MYSQL_ROW  row;
	char      *query;

	derive_t qcache_hits          = 0;
	derive_t qcache_inserts       = 0;
	derive_t qcache_not_cached    = 0;
	derive_t qcache_lowmem_prunes = 0;
	gauge_t qcache_queries_in_cache = NAN;

	gauge_t threads_running   = NAN;
	gauge_t threads_connected = NAN;
	gauge_t threads_cached    = NAN;
	derive_t threads_created = 0;

	unsigned long long traffic_incoming = 0ULL;
	unsigned long long traffic_outgoing = 0ULL;

	if ((ud == NULL) || (ud->data == NULL))
	{
		ERROR ("mysql plugin: mysql_database_read: Invalid user data.");
		return (-1);
	}

	db = (mysql_database_t *) ud->data;

	/* An error message will have been printed in this case */
	if ((con = getconnection (db)) == NULL)
		return (-1);

	query = "SHOW STATUS";
	if (mysql_get_server_version (con) >= 50002)
		query = "SHOW GLOBAL STATUS";

	res = exec_query (con, query);
	if (res == NULL)
		return (-1);

	while ((row = mysql_fetch_row (res)))
	{
		char *key;
		unsigned long long val;

		key = row[0];
		val = atoll (row[1]);

		if (strncmp (key, "Com_", 
			          strlen ("Com_")) == 0)
		{
			if (val == 0ULL)
				continue;

			/* Ignore `prepared statements' */
			if (strncmp (key, "Com_stmt_", strlen ("Com_stmt_")) != 0)
				counter_submit ("mysql_commands", 
						key + strlen ("Com_"), 
						val, db);
		}
		else if (strncmp (key, "Handler_", 
				        strlen ("Handler_")) == 0)
		{
			if (val == 0ULL)
				continue;

			counter_submit ("mysql_handler", 
					key + strlen ("Handler_"), 
					val, db);
		}
		else if (strncmp (key, "Qcache_",
       				        strlen ("Qcache_")) == 0)
		{
			if (strcmp (key, "Qcache_hits") == 0)
				qcache_hits = (derive_t) val;
			else if (strcmp (key, "Qcache_inserts") == 0)
				qcache_inserts = (derive_t) val;
			else if (strcmp (key, "Qcache_not_cached") == 0)
				qcache_not_cached = (derive_t) val;
			else if (strcmp (key, "Qcache_lowmem_prunes") == 0)
				qcache_lowmem_prunes = (derive_t) val;
			else if (strcmp (key, "Qcache_queries_in_cache") == 0)
				qcache_queries_in_cache = (gauge_t) val;
		}
		else if (strncmp (key, "Bytes_", 
				        strlen ("Bytes_")) == 0)
		{
			if (strcmp (key, "Bytes_received") == 0)
				traffic_incoming += val;
			else if (strcmp (key, "Bytes_sent") == 0)
				traffic_outgoing += val;
		}
		else if (strncmp (key, "Threads_", 
       				        strlen ("Threads_")) == 0)
		{
			if (strcmp (key, "Threads_running") == 0)
				threads_running = (gauge_t) val;
			else if (strcmp (key, "Threads_connected") == 0)
				threads_connected = (gauge_t) val;
			else if (strcmp (key, "Threads_cached") == 0)
				threads_cached = (gauge_t) val;
			else if (strcmp (key, "Threads_created") == 0)
				threads_created = (derive_t) val;
		}
		else if (strncmp (key, "Table_locks_",
					strlen ("Table_locks_")) == 0)
		{
			counter_submit ("mysql_locks",
					key + strlen ("Table_locks_"),
					val, db);
		}
	}
	mysql_free_result (res); res = NULL;

	if ((qcache_hits != 0)
			|| (qcache_inserts != 0)
			|| (qcache_not_cached != 0)
			|| (qcache_lowmem_prunes != 0))
	{
		derive_submit ("cache_result", "qcache-hits",
				qcache_hits, db);
		derive_submit ("cache_result", "qcache-inserts",
				qcache_inserts, db);
		derive_submit ("cache_result", "qcache-not_cached",
				qcache_not_cached, db);
		derive_submit ("cache_result", "qcache-prunes",
				qcache_lowmem_prunes, db);

		gauge_submit ("cache_size", "qcache",
				qcache_queries_in_cache, db);
	}

	if (threads_created != 0)
	{
		gauge_submit ("threads", "running",
				threads_running, db);
		gauge_submit ("threads", "connected",
				threads_connected, db);
		gauge_submit ("threads", "cached",
				threads_cached, db);

		derive_submit ("total_threads", "created",
				threads_created, db);
	}

	traffic_submit  (traffic_incoming, traffic_outgoing, db);

	if (db->master_stats)
		mysql_read_master_stats (db, con);

	if ((db->slave_stats) || (db->slave_notif))
		mysql_read_slave_stats (db, con);

	return (0);
} /* int mysql_read */

void module_register (void)
{
	plugin_register_complex_config ("mysql", mysql_config);
} /* void module_register */
