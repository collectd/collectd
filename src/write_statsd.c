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

#include <limits.h>
#include <stdlib.h>

#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_cache.h"


/*** Compile-time constants. ***/
#define MAX_KEY_LENGTH    1024
#define WS_DOUBLE_FORMAT  "%f"
#define VALUE_STR_LEN     20
#define WRITE_STATSD_NAME "write_statsd"

#define FREE_NOT_NULL(var) \
  if (var != NULL) {       \
    free(var);             \
    var = NULL;            \
  }


/*** Type declarations. ***/
struct write_statsd_config_s {
  char* host;
  int   port;
  char* postfix;
  char* prefix;
  _Bool always_append_ds;
  _Bool silence_type_warnings;
  _Bool store_rates;
};
typedef struct write_statsd_config_s write_statsd_config_t;

struct write_statsd_link_s {
  struct sockaddr_in server;
  int    sock;
};
typedef struct write_statsd_link_s write_statsd_link_t;


/*** Module variables. ***/
static char* DS_TYPE_TO_STATSD[] = {
  "c",  /* DS_TYPE_COUNTER  */
  "g",  /* DS_TYPE_GAUGE    */
  "g",  /* DS_TYPE_DERIVE   */
  "c"   /* DS_TYPE_ABSOLUTE */
};

/*
 * The configuration is stored in a global instance and must not
 * be modified outside of load_config or initialise.
 */
static write_statsd_config_t configuration = {
  NULL,  // host
  8125,  // port
  NULL,  // postfix
  NULL,  // prefix
  0,     // always_append_ds
  0,     // silence_type_warnings
  1      // store_rates
};


/*** Module functions. ***/
static void* allocate(size_t size) {
  void* mem = (char*)malloc(size);
  if (mem == NULL) {
    ERROR("write_statsd plugin: not enough memory.");
    return NULL;
  }
  return mem;
}

static char* ds_value_to_string(int type, value_t value) {
  char* result = allocate(VALUE_STR_LEN);
  if (result == NULL) {
    return NULL;
  }

  switch(type) {
    case DS_TYPE_COUNTER:
      ssnprintf(result, VALUE_STR_LEN, "%lld", value.counter);
      break;

    case DS_TYPE_GAUGE:
      ssnprintf(result, VALUE_STR_LEN, WS_DOUBLE_FORMAT, value.gauge);
      break;

    case DS_TYPE_DERIVE:
      ssnprintf(result, VALUE_STR_LEN, "%"PRIi64, value.derive);
      break;

    case DS_TYPE_ABSOLUTE:
      ssnprintf(result, VALUE_STR_LEN, "%"PRIu64, value.absolute);
      break;

    default:
      ERROR("write_statsd: unknown data source type: %i", type);
      free(result);
      return NULL;
  }

  return result;
}


static char* ds_rate_to_string(int type, double rate) {
  char* result = allocate(VALUE_STR_LEN);
  if (result == NULL) {
    return NULL;
  }

  ssnprintf(result, VALUE_STR_LEN, WS_DOUBLE_FORMAT, rate);
  return result;
}


static write_statsd_link_t open_socket(void) {
  write_statsd_link_t link  = {{0}, 0};
  struct addrinfo* resolved = NULL;
  int error = 0;

  link.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (link.sock == -1) {
    ERROR("write_statsd plugin: unable to open socket connection.");
    link.sock = 0;
    return link;
  }

  link.server.sin_family = AF_INET;
  link.server.sin_port   = htons(configuration.port);
  error = getaddrinfo(configuration.host, NULL, NULL, &resolved);

  if (error) {
    ERROR("write_statsd plugin: unable to resolve address - %s.",
          gai_strerror(error));
    close(link.sock);
    link.sock = 0;
    return link;
  }

  memcpy(&link.server.sin_addr,
         &((struct sockaddr_in*)resolved->ai_addr)->sin_addr,
         sizeof(struct in_addr));
  freeaddrinfo(resolved);
  return link;
}


static int write_statsd_config(oconfig_item_t *conf) {
  int idx;
  int status;

  for (idx = 0; idx < conf->children_num; idx++) {
    oconfig_item_t *child = conf->children + idx;
    status = 0;

    if (strcasecmp("Host", child->key) == 0) {
      status = cf_util_get_string(child, &configuration.host);

    } else if (strcasecmp("Port", child->key) == 0) {
      status = cf_util_get_int(child, & configuration.port);
      
    } else if (strcasecmp("Postfix", child->key) == 0) {
      status = cf_util_get_string(child, &configuration.postfix);

    } else if (strcasecmp("Prefix", child->key) == 0) {
      status = cf_util_get_string(child, &configuration.prefix);
    
    } else if (strcasecmp("SilenceTypeWarnings", child->key) == 0) {
      status = cf_util_get_boolean(
          child, &configuration.silence_type_warnings);

    } else if (strcasecmp("AlwaysAppendDS", child->key) == 0) {
      status = cf_util_get_boolean(
          child, &configuration.always_append_ds);

    } else if (strcasecmp("StoreRates", child->key) == 0) {
      status = cf_util_get_boolean(child, &configuration.store_rates);

    } else {
      WARNING("write_statsd plugin: Ignoring unknown config option '%s'.",
              child->key);
    }

    /*
     * If any of the options cannot be parsed print an error message
     * but keep going on with the defaults.
     * If the host was not parsed correctily the plugin init step
     * will fail for us.
     */
    if (status != 0) {
      ERROR("write_statsd plugin: Ignoring config option '%s' due to an error.",
            child->key);
      return status;
    }
  }
  return 0;
}

static int write_statsd_free(void) {
  FREE_NOT_NULL(configuration.host);
  FREE_NOT_NULL(configuration.postfix);
  FREE_NOT_NULL(configuration.prefix);
  DEBUG("Freed %s module.", WRITE_STATSD_NAME);
  return 0;
}

static int write_statsd_init(void) {
  if (configuration.host == NULL) {
    ERROR("write_statsd plugin: missing required 'Host' configuration.");
    FREE_NOT_NULL(configuration.postfix)
    FREE_NOT_NULL(configuration.prefix)
    return -1;
  }

  DEBUG("%s configuration completed.", WRITE_STATSD_NAME);
  DEBUG("%s Host: %s", WRITE_STATSD_NAME, configuration.host);
  DEBUG("%s Port: %i", WRITE_STATSD_NAME, configuration.port);
  DEBUG("%s Prefix: %s", WRITE_STATSD_NAME, configuration.prefix);
  DEBUG("%s Postfix: %s", WRITE_STATSD_NAME, configuration.postfix);

  DEBUG("%s AlwaysAppendDS: %i", WRITE_STATSD_NAME,
        configuration.always_append_ds);
  DEBUG("%s SilenceTypeWarnings: %i", WRITE_STATSD_NAME,
        configuration.silence_type_warnings);
  DEBUG("%s StoreRates: %i", WRITE_STATSD_NAME, configuration.store_rates);

  return 0;
}


static int write_statsd_send_message(const char* message) {
  int slen;
  int result;
  write_statsd_link_t link = open_socket();

  if (link.sock == 0) {
    return 0;
  }

  slen = sizeof(link.server);
  result = sendto(link.sock, message, strlen(message), 0,
                  (struct sockaddr *)&link.server, slen);

  if (result == -1) {
    ERROR("write_statsd plugin: unable to send message.");
  };

  close(link.sock);
  return 0;
}


static size_t write_statsd_format_key(
    const value_list_t *vl, _Bool include_ds_name, char* ds_name, char* key) {
  size_t plugin_instance_len = strlen(vl->plugin_instance);
  size_t type_instance_len = strlen(vl->type_instance);

  // Check which parts of the name should be included.
  _Bool has_prefix  = configuration.prefix != NULL;
  _Bool has_postfix = configuration.postfix != NULL;
  _Bool has_plugin_instance = plugin_instance_len != 0;
  _Bool has_type_instance   = type_instance_len != 0;

  // Check that the string would fit in MAX_KEY_LENGTH.
  size_t key_length = strlen(vl->host) + strlen(vl->plugin);
  key_length += strlen(vl->type) + 2;

  //  optionals:
  if (has_prefix) {
    key_length += strlen(configuration.prefix) + 1;
  }
  if (has_postfix) {
    key_length += strlen(configuration.postfix) + 1;
  }
  if (has_plugin_instance) {
    key_length += plugin_instance_len + 1;
  }
  if (has_type_instance) {
    key_length += type_instance_len + 1;
  }
  if (include_ds_name) {
    key_length += strlen(ds_name) + 1;
  }

  if (key_length >= MAX_KEY_LENGTH) {
    ERROR("write_statsd plugin: cannot process value, name too long");
    return 0;
  }

  // Format key to be :
  //   [prefix.]host.plugin.[plugin_instance.]type[.type_instance][.ds_name][.postfix]
  return snprintf(
      //                    PR. H. P. PI. T . TI. DS. PO
      key, MAX_KEY_LENGTH, "%s%s%s.%s.%s%s%s%s%s%s%s%s%s",
      has_prefix ? configuration.prefix : "", has_prefix ? "." : "",
      vl->host, vl->plugin,
      vl->plugin_instance, has_plugin_instance ? "." : "",
      vl->type, has_type_instance ? "." : "", vl->type_instance,
      include_ds_name ? "." : "", include_ds_name ? ds_name : "",
      has_postfix ? "." : "", has_postfix ? configuration.postfix : ""
  );
}


static int write_statsd_write(
    const data_set_t *ds, const value_list_t *vl, user_data_t *ud) {
  int   idx;
  _Bool include_ds_name = configuration.always_append_ds || ds->ds_num > 1;

  /*
   * Build the type name using all available fields in vl:
   * hostname.plugin[.plugin_instance].type[.type_instance]
   */
  gauge_t* rates = NULL;
  char*    key = NULL;

  key = allocate(MAX_KEY_LENGTH);
  if (key == NULL) {
    return -5;
  }

  if (configuration.store_rates) {
    rates = uc_get_rate(ds, vl);
    if (rates == NULL) {
      ERROR("write_statsd plugin: uc_get_rate failed.");
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
      if (!configuration.silence_type_warnings) {
        WARNING("write_statsd plugin: unsupported StatsD type '%s' "
                "for value with name '%s'.",
                DS_TYPE_TO_STRING(ds->ds[idx].type), ds->ds[idx].name);
      }
      continue;  // To the next value in the data set.
    }

    if (ds->ds[idx].type == DS_TYPE_GAUGE || rates == NULL) {
      value = ds_value_to_string(ds->ds[idx].type, vl->values[idx]);
    } else {
      value = ds_rate_to_string(ds->ds[idx].type, rates[idx]);
    }

    if (value == NULL) {
      ERROR("write_statsd plugin: unable to get value from data set.");
      free(value);
      continue;  // To the next value in the data set.
    }

    /*
     * The full message will have prefix, postfix and separators plus
     * the type name, the value name, value and type identifier.
     */
    actual_key_length = write_statsd_format_key(
        vl, include_ds_name, ds->ds[idx].name, key);
    if (actual_key_length <= 0) {
      ERROR("write_statsd plugin: unable to get key from data set.");
      free(value);
      continue;  // To the next value in the data set.
    }

    message_len =  actual_key_length;
    message_len += strlen(value);
    message_len += strlen(ds_type);
    message_len += 3;  // For the ": " and "|" parts.

    message = allocate(message_len + 1);
    if (message == NULL) {
      free(key);
      free(value);
      return -7;
    }

    snprintf(message, message_len + 1, "%s: %s|%s", key, value, ds_type);
    free(value);

    result = write_statsd_send_message(message);
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
  plugin_register_init(WRITE_STATSD_NAME, write_statsd_init);
  plugin_register_shutdown(WRITE_STATSD_NAME, write_statsd_free);
  plugin_register_write(WRITE_STATSD_NAME, write_statsd_write, NULL);
  DEBUG("Registered %s module.", WRITE_STATSD_NAME);
}

