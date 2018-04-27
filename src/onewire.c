/**
 * collectd - src/onewire.c
 * Copyright (C) 2008  noris network AG
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
 *   Florian octo Forster <octo at noris.net>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#include <owcapi.h>
#include <regex.h>
#include <sys/time.h>
#include <sys/types.h>

#define OW_FAMILY_LENGTH 8
#define OW_FAMILY_MAX_FEATURES 2
struct ow_family_features_s {
  char family[OW_FAMILY_LENGTH];
  struct {
    char filename[DATA_MAX_NAME_LEN];
    char type[DATA_MAX_NAME_LEN];
    char type_instance[DATA_MAX_NAME_LEN];
  } features[OW_FAMILY_MAX_FEATURES];
  size_t features_num;
};
typedef struct ow_family_features_s ow_family_features_t;

/* internal timing info collected in debug version only */
#if COLLECT_DEBUG
static struct timeval tv_begin, tv_end, tv_diff;
#endif /* COLLECT_DEBUG */

/* regexp to extract address (without family) and file from the owfs path */
static const char *regexp_to_match =
    "[A-Fa-f0-9]{2}\\.([A-Fa-f0-9]{12})/([[:alnum:]]+)$";

/* see http://owfs.sourceforge.net/ow_table.html for a list of families */
static ow_family_features_t ow_family_features[] = {
    {/* DS18S20 Precision Thermometer and DS1920 ibutton */
     /* family = */ "10.",
     {{/* filename = */ "temperature",
       /* type = */ "temperature",
       /* type_instance = */ ""}},
     /* features_num = */ 1},
    {/* DS1822 Econo Thermometer */
     /* family = */ "22.",
     {{/* filename = */ "temperature",
       /* type = */ "temperature",
       /* type_instance = */ ""}},
     /* features_num = */ 1},
    {/* DS18B20 Programmable Resolution Thermometer */
     /* family = */ "28.",
     {{/* filename = */ "temperature",
       /* type = */ "temperature",
       /* type_instance = */ ""}},
     /* features_num = */ 1},
    {/* DS2436 Volts/Temp */
     /* family = */ "1B.",
     {{/* filename = */ "temperature",
       /* type = */ "temperature",
       /* type_instance = */ ""}},
     /* features_num = */ 1},
    {/* DS2438 Volts/Temp */
     /* family = */ "26.",
     {{/* filename = */ "temperature",
       /* type = */ "temperature",
       /* type_instance = */ ""}},
     /* features_num = */ 1}};
static int ow_family_features_num = STATIC_ARRAY_SIZE(ow_family_features);

static char *device_g = NULL;
static cdtime_t ow_interval = 0;
static _Bool direct_access = 0;

static const char *config_keys[] = {"Device", "IgnoreSelected", "Sensor",
                                    "Interval"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *sensor_list;

static _Bool regex_direct_initialized = 0;
static regex_t regex_direct;

/**
 * List of onewire owfs "files" to be directly read
 */
typedef struct direct_access_element_s {
  char *path;                           /**< The whole owfs path */
  char *address;                        /**< 1-wire address without family */
  char *file;                           /**< owfs file - e.g. temperature */
  struct direct_access_element_s *next; /**< Next in the list */
} direct_access_element_t;

static direct_access_element_t *direct_list = NULL;

/* ===================================================================================
 */

#if COLLECT_DEBUG
/* Return 1 if the difference is negative, otherwise 0.  */
static int timeval_subtract(struct timeval *result, struct timeval *t2,
                            struct timeval *t1) {
  long int diff = (t2->tv_usec + 1000000 * t2->tv_sec) -
                  (t1->tv_usec + 1000000 * t1->tv_sec);
  result->tv_sec = diff / 1000000;
  result->tv_usec = diff % 1000000;

  return diff < 0;
}
#endif /* COLLECT_DEBUG */

/* ===================================================================================
 */

static void direct_list_element_free(direct_access_element_t *el) {
  if (el != NULL) {
    DEBUG("onewire plugin: direct_list_element_free - deleting <%s>", el->path);
    sfree(el->path);
    sfree(el->address);
    sfree(el->file);
    free(el);
  }
}

static int direct_list_insert(const char *config) {
  regmatch_t pmatch[3];
  size_t nmatch = 3;
  direct_access_element_t *element;

  DEBUG("onewire plugin: direct_list_insert <%s>", config);

  element = malloc(sizeof(*element));
  if (element == NULL) {
    ERROR("onewire plugin: direct_list_insert - cannot allocate element");
    return 1;
  }
  element->path = NULL;
  element->address = NULL;
  element->file = NULL;

  element->path = strdup(config);
  if (element->path == NULL) {
    ERROR("onewire plugin: direct_list_insert - cannot allocate path");
    direct_list_element_free(element);
    return 1;
  }

  DEBUG("onewire plugin: direct_list_insert - about to match %s", config);

  if (!regex_direct_initialized) {
    if (regcomp(&regex_direct, regexp_to_match, REG_EXTENDED)) {
      ERROR("onewire plugin: Cannot compile regex");
      direct_list_element_free(element);
      return 1;
    }
    regex_direct_initialized = 1;
    DEBUG("onewire plugin: Compiled regex!!");
  }

  if (regexec(&regex_direct, config, nmatch, pmatch, 0)) {
    ERROR("onewire plugin: direct_list_insert - no regex  match");
    direct_list_element_free(element);
    return 1;
  }

  if (pmatch[1].rm_so < 0) {
    ERROR("onewire plugin: direct_list_insert - no address regex match");
    direct_list_element_free(element);
    return 1;
  }
  element->address =
      strndup(config + pmatch[1].rm_so, pmatch[1].rm_eo - pmatch[1].rm_so);
  if (element->address == NULL) {
    ERROR("onewire plugin: direct_list_insert - cannot allocate address");
    direct_list_element_free(element);
    return 1;
  }
  DEBUG("onewire plugin: direct_list_insert - found address <%s>",
        element->address);

  if (pmatch[2].rm_so < 0) {
    ERROR("onewire plugin: direct_list_insert - no file regex match");
    direct_list_element_free(element);
    return 1;
  }
  element->file =
      strndup(config + pmatch[2].rm_so, pmatch[2].rm_eo - pmatch[2].rm_so);
  if (element->file == NULL) {
    ERROR("onewire plugin: direct_list_insert - cannot allocate file");
    direct_list_element_free(element);
    return 1;
  }
  DEBUG("onewire plugin: direct_list_insert - found file <%s>", element->file);

  element->next = direct_list;
  direct_list = element;

  return 0;
}

static void direct_list_free(void) {
  direct_access_element_t *traverse = direct_list;
  direct_access_element_t *tmp = NULL;
  ;

  while (traverse != NULL) {
    tmp = traverse;
    traverse = traverse->next;
    direct_list_element_free(tmp);
    tmp = NULL;
  }
}

/* ===================================================================================
 */

static int cow_load_config(const char *key, const char *value) {
  if (sensor_list == NULL)
    sensor_list = ignorelist_create(1);

  if (strcasecmp(key, "Sensor") == 0) {
    if (direct_list_insert(value)) {
      DEBUG("onewire plugin: Cannot add %s to direct_list_insert.", value);

      if (ignorelist_add(sensor_list, value)) {
        ERROR("onewire plugin: Cannot add value to ignorelist.");
        return 1;
      }
    } else {
      DEBUG("onewire plugin: %s is a direct access", value);
      direct_access = 1;
    }
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    ignorelist_set_invert(sensor_list, 1);
    if (IS_TRUE(value))
      ignorelist_set_invert(sensor_list, 0);
  } else if (strcasecmp(key, "Device") == 0) {
    char *temp;
    temp = strdup(value);
    if (temp == NULL) {
      ERROR("onewire plugin: strdup failed.");
      return 1;
    }
    sfree(device_g);
    device_g = temp;
  } else if (strcasecmp("Interval", key) == 0) {
    double tmp;
    tmp = atof(value);
    if (tmp > 0.0)
      ow_interval = DOUBLE_TO_CDTIME_T(tmp);
    else
      ERROR("onewire plugin: Invalid `Interval' setting: %s", value);
  } else {
    return -1;
  }

  return 0;
}

static int cow_read_values(const char *path, const char *name,
                           const ow_family_features_t *family_info) {
  value_list_t vl = VALUE_LIST_INIT;
  int success = 0;

  if (sensor_list != NULL) {
    DEBUG("onewire plugin: Checking ignorelist for `%s'", name);
    if (ignorelist_match(sensor_list, name) != 0)
      return 0;
  }

  sstrncpy(vl.plugin, "onewire", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, name, sizeof(vl.plugin_instance));

  for (size_t i = 0; i < family_info->features_num; i++) {
    char *buffer;
    size_t buffer_size;
    int status;

    char file[4096];
    char *endptr;

    snprintf(file, sizeof(file), "%s/%s", path,
             family_info->features[i].filename);
    file[sizeof(file) - 1] = 0;

    buffer = NULL;
    buffer_size = 0;
    DEBUG("Start reading onewire device %s", file);
    status = OW_get(file, &buffer, &buffer_size);
    if (status < 0) {
      ERROR("onewire plugin: OW_get (%s/%s) failed. error = %s;", path,
            family_info->features[i].filename, STRERRNO);
      return -1;
    }
    DEBUG("Read onewire device %s as %s", file, buffer);

    endptr = NULL;
    gauge_t g = strtod(buffer, &endptr);
    if (endptr == NULL) {
      ERROR("onewire plugin: Buffer is not a number: %s", buffer);
      continue;
    }

    sstrncpy(vl.type, family_info->features[i].type, sizeof(vl.type));
    sstrncpy(vl.type_instance, family_info->features[i].type_instance,
             sizeof(vl.type_instance));

    vl.values = &(value_t){.gauge = g};
    vl.values_len = 1;

    plugin_dispatch_values(&vl);
    success++;

    free(buffer);
  } /* for (i = 0; i < features_num; i++) */

  return (success > 0) ? 0 : -1;
} /* int cow_read_values */

/* Forward declaration so the recursion below works */
static int cow_read_bus(const char *path);

/*
 * cow_read_ds2409
 *
 * Handles:
 * - DS2409 - MicroLAN Coupler
 */
static int cow_read_ds2409(const char *path) {
  char subpath[4096];
  int status;

  status = snprintf(subpath, sizeof(subpath), "%s/main", path);
  if ((status > 0) && (status < (int)sizeof(subpath)))
    cow_read_bus(subpath);

  status = snprintf(subpath, sizeof(subpath), "%s/aux", path);
  if ((status > 0) && (status < (int)sizeof(subpath)))
    cow_read_bus(subpath);

  return 0;
} /* int cow_read_ds2409 */

static int cow_read_bus(const char *path) {
  char *buffer;
  size_t buffer_size;
  int status;

  char *buffer_ptr;
  char *dummy;
  char *saveptr;
  char subpath[4096];

  status = OW_get(path, &buffer, &buffer_size);
  if (status < 0) {
    ERROR("onewire plugin: OW_get (%s) failed. error = %s;", path, STRERRNO);
    return -1;
  }
  DEBUG("onewire plugin: OW_get (%s) returned: %s", path, buffer);

  dummy = buffer;
  saveptr = NULL;
  while ((buffer_ptr = strtok_r(dummy, ",/", &saveptr)) != NULL) {
    int i;

    dummy = NULL;

    if (strcmp("/", path) == 0)
      status = snprintf(subpath, sizeof(subpath), "/%s", buffer_ptr);
    else
      status = snprintf(subpath, sizeof(subpath), "%s/%s", path, buffer_ptr);
    if ((status <= 0) || (status >= (int)sizeof(subpath)))
      continue;

    for (i = 0; i < ow_family_features_num; i++) {
      if (strncmp(ow_family_features[i].family, buffer_ptr,
                  strlen(ow_family_features[i].family)) != 0)
        continue;

      cow_read_values(subpath,
                      buffer_ptr + strlen(ow_family_features[i].family),
                      ow_family_features + i);
      break;
    }
    if (i < ow_family_features_num)
      continue;

    /* DS2409 */
    if (strncmp("1F.", buffer_ptr, strlen("1F.")) == 0) {
      cow_read_ds2409(subpath);
      continue;
    }
  } /* while (strtok_r) */

  free(buffer);
  return 0;
} /* int cow_read_bus */

/* ===================================================================================
 */

static int cow_simple_read(void) {
  value_list_t vl = VALUE_LIST_INIT;
  char *buffer;
  size_t buffer_size;
  int status;
  char *endptr;
  direct_access_element_t *traverse;

  /* traverse list and check entries */
  for (traverse = direct_list; traverse != NULL; traverse = traverse->next) {
    sstrncpy(vl.plugin, "onewire", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, traverse->address, sizeof(vl.plugin_instance));

    status = OW_get(traverse->path, &buffer, &buffer_size);
    if (status < 0) {
      ERROR("onewire plugin: OW_get (%s) failed. status = %s;", traverse->path,
            STRERRNO);
      return -1;
    }
    DEBUG("onewire plugin: Read onewire device %s as %s", traverse->path,
          buffer);

    endptr = NULL;
    gauge_t g = strtod(buffer, &endptr);
    if (endptr == NULL) {
      ERROR("onewire plugin: Buffer is not a number: %s", buffer);
      continue;
    }

    sstrncpy(vl.type, traverse->file, sizeof(vl.type));
    sstrncpy(vl.type_instance, "", sizeof(""));

    vl.values = &(value_t){.gauge = g};
    vl.values_len = 1;

    plugin_dispatch_values(&vl);
    free(buffer);
  } /* for (traverse) */

  return 0;
} /* int cow_simple_read */

/* ===================================================================================
 */

static int cow_read(user_data_t *ud __attribute__((unused))) {
  int result = 0;

#if COLLECT_DEBUG
  gettimeofday(&tv_begin, NULL);
#endif /* COLLECT_DEBUG */

  if (direct_access) {
    DEBUG("onewire plugin: Direct access read");
    result = cow_simple_read();
  } else {
    DEBUG("onewire plugin: Standard access read");
    result = cow_read_bus("/");
  }

#if COLLECT_DEBUG
  gettimeofday(&tv_end, NULL);
  timeval_subtract(&tv_diff, &tv_end, &tv_begin);
  DEBUG("onewire plugin: Onewire read took us %ld.%06ld s", tv_diff.tv_sec,
        tv_diff.tv_usec);
#endif /* COLLECT_DEBUG */

  return result;
} /* int cow_read */

static int cow_shutdown(void) {
  OW_finish();
  ignorelist_free(sensor_list);

  direct_list_free();

  if (regex_direct_initialized) {
    regfree(&regex_direct);
  }

  return 0;
} /* int cow_shutdown */

static int cow_init(void) {
  int status;

  if (device_g == NULL) {
    ERROR("onewire plugin: cow_init: No device configured.");
    return -1;
  }

  DEBUG("onewire plugin: about to init device <%s>.", device_g);
  status = (int)OW_init(device_g);
  if (status != 0) {
    ERROR("onewire plugin: OW_init(%s) failed: %s.", device_g, STRERRNO);
    return 1;
  }

  plugin_register_complex_read(/* group = */ NULL, "onewire", cow_read,
                               ow_interval, /* user data = */ NULL);
  plugin_register_shutdown("onewire", cow_shutdown);

  return 0;
} /* int cow_init */

void module_register(void) {
  plugin_register_init("onewire", cow_init);
  plugin_register_config("onewire", cow_load_config, config_keys,
                         config_keys_num);
}
