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

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/dmi/dmi.h"

#include <microhttpd.h>
#if MHD_VERSION >= 0x00097002
#define MHD_RESULT enum MHD_Result
#else
#define MHD_RESULT int
#endif

#include <jansson.h>
#include <netdb.h>

#define CAP_PLUGIN "capabilities"
#define CONTENT_TYPE_JSON "application/json"
#define LISTEN_BACKLOG 16

typedef struct dmi_type_name_s {
  dmi_type type;
  const char *name;
} dmi_type_name_t;

static char *g_cap_json = NULL;
static char *httpd_host = NULL;
static unsigned short httpd_port = 9104;
static struct MHD_Daemon *httpd;

static dmi_type_name_t types_list[] = {
    {BIOS, "BIOS"},
    {SYSTEM, "SYSTEM"},
    {BASEBOARD, "BASEBOARD"},
    {PROCESSOR, "PROCESSORS"},
    {CACHE, "CACHE"},
    {PHYSICAL_MEMORY_ARRAY, "PHYSICAL MEMORY ARRAYS"},
    {MEMORY_DEVICE, "MEMORY DEVICES"},
    {IPMI_DEVICE, "IPMI DEVICE"},
    {ONBOARD_DEVICES_EXTENDED_INFORMATION,
     "ONBOARD DEVICES EXTENDED INFORMATION"}};

static int cap_get_dmi_variables(json_t *parent, const dmi_type type,
                                 const char *json_name) {
  DEBUG(CAP_PLUGIN ": cap_get_dmi_variables: %d/%s.", type, json_name);
  dmi_reader_t reader;
  if (dmi_reader_init(&reader, type) != DMI_OK) {
    ERROR(CAP_PLUGIN ": dmi_reader_init failed.");
    return -1;
  }

  json_t *section = NULL;
  json_t *entries = NULL;
  json_t *attributes = NULL;
  json_t *arr = json_array();
  if (arr == NULL) {
    ERROR(CAP_PLUGIN ": Failed to allocate json array.");
    dmi_reader_clean(&reader);
    return -1;
  }
  if (json_object_set_new(parent, json_name, arr)) {
    ERROR(CAP_PLUGIN ": Failed to set array to parent.");
    dmi_reader_clean(&reader);
    return -1;
  }

  while (reader.current_type != DMI_ENTRY_END) {
    int status = dmi_read_next(&reader);
    if (status != DMI_OK) {
      ERROR(CAP_PLUGIN ": dmi_read_next failed.");
      return -1;
    }

    switch (reader.current_type) {

    case DMI_ENTRY_NAME:
      DEBUG("%s", reader.name);
      attributes = NULL;
      section = json_object();
      if (section == NULL) {
        ERROR(CAP_PLUGIN ": Failed to allocate json object.");
        dmi_reader_clean(&reader);
        return -1;
      }
      if (json_array_append_new(arr, section)) {
        ERROR(CAP_PLUGIN ": Failed to append json entry.");
        dmi_reader_clean(&reader);
        return -1;
      }
      entries = json_object();
      if (entries == NULL) {
        ERROR(CAP_PLUGIN ": Failed to allocate json object.");
        dmi_reader_clean(&reader);
        return -1;
      }
      if (json_object_set_new(section, reader.name, entries)) {
        ERROR(CAP_PLUGIN ": Failed to set json entry.");
        dmi_reader_clean(&reader);
        return -1;
      }
      break;

    case DMI_ENTRY_MAP:
      DEBUG("    %s:%s", reader.name, reader.value);
      attributes = NULL;
      if (entries == NULL) {
        ERROR(CAP_PLUGIN ": unexpected dmi output format.");
        dmi_reader_clean(&reader);
        return -1;
      }
      if (json_object_set_new(entries, reader.name,
                              json_string(reader.value))) {
        ERROR(CAP_PLUGIN ": Failed to set json object for entries.");
        dmi_reader_clean(&reader);
        return -1;
      }
      break;

    case DMI_ENTRY_LIST_NAME:
      DEBUG("    %s:", reader.name);
      if (entries == NULL) {
        ERROR(CAP_PLUGIN ": unexpected dmi output format.");
        dmi_reader_clean(&reader);
        return -1;
      }
      attributes = json_array();
      if (attributes == NULL) {
        ERROR(CAP_PLUGIN ": Failed to allocate json array for attributes.");
        dmi_reader_clean(&reader);
        return -1;
      }
      if (json_object_set_new(entries, reader.name, attributes)) {
        ERROR(CAP_PLUGIN ": Failed to set json object for entry %s.",
              reader.name);
        dmi_reader_clean(&reader);
        return -1;
      }
      break;

    case DMI_ENTRY_LIST_VALUE:
      DEBUG("        %s", reader.value);
      if (attributes == NULL) {
        ERROR(CAP_PLUGIN ": unexpected dmi output format");
        dmi_reader_clean(&reader);
        return -1;
      }
      if (json_array_append_new(attributes, json_string(reader.value))) {
        ERROR(CAP_PLUGIN ": Failed to append json attribute.");
        dmi_reader_clean(&reader);
        return -1;
      }
      break;

    default:
      section = NULL;
      entries = NULL;
      attributes = NULL;
      break;
    }
  }

  return 0;
}

/* http_handler is the callback called by the microhttpd library. It essentially
 * handles all HTTP request aspects and creates an HTTP response. */
static MHD_RESULT cap_http_handler(void *cls, struct MHD_Connection *connection,
                                   const char *url, const char *method,
                                   const char *version, const char *upload_data,
                                   size_t *upload_data_size,
                                   void **connection_state) {
  if (strcmp(method, MHD_HTTP_METHOD_GET) != 0) {
    return MHD_NO;
  }

  /* On the first call for each connection, return without anything further.
   * The first time only the headers are valid, do not respond in the first
   * round. The docs are not very specific on the issue. */
  if (*connection_state == NULL) {
    /* set to a random non-NULL pointer. */
    *connection_state = &(int){44};
    return MHD_YES;
  }
  DEBUG(CAP_PLUGIN ": formatted response: %s", g_cap_json);

#if defined(MHD_VERSION) && MHD_VERSION >= 0x00090500
  struct MHD_Response *res = MHD_create_response_from_buffer(
      strlen(g_cap_json), g_cap_json, MHD_RESPMEM_PERSISTENT);
#else
  struct MHD_Response *res =
      MHD_create_response_from_data(strlen(g_cap_json), g_cap_json, 0, 0);
#endif
  if (res == NULL) {
    ERROR(CAP_PLUGIN ": MHD create response failed.");
    return MHD_NO;
  }

  MHD_add_response_header(res, MHD_HTTP_HEADER_CONTENT_TYPE, CONTENT_TYPE_JSON);
  int status = MHD_queue_response(connection, MHD_HTTP_OK, res);

  MHD_destroy_response(res);

  return status;
}

static void cap_logger(__attribute__((unused)) void *arg, char const *fmt,
                       va_list ap) {
  char errbuf[1024];
  vsnprintf(errbuf, sizeof(errbuf), fmt, ap);

  ERROR(CAP_PLUGIN ": libmicrohttpd: %s", errbuf);
}

#if defined(MHD_VERSION) && MHD_VERSION >= 0x00090000
static int cap_open_socket() {
  char service[NI_MAXSERV];
  snprintf(service, sizeof(service), "%hu", httpd_port);

  struct addrinfo *res;
  int status = getaddrinfo(httpd_host, service,
                           &(struct addrinfo){
                               .ai_flags = AI_PASSIVE | AI_ADDRCONFIG,
                               .ai_family = PF_INET,
                               .ai_socktype = SOCK_STREAM,
                           },
                           &res);
  if (status != 0) {
    return -1;
  }

  int fd = -1;
  for (struct addrinfo *ai = res; ai != NULL; ai = ai->ai_next) {
    int flags = ai->ai_socktype;
#ifdef SOCK_CLOEXEC
    flags |= SOCK_CLOEXEC;
#endif

    fd = socket(ai->ai_family, flags, 0);
    if (fd == -1)
      continue;

    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) != 0) {
      WARNING(CAP_PLUGIN ": setsockopt(SO_REUSEADDR) failed: %s", STRERRNO);
      close(fd);
      fd = -1;
      continue;
    }

    if (bind(fd, ai->ai_addr, ai->ai_addrlen) != 0) {
      INFO(CAP_PLUGIN ": bind failed: %s", STRERRNO);
      close(fd);
      fd = -1;
      continue;
    }

    if (listen(fd, LISTEN_BACKLOG) != 0) {
      INFO(CAP_PLUGIN ": listen failed: %s", STRERRNO);
      close(fd);
      fd = -1;
      continue;
    }

    char str_node[NI_MAXHOST];
    char str_service[NI_MAXSERV];

    getnameinfo(ai->ai_addr, ai->ai_addrlen, str_node, sizeof(str_node),
                str_service, sizeof(str_service),
                NI_NUMERICHOST | NI_NUMERICSERV);

    INFO(CAP_PLUGIN ": Listening on [%s]:%s.", str_node, str_service);
    break;
  }

  freeaddrinfo(res);

  return fd;
}
#endif

static struct MHD_Daemon *cap_start_daemon() {
#if defined(MHD_VERSION) && MHD_VERSION >= 0x00090000
  int fd = cap_open_socket();
  if (fd == -1) {
    ERROR(CAP_PLUGIN ": Opening a listening socket for [%s]:%hu failed.",
          (httpd_host != NULL) ? httpd_host : "0.0.0.0", httpd_port);
    return NULL;
  }
#endif
  unsigned int flags = MHD_USE_THREAD_PER_CONNECTION | MHD_USE_DEBUG;
#if defined(MHD_VERSION) && MHD_VERSION >= 0x00095300
  flags |= MHD_USE_POLL_INTERNAL_THREAD;
#endif

  struct MHD_Daemon *d = MHD_start_daemon(
      flags, httpd_port, NULL, NULL, cap_http_handler, NULL,
#if defined(MHD_VERSION) && MHD_VERSION >= 0x00090000
      MHD_OPTION_LISTEN_SOCKET, fd,
#endif
      MHD_OPTION_EXTERNAL_LOGGER, cap_logger, NULL, MHD_OPTION_END);

  if (d == NULL)
    ERROR(CAP_PLUGIN ": MHD_start_daemon() failed.");

  return d;
}

static int cap_config(oconfig_item_t *ci) {
  int status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Host", child->key) == 0) {
#if defined(MHD_VERSION) && MHD_VERSION >= 0x00090000
      status = cf_util_get_string(child, &httpd_host);
#else
      ERROR(CAP_PLUGIN ": Option `Host' not supported. Please upgrade "
                       "libmicrohttpd to at least 0.9.0");
      status = -1;
#endif
    } else if (strcasecmp("Port", child->key) == 0) {
      int port = cf_util_get_port_number(child);
      if (port > 0)
        httpd_port = (unsigned short)port;
      else {
        ERROR(CAP_PLUGIN ": Wrong port number, correct range is 1-65535.");
        status = -1;
      }
    } else {
      ERROR(CAP_PLUGIN ": Unknown configuration option \"%s\".", child->key);
      status = -1;
    }

    if (status) {
      ERROR(CAP_PLUGIN ": Invalid configuration parameter \"%s\".", child->key);
      sfree(httpd_host);
      break;
    }
  }

  return status;
}

static int cap_shutdown() {
  if (httpd != NULL) {
    MHD_stop_daemon(httpd);
    httpd = NULL;
  }

  sfree(httpd_host);
  sfree(g_cap_json);
  return 0;
}

static int cap_init(void) {
  json_t *root = json_object();
  if (root == NULL) {
    ERROR(CAP_PLUGIN ": Failed to allocate json root.");
    cap_shutdown();
    return -1;
  }

  for (int i = 0; i < STATIC_ARRAY_SIZE(types_list); i++)
    if (cap_get_dmi_variables(root, types_list[i].type, types_list[i].name)) {
      json_decref(root);
      cap_shutdown();
      return -1;
    }

  g_cap_json = json_dumps(root, JSON_COMPACT);
  json_decref(root);

  if (g_cap_json == NULL) {
    ERROR(CAP_PLUGIN ": json_dumps() failed.");
    cap_shutdown();
    return -1;
  }

  httpd = cap_start_daemon();
  if (httpd == NULL) {
    cap_shutdown();
    return -1;
  }

  return 0;
}

void module_register(void) {
  plugin_register_complex_config(CAP_PLUGIN, cap_config);
  plugin_register_init(CAP_PLUGIN, cap_init);
  plugin_register_shutdown(CAP_PLUGIN, cap_shutdown);
}
