/**
 * collectd - src/netstat-udp.c
 * Copyright (C) 2015  Håvard Eidnes
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
 *   Håvard Eidnes <he at NetBSD.org>
 **/

#include "collectd.h"
#include "utils/common/common.h"
#include "plugin.h"

#if !defined(KERNEL_NETBSD)
# error "No applicable input method."
#endif

#include <sys/cdefs.h>
#include <sys/types.h>
#include <sys/sysctl.h>

#include <netinet/in.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet6/udp6_var.h>

static int
netstat_udp_init (void)
{
	return (0);
} /* int netstat_udp_init */

#define SUBMIT_VARS(...) \
   plugin_dispatch_multivalue (vl, 0, DS_TYPE_DERIVE, __VA_ARGS__, NULL)

static int
netstat_udp_internal (value_list_t *vl)
{
	uint64_t udpstat[UDP_NSTATS];
	uint64_t udp6stat[UDP6_NSTATS];
	size_t size;
	uint64_t delivered, delivered6;
	int err;

	size = sizeof(udpstat);
	if (sysctlbyname("net.inet.udp.stats", udpstat, &size, NULL, 0) == -1) {
		ERROR("netstat-udp plugin: could not get udp stats");
		return -1;
	}

	delivered = udpstat[UDP_STAT_IPACKETS] -
		udpstat[UDP_STAT_HDROPS] -
		udpstat[UDP_STAT_BADLEN] -
		udpstat[UDP_STAT_BADSUM] -
		udpstat[UDP_STAT_NOPORT] -
		udpstat[UDP_STAT_NOPORTBCAST] -
		udpstat[UDP_STAT_FULLSOCK];

	err = SUBMIT_VARS ("udp-received",
		     (derive_t) udpstat[UDP_STAT_IPACKETS],
		     "udp-bad-header",
		     (derive_t) udpstat[UDP_STAT_HDROPS],
		     "udp-bad-length",
		     (derive_t) udpstat[UDP_STAT_BADLEN],
		     "udp-bad-checksum",
		     (derive_t) udpstat[UDP_STAT_BADSUM],
		     "udp-no-port",
		     (derive_t) udpstat[UDP_STAT_NOPORT],
		     "udp-no-port-broadcast",
		     (derive_t) udpstat[UDP_STAT_NOPORTBCAST],
		     "udp-full-socket",
		     (derive_t) udpstat[UDP_STAT_FULLSOCK],
		     "udp-delivered",
		     (derive_t) delivered
		);
	if (err != 0) {
		ERROR("netstat-udp plugin: could not submit, err=%d\n", err);
	}

	size = sizeof(udp6stat);
	if (sysctlbyname("net.inet6.udp6.stats", udp6stat, &size,
			 NULL, 0) == -1) {
		ERROR("netstat-udp plugin: could not get udp6 stats");
		return -1;
	}

	delivered6 = udp6stat[UDP6_STAT_IPACKETS] -
		udp6stat[UDP6_STAT_HDROPS] -
		udp6stat[UDP6_STAT_BADLEN] -
		udp6stat[UDP6_STAT_BADSUM] -
		udp6stat[UDP6_STAT_NOSUM] -
		udp6stat[UDP6_STAT_NOPORT] -
		udp6stat[UDP6_STAT_NOPORTMCAST] -
		udp6stat[UDP6_STAT_FULLSOCK];

	err = SUBMIT_VARS ("udp6-received",
		     (derive_t) udp6stat[UDP6_STAT_IPACKETS],
		     "udp6-bad-header",
		     (derive_t) udp6stat[UDP6_STAT_HDROPS],
		     "udp6-bad-length",
		     (derive_t) udp6stat[UDP6_STAT_BADLEN],
		     "udp6-bad-checksum",
		     (derive_t) udp6stat[UDP6_STAT_BADSUM],
		     "udp6-no-checksum",
		     (derive_t) udp6stat[UDP6_STAT_NOSUM],
		     "udp6-no-port",
		     (derive_t) udp6stat[UDP6_STAT_NOPORT],
		     "udp6-no-port-multicast",
		     (derive_t) udp6stat[UDP6_STAT_NOPORTMCAST],
		     "udp6-full-socket",
		     (derive_t) udp6stat[UDP6_STAT_FULLSOCK],
		     "udp6-delivered",
		     (derive_t) delivered6
		);
	if (err != 0) {
		ERROR("netstat-udp plugin ipv6: could not submit, err=%d\n",
		      err);
	}

	return (0);
} /* }}} int netstat_udp_internal */

static int
netstat_udp_read (void) /* {{{ */
{
	value_t v[1];
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = v;
	vl.values_len = STATIC_ARRAY_SIZE (v);
	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "netstat_udp", sizeof (vl.plugin));
	sstrncpy (vl.type, "packets", sizeof (vl.type));
	vl.time = cdtime ();

	return (netstat_udp_internal (&vl));
} /* }}} int netstat_udp_read */

void
module_register (void)
{
	plugin_register_init ("netstat_udp", netstat_udp_init);
	plugin_register_read ("netstat_udp", netstat_udp_read);
} /* void module_register */
