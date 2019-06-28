/**
 * collectd - src/ipstats.c
 * Copyright (C) 2019  Marco van Tol
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *   Marco van Tol <marco at tols.org>
 **/

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#if KERNEL_FREEBSD
#include <sys/sysctl.h>
#include <sys/types.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet6/ip6_var.h>
#endif

static bool ip4_total = true;          /* total packets received */
static bool ip4_badsum = false;        /* checksum bad */
static bool ip4_tooshort = false;      /* packet too short */
static bool ip4_toosmall = false;      /* not enough data */
static bool ip4_badhlen = false;       /* ip header length < data size */
static bool ip4_badlen = false;        /* ip length < ip header length */
static bool ip4_fragments = false;     /* fragments received */
static bool ip4_fragdropped = false;   /* frags dropped (dups, out of space) */
static bool ip4_fragtimeout = false;   /* fragments timed out */
static bool ip4_forward = true;        /* packets forwarded */
static bool ip4_fastforward = false;   /* packets fast forwarded */
static bool ip4_cantforward = false;   /* packets rcvd for unreachable dest */
static bool ip4_redirectsent = false;  /* packets forwarded on same net */
static bool ip4_noproto = false;       /* unknown or unsupported protocol */
static bool ip4_delivered = false;     /* datagrams delivered to upper level*/
static bool ip4_localout = true;       /* total ip packets generated here */
static bool ip4_odropped = false;      /* lost packets due to nobufs, etc. */
static bool ip4_reassembled = false;   /* total packets reassembled ok */
static bool ip4_fragmented = false;    /* datagrams successfully fragmented */
static bool ip4_ofragments = false;    /* output fragments created */
static bool ip4_cantfrag = false;      /* don't fragment flag was set, etc. */
static bool ip4_badoptions = false;    /* error in option processing */
static bool ip4_noroute = false;       /* packets discarded due to no route */
static bool ip4_badvers = false;       /* ip version != 4 */
static bool ip4_rawout = false;        /* total raw ip packets generated */
static bool ip4_toolong = false;       /* ip length > max ip packet size */
static bool ip4_notmember = false;     /* multicasts for unregistered grps */
static bool ip4_nogif = false;         /* no match gif found */
static bool ip4_badaddr = false;       /* invalid address on header */

static const char *config_keys[] = {"ip4received", "ip4badsum",
                                    "ip4tooshort", "ip4toosmall",
                                    "ip4badhlen", "ip4badlen",
                                    "ip4fragments", "ip4fragdropped",
                                    "ip4fragtimeout", "ip4forwarded",
                                    "ip4fastforward", "ip4cantforward",
                                    "ip4redirectsent", "ip4noproto",
                                    "ip4delivered", "ip4transmitted",
                                    "ip4odropped", "ip4reassembled",
                                    "ip4fragmented", "ip4ofragments",
                                    "ip4cantfrag", "ip4badoptions",
                                    "ip4noroute", "ip4badvers",
                                    "ip4rawout", "ip4toolong",
                                    "ip4notmember", "ip4nogif",
                                    "ip4badaddr"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static int ipstats_config(char const *key, char const *value)
{
  if (strcasecmp(key, "ip4received") == 0)
    ip4_total = IS_TRUE(value);
  else if (strcasecmp(key, "ip4badsum") == 0)
    ip4_badsum = IS_TRUE(value);
  else if (strcasecmp(key, "ip4tooshort") == 0)
    ip4_tooshort = IS_TRUE(value);
  else if (strcasecmp(key, "ip4toosmall") == 0)
    ip4_toosmall = IS_TRUE(value);
  else if (strcasecmp(key, "ip4badhlen") == 0)
    ip4_badhlen = IS_TRUE(value);
  else if (strcasecmp(key, "ip4badlen") == 0)
    ip4_badlen = IS_TRUE(value);
  else if (strcasecmp(key, "ip4fragments") == 0)
    ip4_fragments = IS_TRUE(value);
  else if (strcasecmp(key, "ip4fragdropped") == 0)
    ip4_fragdropped = IS_TRUE(value);
  else if (strcasecmp(key, "ip4fragtimeout") == 0)
    ip4_fragtimeout = IS_TRUE(value);
  else if (strcasecmp(key, "ip4forwarded") == 0)
    ip4_forward = IS_TRUE(value);
  else if (strcasecmp(key, "ip4fastforward") == 0)
    ip4_fastforward = IS_TRUE(value);
  else if (strcasecmp(key, "ip4cantforward") == 0)
    ip4_cantforward = IS_TRUE(value);
  else if (strcasecmp(key, "ip4redirectsent") == 0)
    ip4_redirectsent = IS_TRUE(value);
  else if (strcasecmp(key, "ip4noproto") == 0)
    ip4_noproto = IS_TRUE(value);
  else if (strcasecmp(key, "ip4delivered") == 0)
    ip4_delivered = IS_TRUE(value);
  else if (strcasecmp(key, "ip4transmitted") == 0)
    ip4_localout = IS_TRUE(value);
  else if (strcasecmp(key, "ip4odropped") == 0)
    ip4_odropped = IS_TRUE(value);
  else if (strcasecmp(key, "ip4reassembled") == 0)
    ip4_reassembled = IS_TRUE(value);
  else if (strcasecmp(key, "ip4fragmented") == 0)
    ip4_fragmented = IS_TRUE(value);
  else if (strcasecmp(key, "ip4ofragments") == 0)
    ip4_ofragments = IS_TRUE(value);
  else if (strcasecmp(key, "ip4cantfrag") == 0)
    ip4_cantfrag = IS_TRUE(value);
  else if (strcasecmp(key, "ip4badoptions") == 0)
    ip4_badoptions = IS_TRUE(value);
  else if (strcasecmp(key, "ip4noroute") == 0)
    ip4_noroute = IS_TRUE(value);
  else if (strcasecmp(key, "ip4badvers") == 0)
    ip4_badvers = IS_TRUE(value);
  else if (strcasecmp(key, "ip4rawout") == 0)
    ip4_rawout = IS_TRUE(value);
  else if (strcasecmp(key, "ip4toolong") == 0)
    ip4_toolong = IS_TRUE(value);
  else if (strcasecmp(key, "ip4notmember") == 0)
    ip4_notmember= IS_TRUE(value);
  else if (strcasecmp(key, "ip4nogif") == 0)
    ip4_nogif = IS_TRUE(value);
  else if (strcasecmp(key, "ip4badaddr") == 0)
    ip4_badaddr = IS_TRUE(value);
  else {
    WARNING("ipstats plugin: invalid config key: %s", key);
    return -1;
  }

  return 0;
} /* int ipstats_config */


#if KERNEL_FREEBSD
static void ipstats_submit_v4(const struct ipstat *ipstat_p) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values_len = 1;

  sstrncpy(vl.plugin, "ipstats", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, "ipv4", sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "packets", sizeof(vl.type));

  if (ip4_total == true) {
    sstrncpy(vl.type_instance, "received", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_total};
    plugin_dispatch_values(&vl);
  }

  if (ip4_badsum == true) {
    sstrncpy(vl.type_instance, "badsum", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_badsum};
    plugin_dispatch_values(&vl);
  }

  if (ip4_tooshort == true) {
    sstrncpy(vl.type_instance, "tooshort", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_tooshort};
    plugin_dispatch_values(&vl);
  }

  if (ip4_toosmall == true) {
    sstrncpy(vl.type_instance, "toosmall", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_toosmall};
    plugin_dispatch_values(&vl);
  }

  if (ip4_badhlen == true) {
    sstrncpy(vl.type_instance, "badhlen", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_badhlen};
    plugin_dispatch_values(&vl);
  }

  if (ip4_badlen == true) {
    sstrncpy(vl.type_instance, "badlen", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_badlen};
    plugin_dispatch_values(&vl);
  }

  if (ip4_fragments == true) {
    sstrncpy(vl.type_instance, "fragments", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_fragments};
    plugin_dispatch_values(&vl);
  }

  if (ip4_fragdropped == true) {
    sstrncpy(vl.type_instance, "fragdropped", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_fragdropped};
    plugin_dispatch_values(&vl);
  }

  if (ip4_fragtimeout == true) {
    sstrncpy(vl.type_instance, "fragtimeout", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_fragtimeout};
    plugin_dispatch_values(&vl);
  }

  if (ip4_forward == true) {
    sstrncpy(vl.type_instance, "forwarded", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_forward};
    plugin_dispatch_values(&vl);
  }

  if (ip4_fastforward == true) {
    sstrncpy(vl.type_instance, "fastforward", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_fastforward};
    plugin_dispatch_values(&vl);
  }

  if (ip4_cantforward == true) {
    sstrncpy(vl.type_instance, "cantforward", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_cantforward};
    plugin_dispatch_values(&vl);
  }

  if (ip4_redirectsent == true) {
    sstrncpy(vl.type_instance, "redirectsent", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_redirectsent};
    plugin_dispatch_values(&vl);
  }

  if (ip4_noproto == true) {
    sstrncpy(vl.type_instance, "noproto", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_noproto};
    plugin_dispatch_values(&vl);
  }

  if (ip4_delivered == true) {
    sstrncpy(vl.type_instance, "delivered", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_delivered};
    plugin_dispatch_values(&vl);
  }

  if (ip4_localout == true) {
    sstrncpy(vl.type_instance, "transmitted", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_localout};
    plugin_dispatch_values(&vl);
  }

  if (ip4_odropped == true) {
    sstrncpy(vl.type_instance, "odropped", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_odropped};
    plugin_dispatch_values(&vl);
  }

  if (ip4_reassembled == true) {
    sstrncpy(vl.type_instance, "reassembled", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_reassembled};
    plugin_dispatch_values(&vl);
  }

  if (ip4_fragmented == true) {
    sstrncpy(vl.type_instance, "fragmented", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_fragmented};
    plugin_dispatch_values(&vl);
  }

  if (ip4_ofragments == true) {
    sstrncpy(vl.type_instance, "ofragments", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_ofragments};
    plugin_dispatch_values(&vl);
  }

  if (ip4_cantfrag == true) {
    sstrncpy(vl.type_instance, "cantfrag", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_cantfrag};
    plugin_dispatch_values(&vl);
  }

  if (ip4_badoptions == true) {
    sstrncpy(vl.type_instance, "badoptions", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_badoptions};
    plugin_dispatch_values(&vl);
  }

  if (ip4_noroute == true) {
    sstrncpy(vl.type_instance, "noroute", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_noroute};
    plugin_dispatch_values(&vl);
  }

  if (ip4_badvers == true) {
    sstrncpy(vl.type_instance, "badvers", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_badvers};
    plugin_dispatch_values(&vl);
  }

  if (ip4_rawout == true) {
    sstrncpy(vl.type_instance, "rawout", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_rawout};
    plugin_dispatch_values(&vl);
  }

  if (ip4_toolong == true) {
    sstrncpy(vl.type_instance, "toolong", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_toolong};
    plugin_dispatch_values(&vl);
  }

  if (ip4_notmember == true) {
    sstrncpy(vl.type_instance, "notmember", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_notmember};
    plugin_dispatch_values(&vl);
  }

  if (ip4_nogif == true) {
    sstrncpy(vl.type_instance, "nogif", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_nogif};
    plugin_dispatch_values(&vl);
  }

  if (ip4_badaddr == true) {
    sstrncpy(vl.type_instance, "badaddr", sizeof(vl.type_instance));
    vl.values = &(value_t){.derive = ipstat_p->ips_badaddr};
    plugin_dispatch_values(&vl);
  }
} /* void ipstats_submit */
#endif

static int ipstats_read(void) {
#if KERNEL_FREEBSD
  struct ipstat ipstat;
  size_t ipslen = sizeof(ipstat);
  char mib[] = "net.inet.ip.stats";

  if (sysctlbyname(mib, &ipstat, &ipslen, NULL, 0) != 0)
    WARNING("ipstats plugin: sysctl \"%s\" failed.", mib);
  else
    ipstats_submit_v4(&ipstat);

/*
  struct ip6stat ip6stat;
  size_t ip6slen = sizeof(ip6stat);
  char mib6[] = "net.inet6.ip6.stats";

  if (sysctlbyname(mib6, &ip6stat, &ip6slen, NULL, 0) != 0)
    WARNING("ipstats plugin: sysctl \"%s\" failed.", mib);
  else
    ipstats_submit("ipv6", ip6stat.ip6s_total, ip6stat.ip6s_localout,
                   ip6stat.ip6s_forward);
*/
#endif

  return 0;
} /* int ipstats_read */

void module_register(void) {
  plugin_register_read("ipstats", ipstats_read);
  plugin_register_config("ipstats", ipstats_config, config_keys, config_keys_num);
}
