/**
 * collectd - src/unixsock.c
 * Copyright (C) 2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_debug.h"

/* Folks without pthread will need to disable this plugin. */
#include <pthread.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/poll.h>

#include <grp.h>

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX sizeof (((struct sockaddr_un *)0)->sun_path)
#endif

#define US_DEFAULT_PATH PREFIX"/var/run/"PACKAGE_NAME"-unixsock"

/*
 * Private data structures
 */
/* linked list of cached values */
typedef struct value_cache_s
{
	char       name[4*DATA_MAX_NAME_LEN];
	int        values_num;
	gauge_t   *gauge;
	counter_t *counter;
	const data_set_t *ds;
	time_t     time;
	struct value_cache_s *next;
} value_cache_t;

/*
 * Private variables
 */
/* valid configuration file keys */
static const char *config_keys[] =
{
	"SocketFile",
	"SocketGroup",
	"SocketPerms",
	NULL
};
static int config_keys_num = 3;

/* socket configuration */
static int   sock_fd    = -1;
static char *sock_file  = NULL;
static char *sock_group = NULL;
static int   sock_perms = S_IRWXU | S_IRWXG;

static pthread_t listen_thread = (pthread_t) 0;

/* Linked list and auxilliary variables for saving values */
static value_cache_t   *cache_head = NULL;
static pthread_mutex_t  cache_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned int     cache_oldest = UINT_MAX;

/*
 * Functions
 */
static value_cache_t *cache_search (const char *name)
{
	value_cache_t *vc;

	for (vc = cache_head; vc != NULL; vc = vc->next)
	{
		if (strcmp (vc->name, name) == 0)
			break;
	} /* for vc = cache_head .. NULL */

	return (vc);
} /* value_cache_t *cache_search */

static int cache_alloc_name (char *ret, int ret_len,
		const char *hostname,
		const char *plugin, const char *plugin_instance,
		const char *type, const char *type_instance)
{
	int  status;

	assert (plugin != NULL);
	assert (type != NULL);

	if ((plugin_instance == NULL) || (strlen (plugin_instance) == 0))
	{
		if ((type_instance == NULL) || (strlen (type_instance) == 0))
			status = snprintf (ret, ret_len, "%s/%s/%s",
					hostname, plugin, type);
		else
			status = snprintf (ret, ret_len, "%s/%s/%s-%s",
					hostname, plugin, type, type_instance);
	}
	else
	{
		if ((type_instance == NULL) || (strlen (type_instance) == 0))
			status = snprintf (ret, ret_len, "%s/%s-%s/%s",
					hostname, plugin, plugin_instance, type);
		else
			status = snprintf (ret, ret_len, "%s/%s-%s/%s-%s",
					hostname, plugin, plugin_instance, type, type_instance);
	}

	if ((status < 1) || (status >= ret_len))
		return (-1);
	return (0);
} /* int cache_alloc_name */

static int cache_insert (const data_set_t *ds, const value_list_t *vl)
{
	/* We're called from `cache_update' so we don't need to lock the mutex */
	value_cache_t *vc;
	int i;

	DBG ("ds->ds_num = %i; vl->values_len = %i;",
			ds->ds_num, vl->values_len);
	assert (ds->ds_num == vl->values_len);

	vc = (value_cache_t *) malloc (sizeof (value_cache_t));
	if (vc == NULL)
	{
		pthread_mutex_unlock (&cache_lock);
		syslog (LOG_ERR, "unixsock plugin: malloc failed: %s",
				strerror (errno));
		return (-1);
	}

	vc->gauge = (gauge_t *) malloc (sizeof (gauge_t) * vl->values_len);
	if (vc->gauge == NULL)
	{
		pthread_mutex_unlock (&cache_lock);
		syslog (LOG_ERR, "unixsock plugin: malloc failed: %s",
				strerror (errno));
		free (vc);
		return (-1);
	}

	vc->counter = (counter_t *) malloc (sizeof (counter_t) * vl->values_len);
	if (vc->counter == NULL)
	{
		pthread_mutex_unlock (&cache_lock);
		syslog (LOG_ERR, "unixsock plugin: malloc failed: %s",
				strerror (errno));
		free (vc->gauge);
		free (vc);
		return (-1);
	}

	if (cache_alloc_name (vc->name, sizeof (vc->name),
				vl->host, vl->plugin, vl->plugin_instance,
				ds->type, vl->type_instance) != 0)
	{
		pthread_mutex_unlock (&cache_lock);
		syslog (LOG_ERR, "unixsock plugin: cache_alloc_name failed.");
		free (vc->counter);
		free (vc->gauge);
		free (vc);
		return (-1);
	}

	for (i = 0; i < ds->ds_num; i++)
	{
		if (ds->ds[i].type == DS_TYPE_COUNTER)
		{
			vc->gauge[i] = 0.0;
			vc->counter[i] = vl->values[i].counter;
		}
		else if (ds->ds[i].type == DS_TYPE_GAUGE)
		{
			vc->gauge[i] = vl->values[i].gauge;
			vc->counter[i] = 0;
		}
		else
		{
			vc->gauge[i] = 0.0;
			vc->counter[i] = 0;
		}
	}
	vc->values_num = ds->ds_num;
	vc->ds = ds;

	vc->next = cache_head;
	cache_head = vc;

	vc->time = vl->time;
	if (vc->time < cache_oldest)
		cache_oldest = vc->time;

	pthread_mutex_unlock (&cache_lock);
	return (0);
} /* int cache_insert */

static int cache_update (const data_set_t *ds, const value_list_t *vl)
{
	char name[4*DATA_MAX_NAME_LEN];;
	value_cache_t *vc;
	int i;

	if (cache_alloc_name (name, sizeof (name),
				vl->host,
				vl->plugin, vl->plugin_instance,
				ds->type, vl->type_instance) != 0)
		return (-1);

	pthread_mutex_lock (&cache_lock);

	vc = cache_search (name);

	if (vc == NULL)
		return (cache_insert (ds, vl));

	assert (vc->values_num == ds->ds_num);
	assert (vc->values_num == vl->values_len);

	/*
	 * Update the values. This is possibly a lot more that you'd expect
	 * because we honor min and max values and handle counter overflows here.
	 */
	for (i = 0; i < ds->ds_num; i++)
	{
		if (ds->ds[i].type == DS_TYPE_COUNTER)
		{
			if (vl->values[i].counter < vc->counter[i])
			{
				if (vl->values[i].counter <= 4294967295U)
				{
					vc->gauge[i] = ((4294967295U - vl->values[i].counter)
							+ vc->counter[i]) / (vl->time - vc->time);
				}
				else
				{
					vc->gauge[i] = ((18446744073709551615ULL - vl->values[i].counter)
						+ vc->counter[i]) / (vl->time - vc->time);
				}
			}
			else
			{
				vc->gauge[i] = (vl->values[i].counter - vc->counter[i])
					/ (vl->time - vc->time);
			}

			vc->counter[i] = vl->values[i].counter;
		}
		else if (ds->ds[i].type == DS_TYPE_GAUGE)
		{
			vc->gauge[i] = vl->values[i].gauge;
			vc->counter[i] = 0;
		}
		else
		{
			vc->gauge[i] = NAN;
			vc->counter[i] = 0;
		}

		if ((vc->gauge[i] == NAN)
				|| ((ds->ds[i].min != NAN) && (vc->gauge[i] < ds->ds[i].min))
				|| ((ds->ds[i].max != NAN) && (vc->gauge[i] > ds->ds[i].max)))
			vc->gauge[i] = NAN;
	} /* for i = 0 .. ds->ds_num */

	vc->ds = ds;
	vc->time = vl->time;

	if (vc->time < cache_oldest)
		cache_oldest = vc->time;

	pthread_mutex_unlock (&cache_lock);
	return (0);
} /* int cache_update */

static void cache_flush (int max_age)
{
	value_cache_t *this;
	value_cache_t *prev;
	time_t now;

	pthread_mutex_lock (&cache_lock);

	now = time (NULL);

	if ((now - cache_oldest) <= max_age)
	{
		pthread_mutex_unlock (&cache_lock);
		return;
	}
	
	cache_oldest = now;

	prev = NULL;
	this = cache_head;

	while (this != NULL)
	{
		if ((now - this->time) <= max_age)
		{
			if (this->time < cache_oldest)
				cache_oldest = this->time;

			prev = this;
			this = this->next;
			continue;
		}

		if (prev == NULL)
			cache_head = this->next;
		else
			prev->next = this->next;

		free (this->gauge);
		free (this->counter);
		free (this);

		if (prev == NULL)
			this = cache_head;
		else
			this = prev->next;
	} /* while (this != NULL) */

	pthread_mutex_unlock (&cache_lock);
} /* int cache_flush */

static int us_open_socket (void)
{
	struct sockaddr_un sa;
	int status;

	sock_fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (sock_fd < 0)
	{
		syslog (LOG_ERR, "unixsock plugin: socket failed: %s",
				strerror (errno));
		return (-1);
	}

	memset (&sa, '\0', sizeof (sa));
	sa.sun_family = AF_UNIX;
	strncpy (sa.sun_path, (sock_file != NULL) ? sock_file : US_DEFAULT_PATH,
			sizeof (sa.sun_path) - 1);
	/* unlink (sa.sun_path); */

	status = bind (sock_fd, (struct sockaddr *) &sa, sizeof (sa));
	if (status != 0)
	{
		DBG ("bind failed: %s; sa.sun_path = %s",
				strerror (errno), sa.sun_path);
		syslog (LOG_ERR, "unixsock plugin: bind failed: %s",
				strerror (errno));
		close (sock_fd);
		sock_fd = -1;
		return (-1);
	}

	status = listen (sock_fd, 8);
	if (status != 0)
	{
		syslog (LOG_ERR, "unixsock plugin: listen failed: %s",
				strerror (errno));
		close (sock_fd);
		sock_fd = -1;
		return (-1);
	}

	do
	{
		char *grpname;
		struct group *g;
		struct group sg;
		char grbuf[2048];

		grpname = (sock_group != NULL) ? sock_group : COLLECTD_GRP_NAME;
		g = NULL;

		status = getgrnam_r (grpname, &sg, grbuf, sizeof (grbuf), &g);
		if (status != 0)
		{
			syslog (LOG_WARNING, "unixsock plugin: getgrnam_r (%s) failed: %s",
					grpname, strerror (status));
			break;
		}
		if (g == NULL)
		{
			syslog (LOG_WARNING, "unixsock plugin: No such group: `%s'",
					grpname);
			break;
		}

		if (chown ((sock_file != NULL) ? sock_file : US_DEFAULT_PATH,
					(uid_t) -1, g->gr_gid) != 0)
		{
			syslog (LOG_WARNING, "unixsock plugin: chown (%s, -1, %i) failed: %s",
					(sock_file != NULL) ? sock_file : US_DEFAULT_PATH,
					(int) g->gr_gid,
					strerror (errno));
		}
	} while (0);

	return (0);
} /* int us_open_socket */

static int us_handle_getval (FILE *fh, char **fields, int fields_num)
{
	char *hostname = fields[1];
	char *plugin;
	char *plugin_instance;
	char *type;
	char *type_instance;
	char  name[4*DATA_MAX_NAME_LEN];
	value_cache_t *vc;
	int   status;
	int   i;

	if (fields_num != 2)
		return (-1);

	plugin = strchr (hostname, '/');
	if (plugin == NULL)
		return (-1);
	*plugin = '\0'; plugin++;

	type = strchr (plugin, '/');
	if (type == NULL)
		return (-1);
	*type = '\0'; type++;

	plugin_instance = strchr (plugin, '-');
	if (plugin_instance != NULL)
	{
		*plugin_instance = '\0';
		plugin_instance++;
	}

	type_instance = strchr (type, '-');
	if (type_instance != NULL)
	{
		*type_instance = '\0';
		type_instance++;
	}

	status = cache_alloc_name (name, sizeof (name),
			hostname, plugin, plugin_instance, type, type_instance);
	if (status != 0)
		return (-1);

	pthread_mutex_lock (&cache_lock);

	DBG ("vc = cache_search (%s)", name);
	vc = cache_search (name);

	if (vc == NULL)
	{
		DBG ("Did not find cache entry.");
		fprintf (fh, "-1 No such value");
	}
	else
	{
		DBG ("Found cache entry.");
		fprintf (fh, "%i", vc->values_num);
		for (i = 0; i < vc->values_num; i++)
		{
			fprintf (fh, " %s=", vc->ds->ds[i].name);
			if (vc->gauge[i] == NAN)
				fprintf (fh, "NaN");
			else
				fprintf (fh, "%12e", vc->gauge[i]);
		}
	}

	/* Free the mutex as soon as possible and definitely before flushing */
	pthread_mutex_unlock (&cache_lock);

	fprintf (fh, "\n");
	fflush (fh);

	return (0);
} /* int us_handle_getval */

static void *us_handle_client (void *arg)
{
	int fd;
	FILE *fh;
	char buffer[1024];
	char *fields[128];
	int   fields_num;

	fd = *((int *) arg);
	free (arg);
	arg = NULL;

	DBG ("Reading from fd #%i", fd);

	fh = fdopen (fd, "r+");
	if (fh == NULL)
	{
		syslog (LOG_ERR, "unixsock plugin: fdopen failed: %s",
				strerror (errno));
		close (fd);
		pthread_exit ((void *) 1);
	}

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		int len;

		len = strlen (buffer);
		while ((len > 0)
				&& ((buffer[len - 1] == '\n') || (buffer[len - 1] == '\r')))
			buffer[--len] = '\0';

		if (len == 0)
			continue;

		DBG ("fgets -> buffer = %s; len = %i;", buffer, len);

		fields_num = strsplit (buffer, fields,
				sizeof (fields) / sizeof (fields[0]));

		if (fields_num < 1)
		{
			close (fd);
			break;
		}

		if (strcasecmp (fields[0], "getval") == 0)
		{
			us_handle_getval (fh, fields, fields_num);
		}
		else
		{
			fprintf (fh, "Unknown command: %s\n", fields[0]);
			fflush (fh);
		}
	} /* while (fgets) */

	DBG ("Exiting..");
	close (fd);

	pthread_exit ((void *) 0);
} /* void *us_handle_client */

static void *us_server_thread (void *arg)
{
	int  status;
	int *remote_fd;
	pthread_t th;
	pthread_attr_t th_attr;

	if (us_open_socket () != 0)
		pthread_exit ((void *) 1);

	while (42)
	{
		DBG ("Calling accept..");
		status = accept (sock_fd, NULL, NULL);
		if (status < 0)
		{
			if (errno == EINTR)
				continue;

			syslog (LOG_ERR, "unixsock plugin: accept failed: %s",
					strerror (errno));
			close (sock_fd);
			sock_fd = -1;
			pthread_exit ((void *) 1);
		}

		remote_fd = (int *) malloc (sizeof (int));
		if (remote_fd == NULL)
		{
			syslog (LOG_WARNING, "unixsock plugin: malloc failed: %s",
					strerror (errno));
			close (status);
			continue;
		}
		*remote_fd = status;

		DBG ("Spawning child to handle connection on fd #%i", *remote_fd);

		pthread_attr_init (&th_attr);
		pthread_attr_setdetachstate (&th_attr, PTHREAD_CREATE_DETACHED);

		status = pthread_create (&th, &th_attr, us_handle_client, (void *) remote_fd);
		if (status != 0)
		{
			syslog (LOG_WARNING, "unixsock plugin: pthread_create failed: %s",
					strerror (status));
			close (*remote_fd);
			free (remote_fd);
			continue;
		}
	} /* while (42) */

	return ((void *) 0);
} /* void *us_server_thread */

static int us_config (const char *key, const char *val)
{
	if (strcasecmp (key, "SocketFile") == 0)
	{
		sfree (sock_file);
		sock_file = strdup (val);
	}
	else if (strcasecmp (key, "SocketGroup") == 0)
	{
		sfree (sock_group);
		sock_group = strdup (val);
	}
	else if (strcasecmp (key, "SocketPerms") == 0)
	{
		sock_perms = (int) strtol (val, NULL, 8);
	}
	else
	{
		return (-1);
	}

	return (0);
} /* int us_config */

static int us_init (void)
{
	int status;

	status = pthread_create (&listen_thread, NULL, us_server_thread, NULL);
	if (status != 0)
	{
		syslog (LOG_ERR, "unixsock plugin: pthread_create failed: %s",
				strerror (status));
		return (-1);
	}

	return (0);
} /* int us_init */

static int us_shutdown (void)
{
	void *ret;

	if (listen_thread != (pthread_t) 0)
	{
		pthread_kill (listen_thread, SIGTERM);
		pthread_join (listen_thread, &ret);
		listen_thread = (pthread_t) 0;
	}

	plugin_unregister_init ("unixsock");
	plugin_unregister_write ("unixsock");
	plugin_unregister_shutdown ("unixsock");

	return (0);
} /* int us_shutdown */

static int us_write (const data_set_t *ds, const value_list_t *vl)
{
	cache_update (ds, vl);
	cache_flush (2 * atoi (COLLECTD_STEP));

	return (0);
}

void module_register (void)
{
	plugin_register_config ("unixsock", us_config,
			config_keys, config_keys_num);
	plugin_register_init ("unixsock", us_init);
	plugin_register_write ("unixsock", us_write);
	plugin_register_shutdown ("unixsock", us_shutdown);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 sts=4 tw=78 : */
