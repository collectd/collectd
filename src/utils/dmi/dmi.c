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
 *
 */

/*
 * The util helps to parse the output from dmidecode command.
 * It expects that the output follows the given format, the size of
 * tabulations does not matter:
 *
 * Handle 1(...)
 * SMBIOS type name
 *     item1: value
 *     item2: value
 *     list name:
 *         list elem1
 *         list elem2
 *         (...)
 *     item3: value
 *     (...)
 *
 * Handle 2(...)
 * SMBIOS type name
 * (and so on ...)
 *
 */

#include "collectd.h"

#include "utils/common/common.h"
#include "utils/dmi/dmi.h"

#define UTIL_NAME "dmi_reader"

#define DMIDECODE_CMD_FMT_LEN DMI_MAX_LEN

static int dmi_look_for_handle(dmi_reader_t *r);

static int dmi_read_entry(dmi_reader_t *r) {
  while (fgets(r->_buff, STATIC_ARRAY_SIZE(r->_buff), r->_fd) != NULL) {
    char *buff = r->_buff;
    while (isspace(*buff))
      buff++;
    if (*buff == '\0') {
      r->current_type = DMI_ENTRY_NONE;
      r->_read_next = dmi_look_for_handle;
      return DMI_OK;
    }

    strstripnewline(buff);
    char *sep = strchr(buff, ':');
    if (sep == NULL) {
      r->value = buff;
      r->current_type = DMI_ENTRY_LIST_VALUE;
      return DMI_OK;
    }

    sep[0] = '\0';
    r->name = buff;
    if (sep[1] == '\0') {
      r->current_type = DMI_ENTRY_LIST_NAME;
    } else {
      sep++;
      while (isspace(*sep))
        sep++;
      if (*sep == '\0')
        INFO(UTIL_NAME ": value is empty for: \'%s\'.", r->_buff);
      r->value = sep;
      r->current_type = DMI_ENTRY_MAP;
    }
    return DMI_OK;
  }

  r->current_type = DMI_ENTRY_END;
  return DMI_OK;
}

static int dmi_read_type_name(dmi_reader_t *r) {
  while (fgets(r->_buff, STATIC_ARRAY_SIZE(r->_buff), r->_fd) != NULL) {
    strstripnewline(r->_buff);
    if (strlen(r->_buff) == 0) {
      ERROR(UTIL_NAME ": unexpected format of dmidecode output.");
      return DMI_ERROR;
    }

    r->name = r->_buff;
    r->current_type = DMI_ENTRY_NAME;
    r->_read_next = dmi_read_entry;

    return DMI_OK;
  }

  r->current_type = DMI_ENTRY_END;
  return DMI_OK;
}

static int dmi_look_for_handle(dmi_reader_t *r) {
  while (fgets(r->_buff, STATIC_ARRAY_SIZE(r->_buff), r->_fd) != NULL) {
    const char *handle = "Handle";
    if (strncmp(handle, r->_buff, strlen(handle)) != 0)
      continue;

    r->_read_next = dmi_read_type_name;
    return DMI_OK;
  }

  r->current_type = DMI_ENTRY_END;
  return DMI_OK;
}

int dmi_reader_init(dmi_reader_t *reader, const dmi_type type) {
  if (reader == NULL) {
    ERROR(UTIL_NAME ".%s: NULL pointer.", __func__);
    return DMI_ERROR;
  }
  char cmd[DMIDECODE_CMD_FMT_LEN];

  if (type == DMI_TYPE_ALL)
    strncpy(cmd, "dmidecode 2>/dev/null", STATIC_ARRAY_SIZE(cmd));
  else
    snprintf(cmd, STATIC_ARRAY_SIZE(cmd), "dmidecode -t %d 2>/dev/null", type);

  DEBUG(UTIL_NAME ": dmidecode cmd=\'%s\'.", cmd);

  reader->_fd = popen(cmd, "r");
  if (reader->_fd == NULL) {
    ERROR(UTIL_NAME ": popen failed.");
    return DMI_ERROR;
  }

  reader->name = NULL;
  reader->value = NULL;
  reader->current_type = DMI_ENTRY_NONE;
  reader->_read_next = dmi_look_for_handle;

  return DMI_OK;
}

void dmi_reader_clean(dmi_reader_t *reader) {
  if (reader == NULL) {
    WARNING(UTIL_NAME ".%s: NULL pointer.", __func__);
    return;
  }

  if (reader->_fd) {
    pclose(reader->_fd);
    reader->_fd = NULL;
  }

  reader->_read_next = NULL;
}

int dmi_read_next(dmi_reader_t *reader) {
  if (reader == NULL || reader->_read_next == NULL || reader->_fd == NULL) {
    ERROR(UTIL_NAME ".%s: NULL pointer.", __func__);
    return DMI_ERROR;
  }
  int ret = reader->_read_next(reader);

  if (reader->current_type == DMI_ENTRY_END || ret == DMI_ERROR) {
    pclose(reader->_fd);
    reader->_fd = NULL;
    reader->_read_next = NULL;
    DEBUG(UTIL_NAME ": dmidecode reader finished, status=%d.", ret);
  }

  return ret;
}
