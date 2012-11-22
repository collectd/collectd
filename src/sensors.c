/**
 * collectd - src/sensors.c
 * Copyright (C) 2005-2008  Florian octo Forster
 * Copyright (C) 2006       Luboš Staněk
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
 *
 *   Henrique de Moraes Holschuh <hmh at debian.org>
 *   - use default libsensors config file on API 0x400
 *   - config SensorConfigFile option
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_ignorelist.h"

#if defined(HAVE_SENSORS_SENSORS_H)
# include <sensors/sensors.h>
#endif

#if !defined(SENSORS_API_VERSION)
# define SENSORS_API_VERSION 0x000
#endif

/*
 * The sensors library prior to version 3.0 (internal version 0x400) didn't
 * report the type of values, only a name. The following lists are there to
 * convert from the names to the type. They are not used with the new
 * interface.
 */
#if SENSORS_API_VERSION < 0x400
static char *sensor_type_name_map[] =
{
# define SENSOR_TYPE_VOLTAGE     0
	"voltage",
# define SENSOR_TYPE_FANSPEED    1
	"fanspeed",
# define SENSOR_TYPE_TEMPERATURE 2
	"temperature",
# define SENSOR_TYPE_UNKNOWN     3
	NULL
};

struct sensors_labeltypes_s
{
	char *label;
	int type;
};
typedef struct sensors_labeltypes_s sensors_labeltypes_t;

/* finite list of known labels extracted from lm_sensors */
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
	{ "12V", SENSOR_TYPE_VOLTAGE }
};
static int known_features_num = STATIC_ARRAY_SIZE (known_features);
/* end new naming */
#endif /* SENSORS_API_VERSION < 0x400 */

static const char *config_keys[] =
{
	"Sensor",
	"IgnoreSelected",
	"SensorConfigFile"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

#if SENSORS_API_VERSION < 0x400
typedef struct featurelist
{
	const sensors_chip_name    *chip;
	const sensors_feature_data *data;
	int                         type;
	struct featurelist         *next;
} featurelist_t;

# ifndef SENSORS_CONF_PATH
#  define SENSORS_CONF_PATH "/etc/sensors.conf"
# endif
static char *conffile = SENSORS_CONF_PATH;
/* #endif SENSORS_API_VERSION < 0x400 */

#elif (SENSORS_API_VERSION >= 0x400) && (SENSORS_API_VERSION < 0x500)
typedef struct featurelist
{
	const sensors_chip_name    *chip;
	const sensors_feature      *feature;
	const sensors_subfeature   *subfeature;
	struct featurelist         *next;
} featurelist_t;

static char *conffile = NULL;
/* #endif (SENSORS_API_VERSION >= 0x400) && (SENSORS_API_VERSION < 0x500) */

#else /* if SENSORS_API_VERSION >= 0x500 */
# error "This version of libsensors is not supported yet. Please report this " \
	"as bug."
#endif

featurelist_t *first_feature = NULL;
static ignorelist_t *sensor_list;

#if SENSORS_API_VERSION < 0x400
/* full chip name logic borrowed from lm_sensors */
static int sensors_snprintf_chip_name (char *buf, size_t buf_size,
		const sensors_chip_name *chip)
{
	int status = -1;

	if (chip->bus == SENSORS_CHIP_NAME_BUS_ISA)
	{
		status = ssnprintf (buf, buf_size,
				"%s-isa-%04x",
				chip->prefix,
				chip->addr);
	}
	else if (chip->bus == SENSORS_CHIP_NAME_BUS_DUMMY)
	{
		status = snprintf (buf, buf_size, "%s-%s-%04x",
				chip->prefix,
				chip->busname,
				chip->addr);
	}
	else
	{
		status = snprintf (buf, buf_size, "%s-i2c-%d-%02x",
				chip->prefix,
				chip->bus,
				chip->addr);
	}

	return (status);
} /* int sensors_snprintf_chip_name */

static int sensors_feature_name_to_type (const char *name)
{
	int i;

	/* Yes, this is slow, but it's only ever done during initialization, so
	 * it's a one time cost.. */
	for (i = 0; i < known_features_num; i++)
		if (strcasecmp (known_features[i].label, name) == 0)
			return (known_features[i].type);

	return (SENSOR_TYPE_UNKNOWN);
} /* int sensors_feature_name_to_type */
#endif

static int sensors_config (const char *key, const char *value)
{
	if (sensor_list == NULL)
		sensor_list = ignorelist_create (1);

	/* TODO: This setting exists for compatibility with old versions of
	 * lm-sensors. Remove support for those ancient versions in the next
	 * major release. */
	if (strcasecmp (key, "SensorConfigFile") == 0)
	{
		char *tmp = strdup (value);
		if (tmp != NULL)
		{
			sfree (conffile);
			conffile = tmp;
		}
	}
	else if (strcasecmp (key, "Sensor") == 0)
	{
		if (ignorelist_add (sensor_list, value))
		{
			ERROR ("sensors plugin: "
					"Cannot add value to ignorelist.");
			return (1);
		}
	}
	else if (strcasecmp (key, "IgnoreSelected") == 0)
	{
		ignorelist_set_invert (sensor_list, 1);
		if (IS_TRUE (value))
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

static int sensors_load_conf (void)
{
	static int call_once = 0;

	FILE *fh = NULL;
	featurelist_t *last_feature = NULL;
	
	const sensors_chip_name *chip;
	int chip_num;

	int status;

	if (call_once)
		return 0;

	call_once = 1;

	if (conffile != NULL)
	{
		fh = fopen (conffile, "r");
		if (fh == NULL)
		{
			char errbuf[1024];
			ERROR ("sensors plugin: fopen(%s) failed: %s", conffile,
					sstrerror (errno, errbuf, sizeof (errbuf)));
			return (-1);
		}
	}

	status = sensors_init (fh);
	if (fh)
		fclose (fh);

	if (status != 0)
	{
		ERROR ("sensors plugin: Cannot initialize sensors. "
				"Data will not be collected.");
		return (-1);
	}

#if SENSORS_API_VERSION < 0x400
	chip_num = 0;
	while ((chip = sensors_get_detected_chips (&chip_num)) != NULL)
	{
		int feature_num0 = 0;
		int feature_num1 = 0;

		while (42)
		{
			const sensors_feature_data *feature;
			int feature_type;
			featurelist_t *fl;

			feature = sensors_get_all_features (*chip,
					&feature_num0, &feature_num1);

			/* Check if all features have been read. */
			if (feature == NULL)
				break;

			/* "master features" only */
			if (feature->mapping != SENSORS_NO_MAPPING)
			{
				DEBUG ("sensors plugin: sensors_load_conf: "
						"Ignoring subfeature `%s', "
						"because (feature->mapping "
						"!= SENSORS_NO_MAPPING).",
						feature->name);
				continue;
			}

			/* skip ignored in sensors.conf */
			if (sensors_get_ignored (*chip, feature->number) == 0)
			{
				DEBUG ("sensors plugin: sensors_load_conf: "
						"Ignoring subfeature `%s', "
						"because "
						"`sensors_get_ignored' told "
						"me so.",
						feature->name);
				continue;
			}

			feature_type = sensors_feature_name_to_type (
					feature->name);
			if (feature_type == SENSOR_TYPE_UNKNOWN)
			{
				DEBUG ("sensors plugin: sensors_load_conf: "
						"Ignoring subfeature `%s', "
						"because its type is "
						"unknown.",
						feature->name);
				continue;
			}

			fl = (featurelist_t *) malloc (sizeof (featurelist_t));
			if (fl == NULL)
			{
				ERROR ("sensors plugin: malloc failed.");
				continue;
			}
			memset (fl, '\0', sizeof (featurelist_t));

			fl->chip = chip;
			fl->data = feature;
			fl->type = feature_type;

			if (first_feature == NULL)
				first_feature = fl;
			else
				last_feature->next = fl;
			last_feature = fl;
		} /* while sensors_get_all_features */
	} /* while sensors_get_detected_chips */
/* #endif SENSORS_API_VERSION < 0x400 */

#elif (SENSORS_API_VERSION >= 0x400) && (SENSORS_API_VERSION < 0x500)
	chip_num = 0;
	while ((chip = sensors_get_detected_chips (NULL, &chip_num)) != NULL)
	{
		const sensors_feature *feature;
		int feature_num = 0;

		while ((feature = sensors_get_features (chip, &feature_num)) != NULL)
		{
			const sensors_subfeature *subfeature;
			int subfeature_num = 0;

			/* Only handle voltage, fanspeeds and temperatures */
			if ((feature->type != SENSORS_FEATURE_IN)
					&& (feature->type != SENSORS_FEATURE_FAN)
					&& (feature->type != SENSORS_FEATURE_TEMP))
			{
				DEBUG ("sensors plugin: sensors_load_conf: "
						"Ignoring feature `%s', "
						"because its type is not "
						"supported.", feature->name);
				continue;
			}

			while ((subfeature = sensors_get_all_subfeatures (chip,
							feature, &subfeature_num)) != NULL)
			{
				featurelist_t *fl;

				if ((subfeature->type != SENSORS_SUBFEATURE_IN_INPUT)
						&& (subfeature->type != SENSORS_SUBFEATURE_FAN_INPUT)
						&& (subfeature->type != SENSORS_SUBFEATURE_TEMP_INPUT))
					continue;

				fl = (featurelist_t *) malloc (sizeof (featurelist_t));
				if (fl == NULL)
				{
					ERROR ("sensors plugin: malloc failed.");
					continue;
				}
				memset (fl, '\0', sizeof (featurelist_t));

				fl->chip = chip;
				fl->feature = feature;
				fl->subfeature = subfeature;

				if (first_feature == NULL)
					first_feature = fl;
				else
					last_feature->next = fl;
				last_feature  = fl;
			} /* while (subfeature) */
		} /* while (feature) */
	} /* while (chip) */
#endif /* (SENSORS_API_VERSION >= 0x400) && (SENSORS_API_VERSION < 0x500) */

	if (first_feature == NULL)
	{
		sensors_cleanup ();
		INFO ("sensors plugin: lm_sensors reports no "
				"features. Data will not be collected.");
		return (-1);
	}

	return (0);
} /* int sensors_load_conf */

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
	char match_key[1024];
	int status;

	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	status = ssnprintf (match_key, sizeof (match_key), "%s/%s-%s",
			plugin_instance, type, type_instance);
	if (status < 1)
		return;

	if (sensor_list != NULL)
	{
		DEBUG ("sensors plugin: Checking ignorelist for `%s'", match_key);
		if (ignorelist_match (sensor_list, match_key))
			return;
	}

	values[0].gauge = val;

	vl.values = values;
	vl.values_len = 1;

	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "sensors", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, plugin_instance,
			sizeof (vl.plugin_instance));
	sstrncpy (vl.type, type, sizeof (vl.type));
	sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

	plugin_dispatch_values (&vl);
} /* void sensors_submit */

static int sensors_read (void)
{
	featurelist_t *fl;

	if (sensors_load_conf () != 0)
		return (-1);

#if SENSORS_API_VERSION < 0x400
	for (fl = first_feature; fl != NULL; fl = fl->next)
	{
		double value;
		int status;
		char plugin_instance[DATA_MAX_NAME_LEN];
		char type_instance[DATA_MAX_NAME_LEN];

		status = sensors_get_feature (*fl->chip,
				fl->data->number, &value);
		if (status < 0)
			continue;

		status = sensors_snprintf_chip_name (plugin_instance,
				sizeof (plugin_instance), fl->chip);
		if (status < 0)
			continue;

		sstrncpy (type_instance, fl->data->name,
				sizeof (type_instance));

		sensors_submit (plugin_instance,
				sensor_type_name_map[fl->type],
				type_instance,
				value);
	} /* for fl = first_feature .. NULL */
/* #endif SENSORS_API_VERSION < 0x400 */

#elif (SENSORS_API_VERSION >= 0x400) && (SENSORS_API_VERSION < 0x500)
	for (fl = first_feature; fl != NULL; fl = fl->next)
	{
		double value;
		int status;
		char plugin_instance[DATA_MAX_NAME_LEN];
		char type_instance[DATA_MAX_NAME_LEN];
		const char *type;

		status = sensors_get_value (fl->chip,
				fl->subfeature->number, &value);
		if (status < 0)
			continue;

		status = sensors_snprintf_chip_name (plugin_instance,
				sizeof (plugin_instance), fl->chip);
		if (status < 0)
			continue;

		sstrncpy (type_instance, fl->feature->name,
				sizeof (type_instance));

		if (fl->feature->type == SENSORS_FEATURE_IN)
			type = "voltage";
		else if (fl->feature->type
				== SENSORS_FEATURE_FAN)
			type = "fanspeed";
		else if (fl->feature->type
				== SENSORS_FEATURE_TEMP)
			type = "temperature";
		else
			continue;

		sensors_submit (plugin_instance, type, type_instance, value);
	} /* for fl = first_feature .. NULL */
#endif /* (SENSORS_API_VERSION >= 0x400) && (SENSORS_API_VERSION < 0x500) */

	return (0);
} /* int sensors_read */

void module_register (void)
{
	plugin_register_config ("sensors", sensors_config,
			config_keys, config_keys_num);
	plugin_register_read ("sensors", sensors_read);
	plugin_register_shutdown ("sensors", sensors_shutdown);
} /* void module_register */
