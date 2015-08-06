/**
 * collectd - src/postgresql.c
 * Copyright (C) 2008-2012  Sebastian Harl
 * Copyright (C) 2009       Florian Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Sebastian Harl <sh at tokkee.org>
 *   Florian Forster <octo at collectd.org>
 **/

/*
 * This module collects PostgreSQL database statistics.
 */

#include "collectd.h"
#include "common.h"

#include "configfile.h"
#include "plugin.h"

#include "utils_cache.h"
#include "utils_db_query.h"
#include "utils_complain.h"

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include <pg_config_manual.h>
#include <libpq-fe.h>

#define log_err(...) ERROR ("postgresql: " __VA_ARGS__)
#define log_warn(...) WARNING ("postgresql: " __VA_ARGS__)
#define log_info(...) INFO ("postgresql: " __VA_ARGS__)
#define log_debug(...) DEBUG ("postgresql: " __VA_ARGS__)

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
	C_PSQL_PARAM_INSTANCE,
} c_psql_param_t;

/* Parameter configuration. Stored as `user data' in the query objects. */
typedef struct {
	c_psql_param_t *params;
	int             params_num;
} c_psql_user_data_t;

typedef struct {
	char *name;
	char *statement;
	_Bool store_rates;
} c_psql_writer_t;

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

	c_psql_writer_t **writers;
	size_t            writers_num;

	/* make sure we don't access the database object in parallel */
	pthread_mutex_t   db_lock;

	cdtime_t interval;

	/* writer "caching" settings */
	cdtime_t commit_interval;
	cdtime_t next_commit;
	cdtime_t expire_delay;

	char *host;
	char *port;
	char *database;
	char *user;
	char *password;

	char *instance;

	char *sslmode;

	char *krbsrvname;

	char *service;

	int ref_cnt;
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

static c_psql_database_t **databases     = NULL;
static size_t              databases_num = 0;

static udb_query_t       **queries       = NULL;
static size_t              queries_num   = 0;

static c_psql_writer_t    *writers       = NULL;
static size_t              writers_num   = 0;

static int c_psql_begin (c_psql_database_t *db)
{
	PGresult *r = PQexec (db->conn, "BEGIN");

	int status = 1;

	if (r != NULL) {
		if (PGRES_COMMAND_OK == PQresultStatus (r)) {
			db->next_commit = cdtime() + db->commit_interval;
			status = 0;
		}
		else
			log_warn ("Failed to initiate ('BEGIN') transaction: %s",
					PQerrorMessage (db->conn));
		PQclear (r);
	}
	return status;
} /* c_psql_begin */

static int c_psql_commit (c_psql_database_t *db)
{
	PGresult *r = PQexec (db->conn, "COMMIT");

	int status = 1;

	if (r != NULL) {
		if (PGRES_COMMAND_OK == PQresultStatus (r)) {
			db->next_commit = 0;
			log_debug ("Successfully committed transaction.");
			status = 0;
		}
		else
			log_warn ("Failed to commit transaction: %s",
					PQerrorMessage (db->conn));
		PQclear (r);
	}
	return status;
} /* c_psql_commit */

static c_psql_database_t *c_psql_database_new (const char *name)
{
	c_psql_database_t **tmp;
	c_psql_database_t  *db;

	db = (c_psql_database_t *)malloc (sizeof(*db));
	if (NULL == db) {
		log_err ("Out of memory.");
		return NULL;
	}

	tmp = (c_psql_database_t **)realloc (databases,
			(databases_num + 1) * sizeof (*databases));
	if (NULL == tmp) {
		log_err ("Out of memory.");
		sfree (db);
		return NULL;
	}

	databases = tmp;
	databases[databases_num] = db;
	++databases_num;

	db->conn = NULL;

	C_COMPLAIN_INIT (&db->conn_complaint);

	db->proto_version = 0;
	db->server_version = 0;

	db->max_params_num = 0;

	db->q_prep_areas   = NULL;
	db->queries        = NULL;
	db->queries_num    = 0;

	db->writers        = NULL;
	db->writers_num    = 0;

	pthread_mutex_init (&db->db_lock, /* attrs = */ NULL);

	db->interval   = 0;

	db->commit_interval = 0;
	db->next_commit     = 0;
	db->expire_delay    = 0;

	db->database   = sstrdup (name);
	db->host       = NULL;
	db->port       = NULL;
	db->user       = NULL;
	db->password   = NULL;

	db->instance   = sstrdup (name);

	db->sslmode    = NULL;

	db->krbsrvname = NULL;

	db->service    = NULL;

	db->ref_cnt    = 0;
	return db;
} /* c_psql_database_new */

static void c_psql_database_delete (void *data)
{
	size_t i;

	c_psql_database_t *db = data;

	--db->ref_cnt;
	/* readers and writers may access this database */
	if (db->ref_cnt > 0)
		return;

	/* wait for the lock to be released by the last writer */
	pthread_mutex_lock (&db->db_lock);

	if (db->next_commit > 0)
		c_psql_commit (db);

	PQfinish (db->conn);
	db->conn = NULL;

	if (db->q_prep_areas)
		for (i = 0; i < db->queries_num; ++i)
			udb_query_delete_preparation_area (db->q_prep_areas[i]);
	free (db->q_prep_areas);

	sfree (db->queries);
	db->queries_num = 0;

	sfree (db->writers);
	db->writers_num = 0;

	pthread_mutex_unlock (&db->db_lock);

	pthread_mutex_destroy (&db->db_lock);

	sfree (db->database);
	sfree (db->host);
	sfree (db->port);
	sfree (db->user);
	sfree (db->password);

	sfree (db->instance);

	sfree (db->sslmode);

	sfree (db->krbsrvname);

	sfree (db->service);

	/* don't care about freeing or reordering the 'databases' array
	 * this is done in 'shutdown'; also, don't free the database instance
	 * object just to make sure that in case anybody accesses it before
	 * shutdown won't segfault */
	return;
} /* c_psql_database_delete */

static int c_psql_connect (c_psql_database_t *db)
{
	char  conninfo[4096];
	char *buf     = conninfo;
	int   buf_len = sizeof (conninfo);
	int   status;

	if ((! db) || (! db->database))
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

	if (CONNECTION_OK != PQstatus (db->conn)) {
		PQreset (db->conn);

		/* trigger c_release() */
		if (0 == db->conn_complaint.interval)
			db->conn_complaint.interval = 1;

		if (CONNECTION_OK != PQstatus (db->conn)) {
			c_complain (LOG_ERR, &db->conn_complaint,
					"Failed to connect to database %s (%s): %s",
					db->database, db->instance,
					PQerrorMessage (db->conn));
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
						? CDTIME_T_TO_DOUBLE (db->interval)
						: plugin_get_interval ());
				params[i] = interval;
				break;
			case C_PSQL_PARAM_INSTANCE:
				params[i] = db->instance;
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

/* db->db_lock must be locked when calling this function */
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
		log_err ("Connection to database \"%s\" (%s) does not support "
				"parameters (protocol version %d) - "
				"cannot execute query \"%s\".",
				db->database, db->instance, db->proto_version,
				udb_query_get_name (q));
		return -1;
	}

	/* give c_psql_write() a chance to acquire the lock if called recursively
	 * through dispatch_values(); this will happen if, both, queries and
	 * writers are configured for a single connection */
	pthread_mutex_unlock (&db->db_lock);

	column_names = NULL;
	column_values = NULL;

	if (PGRES_TUPLES_OK != PQresultStatus (res)) {
		pthread_mutex_lock (&db->db_lock);

		if ((CONNECTION_OK != PQstatus (db->conn))
				&& (0 == c_psql_check_connection (db))) {
			PQclear (res);
			return c_psql_exec_query (db, q, prep_area);
		}

		log_err ("Failed to execute SQL query: %s",
				PQerrorMessage (db->conn));
		log_info ("SQL query was: %s",
				udb_query_get_statement (q));
		PQclear (res);
		return -1;
	}

#define BAIL_OUT(status) \
	sfree (column_names); \
	sfree (column_values); \
	PQclear (res); \
	pthread_mutex_lock (&db->db_lock); \
	return status

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
			|| (0 == strcmp (db->host, "127.0.0.1"))
			|| (0 == strcmp (db->host, "localhost")))
		host = hostname_g;
	else
		host = db->host;

	status = udb_query_prepare_result (q, prep_area, host, "postgresql",
			db->instance, column_names, (size_t) column_num, db->interval);
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
	assert (NULL != db->instance);
	assert (NULL != db->queries);

	pthread_mutex_lock (&db->db_lock);

	if (0 != c_psql_check_connection (db)) {
		pthread_mutex_unlock (&db->db_lock);
		return -1;
	}

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

	pthread_mutex_unlock (&db->db_lock);

	if (! success)
		return -1;
	return 0;
} /* c_psql_read */

static char *values_name_to_sqlarray (const data_set_t *ds,
		char *string, size_t string_len)
{
	char  *str_ptr;
	size_t str_len;

	int i;

	str_ptr = string;
	str_len = string_len;

	for (i = 0; i < ds->ds_num; ++i) {
		int status = ssnprintf (str_ptr, str_len, ",'%s'", ds->ds[i].name);

		if (status < 1)
			return NULL;
		else if ((size_t)status >= str_len) {
			str_len = 0;
			break;
		}
		else {
			str_ptr += status;
			str_len -= (size_t)status;
		}
	}

	if (str_len <= 2) {
		log_err ("c_psql_write: Failed to stringify value names");
		return NULL;
	}

	/* overwrite the first comma */
	string[0] = '{';
	str_ptr[0] = '}';
	str_ptr[1] = '\0';

	return string;
} /* values_name_to_sqlarray */

static char *values_type_to_sqlarray (const data_set_t *ds,
		char *string, size_t string_len, _Bool store_rates)
{
	char  *str_ptr;
	size_t str_len;

	int i;

	str_ptr = string;
	str_len = string_len;

	for (i = 0; i < ds->ds_num; ++i) {
		int status;

		if (store_rates)
			status = ssnprintf(str_ptr, str_len, ",'gauge'");
		else
			status = ssnprintf(str_ptr, str_len, ",'%s'",
					DS_TYPE_TO_STRING (ds->ds[i].type));

		if (status < 1) {
			str_len = 0;
			break;
		}
		else if ((size_t)status >= str_len) {
			str_len = 0;
			break;
		}
		else {
			str_ptr += status;
			str_len -= (size_t)status;
		}
	}

	if (str_len <= 2) {
		log_err ("c_psql_write: Failed to stringify value types");
		return NULL;
	}

	/* overwrite the first comma */
	string[0] = '{';
	str_ptr[0] = '}';
	str_ptr[1] = '\0';

	return string;
} /* values_type_to_sqlarray */

static char *values_to_sqlarray (const data_set_t *ds, const value_list_t *vl,
		char *string, size_t string_len, _Bool store_rates)
{
	char  *str_ptr;
	size_t str_len;

	gauge_t *rates = NULL;

	int i;

	str_ptr = string;
	str_len = string_len;

	for (i = 0; i < vl->values_len; ++i) {
		int status = 0;

		if ((ds->ds[i].type != DS_TYPE_GAUGE)
				&& (ds->ds[i].type != DS_TYPE_COUNTER)
				&& (ds->ds[i].type != DS_TYPE_DERIVE)
				&& (ds->ds[i].type != DS_TYPE_ABSOLUTE)) {
			log_err ("c_psql_write: Unknown data source type: %i",
					ds->ds[i].type);
			sfree (rates);
			return NULL;
		}

		if (ds->ds[i].type == DS_TYPE_GAUGE)
			status = ssnprintf (str_ptr, str_len,
					","GAUGE_FORMAT, vl->values[i].gauge);
		else if (store_rates) {
			if (rates == NULL)
				rates = uc_get_rate (ds, vl);

			if (rates == NULL) {
				log_err ("c_psql_write: Failed to determine rate");
				return NULL;
			}

			status = ssnprintf (str_ptr, str_len,
					",%lf", rates[i]);
		}
		else if (ds->ds[i].type == DS_TYPE_COUNTER)
			status = ssnprintf (str_ptr, str_len,
					",%llu", vl->values[i].counter);
		else if (ds->ds[i].type == DS_TYPE_DERIVE)
			status = ssnprintf (str_ptr, str_len,
					",%"PRIi64, vl->values[i].derive);
		else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
			status = ssnprintf (str_ptr, str_len,
					",%"PRIu64, vl->values[i].absolute);

		if (status < 1) {
			str_len = 0;
			break;
		}
		else if ((size_t)status >= str_len) {
			str_len = 0;
			break;
		}
		else {
			str_ptr += status;
			str_len -= (size_t)status;
		}
	}

	sfree (rates);

	if (str_len <= 2) {
		log_err ("c_psql_write: Failed to stringify value list");
		return NULL;
	}

	/* overwrite the first comma */
	string[0] = '{';
	str_ptr[0] = '}';
	str_ptr[1] = '\0';

	return string;
} /* values_to_sqlarray */

static int c_psql_write (const data_set_t *ds, const value_list_t *vl,
		user_data_t *ud)
{
	c_psql_database_t *db;

	char time_str[32];
	char values_name_str[1024];
	char values_type_str[1024];
	char values_str[1024];

	const char *params[9];

	int success = 0;
	int i;

	if ((ud == NULL) || (ud->data == NULL)) {
		log_err ("c_psql_write: Invalid user data.");
		return -1;
	}

	db = ud->data;
	assert (db->database != NULL);
	assert (db->writers != NULL);

	if (cdtime_to_iso8601 (time_str, sizeof (time_str), vl->time) == 0) {
		log_err ("c_psql_write: Failed to convert time to ISO 8601 format");
		return -1;
	}

	if (values_name_to_sqlarray (ds,
				values_name_str, sizeof (values_name_str)) == NULL)
		return -1;

#define VALUE_OR_NULL(v) ((((v) == NULL) || (*(v) == '\0')) ? NULL : (v))

	params[0] = time_str;
	params[1] = vl->host;
	params[2] = vl->plugin;
	params[3] = VALUE_OR_NULL(vl->plugin_instance);
	params[4] = vl->type;
	params[5] = VALUE_OR_NULL(vl->type_instance);
	params[6] = values_name_str;

#undef VALUE_OR_NULL

	if( db->expire_delay > 0 && vl->time < (cdtime() - vl->interval - db->expire_delay) ) {
		log_info ("c_psql_write: Skipped expired value @ %s - %s/%s-%s/%s-%s/%s", 
			params[0], params[1], params[2], params[3], params[4], params[5], params[6] );
		return 0;
        }

	pthread_mutex_lock (&db->db_lock);

	if (0 != c_psql_check_connection (db)) {
		pthread_mutex_unlock (&db->db_lock);
		return -1;
	}

	if ((db->commit_interval > 0)
			&& (db->next_commit == 0))
		c_psql_begin (db);

	for (i = 0; i < db->writers_num; ++i) {
		c_psql_writer_t *writer;
		PGresult *res;

		writer = db->writers[i];

		if (values_type_to_sqlarray (ds,
					values_type_str, sizeof (values_type_str),
					writer->store_rates) == NULL) {
			pthread_mutex_unlock (&db->db_lock);
			return -1;
		}

		if (values_to_sqlarray (ds, vl,
					values_str, sizeof (values_str),
					writer->store_rates) == NULL) {
			pthread_mutex_unlock (&db->db_lock);
			return -1;
		}

		params[7] = values_type_str;
		params[8] = values_str;

		res = PQexecParams (db->conn, writer->statement,
				STATIC_ARRAY_SIZE (params), NULL,
				(const char *const *)params,
				NULL, NULL, /* return text data */ 0);

		if ((PGRES_COMMAND_OK != PQresultStatus (res))
				&& (PGRES_TUPLES_OK != PQresultStatus (res))) {
			PQclear (res);

			if ((CONNECTION_OK != PQstatus (db->conn))
					&& (0 == c_psql_check_connection (db))) {
				/* try again */
				res = PQexecParams (db->conn, writer->statement,
						STATIC_ARRAY_SIZE (params), NULL,
						(const char *const *)params,
						NULL, NULL, /* return text data */ 0);

				if ((PGRES_COMMAND_OK == PQresultStatus (res))
						|| (PGRES_TUPLES_OK == PQresultStatus (res))) {
					PQclear (res);
					success = 1;
					continue;
				}
			}

			log_err ("Failed to execute SQL query: %s",
					PQerrorMessage (db->conn));
			log_info ("SQL query was: '%s', "
					"params: %s, %s, %s, %s, %s, %s, %s, %s",
					writer->statement,
					params[0], params[1], params[2], params[3],
					params[4], params[5], params[6], params[7]);

			/* this will abort any current transaction -> restart */
			if (db->next_commit > 0)
				c_psql_commit (db);

			pthread_mutex_unlock (&db->db_lock);
			return -1;
		}

		PQclear (res);
		success = 1;
	}

	if ((db->next_commit > 0)
			&& (cdtime () > db->next_commit))
		c_psql_commit (db);

	pthread_mutex_unlock (&db->db_lock);

	if (! success)
		return -1;
	return 0;
} /* c_psql_write */

/* We cannot flush single identifiers as all we do is to commit the currently
 * running transaction, thus making sure that all written data is actually
 * visible to everybody. */
static int c_psql_flush (cdtime_t timeout,
		__attribute__((unused)) const char *ident,
		user_data_t *ud)
{
	c_psql_database_t **dbs = databases;
	size_t dbs_num = databases_num;
	size_t i;

	if ((ud != NULL) && (ud->data != NULL)) {
		dbs = (void *)&ud->data;
		dbs_num = 1;
	}

	for (i = 0; i < dbs_num; ++i) {
		c_psql_database_t *db = dbs[i];

		/* don't commit if the timeout is larger than the regular commit
		 * interval as in that case all requested data has already been
		 * committed */
		if ((db->next_commit > 0) && (db->commit_interval > timeout))
			c_psql_commit (db);
	}
	return 0;
} /* c_psql_flush */

static int c_psql_shutdown (void)
{
	size_t i = 0;

	_Bool had_flush = 0;

	plugin_unregister_read_group ("postgresql");

	for (i = 0; i < databases_num; ++i) {
		c_psql_database_t *db = databases[i];

		if (db->writers_num > 0) {
			char cb_name[DATA_MAX_NAME_LEN];
			ssnprintf (cb_name, sizeof (cb_name), "postgresql-%s",
					db->database);

			if (! had_flush) {
				plugin_unregister_flush ("postgresql");
				had_flush = 1;
			}

			plugin_unregister_flush (cb_name);
			plugin_unregister_write (cb_name);
		}

		sfree (db);
	}

	udb_query_free (queries, queries_num);
	queries = NULL;
	queries_num = 0;

	sfree (writers);
	writers = NULL;
	writers_num = 0;

	sfree (databases);
	databases = NULL;
	databases_num = 0;

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
	else if (0 == strcasecmp (param_str, "instance"))
		data->params[data->params_num] = C_PSQL_PARAM_INSTANCE;
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

static int config_add_writer (oconfig_item_t *ci,
		c_psql_writer_t *src_writers, size_t src_writers_num,
		c_psql_writer_t ***dst_writers, size_t *dst_writers_num)
{
	char *name;

	size_t i;

	if ((ci == NULL) || (dst_writers == NULL) || (dst_writers_num == NULL))
		return -1;

	if ((ci->values_num != 1)
			|| (ci->values[0].type != OCONFIG_TYPE_STRING)) {
		log_err ("`Writer' expects a single string argument.");
		return 1;
	}

	name = ci->values[0].value.string;

	for (i = 0; i < src_writers_num; ++i) {
		c_psql_writer_t **tmp;

		if (strcasecmp (name, src_writers[i].name) != 0)
			continue;

		tmp = (c_psql_writer_t **)realloc (*dst_writers,
				sizeof (**dst_writers) * (*dst_writers_num + 1));
		if (tmp == NULL) {
			log_err ("Out of memory.");
			return -1;
		}

		tmp[*dst_writers_num] = src_writers + i;

		*dst_writers = tmp;
		++(*dst_writers_num);
		break;
	}

	if (i >= src_writers_num) {
		log_err ("No such writer: `%s'", name);
		return -1;
	}

	return 0;
} /* config_add_writer */

static int c_psql_config_writer (oconfig_item_t *ci)
{
	c_psql_writer_t *writer;
	c_psql_writer_t *tmp;

	int status = 0;
	int i;

	if ((ci->values_num != 1)
			|| (ci->values[0].type != OCONFIG_TYPE_STRING)) {
		log_err ("<Writer> expects a single string argument.");
		return 1;
	}

	tmp = (c_psql_writer_t *)realloc (writers,
			sizeof (*writers) * (writers_num + 1));
	if (tmp == NULL) {
		log_err ("Out of memory.");
		return -1;
	}

	writers = tmp;
	writer  = writers + writers_num;
	++writers_num;

	writer->name = sstrdup (ci->values[0].value.string);
	writer->statement = NULL;
	writer->store_rates = 1;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (strcasecmp ("Statement", c->key) == 0)
			status = cf_util_get_string (c, &writer->statement);
		else if (strcasecmp ("StoreRates", c->key) == 0)
			status = cf_util_get_boolean (c, &writer->store_rates);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}

	if (status != 0) {
		sfree (writer->statement);
		sfree (writer->name);
		sfree (writer);
		return status;
	}

	return 0;
} /* c_psql_config_writer */

static int c_psql_config_database (oconfig_item_t *ci)
{
	c_psql_database_t *db;

	char cb_name[DATA_MAX_NAME_LEN];
	struct timespec cb_interval = { 0, 0 };
	user_data_t ud;

	static _Bool have_flush = 0;

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
		else if (0 == strcasecmp (c->key, "Instance"))
			cf_util_get_string (c, &db->instance);
		else if (0 == strcasecmp (c->key, "SSLMode"))
			cf_util_get_string (c, &db->sslmode);
		else if (0 == strcasecmp (c->key, "KRBSrvName"))
			cf_util_get_string (c, &db->krbsrvname);
		else if (0 == strcasecmp (c->key, "Service"))
			cf_util_get_string (c, &db->service);
		else if (0 == strcasecmp (c->key, "Query"))
			udb_query_pick_from_list (c, queries, queries_num,
					&db->queries, &db->queries_num);
		else if (0 == strcasecmp (c->key, "Writer"))
			config_add_writer (c, writers, writers_num,
					&db->writers, &db->writers_num);
		else if (0 == strcasecmp (c->key, "Interval"))
			cf_util_get_cdtime (c, &db->interval);
		else if (strcasecmp ("CommitInterval", c->key) == 0)
			cf_util_get_cdtime (c, &db->commit_interval);
		else if (strcasecmp ("ExpireDelay", c->key) == 0)
			cf_util_get_cdtime (c, &db->expire_delay);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}

	/* If no `Query' options were given, add the default queries.. */
	if ((db->queries_num == 0) && (db->writers_num == 0)){
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

	ssnprintf (cb_name, sizeof (cb_name), "postgresql-%s", db->instance);

	if (db->queries_num > 0) {
		CDTIME_T_TO_TIMESPEC (db->interval, &cb_interval);

		++db->ref_cnt;
		plugin_register_complex_read ("postgresql", cb_name, c_psql_read,
				/* interval = */ (db->interval > 0) ? &cb_interval : NULL,
				&ud);
	}
	if (db->writers_num > 0) {
		++db->ref_cnt;
		plugin_register_write (cb_name, c_psql_write, &ud);

		if (! have_flush) {
			/* flush all */
			plugin_register_flush ("postgresql",
					c_psql_flush, /* user data = */ NULL);
			have_flush = 1;
		}

		/* flush this connection only */
		++db->ref_cnt;
		plugin_register_flush (cb_name, c_psql_flush, &ud);
	}
	else if (db->commit_interval > 0) {
		log_warn ("Database '%s': You do not have any writers assigned to "
				"this database connection. Setting 'CommitInterval' does "
				"not have any effect.", db->database);
	}
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
		else if (0 == strcasecmp (c->key, "Writer"))
			c_psql_config_writer (c);
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
