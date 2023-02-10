/**
 * collectd - src/ping.c
 * Copyright (C) 2005-2012  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 *
 *
 * collectd config
 * LoadPlugin ping
 *
 * <Plugin ping
 *     <SourceAddress 0.0.0.0>
 *         Host "example.org"
 *         Host "provider.net"
 *     </SourceAddress>
 *
 *     <SourceAddress 0.0.0.1>
 *         Host "example.org"
 *         Host "provider.net"
 *     </SourceAddress>
 * </Plugin>
 *
 **/

#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <netinet/in.h>
#include <sys/time.h>

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"
#include "utils_complain.h"

#if HAVE_NETDB_H
#include <netdb.h> /* NI_MAXHOST */
#endif

#ifdef HAVE_SYS_CAPABILITY_H
#include <sys/capability.h>
#endif

#include <oping.h>

#ifndef NI_MAXHOST
#define NI_MAXHOST 1025
#endif

#if defined(OPING_VERSION) && (OPING_VERSION >= 1003000)
#define HAVE_OPING_1_3
#endif

/*
 * Private data types
 */
struct hostlist_s {
  char *host;

  uint32_t pkg_sent;
  uint32_t pkg_recv;
  uint32_t pkg_missed;

  double latency_total;
  double latency_squared;

  struct hostlist_s *next;
};
typedef struct hostlist_s hostlist_t;

struct sourcelist_s {
  char *source;
  bool is_device;
  pthread_t ping_thread_id;
  hostlist_t *hosts;
  int ping_af;
  struct sourcelist_s *next;
};
typedef struct sourcelist_s sourcelist_t;

/*
 * Private variables
 */
static sourcelist_t *sourcelist_head;

static char *ping_data;
static int ping_ttl = PING_DEF_TTL;
static double ping_interval = 1.0;
static double ping_timeout = 0.9;
static int ping_max_missed = -1;

static pthread_mutex_t ping_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ping_cond = PTHREAD_COND_INITIALIZER;
static int ping_thread_loop;
static int ping_thread_error;

/*
 * Private functions
 */
static void sourcelist_free() {
  sourcelist_t *sl;
  sl = sourcelist_head;
  while (sl != NULL) {
    sourcelist_t *sl_next;
    hostlist_t *hl;
    hl = sl->hosts;
    while (hl != NULL) {
      hostlist_t *hl_next;
      hl_next = hl->next;
      sfree(hl->host);
      sfree(hl);
      hl = hl_next;
    }
    sfree(sl->source);
    sl_next = sl->next;
    sfree(sl);
    sl = sl_next;
  }
}


/* Assure that `ts->tv_nsec' is in the range 0 .. 999999999 */
static void time_normalize(struct timespec *ts) /* {{{ */
{
  while (ts->tv_nsec < 0) {
    if (ts->tv_sec == 0) {
      ts->tv_nsec = 0;
      return;
    }

    ts->tv_sec -= 1;
    ts->tv_nsec += 1000000000;
  }

  while (ts->tv_nsec >= 1000000000) {
    ts->tv_sec += 1;
    ts->tv_nsec -= 1000000000;
  }
} /* }}} void time_normalize */

/* Add `ts_int' to `tv_begin' and store the result in `ts_dest'. If the result
 * is larger than `tv_end', copy `tv_end' to `ts_dest' instead. */
static void time_calc(struct timespec *ts_dest, /* {{{ */
                      const struct timespec *ts_int,
                      const struct timeval *tv_begin,
                      const struct timeval *tv_end) {
  ts_dest->tv_sec = tv_begin->tv_sec + ts_int->tv_sec;
  ts_dest->tv_nsec = (tv_begin->tv_usec * 1000) + ts_int->tv_nsec;
  time_normalize(ts_dest);

  /* Assure that `(begin + interval) > end'.
   * This may seem overly complicated, but `tv_sec' is of type `time_t'
   * which may be `unsigned. *sigh* */
  if ((tv_end->tv_sec > ts_dest->tv_sec) ||
      ((tv_end->tv_sec == ts_dest->tv_sec) &&
       ((tv_end->tv_usec * 1000) > ts_dest->tv_nsec))) {
    ts_dest->tv_sec = tv_end->tv_sec;
    ts_dest->tv_nsec = 1000 * tv_end->tv_usec;
  }

  time_normalize(ts_dest);
} /* }}} void time_calc */

static int ping_dispatch_all(pingobj_t *pingobj, hostlist_t *hosts) /* {{{ */
{
  hostlist_t *hl;
  int status;

  for (pingobj_iter_t *iter = ping_iterator_get(pingobj); iter != NULL;
       iter = ping_iterator_next(iter)) { /* {{{ */
    char userhost[NI_MAXHOST];
    double latency;
    size_t param_size;

    param_size = sizeof(userhost);
    status = ping_iterator_get_info(iter,
#ifdef PING_INFO_USERNAME
                                    PING_INFO_USERNAME,
#else
                                    PING_INFO_HOSTNAME,
#endif
                                    userhost, &param_size);
    if (status != 0) {
      WARNING("ping plugin: ping_iterator_get_info failed: %s",
              ping_get_error(pingobj));
      continue;
    }

    for (hl = hosts; hl != NULL; hl = hl->next)
      if (strcmp(userhost, hl->host) == 0)
        break;

    if (hl == NULL) {
      WARNING("ping plugin: Cannot find host %s.", userhost);
      continue;
    }

    param_size = sizeof(latency);
    status = ping_iterator_get_info(iter, PING_INFO_LATENCY, (void *)&latency,
                                    &param_size);
    if (status != 0) {
      WARNING("ping plugin: ping_iterator_get_info failed: %s",
              ping_get_error(pingobj));
      continue;
    }

    hl->pkg_sent++;
    if (latency >= 0.0) {
      hl->pkg_recv++;
      hl->latency_total += latency;
      hl->latency_squared += (latency * latency);

      /* reset missed packages counter */
      hl->pkg_missed = 0;
    } else
      hl->pkg_missed++;

    /* if the host did not answer our last N packages, trigger a resolv. */
    if ((ping_max_missed >= 0) &&
        (hl->pkg_missed >= ((uint32_t)ping_max_missed))) { /* {{{ */
      /* we reset the missed package counter here, since we only want to
       * trigger a resolv every N packages and not every package _AFTER_ N
       * missed packages */
      hl->pkg_missed = 0;

      WARNING("ping plugin: host %s has not answered %d PING requests,"
              " triggering resolve",
              hl->host, ping_max_missed);

      /* we trigger the resolv simply be removeing and adding the host to our
       * ping object */
      status = ping_host_remove(pingobj, hl->host);
      if (status != 0) {
        WARNING("ping plugin: ping_host_remove (%s) failed.", hl->host);
      } else {
        status = ping_host_add(pingobj, hl->host);
        if (status != 0)
          ERROR("ping plugin: ping_host_add (%s) failed.", hl->host);
      }
    } /* }}} ping_max_missed */
  }   /* }}} for (iter) */

  return 0;
} /* }}} int ping_dispatch_all */

static void *ping_thread(void *arg) /* {{{ */
{
  struct timeval tv_begin;
  struct timeval tv_end;
  struct timespec ts_wait;
  struct timespec ts_int;

  int count;
  sourcelist_t *sl = arg;

  c_complain_t complaint = C_COMPLAIN_INIT_STATIC;

  pingobj_t *pingobj = ping_construct();
  if (pingobj == NULL) {
    ERROR("ping plugin: ping_construct failed.");
    pthread_mutex_lock(&ping_lock);
    ping_thread_error = 1;
    pthread_mutex_unlock(&ping_lock);
    return (void *)-1;
  }

  if (sl->ping_af != PING_DEF_AF) {
    if (ping_setopt(pingobj, PING_OPT_AF, &sl->ping_af) != 0)
      ERROR("ping plugin: Failed to set address family: %s",
            ping_get_error(pingobj));
  }

  if (sl->source != NULL && !sl->is_device)
    if (ping_setopt(pingobj, PING_OPT_SOURCE, (void *)sl->source) != 0)
      ERROR("ping plugin: Failed to set source address: %s",
            ping_get_error(pingobj));

#ifdef HAVE_OPING_1_3
  if (sl->source != NULL && sl->is_device)
    if (ping_setopt(pingobj, PING_OPT_DEVICE, (void *)sl->source) != 0)
      ERROR("ping plugin: Failed to set device: %s", ping_get_error(pingobj));
#endif

  ping_setopt(pingobj, PING_OPT_TIMEOUT, (void *)&ping_timeout);
  ping_setopt(pingobj, PING_OPT_TTL, (void *)&ping_ttl);

  if (ping_data != NULL)
    ping_setopt(pingobj, PING_OPT_DATA, (void *)ping_data);

  /* Add all the hosts to the ping object. */
  count = 0;
  for (hostlist_t *hl = sl->hosts; hl != NULL; hl = hl->next) {
    int tmp_status;
    tmp_status = ping_host_add(pingobj, hl->host);
    if (tmp_status != 0)
      WARNING("ping plugin: ping_host_add (%s) failed: %s", hl->host,
              ping_get_error(pingobj));
    else
      count++;
  }

  if (count == 0) {
    ERROR("ping plugin: No host could be added to ping object. Giving up.");
    pthread_mutex_lock(&ping_lock);
    ping_thread_error = 1;
    pthread_mutex_unlock(&ping_lock);
    return (void *)-1;
  }

  /* Set up `ts_int' */
  {
    double temp_sec;
    double temp_nsec;

    temp_nsec = modf(ping_interval, &temp_sec);
    ts_int.tv_sec = (time_t)temp_sec;
    ts_int.tv_nsec = (long)(temp_nsec * 1000000000L);
  }

  pthread_mutex_lock(&ping_lock);
  while (ping_thread_loop > 0) {
    bool send_successful = false;

    if (gettimeofday(&tv_begin, NULL) < 0) {
      ERROR("ping plugin: gettimeofday failed: %s", STRERRNO);
      ping_thread_error = 1;
      break;
    }

    pthread_mutex_unlock(&ping_lock);

    int status = ping_send(pingobj);
    if (status < 0) {
      c_complain(LOG_ERR, &complaint, "ping plugin: ping_send failed: %s",
                 ping_get_error(pingobj));
    } else {
      c_release(LOG_NOTICE, &complaint, "ping plugin: ping_send succeeded.");
      send_successful = true;
    }

    pthread_mutex_lock(&ping_lock);

    if (ping_thread_loop <= 0)
      break;

    if (send_successful)
      (void)ping_dispatch_all(pingobj, sl->hosts);

    if (gettimeofday(&tv_end, NULL) < 0) {
      ERROR("ping plugin: gettimeofday failed: %s", STRERRNO);
      ping_thread_error = 1;
      break;
    }

    /* Calculate the absolute time until which to wait and store it in
     * `ts_wait'. */
    time_calc(&ts_wait, &ts_int, &tv_begin, &tv_end);

    pthread_cond_timedwait(&ping_cond, &ping_lock, &ts_wait);
    if (ping_thread_loop <= 0)
      break;
  } /* while (ping_thread_loop > 0) */

  pthread_mutex_unlock(&ping_lock);
  ping_destroy(pingobj);

  return (void *)0;
} /* }}} void *ping_thread */


static int stop_thread(void) /* {{{ */
{
  int status = 0;

  pthread_mutex_lock(&ping_lock);

  if (ping_thread_loop == 0) {
    pthread_mutex_unlock(&ping_lock);
    return -1;
  }

  ping_thread_loop = 0;
  pthread_cond_broadcast(&ping_cond);
  pthread_mutex_unlock(&ping_lock);

  pthread_mutex_lock(&ping_lock);
  for (sourcelist_t *sl = sourcelist_head; sl != NULL; sl = sl->next) {
    if (sl->ping_thread_id <= 0) {
      status = pthread_join(sl->ping_thread_id, /* return = */ NULL);
      if (status != 0) {
        ERROR("ping plugin: Stopping thread failed.");
        status = -1;
      }
    }
    memset(&sl->ping_thread_id, 0, sizeof(sl->ping_thread_id));
  }
  ping_thread_error = 0;
  pthread_mutex_unlock(&ping_lock);
  return status;
} /* }}} int stop_thread */

static int start_thread(void) /* {{{ */
{
  int status;

  pthread_mutex_lock(&ping_lock);

  if (ping_thread_loop != 0) {
    pthread_mutex_unlock(&ping_lock);
    return 0;
  }

  ping_thread_loop = 1;
  ping_thread_error = 0;
  for (sourcelist_t *sl = sourcelist_head; sl != NULL; sl = sl->next) {
    status = plugin_thread_create(&sl->ping_thread_id, ping_thread,
        /* arg = */ (void *)sl, "ping");
    if (status != 0) {
      ERROR("ping plugin: Starting thread failed.");
      pthread_mutex_unlock(&ping_lock);
      stop_thread(); // stop any threads that were started.
      return -1;
    }
  }

  pthread_mutex_unlock(&ping_lock);
  return 0;
} /* }}} int start_thread */


static int ping_init(void) /* {{{ */
{
  sourcelist_t *sl;
  if (sourcelist_head == NULL) {
    NOTICE("ping plugin: No sources have been configured.");
    return -1;
  }
  sl = sourcelist_head;
  while (sl != NULL) {
    if (sl->hosts == NULL) {
      NOTICE("ping plugin: source \"%s\" doesnt have hosts configured.", sl->source);
      return -1;
    }
    sl = sl->next;
  }

  if (ping_timeout > ping_interval) {
    ping_timeout = 0.9 * ping_interval;
    WARNING("ping plugin: Timeout is greater than interval. "
            "Will use a timeout of %gs.",
            ping_timeout);
  }

#if defined(HAVE_SYS_CAPABILITY_H) && defined(CAP_NET_RAW)
  if (check_capability(CAP_NET_RAW) != 0) {
    if (getuid() == 0)
      WARNING("ping plugin: Running collectd as root, but the CAP_NET_RAW "
              "capability is missing. The plugin's read function will probably "
              "fail. Is your init system dropping capabilities?");
    else
      WARNING("ping plugin: collectd doesn't have the CAP_NET_RAW capability. "
              "If you don't want to run collectd as root, try running \"setcap "
              "cap_net_raw=ep\" on the collectd binary.");
  }
#endif

  return start_thread();
} /* }}} int ping_init */


static int ping_config_add (oconfig_item_t *ci, bool is_device) {
  sourcelist_t *sl;
  sl = calloc(1, sizeof(*sl));

  if (sl == NULL) {
    ERROR("ping plugin: calloc failed in ping_config_add.");
    return 1;
  }
  char *source = NULL;
  if (cf_util_get_string(ci, &source) != 0) {
    sfree(sl);
    ERROR("ping plugin: strdup failed: %s in ping_config_add", STRERRNO);
    return 1;
  }
  sl->source = source;
  sl->is_device = is_device;
  sl->next = sourcelist_head;
  sl->ping_af = PING_DEF_AF;
  sourcelist_head = sl;
  sl->hosts = NULL;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp(child->key, "Host") == 0) {
      hostlist_t *hl;
      char *host = NULL;
      hl = malloc(sizeof(*hl));
      if (hl == NULL) {
        ERROR("ping plugin: malloc failed: %s in ping_config_add", STRERRNO);
        sourcelist_free();
        return 1;
      }

      if (cf_util_get_string(child, &host) != 0) {
        sfree(hl);
        ERROR("ping plugin: strdup failed: %s in ping_config_add", STRERRNO);
        sourcelist_free();
        return 1;
      }

      hl->host = host;
      hl->pkg_sent = 0;
      hl->pkg_recv = 0;
      hl->pkg_missed = 0;
      hl->latency_total = 0.0;
      hl->latency_squared = 0.0;
      hl->next = sl->hosts;
      sl->hosts = hl;
    } else if (strcasecmp(child->key, "AddressFamily") == 0) {
      char *af = NULL;
      int status = cf_util_get_string(child, &af);
      if (status != 0)
        return status;

      if (strncmp(af, "any", 3) == 0) {
        sl->ping_af = AF_UNSPEC;
      } else if (strncmp(af, "ipv4", 4) == 0) {
        sl->ping_af = AF_INET;
      } else if (strncmp(af, "ipv6", 4) == 0) {
        sl->ping_af = AF_INET6;
      } else {
        WARNING("ping plugin: Ignoring invalid AddressFamily value %s", af);
      }
      free(af);
    } else {
      WARNING("ping plugin: The config option "
          "\"%s\" is not allowed in \"%s\"", child->key, ci->key);
      sourcelist_free();
      return -1;
    }
  }
  return 0;
}

static int ping_config(oconfig_item_t *ci) /* {{{ */
{

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp(child->key, "SourceAddress") == 0) {
      if (ping_config_add(child, false) != 0)
        return 1;
    }
#ifdef HAVE_OPING_1_3
    else if (strcasecmp(child->key, "Device") == 0) {
      if (ping_config_add(child, true) != 0)
        return 1;
    }
#endif
    else if (strcasecmp(child->key, "TTL") == 0) {
      int ttl;
      if (cf_util_get_int(child, &ttl) != 0)
        return 1;

      if ((ttl > 0) && (ttl <= 255))
        ping_ttl = ttl;
      else
        WARNING("ping plugin: Ignoring invalid TTL %i.", ttl);
    } else if (strcasecmp(child->key, "Interval") == 0) {
      double tmp;
      if (cf_util_get_double(child, &tmp) != 0)
        return 1;

      if (tmp > 0.0)
        ping_interval = tmp;
      else
        WARNING("ping plugin: Ignoring invalid interval %g", tmp);
    } else if (strcasecmp(child->key, "Size") == 0) {
      int packet_size;
      if (cf_util_get_int(child, &packet_size) != 0)
        return 1;
      size_t size = (size_t)(packet_size);

      /* Max IP packet size - (IPv6 + ICMP) = 65535 - (40 + 8) = 65487 */
      if (size <= 65487) {
        sfree(ping_data);
        ping_data = malloc(size + 1);
        if (ping_data == NULL) {
          ERROR("ping plugin: malloc failed.");
          return 1;
        }

        /* Note: By default oping is using constant string
         * "liboping -- ICMP ping library <http://octo.it/liboping/>"
         * which is exactly 56 bytes.
         *
         * Optimally we would follow the ping(1) behaviour, but we
         * cannot use byte 00 or start data payload at exactly same
         * location, due to oping library limitations. */
        for (size_t i = 0; i < size; i++) /* {{{ */
        {
          /* This restricts data pattern to be only composed of easily
           * printable characters, and not NUL character. */
          ping_data[i] = ('0' + i % 64);
        } /* }}} for (i = 0; i < size; i++) */
        ping_data[size] = 0;
      } else
        WARNING("ping plugin: Ignoring invalid Size %" PRIsz ".", size);
    } else if (strcasecmp(child->key, "Timeout") == 0) {
      double tmp;
      if (cf_util_get_double(child, &tmp) != 0)
        return 1;
      if (tmp > 0.0)
        ping_timeout = tmp;
      else
        WARNING("ping plugin: Ignoring invalid timeout %g", tmp);
    } else if (strcasecmp(child->key, "MaxMissed") == 0) {
      if (cf_util_get_int(child, &ping_max_missed) != 0)
        return 1;

      if (ping_max_missed < 0)
        INFO("ping plugin: MaxMissed < 0, disabled re-resolving of hosts");
    } else {
      WARNING("ping plugin: The config option "
              "\"%s\" is not allowed.", child->key);
      return -1;
    }
  }
  return 0;
} /* }}} int ping_config */

static void submit(const char *source, const char *host, const char *type, /* {{{ */
    gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;
  char *src_host = (char *)malloc(strlen(source) + strlen(host) + 2);
  if (src_host == NULL) {
    ERROR("ping plugin: malloc failed to alloc src_host in submit");
  } else {
    sstrncpy(src_host, source, strlen(source) + 1);
    strcat(src_host, "_");
    strcat(src_host, host);
    vl.values = &(value_t){.gauge = value};
    vl.values_len = 1;
    sstrncpy(vl.plugin, "ping", sizeof(vl.plugin));
    sstrncpy(vl.type_instance, src_host, sizeof(vl.type_instance));
    sstrncpy(vl.type, type, sizeof(vl.type));

    plugin_dispatch_values(&vl);
    sfree(src_host);
  }
} /* }}} void ping_submit */

static int ping_read(void) /* {{{ */
{
  if (ping_thread_error != 0) {
    ERROR("ping plugin: The ping thread had a problem. Restarting it.");

    stop_thread();
    for (sourcelist_t *sl = sourcelist_head; sl != NULL; sl = sl->next) {
      for (hostlist_t *hl = sl->hosts; hl != NULL; hl = hl->next) {
        hl->pkg_sent = 0;
        hl->pkg_recv = 0;
        hl->latency_total = 0.0;
        hl->latency_squared = 0.0;
      }
    }
    start_thread();

    return -1;
  } /* if (ping_thread_error != 0) */

  for (sourcelist_t *sl = sourcelist_head; sl != NULL; sl = sl->next) {
    for (hostlist_t *hl = sl->hosts; hl != NULL; hl = hl->next) /* {{{ */
    {
      uint32_t pkg_sent;
      uint32_t pkg_recv;
      double latency_total;
      double latency_squared;

      double latency_average;
      double latency_stddev;

      double droprate;

      /* Locking here works, because the structure of the linked list is only
       * changed during configure and shutdown. */
      pthread_mutex_lock(&ping_lock);

      pkg_sent = hl->pkg_sent;
      pkg_recv = hl->pkg_recv;
      latency_total = hl->latency_total;
      latency_squared = hl->latency_squared;

      hl->pkg_sent = 0;
      hl->pkg_recv = 0;
      hl->latency_total = 0.0;
      hl->latency_squared = 0.0;

      pthread_mutex_unlock(&ping_lock);

      /* This e. g. happens when starting up. */
      if (pkg_sent == 0) {
        DEBUG("ping plugin: No packages for host %s have been sent.", hl->host);
        continue;
      }

      /* Calculate average. Beware of division by zero. */
      if (pkg_recv == 0)
        latency_average = NAN;
      else
        latency_average = latency_total / ((double)pkg_recv);

      /* Calculate standard deviation. Beware even more of division by zero. */
      if (pkg_recv == 0)
        latency_stddev = NAN;
      else if (pkg_recv == 1)
        latency_stddev = 0.0;
      else
        latency_stddev = sqrt(((((double)pkg_recv) * latency_squared) -
              (latency_total * latency_total)) /
            ((double)(pkg_recv * (pkg_recv - 1))));

      /* Calculate drop rate. */
      droprate = ((double)(pkg_sent - pkg_recv)) / ((double)pkg_sent);

      submit(sl->source, hl->host, "ping", latency_average);
      submit(sl->source, hl->host, "ping_stddev", latency_stddev);
      submit(sl->source, hl->host, "ping_droprate", droprate);
    } /* }}} for (hl = hostlist_head; hl != NULL; hl = hl->next) */
  }

  return 0;
} /* }}} int ping_read */

static int ping_shutdown(void) /* {{{ */
{

  INFO("ping plugin: Shutting down thread.");
  if (stop_thread() < 0)
    return -1;

  sourcelist_free();

  if (ping_data != NULL) {
    free(ping_data);
    ping_data = NULL;
  }

  return 0;
} /* }}} int ping_shutdown */

void module_register(void) {
  plugin_register_complex_config("ping", ping_config);
  plugin_register_init("ping", ping_init);
  plugin_register_read("ping", ping_read);
  plugin_register_shutdown("ping", ping_shutdown);
} /* void module_register */
