/**
 * collectd - src/swap.c
 * Copyright (C) 2005-2010  Florian octo Forster
 * Copyright (C) 2009       Stefan Völkel
 * Copyright (C) 2009       Manuel Sanmartin
 * Copyright (C) 2010       Aurélien Reynaud
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
 *   Florian octo Forster <octo at collectd.org>
 *   Manuel Sanmartin
 *   Aurélien Reynaud <collectd at wattapower.net>
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

#elif HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS
static derive_t pagesize;

static const char *config_keys[] =
{
	"ReportByDevice"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static _Bool report_by_device = 0;
/* #endif HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS */

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

static void swap_submit_inst (const char *plugin_instance, /* {{{ */
		const char *type_instance, derive_t value, unsigned type)
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
	if (plugin_instance != NULL)
		sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* }}} void swap_submit_inst */

static void swap_submit (const char *type_instance, derive_t value, unsigned type)
{
	swap_submit_inst (/* plugin instance = */ NULL,
			type_instance, value, type);
}

#if KERNEL_LINUX
static int swap_read (void) /* {{{ */
{
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

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
		if (numfields < 2)
			continue;

		if (strcasecmp (fields[0], "SwapTotal:") == 0)
			strtoderive (fields[1], &swap_total);
		else if (strcasecmp (fields[0], "SwapFree:") == 0)
			strtoderive (fields[1], &swap_free);
		else if (strcasecmp (fields[0], "SwapCached:") == 0)
			strtoderive (fields[1], &swap_cached);
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

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));

		if (!old_kernel)
		{
			if (numfields != 2)
				continue;

			if (strcasecmp ("pswpin", fields[0]) == 0)
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

	swap_submit ("used",   1024 * swap_used,   DS_TYPE_GAUGE);
	swap_submit ("free",   1024 * swap_free,   DS_TYPE_GAUGE);
	swap_submit ("cached", 1024 * swap_cached, DS_TYPE_GAUGE);
	swap_submit ("in",  swap_in,  DS_TYPE_DERIVE);
	swap_submit ("out", swap_out, DS_TYPE_DERIVE);

	return (0);
} /* }}} int swap_read */
/* #endif KERNEL_LINUX */

/*
 * Under Solaris, two mechanisms can be used to read swap statistics, swapctl
 * and kstat. The former reads physical space used on a device, the latter
 * reports the view from the virtual memory system. It was decided that the
 * kstat-based information should be moved to the "vmem" plugin, but nobody
 * with enough Solaris experience was available at that time to do this. The
 * code below is still there for your reference but it won't be activated in
 * *this* plugin again. --octo
 */
#elif 0 && HAVE_LIBKSTAT
/* kstat-based read function */
static int swap_read_kstat (void) /* {{{ */
{
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

	return (0);
} /* }}} int swap_read_kstat */
/* #endif 0 && HAVE_LIBKSTAT */

#elif HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS
/* swapctl-based read function */
static int swap_read (void) /* {{{ */
{
        swaptbl_t *s;
	char *s_paths;
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

	/* Allocate and initialize the swaptbl_t structure */
        s = (swaptbl_t *) smalloc (swap_num * sizeof (swapent_t) + sizeof (struct swaptable));
        if (s == NULL)
        {
                ERROR ("swap plugin: smalloc failed.");
                return (-1);
        }

	/* Memory to store the path names. We only use these paths when the
	 * separate option has been configured, but it's easier to just
	 * allocate enough memory in any case. */
	s_paths = calloc (swap_num, PATH_MAX);
	if (s_paths == NULL)
	{
		ERROR ("swap plugin: malloc failed.");
		sfree (s);
		return (-1);
	}
        for (i = 0; i < swap_num; i++)
		s->swt_ent[i].ste_path = s_paths + (i * PATH_MAX);
        s->swt_n = swap_num;

        status = swapctl (SC_LIST, s);
        if (status < 0)
        {
		char errbuf[1024];
                ERROR ("swap plugin: swapctl (SC_LIST) failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		sfree (s_paths);
                sfree (s);
                return (-1);
        }
	else if (swap_num < status)
	{
		/* more elements returned than requested */
		ERROR ("swap plugin: I allocated memory for %i structure%s, "
				"but swapctl(2) claims to have returned %i. "
				"I'm confused and will give up.",
				swap_num, (swap_num == 1) ? "" : "s",
				status);
		sfree (s_paths);
                sfree (s);
                return (-1);
	}
	else if (swap_num > status)
		/* less elements returned than requested */
		swap_num = status;

        for (i = 0; i < swap_num; i++)
        {
		char path[PATH_MAX];
		derive_t this_total;
		derive_t this_avail;

                if ((s->swt_ent[i].ste_flags & ST_INDEL) != 0)
                        continue;

		this_total = ((derive_t) s->swt_ent[i].ste_pages) * pagesize;
		this_avail = ((derive_t) s->swt_ent[i].ste_free)  * pagesize;

		/* Shortcut for the "combined" setting (default) */
		if (!report_by_device)
		{
			avail += this_avail;
			total += this_total;
			continue;
		}

		/* Okay, using "/" as swap device would be super-weird, but
		 * we'll handle it anyway to cover all cases. */
		if (strcmp ("/", s->swt_ent[i].ste_path) == 0)
			sstrncpy (path, "root", sizeof (path));
		else
		{
			int j;

			s->swt_ent[i].ste_path[PATH_MAX - 1] = 0;
			/* Don't copy the leading slash */
			sstrncpy (path, &s->swt_ent[i].ste_path[1], sizeof (path));
			/* Convert slashes to dashes, just like the "df" plugin. */
			for (j = 0; path[j] != 0; j++)
				if (path[j] == '/')
					path[j] = '-';
		}

		swap_submit_inst (path, "used", this_total - this_avail, DS_TYPE_GAUGE);
		swap_submit_inst (path, "free", this_avail, DS_TYPE_GAUGE);
        } /* for (swap_num) */

        if (total < avail)
        {
                ERROR ("swap plugin: Total swap space (%"PRIi64") "
                                "is less than free swap space (%"PRIi64").",
                                total, avail);
		sfree (s_paths);
                sfree (s);
                return (-1);
        }

	/* If the "separate" option was specified (report_by_device == 2), all
	 * values have already been dispatched from within the loop. */
	if (!report_by_device)
	{
		swap_submit ("used", total - avail, DS_TYPE_GAUGE);
		swap_submit ("free", avail, DS_TYPE_GAUGE);
	}

	sfree (s_paths);
        sfree (s);
	return (0);
} /* }}} int swap_read */

/* Configuration: Present when swapctl or both methods are available. */
static int swap_config (const char *key, const char *value) /* {{{ */
{
	if (strcasecmp ("ReportByDevice", key) == 0)
	{
		if (IS_TRUE (value))
			report_by_device = 1;
		else
			report_by_device = 0;
	}
	else
	{
		return (-1);
	}

	return (0);
} /* }}} int swap_config */
/* #endif HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS */

#elif HAVE_SWAPCTL && HAVE_SWAPCTL_THREE_ARGS
static int swap_read (void) /* {{{ */
{
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

	return (0);
} /* }}} int swap_read */
/* #endif HAVE_SWAPCTL && HAVE_SWAPCTL_THREE_ARGS */

#elif defined(VM_SWAPUSAGE)
static int swap_read (void) /* {{{ */
{
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

	return (0);
} /* }}} int swap_read */
/* #endif VM_SWAPUSAGE */

#elif HAVE_LIBKVM_GETSWAPINFO
static int swap_read (void) /* {{{ */
{
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

	return (0);
} /* }}} int swap_read */
/* #endif HAVE_LIBKVM_GETSWAPINFO */

#elif HAVE_LIBSTATGRAB
static int swap_read (void) /* {{{ */
{
	sg_swap_stats *swap;

	swap = sg_get_swap_stats ();

	if (swap == NULL)
		return (-1);

	swap_submit ("used", (derive_t) swap->used, DS_TYPE_GAUGE);
	swap_submit ("free", (derive_t) swap->free, DS_TYPE_GAUGE);

	return (0);
} /* }}} int swap_read */
/* #endif  HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
static int swap_read (void) /* {{{ */
{
        if(perfstat_memory_total(NULL, &pmemory, sizeof(perfstat_memory_total_t), 1) < 0)
	{
                char errbuf[1024];
                WARNING ("memory plugin: perfstat_memory_total failed: %s",
                        sstrerror (errno, errbuf, sizeof (errbuf)));
                return (-1);
        }
	swap_submit ("used", (derive_t) (pmemory.pgsp_total - pmemory.pgsp_free) * pagesize, DS_TYPE_GAUGE);
	swap_submit ("free", (derive_t) pmemory.pgsp_free * pagesize , DS_TYPE_GAUGE);

	return (0);
} /* }}} int swap_read */
#endif /* HAVE_PERFSTAT */

void module_register (void)
{
#if HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS
	plugin_register_config ("swap", swap_config, config_keys, config_keys_num);
#endif
	plugin_register_init ("swap", swap_init);
	plugin_register_read ("swap", swap_read);
} /* void module_register */

/* vim: set fdm=marker : */
