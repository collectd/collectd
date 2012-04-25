/**
 * collectd - src/bind.c
 * Copyright (C) 2009       Bruno Prémont
 * Copyright (C) 2009,2010  Florian Forster
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
 *   Bruno Prémont <bonbons at linux-vserver.org>
 *   Florian Forster <octo at collectd.org>
 **/

#include "config.h"

#if STRPTIME_NEEDS_STANDARDS
# ifndef _ISOC99_SOURCE
#  define _ISOC99_SOURCE 1
# endif
# ifndef _POSIX_C_SOURCE
#  define _POSIX_C_SOURCE 200112L
# endif
# ifndef _XOPEN_SOURCE
#  define _XOPEN_SOURCE 500
# endif
#endif /* STRPTIME_NEEDS_STANDARDS */

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

/* Some versions of libcurl don't include this themselves and then don't have
 * fd_set available. */
#if HAVE_SYS_SELECT_H
# include <sys/select.h>
#endif

#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

#ifndef BIND_DEFAULT_URL
# define BIND_DEFAULT_URL "http://localhost:8053/"
#endif

/* 
 * Some types used for the callback functions. `translation_table_ptr_t' and
 * `list_info_ptr_t' are passed to the callbacks in the `void *user_data'
 * pointer.
 */
typedef int (*list_callback_t) (const char *name, value_t value,
    time_t current_time, void *user_data);

struct cb_view_s
{
  char *name;

  int qtypes;
  int resolver_stats;
  int cacherrsets;

  char **zones;
  size_t zones_num;
};
typedef struct cb_view_s cb_view_t;

struct translation_info_s
{
  const char *xml_name;
  const char *type;
  const char *type_instance;
};
typedef struct translation_info_s translation_info_t;

struct translation_table_ptr_s
{
  const translation_info_t *table;
  size_t table_length;
  const char *plugin_instance;
};
typedef struct translation_table_ptr_s translation_table_ptr_t;

struct list_info_ptr_s
{
  const char *plugin_instance;
  const char *type;
};
typedef struct list_info_ptr_s list_info_ptr_t;

/* FIXME: Enabled by default for backwards compatibility. */
/* TODO: Remove time parsing code. */
static _Bool config_parse_time = 1;

static char *url                   = NULL;
static int global_opcodes          = 1;
static int global_qtypes           = 1;
static int global_server_stats     = 1;
static int global_zone_maint_stats = 1;
static int global_resolver_stats   = 0;
static int global_memory_stats     = 1;

static cb_view_t *views = NULL;
static size_t     views_num = 0;

static CURL *curl = NULL;

static char  *bind_buffer = NULL;
static size_t bind_buffer_size = 0;
static size_t bind_buffer_fill = 0;
static char   bind_curl_error[CURL_ERROR_SIZE];

/* Translation table for the `nsstats' values. */
static const translation_info_t nsstats_translation_table[] = /* {{{ */
{
  /* Requests */
  { "Requestv4",       "dns_request",  "IPv4"        },
  { "Requestv6",       "dns_request",  "IPv6"        },
  { "ReqEdns0",        "dns_request",  "EDNS0"       },
  { "ReqBadEDNSVer",   "dns_request",  "BadEDNSVer"  },
  { "ReqTSIG",         "dns_request",  "TSIG"        },
  { "ReqSIG0",         "dns_request",  "SIG0"        },
  { "ReqBadSIG",       "dns_request",  "BadSIG"      },
  { "ReqTCP",          "dns_request",  "TCP"         },
  /* Rejects */
  { "AuthQryRej",      "dns_reject",   "authorative" },
  { "RecQryRej",       "dns_reject",   "recursive"   },
  { "XfrRej",          "dns_reject",   "transfer"    },
  { "UpdateRej",       "dns_reject",   "update"      },
  /* Responses */
  { "Response",        "dns_response", "normal"      },
  { "TruncatedResp",   "dns_response", "truncated"   },
  { "RespEDNS0",       "dns_response", "EDNS0"       },
  { "RespTSIG",        "dns_response", "TSIG"        },
  { "RespSIG0",        "dns_response", "SIG0"        },
  /* Queries */
  { "QryAuthAns",      "dns_query",    "authorative" },
  { "QryNoauthAns",    "dns_query",    "nonauth"     },
  { "QryReferral",     "dns_query",    "referral"    },
  { "QryRecursion",    "dns_query",    "recursion"   },
  { "QryDuplicate",    "dns_query",    "dupliate"    },
  { "QryDropped",      "dns_query",    "dropped"     },
  { "QryFailure",      "dns_query",    "failure"     },
  /* Response codes */
  { "QrySuccess",      "dns_rcode",    "tx-NOERROR"  },
  { "QryNxrrset",      "dns_rcode",    "tx-NXRRSET"  },
  { "QrySERVFAIL",     "dns_rcode",    "tx-SERVFAIL" },
  { "QryFORMERR",      "dns_rcode",    "tx-FORMERR"  },
  { "QryNXDOMAIN",     "dns_rcode",    "tx-NXDOMAIN" }
#if 0
  { "XfrReqDone",      "type", "type_instance"       },
  { "UpdateReqFwd",    "type", "type_instance"       },
  { "UpdateRespFwd",   "type", "type_instance"       },
  { "UpdateFwdFail",   "type", "type_instance"       },
  { "UpdateDone",      "type", "type_instance"       },
  { "UpdateFail",      "type", "type_instance"       },
  { "UpdateBadPrereq", "type", "type_instance"       },
#endif
};
static int nsstats_translation_table_length =
  STATIC_ARRAY_SIZE (nsstats_translation_table);
/* }}} */

/* Translation table for the `zonestats' values. */
static const translation_info_t zonestats_translation_table[] = /* {{{ */
{
  /* Notify's */
  { "NotifyOutv4",     "dns_notify",   "tx-IPv4"     },
  { "NotifyOutv6",     "dns_notify",   "tx-IPv6"     },
  { "NotifyInv4",      "dns_notify",   "rx-IPv4"     },
  { "NotifyInv6",      "dns_notify",   "rx-IPv6"     },
  { "NotifyRej",       "dns_notify",   "rejected"    },
  /* SOA/AXFS/IXFS requests */
  { "SOAOutv4",        "dns_opcode",   "SOA-IPv4"    },
  { "SOAOutv6",        "dns_opcode",   "SOA-IPv6"    },
  { "AXFRReqv4",       "dns_opcode",   "AXFR-IPv4"   },
  { "AXFRReqv6",       "dns_opcode",   "AXFR-IPv6"   },
  { "IXFRReqv4",       "dns_opcode",   "IXFR-IPv4"   },
  { "IXFRReqv6",       "dns_opcode",   "IXFR-IPv6"   },
  /* Domain transfers */
  { "XfrSuccess",      "dns_transfer", "success"     },
  { "XfrFail",         "dns_transfer", "failure"     }
};
static int zonestats_translation_table_length =
  STATIC_ARRAY_SIZE (zonestats_translation_table);
/* }}} */

/* Translation table for the `resstats' values. */
static const translation_info_t resstats_translation_table[] = /* {{{ */
{
  /* Generic resolver information */
  { "Queryv4",         "dns_query",    "IPv4"        },
  { "Queryv6",         "dns_query",    "IPv6"        },
  { "Responsev4",      "dns_response", "IPv4"        },
  { "Responsev6",      "dns_response", "IPv6"        },
  /* Received response codes */
  { "NXDOMAIN",        "dns_rcode",    "rx-NXDOMAIN" },
  { "SERVFAIL",        "dns_rcode",    "rx-SERVFAIL" },
  { "FORMERR",         "dns_rcode",    "rx-FORMERR"  },
  { "OtherError",      "dns_rcode",    "rx-OTHER"    },
  { "EDNS0Fail",       "dns_rcode",    "rx-EDNS0Fail"},
  /* Received responses */
  { "Mismatch",        "dns_response", "mismatch"    },
  { "Truncated",       "dns_response", "truncated"   },
  { "Lame",            "dns_response", "lame"        },
  { "Retry",           "dns_query",    "retry"       },
#if 0
  { "GlueFetchv4",     "type", "type_instance" },
  { "GlueFetchv6",     "type", "type_instance" },
  { "GlueFetchv4Fail", "type", "type_instance" },
  { "GlueFetchv6Fail", "type", "type_instance" },
#endif
  /* DNSSEC information */
  { "ValAttempt",      "dns_resolver", "DNSSEC-attempt" },
  { "ValOk",           "dns_resolver", "DNSSEC-okay"    },
  { "ValNegOk",        "dns_resolver", "DNSSEC-negokay" },
  { "ValFail",         "dns_resolver", "DNSSEC-fail"    }
};
static int resstats_translation_table_length =
  STATIC_ARRAY_SIZE (resstats_translation_table);
/* }}} */

/* Translation table for the `memory/summary' values. */
static const translation_info_t memsummary_translation_table[] = /* {{{ */
{
  { "TotalUse",        "memory",       "TotalUse"    },
  { "InUse",           "memory",       "InUse"       },
  { "BlockSize",       "memory",       "BlockSize"   },
  { "ContextSize",     "memory",       "ContextSize" },
  { "Lost",            "memory",       "Lost"        }
};
static int memsummary_translation_table_length =
  STATIC_ARRAY_SIZE (memsummary_translation_table);
/* }}} */

static void submit (time_t ts, const char *plugin_instance, /* {{{ */
    const char *type, const char *type_instance, value_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0] = value;

  vl.values = values;
  vl.values_len = 1;
  if (config_parse_time)
    vl.time = TIME_T_TO_CDTIME_T (ts);
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "bind", sizeof(vl.plugin));
  if (plugin_instance) {
    sstrncpy(vl.plugin_instance, plugin_instance,
        sizeof(vl.plugin_instance));
    replace_special (vl.plugin_instance, sizeof (vl.plugin_instance));
  }
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance) {
    sstrncpy(vl.type_instance, type_instance,
        sizeof(vl.type_instance));
    replace_special (vl.plugin_instance, sizeof (vl.plugin_instance));
  }
  plugin_dispatch_values(&vl);
} /* }}} void submit */

static size_t bind_curl_callback (void *buf, size_t size, /* {{{ */
    size_t nmemb, void __attribute__((unused)) *stream)
{
  size_t len = size * nmemb;

  if (len <= 0)
    return (len);

  if ((bind_buffer_fill + len) >= bind_buffer_size)
  {
    char *temp;

    temp = realloc(bind_buffer, bind_buffer_fill + len + 1);
    if (temp == NULL)
    {
      ERROR ("bind plugin: realloc failed.");
      return (0);
    }
    bind_buffer = temp;
    bind_buffer_size = bind_buffer_fill + len + 1;
  }

  memcpy (bind_buffer + bind_buffer_fill, (char *) buf, len);
  bind_buffer_fill += len;
  bind_buffer[bind_buffer_fill] = 0;

  return (len);
} /* }}} size_t bind_curl_callback */

/*
 * Callback, that's called with a translation table.
 * (Plugin instance is fixed, type and type instance come from lookup table.)
 */
static int bind_xml_table_callback (const char *name, value_t value, /* {{{ */
    time_t current_time, void *user_data)
{
  translation_table_ptr_t *table = (translation_table_ptr_t *) user_data;
  size_t i;

  if (table == NULL)
    return (-1);

  for (i = 0; i < table->table_length; i++)
  {
    if (strcmp (table->table[i].xml_name, name) != 0)
      continue;

    submit (current_time,
        table->plugin_instance,
        table->table[i].type,
        table->table[i].type_instance,
        value);
    break;
  }

  return (0);
} /* }}} int bind_xml_table_callback */

/*
 * Callback, that's used for lists.
 * (Plugin instance and type are fixed, xml name is used as type instance.)
 */
static int bind_xml_list_callback (const char *name, /* {{{ */
    value_t value, time_t current_time, void *user_data)
{
  list_info_ptr_t *list_info = (list_info_ptr_t *) user_data;

  if (list_info == NULL)
    return (-1);

  submit (current_time,
      list_info->plugin_instance,
      list_info->type,
      /* type instance = */ name,
      value);

  return (0);
} /* }}} int bind_xml_list_callback */

static int bind_xml_read_derive (xmlDoc *doc, xmlNode *node, /* {{{ */
    derive_t *ret_value)
{
  char *str_ptr;
  value_t value;
  int status;

  str_ptr = (char *) xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
  if (str_ptr == NULL)
  {
    ERROR ("bind plugin: bind_xml_read_derive: xmlNodeListGetString failed.");
    return (-1);
  }

  status = parse_value (str_ptr, &value, DS_TYPE_DERIVE);
  if (status != 0)
  {
    ERROR ("bind plugin: Parsing string \"%s\" to derive value failed.",
        str_ptr);
    return (-1);
  }

  *ret_value = value.derive;
  return (0);
} /* }}} int bind_xml_read_derive */

static int bind_xml_read_gauge (xmlDoc *doc, xmlNode *node, /* {{{ */
    gauge_t *ret_value)
{
  char *str_ptr, *end_ptr;
  double value;

  str_ptr = (char *) xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
  if (str_ptr == NULL)
  {
    ERROR ("bind plugin: bind_xml_read_gauge: xmlNodeListGetString failed.");
    return (-1);
  }

  errno = 0;
  value = strtod (str_ptr, &end_ptr);
  xmlFree(str_ptr);
  if (str_ptr == end_ptr || errno)
  {
    if (errno && (value < 0))
      ERROR ("bind plugin: bind_xml_read_gauge: strtod failed with underflow.");
    else if (errno && (value > 0))
      ERROR ("bind plugin: bind_xml_read_gauge: strtod failed with overflow.");
    else
      ERROR ("bind plugin: bind_xml_read_gauge: strtod failed.");
    return (-1);
  }

  *ret_value = (gauge_t) value;
  return (0);
} /* }}} int bind_xml_read_gauge */

static int bind_xml_read_timestamp (const char *xpath_expression, /* {{{ */
    xmlDoc *doc, xmlXPathContext *xpathCtx, time_t *ret_value)
{
  xmlXPathObject *xpathObj = NULL;
  xmlNode *node;
  char *str_ptr;
  char *tmp;
  struct tm tm;

  xpathObj = xmlXPathEvalExpression (BAD_CAST xpath_expression, xpathCtx);
  if (xpathObj == NULL)
  {
    ERROR ("bind plugin: Unable to evaluate XPath expression `%s'.",
        xpath_expression);
    return (-1);
  }

  if ((xpathObj->nodesetval == NULL) || (xpathObj->nodesetval->nodeNr < 1))
  {
    xmlXPathFreeObject (xpathObj);
    return (-1);
  }

  if (xpathObj->nodesetval->nodeNr != 1)
  {
    NOTICE ("bind plugin: Evaluating the XPath expression `%s' returned "
        "%i nodes. Only handling the first one.",
        xpath_expression, xpathObj->nodesetval->nodeNr);
  }

  node = xpathObj->nodesetval->nodeTab[0];

  if (node->xmlChildrenNode == NULL)
  {
    ERROR ("bind plugin: bind_xml_read_timestamp: "
        "node->xmlChildrenNode == NULL");
    xmlXPathFreeObject (xpathObj);
    return (-1);
  }

  str_ptr = (char *) xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
  if (str_ptr == NULL)
  {
    ERROR ("bind plugin: bind_xml_read_timestamp: xmlNodeListGetString failed.");
    xmlXPathFreeObject (xpathObj);
    return (-1);
  }

  memset (&tm, 0, sizeof(tm));
  tmp = strptime (str_ptr, "%Y-%m-%dT%T", &tm);
  xmlFree(str_ptr);
  if (tmp == NULL)
  {
    ERROR ("bind plugin: bind_xml_read_timestamp: strptime failed.");
    xmlXPathFreeObject (xpathObj);
    return (-1);
  }

  *ret_value = mktime(&tm);

  xmlXPathFreeObject (xpathObj);
  return (0);
} /* }}} int bind_xml_read_timestamp */

/* 
 * bind_parse_generic_name_value
 *
 * Reads statistics in the form:
 * <foo>
 *   <name>QUERY</name>
 *   <counter>123</counter>
 * </foo>
 */
static int bind_parse_generic_name_value (const char *xpath_expression, /* {{{ */
    list_callback_t list_callback,
    void *user_data,
    xmlDoc *doc, xmlXPathContext *xpathCtx,
    time_t current_time, int ds_type)
{
  xmlXPathObject *xpathObj = NULL;
  int num_entries;
  int i;

  xpathObj = xmlXPathEvalExpression(BAD_CAST xpath_expression, xpathCtx);
  if (xpathObj == NULL)
  {
    ERROR("bind plugin: Unable to evaluate XPath expression `%s'.",
        xpath_expression);
    return (-1);
  }

  num_entries = 0;
  /* Iterate over all matching nodes. */
  for (i = 0; xpathObj->nodesetval && (i < xpathObj->nodesetval->nodeNr); i++)
  {
    xmlNode *name_node = NULL;
    xmlNode *counter = NULL;
    xmlNode *parent;
    xmlNode *child;

    parent = xpathObj->nodesetval->nodeTab[i];
    DEBUG ("bind plugin: bind_parse_generic_name_value: parent->name = %s;",
        (char *) parent->name);

    /* Iterate over all child nodes. */
    for (child = parent->xmlChildrenNode;
        child != NULL;
        child = child->next)
    {
      if (child->type != XML_ELEMENT_NODE)
        continue;

      if (xmlStrcmp (BAD_CAST "name", child->name) == 0)
        name_node = child;
      else if (xmlStrcmp (BAD_CAST "counter", child->name) == 0)
        counter = child;
    }

    if ((name_node != NULL) && (counter != NULL))
    {
      char *name = (char *) xmlNodeListGetString (doc,
          name_node->xmlChildrenNode, 1);
      value_t value;
      int status;

      if (ds_type == DS_TYPE_GAUGE)
        status = bind_xml_read_gauge (doc, counter, &value.gauge);
      else
        status = bind_xml_read_derive (doc, counter, &value.derive);
      if (status != 0)
        continue;

      status = (*list_callback) (name, value, current_time, user_data);
      if (status == 0)
        num_entries++;

      xmlFree (name);
    }
  }

  DEBUG ("bind plugin: Found %d %s for XPath expression `%s'",
      num_entries, (num_entries == 1) ? "entry" : "entries",
      xpath_expression);

  xmlXPathFreeObject(xpathObj);

  return (0);
} /* }}} int bind_parse_generic_name_value */

/* 
 * bind_parse_generic_value_list
 *
 * Reads statistics in the form:
 * <foo>
 *   <name0>123</name0>
 *   <name1>234</name1>
 *   <name2>345</name2>
 *   :
 * </foo>
 */
static int bind_parse_generic_value_list (const char *xpath_expression, /* {{{ */
    list_callback_t list_callback,
    void *user_data,
    xmlDoc *doc, xmlXPathContext *xpathCtx,
    time_t current_time, int ds_type)
{
  xmlXPathObject *xpathObj = NULL;
  int num_entries;
  int i;

  xpathObj = xmlXPathEvalExpression(BAD_CAST xpath_expression, xpathCtx);
  if (xpathObj == NULL)
  {
    ERROR("bind plugin: Unable to evaluate XPath expression `%s'.",
        xpath_expression);
    return (-1);
  }

  num_entries = 0;
  /* Iterate over all matching nodes. */
  for (i = 0; xpathObj->nodesetval && (i < xpathObj->nodesetval->nodeNr); i++)
  {
    xmlNode *child;

    /* Iterate over all child nodes. */
    for (child = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode;
        child != NULL;
        child = child->next)
    {
      char *node_name;
      value_t value;
      int status;

      if (child->type != XML_ELEMENT_NODE)
        continue;

      node_name = (char *) child->name;

      if (ds_type == DS_TYPE_GAUGE)
        status = bind_xml_read_gauge (doc, child, &value.gauge);
      else
        status = bind_xml_read_derive (doc, child, &value.derive);
      if (status != 0)
        continue;

      status = (*list_callback) (node_name, value, current_time, user_data);
      if (status == 0)
        num_entries++;
    }
  }

  DEBUG ("bind plugin: Found %d %s for XPath expression `%s'",
      num_entries, (num_entries == 1) ? "entry" : "entries",
      xpath_expression);

  xmlXPathFreeObject(xpathObj);

  return (0);
} /* }}} int bind_parse_generic_value_list */

static int bind_xml_stats_handle_zone (int version, xmlDoc *doc, /* {{{ */
    xmlXPathContext *path_ctx, xmlNode *node, cb_view_t *view,
    time_t current_time)
{
  xmlXPathObject *path_obj;
  char *zone_name = NULL;
  int i;
  size_t j;

  path_obj = xmlXPathEvalExpression (BAD_CAST "name", path_ctx);
  if (path_obj == NULL)
  {
    ERROR ("bind plugin: xmlXPathEvalExpression failed.");
    return (-1);
  }

  for (i = 0; path_obj->nodesetval && (i < path_obj->nodesetval->nodeNr); i++)
  {
    zone_name = (char *) xmlNodeListGetString (doc,
        path_obj->nodesetval->nodeTab[i]->xmlChildrenNode, 1);
    if (zone_name != NULL)
      break;
  }

  if (zone_name == NULL)
  {
    ERROR ("bind plugin: Could not determine zone name.");
    xmlXPathFreeObject (path_obj);
    return (-1);
  }

  for (j = 0; j < view->zones_num; j++)
  {
    if (strcasecmp (zone_name, view->zones[j]) == 0)
      break;
  }

  xmlFree (zone_name);
  zone_name = NULL;

  if (j >= views_num)
  {
    xmlXPathFreeObject (path_obj);
    return (0);
  }

  zone_name = view->zones[j];

  DEBUG ("bind plugin: bind_xml_stats_handle_zone: Found zone `%s'.",
      zone_name);

  { /* Parse the <counters> tag {{{ */
    char plugin_instance[DATA_MAX_NAME_LEN];
    translation_table_ptr_t table_ptr =
    { 
      nsstats_translation_table,
      nsstats_translation_table_length,
      plugin_instance
    };

    ssnprintf (plugin_instance, sizeof (plugin_instance), "%s-zone-%s",
        view->name, zone_name);

    bind_parse_generic_value_list (/* xpath = */ "counters",
        /* callback = */ bind_xml_table_callback,
        /* user_data = */ &table_ptr,
        doc, path_ctx, current_time, DS_TYPE_COUNTER);
  } /* }}} */

  xmlXPathFreeObject (path_obj);

  return (0);
} /* }}} int bind_xml_stats_handle_zone */

static int bind_xml_stats_search_zones (int version, xmlDoc *doc, /* {{{ */
    xmlXPathContext *path_ctx, xmlNode *node, cb_view_t *view,
    time_t current_time)
{
  xmlXPathObject *zone_nodes = NULL;
  xmlXPathContext *zone_path_context;
  int i;

  zone_path_context = xmlXPathNewContext (doc);
  if (zone_path_context == NULL)
  {
    ERROR ("bind plugin: xmlXPathNewContext failed.");
    return (-1);
  }

  zone_nodes = xmlXPathEvalExpression (BAD_CAST "zones/zone", path_ctx);
  if (zone_nodes == NULL)
  {
    ERROR ("bind plugin: Cannot find any <view> tags.");
    xmlXPathFreeContext (zone_path_context);
    return (-1);
  }

  for (i = 0; i < zone_nodes->nodesetval->nodeNr; i++)
  {
    xmlNode *node;

    node = zone_nodes->nodesetval->nodeTab[i];
    assert (node != NULL);

    zone_path_context->node = node;

    bind_xml_stats_handle_zone (version, doc, zone_path_context, node, view,
        current_time);
  }

  xmlXPathFreeObject (zone_nodes);
  xmlXPathFreeContext (zone_path_context);
  return (0);
} /* }}} int bind_xml_stats_search_zones */

static int bind_xml_stats_handle_view (int version, xmlDoc *doc, /* {{{ */
    xmlXPathContext *path_ctx, xmlNode *node, time_t current_time)
{
  xmlXPathObject *path_obj;
  char *view_name = NULL;
  cb_view_t *view;
  int i;
  size_t j;

  path_obj = xmlXPathEvalExpression (BAD_CAST "name", path_ctx);
  if (path_obj == NULL)
  {
    ERROR ("bind plugin: xmlXPathEvalExpression failed.");
    return (-1);
  }

  for (i = 0; path_obj->nodesetval && (i < path_obj->nodesetval->nodeNr); i++)
  {
    view_name = (char *) xmlNodeListGetString (doc,
        path_obj->nodesetval->nodeTab[i]->xmlChildrenNode, 1);
    if (view_name != NULL)
      break;
  }

  if (view_name == NULL)
  {
    ERROR ("bind plugin: Could not determine view name.");
    xmlXPathFreeObject (path_obj);
    return (-1);
  }

  for (j = 0; j < views_num; j++)
  {
    if (strcasecmp (view_name, views[j].name) == 0)
      break;
  }

  xmlFree (view_name);
  xmlXPathFreeObject (path_obj);

  view_name = NULL;
  path_obj = NULL;

  if (j >= views_num)
    return (0);

  view = views + j;

  DEBUG ("bind plugin: bind_xml_stats_handle_view: Found view `%s'.",
      view->name);

  if (view->qtypes != 0) /* {{{ */
  {
    char plugin_instance[DATA_MAX_NAME_LEN];
    list_info_ptr_t list_info =
    {
      plugin_instance,
      /* type = */ "dns_qtype"
    };

    ssnprintf (plugin_instance, sizeof (plugin_instance), "%s-qtypes",
        view->name);

    bind_parse_generic_name_value (/* xpath = */ "rdtype",
        /* callback = */ bind_xml_list_callback,
        /* user_data = */ &list_info,
        doc, path_ctx, current_time, DS_TYPE_COUNTER);
  } /* }}} */

  if (view->resolver_stats != 0) /* {{{ */
  {
    char plugin_instance[DATA_MAX_NAME_LEN];
    translation_table_ptr_t table_ptr =
    { 
      resstats_translation_table,
      resstats_translation_table_length,
      plugin_instance
    };

    ssnprintf (plugin_instance, sizeof (plugin_instance),
        "%s-resolver_stats", view->name);

    bind_parse_generic_name_value ("resstat",
        /* callback = */ bind_xml_table_callback,
        /* user_data = */ &table_ptr,
        doc, path_ctx, current_time, DS_TYPE_COUNTER);
  } /* }}} */

  /* Record types in the cache */
  if (view->cacherrsets != 0) /* {{{ */
  {
    char plugin_instance[DATA_MAX_NAME_LEN];
    list_info_ptr_t list_info =
    {
      plugin_instance,
      /* type = */ "dns_qtype_cached"
    };

    ssnprintf (plugin_instance, sizeof (plugin_instance), "%s-cache_rr_sets",
        view->name);

    bind_parse_generic_name_value (/* xpath = */ "cache/rrset",
        /* callback = */ bind_xml_list_callback,
        /* user_data = */ &list_info,
        doc, path_ctx, current_time, DS_TYPE_GAUGE);
  } /* }}} */

  if (view->zones_num > 0)
    bind_xml_stats_search_zones (version, doc, path_ctx, node, view,
        current_time);

  return (0);
} /* }}} int bind_xml_stats_handle_view */

static int bind_xml_stats_search_views (int version, xmlDoc *doc, /* {{{ */
    xmlXPathContext *xpathCtx, xmlNode *statsnode, time_t current_time)
{
  xmlXPathObject *view_nodes = NULL;
  xmlXPathContext *view_path_context;
  int i;

  view_path_context = xmlXPathNewContext (doc);
  if (view_path_context == NULL)
  {
    ERROR ("bind plugin: xmlXPathNewContext failed.");
    return (-1);
  }

  view_nodes = xmlXPathEvalExpression (BAD_CAST "views/view", xpathCtx);
  if (view_nodes == NULL)
  {
    ERROR ("bind plugin: Cannot find any <view> tags.");
    xmlXPathFreeContext (view_path_context);
    return (-1);
  }

  for (i = 0; i < view_nodes->nodesetval->nodeNr; i++)
  {
    xmlNode *node;

    node = view_nodes->nodesetval->nodeTab[i];
    assert (node != NULL);

    view_path_context->node = node;

    bind_xml_stats_handle_view (version, doc, view_path_context, node,
        current_time);
  }

  xmlXPathFreeObject (view_nodes);
  xmlXPathFreeContext (view_path_context);
  return (0);
} /* }}} int bind_xml_stats_search_views */

static int bind_xml_stats (int version, xmlDoc *doc, /* {{{ */
    xmlXPathContext *xpathCtx, xmlNode *statsnode)
{
  time_t current_time = 0;
  int status;

  xpathCtx->node = statsnode;

  /* TODO: Check `server/boot-time' to recognize server restarts. */

  status = bind_xml_read_timestamp ("server/current-time",
      doc, xpathCtx, &current_time);
  if (status != 0)
  {
    ERROR ("bind plugin: Reading `server/current-time' failed.");
    return (-1);
  }
  DEBUG ("bind plugin: Current server time is %i.", (int) current_time);

  /* XPath:     server/requests/opcode
   * Variables: QUERY, IQUERY, NOTIFY, UPDATE, ...
   * Layout:
   *   <opcode>
   *     <name>A</name>
   *     <counter>1</counter>
   *   </opcode>
   *   :
   */
  if (global_opcodes != 0)
  {
    list_info_ptr_t list_info =
    {
      /* plugin instance = */ "global-opcodes",
      /* type = */ "dns_opcode"
    };

    bind_parse_generic_name_value (/* xpath = */ "server/requests/opcode",
        /* callback = */ bind_xml_list_callback,
        /* user_data = */ &list_info,
        doc, xpathCtx, current_time, DS_TYPE_COUNTER);
  }

  /* XPath:     server/queries-in/rdtype
   * Variables: RESERVED0, A, NS, CNAME, SOA, MR, PTR, HINFO, MX, TXT, RP,
   *            X25, PX, AAAA, LOC, SRV, NAPTR, A6, DS, RRSIG, NSEC, DNSKEY,
   *            SPF, TKEY, IXFR, AXFR, ANY, ..., Others
   * Layout:
   *   <rdtype>
   *     <name>A</name>
   *     <counter>1</counter>
   *   </rdtype>
   *   :
   */
  if (global_qtypes != 0)
  {
    list_info_ptr_t list_info =
    {
      /* plugin instance = */ "global-qtypes",
      /* type = */ "dns_qtype"
    };

    bind_parse_generic_name_value (/* xpath = */ "server/queries-in/rdtype",
        /* callback = */ bind_xml_list_callback,
        /* user_data = */ &list_info,
        doc, xpathCtx, current_time, DS_TYPE_COUNTER);
  }
  
  /* XPath:     server/nsstats, server/nsstat
   * Variables: Requestv4, Requestv6, ReqEdns0, ReqBadEDNSVer, ReqTSIG,
   *            ReqSIG0, ReqBadSIG, ReqTCP, AuthQryRej, RecQryRej, XfrRej,
   *            UpdateRej, Response, TruncatedResp, RespEDNS0, RespTSIG,
   *            RespSIG0, QrySuccess, QryAuthAns, QryNoauthAns, QryReferral,
   *            QryNxrrset, QrySERVFAIL, QryFORMERR, QryNXDOMAIN, QryRecursion,
   *            QryDuplicate, QryDropped, QryFailure, XfrReqDone, UpdateReqFwd,
   *            UpdateRespFwd, UpdateFwdFail, UpdateDone, UpdateFail,
   *            UpdateBadPrereq
   * Layout v1:
   *   <nsstats>
   *     <Requestv4>1</Requestv4>
   *     <Requestv6>0</Requestv6>
   *     :
   *   </nsstats>
   * Layout v2:
   *   <nsstat>
   *     <name>Requestv4</name>
   *     <counter>1</counter>
   *   </nsstat>
   *   <nsstat>
   *     <name>Requestv6</name>
   *     <counter>0</counter>
   *   </nsstat>
   *   :
   */
  if (global_server_stats)
  {
    translation_table_ptr_t table_ptr =
    { 
      nsstats_translation_table,
      nsstats_translation_table_length,
      /* plugin_instance = */ "global-server_stats"
    };

    if (version == 1)
    {
      bind_parse_generic_value_list ("server/nsstats",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time, DS_TYPE_COUNTER);
    }
    else
    {
      bind_parse_generic_name_value ("server/nsstat",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time, DS_TYPE_COUNTER);
    }
  }

  /* XPath:     server/zonestats, server/zonestat
   * Variables: NotifyOutv4, NotifyOutv6, NotifyInv4, NotifyInv6, NotifyRej,
   *            SOAOutv4, SOAOutv6, AXFRReqv4, AXFRReqv6, IXFRReqv4, IXFRReqv6,
   *            XfrSuccess, XfrFail
   * Layout v1:
   *   <zonestats>
   *     <NotifyOutv4>0</NotifyOutv4>
   *     <NotifyOutv6>0</NotifyOutv6>
   *     :
   *   </zonestats>
   * Layout v2:
   *   <zonestat>
   *     <name>NotifyOutv4</name>
   *     <counter>0</counter>
   *   </zonestat>
   *   <zonestat>
   *     <name>NotifyOutv6</name>
   *     <counter>0</counter>
   *   </zonestat>
   *   :
   */
  if (global_zone_maint_stats)
  {
    translation_table_ptr_t table_ptr =
    { 
      zonestats_translation_table,
      zonestats_translation_table_length,
      /* plugin_instance = */ "global-zone_maint_stats"
    };

    if (version == 1)
    {
      bind_parse_generic_value_list ("server/zonestats",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time, DS_TYPE_COUNTER);
    }
    else
    {
      bind_parse_generic_name_value ("server/zonestat",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time, DS_TYPE_COUNTER);
    }
  }

  /* XPath:     server/resstats
   * Variables: Queryv4, Queryv6, Responsev4, Responsev6, NXDOMAIN, SERVFAIL,
   *            FORMERR, OtherError, EDNS0Fail, Mismatch, Truncated, Lame,
   *            Retry, GlueFetchv4, GlueFetchv6, GlueFetchv4Fail,
   *            GlueFetchv6Fail, ValAttempt, ValOk, ValNegOk, ValFail
   * Layout v1:
   *   <resstats>
   *     <Queryv4>0</Queryv4>
   *     <Queryv6>0</Queryv6>
   *     :
   *   </resstats>
   * Layout v2:
   *   <resstat>
   *     <name>Queryv4</name>
   *     <counter>0</counter>
   *   </resstat>
   *   <resstat>
   *     <name>Queryv6</name>
   *     <counter>0</counter>
   *   </resstat>
   *   :
   */
  if (global_resolver_stats != 0)
  {
    translation_table_ptr_t table_ptr =
    { 
      resstats_translation_table,
      resstats_translation_table_length,
      /* plugin_instance = */ "global-resolver_stats"
    };

    if (version == 1)
    {
      bind_parse_generic_value_list ("server/resstats",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time, DS_TYPE_COUNTER);
    }
    else
    {
      bind_parse_generic_name_value ("server/resstat",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time, DS_TYPE_COUNTER);
    }
  }

  /* XPath:  memory/summary
   * Variables: TotalUse, InUse, BlockSize, ContextSize, Lost
   * Layout: v2:
   *   <summary>
   *     <TotalUse>6587096</TotalUse>
   *     <InUse>1345424</InUse>
   *     <BlockSize>5505024</BlockSize>
   *     <ContextSize>3732456</ContextSize>
   *     <Lost>0</Lost>
   *   </summary>
   */
  if (global_memory_stats != 0)
  {
    translation_table_ptr_t table_ptr =
    {
      memsummary_translation_table,
      memsummary_translation_table_length,
      /* plugin_instance = */ "global-memory_stats"
    };

    bind_parse_generic_value_list ("memory/summary",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time, DS_TYPE_GAUGE);
  }

  if (views_num > 0)
    bind_xml_stats_search_views (version, doc, xpathCtx, statsnode,
        current_time);

  return 0;
} /* }}} int bind_xml_stats */

static int bind_xml (const char *data) /* {{{ */
{
  xmlDoc *doc = NULL;
  xmlXPathContext *xpathCtx = NULL;
  xmlXPathObject *xpathObj = NULL;
  int ret = -1;
  int i;

  doc = xmlParseMemory (data, strlen (data));
  if (doc == NULL)
  {
    ERROR ("bind plugin: xmlParseMemory failed.");
    return (-1);
  }

  xpathCtx = xmlXPathNewContext (doc);
  if (xpathCtx == NULL)
  {
    ERROR ("bind plugin: xmlXPathNewContext failed.");
    xmlFreeDoc (doc);
    return (-1);
  }

  xpathObj = xmlXPathEvalExpression (BAD_CAST "/isc/bind/statistics", xpathCtx);
  if (xpathObj == NULL)
  {
    ERROR ("bind plugin: Cannot find the <statistics> tag.");
    xmlXPathFreeContext (xpathCtx);
    xmlFreeDoc (doc);
    return (-1);
  }
  else if (xpathObj->nodesetval == NULL)
  {
    ERROR ("bind plugin: xmlXPathEvalExpression failed.");
    xmlXPathFreeObject (xpathObj);
    xmlXPathFreeContext (xpathCtx);
    xmlFreeDoc (doc);
    return (-1);
  }

  for (i = 0; i < xpathObj->nodesetval->nodeNr; i++)
  {
    xmlNode *node;
    char *attr_version;
    int parsed_version = 0;

    node = xpathObj->nodesetval->nodeTab[i];
    assert (node != NULL);

    attr_version = (char *) xmlGetProp (node, BAD_CAST "version");
    if (attr_version == NULL)
    {
      NOTICE ("bind plugin: Found <statistics> tag doesn't have a "
          "`version' attribute.");
      continue;
    }
    DEBUG ("bind plugin: Found: <statistics version=\"%s\">", attr_version);

    /* At the time this plugin was written, version "1.0" was used by
     * BIND 9.5.0, version "2.0" was used by BIND 9.5.1 and 9.6.0. We assume
     * that "1.*" and "2.*" don't introduce structural changes, so we just
     * check for the first two characters here. */
    if (strncmp ("1.", attr_version, strlen ("1.")) == 0)
      parsed_version = 1;
    else if (strncmp ("2.", attr_version, strlen ("2.")) == 0)
      parsed_version = 2;
    else
    {
      /* TODO: Use the complaint mechanism here. */
      NOTICE ("bind plugin: Found <statistics> tag with version `%s'. "
          "Unfortunately I have no clue how to parse that. "
          "Please open a bug report for this.", attr_version);
      xmlFree (attr_version);
      continue;
    }

    ret = bind_xml_stats (parsed_version,
        doc, xpathCtx, node);

    xmlFree (attr_version);
    /* One <statistics> node ought to be enough. */
    break;
  }

  xmlXPathFreeObject (xpathObj);
  xmlXPathFreeContext (xpathCtx);
  xmlFreeDoc (doc);

  return (ret);
} /* }}} int bind_xml */

static int bind_config_set_bool (const char *name, int *var, /* {{{ */
    oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || ( ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    WARNING ("bind plugin: The `%s' option needs "
        "exactly one boolean argument.", name);
    return (-1);
  }

  if (ci->values[0].value.boolean)
    *var = 1;
  else
    *var = 0;
  return 0;
} /* }}} int bind_config_set_bool */

static int bind_config_add_view_zone (cb_view_t *view, /* {{{ */
    oconfig_item_t *ci)
{
  char **tmp;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("bind plugin: The `Zone' option needs "
        "exactly one string argument.");
    return (-1);
  }

  tmp = (char **) realloc (view->zones,
      sizeof (char *) * (view->zones_num + 1));
  if (tmp == NULL)
  {
    ERROR ("bind plugin: realloc failed.");
    return (-1);
  }
  view->zones = tmp;

  view->zones[view->zones_num] = strdup (ci->values[0].value.string);
  if (view->zones[view->zones_num] == NULL)
  {
    ERROR ("bind plugin: strdup failed.");
    return (-1);
  }
  view->zones_num++;

  return (0);
} /* }}} int bind_config_add_view_zone */

static int bind_config_add_view (oconfig_item_t *ci) /* {{{ */
{
  cb_view_t *tmp;
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("bind plugin: `View' blocks need exactly one string argument.");
    return (-1);
  }

  tmp = (cb_view_t *) realloc (views, sizeof (*views) * (views_num + 1));
  if (tmp == NULL)
  {
    ERROR ("bind plugin: realloc failed.");
    return (-1);
  }
  views = tmp;
  tmp = views + views_num;

  memset (tmp, 0, sizeof (*tmp));
  tmp->qtypes = 1;
  tmp->resolver_stats = 1;
  tmp->cacherrsets = 1;
  tmp->zones = NULL;
  tmp->zones_num = 0;

  tmp->name = strdup (ci->values[0].value.string);
  if (tmp->name == NULL)
  {
    ERROR ("bind plugin: strdup failed.");
    free (tmp);
    return (-1);
  }

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("QTypes", child->key) == 0)
      bind_config_set_bool ("QTypes", &tmp->qtypes, child);
    else if (strcasecmp ("ResolverStats", child->key) == 0)
      bind_config_set_bool ("ResolverStats", &tmp->resolver_stats, child);
    else if (strcasecmp ("CacheRRSets", child->key) == 0)
      bind_config_set_bool ("CacheRRSets", &tmp->cacherrsets, child);
    else if (strcasecmp ("Zone", child->key) == 0)
      bind_config_add_view_zone (tmp, child);
    else
    {
      WARNING ("bind plugin: Unknown configuration option "
          "`%s' in view `%s' will be ignored.", child->key, tmp->name);
    }
  } /* for (i = 0; i < ci->children_num; i++) */

  views_num++;
  return (0);
} /* }}} int bind_config_add_view */

static int bind_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Url", child->key) == 0) {
      if ((child->values_num != 1) || (child->values[0].type != OCONFIG_TYPE_STRING))
      {
        WARNING ("bind plugin: The `Url' option needs "
                 "exactly one string argument.");
        return (-1);
      }

      url = strdup (child->values[0].value.string);
    } else if (strcasecmp ("OpCodes", child->key) == 0)
      bind_config_set_bool ("OpCodes", &global_opcodes, child);
    else if (strcasecmp ("QTypes", child->key) == 0)
      bind_config_set_bool ("QTypes", &global_qtypes, child);
    else if (strcasecmp ("ServerStats", child->key) == 0)
      bind_config_set_bool ("ServerStats", &global_server_stats, child);
    else if (strcasecmp ("ZoneMaintStats", child->key) == 0)
      bind_config_set_bool ("ZoneMaintStats", &global_zone_maint_stats, child);
    else if (strcasecmp ("ResolverStats", child->key) == 0)
      bind_config_set_bool ("ResolverStats", &global_resolver_stats, child);
    else if (strcasecmp ("MemoryStats", child->key) == 0)
      bind_config_set_bool ("MemoryStats", &global_memory_stats, child);
    else if (strcasecmp ("View", child->key) == 0)
      bind_config_add_view (child);
    else if (strcasecmp ("ParseTime", child->key) == 0)
      cf_util_get_boolean (child, &config_parse_time);
    else
    {
      WARNING ("bind plugin: Unknown configuration option "
          "`%s' will be ignored.", child->key);
    }
  }

  return (0);
} /* }}} int bind_config */

static int bind_init (void) /* {{{ */
{
  if (curl != NULL)
    return (0);

  curl = curl_easy_init ();
  if (curl == NULL)
  {
    ERROR ("bind plugin: bind_init: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt (curl, CURLOPT_NOSIGNAL, 1);
  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, bind_curl_callback);
  curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
  curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, bind_curl_error);
  curl_easy_setopt (curl, CURLOPT_URL, (url != NULL) ? url : BIND_DEFAULT_URL);
  curl_easy_setopt (curl, CURLOPT_FOLLOWLOCATION, 1);

  return (0);
} /* }}} int bind_init */

static int bind_read (void) /* {{{ */
{
  int status;

  if (curl == NULL)
  {
    ERROR ("bind plugin: I don't have a CURL object.");
    return (-1);
  }

  bind_buffer_fill = 0;
  if (curl_easy_perform (curl) != 0)
  {
    ERROR ("bind plugin: curl_easy_perform failed: %s",
        bind_curl_error);
    return (-1);
  }

  status = bind_xml (bind_buffer);
  if (status != 0)
    return (-1);
  else
    return (0);
} /* }}} int bind_read */

static int bind_shutdown (void) /* {{{ */
{
  if (curl != NULL)
  {
    curl_easy_cleanup (curl);
    curl = NULL;
  }

  return (0);
} /* }}} int bind_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("bind", bind_config);
  plugin_register_init ("bind", bind_init);
  plugin_register_read ("bind", bind_read);
  plugin_register_shutdown ("bind", bind_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 et fdm=marker : */
