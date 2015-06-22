/**
 * collectd - src/memory.c
 * Copyright (C) 2005-2008  Florian octo Forster
 * Copyright (C) 2009       Simon Kuhnle
 * Copyright (C) 2009       Manuel Sanmartin
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
 *   Simon Kuhnle <simon at blarzwurst.de>
 *   Manuel Sanmartin
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

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
#ifdef HAVE_MACH_VM_STATISTICS_H
# include <mach/vm_statistics.h>
#endif

#if HAVE_STATGRAB_H
# include <statgrab.h>
#endif

#if HAVE_PERFSTAT
# include <sys/protosw.h>
# include <libperfstat.h>
#endif /* HAVE_PERFSTAT */

/* vm_statistics_data_t */
#if HAVE_HOST_STATISTICS
static mach_port_t port_host;
static vm_size_t pagesize;
/* #endif HAVE_HOST_STATISTICS */

#elif HAVE_SYSCTLBYNAME
/* no global variables */
/* #endif HAVE_SYSCTLBYNAME */

#elif KERNEL_LINUX
/* no global variables */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
static int pagesize;
static kstat_t *ksp;
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SYSCTL
static int pagesize;
/* #endif HAVE_SYSCTL */

#elif HAVE_LIBSTATGRAB
/* no global variables */
/* endif HAVE_LIBSTATGRAB */
#elif HAVE_PERFSTAT
static int pagesize;
static perfstat_memory_total_t pmemory;
/* endif HAVE_PERFSTAT */
#else
# error "No applicable input method."
#endif

static int memory_init (void)
{
#if HAVE_HOST_STATISTICS
	port_host = mach_host_self ();
	host_page_size (port_host, &pagesize);
/* #endif HAVE_HOST_STATISTICS */

#elif HAVE_SYSCTLBYNAME
/* no init stuff */
/* #endif HAVE_SYSCTLBYNAME */

#elif defined(KERNEL_LINUX)
/* no init stuff */
/* #endif KERNEL_LINUX */

#elif defined(HAVE_LIBKSTAT)
	/* getpagesize(3C) tells me this does not fail.. */
	pagesize = getpagesize ();
	if (get_kstat (&ksp, "unix", 0, "system_pages") != 0)
	{
		ksp = NULL;
		return (-1);
	}
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SYSCTL
	pagesize = getpagesize ();
	if (pagesize <= 0)
	{
		ERROR ("memory plugin: Invalid pagesize: %i", pagesize);
		return (-1);
	}
/* #endif HAVE_SYSCTL */

#elif HAVE_LIBSTATGRAB
/* no init stuff */
/* #endif HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
	pagesize = getpagesize ();
#endif /* HAVE_PERFSTAT */
	return (0);
} /* int memory_init */

static void memory_submit (const char *type_instance, gauge_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "memory", sizeof (vl.plugin));
	sstrncpy (vl.type, "memory", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static int memory_read (void)
{
#if HAVE_HOST_STATISTICS
	kern_return_t status;
	vm_statistics_data_t   vm_data;
	mach_msg_type_number_t vm_data_len;

	gauge_t wired;
	gauge_t active;
	gauge_t inactive;
	gauge_t free;

	if (!port_host || !pagesize)
		return (-1);

	vm_data_len = sizeof (vm_data) / sizeof (natural_t);
	if ((status = host_statistics (port_host, HOST_VM_INFO,
					(host_info_t) &vm_data,
					&vm_data_len)) != KERN_SUCCESS)
	{
		ERROR ("memory-plugin: host_statistics failed and returned the value %i", (int) status);
		return (-1);
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

	wired    = (gauge_t) (((uint64_t) vm_data.wire_count)     * ((uint64_t) pagesize));
	active   = (gauge_t) (((uint64_t) vm_data.active_count)   * ((uint64_t) pagesize));
	inactive = (gauge_t) (((uint64_t) vm_data.inactive_count) * ((uint64_t) pagesize));
	free     = (gauge_t) (((uint64_t) vm_data.free_count)     * ((uint64_t) pagesize));

	memory_submit ("wired",    wired);
	memory_submit ("active",   active);
	memory_submit ("inactive", inactive);
	memory_submit ("free",     free);
/* #endif HAVE_HOST_STATISTICS */

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
	double sysctl_vals[8];

	int    i;

	for (i = 0; sysctl_keys[i] != NULL; i++)
	{
		int value;
		size_t value_len = sizeof (value);

		if (sysctlbyname (sysctl_keys[i], (void *) &value, &value_len,
					NULL, 0) == 0)
		{
			sysctl_vals[i] = value;
			DEBUG ("memory plugin: %26s: %g", sysctl_keys[i], sysctl_vals[i]);
		}
		else
		{
			sysctl_vals[i] = NAN;
		}
	} /* for (sysctl_keys) */

	/* multiply all all page counts with the pagesize */
	for (i = 1; sysctl_keys[i] != NULL; i++)
		if (!isnan (sysctl_vals[i]))
			sysctl_vals[i] *= sysctl_vals[0];

	memory_submit ("free",     sysctl_vals[2]);
	memory_submit ("wired",    sysctl_vals[3]);
	memory_submit ("active",   sysctl_vals[4]);
	memory_submit ("inactive", sysctl_vals[5]);
	memory_submit ("cache",    sysctl_vals[6]);
/* #endif HAVE_SYSCTLBYNAME */

#elif KERNEL_LINUX
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
		char errbuf[1024];
		WARNING ("memory: fopen: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
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
	{
		char errbuf[1024];
		WARNING ("memory: fclose: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}

	if (mem_used >= (mem_free + mem_buffered + mem_cached))
	{
		mem_used -= mem_free + mem_buffered + mem_cached;
		memory_submit ("used",     mem_used);
		memory_submit ("buffered", mem_buffered);
		memory_submit ("cached",   mem_cached);
		memory_submit ("free",     mem_free);
	}
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
        /* Most of the additions here were taken as-is from the k9toolkit from
         * Brendan Gregg and are subject to change I guess */
	long long mem_used;
	long long mem_free;
	long long mem_lock;
	long long mem_kern;
	long long mem_unus;

	long long pp_kernel;
	long long physmem;
	long long availrmem;

	if (ksp == NULL)
		return (-1);

	mem_used = get_kstat_value (ksp, "pagestotal");
	mem_free = get_kstat_value (ksp, "pagesfree");
	mem_lock = get_kstat_value (ksp, "pageslocked");
	mem_kern = 0;
	mem_unus = 0;

	pp_kernel = get_kstat_value (ksp, "pp_kernel");
	physmem = get_kstat_value (ksp, "physmem");
	availrmem = get_kstat_value (ksp, "availrmem");

	if ((mem_used < 0LL) || (mem_free < 0LL) || (mem_lock < 0LL))
	{
		WARNING ("memory plugin: one of used, free or locked is negative.");
		return (-1);
	}

	mem_unus = physmem - mem_used;

	if (mem_used < (mem_free + mem_lock))
	{
		/* source: http://wesunsolve.net/bugid/id/4909199
		 * this seems to happen when swap space is small, e.g. 2G on a 32G system
		 * we will make some assumptions here
		 * educated solaris internals help welcome here */
		DEBUG ("memory plugin: pages total is smaller than \"free\" "
				"+ \"locked\". This is probably due to small "
				"swap space");
		mem_free = availrmem;
		mem_used = 0;
	}
	else
	{
		mem_used -= mem_free + mem_lock;
	}

	/* mem_kern is accounted for in mem_lock */
	if ( pp_kernel < mem_lock )
	{
		mem_kern = pp_kernel;
		mem_lock -= pp_kernel;
	}
	else
	{
		mem_kern = mem_lock;
		mem_lock = 0;
	}

	mem_used *= pagesize; /* If this overflows you have some serious */
	mem_free *= pagesize; /* memory.. Why not call me up and give me */
	mem_lock *= pagesize; /* some? ;) */
	mem_kern *= pagesize; /* it's 2011 RAM is cheap */
	mem_unus *= pagesize;

	memory_submit ("used",   mem_used);
	memory_submit ("free",   mem_free);
	memory_submit ("locked", mem_lock);
	memory_submit ("kernel", mem_kern);
	memory_submit ("unusable", mem_unus);
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SYSCTL
	int mib[] = {CTL_VM, VM_METER};
	struct vmtotal vmtotal;
	size_t size;

	memset (&vmtotal, 0, sizeof (vmtotal));
	size = sizeof (vmtotal);

	if (sysctl (mib, 2, &vmtotal, &size, NULL, 0) < 0) {
		char errbuf[1024];
		WARNING ("memory plugin: sysctl failed: %s",
			sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	assert (pagesize > 0);
	memory_submit ("active",   vmtotal.t_arm * pagesize);
	memory_submit ("inactive", (vmtotal.t_rm - vmtotal.t_arm) * pagesize);
	memory_submit ("free",     vmtotal.t_free * pagesize);
/* #endif HAVE_SYSCTL */

#elif HAVE_LIBSTATGRAB
	sg_mem_stats *ios;

	if ((ios = sg_get_mem_stats ()) != NULL)
	{
		memory_submit ("used",   ios->used);
		memory_submit ("cached", ios->cache);
		memory_submit ("free",   ios->free);
	}
/* #endif HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
	if (perfstat_memory_total(NULL, &pmemory, sizeof(perfstat_memory_total_t), 1) < 0)
	{
		char errbuf[1024];
		WARNING ("memory plugin: perfstat_memory_total failed: %s",
			sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}
	memory_submit ("used",   pmemory.real_inuse * pagesize);
	memory_submit ("free",   pmemory.real_free * pagesize);
	memory_submit ("cached", pmemory.numperm * pagesize);
	memory_submit ("system", pmemory.real_system * pagesize);
	memory_submit ("user",   pmemory.real_process * pagesize);
#endif /* HAVE_PERFSTAT */

	return (0);
}

void module_register (void)
{
	plugin_register_init ("memory", memory_init);
	plugin_register_read ("memory", memory_read);
} /* void module_register */
