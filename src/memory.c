/**
 * collectd - src/memory.c
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
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_debug.h"

#ifdef HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
#endif

#ifdef HAVE_MACH_KERN_RETURN_H
# include <mach/kern_return.h>
#endif
#ifdef HAVE_MACH_MACH_INIT_H
# include <mach/mach_init.h>
#endif
#ifdef HAVE_MACH_MACH_HOST_H
# include <mach/mach_host.h>
#endif
#ifdef HAVE_MACH_HOST_PRIV_H
# include <mach/host_priv.h>
#endif
#ifdef MACH_VM_STATISTICS_H
# include <mach/vm_statistics.h>
#endif

#if defined (HOST_VM_INFO) || HAVE_SYSCTLBYNAME || KERNEL_LINUX || HAVE_LIBKSTAT
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

/* vm_statistics_data_t */
#if defined(HOST_VM_INFO)
static mach_port_t port_host;
static vm_size_t pagesize;
/* #endif HOST_VM_INFO */

#elif HAVE_SYSCTLBYNAME
/* no global variables */
/* #endif HAVE_SYSCTLBYNAME */

#elif KERNEL_LINUX
/* no global variables */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
static int pagesize;
static kstat_t *ksp;
#endif /* HAVE_LIBKSTAT */

static void memory_init (void)
{
#if defined(HOST_VM_INFO)
	port_host = mach_host_self ();
	host_page_size (port_host, &pagesize);
/* #endif HOST_VM_INFO */

#elif HAVE_SYSCTLBYNAME
/* no init stuff */
/* #endif HAVE_SYSCTLBYNAME */

#elif defined(KERNEL_LINUX)
/* no init stuff */
/* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBKSTAT)
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
#if defined(HOST_VM_INFO)
	kern_return_t status;
	vm_statistics_data_t   vm_data;
	mach_msg_type_number_t vm_data_len;

	long long wired;
	long long active;
	long long inactive;
	long long free;

	if (!port_host || !pagesize)
		return;

	vm_data_len = sizeof (vm_data) / sizeof (natural_t);
	if ((status = host_statistics (port_host, HOST_VM_INFO,
					(host_info_t) &vm_data,
					&vm_data_len)) != KERN_SUCCESS)
	{
		syslog (LOG_ERR, "memory-plugin: host_statistics failed and returned the value %i", (int) status);
		return;
	}

	/*
	 * From <http://docs.info.apple.com/article.html?artnum=107918>:
	 *
	 * Wired memory
	 *   This information can't be cached to disk, so it must stay in RAM.
	 *   The amount depends on what applications you are using.
	 *
	 * Active memory
	 *   This information is currently in RAM and actively being used.
	 *
	 * Inactive memory
	 *   This information is no longer being used and has been cached to
	 *   disk, but it will remain in RAM until another application needs
	 *   the space. Leaving this information in RAM is to your advantage if
	 *   you (or a client of your computer) come back to it later.
	 *
	 * Free memory
	 *   This memory is not being used.
	 */

	wired    = vm_data.wire_count     * pagesize;
	active   = vm_data.active_count   * pagesize;
	inactive = vm_data.inactive_count * pagesize;
	free     = vm_data.free_count     * pagesize;

	memory_submit (wired + active, -1, inactive, free);
/* #endif HOST_VM_INFO */

#elif HAVE_SYSCTLBYNAME
	/*
	 * vm.stats.vm.v_page_size: 4096
	 * vm.stats.vm.v_page_count: 246178
	 * vm.stats.vm.v_free_count: 28760
	 * vm.stats.vm.v_wire_count: 37526
	 * vm.stats.vm.v_active_count: 55239
	 * vm.stats.vm.v_inactive_count: 113730
	 * vm.stats.vm.v_cache_count: 10809
	 */
	char *sysctl_keys[8] =
	{
		"vm.stats.vm.v_page_size",
		"vm.stats.vm.v_page_count",
		"vm.stats.vm.v_free_count",
		"vm.stats.vm.v_wire_count",
		"vm.stats.vm.v_active_count",
		"vm.stats.vm.v_inactive_count",
		"vm.stats.vm.v_cache_count",
		NULL
	};
	int sysctl_vals[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };

	size_t len;
	int    i;
	int    status;

	for (i = 0; sysctl_keys[i] != NULL; i++)
	{
		len = sizeof (int);
		if ((status = sysctlbyname (sysctl_keys[i],
						(void *) &sysctl_vals[i], &len,
						NULL, 0)) < 0)
		{
			syslog (LOG_ERR, "memory plugin: sysctlbyname (%s): %s",
					sysctl_keys[i], strerror (errno));
			return;
		}
		DBG ("%26s: %6i", sysctl_keys[i], sysctl_vals[i]);
	} /* for i */

	/* multiply all all page counts with the pagesize */
	for (i = 1; sysctl_keys[i] != NULL; i++)
		sysctl_vals[i] = sysctl_vals[i] * sysctl_vals[0];

	memory_submit (sysctl_vals[3] + sysctl_vals[4], /* wired + active */
			sysctl_vals[6],                 /* cache */
			sysctl_vals[5],                 /* inactive */
			sysctl_vals[2]);                /* free */
/* #endif HAVE_SYSCTLBYNAME */

#elif defined(KERNEL_LINUX)
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
