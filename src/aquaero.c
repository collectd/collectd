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

#define _BSD_SOURCE

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <libaquaero5.h>

/*
 * Private variables
 */
/* Default values for contacting daemon */
static char *conf_device = NULL;

static const char *config_keys[] =
{
	"Device",
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);


static int aquaero_config (const char *key, const char *value)
{
	if (strcasecmp (key, "Device") == 0)
	{
		if (conf_device != NULL)
		{
			free (conf_device);
			conf_device = NULL;
		}
		if (value[0] == '\0')
			return (0);
		if ((conf_device = strdup (value)) == NULL)
			return (1);
	}
	else
	{
		return (-1);
	}
	return (0);
} /* int aquaero_config */

static int aquaero_shutdown (void)
{
	libaquaero5_exit();
	return (0);
} /* int aquaero_shutdown */

static void aquaero_submit (const char *type, const char *type_instance, double value)
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
static void aquaero_submit_array (const char *type, const char *type_instance_prefix, double *value_array, int len)
{
	char type_instance[64];
	int i;

	for (i = 0; i < len; i++)
		if (value_array[i] != AQ5_FLOAT_UNDEF)
		{
			snprintf(type_instance, sizeof(type_instance), "%s%d", type_instance_prefix, i+1);
			aquaero_submit(type, type_instance, value_array[i]);
		}
}

static int aquaero_read (void)
{
	aq5_data_t aq_data;
	aq5_settings_t aq_sett;
	char *err_msg = NULL;
	char type_instance[64];
	int i;

	if (libaquaero5_poll(conf_device, &aq_data, &err_msg) < 0)
	{
		ERROR ("Failed to poll device '%s': %s (%s)",
				conf_device?conf_device:"default", err_msg, strerror(errno));
		return (-1);
	}

	if (libaquaero5_getsettings(NULL, &aq_sett, &err_msg) < 0)
	{
		ERROR ("Failed to get settings for device '%s': %s (%s)\n",
				conf_device?conf_device:"default", err_msg, strerror(errno));
		return (-1);
	}

	/* Temperature sensors */
	aquaero_submit_array("temperature", "temp", aq_data.temp, AQ5_NUM_TEMP);

	/* Virtual temperature sensors */
	aquaero_submit_array("temperature", "virttemp", aq_data.vtemp, AQ5_NUM_VIRT_SENSORS);

	/* Software temperature sensors */
	aquaero_submit_array("temperature", "softtemp", aq_data.stemp, AQ5_NUM_SOFT_SENSORS);

	/* Other temperature sensors */
	aquaero_submit_array("temperature", "othertemp", aq_data.otemp, AQ5_NUM_OTHER_SENSORS);

	/* Fans */
	for (i = 0; i < AQ5_NUM_FAN; i++)
	{
		if ((aq_sett.fan_data_source[i] != NONE) && (aq_data.fan_vrm_temp[i] != AQ5_FLOAT_UNDEF))
		{
			snprintf(type_instance, sizeof(type_instance), "fan%d", i+1);
			aquaero_submit("fanspeed", type_instance, aq_data.fan_rpm[i]);
			snprintf(type_instance, sizeof(type_instance), "fan-vrm%d", i+1);
			aquaero_submit("temperature", type_instance, aq_data.fan_vrm_temp[i]);

			snprintf(type_instance, sizeof(type_instance), "fan-duty%d", i+1);
			aquaero_submit("percentage", type_instance, aq_data.fan_duty[i]);

			snprintf(type_instance, sizeof(type_instance), "fan-voltage%d", i+1);
			aquaero_submit("voltage", type_instance, aq_data.fan_voltage[i]);
			snprintf(type_instance, sizeof(type_instance), "fan-current%d", i+1);
			aquaero_submit("current", type_instance, aq_data.fan_current[i]);
		}
	}

	/* Flow sensors */
	aquaero_submit_array("flow", "flow", aq_data.flow, AQ5_NUM_FLOW);

	/* Liquid level */
	aquaero_submit_array("level", "level", aq_data.level, AQ5_NUM_LEVEL);

	return (0);
}

void module_register (void)
{
	plugin_register_config ("aquaero", aquaero_config, config_keys,
			config_keys_num);
	plugin_register_read ("aquaero", aquaero_read);
	plugin_register_shutdown ("aquaero", aquaero_shutdown);
} /* void module_register */
