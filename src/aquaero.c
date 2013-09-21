/**
 * collectd - src/aquaero.c
 * Copyright (C) 2013  Alex Deymo
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
 *   Alex Deymo
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <libaquaero5.h>

/*
 * Private variables
 */
/* Default values for contacting daemon */
static char *conf_device = NULL;

static int aquaero_config (oconfig_item_t *ci)
{
	int i;

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Device", child->key))
			cf_util_get_string (child, &conf_device);
		else
		{
			ERROR ("aquaero plugin: Unknown config option \"%s\".",
					child->key);
		}
	}

	return (0);
}

static int aquaero_shutdown (void)
{
	libaquaero5_exit();
	return (0);
} /* int aquaero_shutdown */

static void aquaero_submit (const char *type, const char *type_instance,
		double value)
{
	const char *instance = conf_device?conf_device:"default";
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	/* Don't report undefined values. */
	if (value == AQ5_FLOAT_UNDEF)
		return;

	values[0].gauge = value;

	vl.values = values;
	vl.values_len = 1;

	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "aquaero", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* int aquaero_submit */

/* aquaero_submit_array submits every value of a given array of values */
static void aquaero_submit_array (const char *type,
		const char *type_instance_prefix, double *value_array, int len)
{
	char type_instance[DATA_MAX_NAME_LEN];
	int i;

	for (i = 0; i < len; i++)
	{
		if (value_array[i] == AQ5_FLOAT_UNDEF)
			continue;

		snprintf (type_instance, sizeof (type_instance), "%s%d",
				type_instance_prefix, i + 1);
		aquaero_submit (type, type_instance, value_array[i]);
	}
}

static int aquaero_read (void)
{
	aq5_data_t aq_data;
	aq5_settings_t aq_sett;
	char *err_msg = NULL;
	char type_instance[DATA_MAX_NAME_LEN];
	int i;

	if (libaquaero5_poll(conf_device, &aq_data, &err_msg) < 0)
	{
		char errbuf[1024];
		ERROR ("aquaero plugin: Failed to poll device \"%s\": %s (%s)",
				conf_device ? conf_device : "default", err_msg,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	if (libaquaero5_getsettings(conf_device, &aq_sett, &err_msg) < 0)
	{
		char errbuf[1024];
		ERROR ("aquaero plugin: Failed to get settings "
				"for device \"%s\": %s (%s)",
				conf_device ? conf_device : "default", err_msg,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (-1);
	}

	/* CPU Temperature sensor */
	aquaero_submit("temperature", "cpu", aq_data.cpu_temp[0]);

	/* Temperature sensors */
	aquaero_submit_array("temperature", "sensor", aq_data.temp,
			AQ5_NUM_TEMP);

	/* Virtual temperature sensors */
	aquaero_submit_array("temperature", "virtual", aq_data.vtemp,
			AQ5_NUM_VIRT_SENSORS);

	/* Software temperature sensors */
	aquaero_submit_array("temperature", "software", aq_data.stemp,
			AQ5_NUM_SOFT_SENSORS);

	/* Other temperature sensors */
	aquaero_submit_array("temperature", "other", aq_data.otemp,
			AQ5_NUM_OTHER_SENSORS);

	/* Fans */
	for (i = 0; i < AQ5_NUM_FAN; i++)
	{
		if ((aq_sett.fan_data_source[i] == NONE)
				|| (aq_data.fan_vrm_temp[i] != AQ5_FLOAT_UNDEF))
			continue;

		snprintf (type_instance, sizeof (type_instance),
				"fan%d", i + 1);

		aquaero_submit ("fanspeed", type_instance,
				aq_data.fan_rpm[i]);
		aquaero_submit ("percent", type_instance,
				aq_data.fan_duty[i]);
		aquaero_submit ("voltage", type_instance,
				aq_data.fan_voltage[i]);
		aquaero_submit ("current", type_instance,
				aq_data.fan_current[i]);

		/* Report the voltage reglator module (VRM) temperature with a
		 * different type instance. */
		snprintf (type_instance, sizeof (type_instance),
				"fan%d-vrm", i + 1);
		aquaero_submit ("temperature", type_instance,
				aq_data.fan_vrm_temp[i]);
	}

	/* Flow sensors */
	aquaero_submit_array("flow", "sensor", aq_data.flow, AQ5_NUM_FLOW);

	/* Liquid level */
	aquaero_submit_array("percent", "waterlevel",
			aq_data.level, AQ5_NUM_LEVEL);

	return (0);
}

void module_register (void)
{
	plugin_register_complex_config ("aquaero", aquaero_config);
	plugin_register_read ("aquaero", aquaero_read);
	plugin_register_shutdown ("aquaero", aquaero_shutdown);
} /* void module_register */

/* vim: set sw=8 sts=8 noet : */
