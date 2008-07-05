/**
 * collectd - src/postgresql.c
 * Copyright (C) 2008  Sebastian Harl
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
 * Author:
 *   Sebastian Harl <sh at tokkee.org>
 **/

/*
 * This module collects PostgreSQL database statistics.
 */

#include "collectd.h"
#include "common.h"

#include "configfile.h"
#include "plugin.h"

#include "utils_complain.h"

#include <pg_config_manual.h>
#include <libpq-fe.h>

#define log_err(...) ERROR ("postgresql: " __VA_ARGS__)
#define log_warn(...) WARNING ("postgresql: " __VA_ARGS__)
#define log_info(...) INFO ("postgresql: " __VA_ARGS__)

/* Appends the (parameter, value) pair to the string
 * pointed to by 'buf' suitable to be used as argument
 * for PQconnectdb(). If value equals NULL, the pair
 * is ignored. */
#define C_PSQL_PAR_APPEND(buf, buf_len, parameter, value) \
	if ((0 < (buf_len)) && (NULL != (value)) && ('\0' != *(value))) { \
		int s = ssnprintf (buf, buf_len, " %s = '%s'", parameter, value); \
		if (0 < s) { \
			buf     += s; \
			buf_len -= s; \
		} \
	}

/* Returns the tuple (major, minor, patchlevel)
 * for the given version number. */
#define C_PSQL_SERVER_VERSION3(server_version) \
	(server_version) / 10000, \
	(server_version) / 100 - (int)((server_version) / 10000) * 100, \
	(server_version) - (int)((server_version) / 100) * 100

/* Returns true if the given host specifies a
 * UNIX domain socket. */
#define C_PSQL_IS_UNIX_DOMAIN_SOCKET(host) \
	((NULL == (host)) || ('\0' == *(host)) || ('/' == *(host)))

/* Returns the tuple (host, delimiter, port) for a
 * given (host, port) pair. Depending on the value of
 * 'host' a UNIX domain socket or a TCP socket is
 * assumed. */
#define C_PSQL_SOCKET3(host, port) \
	((NULL == (host)) || ('\0' == *(host))) ? DEFAULT_PGSOCKET_DIR : host, \
	C_PSQL_IS_UNIX_DOMAIN_SOCKET (host) ? "/.s.PGSQL." : ":", \
	port

typedef struct {
	PGconn      *conn;
	c_complain_t conn_complaint;

	/* user configuration */
	char *host;
	char *port;
	char *database;
	char *user;
	char *password;

	char *sslmode;

	char *krbsrvname;

	char *service;
} c_psql_database_t;

static c_psql_database_t *databases     = NULL;
static int                databases_num = 0;

static void submit (const c_psql_database_t *db,
		const char *type, const char *type_instance,
		value_t *values, size_t values_len)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values     = values;
	vl.values_len = values_len;
	vl.time       = time (NULL);

	if (C_PSQL_IS_UNIX_DOMAIN_SOCKET (db->host)
			|| (0 == strcmp (db->host, "localhost")))
		sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	else
		sstrncpy (vl.host, db->host, sizeof (vl.host));

	sstrncpy (vl.plugin, "postgresql", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, db->database, sizeof (vl.plugin_instance));

	sstrncpy (vl.type, type, sizeof (vl.type));

	if (NULL != type_instance)
		sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
	return;
} /* submit */

static void submit_counter (const c_psql_database_t *db,
		const char *type, const char *type_instance,
		const char *value)
{
	value_t values[1];

	if ((NULL == value) || ('\0' == *value))
		return;

	values[0].counter = atoll (value);
	submit (db, type, type_instance, values, 1);
	return;
} /* submit_counter */

static void submit_gauge (const c_psql_database_t *db,
		const char *type, const char *type_instance,
		const char *value)
{
	value_t values[1];

	if ((NULL == value) || ('\0' == *value))
		return;

	values[0].gauge = atof (value);
	submit (db, type, type_instance, values, 1);
	return;
} /* submit_gauge */

static int c_psql_check_connection (c_psql_database_t *db)
{
	/* "ping" */
	PQclear (PQexec (db->conn, "SELECT 42;"));

	if (CONNECTION_OK != PQstatus (db->conn)) {
		PQreset (db->conn);

		/* trigger c_release() */
		if (0 == db->conn_complaint.interval)
			db->conn_complaint.interval = 1;

		if (CONNECTION_OK != PQstatus (db->conn)) {
			c_complain (LOG_ERR, &db->conn_complaint,
					"Failed to connect to database %s: %s",
					db->database, PQerrorMessage (db->conn));
			return -1;
		}
	}

	c_release (LOG_INFO, &db->conn_complaint,
			"Successfully reconnected to database %s", PQdb (db->conn));
	return 0;
} /* c_psql_check_connection */

static int c_psql_stat_database (c_psql_database_t *db)
{
	const char *const query =
		"SELECT numbackends, xact_commit, xact_rollback "
			"FROM pg_stat_database "
			"WHERE datname = $1;";

	PGresult *res;

	int n;

	res = PQexecParams (db->conn, query, /* number of parameters */ 1,
			NULL, (const char *const *)&db->database, NULL, NULL,
			/* return text data */ 0);

	if (PGRES_TUPLES_OK != PQresultStatus (res)) {
		log_err ("Failed to execute SQL query: %s",
				PQerrorMessage (db->conn));
		log_info ("SQL query was: %s", query);
		PQclear (res);
		return -1;
	}

	n = PQntuples (res);
	if (1 < n) {
		log_warn ("pg_stat_database has more than one entry "
				"for database %s - ignoring additional results.",
				db->database);
	}
	else if (1 > n) {
		log_err ("pg_stat_database has no entry for database %s",
				db->database);
		PQclear (res);
		return -1;
	}

	submit_gauge (db, "pg_numbackends", NULL,  PQgetvalue (res, 0, 0));

	submit_counter (db, "pg_xact", "commit",   PQgetvalue (res, 0, 1));
	submit_counter (db, "pg_xact", "rollback", PQgetvalue (res, 0, 2));

	PQclear (res);
	return 0;
} /* c_psql_stat_database */

static int c_psql_stat_user_tables (c_psql_database_t *db)
{
	const char *const query =
		"SELECT sum(seq_scan), sum(seq_tup_read), "
				"sum(idx_scan), sum(idx_tup_fetch), "
				"sum(n_tup_ins), sum(n_tup_upd), sum(n_tup_del), "
				"sum(n_tup_hot_upd), sum(n_live_tup), sum(n_dead_tup) "
			"FROM pg_stat_user_tables;";

	PGresult *res;

	int n;

	res = PQexec (db->conn, query);

	if (PGRES_TUPLES_OK != PQresultStatus (res)) {
		log_err ("Failed to execute SQL query: %s",
				PQerrorMessage (db->conn));
		log_info ("SQL query was: %s", query);
		PQclear (res);
		return -1;
	}

	n = PQntuples (res);
	assert (1 >= n);

	if (1 > n) /* no user tables */
		return 0;

	submit_counter (db, "pg_scan", "seq",           PQgetvalue (res, 0, 0));
	submit_counter (db, "pg_scan", "seq_tup_read",  PQgetvalue (res, 0, 1));
	submit_counter (db, "pg_scan", "idx",           PQgetvalue (res, 0, 2));
	submit_counter (db, "pg_scan", "idx_tup_fetch", PQgetvalue (res, 0, 3));

	submit_counter (db, "pg_n_tup_c", "ins",        PQgetvalue (res, 0, 4));
	submit_counter (db, "pg_n_tup_c", "upd",        PQgetvalue (res, 0, 5));
	submit_counter (db, "pg_n_tup_c", "del",        PQgetvalue (res, 0, 6));
	submit_counter (db, "pg_n_tup_c", "hot_upd",    PQgetvalue (res, 0, 7));

	submit_gauge (db, "pg_n_tup_g", "live",         PQgetvalue (res, 0, 8));
	submit_gauge (db, "pg_n_tup_g", "dead",         PQgetvalue (res, 0, 9));

	PQclear (res);
	return 0;
} /* c_psql_stat_user_tables */

static int c_psql_statio_user_tables (c_psql_database_t *db)
{
	const char *const query =
		"SELECT sum(heap_blks_read), sum(heap_blks_hit), "
				"sum(idx_blks_read), sum(idx_blks_hit), "
				"sum(toast_blks_read), sum(toast_blks_hit), "
				"sum(tidx_blks_read), sum(tidx_blks_hit) "
			"FROM pg_statio_user_tables;";

	PGresult *res;

	int n;

	res = PQexec (db->conn, query);

	if (PGRES_TUPLES_OK != PQresultStatus (res)) {
		log_err ("Failed to execute SQL query: %s",
				PQerrorMessage (db->conn));
		log_info ("SQL query was: %s", query);
		PQclear (res);
		return -1;
	}

	n = PQntuples (res);
	assert (1 >= n);

	if (1 > n) /* no user tables */
		return 0;

	submit_counter (db, "pg_blks", "heap_read",  PQgetvalue (res, 0, 0));
	submit_counter (db, "pg_blks", "heap_hit",   PQgetvalue (res, 0, 1));

	submit_counter (db, "pg_blks", "idx_read",   PQgetvalue (res, 0, 2));
	submit_counter (db, "pg_blks", "idx_hit",    PQgetvalue (res, 0, 3));

	submit_counter (db, "pg_blks", "toast_read", PQgetvalue (res, 0, 4));
	submit_counter (db, "pg_blks", "toast_hit",  PQgetvalue (res, 0, 5));

	submit_counter (db, "pg_blks", "tidx_read",  PQgetvalue (res, 0, 6));
	submit_counter (db, "pg_blks", "tidx_hit",   PQgetvalue (res, 0, 7));

	PQclear (res);
	return 0;
} /* c_psql_statio_user_tables */

static int c_psql_read (void)
{
	int success = 0;
	int i;

	for (i = 0; i < databases_num; ++i) {
		c_psql_database_t *db = databases + i;

		assert (NULL != db->database);

		if (0 != c_psql_check_connection (db))
			continue;

		c_psql_stat_database (db);
		c_psql_stat_user_tables (db);
		c_psql_statio_user_tables (db);

		++success;
	}

	if (! success)
		return -1;
	return 0;
} /* c_psql_read */

static int c_psql_shutdown (void)
{
	int i;

	if ((NULL == databases) || (0 == databases_num))
		return 0;

	plugin_unregister_read ("postgresql");
	plugin_unregister_shutdown ("postgresql");

	for (i = 0; i < databases_num; ++i) {
		c_psql_database_t *db = databases + i;

		PQfinish (db->conn);

		sfree (db->database);
		sfree (db->host);
		sfree (db->port);
		sfree (db->user);
		sfree (db->password);

		sfree (db->sslmode);

		sfree (db->krbsrvname);

		sfree (db->service);
	}

	sfree (databases);
	databases_num = 0;
	return 0;
} /* c_psql_shutdown */

static int c_psql_init (void)
{
	int i;

	if ((NULL == databases) || (0 == databases_num))
		return 0;

	for (i = 0; i < databases_num; ++i) {
		c_psql_database_t *db = databases + i;

		char  conninfo[4096];
		char *buf     = conninfo;
		int   buf_len = sizeof (conninfo);
		int   status;

		char *server_host;
		int   server_version;

		status = ssnprintf (buf, buf_len, "dbname = '%s'", db->database);
		if (0 < status) {
			buf     += status;
			buf_len -= status;
		}

		C_PSQL_PAR_APPEND (buf, buf_len, "host",       db->host);
		C_PSQL_PAR_APPEND (buf, buf_len, "port",       db->port);
		C_PSQL_PAR_APPEND (buf, buf_len, "user",       db->user);
		C_PSQL_PAR_APPEND (buf, buf_len, "password",   db->password);
		C_PSQL_PAR_APPEND (buf, buf_len, "sslmode",    db->sslmode);
		C_PSQL_PAR_APPEND (buf, buf_len, "krbsrvname", db->krbsrvname);
		C_PSQL_PAR_APPEND (buf, buf_len, "service",    db->service);

		db->conn = PQconnectdb (conninfo);
		if (0 != c_psql_check_connection (db))
			continue;

		server_host    = PQhost (db->conn);
		server_version = PQserverVersion (db->conn);
		log_info ("Sucessfully connected to database %s (user %s) "
				"at server %s%s%s (server version: %d.%d.%d, "
				"protocol version: %d, pid: %d)",
				PQdb (db->conn), PQuser (db->conn),
				C_PSQL_SOCKET3(server_host, PQport (db->conn)),
				C_PSQL_SERVER_VERSION3 (server_version),
				PQprotocolVersion (db->conn), PQbackendPID (db->conn));
	}

	plugin_register_read ("postgresql", c_psql_read);
	plugin_register_shutdown ("postgresql", c_psql_shutdown);
	return 0;
} /* c_psql_init */

static int config_set (char *name, char **var, const oconfig_item_t *ci)
{
	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("%s expects a single string argument.", name);
		return 1;
	}

	sfree (*var);
	*var = sstrdup (ci->values[0].value.string);
	return 0;
} /* config_set */

static int c_psql_config_database (oconfig_item_t *ci)
{
	c_psql_database_t *db;

	int i;

	if ((1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("<Database> expects a single string argument.");
		return 1;
	}

	++databases_num;
	if (NULL == (databases = (c_psql_database_t *)realloc (databases,
				databases_num * sizeof (*databases)))) {
		log_err ("Out of memory.");
		exit (5);
	}

	db = databases + (databases_num - 1);

	db->conn = NULL;

	db->conn_complaint.last     = 0;
	db->conn_complaint.interval = 0;

	db->database   = sstrdup (ci->values[0].value.string);
	db->host       = NULL;
	db->port       = NULL;
	db->user       = NULL;
	db->password   = NULL;

	db->sslmode    = NULL;

	db->krbsrvname = NULL;

	db->service    = NULL;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "Host"))
			config_set ("Host", &db->host, c);
		else if (0 == strcasecmp (c->key, "Port"))
			config_set ("Port", &db->port, c);
		else if (0 == strcasecmp (c->key, "User"))
			config_set ("User", &db->user, c);
		else if (0 == strcasecmp (c->key, "Password"))
			config_set ("Password", &db->password, c);
		else if (0 == strcasecmp (c->key, "SSLMode"))
			config_set ("SSLMode", &db->sslmode, c);
		else if (0 == strcasecmp (c->key, "KRBSrvName"))
			config_set ("KRBSrvName", &db->krbsrvname, c);
		else if (0 == strcasecmp (c->key, "Service"))
			config_set ("Service", &db->service, c);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}
	return 0;
}

static int c_psql_config (oconfig_item_t *ci)
{
	int i;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "Database"))
			c_psql_config_database (c);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}
	return 0;
} /* c_psql_config */

void module_register (void)
{
	plugin_register_complex_config ("postgresql", c_psql_config);
	plugin_register_init ("postgresql", c_psql_init);
} /* module_register */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

