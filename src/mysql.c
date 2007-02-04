/**
 * collectd - src/mysql.c
 * Copyright (C) 2006,2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#ifdef HAVE_MYSQL_MYSQL_H
#include <mysql/mysql.h>
#endif

#if COLLECT_LIBMYSQL
# define MYSQL_HAVE_READ 1
#else
# define MYSQL_HAVE_READ 0
#endif

/* TODO: Understand `Select_*' and possibly do that stuff as well.. */

static data_source_t data_source_counter[1] =
{
	{"value", DS_TYPE_COUNTER, 0, NAN}
};

static data_set_t ds_commands =
{
	"mysql_commands", 1, data_source_counter
};

static data_set_t ds_handler =
{
	"mysql_handler", 1, data_source_counter
};

static data_source_t data_source_qcache[5] =
{
	{"hits",             DS_TYPE_COUNTER, 0, NAN},
	{"inserts",          DS_TYPE_COUNTER, 0, NAN},
	{"not_cached",       DS_TYPE_COUNTER, 0, NAN},
	{"lowmem_prunes",    DS_TYPE_COUNTER, 0, NAN},
	{"queries_in_cache", DS_TYPE_GAUGE,   0, NAN}
};

static data_set_t ds_qcache =
{
	"mysql_qcache", 5, data_source_qcache
};

static data_source_t data_source_threads[4] =
{
	{"running",   DS_TYPE_GAUGE,   0, NAN},
	{"connected", DS_TYPE_GAUGE,   0, NAN},
	{"cached",    DS_TYPE_GAUGE,   0, NAN},
	{"created",   DS_TYPE_COUNTER, 0, NAN}
};

static data_set_t ds_threads =
{
	"mysql_threads", 4, data_source_threads
};

static data_source_t data_source_octets[2] =
{
	{"rx", DS_TYPE_COUNTER, 0, 4294967295.0},
	{"tx", DS_TYPE_COUNTER, 0, 4294967295.0}
};

static data_set_t ds_octets =
{
	"mysql_octets", 2, data_source_octets
};

#if MYSQL_HAVE_READ
static const char *config_keys[] =
{
	"Host",
	"User",
	"Password",
	"Database",
	NULL
};
static int config_keys_num = 4;

static char *host = "localhost";
static char *user;
static char *pass;
static char *db = NULL;

static MYSQL *getconnection (void)
{
	static MYSQL *con;
	static int    state;

	static int wait_for = 0;
	static int wait_increase = 60;

	int step;

	if (state != 0)
	{
		int err;
		if ((err = mysql_ping (con)) != 0)
		{
			syslog (LOG_WARNING, "mysql_ping failed: %s", mysql_error (con));
			state = 0;
		}
		else
		{
			state = 1;
			return (con);
		}
	}

	step = atoi (COLLECTD_STEP);

	if (wait_for > 0)
	{
		wait_for -= step;
		return (NULL);
	}

	wait_for = wait_increase;
	wait_increase *= 2;
	if (wait_increase > 86400)
		wait_increase = 86400;

	if ((con = mysql_init (con)) == NULL)
	{
		syslog (LOG_ERR, "mysql_init failed: %s", mysql_error (con));
		state = 0;
		return (NULL);
	}

	if (mysql_real_connect (con, host, user, pass, db, 0, NULL, 0) == NULL)
	{
		syslog (LOG_ERR, "mysql_real_connect failed: %s", mysql_error (con));
		state = 0;
		return (NULL);
	}
	else
	{
		state = 1;
		wait_for = 0;
		wait_increase = 60;
		return (con);
	}
} /* static MYSQL *getconnection (void) */

static int config (const char *key, const char *value)
{
	if (strcasecmp (key, "host") == 0)
		return ((host = strdup (value)) == NULL ? 1 : 0);
	else if (strcasecmp (key, "user") == 0)
		return ((user = strdup (value)) == NULL ? 1 : 0);
	else if (strcasecmp (key, "password") == 0)
		return ((pass = strdup (value)) == NULL ? 1 : 0);
	else if (strcasecmp (key, "database") == 0)
		return ((db = strdup (value)) == NULL ? 1 : 0);
	else
		return (-1);
}

static void counter_submit (const char *type, const char *type_instance,
		counter_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname);
	strcpy (vl.plugin, "mysql");
	strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (type, &vl);
} /* void counter_submit */

static void qcache_submit (counter_t hits, counter_t inserts,
		counter_t not_cached, counter_t lowmem_prunes,
		gauge_t queries_in_cache)
{
	value_t values[5];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = hits;
	values[1].counter = inserts;
	values[2].counter = not_cached;
	values[3].counter = lowmem_prunes;
	values[4].gauge   = queries_in_cache;

	vl.values = values;
	vl.values_len = 5;
	vl.time = time (NULL);
	strcpy (vl.host, hostname);
	strcpy (vl.plugin, "mysql");

	plugin_dispatch_values ("mysql_qcache", &vl);
} /* void qcache_submit */

static void threads_submit (gauge_t running, gauge_t connected, gauge_t cached,
		counter_t created)
{
	value_t values[4];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge   = running;
	values[1].gauge   = connected;
	values[2].gauge   = cached;
	values[3].counter = created;

	vl.values = values;
	vl.values_len = 4;
	vl.time = time (NULL);
	strcpy (vl.host, hostname);
	strcpy (vl.plugin, "mysql");

	plugin_dispatch_values ("mysql_threads", &vl);
} /* void threads_submit */

static void traffic_submit (counter_t rx, counter_t tx)
{
	value_t values[2];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = rx;
	values[1].counter = tx;

	vl.values = values;
	vl.values_len = 2;
	vl.time = time (NULL);
	strcpy (vl.host, hostname);
	strcpy (vl.plugin, "mysql");

	plugin_dispatch_values ("mysql_octets", &vl);
} /* void traffic_submit */

static int mysql_read (void)
{
	MYSQL     *con;
	MYSQL_RES *res;
	MYSQL_ROW  row;
	char      *query;
	int        query_len;
	int        field_num;

	unsigned long long qcache_hits          = 0ULL;
	unsigned long long qcache_inserts       = 0ULL;
	unsigned long long qcache_not_cached    = 0ULL;
	unsigned long long qcache_lowmem_prunes = 0ULL;
	int qcache_queries_in_cache = -1;

	int threads_running   = -1;
	int threads_connected = -1;
	int threads_cached    = -1;
	unsigned long long threads_created = 0ULL;

	unsigned long long traffic_incoming = 0ULL;
	unsigned long long traffic_outgoing = 0ULL;

	/* An error message will have been printed in this case */
	if ((con = getconnection ()) == NULL)
		return (-1);

	query = "SHOW STATUS";
	if (mysql_get_server_version (con) >= 50002)
		query = "SHOW GLOBAL STATUS";

	query_len = strlen (query);

	if (mysql_real_query (con, query, query_len))
	{
		syslog (LOG_ERR, "mysql_real_query failed: %s\n",
				mysql_error (con));
		return (-1);
	}

	if ((res = mysql_store_result (con)) == NULL)
	{
		syslog (LOG_ERR, "mysql_store_result failed: %s\n",
				mysql_error (con));
		return (-1);
	}

	field_num = mysql_num_fields (res);
	while ((row = mysql_fetch_row (res)))
	{
		char *key;
		unsigned long long val;

		key = row[0];
		val = atoll (row[1]);

		if (strncmp (key, "Com_", 4) == 0)
		{
			if (val == 0ULL)
				continue;

			/* Ignore `prepared statements' */
			if (strncmp (key, "Com_stmt_", 9) != 0)
				counter_submit ("mysql_commands", key + 4, val);
		}
		else if (strncmp (key, "Handler_", 8) == 0)
		{
			if (val == 0ULL)
				continue;

			counter_submit ("mysql_handler", key + 8, val);
		}
		else if (strncmp (key, "Qcache_", 7) == 0)
		{
			if (strcmp (key, "Qcache_hits") == 0)
				qcache_hits = val;
			else if (strcmp (key, "Qcache_inserts") == 0)
				qcache_inserts = val;
			else if (strcmp (key, "Qcache_not_cached") == 0)
				qcache_not_cached = val;
			else if (strcmp (key, "Qcache_lowmem_prunes") == 0)
				qcache_lowmem_prunes = val;
			else if (strcmp (key, "Qcache_queries_in_cache") == 0)
				qcache_queries_in_cache = (int) val;
		}
		else if (strncmp (key, "Bytes_", 6) == 0)
		{
			if (strcmp (key, "Bytes_received") == 0)
				traffic_incoming += val;
			else if (strcmp (key, "Bytes_sent") == 0)
				traffic_outgoing += val;
		}
		else if (strncmp (key, "Threads_", 8) == 0)
		{
			if (strcmp (key, "Threads_running") == 0)
				threads_running = (int) val;
			else if (strcmp (key, "Threads_connected") == 0)
				threads_connected = (int) val;
			else if (strcmp (key, "Threads_cached") == 0)
				threads_cached = (int) val;
			else if (strcmp (key, "Threads_created") == 0)
				threads_created = val;
		}
	}
	mysql_free_result (res); res = NULL;

	if ((qcache_hits != 0ULL)
			|| (qcache_inserts != 0ULL)
			|| (qcache_not_cached != 0ULL)
			|| (qcache_lowmem_prunes != 0ULL))
		qcache_submit (qcache_hits, qcache_inserts, qcache_not_cached,
				qcache_lowmem_prunes, qcache_queries_in_cache);

	if (threads_created != 0ULL)
		threads_submit (threads_running, threads_connected,
				threads_cached, threads_created);

	traffic_submit  (traffic_incoming, traffic_outgoing);

	/* mysql_close (con); */

	return (0);
} /* int mysql_read */
#endif /* MYSQL_HAVE_READ */

void module_register (void)
{
	plugin_register_data_set (&ds_commands);
	plugin_register_data_set (&ds_handler);
	plugin_register_data_set (&ds_qcache);
	plugin_register_data_set (&ds_threads);
	plugin_register_data_set (&ds_octets);

#if MYSQL_HAVE_READ
	plugin_register_config ("mysql", config, config_keys, config_keys_num);
	plugin_register_read ("mysql", mysql_read);
#endif /* MYSQL_HAVE_READ */
} /* void module_register */
