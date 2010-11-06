/**
 * collectd - src/memcachec.c
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2006-2009  Florian octo Forster
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
 *   Doug MacEachern <Doug.MacEachern at hyperic.com>
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_match.h"

#include <libmemcached/memcached.h>

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

  char *server;
  char *key;

  memcached_st *memc;
  char *buffer;

  web_match_t *matches;

  web_page_t *next;
}; /* }}} */

/*
 * Global variables;
 */
static web_page_t *pages_g = NULL;

/*
 * Private functions
 */
static void cmc_web_match_free (web_match_t *wm) /* {{{ */
{
  if (wm == NULL)
    return;

  sfree (wm->regex);
  sfree (wm->type);
  sfree (wm->instance);
  match_destroy (wm->match);
  cmc_web_match_free (wm->next);
  sfree (wm);
} /* }}} void cmc_web_match_free */

static void cmc_web_page_free (web_page_t *wp) /* {{{ */
{
  if (wp == NULL)
    return;

  if (wp->memc != NULL)
    memcached_free(wp->memc);
  wp->memc = NULL;

  sfree (wp->instance);
  sfree (wp->server);
  sfree (wp->key);
  sfree (wp->buffer);

  cmc_web_match_free (wp->matches);
  cmc_web_page_free (wp->next);
  sfree (wp);
} /* }}} void cmc_web_page_free */

static int cmc_page_init_memc (web_page_t *wp) /* {{{ */
{
  memcached_server_st *server;

  wp->memc = memcached_create(NULL);
  if (wp->memc == NULL)
  {
    ERROR ("memcachec plugin: memcached_create failed.");
    return (-1);
  }

  server = memcached_servers_parse (wp->server);
  memcached_server_push (wp->memc, server);
  memcached_server_list_free (server);

  return (0);
} /* }}} int cmc_page_init_memc */

static int cmc_config_add_string (const char *name, char **dest, /* {{{ */
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("memcachec plugin: `%s' needs exactly one string argument.", name);
    return (-1);
  }

  sfree (*dest);
  *dest = strdup (ci->values[0].value.string);
  if (*dest == NULL)
    return (-1);

  return (0);
} /* }}} int cmc_config_add_string */

static int cmc_config_add_match_dstype (int *dstype_ret, /* {{{ */
    oconfig_item_t *ci)
{
  int dstype;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("memcachec plugin: `DSType' needs exactly one string argument.");
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
  else
  {
    dstype = 0;
  }

  if (dstype == 0)
  {
    WARNING ("memcachec plugin: `%s' is not a valid argument to `DSType'.",
	ci->values[0].value.string);
    return (-1);
  }

  *dstype_ret = dstype;
  return (0);
} /* }}} int cmc_config_add_match_dstype */

static int cmc_config_add_match (web_page_t *page, /* {{{ */
    oconfig_item_t *ci)
{
  web_match_t *match;
  int status;
  int i;

  if (ci->values_num != 0)
  {
    WARNING ("memcachec plugin: Ignoring arguments for the `Match' block.");
  }

  match = (web_match_t *) malloc (sizeof (*match));
  if (match == NULL)
  {
    ERROR ("memcachec plugin: malloc failed.");
    return (-1);
  }
  memset (match, 0, sizeof (*match));

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Regex", child->key) == 0)
      status = cmc_config_add_string ("Regex", &match->regex, child);
    else if (strcasecmp ("ExcludeRegex", child->key) == 0)
      status = cmc_config_add_string ("ExcludeRegex", &match->exclude_regex, child);
    else if (strcasecmp ("DSType", child->key) == 0)
      status = cmc_config_add_match_dstype (&match->dstype, child);
    else if (strcasecmp ("Type", child->key) == 0)
      status = cmc_config_add_string ("Type", &match->type, child);
    else if (strcasecmp ("Instance", child->key) == 0)
      status = cmc_config_add_string ("Instance", &match->instance, child);
    else
    {
      WARNING ("memcachec plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  while (status == 0)
  {
    if (match->regex == NULL)
    {
      WARNING ("memcachec plugin: `Regex' missing in `Match' block.");
      status = -1;
    }

    if (match->type == NULL)
    {
      WARNING ("memcachec plugin: `Type' missing in `Match' block.");
      status = -1;
    }

    if (match->dstype == 0)
    {
      WARNING ("memcachec plugin: `DSType' missing in `Match' block.");
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
    ERROR ("memcachec plugin: tail_match_add_match_simple failed.");
    cmc_web_match_free (match);
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
} /* }}} int cmc_config_add_match */

static int cmc_config_add_page (oconfig_item_t *ci) /* {{{ */
{
  web_page_t *page;
  int status;
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("memcachec plugin: `Page' blocks need exactly one string argument.");
    return (-1);
  }

  page = (web_page_t *) malloc (sizeof (*page));
  if (page == NULL)
  {
    ERROR ("memcachec plugin: malloc failed.");
    return (-1);
  }
  memset (page, 0, sizeof (*page));
  page->server = NULL;
  page->key = NULL;

  page->instance = strdup (ci->values[0].value.string);
  if (page->instance == NULL)
  {
    ERROR ("memcachec plugin: strdup failed.");
    sfree (page);
    return (-1);
  }

  /* Process all children */
  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Server", child->key) == 0)
      status = cmc_config_add_string ("Server", &page->server, child);
    if (strcasecmp ("Key", child->key) == 0)
      status = cmc_config_add_string ("Key", &page->key, child);
    else if (strcasecmp ("Match", child->key) == 0)
      /* Be liberal with failing matches => don't set `status'. */
      cmc_config_add_match (page, child);
    else
    {
      WARNING ("memcachec plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  /* Additionial sanity checks and libmemcached initialization. */
  while (status == 0)
  {
    if (page->server == NULL)
    {
      WARNING ("memcachec plugin: `Server' missing in `Page' block.");
      status = -1;
    }

    if (page->key == NULL)
    {
      WARNING ("memcachec plugin: `Key' missing in `Page' block.");
      status = -1;
    }

    if (page->matches == NULL)
    {
      assert (page->instance != NULL);
      WARNING ("memcachec plugin: No (valid) `Match' block "
          "within `Page' block `%s'.", page->instance);
      status = -1;
    }

    if (status == 0)
      status = cmc_page_init_memc (page);

    break;
  } /* while (status == 0) */

  if (status != 0)
  {
    cmc_web_page_free (page);
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
} /* }}} int cmc_config_add_page */

static int cmc_config (oconfig_item_t *ci) /* {{{ */
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
      status = cmc_config_add_page (child);
      if (status == 0)
        success++;
      else
        errors++;
    }
    else
    {
      WARNING ("memcachec plugin: Option `%s' not allowed here.", child->key);
      errors++;
    }
  }

  if ((success == 0) && (errors > 0))
  {
    ERROR ("memcachec plugin: All statements failed.");
    return (-1);
  }

  return (0);
} /* }}} int cmc_config */

static int cmc_init (void) /* {{{ */
{
  if (pages_g == NULL)
  {
    INFO ("memcachec plugin: No pages have been defined.");
    return (-1);
  }
  return (0);
} /* }}} int cmc_init */

static void cmc_submit (const web_page_t *wp, const web_match_t *wm, /* {{{ */
    const cu_match_value_t *mv)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0] = mv->value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "memcachec", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, wp->instance, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, wm->type, sizeof (vl.type));
  sstrncpy (vl.type_instance, wm->instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* }}} void cmc_submit */

static int cmc_read_page (web_page_t *wp) /* {{{ */
{
  web_match_t *wm;
  memcached_return rc;
  size_t string_length;
  uint32_t flags;
  int status;

  if (wp->memc == NULL)
    return (-1);

  wp->buffer = memcached_get (wp->memc, wp->key, strlen (wp->key),
                              &string_length, &flags, &rc);
  if (rc != MEMCACHED_SUCCESS)
  {
    ERROR ("memcachec plugin: memcached_get failed: %s",
        memcached_strerror (wp->memc, rc));
    return (-1);
  }

  for (wm = wp->matches; wm != NULL; wm = wm->next)
  {
    cu_match_value_t *mv;

    status = match_apply (wm->match, wp->buffer);
    if (status != 0)
    {
      WARNING ("memcachec plugin: match_apply failed.");
      continue;
    }

    mv = match_get_user_data (wm->match);
    if (mv == NULL)
    {
      WARNING ("memcachec plugin: match_get_user_data returned NULL.");
      continue;
    }

    cmc_submit (wp, wm, mv);
  } /* for (wm = wp->matches; wm != NULL; wm = wm->next) */

  sfree (wp->buffer);

  return (0);
} /* }}} int cmc_read_page */

static int cmc_read (void) /* {{{ */
{
  web_page_t *wp;

  for (wp = pages_g; wp != NULL; wp = wp->next)
    cmc_read_page (wp);

  return (0);
} /* }}} int cmc_read */

static int cmc_shutdown (void) /* {{{ */
{
  cmc_web_page_free (pages_g);
  pages_g = NULL;

  return (0);
} /* }}} int cmc_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("memcachec", cmc_config);
  plugin_register_init ("memcachec", cmc_init);
  plugin_register_read ("memcachec", cmc_read);
  plugin_register_shutdown ("memcachec", cmc_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
