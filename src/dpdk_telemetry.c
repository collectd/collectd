/*-
 * collectd - src/dpdk_telemetry.c
 * MIT License
 *
 * Copyright(c) 2019 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to
 * do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all
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
 */

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"
#include "utils_time.h"

#include <errno.h>
#include <jansson.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define BUF_SIZE 100000
#define PLUGIN_NAME "dpdk_telemetry"
#define DEFAULT_DPDK_PATH "/var/run/dpdk/rte/telemetry"
#define DEFAULT_CLIENT_PATH "/var/run/.client"
#define MAX_COMMANDS 2

struct client_info {
  int s_send;
  int s_recv;
  int fd;
  const char *dpdk_path;
  const char *client_path;
  struct sockaddr_un addr;
  struct sockaddr_un addrs;
};

typedef struct client_info client_info_t;

static client_info_t client;
static char g_client_path[BUF_SIZE];
static char g_dpdk_path[BUF_SIZE];

static int dpdk_telemetry_config(oconfig_item_t *ci) {
  int ret, i;

  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);

  for (i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("ClientSocketPath", child->key) == 0) {
      ret = cf_util_get_string_buffer(child, g_client_path,
                                      sizeof(g_client_path));
    } else if (strcasecmp("DpdkSocketPath", child->key) == 0) {
      ret = cf_util_get_string_buffer(child, g_dpdk_path, sizeof(g_dpdk_path));
    } else {
      ERROR(PLUGIN_NAME ": Unknown configuration parameter"
                        "\"%s\"",
            child->key);
      ret = -1;
    }

    if (ret < 0) {
      INFO(PLUGIN_NAME ": %s:%d ret =%d", __FUNCTION__, __LINE__, ret);
      return ret;
    }
  }

  return 0;
}

static int dpdk_telemetry_parse(json_t *stats, json_t *port) {
  json_t *statsArrayObj;
  int portid;

  if (!stats) {
    ERROR(PLUGIN_NAME ": Stats pointer is invalid");
    return -1;
  }

  if (!port) {
    ERROR(PLUGIN_NAME ":  Port pointer is invalid");
    return -1;
  }
  portid = json_integer_value(port);

  if (portid < -1) {
    ERROR(PLUGIN_NAME ": portid is invalid");
    return -1;
  }

  json_t *name, *value;
  const char *name_string;
  int statslen, i;
  uint64_t value_int;
  statslen = json_array_size(stats);
  for (i = 0; i < statslen; i++) {
    statsArrayObj = json_array_get(stats, i);
    name = json_object_get(statsArrayObj, "name");
    value = json_object_get(statsArrayObj, "value");
    if (!name) {
      ERROR(PLUGIN_NAME ": Request does not have name field");
      return -1;
    }
    if (!json_is_string(name)) {
      ERROR(PLUGIN_NAME ": Metric name is not a string");
      return -1;
    }
    name_string = json_string_value(name);
    if (!value) {
      ERROR(PLUGIN_NAME ": Request does not have value name");
      return -1;
    }
    if (!json_is_integer(value)) {
      ERROR(PLUGIN_NAME ": Metric value is not an integer");
      return -1;
    }

    char dev_name[DATA_MAX_NAME_LEN];
    if (portid == -1)
      snprintf(dev_name, sizeof(dev_name), "%s", name_string);
    else
      snprintf(dev_name, sizeof(dev_name), "%s.%d", name_string, portid);
    value_int = json_integer_value(value);
    value_t dpdk_telemetry_values[1];
    value_list_t dpdk_telemetry_vl = VALUE_LIST_INIT;
    dpdk_telemetry_values[0].counter = value_int;
    dpdk_telemetry_vl.values = dpdk_telemetry_values;
    dpdk_telemetry_vl.values_len = 1;
    dpdk_telemetry_vl.time = cdtime();
    snprintf(dpdk_telemetry_vl.host, sizeof(dpdk_telemetry_vl.host), "%s",
             hostname_g);
    snprintf(dpdk_telemetry_vl.plugin, sizeof(dpdk_telemetry_vl.plugin),
             "dpdk_telemetry");
    sstrncpy(dpdk_telemetry_vl.plugin_instance, dev_name,
             sizeof(dpdk_telemetry_vl.plugin_instance));
    snprintf(dpdk_telemetry_vl.type, sizeof(dpdk_telemetry_vl.type),
             "dpdk_telemetry");
    snprintf(dpdk_telemetry_vl.type_instance,
             sizeof(dpdk_telemetry_vl.type_instance), "%s", name_string);

    int ret = plugin_dispatch_values(&dpdk_telemetry_vl);
    if (ret < 0) {
      ERROR(PLUGIN_NAME ": Failed to dispatch values");
      return -1;
    }
  }
  return 0;
}

static int parse_json(char *buf) {

  if (!buf) {
    ERROR(PLUGIN_NAME ": buf pointer is invalid");
    return -1;
  }
  json_error_t error;
  json_t *root = json_loads(buf, 0, &error);
  int arraylen, i;
  json_t *status, *dataArray, *stats, *dataArrayObj;
  stats = NULL;

  if (!root) {
    ERROR(PLUGIN_NAME ": Could not load JSON object from data passed in"
                      " : %s",
          error.text);
    return -1;
  } else if (!json_is_object(root)) {
    ERROR(PLUGIN_NAME ": JSON Request is not a JSON object");
    json_decref(root);
    return -1;
  }

  status = json_object_get(root, "status_code");
  if (!status) {
    ERROR(PLUGIN_NAME ": Request does not have status field");
    return -1;
  } else if (!json_is_string(status)) {
    ERROR(PLUGIN_NAME ": Status value is not a string");
    return -1;
  }
  dataArray = json_object_get(root, "data");
  if (!dataArray) {
    ERROR(PLUGIN_NAME ": Request does not have data field");
    return -1;
  }
  arraylen = json_array_size(dataArray);
  if (!arraylen) {
    ERROR(PLUGIN_NAME ": No data to get");
    return -1;
  }

  for (i = 0; i < arraylen; i++) {
    json_t *port;
    dataArrayObj = json_array_get(dataArray, i);
    port = json_object_get(dataArrayObj, "port");
    stats = json_object_get(dataArrayObj, "stats");
    if (!port) {
      ERROR(PLUGIN_NAME ": Request does not have port field");
      return -1;
    }
    if (!json_is_integer(port)) {
      ERROR(PLUGIN_NAME ": Port value is not an integer");
      return -1;
    }

    if (!stats) {
      ERROR(PLUGIN_NAME ": Request does not have stats field");
      return -1;
    }
    dpdk_telemetry_parse(stats, port);
  }
  return 0;
}

static int dpdk_telemetry_cleanup(void) {
  close(client.s_send);
  close(client.s_recv);
  close(client.fd);
  client.s_send = -1;
  client.s_recv = -1;
  client.fd = -1;
  return 0;
}

static int dpdk_telemetry_socket_init(void) {
  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);
  char message[BUF_SIZE];

  /* Here we look up the length of the g_dpdk_path string
   * If it has a length we use it, otherwise we fall back to default
   * See dpdk_telemetry_config() for details
   */
  client.dpdk_path = (strlen(g_dpdk_path)) ? g_dpdk_path : DEFAULT_DPDK_PATH;
  client.client_path =
      (strlen(g_client_path)) ? g_client_path : DEFAULT_CLIENT_PATH;
  client.s_send = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (client.s_send < 0) {
    ERROR(PLUGIN_NAME ": Failed to open socket errno(%d), error(%s)", errno,
          strerror(errno));
    return -1;
  }
  client.s_recv = socket(AF_UNIX, SOCK_SEQPACKET, 0);
  if (client.s_recv < 0) {
    ERROR(PLUGIN_NAME ": Failed to open message socket errno(%d), error(%s)",
          errno, strerror(errno));
    dpdk_telemetry_cleanup();
    return -1;
  }
  client.addr.sun_family = AF_UNIX;
  snprintf(client.addr.sun_path, sizeof(client.addr.sun_path), "%s",
           client.dpdk_path);
  if (connect(client.s_send, (struct sockaddr *)&client.addr,
              sizeof(client.addr)) < 0) {
    ERROR(PLUGIN_NAME ": Failed to connect errno(%d), error(%s)", errno,
          strerror(errno));
    dpdk_telemetry_cleanup();
    return -1;
  }
  client.addrs.sun_family = AF_UNIX;
  snprintf(client.addrs.sun_path, sizeof(client.addrs.sun_path), "%s",
           client.client_path);
  unlink(client.client_path);
  if (bind(client.s_recv, (struct sockaddr *)&client.addrs,
           sizeof(client.addrs)) < 0) {
    ERROR(PLUGIN_NAME ": Failed to bind errno(%d), error(%s)", errno,
          strerror(errno));
    dpdk_telemetry_cleanup();
    return -1;
  }
  if (listen(client.s_recv, 1) < 0) {
    ERROR(PLUGIN_NAME ": Listen failed errno(%d), error(%s)", errno,
          strerror(errno));
    dpdk_telemetry_cleanup();
    return -1;
  }
  snprintf(message, sizeof(message),
           "{\"action\":1,\"command\":\"clients\""
           ",\"data\":{\"client_path\":\"%s\"}}",
           client.client_path);
  if (send(client.s_send, message, strlen(message), 0) < 0) {
    ERROR(PLUGIN_NAME ": Could not send register message errno(%d), error(%s)",
          errno, strerror(errno));
    dpdk_telemetry_cleanup();
    return -1;
  }
  client.fd = accept(client.s_recv, NULL, NULL);
  if (client.fd < 0) {
    ERROR(PLUGIN_NAME ": Failed to accept errno(%d), error(%s)", errno,
          strerror(errno));
    dpdk_telemetry_cleanup();
    return -1;
  }
  return 0;
}

static int dpdk_telemetry_shutdown(void) {
  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);
  char msg[BUF_SIZE];
  int ret;

  snprintf(msg, sizeof(msg),
           "{\"action\":2,\"command\":\"clients\""
           ",\"data\":{\"client_path\":\"%s\"}}",
           client.client_path);
  ret = send(client.fd, msg, strlen(msg), 0);
  if (ret < 0) {
    ERROR(PLUGIN_NAME ": Could not send unregister message");
    dpdk_telemetry_cleanup();
    return -1;
  }
  dpdk_telemetry_cleanup();
  return 0;
}

static int dpdk_telemetry_read(user_data_t *ud) {
  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);
  char buffer[BUF_SIZE];
  int bytes = 0, ret;
  char *json_string[MAX_COMMANDS] = {"{\"action\":0,\"command\":"
                                     "\"ports_all_stat_values\",\"data\":null}",
                                     "{\"action\":0,\"command\":"
                                     "\"global_stat_values\",\"data\":null}"};
  size_t size = sizeof(json_string) / sizeof(json_string[0]);

  for (int i = 0; i < size; i++) {
    if (send(client.fd, json_string[i], strlen(json_string[i]), 0) < 0) {
      ERROR(PLUGIN_NAME
            ": Could not send request for stats errno(%d), error(%s)",
            errno, strerror(errno));
      if (errno == EBADF || errno == ECONNRESET || errno == ENOTCONN ||
          errno == EPIPE) {
        dpdk_telemetry_cleanup();
        dpdk_telemetry_socket_init();
        send(client.fd, json_string[i], strlen(json_string[i]), 0);
      }
    } else {
      bytes = recv(client.fd, buffer, sizeof(buffer) - 1, 0);
      if (bytes < 0) {
        ERROR(PLUGIN_NAME ": Could not receive stats errno(%d), error(%s)",
              errno, strerror(errno));
        dpdk_telemetry_cleanup();
        dpdk_telemetry_socket_init();
      } else {
        buffer[bytes] = '\0';
        ret = parse_json(buffer);
        if (ret < 0)
          ERROR(PLUGIN_NAME ": Parsing failed");
      }
    }
  }
  return 0;
}

static int dpdk_telemetry_init(void) {

  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);

  client.s_send = -1;
  client.s_recv = -1;
  client.fd = -1;
  if (dpdk_telemetry_socket_init() < 0)
    ERROR(PLUGIN_NAME ": Socket initialization failed.");

  return 0;
}

void module_register(void) {
  plugin_register_init(PLUGIN_NAME, dpdk_telemetry_init);
  plugin_register_complex_config(PLUGIN_NAME, dpdk_telemetry_config);
  plugin_register_complex_read(NULL, PLUGIN_NAME, dpdk_telemetry_read, 0, NULL);
  plugin_register_shutdown(PLUGIN_NAME, dpdk_telemetry_shutdown);
}
