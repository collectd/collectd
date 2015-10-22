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


/*** Compile-time constants. ***/
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
  _Bool silence_type_warnings;
  _Bool always_append_ds;

  /* Internally derived options. */
  char* event_line_format;
  size_t event_line_base_len;
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
  NULL  /* DS_TYPE_ABSOLUTE */
};

/*
 * The configuration is stored in a global instance and must not
 * be modified outside of load_config or initialise.
 */
static write_statsd_config_t configuration = {
  NULL, 8125, NULL, NULL, 0, NULL, 0
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
      ssnprintf(result, VALUE_STR_LEN, "%"PRId64, value.derive);
      break;

    default:
      free(result);
      return NULL;
  }

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

  configuration.event_line_base_len = 0;
  FREE_NOT_NULL(configuration.event_line_format);

  DEBUG("Freed %s module.", WRITE_STATSD_NAME);
  return 0;
}

static int write_statsd_init(void) {
  size_t buffer_len = 0;
  size_t len = 0;

  if (configuration.host == NULL) {
    ERROR("write_statsd plugin: missing required 'Host' configuration.");
    FREE_NOT_NULL(configuration.postfix)
    FREE_NOT_NULL(configuration.prefix)
    return -1;
  }

  /*
   * Generate format for event lines to pass to ssnprintf.
   * A line full event line would be:
   * <prefix>.<metric-name>.<value-name>.<postfix>:<value>|<type>
   * The length of the format must exclude the placeholders.
   */
  if (configuration.prefix == NULL && configuration.postfix == NULL) {
    /* %s%s%s:%s|%s */
    configuration.event_line_format = strdup("%s%s%s:%s|%s");
    len = 3;

  } else if (configuration.prefix == NULL) {
    /* %s%s%s.<postfix>:%s|%s */
    len = 4 + strlen(configuration.postfix);
    buffer_len = len + 8 + 1;
    configuration.event_line_format = allocate(buffer_len);
    if (configuration.event_line_format == NULL) {
      return -2;
    }
    ssnprintf(configuration.event_line_format, buffer_len,
             "%%s%%s%%s.%s:%%s|%%s", configuration.postfix);

  } else if (configuration.postfix == NULL) {
    /* <prefix>.%s%s%s:%s|%s */
    len = strlen(configuration.prefix) + 4;
    buffer_len = len + 8 + 1;
    configuration.event_line_format = allocate(buffer_len);
    if (configuration.event_line_format == NULL) {
      return -2;
    }
    ssnprintf(configuration.event_line_format, buffer_len,
             "%s.%%s%%s%%s:%%s|%%s", configuration.prefix);

  } else {
    /* <prefix>.%s%s%s.<potfix>:%s|%s */
    len = 5 + strlen(configuration.prefix) + strlen(configuration.postfix);
    buffer_len = len + 8 + 1;
    configuration.event_line_format = allocate(buffer_len);
    if (configuration.event_line_format == NULL) {
      return -2;
    }
    ssnprintf(configuration.event_line_format, buffer_len,
             "%s.%%s%%s%%s.%s:%%s|%%s", configuration.prefix,
             configuration.postfix);

  }
  configuration.event_line_base_len = len;

  /* Postfix and prefix are now in event_line_format so they can be freed. */
  FREE_NOT_NULL(configuration.postfix)
  FREE_NOT_NULL(configuration.prefix)

  DEBUG("%s configuration completed.", WRITE_STATSD_NAME);
  DEBUG("%s Host: %s", WRITE_STATSD_NAME, configuration.host);
  DEBUG("%s Port: %i", WRITE_STATSD_NAME, configuration.port);
  DEBUG("%s SilenceTypeWarnings: %i", WRITE_STATSD_NAME,
        configuration.silence_type_warnings);
  DEBUG("%s AlwaysAppendDS: %i", WRITE_STATSD_NAME,
        configuration.always_append_ds);
  DEBUG("%s FormatString: %s", WRITE_STATSD_NAME,
        configuration.event_line_format);
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

static int write_statsd_write(
    const data_set_t *ds, const value_list_t *vl, user_data_t *ud) {
  int idx;

  /*
   * Build the type name using all available fields in vl:
   * hostname.plugin[.plugin_instance].type[.type_instance]
   */
  char* value_key = NULL;
  size_t value_key_len = 2;  // Dots are always present.
  size_t plugin_instance_len = strlen(vl->plugin_instance);
  size_t type_instance_len = strlen(vl->type_instance);

  value_key_len += strlen(vl->host);
  value_key_len += strlen(vl->plugin);
  value_key_len += strlen(vl->type);

  // Account for name's optional fields.
  if (plugin_instance_len != 0) {
    value_key_len += plugin_instance_len + 1;
  }
  if (type_instance_len != 0) {
    value_key_len += type_instance_len + 1;
  }

  value_key = allocate(value_key_len + 1);
  if (value_key == NULL) {
    return -5;
  }

  // Determine format and build fixed part of the name.
  if (plugin_instance_len != 0 && type_instance_len != 0) {
    ssnprintf(value_key, value_key_len + 1, "%s.%s.%s.%s.%s",
              vl->host, vl->plugin, vl->plugin_instance,
              vl->type, vl->type_instance);
  } else if (plugin_instance_len != 0) {
    ssnprintf(value_key, value_key_len + 1, "%s.%s.%s.%s",
              vl->host, vl->plugin, vl->plugin_instance, vl->type);
  } else if (type_instance_len != 0) {
    ssnprintf(value_key, value_key_len + 1, "%s.%s.%s.%s",
              vl->host, vl->plugin, vl->type, vl->type_instance);
  } else {
    ssnprintf(value_key, value_key_len + 1, "%s.%s.%s",
              vl->host, vl->plugin, vl->type);
  }

  // Process all values in the data set.
  for (idx = 0; idx < ds->ds_num; idx++) {
    char* message;
    size_t message_len;
    int result;
    char* ds_type = DS_TYPE_TO_STATSD[ds->ds[idx].type];
    char* value = NULL;
    _Bool append_ds_name = (configuration.always_append_ds || ds->ds_num > 1);

    if (ds_type == NULL) {
      if (!configuration.silence_type_warnings) {
        WARNING("write_statsd plugin: unsupported StatsD type '%s' "
                "for value with name '%s'.",
                DS_TYPE_TO_STRING(ds->ds[idx].type), ds->ds[idx].name);
      }
      continue;  // To the next value in the data set.
    }

    value = ds_value_to_string(ds->ds[idx].type, vl->values[idx]);
    if (value == NULL) {
      free(value_key);
      return -6;
    }

    /*
     * The full message will have prefix, postfix and separators plus
     * the type name, the value name, value and type identifier.
     */
    message_len = configuration.event_line_base_len;
    message_len += value_key_len;
    if (append_ds_name) {
      message_len += strlen(ds->ds[idx].name) + 1;
    }
    message_len += strlen(value);
    message_len += strlen(ds_type);

    message = allocate(message_len + 1);
    if (message == NULL) {
      free(value_key);
      free(value);
      return -7;
    }

    if (append_ds_name) {
      ssnprintf(message, message_len + 1, configuration.event_line_format,
               value_key, ".", ds->ds[idx].name, value, ds_type);
    } else {
      ssnprintf(message, message_len + 1, configuration.event_line_format,
               value_key, "", "", value, ds_type);
    }
    free(value);

    result = write_statsd_send_message(message);
    free(message);
    if (result != 0) {
      return result;
    }
  }

  return 0;
}


void module_register(void) {
  plugin_register_complex_config(WRITE_STATSD_NAME, write_statsd_config);
  plugin_register_init(WRITE_STATSD_NAME, write_statsd_init);
  plugin_register_shutdown(WRITE_STATSD_NAME, write_statsd_free);
  plugin_register_write(WRITE_STATSD_NAME, write_statsd_write, NULL);
  DEBUG("Registered %s module.", WRITE_STATSD_NAME);
}

