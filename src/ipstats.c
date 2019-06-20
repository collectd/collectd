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

static void ipstats_submit(const char *family, const char *type, derive_t rx,
                           derive_t tx, derive_t fwd) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[] = {{.derive = rx}, {.derive = tx}, {.derive = fwd}};

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);

  sstrncpy(vl.plugin, "ipstats", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, family, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  plugin_dispatch_values(&vl);
} /* void ipstats_submit */

static int ipstats_read(void) {
#if KERNEL_FREEBSD
  struct ipstat ipstat;
  size_t ipslen = sizeof(ipstat);
  char mib[] = "net.inet.ip.stats";

  if (sysctlbyname(mib, &ipstat, &ipslen, NULL, 0) != 0)
    WARNING("ipstats plugin: sysctl \"%s\" failed.", mib);
  else
    ipstats_submit("ipv4", "ips_packets", ipstat.ips_total, ipstat.ips_localout,
                   ipstat.ips_forward);

  struct ip6stat ip6stat;
  size_t ip6slen = sizeof(ip6stat);
  char mib6[] = "net.inet6.ip6.stats";

  if (sysctlbyname(mib6, &ip6stat, &ip6slen, NULL, 0) != 0)
    WARNING("ipstats plugin: sysctl \"%s\" failed.", mib);
  else
    ipstats_submit("ipv6", "ips_packets", ip6stat.ip6s_total,
                   ip6stat.ip6s_localout, ip6stat.ip6s_forward);
#endif

  return 0;
} /* int ipstats_read */

void module_register(void) { plugin_register_read("ipstats", ipstats_read); }
