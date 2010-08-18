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
#include <sys/systemcfg.h>
#include <sys/utsname.h>

#ifndef XINTFRAC
# define XINTFRAC ((double)(_system_configuration.Xint) / \
                   (double)(_system_configuration.Xfrac))
#endif

static const char *config_keys[] =
{
  "CpuPoolStats"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);
static int pool_stats = 0;

/* As an LPAR can be moved transparently across physical systems
 * through Live Partition Mobility (LPM), and the resources we are
 * monitoring are tied to the underlying hardware, we need to keep
 * track on which physical server we are currently on. This is done
 * through the plugin instance which holds the chassis' serial.
 */
static u_longlong_t last_time_base;
static u_longlong_t last_pcpu_user,
                    last_pcpu_sys,
                    last_pcpu_idle,
                    last_pcpu_wait;
static u_longlong_t last_pool_idle_time = 0;
static u_longlong_t last_idle_donated_purr = 0,
                    last_busy_donated_purr = 0,
                    last_busy_stolen_purr = 0,
                    last_idle_stolen_purr = 0;
static int donate_flag = 0;


/* Save the current values for the next iteration */
static void save_last_values (perfstat_partition_total_t *lparstats)
{
	last_time_base = lparstats->timebase_last;

	last_pcpu_user = lparstats->puser;
	last_pcpu_sys  = lparstats->psys;
	last_pcpu_idle = lparstats->pidle;
	last_pcpu_wait = lparstats->pwait;

	if (donate_flag)
	{
		last_idle_donated_purr = lparstats->idle_donated_purr;
		last_busy_donated_purr = lparstats->busy_donated_purr;
		last_busy_stolen_purr  = lparstats->busy_stolen_purr;
		last_idle_stolen_purr  = lparstats->idle_stolen_purr;
	}

	last_pool_idle_time = lparstats->pool_idle_time;
}

static int lpar_config (const char *key, const char *value)
{
	if (strcasecmp ("CpuPoolStats", key) == 0)
	{
		if (IS_TRUE (value))
			pool_stats = 1;
		else
			pool_stats = 0;
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

	/* retrieve the initial metrics */
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

	/* save the initial data */
	save_last_values (&lparstats);

	return (0);
} /* int lpar_init */

static void lpar_submit (const char *plugin_inst, const char *type_instance, double value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = (gauge_t)value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "lpar", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_inst, sizeof (vl.plugin));
	sstrncpy (vl.type, "lpar_pcpu", sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
}

static int submit_counter (const char *plugin_instance, /* {{{ */
		const char *type, const char *type_instance, counter_t value)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].counter = value;

	vl.values = values;
	vl.values_len = 1;
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "lpar", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_inst, sizeof (vl.plugin));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	return (plugin_dispatch_values (&vl));
} /* }}} int submit_counter */

static int lpar_read (void)
{
	u_longlong_t delta_time_base;
	perfstat_partition_total_t lparstats;
	struct utsname name;
	char plugin_inst[DATA_MAX_NAME_LEN];
	_Bool have_donate = 0;

	/* retrieve the current physical server's id and build the plugin
	   instance's name */
	if (uname (&name) != 0)
	{
		ERROR ("lpar plugin: uname failed.");
		return (-1);
	}
	sstrncpy (plugin_inst, name.machine, sizeof (plugin_inst));

	/* retrieve the current metrics */
	if (!perfstat_partition_total (/* name = */ NULL, /* "must be set to NULL" */
				&lparstats, sizeof (lparstats),
				/* desired_number = */ 1 /* "must be set to 1" */))
	{
		ERROR ("lpar plugin: perfstat_partition_total failed.");
		return (-1);
	}

	if (!lparstats.type.b.shared_enabled
			&& lparstats.type.b.donate_enabled)
		have_donate = 1;

	delta_time_base = lparstats.timebase_last - last_time_base;
	if (delta_time_base == 0)
	{
		/* The system stats have not been updated since last time */
		return (0);
	}

	submit_counter (plugin_inst, "cpu", "user",   (counter_t) lparstats.puser);
	submit_counter (plugin_inst, "cpu", "system", (counter_t) lparstats.psys);
	submit_counter (plugin_inst, "cpu", "idle",   (counter_t) lparstats.pidle);
	submit_counter (plugin_inst, "cpu", "wait",   (counter_t) lparstats.pwait);

	/* FIXME: Use an appropriate GAUGE type here. */
	lpar_submit (plugin_inst, "ent",  (double)lparstats.entitled_proc_capacity / 100.0);
	lpar_submit (plugin_inst, "max",  (double)lparstats.max_proc_capacity / 100.0);
	lpar_submit (plugin_inst, "min",  (double)lparstats.min_proc_capacity / 100.0);

	if (have_donate)
	{
		dlt_idle_donated = lparstats.idle_donated_purr - last_idle_donated_purr;
		dlt_busy_donated = lparstats.busy_donated_purr - last_busy_donated_purr;
		dlt_idle_stolen  = lparstats.idle_stolen_purr  - last_idle_stolen_purr;
		dlt_busy_stolen  = lparstats.busy_stolen_purr  - last_busy_stolen_purr;

		submit_counter (plugin_inst, "cpu", "donated-idle", (counter_t) lparstats.idle_donated_purr);
		submit_counter (plugin_inst, "cpu", "donated-busy", (counter_t) lparstats.busy_donated_purr);
		submit_counter (plugin_inst, "cpu", "stolen-idle",  (counter_t) lparstats.idle_stolen_purr);
		submit_counter (plugin_inst, "cpu", "stolen-busy",  (counter_t) lparstats.busy_stolen_purr);
	}

	if (pool_stats)
	{
		if (!lparstats.type.b.pool_util_authority)
		{
			WARNING ("lpar plugin: Pool utilization data is not available.");
		}
		else
		{
			u_longlong_t dlt_pit;
			double total, idle;
			char type[DATA_MAX_NAME_LEN];

			/* FIXME: The pool id should probably be used as plugin instance. */
			dlt_pit = lparstats.pool_idle_time - last_pool_idle_time;
			total = (double)lparstats.phys_cpus_pool;
			idle  = (double)dlt_pit / XINTFRAC / (double)delta_time_base;
			ssnprintf (type, sizeof(type), "pool-%X-total", lparstats.pool_id);
			lpar_submit (plugin_inst, type, total);
			ssnprintf (type, sizeof(type), "pool-%X-used", lparstats.pool_id);
			lpar_submit (plugin_inst, type, total - idle);
		}
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

/* vim: set sw=8 sts=8 ts=8 noet : */

