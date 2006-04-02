/**
 * collectd - src/mysql.c
 * Copyright (C) 2006  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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

#define MODULE_NAME "mysql"

#if COLLECT_LIBMYSQL
# define MYSQL_HAVE_READ 1
#else
# define MYSQL_HAVE_READ 0
#endif

#define BUFSIZE 512

static char *host = "localhost";
static char *user;
static char *pass;
static char *db = NULL;

#if MYSQL_HAVE_READ
static char  init_suceeded = 0;
#endif

/* TODO
 * understand `Select_*' and possibly do that stuff as well..
 */

static char *commands_file = "mysql/mysql_commands-%s.rrd";
static char *handler_file  = "mysql/mysql_handler-%s.rrd";
static char *qcache_file   = "mysql/mysql_qcache.rrd";
static char *threads_file  = "mysql/mysql_threads.rrd";
static char *traffic_file  = "traffic-mysql.rrd";

static char *commands_ds_def[] =
{
	"DS:value:COUNTER:25:0:U",
	NULL
};
static int commands_ds_num = 1;

static char *handler_ds_def[] =
{
	"DS:value:COUNTER:25:0:U",
	NULL
};
static int handler_ds_num = 1;

static char *qcache_ds_def[] =
{
	"DS:hits:COUNTER:25:0:U",
	"DS:inserts:COUNTER:25:0:U",
	"DS:not_cached:COUNTER:25:0:U",
	"DS:lowmem_prunes:COUNTER:25:0:U",
	"DS:queries_in_cache:GAUGE:25:0:U",
	NULL
};
static int qcache_ds_num = 5;

static char *threads_ds_def[] =
{
	"DS:running:GAUGE:25:0:U",
	"DS:connected:GAUGE:25:0:U",
	"DS:cached:GAUGE:25:0:U",
	"DS:created:COUNTER:25:0:U",
	NULL
};
static int threads_ds_num = 4;

static char *traffic_ds_def[] =
{
	"DS:incoming:COUNTER:25:0:U",
	"DS:outgoing:COUNTER:25:0:U",
	NULL
};
static int traffic_ds_num = 2;

static char *config_keys[] =
{
	"Host",
	"User",
	"Password",
	"Database",
	NULL
};
static int config_keys_num = 4;

#if MYSQL_HAVE_READ
static MYSQL *getconnection (void)
{
	static MYSQL *con;
	static int    state;

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
		return (con);
	}
} /* static MYSQL *getconnection (void) */
#endif /* MYSQL_HAVE_READ */

static void init (void)
{
#if MYSQL_HAVE_READ
	if (getconnection () != NULL)
		init_suceeded = 1;
	else
	{
		syslog (LOG_ERR, "The `mysql' plugin will be disabled because `init' failed to connect to `%s'", host);
		init_suceeded = 0;
	}
#endif /* MYSQL_HAVE_READ */

	return;
}

static int config (char *key, char *value)
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

static void commands_write (char *host, char *inst, char *val)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, commands_file, inst) >= BUFSIZE)
		return;

	rrd_update_file (host, buf, val, commands_ds_def, commands_ds_num);
}

static void handler_write (char *host, char *inst, char *val)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, handler_file, inst) >= BUFSIZE)
		return;

	rrd_update_file (host, buf, val, handler_ds_def, handler_ds_num);
}

static void qcache_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, qcache_file, val,
			qcache_ds_def, qcache_ds_num);
}

static void threads_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, threads_file, val,
			threads_ds_def, threads_ds_num);
}

static void traffic_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, traffic_file, val,
			traffic_ds_def, traffic_ds_num);
}

#if MYSQL_HAVE_READ
static void commands_submit (char *inst, unsigned long long value)
{
	char buf[BUFSIZE];
	int  status;

	status = snprintf (buf, BUFSIZE, "%u:%llu", (unsigned int) curtime, value);

	if (status < 0)
	{
		syslog (LOG_ERR, "snprintf failed");
		return;
	}
	else if (status >= BUFSIZE)
	{
		syslog (LOG_WARNING, "snprintf was truncated");
		return;
	}

	plugin_submit ("mysql_commands", inst, buf);
}

static void handler_submit (char *inst, unsigned long long value)
{
	char buf[BUFSIZE];
	int  status;

	status = snprintf (buf, BUFSIZE, "%u:%llu", (unsigned int) curtime, value);

	if (status < 0)
	{
		syslog (LOG_ERR, "snprintf failed");
		return;
	}
	else if (status >= BUFSIZE)
	{
		syslog (LOG_WARNING, "snprintf was truncated");
		return;
	}

	plugin_submit ("mysql_handler", inst, buf);
}

static void qcache_submit (unsigned long long hits, unsigned long long inserts,
		unsigned long long not_cached, unsigned long long lowmem_prunes,
		int queries_in_cache)
{
	char buf[BUFSIZE];
	int  status;

	status = snprintf (buf, BUFSIZE, "%u:%llu:%llu:%llu:%llu:%i",
			(unsigned int) curtime, hits, inserts, not_cached,
			lowmem_prunes, queries_in_cache);

	if (status < 0)
	{
		syslog (LOG_ERR, "snprintf failed");
		return;
	}
	else if (status >= BUFSIZE)
	{
		syslog (LOG_WARNING, "snprintf was truncated");
		return;
	}

	plugin_submit ("mysql_qcache", "-", buf);
}

static void threads_submit (int running, int connected, int cached,
		unsigned long long created)
{
	char buf[BUFSIZE];
	int  status;

	status = snprintf (buf, BUFSIZE, "%u:%i:%i:%i:%llu",
			(unsigned int) curtime,
			running, connected, cached, created);

	if (status < 0)
	{
		syslog (LOG_ERR, "snprintf failed");
		return;
	}
	else if (status >= BUFSIZE)
	{
		syslog (LOG_WARNING, "snprintf was truncated");
		return;
	}

	plugin_submit ("mysql_threads", "-", buf);
}

static void traffic_submit (unsigned long long incoming,
		unsigned long long outgoing)
{
	char buf[BUFSIZE];
	int  status;

	status = snprintf (buf, BUFSIZE, "%u:%llu:%llu", (unsigned int) curtime,
			incoming, outgoing);

	if (status < 0)
	{
		syslog (LOG_ERR, "snprintf failed");
		return;
	}
	else if (status >= BUFSIZE)
	{
		syslog (LOG_WARNING, "snprintf was truncated");
		return;
	}

	plugin_submit ("mysql_traffic", "-", buf);
}

static void mysql_read (void)
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

	if (init_suceeded == 0)
		return;

	/* An error message will have been printed in this case */
	if ((con = getconnection ()) == NULL)
		return;

	query = "SHOW STATUS";
	if (mysql_get_server_version (con) >= 50002)
		query = "SHOW GLOBAL STATUS";

	query_len = strlen (query);

	if (mysql_real_query (con, query, query_len))
	{
		syslog (LOG_ERR, "mysql_real_query failed: %s\n",
				mysql_error (con));
		return;
	}

	if ((res = mysql_store_result (con)) == NULL)
	{
		syslog (LOG_ERR, "mysql_store_result failed: %s\n",
				mysql_error (con));
		return;
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
				commands_submit (key + 4, val);
		}
		else if (strncmp (key, "Handler_", 8) == 0)
		{
			if (val == 0ULL)
				continue;

			handler_submit (key + 8, val);
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

	return;
}
#else
# define mysql_read NULL
#endif /* MYSQL_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, init, mysql_read, NULL);
	plugin_register ("mysql_commands", NULL, NULL, commands_write);
	plugin_register ("mysql_handler",  NULL, NULL, handler_write);
	plugin_register ("mysql_qcache",   NULL, NULL, qcache_write);
	plugin_register ("mysql_threads",  NULL, NULL, threads_write);
	plugin_register ("mysql_traffic",  NULL, NULL, traffic_write);
	cf_register (MODULE_NAME, config, config_keys, config_keys_num);
}

#undef BUFSIZE
#undef MODULE_NAME
