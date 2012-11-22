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

static char *races_list[] = /* {{{ */
{
  NULL,
  "Human",    /*  1 */
  "Orc",      /*  2 */
  "Dwarf",    /*  3 */
  "Nightelf", /*  4 */
  "Undead",   /*  5 */
  "Tauren",   /*  6 */
  "Gnome",    /*  7 */
  "Troll",    /*  8 */
  NULL,
  "Bloodelf", /* 10 */
  "Draenei"   /* 11 */
}; /* }}} */
#define RACES_LIST_LENGTH STATIC_ARRAY_SIZE (races_list)

static char *classes_list[] = /* {{{ */
{
  NULL,
  "Warrior", /*  1 */
  "Paladin", /*  2 */
  "Hunter",  /*  3 */
  "Rogue",   /*  4 */
  "Priest",  /*  5 */
  NULL,
  "Shaman",  /*  7 */
  "Mage",    /*  8 */
  "Warlock", /*  9 */
  NULL,
  "Druid"    /* 11 */
}; /* }}} */
#define CLASSES_LIST_LENGTH STATIC_ARRAY_SIZE (classes_list)

static char *genders_list[] = /* {{{ */
{
  "Male",
  "Female"
}; /* }}} */
#define GENDERS_LIST_LENGTH STATIC_ARRAY_SIZE (genders_list)

struct player_stats_s
{
  int races[RACES_LIST_LENGTH];
  int classes[CLASSES_LIST_LENGTH];
  int genders[GENDERS_LIST_LENGTH];
  int level_sum;
  int level_num;
  int latency_sum;
  int latency_num;
};
typedef struct player_stats_s player_stats_t;

struct player_info_s
{
  int race;
  int class;
  int gender;
  int level;
  int latency;
};
typedef struct player_info_s player_info_t;
#define PLAYER_INFO_STATIC_INIT { -1, -1, -1, -1, -1 }

static char *url         = NULL;
static char *user        = NULL;
static char *pass        = NULL;
static char *verify_peer = NULL;
static char *verify_host = NULL;
static char *cacert      = NULL;

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
  "VerifyPeer",
  "VerifyHost",
  "CACert"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static int ascent_submit_gauge (const char *plugin_instance, /* {{{ */
    const char *type, const char *type_instance, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "ascent", sizeof (vl.plugin));

  if (plugin_instance != NULL)
    sstrncpy (vl.plugin_instance, plugin_instance,
        sizeof (vl.plugin_instance));

  sstrncpy (vl.type, type, sizeof (vl.type));

  if (type_instance != NULL)
    sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
  return (0);
} /* }}} int ascent_submit_gauge */

static size_t ascent_curl_callback (void *buf, size_t size, size_t nmemb, /* {{{ */
    void __attribute__((unused)) *stream)
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

static int ascent_submit_players (player_stats_t *ps) /* {{{ */
{
  size_t i;
  gauge_t value;

  for (i = 0; i < RACES_LIST_LENGTH; i++)
    if (races_list[i] != NULL)
      ascent_submit_gauge ("by-race", "players", races_list[i],
          (gauge_t) ps->races[i]);

  for (i = 0; i < CLASSES_LIST_LENGTH; i++)
    if (classes_list[i] != NULL)
      ascent_submit_gauge ("by-class", "players", classes_list[i],
          (gauge_t) ps->classes[i]);

  for (i = 0; i < GENDERS_LIST_LENGTH; i++)
    if (genders_list[i] != NULL)
      ascent_submit_gauge ("by-gender", "players", genders_list[i],
          (gauge_t) ps->genders[i]);

  if (ps->level_num <= 0)
    value = NAN;
  else
    value = ((double) ps->level_sum) / ((double) ps->level_num);
  ascent_submit_gauge (NULL, "gauge", "avg-level", value);

  /* Latency is in ms, but we store seconds. */
  if (ps->latency_num <= 0)
    value = NAN;
  else
    value = ((double) ps->latency_sum) / (1000.0 * ((double) ps->latency_num));
  ascent_submit_gauge (NULL, "latency", "average", value);

  return (0);
} /* }}} int ascent_submit_players */

static int ascent_account_player (player_stats_t *ps, /* {{{ */
    player_info_t *pi)
{
  if (pi->race >= 0)
  {
    if (((size_t) pi->race >= RACES_LIST_LENGTH)
        || (races_list[pi->race] == NULL))
      ERROR ("ascent plugin: Ignoring invalid numeric race %i.", pi->race);
    else
      ps->races[pi->race]++;
  }

  if (pi->class >= 0)
  {
    if (((size_t) pi->class >= CLASSES_LIST_LENGTH)
        || (classes_list[pi->class] == NULL))
      ERROR ("ascent plugin: Ignoring invalid numeric class %i.", pi->class);
    else
      ps->classes[pi->class]++;
  }

  if (pi->gender >= 0)
  {
    if (((size_t) pi->gender >= GENDERS_LIST_LENGTH)
        || (genders_list[pi->gender] == NULL))
      ERROR ("ascent plugin: Ignoring invalid numeric gender %i.",
          pi->gender);
    else
      ps->genders[pi->gender]++;
  }


  if (pi->level > 0)
  {
    ps->level_sum += pi->level;
    ps->level_num++;
  }

  if (pi->latency >= 0)
  {
    ps->latency_sum += pi->latency;
    ps->latency_num++;
  }

  return (0);
} /* }}} int ascent_account_player */

static int ascent_xml_submit_gauge (xmlDoc *doc, xmlNode *node, /* {{{ */
    const char *plugin_instance, const char *type, const char *type_instance)
{
  char *str_ptr;
  gauge_t value;

  str_ptr = (char *) xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
  if (str_ptr == NULL)
  {
    ERROR ("ascent plugin: ascent_xml_submit_gauge: xmlNodeListGetString failed.");
    return (-1);
  }

  if (strcasecmp ("N/A", str_ptr) == 0)
    value = NAN;
  else
  {
    char *end_ptr = NULL;
    value = strtod (str_ptr, &end_ptr);
    if (str_ptr == end_ptr)
    {
      xmlFree(str_ptr);
      ERROR ("ascent plugin: ascent_xml_submit_gauge: strtod failed.");
      return (-1);
    }
  }
  xmlFree(str_ptr);

  return (ascent_submit_gauge (plugin_instance, type, type_instance, value));
} /* }}} int ascent_xml_submit_gauge */

static int ascent_xml_read_int (xmlDoc *doc, xmlNode *node, /* {{{ */
    int *ret_value)
{
  char *str_ptr;
  int value;

  str_ptr = (char *) xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
  if (str_ptr == NULL)
  {
    ERROR ("ascent plugin: ascent_xml_read_int: xmlNodeListGetString failed.");
    return (-1);
  }

  if (strcasecmp ("N/A", str_ptr) == 0)
    value = -1;
  else
  {
    char *end_ptr = NULL;
    value = strtol (str_ptr, &end_ptr, 0);
    if (str_ptr == end_ptr)
    {
      xmlFree(str_ptr);
      ERROR ("ascent plugin: ascent_xml_read_int: strtol failed.");
      return (-1);
    }
  }
  xmlFree(str_ptr);

  *ret_value = value;
  return (0);
} /* }}} int ascent_xml_read_int */

static int ascent_xml_sessions_plr (xmlDoc *doc, xmlNode *node, /* {{{ */
    player_info_t *pi)
{
  xmlNode *child;

  for (child = node->xmlChildrenNode; child != NULL; child = child->next)
  {
    if ((xmlStrcmp ((const xmlChar *) "comment", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "text", child->name) == 0))
      /* ignore */;
    else if (xmlStrcmp ((const xmlChar *) "race", child->name) == 0)
      ascent_xml_read_int (doc, child, &pi->race);
    else if (xmlStrcmp ((const xmlChar *) "class", child->name) == 0)
      ascent_xml_read_int (doc, child, &pi->class);
    else if (xmlStrcmp ((const xmlChar *) "gender", child->name) == 0)
      ascent_xml_read_int (doc, child, &pi->gender);
    else if (xmlStrcmp ((const xmlChar *) "level", child->name) == 0)
      ascent_xml_read_int (doc, child, &pi->level);
    else if (xmlStrcmp ((const xmlChar *) "latency", child->name) == 0)
      ascent_xml_read_int (doc, child, &pi->latency);
    else if ((xmlStrcmp ((const xmlChar *) "name", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "pvprank", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "map", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "areaid", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "xpos", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "ypos", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "onime", child->name) == 0))
      /* ignore */;
    else
    {
      WARNING ("ascent plugin: ascent_xml_status: Unknown tag: %s", child->name);
    }
  } /* for (child) */

  return (0);
} /* }}} int ascent_xml_sessions_plr */

static int ascent_xml_sessions (xmlDoc *doc, xmlNode *node) /* {{{ */
{
  xmlNode *child;
  player_stats_t ps;

  memset (&ps, 0, sizeof (ps));

  for (child = node->xmlChildrenNode; child != NULL; child = child->next)
  {
    if ((xmlStrcmp ((const xmlChar *) "comment", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "text", child->name) == 0))
      /* ignore */;
    else if (xmlStrcmp ((const xmlChar *) "plr", child->name) == 0)
    {
      int status;
      player_info_t pi = PLAYER_INFO_STATIC_INIT;

      status = ascent_xml_sessions_plr (doc, child, &pi);
      if (status == 0)
        ascent_account_player (&ps, &pi);
    }
    else
    {
      WARNING ("ascent plugin: ascent_xml_status: Unknown tag: %s", child->name);
    }
  } /* for (child) */

  ascent_submit_players (&ps);

  return (0);
} /* }}} int ascent_xml_sessions */

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
    else if ((xmlStrcmp ((const xmlChar *) "acceptedconns", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "avglat", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "cdbquerysize", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "cpu", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "fthreads", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "gmcount", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "lastupdate", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "ontime", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "oplayers", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "peakcount", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "platform", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "ram", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "threads", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "uptime", child->name) == 0)
        || (xmlStrcmp ((const xmlChar *) "wdbquerysize", child->name) == 0))
      /* ignore */;
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
      ascent_xml_sessions (doc, child);
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
  else if (strcasecmp (key, "VerifyPeer") == 0)
    return (config_set (&verify_peer, value));
  else if (strcasecmp (key, "VerifyHost") == 0)
    return (config_set (&verify_host, value));
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

  curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, ascent_curl_callback);
  curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
  curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, ascent_curl_error);

  if (user != NULL)
  {
    int status;

    status = ssnprintf (credentials, sizeof (credentials), "%s:%s",
        user, (pass == NULL) ? "" : pass);
    if ((status < 0) || ((size_t) status >= sizeof (credentials)))
    {
      ERROR ("ascent plugin: ascent_init: Returning an error because the "
          "credentials have been truncated.");
      return (-1);
    }

    curl_easy_setopt (curl, CURLOPT_USERPWD, credentials);
  }

  curl_easy_setopt (curl, CURLOPT_URL, url);
  curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1);

  if ((verify_peer == NULL) || IS_TRUE (verify_peer))
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 1);
  else
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYPEER, 0);

  if ((verify_host == NULL) || IS_TRUE (verify_host))
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 2);
  else
    curl_easy_setopt (curl, CURLOPT_SSL_VERIFYHOST, 0);

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
