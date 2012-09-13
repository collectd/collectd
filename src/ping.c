/**
 * collectd - src/ping.c
 * Copyright (C) 2005-2009  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <pthread.h>
#include <netinet/in.h>
#if HAVE_NETDB_H
# include <netdb.h> /* NI_MAXHOST */
#endif

#include <oping.h>

#ifndef NI_MAXHOST
# define NI_MAXHOST 1025
#endif

#if defined(OPING_VERSION) && (OPING_VERSION >= 1003000)
# define HAVE_OPING_1_3
#endif

/*
 * Private data types
 */
struct hostlist_s
{
  char *host;

  uint32_t pkg_sent;
  uint32_t pkg_recv;
  uint32_t pkg_missed;

  double latency_total;
  double latency_squared;

  struct hostlist_s *next;
};
typedef struct hostlist_s hostlist_t;

/*
 * Private variables
 */
static hostlist_t *hostlist_head = NULL;

static char  *ping_source = NULL;
#ifdef HAVE_OPING_1_3
static char  *ping_device = NULL;
#endif
static int    ping_ttl = PING_DEF_TTL;
static double ping_interval = 1.0;
static double ping_timeout = 0.9;
static int    ping_max_missed = -1;

static int             ping_thread_loop = 0;
static int             ping_thread_error = 0;
static pthread_t       ping_thread_id;
static pthread_mutex_t ping_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  ping_cond = PTHREAD_COND_INITIALIZER;

static const char *config_keys[] =
{
  "Host",
  "SourceAddress",
#ifdef HAVE_OPING_1_3
  "Device",
#endif
  "TTL",
  "Interval",
  "Timeout",
  "MaxMissed"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/*
 * Private functions
 */
/* Assure that `ts->tv_nsec' is in the range 0 .. 999999999 */
static void time_normalize (struct timespec *ts) /* {{{ */
{
  while (ts->tv_nsec < 0)
  {
    if (ts->tv_sec == 0)
    {
      ts->tv_nsec = 0;
      return;
    }

    ts->tv_sec  -= 1;
    ts->tv_nsec += 1000000000;
  }

  while (ts->tv_nsec >= 1000000000)
  {
    ts->tv_sec  += 1;
    ts->tv_nsec -= 1000000000;
  }
} /* }}} void time_normalize */

/* Add `ts_int' to `tv_begin' and store the result in `ts_dest'. If the result
 * is larger than `tv_end', copy `tv_end' to `ts_dest' instead. */
static void time_calc (struct timespec *ts_dest, /* {{{ */
    const struct timespec *ts_int,
    const struct timeval  *tv_begin,
    const struct timeval  *tv_end)
{
  ts_dest->tv_sec = tv_begin->tv_sec + ts_int->tv_sec;
  ts_dest->tv_nsec = (tv_begin->tv_usec * 1000) + ts_int->tv_nsec;
  time_normalize (ts_dest);

  /* Assure that `(begin + interval) > end'.
   * This may seem overly complicated, but `tv_sec' is of type `time_t'
   * which may be `unsigned. *sigh* */
  if ((tv_end->tv_sec > ts_dest->tv_sec)
      || ((tv_end->tv_sec == ts_dest->tv_sec)
        && ((tv_end->tv_usec * 1000) > ts_dest->tv_nsec)))
  {
    ts_dest->tv_sec = tv_end->tv_sec;
    ts_dest->tv_nsec = 1000 * tv_end->tv_usec;
  }

  time_normalize (ts_dest);
} /* }}} void time_calc */

static void *ping_thread (void *arg) /* {{{ */
{
  static pingobj_t *pingobj = NULL;

  struct timeval  tv_begin;
  struct timeval  tv_end;
  struct timespec ts_wait;
  struct timespec ts_int;

  hostlist_t *hl;
  int count;

  pthread_mutex_lock (&ping_lock);

  pingobj = ping_construct ();
  if (pingobj == NULL)
  {
    ERROR ("ping plugin: ping_construct failed.");
    ping_thread_error = 1;
    pthread_mutex_unlock (&ping_lock);
    return ((void *) -1);
  }

  if (ping_source != NULL)
    if (ping_setopt (pingobj, PING_OPT_SOURCE, (void *) ping_source) != 0)
      ERROR ("ping plugin: Failed to set source address: %s",
          ping_get_error (pingobj));

#ifdef HAVE_OPING_1_3
  if (ping_device != NULL)
    if (ping_setopt (pingobj, PING_OPT_DEVICE, (void *) ping_device) != 0)
      ERROR ("ping plugin: Failed to set device: %s",
          ping_get_error (pingobj));
#endif

  ping_setopt (pingobj, PING_OPT_TIMEOUT, (void *) &ping_timeout);
  ping_setopt (pingobj, PING_OPT_TTL, (void *) &ping_ttl);

  /* Add all the hosts to the ping object. */
  count = 0;
  for (hl = hostlist_head; hl != NULL; hl = hl->next)
  {
    int tmp_status;
    tmp_status = ping_host_add (pingobj, hl->host);
    if (tmp_status != 0)
      WARNING ("ping plugin: ping_host_add (%s) failed: %s",
          hl->host, ping_get_error (pingobj));
    else
      count++;
  }

  if (count == 0)
  {
    ERROR ("ping plugin: No host could be added to ping object. Giving up.");
    ping_thread_error = 1;
    pthread_mutex_unlock (&ping_lock);
    return ((void *) -1);
  }

  /* Set up `ts_int' */
  {
    double temp_sec;
    double temp_nsec;

    temp_nsec = modf (ping_interval, &temp_sec);
    ts_int.tv_sec  = (time_t) temp_sec;
    ts_int.tv_nsec = (long) (temp_nsec * 1000000000L);
  }

  while (ping_thread_loop > 0)
  {
    pingobj_iter_t *iter;
    int status;

    if (gettimeofday (&tv_begin, NULL) < 0)
    {
      char errbuf[1024];
      ERROR ("ping plugin: gettimeofday failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      ping_thread_error = 1;
      break;
    }

    pthread_mutex_unlock (&ping_lock);

    status = ping_send (pingobj);
    if (status < 0)
    {
      ERROR ("ping plugin: ping_send failed: %s", ping_get_error (pingobj));
      pthread_mutex_lock (&ping_lock);
      ping_thread_error = 1;
      break;
    }

    pthread_mutex_lock (&ping_lock);

    if (ping_thread_loop <= 0)
      break;

    for (iter = ping_iterator_get (pingobj);
        iter != NULL;
        iter = ping_iterator_next (iter))
    { /* {{{ */
      char userhost[NI_MAXHOST];
      double latency;
      size_t param_size;

      param_size = sizeof (userhost);
      status = ping_iterator_get_info (iter,
#ifdef PING_INFO_USERNAME
          PING_INFO_USERNAME,
#else
          PING_INFO_HOSTNAME,
#endif
          userhost, &param_size);
      if (status != 0)
      {
        WARNING ("ping plugin: ping_iterator_get_info failed: %s",
            ping_get_error (pingobj));
        continue;
      }

      for (hl = hostlist_head; hl != NULL; hl = hl->next)
        if (strcmp (userhost, hl->host) == 0)
          break;

      if (hl == NULL)
      {
        WARNING ("ping plugin: Cannot find host %s.", userhost);
        continue;
      }

      param_size = sizeof (latency);
      status = ping_iterator_get_info (iter, PING_INFO_LATENCY,
          (void *) &latency, &param_size);
      if (status != 0)
      {
        WARNING ("ping plugin: ping_iterator_get_info failed: %s",
            ping_get_error (pingobj));
        continue;
      }

      hl->pkg_sent++;
      if (latency >= 0.0)
      {
        hl->pkg_recv++;
        hl->latency_total += latency;
        hl->latency_squared += (latency * latency);

        /* reset missed packages counter */
        hl->pkg_missed = 0;
      } else
        hl->pkg_missed++;

      /* if the host did not answer our last N packages, trigger a resolv. */
      if (ping_max_missed >= 0 && hl->pkg_missed >= ping_max_missed)
      { /* {{{ */
        /* we reset the missed package counter here, since we only want to
         * trigger a resolv every N packages and not every package _AFTER_ N
         * missed packages */
        hl->pkg_missed = 0;

        WARNING ("ping plugin: host %s has not answered %d PING requests,"
          " triggering resolve", hl->host, ping_max_missed);

        /* we trigger the resolv simply be removeing and adding the host to our
         * ping object */
        status = ping_host_remove (pingobj, hl->host);
        if (status != 0)
        {
          WARNING ("ping plugin: ping_host_remove (%s) failed.", hl->host);
        }
        else
        {
          status = ping_host_add (pingobj, hl->host);
          if (status != 0)
            WARNING ("ping plugin: ping_host_add (%s) failed.", hl->host);
        }
      } /* }}} ping_max_missed */
    } /* }}} for (iter) */

    if (gettimeofday (&tv_end, NULL) < 0)
    {
      char errbuf[1024];
      ERROR ("ping plugin: gettimeofday failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      ping_thread_error = 1;
      break;
    }

    /* Calculate the absolute time until which to wait and store it in
     * `ts_wait'. */
    time_calc (&ts_wait, &ts_int, &tv_begin, &tv_end);

    status = pthread_cond_timedwait (&ping_cond, &ping_lock, &ts_wait);
    if (ping_thread_loop <= 0)
      break;
  } /* while (ping_thread_loop > 0) */

  pthread_mutex_unlock (&ping_lock);
  ping_destroy (pingobj);

  return ((void *) 0);
} /* }}} void *ping_thread */

static int start_thread (void) /* {{{ */
{
  int status;

  pthread_mutex_lock (&ping_lock);

  if (ping_thread_loop != 0)
  {
    pthread_mutex_unlock (&ping_lock);
    return (-1);
  }

  ping_thread_loop = 1;
  ping_thread_error = 0;
  status = pthread_create (&ping_thread_id, /* attr = */ NULL,
      ping_thread, /* arg = */ (void *) 0);
  if (status != 0)
  {
    ping_thread_loop = 0;
    ERROR ("ping plugin: Starting thread failed.");
    pthread_mutex_unlock (&ping_lock);
    return (-1);
  }
    
  pthread_mutex_unlock (&ping_lock);
  return (0);
} /* }}} int start_thread */

static int stop_thread (void) /* {{{ */
{
  int status;

  pthread_mutex_lock (&ping_lock);

  if (ping_thread_loop == 0)
  {
    pthread_mutex_unlock (&ping_lock);
    return (-1);
  }

  ping_thread_loop = 0;
  pthread_cond_broadcast (&ping_cond);
  pthread_mutex_unlock (&ping_lock);

  status = pthread_join (ping_thread_id, /* return = */ NULL);
  if (status != 0)
  {
    ERROR ("ping plugin: Stopping thread failed.");
    status = -1;
  }

  memset (&ping_thread_id, 0, sizeof (ping_thread_id));
  ping_thread_error = 0;

  return (status);
} /* }}} int stop_thread */

static int ping_init (void) /* {{{ */
{
  if (hostlist_head == NULL)
  {
    NOTICE ("ping plugin: No hosts have been configured.");
    return (-1);
  }

  if (ping_timeout > ping_interval)
  {
    ping_timeout = 0.9 * ping_interval;
    WARNING ("ping plugin: Timeout is greater than interval. "
        "Will use a timeout of %gs.", ping_timeout);
  }

  if (start_thread () != 0)
    return (-1);

  return (0);
} /* }}} int ping_init */

static int config_set_string (const char *name, /* {{{ */
    char **var, const char *value)
{
  char *tmp;

  tmp = strdup (value);
  if (tmp == NULL)
  {
    char errbuf[1024];
    ERROR ("ping plugin: Setting `%s' to `%s' failed: strdup failed: %s",
        name, value, sstrerror (errno, errbuf, sizeof (errbuf)));
    return (1);
  }

  if (*var != NULL)
    free (*var);
  *var = tmp;
  return (0);
} /* }}} int config_set_string */

static int ping_config (const char *key, const char *value) /* {{{ */
{
  if (strcasecmp (key, "Host") == 0)
  {
    hostlist_t *hl;
    char *host;

    hl = (hostlist_t *) malloc (sizeof (hostlist_t));
    if (hl == NULL)
    {
      char errbuf[1024];
      ERROR ("ping plugin: malloc failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      return (1);
    }

    host = strdup (value);
    if (host == NULL)
    {
      char errbuf[1024];
      sfree (hl);
      ERROR ("ping plugin: strdup failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      return (1);
    }

    hl->host = host;
    hl->pkg_sent = 0;
    hl->pkg_recv = 0;
    hl->pkg_missed = 0;
    hl->latency_total = 0.0;
    hl->latency_squared = 0.0;
    hl->next = hostlist_head;
    hostlist_head = hl;
  }
  else if (strcasecmp (key, "SourceAddress") == 0)
  {
    int status = config_set_string (key, &ping_source, value);
    if (status != 0)
      return (status);
  }
#ifdef HAVE_OPING_1_3
  else if (strcasecmp (key, "Device") == 0)
  {
    int status = config_set_string (key, &ping_device, value);
    if (status != 0)
      return (status);
  }
#endif
  else if (strcasecmp (key, "TTL") == 0)
  {
    int ttl = atoi (value);
    if ((ttl > 0) && (ttl <= 255))
      ping_ttl = ttl;
    else
      WARNING ("ping plugin: Ignoring invalid TTL %i.", ttl);
  }
  else if (strcasecmp (key, "Interval") == 0)
  {
    double tmp;

    tmp = atof (value);
    if (tmp > 0.0)
      ping_interval = tmp;
    else
      WARNING ("ping plugin: Ignoring invalid interval %g (%s)",
          tmp, value);
  }
  else if (strcasecmp (key, "Timeout") == 0)
  {
    double tmp;

    tmp = atof (value);
    if (tmp > 0.0)
      ping_timeout = tmp;
    else
      WARNING ("ping plugin: Ignoring invalid timeout %g (%s)",
          tmp, value);
  }
  else if (strcasecmp (key, "MaxMissed") == 0)
  {
    ping_max_missed = atoi (value);
    if (ping_max_missed < 0)
      INFO ("ping plugin: MaxMissed < 0, disabled re-resolving of hosts");
  }
  else
  {
    return (-1);
  }

  return (0);
} /* }}} int ping_config */

static void submit (const char *host, const char *type, /* {{{ */
    gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "ping", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
  sstrncpy (vl.type_instance, host, sizeof (vl.type_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));

  plugin_dispatch_values (&vl);
} /* }}} void ping_submit */

static int ping_read (void) /* {{{ */
{
  hostlist_t *hl;

  if (ping_thread_error != 0)
  {
    ERROR ("ping plugin: The ping thread had a problem. Restarting it.");

    stop_thread ();

    for (hl = hostlist_head; hl != NULL; hl = hl->next)
    {
      hl->pkg_sent = 0;
      hl->pkg_recv = 0;
      hl->latency_total = 0.0;
      hl->latency_squared = 0.0;
    }

    start_thread ();

    return (-1);
  } /* if (ping_thread_error != 0) */

  for (hl = hostlist_head; hl != NULL; hl = hl->next) /* {{{ */
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
    pthread_mutex_lock (&ping_lock);

    pkg_sent = hl->pkg_sent;
    pkg_recv = hl->pkg_recv;
    latency_total = hl->latency_total;
    latency_squared = hl->latency_squared;

    hl->pkg_sent = 0;
    hl->pkg_recv = 0;
    hl->latency_total = 0.0;
    hl->latency_squared = 0.0;

    pthread_mutex_unlock (&ping_lock);

    /* This e. g. happens when starting up. */
    if (pkg_sent == 0)
    {
      DEBUG ("ping plugin: No packages for host %s have been sent.",
          hl->host);
      continue;
    }

    /* Calculate average. Beware of division by zero. */
    if (pkg_recv == 0)
      latency_average = NAN;
    else
      latency_average = latency_total / ((double) pkg_recv);

    /* Calculate standard deviation. Beware even more of division by zero. */
    if (pkg_recv == 0)
      latency_stddev = NAN;
    else if (pkg_recv == 1)
      latency_stddev = 0.0;
    else
      latency_stddev = sqrt (((((double) pkg_recv) * latency_squared)
          - (latency_total * latency_total))
          / ((double) (pkg_recv * (pkg_recv - 1))));

    /* Calculate drop rate. */
    droprate = ((double) (pkg_sent - pkg_recv)) / ((double) pkg_sent);

    submit (hl->host, "ping", latency_average);
    submit (hl->host, "ping_stddev", latency_stddev);
    submit (hl->host, "ping_droprate", droprate);
  } /* }}} for (hl = hostlist_head; hl != NULL; hl = hl->next) */

  return (0);
} /* }}} int ping_read */

static int ping_shutdown (void) /* {{{ */
{
  hostlist_t *hl;

  INFO ("ping plugin: Shutting down thread.");
  if (stop_thread () < 0)
    return (-1);

  hl = hostlist_head;
  while (hl != NULL)
  {
    hostlist_t *hl_next;

    hl_next = hl->next;

    sfree (hl->host);
    sfree (hl);

    hl = hl_next;
  }

  return (0);
} /* }}} int ping_shutdown */

void module_register (void)
{
  plugin_register_config ("ping", ping_config,
      config_keys, config_keys_num);
  plugin_register_init ("ping", ping_init);
  plugin_register_read ("ping", ping_read);
  plugin_register_shutdown ("ping", ping_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
