/**
 * collectd - src/nginx.c
 * Copyright (C) 2006,2007  Florian octo Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_debug.h"
#include "configfile.h"

#define MODULE_NAME "nginx"

#if HAVE_LIBCURL && HAVE_CURL_CURL_H
#  define NGINX_HAVE_READ 1
#  include <curl/curl.h>
#else
#  define NGINX_HAVE_READ 0
#endif

static char *url    = NULL;
static char *user   = NULL;
static char *pass   = NULL;
static char *cacert = NULL;

#if HAVE_LIBCURL
static CURL *curl = NULL;

#define ABUFFER_SIZE 16384
static char nginx_buffer[ABUFFER_SIZE];
static int  nginx_buffer_len = 0;
static char nginx_curl_error[CURL_ERROR_SIZE];
#endif /* HAVE_LIBCURL */

static char *connections_file = "nginx/nginx_connections-%s.rrd";
static char *connections_ds_def[] =
{
  "DS:value:GAUGE:"COLLECTD_HEARTBEAT":0:U",
  NULL
};
static int connections_ds_num = 1;

/* Limit to 2^20 requests/s */
static char *requests_file = "nginx/nginx_requests.rrd";
static char *requests_ds_def[] =
{
  "DS:value:COUNTER:"COLLECTD_HEARTBEAT":0:1048576",
  NULL
};
static int requests_ds_num = 1;

static char *config_keys[] =
{
  "URL",
  "User",
  "Password",
  "CACert",
  NULL
};
static int config_keys_num = 4;

#if HAVE_LIBCURL
static size_t nginx_curl_callback (void *buf, size_t size, size_t nmemb, void *stream)
{
  size_t len = size * nmemb;

  if ((nginx_buffer_len + len) >= ABUFFER_SIZE)
  {
    len = (ABUFFER_SIZE - 1) - nginx_buffer_len;
  }

  if (len <= 0)
    return (len);

  memcpy (nginx_buffer + nginx_buffer_len, (char *) buf, len);
  nginx_buffer_len += len;
  nginx_buffer[nginx_buffer_len] = '\0';

  return (len);
}
#endif /* HAVE_LIBCURL */

static int config_set (char **var, char *value)
{
  if (*var != NULL)
  {
    free (*var);
    *var = NULL;
  }

  if ((*var = strdup (value)) == NULL)
    return (1);
  else
    return (0);
}

static int config (char *key, char *value)
{
  if (strcasecmp (key, "url") == 0)
    return (config_set (&url, value));
  else if (strcasecmp (key, "user") == 0)
    return (config_set (&user, value));
  else if (strcasecmp (key, "password") == 0)
    return (config_set (&pass, value));
  else if (strcasecmp (key, "cacert") == 0)
    return (config_set (&cacert, value));
  else
    return (-1);
}

static void init (void)
{
#if HAVE_LIBCURL
  static char credentials[1024];

  if (curl != NULL)
  {
    curl_easy_cleanup (curl);
  }

  if ((curl = curl_easy_init ()) == NULL)
  {
    syslog (LOG_ERR, "nginx: `curl_easy_init' failed.");
    return;
  }

  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, nginx_curl_callback);
  curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
  curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, nginx_curl_error);

  if (user != NULL)
  {
    if (snprintf (credentials, 1024, "%s:%s", user, pass == NULL ? "" : pass) >= 1024)
    {
      syslog (LOG_ERR, "nginx: Credentials would have been truncated.");
      return;
    }

    curl_easy_setopt (curl, CURLOPT_USERPWD, credentials);
  }

  if (url != NULL)
  {
    curl_easy_setopt (curl, CURLOPT_URL, url);
  }

  if (cacert != NULL)
  {
    curl_easy_setopt (curl, CURLOPT_CAINFO, cacert);
  }
#endif /* HAVE_LIBCURL */
} /* void init */

static void connections_write (char *host, char *inst, char *val)
{
  char buf[1024];

  if (snprintf (buf, 1024, connections_file, inst) >= 1024)
    return;

  rrd_update_file (host, buf, val,
      connections_ds_def, connections_ds_num);
}

static void requests_write (char *host, char *inst, char *val)
{
  rrd_update_file (host, requests_file, val,
      requests_ds_def, requests_ds_num);
}

#if NGINX_HAVE_READ
static void submit (char *type, char *inst, long long value)
{
  char buf[1024];
  int  status;

  DBG ("type = %s; inst = %s; value = %lli;",
      type, (inst == NULL) ? "(nil)" : inst, value);

  status = snprintf (buf, 1024, "%u:%lli", (unsigned int) curtime, value);
  if ((status < 0) || (status >= 1024))
  {
    syslog (LOG_ERR, "nginx: snprintf failed");
    return;
  }

  plugin_submit (type, inst, buf);
}

static void nginx_read (void)
{
  int i;

  char *ptr;
  char *lines[16];
  int   lines_num = 0;

  char *fields[16];
  int   fields_num;

  if (curl == NULL)
    return;
  if (url == NULL)
    return;

  nginx_buffer_len = 0;
  if (curl_easy_perform (curl) != 0)
  {
    syslog (LOG_WARNING, "nginx: curl_easy_perform failed: %s", nginx_curl_error);
    return;
  }

  ptr = nginx_buffer;
  while ((lines[lines_num] = strtok (ptr, "\n\r")) != NULL)
  {
    ptr = NULL;
    lines_num++;

    if (lines_num >= 16)
      break;
  }

  /*
   * Active connections: 291
   * server accepts handled requests
   *  16630948 16630948 31070465
   * Reading: 6 Writing: 179 Waiting: 106
   */
  for (i = 0; i < lines_num; i++)
  {
    fields_num = strsplit (lines[i], fields,
	(sizeof (fields) / sizeof (fields[0])));

    if (fields_num == 3)
    {
      if ((strcmp (fields[0], "Active") == 0)
	  && (strcmp (fields[1], "connections:") == 0))
      {
	submit ("nginx_connections", "active", atoll (fields[2]));
      }
      else if ((atoll (fields[0]) != 0)
	  && (atoll (fields[1]) != 0)
	  && (atoll (fields[2]) != 0))
      {
	submit ("nginx_requests", NULL, atoll (fields[2]));
      }
    }
    else if (fields_num == 6)
    {
      if ((strcmp (fields[0], "Reading:") == 0)
	  && (strcmp (fields[2], "Writing:") == 0)
	  && (strcmp (fields[4], "Waiting:") == 0))
      {
	submit ("nginx_connections", "reading", atoll (fields[1]));
	submit ("nginx_connections", "writing", atoll (fields[3]));
	submit ("nginx_connections", "waiting", atoll (fields[5]));
      }
    }
  }

  nginx_buffer_len = 0;
}
#else
#  define nginx_read NULL
#endif /* NGINX_HAVE_READ */

void module_register (void)
{
  plugin_register (MODULE_NAME, init, nginx_read, NULL);
  plugin_register ("nginx_requests",   NULL, NULL, requests_write);
  plugin_register ("nginx_connections", NULL, NULL, connections_write);
  cf_register (MODULE_NAME, config, config_keys, config_keys_num);
}

#undef MODULE_NAME

/*
 * vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
 */
