/**
 * collectd - src/nginx_multihost.c
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Sebastian Harl
 * Copyright (C) 2016       Maxim Chindyasov
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
 *   Sebastian Harl <sh at tokkee.org>
 *   Maxim Chindyasov <max at chindyasov.ru>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <curl/curl.h>

struct nginx_host_config_s /* {{{ */
{
  char *host_name;
  char *url;
  char *user;
  char *pass;
  char *verify_peer;
  char *verify_host;
  char *cacert;
  char *timeout;

  CURL *curl;

  char   nginx_buffer[16384];
  size_t nginx_buffer_len;
  char   nginx_curl_error[CURL_ERROR_SIZE];
};
typedef struct nginx_host_config_s nginx_host_config_t; /* }}} */

static size_t nginx_curl_callback (void *buf, size_t size, size_t nmemb,
    void *user_data)
{
  nginx_host_config_t *host_config = (nginx_host_config_t*)user_data;
  size_t len = size * nmemb;

  /* Check if the data fits into the memory. If not, truncate it. */
  if ((host_config->nginx_buffer_len + len) >= sizeof (host_config->nginx_buffer))
  {
    assert (sizeof (host_config->nginx_buffer) > host_config->nginx_buffer_len);
    len = (sizeof (host_config->nginx_buffer) - 1) - host_config->nginx_buffer_len;
  }

  if (len == 0)
    return (len);

  memcpy (&host_config->nginx_buffer[host_config->nginx_buffer_len], buf, len);
  host_config->nginx_buffer_len += len;
  host_config->nginx_buffer[host_config->nginx_buffer_len] = 0;

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
} /* int config_set */

static int config_host_parameter (nginx_host_config_t *host_config, const char *key, const char *value)
{
  if (strcasecmp (key, "url") == 0)
    return (config_set (&host_config->url, value));
  else if (strcasecmp (key, "user") == 0)
    return (config_set (&host_config->user, value));
  else if (strcasecmp (key, "password") == 0)
    return (config_set (&host_config->pass, value));
  else if (strcasecmp (key, "verifypeer") == 0)
    return (config_set (&host_config->verify_peer, value));
  else if (strcasecmp (key, "verifyhost") == 0)
    return (config_set (&host_config->verify_host, value));
  else if (strcasecmp (key, "cacert") == 0)
    return (config_set (&host_config->cacert, value));
  else if (strcasecmp (key, "timeout") == 0)
    return (config_set (&host_config->timeout, value));
  else
    return (-1);
} /* int config_host_parameter */

static int init_curl (nginx_host_config_t *host_config)
{
  if (host_config->curl != NULL)
    curl_easy_cleanup (host_config->curl);

  if ((host_config->curl = curl_easy_init ()) == NULL)
  {
    ERROR ("nginx_multihost plugin: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt (host_config->curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt (host_config->curl, CURLOPT_WRITEFUNCTION, nginx_curl_callback);
  curl_easy_setopt (host_config->curl, CURLOPT_WRITEDATA, host_config);
  curl_easy_setopt (host_config->curl, CURLOPT_USERAGENT, COLLECTD_USERAGENT);
  curl_easy_setopt (host_config->curl, CURLOPT_ERRORBUFFER, host_config->nginx_curl_error);

  if (host_config->user != NULL)
  {
#ifdef HAVE_CURLOPT_USERNAME
    curl_easy_setopt (host_config->curl, CURLOPT_USERNAME, host_config->user);
    curl_easy_setopt (host_config->curl, CURLOPT_PASSWORD, (host_config->pass == NULL) ? "" : host_config->pass);
#else
    static char credentials[1024];
    int status = ssnprintf (credentials, sizeof (credentials),
	"%s:%s", host_config->user, host_config->pass == NULL ? "" : host_config->pass);
    if ((status < 0) || ((size_t) status >= sizeof (credentials)))
    {
      ERROR ("nginx_multihost plugin: Credentials would have been truncated.");
      return (-1);
    }

    curl_easy_setopt (host_config->curl, CURLOPT_USERPWD, credentials);
#endif
  }

  if (host_config->url != NULL)
  {
    curl_easy_setopt (host_config->curl, CURLOPT_URL, host_config->url);
  }

  curl_easy_setopt (host_config->curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt (host_config->curl, CURLOPT_MAXREDIRS, 50L);

  if ((host_config->verify_peer == NULL) || IS_TRUE (host_config->verify_peer))
  {
    curl_easy_setopt (host_config->curl, CURLOPT_SSL_VERIFYPEER, 1L);
  }
  else
  {
    curl_easy_setopt (host_config->curl, CURLOPT_SSL_VERIFYPEER, 0L);
  }

  if ((host_config->verify_host == NULL) || IS_TRUE (host_config->verify_host))
  {
    curl_easy_setopt (host_config->curl, CURLOPT_SSL_VERIFYHOST, 2L);
  }
  else
  {
    curl_easy_setopt (host_config->curl, CURLOPT_SSL_VERIFYHOST, 0L);
  }

  if (host_config->cacert != NULL)
  {
    curl_easy_setopt (host_config->curl, CURLOPT_CAINFO, host_config->cacert);
  }

#ifdef HAVE_CURLOPT_TIMEOUT_MS
  if (host_config->timeout != NULL)
  {
    curl_easy_setopt (host_config->curl, CURLOPT_TIMEOUT_MS, atol(host_config->timeout));
  }
  else
  {
    curl_easy_setopt (host_config->curl, CURLOPT_TIMEOUT_MS, (long) CDTIME_T_TO_MS(plugin_get_interval()));
  }
#endif

  return (0);
} /* void init_curl */

static void submit (const char *host_name, const char *type, const char *inst, long long value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  if (strcmp (type, "nginx_connections") == 0)
    values[0].gauge = value;
  else if (strcmp (type, "nginx_requests") == 0)
    values[0].derive = value;
  else if (strcmp (type, "connections") == 0)
    values[0].derive = value;
  else
    return;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "nginx_multihost", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, host_name, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, type, sizeof (vl.type));

  if (inst != NULL)
    sstrncpy (vl.type_instance, inst, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void submit */

static int nginx_read (user_data_t *ud)
{
  char *ptr;
  char *lines[16];
  int   lines_num = 0;
  char *saveptr;

  char *fields[16];
  int   fields_num;

  nginx_host_config_t *host_config = (nginx_host_config_t *)ud->data;

  if (host_config->curl == NULL)
    return (-1);
  if (host_config->url == NULL)
    return (-1);

  host_config->nginx_buffer_len = 0;
  if (curl_easy_perform (host_config->curl) != CURLE_OK)
  {
    WARNING ("nginx_multihost plugin: curl_easy_perform failed: %s", host_config->nginx_curl_error);
    return (-1);
  }

  ptr = host_config->nginx_buffer;
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
  for (int i = 0; i < lines_num; i++)
  {
    fields_num = strsplit (lines[i], fields,
	(sizeof (fields) / sizeof (fields[0])));

    if (fields_num == 3)
    {
      if ((strcmp (fields[0], "Active") == 0)
	  && (strcmp (fields[1], "connections:") == 0))
      {
	submit (host_config->host_name, "nginx_connections", "active", atoll (fields[2]));
      }
      else if ((atoll (fields[0]) != 0)
	  && (atoll (fields[1]) != 0)
	  && (atoll (fields[2]) != 0))
      {
	submit (host_config->host_name, "connections", "accepted", atoll (fields[0]));
	submit (host_config->host_name, "connections", "handled", atoll (fields[1]));
	submit (host_config->host_name, "nginx_requests", NULL, atoll (fields[2]));
      }
    }
    else if (fields_num == 6)
    {
      if ((strcmp (fields[0], "Reading:") == 0)
	  && (strcmp (fields[2], "Writing:") == 0)
	  && (strcmp (fields[4], "Waiting:") == 0))
      {
	submit (host_config->host_name, "nginx_connections", "reading", atoll (fields[1]));
	submit (host_config->host_name, "nginx_connections", "writing", atoll (fields[3]));
	submit (host_config->host_name, "nginx_connections", "waiting", atoll (fields[5]));
      }
    }
  }

  host_config->nginx_buffer_len = 0;

  return (0);
} /* int nginx_read */

static void nginx_host_cleanup (void *cleanup_data)
{
  nginx_host_config_t *host_config = (nginx_host_config_t*)cleanup_data;

  DEBUG ("nginx_multihost plugin: cleaning data for host %s (%s).", host_config->url, host_config->host_name);

  if (host_config->curl != NULL)
    curl_easy_cleanup (host_config->curl);
  
  sfree(cleanup_data);
} /* void nginx_host_cleanup */

static int config_host(oconfig_item_t *host_config_item)
{
  nginx_host_config_t *host_config;
  
  oconfig_item_t *config_parameter;
  char *host_config_key;
  char *host_config_value;
  int status;

  if ((host_config_item->values_num != 1) || (host_config_item->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("nginx_multihost plugin: The `Host' block "
             "needs exactly one string argument.");
    return (-1);
  }

  host_config = calloc (1, sizeof (*host_config));

  if (host_config == NULL) {
    ERROR ("nginx_multihost plugin: calloc failed.");
    return -1;
  }

  host_config->host_name = strdup (host_config_item->values[0].value.string);

  for (int i = 0; i < host_config_item->children_num; i++)
  {
    config_parameter = host_config_item->children + i;
    host_config_key = config_parameter->key;
    host_config_value = config_parameter->values[0].value.string;

    status = config_host_parameter(host_config, host_config_key, host_config_value);

    if (status != 0)
    {
      DEBUG ("nginx_multihost plugin: config parsing failed.");
      sfree (host_config);
      return -1;
    }
  }

  status = init_curl(host_config);

  if (status != 0) {
    DEBUG ("nginx_multihost plugin: curl initialization failed.");
    sfree (host_config);
    return -1;
  }

  user_data_t ud;
  char cb_name[DATA_MAX_NAME_LEN];

  DEBUG ("nginx_multihost plugin: Registering new read callback for host %s (%s).", host_config->url, host_config->host_name);

  memset (&ud, 0, sizeof (ud));

  ud.data = host_config;
  ud.free_func = nginx_host_cleanup;

  ssnprintf (cb_name, sizeof (cb_name), "nginx_multihost.%s", host_config->host_name);

  plugin_register_complex_read (NULL,
                                cb_name,
                                nginx_read,
                                0,
                                &ud);

  return 0;
} /* config_host */

static int config_plugin(oconfig_item_t *ci)
{
  for (int i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Host", child->key) == 0)
    {
      if (config_host(child) != 0)
          return -1;
    }
    else
    {
      WARNING ("nginx_multihost plugin: Ignoring unknown config option `%s'.", child->key);
    }
  }

  return 0;
} /* int config_plugin */

void module_register (void)
{
  plugin_register_complex_config ("nginx_multihost", config_plugin);
} /* void module_register */

/*
 * vim: set shiftwidth=2 softtabstop=2 tabstop=8 :
 */
