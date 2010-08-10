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

/* Max length of the type instance string */
#define TYPE_INST_LEN (sizeof("lpar--total") + 2*sizeof(int) + 1)

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
static char plugin_inst[SYS_NMLN];

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

static void lpar_submit (const char *type_instance, double value)
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

static int lpar_read (void)
{
	u_longlong_t dlt_pcpu_user, dlt_pcpu_sys, dlt_pcpu_idle, dlt_pcpu_wait;
	u_longlong_t delta_time_base;
	perfstat_partition_total_t lparstats;
	struct utsname name;

	/* retrieve the current physical server's id and build the plugin
	   instance's name */
	if (uname (&name) != 0)
	{
		ERROR ("lpar plugin: uname failed.");
		return (-1);
	}
	sstrncpy (plugin_inst, name.machine, sizeof (plugin_inst));

	/* retrieve the current metrics */
	if (!perfstat_partition_total (NULL, &lparstats,
	                               sizeof (perfstat_partition_total_t), 1))
	{
		ERROR ("lpar plugin: perfstat_partition_total failed.");
		return (-1);
	}

	delta_time_base = lparstats.timebase_last - last_time_base;
	if (delta_time_base == 0)
	{
		/* The system stats have not been updated since last time */
		return (0);
	}

	dlt_pcpu_user = lparstats.puser - last_pcpu_user;
	dlt_pcpu_sys  = lparstats.psys  - last_pcpu_sys;
	dlt_pcpu_idle = lparstats.pidle - last_pcpu_idle;
	dlt_pcpu_wait = lparstats.pwait - last_pcpu_wait;

	lpar_submit ("user", (double)dlt_pcpu_user / delta_time_base);
	lpar_submit ("sys",  (double)dlt_pcpu_sys  / delta_time_base);
	lpar_submit ("wait", (double)dlt_pcpu_wait / delta_time_base);
	lpar_submit ("idle", (double)dlt_pcpu_idle / delta_time_base);
	lpar_submit ("ent",  (double)lparstats.entitled_proc_capacity / 100.0);
	lpar_submit ("max",  (double)lparstats.max_proc_capacity / 100.0);
	lpar_submit ("min",  (double)lparstats.min_proc_capacity / 100.0);

	if (donate_flag)
	{
		u_longlong_t dlt_busy_stolen, dlt_idle_stolen;
		u_longlong_t dlt_idle_donated, dlt_busy_donated;

		dlt_idle_donated = lparstats.idle_donated_purr - last_idle_donated_purr;
		dlt_busy_donated = lparstats.busy_donated_purr - last_busy_donated_purr;
		dlt_idle_stolen  = lparstats.idle_stolen_purr  - last_idle_stolen_purr;
		dlt_busy_stolen  = lparstats.busy_stolen_purr  - last_busy_stolen_purr;

		lpar_submit ("idle_donated", (double)dlt_idle_donated / delta_time_base);
		lpar_submit ("busy_donated", (double)dlt_busy_donated / delta_time_base);
		lpar_submit ("idle_stolen",  (double)dlt_idle_stolen  / delta_time_base);
		lpar_submit ("busy_stolen",  (double)dlt_busy_stolen  / delta_time_base);
	}

	if (pool_stats)
	{
		if (!lparstats.type.b.pool_util_authority)
		{
			WARNING ("lpar plugin: this system does not have pool authority.");
		}
		else
		{
			u_longlong_t dlt_pit;
			double total, idle;
			char type[TYPE_INST_LEN];

			dlt_pit = lparstats.pool_idle_time - last_pool_idle_time;
			total = (double)lparstats.phys_cpus_pool;
			idle  = (double)dlt_pit / XINTFRAC / (double)delta_time_base;
			ssnprintf (type, sizeof(type), "pool-%X-total", lparstats.pool_id);
			lpar_submit (type, total);
			ssnprintf (type, sizeof(type), "pool-%X-used", lparstats.pool_id);
			lpar_submit (type, total - idle);
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

/* vim: set sw=2 sts=2 ts=8 : */

