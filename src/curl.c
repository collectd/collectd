/**
 * collectd - src/curl.c
 * Copyright (C) 2006-2009  Florian octo Forster
 * Copyright (C) 2009       Aman Gupta
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
 *   Aman Gupta <aman at tmm1.net>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_match.h"

#include <curl/curl.h>

/*
 * Data types
 */
struct web_match_s;
typedef struct web_match_s web_match_t;
struct web_match_s /* {{{ */
{
  char *regex;
  char *exclude_regex;
  int dstype;
  char *type;
  char *instance;

  cu_match_t *match;

  web_match_t *next;
}; /* }}} */

struct web_page_s;
typedef struct web_page_s web_page_t;
struct web_page_s /* {{{ */
{
  char *instance;

  char *url;
  char *user;
  char *pass;
  char *credentials;
  int   verify_peer;
  int   verify_host;
  char *cacert;
  int   response_time;

  CURL *curl;
  char curl_errbuf[CURL_ERROR_SIZE];
  char *buffer;
  size_t buffer_size;
  size_t buffer_fill;

  web_match_t *matches;

  web_page_t *next;
}; /* }}} */

/*
 * Global variables;
 */
/* static CURLM *curl = NULL; */
static web_page_t *pages_g = NULL;

/*
 * Private functions
 */
static size_t cc_curl_callback (void *buf, /* {{{ */
    size_t size, size_t nmemb, void *user_data)
{
  web_page_t *wp;
  size_t len;
  
  len = size * nmemb;
  if (len <= 0)
    return (len);

  wp = user_data;
  if (wp == NULL)
    return (0);

  if ((wp->buffer_fill + len) >= wp->buffer_size)
  {
    char *temp;
    size_t temp_size;

    temp_size = wp->buffer_fill + len + 1;
    temp = (char *) realloc (wp->buffer, temp_size);
    if (temp == NULL)
    {
      ERROR ("curl plugin: realloc failed.");
      return (0);
    }
    wp->buffer = temp;
    wp->buffer_size = temp_size;
  }

  memcpy (wp->buffer + wp->buffer_fill, (char *) buf, len);
  wp->buffer_fill += len;
  wp->buffer[wp->buffer_fill] = 0;

  return (len);
} /* }}} size_t cc_curl_callback */

static void cc_web_match_free (web_match_t *wm) /* {{{ */
{
  if (wm == NULL)
    return;

  sfree (wm->regex);
  sfree (wm->type);
  sfree (wm->instance);
  match_destroy (wm->match);
  cc_web_match_free (wm->next);
  sfree (wm);
} /* }}} void cc_web_match_free */

static void cc_web_page_free (web_page_t *wp) /* {{{ */
{
  if (wp == NULL)
    return;

  if (wp->curl != NULL)
    curl_easy_cleanup (wp->curl);
  wp->curl = NULL;

  sfree (wp->instance);

  sfree (wp->url);
  sfree (wp->user);
  sfree (wp->pass);
  sfree (wp->credentials);
  sfree (wp->cacert);

  sfree (wp->buffer);

  cc_web_match_free (wp->matches);
  cc_web_page_free (wp->next);
  sfree (wp);
} /* }}} void cc_web_page_free */

static int cc_config_add_string (const char *name, char **dest, /* {{{ */
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("curl plugin: `%s' needs exactly one string argument.", name);
    return (-1);
  }

  sfree (*dest);
  *dest = strdup (ci->values[0].value.string);
  if (*dest == NULL)
    return (-1);

  return (0);
} /* }}} int cc_config_add_string */

static int cc_config_set_boolean (const char *name, int *dest, /* {{{ */
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    WARNING ("curl plugin: `%s' needs exactly one boolean argument.", name);
    return (-1);
  }

  *dest = ci->values[0].value.boolean ? 1 : 0;

  return (0);
} /* }}} int cc_config_set_boolean */

static int cc_config_add_match_dstype (int *dstype_ret, /* {{{ */
    oconfig_item_t *ci)
{
  int dstype;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("curl plugin: `DSType' needs exactly one string argument.");
    return (-1);
  }

  if (strncasecmp ("Gauge", ci->values[0].value.string,
        strlen ("Gauge")) == 0)
  {
    dstype = UTILS_MATCH_DS_TYPE_GAUGE;
    if (strcasecmp ("GaugeAverage", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_GAUGE_AVERAGE;
    else if (strcasecmp ("GaugeMin", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_GAUGE_MIN;
    else if (strcasecmp ("GaugeMax", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_GAUGE_MAX;
    else if (strcasecmp ("GaugeLast", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_GAUGE_LAST;
    else
      dstype = 0;
  }
  else if (strncasecmp ("Counter", ci->values[0].value.string,
        strlen ("Counter")) == 0)
  {
    dstype = UTILS_MATCH_DS_TYPE_COUNTER;
    if (strcasecmp ("CounterSet", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_COUNTER_SET;
    else if (strcasecmp ("CounterAdd", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_COUNTER_ADD;
    else if (strcasecmp ("CounterInc", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_COUNTER_INC;
    else
      dstype = 0;
  }
else if (strncasecmp ("Derive", ci->values[0].value.string,
        strlen ("Derive")) == 0)
  {
    dstype = UTILS_MATCH_DS_TYPE_DERIVE;
    if (strcasecmp ("DeriveSet", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_DERIVE_SET;
    else if (strcasecmp ("DeriveAdd", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_DERIVE_ADD;
    else if (strcasecmp ("DeriveInc", ci->values[0].value.string) == 0)
      dstype |= UTILS_MATCH_CF_DERIVE_INC;
    else
      dstype = 0;
  }
else if (strncasecmp ("Absolute", ci->values[0].value.string,
        strlen ("Absolute")) == 0)
  {
    dstype = UTILS_MATCH_DS_TYPE_ABSOLUTE;
    if (strcasecmp ("AbsoluteSet", ci->values[0].value.string) == 0) /* Absolute DS is reset-on-read so no sense doin anything else but set */
      dstype |= UTILS_MATCH_CF_ABSOLUTE_SET;
    else
      dstype = 0;
  }

  else
  {
    dstype = 0;
  }

  if (dstype == 0)
  {
    WARNING ("curl plugin: `%s' is not a valid argument to `DSType'.",
	ci->values[0].value.string);
    return (-1);
  }

  *dstype_ret = dstype;
  return (0);
} /* }}} int cc_config_add_match_dstype */

static int cc_config_add_match (web_page_t *page, /* {{{ */
    oconfig_item_t *ci)
{
  web_match_t *match;
  int status;
  int i;

  if (ci->values_num != 0)
  {
    WARNING ("curl plugin: Ignoring arguments for the `Match' block.");
  }

  match = (web_match_t *) malloc (sizeof (*match));
  if (match == NULL)
  {
    ERROR ("curl plugin: malloc failed.");
    return (-1);
  }
  memset (match, 0, sizeof (*match));

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Regex", child->key) == 0)
      status = cc_config_add_string ("Regex", &match->regex, child);
    else if (strcasecmp ("ExcludeRegex", child->key) == 0)
      status = cc_config_add_string ("ExcludeRegex", &match->exclude_regex, child);
    else if (strcasecmp ("DSType", child->key) == 0)
      status = cc_config_add_match_dstype (&match->dstype, child);
    else if (strcasecmp ("Type", child->key) == 0)
      status = cc_config_add_string ("Type", &match->type, child);
    else if (strcasecmp ("Instance", child->key) == 0)
      status = cc_config_add_string ("Instance", &match->instance, child);
    else
    {
      WARNING ("curl plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  while (status == 0)
  {
    if (match->regex == NULL)
    {
      WARNING ("curl plugin: `Regex' missing in `Match' block.");
      status = -1;
    }

    if (match->type == NULL)
    {
      WARNING ("curl plugin: `Type' missing in `Match' block.");
      status = -1;
    }

    if (match->dstype == 0)
    {
      WARNING ("curl plugin: `DSType' missing in `Match' block.");
      status = -1;
    }

    break;
  } /* while (status == 0) */

  if (status != 0)
    return (status);

  match->match = match_create_simple (match->regex, match->exclude_regex,
      match->dstype);
  if (match->match == NULL)
  {
    ERROR ("curl plugin: tail_match_add_match_simple failed.");
    cc_web_match_free (match);
    return (-1);
  }
  else
  {
    web_match_t *prev;

    prev = page->matches;
    while ((prev != NULL) && (prev->next != NULL))
      prev = prev->next;

    if (prev == NULL)
      page->matches = match;
    else
      prev->next = match;
  }

  return (0);
} /* }}} int cc_config_add_match */

static int cc_page_init_curl (web_page_t *wp) /* {{{ */
{
  wp->curl = curl_easy_init ();
  if (wp->curl == NULL)
  {
    ERROR ("curl plugin: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt (wp->curl, CURLOPT_NOSIGNAL, 1);
  curl_easy_setopt (wp->curl, CURLOPT_WRITEFUNCTION, cc_curl_callback);
  curl_easy_setopt (wp->curl, CURLOPT_WRITEDATA, wp);
  curl_easy_setopt (wp->curl, CURLOPT_USERAGENT,
      PACKAGE_NAME"/"PACKAGE_VERSION);
  curl_easy_setopt (wp->curl, CURLOPT_ERRORBUFFER, wp->curl_errbuf);
  curl_easy_setopt (wp->curl, CURLOPT_URL, wp->url);
  curl_easy_setopt (wp->curl, CURLOPT_FOLLOWLOCATION, 1);

  if (wp->user != NULL)
  {
    size_t credentials_size;

    credentials_size = strlen (wp->user) + 2;
    if (wp->pass != NULL)
      credentials_size += strlen (wp->pass);

    wp->credentials = (char *) malloc (credentials_size);
    if (wp->credentials == NULL)
    {
      ERROR ("curl plugin: malloc failed.");
      return (-1);
    }

    ssnprintf (wp->credentials, credentials_size, "%s:%s",
        wp->user, (wp->pass == NULL) ? "" : wp->pass);
    curl_easy_setopt (wp->curl, CURLOPT_USERPWD, wp->credentials);
  }

  curl_easy_setopt (wp->curl, CURLOPT_SSL_VERIFYPEER, wp->verify_peer);
  curl_easy_setopt (wp->curl, CURLOPT_SSL_VERIFYHOST,
      wp->verify_host ? 2 : 0);
  if (wp->cacert != NULL)
    curl_easy_setopt (wp->curl, CURLOPT_CAINFO, wp->cacert);

  return (0);
} /* }}} int cc_page_init_curl */

static int cc_config_add_page (oconfig_item_t *ci) /* {{{ */
{
  web_page_t *page;
  int status;
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("curl plugin: `Page' blocks need exactly one string argument.");
    return (-1);
  }

  page = (web_page_t *) malloc (sizeof (*page));
  if (page == NULL)
  {
    ERROR ("curl plugin: malloc failed.");
    return (-1);
  }
  memset (page, 0, sizeof (*page));
  page->url = NULL;
  page->user = NULL;
  page->pass = NULL;
  page->verify_peer = 1;
  page->verify_host = 1;
  page->response_time = 0;

  page->instance = strdup (ci->values[0].value.string);
  if (page->instance == NULL)
  {
    ERROR ("curl plugin: strdup failed.");
    sfree (page);
    return (-1);
  }

  /* Process all children */
  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("URL", child->key) == 0)
      status = cc_config_add_string ("URL", &page->url, child);
    else if (strcasecmp ("User", child->key) == 0)
      status = cc_config_add_string ("User", &page->user, child);
    else if (strcasecmp ("Password", child->key) == 0)
      status = cc_config_add_string ("Password", &page->pass, child);
    else if (strcasecmp ("VerifyPeer", child->key) == 0)
      status = cc_config_set_boolean ("VerifyPeer", &page->verify_peer, child);
    else if (strcasecmp ("VerifyHost", child->key) == 0)
      status = cc_config_set_boolean ("VerifyHost", &page->verify_host, child);
    else if (strcasecmp ("MeasureResponseTime", child->key) == 0)
      status = cc_config_set_boolean (child->key, &page->response_time, child);
    else if (strcasecmp ("CACert", child->key) == 0)
      status = cc_config_add_string ("CACert", &page->cacert, child);
    else if (strcasecmp ("Match", child->key) == 0)
      /* Be liberal with failing matches => don't set `status'. */
      cc_config_add_match (page, child);
    else
    {
      WARNING ("curl plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  /* Additionial sanity checks and libCURL initialization. */
  while (status == 0)
  {
    if (page->url == NULL)
    {
      WARNING ("curl plugin: `URL' missing in `Page' block.");
      status = -1;
    }

    if (page->matches == NULL && !page->response_time)
    {
      assert (page->instance != NULL);
      WARNING ("curl plugin: No (valid) `Match' block "
          "or MeasureResponseTime within `Page' block `%s'.", page->instance);
      status = -1;
    }

    if (status == 0)
      status = cc_page_init_curl (page);

    break;
  } /* while (status == 0) */

  if (status != 0)
  {
    cc_web_page_free (page);
    return (status);
  }

  /* Add the new page to the linked list */
  if (pages_g == NULL)
    pages_g = page;
  else
  {
    web_page_t *prev;

    prev = pages_g;
    while ((prev != NULL) && (prev->next != NULL))
      prev = prev->next;
    prev->next = page;
  }

  return (0);
} /* }}} int cc_config_add_page */

static int cc_config (oconfig_item_t *ci) /* {{{ */
{
  int success;
  int errors;
  int status;
  int i;

  success = 0;
  errors = 0;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Page", child->key) == 0)
    {
      status = cc_config_add_page (child);
      if (status == 0)
        success++;
      else
        errors++;
    }
    else
    {
      WARNING ("curl plugin: Option `%s' not allowed here.", child->key);
      errors++;
    }
  }

  if ((success == 0) && (errors > 0))
  {
    ERROR ("curl plugin: All statements failed.");
    return (-1);
  }

  return (0);
} /* }}} int cc_config */

static int cc_init (void) /* {{{ */
{
  if (pages_g == NULL)
  {
    INFO ("curl plugin: No pages have been defined.");
    return (-1);
  }
  return (0);
} /* }}} int cc_init */

static void cc_submit (const web_page_t *wp, const web_match_t *wm, /* {{{ */
    const cu_match_value_t *mv)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0] = mv->value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "curl", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, wp->instance, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, wm->type, sizeof (vl.type));
  sstrncpy (vl.type_instance, wm->instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* }}} void cc_submit */

static void cc_submit_response_time (const web_page_t *wp, double seconds) /* {{{ */
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = seconds;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "curl", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, wp->instance, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, "response_time", sizeof (vl.type));

  plugin_dispatch_values (&vl);
} /* }}} void cc_submit_response_time */

static int cc_read_page (web_page_t *wp) /* {{{ */
{
  web_match_t *wm;
  int status;
  struct timeval start, end;

  if (wp->response_time)
    gettimeofday (&start, NULL);

  wp->buffer_fill = 0;
  status = curl_easy_perform (wp->curl);
  if (status != 0)
  {
    ERROR ("curl plugin: curl_easy_perform failed with staus %i: %s",
        status, wp->curl_errbuf);
    return (-1);
  }

  if (wp->response_time)
  {
    double secs = 0;
    gettimeofday (&end, NULL);
    secs += end.tv_sec - start.tv_sec;
    secs += (end.tv_usec - start.tv_usec) / 1000000.0;
    cc_submit_response_time (wp, secs);
  }

  for (wm = wp->matches; wm != NULL; wm = wm->next)
  {
    cu_match_value_t *mv;

    status = match_apply (wm->match, wp->buffer);
    if (status != 0)
    {
      WARNING ("curl plugin: match_apply failed.");
      continue;
    }

    mv = match_get_user_data (wm->match);
    if (mv == NULL)
    {
      WARNING ("curl plugin: match_get_user_data returned NULL.");
      continue;
    }

    cc_submit (wp, wm, mv);
  } /* for (wm = wp->matches; wm != NULL; wm = wm->next) */

  return (0);
} /* }}} int cc_read_page */

static int cc_read (void) /* {{{ */
{
  web_page_t *wp;

  for (wp = pages_g; wp != NULL; wp = wp->next)
    cc_read_page (wp);

  return (0);
} /* }}} int cc_read */

static int cc_shutdown (void) /* {{{ */
{
  cc_web_page_free (pages_g);
  pages_g = NULL;

  return (0);
} /* }}} int cc_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("curl", cc_config);
  plugin_register_init ("curl", cc_init);
  plugin_register_read ("curl", cc_read);
  plugin_register_shutdown ("curl", cc_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
