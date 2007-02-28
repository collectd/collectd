/**
 * collectd - src/sensors.c
 * Copyright (C) 2005-2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 *   
 *   Lubos Stanek <lubek at users.sourceforge.net> Wed Oct 27, 2006
 *   - config ExtendedSensorNaming option
 *   - precise sensor feature selection (chip-bus-address/type-feature)
 *     with ExtendedSensorNaming
 *   - more sensor features (finite list)
 *   - honor sensors.conf's ignored
 *   - config Sensor option
 *   - config IgnoreSelected option
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_ignorelist.h"
#include "utils_debug.h"

#if defined(HAVE_SENSORS_SENSORS_H)
# include <sensors/sensors.h>
#else
# undef HAVE_LIBSENSORS
#endif

#if defined(HAVE_LIBSENSORS)
# define SENSORS_HAVE_READ 1
#else
# define SENSORS_HAVE_READ 0
#endif

static data_source_t data_source_fanspeed[1] =
{
	{"value", DS_TYPE_GAUGE, 0, NAN}
};

static data_set_t fanspeed_ds =
{
	"fanspeed", 1, data_source_fanspeed
};

static data_source_t data_source_temperature[1] =
{
	{"value", DS_TYPE_GAUGE, -273.15, NAN}
};

static data_set_t temperature_ds =
{
	"temperature", 1, data_source_temperature
};

static data_source_t data_source_voltage[1] =
{
	{"value", DS_TYPE_GAUGE, NAN, NAN}
};

static data_set_t voltage_ds =
{
	"voltage", 1, data_source_voltage
};

#if SENSORS_HAVE_READ
#define SENSOR_TYPE_VOLTAGE     0
#define SENSOR_TYPE_FANSPEED    1
#define SENSOR_TYPE_TEMPERATURE 2
#define SENSOR_TYPE_UNKNOWN     3

static char *sensor_to_type[] =
{
	"voltage",
	"fanspeed",
	"temperature",
	NULL
};

struct sensors_labeltypes_s
{
	char *label;
	int type;
};
typedef struct sensors_labeltypes_s sensors_labeltypes_t;

/*
 * finite list of known labels extracted from lm_sensors
 */
static sensors_labeltypes_t known_features[] = 
{
	{ "fan1", SENSOR_TYPE_FANSPEED },
	{ "fan2", SENSOR_TYPE_FANSPEED },
	{ "fan3", SENSOR_TYPE_FANSPEED },
	{ "fan4", SENSOR_TYPE_FANSPEED },
	{ "fan5", SENSOR_TYPE_FANSPEED },
	{ "fan6", SENSOR_TYPE_FANSPEED },
	{ "fan7", SENSOR_TYPE_FANSPEED },
	{ "AIN2", SENSOR_TYPE_VOLTAGE },
	{ "AIN1", SENSOR_TYPE_VOLTAGE },
	{ "in10", SENSOR_TYPE_VOLTAGE },
	{ "in9", SENSOR_TYPE_VOLTAGE },
	{ "in8", SENSOR_TYPE_VOLTAGE },
	{ "in7", SENSOR_TYPE_VOLTAGE },
	{ "in6", SENSOR_TYPE_VOLTAGE },
	{ "in5", SENSOR_TYPE_VOLTAGE },
	{ "in4", SENSOR_TYPE_VOLTAGE },
	{ "in3", SENSOR_TYPE_VOLTAGE },
	{ "in2", SENSOR_TYPE_VOLTAGE },
	{ "in0", SENSOR_TYPE_VOLTAGE },
	{ "CPU_Temp", SENSOR_TYPE_TEMPERATURE },
	{ "remote_temp", SENSOR_TYPE_TEMPERATURE },
	{ "temp1", SENSOR_TYPE_TEMPERATURE },
	{ "temp2", SENSOR_TYPE_TEMPERATURE },
	{ "temp3", SENSOR_TYPE_TEMPERATURE },
	{ "temp4", SENSOR_TYPE_TEMPERATURE },
	{ "temp5", SENSOR_TYPE_TEMPERATURE },
	{ "temp6", SENSOR_TYPE_TEMPERATURE },
	{ "temp7", SENSOR_TYPE_TEMPERATURE },
	{ "temp", SENSOR_TYPE_TEMPERATURE },
	{ "Vccp2", SENSOR_TYPE_VOLTAGE },
	{ "Vccp1", SENSOR_TYPE_VOLTAGE },
	{ "vdd", SENSOR_TYPE_VOLTAGE },
	{ "vid5", SENSOR_TYPE_VOLTAGE },
	{ "vid4", SENSOR_TYPE_VOLTAGE },
	{ "vid3", SENSOR_TYPE_VOLTAGE },
	{ "vid2", SENSOR_TYPE_VOLTAGE },
	{ "vid1", SENSOR_TYPE_VOLTAGE },
	{ "vid", SENSOR_TYPE_VOLTAGE },
	{ "vin4", SENSOR_TYPE_VOLTAGE },
	{ "vin3", SENSOR_TYPE_VOLTAGE },
	{ "vin2", SENSOR_TYPE_VOLTAGE },
	{ "vin1", SENSOR_TYPE_VOLTAGE },
	{ "voltbatt", SENSOR_TYPE_VOLTAGE },
	{ "volt12", SENSOR_TYPE_VOLTAGE },
	{ "volt5", SENSOR_TYPE_VOLTAGE },
	{ "vrm", SENSOR_TYPE_VOLTAGE },
	{ "5.0V", SENSOR_TYPE_VOLTAGE },
	{ "5V", SENSOR_TYPE_VOLTAGE },
	{ "3.3V", SENSOR_TYPE_VOLTAGE },
	{ "2.5V", SENSOR_TYPE_VOLTAGE },
	{ "2.0V", SENSOR_TYPE_VOLTAGE },
	{ "12V", SENSOR_TYPE_VOLTAGE },
	{ (char *) 0, SENSOR_TYPE_UNKNOWN }
};
/* end new naming */

static const char *config_keys[] =
{
	"Sensor",
	"IgnoreSelected",
	NULL
};
static int config_keys_num = 2;

static ignorelist_t *sensor_list;

#ifndef SENSORS_CONF_PATH
# define SENSORS_CONF_PATH "/etc/sensors.conf"
#endif

static const char *conffile = SENSORS_CONF_PATH;
/* SENSORS_CONF_PATH */

/*
 * remember stat of the loaded config
 */
static time_t sensors_config_mtime = 0;

typedef struct featurelist
{
	const sensors_chip_name    *chip;
	const sensors_feature_data *data;
	int                         type;
	struct featurelist         *next;
} featurelist_t;

featurelist_t *first_feature = NULL;

static int sensors_config (const char *key, const char *value)
{
	if (sensor_list == NULL)
		sensor_list = ignorelist_create (1);

	if (strcasecmp (key, "Sensor") == 0)
	{
		if (ignorelist_add (sensor_list, value))
		{
			syslog (LOG_ERR, "sensors plugin: "
					"Cannot add value to ignorelist.");
			return (1);
		}
	}
	else if (strcasecmp (key, "IgnoreSelected") == 0)
	{
		ignorelist_set_invert (sensor_list, 1);
		if ((strcasecmp (value, "True") == 0)
				|| (strcasecmp (value, "Yes") == 0)
				|| (strcasecmp (value, "On") == 0))
			ignorelist_set_invert (sensor_list, 0);
	}
	else
	{
		return (-1);
	}

	return (0);
}

void sensors_free_features (void)
{
	featurelist_t *thisft;
	featurelist_t *nextft;

	if (first_feature == NULL)
		return;

	sensors_cleanup ();

	for (thisft = first_feature; thisft != NULL; thisft = nextft)
	{
		nextft = thisft->next;
		sfree (thisft);
	}
	first_feature = NULL;
}

static void sensors_load_conf (void)
{
	FILE *fh;
	featurelist_t *last_feature = NULL;
	featurelist_t *new_feature = NULL;
	
	const sensors_chip_name *chip;
	int chip_num;

	const sensors_feature_data *data;
	int data_num0, data_num1;

	struct stat statbuf;
	int status;
	
	status = stat (conffile, &statbuf);
	if (status != 0)
	{
		syslog (LOG_ERR, "sensors plugin: stat (%s) failed: %s",
				conffile, strerror (errno));
		sensors_config_mtime = 0;
	}

	if ((sensors_config_mtime != 0)
			&& (sensors_config_mtime == statbuf.st_mtime))
		return;

	if (sensors_config_mtime != 0)
	{
		syslog (LOG_NOTICE, "sensors plugin: Reloading config from %s",
				conffile);
		sensors_free_features ();
		sensors_config_mtime = 0;
	}

	fh = fopen (conffile, "r");
	if (fh == NULL)
	{
		syslog (LOG_ERR, "sensors plugin: fopen(%s) failed: %s",
				conffile, strerror(errno));
		return;
	}

	status = sensors_init (fh);
	fclose (fh);
	if (status != 0)
	{
		syslog (LOG_ERR, "sensors plugin: Cannot initialize sensors. "
				"Data will not be collected.");
		return;
	}

	sensors_config_mtime = statbuf.st_mtime;

	chip_num = 0;
	while ((chip = sensors_get_detected_chips (&chip_num)) != NULL)
	{
		data = NULL;
		data_num0 = data_num1 = 0;

		while ((data = sensors_get_all_features (*chip, &data_num0, &data_num1))
				!= NULL)
		{
			int i;

			/* "master features" only */
			if (data->mapping != SENSORS_NO_MAPPING)
				continue;

			/* Only known features */
			for (i = 0; known_features[i].type >= 0; i++)
			{
				if (strcmp (data->name, known_features[i].label) != 0)
					continue;

				/* skip ignored in sensors.conf */
				if (sensors_get_ignored (*chip, data->number) == 0)
					break;

				DBG ("Adding feature: %s-%s-%s",
						chip->prefix,
						sensor_to_type[known_features[i].type],
						data->name);

				if ((new_feature = (featurelist_t *) malloc (sizeof (featurelist_t))) == NULL)
				{
					DBG ("malloc: %s", strerror (errno));
					syslog (LOG_ERR, "sensors plugin:  malloc: %s",
							strerror (errno));
					break;
				}

				new_feature->chip = chip;
				new_feature->data = data;
				new_feature->type = known_features[i].type;
				new_feature->next = NULL;

				if (first_feature == NULL)
				{
					first_feature = new_feature;
					last_feature  = new_feature;
				}
				else
				{
					last_feature->next = new_feature;
					last_feature = new_feature;
				}

				/* stop searching known features at first found */
				break;
			} /* for i */
		} /* while sensors_get_all_features */
	} /* while sensors_get_detected_chips */

	if (first_feature == NULL)
	{
		sensors_cleanup ();
		syslog (LOG_INFO, "sensors plugin: lm_sensors reports no "
				"features. Data will not be collected.");
	}
} /* void sensors_load_conf */

static int sensors_shutdown (void)
{
	sensors_free_features ();
	ignorelist_free (sensor_list);

	return (0);
} /* int sensors_shutdown */

static void sensors_submit (const char *plugin_instance,
		const char *type, const char *type_instance,
		double val)
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	if (ignorelist_match (sensor_list, type_instance))
		return;

	values[0].gauge = val;

	vl.values = values;
	vl.values_len = 1;
	vl.time = time (NULL);
	strcpy (vl.host, hostname_g);
	strcpy (vl.plugin, "sensors");
	strcpy (vl.plugin_instance, plugin_instance);
	strcpy (vl.type_instance, type_instance);

	plugin_dispatch_values (type, &vl);
} /* void sensors_submit */

static int sensors_read (void)
{
	featurelist_t *feature;
	double value;

	char plugin_instance[DATA_MAX_NAME_LEN];
	char type_instance[DATA_MAX_NAME_LEN];

	sensors_load_conf ();

	for (feature = first_feature; feature != NULL; feature = feature->next)
	{
		if (sensors_get_feature (*feature->chip, feature->data->number, &value) < 0)
			continue;

		/* full chip name logic borrowed from lm_sensors */
		if (feature->chip->bus == SENSORS_CHIP_NAME_BUS_ISA)
		{
			if (snprintf (plugin_instance, DATA_MAX_NAME_LEN, "%s-isa-%04x",
						feature->chip->prefix,
						feature->chip->addr)
					>= 512)
				continue;
		}
		else if (feature->chip->bus == SENSORS_CHIP_NAME_BUS_DUMMY)
		{
			if (snprintf (plugin_instance, 512, "%s-%s-%04x",
						feature->chip->prefix,
						feature->chip->busname,
						feature->chip->addr)
					>= 512)
				continue;
		}
		else
		{
			if (snprintf (plugin_instance, 512, "%s-i2c-%d-%02x",
						feature->chip->prefix,
						feature->chip->bus,
						feature->chip->addr)
					>= 512)
				continue;
		}

		strncpy (type_instance, feature->data->name, DATA_MAX_NAME_LEN);

		sensors_submit (plugin_instance,
				sensor_to_type[feature->type],
				type_instance,
				value);
	} /* for feature = first_feature .. NULL */

	return (0);
} /* int sensors_read */
#endif /* SENSORS_HAVE_READ */

void module_register (void)
{
	plugin_register_data_set (&fanspeed_ds);
	plugin_register_data_set (&temperature_ds);
	plugin_register_data_set (&voltage_ds);

#if SENSORS_HAVE_READ
	plugin_register_config ("sensors", sensors_config,
			config_keys, config_keys_num);
	plugin_register_read ("sensors", sensors_read);
	plugin_register_shutdown ("sensors", sensors_shutdown);
#endif
} /* void module_register */
