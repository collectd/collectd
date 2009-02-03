/**
 * collectd - src/bind.c
 * Copyright (C) 2009  Bruno Prémont
 * Copyright (C) 2009  Florian Forster
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
 *   Florian Forster <octo at verplant.org>
 **/

/* Set to C99 and POSIX code */
#ifndef _ISOC99_SOURCE
# define _ISOC99_SOURCE
#endif
#ifndef _POSIX_SOURCE
# define _POSIX_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
# define _POSIX_C_SOURCE 200112L
#endif
#ifndef _REENTRANT
# define _REENTRANT
#endif
#ifndef _XOPEN_SOURCE
# define _XOPEN_SOURCE 600
#endif
#ifndef _BSD_SOURCE
# define _BSD_SOURCE
#endif

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

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
typedef int (*list_callback_t) (const char *name, counter_t value,
    time_t current_time, void *user_data);

struct translation_info_s
{
  const char *xml_name;
  const char *type;
  const char *type_instance;
  const int  *config_variable;
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

static char *url               = NULL;
static int   use_requests      = 1;
static int   use_rejects       = 1;
static int   use_responses     = 1;
static int   use_queries       = 1;
static int   use_rcode         = 1;
static int   use_zonestats     = 1;
static int   use_opcode        = 1;
static int   use_resolver      = 1;
static int   use_dnssec        = 1;

static int   use_rrqueries_in  = 1;
static int   use_query_results = 1;
static int   use_updates       = 1;
static int   use_zone_maint    = 1;

static CURL *curl = NULL;

static char  *bind_buffer = NULL;
static size_t bind_buffer_size = 0;
static size_t bind_buffer_fill = 0;
static char   bind_curl_error[CURL_ERROR_SIZE];

static const char *config_keys[] =
{
  "URL",
  "Requests",
  "Rejects",
  "Responses",
  "Queries",
  "RCode",
  "ZoneStats",
  "OpCodes",
  "Resolver",
  "DNSSEC",

  "RRQueriesIn",
  "QueryResults",
  "Updates",
  "ZoneMaintenance"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

/* Translation table for the `nsstats' values. */
static const translation_info_t nsstats_translation_table[] =
{
  /* Requests */
  { "Requestv4",       "dns_request",  "IPv4",        &use_requests },
  { "Requestv6",       "dns_request",  "IPv6",        &use_requests },
  { "ReqEdns0",        "dns_request",  "EDNS0",       &use_requests },
  { "ReqBadEDNSVer",   "dns_request",  "BadEDNSVer",  &use_requests },
  { "ReqTSIG",         "dns_request",  "TSIG",        &use_requests },
  { "ReqSIG0",         "dns_request",  "SIG0",        &use_requests },
  { "ReqBadSIG",       "dns_request",  "BadSIG",      &use_requests },
  { "ReqTCP",          "dns_request",  "TCP",         &use_requests },
  /* Rejects */
  { "AuthQryRej",      "dns_reject",   "authorative", &use_rejects },
  { "RecQryRej",       "dns_reject",   "recursive",   &use_rejects },
  { "XfrRej",          "dns_reject",   "transer",     &use_rejects },
  { "UpdateRej",       "dns_reject",   "update",      &use_rejects },
  /* Responses */
  { "Response",        "dns_response", "normal",      &use_responses },
  { "TruncatedResp",   "dns_response", "truncated",   &use_responses },
  { "RespEDNS0",       "dns_response", "EDNS0",       &use_responses },
  { "RespTSIG",        "dns_response", "TSIG",        &use_responses },
  { "RespSIG0",        "dns_response", "SIG0",        &use_responses },
  /* Queries */
  { "QryAuthAns",      "dns_query",    "authorative", &use_queries },
  { "QryNoauthAns",    "dns_query",    "nonauth",     &use_queries },
  { "QryReferral",     "dns_query",    "referral",    &use_queries },
  { "QryRecursion",    "dns_query",    "recursion",   &use_queries },
  { "QryDuplicate",    "dns_query",    "dupliate",    &use_queries },
  { "QryDropped",      "dns_query",    "dropped",     &use_queries },
  { "QryFailure",      "dns_query",    "failure",     &use_queries },
  /* Response codes */
  { "QrySuccess",      "dns_rcode",    "tx-NOERROR",  &use_rcode },
  { "QryNxrrset",      "dns_rcode",    "tx-NXRRSET",  &use_rcode },
  { "QrySERVFAIL",     "dns_rcode",    "tx-SERVFAIL", &use_rcode },
  { "QryFORMERR",      "dns_rcode",    "tx-FORMERR",  &use_rcode },
  { "QryNXDOMAIN",     "dns_rcode",    "tx-NXDOMAIN", &use_rcode }
#if 0
  { "XfrReqDone",      "type", "type_instance", &use_something },
  { "UpdateReqFwd",    "type", "type_instance", &use_something },
  { "UpdateRespFwd",   "type", "type_instance", &use_something },
  { "UpdateFwdFail",   "type", "type_instance", &use_something },
  { "UpdateDone",      "type", "type_instance", &use_something },
  { "UpdateFail",      "type", "type_instance", &use_something },
  { "UpdateBadPrereq", "type", "type_instance", &use_something },
#endif
};
static int nsstats_translation_table_length =
  STATIC_ARRAY_SIZE (nsstats_translation_table);
#define PARSE_NSSTATS (use_requests || use_rejects || use_responses \
    || use_queries || use_rcode)

/* Translation table for the `zonestats' values. */
static const translation_info_t zonestats_translation_table[] =
{
  /* Notify's */
  { "NotifyOutv4",     "dns_notify",   "tx-IPv4",     &use_zonestats },
  { "NotifyOutv6",     "dns_notify",   "tx-IPv6",     &use_zonestats },
  { "NotifyInv4",      "dns_notify",   "rx-IPv4",     &use_zonestats },
  { "NotifyInv6",      "dns_notify",   "rx-IPv6",     &use_zonestats },
  { "NotifyRej",       "dns_notify",   "rejected",    &use_zonestats },
  /* SOA/AXFS/IXFS requests */
  { "SOAOutv4",        "dns_opcode",   "SOA-IPv4",    &use_opcode },
  { "SOAOutv6",        "dns_opcode",   "SOA-IPv4",    &use_opcode },
  { "AXFRReqv4",       "dns_opcode",   "AXFR-IPv4",   &use_opcode },
  { "AXFRReqv6",       "dns_opcode",   "AXFR-IPv6",   &use_opcode },
  { "IXFRReqv4",       "dns_opcode",   "IXFR-IPv4",   &use_opcode },
  { "IXFRReqv6",       "dns_opcode",   "IXFR-IPv6",   &use_opcode },
  /* Domain transfers */
  { "XfrSuccess",      "dns_transfer", "success",     &use_zonestats },
  { "XfrFail",         "dns_transfer", "failure",     &use_zonestats }
};
static int zonestats_translation_table_length =
  STATIC_ARRAY_SIZE (zonestats_translation_table);
#define PARSE_ZONESTATS (use_zonestats || use_opcode)

/* Translation table for the `resstats' values. */
static const translation_info_t resstats_translation_table[] =
{
  /* Generic resolver information */
  { "Queryv4",         "dns_query",    "IPv4",           &use_resolver },
  { "Queryv6",         "dns_query",    "IPv6",           &use_resolver },
  { "Responsev4",      "dns_response", "IPv4",           &use_resolver },
  { "Responsev6",      "dns_response", "IPv6",           &use_resolver },
  /* Received response codes */
  { "NXDOMAIN",        "dns_rcode",    "rx-NXDOMAIN",    &use_rcode },
  { "SERVFAIL",        "dns_rcode",    "rx-SERVFAIL",    &use_rcode },
  { "FORMERR",         "dns_rcode",    "rx-FORMERR",     &use_rcode },
  { "OtherError",      "dns_rcode",    "rx-OTHER",       &use_rcode },
  { "EDNS0Fail",       "dns_rcode",    "rx-EDNS0Fail",   &use_rcode },
  /* Received responses */
  { "Mismatch",        "dns_response", "mismatch",       &use_responses },
  { "Truncated",       "dns_response", "truncated",      &use_responses },
  { "Lame",            "dns_response", "lame",           &use_responses },
  { "Retry",           "dns_query",    "retry",          &use_responses },
#if 0
  { "GlueFetchv4",     "type", "type_instance", &use_something },
  { "GlueFetchv6",     "type", "type_instance", &use_something },
  { "GlueFetchv4Fail", "type", "type_instance", &use_something },
  { "GlueFetchv6Fail", "type", "type_instance", &use_something },
#endif
  /* DNSSEC information */
  { "ValAttempt",      "dns_resolver", "DNSSEC-attempt", &use_dnssec },
  { "ValOk",           "dns_resolver", "DNSSEC-okay",    &use_dnssec },
  { "ValNegOk",        "dns_resolver", "DNSSEC-negokay", &use_dnssec },
  { "ValFail",         "dns_resolver", "DNSSEC-fail",    &use_dnssec }
};
static int resstats_translation_table_length =
  STATIC_ARRAY_SIZE (resstats_translation_table);
#define PARSE_RESSTATS (use_resolver || use_rcode || use_responses || use_dnssec)

static void remove_special (char *buffer, size_t buffer_size)
{
  size_t i;

  for (i = 0; i < buffer_size; i++)
  {
    if (buffer[i] == 0)
      return;
    if (!isalnum ((int) buffer[i]))
      buffer[i] = '_';
  }
} /* void remove_special */

static void submit_counter(time_t ts, const char *plugin_instance, const char *type,
    const char *type_instance, counter_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].counter = value;

  vl.values = values;
  vl.values_len = 1;
  vl.time = ts;
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "bind", sizeof(vl.plugin));
  if (plugin_instance) {
    sstrncpy(vl.plugin_instance, plugin_instance,
        sizeof(vl.plugin_instance));
    remove_special (vl.plugin_instance, sizeof (vl.plugin_instance));
  }
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance) {
    sstrncpy(vl.type_instance, type_instance,
        sizeof(vl.type_instance));
    remove_special (vl.plugin_instance, sizeof (vl.plugin_instance));
  }
  plugin_dispatch_values(&vl);
} /* void submit_counter */

static size_t bind_curl_callback (void *buf, size_t size, size_t nmemb,
    void *stream)
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
} /* size_t bind_curl_callback */

/*
 * Callback, that's called with a translation table.
 * (Plugin instance is fixed, type and type instance come from lookup table.)
 */
static int bind_xml_table_callback (const char *name, counter_t value,
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

    if (*table->table[i].config_variable == 0)
      break;

    submit_counter (current_time,
        table->plugin_instance,
        table->table[i].type,
        table->table[i].type_instance,
        value);
    break;
  }

  return (0);
} /* int bind_xml_table_callback */

/*
 * Callback, that's used for lists.
 * (Plugin instance and type are fixed, xml name is used as type instance.)
 */
static int bind_xml_list_callback (const char *name, counter_t value,
    time_t current_time, void *user_data)
{
  list_info_ptr_t *list_info = (list_info_ptr_t *) user_data;

  if (list_info == NULL)
    return (-1);

  submit_counter (current_time,
      list_info->plugin_instance,
      list_info->type,
      /* type instance = */ name,
      value);

  return (0);
} /* int bind_xml_list_callback */

static int bind_xml_read_counter (xmlDoc *doc, xmlNode *node, 
    counter_t *ret_value)
{
  char *str_ptr, *end_ptr;
  long long int value;

  str_ptr = (char *) xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
  if (str_ptr == NULL)
  {
    ERROR ("bind plugin: bind_xml_read_counter: xmlNodeListGetString failed.");
    return (-1);
  }

  errno = 0;
  value = strtoll (str_ptr, &end_ptr, 10);
  xmlFree(str_ptr);
  if (str_ptr == end_ptr || errno)
  {
    if (errno && (value < 0))
      ERROR ("bind plugin: bind_xml_read_counter: strtoll failed with underflow.");
    else if (errno && (value > 0))
      ERROR ("bind plugin: bind_xml_read_counter: strtoll failed with overflow.");
    else
      ERROR ("bind plugin: bind_xml_read_counter: strtoll failed.");
    return (-1);
  }

  *ret_value = value;
  return (0);
} /* int bind_xml_read_counter */

static int bind_xml_read_timestamp (const char *xpath_expression, xmlDoc *doc,
    xmlXPathContext *xpathCtx, time_t *ret_value)
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

  *ret_value = timegm(&tm);

  xmlXPathFreeObject (xpathObj);
  return (0);
} /* int bind_xml_read_timestamp */

/* 
 * bind_parse_generic_name_value
 *
 * Reads statistics in the form:
 * <foo>
 *   <name>QUERY</name>
 *   <value>123</name>
 * </foo>
 */
static int bind_parse_generic_name_value (const char *xpath_expression,
    list_callback_t list_callback,
    void *user_data,
    xmlDoc *doc, xmlXPathContext *xpathCtx,
    time_t current_time)
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
    xmlNode *child;

    /* Iterate over all child nodes. */
    for (child = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode;
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
      counter_t value;
      int status;

      status = bind_xml_read_counter (doc, counter, &value);
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
} /* int bind_parse_generic_name_value */

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
static int bind_parse_generic_value_list (const char *xpath_expression,
    list_callback_t list_callback,
    void *user_data,
    xmlDoc *doc, xmlXPathContext *xpathCtx,
    time_t current_time)
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
      counter_t value;
      int status;

      if (child->type != XML_ELEMENT_NODE)
        continue;

      node_name = (char *) child->name;
      status = bind_xml_read_counter (doc, child, &value);
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
} /* int bind_parse_generic_value_list */

static int bind_xml_stats (int version, xmlDoc *doc,
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
  if (use_opcode)
  {
    list_info_ptr_t list_info =
    {
      /* plugin instance = */ "requests",
      /* type = */ "dns_opcode"
    };

    bind_parse_generic_name_value (/* xpath = */ "server/requests/opcode",
        /* callback = */ bind_xml_list_callback,
        /* user_data = */ &list_info,
        doc, xpathCtx, current_time);
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
  if (use_rrqueries_in)
  {
    list_info_ptr_t list_info =
    {
      /* plugin instance = */ "queries-in",
      /* type = */ "dns_qtype"
    };

    bind_parse_generic_name_value (/* xpath = */ "server/queries-in/rdtype",
        /* callback = */ bind_xml_list_callback,
        /* user_data = */ &list_info,
        doc, xpathCtx, current_time);
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
  if (PARSE_NSSTATS)
  {
    translation_table_ptr_t table_ptr =
    { 
      nsstats_translation_table,
      nsstats_translation_table_length,
      /* plugin_instance = */ "nsstats"
    };

    if (version == 1)
    {
      bind_parse_generic_value_list ("server/nsstats",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time);
    }
    else
    {
      bind_parse_generic_name_value ("server/nsstat",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time);
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
  if (PARSE_ZONESTATS)
  {
    translation_table_ptr_t table_ptr =
    { 
      zonestats_translation_table,
      zonestats_translation_table_length,
      /* plugin_instance = */ "zonestats"
    };

    if (version == 1)
    {
      bind_parse_generic_value_list ("server/zonestats",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time);
    }
    else
    {
      bind_parse_generic_name_value ("server/zonestat",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time);
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
  if (PARSE_RESSTATS)
  {
    translation_table_ptr_t table_ptr =
    { 
      resstats_translation_table,
      resstats_translation_table_length,
      /* plugin_instance = */ "resstats"
    };

    if (version == 1)
    {
      bind_parse_generic_value_list ("server/resstats",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time);
    }
    else
    {
      bind_parse_generic_name_value ("server/resstat",
          /* callback = */ bind_xml_table_callback,
          /* user_data = */ &table_ptr,
          doc, xpathCtx, current_time);
    }
  }

  return 0;
} /* int bind_xml_stats */

static int bind_xml (const char *data)
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
} /* int bind_xml */

static int config_set_str (char **var, const char *value)
{
  if (*var != NULL)
  {
    free (*var);
    *var = NULL;
  }

  if ((*var = strdup (value)) == NULL)
    return (-1);
  else
    return (0);
} /* int config_set_str */

static int config_set_bool (int *var, const char *value)
{
  if (IS_TRUE (value))
    *var = 1;
  else if (IS_FALSE (value))
    *var = 0;
  else
    return -1;
  return 0;
} /* int config_set_bool */

static int bind_config (const char *key, const char *value)
{
  if (strcasecmp (key, "URL") == 0)
    return (config_set_str (&url, value));
  else if (strcasecmp (key, "Requests") == 0)
    return (config_set_bool (&use_requests, value));
  else if (strcasecmp (key, "Rejects") == 0)
    return (config_set_bool (&use_rejects, value));
  else if (strcasecmp (key, "Responses") == 0)
    return (config_set_bool (&use_responses, value));
  else if (strcasecmp (key, "Queries") == 0)
    return (config_set_bool (&use_queries, value));
  else if (strcasecmp (key, "RCode") == 0)
    return (config_set_bool (&use_rcode, value));
  else if (strcasecmp (key, "ZoneStats") == 0)
    return (config_set_bool (&use_zonestats, value));
  else if (strcasecmp (key, "OpCodes") == 0)
    return (config_set_bool (&use_opcode, value));
  else if (strcasecmp (key, "Resolver") == 0)
    return (config_set_bool (&use_resolver, value));
  else if (strcasecmp (key, "DNSSEC") == 0)
    return (config_set_bool (&use_dnssec, value));

  else if (strcasecmp (key, "RRQueriesIn") == 0)
    return (config_set_bool (&use_rrqueries_in, value));
  else if (strcasecmp (key, "QueryResults") == 0)
    return (config_set_bool (&use_query_results, value));
  else if (strcasecmp (key, "Updates") == 0)
    return (config_set_bool (&use_updates, value));
  else if (strcasecmp (key, "ZoneMaintenance") == 0)
    return (config_set_bool (&use_zone_maint, value));

  else
    return (-1);
} /* int bind_config */

static int bind_init (void)
{
  if (curl != NULL)
    return (0);

  curl = curl_easy_init ();
  if (curl == NULL)
  {
    ERROR ("bind plugin: bind_init: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, bind_curl_callback);
  curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
  curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, bind_curl_error);
  curl_easy_setopt (curl, CURLOPT_URL, (url != NULL) ? url : BIND_DEFAULT_URL);

  return (0);
} /* int bind_init */

static int bind_read (void)
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
} /* int bind_read */

static int bind_shutdown (void)
{
  if (curl != NULL)
  {
    curl_easy_cleanup (curl);
    curl = NULL;
  }

  return (0);
} /* int bind_shutdown */

void module_register (void)
{
  plugin_register_config ("bind", bind_config, config_keys, config_keys_num);
  plugin_register_init ("bind", bind_init);
  plugin_register_read ("bind", bind_read);
  plugin_register_shutdown ("bind", bind_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 et fdm=marker : */
