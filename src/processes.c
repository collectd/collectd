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
#include "utils_debug.h"

#if HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
#endif

#define MODULE_NAME "processes"

#if defined(KERNEL_LINUX) || defined(HAVE_SYSCTLBYNAME)
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
/* #endif defined(KERNEL_LINUX) */

#elif HAVE_SYSCTLBYNAME
	int mib[3];
	size_t len;
	size_t num;
	int i;
	int tries;
	struct kinfo_proc *kp;

	unsigned int state_idle   = 0;
	unsigned int state_run    = 0;
	unsigned int state_sleep  = 0;
	unsigned int state_stop   = 0;
	unsigned int state_zombie = 0;

	mib[0] = CTL_KERN;
	mib[1] = KERN_PROC;
	mib[2] = KERN_PROC_ALL;

	tries = 0;
	kp    = NULL;
	while (1)
	{
		if (tries >= 3)
			return;
		tries++;

		len = 0;
		if (sysctl(mib, 3, NULL, &len, NULL, 0) != 0)
		{
			syslog (LOG_ERR, "processes: sysctl failed: %s",
					strerror (errno));
			return;
		}

		if ((kp = (struct kinfo_proc *) malloc (len)) == NULL)
		{
			syslog (LOG_ERR, "processes: malloc failed: %s",
					strerror (errno));
			return;
		}

		if (sysctl(mib, 3, (void *) kp, &len, NULL, 0) != 0)
		{
			syslog (LOG_WARNING, "processes: sysctl failed: %s",
					strerror (errno));
			free (kp);
			kp = NULL;
			continue;
		}

		break;
	} /* while true */

	/* If we get past the while-loop, `kp' containes a valid `struct
	 * kinfo_proc'. */

	num = len / sizeof (struct kinfo_proc);

	for (i = 0; i < num; i++)
	{
		DBG ("%3i: Process %i is in state %i", i,
				(int) kp[i].kp_proc.p_pid,
			       	(int) kp[i].kp_proc.p_stat);

		switch (kp[i].kp_proc.p_stat)
		{
			case SIDL:
				state_idle++;
				break;

			case SRUN:
				state_run++;
				break;

			case SSLEEP:
#ifdef P_SINTR
				if ((kp[i].kp_proc.p_flag & P_SINTR) == 0)
					state_sleep++; /* TODO change this to `state_blocked' or something.. */
				else
#endif /* P_SINTR */
					state_sleep++;
				break;

			case SSTOP:
				state_stop++;
				break;

			case SZOMB:
				state_zombie++;
				break;

			default:
				syslog (LOG_WARNING, "processes: PID %i in unknown state 0x%2x",
						(int) kp[i].kp_proc.p_pid,
						(int) kp[i].kp_proc.p_stat);
		} /* switch (state) */
	} /* for (i = 0 .. num-1) */

	free (kp);

	if (state_run || state_idle || state_sleep || state_zombie)
		ps_submit (state_run, state_idle + state_sleep, state_zombie,
				state_stop, -1, -1);
#endif /* HAVE_SYSCTLBYNAME */
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
