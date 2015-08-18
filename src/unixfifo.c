/**
 * collectd - src/unixfifo.c
 * Copyright (C) 2007,2008,2015  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 *   Jeff Adams <intensifi at mac.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include "utils_cmd_flush.h"
#include "utils_cmd_putval.h"
#include "utils_cmd_putnotif.h"

#include <grp.h>

/* Folks without pthread will need to disable this plugin. */
#include <pthread.h>

#include <stdio.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>

#ifndef UNIX_PATH_MAX
# define UNIX_PATH_MAX PATH_MAX
#endif

#define UF_DEFAULT_PATH LOCALSTATEDIR"/run/"PACKAGE_NAME"-unixfifo"
#define UF_DEFAULT_GROUP COLLECTD_GRP_NAME
#define UF_DEFAULT_PERMS S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH

/*
 * Private variables
 */

/* valid configuration file keys */
static const char *config_keys[] =
{
	"FifoFile",
	"FifoGroup",
	"FifoPerms"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

volatile static int should_shutdown = 0;

/* fifo configuration */
static FILE   *fifo_fd    = NULL;
static char   *fifo_file  = NULL;
static char   *fifo_group = NULL;
static mode_t fifo_perms  = (mode_t)0;

static pthread_t listen_thread = (pthread_t) 0;

/*
 * Functions
 */
static int uf_open_fifo (void)
{
	int status = -1;
    
	DEBUG ("unixfifo plugin uf_open_fifo : fifo path = %s", fifo_file);

    errno = 0;

    status = unlink (fifo_file);
    if ((status != 0) && (errno != ENOENT))
    {
        char errbuf[1024];
        WARNING ("unixfifo plugin: Deleting fifo file \"%s\" failed: %s",
                fifo_file,
                sstrerror (errno, errbuf, sizeof (errbuf)));
    }
    else if (status == 0)
    {
        INFO ("unixfifo plugin: Successfully deleted fifo file \"%s\".",
                fifo_file);
    }
    
    /* The default umask (022) is backward for fifos.
       World needs write access, not read access
     */
    
    mode_t old_mode = umask (0);

    status = mkfifo (fifo_file, S_IFIFO | fifo_perms);
    
    umask (old_mode); /* return to old umask */
    
    if (status < 0)
    {
        char errbuf[1024];
        ERROR ("unixfifo plugin: fifo creation failed: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return (-1);
    }
    
    /* set the proper group for the fifo */
    do
    {
        char *grpname;
        struct group *g;
        struct group sg;
        char grbuf[2048];
        
        grpname = (fifo_group != NULL) ? fifo_group : COLLECTD_GRP_NAME;
        g = NULL;
        
        status = getgrnam_r (grpname, &sg, grbuf, sizeof (grbuf), &g);
        if (status != 0)
        {
            char errbuf[1024];
            WARNING ("unixfifo plugin: getgrnam_r (%s) failed: %s", grpname,
                     sstrerror (errno, errbuf, sizeof (errbuf)));
            break;
        }
        
        if (NULL == g)
        {
            WARNING ("unixfifo plugin: No such group: `%s'",
                     grpname);
            break;
        }
        
        if (chown ((fifo_file != NULL) ? fifo_file : UF_DEFAULT_PATH,
                   (uid_t) -1, g->gr_gid) != 0)
        {
            char errbuf[1024];
            WARNING ("unixfifo plugin: chown (%s, -1, %i) failed: %s",
                     (fifo_file != NULL) ? fifo_file : UF_DEFAULT_PATH,
                     (int) g->gr_gid,
                     sstrerror (errno, errbuf, sizeof (errbuf)));
        }
    } while (0);
    
    /* open read/write at "end" of fifo.  
     No writing will occur.
     Maintaining read & write connections just keeps the fifo in a standard (i.e. receiving) state
     */
    fifo_fd = fopen (fifo_file, "a+");
    
    if (NULL == fifo_fd)
    {
        char errbuf[1024];
        ERROR ("unixfifo plugin: fifo open failed: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return (-1);
    }
    
    /* set file to be non-blocking
     */
    
    int fifo_fileno = fileno(fifo_fd);
    
    int flags = 0;
    flags = fcntl (fifo_fileno, F_GETFL, 0);
    flags |= O_NONBLOCK;
    
    
    status = fcntl (fifo_fileno, F_SETFL, flags);
    if ( status < 0)
    {
        char errbuf[1024];
        ERROR ("unixfifo plugin: fifo set to non-blocking failed: %s",
               sstrerror (errno, errbuf, sizeof (errbuf)));
        return (-1);
    }

	return (0);
} /* int uf_open_fifo */

int uf_handle_client (FILE *the_fifo_fd)
{
    DEBUG ("unixfifo plugin: uf_handle_client: Reading from fd #%i", fileno (the_fifo_fd));

    /* continue reading until there is no more data.
       In some cases select() may not always be triggered again if additional
       data arrives while previous data is not yet read.
     */
    while (1)
	{
		char buffer[1024];
		char buffer_copy[1024];
		char *fields[128];
		int  fields_num;
		int  len;

		errno = 0;
		if (fgets (buffer, sizeof (buffer), the_fifo_fd) == NULL)
		{
			if (errno != 0 &&
                errno != EWOULDBLOCK && /* if no more data, exit loop */
                errno != EAGAIN)
			{
				char errbuf[1024];
				WARNING ("unixfifo plugin: failed to read from fifo #%i: %s",
						fileno (the_fifo_fd),
						sstrerror (errno, errbuf, sizeof (errbuf)));
			}
            break;
		}

        /* read the incoming data*/
		len = strlen (buffer);
		while ((len > 0)
				&& ((buffer[len - 1] == '\n') || (buffer[len - 1] == '\r')))
        {
            buffer[--len] = '\0';
        }

		if (0 == len)
        {
            break;
        }
        
		sstrncpy (buffer_copy, buffer, sizeof (buffer_copy));
        
		fields_num = strsplit (buffer_copy, fields,
				sizeof (fields) / sizeof (fields[0]));
		if (fields_num < 1)
		{
			ERROR ("Internal error");
			return (-4);
		}

		if (strcasecmp (fields[0], "putval") == 0)
		{
            handle_putval (NULL, buffer);
		}
		else if (strcasecmp (fields[0], "putnotif") == 0)
		{
			handle_putnotif (NULL, buffer);
		}
		else if (strcasecmp (fields[0], "flush") == 0)
		{
			handle_flush (NULL, buffer);
		}
        else
		{
            ERROR( "Unknown command: %s", fields[0] );
		}
	}  /* while (1) */

	return (0);
} /* int uf_handle_client */

static void *uf_server_thread (void __attribute__((unused)) *arg)
{
	int status;

    fd_set input_set;
    fd_set error_set;
    
    struct timeval timeout;
    int select_error = 0;

    if (uf_open_fifo () != 0)
    {
		pthread_exit ((void *) 1);
    }
    
    while (0 == should_shutdown)
	{
        DEBUG ("unixfifo plugin: Calling select..");
        
        FD_ZERO(&input_set);
        FD_SET(fileno (fifo_fd), &input_set);

        FD_ZERO(&error_set);
        FD_SET(fileno (fifo_fd), &error_set);
        
        timeout.tv_sec = 10;
        timeout.tv_usec = 0;
        
        status = select(fileno (fifo_fd) + 1, &input_set, NULL, &error_set, NULL /*&timeout*/);

        if (1 == should_shutdown)
        {
            DEBUG ("unixfifo plugin: Shutting Down after select");
            break;
        }
        
        if (0 == status)
        {
            continue;
        }
        
 		if (status < 0)
		{
			char errbuf[1024];

			if (errno == EINTR)
            {
				continue;
            }

            select_error = 1;
            
			ERROR ("unixfifo plugin: select failed: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
            break;
		}

		DEBUG ("Calling child to handle connection on fd #%i", fileno (fifo_fd));
        
        status = uf_handle_client(fifo_fd);

        if (status < 0)
        {
            break;
        }
        
	} /* while (0 == should_shutdown) */

	fclose (fifo_fd);
	fifo_fd = NULL;

	status = unlink (fifo_file);
	if (status != 0)
	{
		char errbuf[1024];
		NOTICE ("unixfifo plugin: unlink (%s) failed: %s",
				fifo_file,
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}

    if (select_error)
    {
        pthread_exit ((void *) 1);
    }
	return ((void *) 0);
} /* void *uf_server_thread */

static int uf_config (const char *key, const char *val)
{
	if (strcasecmp (key, "FifoFile") == 0)
	{
		char *new_fifo_file = strdup (val);
		if (new_fifo_file == NULL)
        {
			return (1);
        }

        sfree (fifo_file); /* in case specified multiple times */
		fifo_file = new_fifo_file;
	}
	else if (strcasecmp (key, "FifoGroup") == 0)
	{
		char *new_fifo_group = strdup (val);
		if (new_fifo_group == NULL)
        {
			return (1);
        }

        sfree (fifo_group); /* in case specified multiple times */
		fifo_group = new_fifo_group;
	}
	else if (strcasecmp (key, "FifoPerms") == 0)
	{
        if (val == NULL)
        {
            return (1);
        }
		fifo_perms = (mode_t) strtol (val, NULL, 8);
	}
	else
	{
		return (-1);
	}
    
	return (0);
} /* int uf_config */

static int uf_init (void)
{
    static int have_init = 0;

	int status = -1;

	/* Initialize only once. */
	if (have_init != 0)
    {
		return (0);
    }
    
	have_init = 1;

    should_shutdown = 0;
    
    if (NULL == fifo_file)
    {
        fifo_file = UF_DEFAULT_PATH;
    }
    
    if (NULL == fifo_group)
    {
        fifo_group = UF_DEFAULT_GROUP;
    }
    
    if ((mode_t)0 == fifo_perms)
    {
        fifo_perms = (mode_t) (UF_DEFAULT_PERMS);
    }
   
    /* TODO:  Add code to make sure owner has a minimum of read & write access
     and world has write access */
    
 	status = plugin_thread_create (&listen_thread, NULL,
			uf_server_thread, NULL);

    if (status != 0)
	{
		char errbuf[1024];
		ERROR ("unixfifo plugin: pthread_create failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	return (0);
} /* int uf_init */

static int uf_shutdown (void)
{
    void *ret;

    should_shutdown = 1;

    fclose (fifo_fd); /* will cause select() to return */
    
	if (listen_thread != (pthread_t) 0)
	{
		pthread_kill (listen_thread, SIGTERM);
		pthread_join (listen_thread, &ret);
		listen_thread = (pthread_t) 0;
	}

	plugin_unregister_init ("unixfifo");
	plugin_unregister_shutdown ("unixfifo");

	return (0);
} /* int uf_shutdown */

void module_register (void)
{
	plugin_register_config ("unixfifo", uf_config,
			config_keys, config_keys_num);
	plugin_register_init ("unixfifo", uf_init);
	plugin_register_shutdown ("unixfifo", uf_shutdown);
} /* void module_register (void) */

/* vim: set sw=4 ts=4 sts=4 tw=78 : */
