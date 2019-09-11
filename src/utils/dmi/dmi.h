/*
 * MIT License
 *
 * Copyright(c) 2019 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
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
 * Kamil Wiatrowski <kamilx.wiatrowski@intel.com>
 */

#ifndef UTILS_DMI_H
#define UTILS_DMI_H

/* Size of buffer for single dmi entry, just an estimation with big margin,
 * could be increased if the new generation of platform requires it to.
 */
#define DMI_MAX_LEN 256

#define DMI_OK 0
#define DMI_ERROR -1

/* Following values should match Types assigned in System Management BIOS
 * (SMBIOS) Reference Specification
 */
typedef enum dmi_type_e {
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
  MANAGEMENT_CONTROLLER_HOST_INTERFACE,
  DMI_TYPE_ALL /* special type to read all SMBIOS handles */
} dmi_type;

typedef enum entry_type_e {
  DMI_ENTRY_NONE,
  DMI_ENTRY_NAME,
  DMI_ENTRY_MAP,
  DMI_ENTRY_LIST_NAME,
  DMI_ENTRY_LIST_VALUE,
  DMI_ENTRY_END
} entry_type;

typedef struct dmi_reader_s {
  FILE *_fd;
  char _buff[DMI_MAX_LEN];
  int (*_read_next)(struct dmi_reader_s *reader);
  /* Type of current entry */
  entry_type current_type;
  /* Entry name, the pointer changes after every read. */
  char *name;
  /* Entry value, the pointer changes after every read. */
  char *value;
} dmi_reader_t;

/*
 * NAME
 *   dmi_reader_init
 *
 * DESCRIPTION
 *   Initialize reader structure and use popen to read SMBIOS using dmidecode.
 *
 * PARAMETERS
 *   `reader'     Pointer to dmi_reader structure, can't be NULL.
 *   `type'       Type of DMI entries to read.
 *
 * RETURN VALUE
 *    DMI_OK upon success or DMI_ERROR if an error occurred.
 *
 * NOTES
 *    The dmi_reader_clean needs to be called to close pipe if
 *    reader hasn't finished yet. In case of error cleanup is
 *    done by the function itself.
 */
int dmi_reader_init(dmi_reader_t *reader, const dmi_type type);

/*
 * NAME
 *   dmi_reader_clean
 *
 * DESCRIPTION
 *   Clean up reader structure and close pipe.
 *
 * PARAMETERS
 *   `reader'     Pointer to dmi_reader structure, can't be NULL.
 */
void dmi_reader_clean(dmi_reader_t *reader);

/*
 * NAME
 *   dmi_reader_next
 *
 * DESCRIPTION
 *   Read next entry and store values.
 *
 * PARAMETERS
 *   `reader'     Pointer to dmi_reader structure, can't be NULL.
 *
 * RETURN VALUE
 *    DMI_OK upon success or DMI_ERROR if an error occurred.
 *
 * NOTES
 *    In case of error the function cleans the reader structure.
 *    After all entries are read the function sets state to DMI_ENTRY_END
 *    and no further clean-up is required.
 */
int dmi_read_next(dmi_reader_t *reader);

#endif /* UTILS_DMI_H */
