/**
 * collectd - src/uptime.c
 * Copyright (C) 2009	Marco Chiappero
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
 *   Marco Chiappero <marco at absence.it>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#if KERNEL_LINUX
# define STAT_FILE "/proc/stat"
/* Using /proc filesystem to retrieve the boot time, Linux only. */
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
/* Using kstats chain to retrieve the boot time on Solaris / OpenSolaris systems */
/* #endif HAVE_LIBKSTAT */

#elif HAVE_SYS_SYSCTL_H
# include <sys/sysctl.h>
/* Using sysctl interface to retrieve the boot time on *BSD / Darwin / OS X systems */
/* #endif HAVE_SYS_SYSCTL_H */

#else
# error "No applicable input method."
#endif

/*
 * Global variables
 */
/* boottime always used, no OS distinction */
static time_t boottime;

#if HAVE_LIBKSTAT
extern kstat_ctl_t *kc;
#endif /* #endif HAVE_LIBKSTAT */

static void uptime_submit (gauge_t uptime)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = uptime;

	vl.values = values;
	vl.values_len = 1;

	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "uptime", sizeof (vl.plugin));
	sstrncpy (vl.type, "uptime", sizeof (vl.type));

	plugin_dispatch_values (&vl);
}

static int uptime_init (void) /* {{{ */
{
	/*
	 * On most unix systems the uptime is calculated by looking at the boot
	 * time (stored in unix time, since epoch) and the current one. We are
	 * going to do the same, reading the boot time value while executing
	 * the uptime_init function (there is no need to read, every time the
	 * plugin_read is called, a value that won't change). However, since
	 * uptime_init is run only once, if the function fails in retrieving
	 * the boot time, the plugin is unregistered and there is no chance to
	 * try again later. Nevertheless, this is very unlikely to happen.
	 */

#if KERNEL_LINUX
	unsigned long starttime;
	char buffer[1024];
	int ret;
	FILE *fh;

	ret = 0;

	fh = fopen (STAT_FILE, "r");

	if (fh == NULL)
	{
		char errbuf[1024];
		ERROR ("uptime plugin: Cannot open "STAT_FILE": %s",
			sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	while (fgets (buffer, 1024, fh) != NULL)
	{
		/* look for the btime string and read the value */
		ret = sscanf (buffer, "btime %lu", &starttime);
		/* avoid further loops if btime has been found and read
		 * correctly (hopefully) */
		if (ret == 1)
			break;
	}

	fclose (fh);

	/* loop done, check if no value has been found/read */
	if (ret != 1)
	{
		ERROR ("uptime plugin: No value read from "STAT_FILE"");
		return (-1);
	}

	boottime = (time_t) starttime;

	if (boottime == 0)
	{
		ERROR ("uptime plugin: btime read from "STAT_FILE", "
				"but `boottime' is zero!");
		return (-1);
	}
/* #endif KERNEL_LINUX */

#elif HAVE_LIBKSTAT
	kstat_t *ksp;
	kstat_named_t *knp;

	ksp = NULL;
	knp = NULL;

	/* kstats chain already opened by update_kstat (using *kc), verify everything went fine. */
	if (kc == NULL)
	{
		ERROR ("uptime plugin: kstat chain control structure not available.");
		return (-1);
	}

	ksp = kstat_lookup (kc, "unix", 0, "system_misc");
	if (ksp == NULL)
	{
		ERROR ("uptime plugin: Cannot find unix:0:system_misc kstat.");
		return (-1);
	}

	if (kstat_read (kc, ksp, NULL) < 0)
	{
		ERROR ("uptime plugin: kstat_read failed.");
		return (-1);
	}

	knp = (kstat_named_t *) kstat_data_lookup (ksp, "boot_time");
	if (knp == NULL)
	{
		ERROR ("uptime plugin: kstat_data_lookup (boot_time) failed.");
		return (-1);
	}

	boottime = (time_t) knp->value.ui32;

	if (boottime == 0)
	{
		ERROR ("uptime plugin: kstat_data_lookup returned success, "
			"but `boottime' is zero!");
		return (-1);
	}
/* #endif HAVE_LIBKSTAT */

# elif HAVE_SYS_SYSCTL_H
	struct timeval boottv;
	size_t boottv_len;
	int status;

	int mib[2];

	mib[0] = CTL_KERN;
	mib[1] = KERN_BOOTTIME;

	boottv_len = sizeof (boottv);
	memset (&boottv, 0, boottv_len);

	status = sysctl (mib, STATIC_ARRAY_SIZE (mib), &boottv, &boottv_len,
			/* new_value = */ NULL, /* new_length = */ 0);
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("uptime plugin: No value read from sysctl interface: %s",
			sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	boottime = boottv.tv_sec;

	if (boottime == 0)
	{
		ERROR ("uptime plugin: sysctl(3) returned success, "
				"but `boottime' is zero!");
		return (-1);
	}
#endif /* HAVE_SYS_SYSCTL_H */

	return (0);
} /* }}} int uptime_init */

static int uptime_read (void)
{
	gauge_t uptime;
	time_t elapsed;

	/* calculate the ammount of time elapsed since boot, AKA uptime */
	elapsed = time (NULL) - boottime;

	uptime = (gauge_t) elapsed;

	uptime_submit (uptime);

	return (0);
}

void module_register (void)
{
	plugin_register_init ("uptime", uptime_init);
	plugin_register_read ("uptime", uptime_read);
} /* void module_register */
