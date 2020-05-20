/**
 * collectd - src/livestatus.c
 *
 * Copyright (C) 2019 IN2P3 Computing Centre, IN2P3, CNRS
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Remi Ferrand <remi.ferrand at cc.in2p3.fr>
 **/

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#define IO_BUFFER_SIZE 4096
#define LIVESTATUS_FIELD_SEP ';'
#define LIVESTATUS_PLUGIN_NAME "livestatus"

typedef uint64_t lcounter_t;

static const char *config_keys[] = {"LivestatusSocketFile", "OnFailureMaxRetry",
                                    "OnFailureBackOffSeconds"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

struct livestatus_s {
  char *socket_file;
  int max_retry;
  int backoff_sec;
};
typedef struct livestatus_s livestatus_t;

#define LIVESTATUS_QUERY_COLUMNS                                               \
  "cached_log_messages "                                                       \
  "connections "                                                               \
  "connections_rate "                                                          \
  "forks "                                                                     \
  "forks_rate "                                                                \
  "host_checks "                                                               \
  "host_checks_rate "                                                          \
  "livecheck_overflows "                                                       \
  "livecheck_overflows_rate "                                                  \
  "livechecks "                                                                \
  "livechecks_rate "                                                           \
  "log_messages "                                                              \
  "log_messages_rate "                                                         \
  "neb_callbacks "                                                             \
  "neb_callbacks_rate "                                                        \
  "requests "                                                                  \
  "requests_rate "                                                             \
  "service_checks "                                                            \
  "service_checks_rate"

#define LIVESTATUS_EXPECTED_FIELDS_RESP_NB 19

struct livestatus_status_s {
  int cached_log_messages;
  lcounter_t connections;
  double connections_rate;
  lcounter_t forks;
  double forks_rate;
  lcounter_t host_checks;
  double host_checks_rate;
  lcounter_t livecheck_overflows;
  double livecheck_overflows_rate;
  lcounter_t livechecks;
  double livechecks_rate;
  lcounter_t log_messages;
  double log_messages_rate;
  lcounter_t neb_callbacks;
  double neb_callbacks_rate;
  lcounter_t requests;
  double requests_rate;
  lcounter_t service_checks;
  double service_checks_rate;
};
typedef struct livestatus_status_s livestatus_status_t;

static livestatus_t livestatus_obj;

livestatus_status_t c_to_livestatus_status(const char **fields) {
  livestatus_status_t status = {
      .cached_log_messages = atoi(fields[0]),
      .connections = strtoull(fields[1], NULL, 10),
      .connections_rate = strtod(fields[2], NULL),
      .forks = strtoull(fields[3], NULL, 10),
      .forks_rate = strtod(fields[4], NULL),
      .host_checks = strtoull(fields[5], NULL, 10),
      .host_checks_rate = strtod(fields[6], NULL),
      .livecheck_overflows = strtoull(fields[7], NULL, 10),
      .livecheck_overflows_rate = strtod(fields[8], NULL),
      .livechecks = strtoull(fields[9], NULL, 10),
      .livechecks_rate = strtod(fields[10], NULL),
      .log_messages = strtoull(fields[11], NULL, 10),
      .log_messages_rate = strtod(fields[12], NULL),
      .neb_callbacks = strtoull(fields[13], NULL, 10),
      .neb_callbacks_rate = strtod(fields[14], NULL),
      .requests = strtoull(fields[15], NULL, 10),
      .requests_rate = strtod(fields[16], NULL),
      .service_checks = strtoull(fields[17], NULL, 10),
      .service_checks_rate = strtod(fields[18], NULL),
  };

  return status;
} /* livestatus_status_t c_to_livestatus_status */

// Borrowed from https://stackoverflow.com/a/29378380
static long ls_strtol_subrange(const char *s, char **endptr, int base, long min,
                               long max) {
  long y = strtol(s, endptr, base);
  if (y > max) {
    errno = ERANGE;
    return max;
  }
  if (y < min) {
    errno = ERANGE;
    return min;
  }
  return y;
} /* static long ls_strtol_subrange */

// Borrowed from https://stackoverflow.com/a/29378380
static int ls_strtoi(const char *s, char **endptr, int base) {
#if INT_MAX == LONG_MAX && INT_MIN == LONG_MIN
  return (int)strtol(s, endptr, base);
#else
  return (int)ls_strtol_subrange(s, endptr, base, INT_MIN, INT_MAX);
#endif
} /* static int ls_strtoi */

static int ls_init(void) {
  livestatus_obj.socket_file = "/var/cache/naemon/live";
  livestatus_obj.max_retry = 20;
  livestatus_obj.backoff_sec = 1;

  return 0;
} /* int ls_init */

static int ls_config(const char *key, const char *value) /* {{{ */
{
  if (strcasecmp("LivestatusSocketFile", key) == 0) {
    char *socket_file = strdup(value);
    if (socket_file == NULL) {
      ERROR("livestatus plugin: strdup failed: %s", STRERRNO);
      return -1;
    }

    livestatus_obj.socket_file = socket_file;
  }

  if (strcasecmp("OnFailureMaxRetry", key) == 0) {
    livestatus_obj.max_retry = ls_strtoi(value, NULL, 10);
    if (errno != 0) {
      ERROR("livestatus plugin: strtoi failed for OnFailureMaxRetry: %s",
            STRERRNO);
      return -1;
    }
  }

  if (strcasecmp("OnFailureBackOffSeconds", key) == 0) {
    livestatus_obj.backoff_sec = ls_strtoi(value, NULL, 10);
    if (errno != 0) {
      ERROR("livestatus plugin: strtoi failed for OnFailureBackOffSeconds: %s",
            STRERRNO);
      return -1;
    }
  }

    return 0;
  } /* static int ls_config */

static int unix_connect(const char *sockfile, int *sockfd) {
  struct sockaddr_un sun;
  int rc = -1;
  int sfd = -1;

  sfd = socket(AF_UNIX, SOCK_STREAM, 0);
  if (sfd < 0) {
    ERROR("livestatus plugin: creating client socket: %s", STRERRNO);
    return -1;
  }

  memset(&sun, 0x0, sizeof(sun));
  sun.sun_family = AF_UNIX;
  strcpy(sun.sun_path, sockfile);

  do {
    rc = connect(sfd, (struct sockaddr *)&sun, sizeof(sun));
  } while (errno == EINTR);

  if (rc < 0) {
    ERROR("livestatus plugin: connect to socket file %s: %s", sockfile,
          STRERRNO);
    return -1;
  }

  *sockfd = sfd;

  return 0;
} /* unix_connect */

static int ls_parse(const char *lresponse, livestatus_status_t *lstatus) {
  int rc = -1;
  int i = 0;
  char **fields = NULL;
  char *pchar = NULL;

  fields = malloc(sizeof(char **) * LIVESTATUS_EXPECTED_FIELDS_RESP_NB);
  if (fields == NULL) {
    ERROR("livestatus plugin: malloc failed: %s", STRERRNO);
    return -1;
  }

  for (int j = 0; j < LIVESTATUS_EXPECTED_FIELDS_RESP_NB; j++) {
    fields[j] = calloc(1, sizeof(char *) * 32);
    if (fields[j] == NULL) {
      ERROR("livestatus plugin: malloc failed: %s", STRERRNO);
      rc = -1;
      goto free_all_fields;
    }
  }

  pchar = (char *)lresponse;
  char *wstart = (char *)lresponse;
  int exit = 0;
  while (!exit) {
    if (*pchar == '\0' || *pchar == '\n') {
      exit = 1;
    } else if (*pchar != LIVESTATUS_FIELD_SEP) {
      pchar++;
      continue;
    }

    strncpy(fields[i], wstart, pchar - wstart);
    wstart = ++pchar;
    i++;

    if (!exit) {
      if (i >= LIVESTATUS_EXPECTED_FIELDS_RESP_NB) {
        ERROR("livestatus plugin: too many fields in livestatus output");
        rc = -2;
        goto free_all_fields;
      }
    }
  }

  if (i < LIVESTATUS_EXPECTED_FIELDS_RESP_NB) {
    ERROR("livestatus plugin: not enough fields in livestatus output");
    rc = -3;
    goto free_all_fields;
  }

  *lstatus = c_to_livestatus_status((const char **)fields);

  rc = 0;

free_all_fields:
  for (int j = 0; j < LIVESTATUS_EXPECTED_FIELDS_RESP_NB; j++) {
    if (fields[j] != NULL) {
      free(fields[j]);
      fields[j] = NULL;
    }
  }

  free(fields);
  fields = NULL;

  return rc;
} /* int ls_parse */

static int ls_read_parse(const int sockfd, livestatus_status_t *lstatus) {
  ssize_t bread = -1;
  char buffer[IO_BUFFER_SIZE];

  do {
    memset(buffer, 0x0, sizeof(buffer));
    bread = read((int)sockfd, buffer, IO_BUFFER_SIZE - 1);
  } while (errno == EINTR);

  if (bread < 0) {
    ERROR("livestatus plugin: reading from socket: %s", STRERRNO);
    return -1;
  }

  buffer[IO_BUFFER_SIZE - 1] = '\0';

  return ls_parse((const char *)buffer, lstatus);
} /* int ls_read_parse */

static int ls_send_request(const int sockfd) {
  int written = -1;
  char request[512];

  memset(request, 0x0, sizeof(request));

  snprintf(request, sizeof(request) - 1, "GET status\nColumns: %s\n\n",
           LIVESTATUS_QUERY_COLUMNS);

  do {
    written = write(sockfd, request, sizeof(request));
  } while (errno == EINTR);

  if (written < 0) {
    ERROR("livestatus plugin: writing to socket: %s", STRERRNO);
    return -1;
  }

  if (shutdown(sockfd, SHUT_WR) < 0) {
    ERROR("livestatus plugin: closing socket write: %s", STRERRNO);
    return -2;
  }

  return 0;
} /* int ls_send_request */

static value_list_t ls_collectd_init_vl(void) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values_len = 1;
  sstrncpy(vl.plugin, LIVESTATUS_PLUGIN_NAME, sizeof(vl.plugin));

  return vl;
} /* value_list_t ls_collectd_init_vl */

static int ls_collectd_dispatch_count(gauge_t value,
                                      const char *plugin_instance) {
  value_list_t vl = ls_collectd_init_vl();

  sstrncpy(vl.type, "count", sizeof(vl.type));
  sstrncpy(vl.plugin_instance, (char *)plugin_instance,
           sizeof(vl.plugin_instance));
  vl.values = &(value_t){.gauge = value};

  return plugin_dispatch_values(&vl);
} /* int ls_collectd_dispatch_count */

static int ls_collectd_dispatch_counter(counter_t value,
                                        const char *plugin_instance) {
  value_list_t vl = ls_collectd_init_vl();

  sstrncpy(vl.type, "counter", sizeof(vl.type));
  sstrncpy(vl.plugin_instance, (char *)plugin_instance,
           sizeof(vl.plugin_instance));
  vl.values = &(value_t){.counter = value};

  return plugin_dispatch_values(&vl);
} /* int ls_collectd_dispatch_counter */

#define L_DISPATCH(f, v, plinst)                                               \
  if (f(v, plinst) < 0) {                                                      \
    ERROR("livestatus plugin: fail to dispatch value for " plinst);            \
    rc = -1;                                                                   \
  }

static int ls_collectd_dispatch(const livestatus_status_t *status) {
  int rc = 0;

  L_DISPATCH(ls_collectd_dispatch_count, status->cached_log_messages,
             "cached_log_messages");
  L_DISPATCH(ls_collectd_dispatch_counter, status->connections, "connections");
  L_DISPATCH(ls_collectd_dispatch_count, status->connections_rate,
             "connections_rate");
  L_DISPATCH(ls_collectd_dispatch_counter, status->forks, "forks");
  L_DISPATCH(ls_collectd_dispatch_count, status->forks_rate, "forks_rate");
  L_DISPATCH(ls_collectd_dispatch_counter, status->host_checks, "host_checks");
  L_DISPATCH(ls_collectd_dispatch_count, status->host_checks_rate,
             "host_checks_rate");
  L_DISPATCH(ls_collectd_dispatch_counter, status->livecheck_overflows,
             "livecheck_overflows");
  L_DISPATCH(ls_collectd_dispatch_count, status->livecheck_overflows_rate,
             "livecheck_overflows_rate");
  L_DISPATCH(ls_collectd_dispatch_counter, status->livechecks, "livechecks");
  L_DISPATCH(ls_collectd_dispatch_count, status->livechecks_rate,
             "livechecks_rate");
  L_DISPATCH(ls_collectd_dispatch_counter, status->log_messages,
             "log_messages");
  L_DISPATCH(ls_collectd_dispatch_count, status->log_messages_rate,
             "log_messages_rate");
  L_DISPATCH(ls_collectd_dispatch_counter, status->neb_callbacks,
             "neb_callbacks");
  L_DISPATCH(ls_collectd_dispatch_count, status->neb_callbacks_rate,
             "neb_callbacks_rate");
  L_DISPATCH(ls_collectd_dispatch_counter, status->requests, "requests");
  L_DISPATCH(ls_collectd_dispatch_count, status->requests_rate,
             "requests_rate");
  L_DISPATCH(ls_collectd_dispatch_counter, status->service_checks,
             "service_checks");
  L_DISPATCH(ls_collectd_dispatch_count, status->service_checks_rate,
             "service_checks_rate");

  return rc;
} /* int ls_collectd_dispatch */

static int ls_read(void) /* {{{ */
{
  int connected = 0;
  int sockfd = -1;
  int rc = -1;
  int attempt = 0;
  livestatus_status_t lstatus;

  while (attempt < livestatus_obj.max_retry) {
    rc = unix_connect(livestatus_obj.socket_file, &sockfd);
    if (rc < 0) {
      WARNING("livestatus plugin: fail to connect to livestatus on attempt %d "
              "/ %d, will retry after backoff",
              attempt, livestatus_obj.max_retry);
      attempt++;
      sleep(livestatus_obj.backoff_sec);
      continue;
    }
    connected = 1;
    break;
  }

  if (!connected) {
    ERROR("livestatus plugin: fail to connect to livestatus");
    rc = -1;
    goto close_sock;
  }

  rc = ls_send_request(sockfd);
  if (rc < 0) {
    ERROR("livestatus plugin: sending livestatus request");
    goto close_sock;
  }

  memset(&lstatus, 0x0, sizeof(lstatus));

  rc = ls_read_parse(sockfd, &lstatus);
  if (rc < 0) {
    ERROR("livestatus plugin: reading or parsing livestatus response: %s",
          STRERRNO);
    goto close_sock;
  }

  rc = ls_collectd_dispatch(&lstatus);
  if (rc < 0) {
    ERROR("livestatus plugin: dispatching values to collectd");
    goto close_sock;
  }

close_sock:
  if (sockfd > 0) {
    close(sockfd);
    sockfd = -1;
  }

  return rc;
} /* static int ls_read */

void module_register(void) {
  plugin_register_config("livestatus", ls_config, config_keys, config_keys_num);
  plugin_register_init("livestatus", ls_init);
  plugin_register_read("livestatus", ls_read);
} /* void module_register */
