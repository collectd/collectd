/**
 * collectd - src/netcmd.c
 * Copyright (C) 2007-2009  Florian octo Forster
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
#include <sys/poll.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/un.h>

#include <grp.h>

#define NC_DEFAULT_PORT "25826"

/*
 * Private data structures
 */
struct socket_entry_s
{
	char *node;
	char *service;
	int fd;
};
typedef struct socket_entry_s socket_entry_t;

/*
 * Private variables
 */
/* valid configuration file keys */
static const char *config_keys[] =
{
	"Listen",
	"SocketPerms"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/* socket configuration */
static socket_entry_t *sockets = NULL;
static size_t          sockets_num;

static struct pollfd  *pollfd = NULL;
static size_t          pollfd_num;

static int       listen_thread_loop = 0;
static int       listen_thread_running = 0;
static pthread_t listen_thread;

static int unix_sock_perms = S_IRWXU | S_IRWXG;
static char *unix_sock_group = NULL;

static char **unix_sock_paths = NULL;
static size_t unix_sock_paths_num = 0;

/*
 * Functions
 */
static int nc_register_fd (int fd, const char *path) /* {{{ */
{
	struct pollfd *tmp;

	if (path != NULL)
	{
		char **tmp;

		tmp = realloc (unix_sock_paths,
				(unix_sock_paths_num + 1) * sizeof (*unix_sock_paths));
		if (tmp == NULL)
		{
			ERROR ("netcmd plugin: realloc failed.");
			return (-1);
		}
		unix_sock_paths = tmp;

		unix_sock_paths[unix_sock_paths_num] = sstrdup (path);
		unix_sock_paths_num++;
	}

	tmp = realloc (pollfd, (pollfd_num + 1) * sizeof (*pollfd));
	if (tmp == NULL)
	{
		ERROR ("netcmd plugin: realloc failed.");
		if (path != NULL)
		{
			unix_sock_paths_num--;
			sfree (unix_sock_paths[unix_sock_paths_num]);
		}

		return (-1);
	}
	pollfd = tmp;

	memset (&pollfd[pollfd_num], 0, sizeof (pollfd[pollfd_num]));
	pollfd[pollfd_num].fd = fd;
	pollfd[pollfd_num].events = POLLIN | POLLPRI;
	pollfd[pollfd_num].revents = 0;

	pollfd_num++;

	return (0);
} /* }}} int nc_register_fd */

static int nc_open_unix_socket (const char *path, /* {{{ */
		const char *group)
{
	struct sockaddr_un sa;
	int fd;
	char errbuf[1024];
	int status;

	if (path == NULL)
		return (-1);

	DEBUG ("netcmd plugin: nc_open_unix_socket (path = %s, group = %s);",
			(path != NULL) ? path : "(null)",
			(group != NULL) ? group : "(null)");
	if (strncasecmp ("unix:", path, strlen ("unix:")) == 0)
		path += strlen ("unix:");

	fd = socket (PF_UNIX, SOCK_STREAM, 0);
	if (fd < 0)
	{
		ERROR ("netcmd plugin: socket(2) failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	memset (&sa, '\0', sizeof (sa));
	sa.sun_family = AF_UNIX;
	sstrncpy (sa.sun_path, path, sizeof (sa.sun_path));
	/* unlink (sa.sun_path); */

	DEBUG ("netcmd plugin: socket path = %s", sa.sun_path);

	status = bind (fd, (struct sockaddr *) &sa, sizeof (sa));
	if (status != 0)
	{
		ERROR ("netcmd plugin: bind failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fd);
		fd = -1;
		return (-1);
	}

	/* FIXME: Copy unix_sock_perms stuff from unixsock. */
	chmod (sa.sun_path, unix_sock_perms);

	status = listen (fd, 8);
	if (status != 0)
	{
		ERROR ("netcmd plugin: listen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fd);
		fd = -1;
		return (-1);
	}

	/* If `group' is not NULL, `chown' the file. */
	while (group != NULL) /* {{{ */
	{
		struct group *g;
		struct group sg;
		char grbuf[2048];

		g = NULL;
		status = getgrnam_r (group, &sg, grbuf, sizeof (grbuf), &g);
		if (status != 0)
		{
			WARNING ("netcmd plugin: getgrnam_r (%s) failed: %s", group,
					sstrerror (errno, errbuf, sizeof (errbuf)));
			break;
		}

		if (g == NULL)
		{
			WARNING ("netcmd plugin: No such group: `%s'", group);
			break;
		}

		status = chown (sa.sun_path, (uid_t) -1, g->gr_gid);
		if (status != 0)
		{
			WARNING ("netcmd plugin: chown (%s, -1, %i) failed: %s",
					sa.sun_path, (int) g->gr_gid,
					sstrerror (errno, errbuf, sizeof (errbuf)));
		}

		break;
	} /* }}} while (group != NULL) */

	status = nc_register_fd (fd, sa.sun_path);
	if (status != 0)
	{
		close (fd);
		unlink (sa.sun_path);
		return (status);
	}

	return (0);
} /* }}} int nc_open_unix_socket */

static int nc_open_network_socket (const char *node, /* {{{ */
		const char *service)
{
	struct addrinfo ai_hints;
	struct addrinfo *ai_list;
	struct addrinfo *ai_ptr;
	int status;

	DEBUG ("netcmd plugin: nc_open_network_socket (node = %s, service = %s);",
			(node != NULL) ? node : "(null)",
			(service != NULL) ? service : "(null)");

	memset (&ai_hints, 0, sizeof (ai_hints));
#ifdef AI_PASSIVE
	ai_hints.ai_flags |= AI_PASSIVE;
#endif
#ifdef AI_ADDRCONFIG
	ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
	ai_hints.ai_family = AF_UNSPEC;
	ai_hints.ai_socktype = SOCK_STREAM;

	ai_list = NULL;

	if (service == NULL)
		service = NC_DEFAULT_PORT;

	status = getaddrinfo (node, service, &ai_hints, &ai_list);
	if (status != 0)
	{
		ERROR ("netcmd plugin: getaddrinfo failed: %s",
				gai_strerror (status));
		return (-1);
	}

	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		char errbuf[1024];
		int fd;

		fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype,
				ai_ptr->ai_protocol);
		if (fd < 0)
		{
			ERROR ("netcmd plugin: socket(2) failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			continue;
		}

		status = bind (fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
		if (status != 0)
		{
			close (fd);
			ERROR ("netcmd plugin: bind(2) failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			continue;
		}

		status = listen (fd, /* backlog = */ 8);
		if (status != 0)
		{
			close (fd);
			ERROR ("netcmd plugin: listen(2) failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			continue;
		}

		status = nc_register_fd (fd, /* path = */ NULL);
		if (status != 0)
		{
			close (fd);
			continue;
		}
	} /* for (ai_next) */

	freeaddrinfo (ai_list);

	return (0);
} /* }}} int nc_open_network_socket */

static int nc_open_socket (const char *node, /* {{{ */
		const char *service)
{
	if ((node != NULL)
			&& (strncasecmp ("unix:", node, strlen ("unix:")) == 0))
	{
		return (nc_open_unix_socket (node, service));
	}
	else
	{
		return (nc_open_network_socket (node, service));
	}
} /* }}} int nc_open_socket */

static void *nc_handle_client (void *arg) /* {{{ */
{
	int fd;
	FILE *fhin, *fhout;
	char errbuf[1024];

	fd = *((int *) arg);
	sfree (arg);

	DEBUG ("netcmd plugin: nc_handle_client: Reading from fd #%i", fd);

	fhin  = fdopen (fd, "r");
	if (fhin == NULL)
	{
		ERROR ("netcmd plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fd);
		pthread_exit ((void *) 1);
	}

	fhout = fdopen (fd, "w");
	if (fhout == NULL)
	{
		ERROR ("netcmd plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (fhin); /* this closes fd as well */
		pthread_exit ((void *) 1);
	}

	/* change output buffer to line buffered mode */
	if (setvbuf (fhout, NULL, _IOLBF, 0) != 0)
	{
		ERROR ("netcmd plugin: setvbuf failed: %s",
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
				WARNING ("netcmd plugin: failed to read from socket #%i: %s",
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
			close (fd);
			break;
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
				WARNING ("netcmd plugin: failed to write to socket #%i: %s",
						fileno (fhout),
						sstrerror (errno, errbuf, sizeof (errbuf)));
				break;
			}
		}
	} /* while (fgets) */

	DEBUG ("netcmd plugin: nc_handle_client: Exiting..");
	fclose (fhin);
	fclose (fhout);

	pthread_exit ((void *) 0);
	return ((void *) 0);
} /* }}} void *nc_handle_client */

static void *nc_server_thread (void __attribute__((unused)) *arg) /* {{{ */
{
	int  status;
	pthread_t th;
	pthread_attr_t th_attr;
	char errbuf[1024];
	size_t i;

	for (i = 0; i < sockets_num; i++)
		nc_open_socket (sockets[i].node, sockets[i].service);

	if (sockets_num == 0)
		nc_open_socket (NULL, NULL);

	if (pollfd_num == 0)
	{
		ERROR ("netcmd plugin: No sockets could be opened.");
		pthread_exit ((void *) -1);
	}

	while (listen_thread_loop != 0)
	{
		status = poll (pollfd, (nfds_t) pollfd_num, /* timeout = */ -1);
		if (status < 0)
		{
			if ((errno == EINTR) || (errno == EAGAIN))
				continue;

			ERROR ("netcmd plugin: poll(2) failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			listen_thread_loop = 0;
			continue;
		}

		for (i = 0; i < pollfd_num; i++)
		{
			int *client_fd;

			if (pollfd[i].revents == 0)
			{
				continue;
			}
			else if ((pollfd[i].revents & (POLLERR | POLLHUP | POLLNVAL))
					!= 0)
			{
				WARNING ("netcmd plugin: File descriptor %i failed.",
						pollfd[i].fd);
				close (pollfd[i].fd);
				pollfd[i].fd = -1;
				pollfd[i].events = 0;
				pollfd[i].revents = 0;
				continue;
			}
			pollfd[i].revents = 0;

			status = accept (pollfd[i].fd,
					/* sockaddr = */ NULL,
					/* sockaddr_len = */ NULL);
			if (status < 0)
			{
				if (errno == EINTR)
					continue;

				ERROR ("netcmd plugin: accept failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
				continue;
			}

			client_fd = malloc (sizeof (*client_fd));
			if (client_fd == NULL)
			{
				ERROR ("netcmd plugin: malloc failed.");
				close (status);
				continue;
			}
			*client_fd = status;

			DEBUG ("Spawning child to handle connection on fd %i", *client_fd);

			pthread_attr_init (&th_attr);
			pthread_attr_setdetachstate (&th_attr, PTHREAD_CREATE_DETACHED);

			status = pthread_create (&th, &th_attr, nc_handle_client,
					client_fd);
			if (status != 0)
			{
				WARNING ("netcmd plugin: pthread_create failed: %s",
						sstrerror (errno, errbuf, sizeof (errbuf)));
				close (*client_fd);
				continue;
			}
		}
	} /* while (listen_thread_loop) */

	for (i = 0; i < pollfd_num; i++)
	{
		if (pollfd[i].fd < 0)
			continue;

		close (pollfd[i].fd);
		pollfd[i].fd = -1;
		pollfd[i].events = 0;
		pollfd[i].revents = 0;
	}

	sfree (pollfd);
	pollfd_num = 0;

	for (i = 0; i < unix_sock_paths_num; i++)
	{
		DEBUG ("netcmd plugin: Unlinking `%s'.",
				unix_sock_paths[i]);
		unlink (unix_sock_paths[i]);
		sfree (unix_sock_paths[i]);
	}
	sfree (unix_sock_paths);

	return ((void *) 0);
} /* }}} void *nc_server_thread */

static int nc_config (const char *key, const char *val)
{
	if (strcasecmp ("Listen", key) == 0)
	{
		socket_entry_t *tmp;

		tmp = realloc (sockets, (sockets_num + 1) * sizeof (*sockets));
		if (tmp == NULL)
		{
			ERROR ("netcmd plugin: realloc failed.");
			return (-1);
		}
		sockets = tmp;
		tmp = sockets + sockets_num;

		memset (tmp, 0, sizeof (*tmp));
		tmp->node = sstrdup (val);
		tmp->service = strchr (tmp->node, ' ');
		if (tmp->service != NULL)
		{
			while ((tmp->service[0] == ' ') || (tmp->service[0] == '\t'))
			{
				tmp->service[0] = 0;
				tmp->service++;
			}
			if (tmp->service[0] == 0)
				tmp->service = NULL;
		}
		tmp->fd = -1;

		sockets_num++;
	}
	else if (strcasecmp (key, "SocketGroup") == 0)
	{
		char *new_sock_group = strdup (val);
		if (new_sock_group == NULL)
			return (1);

		sfree (unix_sock_group);
		unix_sock_group = new_sock_group;
	}
	else if (strcasecmp (key, "SocketPerms") == 0)
	{
		int tmp;

		errno = 0;
		tmp = (int) strtol (val, NULL, 8);
		if ((errno != 0) || (tmp == 0))
			return (1);
		unix_sock_perms = tmp;
	}
	else
	{
		return (-1);
	}

	return (0);
} /* int nc_config */

static int nc_init (void)
{
	static int have_init = 0;

	int status;

	/* Initialize only once. */
	if (have_init != 0)
		return (0);
	have_init = 1;

	listen_thread_loop = 1;

	status = pthread_create (&listen_thread, NULL, nc_server_thread, NULL);
	if (status != 0)
	{
		char errbuf[1024];
		listen_thread_loop = 0;
		listen_thread_running = 0;
		ERROR ("netcmd plugin: pthread_create failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	listen_thread_running = 1;
	return (0);
} /* int nc_init */

static int nc_shutdown (void)
{
	void *ret;

	listen_thread_loop = 0;

	if (listen_thread != (pthread_t) 0)
	{
		pthread_kill (listen_thread, SIGTERM);
		pthread_join (listen_thread, &ret);
		listen_thread = (pthread_t) 0;
	}

	plugin_unregister_init ("netcmd");
	plugin_unregister_shutdown ("netcmd");

	return (0);
} /* int nc_shutdown */

void module_register (void)
{
	plugin_register_config ("netcmd", nc_config,
			config_keys, config_keys_num);
	plugin_register_init ("netcmd", nc_init);
	plugin_register_shutdown ("netcmd", nc_shutdown);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 sts=4 tw=78 fdm=marker : */
