/**
 * collectd - src/memcached.c, based on src/hddtemp.c
 * Copyright (C) 2007       Antony Dovgal
 * Copyright (C) 2007-2012  Florian Forster
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2009       Franck Lombardi
 * Copyright (C) 2012       Nicolas Szalay
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Antony Dovgal <tony at daylessday dot org>
 *   Florian octo Forster <octo at collectd.org>
 *   Doug MacEachern <dougm at hyperic.com>
 *   Franck Lombardi
 *   Nicolas Szalay
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>

#define MEMCACHED_DEF_HOST "127.0.0.1"
#define MEMCACHED_DEF_PORT "11211"

struct memcached_s {
  char *name;
  char *host;
  char *socket;
  char *connhost;
  char *connport;
};
typedef struct memcached_s memcached_t;

static _Bool memcached_have_instances = 0;

static void memcached_free(void *arg) {
  memcached_t *st = arg;
  if (st == NULL)
    return;

  sfree(st->name);
  sfree(st->host);
  sfree(st->socket);
  sfree(st->connhost);
  sfree(st->connport);
  sfree(st);
}

static int memcached_connect_unix(memcached_t *st) {
  struct sockaddr_un serv_addr = {0};
  int fd;

  serv_addr.sun_family = AF_UNIX;
  sstrncpy(serv_addr.sun_path, st->socket, sizeof(serv_addr.sun_path));

  /* create our socket descriptor */
  fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    char errbuf[1024];
    ERROR("memcached plugin: memcached_connect_unix: socket(2) failed: %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    return (-1);
  }

  /* connect to the memcached daemon */
  int status = connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (status != 0) {
    shutdown(fd, SHUT_RDWR);
    close(fd);
    fd = -1;
  }

  return (fd);
} /* int memcached_connect_unix */

static int memcached_connect_inet(memcached_t *st) {
  struct addrinfo *ai_list;
  int status;
  int fd = -1;

  struct addrinfo ai_hints = {.ai_family = AF_UNSPEC,
                              .ai_flags = AI_ADDRCONFIG,
                              .ai_socktype = SOCK_STREAM};

  status = getaddrinfo(st->connhost, st->connport, &ai_hints, &ai_list);
  if (status != 0) {
    char errbuf[1024];
    ERROR("memcached plugin: memcached_connect_inet: "
          "getaddrinfo(%s,%s) failed: %s",
          st->connhost, st->connport,
          (status == EAI_SYSTEM) ? sstrerror(errno, errbuf, sizeof(errbuf))
                                 : gai_strerror(status));
    return (-1);
  }

  for (struct addrinfo *ai_ptr = ai_list; ai_ptr != NULL;
       ai_ptr = ai_ptr->ai_next) {
    /* create our socket descriptor */
    fd = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
    if (fd < 0) {
      char errbuf[1024];
      WARNING("memcached plugin: memcached_connect_inet: "
              "socket(2) failed: %s",
              sstrerror(errno, errbuf, sizeof(errbuf)));
      continue;
    }

    /* connect to the memcached daemon */
    status = (int)connect(fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    if (status != 0) {
      shutdown(fd, SHUT_RDWR);
      close(fd);
      fd = -1;
      continue;
    }

    /* A socket could be opened and connecting succeeded. We're done. */
    break;
  }

  freeaddrinfo(ai_list);
  return (fd);
} /* int memcached_connect_inet */

static int memcached_connect(memcached_t *st) {
  if (st->socket != NULL)
    return (memcached_connect_unix(st));
  else
    return (memcached_connect_inet(st));
}

static int memcached_query_daemon(char *buffer, size_t buffer_size,
                                  memcached_t *st) {
  int fd, status;
  size_t buffer_fill;

  fd = memcached_connect(st);
  if (fd < 0) {
    ERROR("memcached plugin: Instance \"%s\" could not connect to daemon.",
          st->name);
    return -1;
  }

  status = (int)swrite(fd, "stats\r\n", strlen("stats\r\n"));
  if (status != 0) {
    char errbuf[1024];
    ERROR("memcached plugin: write(2) failed: %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return (-1);
  }

  /* receive data from the memcached daemon */
  memset(buffer, 0, buffer_size);

  buffer_fill = 0;
  while ((status = (int)recv(fd, buffer + buffer_fill,
                             buffer_size - buffer_fill, /* flags = */ 0)) !=
         0) {
    char const end_token[5] = {'E', 'N', 'D', '\r', '\n'};
    if (status < 0) {
      char errbuf[1024];

      if ((errno == EAGAIN) || (errno == EINTR))
        continue;

      ERROR("memcached: Error reading from socket: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
      shutdown(fd, SHUT_RDWR);
      close(fd);
      return (-1);
    }

    buffer_fill += (size_t)status;
    if (buffer_fill > buffer_size) {
      buffer_fill = buffer_size;
      WARNING("memcached plugin: Message was truncated.");
      break;
    }

    /* If buffer ends in end_token, we have all the data. */
    if (memcmp(buffer + buffer_fill - sizeof(end_token), end_token,
               sizeof(end_token)) == 0)
      break;
  } /* while (recv) */

  status = 0;
  if (buffer_fill == 0) {
    WARNING("memcached plugin: No data returned by memcached.");
    status = -1;
  }

  shutdown(fd, SHUT_RDWR);
  close(fd);
  return (status);
} /* int memcached_query_daemon */

static void memcached_init_vl(value_list_t *vl, memcached_t const *st) {
  sstrncpy(vl->plugin, "memcached", sizeof(vl->plugin));
  if (st->host != NULL)
    sstrncpy(vl->host, st->host, sizeof(vl->host));
  if (st->name != NULL)
    sstrncpy(vl->plugin_instance, st->name, sizeof(vl->plugin_instance));
}

static void submit_derive(const char *type, const char *type_inst,
                          derive_t value, memcached_t *st) {
  value_list_t vl = VALUE_LIST_INIT;

  memcached_init_vl(&vl, st);
  vl.values = &(value_t){.derive = value};
  vl.values_len = 1;
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_inst != NULL)
    sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void submit_derive2(const char *type, const char *type_inst,
                           derive_t value0, derive_t value1, memcached_t *st) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {
      {.derive = value0}, {.derive = value1},
  };

  memcached_init_vl(&vl, st);
  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_inst != NULL)
    sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void submit_gauge(const char *type, const char *type_inst, gauge_t value,
                         memcached_t *st) {
  value_list_t vl = VALUE_LIST_INIT;

  memcached_init_vl(&vl, st);
  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_inst != NULL)
    sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void submit_gauge2(const char *type, const char *type_inst,
                          gauge_t value0, gauge_t value1, memcached_t *st) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {
      {.gauge = value0}, {.gauge = value1},
  };

  memcached_init_vl(&vl, st);
  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_inst != NULL)
    sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static int memcached_read(user_data_t *user_data) {
  char buf[4096];
  char *fields[3];
  char *ptr;
  char *line;
  char *saveptr;
  int fields_num;

  gauge_t bytes_used = NAN;
  gauge_t bytes_total = NAN;
  gauge_t hits = NAN;
  gauge_t gets = NAN;
  gauge_t incr_hits = NAN;
  derive_t incr = 0;
  gauge_t decr_hits = NAN;
  derive_t decr = 0;
  derive_t rusage_user = 0;
  derive_t rusage_syst = 0;
  derive_t octets_rx = 0;
  derive_t octets_tx = 0;

  memcached_t *st;
  st = user_data->data;

  /* get data from daemon */
  if (memcached_query_daemon(buf, sizeof(buf), st) < 0) {
    return -1;
  }

#define FIELD_IS(cnst)                                                         \
  (((sizeof(cnst) - 1) == name_len) && (strcmp(cnst, fields[1]) == 0))

  ptr = buf;
  saveptr = NULL;
  while ((line = strtok_r(ptr, "\n\r", &saveptr)) != NULL) {
    int name_len;

    ptr = NULL;

    fields_num = strsplit(line, fields, 3);
    if (fields_num != 3)
      continue;

    name_len = strlen(fields[1]);
    if (name_len == 0)
      continue;

    /*
     * For an explanation on these fields please refer to
     * <https://github.com/memcached/memcached/blob/master/doc/protocol.txt>
     */

    /*
     * CPU time consumed by the memcached process
     */
    if (FIELD_IS("rusage_user")) {
      rusage_user = atoll(fields[2]);
    } else if (FIELD_IS("rusage_system")) {
      rusage_syst = atoll(fields[2]);
    }

    /*
     * Number of threads of this instance
     */
    else if (FIELD_IS("threads")) {
      submit_gauge2("ps_count", NULL, NAN, atof(fields[2]), st);
    }

    /*
     * Number of items stored
     */
    else if (FIELD_IS("curr_items")) {
      submit_gauge("memcached_items", "current", atof(fields[2]), st);
    }

    /*
     * Number of bytes used and available (total - used)
     */
    else if (FIELD_IS("bytes")) {
      bytes_used = atof(fields[2]);
    } else if (FIELD_IS("limit_maxbytes")) {
      bytes_total = atof(fields[2]);
    }

    /*
     * Connections
     */
    else if (FIELD_IS("curr_connections")) {
      submit_gauge("memcached_connections", "current", atof(fields[2]), st);
    } else if (FIELD_IS("listen_disabled_num")) {
      submit_derive("connections", "listen_disabled", atof(fields[2]), st);
    }

    /*
     * Commands
     */
    else if ((name_len > 4) && (strncmp(fields[1], "cmd_", 4) == 0)) {
      const char *name = fields[1] + 4;
      submit_derive("memcached_command", name, atoll(fields[2]), st);
      if (strcmp(name, "get") == 0)
        gets = atof(fields[2]);
    }

    /*
     * Increment/Decrement
     */
    else if (FIELD_IS("incr_misses")) {
      derive_t incr_count = atoll(fields[2]);
      submit_derive("memcached_ops", "incr_misses", incr_count, st);
      incr += incr_count;
    } else if (FIELD_IS("incr_hits")) {
      derive_t incr_count = atoll(fields[2]);
      submit_derive("memcached_ops", "incr_hits", incr_count, st);
      incr_hits = atof(fields[2]);
      incr += incr_count;
    } else if (FIELD_IS("decr_misses")) {
      derive_t decr_count = atoll(fields[2]);
      submit_derive("memcached_ops", "decr_misses", decr_count, st);
      decr += decr_count;
    } else if (FIELD_IS("decr_hits")) {
      derive_t decr_count = atoll(fields[2]);
      submit_derive("memcached_ops", "decr_hits", decr_count, st);
      decr_hits = atof(fields[2]);
      decr += decr_count;
    }

    /*
     * Operations on the cache, i. e. cache hits, cache misses and evictions of
     * items
     */
    else if (FIELD_IS("get_hits")) {
      submit_derive("memcached_ops", "hits", atoll(fields[2]), st);
      hits = atof(fields[2]);
    } else if (FIELD_IS("get_misses")) {
      submit_derive("memcached_ops", "misses", atoll(fields[2]), st);
    } else if (FIELD_IS("evictions")) {
      submit_derive("memcached_ops", "evictions", atoll(fields[2]), st);
    }

    /*
     * Network traffic
     */
    else if (FIELD_IS("bytes_read")) {
      octets_rx = atoll(fields[2]);
    } else if (FIELD_IS("bytes_written")) {
      octets_tx = atoll(fields[2]);
    }
  } /* while ((line = strtok_r (ptr, "\n\r", &saveptr)) != NULL) */

  if (!isnan(bytes_used) && !isnan(bytes_total) && (bytes_used <= bytes_total))
    submit_gauge2("df", "cache", bytes_used, bytes_total - bytes_used, st);

  if ((rusage_user != 0) || (rusage_syst != 0))
    submit_derive2("ps_cputime", NULL, rusage_user, rusage_syst, st);

  if ((octets_rx != 0) || (octets_tx != 0))
    submit_derive2("memcached_octets", NULL, octets_rx, octets_tx, st);

  if (!isnan(gets) && !isnan(hits)) {
    gauge_t rate = NAN;

    if (gets != 0.0)
      rate = 100.0 * hits / gets;

    submit_gauge("percent", "hitratio", rate, st);
  }

  if (!isnan(incr_hits) && incr != 0) {
    gauge_t incr_rate = 100.0 * incr_hits / incr;
    submit_gauge("percent", "incr_hitratio", incr_rate, st);
    submit_derive("memcached_ops", "incr", incr, st);
  }

  if (!isnan(decr_hits) && decr != 0) {
    gauge_t decr_rate = 100.0 * decr_hits / decr;
    submit_gauge("percent", "decr_hitratio", decr_rate, st);
    submit_derive("memcached_ops", "decr", decr, st);
  }

  return 0;
} /* int memcached_read */

static int memcached_add_read_callback(memcached_t *st) {
  char callback_name[3 * DATA_MAX_NAME_LEN];
  int status;

  ssnprintf(callback_name, sizeof(callback_name), "memcached/%s",
            (st->name != NULL) ? st->name : "__legacy__");

  /* If no <Address> used then:
   * - Connect to the destination specified by <Host>, if present.
   *   If not, use the default address.
   * - Use the default hostname (set st->host to NULL), if
   *    - Legacy mode is used (no configuration options at all), or
   *    - "Host" option is not provided, or
   *    - "Host" option is set to "localhost" or "127.0.0.1".
   *
   * If <Address> used then host may be set to "localhost" or "127.0.0.1"
   * explicitly.
   */
  if (st->connhost == NULL) {
    if (st->host) {
      st->connhost = strdup(st->host);
      if (st->connhost == NULL)
        return (ENOMEM);

      if ((strcmp("127.0.0.1", st->host) == 0) ||
          (strcmp("localhost", st->host) == 0))
        sfree(st->host);
    } else {
      st->connhost = strdup(MEMCACHED_DEF_HOST);
      if (st->connhost == NULL)
        return (ENOMEM);
    }
  }

  if (st->connport == NULL) {
    st->connport = strdup(MEMCACHED_DEF_PORT);
    if (st->connport == NULL)
      return (ENOMEM);
  }

  assert(st->connhost != NULL);
  assert(st->connport != NULL);

  status = plugin_register_complex_read(
      /* group = */ "memcached",
      /* name      = */ callback_name,
      /* callback  = */ memcached_read,
      /* interval  = */ 0, &(user_data_t){
                               .data = st, .free_func = memcached_free,
                           });

  return (status);
} /* int memcached_add_read_callback */

/* Configuration handling functiions
 * <Plugin memcached>
 *   <Instance "instance_name">
 *     Host foo.zomg.com
 *     Address 1.2.3.4
 *     Port "1234"
 *   </Instance>
 * </Plugin>
 */
static int config_add_instance(oconfig_item_t *ci) {
  memcached_t *st;
  int status = 0;

  /* Disable automatic generation of default instance in the init callback. */
  memcached_have_instances = 1;

  st = calloc(1, sizeof(*st));
  if (st == NULL) {
    ERROR("memcached plugin: calloc failed.");
    return (ENOMEM);
  }

  st->name = NULL;
  st->host = NULL;
  st->socket = NULL;
  st->connhost = NULL;
  st->connport = NULL;

  if (strcasecmp(ci->key, "Instance") == 0)
    status = cf_util_get_string(ci, &st->name);

  if (status != 0) {
    sfree(st);
    return (status);
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Socket", child->key) == 0)
      status = cf_util_get_string(child, &st->socket);
    else if (strcasecmp("Host", child->key) == 0)
      status = cf_util_get_string(child, &st->host);
    else if (strcasecmp("Address", child->key) == 0)
      status = cf_util_get_string(child, &st->connhost);
    else if (strcasecmp("Port", child->key) == 0)
      status = cf_util_get_service(child, &st->connport);
    else {
      WARNING("memcached plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status == 0)
    status = memcached_add_read_callback(st);

  if (status != 0) {
    memcached_free(st);
    return (-1);
  }

  return (0);
}

static int memcached_config(oconfig_item_t *ci) {
  int status = 0;
  _Bool have_instance_block = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Instance", child->key) == 0) {
      config_add_instance(child);
      have_instance_block = 1;
    } else if (!have_instance_block) {
      /* Non-instance option: Assume legacy configuration (without <Instance />
       * blocks) and call config_add_instance() with the <Plugin /> block. */
      return (config_add_instance(ci));
    } else
      WARNING("memcached plugin: The configuration option "
              "\"%s\" is not allowed here. Did you "
              "forget to add an <Instance /> block "
              "around the configuration?",
              child->key);
  } /* for (ci->children) */

  return (status);
}

static int memcached_init(void) {
  memcached_t *st;
  int status;

  if (memcached_have_instances)
    return (0);

  /* No instances were configured, lets start a default instance. */
  st = calloc(1, sizeof(*st));
  if (st == NULL)
    return (ENOMEM);
  st->name = NULL;
  st->host = NULL;
  st->socket = NULL;
  st->connhost = NULL;
  st->connport = NULL;

  status = memcached_add_read_callback(st);
  if (status == 0)
    memcached_have_instances = 1;
  else
    memcached_free(st);

  return (status);
} /* int memcached_init */

void module_register(void) {
  plugin_register_complex_config("memcached", memcached_config);
  plugin_register_init("memcached", memcached_init);
}
