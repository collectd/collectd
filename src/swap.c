/**
 * collectd - src/swap.c
 * Copyright (C) 2005-2009  Florian octo Forster
 * Copyright (C) 2009       Stefan Völkel
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
 *   Manuel Sanmartin
 **/

#if HAVE_CONFIG_H
# include "config.h"
# undef HAVE_CONFIG_H
#endif
/* avoid swap.h error "Cannot use swapctl in the large files compilation environment" */
#if HAVE_SYS_SWAP_H && !defined(_LP64) && _FILE_OFFSET_BITS == 64
#  undef _FILE_OFFSET_BITS
#  undef _LARGEFILE64_SOURCE
#endif

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if HAVE_SYS_SWAP_H
# include <sys/swap.h>
#endif
#if HAVE_VM_ANON_H
# include <vm/anon.h>
#endif
#if HAVE_SYS_PARAM_H
#  include <sys/param.h>
#endif
#if HAVE_SYS_SYSCTL_H
#  include <sys/sysctl.h>
#endif
#if HAVE_SYS_DKSTAT_H
#  include <sys/dkstat.h>
#endif
#if HAVE_KVM_H
#  include <kvm.h>
#endif

#if HAVE_STATGRAB_H
# include <statgrab.h>
#endif

#if HAVE_PERFSTAT
# include <sys/protosw.h>
# include <libperfstat.h>
#endif

#undef  MAX
#define MAX(x,y) ((x) > (y) ? (x) : (y))

#if KERNEL_LINUX
/* No global variables */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
static derive_t pagesize;
static kstat_t *ksp;
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS
static derive_t pagesize;
/* #endif HAVE_SWAPCTL */

#elif defined(VM_SWAPUSAGE)
/* No global variables */
/* #endif defined(VM_SWAPUSAGE) */

#elif HAVE_LIBKVM_GETSWAPINFO
static kvm_t *kvm_obj = NULL;
int kvm_pagesize;
/* #endif HAVE_LIBKVM_GETSWAPINFO */

#elif HAVE_LIBSTATGRAB
/* No global variables */
/* #endif HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
static int pagesize;
static perfstat_memory_total_t pmemory;
/*# endif HAVE_PERFSTAT */

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
	pagesize = (derive_t) getpagesize ();
	if (get_kstat (&ksp, "unix", 0, "system_pages"))
		ksp = NULL;
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS
	/* getpagesize(3C) tells me this does not fail.. */
	pagesize = (derive_t) getpagesize ();
/* #endif HAVE_SWAPCTL */

#elif defined(VM_SWAPUSAGE)
	/* No init stuff */
/* #endif defined(VM_SWAPUSAGE) */

#elif HAVE_LIBKVM_GETSWAPINFO
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
/* #endif HAVE_LIBKVM_GETSWAPINFO */

#elif HAVE_LIBSTATGRAB
	/* No init stuff */
/* #endif HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
	pagesize = getpagesize();
#endif /* HAVE_PERFSTAT */

	return (0);
}

static void swap_submit (const char *type_instance, derive_t value, unsigned type)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	switch (type)
	{
		case DS_TYPE_GAUGE:
			values[0].gauge = (gauge_t) value;
			sstrncpy (vl.type, "swap", sizeof (vl.type));
			break;
		case DS_TYPE_DERIVE:
			values[0].derive = value;
			sstrncpy (vl.type, "swap_io", sizeof (vl.type));
			break;
		default:
			ERROR ("swap plugin: swap_submit called with wrong"
				" type");
	}

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "swap", sizeof (vl.plugin));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void swap_submit */

static int swap_read (void)
{
#if KERNEL_LINUX
	FILE *fh;
	char buffer[1024];

	char *fields[8];
	int numfields;

	_Bool old_kernel=0;

	derive_t swap_used   = 0;
	derive_t swap_cached = 0;
	derive_t swap_free   = 0;
	derive_t swap_total  = 0;
	derive_t swap_in     = 0;
	derive_t swap_out    = 0;

	if ((fh = fopen ("/proc/meminfo", "r")) == NULL)
	{
		char errbuf[1024];
		WARNING ("memory: fopen: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		derive_t *val = NULL;

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

		*val = (derive_t) atoll (fields[1]) * 1024LL;
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

	if ((fh = fopen ("/proc/vmstat", "r")) == NULL)
	{
		// /proc/vmstat does not exist in kernels <2.6
		if ((fh = fopen ("/proc/stat", "r")) == NULL )
		{
			char errbuf[1024];
			WARNING ("swap: fopen: %s",
					sstrerror (errno, errbuf, sizeof (errbuf)));
			return (-1);
		}
		else
			old_kernel = 1;
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));

		if (!old_kernel)
		{
			if (numfields != 2)
				continue;

			if (strcasecmp ("pswpin", fields[0]) != 0)
				strtoderive (fields[1], &swap_in);
			else if (strcasecmp ("pswpout", fields[0]) == 0)
				strtoderive (fields[1], &swap_out);
		}
		else /* if (old_kernel) */
		{
			if (numfields != 3)
				continue;

			if (strcasecmp ("page", fields[0]) == 0)
			{
				strtoderive (fields[1], &swap_in);
				strtoderive (fields[2], &swap_out);
			}
		}
	} /* while (fgets) */

	if (fclose (fh))
	{
		char errbuf[1024];
		WARNING ("swap: fclose: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
	}

	swap_submit ("used", swap_used, DS_TYPE_GAUGE);
	swap_submit ("free", swap_free, DS_TYPE_GAUGE);
	swap_submit ("cached", swap_cached, DS_TYPE_GAUGE);
	swap_submit ("in", swap_in, DS_TYPE_DERIVE);
	swap_submit ("out", swap_out, DS_TYPE_DERIVE);
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
	derive_t swap_alloc;
	derive_t swap_resv;
	derive_t swap_avail;

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
	swap_alloc  = (derive_t) ((ai.ani_max - ai.ani_free) * pagesize);
	swap_resv   = (derive_t) ((ai.ani_resv + ai.ani_free - ai.ani_max)
			* pagesize);
	swap_avail  = (derive_t) ((ai.ani_max - ai.ani_resv) * pagesize);

	swap_submit ("used", swap_alloc, DS_TYPE_GAUGE);
	swap_submit ("free", swap_avail, DS_TYPE_GAUGE);
	swap_submit ("reserved", swap_resv, DS_TYPE_GAUGE);
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SWAPCTL
 #if HAVE_SWAPCTL_TWO_ARGS
        swaptbl_t *s;
        char strtab[255];
        int swap_num;
        int status;
        int i;

        derive_t avail = 0;
        derive_t total = 0;

        swap_num = swapctl (SC_GETNSWP, NULL);
        if (swap_num < 0)
        {
                ERROR ("swap plugin: swapctl (SC_GETNSWP) failed with status %i.",
                                swap_num);
                return (-1);
        }
        else if (swap_num == 0)
                return (0);

        s = (swaptbl_t *) smalloc (swap_num * sizeof (swapent_t) + sizeof (struct swaptable));
        if (s == NULL)
        {
                ERROR ("swap plugin: smalloc failed.");
                return (-1);
        }
        /* Initialize string pointers. We have them share the same buffer as we don't care
	 * about the device's name, only its size. This saves memory and alloc/free ops */
        for (i = 0; i < (swap_num + 1); i++) {
                s->swt_ent[i].ste_path = strtab;
        }
        s->swt_n = swap_num + 1;
        status = swapctl (SC_LIST, s);
        if (status != swap_num)
        {
                ERROR ("swap plugin: swapctl (SC_LIST) failed with status %i.",
                                status);
                sfree (s);
                return (-1);
        }

        for (i = 0; i < swap_num; i++)
        {
                if ((s->swt_ent[i].ste_flags & ST_INDEL) != 0)
                        continue;

                avail += ((derive_t) s->swt_ent[i].ste_free)
                         * pagesize;
                total += ((derive_t) s->swt_ent[i].ste_pages)
                         * pagesize;
        }

        if (total < avail)
        {
                ERROR ("swap plugin: Total swap space (%"PRIi64") "
                                "is less than free swap space (%"PRIi64").",
                                total, avail);
                sfree (s);
                return (-1);
        }

        swap_submit ("used", total - avail, DS_TYPE_GAUGE);
        swap_submit ("free", avail, DS_TYPE_GAUGE);

        sfree (s);
 /* #endif HAVE_SWAPCTL_TWO_ARGS */
 #elif HAVE_SWAPCTL_THREE_ARGS

	struct swapent *swap_entries;
	int swap_num;
	int status;
	int i;

	derive_t used  = 0;
	derive_t total = 0;

	swap_num = swapctl (SWAP_NSWAP, NULL, 0);
	if (swap_num < 0)
	{
		ERROR ("swap plugin: swapctl (SWAP_NSWAP) failed with status %i.",
				swap_num);
		return (-1);
	}
	else if (swap_num == 0)
		return (0);

	swap_entries = calloc (swap_num, sizeof (*swap_entries));
	if (swap_entries == NULL)
	{
		ERROR ("swap plugin: calloc failed.");
		return (-1);
	}

	status = swapctl (SWAP_STATS, swap_entries, swap_num);
	if (status != swap_num)
	{
		ERROR ("swap plugin: swapctl (SWAP_STATS) failed with status %i.",
				status);
		sfree (swap_entries);
		return (-1);
	}

#if defined(DEV_BSIZE) && (DEV_BSIZE > 0)
# define C_SWAP_BLOCK_SIZE ((derive_t) DEV_BSIZE)
#else
# define C_SWAP_BLOCK_SIZE ((derive_t) 512)
#endif

	for (i = 0; i < swap_num; i++)
	{
		if ((swap_entries[i].se_flags & SWF_ENABLE) == 0)
			continue;

		used  += ((derive_t) swap_entries[i].se_inuse)
			* C_SWAP_BLOCK_SIZE;
		total += ((derive_t) swap_entries[i].se_nblks)
			* C_SWAP_BLOCK_SIZE;
	}

	if (total < used)
	{
		ERROR ("swap plugin: Total swap space (%"PRIu64") "
				"is less than used swap space (%"PRIu64").",
				total, used);
		return (-1);
	}

	swap_submit ("used", used, DS_TYPE_GAUGE);
	swap_submit ("free", total - used, DS_TYPE_GAUGE);

	sfree (swap_entries);
 #endif /* HAVE_SWAPCTL_THREE_ARGS */
/* #endif HAVE_SWAPCTL */

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
	swap_submit ("used", (derive_t) sw_usage.xsu_used, DS_TYPE_GAUGE);
	swap_submit ("free", (derive_t) sw_usage.xsu_avail, DS_TYPE_GAUGE);
/* #endif VM_SWAPUSAGE */

#elif HAVE_LIBKVM_GETSWAPINFO
	struct kvm_swap data_s;
	int             status;

	derive_t used;
	derive_t free;
	derive_t total;

	if (kvm_obj == NULL)
		return (-1);

	/* only one structure => only get the grand total, no details */
	status = kvm_getswapinfo (kvm_obj, &data_s, 1, 0);
	if (status == -1)
		return (-1);

	total = (derive_t) data_s.ksw_total;
	used  = (derive_t) data_s.ksw_used;

	total *= (derive_t) kvm_pagesize;
	used  *= (derive_t) kvm_pagesize;

	free = total - used;

	swap_submit ("used", used, DS_TYPE_GAUGE);
	swap_submit ("free", free, DS_TYPE_GAUGE);
/* #endif HAVE_LIBKVM_GETSWAPINFO */

#elif HAVE_LIBSTATGRAB
	sg_swap_stats *swap;

	swap = sg_get_swap_stats ();

	if (swap == NULL)
		return (-1);

	swap_submit ("used", (derive_t) swap->used, DS_TYPE_GAUGE);
	swap_submit ("free", (derive_t) swap->free, DS_TYPE_GAUGE);
/* #endif  HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
        if(perfstat_memory_total(NULL, &pmemory, sizeof(perfstat_memory_total_t), 1) < 0)
	{
                char errbuf[1024];
                WARNING ("memory plugin: perfstat_memory_total failed: %s",
                        sstrerror (errno, errbuf, sizeof (errbuf)));
                return (-1);
        }
	swap_submit ("used", (derive_t) (pmemory.pgsp_total - pmemory.pgsp_free) * pagesize, DS_TYPE_GAUGE);
	swap_submit ("free", (derive_t) pmemory.pgsp_free * pagesize , DS_TYPE_GAUGE);
#endif /* HAVE_PERFSTAT */

	return (0);
} /* int swap_read */

void module_register (void)
{
	plugin_register_init ("swap", swap_init);
	plugin_register_read ("swap", swap_read);
} /* void module_register */
