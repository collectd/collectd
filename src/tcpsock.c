/**
 * collectd - src/tcpsock.c
 * Copyright (C) 2015 Claudius Zingerli
 * most of the code is based on unixsock plugin:
 * Copyright (C) 2007,2008  Florian octo Forster
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
 *   Claudius Zingerli <collectd-tcpsockmail at zeuz.ch>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include "utils_cmd_flush.h"
#include "utils_cmd_getval.h"
#include "utils_cmd_getthreshold.h"
#include "utils_cmd_listval.h"
#include "utils_cmd_putval.h"
#include "utils_cmd_putnotif.h"

/* Folks without pthread will need to disable this plugin. */
#include <pthread.h>

#include <sys/socket.h>
#include <sys/stat.h>
#include <netdb.h>

#include <grp.h>

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX sizeof (((struct sockaddr_un *)0)->sun_path)
#endif

#define US_DEFAULT_PATH LOCALSTATEDIR"/run/"PACKAGE_NAME"-tcpsock"

/*
 * Private variables
 */
/* valid configuration file keys */
static const char *config_keys[] =
{
	"Listen",
	"Port",
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int g_loop_active = 0;

/* socket configuration */
static int   g_sock_fd    = -1;
static char *g_listen_addr  = NULL;
static char *g_listen_port  = NULL;
static int   g_sock_backlog = 10;
static int   g_sock_type  = SOCK_STREAM;

static pthread_t g_listen_thread = (pthread_t) 0;

/*
 * Functions
 */
static int tcps_open_socket (void)
{
	struct addrinfo hints, *result;
	memset(&hints, 0, sizeof(struct addrinfo));

	hints.ai_flags    = 0;
	hints.ai_socktype = g_sock_type;
	hints.ai_family   = AF_UNSPEC;

	int rc = getaddrinfo(g_listen_addr, g_listen_port, &hints, &result);
	if (rc != 0)
	{
		ERROR ("getaddrinfo(p_host, p_service, &hints, &result);");
		return -1;
	}

	struct addrinfo *now_result = result;
	while (now_result)
	{
		g_sock_fd = socket(now_result->ai_family, now_result->ai_socktype, now_result->ai_protocol);
		if (g_sock_fd == -1)
		{
			now_result = now_result->ai_next;
			continue;
		}

#if HAVE_DECL_SO_REUSEPORT
		int one = 1;
		if (setsockopt(g_sock_fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) != 0)
		{
			ERROR("setsockopt(f_socket, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one): %s", strerror(errno));
			return -1;
		}
#endif
		if (now_result->ai_family == AF_INET6)
		{
			//Allow listening on IPv4 and IPv6
			int zero = 0;
			if (setsockopt(g_sock_fd, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&zero, sizeof(zero)))
			{
				ERROR("setsockopt(f_socket, IPPROTO_IPV6, IPV6_V6ONLY, (void *)&zero, sizeof(zero)): %s", strerror(errno));
				return -1;
			}
		}


		if (bind(g_sock_fd, now_result->ai_addr, now_result->ai_addrlen) == 0)
		{
			break;
		}

		close(g_sock_fd);
		g_sock_fd = -1;
		now_result = now_result->ai_next;
	}
	if (g_sock_fd == -1)
	{
		freeaddrinfo(result);
		ERROR("binding socket");
		return -1;
	}

	if (listen(g_sock_fd, g_sock_backlog) == -1)
	{
		ERROR("listen(f_socket, p_backlog): %s", strerror(errno));
		return -1;
	}
#if 0
	if (status != 0)
	{
		char errbuf[1024];
		sstrerror (errno, errbuf, sizeof (errbuf));
		ERROR ("tcpsock plugin: bind failed: %s", errbuf);
		close (g_sock_fd);
		g_sock_fd = -1;
		return (-1);
	}
#endif

	return 0;
} /* int tcps_open_socket */

static void *tcps_handle_client (void *p_arg)
{
	int fdin;
	int fdout;
	FILE *fhin, *fhout;

	fdin = *((int *) p_arg);
	free (p_arg);
	p_arg = NULL;

	DEBUG ("tcpsock plugin: tcps_handle_client: Reading from fd #%i", fdin);

	fdout = dup (fdin);
	if (fdout < 0)
	{
		char errbuf[1024];
		ERROR ("tcpsock plugin: dup failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fdin);
		pthread_exit ((void *) 1);
	}

	fhin  = fdopen (fdin, "r");
	if (fhin == NULL)
	{
		char errbuf[1024];
		ERROR ("tcpsock plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fdin);
		close (fdout);
		pthread_exit ((void *) 1);
		return ((void *) 1);
	}

	fhout = fdopen (fdout, "w");
	if (fhout == NULL)
	{
		char errbuf[1024];
		ERROR ("tcpsock plugin: fdopen failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (fhin); /* this closes fdin as well */
		close (fdout);
		pthread_exit ((void *) 1);
		return ((void *) 1);
	}

	/* change output buffer to line buffered mode */
	if (setvbuf (fhout, NULL, _IOLBF, 0) != 0)
	{
		char errbuf[1024];
		ERROR ("tcpsock plugin: setvbuf failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (fhin);
		fclose (fhout);
		pthread_exit ((void *) 1);
		return ((void *) 0);
	}

	while (88)
	{
		char buffer[1024];
		char buffer_copy[1024];
		char *fields[128];
		int   fields_num;
		int   len;

		errno = 0;
		if (fgets (buffer, sizeof (buffer), fhin) == NULL)
		{
			if ((errno == EINTR) || (errno == EAGAIN))
			{
				continue;
			}

			if (errno != 0)
			{
				char errbuf[1024];
				WARNING ("tcpsock plugin: failed to read from socket #%i: %s",
						fileno (fhin),
						sstrerror (errno, errbuf, sizeof (errbuf)));
			}
			break;
		}

		len = strlen (buffer);
		while ((len > 0)
				&& ((buffer[len - 1] == '\n') || (buffer[len - 1] == '\r')))
		{
			buffer[--len] = '\0';
		}

		if (len == 0)
		{
			continue;
		}

		sstrncpy (buffer_copy, buffer, sizeof (buffer_copy));

		fields_num = strsplit (buffer_copy, fields,
								sizeof (fields) / sizeof (fields[0]));
		if (fields_num < 1)
		{
			fprintf (fhout, "-1 Internal error\n");
			fclose (fhin);
			fclose (fhout);
			pthread_exit ((void *) 1);
			return ((void *) 1);
		}

		if (strcasecmp (fields[0], "getval") == 0)
		{
			handle_getval (fhout, buffer);
		} else if (strcasecmp (fields[0], "getthreshold") == 0)
		{
			handle_getthreshold (fhout, buffer);
		} else if (strcasecmp (fields[0], "putval") == 0)
		{
			handle_putval (fhout, buffer);
		} else if (strcasecmp (fields[0], "listval") == 0)
		{
			handle_listval (fhout, buffer);
		} else if (strcasecmp (fields[0], "putnotif") == 0)
		{
			handle_putnotif (fhout, buffer);
		} else if (strcasecmp (fields[0], "flush") == 0)
		{
			handle_flush (fhout, buffer);
		} else {
			if (fprintf (fhout, "-1 Unknown command: %s\n", fields[0]) < 0)
			{
				char errbuf[1024];
				WARNING ("tcpsock plugin: failed to write to socket #%i: %s",
						fileno (fhout),
						sstrerror (errno, errbuf, sizeof (errbuf)));
				break;
			}
		}
	} /* while (fgets) */

	DEBUG ("tcpsock plugin: tcps_handle_client: Exiting..");
	fclose (fhin);
	fclose (fhout);

	pthread_exit ((void *) 0);
	return ((void *) 0);

} /* void *tcps_handle_client */

static void *tcps_server_thread (void __attribute__((unused)) *p_arg)
{
	int  status;
	int *remote_fd;
	pthread_t th;
	pthread_attr_t th_attr;

	pthread_attr_init (&th_attr);
	pthread_attr_setdetachstate (&th_attr, PTHREAD_CREATE_DETACHED);

	if (tcps_open_socket () != 0)
	{
		pthread_exit ((void *) 1);
	}

	while (g_loop_active != 0)
	{
		DEBUG ("tcpsock plugin: Calling accept..");
		status = accept (g_sock_fd, NULL, NULL);
		if (status < 0)
		{
			char errbuf[1024];

			if (errno == EINTR)
			{
				continue;
			}

			ERROR ("tcpsock plugin: accept failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (g_sock_fd);
			g_sock_fd = -1;
			pthread_attr_destroy (&th_attr);
			pthread_exit ((void *) 1);
		}

		remote_fd = (int *) malloc (sizeof (int));
		if (remote_fd == NULL)
		{
			char errbuf[1024];
			WARNING ("tcpsock plugin: malloc failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (status);
			continue;
		}
		*remote_fd = status;

		DEBUG ("Spawning child to handle connection on fd #%i", *remote_fd);

		status = plugin_thread_create (&th, &th_attr,
				tcps_handle_client, (void *) remote_fd);
		if (status != 0)
		{
			char errbuf[1024];
			WARNING ("tcpsock plugin: pthread_create failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			close (*remote_fd);
			free (remote_fd);
			continue;
		}
	} /* while (g_loop_active) */

	close (g_sock_fd);
	g_sock_fd = -1;
	pthread_attr_destroy (&th_attr);

	return ((void *) 0);
} /* void *tcps_server_thread */

static int tcps_config (const char *p_key, const char *p_val)
{
	if (strcasecmp (p_key, "Listen") == 0)
	{
		char *new_listen_addr = strdup (p_val);
		if (new_listen_addr == NULL)
		{
			return 1;
		}
		sfree (g_listen_addr);
		g_listen_addr = new_listen_addr;
	} else if (strcasecmp (p_key, "Port") == 0)
	{
		char *new_listen_port= strdup (p_val);
		if (new_listen_port == NULL)
		{
			return 1;
		}
		sfree (g_listen_port);
		g_listen_port = new_listen_port;
	}
	else
	{
		return -1;
	}

	return 0;
} /* int tcps_config */

static int tcps_init (void)
{
	static int have_init = 0;

	int status;

	/* Initialize only once. */
	if (have_init != 0)
	{
		return 0;
	}
	have_init = 1;

	g_loop_active = 1;

	status = plugin_thread_create (&g_listen_thread, NULL,
			tcps_server_thread, NULL);
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("tcpsock plugin: pthread_create failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	return 0;
} /* int tcps_init */

static int tcps_shutdown (void)
{
	void *ret;

	g_loop_active = 0;

	if (g_listen_thread != (pthread_t) 0)
	{
		pthread_kill(g_listen_thread, SIGTERM);
		pthread_join(g_listen_thread, &ret);
		g_listen_thread = (pthread_t) 0;
	}

	plugin_unregister_init(    "tcpsock");
	plugin_unregister_shutdown("tcpsock");

	return (0);
} /* int tcps_shutdown */

void module_register (void)
{
	plugin_register_config(  "tcpsock", tcps_config,
			config_keys, config_keys_num);
	plugin_register_init(    "tcpsock", tcps_init);
	plugin_register_shutdown("tcpsock", tcps_shutdown);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 sts=4 tw=78 : */
