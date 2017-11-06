/**
 * collectd - src/memcached.c, based on src/hddtemp.c
 * Copyright (C) 2007       Antony Dovgal
 * Copyright (C) 2007-2012  Florian Forster
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2009       Franck Lombardi
 * Copyright (C) 2012       Nicolas Szalay
 * Copyright (C) 2017       Pavel Rochnyak
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
 *   Pavel Rochnyak <pavel2000 ngs.ru>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/un.h>

#include <poll.h>

#define MEMCACHED_DEF_HOST "127.0.0.1"
#define MEMCACHED_DEF_PORT "11211"
#define MEMCACHED_CONNECT_TIMEOUT 10000
#define MEMCACHED_IO_TIMEOUT 5000

struct prev_s {
  derive_t hits;
  derive_t gets;
  derive_t incr_hits;
  derive_t incr_misses;
  derive_t decr_hits;
  derive_t decr_misses;
};

typedef struct prev_s prev_t;

struct memcached_s {
  char *name;
  char *host;
  char *socket;
  char *connhost;
  char *connport;
  int fd;
  prev_t prev;
};
typedef struct memcached_s memcached_t;

static _Bool memcached_have_instances = 0;

static void memcached_free(void *arg) {
  memcached_t *st = arg;
  if (st == NULL)
    return;

  if (st->fd >= 0) {
    shutdown(st->fd, SHUT_RDWR);
    close(st->fd);
    st->fd = -1;
  }

  sfree(st->name);
  sfree(st->host);
  sfree(st->socket);
  sfree(st->connhost);
  sfree(st->connport);
  sfree(st);
}

static int memcached_connect_unix(memcached_t *st) {
  struct sockaddr_un serv_addr = {0};

  serv_addr.sun_family = AF_UNIX;
  sstrncpy(serv_addr.sun_path, st->socket, sizeof(serv_addr.sun_path));

  /* create our socket descriptor */
  int fd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0) {
    ERROR("memcached plugin: memcached_connect_unix: socket(2) failed: %s",
          STRERRNO);
    return -1;
  }

  /* connect to the memcached daemon */
  int status = connect(fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr));
  if (status != 0) {
    shutdown(fd, SHUT_RDWR);
    close(fd);
    return -1;
  }

  /* switch to non-blocking mode */
  int flags = fcntl(fd, F_GETFL);
  status = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
  if (status != 0) {
    close(fd);
    return -1;
  }

  return fd;
} /* int memcached_connect_unix */

static int memcached_connect_inet(memcached_t *st) {
  struct addrinfo *ai_list;
  int fd = -1;

  struct addrinfo ai_hints = {.ai_family = AF_UNSPEC,
                              .ai_flags = AI_ADDRCONFIG,
                              .ai_socktype = SOCK_STREAM};

  int status = getaddrinfo(st->connhost, st->connport, &ai_hints, &ai_list);
  if (status != 0) {
    ERROR("memcached plugin: memcached_connect_inet: "
          "getaddrinfo(%s,%s) failed: %s",
          st->connhost, st->connport,
          (status == EAI_SYSTEM) ? STRERRNO : gai_strerror(status));
    return -1;
  }

  for (struct addrinfo *ai_ptr = ai_list; ai_ptr != NULL;
       ai_ptr = ai_ptr->ai_next) {
    /* create our socket descriptor */
    fd = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
    if (fd < 0) {
      WARNING("memcached plugin: memcached_connect_inet: "
              "socket(2) failed: %s",
              STRERRNO);
      continue;
    }

    /* switch socket to non-blocking mode */
    int flags = fcntl(fd, F_GETFL);
    status = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    if (status != 0) {
      close(fd);
      fd = -1;
      continue;
    }

    /* connect to the memcached daemon */
    status = (int)connect(fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    if (status != 0 && errno != EINPROGRESS) {
      shutdown(fd, SHUT_RDWR);
      close(fd);
      fd = -1;
      continue;
    }

    /* Wait until connection establishes */
    struct pollfd pollfd = {
        .fd = fd, .events = POLLOUT,
    };
    do
      status = poll(&pollfd, 1, MEMCACHED_CONNECT_TIMEOUT);
    while (status < 0 && errno == EINTR);
    if (status <= 0) {
      close(fd);
      fd = -1;
      continue;
    }

    /* Check if all is good */
    int socket_error;
    status = getsockopt(fd, SOL_SOCKET, SO_ERROR, (void *)&socket_error,
                        &(socklen_t){sizeof(socket_error)});
    if (status != 0 || socket_error != 0) {
      close(fd);
      fd = -1;
      continue;
    }
    /* A socket is opened and connection succeeded. We're done. */
    break;
  }

  freeaddrinfo(ai_list);
  return fd;
} /* int memcached_connect_inet */

static void memcached_connect(memcached_t *st) {
  if (st->fd >= 0)
    return;

  if (st->socket != NULL)
    st->fd = memcached_connect_unix(st);
  else
    st->fd = memcached_connect_inet(st);

  if (st->fd >= 0)
    INFO("memcached plugin: Instance \"%s\": connection established.",
         st->name);
}

static int memcached_query_daemon(char *buffer, size_t buffer_size,
                                  memcached_t *st) {
  int status;
  size_t buffer_fill;

  memcached_connect(st);
  if (st->fd < 0) {
    ERROR("memcached plugin: Instance \"%s\" could not connect to daemon.",
          st->name);
    return -1;
  }

  struct pollfd pollfd = {
      .fd = st->fd, .events = POLLOUT,
  };

  do
    status = poll(&pollfd, 1, MEMCACHED_IO_TIMEOUT);
  while (status < 0 && errno == EINTR);

  if (status <= 0) {
    ERROR("memcached plugin: poll() failed for write() call.");
    close(st->fd);
    st->fd = -1;
    return -1;
  }

  status = (int)swrite(st->fd, "stats\r\n", strlen("stats\r\n"));
  if (status != 0) {
    ERROR("memcached plugin: Instance \"%s\": write(2) failed: %s", st->name,
          STRERRNO);
    shutdown(st->fd, SHUT_RDWR);
    close(st->fd);
    st->fd = -1;
    return -1;
  }

  /* receive data from the memcached daemon */
  memset(buffer, 0, buffer_size);

  buffer_fill = 0;
  pollfd.events = POLLIN;
  while (1) {
    do
      status = poll(&pollfd, 1, MEMCACHED_IO_TIMEOUT);
    while (status < 0 && errno == EINTR);

    if (status <= 0) {
      ERROR("memcached plugin: Instance \"%s\": Timeout reading from socket",
            st->name);
      close(st->fd);
      st->fd = -1;
      return -1;
    }

    do
      status = (int)recv(st->fd, buffer + buffer_fill,
                         buffer_size - buffer_fill, /* flags = */ 0);
    while (status < 0 && errno == EINTR);

    char const end_token[5] = {'E', 'N', 'D', '\r', '\n'};
    if (status < 0) {

      if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
        continue;

      ERROR("memcached plugin: Instance \"%s\": Error reading from socket: %s",
            st->name, STRERRNO);
      shutdown(st->fd, SHUT_RDWR);
      close(st->fd);
      st->fd = -1;
      return -1;
    }

    buffer_fill += (size_t)status;
    if (buffer_fill > buffer_size) {
      buffer_fill = buffer_size;
      WARNING("memcached plugin: Instance \"%s\": Message was truncated.",
              st->name);
      shutdown(st->fd, SHUT_RDWR);
      close(st->fd);
      st->fd = -1;
      break;
    }

    /* If buffer ends in end_token, we have all the data. */
    if (memcmp(buffer + buffer_fill - sizeof(end_token), end_token,
               sizeof(end_token)) == 0)
      break;
  } /* while (recv) */

  status = 0;
  if (buffer_fill == 0) {
    WARNING("memcached plugin: Instance \"%s\": No data returned by memcached.",
            st->name);
    status = -1;
  }

  return status;
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

static gauge_t calculate_ratio_percent(derive_t part, derive_t total,
                                       derive_t *prev_part,
                                       derive_t *prev_total) {
  if ((*prev_part == 0) || (*prev_total == 0) || (part < *prev_part) ||
      (total < *prev_total)) {
    *prev_part = part;
    *prev_total = total;
    return NAN;
  }

  derive_t num = part - *prev_part;
  derive_t denom = total - *prev_total;

  *prev_part = part;
  *prev_total = total;

  if (denom == 0)
    return NAN;

  if (num == 0)
    return 0;

  return 100.0 * (gauge_t)num / (gauge_t)denom;
}

static gauge_t calculate_ratio_percent2(derive_t part1, derive_t part2,
                                        derive_t *prev1, derive_t *prev2) {
  if ((*prev1 == 0) || (*prev2 == 0) || (part1 < *prev1) || (part2 < *prev2)) {
    *prev1 = part1;
    *prev2 = part2;
    return NAN;
  }

  derive_t num = part1 - *prev1;
  derive_t denom = part2 - *prev2 + num;

  *prev1 = part1;
  *prev2 = part2;

  if (denom == 0)
    return NAN;

  if (num == 0)
    return 0;

  return 100.0 * (gauge_t)num / (gauge_t)denom;
}

static int memcached_read(user_data_t *user_data) {
  char buf[4096];
  char *fields[3];
  char *line;

  derive_t bytes_used = 0;
  derive_t bytes_total = 0;
  derive_t get_hits = 0;
  derive_t cmd_get = 0;
  derive_t incr_hits = 0;
  derive_t incr_misses = 0;
  derive_t decr_hits = 0;
  derive_t decr_misses = 0;
  derive_t rusage_user = 0;
  derive_t rusage_syst = 0;
  derive_t octets_rx = 0;
  derive_t octets_tx = 0;

  memcached_t *st = user_data->data;
  prev_t *prev = &st->prev;

  /* get data from daemon */
  if (memcached_query_daemon(buf, sizeof(buf), st) < 0) {
    return -1;
  }

#define FIELD_IS(cnst)                                                         \
  (((sizeof(cnst) - 1) == name_len) && (strcmp(cnst, fields[1]) == 0))

  char *ptr = buf;
  char *saveptr = NULL;
  while ((line = strtok_r(ptr, "\n\r", &saveptr)) != NULL) {
    ptr = NULL;

    if (strsplit(line, fields, 3) != 3)
      continue;

    int name_len = strlen(fields[1]);
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
      /* Convert to useconds */
      rusage_user = atof(fields[2]) * 1000000;
    } else if (FIELD_IS("rusage_system")) {
      rusage_syst = atof(fields[2]) * 1000000;
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
      bytes_used = atoll(fields[2]);
    } else if (FIELD_IS("limit_maxbytes")) {
      bytes_total = atoll(fields[2]);
    }

    /*
     * Connections
     */
    else if (FIELD_IS("curr_connections")) {
      submit_gauge("memcached_connections", "current", atof(fields[2]), st);
    } else if (FIELD_IS("listen_disabled_num")) {
      submit_derive("total_events", "listen_disabled", atoll(fields[2]), st);
    }
    /*
     * Total number of connections opened since the server started running
     * Report this as connection rate.
     */
    else if (FIELD_IS("total_connections")) {
      submit_derive("connections", "opened", atoll(fields[2]), st);
    }

    /*
     * Commands
     */
    else if ((name_len > 4) && (strncmp(fields[1], "cmd_", 4) == 0)) {
      const char *name = fields[1] + 4;
      submit_derive("memcached_command", name, atoll(fields[2]), st);
      if (strcmp(name, "get") == 0)
        cmd_get = atoll(fields[2]);
    }

    /*
     * Increment/Decrement
     */
    else if (FIELD_IS("incr_misses")) {
      incr_misses = atoll(fields[2]);
      submit_derive("memcached_ops", "incr_misses", incr_misses, st);
    } else if (FIELD_IS("incr_hits")) {
      incr_hits = atoll(fields[2]);
      submit_derive("memcached_ops", "incr_hits", incr_hits, st);
    } else if (FIELD_IS("decr_misses")) {
      decr_misses = atoll(fields[2]);
      submit_derive("memcached_ops", "decr_misses", decr_misses, st);
    } else if (FIELD_IS("decr_hits")) {
      decr_hits = atoll(fields[2]);
      submit_derive("memcached_ops", "decr_hits", decr_hits, st);
    }

    /*
     * Operations on the cache:
     * - get hits/misses
     * - delete hits/misses
     * - evictions
     */
    else if (FIELD_IS("get_hits")) {
      get_hits = atoll(fields[2]);
      submit_derive("memcached_ops", "hits", get_hits, st);
    } else if (FIELD_IS("get_misses")) {
      submit_derive("memcached_ops", "misses", atoll(fields[2]), st);
    } else if (FIELD_IS("evictions")) {
      submit_derive("memcached_ops", "evictions", atoll(fields[2]), st);
    } else if (FIELD_IS("delete_hits")) {
      submit_derive("memcached_ops", "delete_hits", atoll(fields[2]), st);
    } else if (FIELD_IS("delete_misses")) {
      submit_derive("memcached_ops", "delete_misses", atoll(fields[2]), st);
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

  if ((bytes_total > 0) && (bytes_used <= bytes_total))
    submit_gauge2("df", "cache", bytes_used, bytes_total - bytes_used, st);

  if ((rusage_user != 0) || (rusage_syst != 0))
    submit_derive2("ps_cputime", NULL, rusage_user, rusage_syst, st);

  if ((octets_rx != 0) || (octets_tx != 0))
    submit_derive2("memcached_octets", NULL, octets_rx, octets_tx, st);

  if ((cmd_get != 0) && (get_hits != 0)) {
    gauge_t ratio =
        calculate_ratio_percent(get_hits, cmd_get, &prev->hits, &prev->gets);
    submit_gauge("percent", "hitratio", ratio, st);
  }

  if ((incr_hits != 0) && (incr_misses != 0)) {
    gauge_t ratio = calculate_ratio_percent2(
        incr_hits, incr_misses, &prev->incr_hits, &prev->incr_misses);
    submit_gauge("percent", "incr_hitratio", ratio, st);
    submit_derive("memcached_ops", "incr", incr_hits + incr_misses, st);
  }

  if ((decr_hits != 0) && (decr_misses != 0)) {
    gauge_t ratio = calculate_ratio_percent2(
        decr_hits, decr_misses, &prev->decr_hits, &prev->decr_misses);
    submit_gauge("percent", "decr_hitratio", ratio, st);
    submit_derive("memcached_ops", "decr", decr_hits + decr_misses, st);
  }

  return 0;
} /* int memcached_read */

static int memcached_set_defaults(memcached_t *st) {
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
        return ENOMEM;

      if ((strcmp("127.0.0.1", st->host) == 0) ||
          (strcmp("localhost", st->host) == 0))
        sfree(st->host);
    } else {
      st->connhost = strdup(MEMCACHED_DEF_HOST);
      if (st->connhost == NULL)
        return ENOMEM;
    }
  }

  if (st->connport == NULL) {
    st->connport = strdup(MEMCACHED_DEF_PORT);
    if (st->connport == NULL)
      return ENOMEM;
  }

  assert(st->connhost != NULL);
  assert(st->connport != NULL);

  st->prev.hits = 0;
  st->prev.gets = 0;
  st->prev.incr_hits = 0;
  st->prev.incr_misses = 0;
  st->prev.decr_hits = 0;
  st->prev.decr_misses = 0;

  return 0;
} /* int memcached_set_defaults */

static int memcached_add_read_callback(memcached_t *st) {
  char callback_name[3 * DATA_MAX_NAME_LEN];

  if (memcached_set_defaults(st) != 0) {
    memcached_free(st);
    return -1;
  }

  snprintf(callback_name, sizeof(callback_name), "memcached/%s",
           (st->name != NULL) ? st->name : "__legacy__");

  return plugin_register_complex_read(
      /* group = */ "memcached",
      /* name      = */ callback_name,
      /* callback  = */ memcached_read,
      /* interval  = */ 0,
      &(user_data_t){
          .data = st, .free_func = memcached_free,
      });
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
  int status = 0;

  /* Disable automatic generation of default instance in the init callback. */
  memcached_have_instances = 1;

  memcached_t *st = calloc(1, sizeof(*st));
  if (st == NULL) {
    ERROR("memcached plugin: calloc failed.");
    return ENOMEM;
  }

  st->name = NULL;
  st->host = NULL;
  st->socket = NULL;
  st->connhost = NULL;
  st->connport = NULL;

  st->fd = -1;

  if (strcasecmp(ci->key, "Instance") == 0)
    status = cf_util_get_string(ci, &st->name);

  if (status != 0) {
    sfree(st);
    return status;
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

  if (status != 0) {
    memcached_free(st);
    return -1;
  }

  return memcached_add_read_callback(st);
} /* int config_add_instance */

static int memcached_config(oconfig_item_t *ci) {
  _Bool have_instance_block = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Instance", child->key) == 0) {
      config_add_instance(child);
      have_instance_block = 1;
    } else if (!have_instance_block) {
      /* Non-instance option: Assume legacy configuration (without <Instance />
       * blocks) and call config_add_instance() with the <Plugin /> block. */
      return config_add_instance(ci);
    } else
      WARNING("memcached plugin: The configuration option "
              "\"%s\" is not allowed here. Did you "
              "forget to add an <Instance /> block "
              "around the configuration?",
              child->key);
  } /* for (ci->children) */

  return 0;
} /* int memcached_config */

static int memcached_init(void) {

  if (memcached_have_instances)
    return 0;

  /* No instances were configured, lets start a default instance. */
  memcached_t *st = calloc(1, sizeof(*st));
  if (st == NULL)
    return ENOMEM;
  st->name = NULL;
  st->host = NULL;
  st->socket = NULL;
  st->connhost = NULL;
  st->connport = NULL;

  st->fd = -1;

  int status = memcached_add_read_callback(st);
  if (status == 0)
    memcached_have_instances = 1;

  return status;
} /* int memcached_init */

void module_register(void) {
  plugin_register_complex_config("memcached", memcached_config);
  plugin_register_init("memcached", memcached_init);
}
