/**
 * collectd - src/curl_json.c
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2006-2013  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_avltree.h"
#include "utils_complain.h"

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>

#include <curl/curl.h>

#include <yajl/yajl_parse.h>
#if HAVE_YAJL_YAJL_VERSION_H
# include <yajl/yajl_version.h>
#endif

#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
# define HAVE_YAJL_V2 1
#endif

#define CJ_DEFAULT_HOST "localhost"
#define CJ_KEY_MAGIC 0x43484b59UL /* CHKY */
#define CJ_IS_KEY(key) ((key)->magic == CJ_KEY_MAGIC)
#define CJ_ANY "*"
#define COUCH_MIN(x,y) ((x) < (y) ? (x) : (y))

struct cj_key_s;
typedef struct cj_key_s cj_key_t;
struct cj_key_s /* {{{ */
{
  unsigned long magic;
  char *path;
  char *type;
  char *instance;
};
/* }}} */

struct cj_s /* {{{ */
{
  char *instance;
  char *host;

  char *sock;

  char *url;
  char *user;
  char *pass;
  char *credentials;
  _Bool digest;
  _Bool verify_peer;
  _Bool verify_host;
  char *cacert;
  struct curl_slist *headers;
  char *post_body;
  cdtime_t interval;
  int timeout;

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
    _Bool in_array;
    int index;
    char name[DATA_MAX_NAME_LEN];
  } state[YAJL_MAX_DEPTH];
};
typedef struct cj_s cj_t; /* }}} */

#if HAVE_YAJL_V2
typedef size_t yajl_len_t;
#else
typedef unsigned int yajl_len_t;
#endif

static int cj_read (user_data_t *ud);
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
    return (len);
#if !HAVE_YAJL_V2
  else if (status == yajl_status_insufficient_data)
    return (len);
#endif

  if (status != yajl_status_ok)
  {
    unsigned char *msg =
      yajl_get_error(db->yajl, /* verbose = */ 1,
          /* jsonText = */ (unsigned char *) buf, (unsigned int) len);
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
  {
    static char type[DATA_MAX_NAME_LEN] = "!!!invalid!!!";

    assert (key->type != NULL);
    if (strcmp (type, key->type) != 0)
    {
      ERROR ("curl_json plugin: Unable to look up DS type \"%s\".",
          key->type);
      sstrncpy (type, key->type, sizeof (type));
    }

    return -1;
  }
  else if (ds->ds_num > 1)
  {
    static c_complain_t complaint = C_COMPLAIN_INIT_STATIC;

    c_complain_once (LOG_WARNING, &complaint,
        "curl_json plugin: The type \"%s\" has more than one data source. "
        "This is currently not supported. I will return the type of the "
        "first data source, but this will likely lead to problems later on.",
        key->type);
  }

  return ds->ds[0].type;
}

static int cj_cb_map_key (void *ctx, const unsigned char *val,
    yajl_len_t len);

static void cj_cb_inc_array_index (void *ctx, _Bool update_key)
{
  cj_t *db = (cj_t *)ctx;

  if (!db->state[db->depth].in_array)
    return;

  db->state[db->depth].index++;

  if (update_key)
  {
    char name[DATA_MAX_NAME_LEN];

    ssnprintf (name, sizeof (name), "%d", db->state[db->depth].index - 1);

    cj_cb_map_key (ctx, (unsigned char *)name, (yajl_len_t) strlen (name));
  }
}

/* yajl callbacks */
#define CJ_CB_ABORT    0
#define CJ_CB_CONTINUE 1

static int cj_cb_boolean (void * ctx, int boolVal)
{
  cj_cb_inc_array_index (ctx, /* update_key = */ 0);
  return (CJ_CB_CONTINUE);
}

static int cj_cb_null (void * ctx)
{
  cj_cb_inc_array_index (ctx, /* update_key = */ 0);
  return (CJ_CB_CONTINUE);
}

static int cj_cb_number (void *ctx,
    const char *number, yajl_len_t number_len)
{
  char buffer[number_len + 1];

  cj_t *db = (cj_t *)ctx;
  cj_key_t *key = db->state[db->depth].key;
  value_t vt;
  int type;
  int status;

  /* Create a null-terminated version of the string. */
  memcpy (buffer, number, number_len);
  buffer[sizeof (buffer) - 1] = 0;

  if ((key == NULL) || !CJ_IS_KEY (key)) {
    if (key != NULL && !db->state[db->depth].in_array/*can be inhomogeneous*/)
      NOTICE ("curl_json plugin: Found \"%s\", but the configuration expects"
              " a map.", buffer);
    cj_cb_inc_array_index (ctx, /* update_key = */ 1);
    key = db->state[db->depth].key;
    if (key == NULL) {
      return (CJ_CB_CONTINUE);
    }
  }
  else
  {
    cj_cb_inc_array_index (ctx, /* update_key = */ 1);
  }

  type = cj_get_type (key);
  status = parse_value (buffer, &vt, type);
  if (status != 0)
  {
    NOTICE ("curl_json plugin: Unable to parse number: \"%s\"", buffer);
    return (CJ_CB_CONTINUE);
  }

  cj_submit (db, key, &vt);
  return (CJ_CB_CONTINUE);
} /* int cj_cb_number */

/* Queries the key-tree of the parent context for "in_name" and, if found,
 * updates the "key" field of the current context. Otherwise, "key" is set to
 * NULL. */
static int cj_cb_map_key (void *ctx,
    unsigned char const *in_name, yajl_len_t in_name_len)
{
  cj_t *db = (cj_t *)ctx;
  c_avl_tree_t *tree;

  tree = db->state[db->depth-1].tree;

  if (tree != NULL)
  {
    cj_key_t *value = NULL;
    char *name;
    size_t name_len;

    /* Create a null-terminated version of the name. */
    name = db->state[db->depth].name;
    name_len = COUCH_MIN ((size_t) in_name_len,
        sizeof (db->state[db->depth].name) - 1);
    memcpy (name, in_name, name_len);
    name[name_len] = 0;

    if (c_avl_get (tree, name, (void *) &value) == 0) {
      if (CJ_IS_KEY((cj_key_t*)value)) {
        db->state[db->depth].key = value;
      }
      else {
        db->state[db->depth].tree = (c_avl_tree_t*) value;
      }
    }
    else if (c_avl_get (tree, CJ_ANY, (void *) &value) == 0)
      if (CJ_IS_KEY((cj_key_t*)value)) {
        db->state[db->depth].key = value;
      }
      else {
        db->state[db->depth].tree = (c_avl_tree_t*) value;
      }
    else
      db->state[db->depth].key = NULL;
  }

  return (CJ_CB_CONTINUE);
}

static int cj_cb_string (void *ctx, const unsigned char *val,
    yajl_len_t len)
{
  /* Handle the string as if it was a number. */
  return (cj_cb_number (ctx, (const char *) val, len));
} /* int cj_cb_string */

static int cj_cb_start (void *ctx)
{
  cj_t *db = (cj_t *)ctx;
  if (++db->depth >= YAJL_MAX_DEPTH)
  {
    ERROR ("curl_json plugin: %s depth exceeds max, aborting.",
           db->url ? db->url : db->sock);
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
  cj_cb_inc_array_index (ctx, /* update_key = */ 1);
  return cj_cb_start (ctx);
}

static int cj_cb_end_map (void *ctx)
{
  return cj_cb_end (ctx);
}

static int cj_cb_start_array (void * ctx)
{
  cj_t *db = (cj_t *)ctx;
  cj_cb_inc_array_index (ctx, /* update_key = */ 1);
  if (db->depth+1 < YAJL_MAX_DEPTH) {
    db->state[db->depth+1].in_array = 1;
    db->state[db->depth+1].index = 0;
  }
  return cj_cb_start (ctx);
}

static int cj_cb_end_array (void * ctx)
{
  cj_t *db = (cj_t *)ctx;
  db->state[db->depth].in_array = 0;
  return cj_cb_end (ctx);
}

static yajl_callbacks ycallbacks = {
  cj_cb_null, /* null */
  cj_cb_boolean, /* boolean */
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

  sfree (db->sock);

  sfree (db->url);
  sfree (db->user);
  sfree (db->pass);
  sfree (db->credentials);
  sfree (db->cacert);
  sfree (db->post_body);
  curl_slist_free_all (db->headers);

  sfree (db);
} /* }}} void cj_free */

/* Configuration handling functions {{{ */

static c_avl_tree_t *cj_avl_create(void)
{
  return c_avl_create ((int (*) (const void *, const void *)) strcmp);
}

static int cj_config_append_string (const char *name, struct curl_slist **dest, /* {{{ */
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("curl_json plugin: `%s' needs exactly one string argument.", name);
    return (-1);
  }

  *dest = curl_slist_append(*dest, ci->values[0].value.string);
  if (*dest == NULL)
    return (-1);

  return (0);
} /* }}} int cj_config_append_string */

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
    status = cf_util_get_string (ci, &key->path);
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
      status = cf_util_get_string (child, &key->type);
    else if (strcasecmp ("Instance", child->key) == 0)
      status = cf_util_get_string (child, &key->instance);
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
        len = COUCH_MIN(len, sizeof (ent)-1);
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

  curl_easy_setopt (db->curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt (db->curl, CURLOPT_WRITEFUNCTION, cj_curl_callback);
  curl_easy_setopt (db->curl, CURLOPT_WRITEDATA, db);
  curl_easy_setopt (db->curl, CURLOPT_USERAGENT, COLLECTD_USERAGENT);
  curl_easy_setopt (db->curl, CURLOPT_ERRORBUFFER, db->curl_errbuf);
  curl_easy_setopt (db->curl, CURLOPT_URL, db->url);
  curl_easy_setopt (db->curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt (db->curl, CURLOPT_MAXREDIRS, 50L);

  if (db->user != NULL)
  {
#ifdef HAVE_CURLOPT_USERNAME
    curl_easy_setopt (db->curl, CURLOPT_USERNAME, db->user);
    curl_easy_setopt (db->curl, CURLOPT_PASSWORD,
        (db->pass == NULL) ? "" : db->pass);
#else
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
#endif

    if (db->digest)
      curl_easy_setopt (db->curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
  }

  curl_easy_setopt (db->curl, CURLOPT_SSL_VERIFYPEER, (long) db->verify_peer);
  curl_easy_setopt (db->curl, CURLOPT_SSL_VERIFYHOST,
                    db->verify_host ? 2L : 0L);
  if (db->cacert != NULL)
    curl_easy_setopt (db->curl, CURLOPT_CAINFO, db->cacert);
  if (db->headers != NULL)
    curl_easy_setopt (db->curl, CURLOPT_HTTPHEADER, db->headers);
  if (db->post_body != NULL)
    curl_easy_setopt (db->curl, CURLOPT_POSTFIELDS, db->post_body);

#ifdef HAVE_CURLOPT_TIMEOUT_MS
  if (db->timeout >= 0)
    curl_easy_setopt (db->curl, CURLOPT_TIMEOUT_MS, (long) db->timeout);
  else if (db->interval > 0)
    curl_easy_setopt (db->curl, CURLOPT_TIMEOUT_MS,
        CDTIME_T_TO_MS(db->timeout));
  else
    curl_easy_setopt (db->curl, CURLOPT_TIMEOUT_MS,
        CDTIME_T_TO_MS(plugin_get_interval()));
#endif

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

  db->timeout = -1;

  if (strcasecmp ("URL", ci->key) == 0)
    status = cf_util_get_string (ci, &db->url);
  else if (strcasecmp ("Sock", ci->key) == 0)
    status = cf_util_get_string (ci, &db->sock);
  else
  {
    ERROR ("curl_json plugin: cj_config: "
           "Invalid key: %s", ci->key);
    return (-1);
  }
  if (status != 0)
  {
    sfree (db);
    return (status);
  }

  /* Fill the `cj_t' structure.. */
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Instance", child->key) == 0)
      status = cf_util_get_string (child, &db->instance);
    else if (strcasecmp ("Host", child->key) == 0)
      status = cf_util_get_string (child, &db->host);
    else if (db->url && strcasecmp ("User", child->key) == 0)
      status = cf_util_get_string (child, &db->user);
    else if (db->url && strcasecmp ("Password", child->key) == 0)
      status = cf_util_get_string (child, &db->pass);
    else if (strcasecmp ("Digest", child->key) == 0)
      status = cf_util_get_boolean (child, &db->digest);
    else if (db->url && strcasecmp ("VerifyPeer", child->key) == 0)
      status = cf_util_get_boolean (child, &db->verify_peer);
    else if (db->url && strcasecmp ("VerifyHost", child->key) == 0)
      status = cf_util_get_boolean (child, &db->verify_host);
    else if (db->url && strcasecmp ("CACert", child->key) == 0)
      status = cf_util_get_string (child, &db->cacert);
    else if (db->url && strcasecmp ("Header", child->key) == 0)
      status = cj_config_append_string ("Header", &db->headers, child);
    else if (db->url && strcasecmp ("Post", child->key) == 0)
      status = cf_util_get_string (child, &db->post_body);
    else if (strcasecmp ("Key", child->key) == 0)
      status = cj_config_add_key (db, child);
    else if (strcasecmp ("Interval", child->key) == 0)
      status = cf_util_get_cdtime(child, &db->interval);
    else if (strcasecmp ("Timeout", child->key) == 0)
      status = cf_util_get_int (child, &db->timeout);
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
      WARNING ("curl_json plugin: No (valid) `Key' block within `%s' \"`%s'\".",
               db->url ? "URL" : "Sock", db->url ? db->url : db->sock);
      status = -1;
    }
    if (status == 0 && db->url)
      status = cj_init_curl (db);
  }

  /* If all went well, register this database for reading */
  if (status == 0)
  {
    user_data_t ud;
    char *cb_name;
    struct timespec interval = { 0, 0 };

    CDTIME_T_TO_TIMESPEC (db->interval, &interval);

    if (db->instance == NULL)
      db->instance = strdup("default");

    DEBUG ("curl_json plugin: Registering new read callback: %s",
           db->instance);

    memset (&ud, 0, sizeof (ud));
    ud.data = (void *) db;
    ud.free_func = cj_free;

    cb_name = ssnprintf_alloc ("curl_json-%s-%s",
               db->instance, db->url ? db->url : db->sock);

    plugin_register_complex_read (/* group = */ NULL, cb_name, cj_read,
                                  /* interval = */ (db->interval > 0) ? &interval : NULL,
                                  &ud);
    sfree (cb_name);
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

    if (strcasecmp ("Sock", child->key) == 0
        || strcasecmp ("URL", child->key) == 0)
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
  {
    int i, len = 0;
    for (i = 0; i < db->depth; i++)
      len += ssnprintf(vl.type_instance+len, sizeof(vl.type_instance)-len,
                       i ? "-%s" : "%s", db->state[i+1].name);
  }
  else
    sstrncpy (vl.type_instance, key->instance, sizeof (vl.type_instance));

  sstrncpy (vl.host, host, sizeof (vl.host));
  sstrncpy (vl.plugin, "curl_json", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, db->instance, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, key->type, sizeof (vl.type));

  if (db->interval > 0)
    vl.interval = db->interval;

  plugin_dispatch_values (&vl);
} /* }}} int cj_submit */

static int cj_sock_perform (cj_t *db) /* {{{ */
{
  char errbuf[1024];
  struct sockaddr_un sa_unix = {};
  sa_unix.sun_family = AF_UNIX;
  sstrncpy (sa_unix.sun_path, db->sock, sizeof (sa_unix.sun_path));

  int fd = socket (AF_UNIX, SOCK_STREAM, 0);
  if (fd < 0)
    return (-1);
  if (connect (fd, (struct sockaddr *)&sa_unix, sizeof(sa_unix)) < 0)
  {
    ERROR ("curl_json plugin: connect(%s) failed: %s",
           (db->sock != NULL) ? db->sock : "<null>",
           sstrerror(errno, errbuf, sizeof (errbuf)));
    close (fd);
    return (-1);
  }

  ssize_t red;
  do {
    unsigned char buffer[4096];
    red = read (fd, buffer, sizeof(buffer));
    if (red < 0) {
        ERROR ("curl_json plugin: read(%s) failed: %s",
               (db->sock != NULL) ? db->sock : "<null>",
               sstrerror(errno, errbuf, sizeof (errbuf)));
        close (fd);
        return (-1);
    }
    if (!cj_curl_callback (buffer, red, 1, db))
        break;
  } while (red > 0);
  close (fd);
  return (0);
} /* }}} int cj_sock_perform */


static int cj_curl_perform(cj_t *db) /* {{{ */
{
  int status;
  long rc;
  char *url;
  url = db->url;

  status = curl_easy_perform (db->curl);
  if (status != CURLE_OK)
  {
    ERROR ("curl_json plugin: curl_easy_perform failed with status %i: %s (%s)",
           status, db->curl_errbuf, url);
    return (-1);
  }

  curl_easy_getinfo(db->curl, CURLINFO_EFFECTIVE_URL, &url);
  curl_easy_getinfo(db->curl, CURLINFO_RESPONSE_CODE, &rc);

  /* The response code is zero if a non-HTTP transport was used. */
  if ((rc != 0) && (rc != 200))
  {
    ERROR ("curl_json plugin: curl_easy_perform failed with "
        "response code %ld (%s)", rc, url);
    return (-1);
  }
  return (0);
} /* }}} int cj_curl_perform */

static int cj_perform (cj_t *db) /* {{{ */
{
  int status;
  yajl_handle yprev = db->yajl;

  db->yajl = yajl_alloc (&ycallbacks,
#if HAVE_YAJL_V2
      /* alloc funcs = */ NULL,
#else
      /* alloc funcs = */ NULL, NULL,
#endif
      /* context = */ (void *)db);
  if (db->yajl == NULL)
  {
    ERROR ("curl_json plugin: yajl_alloc failed.");
    db->yajl = yprev;
    return (-1);
  }

  if (db->url)
    status = cj_curl_perform (db);
  else
    status = cj_sock_perform (db);
  if (status < 0)
  {
    yajl_free (db->yajl);
    db->yajl = yprev;
    return (-1);
  }

#if HAVE_YAJL_V2
    status = yajl_complete_parse(db->yajl);
#else
    status = yajl_parse_complete(db->yajl);
#endif
  if (status != yajl_status_ok)
  {
    unsigned char *errmsg;

    errmsg = yajl_get_error (db->yajl, /* verbose = */ 0,
        /* jsonText = */ NULL, /* jsonTextLen = */ 0);
    ERROR ("curl_json plugin: yajl_parse_complete failed: %s",
        (char *) errmsg);
    yajl_free_error (db->yajl, errmsg);
    yajl_free (db->yajl);
    db->yajl = yprev;
    return (-1);
  }

  yajl_free (db->yajl);
  db->yajl = yprev;
  return (0);
} /* }}} int cj_perform */

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

  return cj_perform (db);
} /* }}} int cj_read */

static int cj_init (void) /* {{{ */
{
  /* Call this while collectd is still single-threaded to avoid
   * initialization issues in libgcrypt. */
  curl_global_init (CURL_GLOBAL_SSL);
  return (0);
} /* }}} int cj_init */

void module_register (void)
{
  plugin_register_complex_config ("curl_json", cj_config);
  plugin_register_init ("curl_json", cj_init);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
