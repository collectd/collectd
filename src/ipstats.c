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

static const char *config_keys_v4[] = {
    "ip4receive",      "ip4badsum",     "ip4tooshort",    "ip4toosmall",
    "ip4badhlen",      "ip4badlen",     "ip4fragment",    "ip4fragdrop",
    "ip4fragtimeout",  "ip4forward",    "ip4fastforward", "ip4cantforward",
    "ip4redirectsent", "ip4noproto",    "ip4deliver",     "ip4transmit",
    "ip4odrop",        "ip4reassemble", "ip4fragmented",  "ip4ofragment",
    "ip4cantfrag",     "ip4badoptions", "ip4noroute",     "ip4badvers",
    "ip4rawout",       "ip4toolong",    "ip4notmember",   "ip4nogif",
    "ip4badaddr"};
static int config_keys_v4_num = STATIC_ARRAY_SIZE(config_keys_v4);

static bool config_vals_v4[] = {
    true,  false, false, false, false, false, false, false, false, true,
    false, false, false, false, false, true,  false, false, false, false,
    false, false, false, false, false, false, false, false, false};
static int config_vals_v4_num = STATIC_ARRAY_SIZE(config_vals_v4);

static const char *value_keys_v4[] = {
    "receive",     "badsum",      "tooshort",     "toosmall",    "badhlen",
    "badlen",      "fragment",    "fragdrop",     "fragtimeout", "forward",
    "fastforward", "cantforward", "redirectsent", "noproto",     "deliver",
    "transmit",    "odrop",       "reassemble",   "fragmented",  "ofragment",
    "cantfrag",    "badoptions",  "noroute",      "badvers",     "rawout",
    "toolong",     "notmember",   "nogif",        "badaddr"};
static int value_keys_v4_num = STATIC_ARRAY_SIZE(value_keys_v4);

static uint64_t value_vals_v4[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int value_vals_v4_num = STATIC_ARRAY_SIZE(value_vals_v4);

static const char *config_keys_v6[] = {
    "ip6receive",     "ip6tooshort",     "ip6toosmall",     "ip6fragment",
    "ip6fragdrop",    "ip6fragtimeout",  "ip6fragoverflow", "ip6forward",
    "ip6cantforward", "ip6redirectsent", "ip6deliver",      "ip6transmit",
    "ip6odrop",       "ip6reassemble",   "ip6fragmented",   "ip6ofragment",
    "ip6cantfrag",    "ip6badoptions",   "ip6noroute",      "ip6badvers",
    "ip6rawout",      "ip6badscope",     "ip6notmember",    "ip6nogif",
    "ip6toomanyhdr"};
static int config_keys_v6_num = STATIC_ARRAY_SIZE(config_keys_v6);

static bool config_vals_v6[] = {true,  false, false, false, false, false, false,
                                true,  false, false, false, true,  false, false,
                                false, false, false, false, false, false, false,
                                false, false, false, false};
static int config_vals_v6_num = STATIC_ARRAY_SIZE(config_vals_v6);

static const char *value_keys_v6[] = {
    "receive",     "tooshort",     "toosmall",   "fragment",    "fragdrop",
    "fragtimeout", "fragoverflow", "forward",    "cantforward", "redirectsent",
    "deliver",     "transmit",     "odrop",      "reassemble",  "fragmented",
    "ofragment",   "cantfrag",     "badoptions", "noroute",     "badvers",
    "rawout",      "badscope",     "notmember",  "nogif",       "toomanyhdr"};
static int value_keys_v6_num = STATIC_ARRAY_SIZE(value_keys_v6);

static uint64_t value_vals_v6[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                   0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static int value_vals_v6_num = STATIC_ARRAY_SIZE(value_vals_v6);

static int ipstats_init(void) {
  /* IPv4 */
  if (config_keys_v4_num != config_vals_v4_num) {
    ERROR("config_keys_v4 must be same size as config_vals_v4");
    return (-1);
  }

  if (value_keys_v4_num != config_keys_v4_num) {
    ERROR("value_keys_v4 must be same size as config_keys_v4");
    return (-1);
  }

  if (value_keys_v4_num != value_vals_v4_num) {
    ERROR("value_keys_v4 must be same size as value_vals_v4");
    return (-1);
  }

  /* IPv6 */
  if (config_keys_v6_num != config_vals_v6_num) {
    ERROR("config_keys_v6 must be same size as config_vals_v6");
    return (-1);
  }

  if (value_keys_v6_num != config_keys_v6_num) {
    ERROR("value_keys_v6 must be same size as config_keys_v6");
    return (-1);
  }

  if (value_keys_v6_num != value_vals_v6_num) {
    ERROR("value_keys_v6 must be same size as value_vals_v6");
    return (-1);
  }

  return (0);
}

static int ipstats_config(char const *key, char const *value) {
  for (int i = 0; i < config_keys_v4_num; i++)
    if (strcasecmp(key, config_keys_v4[i]) == 0) {
      config_vals_v4[i] = true;
      return (0);
    }

  for (int i = 0; i < config_keys_v6_num; i++)
    if (strcasecmp(key, config_keys_v6[i]) == 0) {
      config_vals_v6[i] = true;
      return (0);
    }

  WARNING("ipstats plugin: invalid config key: %s", key);
  return -1;
} /* int ipstats_config */

#if KERNEL_FREEBSD
static void ipstats_submit_v4(const struct ipstat *ipstat_p) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values_len = 1;

  int i = 0;
  value_vals_v4[i++] = ipstat_p->ips_total;
  value_vals_v4[i++] = ipstat_p->ips_badsum;
  value_vals_v4[i++] = ipstat_p->ips_tooshort;
  value_vals_v4[i++] = ipstat_p->ips_toosmall;
  value_vals_v4[i++] = ipstat_p->ips_badhlen;
  value_vals_v4[i++] = ipstat_p->ips_badlen;
  value_vals_v4[i++] = ipstat_p->ips_fragments;
  value_vals_v4[i++] = ipstat_p->ips_fragdropped;
  value_vals_v4[i++] = ipstat_p->ips_fragtimeout;
  value_vals_v4[i++] = ipstat_p->ips_forward;
  value_vals_v4[i++] = ipstat_p->ips_fastforward;
  value_vals_v4[i++] = ipstat_p->ips_cantforward;
  value_vals_v4[i++] = ipstat_p->ips_redirectsent;
  value_vals_v4[i++] = ipstat_p->ips_noproto;
  value_vals_v4[i++] = ipstat_p->ips_delivered;
  value_vals_v4[i++] = ipstat_p->ips_localout;
  value_vals_v4[i++] = ipstat_p->ips_odropped;
  value_vals_v4[i++] = ipstat_p->ips_reassembled;
  value_vals_v4[i++] = ipstat_p->ips_fragmented;
  value_vals_v4[i++] = ipstat_p->ips_ofragments;
  value_vals_v4[i++] = ipstat_p->ips_cantfrag;
  value_vals_v4[i++] = ipstat_p->ips_badoptions;
  value_vals_v4[i++] = ipstat_p->ips_noroute;
  value_vals_v4[i++] = ipstat_p->ips_badvers;
  value_vals_v4[i++] = ipstat_p->ips_rawout;
  value_vals_v4[i++] = ipstat_p->ips_toolong;
  value_vals_v4[i++] = ipstat_p->ips_notmember;
  value_vals_v4[i++] = ipstat_p->ips_nogif;
  value_vals_v4[i++] = ipstat_p->ips_badaddr;

  sstrncpy(vl.plugin, "ipstats", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, "ipv4", sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "packets", sizeof(vl.type));

  for (int i = 0; i < config_vals_v4_num; i++)
    if (config_vals_v4[i] == true) {
      sstrncpy(vl.type_instance, value_keys_v4[i], sizeof(vl.type_instance));
      vl.values = &(value_t){.derive = value_vals_v4[i]};
      plugin_dispatch_values(&vl);
    }
} /* void ipstats_submit_v4 */

static void ipstats_submit_v6(const struct ip6stat *ip6stat_p) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values_len = 1;

  int i = 0;
  value_vals_v6[i++] = ip6stat_p->ip6s_total;
  value_vals_v6[i++] = ip6stat_p->ip6s_tooshort;
  value_vals_v6[i++] = ip6stat_p->ip6s_toosmall;
  value_vals_v6[i++] = ip6stat_p->ip6s_fragments;
  value_vals_v6[i++] = ip6stat_p->ip6s_fragdropped;
  value_vals_v6[i++] = ip6stat_p->ip6s_fragtimeout;
  value_vals_v6[i++] = ip6stat_p->ip6s_fragoverflow;
  value_vals_v6[i++] = ip6stat_p->ip6s_forward;
  value_vals_v6[i++] = ip6stat_p->ip6s_cantforward;
  value_vals_v6[i++] = ip6stat_p->ip6s_redirectsent;
  value_vals_v6[i++] = ip6stat_p->ip6s_delivered;
  value_vals_v6[i++] = ip6stat_p->ip6s_localout;
  value_vals_v6[i++] = ip6stat_p->ip6s_odropped;
  value_vals_v6[i++] = ip6stat_p->ip6s_reassembled;
  value_vals_v6[i++] = ip6stat_p->ip6s_fragmented;
  value_vals_v6[i++] = ip6stat_p->ip6s_ofragments;
  value_vals_v6[i++] = ip6stat_p->ip6s_cantfrag;
  value_vals_v6[i++] = ip6stat_p->ip6s_badoptions;
  value_vals_v6[i++] = ip6stat_p->ip6s_noroute;
  value_vals_v6[i++] = ip6stat_p->ip6s_badvers;
  value_vals_v6[i++] = ip6stat_p->ip6s_rawout;
  value_vals_v6[i++] = ip6stat_p->ip6s_badscope;
  value_vals_v6[i++] = ip6stat_p->ip6s_notmember;
  value_vals_v6[i++] = ip6stat_p->ip6s_nogif;
  value_vals_v6[i++] = ip6stat_p->ip6s_toomanyhdr;

  sstrncpy(vl.plugin, "ipstats", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, "ipv6", sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "packets", sizeof(vl.type));

  for (int i = 0; i < config_vals_v6_num; i++)
    if (config_vals_v6[i] == true) {
      sstrncpy(vl.type_instance, value_keys_v6[i], sizeof(vl.type_instance));
      vl.values = &(value_t){.derive = value_vals_v6[i]};
      plugin_dispatch_values(&vl);
    }
} /* void ipstats_submit_v6 */
#endif

static int ipstats_read(void) {
#if KERNEL_FREEBSD
  /* IPv4 */
  struct ipstat ipstat;
  size_t ipslen = sizeof(ipstat);
  char mib[] = "net.inet.ip.stats";

  if (sysctlbyname(mib, &ipstat, &ipslen, NULL, 0) != 0)
    WARNING("ipstats plugin: sysctl \"%s\" failed.", mib);
  else
    ipstats_submit_v4(&ipstat);

  /* IPv6 */
  struct ip6stat ip6stat;
  size_t ip6slen = sizeof(ip6stat);
  char mib6[] = "net.inet6.ip6.stats";

  if (sysctlbyname(mib6, &ip6stat, &ip6slen, NULL, 0) != 0)
    WARNING("ipstats plugin: sysctl \"%s\" failed.", mib6);
  else
    ipstats_submit_v6(&ip6stat);
#endif

  return 0;
} /* int ipstats_read */

void module_register(void) {
  plugin_register_init("ipstats", ipstats_init);
  plugin_register_read("ipstats", ipstats_read);
  plugin_register_config("ipstats", ipstats_config, config_keys_v4,
                         config_keys_v4_num);
}
