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
 *   Florian octo Forster <octo at collectd.org>
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

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#if defined(HAVE_SENSORS_SENSORS_H)
#include <sensors/sensors.h>
#endif

#if !defined(SENSORS_API_VERSION)
#define SENSORS_API_VERSION 0x000
#endif

static const char *config_keys[] = {"Sensor", "IgnoreSelected",
                                    "SensorConfigFile", "UseLabels"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

#if SENSORS_API_VERSION < 0x400
typedef struct featurelist {
  const sensors_chip_name *chip;
  const sensors_feature_data *data;
  int type;
  struct featurelist *next;
} featurelist_t;

#ifndef SENSORS_CONF_PATH
#define SENSORS_CONF_PATH "/etc/sensors.conf"
#endif
static char *conffile = SENSORS_CONF_PATH;
/* #endif SENSORS_API_VERSION < 0x400 */

#elif (SENSORS_API_VERSION >= 0x400)
typedef struct featurelist {
  const sensors_chip_name *chip;
  const sensors_feature *feature;
  const sensors_subfeature *subfeature;
  struct featurelist *next;
} featurelist_t;

static char *conffile;
static bool use_labels;
#endif

static featurelist_t *first_feature;
static ignorelist_t *sensor_list;

static int sensors_config(const char *key, const char *value) {
  if (sensor_list == NULL)
    sensor_list = ignorelist_create(1);

  /* TODO: This setting exists for compatibility with old versions of
   * lm-sensors. Remove support for those ancient versions in the next
   * major release. */
  if (strcasecmp(key, "SensorConfigFile") == 0) {
    char *tmp = strdup(value);
    if (tmp != NULL) {
      sfree(conffile);
      conffile = tmp;
    }
  } else if (strcasecmp(key, "Sensor") == 0) {
    if (ignorelist_add(sensor_list, value)) {
      ERROR("sensors plugin: "
            "Cannot add value to ignorelist.");
      return 1;
    }
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    ignorelist_set_invert(sensor_list, 1);
    if (IS_TRUE(value))
      ignorelist_set_invert(sensor_list, 0);
  }
#if (SENSORS_API_VERSION >= 0x400)
  else if (strcasecmp(key, "UseLabels") == 0) {
    use_labels = IS_TRUE(value);
  }
#endif
  else {
    return -1;
  }

  return 0;
}

static void sensors_free_features(void) {
  featurelist_t *nextft;

  if (first_feature == NULL)
    return;

  sensors_cleanup();

  for (featurelist_t *thisft = first_feature; thisft != NULL; thisft = nextft) {
    nextft = thisft->next;
    sfree(thisft);
  }
  first_feature = NULL;
}

static int sensors_load_conf(void) {
  static int call_once;

  FILE *fh = NULL;
  featurelist_t *last_feature = NULL;

  const sensors_chip_name *chip;
  int chip_num;

  int status;

  if (call_once)
    return 0;

  call_once = 1;

  if (conffile != NULL) {
    fh = fopen(conffile, "r");
    if (fh == NULL) {
      ERROR("sensors plugin: fopen(%s) failed: %s", conffile, STRERRNO);
      return -1;
    }
  }

  status = sensors_init(fh);
  if (fh)
    fclose(fh);

  if (status != 0) {
    ERROR("sensors plugin: Cannot initialize sensors. "
          "Data will not be collected.");
    return -1;
  }

#if SENSORS_API_VERSION < 0x400
  chip_num = 0;
  while ((chip = sensors_get_detected_chips(&chip_num)) != NULL) {
    int feature_num0 = 0;
    int feature_num1 = 0;

    while (42) {
      const sensors_feature_data *feature;
      int feature_type;
      featurelist_t *fl;

      feature = sensors_get_all_features(*chip, &feature_num0, &feature_num1);

      /* Check if all features have been read. */
      if (feature == NULL)
        break;

      /* "master features" only */
      if (feature->mapping != SENSORS_NO_MAPPING) {
        DEBUG("sensors plugin: sensors_load_conf: "
              "Ignoring subfeature `%s', "
              "because (feature->mapping "
              "!= SENSORS_NO_MAPPING).",
              feature->name);
        continue;
      }

      /* skip ignored in sensors.conf */
      if (sensors_get_ignored(*chip, feature->number) == 0) {
        DEBUG("sensors plugin: sensors_load_conf: "
              "Ignoring subfeature `%s', "
              "because "
              "`sensors_get_ignored' told "
              "me so.",
              feature->name);
        continue;
      }

      feature_type = sensors_feature_name_to_type(feature->name);
      if (feature_type == SENSOR_TYPE_UNKNOWN) {
        DEBUG("sensors plugin: sensors_load_conf: "
              "Ignoring subfeature `%s', "
              "because its type is "
              "unknown.",
              feature->name);
        continue;
      }

      fl = calloc(1, sizeof(*fl));
      if (fl == NULL) {
        ERROR("sensors plugin: calloc failed.");
        continue;
      }

      fl->chip = chip;
      fl->data = feature;
      fl->type = feature_type;

      if (first_feature == NULL)
        first_feature = fl;
      else
        last_feature->next = fl;
      last_feature = fl;
    } /* while sensors_get_all_features */
  }   /* while sensors_get_detected_chips */
/* #endif SENSORS_API_VERSION < 0x400 */

#elif (SENSORS_API_VERSION >= 0x400)
  chip_num = 0;
  while ((chip = sensors_get_detected_chips(NULL, &chip_num)) != NULL) {
    const sensors_feature *feature;
    int feature_num = 0;

    while ((feature = sensors_get_features(chip, &feature_num)) != NULL) {
      const sensors_subfeature *subfeature;
      int subfeature_num = 0;

      /* Only handle voltage, fanspeeds and temperatures */
      if ((feature->type != SENSORS_FEATURE_IN) &&
          (feature->type != SENSORS_FEATURE_FAN) &&
          (feature->type != SENSORS_FEATURE_TEMP) &&
#if SENSORS_API_VERSION >= 0x402
          (feature->type != SENSORS_FEATURE_CURR) &&
#endif
#if SENSORS_API_VERSION >= 0x431
          (feature->type != SENSORS_FEATURE_HUMIDITY) &&
#endif
          (feature->type != SENSORS_FEATURE_POWER)) {
        DEBUG("sensors plugin: sensors_load_conf: "
              "Ignoring feature `%s', "
              "because its type is not "
              "supported.",
              feature->name);
        continue;
      }

      while ((subfeature = sensors_get_all_subfeatures(
                  chip, feature, &subfeature_num)) != NULL) {
        featurelist_t *fl;

        if ((subfeature->type != SENSORS_SUBFEATURE_IN_INPUT) &&
            (subfeature->type != SENSORS_SUBFEATURE_FAN_INPUT) &&
            (subfeature->type != SENSORS_SUBFEATURE_TEMP_INPUT) &&
#if SENSORS_API_VERSION >= 0x402
            (subfeature->type != SENSORS_SUBFEATURE_CURR_INPUT) &&
#endif
#if SENSORS_API_VERSION >= 0x431
            (subfeature->type != SENSORS_SUBFEATURE_HUMIDITY_INPUT) &&
#endif
            (subfeature->type != SENSORS_SUBFEATURE_POWER_INPUT))
          continue;

        fl = calloc(1, sizeof(*fl));
        if (fl == NULL) {
          ERROR("sensors plugin: calloc failed.");
          continue;
        }

        fl->chip = chip;
        fl->feature = feature;
        fl->subfeature = subfeature;

        if (first_feature == NULL)
          first_feature = fl;
        else
          last_feature->next = fl;
        last_feature = fl;
      } /* while (subfeature) */
    }   /* while (feature) */
  }     /* while (chip) */
#endif /* (SENSORS_API_VERSION >= 0x400) */

  if (first_feature == NULL) {
    sensors_cleanup();
    INFO("sensors plugin: lm_sensors reports no "
         "features. Data will not be collected.");
    return -1;
  }

  return 0;
} /* int sensors_load_conf */

static int sensors_shutdown(void) {
  sensors_free_features();
  ignorelist_free(sensor_list);

  return 0;
} /* int sensors_shutdown */

static void sensors_submit(const char *plugin_instance, const char *type,
                           const char *type_instance, double value) {
  char match_key[1024];
  int status;

  value_list_t vl = VALUE_LIST_INIT;

  status = snprintf(match_key, sizeof(match_key), "%s/%s-%s", plugin_instance,
                    type, type_instance);
  if (status < 1)
    return;

  if (sensor_list != NULL) {
    DEBUG("sensors plugin: Checking ignorelist for `%s'", match_key);
    if (ignorelist_match(sensor_list, match_key))
      return;
  }

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, "sensors", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void sensors_submit */

static int sensors_read(void) {
  if (sensors_load_conf() != 0)
    return -1;

#if SENSORS_API_VERSION < 0x400
  for (featurelist_t *fl = first_feature; fl != NULL; fl = fl->next) {
    double value;
    int status;
    char plugin_instance[DATA_MAX_NAME_LEN];
    char type_instance[DATA_MAX_NAME_LEN];

    status = sensors_get_feature(*fl->chip, fl->data->number, &value);
    if (status < 0)
      continue;

    status = sensors_snprintf_chip_name(plugin_instance,
                                        sizeof(plugin_instance), fl->chip);
    if (status < 0)
      continue;

    sstrncpy(type_instance, fl->data->name, sizeof(type_instance));

    sensors_submit(plugin_instance, sensor_type_name_map[fl->type],
                   type_instance, value);
  } /* for fl = first_feature .. NULL */
/* #endif SENSORS_API_VERSION < 0x400 */

#elif (SENSORS_API_VERSION >= 0x400)
  for (featurelist_t *fl = first_feature; fl != NULL; fl = fl->next) {
    double value;
    int status;
    char plugin_instance[DATA_MAX_NAME_LEN];
    char type_instance[DATA_MAX_NAME_LEN];
    char *sensor_label;
    const char *type;

    status = sensors_get_value(fl->chip, fl->subfeature->number, &value);
    if (status < 0)
      continue;

    status = sensors_snprintf_chip_name(plugin_instance,
                                        sizeof(plugin_instance), fl->chip);
    if (status < 0)
      continue;

    if (use_labels) {
      sensor_label = sensors_get_label(fl->chip, fl->feature);
      sstrncpy(type_instance, sensor_label, sizeof(type_instance));
      free(sensor_label);
    } else {
      sstrncpy(type_instance, fl->feature->name, sizeof(type_instance));
    }

    if (fl->feature->type == SENSORS_FEATURE_IN)
      type = "voltage";
    else if (fl->feature->type == SENSORS_FEATURE_FAN)
      type = "fanspeed";
    else if (fl->feature->type == SENSORS_FEATURE_TEMP)
      type = "temperature";
    else if (fl->feature->type == SENSORS_FEATURE_POWER)
      type = "power";
#if SENSORS_API_VERSION >= 0x402
    else if (fl->feature->type == SENSORS_FEATURE_CURR)
      type = "current";
#endif
#if SENSORS_API_VERSION >= 0x431
    else if (fl->feature->type == SENSORS_FEATURE_HUMIDITY)
      type = "humidity";
#endif
    else
      continue;

    sensors_submit(plugin_instance, type, type_instance, value);
  } /* for fl = first_feature .. NULL */
#endif /* (SENSORS_API_VERSION >= 0x400) */

  return 0;
} /* int sensors_read */

void module_register(void) {
  plugin_register_config("sensors", sensors_config, config_keys,
                         config_keys_num);
  plugin_register_read("sensors", sensors_read);
  plugin_register_shutdown("sensors", sensors_shutdown);
} /* void module_register */
