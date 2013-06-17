/**
 * collectd - src/statsd.c
 *
 * Copyright (C) 2013       Florian octo Forster
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
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

#include <pthread.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <poll.h>

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
  STATSD_GAUGE
};
typedef enum metric_type_e metric_type_t;

struct statsd_metric_s
{
  metric_type_t type;
  int64_t value;
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

/* Must hold metrics_lock when calling this function. */
static int statsd_metric_set_unsafe (char const *name, int64_t value, /* {{{ */
    metric_type_t type)
{
  statsd_metric_t *metric;
  char *key;
  int status;

  status = c_avl_get (metrics_tree, name, (void *) &metric);
  if (status == 0)
  {
    metric->value = value;
    metric->updates_num++;

    return (0);
  }

  DEBUG ("stats plugin: Adding new metric \"%s\".", name);
  /* FIXME: The keys should have a prefix so counter, gauge and timer with the
   * same name can exist. */
  key = strdup (name);
  metric = calloc (1, sizeof (*metric));
  if ((key == NULL) || (metric == NULL))
  {
    sfree (key);
    sfree (metric);
    return (-1);
  }

  metric->type = type;
  metric->value = value;
  metric->updates_num = 1;

  status = c_avl_insert (metrics_tree, key, metric);
  if (status != 0)
  {
    sfree (key);
    sfree (metric);

    return (-1);
  }

  return (0);
} /* }}} int statsd_metric_set_unsafe */

static int statsd_metric_set (char const *name, int64_t value, /* {{{ */
    metric_type_t type)
{
  int status;

  pthread_mutex_lock (&metrics_lock);
  status = statsd_metric_set_unsafe (name, value, type);
  pthread_mutex_unlock (&metrics_lock);

  return (status);
} /* }}} int statsd_metric_set */

static int statsd_metric_add (char const *name, int64_t delta, /* {{{ */
    metric_type_t type)
{
  statsd_metric_t *metric;
  int status;

  pthread_mutex_lock (&metrics_lock);

  status = c_avl_get (metrics_tree, name, (void *) &metric);
  if (status == 0)
  {
    metric->value += delta;
    metric->updates_num++;

    pthread_mutex_unlock (&metrics_lock);
    return (0);
  }
  else /* no such value yet */
  {
    status = statsd_metric_set_unsafe (name, delta, type);

    pthread_mutex_unlock (&metrics_lock);
    return (status);
  }
} /* }}} int statsd_metric_add */

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
    status = parse_value (extra + 1, &scale, DS_TYPE_GAUGE);
    if (status != 0)
      return (status);

    if (!isfinite (scale.gauge) || (scale.gauge <= 0.0) || (scale.gauge > 1.0))
      return (-1);
  }

  value.derive = 1;
  status = parse_value (value_str, &value, DS_TYPE_DERIVE);
  if (status != 0)
    return (status);

  if (value.derive < 1)
    return (-1);

  return (statsd_metric_add (name,
        (int64_t) (((gauge_t) value.derive) / scale.gauge),
        STATSD_COUNTER));
} /* }}} int statsd_handle_counter */

static int statsd_handle_gauge (char const *name, /* {{{ */
    char const *value_str)
{
  value_t value;
  int status;

  value.derive = 0;
  status = parse_value (value_str, &value, DS_TYPE_DERIVE);
  if (status != 0)
    return (status);

  if ((value_str[0] == '+') || (value_str[0] == '-'))
    return (statsd_metric_add (name, (int64_t) value.derive, STATSD_GAUGE));
  else
    return (statsd_metric_set (name, (int64_t) value.derive, STATSD_GAUGE));
} /* }}} int statsd_handle_gauge */

static int statsd_handle_timer (char const *name, /* {{{ */
    char const *value_str)
{
  value_t value;
  int status;

  value.derive = 0;
  status = parse_value (value_str, &value, DS_TYPE_DERIVE);
  if (status != 0)
    return (status);

  return (statsd_metric_add (name, (int64_t) value.derive, STATSD_TIMER));
} /* }}} int statsd_handle_timer */

static int statsd_handle_set (char const *name __attribute__((unused)), /* {{{ */
    char const *value_str __attribute__((unused)))
{
  static c_complain_t c = C_COMPLAIN_INIT_STATIC;

  c_complain (LOG_WARNING, &c,
      "statsd plugin: Support for sets is not yet implemented.");

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

  /* extra is only valid for counters */
  if (extra != NULL)
    return (-1);

  if (strcmp ("g", type) == 0)
    return (statsd_handle_gauge (name, value));
  else if (strcmp ("ms", type) == 0)
    return (statsd_handle_timer (name, value));
  else if (strcmp ("s", type) == 0)
    return (statsd_handle_set (name, value));
  else
    return (-1);
} /* }}} void statsd_parse_line */

static void statsd_parse_buffer (char *buffer) /* {{{ */
{
  char *dummy;
  char *saveptr = NULL;
  char *ptr;

  for (dummy = buffer;
      (ptr = strtok_r (dummy, "\r\n", &saveptr)) != NULL;
      dummy = NULL)
  {
    char *line_orig = sstrdup (ptr);
    int status;

    status = statsd_parse_line (ptr);
    if (status != 0)
      ERROR ("statsd plugin: Unable to parse line: \"%s\"", line_orig);

    sfree (line_orig);
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
    metrics_tree = c_avl_create ((void *) strcasecmp);

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

static int statsd_metric_submit (char const *name, /* {{{ */
    statsd_metric_t const *metric)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  if (metric->type == STATSD_GAUGE)
    values[0].gauge = (gauge_t) metric->value;
  else if (metric->type == STATSD_TIMER)
  {
    if (metric->updates_num == 0)
      values[0].gauge = NAN;
    else
      values[0].gauge =
        ((gauge_t) metric->value) / ((gauge_t) metric->updates_num);
  }
  else
    values[0].derive = (derive_t) metric->value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "statsd", sizeof (vl.plugin));

  if (metric->type == STATSD_GAUGE)
    sstrncpy (vl.type, "gauge", sizeof (vl.type));
  else if (metric->type == STATSD_TIMER)
    sstrncpy (vl.type, "latency", sizeof (vl.type));
  else /* if (metric->type == STATSD_COUNTER) */
    sstrncpy (vl.type, "derive", sizeof (vl.type));

  sstrncpy (vl.type_instance, name, sizeof (vl.type_instance));

  return (plugin_dispatch_values (&vl));
} /* }}} int statsd_metric_submit */

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
          || (conf_delete_gauges && (metric->type == STATSD_GAUGE))))
    {
      DEBUG ("statsd plugin: Deleting metric \"%s\".", name);
      strarray_add (&to_be_deleted, &to_be_deleted_num, name);
      continue;
    }

    statsd_metric_submit (name, metric);
    metric->updates_num = 0;
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
