/**
 * collectd - src/sensors.c
 * Copyright (C) 2005,2006  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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

#define MODULE_NAME "sensors"
#define MODULE_NAME_VOLTAGE MODULE_NAME"_voltage"

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

#define BUFSIZE 512

/* temperature and fan sensors */
static char *ds_def[] =
{
	"DS:value:GAUGE:"COLLECTD_HEARTBEAT":U:U",
	NULL
};
static int ds_num = 1;

/* voltage sensors */
static char *sensor_voltage_ds_def[] = 
{
	"DS:voltage:GAUGE:"COLLECTD_HEARTBEAT":U:U",
	NULL
};
static int sensor_voltage_ds_num = 1;

/* old naming */
static char *old_filename_format = "sensors-%s.rrd";
/* end old naming */

/* new naming <chip-bus-address/type-feature */
static char *extended_filename_format = "lm_sensors-%s.rrd";

#define SENSOR_TYPE_UNKNOWN 0
#define SENSOR_TYPE_VOLTAGE 1
#define SENSOR_TYPE_FANSPEED 2
#define SENSOR_TYPE_TEMPERATURE 3

#if SENSORS_HAVE_READ
static char *sensor_type_prefix[] =
{
	"unknown",
	"voltage",
	"fanspeed",
	"temperature",
	NULL
};
#endif

typedef struct sensors_labeltypes {
	char *label;
	int type;
} sensors_labeltypes;

/*
 * finite list of known labels extracted from lm_sensors
 */
#if SENSORS_HAVE_READ
static sensors_labeltypes known_features[] = 
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
	{ 0, -1 }
};
#endif
/* end new naming */

static char *config_keys[] =
{
	"Sensor",
	"IgnoreSelected",
	"ExtendedSensorNaming",
	NULL
};
static int config_keys_num = 3;

static ignorelist_t *sensor_list;

/* 
 * sensor_extended_naming:
 * 0 => default is to create chip-feature
 * 1 => use new naming scheme chip-bus-address/type-feature
 */
static int sensor_extended_naming = 0;

#if SENSORS_HAVE_READ
#ifndef SENSORS_CONF_PATH
# define SENSORS_CONF_PATH "/etc/sensors.conf"
#endif
static const char *conffile = SENSORS_CONF_PATH;
/* SENSORS_CONF_PATH */

/*
 * remember stat of the loaded config
 */
static struct stat sensors_conf_stat;
static int sensors_conf_loaded = 0;

typedef struct featurelist
{
	const sensors_chip_name    *chip;
	const sensors_feature_data *data;
	int                         type;
	struct featurelist         *next;
} featurelist_t;

featurelist_t *first_feature = NULL;
#endif /* if SENSORS_HAVE_READ */

static int sensors_config (char *key, char *value)
{
	if (sensor_list == NULL)
		sensor_list = ignorelist_create (1);

	if (strcasecmp (key, "Sensor") == 0)
	{
		if (ignorelist_add (sensor_list, value))
		{
			syslog (LOG_EMERG, MODULE_NAME": Cannot add value to ignorelist.");
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
	else if (strcasecmp (key, "ExtendedSensorNaming") == 0)
	{
		if ((strcasecmp (value, "True") == 0)
				|| (strcasecmp (value, "Yes") == 0)
				|| (strcasecmp (value, "On") == 0))
			sensor_extended_naming = 1;
		else
			sensor_extended_naming = 0;
	}
	else
	{
		return (-1);
	}

	return (0);
}

#if SENSORS_HAVE_READ
void sensors_free_features (void)
{
	featurelist_t *thisft;
	featurelist_t *nextft;

	if (sensors_conf_loaded)
	{
		sensors_cleanup ();
		sensors_conf_loaded = 0;
	}

	for (thisft = first_feature; thisft != NULL; thisft = nextft)
	{
		nextft = thisft->next;
		sfree (thisft);
	}
	first_feature = NULL;
}

static void sensors_load_conf (int firsttime)
{
	FILE *fh;
	featurelist_t *last_feature = NULL;
	featurelist_t *new_feature;
	
	const sensors_chip_name *chip;
	int chip_num;

	const sensors_feature_data *data;
	int data_num0, data_num1;
	
	sensors_free_features ();
	new_feature = first_feature;

#ifdef assert
	assert (new_feature == NULL);
	assert (last_feature == NULL);
#endif

	if ((fh = fopen (conffile, "r")) == NULL)
	{
		syslog (LOG_ERR, MODULE_NAME": cannot open %s: %s. "
				"Data will not be collected.", conffile, strerror(errno));
		return;
	}

	if (sensors_init (fh))
	{
		fclose (fh);
		syslog (LOG_ERR, MODULE_NAME": Cannot initialize sensors. "
				"Data will not be collected.");
		return;
	}

	/* remember file status to detect changes */
	if (fstat (fileno (fh), &sensors_conf_stat) == -1)
	{
		fclose (fh);
		syslog (LOG_ERR, MODULE_NAME": cannot fstat %s: %s "
				"Data will not be collected.", conffile, strerror(errno));
		return;
	}

	fclose (fh);

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
						sensor_type_prefix[known_features[i].type],
						data->name);

				if ((new_feature = (featurelist_t *) malloc (sizeof (featurelist_t))) == NULL)
				{
					DBG ("malloc: %s", strerror (errno));
					syslog (LOG_ERR, MODULE_NAME":  malloc: %s",
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

	if (!firsttime)
		syslog (LOG_INFO, MODULE_NAME": lm_sensors' configuration reloaded.");

	if (first_feature == NULL)
	{
		sensors_cleanup ();
		sensors_conf_loaded = 0;
		syslog (LOG_INFO, MODULE_NAME": lm_sensors reports no features. "
			"Data will not be collected.");
	}
	else
		sensors_conf_loaded = 1;
}
#endif /* if SENSORS_HAVE_READ */

static void collectd_sensors_init (void)
{
#if SENSORS_HAVE_READ
	sensors_load_conf (1);
#endif /* if SENSORS_HAVE_READ */
}

static void sensors_shutdown (void)
{
#if SENSORS_HAVE_READ
	sensors_free_features ();
#endif /* if SENSORS_HAVE_READ */

	ignorelist_free (sensor_list);
}

static void sensors_voltage_write (char *host, char *inst, char *val)
{
	char file[BUFSIZE];
	int status;

	/* skip ignored in our config */
	if (ignorelist_match (sensor_list, inst))
		return;

	/* extended sensor naming */
	if(sensor_extended_naming)
		status = snprintf (file, BUFSIZE, extended_filename_format, inst);
	else
		status = snprintf (file, BUFSIZE, old_filename_format, inst);

	if ((status < 1) || (status >= BUFSIZE))
		return;

	rrd_update_file (host, file, val, sensor_voltage_ds_def, sensor_voltage_ds_num);
}

static void sensors_write (char *host, char *inst, char *val)
{
	char file[BUFSIZE];
	int status;

	/* skip ignored in our config */
	if (ignorelist_match (sensor_list, inst))
		return;

	/* extended sensor naming */
	if (sensor_extended_naming)
		status = snprintf (file, BUFSIZE, extended_filename_format, inst);
	else
		status = snprintf (file, BUFSIZE, old_filename_format, inst);

	if ((status < 1) || (status >= BUFSIZE))
		return;

	rrd_update_file (host, file, val, ds_def, ds_num);
}

#if SENSORS_HAVE_READ
static void sensors_submit (const char *feat_name,
		const char *chip_prefix, double value, int type)
{
	char buf[BUFSIZE];
	char inst[BUFSIZE];

	if (snprintf (inst, BUFSIZE, "%s-%s", chip_prefix, feat_name)
			>= BUFSIZE)
		return;

	/* skip ignored in our config */
	if (ignorelist_match (sensor_list, inst))
		return;

	if (snprintf (buf, BUFSIZE, "%u:%.3f", (unsigned int) curtime,
				value) >= BUFSIZE)
		return;

	if (type == SENSOR_TYPE_VOLTAGE)
	{
		DBG ("%s: %s/%s, %s", MODULE_NAME_VOLTAGE,
				sensor_type_prefix[type], inst, buf);
		plugin_submit (MODULE_NAME_VOLTAGE, inst, buf);
	}
	else
	{
		DBG ("%s: %s/%s, %s", MODULE_NAME,
				sensor_type_prefix[type], inst, buf);
		plugin_submit (MODULE_NAME, inst, buf);
	}
}

static void sensors_read (void)
{
	featurelist_t *feature;
	double value;
	char chip_fullprefix[BUFSIZE];
	struct stat changedbuf;

	/* check sensors.conf for changes */
	if (sensors_conf_loaded)
	{
		if (stat (conffile, &changedbuf) == -1)
		{
			syslog (LOG_ERR, MODULE_NAME": cannot stat %s: %s",
					conffile, strerror(errno));
			/*
			 * sensors.conf does not exist,
			 * throw away previous conf
			 */
			sensors_load_conf (0);
		}
		else
		{
			if ((changedbuf.st_size != sensors_conf_stat.st_size) ||
					(changedbuf.st_mtime != sensors_conf_stat.st_mtime) ||
					(changedbuf.st_ino != sensors_conf_stat.st_ino))
			{
				sensors_load_conf (0);
			}
		}
	}

	for (feature = first_feature; feature != NULL; feature = feature->next)
	{
		if (sensors_get_feature (*feature->chip, feature->data->number, &value) < 0)
			continue;

		if (sensor_extended_naming)
		{
			/* full chip name logic borrowed from lm_sensors */
			if (feature->chip->bus == SENSORS_CHIP_NAME_BUS_ISA)
			{
				if (snprintf (chip_fullprefix, BUFSIZE, "%s-isa-%04x/%s",
							feature->chip->prefix,
							feature->chip->addr,
							sensor_type_prefix[feature->type])
						>= BUFSIZE)
					continue;
			}
			else if (feature->chip->bus == SENSORS_CHIP_NAME_BUS_DUMMY)
			{
				if (snprintf (chip_fullprefix, BUFSIZE, "%s-%s-%04x/%s",
							feature->chip->prefix,
							feature->chip->busname,
							feature->chip->addr,
							sensor_type_prefix[feature->type])
						>= BUFSIZE)
					continue;
			}
			else
			{
				if (snprintf (chip_fullprefix, BUFSIZE, "%s-i2c-%d-%02x/%s",
							feature->chip->prefix,
							feature->chip->bus,
							feature->chip->addr,
							sensor_type_prefix[feature->type])
						>= BUFSIZE)
					continue;
			}

			sensors_submit (feature->data->name,
					chip_fullprefix,
					value, feature->type);
		}
		else
		{
			sensors_submit (feature->data->name,
					feature->chip->prefix,
					value, feature->type);
		}
	}
}
#else
# define sensors_read NULL
#endif /* SENSORS_HAVE_READ */

void module_register (void)
{
	plugin_register (MODULE_NAME, collectd_sensors_init, sensors_read, sensors_write);
	plugin_register (MODULE_NAME_VOLTAGE, NULL, NULL, sensors_voltage_write);
	plugin_register_shutdown (MODULE_NAME, sensors_shutdown);
	cf_register (MODULE_NAME, sensors_config, config_keys, config_keys_num);
}

#undef BUFSIZE
#undef MODULE_NAME
