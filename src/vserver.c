/**
 * collectd - src/vserver.c
 * Copyright (C) 2006  Sebastian Harl
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
 *   Sebastian Harl <sh at tokkee.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <unistd.h>

#define BUFSIZE 512

#define MODULE_NAME "vserver"
#define PROCDIR "/proc/virtual"

#if defined(KERNEL_LINUX)
# define VSERVER_HAVE_READ 1
#else
# define VSERVER_HAVE_READ 0
#endif /* defined(KERNEL_LINUX) */

static char *rrd_socket	= "vserver-%s/socket.rrd";
static char *rrd_thread	= "vserver-%s/threads.rrd";
static char *rrd_load	= "vserver-%s/load.rrd";
static char *rrd_procs	= "vserver-%s/processes.rrd";
static char *rrd_memory	= "vserver-%s/memory.rrd";

/* 9223372036854775807 == LLONG_MAX */
/* bytes transferred */
static char *ds_def_socket[] =
{
	"DS:unix_in:COUNTER:25:0:9223372036854775807",
	"DS:unix_out:COUNTER:25:0:9223372036854775807",
	"DS:inet_in:COUNTER:25:0:9223372036854775807",
	"DS:inet_out:COUNTER:25:0:9223372036854775807",
	"DS:inet6_in:COUNTER:25:0:9223372036854775807",
	"DS:inet6_out:COUNTER:25:0:9223372036854775807",
	"DS:other_in:COUNTER:25:0:9223372036854775807",
	"DS:other_out:COUNTER:25:0:9223372036854775807",
	"DS:unspec_in:COUNTER:25:0:9223372036854775807",
	"DS:unspec_out:COUNTER:25:0:9223372036854775807",
	NULL
};
static int ds_num_socket = 10;

static char *ds_def_threads[] =
{
	"DS:total:GAUGE:25:0:65535",
	"DS:running:GAUGE:25:0:65535",
	"DS:uninterruptible:GAUGE:25:0:65535",
	"DS:onhold:GAUGE:25:0:65535",
	NULL
};
static int ds_num_threads = 4;

static char *ds_def_load[] =
{
	"DS:avg1:GAUGE:25:0:100",
	"DS:avg5:GAUGE:25:0:100",
	"DS:avg15:GAUGE:25:0:100",
	NULL
};
static int ds_num_load = 3;

static char *ds_def_procs[] =
{
	"DS:total:GAUGE:25:0:65535",
	NULL
};
static int ds_num_procs = 1;

/* 9223372036854775807 == LLONG_MAX */
/* bytes */
static char *ds_def_memory[] =
{
	"DS:vm:GAUGE:25:0:9223372036854775807",
	"DS:vml:GAUGE:25:0:9223372036854775807",
	"DS:rss:GAUGE:25:0:9223372036854775807",
	"DS:anon:GAUGE:25:0:9223372036854775807",
	NULL
};
static int ds_num_memory = 4;

static int pagesize = 0;

static void vserver_init (void)
{
	/* XXX Should we check for getpagesize () in configure?
	 * What's the right thing to do, if there is no getpagesize ()? */
	pagesize = getpagesize ();
	return;
} /* static void vserver_init(void) */

static void vserver_socket_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_socket, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_socket, ds_num_socket);
	return;
} /* static void vserver_socket_write(char *host, char *inst, char *val) */

static void vserver_threads_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_thread, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_threads, ds_num_threads);
	return;
} /* static void vserver_threads_write(char *host, char *inst, char *val) */

static void vserver_load_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_load, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_load, ds_num_load);
	return;
} /* static void vserver_load_write(char *host, char *inst, char *val) */

static void vserver_procs_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_procs, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_procs, ds_num_procs);
	return;
} /* static void vserver_procs_write(char *host, char *inst, char *val) */

static void vserver_memory_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_memory, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_memory, ds_num_memory);
	return;
} /* static void vserver_memory_write(char *host, char *inst, char *val) */

#if VSERVER_HAVE_READ
static void vserver_submit (char *inst, long long unix_in, long long unix_out, 
		long long inet_in, long long inet_out, long long inet6_in,
		long long inet6_out, long long other_in, long long other_out,
		long long unspec_in, long long unspec_out, int t_total, int t_running, 
		int t_uninterruptible, int t_onhold, double avg1, double avg5, 
		double avg15, int p_total, long long vm, long long vml, long long rss, 
		long long anon)
{
	int  len;
	char buffer[BUFSIZE];

	len = snprintf (buffer, BUFSIZE, 
			"N:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld:%lld",
			unix_in, unix_out, inet_in, inet_out, inet6_in, inet6_out, 
			other_in, other_out, unspec_in, unspec_out);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_socket", inst, buffer);


	len = snprintf (buffer, BUFSIZE, "N:%d:%d:%d:%d",
			t_total, t_running, t_uninterruptible, t_onhold);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_threads", inst, buffer);


	len = snprintf (buffer, BUFSIZE, "N:%.2f:%.2f:%.2f",
			avg1, avg5, avg15);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_load", inst, buffer);


	len = snprintf (buffer, BUFSIZE, "N:%d",
			p_total);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_procs", inst, buffer);


	len = snprintf (buffer, BUFSIZE, "N:%lld:%lld:%lld:%lld",
			vm, vml, rss, anon);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_memory", inst, buffer);
	return;
} /* static void vserver_submit() */

static inline long long __get_sock_bytes(const char *s)
{
	while (s[0] != '/')
		++s;

	/* Remove '/' */
	++s;
	return atoll(s);
}

static void vserver_read (void)
{
	DIR 			*proc;
	struct dirent 	*dent; /* 42 */

	errno = 0;
	if (NULL == (proc = opendir (PROCDIR))) {
		syslog (LOG_ERR, "Cannot open '%s': %s", PROCDIR, strerror (errno));
		return;
	}

	while (NULL != (dent = readdir (proc))) {
		int  len;
		char file[BUFSIZE];

		FILE *fh;
		char buffer[BUFSIZE];

		char *cols[4];

		long long	socket[10]	= {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
		int			threads[4]	= {-1, -1, -1, -1};
		double		load[3]		= {-1, -1, -1};
		/* Just to be consistent ;-) */
		int			procs[1]	= {-1};
		long long	memory[4]	= {-1, -1, -1, -1};

		if (dent->d_name[0] == '.')
			continue;

		/* XXX This check is just the result of a trial-and-error test.
		 * I did not find any documentation describing the d_type field. */
		if (!(dent->d_type & 0x4))
			/* This is not a directory */
			continue;

		/* socket message accounting */
		len = snprintf (file, BUFSIZE, PROCDIR "/%s/cacct", dent->d_name);
		if ((len < 0) || (len >= BUFSIZE))
			continue;

		if (NULL == (fh = fopen (file, "r"))) {
			syslog (LOG_ERR, "Cannot open '%s': %s", file, strerror (errno));
			continue;
		}

		while (NULL != fgets (buffer, BUFSIZE, fh)) {
			if (strsplit (buffer, cols, 3) < 3)
				continue;

			if (0 == strcmp (cols[0], "UNIX:")) {
				socket[0] = __get_sock_bytes (cols[1]);
				socket[1] = __get_sock_bytes (cols[2]);
			}
			else if (0 == strcmp (cols[0], "INET:")) {
				socket[2] = __get_sock_bytes (cols[1]);
				socket[3] = __get_sock_bytes (cols[2]);
			}
			else if (0 == strcmp (cols[0], "INET6:")) {
				socket[4] = __get_sock_bytes (cols[1]);
				socket[5] = __get_sock_bytes (cols[2]);
			}
			else if (0 == strcmp (cols[0], "OTHER:")) {
				socket[6] = __get_sock_bytes (cols[1]);
				socket[7] = __get_sock_bytes (cols[2]);
			}
			else if (0 == strcmp (cols[0], "UNSPEC:")) {
				socket[8] = __get_sock_bytes (cols[1]);
				socket[9] = __get_sock_bytes (cols[2]);
			}
		}

		fclose (fh);

		/* thread information and load */
		len = snprintf (file, BUFSIZE, PROCDIR "/%s/cvirt", dent->d_name);
		if ((len < 0) || (len >= BUFSIZE))
			continue;

		if (NULL == (fh = fopen (file, "r"))) {
			syslog (LOG_ERR, "Cannot open '%s': %s", file, strerror (errno));
			continue;
		}

		while (NULL != fgets (buffer, BUFSIZE, fh)) {
			int n = strsplit (buffer, cols, 4);

			if (2 == n) {
				if (0 == strcmp (cols[0], "nr_threads:")) {
					threads[0] = atoi (cols[1]);
				}
				else if (0 == strcmp (cols[0], "nr_running:")) {
					threads[1] = atoi (cols[1]);
				}
				else if (0 == strcmp (cols[0], "nr_unintr:")) {
					threads[2] = atoi (cols[1]);
				}
				else if (0 == strcmp (cols[0], "nr_onhold:")) {
					threads[3] = atoi (cols[1]);
				}
			}
			else if (4 == n) {
				if (0 == strcmp (cols[0], "loadavg:")) {
					load[0] = atof (cols[1]);
					load[1] = atof (cols[2]);
					load[2] = atof (cols[3]);
				}
			}
		}

		fclose (fh);

		/* processes and memory usage */
		len = snprintf (file, BUFSIZE, PROCDIR "/%s/limit", dent->d_name);
		if ((len < 0) || (len >= BUFSIZE))
			continue;

		if (NULL == (fh = fopen (file, "r"))) {
			syslog (LOG_ERR, "Cannot open '%s': %s", file, strerror (errno));
			continue;
		}

		while (NULL != fgets (buffer, BUFSIZE, fh)) {
			if (strsplit (buffer, cols, 2) < 2)
				continue;

			if (0 == strcmp (cols[0], "PROC:")) {
				procs[0] = atoi (cols[1]);
			}
			else if (0 == strcmp (cols[0], "VM:")) {
				memory[0] = atoll (cols[1]) * pagesize;
			}
			else if (0 == strcmp (cols[0], "VML:")) {
				memory[1] = atoll (cols[1]) * pagesize;
			}
			else if (0 == strcmp (cols[0], "RSS:")) {
				memory[2] = atoll (cols[1]) * pagesize;
			}
			else if (0 == strcmp (cols[0], "ANON:")) {
				memory[3] = atoll (cols[1]) * pagesize;
			}
		}

		fclose (fh);

		/* XXX What to do in case of an error (i.e. some value is
		 * still -1)? */

		vserver_submit (dent->d_name, socket[0], socket[1], socket[2], 
				socket[3], socket[4], socket[5], socket[6], socket[7],
				socket[8], socket[9], threads[0], threads[1], threads[2],
				threads[3], load[0], load[1], load[2], procs[0], memory[0],
				memory[1], memory[2], memory[3]);
	}

	closedir (proc);
	return;
} /* static void vserver_read(void) */
#else
# define vserver_read NULL
#endif /* VSERVER_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, vserver_init, vserver_read, NULL);
	plugin_register ("vserver_socket", NULL, NULL, vserver_socket_write);
	plugin_register ("vserver_threads", NULL, NULL, vserver_threads_write);
	plugin_register ("vserver_load", NULL, NULL, vserver_load_write);
	plugin_register ("vserver_procs", NULL, NULL, vserver_procs_write);
	plugin_register ("vserver_memory", NULL, NULL, vserver_memory_write);
	return;
} /* void module_register(void) */

/* vim: set ts=4 sw=4 noexpandtab : */
