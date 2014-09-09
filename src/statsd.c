/**
 * collectd - src/statsd.c
 * Copyright (C) 2013       Florian octo Forster
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
 */

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "utils_avltree.h"
#include "utils_complain.h"
#include "utils_latency.h"

#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

/* AIX doesn't have MSG_DONTWAIT */
#ifndef MSG_DONTWAIT
#  define MSG_DONTWAIT MSG_NONBLOCK
#endif

#ifndef STATSD_DEFAULT_NODE
# define STATSD_DEFAULT_NODE NULL
#endif

#ifndef STATSD_DEFAULT_SERVICE
# define STATSD_DEFAULT_SERVICE "8125"
#endif

enum metric_type_e
{
  STATSD_COUNTER,
  STATSD_TIMER,
  STATSD_GAUGE,
  STATSD_SET
};
typedef enum metric_type_e metric_type_t;

struct statsd_metric_s
{
  metric_type_t type;
  double value;
  latency_counter_t *latency;
  c_avl_tree_t *set;
  unsigned long updates_num;
};
typedef struct statsd_metric_s statsd_metric_t;

static c_avl_tree_t   *metrics_tree = NULL;
static pthread_mutex_t metrics_lock = PTHREAD_MUTEX_INITIALIZER;

static pthread_t network_thread;
static _Bool     network_thread_running = 0;
static _Bool     network_thread_shutdown = 0;

static char *conf_node = NULL;
static char *conf_service = NULL;

static _Bool conf_delete_counters = 0;
static _Bool conf_delete_timers   = 0;
static _Bool conf_delete_gauges   = 0;
static _Bool conf_delete_sets     = 0;

static double *conf_timer_percentile = NULL;
static size_t  conf_timer_percentile_num = 0;

static _Bool conf_timer_lower     = 0;
static _Bool conf_timer_upper     = 0;
static _Bool conf_timer_sum       = 0;
static _Bool conf_timer_count     = 0;

/* Must hold metrics_lock when calling this function. */
static statsd_metric_t *statsd_metric_lookup_unsafe (char const *name, /* {{{ */
    metric_type_t type)
{
  char key[DATA_MAX_NAME_LEN + 2];
  char *key_copy;
  statsd_metric_t *metric;
  int status;

  switch (type)
  {
    case STATSD_COUNTER: key[0] = 'c'; break;
    case STATSD_TIMER:   key[0] = 't'; break;
    case STATSD_GAUGE:   key[0] = 'g'; break;
    case STATSD_SET:     key[0] = 's'; break;
    default: return (NULL);
  }

  key[1] = ':';
  sstrncpy (&key[2], name, sizeof (key) - 2);

  status = c_avl_get (metrics_tree, key, (void *) &metric);
  if (status == 0)
    return (metric);

  key_copy = strdup (key);
  if (key_copy == NULL)
  {
    ERROR ("statsd plugin: strdup failed.");
    return (NULL);
  }

  metric = malloc (sizeof (*metric));
  if (metric == NULL)
  {
    ERROR ("statsd plugin: malloc failed.");
    sfree (key_copy);
    return (NULL);
  }
  memset (metric, 0, sizeof (*metric));

  metric->type = type;
  metric->latency = NULL;
  metric->set = NULL;

  status = c_avl_insert (metrics_tree, key_copy, metric);
  if (status != 0)
  {
    ERROR ("statsd plugin: c_avl_insert failed.");
    sfree (key_copy);
    sfree (metric);
    return (NULL);
  }

  return (metric);
} /* }}} statsd_metric_lookup_unsafe */

static int statsd_metric_set (char const *name, double value, /* {{{ */
    metric_type_t type)
{
  statsd_metric_t *metric;

  pthread_mutex_lock (&metrics_lock);

  metric = statsd_metric_lookup_unsafe (name, type);
  if (metric == NULL)
  {
    pthread_mutex_unlock (&metrics_lock);
    return (-1);
  }

  metric->value = value;
  metric->updates_num++;

  pthread_mutex_unlock (&metrics_lock);

  return (0);
} /* }}} int statsd_metric_set */

static int statsd_metric_add (char const *name, double delta, /* {{{ */
    metric_type_t type)
{
  statsd_metric_t *metric;

  pthread_mutex_lock (&metrics_lock);

  metric = statsd_metric_lookup_unsafe (name, type);
  if (metric == NULL)
  {
    pthread_mutex_unlock (&metrics_lock);
    return (-1);
  }

  metric->value += delta;
  metric->updates_num++;

  pthread_mutex_unlock (&metrics_lock);

  return (0);
} /* }}} int statsd_metric_add */

static int statsd_parse_value (char const *str, value_t *ret_value) /* {{{ */
{
  char *endptr = NULL;

  ret_value->gauge = (gauge_t) strtod (str, &endptr);
  if ((str == endptr) || ((endptr != NULL) && (*endptr != 0)))
    return (-1);

  return (0);
} /* }}} int statsd_parse_value */

static int statsd_handle_counter (char const *name, /* {{{ */
    char const *value_str,
    char const *extra)
{
  value_t value;
  value_t scale;
  int status;

  if ((extra != NULL) && (extra[0] != '@'))
    return (-1);

  scale.gauge = 1.0;
  if (extra != NULL)
  {
    status = statsd_parse_value (extra + 1, &scale);
    if (status != 0)
      return (status);

    if (!isfinite (scale.gauge) || (scale.gauge <= 0.0) || (scale.gauge > 1.0))
      return (-1);
  }

  value.gauge = 1.0;
  status = statsd_parse_value (value_str, &value);
  if (status != 0)
    return (status);

  return (statsd_metric_add (name, (double) (value.gauge / scale.gauge),
        STATSD_COUNTER));
} /* }}} int statsd_handle_counter */

static int statsd_handle_gauge (char const *name, /* {{{ */
    char const *value_str)
{
  value_t value;
  int status;

  value.gauge = 0;
  status = statsd_parse_value (value_str, &value);
  if (status != 0)
    return (status);

  if ((value_str[0] == '+') || (value_str[0] == '-'))
    return (statsd_metric_add (name, (double) value.gauge, STATSD_GAUGE));
  else
    return (statsd_metric_set (name, (double) value.gauge, STATSD_GAUGE));
} /* }}} int statsd_handle_gauge */

static int statsd_handle_timer (char const *name, /* {{{ */
    char const *value_str,
    char const *extra)
{
  statsd_metric_t *metric;
  value_t value_ms;
  value_t scale;
  cdtime_t value;
  int status;

  if ((extra != NULL) && (extra[0] != '@'))
    return (-1);

  scale.gauge = 1.0;
  if (extra != NULL)
  {
    status = statsd_parse_value (extra + 1, &scale);
    if (status != 0)
      return (status);

    if (!isfinite (scale.gauge) || (scale.gauge <= 0.0) || (scale.gauge > 1.0))
      return (-1);
  }

  value_ms.derive = 0;
  status = statsd_parse_value (value_str, &value_ms);
  if (status != 0)
    return (status);

  value = MS_TO_CDTIME_T (value_ms.gauge / scale.gauge);

  pthread_mutex_lock (&metrics_lock);

  metric = statsd_metric_lookup_unsafe (name, STATSD_TIMER);
  if (metric == NULL)
  {
    pthread_mutex_unlock (&metrics_lock);
    return (-1);
  }

  if (metric->latency == NULL)
    metric->latency = latency_counter_create ();
  if (metric->latency == NULL)
  {
    pthread_mutex_unlock (&metrics_lock);
    return (-1);
  }

  latency_counter_add (metric->latency, value);
  metric->updates_num++;

  pthread_mutex_unlock (&metrics_lock);
  return (0);
} /* }}} int statsd_handle_timer */

static int statsd_handle_set (char const *name, /* {{{ */
    char const *set_key_orig)
{
  statsd_metric_t *metric = NULL;
  char *set_key;
  int status;

  pthread_mutex_lock (&metrics_lock);

  metric = statsd_metric_lookup_unsafe (name, STATSD_SET);
  if (metric == NULL)
  {
    pthread_mutex_unlock (&metrics_lock);
    return (-1);
  }

  /* Make sure metric->set exists. */
  if (metric->set == NULL)
    metric->set = c_avl_create ((void *) strcmp);

  if (metric->set == NULL)
  {
    pthread_mutex_unlock (&metrics_lock);
    ERROR ("statsd plugin: c_avl_create failed.");
    return (-1);
  }

  set_key = strdup (set_key_orig);
  if (set_key == NULL)
  {
    pthread_mutex_unlock (&metrics_lock);
    ERROR ("statsd plugin: strdup failed.");
    return (-1);
  }

  status = c_avl_insert (metric->set, set_key, /* value = */ NULL);
  if (status < 0)
  {
    pthread_mutex_unlock (&metrics_lock);
    if (status < 0)
      ERROR ("statsd plugin: c_avl_insert (\"%s\") failed with status %i.",
          set_key, status);
    sfree (set_key);
    return (-1);
  }
  else if (status > 0) /* key already exists */
  {
    sfree (set_key);
  }

  metric->updates_num++;

  pthread_mutex_unlock (&metrics_lock);
  return (0);
} /* }}} int statsd_handle_set */

static int statsd_parse_line (char *buffer) /* {{{ */
{
  char *name = buffer;
  char *value;
  char *type;
  char *extra;

  type = strchr (name, '|');
  if (type == NULL)
    return (-1);
  *type = 0;
  type++;

  value = strrchr (name, ':');
  if (value == NULL)
    return (-1);
  *value = 0;
  value++;

  extra = strchr (type, '|');
  if (extra != NULL)
  {
    *extra = 0;
    extra++;
  }

  if (strcmp ("c", type) == 0)
    return (statsd_handle_counter (name, value, extra));
  else if (strcmp ("ms", type) == 0)
    return (statsd_handle_timer (name, value, extra));

  /* extra is only valid for counters and timers */
  if (extra != NULL)
    return (-1);

  if (strcmp ("g", type) == 0)
    return (statsd_handle_gauge (name, value));
  else if (strcmp ("s", type) == 0)
    return (statsd_handle_set (name, value));
  else
    return (-1);
} /* }}} void statsd_parse_line */

static void statsd_parse_buffer (char *buffer) /* {{{ */
{
  while (buffer != NULL)
  {
    char orig[64];
    char *next;
    int status;

    next = strchr (buffer, '\n');
    if (next != NULL)
    {
      *next = 0;
      next++;
    }

    if (*buffer == 0)
    {
      buffer = next;
      continue;
    }

    sstrncpy (orig, buffer, sizeof (orig));

    status = statsd_parse_line (buffer);
    if (status != 0)
      ERROR ("statsd plugin: Unable to parse line: \"%s\"", orig);

    buffer = next;
  }
} /* }}} void statsd_parse_buffer */

static void statsd_network_read (int fd) /* {{{ */
{
  char buffer[4096];
  size_t buffer_size;
  ssize_t status;

  status = recv (fd, buffer, sizeof (buffer), /* flags = */ MSG_DONTWAIT);
  if (status < 0)
  {
    char errbuf[1024];

    if ((errno == EAGAIN) || (errno == EWOULDBLOCK))
      return;

    ERROR ("statsd plugin: recv(2) failed: %s",
        sstrerror (errno, errbuf, sizeof (errbuf)));
    return;
  }

  buffer_size = (size_t) status;
  if (buffer_size >= sizeof (buffer))
    buffer_size = sizeof (buffer) - 1;
  buffer[buffer_size] = 0;

  statsd_parse_buffer (buffer);
} /* }}} void statsd_network_read */

static int statsd_network_init (struct pollfd **ret_fds, /* {{{ */
    size_t *ret_fds_num)
{
  struct pollfd *fds = NULL;
  size_t fds_num = 0;

  struct addrinfo ai_hints;
  struct addrinfo *ai_list = NULL;
  struct addrinfo *ai_ptr;
  int status;

  char const *node = (conf_node != NULL) ? conf_node : STATSD_DEFAULT_NODE;
  char const *service = (conf_service != NULL)
    ? conf_service : STATSD_DEFAULT_SERVICE;

  memset (&ai_hints, 0, sizeof (ai_hints));
  ai_hints.ai_flags = AI_PASSIVE;
#ifdef AI_ADDRCONFIG
  ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
  ai_hints.ai_family = AF_UNSPEC;
  ai_hints.ai_socktype = SOCK_DGRAM;

  status = getaddrinfo (node, service, &ai_hints, &ai_list);
  if (status != 0)
  {
    ERROR ("statsd plugin: getaddrinfo (\"%s\", \"%s\") failed: %s",
        node, service, gai_strerror (status));
    return (status);
  }

  for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
  {
    int fd;
    struct pollfd *tmp;

    char dbg_node[NI_MAXHOST];
    char dbg_service[NI_MAXSERV];

    fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
    if (fd < 0)
    {
      char errbuf[1024];
      ERROR ("statsd plugin: socket(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      continue;
    }

    getnameinfo (ai_ptr->ai_addr, ai_ptr->ai_addrlen,
        dbg_node, sizeof (dbg_node), dbg_service, sizeof (dbg_service),
        NI_DGRAM | NI_NUMERICHOST | NI_NUMERICSERV);
    DEBUG ("statsd plugin: Trying to bind to [%s]:%s ...", dbg_node, dbg_service);

    status = bind (fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    if (status != 0)
    {
      char errbuf[1024];
      ERROR ("statsd plugin: bind(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      close (fd);
      continue;
    }

    tmp = realloc (fds, sizeof (*fds) * (fds_num + 1));
    if (tmp == NULL)
    {
      ERROR ("statsd plugin: realloc failed.");
      continue;
    }
    fds = tmp;
    tmp = fds + fds_num;
    fds_num++;

    memset (tmp, 0, sizeof (*tmp));
    tmp->fd = fd;
    tmp->events = POLLIN | POLLPRI;
  }

  freeaddrinfo (ai_list);

  if (fds_num == 0)
  {
    ERROR ("statsd plugin: Unable to create listening socket for [%s]:%s.",
        (node != NULL) ? node : "::", service);
    return (ENOENT);
  }

  *ret_fds = fds;
  *ret_fds_num = fds_num;
  return (0);
} /* }}} int statsd_network_init */

static void *statsd_network_thread (void *args) /* {{{ */
{
  struct pollfd *fds = NULL;
  size_t fds_num = 0;
  int status;
  size_t i;

  status = statsd_network_init (&fds, &fds_num);
  if (status != 0)
  {
    ERROR ("statsd plugin: Unable to open listening sockets.");
    pthread_exit ((void *) 0);
  }

  while (!network_thread_shutdown)
  {
    status = poll (fds, (nfds_t) fds_num, /* timeout = */ -1);
    if (status < 0)
    {
      char errbuf[1024];

      if ((errno == EINTR) || (errno == EAGAIN))
        continue;

      ERROR ("statsd plugin: poll(2) failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      break;
    }

    for (i = 0; i < fds_num; i++)
    {
      if ((fds[i].revents & (POLLIN | POLLPRI)) == 0)
        continue;

      statsd_network_read (fds[i].fd);
      fds[i].revents = 0;
    }
  } /* while (!network_thread_shutdown) */

  /* Clean up */
  for (i = 0; i < fds_num; i++)
    close (fds[i].fd);
  sfree (fds);

  return ((void *) 0);
} /* }}} void *statsd_network_thread */

static int statsd_config_timer_percentile (oconfig_item_t *ci) /* {{{ */
{
  double percent = NAN;
  double *tmp;
  int status;

  status = cf_util_get_double (ci, &percent);
  if (status != 0)
    return (status);

  if ((percent <= 0.0) || (percent >= 100))
  {
    ERROR ("statsd plugin: The value for \"%s\" must be between 0 and 100, "
        "exclusively.", ci->key);
    return (ERANGE);
  }

  tmp = realloc (conf_timer_percentile,
      sizeof (*conf_timer_percentile) * (conf_timer_percentile_num + 1));
  if (tmp == NULL)
  {
    ERROR ("statsd plugin: realloc failed.");
    return (ENOMEM);
  }
  conf_timer_percentile = tmp;
  conf_timer_percentile[conf_timer_percentile_num] = percent;
  conf_timer_percentile_num++;

  return (0);
} /* }}} int statsd_config_timer_percentile */

static int statsd_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Host", child->key) == 0)
      cf_util_get_string (child, &conf_node);
    else if (strcasecmp ("Port", child->key) == 0)
      cf_util_get_service (child, &conf_service);
    else if (strcasecmp ("DeleteCounters", child->key) == 0)
      cf_util_get_boolean (child, &conf_delete_counters);
    else if (strcasecmp ("DeleteTimers", child->key) == 0)
      cf_util_get_boolean (child, &conf_delete_timers);
    else if (strcasecmp ("DeleteGauges", child->key) == 0)
      cf_util_get_boolean (child, &conf_delete_gauges);
    else if (strcasecmp ("DeleteSets", child->key) == 0)
      cf_util_get_boolean (child, &conf_delete_sets);
    else if (strcasecmp ("TimerLower", child->key) == 0)
      cf_util_get_boolean (child, &conf_timer_lower);
    else if (strcasecmp ("TimerUpper", child->key) == 0)
      cf_util_get_boolean (child, &conf_timer_upper);
    else if (strcasecmp ("TimerSum", child->key) == 0)
      cf_util_get_boolean (child, &conf_timer_sum);
    else if (strcasecmp ("TimerCount", child->key) == 0)
      cf_util_get_boolean (child, &conf_timer_count);
    else if (strcasecmp ("TimerPercentile", child->key) == 0)
      statsd_config_timer_percentile (child);
    else
      ERROR ("statsd plugin: The \"%s\" config option is not valid.",
          child->key);
  }

  return (0);
} /* }}} int statsd_config */

static int statsd_init (void) /* {{{ */
{
  pthread_mutex_lock (&metrics_lock);
  if (metrics_tree == NULL)
    metrics_tree = c_avl_create ((void *) strcmp);

  if (!network_thread_running)
  {
    int status;

    status = pthread_create (&network_thread,
        /* attr = */ NULL,
        statsd_network_thread,
        /* args = */ NULL);
    if (status != 0)
    {
      char errbuf[1024];
      pthread_mutex_unlock (&metrics_lock);
      ERROR ("statsd plugin: pthread_create failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      return (status);
    }
  }
  network_thread_running = 1;

  pthread_mutex_unlock (&metrics_lock);

  return (0);
} /* }}} int statsd_init */

/* Must hold metrics_lock when calling this function. */
static int statsd_metric_clear_set_unsafe (statsd_metric_t *metric) /* {{{ */
{
  void *key;
  void *value;

  if ((metric == NULL) || (metric->type != STATSD_SET))
    return (EINVAL);

  if (metric->set == NULL)
    return (0);

  while (c_avl_pick (metric->set, &key, &value) == 0)
  {
    sfree (key);
    sfree (value);
  }

  return (0);
} /* }}} int statsd_metric_clear_set_unsafe */

/* Must hold metrics_lock when calling this function. */
static int statsd_metric_submit_unsafe (char const *name, /* {{{ */
    statsd_metric_t const *metric)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "statsd", sizeof (vl.plugin));

  if (metric->type == STATSD_GAUGE)
    sstrncpy (vl.type, "gauge", sizeof (vl.type));
  else if (metric->type == STATSD_TIMER)
    sstrncpy (vl.type, "latency", sizeof (vl.type));
  else if (metric->type == STATSD_SET)
    sstrncpy (vl.type, "objects", sizeof (vl.type));
  else /* if (metric->type == STATSD_COUNTER) */
    sstrncpy (vl.type, "derive", sizeof (vl.type));

  sstrncpy (vl.type_instance, name, sizeof (vl.type_instance));

  if (metric->type == STATSD_GAUGE)
    values[0].gauge = (gauge_t) metric->value;
  else if (metric->type == STATSD_TIMER)
  {
    size_t i;

    if (metric->updates_num == 0)
      return (0);

    vl.time = cdtime ();

    ssnprintf (vl.type_instance, sizeof (vl.type_instance),
        "%s-average", name);
    values[0].gauge = CDTIME_T_TO_DOUBLE (
        latency_counter_get_average (metric->latency));
    plugin_dispatch_values (&vl);

    if (conf_timer_lower) {
      ssnprintf (vl.type_instance, sizeof (vl.type_instance),
          "%s-lower", name);
      values[0].gauge = CDTIME_T_TO_DOUBLE (
          latency_counter_get_min (metric->latency));
      plugin_dispatch_values (&vl);
    }

    if (conf_timer_upper) {
      ssnprintf (vl.type_instance, sizeof (vl.type_instance),
          "%s-upper", name);
      values[0].gauge = CDTIME_T_TO_DOUBLE (
          latency_counter_get_max (metric->latency));
      plugin_dispatch_values (&vl);
    }

    if (conf_timer_sum) {
      ssnprintf (vl.type_instance, sizeof (vl.type_instance),
          "%s-sum", name);
      values[0].gauge = CDTIME_T_TO_DOUBLE (
          latency_counter_get_sum (metric->latency));
      plugin_dispatch_values (&vl);
    }

    for (i = 0; i < conf_timer_percentile_num; i++)
    {
      ssnprintf (vl.type_instance, sizeof (vl.type_instance),
          "%s-percentile-%.0f", name, conf_timer_percentile[i]);
      values[0].gauge = CDTIME_T_TO_DOUBLE (
          latency_counter_get_percentile (
            metric->latency, conf_timer_percentile[i]));
      plugin_dispatch_values (&vl);
    }

    /* Keep this at the end, since vl.type is set to "gauge" here. The
     * vl.type's above are implicitly set to "latency". */
    if (conf_timer_count) {
      sstrncpy (vl.type, "gauge", sizeof (vl.type));
      ssnprintf (vl.type_instance, sizeof (vl.type_instance),
          "%s-count", name);
      values[0].gauge = latency_counter_get_num (metric->latency);
      plugin_dispatch_values (&vl);
    }

    latency_counter_reset (metric->latency);
    return (0);
  }
  else if (metric->type == STATSD_SET)
  {
    if (metric->set == NULL)
      values[0].gauge = 0.0;
    else
      values[0].gauge = (gauge_t) c_avl_size (metric->set);
  }
  else
    values[0].derive = (derive_t) metric->value;

  return (plugin_dispatch_values (&vl));
} /* }}} int statsd_metric_submit_unsafe */

static int statsd_read (void) /* {{{ */
{
  c_avl_iterator_t *iter;
  char *name;
  statsd_metric_t *metric;

  char **to_be_deleted = NULL;
  size_t to_be_deleted_num = 0;
  size_t i;

  pthread_mutex_lock (&metrics_lock);

  if (metrics_tree == NULL)
  {
    pthread_mutex_unlock (&metrics_lock);
    return (0);
  }

  iter = c_avl_get_iterator (metrics_tree);
  while (c_avl_iterator_next (iter, (void *) &name, (void *) &metric) == 0)
  {
    if ((metric->updates_num == 0)
        && ((conf_delete_counters && (metric->type == STATSD_COUNTER))
          || (conf_delete_timers && (metric->type == STATSD_TIMER))
          || (conf_delete_gauges && (metric->type == STATSD_GAUGE))
          || (conf_delete_sets && (metric->type == STATSD_SET))))
    {
      DEBUG ("statsd plugin: Deleting metric \"%s\".", name);
      strarray_add (&to_be_deleted, &to_be_deleted_num, name);
      continue;
    }

    /* Names have a prefix, e.g. "c:", which determines the (statsd) type.
     * Remove this here. */
    statsd_metric_submit_unsafe (name + 2, metric);

    /* Reset the metric. */
    metric->updates_num = 0;
    if (metric->type == STATSD_SET)
      statsd_metric_clear_set_unsafe (metric);
  }
  c_avl_iterator_destroy (iter);

  for (i = 0; i < to_be_deleted_num; i++)
  {
    int status;

    status = c_avl_remove (metrics_tree, to_be_deleted[i],
        (void *) &name, (void *) &metric);
    if (status != 0)
    {
      ERROR ("stats plugin: c_avl_remove (\"%s\") failed with status %i.",
          to_be_deleted[i], status);
      continue;
    }

    sfree (name);
    sfree (metric);
  }

  pthread_mutex_unlock (&metrics_lock);

  strarray_free (to_be_deleted, to_be_deleted_num);

  return (0);
} /* }}} int statsd_read */

static int statsd_shutdown (void) /* {{{ */
{
  void *key;
  void *value;

  pthread_mutex_lock (&metrics_lock);

  if (network_thread_running)
  {
    network_thread_shutdown = 1;
    pthread_kill (network_thread, SIGTERM);
    pthread_join (network_thread, /* retval = */ NULL);
  }
  network_thread_running = 0;

  while (c_avl_pick (metrics_tree, &key, &value) == 0)
  {
    sfree (key);
    sfree (value);
  }
  c_avl_destroy (metrics_tree);
  metrics_tree = NULL;

  sfree (conf_node);
  sfree (conf_service);

  pthread_mutex_unlock (&metrics_lock);

  return (0);
} /* }}} int statsd_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("statsd", statsd_config);
  plugin_register_init ("statsd", statsd_init);
  plugin_register_read ("statsd", statsd_read);
  plugin_register_shutdown ("statsd", statsd_shutdown);
}

/* vim: set sw=2 sts=2 et fdm=marker : */
