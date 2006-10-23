/**
 * collectd - src/swap.c
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

#define MODULE_NAME "swap"

#if KERNEL_LINUX || HAVE_LIBKSTAT || defined(VM_SWAPUSAGE) || HAVE_LIBKVM || HAVE_LIBSTATGRAB
# define SWAP_HAVE_READ 1
#else
# define SWAP_HAVE_READ 0
#endif

#undef  MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))

static char *swap_file = "swap.rrd";

/* 1099511627776 == 1TB ought to be enough for anyone ;) */
static char *ds_def[] =
{
	"DS:used:GAUGE:"COLLECTD_HEARTBEAT":0:1099511627776",
	"DS:free:GAUGE:"COLLECTD_HEARTBEAT":0:1099511627776",
	"DS:cached:GAUGE:"COLLECTD_HEARTBEAT":0:1099511627776",
	"DS:resv:GAUGE:"COLLECTD_HEARTBEAT":0:1099511627776",
	NULL
};
static int ds_num = 4;

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
#endif /* HAVE_LIBSTATGRAB */

static void swap_init (void)
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
		syslog (LOG_ERR, "swap plugin: kvm_open failed.");
		return;
	}
/* #endif HAVE_LIBKVM */

#elif HAVE_LIBSTATGRAB
	/* No init stuff */
#endif /* HAVE_LIBSTATGRAB */

	return;
}

static void swap_write (char *host, char *inst, char *val)
{
	rrd_update_file (host, swap_file, val, ds_def, ds_num);
}

#if SWAP_HAVE_READ
static void swap_submit (unsigned long long swap_used,
		unsigned long long swap_free,
		unsigned long long swap_cached,
		unsigned long long swap_resv)
{
	char buffer[512];

	if (snprintf (buffer, 512, "%u:%llu:%llu:%llu:%llu", (unsigned int) curtime,
				swap_used, swap_free, swap_cached, swap_resv) >= 512)
		return;

	plugin_submit (MODULE_NAME, "-", buffer);
}

static void swap_read (void)
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
		syslog (LOG_WARNING, "memory: fopen: %s", strerror (errno));
		return;
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
		syslog (LOG_WARNING, "memory: fclose: %s", strerror (errno));

	if ((swap_total == 0LL) || ((swap_free + swap_cached) > swap_total))
		return;

	swap_used = swap_total - (swap_free + swap_cached);

	swap_submit (swap_used, swap_free, swap_cached, -1LL);
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
	unsigned long long swap_alloc;
	unsigned long long swap_resv;
	unsigned long long swap_avail;

	struct anoninfo ai;

	if (swapctl (SC_AINFO, &ai) == -1)
	{
		syslog (LOG_ERR, "swap plugin: swapctl failed: %s",
				strerror (errno));
		return;
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

	swap_submit (swap_alloc, swap_avail, -1LL, swap_resv - swap_alloc);
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
		return;

	/* The returned values are bytes. */
	swap_submit (sw_usage.xsu_used, sw_usage.xsu_avail, -1LL, -1LL);
/* #endif VM_SWAPUSAGE */

#elif HAVE_LIBKVM
	struct kvm_swap data_s;
	int             status;

	unsigned long long used;
	unsigned long long free;
	unsigned long long total;

	if (kvm_obj == NULL)
		return;

	/* only one structure => only get the grand total, no details */
	status = kvm_getswapinfo (kvm_obj, &data_s, 1, 0);
	if (status == -1)
		return;

	total = (unsigned long long) data_s.ksw_total;
	used  = (unsigned long long) data_s.ksw_used;

	total *= (unsigned long long) kvm_pagesize;
	used  *= (unsigned long long) kvm_pagesize;

	free = total - used;

	swap_submit (used, free, -1LL, -1LL);
/* #endif HAVE_LIBKVM */

#elif HAVE_LIBSTATGRAB
	sg_swap_stats *swap;

	if ((swap = sg_get_swap_stats ()) != NULL)
		swap_submit (swap->used, swap->free, -1LL, -1LL);
#endif /* HAVE_LIBSTATGRAB */
}
#else
# define swap_read NULL
#endif /* SWAP_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, swap_init, swap_read, swap_write);
}

#undef MODULE_NAME
