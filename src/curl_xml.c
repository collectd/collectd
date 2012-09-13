/**
 * collectd - src/curl_xml.c
 * Copyright (C) 2009,2010       Amit Gupta
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
 *   Amit Gupta <amit.gupta221 at gmail.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_llist.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>

#include <curl/curl.h>

#define CX_DEFAULT_HOST "localhost"

/*
 * Private data structures
 */
struct cx_values_s /* {{{ */
{
  char path[DATA_MAX_NAME_LEN];
  size_t path_len;
};
typedef struct cx_values_s cx_values_t;
/* }}} */

struct cx_xpath_s /* {{{ */
{
  char *path;
  char *type;
  cx_values_t *values;
  int values_len;
  char *instance_prefix;
  char *instance;
  int is_table;
  unsigned long magic;
};
typedef struct cx_xpath_s cx_xpath_t;
/* }}} */

struct cx_s /* {{{ */
{
  char *instance;
  char *host;

  char *url;
  char *user;
  char *pass;
  char *credentials;
  _Bool verify_peer;
  _Bool verify_host;
  char *cacert;

  CURL *curl;
  char curl_errbuf[CURL_ERROR_SIZE];
  char *buffer;
  size_t buffer_size;
  size_t buffer_fill;

  llist_t *list; /* list of xpath blocks */
};
typedef struct cx_s cx_t; /* }}} */

/*
 * Private functions
 */
static size_t cx_curl_callback (void *buf, /* {{{ */
    size_t size, size_t nmemb, void *user_data)
{
  size_t len = size * nmemb;
  cx_t *db;

  db = user_data;
  if (db == NULL)
  {
    ERROR ("curl_xml plugin: cx_curl_callback: "
           "user_data pointer is NULL.");
    return (0);
  }

   if (len <= 0)
    return (len);

  if ((db->buffer_fill + len) >= db->buffer_size)
  {
    char *temp;

    temp = (char *) realloc (db->buffer,
                    db->buffer_fill + len + 1);
    if (temp == NULL)
    {
      ERROR ("curl_xml plugin: realloc failed.");
      return (0);
    }
    db->buffer = temp;
    db->buffer_size = db->buffer_fill + len + 1;
  }

  memcpy (db->buffer + db->buffer_fill, (char *) buf, len);
  db->buffer_fill += len;
  db->buffer[db->buffer_fill] = 0;

  return (len);
} /* }}} size_t cx_curl_callback */

static void cx_xpath_free (cx_xpath_t *xpath) /* {{{ */
{
  if (xpath == NULL)
    return;

  sfree (xpath->path);
  sfree (xpath->type);
  sfree (xpath->instance_prefix);
  sfree (xpath->instance);
  sfree (xpath->values);
  sfree (xpath);
} /* }}} void cx_xpath_free */

static void cx_list_free (llist_t *list) /* {{{ */
{
  llentry_t *le;

  le = llist_head (list);
  while (le != NULL)
  {
    llentry_t *le_next;

    le_next = le->next;

    sfree (le->key);
    cx_xpath_free (le->value);

    le = le_next;
  }

  llist_destroy (list);
  list = NULL;
} /* }}} void cx_list_free */

static void cx_free (void *arg) /* {{{ */
{
  cx_t *db;

  DEBUG ("curl_xml plugin: cx_free (arg = %p);", arg);

  db = (cx_t *) arg;

  if (db == NULL)
    return;

  if (db->curl != NULL)
    curl_easy_cleanup (db->curl);
  db->curl = NULL;

  if (db->list != NULL)
    cx_list_free (db->list);

  sfree (db->buffer);
  sfree (db->instance);
  sfree (db->host);

  sfree (db->url);
  sfree (db->user);
  sfree (db->pass);
  sfree (db->credentials);
  sfree (db->cacert);

  sfree (db);
} /* }}} void cx_free */

static int cx_check_type (const data_set_t *ds, cx_xpath_t *xpath) /* {{{ */
{
  if (!ds)
  {
    WARNING ("curl_xml plugin: DataSet `%s' not defined.", xpath->type);
    return (-1);
  }

  if (ds->ds_num != xpath->values_len)
  {
    WARNING ("curl_xml plugin: DataSet `%s' requires %i values, but config talks about %i",
        xpath->type, ds->ds_num, xpath->values_len);
    return (-1);
  }

  return (0);
} /* }}} cx_check_type */

static xmlXPathObjectPtr cx_evaluate_xpath (xmlXPathContextPtr xpath_ctx, /* {{{ */ 
           xmlChar *expr)
{
  xmlXPathObjectPtr xpath_obj;

  /* XXX: When to free this? */
  xpath_obj = xmlXPathEvalExpression(BAD_CAST expr, xpath_ctx);
  if (xpath_obj == NULL)
  {
     WARNING ("curl_xml plugin: "
               "Error unable to evaluate xpath expression \"%s\". Skipping...", expr);
     return NULL;
  }

  return xpath_obj;
} /* }}} cx_evaluate_xpath */

static int cx_if_not_text_node (xmlNodePtr node) /* {{{ */
{
  if (node->type == XML_TEXT_NODE || node->type == XML_ATTRIBUTE_NODE)
    return (0);

  WARNING ("curl_xml plugin: "
           "Node \"%s\" doesn't seem to be a text node. Skipping...", node->name);
  return -1;
} /* }}} cx_if_not_text_node */

static int cx_handle_single_value_xpath (xmlXPathContextPtr xpath_ctx, /* {{{ */
    cx_xpath_t *xpath,
    const data_set_t *ds, value_list_t *vl, int index)
{
  xmlXPathObjectPtr values_node_obj;
  xmlNodeSetPtr values_node;
  int tmp_size;
  char *node_value;

  values_node_obj = cx_evaluate_xpath (xpath_ctx, BAD_CAST xpath->values[index].path);
  if (values_node_obj == NULL)
    return (-1); /* Error already logged. */

  values_node = values_node_obj->nodesetval;
  tmp_size = (values_node) ? values_node->nodeNr : 0;

  if (tmp_size == 0)
  {
    WARNING ("curl_xml plugin: "
        "relative xpath expression \"%s\" doesn't match any of the nodes. "
        "Skipping...", xpath->values[index].path);
    xmlXPathFreeObject (values_node_obj);
    return (-1);
  }

  if (tmp_size > 1)
  {
    WARNING ("curl_xml plugin: "
        "relative xpath expression \"%s\" is expected to return "
        "only one node. Skipping...", xpath->values[index].path);
    xmlXPathFreeObject (values_node_obj);
    return (-1);
  }

  /* ignoring the element if other than textnode/attribute*/
  if (cx_if_not_text_node(values_node->nodeTab[0]))
  {
    WARNING ("curl_xml plugin: "
        "relative xpath expression \"%s\" is expected to return "
        "only text/attribute node which is not the case. Skipping...", 
        xpath->values[index].path);
    xmlXPathFreeObject (values_node_obj);
    return (-1);
  }

  node_value = (char *) xmlNodeGetContent(values_node->nodeTab[0]);
  switch (ds->ds[index].type)
  {
    case DS_TYPE_COUNTER:
      vl->values[index].counter = (counter_t) strtoull (node_value,
          /* endptr = */ NULL, /* base = */ 0);
      break;
    case DS_TYPE_DERIVE:
      vl->values[index].derive = (derive_t) strtoll (node_value,
          /* endptr = */ NULL, /* base = */ 0);
      break;
    case DS_TYPE_ABSOLUTE:
      vl->values[index].absolute = (absolute_t) strtoull (node_value,
          /* endptr = */ NULL, /* base = */ 0);
      break;
    case DS_TYPE_GAUGE: 
      vl->values[index].gauge = (gauge_t) strtod (node_value,
          /* endptr = */ NULL);
  }

  /* free up object */
  xmlXPathFreeObject (values_node_obj);

  /* We have reached here which means that
   * we have got something to work */
  return (0);
} /* }}} int cx_handle_single_value_xpath */

static int cx_handle_all_value_xpaths (xmlXPathContextPtr xpath_ctx, /* {{{ */
    cx_xpath_t *xpath,
    const data_set_t *ds, value_list_t *vl)
{
  value_t values[xpath->values_len];
  int status;
  int i;

  assert (xpath->values_len > 0);
  assert (xpath->values_len == vl->values_len);
  assert (xpath->values_len == ds->ds_num);
  vl->values = values;

  for (i = 0; i < xpath->values_len; i++)
  {
    status = cx_handle_single_value_xpath (xpath_ctx, xpath, ds, vl, i);
    if (status != 0)
      return (-1); /* An error has been printed. */
  } /* for (i = 0; i < xpath->values_len; i++) */

  plugin_dispatch_values (vl);
  vl->values = NULL;

  return (0);
} /* }}} int cx_handle_all_value_xpaths */

static int cx_handle_instance_xpath (xmlXPathContextPtr xpath_ctx, /* {{{ */
    cx_xpath_t *xpath, value_list_t *vl,
    _Bool is_table)
{
  xmlXPathObjectPtr instance_node_obj = NULL;
  xmlNodeSetPtr instance_node = NULL;

  memset (vl->type_instance, 0, sizeof (vl->type_instance));

  /* If the base xpath returns more than one block, the result is assumed to be
   * a table. The `Instnce' option is not optional in this case. Check for the
   * condition and inform the user. */
  if (is_table && (vl->type_instance == NULL))
  {
    WARNING ("curl_xml plugin: "
        "Base-XPath %s is a table (more than one result was returned), "
        "but no instance-XPath has been defined.",
        xpath->path);
    return (-1);
  }

  /* instance has to be an xpath expression */
  if (xpath->instance != NULL)
  {
    int tmp_size;

    instance_node_obj = cx_evaluate_xpath (xpath_ctx, BAD_CAST xpath->instance);
    if (instance_node_obj == NULL)
      return (-1); /* error is logged already */

    instance_node = instance_node_obj->nodesetval;
    tmp_size = (instance_node) ? instance_node->nodeNr : 0;

    if ( (tmp_size == 0) && (is_table) )
    {
      WARNING ("curl_xml plugin: "
          "relative xpath expression for 'InstanceFrom' \"%s\" doesn't match "
          "any of the nodes. Skipping the node.", xpath->instance);
      xmlXPathFreeObject (instance_node_obj);
      return (-1);
    }

    if (tmp_size > 1)
    {
      WARNING ("curl_xml plugin: "
          "relative xpath expression for 'InstanceFrom' \"%s\" is expected "
          "to return only one text node. Skipping the node.", xpath->instance);
      xmlXPathFreeObject (instance_node_obj);
      return (-1);
    }

    /* ignoring the element if other than textnode/attribute */
    if (cx_if_not_text_node(instance_node->nodeTab[0]))
    {
      WARNING ("curl_xml plugin: "
          "relative xpath expression \"%s\" is expected to return only text node "
          "which is not the case. Skipping the node.", xpath->instance);
      xmlXPathFreeObject (instance_node_obj);
      return (-1);
    }
  } /* if (xpath->instance != NULL) */

  if (xpath->instance_prefix != NULL)
  {
    if (instance_node != NULL)
      ssnprintf (vl->type_instance, sizeof (vl->type_instance),"%s%s",
          xpath->instance_prefix, (char *) xmlNodeGetContent(instance_node->nodeTab[0]));
    else
      sstrncpy (vl->type_instance, xpath->instance_prefix,
          sizeof (vl->type_instance));
  }
  else
  {
    /* If instance_prefix and instance_node are NULL, then
     * don't set the type_instance */
    if (instance_node != NULL)
      sstrncpy (vl->type_instance, (char *) xmlNodeGetContent(instance_node->nodeTab[0]),
          sizeof (vl->type_instance));
  }

  /* Free `instance_node_obj' this late, because `instance_node' points to
   * somewhere inside this structure. */
  xmlXPathFreeObject (instance_node_obj);

  return (0);
} /* }}} int cx_handle_instance_xpath */

static int  cx_handle_base_xpath (char *plugin_instance, /* {{{ */
    xmlXPathContextPtr xpath_ctx, const data_set_t *ds, 
    char *base_xpath, cx_xpath_t *xpath)
{
  int total_nodes;
  int i;

  xmlXPathObjectPtr base_node_obj = NULL;
  xmlNodeSetPtr base_nodes = NULL;

  value_list_t vl = VALUE_LIST_INIT;

  base_node_obj = cx_evaluate_xpath (xpath_ctx, BAD_CAST base_xpath); 
  if (base_node_obj == NULL)
    return -1; /* error is logged already */

  base_nodes = base_node_obj->nodesetval;
  total_nodes = (base_nodes) ? base_nodes->nodeNr : 0;

  if (total_nodes == 0)
  {
     ERROR ("curl_xml plugin: "
              "xpath expression \"%s\" doesn't match any of the nodes. "
              "Skipping the xpath block...", base_xpath);
     xmlXPathFreeObject (base_node_obj);
     return -1;
  }

  /* If base_xpath returned multiple results, then */
  /* Instance in the xpath block is required */ 
  if (total_nodes > 1 && xpath->instance == NULL)
  {
    ERROR ("curl_xml plugin: "
             "InstanceFrom is must in xpath block since the base xpath expression \"%s\" "
             "returned multiple results. Skipping the xpath block...", base_xpath);
    return -1;
  }

  /* set the values for the value_list */
  vl.values_len = ds->ds_num;
  sstrncpy (vl.type, xpath->type, sizeof (vl.type));
  sstrncpy (vl.plugin, "curl_xml", sizeof (vl.plugin));
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  if (plugin_instance != NULL)
    sstrncpy (vl.plugin_instance, plugin_instance, sizeof (vl.plugin_instance)); 

  for (i = 0; i < total_nodes; i++)
  {
    int status;

    xpath_ctx->node = base_nodes->nodeTab[i];

    status = cx_handle_instance_xpath (xpath_ctx, xpath, &vl,
        /* is_table = */ (total_nodes > 1));
    if (status != 0)
      continue; /* An error has already been reported. */

    status = cx_handle_all_value_xpaths (xpath_ctx, xpath, ds, &vl);
    if (status != 0)
      continue; /* An error has been logged. */
  } /* for (i = 0; i < total_nodes; i++) */

  /* free up the allocated memory */
  xmlXPathFreeObject (base_node_obj); 

  return (0); 
} /* }}} cx_handle_base_xpath */

static int cx_handle_parsed_xml(xmlDocPtr doc, /* {{{ */ 
                       xmlXPathContextPtr xpath_ctx, cx_t *db)
{
  llentry_t *le;
  const data_set_t *ds;
  cx_xpath_t *xpath;
  int status=-1;
  

  le = llist_head (db->list);
  while (le != NULL)
  {
    /* get the ds */
    xpath = (cx_xpath_t *) le->value;
    ds = plugin_get_ds (xpath->type);

    if ( (cx_check_type(ds, xpath) == 0) &&
         (cx_handle_base_xpath(db->instance, xpath_ctx, ds, le->key, xpath) == 0) )
      status = 0; /* we got atleast one success */

    le = le->next;
  } /* while (le != NULL) */

  return status;
} /* }}} cx_handle_parsed_xml */

static int cx_parse_stats_xml(xmlChar* xml, cx_t *db) /* {{{ */
{
  int status;
  xmlDocPtr doc;
  xmlXPathContextPtr xpath_ctx;

  /* Load the XML */
  doc = xmlParseDoc(xml);
  if (doc == NULL)
  {
    ERROR ("curl_xml plugin: Failed to parse the xml document  - %s", xml);
    return (-1);
  }

  xpath_ctx = xmlXPathNewContext(doc);
  if(xpath_ctx == NULL)
  {
    ERROR ("curl_xml plugin: Failed to create the xml context");
    xmlFreeDoc(doc);
    return (-1);
  }

  status = cx_handle_parsed_xml (doc, xpath_ctx, db);
  /* Cleanup */
  xmlXPathFreeContext(xpath_ctx);
  xmlFreeDoc(doc);
  return status;
} /* }}} cx_parse_stats_xml */

static int cx_curl_perform (cx_t *db, CURL *curl) /* {{{ */
{
  int status;
  long rc;
  char *ptr;
  char *url;

  db->buffer_fill = 0; 
  status = curl_easy_perform (curl);

  curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &url);
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &rc);

  /* The response code is zero if a non-HTTP transport was used. */
  if ((rc != 0) && (rc != 200))
  {
    ERROR ("curl_xml plugin: curl_easy_perform failed with response code %ld (%s)",
           rc, url);
    return (-1);
  }

  if (status != 0)
  {
    ERROR ("curl_xml plugin: curl_easy_perform failed with status %i: %s (%s)",
           status, db->curl_errbuf, url);
    return (-1);
  }

  ptr = db->buffer;

  status = cx_parse_stats_xml(BAD_CAST ptr, db);
  db->buffer_fill = 0;

  return status;
} /* }}} int cx_curl_perform */

static int cx_read (user_data_t *ud) /* {{{ */
{
  cx_t *db;

  if ((ud == NULL) || (ud->data == NULL))
  {
    ERROR ("curl_xml plugin: cx_read: Invalid user data.");
    return (-1);
  }

  db = (cx_t *) ud->data;

  return cx_curl_perform (db, db->curl);
} /* }}} int cx_read */

/* Configuration handling functions {{{ */

static int cx_config_add_values (const char *name, cx_xpath_t *xpath, /* {{{ */
                                      oconfig_item_t *ci)
{
  int i;

  if (ci->values_num < 1)
  {
    WARNING ("curl_xml plugin: `ValuesFrom' needs at least one argument.");
    return (-1);
  }

  for (i = 0; i < ci->values_num; i++)
    if (ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("curl_xml plugin: `ValuesFrom' needs only string argument.");
      return (-1);
    }

  sfree (xpath->values);

  xpath->values_len = 0;
  xpath->values = (cx_values_t *) malloc (sizeof (cx_values_t) * ci->values_num);
  if (xpath->values == NULL)
    return (-1);
  xpath->values_len = ci->values_num;

  /* populate cx_values_t structure */
  for (i = 0; i < ci->values_num; i++)
  {
    xpath->values[i].path_len = sizeof (ci->values[i].value.string);
    sstrncpy (xpath->values[i].path, ci->values[i].value.string, sizeof (xpath->values[i].path));
  }

  return (0); 
} /* }}} cx_config_add_values */

static int cx_config_add_xpath (cx_t *db, /* {{{ */
                                   oconfig_item_t *ci)
{
  cx_xpath_t *xpath;
  int status;
  int i;

  xpath = (cx_xpath_t *) malloc (sizeof (*xpath));
  if (xpath == NULL)
  {
    ERROR ("curl_xml plugin: malloc failed.");
    return (-1);
  }
  memset (xpath, 0, sizeof (*xpath));

  status = cf_util_get_string (ci, &xpath->path);
  if (status != 0)
  {
    sfree (xpath);
    return (status);
  }

  /* error out if xpath->path is an empty string */
  if (*xpath->path == 0)
  {
    ERROR ("curl_xml plugin: invalid xpath. "
           "xpath value can't be an empty string");
    return (-1);
  }

  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Type", child->key) == 0)
      status = cf_util_get_string (child, &xpath->type);
    else if (strcasecmp ("InstancePrefix", child->key) == 0)
      status = cf_util_get_string (child, &xpath->instance_prefix);
    else if (strcasecmp ("InstanceFrom", child->key) == 0)
      status = cf_util_get_string (child, &xpath->instance);
    else if (strcasecmp ("ValuesFrom", child->key) == 0)
      status = cx_config_add_values ("ValuesFrom", xpath, child);
    else
    {
      WARNING ("curl_xml plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status == 0 && xpath->type == NULL)
  {
    WARNING ("curl_xml plugin: `Type' missing in `xpath' block.");
    status = -1;
  }

  if (status == 0)
  {
    char *name;
    llentry_t *le;

    if (db->list == NULL)
    {
      db->list = llist_create();
      if (db->list == NULL)
      {
        ERROR ("curl_xml plugin: list creation failed.");
        return (-1);
      }
    }

    name = strdup(xpath->path);
    if (name == NULL)
    {
        ERROR ("curl_xml plugin: strdup failed.");
        return (-1);
    }

    le = llentry_create (name, xpath);
    if (le == NULL)
    {
      ERROR ("curl_xml plugin: llentry_create failed.");
      return (-1);
    }

    llist_append (db->list, le);
  }

  return (status);
} /* }}} int cx_config_add_xpath */

/* Initialize db->curl */
static int cx_init_curl (cx_t *db) /* {{{ */
{
  db->curl = curl_easy_init ();
  if (db->curl == NULL)
  {
    ERROR ("curl_xml plugin: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt (db->curl, CURLOPT_NOSIGNAL, 1);
  curl_easy_setopt (db->curl, CURLOPT_WRITEFUNCTION, cx_curl_callback);
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
      ERROR ("curl_xml plugin: malloc failed.");
      return (-1);
    }

    ssnprintf (db->credentials, credentials_size, "%s:%s",
               db->user, (db->pass == NULL) ? "" : db->pass);
    curl_easy_setopt (db->curl, CURLOPT_USERPWD, db->credentials);
  }

  curl_easy_setopt (db->curl, CURLOPT_SSL_VERIFYPEER, db->verify_peer ? 1L : 0L);
  curl_easy_setopt (db->curl, CURLOPT_SSL_VERIFYHOST,
                    db->verify_host ? 2L : 0L);
  if (db->cacert != NULL)
    curl_easy_setopt (db->curl, CURLOPT_CAINFO, db->cacert);

  return (0);
} /* }}} int cx_init_curl */

static int cx_config_add_url (oconfig_item_t *ci) /* {{{ */
{
  cx_t *db;
  int status = 0;
  int i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("curl_xml plugin: The `URL' block "
             "needs exactly one string argument.");
    return (-1);
  }

  db = (cx_t *) malloc (sizeof (*db));
  if (db == NULL)
  {
    ERROR ("curl_xml plugin: malloc failed.");
    return (-1);
  }
  memset (db, 0, sizeof (*db));

  if (strcasecmp ("URL", ci->key) == 0)
  {
    status = cf_util_get_string (ci, &db->url);
    if (status != 0)
    {
      sfree (db);
      return (status);
    }
  }
  else
  {
    ERROR ("curl_xml plugin: cx_config: "
           "Invalid key: %s", ci->key);
    return (-1);
  }

  /* Fill the `cx_t' structure.. */
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Instance", child->key) == 0)
      status = cf_util_get_string (child, &db->instance);
    else if (strcasecmp ("Host", child->key) == 0)
      status = cf_util_get_string (child, &db->host);
    else if (strcasecmp ("User", child->key) == 0)
      status = cf_util_get_string (child, &db->user);
    else if (strcasecmp ("Password", child->key) == 0)
      status = cf_util_get_string (child, &db->pass);
    else if (strcasecmp ("VerifyPeer", child->key) == 0)
      status = cf_util_get_boolean (child, &db->verify_peer);
    else if (strcasecmp ("VerifyHost", child->key) == 0)
      status = cf_util_get_boolean (child, &db->verify_host);
    else if (strcasecmp ("CACert", child->key) == 0)
      status = cf_util_get_string (child, &db->cacert);
    else if (strcasecmp ("xpath", child->key) == 0)
      status = cx_config_add_xpath (db, child);
    else
    {
      WARNING ("curl_xml plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status == 0)
  {
    if (db->list == NULL)
    {
      WARNING ("curl_xml plugin: No (valid) `Key' block "
               "within `URL' block `%s'.", db->url);
      status = -1;
    }
    if (status == 0)
      status = cx_init_curl (db);
  }

  /* If all went well, register this database for reading */
  if (status == 0)
  {
    user_data_t ud;
    char cb_name[DATA_MAX_NAME_LEN];

    if (db->instance == NULL)
      db->instance = strdup("default");

    DEBUG ("curl_xml plugin: Registering new read callback: %s",
           db->instance);

    memset (&ud, 0, sizeof (ud));
    ud.data = (void *) db;
    ud.free_func = cx_free;

    ssnprintf (cb_name, sizeof (cb_name), "curl_xml-%s-%s",
               db->instance, db->url);

    plugin_register_complex_read (/* group = */ NULL, cb_name, cx_read,
                                  /* interval = */ NULL, &ud);
  }
  else
  {
    cx_free (db);
    return (-1);
  }

  return (0);
} /* }}} int cx_config_add_url */

/* }}} End of configuration handling functions */

static int cx_config (oconfig_item_t *ci) /* {{{ */
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
      status = cx_config_add_url (child);
      if (status == 0)
        success++;
      else
        errors++;
    }
    else
    {
      WARNING ("curl_xml plugin: Option `%s' not allowed here.", child->key);
      errors++;
    }
  }

  if ((success == 0) && (errors > 0))
  {
    ERROR ("curl_xml plugin: All statements failed.");
    return (-1);
  }

  return (0);
} /* }}} int cx_config */

void module_register (void)
{
  plugin_register_complex_config ("curl_xml", cx_config);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
