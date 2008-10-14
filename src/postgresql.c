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
} c_psql_param_t;

typedef struct {
	char *type;
	char *type_instance;
	int   ds_type;
} c_psql_col_t;

typedef struct {
	char *name;
	char *query;

	c_psql_param_t *params;
	int             params_num;

	c_psql_col_t *cols;
	int           cols_num;

	int min_pg_version;
	int max_pg_version;
} c_psql_query_t;

typedef struct {
	PGconn      *conn;
	c_complain_t conn_complaint;

	int proto_version;

	int max_params_num;

	/* user configuration */
	c_psql_query_t **queries;
	int              queries_num;

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

static c_psql_query_t *queries          = NULL;
static int             queries_num      = 0;

static c_psql_database_t *databases     = NULL;
static int                databases_num = 0;

static c_psql_query_t *c_psql_query_new (const char *name)
{
	c_psql_query_t *query;

	++queries_num;
	if (NULL == (queries = (c_psql_query_t *)realloc (queries,
				queries_num * sizeof (*queries)))) {
		log_err ("Out of memory.");
		exit (5);
	}
	query = queries + queries_num - 1;

	query->name  = sstrdup (name);
	query->query = NULL;

	query->params     = NULL;
	query->params_num = 0;

	query->cols     = NULL;
	query->cols_num = 0;

	query->min_pg_version = 0;
	query->max_pg_version = INT_MAX;
	return query;
} /* c_psql_query_new */

static void c_psql_query_delete (c_psql_query_t *query)
{
	int i;

	sfree (query->name);
	sfree (query->query);

	sfree (query->params);
	query->params_num = 0;

	for (i = 0; i < query->cols_num; ++i) {
		sfree (query->cols[i].type);
		sfree (query->cols[i].type_instance);
	}
	sfree (query->cols);
	query->cols_num = 0;
	return;
} /* c_psql_query_delete */

static c_psql_query_t *c_psql_query_get (const char *name, int server_version)
{
	int i;

	for (i = 0; i < queries_num; ++i)
		if (0 == strcasecmp (name, queries[i].name)
				&& ((-1 == server_version)
					|| ((queries[i].min_pg_version <= server_version)
						&& (server_version <= queries[i].max_pg_version))))
			return queries + i;
	return NULL;
} /* c_psql_query_get */

static c_psql_database_t *c_psql_database_new (const char *name)
{
	c_psql_database_t *db;

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

	db->proto_version = 0;

	db->max_params_num = 0;

	db->queries     = NULL;
	db->queries_num = 0;

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

static void c_psql_database_delete (c_psql_database_t *db)
{
	PQfinish (db->conn);
	db->conn = NULL;

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

		db->proto_version = PQprotocolVersion (db->conn);
		if (3 > db->proto_version)
			log_warn ("Protocol version %d does not support parameters.",
					db->proto_version);
	}

	c_release (LOG_INFO, &db->conn_complaint,
			"Successfully reconnected to database %s", PQdb (db->conn));
	return 0;
} /* c_psql_check_connection */

static PGresult *c_psql_exec_query_params (c_psql_database_t *db,
		c_psql_query_t *query)
{
	char *params[db->max_params_num];
	int   i;

	assert (db->max_params_num >= query->params_num);

	for (i = 0; i < query->params_num; ++i) {
		switch (query->params[i]) {
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
			default:
				assert (0);
		}
	}

	return PQexecParams (db->conn, query->query, query->params_num, NULL,
			(const char *const *)((0 == query->params_num) ? NULL : params),
			NULL, NULL, /* return text data */ 0);
} /* c_psql_exec_query_params */

static PGresult *c_psql_exec_query_noparams (c_psql_database_t *db,
		c_psql_query_t *query)
{
	return PQexec (db->conn, query->query);
} /* c_psql_exec_query_noparams */

static int c_psql_exec_query (c_psql_database_t *db, int idx)
{
	c_psql_query_t *query;
	PGresult       *res;

	int rows, cols;
	int i;

	if (idx >= db->queries_num)
		return -1;

	query = db->queries[idx];

	if (3 <= db->proto_version)
		res = c_psql_exec_query_params (db, query);
	else if (0 == query->params_num)
		res = c_psql_exec_query_noparams (db, query);
	else {
		log_err ("Connection to database \"%s\" does not support parameters "
				"(protocol version %d) - cannot execute query \"%s\".",
				db->database, db->proto_version, query->name);
		return -1;
	}

	if (PGRES_TUPLES_OK != PQresultStatus (res)) {
		log_err ("Failed to execute SQL query: %s",
				PQerrorMessage (db->conn));
		log_info ("SQL query was: %s", query->query);
		PQclear (res);
		return -1;
	}

	rows = PQntuples (res);
	if (1 > rows) {
		PQclear (res);
		return 0;
	}

	cols = PQnfields (res);
	if (query->cols_num != cols) {
		log_err ("SQL query returned wrong number of fields "
				"(expected: %i, got: %i)", query->cols_num, cols);
		log_info ("SQL query was: %s", query->query);
		PQclear (res);
		return -1;
	}

	for (i = 0; i < rows; ++i) {
		int j;

		for (j = 0; j < cols; ++j) {
			c_psql_col_t col = query->cols[j];

			char *value = PQgetvalue (res, i, j);

			if (col.ds_type == DS_TYPE_COUNTER)
				submit_counter (db, col.type, col.type_instance, value);
			else if (col.ds_type == DS_TYPE_GAUGE)
				submit_gauge (db, col.type, col.type_instance, value);
		}
	}
	PQclear (res);
	return 0;
} /* c_psql_exec_query */

static int c_psql_read (void)
{
	int success = 0;
	int i;

	for (i = 0; i < databases_num; ++i) {
		c_psql_database_t *db = databases + i;

		int j;

		assert (NULL != db->database);

		if (0 != c_psql_check_connection (db))
			continue;

		for (j = 0; j < db->queries_num; ++j)
			c_psql_exec_query (db, j);

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
		c_psql_database_delete (db);
	}

	sfree (databases);
	databases_num = 0;

	for (i = 0; i < queries_num; ++i) {
		c_psql_query_t *query = queries + i;
		c_psql_query_delete (query);
	}

	sfree (queries);
	queries_num = 0;
	return 0;
} /* c_psql_shutdown */

static int c_psql_init (void)
{
	int i;

	if ((NULL == databases) || (0 == databases_num))
		return 0;

	for (i = 0; i < queries_num; ++i) {
		c_psql_query_t *query = queries + i;
		int j;

		for (j = 0; j < query->cols_num; ++j) {
			c_psql_col_t     *col = query->cols + j;
			const data_set_t *ds;

			ds = plugin_get_ds (col->type);
			if (NULL == ds) {
				log_err ("Column: Unknown type \"%s\".", col->type);
				c_psql_shutdown ();
				return -1;
			}

			if (1 != ds->ds_num) {
				log_err ("Column: Invalid type \"%s\" - types defining "
						"one data source are supported only (got: %i).",
						col->type, ds->ds_num);
				c_psql_shutdown ();
				return -1;
			}

			col->ds_type = ds->ds[0].type;
		}
	}

	for (i = 0; i < databases_num; ++i) {
		c_psql_database_t *db = databases + i;

		char  conninfo[4096];
		char *buf     = conninfo;
		int   buf_len = sizeof (conninfo);
		int   status;

		char *server_host;
		int   server_version;

		int j;

		/* this will happen during reinitialization */
		if (NULL != db->conn) {
			c_psql_check_connection (db);
			continue;
		}

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

		db->proto_version = PQprotocolVersion (db->conn);

		server_host    = PQhost (db->conn);
		server_version = PQserverVersion (db->conn);
		log_info ("Sucessfully connected to database %s (user %s) "
				"at server %s%s%s (server version: %d.%d.%d, "
				"protocol version: %d, pid: %d)",
				PQdb (db->conn), PQuser (db->conn),
				C_PSQL_SOCKET3 (server_host, PQport (db->conn)),
				C_PSQL_SERVER_VERSION3 (server_version),
				db->proto_version, PQbackendPID (db->conn));

		if (3 > db->proto_version)
			log_warn ("Protocol version %d does not support parameters.",
					db->proto_version);

		/* Now that we know the PostgreSQL server version, we can get the
		 * right version of each query definition. */
		for (j = 0; j < db->queries_num; ++j) {
			c_psql_query_t *tmp;

			tmp = c_psql_query_get (db->queries[j]->name, server_version);

			if (tmp == db->queries[j])
				continue;

			if (NULL == tmp) {
				log_err ("Query \"%s\" not found for server version %i - "
						"please check your configuration.",
						db->queries[j]->name, server_version);

				if (db->queries_num - j - 1 > 0)
					memmove (db->queries + j, db->queries + j + 1,
							(db->queries_num - j - 1) * sizeof (*db->queries));

				--db->queries_num;
				--j;
				continue;
			}

			db->queries[j] = tmp;
		}
	}

	plugin_register_read ("postgresql", c_psql_read);
	plugin_register_shutdown ("postgresql", c_psql_shutdown);
	return 0;
} /* c_psql_init */

static int config_set_s (char *name, char **var, const oconfig_item_t *ci)
{
	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("%s expects a single string argument.", name);
		return 1;
	}

	sfree (*var);
	*var = sstrdup (ci->values[0].value.string);
	return 0;
} /* config_set_s */

static int config_set_i (char *name, int *var, const oconfig_item_t *ci)
{
	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_NUMBER != ci->values[0].type)) {
		log_err ("%s expects a single number argument.", name);
		return 1;
	}

	*var = (int)ci->values[0].value.number;
	return 0;
} /* config_set_i */

static int config_set_param (c_psql_query_t *query, const oconfig_item_t *ci)
{
	c_psql_param_t param;
	char          *param_str;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("Param expects a single string argument.");
		return 1;
	}

	param_str = ci->values[0].value.string;
	if (0 == strcasecmp (param_str, "hostname"))
		param = C_PSQL_PARAM_HOST;
	else if (0 == strcasecmp (param_str, "database"))
		param = C_PSQL_PARAM_DB;
	else if (0 == strcasecmp (param_str, "username"))
		param = C_PSQL_PARAM_USER;
	else {
		log_err ("Invalid parameter \"%s\".", param_str);
		return 1;
	}

	++query->params_num;
	if (NULL == (query->params = (c_psql_param_t *)realloc (query->params,
				query->params_num * sizeof (*query->params)))) {
		log_err ("Out of memory.");
		exit (5);
	}

	query->params[query->params_num - 1] = param;
	return 0;
} /* config_set_param */

static int config_set_column (c_psql_query_t *query, const oconfig_item_t *ci)
{
	c_psql_col_t *col;

	int i;

	if ((0 != ci->children_num)
			|| (1 > ci->values_num) || (2 < ci->values_num)) {
		log_err ("Column expects either one or two arguments.");
		return 1;
	}

	for (i = 0; i < ci->values_num; ++i) {
		if (OCONFIG_TYPE_STRING != ci->values[i].type) {
			log_err ("Column expects either one or two string arguments.");
			return 1;
		}
	}

	++query->cols_num;
	if (NULL == (query->cols = (c_psql_col_t *)realloc (query->cols,
				query->cols_num * sizeof (*query->cols)))) {
		log_err ("Out of memory.");
		exit (5);
	}

	col = query->cols + query->cols_num - 1;

	col->ds_type = -1;

	col->type = sstrdup (ci->values[0].value.string);
	col->type_instance = (2 == ci->values_num)
		? sstrdup (ci->values[1].value.string) : NULL;
	return 0;
} /* config_set_column */

static int set_query (c_psql_database_t *db, const char *name)
{
	c_psql_query_t *query;

	query = c_psql_query_get (name, -1);
	if (NULL == query) {
		log_err ("Query \"%s\" not found - please check your configuration.",
				name);
		return 1;
	}

	++db->queries_num;
	if (NULL == (db->queries = (c_psql_query_t **)realloc (db->queries,
				db->queries_num * sizeof (*db->queries)))) {
		log_err ("Out of memory.");
		exit (5);
	}

	if (query->params_num > db->max_params_num)
		db->max_params_num = query->params_num;

	db->queries[db->queries_num - 1] = query;
	return 0;
} /* set_query */

static int config_set_query (c_psql_database_t *db, const oconfig_item_t *ci)
{
	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("Query expects a single string argument.");
		return 1;
	}
	return set_query (db, ci->values[0].value.string);
} /* config_set_query */

static int c_psql_config_query (oconfig_item_t *ci)
{
	c_psql_query_t *query;

	int i;

	if ((1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("<Query> expects a single string argument.");
		return 1;
	}

	query = c_psql_query_new (ci->values[0].value.string);

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "Query"))
			config_set_s ("Query", &query->query, c);
		else if (0 == strcasecmp (c->key, "Param"))
			config_set_param (query, c);
		else if (0 == strcasecmp (c->key, "Column"))
			config_set_column (query, c);
		else if (0 == strcasecmp (c->key, "MinPGVersion"))
			config_set_i ("MinPGVersion", &query->min_pg_version, c);
		else if (0 == strcasecmp (c->key, "MaxPGVersion"))
			config_set_i ("MaxPGVersion", &query->max_pg_version, c);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}

	for (i = 0; i < queries_num - 1; ++i) {
		c_psql_query_t *q = queries + i;

		if ((0 == strcasecmp (q->name, query->name))
				&& (q->min_pg_version <= query->max_pg_version)
				&& (query->min_pg_version <= q->max_pg_version)) {
			log_err ("Ignoring redefinition (with overlapping version ranges) "
					"of query \"%s\".", query->name);
			c_psql_query_delete (query);
			--queries_num;
			return 1;
		}
	}

	if (query->min_pg_version > query->max_pg_version) {
		log_err ("Query \"%s\": MinPGVersion > MaxPGVersion.",
				query->name);
		c_psql_query_delete (query);
		--queries_num;
		return 1;
	}

	if (NULL == query->query) {
		log_err ("Query \"%s\" does not include an SQL query string - "
				"please check your configuration.", query->name);
		c_psql_query_delete (query);
		--queries_num;
		return 1;
	}
	return 0;
} /* c_psql_config_query */

static int c_psql_config_database (oconfig_item_t *ci)
{
	c_psql_database_t *db;

	int i;

	if ((1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("<Database> expects a single string argument.");
		return 1;
	}

	db = c_psql_database_new (ci->values[0].value.string);

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "Host"))
			config_set_s ("Host", &db->host, c);
		else if (0 == strcasecmp (c->key, "Port"))
			config_set_s ("Port", &db->port, c);
		else if (0 == strcasecmp (c->key, "User"))
			config_set_s ("User", &db->user, c);
		else if (0 == strcasecmp (c->key, "Password"))
			config_set_s ("Password", &db->password, c);
		else if (0 == strcasecmp (c->key, "SSLMode"))
			config_set_s ("SSLMode", &db->sslmode, c);
		else if (0 == strcasecmp (c->key, "KRBSrvName"))
			config_set_s ("KRBSrvName", &db->krbsrvname, c);
		else if (0 == strcasecmp (c->key, "Service"))
			config_set_s ("Service", &db->service, c);
		else if (0 == strcasecmp (c->key, "Query"))
			config_set_query (db, c);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}

	if (NULL == db->queries) {
		for (i = 0; i < def_queries_num; ++i)
			set_query (db, def_queries[i]);
	}
	return 0;
}

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
			c_psql_config_query (c);
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
	plugin_register_init ("postgresql", c_psql_init);
} /* module_register */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

