/**
 * collectd - src/collectd.c
 * Copyright (C) 2005-2007  Florian octo Forster
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
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 *   Alvaro Barcellos <alvaro.barcellos at gmail.com>
 **/

#include "collectd.h"
#include "common.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#include <pthread.h>

#include "plugin.h"
#include "configfile.h"

#if HAVE_STATGRAB_H
# include <statgrab.h>
#endif

/*
 * Global variables
 */
char hostname_g[DATA_MAX_NAME_LEN];
cdtime_t interval_g;
int  timeout_g;
#if HAVE_LIBKSTAT
kstat_ctl_t *kc;
#endif /* HAVE_LIBKSTAT */

static int loop = 0;

static void *do_flush (void __attribute__((unused)) *arg)
{
	INFO ("Flushing all data.");
	plugin_flush (/* plugin = */ NULL,
			/* timeout = */ 0,
			/* ident = */ NULL);
	INFO ("Finished flushing all data.");
	pthread_exit (NULL);
	return NULL;
}

static void sig_int_handler (int __attribute__((unused)) signal)
{
	loop++;
}

static void sig_term_handler (int __attribute__((unused)) signal)
{
	loop++;
}

static void sig_usr1_handler (int __attribute__((unused)) signal)
{
	pthread_t      thread;
	pthread_attr_t attr;

	/* flushing the data might take a while,
	 * so it should be done asynchronously */
	pthread_attr_init (&attr);
	pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED);
	pthread_create (&thread, &attr, do_flush, NULL);
}

static int init_hostname (void)
{
	const char *str;

	struct addrinfo  ai_hints;
	struct addrinfo *ai_list;
	struct addrinfo *ai_ptr;
	int status;

	str = global_option_get ("Hostname");
	if (str != NULL)
	{
		sstrncpy (hostname_g, str, sizeof (hostname_g));
		return (0);
	}

	if (gethostname (hostname_g, sizeof (hostname_g)) != 0)
	{
		fprintf (stderr, "`gethostname' failed and no "
				"hostname was configured.\n");
		return (-1);
	}

	str = global_option_get ("FQDNLookup");
	if (IS_FALSE (str))
		return (0);

	memset (&ai_hints, '\0', sizeof (ai_hints));
	ai_hints.ai_flags = AI_CANONNAME;

	status = getaddrinfo (hostname_g, NULL, &ai_hints, &ai_list);
	if (status != 0)
	{
		ERROR ("Looking up \"%s\" failed. You have set the "
				"\"FQDNLookup\" option, but I cannot resolve "
				"my hostname to a fully qualified domain "
				"name. Please fix you network "
				"configuration.", hostname_g);
		return (-1);
	}

	for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
	{
		if (ai_ptr->ai_canonname == NULL)
			continue;

		sstrncpy (hostname_g, ai_ptr->ai_canonname, sizeof (hostname_g));
		break;
	}

	freeaddrinfo (ai_list);
	return (0);
} /* int init_hostname */

static int init_global_variables (void)
{
	const char *str;

	str = global_option_get ("Interval");
	if (str == NULL)
	{
		interval_g = TIME_T_TO_CDTIME_T (10);
	}
	else
	{
		double tmp;

		tmp = atof (str);
		if (tmp <= 0.0)
		{
			fprintf (stderr, "Cannot set the interval to a "
					"correct value.\n"
					"Please check your settings.\n");
			return (-1);
		}

		interval_g = DOUBLE_TO_CDTIME_T (tmp);
	}
	DEBUG ("interval_g = %.3f;", CDTIME_T_TO_DOUBLE (interval_g));

	str = global_option_get ("Timeout");
	if (str == NULL)
		str = "2";
	timeout_g = atoi (str);
	if (timeout_g <= 1)
	{
		fprintf (stderr, "Cannot set the timeout to a correct value.\n"
				"Please check your settings.\n");
		return (-1);
	}
	DEBUG ("timeout_g = %i;", timeout_g);

	if (init_hostname () != 0)
		return (-1);
	DEBUG ("hostname_g = %s;", hostname_g);

	return (0);
} /* int init_global_variables */

static int change_basedir (const char *orig_dir)
{
	char *dir = strdup (orig_dir);
	int dirlen;
	int status;

	if (dir == NULL)
	{
		char errbuf[1024];
		ERROR ("strdup failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
	
	dirlen = strlen (dir);
	while ((dirlen > 0) && (dir[dirlen - 1] == '/'))
		dir[--dirlen] = '\0';

	if (dirlen <= 0)
		return (-1);

	status = chdir (dir);
	free (dir);

	if (status != 0)
	{
		if (errno == ENOENT)
		{
			if (mkdir (orig_dir, 0755) == -1)
			{
				char errbuf[1024];
				ERROR ("change_basedir: mkdir (%s): %s", orig_dir,
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}
			else if (chdir (orig_dir) == -1)
			{
				char errbuf[1024];
				ERROR ("chdir (%s): %s", orig_dir,
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}
		}
		else
		{
			char errbuf[1024];
			ERROR ("chdir (%s): %s", orig_dir,
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
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
			ERROR ("Unable to open kstat control structure");
	}
	else
	{
		kid_t kid;
		kid = kstat_chain_update (kc);
		if (kid > 0)
		{
			INFO ("kstat chain has been updated");
			plugin_init_all ();
		}
		else if (kid < 0)
			ERROR ("kstat chain update failed");
		/* else: everything works as expected */
	}

	return;
} /* static void update_kstat (void) */
#endif /* HAVE_LIBKSTAT */

/* TODO
 * Remove all settings but `-f' and `-C'
 */
static void exit_usage (int status)
{
	printf ("Usage: "PACKAGE" [OPTIONS]\n\n"
			
			"Available options:\n"
			"  General:\n"
			"    -C <file>       Configuration file.\n"
			"                    Default: "CONFIGFILE"\n"
			"    -t              Test config and exit.\n"
			"    -T              Test plugin read and exit.\n"
			"    -P <file>       PID-file.\n"
			"                    Default: "PIDFILE"\n"
#if COLLECT_DAEMON
			"    -f              Don't fork to the background.\n"
#endif
			"    -h              Display help (this message)\n"
			"\nBuiltin defaults:\n"
			"  Config file       "CONFIGFILE"\n"
			"  PID file          "PIDFILE"\n"
			"  Plugin directory  "PLUGINDIR"\n"
			"  Data directory    "PKGLOCALSTATEDIR"\n"
			"\n"PACKAGE" "VERSION", http://collectd.org/\n"
			"by Florian octo Forster <octo@verplant.org>\n"
			"for contributions see `AUTHORS'\n");
	exit (status);
} /* static void exit_usage (int status) */

static int do_init (void)
{
#if HAVE_LIBKSTAT
	kc = NULL;
	update_kstat ();
#endif

#if HAVE_LIBSTATGRAB
	if (sg_init ())
	{
		ERROR ("sg_init: %s", sg_str_error (sg_get_error ()));
		return (-1);
	}

	if (sg_drop_privileges ())
	{
		ERROR ("sg_drop_privileges: %s", sg_str_error (sg_get_error ()));
		return (-1);
	}
#endif

	plugin_init_all ();

	return (0);
} /* int do_init () */


static int do_loop (void)
{
	cdtime_t wait_until;

	wait_until = cdtime () + interval_g;

	while (loop == 0)
	{
		struct timespec ts_wait = { 0, 0 };
		cdtime_t now;

#if HAVE_LIBKSTAT
		update_kstat ();
#endif

		/* Issue all plugins */
		plugin_read_all ();

		now = cdtime ();
		if (now >= wait_until)
		{
			WARNING ("Not sleeping because the next interval is "
					"%.3f seconds in the past!",
					CDTIME_T_TO_DOUBLE (now - wait_until));
			wait_until = now + interval_g;
			continue;
		}

		CDTIME_T_TO_TIMESPEC (wait_until - now, &ts_wait);
		wait_until = wait_until + interval_g;

		while ((loop == 0) && (nanosleep (&ts_wait, &ts_wait) != 0))
		{
			if (errno != EINTR)
			{
				char errbuf[1024];
				ERROR ("nanosleep failed: %s",
						sstrerror (errno, errbuf,
							sizeof (errbuf)));
				return (-1);
			}
		}
	} /* while (loop == 0) */

	return (0);
} /* int do_loop */

static int do_shutdown (void)
{
	plugin_shutdown_all ();
	return (0);
} /* int do_shutdown */

#if COLLECT_DAEMON
static int pidfile_create (void)
{
	FILE *fh;
	const char *file = global_option_get ("PIDFile");

	if ((fh = fopen (file, "w")) == NULL)
	{
		char errbuf[1024];
		ERROR ("fopen (%s): %s", file,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (1);
	}

	fprintf (fh, "%i\n", (int) getpid ());
	fclose(fh);

	return (0);
} /* static int pidfile_create (const char *file) */

static int pidfile_remove (void)
{
	const char *file = global_option_get ("PIDFile");

	DEBUG ("unlink (%s)", (file != NULL) ? file : "<null>");
	return (unlink (file));
} /* static int pidfile_remove (const char *file) */
#endif /* COLLECT_DAEMON */

int main (int argc, char **argv)
{
	struct sigaction sig_int_action;
	struct sigaction sig_term_action;
	struct sigaction sig_usr1_action;
	struct sigaction sig_pipe_action;
	char *configfile = CONFIGFILE;
	int test_config  = 0;
	int test_readall = 0;
	const char *basedir;
#if COLLECT_DAEMON
	struct sigaction sig_chld_action;
	pid_t pid;
	int daemonize    = 1;
#endif
	int exit_status = 0;

	/* read options */
	while (1)
	{
		int c;

		c = getopt (argc, argv, "htTC:"
#if COLLECT_DAEMON
				"fP:"
#endif
		);

		if (c == -1)
			break;

		switch (c)
		{
			case 'C':
				configfile = optarg;
				break;
			case 't':
				test_config = 1;
				break;
			case 'T':
				test_readall = 1;
				global_option_set ("ReadThreads", "-1");
#if COLLECT_DAEMON
				daemonize = 0;
#endif /* COLLECT_DAEMON */
				break;
#if COLLECT_DAEMON
			case 'P':
				global_option_set ("PIDFile", optarg);
				break;
			case 'f':
				daemonize = 0;
				break;
#endif /* COLLECT_DAEMON */
			case 'h':
				exit_usage (0);
				break;
			default:
				exit_usage (1);
		} /* switch (c) */
	} /* while (1) */

	if (optind < argc)
		exit_usage (1);

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
	if ((basedir = global_option_get ("BaseDir")) == NULL)
	{
		fprintf (stderr, "Don't have a basedir to use. This should not happen. Ever.");
		return (1);
	}
	else if (change_basedir (basedir))
	{
		fprintf (stderr, "Error: Unable to change to directory `%s'.\n", basedir);
		return (1);
	}

	/*
	 * Set global variables or, if that failes, exit. We cannot run with
	 * them being uninitialized. If nothing is configured, then defaults
	 * are being used. So this means that the user has actually done
	 * something wrong.
	 */
	if (init_global_variables () != 0)
		return (1);

	if (test_config)
		return (0);

#if COLLECT_DAEMON
	/*
	 * fork off child
	 */
	memset (&sig_chld_action, '\0', sizeof (sig_chld_action));
	sig_chld_action.sa_handler = SIG_IGN;
	sigaction (SIGCHLD, &sig_chld_action, NULL);

	if (daemonize)
	{
		if ((pid = fork ()) == -1)
		{
			/* error */
			char errbuf[1024];
			fprintf (stderr, "fork: %s",
					sstrerror (errno, errbuf,
						sizeof (errbuf)));
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
			ERROR ("Error: Could not connect `STDIN' to `/dev/null'");
			return (1);
		}
		if (dup (0) != 1)
		{
			ERROR ("Error: Could not connect `STDOUT' to `/dev/null'");
			return (1);
		}
		if (dup (0) != 2)
		{
			ERROR ("Error: Could not connect `STDERR' to `/dev/null'");
			return (1);
		}
	} /* if (daemonize) */
#endif /* COLLECT_DAEMON */

	memset (&sig_pipe_action, '\0', sizeof (sig_pipe_action));
	sig_pipe_action.sa_handler = SIG_IGN;
	sigaction (SIGPIPE, &sig_pipe_action, NULL);

	/*
	 * install signal handlers
	 */
	memset (&sig_int_action, '\0', sizeof (sig_int_action));
	sig_int_action.sa_handler = sig_int_handler;
	if (0 != sigaction (SIGINT, &sig_int_action, NULL)) {
		char errbuf[1024];
		ERROR ("Error: Failed to install a signal handler for signal INT: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (1);
	}

	memset (&sig_term_action, '\0', sizeof (sig_term_action));
	sig_term_action.sa_handler = sig_term_handler;
	if (0 != sigaction (SIGTERM, &sig_term_action, NULL)) {
		char errbuf[1024];
		ERROR ("Error: Failed to install a signal handler for signal TERM: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (1);
	}

	memset (&sig_usr1_action, '\0', sizeof (sig_usr1_action));
	sig_usr1_action.sa_handler = sig_usr1_handler;
	if (0 != sigaction (SIGUSR1, &sig_usr1_action, NULL)) {
		char errbuf[1024];
		ERROR ("Error: Failed to install a signal handler for signal USR1: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (1);
	}

	/*
	 * run the actual loops
	 */
	do_init ();

	if (test_readall)
	{
		if (plugin_read_all_once () != 0)
			exit_status = 1;
	}
	else
	{
		INFO ("Initialization complete, entering read-loop.");
		do_loop ();
	}

	/* close syslog */
	INFO ("Exiting normally.");

	do_shutdown ();

#if COLLECT_DAEMON
	if (daemonize)
		pidfile_remove ();
#endif /* COLLECT_DAEMON */

	return (exit_status);
} /* int main */
