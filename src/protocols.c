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
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#if !KERNEL_LINUX
#error "No applicable input method."
#endif

#define SNMP_FILE "/proc/net/snmp"
#define SNMP6_FILE "/proc/net/snmp6"
#define NETSTAT_FILE "/proc/net/netstat"

#define LITERAL_STRLEN(literal) (sizeof(literal) - 1)
#define IP6 "Ip6"
#define ICMP6 "Icmp6"
#define UDP6 "Udp6"
#define UDPLITE6 "UdpLite6"

/*
 * Global variables
 */
static const char *config_keys[] = {
    "Value", "IgnoreSelected",
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static ignorelist_t *values_list = NULL;

/*
 * Functions
 */
static void submit(const char *protocol_name, const char *str_key,
                   const char *str_value) {
  value_t value;
  value_list_t vl = VALUE_LIST_INIT;
  int status;

  status = parse_value(str_value, &value, DS_TYPE_DERIVE);
  if (status != 0) {
    ERROR("protocols plugin: Parsing string as integer failed: %s", str_value);
    return;
  }

  vl.values = &value;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "protocols", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, protocol_name, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "protocol_counter", sizeof(vl.type));
  sstrncpy(vl.type_instance, str_key, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* void submit */

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
    ERROR("protocols plugin: fopen (%s) failed: %s.", path,
          sstrerror(errno, key_buffer, sizeof(key_buffer)));
    return (-1);
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

      submit(key_buffer, key_fields[i], value_fields[i]);
    } /* for (i = 0; i < key_fields_num; i++) */
  }   /* while (42) */

  fclose(fh);

  return (status);
} /* int read_file */

static int read_snmp6_file(const char *path){
  FILE *fh;
  char kv_buffer[4096];
  char *kv_ptr;
  char *kv_fields[2];
  int kv_fields_num;
  char *key_ptr;
  char *value_ptr;
  char protocol_buffer[32];
  int status;

  fh = fopen (path, "r");
  if (fh == NULL) {
    ERROR ("protocols plugin: fopen (%s) failed: %s.",
        path, sstrerror (errno, kv_buffer, sizeof (kv_buffer)));
    return (-1);
  }

  status = -1;
  while (42) {
    clearerr (fh);
    kv_ptr = fgets (kv_buffer, sizeof (kv_buffer), fh);
    if (kv_ptr == NULL) {
      if (feof (fh) != 0) {
        status = 0;
        break;
      }
      else if (ferror (fh) != 0) {
        ERROR ("protocols plugin: Reading from %s failed.", path);
        break;
      }
      else {
        ERROR ("protocols plugin: fgets failed for an unknown reason.");
        break;
      }
    } /* if (kv_ptr == NULL) */

    /* split kv_buffer into array */
    kv_fields_num = strsplit(kv_ptr, kv_fields, STATIC_ARRAY_SIZE(kv_fields));
    if (kv_fields_num != 2) {
      ERROR("protocols plugin: Line in %s does not contain a key and value exclusively: %s",
            path, kv_ptr);
      break;
    }
    key_ptr = kv_fields[0];
    value_ptr = kv_fields[1];

    /* set protocol and offset the key string to exclude the protocol */
    if (strstartswith(IP6, key_ptr) && strnlen(key_ptr, 32) > LITERAL_STRLEN(IP6)) {
      sstrcpy(protocol_buffer, IP6);
      key_ptr = key_ptr + LITERAL_STRLEN(IP6);
    } else if (strstartswith(ICMP6, key_ptr) && strnlen(key_ptr, 32) > LITERAL_STRLEN(ICMP6)) {
      sstrcpy(protocol_buffer, ICMP6);
      key_ptr = key_ptr + LITERAL_STRLEN(ICMP6);
    } else if (strstartswith(UDP6, key_ptr) && strnlen(key_ptr, 32) > LITERAL_STRLEN(UDP6)) {
      sstrcpy(protocol_buffer, UDP6);
      key_ptr = key_ptr + LITERAL_STRLEN(UDP6);
    } else if (strstartswith(UDPLITE6, key_ptr) && strnlen(key_ptr, 32) > LITERAL_STRLEN(UDPLITE6)) {
      sstrcpy(protocol_buffer, UDPLITE6);
      key_ptr = key_ptr + LITERAL_STRLEN(UDPLITE6);
    } else { /* skip unknown protocols or known protocols with no key after it */
      continue;
    }

    if (values_list != NULL) {
      char match_name[2 * DATA_MAX_NAME_LEN];

      ssnprintf (match_name, sizeof (match_name), "%s:%s",
          protocol_buffer, key_ptr);

      if (ignorelist_match (values_list, match_name))
        continue;
    } /* if (values_list != NULL) */

    submit(protocol_buffer, key_ptr, value_ptr);

  } /* while (42) */

  fclose(fh);

  return (status);
} /* int read_snmp6_file */

static int protocols_read(void) {
  int status;
  int success = 0;

  status = read_file(SNMP_FILE);
  if (status == 0)
    success++;

  status = read_snmp6_file(SNMP6_FILE);
  if (status == 0)
    success++;

  status = read_file(NETSTAT_FILE);
  if (status == 0)
    success++;

  if (success == 0)
    return (-1);

  return (0);
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
    return (-1);
  }

  return (0);
} /* int protocols_config */

void module_register(void) {
  plugin_register_config("protocols", protocols_config, config_keys,
                         config_keys_num);
  plugin_register_read("protocols", protocols_read);
} /* void module_register */
