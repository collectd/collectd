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

#ifndef XINTFRAC
# include <sys/systemcfg.h>
# define XINTFRAC ((double)(_system_configuration.Xint) / \
                   (double)(_system_configuration.Xfrac))
#endif
#define HTIC2SEC(x)	((double)x * XINTFRAC / 1000000000.0)

/* Max length of the type instance string */
#define TYPE_INST_LEN (sizeof("pool--total") + 2*sizeof(int) + 1)

static const char *config_keys[] =
{
  "CpuPoolStats",
  "ReportBySerial"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static _Bool pool_stats = 0;
static _Bool report_by_serial = 0;

static u_longlong_t last_time_base;
static u_longlong_t ent_counter;
static _Bool donate_flag = 0;


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

	/* Retrieve the initial metrics */
	if (!perfstat_partition_total (NULL, &lparstats,
	                               sizeof (perfstat_partition_total_t), 1))
	{
		ERROR ("lpar plugin: perfstat_partition_total failed.");
		return (-1);
	}

	if (!lparstats.type.b.shared_enabled && lparstats.type.b.donate_enabled)
	{
		donate_flag = 1;
	}

	if (pool_stats && !lparstats.type.b.pool_util_authority)
	{
		WARNING ("lpar plugin: this system does not have pool authority. "
			"Disabling CPU pool statistics collection.");
		pool_stats = 0;
	}

	/* Initialize the fake counter for entitled capacity */
	last_time_base = lparstats.timebase_last;
	ent_counter = 0;

	return (0);
} /* int lpar_init */

static void lpar_submit (const char *type_instance, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	/* Although it appears as a double, value is really a (scaled) counter,
	   expressed in CPU x seconds. At high collection rates (< 1 min), its
	   integer part is very small and the resulting graphs get blocky. We regain
	   some precision by applying a x100 factor before casting it to a counter,
	   turning the final value into CPU units instead of CPUs. */
	values[0].counter = (counter_t)(value * 100.0 + 0.5);

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

static int lpar_read (void)
{
	u_longlong_t delta_time_base;
	perfstat_partition_total_t lparstats;

	/* Retrieve the current metrics */
	if (!perfstat_partition_total (NULL, &lparstats,
	                               sizeof (perfstat_partition_total_t), 1))
	{
		ERROR ("lpar plugin: perfstat_partition_total failed.");
		return (-1);
	}

	delta_time_base = lparstats.timebase_last - last_time_base;
	last_time_base  = lparstats.timebase_last;

	lpar_submit ("user", HTIC2SEC(lparstats.puser));
	lpar_submit ("sys",  HTIC2SEC(lparstats.psys));
	lpar_submit ("wait", HTIC2SEC(lparstats.pwait));
	lpar_submit ("idle", HTIC2SEC(lparstats.pidle));
	/* Entitled capacity is reported as an absolute value instead of a counter,
	   so we fake one. It's also in CPU units, hence the division by 100 before
	   submission. */
	ent_counter += lparstats.entitled_proc_capacity * delta_time_base;
	lpar_submit ("ent",  HTIC2SEC(ent_counter) / 100.0);

	if (donate_flag)
	{
		lpar_submit ("idle_donated", HTIC2SEC(lparstats.idle_donated_purr));
		lpar_submit ("busy_donated", HTIC2SEC(lparstats.busy_donated_purr));
		lpar_submit ("idle_stolen",  HTIC2SEC(lparstats.idle_stolen_purr));
		lpar_submit ("busy_stolen",  HTIC2SEC(lparstats.busy_stolen_purr));
	}

	if (pool_stats)
	{
		char typinst[TYPE_INST_LEN];

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
	plugin_register_init ("lpar", lpar_init);
	plugin_register_read ("lpar", lpar_read);
} /* void module_register */

/* vim: set sw=8 noet : */

