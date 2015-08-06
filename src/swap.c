/**
 * collectd - src/swap.c
 * Copyright (C) 2005-2014  Florian octo Forster
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
# define SWAP_HAVE_REPORT_BY_DEVICE 1
static derive_t pagesize;
static _Bool report_bytes = 0;
static _Bool report_by_device = 0;
/* #endif KERNEL_LINUX */

#elif HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS
# define SWAP_HAVE_REPORT_BY_DEVICE 1
static derive_t pagesize;
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
/*# endif HAVE_PERFSTAT */

#else
# error "No applicable input method."
#endif /* HAVE_LIBSTATGRAB */

static _Bool values_absolute = 1;
static _Bool values_percentage = 0;

static int swap_config (oconfig_item_t *ci) /* {{{ */
{
	int i;

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;
		if (strcasecmp ("ReportBytes", child->key) == 0)
#if KERNEL_LINUX
			cf_util_get_boolean (child, &report_bytes);
#else
			WARNING ("swap plugin: The \"ReportBytes\" option "
					"is only valid under Linux. "
					"The option is going to be ignored.");
#endif
		else if (strcasecmp ("ReportByDevice", child->key) == 0)
#if SWAP_HAVE_REPORT_BY_DEVICE
			cf_util_get_boolean (child, &report_by_device);
#else
			WARNING ("swap plugin: The \"ReportByDevice\" option "
					"is not supported on this platform. "
					"The option is going to be ignored.");
#endif /* SWAP_HAVE_REPORT_BY_DEVICE */
		else if (strcasecmp ("ValuesAbsolute", child->key) == 0)
			cf_util_get_boolean (child, &values_absolute);
		else if (strcasecmp ("ValuesPercentage", child->key) == 0)
			cf_util_get_boolean (child, &values_percentage);
		else
			WARNING ("swap plugin: Unknown config option: \"%s\"",
					child->key);
	}

	return (0);
} /* }}} int swap_config */

static int swap_init (void) /* {{{ */
{
#if KERNEL_LINUX
	pagesize = (derive_t) sysconf (_SC_PAGESIZE);
/* #endif KERNEL_LINUX */

#elif HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS
	/* getpagesize(3C) tells me this does not fail.. */
	pagesize = (derive_t) getpagesize ();
/* #endif HAVE_SWAPCTL */

#elif defined(VM_SWAPUSAGE)
	/* No init stuff */
/* #endif defined(VM_SWAPUSAGE) */

#elif HAVE_LIBKVM_GETSWAPINFO
	char errbuf[_POSIX2_LINE_MAX];

	if (kvm_obj != NULL)
	{
		kvm_close (kvm_obj);
		kvm_obj = NULL;
	}

	kvm_pagesize = getpagesize ();

	kvm_obj = kvm_openfiles (NULL, "/dev/null", NULL, O_RDONLY, errbuf);

	if (kvm_obj == NULL)
	{
		ERROR ("swap plugin: kvm_openfiles failed, %s", errbuf);
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
} /* }}} int swap_init */

static void swap_submit_usage (char const *plugin_instance, /* {{{ */
		gauge_t used, gauge_t free,
		char const *other_name, gauge_t other_value)
{
	value_t v[1];
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = v;
	vl.values_len = STATIC_ARRAY_SIZE (v);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "swap", sizeof (vl.plugin));
	if (plugin_instance != NULL)
		sstrncpy (vl.plugin_instance, plugin_instance,
				sizeof (vl.plugin_instance));
	sstrncpy (vl.type, "swap", sizeof (vl.type));

	if (values_absolute)
		plugin_dispatch_multivalue (&vl, 0, DS_TYPE_GAUGE,
				"used", used, "free", free,
				other_name, other_value, NULL);
	if (values_percentage)
		plugin_dispatch_multivalue (&vl, 1, DS_TYPE_GAUGE,
				"used", used, "free", free,
				other_name, other_value, NULL);
} /* }}} void swap_submit_usage */

#if KERNEL_LINUX || HAVE_PERFSTAT
__attribute__((nonnull(1)))
static void swap_submit_derive (char const *type_instance, /* {{{ */
		derive_t value)
{
	value_list_t vl = VALUE_LIST_INIT;
	value_t v[1];

	v[0].derive = value;

	vl.values = v;
	vl.values_len = STATIC_ARRAY_SIZE (v);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "swap", sizeof (vl.plugin));
	sstrncpy (vl.type, "swap_io", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* }}} void swap_submit_derive */
#endif

#if KERNEL_LINUX
static int swap_read_separate (void) /* {{{ */
{
	FILE *fh;
	char buffer[1024];

	fh = fopen ("/proc/swaps", "r");
	if (fh == NULL)
	{
		char errbuf[1024];
		WARNING ("swap plugin: fopen (/proc/swaps) failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		char *fields[8];
		int numfields;
		char *endptr;

		char path[PATH_MAX];
		gauge_t total;
		gauge_t used;

		numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
		if (numfields != 5)
			continue;

		sstrncpy (path, fields[0], sizeof (path));
		escape_slashes (path, sizeof (path));

		errno = 0;
		endptr = NULL;
		total = strtod (fields[2], &endptr);
		if ((endptr == fields[2]) || (errno != 0))
			continue;

		errno = 0;
		endptr = NULL;
		used = strtod (fields[3], &endptr);
		if ((endptr == fields[3]) || (errno != 0))
			continue;

		if (total < used)
			continue;

		swap_submit_usage (path, used, total - used, NULL, NAN);
	}

	fclose (fh);

	return (0);
} /* }}} int swap_read_separate */

static int swap_read_combined (void) /* {{{ */
{
	FILE *fh;
	char buffer[1024];

	gauge_t swap_used   = NAN;
	gauge_t swap_cached = NAN;
	gauge_t swap_free   = NAN;
	gauge_t swap_total  = NAN;

	fh = fopen ("/proc/meminfo", "r");
	if (fh == NULL)
	{
		char errbuf[1024];
		WARNING ("swap plugin: fopen (/proc/meminfo) failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buffer, sizeof (buffer), fh) != NULL)
	{
		char *fields[8];
		int numfields;

		numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));
		if (numfields < 2)
			continue;

		if (strcasecmp (fields[0], "SwapTotal:") == 0)
			strtogauge (fields[1], &swap_total);
		else if (strcasecmp (fields[0], "SwapFree:") == 0)
			strtogauge (fields[1], &swap_free);
		else if (strcasecmp (fields[0], "SwapCached:") == 0)
			strtogauge (fields[1], &swap_cached);
	}

	fclose (fh);

	if (isnan (swap_total) || isnan (swap_free))
		return (ENOENT);

	/* Some systems, OpenVZ for example, don't provide SwapCached. */
	if (isnan (swap_cached))
		swap_used = swap_total - swap_free;
	else
		swap_used = swap_total - (swap_free + swap_cached);
	assert (!isnan (swap_used));

	if (swap_used < 0.0)
		return (EINVAL);

	swap_submit_usage (NULL, swap_used, swap_free,
			isnan (swap_cached) ? NULL : "cached", swap_cached);
	return (0);
} /* }}} int swap_read_combined */

static int swap_read_io (void) /* {{{ */
{
	FILE *fh;
	char buffer[1024];

	_Bool old_kernel = 0;

	uint8_t have_data = 0;
	derive_t swap_in  = 0;
	derive_t swap_out = 0;

	fh = fopen ("/proc/vmstat", "r");
	if (fh == NULL)
	{
		/* /proc/vmstat does not exist in kernels <2.6 */
		fh = fopen ("/proc/stat", "r");
		if (fh == NULL)
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
		char *fields[8];
		int numfields;

		numfields = strsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));

		if (!old_kernel)
		{
			if (numfields != 2)
				continue;

			if (strcasecmp ("pswpin", fields[0]) == 0)
			{
				strtoderive (fields[1], &swap_in);
				have_data |= 0x01;
			}
			else if (strcasecmp ("pswpout", fields[0]) == 0)
			{
				strtoderive (fields[1], &swap_out);
				have_data |= 0x02;
			}
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

	fclose (fh);

	if (have_data != 0x03)
		return (ENOENT);

	if (report_bytes)
	{
		swap_in = swap_in * pagesize;
		swap_out = swap_out * pagesize;
	}

	swap_submit_derive ("in",  swap_in);
	swap_submit_derive ("out", swap_out);

	return (0);
} /* }}} int swap_read_io */

static int swap_read (void) /* {{{ */
{
	if (report_by_device)
		swap_read_separate ();
	else
		swap_read_combined ();

	swap_read_io ();

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
	gauge_t swap_alloc;
	gauge_t swap_resv;
	gauge_t swap_avail;

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
	swap_alloc = (gauge_t) ((ai.ani_max - ai.ani_free) * pagesize);
	swap_resv  = (gauge_t) ((ai.ani_resv + ai.ani_free - ai.ani_max) * pagesize);
	swap_avail = (gauge_t) ((ai.ani_max - ai.ani_resv) * pagesize);

	swap_submit_usage (NULL, swap_alloc, swap_avail, "reserved", swap_resv);
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

        gauge_t avail = 0;
        gauge_t total = 0;

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
		gauge_t this_total;
		gauge_t this_avail;

                if ((s->swt_ent[i].ste_flags & ST_INDEL) != 0)
                        continue;

		this_total = (gauge_t) (s->swt_ent[i].ste_pages * pagesize);
		this_avail = (gauge_t) (s->swt_ent[i].ste_free  * pagesize);

		/* Shortcut for the "combined" setting (default) */
		if (!report_by_device)
		{
			avail += this_avail;
			total += this_total;
			continue;
		}

		sstrncpy (path, s->swt_ent[i].ste_path, sizeof (path));
		escape_slashes (path, sizeof (path));

		swap_submit_usage (path, this_total - this_avail, this_avail,
				NULL, NAN);
        } /* for (swap_num) */

        if (total < avail)
        {
                ERROR ("swap plugin: Total swap space (%g) is less than free swap space (%g).",
                                total, avail);
		sfree (s_paths);
                sfree (s);
                return (-1);
        }

	/* If the "separate" option was specified (report_by_device == 1), all
	 * values have already been dispatched from within the loop. */
	if (!report_by_device)
		swap_submit_usage (NULL, total - avail, avail, NULL, NAN);

	sfree (s_paths);
        sfree (s);
	return (0);
} /* }}} int swap_read */
/* #endif HAVE_SWAPCTL && HAVE_SWAPCTL_TWO_ARGS */

#elif HAVE_SWAPCTL && HAVE_SWAPCTL_THREE_ARGS
static int swap_read (void) /* {{{ */
{
	struct swapent *swap_entries;
	int swap_num;
	int status;
	int i;

	gauge_t used  = 0;
	gauge_t total = 0;

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
# define C_SWAP_BLOCK_SIZE ((gauge_t) DEV_BSIZE)
#else
# define C_SWAP_BLOCK_SIZE 512.0
#endif

	/* TODO: Report per-device stats. The path name is available from
	 * swap_entries[i].se_path */
	for (i = 0; i < swap_num; i++)
	{
		if ((swap_entries[i].se_flags & SWF_ENABLE) == 0)
			continue;

		used  += ((gauge_t) swap_entries[i].se_inuse) * C_SWAP_BLOCK_SIZE;
		total += ((gauge_t) swap_entries[i].se_nblks) * C_SWAP_BLOCK_SIZE;
	}

	if (total < used)
	{
		ERROR ("swap plugin: Total swap space (%g) is less than used swap space (%g).",
				total, used);
		return (-1);
	}

	swap_submit_usage (NULL, used, total - used, NULL, NAN);

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
	swap_submit_usage (NULL,
			(gauge_t) sw_usage.xsu_used, (gauge_t) sw_usage.xsu_avail,
			NULL, NAN);

	return (0);
} /* }}} int swap_read */
/* #endif VM_SWAPUSAGE */

#elif HAVE_LIBKVM_GETSWAPINFO
static int swap_read (void) /* {{{ */
{
	struct kvm_swap data_s;
	int             status;

	gauge_t used;
	gauge_t total;

	if (kvm_obj == NULL)
		return (-1);

	/* only one structure => only get the grand total, no details */
	status = kvm_getswapinfo (kvm_obj, &data_s, 1, 0);
	if (status == -1)
		return (-1);

	total = (gauge_t) data_s.ksw_total;
	used  = (gauge_t) data_s.ksw_used;

	total *= (gauge_t) kvm_pagesize;
	used  *= (gauge_t) kvm_pagesize;

	swap_submit_usage (NULL, used, total - used, NULL, NAN);

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

	swap_submit_usage (NULL, (gauge_t) swap->used, (gauge_t) swap->free,
			NULL, NAN);

	return (0);
} /* }}} int swap_read */
/* #endif  HAVE_LIBSTATGRAB */

#elif HAVE_PERFSTAT
static int swap_read (void) /* {{{ */
{
	perfstat_memory_total_t pmemory;
	int status;

	gauge_t total;
	gauge_t free;
	gauge_t reserved;

	memset (&pmemory, 0, sizeof (pmemory));
        status = perfstat_memory_total (NULL, &pmemory, sizeof(perfstat_memory_total_t), 1);
	if (status < 0)
	{
                char errbuf[1024];
                WARNING ("swap plugin: perfstat_memory_total failed: %s",
                        sstrerror (errno, errbuf, sizeof (errbuf)));
                return (-1);
        }

	total    = (gauge_t) (pmemory.pgsp_total * pagesize);
	free     = (gauge_t) (pmemory.pgsp_free * pagesize);
	reserved = (gauge_t) (pmemory.pgsp_rsvd * pagesize);

	swap_submit_usage (NULL, total - free, free, "reserved", reserved);
	swap_submit_derive ("in",  (derive_t) pmemory.pgspins * pagesize);
	swap_submit_derive ("out", (derive_t) pmemory.pgspouts * pagesize);

	return (0);
} /* }}} int swap_read */
#endif /* HAVE_PERFSTAT */

void module_register (void)
{
	plugin_register_complex_config ("swap", swap_config);
	plugin_register_init ("swap", swap_init);
	plugin_register_read ("swap", swap_read);
} /* void module_register */

/* vim: set fdm=marker : */
