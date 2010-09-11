/**
 * collectd - src/lpar.c
 * Copyright (C) 2010  Aurélien Reynaud
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
 *   Aurélien Reynaud <collectd at wattapower.net>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <sys/protosw.h>
#include <libperfstat.h>
#include <sys/utsname.h>

/* XINTFRAC was defined in libperfstat.h somewhere between AIX 5.3 and 6.1 */
#ifndef XINTFRAC
# include <sys/systemcfg.h>
# define XINTFRAC ((double)(_system_configuration.Xint) / \
                   (double)(_system_configuration.Xfrac))
#endif

#define NS_TO_TICKS(ns) ((ns) / XINTFRAC)

static const char *config_keys[] =
{
  "CpuPoolStats",
  "ReportBySerial"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static _Bool pool_stats = 0;
static _Bool report_by_serial = 0;
static _Bool donate_flag = 0;
static char serial[SYS_NMLN];

static u_longlong_t time_old;
static u_longlong_t user_old,
                    syst_old,
                    idle_old,
                    wait_old;
static u_longlong_t pool_busy_time_old,
                    pool_max_time_old;
static u_longlong_t idle_donated_old,
                    busy_donated_old,
                    busy_stolen_old,
                    idle_stolen_old;


static void save_last_values (perfstat_partition_total_t *lparstats)
{
	time_old = lparstats->timebase_last;

	user_old = lparstats->puser;
	syst_old = lparstats->psys;
	idle_old = lparstats->pidle;
	wait_old = lparstats->pwait;

	if (donate_flag)
	{
		idle_donated_old = lparstats->idle_donated_purr;
		busy_donated_old = lparstats->busy_donated_purr;
		busy_stolen_old  = lparstats->busy_stolen_purr;
		idle_stolen_old  = lparstats->idle_stolen_purr;
	}

	if (pool_stats)
	{
		pool_busy_time_old = lparstats->pool_busy_time;
		pool_max_time_old  = lparstats->pool_max_time;
	}
} /* void save_last_values */

static int lpar_config (const char *key, const char *value)
{
	if (strcasecmp ("CpuPoolStats", key) == 0)
	{
		if (IS_TRUE (value))
			pool_stats = 1;
		else
			pool_stats = 0;
	}
	else if (strcasecmp ("ReportBySerial", key) == 0)
	{
		if (IS_TRUE (value))
			report_by_serial = 1;
		else
			report_by_serial = 0;
	}
	else
	{
		return (-1);
	}

	return (0);
} /* int lpar_config */

static int lpar_init (void)
{
	perfstat_partition_total_t lparstats;
	int status;

	/* Retrieve the initial metrics. Returns the number of structures filled. */
	status = perfstat_partition_total (/* name = */ NULL, /* (must be NULL) */
			&lparstats, sizeof (perfstat_partition_total_t),
			/* number = */ 1 /* (must be 1) */);
	if (status != 1)
	{
		char errbuf[1024];
		ERROR ("lpar plugin: perfstat_partition_total failed: %s (%i)",
				sstrerror (errno, errbuf, sizeof (errbuf)),
				status);
		return (-1);
	}

	if (!lparstats.type.b.shared_enabled && lparstats.type.b.donate_enabled)
	{
		donate_flag = 1;
	}

	if (pool_stats && !lparstats.type.b.pool_util_authority)
	{
		WARNING ("lpar plugin: This partition does not have pool authority. "
				"Disabling CPU pool statistics collection.");
		pool_stats = 0;
	}

	/* Save the initial data */
	save_last_values (&lparstats);

	return (0);
} /* int lpar_init */

static void lpar_submit (const char *type_instance, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = (gauge_t)value;

	vl.values = values;
	vl.values_len = 1;
	if (report_by_serial)
	{
		sstrncpy (vl.host, serial, sizeof (vl.host));
		sstrncpy (vl.plugin_instance, hostname_g, sizeof (vl.plugin));
	}
	else
	{
		sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	}
	sstrncpy (vl.plugin, "lpar", sizeof (vl.plugin));
	sstrncpy (vl.type, "vcpu", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void lpar_submit */

static int lpar_read (void)
{
	perfstat_partition_total_t lparstats;
	int status;
	struct utsname name;
	u_longlong_t ticks;
	u_longlong_t user_ticks, syst_ticks, wait_ticks, idle_ticks;
	u_longlong_t consumed_ticks;
	double entitled_proc_capacity;

	/* An LPAR has the same serial number as the physical system it is currently
	   running on. It is a convenient way of tracking LPARs as they are moved
	   from chassis to chassis through Live Partition Mobility (LPM). */
	if (uname (&name) != 0)
	{
		ERROR ("lpar plugin: uname failed.");
		return (-1);
	}
	sstrncpy (serial, name.machine, sizeof (serial));

	/* Retrieve the current metrics. Returns the number of structures filled. */
	status = perfstat_partition_total (/* name = */ NULL, /* (must be NULL) */
			&lparstats, sizeof (perfstat_partition_total_t),
			/* number = */ 1 /* (must be 1) */);
	if (status != 1)
	{
		char errbuf[1024];
		ERROR ("lpar plugin: perfstat_partition_total failed: %s (%i)",
				sstrerror (errno, errbuf, sizeof (errbuf)),
				status);
		return (-1);
	}

	/* Number of ticks since we last run. */
	ticks = lparstats.timebase_last - time_old;
	if (ticks == 0)
	{
		/* The stats have not been updated. Return now to avoid dividing by zero */
		return (0);
	}

	/*
	 * On a shared partition, we're "entitled" to a certain amount of
	 * processing power, for example 250/100 of a physical CPU. Processing
	 * capacity not used by the partition may be assigned to a different
	 * partition by the hypervisor, so "idle" is hopefully a very small
	 * number.
	 */

	/* entitled_proc_capacity is in 1/100th of a CPU */
	entitled_proc_capacity = 0.01 * ((double) lparstats.entitled_proc_capacity);
	lpar_submit ("entitled", entitled_proc_capacity);

	/* The number of ticks actually spent in the various states */
	user_ticks = lparstats.puser - user_old;
	syst_ticks = lparstats.psys  - syst_old;
	wait_ticks = lparstats.pwait - wait_old;
	idle_ticks = lparstats.pidle - idle_old;
	consumed_ticks = user_ticks + syst_ticks + wait_ticks + idle_ticks;

	lpar_submit ("user", (double) user_ticks / (double) ticks);
	lpar_submit ("sys",  (double) syst_ticks / (double) ticks);
	lpar_submit ("wait", (double) wait_ticks / (double) ticks);
	lpar_submit ("idle", (double) idle_ticks / (double) ticks);

	if (donate_flag)
	{
		u_longlong_t idle_donated_ticks, busy_donated_ticks;
		u_longlong_t idle_stolen_ticks, busy_stolen_ticks;

		idle_donated_ticks = lparstats.idle_donated_purr - idle_donated_old;
		busy_donated_ticks = lparstats.busy_donated_purr - busy_donated_old;
		idle_stolen_ticks  = lparstats.idle_stolen_purr  - idle_stolen_old;
		busy_stolen_ticks  = lparstats.busy_stolen_purr  - busy_stolen_old;

		/* FYI:  PURR == Processor Utilization of Resources Register
		 *      SPURR == Scaled PURR */
		/* donated => ticks given to another partition
		 * stolen  => ticks received from another partition */
		lpar_submit ("idle_donated", (double) idle_donated_ticks / (double) ticks);
		lpar_submit ("busy_donated", (double) busy_donated_ticks / (double) ticks);
		lpar_submit ("idle_stolen",  (double) idle_stolen_ticks  / (double) ticks);
		lpar_submit ("busy_stolen",  (double) busy_stolen_ticks  / (double) ticks);

		/* Donated ticks will be accounted for as stolen ticks in other LPARs */
		consumed_ticks += idle_stolen_ticks + busy_stolen_ticks;
	}

	lpar_submit ("consumed", (double) consumed_ticks / (double) ticks);

	if (pool_stats)
	{
		char typinst[DATA_MAX_NAME_LEN];
		u_longlong_t pool_busy_ns, pool_max_ns;

		pool_busy_ns = lparstats.pool_busy_time - pool_busy_time_old;
		pool_max_ns  = lparstats.pool_max_time  - pool_max_time_old;

		/* Pool stats are in CPU x ns */
		ssnprintf (typinst, sizeof (typinst), "pool-%X-busy", lparstats.pool_id);
		lpar_submit (typinst, NS_TO_TICKS ((double) pool_busy_ns) / (double) ticks);

		ssnprintf (typinst, sizeof (typinst), "pool-%X-total", lparstats.pool_id);
		lpar_submit (typinst, NS_TO_TICKS ((double) pool_max_ns) / (double) ticks);
	}

	save_last_values (&lparstats);

	return (0);
} /* int lpar_read */

void module_register (void)
{
	plugin_register_config ("lpar", lpar_config,
	                        config_keys, config_keys_num);
	plugin_register_init ("lpar", lpar_init);
	plugin_register_read ("lpar", lpar_read);
} /* void module_register */

/* vim: set sw=8 noet : */

