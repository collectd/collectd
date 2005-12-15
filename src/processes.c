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

#include "processes.h"

#if COLLECT_PROCESSES
#define MODULE_NAME "processes"

#include "common.h"
#include "plugin.h"

static char *ps_file = "processes.rrd";

static char *ds_def[] =
{
	"DS:running:GAUGE:25:0:65535",
	"DS:sleeping:GAUGE:25:0:65535",
	"DS:zombies:GAUGE:25:0:65535",
	"DS:stopped:GAUGE:25:0:65535",
	"DS:paging:GAUGE:25:0:65535",
	"DS:blocked:GAUGE:25:0:65535",
	NULL
};
static int ds_num = 6;

void ps_init (void)
{
}

void ps_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, ps_file, val, ds_def, ds_num);
}

#define BUFSIZE 256
void ps_submit (unsigned int running,
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

void ps_read (void)
{
#ifdef KERNEL_LINUX
	unsigned int running, sleeping, zombies, stopped, paging, blocked;

	char buf[BUFSIZE];
	char filename[20]; /* need 17 bytes */
	char *fields[256];

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

		if (strsplit (buf, fields, 256) < 3)
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
#undef BUFSIZE

void module_register (void)
{
	plugin_register (MODULE_NAME, ps_init, ps_read, ps_write);
}

#undef MODULE_NAME
#endif /* COLLECT_PROCESSES */
