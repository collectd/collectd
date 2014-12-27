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
static const char *config_keys[] = {
  "Host",
  "Port",
  "Postfix",
  "Prefix",
  "SilenceTypeWarnings"
};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

struct write_statsd_config_s {
  char* host;
  int   port;
  char* postfix;
  char* prefix;
  _Bool silence_type_warnings;

  /* Internally derved options. */
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
  write_statsd_link_t link      = {{0}, 0};
  struct addrinfo     requested = {0};
  struct addrinfo*    resolved  = NULL;
  int error = 0;

  link.sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (link.sock == -1) {
    ERROR("write_statsd plugin: unable to open socket connection.");
    link.sock = 0;
    return link;
  }

  link.server.sin_family = AF_INET;
  link.server.sin_port   = htons(configuration.port);
  requested.ai_family    = AF_INET;
  requested.ai_socktype  = SOCK_DGRAM;
  error = getaddrinfo(configuration.host, NULL, &requested, &resolved);

  if (error) {
    ERROR("write_statsd plugin: unable to resolve addres - %s.",
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


static int write_statsd_config(const char *key, const char *value) {
  if (strcasecmp(key, "Host") == 0) {
    configuration.host = strdup(value);
  }

  if (strcasecmp(key, "Port") == 0) {
    configuration.port = strtol(value, NULL, 10);
    if (configuration.port == 0 || configuration.port == LONG_MAX ||
        configuration.port == LONG_MIN) {
      return -1;
    }
  }

  if (strcasecmp(key, "Postfix") == 0) {
    configuration.postfix = strdup(value);
  }

  if (strcasecmp(key, "Prefix") == 0) {
    configuration.prefix = strdup(value);
  }

  if (strcasecmp(key, "SilenceTypeWarnings") == 0) {
    configuration.silence_type_warnings = IS_TRUE(value);
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
    /* %s.%s:%s|%s */
    configuration.event_line_format = strdup("%s.%s:%s|%s");
    len = 3;

  } else if (configuration.prefix == NULL) {
    /* %s.%s.<postfix>:%s|%s */
    len = 4 + strlen(configuration.postfix);
    buffer_len = len + 8 + 1;
    configuration.event_line_format = allocate(buffer_len);
    if (configuration.event_line_format == NULL) {
      return -2;
    }
    ssnprintf(configuration.event_line_format, buffer_len,
             "%%s.%%s.%s:%%s|%%s", configuration.postfix);

  } else if (configuration.postfix == NULL) {
    /* <prefix>.%s.%s:%s|%s */
    len = strlen(configuration.prefix) + 4;
    buffer_len = len + 8 + 1;
    configuration.event_line_format = allocate(buffer_len);
    if (configuration.event_line_format == NULL) {
      return -2;
    }
    ssnprintf(configuration.event_line_format, buffer_len,
             "%s.%%s.%%s:%%s|%%s", configuration.prefix);

  } else {
    /* <prefix>.%s.%s.<potfix>:%s|%s */
    len = 5 + strlen(configuration.prefix) + strlen(configuration.postfix);
    buffer_len = len + 8 + 1;
    configuration.event_line_format = allocate(buffer_len);
    if (configuration.event_line_format == NULL) {
      return -2;
    }
    ssnprintf(configuration.event_line_format, buffer_len,
             "%s.%%s.%%s.%s:%%s|%%s", configuration.prefix,
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

  for (idx = 0; idx < ds->ds_num; idx++) {
    char* message;
    size_t message_len;
    int result;
    char* type = DS_TYPE_TO_STATSD[ds->ds[idx].type];
    char* value = NULL;

    if (type == NULL) {
      if (!configuration.silence_type_warnings) {
        WARNING("write_statsd plugin: unsupported StatsD type '%s' "
                "for value with name '%s'.",
                DS_TYPE_TO_STRING(ds->ds[idx].type), ds->ds[idx].name);
      }
      return 0;
    }

    value = ds_value_to_string(ds->ds[idx].type, vl->values[idx]);
    if (value == NULL) {
      return -5;
    }

    /*
     * The full message will have prefix, postfix and separators plus
     * the type name, the value name, value and type identifier.
     */
    message_len = configuration.event_line_base_len;
    message_len += strlen(ds->type);
    message_len += strlen(ds->ds[idx].name);
    message_len += strlen(value);
    message_len += strlen(type);

    message = allocate(message_len + 1);
    if (message == NULL) {
      free(value);
      return -6;
    }

    ssnprintf(message, message_len + 1, configuration.event_line_format,
             ds->type, ds->ds[idx].name, value, type);
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
  plugin_register_config(
      WRITE_STATSD_NAME, write_statsd_config, config_keys, config_keys_num);
  plugin_register_init(WRITE_STATSD_NAME, write_statsd_init);
  plugin_register_shutdown(WRITE_STATSD_NAME, write_statsd_free);
  plugin_register_write(WRITE_STATSD_NAME, write_statsd_write, NULL);
  DEBUG("Registered %s module.", WRITE_STATSD_NAME);
}

