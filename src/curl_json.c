/**
 * collectd - src/curl_json.c
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2006-2010  Florian octo Forster
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
 *   Doug MacEachern <dougm at hyperic.com>
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_avltree.h"

#include <curl/curl.h>
#include <yajl/yajl_parse.h>

#define CJ_DEFAULT_HOST "localhost"
#define CJ_KEY_MAGIC 0x43484b59UL /* CHKY */
#define CJ_IS_KEY(key) (key)->magic == CJ_KEY_MAGIC
#define CJ_ANY "*"
#define COUCH_MIN(x,y) ((x) < (y) ? (x) : (y))

struct cj_key_s;
typedef struct cj_key_s cj_key_t;
struct cj_key_s /* {{{ */
{
  char *path;
  char *type;
  char *instance;
  unsigned long magic;
};
/* }}} */

struct cj_s /* {{{ */
{
  char *instance;
  char *host;

  char *url;
  char *user;
  char *pass;
  char *credentials;
  int   verify_peer;
  int   verify_host;
  char *cacert;

  CURL *curl;
  char curl_errbuf[CURL_ERROR_SIZE];

  yajl_handle yajl;
  c_avl_tree_t *tree;
  cj_key_t *key;
  int depth;
  struct {
    union {
      c_avl_tree_t *tree;
      cj_key_t *key;
    };
    char name[DATA_MAX_NAME_LEN];
  } state[YAJL_MAX_DEPTH];
};
typedef struct cj_s cj_t; /* }}} */

static int cj_read (user_data_t *ud);
static int cj_curl_perform (cj_t *db, CURL *curl);
static void cj_submit (cj_t *db, cj_key_t *key, value_t *value);

static size_t cj_curl_callback (void *buf, /* {{{ */
    size_t size, size_t nmemb, void *user_data)
{
  cj_t *db;
  size_t len;
  yajl_status status;

  len = size * nmemb;

  if (len <= 0)
    return (len);

  db = user_data;
  if (db == NULL)
    return (0);

  status = yajl_parse(db->yajl, (unsigned char *)buf, len);
  if (status == yajl_status_ok)
  {
    status = yajl_parse_complete(db->yajl);
    return (len);
  }
  else if (status == yajl_status_insufficient_data)
    return (len);

  if (status != yajl_status_ok)
  {
    unsigned char *msg =
      yajl_get_error(db->yajl, 1, (unsigned char *)buf, len);
    ERROR ("curl_json plugin: yajl_parse failed: %s", msg);
    yajl_free_error(db->yajl, msg);
    return (0); /* abort write callback */
  }

  return (len);
} /* }}} size_t cj_curl_callback */

static int cj_get_type (cj_key_t *key)
{
  const data_set_t *ds;

  ds = plugin_get_ds (key->type);
  if (ds == NULL)
    return -1; /* let plugin_write do the complaining */
  else
    return ds->ds[0].type; /* XXX support ds->ds_len > 1 */
}

/* yajl callbacks */
#define CJ_CB_ABORT    0
#define CJ_CB_CONTINUE 1

/* "number" may not be null terminated, so copy it into a buffer before
 * parsing. */
static int cj_cb_number (void *ctx,
    const char *number, unsigned int number_len)
{
  char buffer[number_len + 1];

  cj_t *db = (cj_t *)ctx;
  cj_key_t *key = db->state[db->depth].key;
  char *endptr;
  value_t vt;
  int type;

  if (key == NULL)
    return (CJ_CB_CONTINUE);

  memcpy (buffer, number, number_len);
  buffer[sizeof (buffer) - 1] = 0;

  type = cj_get_type (key);

  endptr = NULL;
  errno = 0;

  if (type == DS_TYPE_COUNTER)
    vt.counter = (counter_t) strtoull (buffer, &endptr, /* base = */ 0);
  else if (type == DS_TYPE_GAUGE)
    vt.gauge = (gauge_t) strtod (buffer, &endptr);
  else if (type == DS_TYPE_DERIVE)
    vt.derive = (derive_t) strtoll (buffer, &endptr, /* base = */ 0);
  else if (type == DS_TYPE_ABSOLUTE)
    vt.absolute = (absolute_t) strtoull (buffer, &endptr, /* base = */ 0);
  else
  {
    ERROR ("curl_json plugin: Unknown data source type: \"%s\"", key->type);
    return (CJ_CB_ABORT);
  }

  if ((endptr == &buffer[0]) || (errno != 0))
  {
    NOTICE ("curl_json plugin: Overflow while parsing number. "
        "Ignoring this value.");
    return (CJ_CB_CONTINUE);
  }

  cj_submit (db, key, &vt);
  return (CJ_CB_CONTINUE);
} /* int cj_cb_number */

static int cj_cb_map_key (void *ctx, const unsigned char *val,
                            unsigned int len)
{
  cj_t *db = (cj_t *)ctx;
  c_avl_tree_t *tree;

  tree = db->state[db->depth-1].tree;

  if (tree != NULL)
  {
    cj_key_t *value;
    char *name;

    name = db->state[db->depth].name;
    len = COUCH_MIN(len, sizeof (db->state[db->depth].name)-1);
    sstrncpy (name, (char *)val, len+1);

    if (c_avl_get (tree, name, (void *) &value) == 0)
      db->state[db->depth].key = value;
    else if (c_avl_get (tree, CJ_ANY, (void *) &value) == 0)
      db->state[db->depth].key = value;
    else
      db->state[db->depth].key = NULL;
  }

  return (CJ_CB_CONTINUE);
}

static int cj_cb_string (void *ctx, const unsigned char *val,
                           unsigned int len)
{
  cj_t *db = (cj_t *)ctx;
  c_avl_tree_t *tree;
  char *ptr;

  if (db->depth != 1) /* e.g. _all_dbs */
    return (CJ_CB_CONTINUE);

  cj_cb_map_key (ctx, val, len); /* same logic */

  tree = db->state[db->depth].tree;

  if ((tree != NULL) && (ptr = rindex (db->url, '/')))
  {
    char url[PATH_MAX];
    CURL *curl;

    /* url =~ s,[^/]+$,$name, */
    len = (ptr - db->url) + 1;
    ptr = url;
    sstrncpy (ptr, db->url, sizeof (url));
    sstrncpy (ptr + len, db->state[db->depth].name, sizeof (url) - len);

    curl = curl_easy_duphandle (db->curl);
    curl_easy_setopt (curl, CURLOPT_URL, url);
    cj_curl_perform (db, curl);
    curl_easy_cleanup (curl);
  }
  return (CJ_CB_CONTINUE);
}

static int cj_cb_start (void *ctx)
{
  cj_t *db = (cj_t *)ctx;
  if (++db->depth >= YAJL_MAX_DEPTH)
  {
    ERROR ("curl_json plugin: %s depth exceeds max, aborting.", db->url);
    return (CJ_CB_ABORT);
  }
  return (CJ_CB_CONTINUE);
}

static int cj_cb_end (void *ctx)
{
  cj_t *db = (cj_t *)ctx;
  db->state[db->depth].tree = NULL;
  --db->depth;
  return (CJ_CB_CONTINUE);
}

static int cj_cb_start_map (void *ctx)
{
  return cj_cb_start (ctx);
}

static int cj_cb_end_map (void *ctx)
{
  return cj_cb_end (ctx);
}

static int cj_cb_start_array (void * ctx)
{
  return cj_cb_start (ctx);
}

static int cj_cb_end_array (void * ctx)
{
  return cj_cb_start (ctx);
}

static yajl_callbacks ycallbacks = {
  NULL, /* null */
  NULL, /* boolean */
  NULL, /* integer */
  NULL, /* double */
  cj_cb_number,
  cj_cb_string,
  cj_cb_start_map,
  cj_cb_map_key,
  cj_cb_end_map,
  cj_cb_start_array,
  cj_cb_end_array
};

/* end yajl callbacks */

static void cj_key_free (cj_key_t *key) /* {{{ */
{
  if (key == NULL)
    return;

  sfree (key->path);
  sfree (key->type);
  sfree (key->instance);

  sfree (key);
} /* }}} void cj_key_free */

static void cj_tree_free (c_avl_tree_t *tree) /* {{{ */
{
  char *name;
  void *value;

  while (c_avl_pick (tree, (void *) &name, (void *) &value) == 0)
  {
    cj_key_t *key = (cj_key_t *)value;

    if (CJ_IS_KEY(key))
      cj_key_free (key);
    else
      cj_tree_free ((c_avl_tree_t *)value);

    sfree (name);
  }

  c_avl_destroy (tree);
} /* }}} void cj_tree_free */

static void cj_free (void *arg) /* {{{ */
{
  cj_t *db;

  DEBUG ("curl_json plugin: cj_free (arg = %p);", arg);

  db = (cj_t *) arg;

  if (db == NULL)
    return;

  if (db->curl != NULL)
    curl_easy_cleanup (db->curl);
  db->curl = NULL;

  if (db->tree != NULL)
    cj_tree_free (db->tree);
  db->tree = NULL;

  sfree (db->instance);
  sfree (db->host);

  sfree (db->url);
  sfree (db->user);
  sfree (db->pass);
  sfree (db->credentials);
  sfree (db->cacert);

  sfree (db);
} /* }}} void cj_free */

/* Configuration handling functions {{{ */

static int cj_config_add_string (const char *name, char **dest, /* {{{ */
                                      oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("curl_json plugin: `%s' needs exactly one string argument.", name);
    return (-1);
  }

  sfree (*dest);
  *dest = strdup (ci->values[0].value.string);
  if (*dest == NULL)
    return (-1);

  return (0);
} /* }}} int cj_config_add_string */

static int cj_config_set_boolean (const char *name, int *dest, /* {{{ */
                                       oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    WARNING ("curl_json plugin: `%s' needs exactly one boolean argument.", name);
    return (-1);
  }

  *dest = ci->values[0].value.boolean ? 1 : 0;

  return (0);
} /* }}} int cj_config_set_boolean */

static c_avl_tree_t *cj_avl_create(void)
{
  return c_avl_create ((int (*) (const void *, const void *)) strcmp);
}

static int cj_config_add_key (cj_t *db, /* {{{ */
                                   oconfig_item_t *ci)
{
  cj_key_t *key;
  int status;
  int i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("curl_json plugin: The `Key' block "
             "needs exactly one string argument.");
    return (-1);
  }

  key = (cj_key_t *) malloc (sizeof (*key));
  if (key == NULL)
  {
    ERROR ("curl_json plugin: malloc failed.");
    return (-1);
  }
  memset (key, 0, sizeof (*key));
  key->magic = CJ_KEY_MAGIC;

  if (strcasecmp ("Key", ci->key) == 0)
  {
    status = cj_config_add_string ("Key", &key->path, ci);
    if (status != 0)
    {
      sfree (key);
      return (status);
    }
  }
  else
  {
    ERROR ("curl_json plugin: cj_config: "
           "Invalid key: %s", ci->key);
    return (-1);
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Type", child->key) == 0)
      status = cj_config_add_string ("Type", &key->type, child);
    else if (strcasecmp ("Instance", child->key) == 0)
      status = cj_config_add_string ("Instance", &key->instance, child);
    else
    {
      WARNING ("curl_json plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  while (status == 0)
  {
    if (key->type == NULL)
    {
      WARNING ("curl_json plugin: `Type' missing in `Key' block.");
      status = -1;
    }

    break;
  } /* while (status == 0) */

  /* store path in a tree that will match the json map structure, example:
   * "httpd/requests/count",
   * "httpd/requests/current" ->
   * { "httpd": { "requests": { "count": $key, "current": $key } } }
   */
  if (status == 0)
  {
    char *ptr;
    char *name;
    char ent[PATH_MAX];
    c_avl_tree_t *tree;

    if (db->tree == NULL)
      db->tree = cj_avl_create();

    tree = db->tree;
    name = key->path;
    ptr = key->path;
    if (*ptr == '/')
      ++ptr;

    name = ptr;
    while (*ptr)
    {
      if (*ptr == '/')
      {
        c_avl_tree_t *value;
        int len;

        len = ptr-name;
        if (len == 0)
          break;
        sstrncpy (ent, name, len+1);

        if (c_avl_get (tree, ent, (void *) &value) != 0)
        {
          value = cj_avl_create ();
          c_avl_insert (tree, strdup (ent), value);
        }

        tree = value;
        name = ptr+1;
      }
      ++ptr;
    }
    if (*name)
      c_avl_insert (tree, strdup(name), key);
    else
    {
      ERROR ("curl_json plugin: invalid key: %s", key->path);
      status = -1;
    }
  }

  return (status);
} /* }}} int cj_config_add_key */

static int cj_init_curl (cj_t *db) /* {{{ */
{
  db->curl = curl_easy_init ();
  if (db->curl == NULL)
  {
    ERROR ("curl_json plugin: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt (db->curl, CURLOPT_WRITEFUNCTION, cj_curl_callback);
  curl_easy_setopt (db->curl, CURLOPT_WRITEDATA, db);
  curl_easy_setopt (db->curl, CURLOPT_USERAGENT,
                    PACKAGE_NAME"/"PACKAGE_VERSION);
  curl_easy_setopt (db->curl, CURLOPT_ERRORBUFFER, db->curl_errbuf);
  curl_easy_setopt (db->curl, CURLOPT_URL, db->url);

  if (db->user != NULL)
  {
    size_t credentials_size;

    credentials_size = strlen (db->user) + 2;
    if (db->pass != NULL)
      credentials_size += strlen (db->pass);

    db->credentials = (char *) malloc (credentials_size);
    if (db->credentials == NULL)
    {
      ERROR ("curl_json plugin: malloc failed.");
      return (-1);
    }

    ssnprintf (db->credentials, credentials_size, "%s:%s",
               db->user, (db->pass == NULL) ? "" : db->pass);
    curl_easy_setopt (db->curl, CURLOPT_USERPWD, db->credentials);
  }

  curl_easy_setopt (db->curl, CURLOPT_SSL_VERIFYPEER, db->verify_peer);
  curl_easy_setopt (db->curl, CURLOPT_SSL_VERIFYHOST,
                    db->verify_host ? 2 : 0);
  if (db->cacert != NULL)
    curl_easy_setopt (db->curl, CURLOPT_CAINFO, db->cacert);

  return (0);
} /* }}} int cj_init_curl */

static int cj_config_add_url (oconfig_item_t *ci) /* {{{ */
{
  cj_t *db;
  int status = 0;
  int i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("curl_json plugin: The `URL' block "
             "needs exactly one string argument.");
    return (-1);
  }

  db = (cj_t *) malloc (sizeof (*db));
  if (db == NULL)
  {
    ERROR ("curl_json plugin: malloc failed.");
    return (-1);
  }
  memset (db, 0, sizeof (*db));

  if (strcasecmp ("URL", ci->key) == 0)
  {
    status = cj_config_add_string ("URL", &db->url, ci);
    if (status != 0)
    {
      sfree (db);
      return (status);
    }
  }
  else
  {
    ERROR ("curl_json plugin: cj_config: "
           "Invalid key: %s", ci->key);
    return (-1);
  }

  /* Fill the `cj_t' structure.. */
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Instance", child->key) == 0)
      status = cj_config_add_string ("Instance", &db->instance, child);
    else if (strcasecmp ("Host", child->key) == 0)
      status = cj_config_add_string ("Host", &db->host, child);
    else if (strcasecmp ("User", child->key) == 0)
      status = cj_config_add_string ("User", &db->user, child);
    else if (strcasecmp ("Password", child->key) == 0)
      status = cj_config_add_string ("Password", &db->pass, child);
    else if (strcasecmp ("VerifyPeer", child->key) == 0)
      status = cj_config_set_boolean ("VerifyPeer", &db->verify_peer, child);
    else if (strcasecmp ("VerifyHost", child->key) == 0)
      status = cj_config_set_boolean ("VerifyHost", &db->verify_host, child);
    else if (strcasecmp ("CACert", child->key) == 0)
      status = cj_config_add_string ("CACert", &db->cacert, child);
    else if (strcasecmp ("Key", child->key) == 0)
      status = cj_config_add_key (db, child);
    else
    {
      WARNING ("curl_json plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status == 0)
  {
    if (db->tree == NULL)
    {
      WARNING ("curl_json plugin: No (valid) `Key' block "
               "within `URL' block `%s'.", db->url);
      status = -1;
    }
    if (status == 0)
      status = cj_init_curl (db);
  }

  /* If all went well, register this database for reading */
  if (status == 0)
  {
    user_data_t ud;
    char cb_name[DATA_MAX_NAME_LEN];

    if (db->instance == NULL)
      db->instance = strdup("default");

    DEBUG ("curl_json plugin: Registering new read callback: %s",
           db->instance);

    memset (&ud, 0, sizeof (ud));
    ud.data = (void *) db;
    ud.free_func = cj_free;

    ssnprintf (cb_name, sizeof (cb_name), "curl_json-%s-%s",
               db->instance, db->url);

    plugin_register_complex_read (/* group = */ NULL, cb_name, cj_read,
                                  /* interval = */ NULL, &ud);
  }
  else
  {
    cj_free (db);
    return (-1);
  }

  return (0);
}
 /* }}} int cj_config_add_database */

static int cj_config (oconfig_item_t *ci) /* {{{ */
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

    if (strcasecmp ("URL", child->key) == 0)
    {
      status = cj_config_add_url (child);
      if (status == 0)
        success++;
      else
        errors++;
    }
    else
    {
      WARNING ("curl_json plugin: Option `%s' not allowed here.", child->key);
      errors++;
    }
  }

  if ((success == 0) && (errors > 0))
  {
    ERROR ("curl_json plugin: All statements failed.");
    return (-1);
  }

  return (0);
} /* }}} int cj_config */

/* }}} End of configuration handling functions */

static void cj_submit (cj_t *db, cj_key_t *key, value_t *value) /* {{{ */
{
  value_list_t vl = VALUE_LIST_INIT;
  char *host;

  vl.values     = value;
  vl.values_len = 1;

  if ((db->host == NULL)
      || (strcmp ("", db->host) == 0)
      || (strcmp (CJ_DEFAULT_HOST, db->host) == 0))
    host = hostname_g;
  else
    host = db->host;

  if (key->instance == NULL)
    ssnprintf (vl.type_instance, sizeof (vl.type_instance), "%s-%s",
               db->state[db->depth-1].name, db->state[db->depth].name);
  else
    sstrncpy (vl.type_instance, key->instance, sizeof (vl.type_instance));

  sstrncpy (vl.host, host, sizeof (vl.host));
  sstrncpy (vl.plugin, "curl_json", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, db->instance, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, key->type, sizeof (vl.type));

  plugin_dispatch_values (&vl);
} /* }}} int cj_submit */

static int cj_curl_perform (cj_t *db, CURL *curl) /* {{{ */
{
  int status;
  long rc;
  char *url;
  yajl_handle yprev = db->yajl;

  db->yajl = yajl_alloc (&ycallbacks, NULL, NULL, (void *)db);
  if (db->yajl == NULL)
  {
    ERROR ("curl_json plugin: yajl_alloc failed.");
    return (-1);
  }

  status = curl_easy_perform (curl);

  yajl_free (db->yajl);
  db->yajl = yprev;

  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rc);

  /* The response code is zero if a non-HTTP transport was used. */
  if ((rc != 0) && (rc != 200))
  {
    ERROR ("curl_json plugin: curl_easy_perform failed with response code %ld (%s)",
           rc, url);
    return (-1);
  }

  if (status != 0)
  {
    ERROR ("curl_json plugin: curl_easy_perform failed with status %i: %s (%s)",
           status, db->curl_errbuf, url);
    return (-1);
  }

  return (0);
} /* }}} int cj_curl_perform */

static int cj_read (user_data_t *ud) /* {{{ */
{
  cj_t *db;

  if ((ud == NULL) || (ud->data == NULL))
  {
    ERROR ("curl_json plugin: cj_read: Invalid user data.");
    return (-1);
  }

  db = (cj_t *) ud->data;

  db->depth = 0;
  memset (&db->state, 0, sizeof(db->state));
  db->state[db->depth].tree = db->tree;
  db->key = NULL;

  return cj_curl_perform (db, db->curl);
} /* }}} int cj_read */

void module_register (void)
{
  plugin_register_complex_config ("curl_json", cj_config);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
