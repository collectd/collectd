/**
 * collectd - src/collectd.c
 * Copyright (C) 2005,2006  Florian octo Forster
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
#include "common.h"
#include "utils_debug.h"

#include "network.h"
#include "plugin.h"
#include "configfile.h"

static int loop = 0;

#if HAVE_LIBKSTAT
kstat_ctl_t *kc;
#endif /* HAVE_LIBKSTAT */

/*
 * exported variables
 */
time_t curtime;
int    operating_mode;

static void sigIntHandler (int signal)
{
	loop++;
}

static void sigTermHandler (int signal)
{
	loop++;
}

static int change_basedir (char *dir)
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
				syslog (LOG_ERR, "mkdir (%s): %s", dir, strerror (errno));
				return (-1);
			}
			else if (chdir (dir) == -1)
			{
				syslog (LOG_ERR, "chdir (%s): %s", dir, strerror (errno));
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
} /* static int change_basedir (char *dir) */

#if HAVE_LIBKSTAT
static void update_kstat (void)
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
} /* static void update_kstat (void) */
#endif /* HAVE_LIBKSTAT */

/* TODO
 * Remove all settings but `-f' and `-C'
 */
static void exit_usage (char *name)
{
	printf ("Usage: "PACKAGE" [OPTIONS]\n\n"
			
			"Available options:\n"
			"  General:\n"
			"    -C <file>       Configuration file.\n"
			"                    Default: "CONFIGFILE"\n"
#if COLLECT_DAEMON
			"    -f              Don't fork to the background.\n"
#endif
			"\nBuiltin defaults:\n"
			"  Config-File       "CONFIGFILE"\n"
			"  PID-File          "PIDFILE"\n"
			"  Data-Directory    "PKGLOCALSTATEDIR"\n"
#if COLLECT_DEBUG
			"  Log-File          "LOGFILE"\n"
#endif
			"  Step              "COLLECTD_STEP" seconds\n"
			"  Heartbeat         "COLLECTD_HEARTBEAT" seconds\n"
			"\n"PACKAGE" "VERSION", http://collectd.org/\n"
			"by Florian octo Forster <octo@verplant.org>\n"
			"for contributions see `AUTHORS'\n");
	exit (0);
} /* static void exit_usage (char *name) */

static int start_client (void)
{
	int step;

	struct timeval tv_now;
	struct timeval tv_next;
	struct timespec ts_wait;

	step = atoi (COLLECTD_STEP);
	if (step <= 0)
		step = 10;

#if HAVE_LIBKSTAT
	kc = NULL;
	update_kstat ();
#endif

#if HAVE_LIBSTATGRAB
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
		if (gettimeofday (&tv_next, NULL) < 0)
		{
			syslog (LOG_ERR, "gettimeofday failed: %s", strerror (errno));
			return (-1);
		}
		tv_next.tv_sec += step;

#if HAVE_LIBKSTAT
		update_kstat ();
#endif
		/* `curtime' is used by many (all?) plugins as the
		 * data-sample-time passed to RRDTool */
		curtime = time (NULL);

		/* Issue all plugins */
		plugin_read_all ();

		if (gettimeofday (&tv_now, NULL) < 0)
		{
			syslog (LOG_ERR, "gettimeofday failed: %s", strerror (errno));
			return (-1);
		}

		if (timeval_sub_timespec (&tv_next, &tv_now, &ts_wait) != 0)
		{
			syslog (LOG_WARNING, "Not sleeping because `timeval_sub_timespec' returned non-zero!");
			continue;
		}

		while (nanosleep (&ts_wait, &ts_wait) == -1)
		{
			if (errno != EINTR)
			{
				syslog (LOG_ERR, "nanosleep failed: %s", strerror (errno));
				break;
			}
		}
	}

	return (0);
} /* static int start_client (void) */

#if HAVE_LIBRRD
static int start_server (void)
{
	/* FIXME use stack here! */
	char *host;
	char *type;
	char *instance;
	char *values;

	while (loop == 0)
	{
		if (network_receive (&host, &type, &instance, &values) == 0)
			plugin_write (host, type, instance, values);

		if (host     != NULL) free (host);     host     = NULL;
		if (type     != NULL) free (type);     type     = NULL;
		if (instance != NULL) free (instance); instance = NULL;
		if (values   != NULL) free (values);   values   = NULL;
	}
	
	return (0);
} /* static int start_server (void) */
#endif /* HAVE_LIBRRD */

#if COLLECT_DAEMON
static int pidfile_create (const char *file)
{
	FILE *fh;

	if (file == NULL)
		file = PIDFILE;

	if ((fh = fopen (file, "w")) == NULL)
	{
		syslog (LOG_ERR, "fopen (%s): %s", file, strerror (errno));
		return (1);
	}

	fprintf (fh, "%i\n", (int) getpid ());
	fclose(fh);

	return (0);
} /* static int pidfile_create (const char *file) */
#endif /* COLLECT_DAEMON */

#if COLLECT_DAEMON
static int pidfile_remove (const char *file)
{
	if (file == NULL) {
		file = PIDFILE;
	}
	return (unlink (file));
} /* static int pidfile_remove (const char *file) */
#endif /* COLLECT_DAEMON */

int main (int argc, char **argv)
{
	struct sigaction sigIntAction;
	struct sigaction sigTermAction;
	char *datadir    = PKGLOCALSTATEDIR;
	char *configfile = CONFIGFILE;
#if COLLECT_DAEMON
	struct sigaction sigChldAction;
	char *pidfile    = PIDFILE;
	pid_t pid;
	int daemonize    = 1;
#endif
#if COLLECT_DEBUG
	char *logfile    = LOGFILE;
#endif

#if HAVE_LIBRRD
	operating_mode = MODE_LOCAL;
#endif

	/* open syslog */
	openlog (PACKAGE, LOG_CONS | LOG_PID, LOG_DAEMON);

	/* read options */
	while (1)
	{
		int c;

		c = getopt (argc, argv, "hC:"
#if COLLECT_DAEMON
				"f"
#endif
		);

		if (c == -1)
			break;

		switch (c)
		{
			case 'C':
				configfile = optarg;
				break;
#if COLLECT_DAEMON
			case 'f':
				daemonize = 0;
				break;
#endif /* COLLECT_DAEMON */
			case 'h':
			default:
				exit_usage (argv[0]);
		} /* switch (c) */
	} /* while (1) */

#if COLLECT_DEBUG
	if ((logfile = cf_get_option ("LogFile", LOGFILE)) != NULL)
		DBG_STARTFILE (logfile, "Debug file opened.");
#endif

	/*
	 * Read options from the config file, the environment and the command
	 * line (in that order, with later options overwriting previous ones in
	 * general).
	 * Also, this will automatically load modules.
	 */
	if (cf_read (configfile))
	{
		fprintf (stderr, "Error: Reading the config file failed!\n"
				"Read the syslog for details.\n");
		return (1);
	}

	/*
	 * Change directory. We do this _after_ reading the config and loading
	 * modules to relative paths work as expected.
	 */
	if ((datadir = cf_get_option ("DataDir", PKGLOCALSTATEDIR)) == NULL)
	{
		fprintf (stderr, "Don't have a datadir to use. This should not happen. Ever.");
		return (1);
	}
	if (change_basedir (datadir))
	{
		fprintf (stderr, "Error: Unable to change to directory `%s'.\n", datadir);
		return (1);
	}

#if COLLECT_DAEMON
	/*
	 * fork off child
	 */
	sigChldAction.sa_handler = SIG_IGN;
	sigaction (SIGCHLD, &sigChldAction, NULL);

	if ((pidfile = cf_get_option ("PIDFile", PIDFILE)) == NULL)
	{
		fprintf (stderr, "Cannot obtain pidfile. This shoud not happen. Ever.");
		return (1);
	}

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
		if (pidfile_create (pidfile))
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
	 * install signal handlers
	 */
	sigIntAction.sa_handler = sigIntHandler;
	sigaction (SIGINT, &sigIntAction, NULL);

	sigTermAction.sa_handler = sigTermHandler;
	sigaction (SIGTERM, &sigTermAction, NULL);

	/*
	 * run the actual loops
	 */
#if HAVE_LIBRRD
	if (operating_mode == MODE_SERVER)
		start_server ();
	else /* if (operating_mode == MODE_CLIENT || operating_mode == MODE_LOCAL || operating_mode == MODE_LOG) */
#endif
		start_client ();

#if COLLECT_DEBUG
	if (logfile != NULL)
		DBG_STOPFILE("debug file closed.");
#endif

	/* close syslog */
	syslog (LOG_INFO, "Exiting normally");
	closelog ();

#if COLLECT_DAEMON
	if (daemonize)
		pidfile_remove (pidfile);
#endif /* COLLECT_DAEMON */

	return (0);
} /* int main (int argc, char **argv) */
