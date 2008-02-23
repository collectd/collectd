/**
 * collectd - src/swap.c
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
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_SYS_SWAP_H
# include <sys/swap.h>
#endif
#if HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif
#if HAVE_SYS_SYSCTL_H
#  include <sys/sysctl.h>
#endif
#if HAVE_KVM_H
#  include <kvm.h>
#endif

#undef  MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))

#if KERNEL_LINUX
/* No global variables */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
static unsigned long long pagesize;
static kstat_t *ksp;
/* #endif HAVE_LIBKSTAT */

#elif defined(VM_SWAPUSAGE)
/* No global variables */
/* #endif defined(VM_SWAPUSAGE) */

#elif HAVE_LIBKVM
static kvm_t *kvm_obj = NULL;
int kvm_pagesize;
/* #endif HAVE_LIBKVM */

#elif HAVE_LIBSTATGRAB
/* No global variables */
/* #endif HAVE_LIBSTATGRAB */

#else
# error "No applicable input method."
#endif /* HAVE_LIBSTATGRAB */

static int swap_init (void)
{
#if KERNEL_LINUX
	/* No init stuff */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
	/* getpagesize(3C) tells me this does not fail.. */
	pagesize = (unsigned long long) getpagesize ();
	if (get_kstat (&ksp, "unix", 0, "system_pages"))
		ksp = NULL;
/* #endif HAVE_LIBKSTAT */

#elif defined(VM_SWAPUSAGE)
	/* No init stuff */
/* #endif defined(VM_SWAPUSAGE) */

#elif HAVE_LIBKVM
	if (kvm_obj != NULL)
	{
		kvm_close (kvm_obj);
		kvm_obj = NULL;
	}

	kvm_pagesize = getpagesize ();

	if ((kvm_obj = kvm_open (NULL, /* execfile */
					NULL, /* corefile */
					NULL, /* swapfile */
					O_RDONLY, /* flags */
					NULL)) /* errstr */
			== NULL)
	{
		ERROR ("swap plugin: kvm_open failed.");
		return (-1);
	}
/* #endif HAVE_LIBKVM */

#elif HAVE_LIBSTATGRAB
	/* No init stuff */
#endif /* HAVE_LIBSTATGRAB */

	return (0);
}

static void swap_submit (const char *type_instance, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "swap");
	strncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values ("swap", &vl);
} /* void swap_submit */

static int swap_read (void)
{
#if KERNEL_LINUX
	FILE *fh;
	char buffer[1024];
	
	char *fields[8];
	int numfields;

	unsigned long long swap_used   = 0LL;
	unsigned long long swap_cached = 0LL;
	unsigned long long swap_free   = 0LL;
	unsigned long long swap_total  = 0LL;

	if ((fh = fopen ("/proc/meminfo", "r")) == NULL)
	{
		char errbuf[1024];
		WARNING ("memory: fopen: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		unsigned long long *val = NULL;

		if (strncasecmp (buffer, "SwapTotal:", 10) == 0)
			val = &swap_total;
		else if (strncasecmp (buffer, "SwapFree:", 9) == 0)
			val = &swap_free;
		else if (strncasecmp (buffer, "SwapCached:", 11) == 0)
			val = &swap_cached;
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

	if ((swap_total == 0LL) || ((swap_free + swap_cached) > swap_total))
		return (-1);

	swap_used = swap_total - (swap_free + swap_cached);

	swap_submit ("used", swap_used);
	swap_submit ("free", swap_free);
	swap_submit ("cached", swap_cached);
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
	unsigned long long swap_alloc;
	unsigned long long swap_resv;
	unsigned long long swap_avail;

	struct anoninfo ai;

	if (swapctl (SC_AINFO, &ai) == -1)
	{
		char errbuf[1024];
		ERROR ("swap plugin: swapctl failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	/*
	 * Calculations from:
	 * http://cvs.opensolaris.org/source/xref/on/usr/src/cmd/swap/swap.c
	 * Also see:
	 * http://www.itworld.com/Comp/2377/UIR980701perf/ (outdated?)
	 * /usr/include/vm/anon.h
	 *
	 * In short, swap -s shows: allocated + reserved = used, available
	 *
	 * However, Solaris does not allow to allocated/reserved more than the
	 * available swap (physical memory + disk swap), so the pedant may
	 * prefer: allocated + unallocated = reserved, available
	 * 
	 * We map the above to: used + resv = n/a, free
	 *
	 * Does your brain hurt yet?  - Christophe Kalt
	 *
	 * Oh, and in case you wonder,
	 * swap_alloc = pagesize * ( ai.ani_max - ai.ani_free );
	 * can suffer from a 32bit overflow.
	 */
	swap_alloc  = ai.ani_max - ai.ani_free;
	swap_alloc *= pagesize;
	swap_resv   = ai.ani_resv + ai.ani_free - ai.ani_max;
	swap_resv  *= pagesize;
	swap_avail  = ai.ani_max - ai.ani_resv;
	swap_avail *= pagesize;

	swap_submit ("used", swap_alloc);
	swap_submit ("free", swap_avail);
	swap_submit ("reserved", swap_resv);
/* #endif HAVE_LIBKSTAT */

#elif defined(VM_SWAPUSAGE)
	int              mib[3];
	size_t           mib_len;
	struct xsw_usage sw_usage;
	size_t           sw_usage_len;

	mib_len = 2;
	mib[0]  = CTL_VM;
	mib[1]  = VM_SWAPUSAGE;

	sw_usage_len = sizeof (struct xsw_usage);

	if (sysctl (mib, mib_len, &sw_usage, &sw_usage_len, NULL, 0) != 0)
		return (-1);

	/* The returned values are bytes. */
	swap_submit ("used", sw_usage.xsu_used);
	swap_submit ("free", sw_usage.xsu_avail);
/* #endif VM_SWAPUSAGE */

#elif HAVE_LIBKVM
	struct kvm_swap data_s;
	int             status;

	unsigned long long used;
	unsigned long long free;
	unsigned long long total;

	if (kvm_obj == NULL)
		return (-1);

	/* only one structure => only get the grand total, no details */
	status = kvm_getswapinfo (kvm_obj, &data_s, 1, 0);
	if (status == -1)
		return (-1);

	total = (unsigned long long) data_s.ksw_total;
	used  = (unsigned long long) data_s.ksw_used;

	total *= (unsigned long long) kvm_pagesize;
	used  *= (unsigned long long) kvm_pagesize;

	free = total - used;

	swap_submit ("used", used);
	swap_submit ("free", free);
/* #endif HAVE_LIBKVM */

#elif HAVE_LIBSTATGRAB
	sg_swap_stats *swap;

	swap = sg_get_swap_stats ();

	if (swap == NULL)
		return (-1);

	swap_submit ("used", swap->used);
	swap_submit ("free", swap->free);
#endif /* HAVE_LIBSTATGRAB */

	return (0);
} /* int swap_read */

void module_register (void)
{
	plugin_register_init ("swap", swap_init);
	plugin_register_read ("swap", swap_read);
} /* void module_register */
