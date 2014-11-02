/**
 * collectd - src/write_statsd.c
 * Copyright (C) 2014       Stefano Pogliani
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

#define WS_DOUBLE_FORMAT  "%f"
#define VALUE_STR_LEN     20
#define WRITE_STATSD_NAME "write_statsd"

#define FREE_NOT_NULL(var) \
  if (var != NULL) {       \
    free(var);             \
    var = NULL;            \
  }


// *** Type declarations. ***
static const char *config_keys[] =
{
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
  int   silence_type_warnings;

  // Internally derved options.
  char* base_key_format;
  size_t base_key_len;
};
typedef struct write_statsd_config_s write_statsd_config_t;

struct write_statsd_link_s {
  struct sockaddr_in server;
  int    sock;
};
typedef struct write_statsd_link_s write_statsd_link_t;


// *** Module variables. ***
static char* DS_TYPE_TO_STATSD[] = {
  "c",  // DS_TYPE_COUNTER
  "g",  // DS_TYPE_GAUGE
  "g",  // DS_TYPE_DERIVE
  NULL  // DS_TYPE_ABSOLUTE
};

// The configuration is stored in a global instance and must not
// be modified outside of load_config or initialise.
static write_statsd_config_t configuration = {
  NULL, 8125, NULL, NULL, 0, NULL, 0
};


// *** Function declarations. ***
// Utility.
static void* allocate(size_t size);
static char* ds_value_to_string(int type, value_t value);

// StatsD communication.
static write_statsd_link_t open_socket(void);

// Module.
void module_register(void);
static int write_statsd_config(const char*, const char*);
static int write_statsd_free(void);
static int write_statsd_init(void);

// Write.
static int write_statsd_send_message(const char*);
static int write_statsd_write(
    const data_set_t*, const value_list_t*, user_data_t*);


// *** Function definitions. *** 
void* allocate(size_t size)
{
  void* mem = (char*)malloc(size);
  if (mem == NULL) {
    ERROR("write_statsd plugin: not enough memory.");
    return NULL;
  }
  return mem;
}

char* ds_value_to_string(int type, value_t value)
{
  char* result = allocate(VALUE_STR_LEN);
  if (result == NULL) {
    return NULL;
  }

  switch(type) {
    case DS_TYPE_COUNTER:
      sprintf(result, "%lld", value.counter);
      break;

    case DS_TYPE_GAUGE:
      sprintf(result, WS_DOUBLE_FORMAT, value.gauge);
      break;

    case DS_TYPE_DERIVE:
      sprintf(result, "%"PRId64, value.derive);
      break;

    default:
      free(result);
      return NULL;
  }

  return result;
}


write_statsd_link_t open_socket(void)
{
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


void module_register(void)
{
  plugin_register_config(
      WRITE_STATSD_NAME, write_statsd_config, config_keys, config_keys_num);
  plugin_register_init(WRITE_STATSD_NAME, write_statsd_init);
  plugin_register_shutdown(WRITE_STATSD_NAME, write_statsd_free);
  plugin_register_write(WRITE_STATSD_NAME, write_statsd_write, NULL);
}

int write_statsd_config(const char *key, const char *value)
{
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
    configuration.silence_type_warnings = strcasecmp(value, "yes") == 0;
  }

  return 0;
}

int write_statsd_free(void)
{
  FREE_NOT_NULL(configuration.host)
  FREE_NOT_NULL(configuration.postfix)
  FREE_NOT_NULL(configuration.prefix)

  configuration.base_key_len = 0;
  FREE_NOT_NULL(configuration.base_key_format)
  return 0;
}

int write_statsd_init(void)
{
  if (configuration.host == NULL) {
    ERROR("write_statsd plugin: missing required 'Host' configuration.");
    FREE_NOT_NULL(configuration.postfix)
    FREE_NOT_NULL(configuration.prefix)
    return -1;
  }

  // Derive internal options.
  size_t len = 0;
  if (configuration.prefix == NULL && configuration.postfix == NULL) {
    configuration.base_key_format = strdup("%s");
    len = 2;

  } else if (configuration.prefix == NULL) {
    // %s.<postfix>
    len = 2 + 1 + strlen(configuration.postfix);
    configuration.base_key_format = allocate(len + 1);
    if (configuration.base_key_format == NULL) {
      return -2;
    }
    sprintf(configuration.base_key_format, "%%s.%s", configuration.postfix);

  } else if (configuration.postfix == NULL) {
    // <prefix>.%s
    len = strlen(configuration.prefix) + 1 + 2;
    configuration.base_key_format = allocate(len + 1);
    if (configuration.base_key_format == NULL) {
      return -2;
    }
    sprintf(configuration.base_key_format, "%s.%%s", configuration.prefix);

  } else {
    // <prefix>.%s.<potfix>
    len =  strlen(configuration.prefix) + 1 + 2;
    len += 1 + strlen(configuration.postfix);
    configuration.base_key_format = allocate(len + 1);
    if (configuration.base_key_format == NULL) {
      return -2;
    }
    sprintf(configuration.base_key_format, "%s.%%s.%s",
            configuration.prefix, configuration.postfix);

  }
  configuration.base_key_len = len;

  return 0;
}


int write_statsd_send_message(const char* message)
{
  write_statsd_link_t link = open_socket();
  if (link.sock == 0) {
    return 0;
  }

  int slen = sizeof(link.server);
  int result = sendto(link.sock, message, strlen(message), 0,
                      (struct sockaddr *)&link.server, slen);

  if (result == -1) {
    ERROR("write_statsd plugin: unable to send message.");
  };

  close(link.sock);
  return 0;
}

int write_statsd_write(
    const data_set_t *ds, const value_list_t *vl, user_data_t *ud)
{
  int idx;

  for (idx = 0; idx < ds->ds_num; idx++) {
    char*  key_name;
    size_t key_name_len;
    char*  value_name;
    size_t value_name_len;

    value_name_len =  strlen(ds->type) + 1 + strlen(ds->ds[idx].name);
    key_name_len = value_name_len + configuration.base_key_len;
    key_name = allocate(key_name_len + 1);
    if (key_name == NULL) {
      return -3;
    }
    value_name = allocate(value_name_len + 1);
    if (value_name == NULL) {
      free(key_name);
      return -4;
    }

    // Format the key name.
    sprintf(value_name, "%s.%s", ds->type, ds->ds[idx].name);
    sprintf(key_name, configuration.base_key_format, value_name);
    free(value_name);

    char* type = DS_TYPE_TO_STATSD[ds->ds[idx].type];
    if (type == NULL) {
      if (!configuration.silence_type_warnings) {
        WARNING("write_statsd plugin: unsupported StatsD type '%s' "
                "for key '%s'.",
                DS_TYPE_TO_STRING(ds->ds[idx].type), key_name);
      }

      free(key_name);
      return 0;
    }
    char* value = ds_value_to_string(ds->ds[idx].type, vl->values[idx]);
    if (value == NULL) {
      return -5;
    }

    // Compute length of StatsD message and allocate buffer.
    //   Format <key>:<value>|<type>@<rate>
    size_t message_len = key_name_len + strlen(value) + strlen(type) + 2;
    char*  message     = allocate(message_len + 1);
    if (message == NULL) {
      free(key_name);
      free(value);
      return -6;
    }

    sprintf(message, "%s:%s|%s", key_name, value, type);
    free(key_name);
    free(value);

    int ret = write_statsd_send_message(message);
    free(message);
    if (ret != 0) {
      return ret;
    }
  }

  return 0;
}

