/**
 * collectd - src/collectd.c
 * Copyright (C) 2005  Florian octo Forster
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
 *   Alvaro Barcellos <alvaro.barcellos at gmail.com>
 **/

#include "collectd.h"

#include "multicast.h"
#include "plugin.h"

#include "ping.h"

static int loop = 0;

#ifdef HAVE_LIBKSTAT
kstat_ctl_t *kc;
#endif /* HAVE_LIBKSTAT */

#if COLLECT_PING
char *pinghosts[MAX_PINGHOSTS];
int   num_pinghosts = 0;
#endif

/*
 * exported variables
 */
time_t curtime;

#ifdef HAVE_LIBRRD
int operating_mode;
#endif

void sigIntHandler (int signal)
{
	loop++;
}

int change_basedir (char *dir)
{
	int dirlen = strlen (dir);
	
	while ((dirlen > 0) && (dir[dirlen - 1] == '/'))
		dir[--dirlen] = '\0';

	if (dirlen <= 0)
		return (-1);

	if (chdir (dir) == -1)
	{
		if (errno == ENOENT)
		{
			if (mkdir (dir, 0755) == -1)
			{
				syslog (LOG_ERR, "mkdir: %s", strerror (errno));
				return (-1);
			}
			else if (chdir (dir) == -1)
			{
				syslog (LOG_ERR, "chdir: %s", strerror (errno));
				return (-1);
			}
		}
		else
		{
			syslog (LOG_ERR, "chdir: %s", strerror (errno));
			return (-1);
		}
	}

	return (0);
}

#ifdef HAVE_LIBKSTAT
void update_kstat (void)
{
	if (kc == NULL)
	{
		if ((kc = kstat_open ()) == NULL)
			syslog (LOG_ERR, "Unable to open kstat control structure");
	}
	else
	{
		kid_t kid;
		kid = kstat_chain_update (kc);
		if (kid > 0)
		{
			syslog (LOG_INFO, "kstat chain has been updated");
			plugin_init_all ();
		}
		else if (kid < 0)
			syslog (LOG_ERR, "kstat chain update failed");
		/* else: everything works as expected */
	}

	return;
}
#endif /* HAVE_LIBKSTAT */

void exit_usage (char *name)
{
	printf ("Usage: %s [OPTIONS]\n\n"
			
			"Available options:\n"
			"  General:\n"
			"    -C <dir>        Configuration file.\n"
			"                    Default: %s\n"
			"    -P <file>       PID File.\n"
			"                    Default: %s\n"
			"    -M <dir>        Module/Plugin directory.\n"
			"                    Default: %s\n"
			"    -D <dir>        Data storage directory.\n"
			"                    Default: %s\n"
			"    -f              Don't fork to the background.\n"
#ifdef HAVE_LIBRRD
			"    -l              Start in local mode (no network).\n"
			"    -c              Start in client (sender) mode.\n"
			"    -s              Start in server (listener) mode.\n"
#endif /* HAVE_LIBRRD */
#if COLLECT_PING
			"  Ping:\n"
			"    -p <host>       Host to ping periodically, may be repeated to ping\n"
			"                    more than one host.\n"
#endif /* COLLECT_PING */
			"\n%s %s, http://verplant.org/collectd/\n"
			"by Florian octo Forster <octo@verplant.org>\n"
			"for contributions see `AUTHORS'\n",
			PACKAGE, CONFIGFILE, PIDFILE, PLUGINDIR, PKGLOCALSTATEDIR, PACKAGE, VERSION);
	exit (0);
}

int start_client (void)
{
	int sleepingtime;

#ifdef HAVE_LIBKSTAT
	kc = NULL;
	update_kstat ();
#endif

#ifdef HAVE_LIBSTATGRAB
	if (sg_init ())
	{
		syslog (LOG_ERR, "sg_init: %s", sg_str_error (sg_get_error ()));
		return (-1);
	}

	if (sg_drop_privileges ())
	{
		syslog (LOG_ERR, "sg_drop_privileges: %s", sg_str_error (sg_get_error ()));
		return (-1);
	}
#endif

	plugin_init_all ();

	while (loop == 0)
	{
		curtime = time (NULL);
#ifdef HAVE_LIBKSTAT
		update_kstat ();
#endif
		plugin_read_all ();

		sleepingtime = 10;
		while (sleepingtime != 0)
		{
			if (loop != 0)
				break;
			sleepingtime = sleep (sleepingtime);
		}
	}

	return (0);
}

#ifdef HAVE_LIBRRD
int start_server (void)
{
	char *host;
	char *type;
	char *instance;
	char *values;

	while (loop == 0)
	{
		if (multicast_receive (&host, &type, &instance, &values) == 0)
			plugin_write (host, type, instance, values);

		if (host     != NULL) free (host);     host     = NULL;
		if (type     != NULL) free (type);     type     = NULL;
		if (instance != NULL) free (instance); instance = NULL;
		if (values   != NULL) free (values);   values   = NULL;
	}
	
	return (0);
}
#endif /* HAVE_LIBRRD */

int pidfile_create (void)
{
	FILE *fh;

	if ((fh = fopen (PIDFILE, "w")) == NULL)
	{
		syslog (LOG_ERR, "fopen (pidfile): %s", strerror (errno));
		return (1);
	}

	fprintf (fh, "%d\n", getpid());
	fclose(fh);

	return (0);
}

int pidfile_remove (void)
{
      return (unlink (PIDFILE));
}

int main (int argc, char **argv)
{
	struct sigaction sigIntAction, sigChldAction;
#if COLLECT_DAEMON
	pid_t pid;
#endif

	char *configfile = CONFIGFILE;
	char *pidfile    = PIDFILE;
	char *plugindir  = PLUGINDIR;
	char *datadir    = PKGLOCALSTATEDIR;

	int daemonize = 1;

#ifdef HAVE_LIBRRD
	operating_mode = MODE_LOCAL;
#endif
	
	/* open syslog */
	openlog (PACKAGE, LOG_CONS | LOG_PID, LOG_DAEMON);

	/* read options */
	while (1)
	{
		int c;

		c = getopt (argc, argv, "C:P:M:D:fh"
#if HAVE_LIBRRD
				"csl"
#endif /* HAVE_LIBRRD */
#if COLLECT_PING
				"p:"
#endif /* COLLECT_PING */
		);

		if (c == -1)
			break;

		switch (c)
		{
#ifdef HAVE_LIBRRD
			case 'c':
				operating_mode = MODE_CLIENT;
				break;

			case 's':
				operating_mode = MODE_SERVER;
				break;

			case 'l':
				operating_mode = MODE_LOCAL;
				break;
#endif /* HAVE_LIBRRD */
			case 'C':
				configfile = optarg;
				break;
			case 'P':
				pidfile = optarg;
				break;
			case 'M':
				plugindir = optarg;
				break;
			case 'D':
				datadir = optarg;
				break;
			case 'f':
				daemonize = 0;
				break;
#if COLLECT_PING
			case 'p':
				if (num_pinghosts < MAX_PINGHOSTS)
					pinghosts[num_pinghosts++] = optarg;
				else
					fprintf (stderr, "Maximum of %i ping hosts reached.\n", MAX_PINGHOSTS);
				break;
#endif /* COLLECT_PING */
			case 'h':
			default:
				exit_usage (argv[0]);
		}
				
	}

	/*
	 * Load plugins and change to output directory
	 * Loading plugins is done first so relative paths work as expected..
	 */
	if (plugin_load_all (plugindir) < 1)
	{
		fprintf (stderr, "Error: No plugins found.\n");
		return (1);
	}

	if (change_basedir (datadir))
	{
		fprintf (stderr, "Error: Unable to change to directory `%s'.\n", datadir);
		return (1);
	}

	/*
	 * install signal handlers
	 */
	sigIntAction.sa_handler = sigIntHandler;
	sigaction (SIGINT, &sigIntAction, NULL);

	sigChldAction.sa_handler = SIG_IGN;
	sigaction (SIGCHLD, &sigChldAction, NULL);

	/*
	 * fork off child
	 */
#if COLLECT_DAEMON
	if (daemonize)
	{
		if ((pid = fork ()) == -1)
		{
			/* error */
			fprintf (stderr, "fork: %s", strerror (errno));
			return (1);
		}
		else if (pid != 0)
		{
			/* parent */
			/* printf ("Running (PID %i)\n", pid); */
			return (0);
		}

		/* Detach from session */
		setsid ();

		/* Write pidfile */
		if (pidfile_create ())
			exit (2);

		/* close standard descriptors */
		close (2);
		close (1);
		close (0);

		if (open ("/dev/null", O_RDWR) != 0)
		{
			syslog (LOG_ERR, "Error: Could not connect `STDIN' to `/dev/null'");
			return (1);
		}
		if (dup (0) != 1)
		{
			syslog (LOG_ERR, "Error: Could not connect `STDOUT' to `/dev/null'");
			return (1);
		}
		if (dup (0) != 2)
		{
			syslog (LOG_ERR, "Error: Could not connect `STDERR' to `/dev/null'");
			return (1);
		}
	} /* if (daemonize) */
#endif /* COLLECT_DAEMON */

	/*
	 * run the actual loops
	 */
#ifdef HAVE_LIBRRD
	if (operating_mode == MODE_SERVER)
		start_server ();
	else /* if (operating_mode == MODE_CLIENT || operating_mode == MODE_LOCAL) */
#endif
		start_client ();

	/* close syslog */
	syslog (LOG_INFO, "Exiting normally");
	closelog ();

	if (daemonize)
		pidfile_remove();

	return (0);
}
