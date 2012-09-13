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

#include "utils_cmd_flush.h"
#include "utils_cmd_getval.h"
#include "utils_cmd_listval.h"
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
 * Private variables
 */
/* valid configuration file keys */
static const char *config_keys[] =
{
	"SocketFile",
	"SocketGroup",
	"SocketPerms",
	"DeleteSocket"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int loop = 0;

/* socket configuration */
static int   sock_fd    = -1;
static char *sock_file  = NULL;
static char *sock_group = NULL;
static int   sock_perms = S_IRWXU | S_IRWXG;
static _Bool delete_socket = 0;

static pthread_t listen_thread = (pthread_t) 0;

/*
 * Functions
 */
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
	sstrncpy (sa.sun_path, (sock_file != NULL) ? sock_file : US_DEFAULT_PATH,
			sizeof (sa.sun_path));

	DEBUG ("unixsock plugin: socket path = %s", sa.sun_path);

	if (delete_socket)
	{
		errno = 0;
		status = unlink (sa.sun_path);
		if ((status != 0) && (errno != ENOENT))
		{
			char errbuf[1024];
			WARNING ("unixsock plugin: Deleting socket file \"%s\" failed: %s",
					sa.sun_path,
					sstrerror (errno, errbuf, sizeof (errbuf)));
		}
		else if (status == 0)
		{
			INFO ("unixsock plugin: Successfully deleted socket file \"%s\".",
					sa.sun_path);
		}
	}

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

static void *us_handle_client (void *arg)
{
	int fdin;
	int fdout;
	FILE *fhin, *fhout;

	fdin = *((int *) arg);
	free (arg);
	arg = NULL;

	DEBUG ("unixsock plugin: us_handle_client: Reading from fd #%i", fdin);

	fdout = dup (fdin);
	if (fdout < 0)
	{
		char errbuf[1024];
		ERROR ("unixsock plugin: dup failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fdin);
		pthread_exit ((void *) 1);
	}

	fhin  = fdopen (fdin, "r");
	if (fhin == NULL)
	{
		char errbuf[1024];
		ERROR ("unixsock plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fdin);
		close (fdout);
		pthread_exit ((void *) 1);
	}

	fhout = fdopen (fdout, "w");
	if (fhout == NULL)
	{
		char errbuf[1024];
		ERROR ("unixsock plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (fhin); /* this closes fdin as well */
		close (fdout);
		pthread_exit ((void *) 1);
	}

	/* change output buffer to line buffered mode */
	if (setvbuf (fhout, NULL, _IOLBF, 0) != 0)
	{
		char errbuf[1024];
		ERROR ("unixsock plugin: setvbuf failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (fhin);
		fclose (fhout);
		pthread_exit ((void *) 1);
	}

	while (42)
	{
		char buffer[1024];
		char buffer_copy[1024];
		char *fields[128];
		int   fields_num;
		int   len;

		errno = 0;
		if (fgets (buffer, sizeof (buffer), fhin) == NULL)
		{
			if (errno != 0)
			{
				char errbuf[1024];
				WARNING ("unixsock plugin: failed to read from socket #%i: %s",
						fileno (fhin),
						sstrerror (errno, errbuf, sizeof (errbuf)));
			}
			break;
		}

		len = strlen (buffer);
		while ((len > 0)
				&& ((buffer[len - 1] == '\n') || (buffer[len - 1] == '\r')))
			buffer[--len] = '\0';

		if (len == 0)
			continue;

		sstrncpy (buffer_copy, buffer, sizeof (buffer_copy));

		fields_num = strsplit (buffer_copy, fields,
				sizeof (fields) / sizeof (fields[0]));
		if (fields_num < 1)
		{
			fprintf (fhout, "-1 Internal error\n");
			fclose (fhin);
			fclose (fhout);
			pthread_exit ((void *) 1);
		}

		if (strcasecmp (fields[0], "getval") == 0)
		{
			handle_getval (fhout, buffer);
		}
		else if (strcasecmp (fields[0], "putval") == 0)
		{
			handle_putval (fhout, buffer);
		}
		else if (strcasecmp (fields[0], "listval") == 0)
		{
			handle_listval (fhout, buffer);
		}
		else if (strcasecmp (fields[0], "putnotif") == 0)
		{
			handle_putnotif (fhout, buffer);
		}
		else if (strcasecmp (fields[0], "flush") == 0)
		{
			handle_flush (fhout, buffer);
		}
		else
		{
			if (fprintf (fhout, "-1 Unknown command: %s\n", fields[0]) < 0)
			{
				char errbuf[1024];
				WARNING ("unixsock plugin: failed to write to socket #%i: %s",
						fileno (fhout),
						sstrerror (errno, errbuf, sizeof (errbuf)));
				break;
			}
		}
	} /* while (fgets) */

	DEBUG ("unixsock plugin: us_handle_client: Exiting..");
	fclose (fhin);
	fclose (fhout);

	pthread_exit ((void *) 0);
	return ((void *) 0);
} /* void *us_handle_client */

static void *us_server_thread (void __attribute__((unused)) *arg)
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
		char *new_sock_file = strdup (val);
		if (new_sock_file == NULL)
			return (1);

		sfree (sock_file);
		sock_file = new_sock_file;
	}
	else if (strcasecmp (key, "SocketGroup") == 0)
	{
		char *new_sock_group = strdup (val);
		if (new_sock_group == NULL)
			return (1);

		sfree (sock_group);
		sock_group = new_sock_group;
	}
	else if (strcasecmp (key, "SocketPerms") == 0)
	{
		sock_perms = (int) strtol (val, NULL, 8);
	}
	else if (strcasecmp (key, "DeleteSocket") == 0)
	{
		if (IS_TRUE (val))
			delete_socket = 1;
		else
			delete_socket = 0;
	}
	else
	{
		return (-1);
	}

	return (0);
} /* int us_config */

static int us_init (void)
{
	static int have_init = 0;

	int status;

	/* Initialize only once. */
	if (have_init != 0)
		return (0);
	have_init = 1;

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
	plugin_unregister_shutdown ("unixsock");

	return (0);
} /* int us_shutdown */

void module_register (void)
{
	plugin_register_config ("unixsock", us_config,
			config_keys, config_keys_num);
	plugin_register_init ("unixsock", us_init);
	plugin_register_shutdown ("unixsock", us_shutdown);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 sts=4 tw=78 : */
