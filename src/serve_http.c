/**
 * collectd - src/serve_http.c
 * Copyright (C) 2007,2008  Florian octo Forster
 * Copyright (C) 2013-2014  David Nicklay
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
 * Based on the unixsock plugin. Authors:
 *   Florian octo Forster <octo at collectd.org>
 * Authors:
 *   David Nicklay <david at nicklay.com>
 **/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <err.h>

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_cache.h"
#include "utils_format_json.h"


#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include "utils_cmd_listjson.h"

/*
 * Private variables
 */

/* valid configuration file keys */
static const char *config_keys[] =
{
    "Port",
    "StripHostnames"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);
static _Bool strip_hostnames = 1;

static int loop = 0;

/* socket configuration */
static int   sh_port = 25827;

static pthread_t listen_thread = (pthread_t) 0;

static void *sh_handle_client (void *arg)
{
	int fdin;
	int fdout;
	FILE *fhin, *fhout;

	fdin = *((int *) arg);
	free (arg);
	arg = NULL;

    char response_header[] = "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=UTF-8\r\n\r\n";

    if (write(fdin, response_header, sizeof(response_header) - 1) == -1) {
        ERROR ("serve_http plugin: Sending of response headers to client failed.");
		close (fdin);
		pthread_exit ((void *) 1);
    }

	DEBUG ("serve_http plugin: serving remaining content");
	fdout = dup (fdin);
	if (fdout < 0)
	{
		char errbuf[1024];
		ERROR ("serve_http plugin: dup failed: %s", sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fdin);
		pthread_exit ((void *) 1);
	}

	fhin  = fdopen (fdin, "r");
	if (fhin == NULL)
	{
		char errbuf[1024];
		ERROR ("serve_http plugin: fdopen failed: %s", sstrerror (errno, errbuf, sizeof (errbuf)));
		close (fdin);
		close (fdout);
		pthread_exit ((void *) 1);
		return ((void *) 1);
	}

	fhout = fdopen (fdout, "w");
	if (fhout == NULL)
	{
		char errbuf[1024];
		ERROR ("serve_http plugin: fdopen failed: %s", sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (fhin); /* this closes fdin as well */
		close (fdout);
		pthread_exit ((void *) 1);
		return ((void *) 1);
	}

	/* change output buffer to line buffered mode */
	if (setvbuf (fhout, NULL, _IOLBF, 0) != 0)
	{
		char errbuf[1024];
		ERROR ("serve_http plugin: setvbuf failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (fhin);
		fclose (fhout);
		pthread_exit ((void *) 1);
		return ((void *) 0);
	}

	DEBUG ("serve_http plugin: sh_handle_client: Handle listjson");
    handle_listjson(fhout,strip_hostnames);

	DEBUG ("serve_http plugin: sh_handle_client: Exiting..");
	fclose (fhin);
	fclose (fhout);

	pthread_exit ((void *) 0);
	return ((void *) 0);
} /* void *sh_handle_client */


static void *sh_server_thread (void __attribute__((unused)) *arg)
{
	int  status;
	int *remote_fd;
	pthread_t th;
	pthread_attr_t th_attr;

	pthread_attr_init (&th_attr);
	pthread_attr_setdetachstate (&th_attr, PTHREAD_CREATE_DETACHED);

    int one = 1;
    struct sockaddr_in svr_addr, cli_addr;
    socklen_t sin_len = sizeof(cli_addr);
   
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0)
        err(1, "can't open socket");
   
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int));
   
    svr_addr.sin_family = AF_INET;
    svr_addr.sin_addr.s_addr = INADDR_ANY;
    svr_addr.sin_port = htons(sh_port);
   
    if (bind(sock, (struct sockaddr *) &svr_addr, sizeof(svr_addr)) == -1) {
        close(sock);
        err(1, "Can't bind");
		pthread_exit ((void *) 1);
    }
   
    listen(sock, 5);

	while (loop != 0)
	{
		DEBUG ("serve_http plugin: Calling accept..");
        status = accept(sock, (struct sockaddr *) &cli_addr, &sin_len);
        if (status < 0)
		{
			char errbuf[1024];

			if (errno == EINTR)
				continue;

			ERROR ("serve_http plugin: accept failed: %s", sstrerror (errno, errbuf, sizeof (errbuf)));
			close (sock);
			sock = -1;
			pthread_attr_destroy (&th_attr);
			pthread_exit ((void *) 1);
		}

		remote_fd = (int *) malloc (sizeof (int));
		if (remote_fd == NULL)
		{
			char errbuf[1024];
			WARNING ("serve_http plugin: malloc failed: %s", sstrerror (errno, errbuf, sizeof (errbuf)));
			close (status);
			continue;
		}
		*remote_fd = status;

		DEBUG ("Spawning child to handle connection on fd #%i", *remote_fd);

		status = plugin_thread_create (&th, &th_attr, sh_handle_client, (void *) remote_fd);

		if (status != 0)
		{
			char errbuf[1024];
			WARNING ("serve_http plugin: pthread_create failed: %s", sstrerror (errno, errbuf, sizeof (errbuf)));
			close (*remote_fd);
			free (remote_fd);
			continue;
		}
	} /* while (loop) */

	close (sock);
	sock = -1;
	pthread_attr_destroy (&th_attr);

	return ((void *) 0);
} /* void *sh_server_thread */

static int sh_config (const char *key, const char *val)
{
	if (strcasecmp (key, "Port") == 0)
	{
      sh_port = atoi(val);
	}
	else if (strcasecmp (key, "StripHostnames") == 0)
	{
        if (IS_FALSE(val))
            strip_hostnames = 0;
        else
            strip_hostnames = 1;
	}
	else
	{
		return (-1);
	}

	return (0);
} /* int sh_config */

static int sh_init (void)
{
	static int have_init = 0;

	int status;

	/* Initialize only once. */
	if (have_init != 0)
		return (0);
	have_init = 1;

	loop = 1;

	status = plugin_thread_create (&listen_thread, NULL, sh_server_thread, NULL);
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("serve_http plugin: pthread_create failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	return (0);
} /* int sh_init */

static int sh_shutdown (void)
{
	void *ret;

	loop = 0;

	if (listen_thread != (pthread_t) 0)
	{
		pthread_kill (listen_thread, SIGTERM);
		pthread_join (listen_thread, &ret);
		listen_thread = (pthread_t) 0;
	}

	plugin_unregister_init ("serve_http");
	plugin_unregister_shutdown ("serve_http");

	return (0);
} /* int sh_shutdown */

void module_register (void)
{
	plugin_register_config ("serve_http", sh_config, config_keys, config_keys_num);
	plugin_register_init ("serve_http", sh_init);
	plugin_register_shutdown ("serve_http", sh_shutdown);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 sts=4 tw=78 : */
