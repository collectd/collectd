/**
 * collectd - src/processes.c
 * Copyright (C) 2005  Lyonel Vincent
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
 *   Lyonel Vincent <lyonel at ezix.org>
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#define MODULE_NAME "processes"

#ifdef KERNEL_LINUX
# define PROCESSES_HAVE_READ 1
#else
# define PROCESSES_HAVE_READ 0
#endif

#define BUFSIZE 256

static char *ps_file = "processes.rrd";

static char *ds_def[] =
{
	"DS:running:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:sleeping:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:zombies:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:stopped:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:paging:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	"DS:blocked:GAUGE:"COLLECTD_HEARTBEAT":0:65535",
	NULL
};
static int ds_num = 6;

static void ps_init (void)
{
}

static void ps_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, ps_file, val, ds_def, ds_num);
}

#if PROCESSES_HAVE_READ
static void ps_submit (unsigned int running,
		unsigned int sleeping,
		unsigned int zombies,
		unsigned int stopped,
		unsigned int paging,
		unsigned int blocked)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%u:%u:%u:%u:%u:%u",
				(unsigned int) curtime,
				running, sleeping, zombies, stopped, paging,
				blocked) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, "-", buf);
}

static void ps_read (void)
{
#ifdef KERNEL_LINUX
	unsigned int running, sleeping, zombies, stopped, paging, blocked;

	char buf[BUFSIZE];
	char filename[20]; /* need 17 bytes */
	char *fields[BUFSIZE];

	struct dirent *ent;
	DIR *proc;
	FILE *fh;

	running = sleeping = zombies = stopped = paging = blocked = 0;

	if ((proc = opendir ("/proc")) == NULL)
	{
		syslog (LOG_ERR, "Cannot open `/proc': %s", strerror (errno));
		return;
	}

	while ((ent = readdir (proc)) != NULL)
	{
		if (!isdigit (ent->d_name[0]))
			continue;

		if (snprintf (filename, 20, "/proc/%s/stat", ent->d_name) >= 20)
			continue;

		if ((fh = fopen (filename, "r")) == NULL)
		{
			syslog (LOG_ERR, "Cannot open `%s': %s", filename, strerror (errno));
			continue;
		}

		if (fgets (buf, BUFSIZE, fh) == NULL)
		{
			fclose (fh);
			continue;
		}

		fclose (fh);

		if (strsplit (buf, fields, BUFSIZE) < 3)
			continue;

		switch (fields[2][0])
		{
			case 'R': running++;  break;
			case 'S': sleeping++; break;
			case 'D': blocked++;  break;
			case 'Z': zombies++;  break;
			case 'T': stopped++;  break;
			case 'W': paging++;   break;
		}
	}

	closedir(proc);

	ps_submit (running, sleeping, zombies, stopped, paging, blocked);
#endif /* defined(KERNEL_LINUX) */
}
#else
# define ps_read NULL
#endif /* PROCESSES_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, ps_init, ps_read, ps_write);
}

#undef BUFSIZE
#undef MODULE_NAME
