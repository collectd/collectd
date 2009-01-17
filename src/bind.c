/**
 * collectd - src/bind.c
 * Copyright (C) 2009  Bruno Prémont
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
 **/

#define _ISOC99_SOURCE
#define _XOPEN_SOURCE
#define _BSD_SOURCE
#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <curl/curl.h>
#include <libxml/parser.h>
#include <libxml/xpath.h>

static char *url              = NULL;
static bool use_rrqueries_in  = 1;
static bool use_requests      = 1;
static bool use_query_results = 1;
static bool use_updates       = 1;
static bool use_zone_maint    = 1;
static bool use_resolver      = 1;
static char *srv_boot_ts      = NULL;

static CURL *curl = NULL;

static char  *bind_buffer = NULL;
static size_t bind_buffer_size = 0;
static size_t bind_buffer_fill = 0;
static char   bind_curl_error[CURL_ERROR_SIZE];

static const char *config_keys[] =
{
  "URL",
  "RRQueriesIn",
  "Requests",
  "QueryResults",
  "Updates",
  "ZoneMaintenance",
  "Resolver"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static void submit_counter(time_t ts, const char *plugin_instance, const char *type,
    const char *type_instance, counter_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;
  char *p;

  values[0].counter = value;

  vl.values = values;
  vl.values_len = 1;
  vl.time = ts == 0 ? time(NULL) : ts;
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, "bind", sizeof(vl.plugin));
  if (plugin_instance) {
    sstrncpy(vl.plugin_instance, plugin_instance,
        sizeof(vl.plugin_instance));
    for (p = vl.type_instance; *p; p++)
      if ((*p < 'a' || *p > 'z') && (*p < 'A' || *p > 'Z')  && (*p < '0' || *p > '9') && *p != '_')
        *p = '_';
  }
  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance) {
    sstrncpy(vl.type_instance, type_instance,
        sizeof(vl.type_instance));
    for (p = vl.type_instance; *p; p++)
      if ((*p < 'a' || *p > 'z') && (*p < 'A' || *p > 'Z')  && (*p < '0' || *p > '9') && *p != '_')
        *p = '_';
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

static int bind_xml_read_counter (xmlDoc *doc, xmlNode *node, 
    counter_t *ret_value)
{
  char *str_ptr, *end_ptr;
  long long int value;

  str_ptr = (char *) xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
  if (str_ptr == NULL)
  {
    ERROR ("bind plugin: bind_xml_read_int64: xmlNodeListGetString failed.");
    return (-1);
  }

  errno = 0;
  value = strtoll (str_ptr, &end_ptr, 10);
  xmlFree(str_ptr);
  if (str_ptr == end_ptr || errno)
  {
    if (errno && value == LLONG_MIN)
      ERROR ("bind plugin: bind_xml_read_int64: strtoll failed with underflow.");
    else if (errno && value == LLONG_MAX)
      ERROR ("bind plugin: bind_xml_read_int64: strtoll failed with overflow.");
    else
      ERROR ("bind plugin: bind_xml_read_int64: strtoll failed.");
    return (-1);
  }

  *ret_value = value;
  return (0);
} /* int bind_xml_read_counter */

static int bind_xml_read_timestamp (xmlDoc *doc, xmlNode *node,
    time_t *ret_value)
{
  char *str_ptr, *p;
  struct tm tm;

  str_ptr = (char *) xmlNodeListGetString (doc, node->xmlChildrenNode, 1);
  if (str_ptr == NULL)
  {
    ERROR ("bind plugin: bind_xml_read_int: xmlNodeListGetString failed.");
    return (-1);
  }

  memset(&tm, 0, sizeof(tm));
  p = strptime(str_ptr, "%Y-%m-%dT%T", &tm);
  xmlFree(str_ptr);
  if (p == NULL)
  {
    ERROR ("bind plugin: bind_xml_read_timestamp: strptime failed.");
    return (-1);
  }

  *ret_value = timegm(&tm);
  return (0);
} /* int bind_xml_read_timestamp */

/* Bind 9.5.x */
static int bind_xml_stats_v1(xmlDoc *doc, xmlXPathContext *xpathCtx, xmlNode *statsnode)
{
  xmlXPathObjectPtr xpathObj = NULL;
  time_t current_time;
  int i;

  xpathCtx->node = statsnode;

  /* server/boot-time -- detect possible counter-resets
   * Type: XML DateTime */
  if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/boot-time", xpathCtx)) == NULL) {
    ERROR("bind plugin: unable to evaluate XPath expression StatsV1-boottime");
    return -1;
  } else if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
    char *boot_tm = (char *) xmlNodeListGetString (doc, xpathObj->nodesetval->nodeTab[0]->xmlChildrenNode, 1);
    if (srv_boot_ts == NULL || strcmp(srv_boot_ts, boot_tm) != 0) {
      xmlFree(srv_boot_ts);
      srv_boot_ts = boot_tm;
      /* TODO: tell collectd that our counters got reset ... */
      DEBUG ("bind plugin: Statv1: Server boot time: %s (%d nodes)", srv_boot_ts, xpathObj->nodesetval->nodeNr);
    } else
      xmlFree(boot_tm);
  }
  xmlXPathFreeObject(xpathObj);

  /* server/current-time -- parse our time-stamp
   * Type: XML DateTime */
  if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/current-time", xpathCtx)) == NULL) {
    ERROR("bind plugin: unable to evaluate XPath expression StatsV1-currenttime");
    return -1;
  } else if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
    if (bind_xml_read_timestamp(doc, xpathObj->nodesetval->nodeTab[0], &current_time))
      current_time = time(NULL);
    else
      DEBUG ("bind plugin: Statv1: Server current time: %ld (%d nodes)", current_time, xpathObj->nodesetval->nodeNr);
  }
  xmlXPathFreeObject(xpathObj);

  /* requests/opcode -- [name] = [counter]
   * Variables: QUERY, IQUERY, NOTIFY, UPDATE, ... */
  if (use_requests) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/requests/opcode", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV1-currenttime");
      return -1;
    } else for (i = 0; xpathObj->nodesetval && i < xpathObj->nodesetval->nodeNr; i++) {
      xmlNode *name = NULL, *counter = NULL, *child;
      for (child = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode; child != NULL; child = child->next)
        if (xmlStrcmp (BAD_CAST "name", child->name) == 0)
          name = child;
        else if (xmlStrcmp (BAD_CAST "counter", child->name) == 0)
          counter = child;
      if (name && counter) {
        char *tinst   = (char *) xmlNodeListGetString (doc, name->xmlChildrenNode, 1);
        counter_t val;
        if (!bind_xml_read_counter(doc, counter, &val))
          submit_counter(current_time, NULL, "dns_opcode", tinst, val);
        xmlFree(tinst);
      }
    }
    DEBUG ("bind plugin: Statv1: Found %d entries for requests/opcode", xpathObj->nodesetval->nodeNr);
    xmlXPathFreeObject(xpathObj);
  }

  /* queries-in/rdtype -- [name] = [counter]
   * Variables: RESERVED0, A, NS, CNAME, SOA, MR, PTR, HINFO, MX, TXT, RP, X25, PX, AAAA, LOC,
   *            SRV, NAPTR, A6, DS, RRSIG, NSEC, DNSKEY, SPF, TKEY, IXFR, AXFR, ANY, ..., Others */
  if (use_rrqueries_in) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/queries-in/rdtype", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV1-currenttime");
      return -1;
    } else for (i = 0; xpathObj->nodesetval && i < xpathObj->nodesetval->nodeNr; i++) {
      xmlNode *name = NULL, *counter = NULL, *child;
      for (child = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode; child != NULL; child = child->next)
        if (xmlStrcmp (BAD_CAST "name", child->name) == 0)
          name = child;
        else if (xmlStrcmp (BAD_CAST "counter", child->name) == 0)
          counter = child;
      if (name && counter) {
        char *tinst   = (char *) xmlNodeListGetString (doc, name->xmlChildrenNode, 1);
        counter_t val;
        if (!bind_xml_read_counter(doc, counter, &val))
          submit_counter(current_time, NULL, "dns_qtype", tinst, val);
        xmlFree(tinst);
      }
    }
    DEBUG ("bind plugin: Statv1: Found %d entries for queries-in/rdtype", xpathObj->nodesetval->nodeNr);
    xmlXPathFreeObject(xpathObj);
  }

  /* nsstats -- [$name] = [$value] */
  /* Variables: Requestv4, Requestv6, ReqEdns0, ReqBadEDNSVer, ReqTSIG, ReqSIG0, ReqBadSIG, ReqTCP,
   *            AuthQryRej, RecQryRej, XfrRej, UpdateRej, Response, TruncatedResp, RespEDNS0, RespTSIG,
   *            RespSIG0, QrySuccess, QryAuthAns, QryNoauthAns, QryReferral, QryNxrrset, QrySERVFAIL,
   *            QryFORMERR, QryNXDOMAIN, QryRecursion, QryDuplicate, QryDropped, QryFailure, XfrReqDone,
   *            UpdateReqFwd, UpdateRespFwd, UpdateFwdFail, UpdateDone, UpdateFail, UpdateBadPrereq */
  if (use_updates || use_query_results || use_requests) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/nsstats", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV1-nsstats");
      return -1;
    } else if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
      xmlNode *child;
      counter_t val;
      int n = 0;
      for (child = xpathObj->nodesetval->nodeTab[0]->xmlChildrenNode; child != NULL; child = child->next, n++)
        if (child->type == XML_ELEMENT_NODE && !bind_xml_read_counter(doc, child, &val)) {
          if (use_updates && (strncmp("Update", (char *)child->name, 6) == 0 || strcmp("XfrReqDone", (char *)child->name) == 0))
            submit_counter(current_time, NULL, "dns_update", (char *)child->name, val);
          else if (use_query_results && strncmp("Qry", (char *)child->name, 3) == 0)
            submit_counter(current_time, NULL, "dns_rcode", (char *)child->name, val);
          else if (use_query_results && (strncmp("Resp", (char *)child->name, 4) == 0 ||
                strcmp("AuthQryRej", (char *)child->name) == 0 || strcmp("RecQryRej", (char *)child->name) == 0 ||
                strcmp("XfrRej", (char *)child->name) == 0 || strcmp("TruncatedResp", (char *)child->name) == 0))
            submit_counter(current_time, NULL, "dns_rcode", (char *)child->name, val);
          else if (use_requests && strncmp("Req", (char *)child->name, 3) == 0)
            submit_counter(current_time, NULL, "dns_request", (char *)child->name, val);
        }
      DEBUG ("bind plugin: Statv1: Found %d entries for %d nsstats", n, xpathObj->nodesetval->nodeNr);
    }
    xmlXPathFreeObject(xpathObj);
  }

  /* zonestats -- [$name] = [$value] */
  /* Variables: NotifyOutv4, NotifyOutv6, NotifyInv4, NotifyInv6, NotifyRej, SOAOutv4, SOAOutv6,
   *            AXFRReqv4, AXFRReqv6, IXFRReqv4, IXFRReqv6, XfrSuccess, XfrFail */
  if (use_zone_maint) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/zonestats", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV1-zonestats");
      return -1;
    } else if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
      xmlNode *child;
      counter_t val;
      int n = 0;
      for (child = xpathObj->nodesetval->nodeTab[0]->xmlChildrenNode; child != NULL; child = child->next, n++)
        if (child->type == XML_ELEMENT_NODE) {
          if (!bind_xml_read_counter(doc, child, &val))
            submit_counter(current_time, NULL, "dns_zops", (char *)child->name, val);
        }
      DEBUG ("bind plugin: Statv1: Found %d entries for %d zonestats", n, xpathObj->nodesetval->nodeNr);
    }
    xmlXPathFreeObject(xpathObj);
  }

  /* resstats -- [$name] = [$value] */
  /* Variables: Queryv4, Queryv6, Responsev4, Responsev6, NXDOMAIN, SERVFAIL, FORMERR, OtherError,
   *            EDNS0Fail, Mismatch, Truncated, Lame, Retry, GlueFetchv4, GlueFetchv6, GlueFetchv4Fail,
   *            GlueFetchv6Fail, ValAttempt, ValOk, ValNegOk, ValFail */
  if (use_resolver) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/resstats", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV1-resstats");
      return -1;
    } else if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
      xmlNode *child;
      counter_t val;
      int n = 0;
      for (child = xpathObj->nodesetval->nodeTab[0]->xmlChildrenNode; child != NULL; child = child->next, n++)
        if (child->type == XML_ELEMENT_NODE) {
          if (!bind_xml_read_counter(doc, child, &val))
            submit_counter(current_time, NULL, "dns_resolver", (char *)child->name, val);
        }
      DEBUG ("bind plugin: Statv1: Found %d entries for %d resstats", n, xpathObj->nodesetval->nodeNr);
    }
    xmlXPathFreeObject(xpathObj);
  }
  return 0;
}

/* Bind 9.6.x */
static int bind_xml_stats_v2(xmlDoc *doc, xmlXPathContext *xpathCtx, xmlNode *statsnode)
{
  xmlXPathObjectPtr xpathObj = NULL;
  time_t current_time;
  int i;

  xpathCtx->node = statsnode;

  /* server/boot-time -- detect possible counter-resets
   * Type: XML DateTime */
  if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/boot-time", xpathCtx)) == NULL) {
    ERROR("bind plugin: unable to evaluate XPath expression StatsV2-boottime");
    return -1;
  } else if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
    char *boot_tm = (char *) xmlNodeListGetString (doc, xpathObj->nodesetval->nodeTab[0]->xmlChildrenNode, 1);
    if (srv_boot_ts == NULL || strcmp(srv_boot_ts, boot_tm) != 0) {
      xmlFree(srv_boot_ts);
      srv_boot_ts = boot_tm;
      /* TODO: tell collectd that our counters got reset ... */
      DEBUG ("bind plugin: Statv2: Server boot time: %s (%d nodes)", srv_boot_ts, xpathObj->nodesetval->nodeNr);
    } else
      xmlFree(boot_tm);
  }
  xmlXPathFreeObject(xpathObj);

  /* server/current-time -- parse our time-stamp
   * Type: XML DateTime */
  if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/current-time", xpathCtx)) == NULL) {
    ERROR("bind plugin: unable to evaluate XPath expression StatsV2-currenttime");
    return -1;
  } else if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
    if (bind_xml_read_timestamp(doc, xpathObj->nodesetval->nodeTab[0], &current_time))
      current_time = time(NULL);
    else
      DEBUG ("bind plugin: Statv2: Server current time: %ld (%d nodes)", current_time, xpathObj->nodesetval->nodeNr);
  }
  xmlXPathFreeObject(xpathObj);

  /* requests/opcode -- [name] = [counter]
   * Variables: QUERY, ... */
  if (use_requests) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/requests/opcode", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV2-opcode");
      return -1;
    } else for (i = 0; xpathObj->nodesetval && i < xpathObj->nodesetval->nodeNr; i++) {
      xmlNode *name = NULL, *counter = NULL, *child;
      for (child = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode; child != NULL; child = child->next)
        if (xmlStrcmp (BAD_CAST "name", child->name) == 0)
          name = child;
        else if (xmlStrcmp (BAD_CAST "counter", child->name) == 0)
          counter = child;
      if (name && counter) {
        char *tinst   = (char *) xmlNodeListGetString (doc, name->xmlChildrenNode, 1);
        counter_t val;
        if (!bind_xml_read_counter(doc, counter, &val))
          submit_counter(current_time, NULL, "dns_opcode", tinst, val);
        xmlFree(tinst);
      }
    }
    DEBUG ("bind plugin: Statv2: Found %d entries for requests/opcode", xpathObj->nodesetval->nodeNr);
    xmlXPathFreeObject(xpathObj);
  }

  /* queries-in/rdtype -- [name] = [counter]
   * Variables: A, NS, SOA, PTR, MX, TXT, SRV, ... */
  if (use_rrqueries_in) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/queries-in/rdtype", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV2-rdtype");
      return -1;
    } else for (i = 0; xpathObj->nodesetval && i < xpathObj->nodesetval->nodeNr; i++) {
      xmlNode *name = NULL, *counter = NULL, *child;
      for (child = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode; child != NULL; child = child->next)
        if (xmlStrcmp (BAD_CAST "name", child->name) == 0)
          name = child;
        else if (xmlStrcmp (BAD_CAST "counter", child->name) == 0)
          counter = child;
      if (name && counter) {
        char *tinst   = (char *) xmlNodeListGetString (doc, name->xmlChildrenNode, 1);
        counter_t val;
        if (!bind_xml_read_counter(doc, counter, &val))
          submit_counter(current_time, NULL, "dns_qtype", tinst, val);
        xmlFree(tinst);
      }
    }
    DEBUG ("bind plugin: Statv2: Found %d entries for queries-in/rdtype", xpathObj->nodesetval->nodeNr);
    xmlXPathFreeObject(xpathObj);
  }

  /* nsstat -- [name] = [counter]
   * Variables: Requestv4, Requestv6, ReqEdns0, ReqBadEDNSVer, ReqTSIG, ReqSIG0, ReqBadSIG, ReqTCP,
   *            AuthQryRej, RecQryRej, XfrRej, UpdateRej, Response, TruncatedResp, RespEDNS0, RespTSIG,
   *            RespSIG0, QrySuccess, QryAuthAns, QryNoauthAns, QryReferral, QryNxrrset, QrySERVFAIL,
   *            QryFORMERR, QryNXDOMAIN, QryRecursion, QryDuplicate, QryDropped, QryFailure, XfrReqDone,
   *            UpdateReqFwd, UpdateRespFwd, UpdateFwdFail, UpdateDone, UpdateFail, UpdateBadPrereq, */
  if (use_updates || use_query_results || use_requests) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/nsstat", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV2-nsstat");
      return -1;
    } else for (i = 0; xpathObj->nodesetval && i < xpathObj->nodesetval->nodeNr; i++) {
      xmlNode *name = NULL, *counter = NULL, *child;
      for (child = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode; child != NULL; child = child->next)
        if (xmlStrcmp (BAD_CAST "name", child->name) == 0)
          name = child;
        else if (xmlStrcmp (BAD_CAST "counter", child->name) == 0)
          counter = child;
      if (name && counter) {
        char *tinst   = (char *) xmlNodeListGetString (doc, name->xmlChildrenNode, 1);
        counter_t val;
        if (!bind_xml_read_counter(doc, counter, &val)) {
          if (use_updates && (strncmp("Update", tinst, 6) == 0 || strcmp("XfrReqDone", tinst) == 0))
            submit_counter(current_time, NULL, "dns_update", tinst, val);
          else if (use_query_results && strncmp("Qry", tinst, 3) == 0)
            submit_counter(current_time, NULL, "dns_rcode", tinst, val);
          else if (use_query_results && (strncmp("Resp", tinst, 4) == 0 || strcmp("AuthQryRej", tinst) == 0 ||
                strcmp("RecQryRej", tinst) == 0 || strcmp("XfrRej", tinst) == 0 || strcmp("TruncatedResp", tinst) == 0))
            submit_counter(current_time, NULL, "dns_rcode", tinst, val);
          else if (use_requests && strncmp("Req", tinst, 3) == 0)
            submit_counter(current_time, NULL, "dns_request", tinst, val);
        }
        xmlFree(tinst);
      }
    }
    DEBUG ("bind plugin: Statv2: Found %d entries for nsstat", xpathObj->nodesetval->nodeNr);
    xmlXPathFreeObject(xpathObj);
  }

  /* zonestat -- [name] = [counter]
   * Variables: NotifyOutv4, NotifyOutv6, NotifyInv4, NotifyInv6, NotifyRej, SOAOutv4, SOAOutv6,
   *            AXFRReqv4, AXFRReqv6, IXFRReqv4, IXFRReqv6, XfrSuccess, XfrFail */
  if (use_zone_maint) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "server/zonestat", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV2-zonestat");
      return -1;
    } else for (i = 0; xpathObj->nodesetval && i < xpathObj->nodesetval->nodeNr; i++) {
      xmlNode *name = NULL, *counter = NULL, *child;
      for (child = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode; child != NULL; child = child->next)
        if (xmlStrcmp (BAD_CAST "name", child->name) == 0)
          name = child;
        else if (xmlStrcmp (BAD_CAST "counter", child->name) == 0)
          counter = child;
      if (name && counter) {
        char *tinst   = (char *) xmlNodeListGetString (doc, name->xmlChildrenNode, 1);
        counter_t val;
        if (!bind_xml_read_counter(doc, counter, &val))
          submit_counter(current_time, NULL, "dns_zops", tinst, val);
        xmlFree(tinst);
      }
    }
    DEBUG ("bind plugin: Statv2: Found %d entries for zonestat", xpathObj->nodesetval->nodeNr);
    xmlXPathFreeObject(xpathObj);
  }

  /* WARNING: per-view only: views/view/resstat, view-name as plugin-instance */
  /* resstat -- [name] = [counter]
   * Variables: Queryv4, Queryv6, Responsev4, Responsev6, NXDOMAIN, SERVFAIL, FORMERR, OtherError,
   *            EDNS0Fail, Mismatch, Truncated, Lame, Retry, GlueFetchv4, GlueFetchv6, GlueFetchv4Fail,
   *            GlueFetchv6Fail, ValAttempt, ValOk, ValNegOk, ValFail */
  if (use_resolver) {
    if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "views/view", xpathCtx)) == NULL) {
      ERROR("bind plugin: unable to evaluate XPath expression StatsV2-view");
      return -1;
    } else for (i = 0; xpathObj->nodesetval && i < xpathObj->nodesetval->nodeNr; i++) {
      char *zname = NULL;
      xmlNode *name = NULL, *counter = NULL, *rchild, *child;
      int n = 0;
      for (child = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode; child != NULL && zname == NULL; child = child->next)
        if (xmlStrcmp (BAD_CAST "name", child->name) == 0)
          zname = (char *) xmlNodeListGetString (doc, child->xmlChildrenNode, 1);
      if (!zname || strcmp("_bind", zname) == 0)
        continue; /* Unnamed zone?? */
      /* else TODO: allow zone filtering */
      for (rchild = xpathObj->nodesetval->nodeTab[i]->xmlChildrenNode; rchild != NULL; rchild = rchild->next, n++)
        if (xmlStrcmp (BAD_CAST "resstat", rchild->name) == 0) {
          for (child = rchild->xmlChildrenNode; child != NULL; child = child->next)
            if (xmlStrcmp (BAD_CAST "name", child->name) == 0)
              name = child;
            else if (xmlStrcmp (BAD_CAST "counter", child->name) == 0)
              counter = child;
          if (name && counter) {
            char *tinst   = (char *) xmlNodeListGetString (doc, name->xmlChildrenNode, 1);
            counter_t val;
            if (!bind_xml_read_counter(doc, counter, &val))
              submit_counter(current_time, zname, "dns_resolver", tinst, val);
            xmlFree(tinst);
          }
        }
      DEBUG ("bind plugin: Statv2: Found %d entries for view %s", n, zname);
      xmlFree(zname);
    }
    xmlXPathFreeObject(xpathObj);
  }
  return 0;
}

static int bind_xml (const char *data)
{
  xmlDoc *doc = NULL;
  xmlXPathContextPtr xpathCtx = NULL;
  xmlXPathObjectPtr xpathObj = NULL;
  int ret = -1;

  doc = xmlParseMemory (data, strlen (data));
  if (doc == NULL) {
    ERROR ("bind plugin: xmlParseMemory failed.");
    goto out;
  }

  if ((xpathCtx = xmlXPathNewContext(doc)) == NULL) {
    ERROR ("bind plugin: xmlXPathNewContext failed.");
    goto out;
  }
  /* Look for /isc/bind/statistics[@version='2.0'] */
  if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "/isc[@version='1.0']/bind/statistics[@version='1.0']", xpathCtx)) == NULL) {
    ERROR("bind plugin: unable to evaluate XPath expression StatsV1");
    goto out;
  } else if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
    /* We have Bind-9.5.x */
    ret = bind_xml_stats_v1(doc, xpathCtx, xpathObj->nodesetval->nodeTab[0]);
    goto out;
  } else {
    xmlXPathFreeObject(xpathObj);
    xpathObj = NULL;
  }
  if ((xpathObj = xmlXPathEvalExpression(BAD_CAST "/isc[@version='1.0']/bind/statistics[@version='2.0']", xpathCtx)) == NULL) {
    ERROR("bind plugin: unable to evaluate XPath expression StatsV2");
    goto out;
  } else if (xpathObj->nodesetval && xpathObj->nodesetval->nodeNr > 0) {
    /* We have Bind-9.6.x */
    ret = bind_xml_stats_v2(doc, xpathCtx, xpathObj->nodesetval->nodeTab[0]);
    goto out;
  } else {
    xmlXPathFreeObject(xpathObj);
    xpathObj = NULL;
  }
  ERROR("bind plugin: unable to find statistics in supported version.");

out:
  xmlXPathFreeObject(xpathObj);
  xmlXPathFreeContext(xpathCtx);
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

static int config_set_bool (bool *var, const char *value)
{
  if ((strcasecmp ("true", value) == 0)
      || (strcasecmp ("yes", value) == 0)
      || (strcasecmp ("on", value) == 0))
    *var = 1;
  else if ((strcasecmp ("false", value) == 0)
      || (strcasecmp ("no", value) == 0)
      || (strcasecmp ("off", value) == 0))
    *var = 0;
  else
    return -1;
  return 0;
} /* int config_set_bool */

static int bind_config (const char *key, const char *value)
{
  if (strcasecmp (key, "URL") == 0)
    return (config_set_str (&url, value));
  else if (strcasecmp (key, "RRQueriesIn") == 0)
    return (config_set_bool (&use_rrqueries_in, value));
  else if (strcasecmp (key, "Requests") == 0)
    return (config_set_bool (&use_requests, value));
  else if (strcasecmp (key, "QueryResults") == 0)
    return (config_set_bool (&use_query_results, value));
  else if (strcasecmp (key, "Updates") == 0)
    return (config_set_bool (&use_updates, value));
  else if (strcasecmp (key, "ZoneMaintenance") == 0)
    return (config_set_bool (&use_zone_maint, value));
  else if (strcasecmp (key, "Resolver") == 0)
    return (config_set_bool (&use_resolver, value));
  else
    return (-1);
} /* int bind_config */

static int bind_init (void)
{
  if (url == NULL)
  {
    WARNING ("bind plugin: bind_init: No URL configured, "
        "returning an error.");
    return (-1);
  }

  if (curl != NULL)
    curl_easy_cleanup (curl);

  if ((curl = curl_easy_init ()) == NULL)
  {
    ERROR ("bind plugin: bind_init: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt (curl, CURLOPT_WRITEFUNCTION, bind_curl_callback);
  curl_easy_setopt (curl, CURLOPT_USERAGENT, PACKAGE_NAME"/"PACKAGE_VERSION);
  curl_easy_setopt (curl, CURLOPT_ERRORBUFFER, bind_curl_error);
  curl_easy_setopt (curl, CURLOPT_URL, url);
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

  if (url == NULL)
  {
    ERROR ("bind plugin: No URL has been configured.");
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

void module_register (void)
{
  plugin_register_config ("bind", bind_config, config_keys, config_keys_num);
  plugin_register_init ("bind", bind_init);
  plugin_register_read ("bind", bind_read);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 et fdm=marker : */
