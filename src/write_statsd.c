/**
 * collectd - src/write_statsd.c
 * Copyright (c) 2014 Stefano Pogliani
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
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * Authors:
 *   Stefano Pogliani <stefano at spogliani.net>
 **/

#define _BSD_SOURCE

#include "collectd.h"

#include <netdb.h>

#include "common.h"
#include "utils_cache.h"
#include "utils_subst.h"


/*** Compile-time constants. ***/
#define ESCAPE_KEY_CHAR   '_'
#define INVALID_KEY_CHARS ".:| "
#define MAX_KEY_LENGTH    1024
#define VALUE_STR_LEN     20
#define WRITE_STATSD_NAME "write_statsd"


/*** Type declarations. ***/
struct write_statsd_config_s {
  char* host;
  char* port;
  char* postfix;
  char* prefix;
  _Bool always_append_ds;
  _Bool silence_type_warnings;
  _Bool store_rates;
};
typedef struct write_statsd_config_s write_statsd_config_t;


/*** Module variables. ***/
static char* DS_TYPE_TO_STATSD[] = {
  "c",  /* DS_TYPE_COUNTER  */
  "g",  /* DS_TYPE_GAUGE    */
  "g",  /* DS_TYPE_DERIVE   */
  "c"   /* DS_TYPE_ABSOLUTE */
};

/*** Module functions. ***/
static void write_statsd_free_config(void* configuration);
static int write_statsd_write(
    const data_set_t *ds, const value_list_t *vl, user_data_t *ud);


static char* ds_value_to_string(int type, value_t value) {
  char* result = malloc(VALUE_STR_LEN);
  if (result == NULL) {
    ERROR("%s: not enough memory for value buffer", WRITE_STATSD_NAME);
    return NULL;
  }

  switch(type) {
    case DS_TYPE_COUNTER:
      ssnprintf(result, VALUE_STR_LEN, "%lld", value.counter);
      break;

    case DS_TYPE_GAUGE:
      ssnprintf(result, VALUE_STR_LEN, GAUGE_FORMAT, value.gauge);
      break;

    case DS_TYPE_DERIVE:
      ssnprintf(result, VALUE_STR_LEN, "%"PRIi64, value.derive);
      break;

    case DS_TYPE_ABSOLUTE:
      ssnprintf(result, VALUE_STR_LEN, "%"PRIu64, value.absolute);
      break;

    default:
      ERROR("%s: unknown data source type: %i", WRITE_STATSD_NAME, type);
      free(result);
      return NULL;
  }

  return result;
}


static char* ds_rate_to_string(int type, double rate) {
  char* result = malloc(VALUE_STR_LEN);
  if (result == NULL) {
    ERROR("%s: not enough memory for rates buffer", WRITE_STATSD_NAME);
    return NULL;
  }

  ssnprintf(result, VALUE_STR_LEN, GAUGE_FORMAT, rate);
  return result;
}


static int open_socket(write_statsd_config_t* config) {
  int error = 0;
  int sock  = 0;

  struct addrinfo* ai;
  struct addrinfo  ai_hints;
  struct addrinfo* ai_list;

  // Resolve address.
  memset((void*)&ai_hints, '\0', sizeof(ai_hints));
  ai_hints.ai_family   = AF_UNSPEC;
  ai_hints.ai_socktype = SOCK_DGRAM;
  error = getaddrinfo(config->host, config->port, &ai_hints, &ai_list);

  if (error != 0) {
    ERROR("%s: unable to resolve address", WRITE_STATSD_NAME);
    return 0;
  }

  // Find a usable address.
  for (ai = ai_list; ai != NULL; ai = ai->ai_next) {
    sock = socket(ai->ai_family, SOCK_DGRAM, 0);
    if (sock < 0) {
      char errbuf[1024];
      WARNING("%s: socket(2) failed: %s", WRITE_STATSD_NAME,
              sstrerror(errno, errbuf, sizeof(errbuf)));
      sock = 0;
      continue;
    }

    error = (int)connect(sock, ai->ai_addr, ai->ai_addrlen);
    if (error == -1) {
      char errbuf[1024];
      WARNING ("%s: connect(2) failed: %s", WRITE_STATSD_NAME,
               sstrerror(errno, errbuf, sizeof(errbuf)));
      close(sock);
      sock = 0;
      continue;
    }

    break;
  }

  // Return the link to the caller.
  freeaddrinfo(ai_list);
  return sock;
}


static char* write_statsd_escape(const char* string) {
  return subst_escape(string, INVALID_KEY_CHARS, ESCAPE_KEY_CHAR);
}


static int write_statsd_config(oconfig_item_t *conf) {
  int idx;
  int status;

  // Initialise configuration holder.
  write_statsd_config_t* configuration = malloc(sizeof(write_statsd_config_t));
  if (configuration == NULL) {
    ERROR("%s: not enough memory for configuration", WRITE_STATSD_NAME);
    return -1;
  }
  configuration->host = NULL;
  configuration->port = strdup("8125");
  configuration->postfix = NULL;
  configuration->prefix  = NULL;
  configuration->always_append_ds = 0;
  configuration->silence_type_warnings = 0;
  configuration->store_rates = 1;

  // Process configuration.
  for (idx = 0; idx < conf->children_num; idx++) {
    oconfig_item_t *child = conf->children + idx;
    status = 0;

    if (strcasecmp("Host", child->key) == 0) {
      status = cf_util_get_string(child, &configuration->host);

    } else if (strcasecmp("Port", child->key) == 0) {
      sfree(configuration->port);
      status = cf_util_get_service(child, &configuration->port);

    } else if (strcasecmp("Postfix", child->key) == 0) {
      status = cf_util_get_string(child, &configuration->postfix);

    } else if (strcasecmp("Prefix", child->key) == 0) {
      status = cf_util_get_string(child, &configuration->prefix);

    } else if (strcasecmp("SilenceTypeWarnings", child->key) == 0) {
      status = cf_util_get_boolean(
          child, &configuration->silence_type_warnings);

    } else if (strcasecmp("AlwaysAppendDS", child->key) == 0) {
      status = cf_util_get_boolean(
          child, &configuration->always_append_ds);

    } else if (strcasecmp("StoreRates", child->key) == 0) {
      status = cf_util_get_boolean(child, &configuration->store_rates);

    } else {
      WARNING("%s: Ignoring unknown config option '%s'", WRITE_STATSD_NAME,
              child->key);
    }

    /*
     * If any of the options cannot be parsed print an error message
     * but keep going on with the defaults.
     * If the host was not parsed correctily the configuration will fail.
     */
    if (status != 0) {
      ERROR("%s: Ignoring config option '%s' due to an error",
            WRITE_STATSD_NAME, child->key);
      return status;
    }
  }

  // Check required options.
  if (configuration->host == NULL) {
    ERROR("%s: missing required 'Host' configuration", WRITE_STATSD_NAME);
    sfree(configuration->postfix);
    sfree(configuration->prefix);
    return -2;
  }

  // Escape prefix and postfix.
  if (configuration->postfix) {
    char* current = configuration->postfix;
    configuration->postfix = write_statsd_escape(current);
    sfree(current);
  }
  if (configuration->prefix) {
    char* current = configuration->prefix;
    configuration->prefix = write_statsd_escape(current);
    sfree(current);
  }

  // Print loaded configuration (for debug).
  DEBUG("%s configuration completed", WRITE_STATSD_NAME);
  DEBUG("%s Host: %s", WRITE_STATSD_NAME, configuration->host);
  DEBUG("%s Port: %s", WRITE_STATSD_NAME, configuration->port);
  DEBUG("%s Prefix: %s", WRITE_STATSD_NAME, configuration->prefix);
  DEBUG("%s Postfix: %s", WRITE_STATSD_NAME, configuration->postfix);
  DEBUG("%s AlwaysAppendDS: %i", WRITE_STATSD_NAME,
        configuration->always_append_ds);
  DEBUG("%s SilenceTypeWarnings: %i", WRITE_STATSD_NAME,
        configuration->silence_type_warnings);
  DEBUG("%s StoreRates: %i", WRITE_STATSD_NAME, configuration->store_rates);

  // Register write callback with configuration as the user data.
  user_data_t ud;
  ud.data = (void*)configuration;
  ud.free_func = write_statsd_free_config;
  plugin_register_write(WRITE_STATSD_NAME, write_statsd_write, &ud);

  return 0;
}


static size_t write_statsd_format_key(
    const value_list_t *vl, _Bool include_ds_name, char* ds_name, char* key,
    write_statsd_config_t* configuration) {
  size_t plugin_instance_len = strlen(vl->plugin_instance);
  size_t type_instance_len = strlen(vl->type_instance);

  // Check which parts of the name should be included.
  _Bool has_prefix  = configuration->prefix != NULL;
  _Bool has_postfix = configuration->postfix != NULL;
  _Bool has_plugin_instance = plugin_instance_len != 0;
  _Bool has_type_instance   = type_instance_len != 0;

  // Escape key parts:
  char* host = write_statsd_escape(vl->host);
  char* plugin = write_statsd_escape(vl->plugin);
  char* plugin_instance = write_statsd_escape(vl->plugin_instance);
  char* type = write_statsd_escape(vl->type);
  char* type_instance = write_statsd_escape(vl->type_instance);

  // Format key to be :
  //   [prefix.]host.plugin.[plugin_instance.]type[.type_instance][.ds_name][.postfix]
  size_t length = snprintf(
      //   format   ->      PR. H. P. PI. T . TI. DS. PO
      key, MAX_KEY_LENGTH, "%s%s%s.%s.%s%s%s%s%s%s%s%s%s",
      has_prefix ? configuration->prefix : "", has_prefix ? "." : "",
      host, plugin, plugin_instance, has_plugin_instance ? "." : "",
      type, has_type_instance ? "." : "", type_instance,
      include_ds_name ? "." : "", include_ds_name ? ds_name : "",
      has_postfix ? "." : "", has_postfix ? configuration->postfix : ""
  );

  // Free escaped versions of strings.
  sfree(host);
  sfree(plugin);
  sfree(plugin_instance);
  sfree(type);
  sfree(type_instance);

  // Check that the string would fit in MAX_KEY_LENGTH.
  if (length >= MAX_KEY_LENGTH) {
    ERROR("%s: value name too long, truncated", WRITE_STATSD_NAME);
    return MAX_KEY_LENGTH;
  }
  return length;
}


static void write_statsd_free_config(void* configuration) {
  write_statsd_config_t* config = (write_statsd_config_t*)configuration;
  sfree(config->host);
  sfree(config->port);
  sfree(config->postfix);
  sfree(config->prefix);
  sfree(configuration);
};


static int write_statsd_send_message(
    const char* message, write_statsd_config_t* configuration) {
  int msg_len;
  int result;
  int sock = open_socket(configuration);

  if (sock == 0) {
    return 0;
  }

  msg_len = strlen(message);
  result = write(sock, message, msg_len);
  if (result != msg_len) {
    char errbuf[1024];
    ERROR("%s: sendto(2) failed: %s", WRITE_STATSD_NAME,
          sstrerror(errno, errbuf, sizeof(errbuf)));
  };

  close(sock);
  return 0;
}


static int write_statsd_write(
    const data_set_t *ds, const value_list_t *vl, user_data_t *ud) {
  write_statsd_config_t* configuration = (write_statsd_config_t*)ud->data;
  int   idx;
  _Bool include_ds_name = configuration->always_append_ds || ds->ds_num > 1;

  /*
   * Build the type name using all available fields in vl:
   * hostname.plugin[.plugin_instance].type[.type_instance]
   */
  gauge_t* rates = NULL;
  char*    key = NULL;

  key = malloc(MAX_KEY_LENGTH);
  if (key == NULL) {
    ERROR("%s: not enough memory for key buffer", WRITE_STATSD_NAME);
    return -5;
  }

  if (configuration->store_rates) {
    rates = uc_get_rate(ds, vl);
    if (rates == NULL) {
      ERROR("%s: uc_get_rate failed", WRITE_STATSD_NAME);
      free(key);
      return -6;
    }
  }

  // Process all values in the data set.
  for (idx = 0; idx < ds->ds_num; idx++) {
    size_t actual_key_length;
    char*  ds_type = DS_TYPE_TO_STATSD[ds->ds[idx].type];
    char*  message;
    size_t message_len;
    int    result;
    char*  value = NULL;

    if (ds_type == NULL) {
      if (!configuration->silence_type_warnings) {
        WARNING("%s: unsupported StatsD type '%s' for value with name '%s'",
                WRITE_STATSD_NAME, DS_TYPE_TO_STRING(ds->ds[idx].type),
                ds->ds[idx].name);
      }
      continue;  // To the next value in the data set.
    }

    if (ds->ds[idx].type == DS_TYPE_GAUGE || rates == NULL) {
      value = ds_value_to_string(ds->ds[idx].type, vl->values[idx]);
    } else {
      value = ds_rate_to_string(ds->ds[idx].type, rates[idx]);
    }

    if (value == NULL) {
      ERROR("%s: unable to get value from data set", WRITE_STATSD_NAME);
      free(value);
      continue;  // To the next value in the data set.
    }

    /*
     * The full message will have prefix, postfix and separators plus
     * the type name, the value name, value and type identifier.
     */
    actual_key_length = write_statsd_format_key(
        vl, include_ds_name, ds->ds[idx].name, key, configuration);
    if (actual_key_length <= 0) {
      ERROR("%s: unable to get key from data set", WRITE_STATSD_NAME);
      free(value);
      continue;  // To the next value in the data set.
    }

    message_len =  actual_key_length;
    message_len += strlen(value);
    message_len += strlen(ds_type);
    message_len += 3;  // For the ": " and "|" parts.

    message = malloc(message_len + 1);
    if (message == NULL) {
      ERROR("%s: not enough memory for message buffer", WRITE_STATSD_NAME);
      free(key);
      free(value);
      return -7;
    }

    snprintf(message, message_len + 1, "%s: %s|%s", key, value, ds_type);
    free(value);

    result = write_statsd_send_message(message, configuration);
    free(message);
    if (result != 0) {
      free(key);
      return result;
    }
  }

  free(key);
  free(rates);
  return 0;
}


void module_register(void) {
  plugin_register_complex_config(WRITE_STATSD_NAME, write_statsd_config);
  DEBUG("Registered %s module.", WRITE_STATSD_NAME);
}
