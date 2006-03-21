/**
 * collectd - src/memory.c
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
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if defined(KERNEL_LINUX) || defined(HAVE_LIBKSTAT)
# define MEMORY_HAVE_READ 1
#else
# define MEMORY_HAVE_READ 0
#endif

#define MODULE_NAME "memory"

static char *memory_file = "memory.rrd";

/* 9223372036854775807 == LLONG_MAX */
static char *ds_def[] =
{
	"DS:used:GAUGE:"COLLECTD_HEARTBEAT":0:9223372036854775807",
	"DS:free:GAUGE:"COLLECTD_HEARTBEAT":0:9223372036854775807",
	"DS:buffers:GAUGE:"COLLECTD_HEARTBEAT":0:9223372036854775807",
	"DS:cached:GAUGE:"COLLECTD_HEARTBEAT":0:9223372036854775807",
	NULL
};
static int ds_num = 4;

#ifdef HAVE_LIBKSTAT
static int pagesize;
static kstat_t *ksp;
#endif /* HAVE_LIBKSTAT */

static void memory_init (void)
{
#ifdef HAVE_LIBKSTAT
	/* getpagesize(3C) tells me this does not fail.. */
	pagesize = getpagesize ();
	if (get_kstat (&ksp, "unix", 0, "system_pages"))
		ksp = NULL;
#endif /* HAVE_LIBKSTAT */

	return;
}

static void memory_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, memory_file, val, ds_def, ds_num);
}

#if MEMORY_HAVE_READ
#define BUFSIZE 512
static void memory_submit (long long mem_used, long long mem_buffered,
		long long mem_cached, long long mem_free)
{
	char buf[BUFSIZE];

	if (snprintf (buf, BUFSIZE, "%u:%lli:%lli:%lli:%lli",
				(unsigned int) curtime, mem_used, mem_free,
				mem_buffered, mem_cached) >= BUFSIZE)
		return;

	plugin_submit (MODULE_NAME, "-", buf);
}
#undef BUFSIZE

static void memory_read (void)
{
#ifdef KERNEL_LINUX
	FILE *fh;
	char buffer[1024];
	
	char *fields[8];
	int numfields;

	long long mem_used = 0;
	long long mem_buffered = 0;
	long long mem_cached = 0;
	long long mem_free = 0;

	if ((fh = fopen ("/proc/meminfo", "r")) == NULL)
	{
		syslog (LOG_WARNING, "memory: fopen: %s", strerror (errno));
		return;
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		long long *val = NULL;

		if (strncasecmp (buffer, "MemTotal:", 9) == 0)
			val = &mem_used;
		else if (strncasecmp (buffer, "MemFree:", 8) == 0)
			val = &mem_free;
		else if (strncasecmp (buffer, "Buffers:", 8) == 0)
			val = &mem_buffered;
		else if (strncasecmp (buffer, "Cached:", 7) == 0)
			val = &mem_cached;
		else
			continue;

		numfields = strsplit (buffer, fields, 8);

		if (numfields < 2)
			continue;

		*val = atoll (fields[1]) * 1024LL;
	}

	if (fclose (fh))
		syslog (LOG_WARNING, "memory: fclose: %s", strerror (errno));

	if (mem_used >= (mem_free + mem_buffered + mem_cached))
	{
		mem_used -= mem_free + mem_buffered + mem_cached;
		memory_submit (mem_used, mem_buffered, mem_cached, mem_free);
	}
/* #endif defined(KERNEL_LINUX) */

#elif defined(HAVE_LIBKSTAT)
	long long mem_used;
	long long mem_free;
	long long mem_lock;

	if (ksp == NULL)
		return;

	mem_used = get_kstat_value (ksp, "pagestotal");
	mem_free = get_kstat_value (ksp, "pagesfree");
	mem_lock = get_kstat_value (ksp, "pageslocked");

	if ((mem_used < 0LL) || (mem_free < 0LL) || (mem_lock < 0LL))
		return;
	if (mem_used < (mem_free + mem_lock))
		return;

	mem_used -= mem_free + mem_lock;
	mem_used *= pagesize; /* If this overflows you have some serious */
	mem_free *= pagesize; /* memory.. Why not call me up and give me */
	mem_lock *= pagesize; /* some? ;) */

	memory_submit (mem_used, mem_lock, 0LL, mem_free);
/* #endif defined(HAVE_LIBKSTAT) */

#elif defined(HAVE_LIBSTATGRAB)
	sg_mem_stats *ios;

	if ((ios = sg_get_mem_stats ()) != NULL)
		memory_submit (ios->used, 0LL, ios->cache, ios->free);
#endif /* HAVE_LIBSTATGRAB */
}
#else
# define memory_read NULL
#endif /* MEMORY_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, memory_init, memory_read, memory_write);
}

#undef MODULE_NAME
