/**
 * collectd - src/olsrd.c
 * Copyright (C) 2009  Florian octo Forster
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

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

#define OLSRD_DEFAULT_NODE "localhost"
#define OLSRD_DEFAULT_SERVICE "2006"

static const char *config_keys[] =
{
  "Host",
  "Port",
  "CollectLinks",
  "CollectRoutes",
  "CollectTopology"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *config_node = NULL;
static char *config_service = NULL;

#define OLSRD_WANT_NOT     0
#define OLSRD_WANT_SUMMARY 1
#define OLSRD_WANT_DETAIL  2
static int config_want_links    = OLSRD_WANT_DETAIL;
static int config_want_routes   = OLSRD_WANT_SUMMARY;
static int config_want_topology = OLSRD_WANT_SUMMARY;

static const char *olsrd_get_node (void) /* {{{ */
{
  if (config_node != NULL)
    return (config_node);
  return (OLSRD_DEFAULT_NODE);
} /* }}} const char *olsrd_get_node */

static const char *olsrd_get_service (void) /* {{{ */
{
  if (config_service != NULL)
    return (config_service);
  return (OLSRD_DEFAULT_SERVICE);
} /* }}} const char *olsrd_get_service */

static void olsrd_set_node (const char *node) /* {{{ */
{
  char *tmp;
  if (node == NULL)
    return;
  tmp = strdup (node);
  if (tmp == NULL)
    return;
  config_node = tmp;
} /* }}} void olsrd_set_node */

static void olsrd_set_service (const char *service) /* {{{ */
{
  char *tmp;
  if (service == NULL)
    return;
  tmp = strdup (service);
  if (tmp == NULL)
    return;
  config_service = tmp;
} /* }}} void olsrd_set_service */

static void olsrd_set_detail (int *varptr, const char *detail, /* {{{ */
    const char *key)
{
  if (strcasecmp ("No", detail) == 0)
    *varptr = OLSRD_WANT_NOT;
  else if (strcasecmp ("Summary", detail) == 0)
    *varptr = OLSRD_WANT_SUMMARY;
  else if (strcasecmp ("Detail", detail) == 0)
    *varptr = OLSRD_WANT_DETAIL;
  else
  {
    ERROR ("olsrd plugin: Invalid argument given to the `%s' configuration "
        "option: `%s'. Expected: `No', `Summary', or `Detail'.",
        key, detail);
  }
} /* }}} void olsrd_set_detail */

/* Strip trailing newline characters. Returns length of string. */
static size_t strchomp (char *buffer) /* {{{ */
{
  size_t buffer_len;

  buffer_len = strlen (buffer);
  while ((buffer_len > 0)
      && ((buffer[buffer_len - 1] == '\r')
        || (buffer[buffer_len - 1] == '\n')))
  {
    buffer_len--;
    buffer[buffer_len] = 0;
  }

  return (buffer_len);
} /* }}} size_t strchomp */

static size_t strtabsplit (char *string, char **fields, size_t size) /* {{{ */
{
  size_t i;
  char *ptr;
  char *saveptr;

  i = 0;
  ptr = string;
  saveptr = NULL;
  while ((fields[i] = strtok_r (ptr, " \t\r\n", &saveptr)) != NULL)
  {
    ptr = NULL;
    i++;

    if (i >= size)
      break;
  }

  return (i);
} /* }}} size_t strtabsplit */

static FILE *olsrd_connect (void) /* {{{ */
{
  struct addrinfo  ai_hints;
  struct addrinfo *ai_list, *ai_ptr;
  int              ai_return;

  FILE *fh;

  memset (&ai_hints, 0, sizeof (ai_hints));
  ai_hints.ai_flags    = 0;
#ifdef AI_ADDRCONFIG
  ai_hints.ai_flags   |= AI_ADDRCONFIG;
#endif
  ai_hints.ai_family   = PF_UNSPEC;
  ai_hints.ai_socktype = SOCK_STREAM;
  ai_hints.ai_protocol = IPPROTO_TCP;

  ai_list = NULL;
  ai_return = getaddrinfo (olsrd_get_node (), olsrd_get_service (),
      &ai_hints, &ai_list);
  if (ai_return != 0)
  {
    ERROR ("olsrd plugin: getaddrinfo (%s, %s) failed: %s",
        olsrd_get_node (), olsrd_get_service (),
        gai_strerror (ai_return));
    return (NULL);
  }

  fh = NULL;
  for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
  {
    int fd;
    int status;
    char errbuf[1024];

    fd = socket (ai_ptr->ai_family, ai_ptr->ai_socktype, ai_ptr->ai_protocol);
    if (fd < 0)
    {
      ERROR ("olsrd plugin: socket failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      continue;
    }

    status = connect (fd, ai_ptr->ai_addr, ai_ptr->ai_addrlen);
    if (status != 0)
    {
      ERROR ("olsrd plugin: connect failed: %s",
          sstrerror (errno, errbuf, sizeof (errbuf)));
      close (fd);
      continue;
    }

    fh = fdopen (fd, "r+");
    if (fh == NULL)
    {
      ERROR ("olsrd plugin: fdopen failed.");
      close (fd);
      continue;
    }

    break;
  } /* for (ai_ptr) */

  freeaddrinfo (ai_list);

  return (fh);
} /* }}} FILE *olsrd_connect */

static int olsrd_cb_ignore (int lineno, /* {{{ */
    size_t fields_num, char **fields)
{
  return (0);
} /* }}} int olsrd_cb_ignore */

static int olsrd_cb_links (int lineno, /* {{{ */
    size_t fields_num, char **fields)
{
  /* Fields:
   *  0 = Local IP
   *  1 = Remote IP
   *  2 = Hyst.
   *  3 = LQ
   *  4 = NLQ
   *  5 = Cost */

  static uint32_t links_num;
  static double    lq_sum;
  static uint32_t  lq_num;
  static double   nlq_sum;
  static uint32_t nlq_num;

  char link_name[DATA_MAX_NAME_LEN];
  double lq;  /* tx */
  double nlq; /* rx */

  char *endptr;

  if (config_want_links == OLSRD_WANT_NOT)
    return (0);

  /* Special handling of the first line. */
  if (lineno <= 0)
  {
    links_num = 0;
    lq_sum = 0.0;
    lq_num = 0;
    nlq_sum = 0.0;
    nlq_num = 0;

    return (0);
  }

  /* Special handling of the last line. */
  if (fields_num == 0)
  {
    DEBUG ("olsrd plugin: fields_num = %"PRIu32";", links_num);

    lq = NAN;
    if (lq_num > 0)
      lq = lq_sum / ((double) lq_num);
    DEBUG ("olsrd plugin: Average  LQ: %g", lq);

    nlq = NAN;
    if (nlq_num > 0)
      nlq = nlq_sum / ((double) nlq_num);
    DEBUG ("olsrd plugin: Average NLQ: %g", nlq);

    return (0);
  }

  if (fields_num != 6)
    return (-1);

  links_num++;

  memset (link_name, 0, sizeof (link_name));
  ssnprintf (link_name, sizeof (link_name), "%s-%s", fields[0], fields[1]);

  errno = 0;
  endptr = NULL;
  lq = strtod (fields[3], &endptr);
  if ((errno != 0) || (endptr == fields[3]))
  {
    ERROR ("olsrd plugin: Cannot parse link quality: %s", fields[3]);
  }
  else
  {
    if (!isnan (lq))
    {
      lq_sum += lq;
      lq_num++;
    }

    if (config_want_links == OLSRD_WANT_DETAIL)
    {
      DEBUG ("olsrd plugin: %s:  LQ = %g.", link_name, lq);
    }
  }

  errno = 0;
  endptr = NULL;
  nlq = strtod (fields[4], &endptr);
  if ((errno != 0) || (endptr == fields[4]))
  {
    ERROR ("olsrd plugin: Cannot parse neighbor link quality: %s", fields[4]);
  }
  else
  {
    if (!isnan (nlq))
    {
      nlq_sum += nlq;
      nlq_num++;
    }

    if (config_want_links == OLSRD_WANT_DETAIL)
    {
      DEBUG ("olsrd plugin: %s: NLQ = %g.", link_name, lq);
    }
  }

  return (0);
} /* }}} int olsrd_cb_links */

static int olsrd_cb_routes (int lineno, /* {{{ */
    size_t fields_num, char **fields)
{
  /* Fields:
   *  0 = Destination
   *  1 = Gateway IP
   *  2 = Metric
   *  3 = ETX
   *  4 = Interface */

  static uint32_t metric_sum;
  static uint32_t metric_num;

  static double   etx_sum;
  static uint32_t etx_num;

  uint32_t metric;
  double etx;
  char *endptr;

  if (config_want_routes == OLSRD_WANT_NOT)
    return (0);

  /* Special handling of the first line */
  if (lineno <= 0)
  {
    metric_num = 0;
    metric_sum = 0;
    return (0);
  }

  /* Special handling after the last line */
  if (fields_num == 0)
  {
    double metric_avg;

    metric_avg = NAN;
    if (metric_num > 0)
      metric_avg = ((double) metric_sum) / ((double) metric_num);

    etx = NAN;
    if (etx_num > 0)
      etx = etx_sum / ((double) etx_sum);

    DEBUG ("olsrd plugin: Number of routes: %"PRIu32"; Average metric: %g",
        metric_num, metric_avg);
    return (0);
  }

  if (fields_num != 5)
    return (-1);

  errno = 0;
  endptr = NULL;
  metric = (uint32_t) strtoul (fields[2], &endptr, 0);
  if ((errno != 0) || (endptr == fields[2]))
  {
    ERROR ("olsrd plugin: Unable to parse metric: %s", fields[2]);
  }
  else
  {
    metric_num++;
    metric_sum += metric;

    if (config_want_routes == OLSRD_WANT_DETAIL)
    {
      DEBUG ("olsrd plugin: Route with metric %"PRIu32".", metric);
    }
  }

  errno = 0;
  endptr = NULL;
  etx = strtod (fields[3], &endptr);
  if ((errno != 0) || (endptr == fields[3]))
  {
    ERROR ("olsrd plugin: Unable to parse ETX: %s", fields[3]);
  }
  else
  {
    if (!isnan (etx))
    {
      etx_sum += etx;
      etx_num++;
    }

    if (config_want_routes == OLSRD_WANT_DETAIL)
    {
      DEBUG ("olsrd plugin: Route with ETX %g.", etx);
    }
  }

  return (0);
} /* }}} int olsrd_cb_routes */

static int olsrd_cb_topology (int lineno, /* {{{ */
    size_t fields_num, char **fields)
{
  /* Fields:
   *  0 = Dest. IP
   *  1 = Last hop IP
   *  2 = LQ
   *  3 = NLQ
   *  4 = Cost */

  static double   lq_sum;
  static uint32_t lq_num;

  static uint32_t links_num;

  char link_name[DATA_MAX_NAME_LEN];
  double lq;
  char *endptr;

  if (config_want_topology == OLSRD_WANT_NOT)
    return (0);

  /* Special handling of the first line */
  if (lineno <= 0)
  {
    lq_sum = 0.0;
    lq_num = 0;
    links_num = 0;

    return (0);
  }

  /* Special handling after the last line */
  if (fields_num == 0)
  {
    lq = NAN;
    if (lq_num > 0)
      lq = lq_sum / ((double) lq_sum);

    DEBUG ("olsrd plugin: Number of links: %"PRIu32, links_num);
    DEBUG ("olsrd plugin: Average link quality: %g", lq);

    return (0);
  }

  if (fields_num != 5)
    return (-1);

  memset (link_name, 0, sizeof (link_name));
  ssnprintf (link_name, sizeof (link_name), "%s-%s", fields[0], fields[1]);
  links_num++;

  errno = 0;
  endptr = NULL;
  lq = strtod (fields[2], &endptr);
  if ((errno != 0) || (endptr == fields[2]))
  {
    ERROR ("olsrd plugin: Unable to parse LQ: %s", fields[2]);
  }
  else
  {
    if (!isnan (lq))
    {
      lq_sum += lq;
      lq_num++;
    }

    if (config_want_topology == OLSRD_WANT_DETAIL)
    {
      DEBUG ("olsrd plugin: link_name = %s; lq = %g;", link_name, lq);
    }
  }

  if (config_want_topology == OLSRD_WANT_DETAIL)
  {
    double nlq;

    errno = 0;
    endptr = NULL;
    nlq = strtod (fields[3], &endptr);
    if ((errno != 0) || (endptr == fields[3]))
    {
      ERROR ("olsrd plugin: Unable to parse NLQ: %s", fields[3]);
    }
    else
    {
      DEBUG ("olsrd plugin: link_name = %s; nlq = %g;", link_name, nlq);
    }
  }

  return (0);
} /* }}} int olsrd_cb_topology */

static int olsrd_read_table (FILE *fh, /* {{{ */
    int (*callback) (int lineno, size_t fields_num, char **fields))
{
  char buffer[1024];
  size_t buffer_len;

  char *fields[32];
  size_t fields_num;

  int lineno;

  lineno = 0;
  while (fgets (buffer, sizeof (buffer), fh) != NULL)
  {
    /* An empty line ends the table. */
    buffer_len = strchomp (buffer);
    if (buffer_len <= 0)
    {
      (*callback) (lineno, /* fields_num = */ 0, /* fields = */ NULL);
      break;
    }

    fields_num = strtabsplit (buffer, fields, STATIC_ARRAY_SIZE (fields));

    (*callback) (lineno, fields_num, fields);
    lineno++;
  } /* while (fgets) */
  
  return (0);
} /* }}} int olsrd_read_table */

static int olsrd_config (const char *key, const char *value) /* {{{ */
{
  if (strcasecmp ("Host", key) == 0)
    olsrd_set_node (value);
  else if (strcasecmp ("Port", key) == 0)
    olsrd_set_service (value);
  else if (strcasecmp ("CollectLinks", key) == 0)
    olsrd_set_detail (&config_want_links, value, key);
  else if (strcasecmp ("CollectRoutes", key) == 0)
    olsrd_set_detail (&config_want_routes, value, key);
  else if (strcasecmp ("CollectTopology", key) == 0)
    olsrd_set_detail (&config_want_topology, value, key);
  else
  {
    ERROR ("olsrd plugin: Unknown configuration option given: %s", key);
    return (-1);
  }

  return (0);
} /* }}} int olsrd_config */

static int olsrd_read (void) /* {{{ */
{
  FILE *fh;
  char buffer[1024];
  size_t buffer_len;

  fh = olsrd_connect ();
  if (fh == NULL)
    return (-1);

  while (fgets (buffer, sizeof (buffer), fh) != NULL)
  {
    buffer_len = strchomp (buffer);
    if (buffer_len <= 0)
      continue;
    
    if (strcmp ("Table: Links", buffer) == 0)
      olsrd_read_table (fh, olsrd_cb_links);
    else if (strcmp ("Table: Neighbors", buffer) == 0)
      olsrd_read_table (fh, olsrd_cb_ignore);
    else if (strcmp ("Table: Topology", buffer) == 0)
      olsrd_read_table (fh, olsrd_cb_topology);
    else if (strcmp ("Table: HNA", buffer) == 0)
      olsrd_read_table (fh, olsrd_cb_ignore);
    else if (strcmp ("Table: MID", buffer) == 0)
      olsrd_read_table (fh, olsrd_cb_ignore);
    else if (strcmp ("Table: Routes", buffer) == 0)
      olsrd_read_table (fh, olsrd_cb_routes);
    else if ((strcmp ("HTTP/1.0 200 OK", buffer) == 0)
        || (strcmp ("Content-type: text/plain", buffer) == 0))
    {
      /* ignore */
    }
    else
    {
      DEBUG ("olsrd plugin: Unable to handle line: %s", buffer);
    }
  } /* while (fgets) */

  fclose (fh);
  
  return (0);
} /* }}} int olsrd_read */

static int olsrd_shutdown (void) /* {{{ */
{
  sfree (config_node);
  sfree (config_service);

  return (0);
} /* }}} int olsrd_shutdown */

void module_register (void)
{
  plugin_register_config ("olsrd", olsrd_config,
      config_keys, config_keys_num);
  plugin_register_read ("olsrd", olsrd_read);
  plugin_register_shutdown ("olsrd", olsrd_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
