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
#include "utils_curl_stats.h"
#include "utils_llist.h"

#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

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
  size_t values_len;
  char *instance_prefix;
  char *instance;
  char *plugin_instance_from;
  int is_table;
  unsigned long magic;
};
typedef struct cx_xpath_s cx_xpath_t;
/* }}} */

struct cx_namespace_s /* {{{ */
{
  char *prefix;
  char *url;
};
typedef struct cx_namespace_s cx_namespace_t;
/* }}} */

struct cx_s /* {{{ */
{
  char *instance;
  char *plugin_name;
  char *host;

  char *url;
  char *user;
  char *pass;
  char *credentials;
  _Bool digest;
  _Bool verify_peer;
  _Bool verify_host;
  char *cacert;
  char *post_body;
  int timeout;
  struct curl_slist *headers;
  curl_stats_t *stats;

  cx_namespace_t *namespaces;
  size_t namespaces_num;

  CURL *curl;
  char curl_errbuf[CURL_ERROR_SIZE];
  char *buffer;
  size_t buffer_size;
  size_t buffer_fill;

  llist_t *xpath_list; /* list of xpath blocks */
};
typedef struct cx_s cx_t; /* }}} */

/*
 * Private functions
 */
static size_t cx_curl_callback(void *buf, /* {{{ */
                               size_t size, size_t nmemb, void *user_data) {
  size_t len = size * nmemb;

  cx_t *db = user_data;
  if (db == NULL) {
    ERROR("curl_xml plugin: cx_curl_callback: "
          "user_data pointer is NULL.");
    return 0;
  }

  if (len == 0)
    return len;

  if ((db->buffer_fill + len) >= db->buffer_size) {
    char *temp = realloc(db->buffer, db->buffer_fill + len + 1);
    if (temp == NULL) {
      ERROR("curl_xml plugin: realloc failed.");
      return 0;
    }
    db->buffer = temp;
    db->buffer_size = db->buffer_fill + len + 1;
  }

  memcpy(db->buffer + db->buffer_fill, (char *)buf, len);
  db->buffer_fill += len;
  db->buffer[db->buffer_fill] = 0;

  return len;
} /* }}} size_t cx_curl_callback */

static void cx_xpath_free(cx_xpath_t *xpath) /* {{{ */
{
  if (xpath == NULL)
    return;

  sfree(xpath->path);
  sfree(xpath->type);
  sfree(xpath->instance_prefix);
  sfree(xpath->plugin_instance_from);
  sfree(xpath->instance);
  sfree(xpath->values);
  sfree(xpath);
} /* }}} void cx_xpath_free */

static void cx_xpath_list_free(llist_t *list) /* {{{ */
{
  llentry_t *le;

  le = llist_head(list);
  while (le != NULL) {
    llentry_t *le_next = le->next;

    /* this also frees xpath->path used for le->key */
    cx_xpath_free(le->value);

    le = le_next;
  }

  llist_destroy(list);
} /* }}} void cx_xpath_list_free */

static void cx_free(void *arg) /* {{{ */
{
  cx_t *db;

  DEBUG("curl_xml plugin: cx_free (arg = %p);", arg);

  db = (cx_t *)arg;

  if (db == NULL)
    return;

  if (db->curl != NULL)
    curl_easy_cleanup(db->curl);
  db->curl = NULL;

  if (db->xpath_list != NULL)
    cx_xpath_list_free(db->xpath_list);

  sfree(db->buffer);
  sfree(db->instance);
  sfree(db->plugin_name);
  sfree(db->host);

  sfree(db->url);
  sfree(db->user);
  sfree(db->pass);
  sfree(db->credentials);
  sfree(db->cacert);
  sfree(db->post_body);
  curl_slist_free_all(db->headers);
  curl_stats_destroy(db->stats);

  for (size_t i = 0; i < db->namespaces_num; i++) {
    sfree(db->namespaces[i].prefix);
    sfree(db->namespaces[i].url);
  }
  sfree(db->namespaces);

  sfree(db);
} /* }}} void cx_free */

static const char *cx_host(const cx_t *db) /* {{{ */
{
  if (db->host == NULL)
    return hostname_g;
  return db->host;
} /* }}} cx_host */

static int cx_config_append_string(const char *name,
                                   struct curl_slist **dest, /* {{{ */
                                   oconfig_item_t *ci) {
  struct curl_slist *temp = NULL;
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("curl_xml plugin: `%s' needs exactly one string argument.", name);
    return -1;
  }

  temp = curl_slist_append(*dest, ci->values[0].value.string);
  if (temp == NULL)
    return -1;

  *dest = temp;

  return 0;
} /* }}} int cx_config_append_string */

static int cx_check_type(const data_set_t *ds, cx_xpath_t *xpath) /* {{{ */
{
  if (!ds) {
    WARNING("curl_xml plugin: DataSet `%s' not defined.", xpath->type);
    return -1;
  }

  if (ds->ds_num != xpath->values_len) {
    WARNING("curl_xml plugin: DataSet `%s' requires %" PRIsz
            " values, but config talks about %" PRIsz,
            xpath->type, ds->ds_num, xpath->values_len);
    return -1;
  }

  return 0;
} /* }}} cx_check_type */

static xmlXPathObjectPtr cx_evaluate_xpath(xmlXPathContextPtr xpath_ctx,
                                           char *expr) /* {{{ */
{
  xmlXPathObjectPtr xpath_obj =
      xmlXPathEvalExpression(BAD_CAST expr, xpath_ctx);
  if (xpath_obj == NULL) {
    WARNING("curl_xml plugin: "
            "Error unable to evaluate xpath expression \"%s\". Skipping...",
            expr);
    return NULL;
  }

  return xpath_obj;
} /* }}} cx_evaluate_xpath */

static int cx_if_not_text_node(xmlNodePtr node) /* {{{ */
{
  if (node->type == XML_TEXT_NODE || node->type == XML_ATTRIBUTE_NODE ||
      node->type == XML_ELEMENT_NODE)
    return 0;

  WARNING("curl_xml plugin: "
          "Node \"%s\" doesn't seem to be a text node. Skipping...",
          node->name);
  return -1;
} /* }}} cx_if_not_text_node */

/*
 * Returned value should be freed with xmlFree().
 */
static char *cx_get_text_node_value(xmlXPathContextPtr xpath_ctx, /* {{{ */
                                    char *expr, const char *from_option) {
  xmlXPathObjectPtr values_node_obj = cx_evaluate_xpath(xpath_ctx, expr);
  if (values_node_obj == NULL)
    return NULL; /* Error already logged. */

  xmlNodeSetPtr values_node = values_node_obj->nodesetval;
  size_t tmp_size = (values_node) ? values_node->nodeNr : 0;

  if (tmp_size == 0) {
    WARNING("curl_xml plugin: "
            "relative xpath expression \"%s\" from '%s' doesn't match "
            "any of the nodes.",
            expr, from_option);
    xmlXPathFreeObject(values_node_obj);
    return NULL;
  }

  if (tmp_size > 1) {
    WARNING("curl_xml plugin: "
            "relative xpath expression \"%s\" from '%s' is expected to return "
            "only one text node. Skipping the node.",
            expr, from_option);
    xmlXPathFreeObject(values_node_obj);
    return NULL;
  }

  /* ignoring the element if other than textnode/attribute*/
  if (cx_if_not_text_node(values_node->nodeTab[0])) {
    WARNING("curl_xml plugin: "
            "relative xpath expression \"%s\" from '%s' is expected to return "
            "only text/attribute node which is not the case. "
            "Skipping the node.",
            expr, from_option);
    xmlXPathFreeObject(values_node_obj);
    return NULL;
  }

  char *node_value = (char *)xmlNodeGetContent(values_node->nodeTab[0]);

  /* free up object */
  xmlXPathFreeObject(values_node_obj);

  return node_value;
} /* }}} char * cx_get_text_node_value */

static int cx_handle_single_value_xpath(xmlXPathContextPtr xpath_ctx, /* {{{ */
                                        cx_xpath_t *xpath, const data_set_t *ds,
                                        value_list_t *vl, int index) {

  char *node_value = cx_get_text_node_value(
      xpath_ctx, xpath->values[index].path, "ValuesFrom");

  if (node_value == NULL)
    return -1;

  switch (ds->ds[index].type) {
  case DS_TYPE_COUNTER:
    vl->values[index].counter =
        (counter_t)strtoull(node_value,
                            /* endptr = */ NULL, /* base = */ 0);
    break;
  case DS_TYPE_DERIVE:
    vl->values[index].derive =
        (derive_t)strtoll(node_value,
                          /* endptr = */ NULL, /* base = */ 0);
    break;
  case DS_TYPE_ABSOLUTE:
    vl->values[index].absolute =
        (absolute_t)strtoull(node_value,
                             /* endptr = */ NULL, /* base = */ 0);
    break;
  case DS_TYPE_GAUGE:
    vl->values[index].gauge = (gauge_t)strtod(node_value,
                                              /* endptr = */ NULL);
  }

  xmlFree(node_value);

  /* We have reached here which means that
   * we have got something to work */
  return 0;
} /* }}} int cx_handle_single_value_xpath */

static int cx_handle_all_value_xpaths(xmlXPathContextPtr xpath_ctx, /* {{{ */
                                      cx_xpath_t *xpath, const data_set_t *ds,
                                      value_list_t *vl) {
  value_t values[xpath->values_len];

  assert(xpath->values_len > 0);
  assert(xpath->values_len == vl->values_len);
  assert(xpath->values_len == ds->ds_num);
  vl->values = values;

  for (size_t i = 0; i < xpath->values_len; i++) {
    if (cx_handle_single_value_xpath(xpath_ctx, xpath, ds, vl, i) != 0)
      return -1; /* An error has been printed. */
  }              /* for (i = 0; i < xpath->values_len; i++) */

  plugin_dispatch_values(vl);
  vl->values = NULL;

  return 0;
} /* }}} int cx_handle_all_value_xpaths */

static int cx_handle_instance_xpath(xmlXPathContextPtr xpath_ctx, /* {{{ */
                                    cx_xpath_t *xpath, value_list_t *vl) {

  /* Handle type instance */
  if (xpath->instance != NULL) {
    char *node_value =
        cx_get_text_node_value(xpath_ctx, xpath->instance, "InstanceFrom");
    if (node_value == NULL)
      return -1;

    if (xpath->instance_prefix != NULL)
      snprintf(vl->type_instance, sizeof(vl->type_instance), "%s%s",
               xpath->instance_prefix, node_value);
    else
      sstrncpy(vl->type_instance, node_value, sizeof(vl->type_instance));

    xmlFree(node_value);
  } else if (xpath->instance_prefix != NULL)
    sstrncpy(vl->type_instance, xpath->instance_prefix,
             sizeof(vl->type_instance));

  /* Handle plugin instance */
  if (xpath->plugin_instance_from != NULL) {
    char *node_value = cx_get_text_node_value(
        xpath_ctx, xpath->plugin_instance_from, "PluginInstanceFrom");

    if (node_value == NULL)
      return -1;

    sstrncpy(vl->plugin_instance, node_value, sizeof(vl->plugin_instance));
    xmlFree(node_value);
  }

  return 0;
} /* }}} int cx_handle_instance_xpath */

static int cx_handle_xpath(const cx_t *db, /* {{{ */
                           xmlXPathContextPtr xpath_ctx, cx_xpath_t *xpath) {

  const data_set_t *ds = plugin_get_ds(xpath->type);
  if (cx_check_type(ds, xpath) != 0)
    return -1;

  xmlXPathObjectPtr base_node_obj = cx_evaluate_xpath(xpath_ctx, xpath->path);
  if (base_node_obj == NULL)
    return -1; /* error is logged already */

  xmlNodeSetPtr base_nodes = base_node_obj->nodesetval;
  int total_nodes = (base_nodes) ? base_nodes->nodeNr : 0;

  if (total_nodes == 0) {
    ERROR("curl_xml plugin: "
          "xpath expression \"%s\" doesn't match any of the nodes. "
          "Skipping the xpath block...",
          xpath->path);
    xmlXPathFreeObject(base_node_obj);
    return -1;
  }

  /* If base_xpath returned multiple results, then */
  /* InstanceFrom or PluginInstanceFrom in the xpath block is required */
  if (total_nodes > 1 && xpath->instance == NULL &&
      xpath->plugin_instance_from == NULL) {
    ERROR("curl_xml plugin: "
          "InstanceFrom or PluginInstanceFrom is must in xpath block "
          "since the base xpath expression \"%s\" "
          "returned multiple results. Skipping the xpath block...",
          xpath->path);
    xmlXPathFreeObject(base_node_obj);
    return -1;
  }

  value_list_t vl = VALUE_LIST_INIT;

  /* set the values for the value_list */
  vl.values_len = ds->ds_num;
  sstrncpy(vl.type, xpath->type, sizeof(vl.type));
  sstrncpy(vl.plugin, (db->plugin_name != NULL) ? db->plugin_name : "curl_xml",
           sizeof(vl.plugin));
  sstrncpy(vl.host, cx_host(db), sizeof(vl.host));

  for (int i = 0; i < total_nodes; i++) {
    xpath_ctx->node = base_nodes->nodeTab[i];

    if (db->instance != NULL)
      sstrncpy(vl.plugin_instance, db->instance, sizeof(vl.plugin_instance));

    if (cx_handle_instance_xpath(xpath_ctx, xpath, &vl) != 0)
      continue; /* An error has already been reported. */

    if (cx_handle_all_value_xpaths(xpath_ctx, xpath, ds, &vl) != 0)
      continue; /* An error has been logged. */
  }             /* for (i = 0; i < total_nodes; i++) */

  /* free up the allocated memory */
  xmlXPathFreeObject(base_node_obj);

  return 0;
} /* }}} cx_handle_xpath */

static int cx_handle_parsed_xml(cx_t *db, xmlDocPtr doc, /* {{{ */
                                xmlXPathContextPtr xpath_ctx) {
  int status = -1;

  llentry_t *le = llist_head(db->xpath_list);
  while (le != NULL) {
    cx_xpath_t *xpath = (cx_xpath_t *)le->value;

    if (cx_handle_xpath(db, xpath_ctx, xpath) == 0)
      status = 0; /* we got atleast one success */

    le = le->next;
  } /* while (le != NULL) */

  return status;
} /* }}} cx_handle_parsed_xml */

static int cx_parse_xml(cx_t *db, char *xml) /* {{{ */
{
  /* Load the XML */
  xmlDocPtr doc = xmlParseDoc(BAD_CAST xml);
  if (doc == NULL) {
    ERROR("curl_xml plugin: Failed to parse the xml document  - %s", xml);
    return -1;
  }

  xmlXPathContextPtr xpath_ctx = xmlXPathNewContext(doc);
  if (xpath_ctx == NULL) {
    ERROR("curl_xml plugin: Failed to create the xml context");
    xmlFreeDoc(doc);
    return -1;
  }

  for (size_t i = 0; i < db->namespaces_num; i++) {
    cx_namespace_t const *ns = db->namespaces + i;
    int status =
        xmlXPathRegisterNs(xpath_ctx, BAD_CAST ns->prefix, BAD_CAST ns->url);
    if (status != 0) {
      ERROR("curl_xml plugin: "
            "unable to register NS with prefix=\"%s\" and href=\"%s\"\n",
            ns->prefix, ns->url);
      xmlXPathFreeContext(xpath_ctx);
      xmlFreeDoc(doc);
      return status;
    }
  }

  int status = cx_handle_parsed_xml(db, doc, xpath_ctx);
  /* Cleanup */
  xmlXPathFreeContext(xpath_ctx);
  xmlFreeDoc(doc);
  return status;
} /* }}} cx_parse_xml */

static int cx_read(user_data_t *ud) /* {{{ */
{
  if ((ud == NULL) || (ud->data == NULL)) {
    ERROR("curl_xml plugin: cx_read: Invalid user data.");
    return -1;
  }

  long rc;
  char *url;
  cx_t *db = (cx_t *)ud->data;

  db->buffer_fill = 0;

  curl_easy_setopt(db->curl, CURLOPT_URL, db->url);

  int status = curl_easy_perform(db->curl);
  if (status != CURLE_OK) {
    ERROR("curl_xml plugin: curl_easy_perform failed with status %i: %s (%s)",
          status, db->curl_errbuf, db->url);
    return -1;
  }
  if (db->stats != NULL)
    curl_stats_dispatch(db->stats, db->curl, cx_host(db), "curl_xml",
                        db->instance);

  curl_easy_getinfo(db->curl, CURLINFO_EFFECTIVE_URL, &url);
  curl_easy_getinfo(db->curl, CURLINFO_RESPONSE_CODE, &rc);

  /* The response code is zero if a non-HTTP transport was used. */
  if ((rc != 0) && (rc != 200)) {
    ERROR(
        "curl_xml plugin: curl_easy_perform failed with response code %ld (%s)",
        rc, url);
    return -1;
  }

  status = cx_parse_xml(db, db->buffer);
  db->buffer_fill = 0;

  return status;
} /* }}} int cx_read */

/* Configuration handling functions {{{ */

static int cx_config_add_values(const char *name, cx_xpath_t *xpath, /* {{{ */
                                oconfig_item_t *ci) {
  if (ci->values_num < 1) {
    WARNING("curl_xml plugin: `ValuesFrom' needs at least one argument.");
    return -1;
  }

  for (int i = 0; i < ci->values_num; i++)
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING("curl_xml plugin: `ValuesFrom' needs only string argument.");
      return -1;
    }

  sfree(xpath->values);

  xpath->values_len = 0;
  xpath->values = malloc(sizeof(cx_values_t) * ci->values_num);
  if (xpath->values == NULL)
    return -1;
  xpath->values_len = (size_t)ci->values_num;

  /* populate cx_values_t structure */
  for (int i = 0; i < ci->values_num; i++) {
    xpath->values[i].path_len = sizeof(ci->values[i].value.string);
    sstrncpy(xpath->values[i].path, ci->values[i].value.string,
             sizeof(xpath->values[i].path));
  }

  return 0;
} /* }}} cx_config_add_values */

static int cx_config_add_xpath(cx_t *db, oconfig_item_t *ci) /* {{{ */
{
  cx_xpath_t *xpath = calloc(1, sizeof(*xpath));
  if (xpath == NULL) {
    ERROR("curl_xml plugin: calloc failed.");
    return -1;
  }

  int status = cf_util_get_string(ci, &xpath->path);
  if (status != 0) {
    cx_xpath_free(xpath);
    return status;
  }

  /* error out if xpath->path is an empty string */
  if (strlen(xpath->path) == 0) {
    ERROR("curl_xml plugin: invalid xpath. "
          "xpath value can't be an empty string");
    cx_xpath_free(xpath);
    return -1;
  }

  status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Type", child->key) == 0)
      status = cf_util_get_string(child, &xpath->type);
    else if (strcasecmp("InstancePrefix", child->key) == 0)
      status = cf_util_get_string(child, &xpath->instance_prefix);
    else if (strcasecmp("InstanceFrom", child->key) == 0)
      status = cf_util_get_string(child, &xpath->instance);
    else if (strcasecmp("PluginInstanceFrom", child->key) == 0)
      status = cf_util_get_string(child, &xpath->plugin_instance_from);
    else if (strcasecmp("ValuesFrom", child->key) == 0)
      status = cx_config_add_values("ValuesFrom", xpath, child);
    else {
      WARNING("curl_xml plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  if (status != 0) {
    cx_xpath_free(xpath);
    return status;
  }

  if (xpath->type == NULL) {
    WARNING("curl_xml plugin: `Type' missing in `xpath' block.");
    cx_xpath_free(xpath);
    return -1;
  }

  if (xpath->values_len == 0) {
    WARNING("curl_xml plugin: `ValuesFrom' missing in `xpath' block.");
    cx_xpath_free(xpath);
    return -1;
  }

  llentry_t *le = llentry_create(xpath->path, xpath);
  if (le == NULL) {
    ERROR("curl_xml plugin: llentry_create failed.");
    cx_xpath_free(xpath);
    return -1;
  }

  llist_append(db->xpath_list, le);
  return 0;
} /* }}} int cx_config_add_xpath */

static int cx_config_add_namespace(cx_t *db, /* {{{ */
                                   oconfig_item_t *ci) {

  if ((ci->values_num != 2) || (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING)) {
    WARNING("curl_xml plugin: The `Namespace' option "
            "needs exactly two string arguments.");
    return EINVAL;
  }

  cx_namespace_t *ns = realloc(
      db->namespaces, sizeof(*db->namespaces) * (db->namespaces_num + 1));
  if (ns == NULL) {
    ERROR("curl_xml plugin: realloc failed.");
    return ENOMEM;
  }
  db->namespaces = ns;
  ns = db->namespaces + db->namespaces_num;
  memset(ns, 0, sizeof(*ns));

  ns->prefix = strdup(ci->values[0].value.string);
  ns->url = strdup(ci->values[1].value.string);

  if ((ns->prefix == NULL) || (ns->url == NULL)) {
    sfree(ns->prefix);
    sfree(ns->url);
    ERROR("curl_xml plugin: strdup failed.");
    return ENOMEM;
  }

  db->namespaces_num++;
  return 0;
} /* }}} int cx_config_add_namespace */

/* Initialize db->curl */
static int cx_init_curl(cx_t *db) /* {{{ */
{
  db->curl = curl_easy_init();
  if (db->curl == NULL) {
    ERROR("curl_xml plugin: curl_easy_init failed.");
    return -1;
  }

  curl_easy_setopt(db->curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(db->curl, CURLOPT_WRITEFUNCTION, cx_curl_callback);
  curl_easy_setopt(db->curl, CURLOPT_WRITEDATA, db);
  curl_easy_setopt(db->curl, CURLOPT_USERAGENT, COLLECTD_USERAGENT);
  curl_easy_setopt(db->curl, CURLOPT_ERRORBUFFER, db->curl_errbuf);
  curl_easy_setopt(db->curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(db->curl, CURLOPT_MAXREDIRS, 50L);

  if (db->user != NULL) {
#ifdef HAVE_CURLOPT_USERNAME
    curl_easy_setopt(db->curl, CURLOPT_USERNAME, db->user);
    curl_easy_setopt(db->curl, CURLOPT_PASSWORD,
                     (db->pass == NULL) ? "" : db->pass);
#else
    size_t credentials_size;

    credentials_size = strlen(db->user) + 2;
    if (db->pass != NULL)
      credentials_size += strlen(db->pass);

    db->credentials = malloc(credentials_size);
    if (db->credentials == NULL) {
      ERROR("curl_xml plugin: malloc failed.");
      return -1;
    }

    snprintf(db->credentials, credentials_size, "%s:%s", db->user,
             (db->pass == NULL) ? "" : db->pass);
    curl_easy_setopt(db->curl, CURLOPT_USERPWD, db->credentials);
#endif

    if (db->digest)
      curl_easy_setopt(db->curl, CURLOPT_HTTPAUTH, CURLAUTH_DIGEST);
  }

  curl_easy_setopt(db->curl, CURLOPT_SSL_VERIFYPEER, db->verify_peer ? 1L : 0L);
  curl_easy_setopt(db->curl, CURLOPT_SSL_VERIFYHOST, db->verify_host ? 2L : 0L);
  if (db->cacert != NULL)
    curl_easy_setopt(db->curl, CURLOPT_CAINFO, db->cacert);
  if (db->headers != NULL)
    curl_easy_setopt(db->curl, CURLOPT_HTTPHEADER, db->headers);
  if (db->post_body != NULL)
    curl_easy_setopt(db->curl, CURLOPT_POSTFIELDS, db->post_body);

#ifdef HAVE_CURLOPT_TIMEOUT_MS
  if (db->timeout >= 0)
    curl_easy_setopt(db->curl, CURLOPT_TIMEOUT_MS, (long)db->timeout);
  else
    curl_easy_setopt(db->curl, CURLOPT_TIMEOUT_MS,
                     (long)CDTIME_T_TO_MS(plugin_get_interval()));
#endif

  return 0;
} /* }}} int cx_init_curl */

static int cx_config_add_url(oconfig_item_t *ci) /* {{{ */
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("curl_xml plugin: The `URL' block "
            "needs exactly one string argument.");
    return -1;
  }

  cx_t *db = calloc(1, sizeof(*db));
  if (db == NULL) {
    ERROR("curl_xml plugin: calloc failed.");
    return -1;
  }

  db->instance = strdup("default");
  if (db->instance == NULL) {
    ERROR("curl_xml plugin: strdup failed.");
    sfree(db);
    return -1;
  }

  db->xpath_list = llist_create();
  if (db->xpath_list == NULL) {
    ERROR("curl_xml plugin: list creation failed.");
    sfree(db->instance);
    sfree(db);
    return -1;
  }

  db->timeout = -1;

  int status = cf_util_get_string(ci, &db->url);
  if (status != 0) {
    llist_destroy(db->xpath_list);
    sfree(db->instance);
    sfree(db);
    return status;
  }

  /* Fill the `cx_t' structure.. */
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Instance", child->key) == 0)
      status = cf_util_get_string(child, &db->instance);
    else if (strcasecmp("Plugin", child->key) == 0)
      status = cf_util_get_string(child, &db->plugin_name);
    else if (strcasecmp("Host", child->key) == 0)
      status = cf_util_get_string(child, &db->host);
    else if (strcasecmp("User", child->key) == 0)
      status = cf_util_get_string(child, &db->user);
    else if (strcasecmp("Password", child->key) == 0)
      status = cf_util_get_string(child, &db->pass);
    else if (strcasecmp("Digest", child->key) == 0)
      status = cf_util_get_boolean(child, &db->digest);
    else if (strcasecmp("VerifyPeer", child->key) == 0)
      status = cf_util_get_boolean(child, &db->verify_peer);
    else if (strcasecmp("VerifyHost", child->key) == 0)
      status = cf_util_get_boolean(child, &db->verify_host);
    else if (strcasecmp("CACert", child->key) == 0)
      status = cf_util_get_string(child, &db->cacert);
    else if (strcasecmp("xpath", child->key) == 0)
      status = cx_config_add_xpath(db, child);
    else if (strcasecmp("Header", child->key) == 0)
      status = cx_config_append_string("Header", &db->headers, child);
    else if (strcasecmp("Post", child->key) == 0)
      status = cf_util_get_string(child, &db->post_body);
    else if (strcasecmp("Namespace", child->key) == 0)
      status = cx_config_add_namespace(db, child);
    else if (strcasecmp("Timeout", child->key) == 0)
      status = cf_util_get_int(child, &db->timeout);
    else if (strcasecmp("Statistics", child->key) == 0) {
      db->stats = curl_stats_from_config(child);
      if (db->stats == NULL)
        status = -1;
    } else {
      WARNING("curl_xml plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status != 0) {
    cx_free(db);
    return status;
  }

  if (llist_size(db->xpath_list) == 0) {
    WARNING("curl_xml plugin: No `xpath' block within `URL' block `%s'.",
            db->url);
    cx_free(db);
    return -1;
  }

  if (cx_init_curl(db) != 0) {
    cx_free(db);
    return -1;
  }

  /* If all went well, register this database for reading */
  DEBUG("curl_xml plugin: Registering new read callback: %s", db->instance);

  char *cb_name = ssnprintf_alloc("curl_xml-%s-%s", db->instance, db->url);

  plugin_register_complex_read(/* group = */ "curl_xml", cb_name, cx_read,
                               /* interval = */ 0,
                               &(user_data_t){
                                   .data = db, .free_func = cx_free,
                               });
  sfree(cb_name);
  return 0;
} /* }}} int cx_config_add_url */

/* }}} End of configuration handling functions */

static int cx_config(oconfig_item_t *ci) /* {{{ */
{
  int success = 0;
  int errors = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("URL", child->key) == 0) {
      if (cx_config_add_url(child) == 0)
        success++;
      else
        errors++;
    } else {
      WARNING("curl_xml plugin: Option `%s' not allowed here.", child->key);
      errors++;
    }
  }

  if ((success == 0) && (errors > 0)) {
    ERROR("curl_xml plugin: All statements failed.");
    return -1;
  }

  return 0;
} /* }}} int cx_config */

static int cx_init(void) /* {{{ */
{
  /* Call this while collectd is still single-threaded to avoid
   * initialization issues in libgcrypt. */
  curl_global_init(CURL_GLOBAL_SSL);
  return 0;
} /* }}} int cx_init */

void module_register(void) {
  plugin_register_complex_config("curl_xml", cx_config);
  plugin_register_init("curl_xml", cx_init);
} /* void module_register */
