/*
 * MIT License
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 * Przemyslaw Szczerbik <przemyslawx.szczerbik@intel.com>
 * Roman Ulan <romanx.ulan@intel.com>
 */

#ifndef UTILS_DMI_H
#define UTILS_DMI_H

#define DMI_MAX_VAL_LEN 128
#define DMI_MAX_NAME_LEN 64

/* Following values should match Types assigned in System Management BIOS
 * (SMBIOS) Reference Specification
 */
typedef enum dmi_type {
  BIOS = 0,
  SYSTEM,
  BASEBOARD,
  CHASSIS,
  PROCESSOR,
  MEMORY_CONTROLLER,
  MEMORY_MODULE,
  CACHE,
  PORT_CONNECTOR,
  SYSTEM_SLOTS,
  ON_BOARD_DEVICES,
  OEM_STRINGS,
  SYSTEM_CONFIGURATION_OPTIONS,
  BIOS_LANGUAGE,
  GROUP_ASSOCIATIONS,
  SYSTEM_EVENT_LOG,
  PHYSICAL_MEMORY_ARRAY,
  MEMORY_DEVICE,
  MEMORY_ERROR_32_BIT,
  MEMORY_ARRAY_MAPPED_ADDRESS,
  MEMORY_DEVICE_MAPPED_ADDRESS,
  BUILT_IN_POINTING_DEVICE,
  PORTABLE_BATTERY,
  SYSTEM_RESET,
  HARDWARE_SECURITY,
  SYSTEM_POWER_CONTROLS,
  VOLTAGE_PROBE,
  COOLING_DEVICE,
  TEMPERATURE_PROBE,
  ELECTRICAL_CURRENT_PROBE,
  OUT_OF_BAND_REMOTE_ACCESS,
  BOOT_INTEGRITY_SERVICES,
  SYSTEM_BOOT,
  MEMORY_ERROR_64_BIT,
  MANAGEMENT_DEVICE,
  MANAGEMENT_DEVICE_COMPONENT,
  MANAGEMENT_DEVICE_THRESHOLD_DATA,
  MEMORY_CHANNEL,
  IPMI_DEVICE,
  POWER_SUPPLY,
  ADDITIONAL_INFORMATION,
  ONBOARD_DEVICES_EXTENDED_INFORMATION,
  MANAGEMENT_CONTROLLER_HOST_INTERFACE
} dmi_type;

typedef struct dmi_setting {
  /* Setting to be read. Can't be longer than DMI_MAX_NAME_LEN */
  const char *name;
  /* Setting value. At most DMI_MAX_VAL_LEN characters are read */
  char *value;
} dmi_setting;

typedef struct dmi_s {
  /* SMBIOS structure type for which settings will be read */
  dmi_type type;
  /* Pointer to dmi_setting structure array */
  dmi_setting *settings;
  /* dmi_setting structure array size */
  size_t s_len;
} dmi_t;

typedef struct dmi_reader {
  /* Private context. */
  void *ctx;
  /*
  * NAME
  * get
  *
  * DESCRIPTION
  * Method for retrieving multiple settings from a single SMBIOS structure.
  *
  * PARAMETERS
  * `self' Pointer to dmi_reader structure. Can't be a NULL pointer.
  * `s'    Pointer to dmi_t structure. Can't be a NULL pointer.
  *
  * RETURN VALUE
  * Number of successfully retrieved DMI settings.
  */
  size_t (*get)(struct dmi_reader *self, dmi_t *s);
  /*
  * NAME
  * get_bulk
  *
  * DESCRIPTION
  * Method for retrieving multiple settings from multiple SMBIOS structures.
  *
  * PARAMETERS
  * `self'    Pointer to dmi_reader structure. Can't be a NULL pointer.
  * `s'       Pointer to dmi_t structure array. Can't be a NULL pointer.
  * `s_len'   dmi_t structure array size.
  *
  * RETURN VALUE
  * Number of successfully retrieved DMI settings.
  */
  size_t (*get_bulk)(struct dmi_reader *self, dmi_t **s, size_t s_len);
  /*
  * NAME
  * clean
  *
  * DESCRIPTION
  * dmi_reader destructor. Should be called before object goes out of scope or
  * memory is freed.
  *
  * PARAMETERS
  * `self'    Pointer to dmi_reader structure. Can't be a NULL pointer.
  */
  void (*clean)(struct dmi_reader *self);
  /*
  * NAME
  * free
  *
  * DESCRIPTION
  * Calls dmi_reader destructor and deallocates memory. Do *NOT* use for
  * automatic variables. Should be used only if dmi_reader was allocated with
  * dmidecode_alloc function.
  *
  * PARAMETERS
  * `self'    Pointer to dmi_reader structure. Can't be a NULL pointer.
  */
  void (*free)(struct dmi_reader *self);
} dmi_reader;

/*
 * NAME
 * dmidecode_init
 *
 * DESCRIPTION
 * dmidecode constructor, which initializes dmi_reader structure pointers
 *
 * PARAMETERS
 * `self' Pointer to dmi_reader structure. Can't be a NULL pointer.
 */
void dmidecode_init(dmi_reader *self);
/*
 * NAME
 * dmidecode_alloc
 *
 * DESCRIPTION
 * Allocates memory for dmi_reader structure and calls dmidecode constructor.
 *
 * RETURN VALUE
 * Pointer to dmi_reader structure or NULL pointer if an error occurred.
 */
dmi_reader *dmidecode_alloc(void);

#endif /* UTILS_DMI_H */
