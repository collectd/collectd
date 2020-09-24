/**
 * collectd - src/protocols.c
 * Copyright (C) 2009,2010  Florian octo Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 *   Cosmin Ioiart <cioiart at gmail.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"

#if !(KERNEL_LINUX || KERNEL_SOLARIS)
#error "No applicable input method."
#endif

#if KERNEL_LINUX
#define SNMP_FILE "/proc/net/snmp"
#define NETSTAT_FILE "/proc/net/netstat"
#endif

/*
 * On Solaris, all the key/value pairs are read via kstat
 */
#if HAVE_KSTAT_H
#include <kstat.h>
#endif

/*
 * Global variables
 */
static const char *config_keys[] = {
    "Value",
    "IgnoreSelected",
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *values_list;

/*
 * Functions
 */
static void submit_value(const char *protocol_name, const char *str_key,
                         value_t *value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = value;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "protocols", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, protocol_name, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "protocol_counter", sizeof(vl.type));
  sstrncpy(vl.type_instance, str_key, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void submit_value */

static void submit_string(const char *protocol_name, const char *str_key,
                          const char *str_value) {
  value_t value;

  int status = parse_value(str_value, &value, DS_TYPE_DERIVE);
  if (status != 0) {
    return;
  }

  submit_value(protocol_name, str_key, &value);
} /* void submit_string */

#if KERNEL_LINUX

static int read_file(const char *path) {
  FILE *fh;
  char key_buffer[4096];
  char value_buffer[4096];
  char *key_ptr;
  char *value_ptr;
  char *key_fields[256];
  char *value_fields[256];
  int key_fields_num;
  int value_fields_num;
  int status;
  int i;

  fh = fopen(path, "r");
  if (fh == NULL) {
    ERROR("protocols plugin: fopen (%s) failed: %s.", path, STRERRNO);
    return -1;
  }

  status = -1;
  while (42) {
    clearerr(fh);
    key_ptr = fgets(key_buffer, sizeof(key_buffer), fh);
    if (key_ptr == NULL) {
      if (feof(fh) != 0) {
        status = 0;
        break;
      } else if (ferror(fh) != 0) {
        ERROR("protocols plugin: Reading from %s failed.", path);
        break;
      } else {
        ERROR("protocols plugin: fgets failed for an unknown reason.");
        break;
      }
    } /* if (key_ptr == NULL) */

    value_ptr = fgets(value_buffer, sizeof(value_buffer), fh);
    if (value_ptr == NULL) {
      ERROR("protocols plugin: read_file (%s): Could not read values line.",
            path);
      break;
    }

    key_ptr = strchr(key_buffer, ':');
    if (key_ptr == NULL) {
      ERROR("protocols plugin: Could not find protocol name in keys line.");
      break;
    }
    *key_ptr = 0;
    key_ptr++;

    value_ptr = strchr(value_buffer, ':');
    if (value_ptr == NULL) {
      ERROR("protocols plugin: Could not find protocol name "
            "in values line.");
      break;
    }
    *value_ptr = 0;
    value_ptr++;

    if (strcmp(key_buffer, value_buffer) != 0) {
      ERROR("protocols plugin: Protocol names in keys and values lines "
            "don't match: `%s' vs. `%s'.",
            key_buffer, value_buffer);
      break;
    }

    key_fields_num =
        strsplit(key_ptr, key_fields, STATIC_ARRAY_SIZE(key_fields));
    value_fields_num =
        strsplit(value_ptr, value_fields, STATIC_ARRAY_SIZE(value_fields));

    if (key_fields_num != value_fields_num) {
      ERROR("protocols plugin: Number of fields in keys and values lines "
            "don't match: %i vs %i.",
            key_fields_num, value_fields_num);
      break;
    }

    for (i = 0; i < key_fields_num; i++) {
      if (values_list != NULL) {
        char match_name[2 * DATA_MAX_NAME_LEN];

        ssnprintf(match_name, sizeof(match_name), "%s:%s", key_buffer,
                  key_fields[i]);

        if (ignorelist_match(values_list, match_name))
          continue;
      } /* if (values_list != NULL) */

      submit_string(key_buffer, key_fields[i], value_fields[i]);
    } /* for (i = 0; i < key_fields_num; i++) */
  }   /* while (42) */

  fclose(fh);

  return status;
} /* int read_file */
#endif

#if KERNEL_SOLARIS && HAVE_KSTAT_H
/*
 * Retrieves all available key/value pairs for IP, ICMP, UDP and TCP from kstat
 * modules ip, icmp, tcp and udp
 */
static int read_kstat(const char *mod_name) {
  extern kstat_ctl_t *kc;
  kstat_t *ksp_chain = NULL;

  if (mod_name == NULL)
    return -1;

  if (kc == NULL)
    return -1;

  for (ksp_chain = kc->kc_chain; ksp_chain != NULL;
       ksp_chain = ksp_chain->ks_next) {
    if (strcmp(ksp_chain->ks_module, mod_name) == 0 &&
        ksp_chain->ks_type == KSTAT_TYPE_NAMED) {
      kstat_named_t *kn = NULL;
      kstat_read(kc, ksp_chain, kn);
      kn = (kstat_named_t *)ksp_chain->ks_data;

      for (int i = 0; (kn != NULL) && (i < ksp_chain->ks_ndata); i++, kn++) {
        if (strlen(kn->name) == 0) {
          continue;
        }

        value_t value;

        switch (kn->data_type) {
        case KSTAT_DATA_INT32:
          value.derive = (uint64_t)kn->value.i32;
          break;
        case KSTAT_DATA_UINT32:
          value.derive = (uint64_t)kn->value.ui32;
          break;
        case KSTAT_DATA_INT64:
          value.derive = (uint64_t)kn->value.i64;
          break;
        case KSTAT_DATA_UINT64:
          value.derive = (uint64_t)kn->value.ui64;
          break;
        default:
          WARNING("protocol plugin: unable to read data from module '%s' "
                  "with name '%s' because type '%d' is unknown.",
                  mod_name, kn->name, kn->data_type);
          continue;
        }

        submit_value(mod_name, kn->name, &value);

      } /* end for */
    }   /* end if mod_name && KSTAT_TYPE_NAMED */
  }     /* end main for loop */
  return 0;
}
#endif

static int protocols_read(void) {
  int status;
  int success = 0;

#if KERNEL_LINUX
  status = read_file(SNMP_FILE);
  if (status == 0)
    success++;

  status = read_file(NETSTAT_FILE);
  if (status == 0)
    success++;

#elif KERNEL_SOLARIS
  status = read_kstat("ip");
  if (status == 0)
    success++;
  status = read_kstat("icmp");
  if (status == 0)
    success++;
  status = read_kstat("udp");
  if (status == 0)
    success++;
  status = read_kstat("tcp");
  if (status == 0)
    success++;
#endif
  if (success == 0)
    return -1;

  return 0;
} /* int protocols_read */

static int protocols_config(const char *key, const char *value) {
  if (values_list == NULL)
    values_list = ignorelist_create(/* invert = */ 1);

  if (strcasecmp(key, "Value") == 0) {
    ignorelist_add(values_list, value);
  } else if (strcasecmp(key, "IgnoreSelected") == 0) {
    int invert = 1;
    if (IS_TRUE(value))
      invert = 0;
    ignorelist_set_invert(values_list, invert);
  } else {
    return -1;
  }

  return 0;
} /* int protocols_config */

void module_register(void) {
  plugin_register_config("protocols", protocols_config, config_keys,
                         config_keys_num);
  plugin_register_read("protocols", protocols_read);
} /* void module_register */
