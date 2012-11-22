/**
 * collectd - src/nginx.c
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Sebastian Harl
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
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <curl/curl.h>

static char *url         = NULL;
static char *user        = NULL;
static char *pass        = NULL;
static char *verify_peer = NULL;
static char *verify_host = NULL;
static char *cacert      = NULL;

static CURL *curl = NULL;

static char   nginx_buffer[16384];
static size_t nginx_buffer_len = 0;
static char   nginx_curl_error[CURL_ERROR_SIZE];

static const char *config_keys[] =
{
  "URL",
  "User",
  "Password",
  "VerifyPeer",
  "VerifyHost",
  "CACert"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static size_t nginx_curl_callback (void *buf, size_t size, size_t nmemb,
    void __attribute__((unused)) *stream)
{
  size_t len = size * nmemb;

  /* Check if the data fits into the memory. If not, truncate it. */
  if ((nginx_buffer_len + len) >= sizeof (nginx_buffer))
  {
    assert (sizeof (nginx_buffer) > nginx_buffer_len);
    len = (sizeof (nginx_buffer) - 1) - nginx_buffer_len;
  }

  if (len <= 0)
    return (len);

  memcpy (&nginx_buffer[nginx_buffer_len], buf, len);
  nginx_buffer_len += len;
  nginx_buffer[nginx_buffer_len] = 0;

  return (len);
}

static int config_set (char **var, const char *value)
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

static int config (const char *key, const char *value)
{
  if (strcasecmp (key, "url") == 0)
    return (config_set (&url, value));
  else if (strcasecmp (key, "user") == 0)
    return (config_set (&user, value));
  else if (strcasecmp (key, "password") == 0)
    return (config_set (&pass, value));
  else if (strcasecmp (key, "verifypeer") == 0)
    return (config_set (&verify_peer, value));
  else if (strcasecmp (key, "verifyhost") == 0)
    return (config_set (&verify_host, value));
  else if (strcasecmp (key, "cacert") == 0)
    return (config_set (&cacert, value));
  else
    return (-1);
} /* int config */

static int init (void)
{
  static char credentials[1024];

  if (curl != NULL)
    curl_easy_cleanup (curl);

  if ((curl = curl_easy_init ()) == NULL)
  {
    ERROR ("nginx plugin: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, nginx_curl_callback);
  curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
  curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, nginx_curl_error);

  if (user != NULL)
  {
    int status = ssnprintf (credentials, sizeof (credentials),
	"%s:%s", user, pass == NULL ? "" : pass);
    if ((status < 0) || ((size_t) status >= sizeof (credentials)))
    {
      ERROR ("nginx plugin: Credentials would have been truncated.");
      return (-1);
    }

    curl_easy_setopt (curl, CURLOPT_USERPWD, credentials);
  }

  if (url != NULL)
  {
    curl_easy_setopt (curl, CURLOPT_URL, url);
  }

  curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1);

  if ((verify_peer == NULL) || IS_TRUE (verify_peer))
  {
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 1);
  }
  else
  {
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0);
  }

  if ((verify_host == NULL) || IS_TRUE (verify_host))
  {
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 2);
  }
  else
  {
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 0);
  }

  if (cacert != NULL)
  {
    curl_easy_setopt (curl, CURLOPT_CAINFO, cacert);
  }

  return (0);
} /* void init */

static void submit (char *type, char *inst, long long value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  if (strcmp (type, "nginx_connections") == 0)
    values[0].gauge = value;
  else if (strcmp (type, "nginx_requests") == 0)
    values[0].derive = value;
  else
    return;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "nginx", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, "", sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));

  if (inst != NULL)
    sstrncpy (vl.type_instance, inst, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void submit */

static int nginx_read (void)
{
  int i;

  char *ptr;
  char *lines[16];
  int   lines_num = 0;
  char *saveptr;

  char *fields[16];
  int   fields_num;

  if (curl == NULL)
    return (-1);
  if (url == NULL)
    return (-1);

  nginx_buffer_len = 0;
  if (curl_easy_perform (curl) != 0)
  {
    WARNING ("nginx plugin: curl_easy_perform failed: %s", nginx_curl_error);
    return (-1);
  }

  ptr = nginx_buffer;
  saveptr = NULL;
  while ((lines[lines_num] = strtok_r (ptr, "\n\r", &saveptr)) != NULL)
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

  return (0);
} /* int nginx_read */

void module_register (void)
{
  plugin_register_config ("nginx", config, config_keys, config_keys_num);
  plugin_register_init ("nginx", init);
  plugin_register_read ("nginx", nginx_read);
} /* void module_register */

/*
 * vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
 */
