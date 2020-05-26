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

/**
 * Offset at which IPv6 values start in:
 * - config_keys
 * - config_vals
 * - value_keys
 * - value_vals
 **/
static const int v6_config_offset = 29;

static const char *config_keys[] = {
    "ip4receive",      "ip4badsum",      "ip4tooshort",     "ip4toosmall",
    "ip4badhlen",      "ip4badlen",      "ip4fragment",     "ip4fragdrop",
    "ip4fragtimeout",  "ip4forward",     "ip4fastforward",  "ip4cantforward",
    "ip4redirectsent", "ip4noproto",     "ip4deliver",      "ip4transmit",
    "ip4odrop",        "ip4reassemble",  "ip4fragmented",   "ip4ofragment",
    "ip4cantfrag",     "ip4badoptions",  "ip4noroute",      "ip4badvers",
    "ip4rawout",       "ip4toolong",     "ip4notmember",    "ip4nogif",
    "ip4badaddr",      "ip6receive",     "ip6tooshort",     "ip6toosmall",
    "ip6fragment",     "ip6fragdrop",    "ip6fragtimeout",  "ip6fragoverflow",
    "ip6forward",      "ip6cantforward", "ip6redirectsent", "ip6deliver",
    "ip6transmit",     "ip6odrop",       "ip6reassemble",   "ip6fragmented",
    "ip6ofragment",    "ip6cantfrag",    "ip6badoptions",   "ip6noroute",
    "ip6badvers",      "ip6rawout",      "ip6badscope",     "ip6notmember",
    "ip6nogif",        "ip6toomanyhdr"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

static bool config_vals[] = {
    true,  false, false, false, false, false, false, false, false, true,  false,
    false, false, false, false, true,  false, false, false, false, false, false,
    false, false, false, false, false, false, false, true,  false, false, false,
    false, false, false, true,  false, false, false, true,  false, false, false,
    false, false, false, false, false, false, false, false, false, false};
static int config_vals_num = STATIC_ARRAY_SIZE(config_vals);

static const char *value_keys[] = {
    "receive",      "badsum",      "tooshort",     "toosmall",
    "badhlen",      "badlen",      "fragment",     "fragdrop",
    "fragtimeout",  "forward",     "fastforward",  "cantforward",
    "redirectsent", "noproto",     "deliver",      "transmit",
    "odrop",        "reassemble",  "fragmented",   "ofragment",
    "cantfrag",     "badoptions",  "noroute",      "badvers",
    "rawout",       "toolong",     "notmember",    "nogif",
    "badaddr",      "receive",     "tooshort",     "toosmall",
    "fragment",     "fragdrop",    "fragtimeout",  "fragoverflow",
    "forward",      "cantforward", "redirectsent", "deliver",
    "transmit",     "odrop",       "reassemble",   "fragmented",
    "ofragment",    "cantfrag",    "badoptions",   "noroute",
    "badvers",      "rawout",      "badscope",     "notmember",
    "nogif",        "toomanyhdr"};
static int value_keys_num = STATIC_ARRAY_SIZE(value_keys);

static uint64_t value_vals[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int value_vals_num = STATIC_ARRAY_SIZE(value_vals);

static int ipstats_init(void) {
  if (config_keys_num != config_vals_num) {
    ERROR("config_keys must be same size as config_vals");
    return -1;
  }

  if (value_keys_num != config_keys_num) {
    ERROR("value_keys must be same size as config_keys");
    return -1;
  }

  if (value_keys_num != value_vals_num) {
    ERROR("value_keys must be same size as value_vals");
    return -1;
  }

  return 0;
}

static int ipstats_config(char const *key, char const *value) {
  for (int i = 0; i < config_keys_num; i++)
    if (strcasecmp(key, config_keys[i]) == 0) {
      config_vals[i] = true;
      return 0;
    }

  WARNING("ipstats plugin: invalid config key: %s", key);
  return -1;
} /* int ipstats_config */

#if KERNEL_FREEBSD
static void ipstats_submit(const struct ipstat *ipstat_p,
                           const struct ip6stat *ip6stat_p) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values_len = 1;

  int i = 0;
  value_vals[i++] = ipstat_p->ips_total;
  value_vals[i++] = ipstat_p->ips_badsum;
  value_vals[i++] = ipstat_p->ips_tooshort;
  value_vals[i++] = ipstat_p->ips_toosmall;
  value_vals[i++] = ipstat_p->ips_badhlen;
  value_vals[i++] = ipstat_p->ips_badlen;
  value_vals[i++] = ipstat_p->ips_fragments;
  value_vals[i++] = ipstat_p->ips_fragdropped;
  value_vals[i++] = ipstat_p->ips_fragtimeout;
  value_vals[i++] = ipstat_p->ips_forward;
  value_vals[i++] = ipstat_p->ips_fastforward;
  value_vals[i++] = ipstat_p->ips_cantforward;
  value_vals[i++] = ipstat_p->ips_redirectsent;
  value_vals[i++] = ipstat_p->ips_noproto;
  value_vals[i++] = ipstat_p->ips_delivered;
  value_vals[i++] = ipstat_p->ips_localout;
  value_vals[i++] = ipstat_p->ips_odropped;
  value_vals[i++] = ipstat_p->ips_reassembled;
  value_vals[i++] = ipstat_p->ips_fragmented;
  value_vals[i++] = ipstat_p->ips_ofragments;
  value_vals[i++] = ipstat_p->ips_cantfrag;
  value_vals[i++] = ipstat_p->ips_badoptions;
  value_vals[i++] = ipstat_p->ips_noroute;
  value_vals[i++] = ipstat_p->ips_badvers;
  value_vals[i++] = ipstat_p->ips_rawout;
  value_vals[i++] = ipstat_p->ips_toolong;
  value_vals[i++] = ipstat_p->ips_notmember;
  value_vals[i++] = ipstat_p->ips_nogif;
  value_vals[i++] = ipstat_p->ips_badaddr;

  value_vals[i++] = ip6stat_p->ip6s_total;
  value_vals[i++] = ip6stat_p->ip6s_tooshort;
  value_vals[i++] = ip6stat_p->ip6s_toosmall;
  value_vals[i++] = ip6stat_p->ip6s_fragments;
  value_vals[i++] = ip6stat_p->ip6s_fragdropped;
  value_vals[i++] = ip6stat_p->ip6s_fragtimeout;
  value_vals[i++] = ip6stat_p->ip6s_fragoverflow;
  value_vals[i++] = ip6stat_p->ip6s_forward;
  value_vals[i++] = ip6stat_p->ip6s_cantforward;
  value_vals[i++] = ip6stat_p->ip6s_redirectsent;
  value_vals[i++] = ip6stat_p->ip6s_delivered;
  value_vals[i++] = ip6stat_p->ip6s_localout;
  value_vals[i++] = ip6stat_p->ip6s_odropped;
  value_vals[i++] = ip6stat_p->ip6s_reassembled;
  value_vals[i++] = ip6stat_p->ip6s_fragmented;
  value_vals[i++] = ip6stat_p->ip6s_ofragments;
  value_vals[i++] = ip6stat_p->ip6s_cantfrag;
  value_vals[i++] = ip6stat_p->ip6s_badoptions;
  value_vals[i++] = ip6stat_p->ip6s_noroute;
  value_vals[i++] = ip6stat_p->ip6s_badvers;
  value_vals[i++] = ip6stat_p->ip6s_rawout;
  value_vals[i++] = ip6stat_p->ip6s_badscope;
  value_vals[i++] = ip6stat_p->ip6s_notmember;
  value_vals[i++] = ip6stat_p->ip6s_nogif;
  value_vals[i++] = ip6stat_p->ip6s_toomanyhdr;

  sstrncpy(vl.plugin, "ipstats", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, "ipv4", sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "packets", sizeof(vl.type));

  for (int i = 0; i < config_vals_num; i++) {
    if (i == v6_config_offset)
      sstrncpy(vl.plugin_instance, "ipv6", sizeof(vl.plugin_instance));

    if (config_vals[i] == true) {
      sstrncpy(vl.type_instance, value_keys[i], sizeof(vl.type_instance));
      vl.values = &(value_t){.derive = value_vals[i]};
      plugin_dispatch_values(&vl);
    }
  }
} /* void ipstats_submit */
#endif

static int ipstats_read(void) {
#if KERNEL_FREEBSD
  struct ipstat ipstat;
  size_t ipslen = sizeof(ipstat);
  char mib[] = "net.inet.ip.stats";

  if (sysctlbyname(mib, &ipstat, &ipslen, NULL, 0) != 0) {
    WARNING("ipstats plugin: sysctl \"%s\" failed.", mib);
    return -1;
  }

  struct ip6stat ip6stat;
  size_t ip6slen = sizeof(ip6stat);
  char mib6[] = "net.inet6.ip6.stats";

  if (sysctlbyname(mib6, &ip6stat, &ip6slen, NULL, 0) != 0) {
    WARNING("ipstats plugin: sysctl \"%s\" failed.", mib6);
    return -1;
  }

  ipstats_submit(&ipstat, &ip6stat);
#endif

  return 0;
} /* int ipstats_read */

void module_register(void) {
  plugin_register_init("ipstats", ipstats_init);
  plugin_register_read("ipstats", ipstats_read);
  plugin_register_config("ipstats", ipstats_config, config_keys,
                         config_keys_num);
}
