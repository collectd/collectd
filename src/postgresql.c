/**
 * collectd - src/postgresql.c
 * Copyright (C) 2008, 2009  Sebastian Harl
 * Copyright (C) 2009        Florian Forster
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors:
 *   Sebastian Harl <sh at tokkee.org>
 *   Florian Forster <octo at verplant.org>
 **/

/*
 * This module collects PostgreSQL database statistics.
 */

#include "collectd.h"
#include "common.h"

#include "configfile.h"
#include "plugin.h"

#include "utils_db_query.h"
#include "utils_complain.h"

#include <pg_config_manual.h>
#include <libpq-fe.h>

#define log_err(...) ERROR ("postgresql: " __VA_ARGS__)
#define log_warn(...) WARNING ("postgresql: " __VA_ARGS__)
#define log_info(...) INFO ("postgresql: " __VA_ARGS__)

#ifndef C_PSQL_DEFAULT_CONF
# define C_PSQL_DEFAULT_CONF PKGDATADIR "/postgresql_default.conf"
#endif

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

typedef enum {
	C_PSQL_PARAM_HOST = 1,
	C_PSQL_PARAM_DB,
	C_PSQL_PARAM_USER,
	C_PSQL_PARAM_INTERVAL,
} c_psql_param_t;

/* Parameter configuration. Stored as `user data' in the query objects. */
typedef struct {
	c_psql_param_t *params;
	int             params_num;
} c_psql_user_data_t;

typedef struct {
	PGconn      *conn;
	c_complain_t conn_complaint;

	int proto_version;
	int server_version;

	int max_params_num;

	/* user configuration */
	udb_query_preparation_area_t **q_prep_areas;
	udb_query_t    **queries;
	size_t           queries_num;

	cdtime_t interval;

	char *host;
	char *port;
	char *database;
	char *user;
	char *password;

	char *sslmode;

	char *krbsrvname;

	char *service;
} c_psql_database_t;

static char *def_queries[] = {
	"backends",
	"transactions",
	"queries",
	"query_plans",
	"table_states",
	"disk_io",
	"disk_usage"
};
static int def_queries_num = STATIC_ARRAY_SIZE (def_queries);

static udb_query_t      **queries       = NULL;
static size_t             queries_num   = 0;

static c_psql_database_t *c_psql_database_new (const char *name)
{
	c_psql_database_t *db;

	db = (c_psql_database_t *)malloc (sizeof (*db));
	if (NULL == db) {
		log_err ("Out of memory.");
		return NULL;
	}

	db->conn = NULL;

	C_COMPLAIN_INIT (&db->conn_complaint);

	db->proto_version = 0;
	db->server_version = 0;

	db->max_params_num = 0;

	db->q_prep_areas   = NULL;
	db->queries        = NULL;
	db->queries_num    = 0;

	db->interval   = 0;

	db->database   = sstrdup (name);
	db->host       = NULL;
	db->port       = NULL;
	db->user       = NULL;
	db->password   = NULL;

	db->sslmode    = NULL;

	db->krbsrvname = NULL;

	db->service    = NULL;
	return db;
} /* c_psql_database_new */

static void c_psql_database_delete (void *data)
{
	size_t i;

	c_psql_database_t *db = data;

	PQfinish (db->conn);
	db->conn = NULL;

	if (db->q_prep_areas)
		for (i = 0; i < db->queries_num; ++i)
			udb_query_delete_preparation_area (db->q_prep_areas[i]);
	free (db->q_prep_areas);

	sfree (db->queries);
	db->queries_num = 0;

	sfree (db->database);
	sfree (db->host);
	sfree (db->port);
	sfree (db->user);
	sfree (db->password);

	sfree (db->sslmode);

	sfree (db->krbsrvname);

	sfree (db->service);
	return;
} /* c_psql_database_delete */

static int c_psql_connect (c_psql_database_t *db)
{
	char  conninfo[4096];
	char *buf     = conninfo;
	int   buf_len = sizeof (conninfo);
	int   status;

	if (! db)
		return -1;

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
	db->proto_version = PQprotocolVersion (db->conn);
	return 0;
} /* c_psql_connect */

static int c_psql_check_connection (c_psql_database_t *db)
{
	_Bool init = 0;

	if (! db->conn) {
		init = 1;

		/* trigger c_release() */
		if (0 == db->conn_complaint.interval)
			db->conn_complaint.interval = 1;

		c_psql_connect (db);
	}

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

		db->proto_version = PQprotocolVersion (db->conn);
	}

	db->server_version = PQserverVersion (db->conn);

	if (c_would_release (&db->conn_complaint)) {
		char *server_host;
		int   server_version;

		server_host    = PQhost (db->conn);
		server_version = PQserverVersion (db->conn);

		c_do_release (LOG_INFO, &db->conn_complaint,
				"Successfully %sconnected to database %s (user %s) "
				"at server %s%s%s (server version: %d.%d.%d, "
				"protocol version: %d, pid: %d)", init ? "" : "re",
				PQdb (db->conn), PQuser (db->conn),
				C_PSQL_SOCKET3 (server_host, PQport (db->conn)),
				C_PSQL_SERVER_VERSION3 (server_version),
				db->proto_version, PQbackendPID (db->conn));

		if (3 > db->proto_version)
			log_warn ("Protocol version %d does not support parameters.",
					db->proto_version);
	}
	return 0;
} /* c_psql_check_connection */

static PGresult *c_psql_exec_query_noparams (c_psql_database_t *db,
		udb_query_t *q)
{
	return PQexec (db->conn, udb_query_get_statement (q));
} /* c_psql_exec_query_noparams */

static PGresult *c_psql_exec_query_params (c_psql_database_t *db,
		udb_query_t *q, c_psql_user_data_t *data)
{
	char *params[db->max_params_num];
	char  interval[64];
	int   i;

	if ((data == NULL) || (data->params_num == 0))
		return (c_psql_exec_query_noparams (db, q));

	assert (db->max_params_num >= data->params_num);

	for (i = 0; i < data->params_num; ++i) {
		switch (data->params[i]) {
			case C_PSQL_PARAM_HOST:
				params[i] = C_PSQL_IS_UNIX_DOMAIN_SOCKET (db->host)
					? "localhost" : db->host;
				break;
			case C_PSQL_PARAM_DB:
				params[i] = db->database;
				break;
			case C_PSQL_PARAM_USER:
				params[i] = db->user;
				break;
			case C_PSQL_PARAM_INTERVAL:
				ssnprintf (interval, sizeof (interval), "%.3f",
						(db->interval > 0)
						? CDTIME_T_TO_DOUBLE (db->interval) : interval_g);
				params[i] = interval;
				break;
			default:
				assert (0);
		}
	}

	return PQexecParams (db->conn, udb_query_get_statement (q),
			data->params_num, NULL,
			(const char *const *) params,
			NULL, NULL, /* return text data */ 0);
} /* c_psql_exec_query_params */

static int c_psql_exec_query (c_psql_database_t *db, udb_query_t *q,
		udb_query_preparation_area_t *prep_area)
{
	PGresult *res;

	c_psql_user_data_t *data;

	const char *host;

	char **column_names;
	char **column_values;
	int    column_num;

	int rows_num;
	int status;
	int row, col;

	/* The user data may hold parameter information, but may be NULL. */
	data = udb_query_get_user_data (q);

	/* Versions up to `3' don't know how to handle parameters. */
	if (3 <= db->proto_version)
		res = c_psql_exec_query_params (db, q, data);
	else if ((NULL == data) || (0 == data->params_num))
		res = c_psql_exec_query_noparams (db, q);
	else {
		log_err ("Connection to database \"%s\" does not support parameters "
				"(protocol version %d) - cannot execute query \"%s\".",
				db->database, db->proto_version,
				udb_query_get_name (q));
		return -1;
	}

	column_names = NULL;
	column_values = NULL;

#define BAIL_OUT(status) \
	sfree (column_names); \
	sfree (column_values); \
	PQclear (res); \
	return status

	if (PGRES_TUPLES_OK != PQresultStatus (res)) {
		log_err ("Failed to execute SQL query: %s",
				PQerrorMessage (db->conn));
		log_info ("SQL query was: %s",
				udb_query_get_statement (q));
		BAIL_OUT (-1);
	}

	rows_num = PQntuples (res);
	if (1 > rows_num) {
		BAIL_OUT (0);
	}

	column_num = PQnfields (res);
	column_names = (char **) calloc (column_num, sizeof (char *));
	if (NULL == column_names) {
		log_err ("calloc failed.");
		BAIL_OUT (-1);
	}

	column_values = (char **) calloc (column_num, sizeof (char *));
	if (NULL == column_values) {
		log_err ("calloc failed.");
		BAIL_OUT (-1);
	}
	
	for (col = 0; col < column_num; ++col) {
		/* Pointers returned by `PQfname' are freed by `PQclear' via
		 * `BAIL_OUT'. */
		column_names[col] = PQfname (res, col);
		if (NULL == column_names[col]) {
			log_err ("Failed to resolve name of column %i.", col);
			BAIL_OUT (-1);
		}
	}

	if (C_PSQL_IS_UNIX_DOMAIN_SOCKET (db->host)
			|| (0 == strcmp (db->host, "localhost")))
		host = hostname_g;
	else
		host = db->host;

	status = udb_query_prepare_result (q, prep_area, host, "postgresql",
			db->database, column_names, (size_t) column_num, db->interval);
	if (0 != status) {
		log_err ("udb_query_prepare_result failed with status %i.",
				status);
		BAIL_OUT (-1);
	}

	for (row = 0; row < rows_num; ++row) {
		for (col = 0; col < column_num; ++col) {
			/* Pointers returned by `PQgetvalue' are freed by `PQclear' via
			 * `BAIL_OUT'. */
			column_values[col] = PQgetvalue (res, row, col);
			if (NULL == column_values[col]) {
				log_err ("Failed to get value at (row = %i, col = %i).",
						row, col);
				break;
			}
		}

		/* check for an error */
		if (col < column_num)
			continue;

		status = udb_query_handle_result (q, prep_area, column_values);
		if (status != 0) {
			log_err ("udb_query_handle_result failed with status %i.",
					status);
		}
	} /* for (row = 0; row < rows_num; ++row) */

	udb_query_finish_result (q, prep_area);

	BAIL_OUT (0);
#undef BAIL_OUT
} /* c_psql_exec_query */

static int c_psql_read (user_data_t *ud)
{
	c_psql_database_t *db;

	int success = 0;
	int i;

	if ((ud == NULL) || (ud->data == NULL)) {
		log_err ("c_psql_read: Invalid user data.");
		return -1;
	}

	db = ud->data;

	assert (NULL != db->database);

	if (0 != c_psql_check_connection (db))
		return -1;

	for (i = 0; i < db->queries_num; ++i)
	{
		udb_query_preparation_area_t *prep_area;
		udb_query_t *q;

		prep_area = db->q_prep_areas[i];
		q = db->queries[i];

		if ((0 != db->server_version)
				&& (udb_query_check_version (q, db->server_version) <= 0))
			continue;

		if (0 == c_psql_exec_query (db, q, prep_area))
			success = 1;
	}

	if (! success)
		return -1;
	return 0;
} /* c_psql_read */

static int c_psql_shutdown (void)
{
	plugin_unregister_read_group ("postgresql");

	udb_query_free (queries, queries_num);
	queries = NULL;
	queries_num = 0;

	return 0;
} /* c_psql_shutdown */

static int config_query_param_add (udb_query_t *q, oconfig_item_t *ci)
{
	c_psql_user_data_t *data;
	const char *param_str;

	c_psql_param_t *tmp;

	data = udb_query_get_user_data (q);
	if (NULL == data) {
		data = (c_psql_user_data_t *) smalloc (sizeof (*data));
		if (NULL == data) {
			log_err ("Out of memory.");
			return -1;
		}
		memset (data, 0, sizeof (*data));
		data->params = NULL;
	}

	tmp = (c_psql_param_t *) realloc (data->params,
			(data->params_num + 1) * sizeof (c_psql_param_t));
	if (NULL == tmp) {
		log_err ("Out of memory.");
		return -1;
	}
	data->params = tmp;

	param_str = ci->values[0].value.string;
	if (0 == strcasecmp (param_str, "hostname"))
		data->params[data->params_num] = C_PSQL_PARAM_HOST;
	else if (0 == strcasecmp (param_str, "database"))
		data->params[data->params_num] = C_PSQL_PARAM_DB;
	else if (0 == strcasecmp (param_str, "username"))
		data->params[data->params_num] = C_PSQL_PARAM_USER;
	else if (0 == strcasecmp (param_str, "interval"))
		data->params[data->params_num] = C_PSQL_PARAM_INTERVAL;
	else {
		log_err ("Invalid parameter \"%s\".", param_str);
		return 1;
	}

	data->params_num++;
	udb_query_set_user_data (q, data);

	return (0);
} /* config_query_param_add */

static int config_query_callback (udb_query_t *q, oconfig_item_t *ci)
{
	if (0 == strcasecmp ("Param", ci->key))
		return config_query_param_add (q, ci);

	log_err ("Option not allowed within a Query block: `%s'", ci->key);

	return (-1);
} /* config_query_callback */

static int c_psql_config_database (oconfig_item_t *ci)
{
	c_psql_database_t *db;

	char cb_name[DATA_MAX_NAME_LEN];
	struct timespec cb_interval = { 0, 0 };
	user_data_t ud;

	int i;

	if ((1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("<Database> expects a single string argument.");
		return 1;
	}

	memset (&ud, 0, sizeof (ud));

	db = c_psql_database_new (ci->values[0].value.string);
	if (db == NULL)
		return -1;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "Host"))
			cf_util_get_string (c, &db->host);
		else if (0 == strcasecmp (c->key, "Port"))
			cf_util_get_service (c, &db->port);
		else if (0 == strcasecmp (c->key, "User"))
			cf_util_get_string (c, &db->user);
		else if (0 == strcasecmp (c->key, "Password"))
			cf_util_get_string (c, &db->password);
		else if (0 == strcasecmp (c->key, "SSLMode"))
			cf_util_get_string (c, &db->sslmode);
		else if (0 == strcasecmp (c->key, "KRBSrvName"))
			cf_util_get_string (c, &db->krbsrvname);
		else if (0 == strcasecmp (c->key, "Service"))
			cf_util_get_string (c, &db->service);
		else if (0 == strcasecmp (c->key, "Query"))
			udb_query_pick_from_list (c, queries, queries_num,
					&db->queries, &db->queries_num);
		else if (0 == strcasecmp (c->key, "Interval"))
			cf_util_get_cdtime (c, &db->interval);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}

	/* If no `Query' options were given, add the default queries.. */
	if (db->queries_num == 0) {
		for (i = 0; i < def_queries_num; i++)
			udb_query_pick_from_list_by_name (def_queries[i],
					queries, queries_num,
					&db->queries, &db->queries_num);
	}

	if (db->queries_num > 0) {
		db->q_prep_areas = (udb_query_preparation_area_t **) calloc (
				db->queries_num, sizeof (*db->q_prep_areas));

		if (db->q_prep_areas == NULL) {
			log_err ("Out of memory.");
			c_psql_database_delete (db);
			return -1;
		}
	}

	for (i = 0; (size_t)i < db->queries_num; ++i) {
		c_psql_user_data_t *data;
		data = udb_query_get_user_data (db->queries[i]);
		if ((data != NULL) && (data->params_num > db->max_params_num))
			db->max_params_num = data->params_num;

		db->q_prep_areas[i]
			= udb_query_allocate_preparation_area (db->queries[i]);

		if (db->q_prep_areas[i] == NULL) {
			log_err ("Out of memory.");
			c_psql_database_delete (db);
			return -1;
		}
	}

	ud.data = db;
	ud.free_func = c_psql_database_delete;

	ssnprintf (cb_name, sizeof (cb_name), "postgresql-%s", db->database);

	CDTIME_T_TO_TIMESPEC (db->interval, &cb_interval);

	plugin_register_complex_read ("postgresql", cb_name, c_psql_read,
			/* interval = */ (db->interval > 0) ? &cb_interval : NULL,
			&ud);
	return 0;
} /* c_psql_config_database */

static int c_psql_config (oconfig_item_t *ci)
{
	static int have_def_config = 0;

	int i;

	if (0 == have_def_config) {
		oconfig_item_t *c;

		have_def_config = 1;

		c = oconfig_parse_file (C_PSQL_DEFAULT_CONF);
		if (NULL == c)
			log_err ("Failed to read default config ("C_PSQL_DEFAULT_CONF").");
		else
			c_psql_config (c);

		if (NULL == queries)
			log_err ("Default config ("C_PSQL_DEFAULT_CONF") did not define "
					"any queries - please check your installation.");
	}

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "Query"))
			udb_query_create (&queries, &queries_num, c,
					/* callback = */ config_query_callback);
		else if (0 == strcasecmp (c->key, "Database"))
			c_psql_config_database (c);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}
	return 0;
} /* c_psql_config */

void module_register (void)
{
	plugin_register_complex_config ("postgresql", c_psql_config);
	plugin_register_shutdown ("postgresql", c_psql_shutdown);
} /* module_register */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */
