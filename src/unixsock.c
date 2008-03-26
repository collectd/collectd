/**
 * collectd - src/unixsock.c
 * Copyright (C) 2007,2008  Florian octo Forster
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

#include "utils_cmd_putval.h"
#include "utils_cmd_putnotif.h"

/* Folks without pthread will need to disable this plugin. */
#include <pthread.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <grp.h>

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX sizeof (((struct sockaddr_un *)0)->sun_path)
#endif

#define US_DEFAULT_PATH LOCALSTATEDIR"/run/"PACKAGE_NAME"-unixsock"

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

static int loop = 0;

/* socket configuration */
static int   sock_fd    = -1;
static char *sock_file  = NULL;
static char *sock_group = NULL;
static int   sock_perms = S_IRWXU | S_IRWXG;

static pthread_t listen_thread = (pthread_t) 0;

/* Linked list and auxilliary variables for saving values */
static value_cache_t   *cache_head = NULL;
static pthread_mutex_t  cache_lock = PTHREAD_MUTEX_INITIALIZER;
static time_t           cache_oldest = -1;

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

static int cache_insert (const data_set_t *ds, const value_list_t *vl)
{
	/* We're called from `cache_update' so we don't need to lock the mutex */
	value_cache_t *vc;
	int i;

	DEBUG ("unixsock plugin: cache_insert: ds->type = %s; ds->ds_num = %i;"
			" vl->values_len = %i;",
			ds->type, ds->ds_num, vl->values_len);
#if COLLECT_DEBUG
	assert (ds->ds_num == vl->values_len);
#else
	if (ds->ds_num != vl->values_len)
	{
		ERROR ("unixsock plugin: ds->type = %s: (ds->ds_num = %i) != "
				"(vl->values_len = %i)",
				ds->type, ds->ds_num, vl->values_len);
		return (-1);
	}
#endif

	vc = (value_cache_t *) malloc (sizeof (value_cache_t));
	if (vc == NULL)
	{
		char errbuf[1024];
		pthread_mutex_unlock (&cache_lock);
		ERROR ("unixsock plugin: malloc failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	vc->gauge = (gauge_t *) malloc (sizeof (gauge_t) * vl->values_len);
	if (vc->gauge == NULL)
	{
		char errbuf[1024];
		pthread_mutex_unlock (&cache_lock);
		ERROR ("unixsock plugin: malloc failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		free (vc);
		return (-1);
	}

	vc->counter = (counter_t *) malloc (sizeof (counter_t) * vl->values_len);
	if (vc->counter == NULL)
	{
		char errbuf[1024];
		pthread_mutex_unlock (&cache_lock);
		ERROR ("unixsock plugin: malloc failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		free (vc->gauge);
		free (vc);
		return (-1);
	}

	if (FORMAT_VL (vc->name, sizeof (vc->name), vl, ds))
	{
		pthread_mutex_unlock (&cache_lock);
		ERROR ("unixsock plugin: FORMAT_VL failed.");
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
	if ((vc->time < cache_oldest) || (-1 == cache_oldest))
		cache_oldest = vc->time;

	pthread_mutex_unlock (&cache_lock);
	return (0);
} /* int cache_insert */

static int cache_update (const data_set_t *ds, const value_list_t *vl)
{
	char name[4*DATA_MAX_NAME_LEN];;
	value_cache_t *vc;
	int i;

	if (FORMAT_VL (name, sizeof (name), vl, ds) != 0)
		return (-1);

	pthread_mutex_lock (&cache_lock);

	vc = cache_search (name);

	/* pthread_mutex_lock is called by cache_insert. */
	if (vc == NULL)
		return (cache_insert (ds, vl));

	assert (vc->values_num == ds->ds_num);
	assert (vc->values_num == vl->values_len);

	/* Avoid floating-point exceptions due to division by zero. */
	if (vc->time >= vl->time)
	{
		pthread_mutex_unlock (&cache_lock);
		ERROR ("unixsock plugin: vc->time >= vl->time. vc->time = %u; "
				"vl->time = %u; vl = %s;",
				(unsigned int) vc->time, (unsigned int) vl->time,
				name);
		return (-1);
	} /* if (vc->time >= vl->time) */

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

		if (isnan (vc->gauge[i])
				|| (!isnan (ds->ds[i].min) && (vc->gauge[i] < ds->ds[i].min))
				|| (!isnan (ds->ds[i].max) && (vc->gauge[i] > ds->ds[i].max)))
			vc->gauge[i] = NAN;
	} /* for i = 0 .. ds->ds_num */

	vc->ds = ds;
	vc->time = vl->time;

	if ((vc->time < cache_oldest) || (-1 == cache_oldest))
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
} /* void cache_flush */

static int us_open_socket (void)
{
	struct sockaddr_un sa;
	int status;

	sock_fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (sock_fd < 0)
	{
		char errbuf[1024];
		ERROR ("unixsock plugin: socket failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	memset (&sa, '\0', sizeof (sa));
	sa.sun_family = AF_UNIX;
	strncpy (sa.sun_path, (sock_file != NULL) ? sock_file : US_DEFAULT_PATH,
			sizeof (sa.sun_path) - 1);
	/* unlink (sa.sun_path); */

	DEBUG ("unixsock plugin: socket path = %s", sa.sun_path);

	status = bind (sock_fd, (struct sockaddr *) &sa, sizeof (sa));
	if (status != 0)
	{
		char errbuf[1024];
		sstrerror (errno, errbuf, sizeof (errbuf));
		ERROR ("unixsock plugin: bind failed: %s", errbuf);
		close (sock_fd);
		sock_fd = -1;
		return (-1);
	}

	chmod (sa.sun_path, sock_perms);

	status = listen (sock_fd, 8);
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("unixsock plugin: listen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
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
			char errbuf[1024];
			WARNING ("unixsock plugin: getgrnam_r (%s) failed: %s", grpname,
					sstrerror (errno, errbuf, sizeof (errbuf)));
			break;
		}
		if (g == NULL)
		{
			WARNING ("unixsock plugin: No such group: `%s'",
					grpname);
			break;
		}

		if (chown ((sock_file != NULL) ? sock_file : US_DEFAULT_PATH,
					(uid_t) -1, g->gr_gid) != 0)
		{
			char errbuf[1024];
			WARNING ("unixsock plugin: chown (%s, -1, %i) failed: %s",
					(sock_file != NULL) ? sock_file : US_DEFAULT_PATH,
					(int) g->gr_gid,
					sstrerror (errno, errbuf, sizeof (errbuf)));
		}
	} while (0);

	return (0);
} /* int us_open_socket */

static int us_handle_getval (FILE *fh, char **fields, int fields_num)
{
	char *hostname;
	char *plugin;
	char *plugin_instance;
	char *type;
	char *type_instance;
	char  name[4*DATA_MAX_NAME_LEN];
	value_cache_t *vc;
	int   status;
	int   i;

	if (fields_num != 2)
	{
		DEBUG ("unixsock plugin: Wrong number of fields: %i", fields_num);
		fprintf (fh, "-1 Wrong number of fields: Got %i, expected 2.\n",
				fields_num);
		fflush (fh);
		return (-1);
	}
	DEBUG ("unixsock plugin: Got query for `%s'", fields[1]);

	status = parse_identifier (fields[1], &hostname,
			&plugin, &plugin_instance,
			&type, &type_instance);
	if (status != 0)
	{
		DEBUG ("unixsock plugin: Cannot parse `%s'", fields[1]);
		fprintf (fh, "-1 Cannot parse identifier.\n");
		fflush (fh);
		return (-1);
	}

	status = format_name (name, sizeof (name),
			hostname, plugin, plugin_instance, type, type_instance);
	if (status != 0)
	{
		fprintf (fh, "-1 format_name failed.\n");
		return (-1);
	}

	pthread_mutex_lock (&cache_lock);

	DEBUG ("vc = cache_search (%s)", name);
	vc = cache_search (name);

	if (vc == NULL)
	{
		DEBUG ("Did not find cache entry.");
		fprintf (fh, "-1 No such value");
	}
	else
	{
		DEBUG ("Found cache entry.");
		fprintf (fh, "%i", vc->values_num);
		for (i = 0; i < vc->values_num; i++)
		{
			fprintf (fh, " %s=", vc->ds->ds[i].name);
			if (isnan (vc->gauge[i]))
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

static int us_handle_listval (FILE *fh, char **fields, int fields_num)
{
	char buffer[1024];
	char **value_list = NULL;
	int value_list_len = 0;
	value_cache_t *entry;
	int i;

	if (fields_num != 1)
	{
		DEBUG ("unixsock plugin: us_handle_listval: "
				"Wrong number of fields: %i", fields_num);
		fprintf (fh, "-1 Wrong number of fields: Got %i, expected 1.\n",
				fields_num);
		fflush (fh);
		return (-1);
	}

	pthread_mutex_lock (&cache_lock);

	for (entry = cache_head; entry != NULL; entry = entry->next)
	{
		char **tmp;

		snprintf (buffer, sizeof (buffer), "%u %s\n",
				(unsigned int) entry->time, entry->name);
		buffer[sizeof (buffer) - 1] = '\0';
		
		tmp = realloc (value_list, sizeof (char *) * (value_list_len + 1));
		if (tmp == NULL)
			continue;
		value_list = tmp;

		value_list[value_list_len] = strdup (buffer);

		if (value_list[value_list_len] != NULL)
			value_list_len++;
	} /* for (entry) */

	pthread_mutex_unlock (&cache_lock);

	DEBUG ("unixsock plugin: us_handle_listval: value_list_len = %i", value_list_len);
	fprintf (fh, "%i Values found\n", value_list_len);
	for (i = 0; i < value_list_len; i++)
		fputs (value_list[i], fh);
	fflush (fh);

	return (0);
} /* int us_handle_listval */

static void *us_handle_client (void *arg)
{
	int fd;
	FILE *fhin, *fhout;
	char buffer[1024];
	char *fields[128];
	int   fields_num;

	fd = *((int *) arg);
	free (arg);
	arg = NULL;

	DEBUG ("Reading from fd #%i", fd);

	fhin  = fdopen (fd, "r");
	if (fhin == NULL)
	{
		char errbuf[1024];
		ERROR ("unixsock plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fd);
		pthread_exit ((void *) 1);
	}

	fhout = fdopen (fd, "w");
	if (fhout == NULL)
	{
		char errbuf[1024];
		ERROR ("unixsock plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (fhin); /* this closes fd as well */
		pthread_exit ((void *) 1);
	}

	while (fgets (buffer, sizeof (buffer), fhin) != NULL)
	{
		int len;

		len = strlen (buffer);
		while ((len > 0)
				&& ((buffer[len - 1] == '\n') || (buffer[len - 1] == '\r')))
			buffer[--len] = '\0';

		if (len == 0)
			continue;

		DEBUG ("fgets -> buffer = %s; len = %i;", buffer, len);

		fields_num = strsplit (buffer, fields,
				sizeof (fields) / sizeof (fields[0]));

		if (fields_num < 1)
		{
			close (fd);
			break;
		}

		if (strcasecmp (fields[0], "getval") == 0)
		{
			us_handle_getval (fhout, fields, fields_num);
		}
		else if (strcasecmp (fields[0], "putval") == 0)
		{
			handle_putval (fhout, fields, fields_num);
		}
		else if (strcasecmp (fields[0], "listval") == 0)
		{
			us_handle_listval (fhout, fields, fields_num);
		}
		else if (strcasecmp (fields[0], "putnotif") == 0)
		{
			handle_putnotif (fhout, fields, fields_num);
		}
		else
		{
			fprintf (fhout, "-1 Unknown command: %s\n", fields[0]);
			fflush (fhout);
		}
	} /* while (fgets) */

	DEBUG ("Exiting..");
	fclose (fhin);
	fclose (fhout);

	pthread_exit ((void *) 0);
	return ((void *) 0);
} /* void *us_handle_client */

static void *us_server_thread (void *arg)
{
	int  status;
	int *remote_fd;
	pthread_t th;
	pthread_attr_t th_attr;

	if (us_open_socket () != 0)
		pthread_exit ((void *) 1);

	while (loop != 0)
	{
		DEBUG ("unixsock plugin: Calling accept..");
		status = accept (sock_fd, NULL, NULL);
		if (status < 0)
		{
			char errbuf[1024];

			if (errno == EINTR)
				continue;

			ERROR ("unixsock plugin: accept failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (sock_fd);
			sock_fd = -1;
			pthread_exit ((void *) 1);
		}

		remote_fd = (int *) malloc (sizeof (int));
		if (remote_fd == NULL)
		{
			char errbuf[1024];
			WARNING ("unixsock plugin: malloc failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (status);
			continue;
		}
		*remote_fd = status;

		DEBUG ("Spawning child to handle connection on fd #%i", *remote_fd);

		pthread_attr_init (&th_attr);
		pthread_attr_setdetachstate (&th_attr, PTHREAD_CREATE_DETACHED);

		status = pthread_create (&th, &th_attr, us_handle_client, (void *) remote_fd);
		if (status != 0)
		{
			char errbuf[1024];
			WARNING ("unixsock plugin: pthread_create failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (*remote_fd);
			free (remote_fd);
			continue;
		}
	} /* while (loop) */

	close (sock_fd);
	sock_fd = -1;

	status = unlink ((sock_file != NULL) ? sock_file : US_DEFAULT_PATH);
	if (status != 0)
	{
		char errbuf[1024];
		NOTICE ("unixsock plugin: unlink (%s) failed: %s",
				(sock_file != NULL) ? sock_file : US_DEFAULT_PATH,
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}

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

	loop = 1;

	status = pthread_create (&listen_thread, NULL, us_server_thread, NULL);
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("unixsock plugin: pthread_create failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	return (0);
} /* int us_init */

static int us_shutdown (void)
{
	void *ret;

	loop = 0;

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
	cache_flush (2 * interval_g);

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
