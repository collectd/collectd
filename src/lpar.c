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
 *   Aurelien Reynaud <collectd at wattapower.net>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <sys/protosw.h>
#include <libperfstat.h>
#include <sys/utsname.h>

static const char *config_keys[] =
{
  "CpuPoolStats",
  "ReportBySerial"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static _Bool pool_stats = 0;
static _Bool report_by_serial = 0;

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

static void lpar_submit (const char *type_instance, counter_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	/* Although it appears as a double, value is really a (scaled) counter,
	   expressed in CPU x seconds. At high collection rates (< 1 min), its
	   integer part is very small and the resulting graphs get blocky. We regain
	   some precision by applying a x100 factor before casting it to a counter,
	   turning the final value into CPU units instead of CPUs. */
	values[0].counter = value;

	vl.values = values;
	vl.values_len = 1;

	/* An LPAR has the same serial number as the physical system it is currently
	   running on. It is a convenient way of tracking LPARs as they are moved
	   from chassis to chassis through Live Partition Mobility (LPM). */
	if (report_by_serial)
	{
		struct utsname name;
		if (uname (&name) != 0)
		{
			ERROR ("lpar plugin: uname failed.");
			return;
		}
		sstrncpy (vl.host, name.machine, sizeof (vl.host));
		sstrncpy (vl.plugin_instance, hostname_g, sizeof (vl.plugin));
	}
	else
	{
		sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	}
	sstrncpy (vl.plugin, "lpar", sizeof (vl.plugin));
	sstrncpy (vl.type, "cpu", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static int lpar_read_shared_partition (const perfstat_partition_total_t *data)
{
	static counter_t time_old;
	static counter_t user_old;
	static counter_t syst_old;
	static counter_t wait_old;
	static counter_t idle_old;
	static counter_t unav_old;

	counter_t user = (counter_t) data->puser;
	counter_t syst = (counter_t) data->psys;
	counter_t wait = (counter_t) data->pwait;
	counter_t idle = (counter_t) data->pidle;
	counter_t unav = 0;

	/*
	 * On a shared partition, we're "entitled" to a certain amount of
	 * processing power, for example 250/100 of a physical CPU. Processing
	 * capacity not used by the partition may be assigned to a different
	 * partition by the hypervisor, so "idle" is hopefully a very small
	 * number.
	 *
	 * We calculate the amount of ticks assigned to a different partition
	 * from the number of ticks we're entitled to and the number of ticks
	 * we used up.
	 */
	if (time_old != 0)
	{
		counter_t time_diff;
		counter_t entitled_ticks;
		counter_t consumed_ticks;
		counter_t user_diff;
		counter_t syst_diff;
		counter_t wait_diff;
		counter_t idle_diff;
		counter_t unav_diff;

		double entitled_pool_capacity;

		/* Number of ticks since we last run. */
		time_diff = ((counter_t) data->timebase_last) - time_old;

		/* entitled_pool_capacity is in 1/100th of a CPU */
		entitled_pool_capacity = 0.01 * ((double) data->entitled_pool_capacity);

		/* The number of ticks this partition would have been entitled to. */
		entitled_ticks = (counter_t) ((entitled_pool_capacity * ((double) time_diff)) + .5);

		/* The number of ticks actually spent in the various states */
		user_diff = user - user_old;
		syst_diff = syst - syst_old;
		wait_diff = wait - wait_old;
		idle_diff = idle - idle_old;
		consumed_ticks = user_diff + syst_diff + wait_diff + idle_diff;

		/* "uncapped" partitions are allowed to consume more ticks than
		 * they are entitled to. */
		if (entitled_ticks >= consumed_ticks)
			unav_diff = entitled_ticks - consumed_ticks;
		else
			unav_diff = 0;
		unav = unav_old + unav_diff;

		lpar_submit ("user", user);
		lpar_submit ("system", syst);
		lpar_submit ("wait", wait);
		lpar_submit ("idle", idle);
		lpar_submit ("unavailable", unav);
	}

	time_old = (counter_t) data->timebase_last;
	user_old = user;
	syst_old = syst;
	wait_old = wait;
	idle_old = idle;
	unav_old = unav;

	return (0);
} /* int lpar_read_shared_partition */

static int lpar_read_dedicated_partition (const perfstat_partition_total_t *data)
{
	lpar_submit ("user",   (counter_t) data->puser);
	lpar_submit ("system", (counter_t) data->psys);
	lpar_submit ("wait",   (counter_t) data->pwait);
	lpar_submit ("idle",   (counter_t) data->pidle);

	if (data->type.b.donate_enabled)
	{
		/* FYI:  PURR == Processor Utilization of Resources Register
		 *      SPURR == Scaled PURR */
		lpar_submit ("idle_donated", (counter_t) data->idle_donated_purr);
		lpar_submit ("busy_donated", (counter_t) data->busy_donated_purr);
		lpar_submit ("idle_stolen",  (counter_t) data->idle_stolen_purr);
		lpar_submit ("busy_stolen",  (counter_t) data->busy_stolen_purr);
	}

	return (0);
} /* int lpar_read_dedicated_partition */

static int lpar_read (void)
{
	perfstat_partition_total_t lparstats;
	int status;

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

	if (lparstats.type.b.shared_enabled)
		lpar_read_shared_partition (&lparstats);
	else /* if (!shared_enabled) */
		lpar_read_dedicated_partition (&lparstats);

	if (pool_stats && !lparstats.type.b.pool_util_authority)
	{
		WARNING ("lpar plugin: This partition does not have pool authority. "
				"Disabling CPU pool statistics collection.");
		pool_stats = 0;
	}

	if (pool_stats)
	{
		char typinst[DATA_MAX_NAME_LEN];

		/* Pool stats are in CPU x ns */
		ssnprintf (typinst, sizeof(typinst), "pool-%X-busy", lparstats.pool_id);
		lpar_submit (typinst, (double)lparstats.pool_busy_time / 1000000000.0);

		ssnprintf (typinst, sizeof(typinst), "pool-%X-total", lparstats.pool_id);
		lpar_submit (typinst, (double)lparstats.pool_max_time / 1000000000.0);
	}

	return (0);
} /* int lpar_read */

void module_register (void)
{
	plugin_register_config ("lpar", lpar_config,
	                        config_keys, config_keys_num);
	plugin_register_read ("lpar", lpar_read);
} /* void module_register */

/* vim: set sw=8 noet : */

