/**
 * collectd - src/hddtemp.c
 * Copyright (C) 2005,2006  Vincent Stehlé
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Sebastian Harl
 * Copyright (C) 2014       Carnegie Mellon University
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
 *   Vincent Stehlé <vincent.stehle at free.fr>
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 *   Benjamin Gilbert <bgilbert at backtick.net>
 *
 * TODO:
 *   Do a pass, some day, and spare some memory. We consume too much for now
 *   in string buffers and the like.
 *
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <assert.h>
#include <libgen.h> /* for basename */
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#if HAVE_LINUX_MAJOR_H
#include <linux/major.h>
#endif

#define HDDTEMP_DEF_HOST "127.0.0.1"
#define HDDTEMP_DEF_PORT "7634"
#define HDDTEMP_MAX_RECV_BUF (1 << 20)

static const char *config_keys[] = {"Host", "Port"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static char *hddtemp_host = NULL;
static char hddtemp_port[16];

/*
 * NAME
 *  hddtemp_query_daemon
 *
 * DESCRIPTION
 * Connect to the hddtemp daemon and receive data.
 *
 * ARGUMENTS:
 *  `buffer'            The buffer where we put the received ascii string.
 *  `buffer_size'       Size of the buffer
 *
 * RETURN VALUE:
 *   >= 0 if ok, < 0 otherwise.
 *
 * NOTES:
 *  Example of possible strings, as received from daemon:
 *    |/dev/hda|ST340014A|36|C|
 *    |/dev/hda|ST380011A|46|C||/dev/hdd|ST340016A|SLP|*|
 *
 * FIXME:
 *  we need to create a new socket each time. Is there another way?
 *  Hm, maybe we can re-use the `sockaddr' structure? -octo
 */
static char *hddtemp_query_daemon(void) {
  int fd;
  ssize_t status;

  char *buffer;
  int buffer_size;
  int buffer_fill;
  char *new_buffer;

  const char *host;
  const char *port;

  struct addrinfo *ai_list;
  int ai_return;

  host = hddtemp_host;
  if (host == NULL)
    host = HDDTEMP_DEF_HOST;

  port = hddtemp_port;
  if (strlen(port) == 0)
    port = HDDTEMP_DEF_PORT;

  struct addrinfo ai_hints = {.ai_flags = AI_ADDRCONFIG,
                              .ai_family = AF_UNSPEC,
                              .ai_protocol = IPPROTO_TCP,
                              .ai_socktype = SOCK_STREAM};

  if ((ai_return = getaddrinfo(host, port, &ai_hints, &ai_list)) != 0) {
    ERROR("hddtemp plugin: getaddrinfo (%s, %s): %s", host, port,
          (ai_return == EAI_SYSTEM) ? STRERRNO : gai_strerror(ai_return));
    return NULL;
  }

  fd = -1;
  for (struct addrinfo *ai_ptr = ai_list; ai_ptr != NULL;
       ai_ptr = ai_ptr->ai_next) {
    /* create our socket descriptor */
    fd = socket(ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
    if (fd < 0) {
      ERROR("hddtemp plugin: socket: %s", STRERRNO);
      continue;
    }

    /* connect to the hddtemp daemon */
    if (connect(fd, (struct sockaddr *)ai_ptr->ai_addr, ai_ptr->ai_addrlen)) {
      INFO("hddtemp plugin: connect (%s, %s) failed: %s", host, port, STRERRNO);
      close(fd);
      fd = -1;
      continue;
    }

    /* A socket could be opened and connecting succeeded. We're
     * done. */
    break;
  }

  freeaddrinfo(ai_list);

  if (fd < 0) {
    ERROR("hddtemp plugin: Could not connect to daemon.");
    return NULL;
  }

  /* receive data from the hddtemp daemon */
  buffer = NULL;
  buffer_size = 0;
  buffer_fill = 0;
  while (1) {
    if ((buffer_size == 0) || (buffer_fill >= buffer_size - 1)) {
      if (buffer_size == 0)
        buffer_size = 1024;
      else
        buffer_size *= 2;
      if (buffer_size > HDDTEMP_MAX_RECV_BUF) {
        WARNING("hddtemp plugin: Message from hddtemp has been "
                "truncated.");
        break;
      }
      new_buffer = realloc(buffer, buffer_size);
      if (new_buffer == NULL) {
        close(fd);
        free(buffer);
        ERROR("hddtemp plugin: Allocation failed.");
        return NULL;
      }
      buffer = new_buffer;
    }
    status = read(fd, buffer + buffer_fill, buffer_size - buffer_fill - 1);
    if (status == 0) {
      break;
    } else if (status == -1) {

      if ((errno == EAGAIN) || (errno == EINTR))
        continue;

      ERROR("hddtemp plugin: Error reading from socket: %s", STRERRNO);
      close(fd);
      free(buffer);
      return NULL;
    }
    buffer_fill += status;
  }

  if (buffer_fill == 0) {
    WARNING("hddtemp plugin: Peer has unexpectedly shut down "
            "the socket. Buffer: `%s'",
            buffer);
    close(fd);
    free(buffer);
    return NULL;
  }

  assert(buffer_fill < buffer_size);
  buffer[buffer_fill] = '\0';
  close(fd);
  return buffer;
}

static int hddtemp_config(const char *key, const char *value) {
  if (strcasecmp(key, "Host") == 0) {
    if (hddtemp_host != NULL)
      free(hddtemp_host);
    hddtemp_host = strdup(value);
  } else if (strcasecmp(key, "Port") == 0) {
    int port = (int)(atof(value));
    if ((port > 0) && (port <= 65535))
      snprintf(hddtemp_port, sizeof(hddtemp_port), "%i", port);
    else
      sstrncpy(hddtemp_port, value, sizeof(hddtemp_port));
  } else {
    return -1;
  }

  return 0;
}

static void hddtemp_submit(char *type_instance, double value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "hddtemp", sizeof(vl.plugin));
  sstrncpy(vl.type, "temperature", sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static int hddtemp_read(void) {
  char *buf;
  char *ptr;
  char *saveptr;
  char *name;
  char *model;
  char *temperature;
  char *mode;

  /* get data from daemon */
  buf = hddtemp_query_daemon();
  if (buf == NULL)
    return -1;

  /* NB: strtok_r will eat up "||" and leading "|"'s */
  ptr = buf;
  saveptr = NULL;
  while ((name = strtok_r(ptr, "|", &saveptr)) != NULL &&
         (model = strtok_r(NULL, "|", &saveptr)) != NULL &&
         (temperature = strtok_r(NULL, "|", &saveptr)) != NULL &&
         (mode = strtok_r(NULL, "|", &saveptr)) != NULL) {
    double temperature_value;

    ptr = NULL;

    /* Skip non-temperature information */
    if (mode[0] != 'C' && mode[0] != 'F')
      continue;

    name = basename(name);
    temperature_value = atof(temperature);

    /* Convert farenheit to celsius */
    if (mode[0] == 'F')
      temperature_value = (temperature_value - 32.0) * 5.0 / 9.0;

    hddtemp_submit(name, temperature_value);
  }

  free(buf);
  return 0;
} /* int hddtemp_read */

/* module_register
   Register collectd plugin. */
void module_register(void) {
  plugin_register_config("hddtemp", hddtemp_config, config_keys,
                         config_keys_num);
  plugin_register_read("hddtemp", hddtemp_read);
}
