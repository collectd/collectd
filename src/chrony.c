/* chrony plugin for collectd (monitoring of chrony time server daemon)
 **********************************************************************
 * Copyright (C) Claudius M Zingerli, ZSeng, 2015-2016
 *
 * Internals roughly based on the ntpd plugin
 * Some functions copied from chronyd/web (as marked)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "collectd.h"

#include "plugin.h"              /* plugin_register_*, plugin_dispatch_values */
#include "utils/common/common.h" /* auxiliary functions */

#include <chrony.h>
#include <poll.h>

#if HAVE_NETDB_H
#include <netdb.h> /* struct addrinfo */
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h> /* ntohs/ntohl */
#endif

/* AIX doesn't have MSG_DONTWAIT */
#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT MSG_NONBLOCK
#endif

#define CONFIG_KEY_HOST "Host"
#define CONFIG_KEY_PORT "Port"
#define CONFIG_KEY_TIMEOUT "Timeout"

#define URAND_DEVICE_PATH                                                      \
  "/dev/urandom" /* Used to initialize seq nr generator */
#define RAND_DEVICE_PATH                                                       \
  "/dev/random" /* Used to initialize seq nr generator (fall back) */

static const char *g_config_keys[] = {CONFIG_KEY_HOST, CONFIG_KEY_PORT,
                                      CONFIG_KEY_TIMEOUT};

static int g_config_keys_num = STATIC_ARRAY_SIZE(g_config_keys);
static int g_chrony_is_connected;
static int g_chrony_socket = -1;
static time_t g_chrony_timeout = -1;
static char *g_chrony_plugin_instance;
static char *g_chrony_host;
static char *g_chrony_port;

#define PLUGIN_NAME_SHORT "chrony"
#define PLUGIN_NAME PLUGIN_NAME_SHORT " plugin"
#define DAEMON_NAME PLUGIN_NAME_SHORT
#define CHRONY_DEFAULT_HOST "localhost"
#define CHRONY_DEFAULT_PORT "323"
#define CHRONY_DEFAULT_TIMEOUT 2

/* Return codes (collectd expects non-zero on errors) */
#define CHRONY_RC_OK 0
#define CHRONY_RC_FAIL 1

#define IPV6_STR_MAX_SIZE (8 * 4 + 7 + 1)

/*****************************************************************************/
/* Internal functions */
/*****************************************************************************/

/* connect_client code adapted from:
 * http://long.ccaba.upc.edu/long/045Guidelines/eva/ipv6.html#daytimeClient6 */
/* License granted by Eva M Castro via e-mail on 2016-02-18 under the terms of
 * GPLv3 */
static int connect_client(const char *p_hostname, const char *p_service,
                          int p_family, int p_socktype) {
  struct addrinfo *res, *ressave;
  int n, sockfd;
  char buf[64];

  /* Handle Unix domain socket */
  if (p_hostname[0] == '/')
    return chrony_open_socket(p_hostname);

  struct addrinfo ai_hints = {.ai_family = p_family, .ai_socktype = p_socktype};

  n = getaddrinfo(p_hostname, p_service, &ai_hints, &res);

  if (n < 0) {
    ERROR(PLUGIN_NAME ": getaddrinfo error:: [%s]", gai_strerror(n));
    return -1;
  }

  ressave = res;

  sockfd = -1;
  while (res) {
    fprintf(stderr, "%d\n", res->ai_family);
    switch (res->ai_family) {
      case AF_INET:
        inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr.s_addr,
                  buf, sizeof (buf));
        break;
      case AF_INET6:
        inet_ntop(AF_INET6, &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr,
                  buf + 1, sizeof (buf) - 1);
        buf[0] = '[';
        buf[strlen(buf)] = ']';
        break;
      default:
        res = res->ai_next;
        continue;
    }

    snprintf(buf + strlen(buf), sizeof (buf) - strlen(buf), ":%u", atoi(p_service));

    sockfd = chrony_open_socket(buf);
    if (sockfd >= 0)
      break;

    res = res->ai_next;
  }

  freeaddrinfo(ressave);
  return sockfd;
}

static int chrony_connect(void) {
  /* Connects to the chrony daemon */
  /* Returns 0 on success, !0 on error (check errno) */
  int socket;

  if (g_chrony_host == NULL) {
    g_chrony_host = strdup(CHRONY_DEFAULT_HOST);
    if (g_chrony_host == NULL) {
      ERROR(PLUGIN_NAME ": Error duplicating chrony host name");
      return CHRONY_RC_FAIL;
    }
  }
  if (g_chrony_port == NULL) {
    g_chrony_port = strdup(CHRONY_DEFAULT_PORT);
    if (g_chrony_port == NULL) {
      ERROR(PLUGIN_NAME ": Error duplicating chrony port string");
      return CHRONY_RC_FAIL;
    }
  }
  if (g_chrony_timeout < 0) {
    g_chrony_timeout = CHRONY_DEFAULT_TIMEOUT;
    assert(g_chrony_timeout >= 0);
  }

  DEBUG(PLUGIN_NAME ": Connecting to %s:%s", g_chrony_host, g_chrony_port);
  socket = connect_client(g_chrony_host, g_chrony_port, AF_UNSPEC, SOCK_DGRAM);
  if (socket < 0) {
    ERROR(PLUGIN_NAME ": Error connecting to daemon. Errno = %d", errno);
    return CHRONY_RC_FAIL;
  }
  DEBUG(PLUGIN_NAME ": Connected");
  g_chrony_socket = socket;

  return CHRONY_RC_OK;
}

static void chrony_push_data(const char *p_type, const char *p_type_inst,
                             double p_value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = p_value};
  vl.values_len = 1;

  /* XXX: Shall g_chrony_host/g_chrony_port be reflected in the plugin's output?
   */
  sstrncpy(vl.plugin, PLUGIN_NAME_SHORT, sizeof(vl.plugin));
  if (g_chrony_plugin_instance != NULL) {
    sstrncpy(vl.plugin_instance, g_chrony_plugin_instance,
             sizeof(vl.plugin_instance));
  }
  if (p_type != NULL)
    sstrncpy(vl.type, p_type, sizeof(vl.type));

  if (p_type_inst != NULL)
    sstrncpy(vl.type_instance, p_type_inst, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

static void push_field(chrony_session *s, const char *name,
                       const char *p_type, const char *p_type_inst, int p_is_valid) {
  struct timespec ts;
  double value;
  int index;

  index = chrony_get_field_index(s, name);
  if (index < 0) {
    ERROR(PLUGIN_NAME ": Missing field %s", name);
    return;
  }

  switch (chrony_get_field_type(s, index)) {
    case CHRONY_TYPE_UINTEGER:
      value = chrony_get_field_uinteger(s, index);
      break;
    case CHRONY_TYPE_INTEGER:
      value = chrony_get_field_integer(s, index);
      break;
    case CHRONY_TYPE_FLOAT:
      value = chrony_get_field_float(s, index);
      break;
    case CHRONY_TYPE_TIMESPEC:
      ts = chrony_get_field_timespec(s, index);
      value = ts.tv_sec + ts.tv_nsec / 1.0e9;
      break;
    default:
      ERROR(PLUGIN_NAME ": Unhandled type of field %s", name);
      return;
  }

  /* Push real value if p_is_valid is true, push NAN if p_is_valid is not true
   * (idea from ntp plugin) */
  if (!p_is_valid)
    value = NAN;

  DEBUG(PLUGIN_NAME ": %s[%s] = %f", name, p_type_inst, value);

  chrony_push_data(p_type, p_type_inst, value);
}

static chrony_err process_responses(chrony_session *s) {
  struct pollfd pfd = { .fd = chrony_get_fd(s), .events = POLLIN };
  struct timespec ts1, ts2;
  int n, timeout;
  chrony_err r;

  if (clock_gettime(CLOCK_MONOTONIC, &ts1) < 0) {
    ERROR(PLUGIN_NAME ": Could not read monotonic clock");
    return -1;
  }

  timeout = g_chrony_timeout * 1000;

  while (chrony_needs_response(s)) {
    n = poll(&pfd, 1, timeout);
    if (n < 0) {
      ERROR(PLUGIN_NAME ": poll() failed %s", strerror(errno));
      return -1;
    } else if (n == 0) {
      ERROR(PLUGIN_NAME ": No valid response received\n");
      return -1;
    }
    r = chrony_process_response(s);
    if (r != CHRONY_OK)
      return r;

    if (clock_gettime(CLOCK_MONOTONIC, &ts2) < 0) {
      ERROR(PLUGIN_NAME ": Could not read monotonic clock");
      return -1;
    }
    timeout -= (ts2.tv_sec - ts1.tv_sec) * 1000 +
      ((int32_t)ts2.tv_nsec - (int32_t)ts1.tv_nsec) / 1000000;
    if (timeout < 0)
      timeout = 0;
    ts1 = ts2;
  }

  return CHRONY_OK;
}

/*****************************************************************************/
/* Exported functions */
/*****************************************************************************/
static int chrony_config(const char *p_key, const char *p_value) {
  assert(p_key);
  assert(p_value);

  /* Parse config variables */
  if (strcasecmp(p_key, CONFIG_KEY_HOST) == 0) {
    if (g_chrony_host != NULL)
      free(g_chrony_host);

    if ((g_chrony_host = strdup(p_value)) == NULL) {
      ERROR(PLUGIN_NAME ": Error duplicating host name");
      return CHRONY_RC_FAIL;
    }
  } else {
    if (strcasecmp(p_key, CONFIG_KEY_PORT) == 0) {
      if (g_chrony_port != NULL)
        free(g_chrony_port);

      if ((g_chrony_port = strdup(p_value)) == NULL) {
        ERROR(PLUGIN_NAME ": Error duplicating port name");
        return CHRONY_RC_FAIL;
      }
    } else {
      if (strcasecmp(p_key, CONFIG_KEY_TIMEOUT) == 0) {
        time_t tosec = strtol(p_value, NULL, 0);
        g_chrony_timeout = tosec;
      } else {
        WARNING(PLUGIN_NAME ": Unknown configuration variable: %s %s", p_key,
                p_value);
        return CHRONY_RC_FAIL;
      }
    }
  }
  /* XXX: We could set g_chrony_plugin_instance here to
   * "g_chrony_host-g_chrony_port", but as multiple instances aren't yet
   * supported, we skip this for now */

  return CHRONY_RC_OK;
}

static int get_chrony_record(chrony_session *s, const char *report, int record) {
  int rc;

  rc = chrony_request_record(s, report, record);
  if (rc != CHRONY_OK)
    return rc;

  return process_responses(s);
}

static int chrony_request_daemon_stats(chrony_session *s) {
  /* Perform Tracking request */
  int rc;

  rc = get_chrony_record(s, "tracking", 0);
  if (rc != CHRONY_OK)
    return rc;

  /* Forward results to collectd-daemon */
  /* Type_instance is always 'chrony' to tag daemon-wide data */
  push_field(s, "stratum", "clock_stratum", DAEMON_NAME, 1);
  push_field(s, "reference time", "time_ref", DAEMON_NAME, 1);
  push_field(s, "current correction", "time_offset_ntp", DAEMON_NAME, 1);
  push_field(s, "last offset", "time_offset", DAEMON_NAME, 1);
  push_field(s, "frequency offset", "frequency_error", DAEMON_NAME, 1);
  push_field(s, "skew", "clock_skew_ppm", DAEMON_NAME, 1);
  push_field(s, "root delay", "root_delay", DAEMON_NAME, 1);
  push_field(s, "root dispersion", "root_dispersion", DAEMON_NAME, 1);
  push_field(s, "last update interval", "clock_last_update", DAEMON_NAME, 1);

  return CHRONY_RC_OK;
}

static int chrony_request_sources_count(chrony_session *s, unsigned int *p_count) {
  /* Requests the number of time sources from the chrony daemon */
  int rc;

  rc = chrony_request_report_number_records(s, "sources");
  if (rc != CHRONY_OK)
    return rc;

  rc = process_responses(s);
  if (rc != CHRONY_OK)
    return rc;

  *p_count = chrony_get_report_number_records(s);
  return rc;
}

static int chrony_request_source_data(chrony_session *s, int p_src_idx, char *src_addr,
                                      size_t addr_size, int *p_is_reachable) {
  /* Perform Source data request for source #p_src_idx */
  int rc, index;

  rc = get_chrony_record(s, "sources", p_src_idx);
  if (rc != CHRONY_OK)
    return rc;

  index = chrony_get_field_index(s, "address");
  if (index >= 0) {
    sstrncpy(src_addr, chrony_get_field_string(s, index), addr_size);
  } else {
    index = chrony_get_field_index(s, "reference ID");
    if (index < 0)
      return CHRONY_RC_FAIL;
    snprintf(src_addr, addr_size, "%X", (uint32_t)chrony_get_field_uinteger(s, index));
  }

  index = chrony_get_field_index(s, "reachability");
  if (index < 0)
    return CHRONY_RC_FAIL;

  /* Push NaN if source is currently not reachable */
  int is_reachable = chrony_get_field_uinteger(s, index) & 0x01;
  *p_is_reachable = is_reachable;

  /* Forward results to collectd-daemon */
  push_field(s, "stratum", "clock_stratum", src_addr, is_reachable);
  push_field(s, "state", "clock_state", src_addr, is_reachable);
  push_field(s, "mode", "clock_mode", src_addr, is_reachable);
  push_field(s, "reachability", "clock_reachability", src_addr, is_reachable);
  push_field(s, "last sample ago", "clock_last_meas", src_addr, is_reachable);
  push_field(s, "last sample offset (original)", "time_offset", src_addr, is_reachable);

  return CHRONY_RC_OK;
}

static int chrony_request_source_stats(chrony_session *s, int p_src_idx, const char *src_addr,
                                       const int *p_is_reachable) {
  /* Perform Source stats request for source #p_src_idx */
  int rc;

  rc = get_chrony_record(s, "sourcestats", p_src_idx);
  if (rc != CHRONY_OK)
    return rc;

  /* Forward results to collectd-daemon */
  push_field(s, "skew", "clock_skew_ppm", src_addr, *p_is_reachable);
  push_field(s, "residual frequency", "frequency_error", src_addr, *p_is_reachable);

  return CHRONY_RC_OK;
}

static int read_reports(chrony_session *s) {
  unsigned int n_sources;
  chrony_err rc;

  /* Get daemon stats */
  rc = chrony_request_daemon_stats(s);
  if (rc != CHRONY_RC_OK)
    return rc;

  /* Get number of time sources, then check every source for status */
  rc = chrony_request_sources_count(s, &n_sources);
  if (rc != CHRONY_RC_OK)
    return rc;

  for (unsigned int now_src = 0; now_src < n_sources; ++now_src) {
    char src_addr[IPV6_STR_MAX_SIZE] = {0};
    int is_reachable;
    rc = chrony_request_source_data(s, now_src, src_addr, sizeof(src_addr),
                                    &is_reachable);
    if (rc != CHRONY_RC_OK)
      return rc;

    rc = chrony_request_source_stats(s, now_src, src_addr, &is_reachable);
    if (rc != CHRONY_RC_OK)
      return rc;
  }

  return rc;
}

static int chrony_read(void) {
  /* collectd read callback: Perform data acquisition */
  chrony_session *s;
  int rc;

  if (g_chrony_is_connected == 0) {
    if (chrony_connect() == CHRONY_RC_OK) {
      g_chrony_is_connected = 1;
    } else {
      ERROR(PLUGIN_NAME ": Unable to connect. Errno = %d", errno);
      return CHRONY_RC_FAIL;
    }
  }

  if (chrony_init_session(&s, g_chrony_socket) != CHRONY_OK)
    return CHRONY_RC_FAIL;

  rc = read_reports(s);
  chrony_deinit_session(s);

  return rc;
}

static int chrony_shutdown(void) {
  /* Collectd shutdown callback: Free mem */
  if (g_chrony_is_connected != 0) {
    chrony_close_socket(g_chrony_socket);
    g_chrony_is_connected = 0;
  }
  if (g_chrony_host != NULL)
    sfree(g_chrony_host);

  if (g_chrony_port != NULL)
    sfree(g_chrony_port);

  if (g_chrony_plugin_instance != NULL)
    sfree(g_chrony_plugin_instance);

  return CHRONY_RC_OK;
}

void module_register(void) {
  plugin_register_config(PLUGIN_NAME_SHORT, chrony_config, g_config_keys,
                         g_config_keys_num);
  plugin_register_read(PLUGIN_NAME_SHORT, chrony_read);
  plugin_register_shutdown(PLUGIN_NAME_SHORT, chrony_shutdown);
}
