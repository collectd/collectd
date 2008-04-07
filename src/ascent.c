/**
 * collectd - src/ascent.c
 * Copyright (C) 2008  Florian octo Forster
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

#include <curl/curl.h>
#include <libxml/parser.h>

static char *url    = NULL;
static char *user   = NULL;
static char *pass   = NULL;
static char *cacert = NULL;

static CURL *curl = NULL;

static char  *ascent_buffer = NULL;
static size_t ascent_buffer_size = 0;
static size_t ascent_buffer_fill = 0;
static char   ascent_curl_error[CURL_ERROR_SIZE];

static const char *config_keys[] =
{
  "URL",
  "User",
  "Password",
  "CACert"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static size_t ascent_curl_callback (void *buf, size_t size, size_t nmemb, /* {{{ */
    void *stream)
{
  size_t len = size * nmemb;

  if (len <= 0)
    return (len);

  if ((ascent_buffer_fill + len) >= ascent_buffer_size)
  {
    char *temp;

    temp = (char *) realloc (ascent_buffer,
        ascent_buffer_fill + len + 1);
    if (temp == NULL)
    {
      ERROR ("ascent plugin: realloc failed.");
      return (0);
    }
    ascent_buffer = temp;
    ascent_buffer_size = ascent_buffer_fill + len + 1;
  }

  memcpy (ascent_buffer + ascent_buffer_fill, (char *) buf, len);
  ascent_buffer_fill += len;
  ascent_buffer[ascent_buffer_fill] = 0;

  return (len);
} /* }}} size_t ascent_curl_callback */

static int ascent_xml_submit_gauge (xmlDoc *doc, xmlNode *node, /* {{{ */
    const char *plugin_instance, const char *type, const char *type_instance)
{
  char *str_ptr;

  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  str_ptr = (char *) xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
  if (str_ptr == NULL)
  {
    ERROR ("ascent plugin: ascent_xml_submit_gauge: xmlNodeListGetString failed.");
    return (-1);
  }

  if (strcasecmp ("N/A", str_ptr) == 0)
    values[0].gauge = NAN;
  else
  {
    char *end_ptr = NULL;
    values[0].gauge = strtod (str_ptr, &end_ptr);
    if (str_ptr == end_ptr)
    {
      ERROR ("ascent plugin: ascent_xml_submit_gauge: strtod failed.");
      return (-1);
    }
  }

  vl.values = values;
  vl.values_len = 1;
  vl.time = time (NULL);
  strcpy (vl.host, hostname_g);
  strcpy (vl.plugin, "ascent");

  if (plugin_instance != NULL)
    sstrncpy (vl.plugin_instance, plugin_instance,
        sizeof (vl.plugin_instance));

  if (type_instance != NULL)
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (type, &vl);
} /* }}} int ascent_xml_submit_gauge */

static int ascent_xml_status (xmlDoc *doc, xmlNode *node) /* {{{ */
{
  xmlNode *child;

  for (child = node->xmlChildrenNode; child != NULL; child = child->next)
  {
    if ((xmlStrcmp ((const xmlChar *) "comment", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "text", child->name) == 0))
      /* ignore */;
    else if (xmlStrcmp ((const xmlChar *) "alliance", child->name) == 0)
      ascent_xml_submit_gauge (doc, child, NULL, "players", "alliance");
    else if (xmlStrcmp ((const xmlChar *) "horde", child->name) == 0)
      ascent_xml_submit_gauge (doc, child, NULL, "players", "horde");
    else if (xmlStrcmp ((const xmlChar *) "qplayers", child->name) == 0)
      ascent_xml_submit_gauge (doc, child, NULL, "players", "queued");
    else
    {
      WARNING ("ascent plugin: ascent_xml_status: Unknown tag: %s", child->name);
    }
  } /* for (child) */

  return (0);
} /* }}} int ascent_xml_status */

static int ascent_xml (const char *data) /* {{{ */
{
  xmlDoc *doc;
  xmlNode *cur;
  xmlNode *child;

#if 0
  doc = xmlParseMemory (data, strlen (data),
      /* URL = */ "ascent.xml",
      /* encoding = */ NULL,
      /* options = */ 0);
#else
  doc = xmlParseMemory (data, strlen (data));
#endif
  if (doc == NULL)
  {
    ERROR ("ascent plugin: xmlParseMemory failed.");
    return (-1);
  }

  cur = xmlDocGetRootElement (doc);
  if (cur == NULL)
  {
    ERROR ("ascent plugin: XML document is empty.");
    xmlFreeDoc (doc);
    return (-1);
  }

  if (xmlStrcmp ((const xmlChar *) "serverpage", cur->name) != 0)
  {
    ERROR ("ascent plugin: XML root element is not \"serverpage\".");
    xmlFreeDoc (doc);
    return (-1);
  }

  for (child = cur->xmlChildrenNode; child != NULL; child = child->next)
  {
    if ((xmlStrcmp ((const xmlChar *) "comment", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "text", child->name) == 0))
      /* ignore */;
    else if (xmlStrcmp ((const xmlChar *) "status", child->name) == 0)
      ascent_xml_status (doc, child);
    else if (xmlStrcmp ((const xmlChar *) "instances", child->name) == 0)
      /* ignore for now */;
    else if (xmlStrcmp ((const xmlChar *) "gms", child->name) == 0)
      /* ignore for now */;
    else if (xmlStrcmp ((const xmlChar *) "sessions", child->name) == 0)
      /* ignore for now */;
    else
    {
      WARNING ("ascent plugin: ascent_xml: Unknown tag: %s", child->name);
    }
  } /* for (child) */

  xmlFreeDoc (doc);
  return (0);
} /* }}} int ascent_xml */

static int config_set (char **var, const char *value) /* {{{ */
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
} /* }}} int config_set */

static int ascent_config (const char *key, const char *value) /* {{{ */
{
  if (strcasecmp (key, "URL") == 0)
    return (config_set (&url, value));
  else if (strcasecmp (key, "User") == 0)
    return (config_set (&user, value));
  else if (strcasecmp (key, "Password") == 0)
    return (config_set (&pass, value));
  else if (strcasecmp (key, "CACert") == 0)
    return (config_set (&cacert, value));
  else
    return (-1);
} /* }}} int ascent_config */

static int ascent_init (void) /* {{{ */
{
  static char credentials[1024];

  if (url == NULL)
  {
    WARNING ("ascent plugin: ascent_init: No URL configured, "
        "returning an error.");
    return (-1);
  }

  if (curl != NULL)
  {
    curl_easy_cleanup (curl);
  }

  if ((curl = curl_easy_init ()) == NULL)
  {
    ERROR ("ascent plugin: ascent_init: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, ascent_curl_callback);
  curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
  curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, ascent_curl_error);

  if (user != NULL)
  {
    int status;

    status = snprintf (credentials, sizeof (credentials), "%s:%s",
        user, (pass == NULL) ? "" : pass);
    if (status >= sizeof (credentials))
    {
      ERROR ("ascent plugin: ascent_init: Returning an error because the "
          "credentials have been truncated.");
      return (-1);
    }
    credentials[sizeof (credentials) - 1] = '\0';

    curl_easy_setopt (curl, CURLOPT_USERPWD, credentials);
  }

  curl_easy_setopt (curl, CURLOPT_URL, url);

  if (cacert != NULL)
    curl_easy_setopt (curl, CURLOPT_CAINFO, cacert);

  return (0);
} /* }}} int ascent_init */

static int ascent_read (void) /* {{{ */
{
  int status;

  if (curl == NULL)
  {
    ERROR ("ascent plugin: I don't have a CURL object.");
    return (-1);
  }

  if (url == NULL)
  {
    ERROR ("ascent plugin: No URL has been configured.");
    return (-1);
  }

  ascent_buffer_fill = 0;
  if (curl_easy_perform (curl) != 0)
  {
    ERROR ("ascent plugin: curl_easy_perform failed: %s",
        ascent_curl_error);
    return (-1);
  }

  status = ascent_xml (ascent_buffer);
  if (status != 0)
    return (-1);
  else
    return (0);
} /* }}} int ascent_read */

void module_register (void)
{
  plugin_register_config ("ascent", ascent_config, config_keys, config_keys_num);
  plugin_register_init ("ascent", ascent_init);
  plugin_register_read ("ascent", ascent_read);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 et fdm=marker : */
