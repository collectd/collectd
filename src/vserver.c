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

#include "vserver.h"

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

#if defined(KERNEL_LINUX)
# define VSERVER_HAVE_READ 1
#else
# define VSERVER_HAVE_READ 0
#endif /* defined(KERNEL_LINUX) */

static char *rrd_unix	= "vserver-%s/traffic-unix.rrd";
static char *rrd_inet	= "vserver-%s/traffic-inet.rrd";
static char *rrd_inet6	= "vserver-%s/traffic-inet6.rrd";
static char *rrd_other	= "vserver-%s/traffic-other.rrd";
static char *rrd_unspec	= "vserver-%s/traffic-unspec.rrd";

static char *rrd_thread	= "vserver-%s/threads.rrd";

static char *rrd_load	= "vserver-%s/load.rrd";

static char *rrd_procs	= "vserver-%s/vs_processes.rrd";
static char *rrd_memory	= "vserver-%s/vs_memory.rrd";

/* 9223372036854775807 == LLONG_MAX */
/* bytes transferred */
static char *ds_def_unix[] =
{
	"DS:incoming:COUNTER:25:0:9223372036854775807",
	"DS:outgoing:COUNTER:25:0:9223372036854775807",
	"DS:failed:COUNTER:25:0:9223372036854775807",
	NULL
};
static int ds_num_unix = 3;

static char *ds_def_inet[] =
{
	"DS:incoming:COUNTER:25:0:9223372036854775807",
	"DS:outgoing:COUNTER:25:0:9223372036854775807",
	"DS:failed:COUNTER:25:0:9223372036854775807",
	NULL
};
static int ds_num_inet = 3;

static char *ds_def_inet6[] =
{
	"DS:incoming:COUNTER:25:0:9223372036854775807",
	"DS:outgoing:COUNTER:25:0:9223372036854775807",
	"DS:failed:COUNTER:25:0:9223372036854775807",
	NULL
};
static int ds_num_inet6 = 3;

static char *ds_def_other[] =
{
	"DS:incoming:COUNTER:25:0:9223372036854775807",
	"DS:outgoing:COUNTER:25:0:9223372036854775807",
	"DS:failed:COUNTER:25:0:9223372036854775807",
	NULL
};
static int ds_num_other = 3;

static char *ds_def_unspec[] =
{
	"DS:incoming:COUNTER:25:0:9223372036854775807",
	"DS:outgoing:COUNTER:25:0:9223372036854775807",
	"DS:failed:COUNTER:25:0:9223372036854775807",
	NULL
};
static int ds_num_unspec = 3;

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
	"DS:shortterm:GAUGE:25:0:100",
	"DS:midterm:GAUGE:25:0:100",
	"DS:longterm:GAUGE:25:0:100",
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

static void vserver_unix_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_unix, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_unix, ds_num_unix);
	return;
} /* static void vserver_unix_write(char *host, char *inst, char *val) */

static void vserver_inet_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_inet, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_inet, ds_num_inet);
	return;
} /* static void vserver_inet_write(char *host, char *inst, char *val) */

static void vserver_inet6_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_inet6, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_inet6, ds_num_inet6);
	return;
} /* static void vserver_inet6_write(char *host, char *inst, char *val) */

static void vserver_other_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_other, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_other, ds_num_other);
	return;
} /* static void vserver_other_write(char *host, char *inst, char *val) */

static void vserver_unspec_write (char *host, char *inst, char *val)
{
	int  len;
	char filename[BUFSIZE];

	len = snprintf (filename, BUFSIZE, rrd_unspec, inst);
	if ((len > 0) && (len < BUFSIZE))
		rrd_update_file (host, filename, val, ds_def_unspec, ds_num_unspec);
	return;
} /* static void vserver_unspec_write(char *host, char *inst, char *val) */

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
		long long unix_failed, long long inet_in, long long inet_out, 
		long long inet_failed, long long inet6_in, long long inet6_out, 
		long long inet6_failed, long long other_in, long long other_out,
		long long other_failed, long long unspec_in, long long unspec_out, 
		long long unspec_failed, int t_total, int t_running, 
		int t_uninterruptible, int t_onhold, double avg1, double avg5, 
		double avg15, int p_total, long long vm, long long vml, long long rss, 
		long long anon)
{
	int  len;
	char buffer[BUFSIZE];

	len = snprintf (buffer, BUFSIZE, 
			"N:%lld:%lld:%lld", unix_in, unix_out, unix_failed);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_unix", inst, buffer);


	len = snprintf (buffer, BUFSIZE, 
			"N:%lld:%lld:%lld", inet_in, inet_out, inet_failed);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_inet", inst, buffer);


	len = snprintf (buffer, BUFSIZE, 
			"N:%lld:%lld:%lld", inet6_in, inet6_out, inet6_failed);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_inet6", inst, buffer);


	len = snprintf (buffer, BUFSIZE, 
			"N:%lld:%lld:%lld", other_in, other_out, other_failed);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_other", inst, buffer);


	len = snprintf (buffer, BUFSIZE, 
			"N:%lld:%lld:%lld", unspec_in, unspec_out, unspec_failed);

	if ((len > 0) && (len < BUFSIZE))
		plugin_submit ("vserver_unspec", inst, buffer);


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

		long long	unix_s[3]	= {-1, -1, -1};
		long long	inet[3]		= {-1, -1, -1};
		long long	inet6[3]	= {-1, -1, -1};
		long long	other[3]	= {-1, -1, -1};
		long long	unspec[3]	= {-1, -1, -1};
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
			if (strsplit (buffer, cols, 4) < 4)
				continue;

			if (0 == strcmp (cols[0], "UNIX:")) {
				unix_s[0] = __get_sock_bytes (cols[1]);
				unix_s[1] = __get_sock_bytes (cols[2]);
				unix_s[2] = __get_sock_bytes (cols[3]);
			}
			else if (0 == strcmp (cols[0], "INET:")) {
				inet[0] = __get_sock_bytes (cols[1]);
				inet[1] = __get_sock_bytes (cols[2]);
				inet[2] = __get_sock_bytes (cols[3]);
			}
			else if (0 == strcmp (cols[0], "INET6:")) {
				inet6[0] = __get_sock_bytes (cols[1]);
				inet6[1] = __get_sock_bytes (cols[2]);
				inet6[2] = __get_sock_bytes (cols[3]);
			}
			else if (0 == strcmp (cols[0], "OTHER:")) {
				other[0] = __get_sock_bytes (cols[1]);
				other[1] = __get_sock_bytes (cols[2]);
				other[2] = __get_sock_bytes (cols[3]);
			}
			else if (0 == strcmp (cols[0], "UNSPEC:")) {
				unspec[0] = __get_sock_bytes (cols[1]);
				unspec[1] = __get_sock_bytes (cols[2]);
				unspec[2] = __get_sock_bytes (cols[3]);
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

		vserver_submit (dent->d_name, unix_s[0], unix_s[1], unix_s[2], 
				inet[0], inet[1], inet[2], inet6[0], inet6[1], inet6[2], 
				other[0], other[1], other[2], unspec[0], unspec[1], unspec[2],
				threads[0], threads[1], threads[2], threads[3], load[0], 
				load[1], load[2], procs[0], memory[0], memory[1], memory[2], 
				memory[3]);
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
	plugin_register ("vserver_unix", NULL, NULL, vserver_unix_write);
	plugin_register ("vserver_inet", NULL, NULL, vserver_inet_write);
	plugin_register ("vserver_inet6", NULL, NULL, vserver_inet6_write);
	plugin_register ("vserver_other", NULL, NULL, vserver_other_write);
	plugin_register ("vserver_unspec", NULL, NULL, vserver_unspec_write);
	plugin_register ("vserver_threads", NULL, NULL, vserver_threads_write);
	plugin_register ("vserver_load", NULL, NULL, vserver_load_write);
	plugin_register ("vserver_procs", NULL, NULL, vserver_procs_write);
	plugin_register ("vserver_memory", NULL, NULL, vserver_memory_write);
	return;
} /* void module_register(void) */

/* vim: set ts=4 sw=4 noexpandtab : */
