/**
 * collectd - src/netstat.c
 * Copyright (C) 2007,2008  Florian octo Forster
 * Copyright (C) 2012       Brett Hawn
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
 * You should have rcv a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Author:
 *   Florian octo Forster <octo at verplant.org>
 *   Brett Hawn <bhawn at llnw.com>
 **/

/**
 * Code within `HAVE_LIBKVM_NLIST' blocks is provided under the following
 * license:
 *
 * $collectd: parts of netstat.c, 2008/08/08 03:48:30 Michael Stapelberg $
 * $OpenBSD: inet.c,v 1.100 2007/06/19 05:28:30 ray Exp $
 * $NetBSD: inet.c,v 1.14 1995/10/03 21:42:37 thorpej Exp $
 *
 * Copyright (c) 1983, 1988, 1993
 *      The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products counterd from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#if !HAVE_SYSCTLBYNAME && !KERNEL_LINUX
# error "No applicable input method."
#endif

# include <sys/socketvar.h>
# include <sys/sysctl.h>

/* Some includes needed for compiling on FreeBSD */
#include <sys/time.h>
#if HAVE_SYS_TYPES_H
# include <sys/types.h>
#endif
#if HAVE_SYS_SOCKET_H
# include <sys/socket.h>
#endif
#if HAVE_NET_IF_H
# include <net/if.h>
#endif

#ifdef HAVE_SYSCTLBYNAME
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_var.h>
#include <netinet/ip_icmp.h>
#include <netinet/icmp_var.h>
#include <netinet/tcp.h>
#include <netinet/tcp_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#ifdef COLLECT_IPV6
#include <net/if_var.h>
#include <netinet/ip6.h>
#include <netinet/icmp6.h>
#include <netinet6/in6_pcb.h>
#include <netinet6/in6_var.h>
#include <netinet6/ip6_var.h>
#endif /* COLLECT_IPV6 */
#endif /* HAVE_SYSCTLBYNAME */

#ifdef KERNEL_LINUX
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "config.h"
#endif /* KERNEL_LINUX */

static const char *config_keys[] =
{
  "Proto"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);
static ignorelist_t *ignorelist = NULL;

static int netstat_config (const char *key, const char *value)
{

  if (ignorelist == NULL)
    ignorelist = ignorelist_create (/* invert = */ 1);

  if (strcasecmp (key, "Proto") == 0) {
    ignorelist_add (ignorelist, value);
    DEBUG("netstat key: %s", key);
  } else {
    return (-1);
  }

  return (0);
}

static void submit (const char *proto, const char *name, counter_t v)
{
  value_t value;
  value_list_t vl = VALUE_LIST_INIT;
  vl.values_len = 1;

  value.counter = v;
  vl.values = &value;

  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "netstat", sizeof (vl.plugin));
  sstrncpy (vl.type, name, sizeof (vl.type));
  sstrncpy (vl.plugin_instance, proto, sizeof (vl.plugin_instance));

  plugin_dispatch_values (&vl);
}

#ifdef HAVE_SYSCTLBYNAME
static int netstat_read (void)
{
  size_t len;

  struct ipstat ipstat;
  struct icmpstat icmpstat;
  struct tcpstat tcpstat;
  struct udpstat udpstat;

#define i(n,v) submit("ip", n, ipstat.v)
#define ic(n,v) submit("icmp", n, icmpstat.v)
#define t(n,v) submit("tcp", n, tcpstat.v)
#define u(n,v) submit("udp", n, udpstat.v)

#ifdef COLLECT_IPV6
  struct ip6stat ip6stat;
  struct icmp6stat icmp6stat;

#define i6(n,v) submit("ip6", n, ip6stat.v)
#define ic6(n,v) submit("icmp6", n, icmp6stat.v)

#endif /* COLLECT_IPV6 */

  if (ignorelist_match (ignorelist, "ip") == 0) {
    len = sizeof (ipstat);
    if (sysctlbyname("net.inet.ip.stats", &ipstat, &len, NULL, 0) > 0) {
      ERROR ("netstat plugin (ipstat): sysctlbyname failed.");
    } else {
      i("ip_packets", ips_total);
      i("ip_cksum_error", ips_badsum);
    }

#ifdef COLLECT_IPV6
    len = sizeof (ip6stat);
    if (sysctlbyname("net.inet6.ip6.stats", &ip6stat, &len, NULL, 0) > 0) {
      ERROR ("netstat plugin (ip6stat): sysctlbyname failed.");
    } else {
      i6("ip_packets", ip6s_total);
      i6("ip_bad_options", ip6s_badoptions);
      i6("ip_bad_version", ip6s_badvers);
      i6("ip_bad_scope", ip6s_badscope);
    }
  }
#endif /* COLLECT_IPV6 */

  if (ignorelist_match (ignorelist, "icmp") == 0) {
    len = sizeof (icmpstat);
    if (sysctlbyname("net.inet.icmp.stats", &icmpstat, &len, NULL, 0) > 0) {
      ERROR ("netstat plugin (icmp6stat): sysctlbyname failed.");
    } else {
      ic("icmp_errors", icps_error);
      ic("icmp_cksum_error", icps_checksum);
      ic("icmp_outbound_unreachable", icps_outhist[4]);
      ic("icmp_inbound_unreachable", icps_inhist[4]);
      ic("icmp_inbound_source_quench", icps_inhist[5]);
      ic("icmp_inbound_redirect", icps_inhist[6]);
      ic("icmp_inbound_time_exceeded", icps_inhist[11]);
    }

#ifdef COLLECT_IPV6
    len = sizeof (icmp6stat);
    if (sysctlbyname("net.inet6.icmp6.stats", &icmp6stat, &len, NULL, 0) > 0) {
      ERROR ("netstat plugin (icmpstat): sysctlbyname failed.");
    } else {
      ic6("icmp_errors", icp6s_error);
      ic6("icmp_cksum_error", icp6s_checksum);
      ic6("icmp_outbound_unreachable", icp6s_outhist[4]);
      ic6("icmp_inbound_unreachable", icp6s_inhist[4]);
      ic6("icmp_inbound_source_quench", icp6s_inhist[5]);
      ic6("icmp_inbound_redirect", icp6s_inhist[6]);
      ic6("icmp_inbound_time_exceeded", icp6s_inhist[11]);
    }
  }
  
#endif /* COLLECT_IPV6 */

  if (ignorelist_match (ignorelist, "tcp") == 0) {
    len = sizeof (tcpstat);
    if (sysctlbyname("net.inet.tcp.stats", &tcpstat, &len, NULL, 0) > 0) {
      ERROR ("netstat plugin (tcpstat): sysctlbyname failed.");
    } else {
      t("tcp_packets", tcps_sndpack);
      t("tcp_bytes", tcps_sndbyte);
      t("tcp_bad_rex_packets", tcps_sndrexmitbad);
      t("tcp_mtu_resend", tcps_mturesent);
      t("tcp_packetss_received", tcps_rcvtotal);
      t("tcp_received_acks", tcps_rcvackpack);
      t("tcp_received_ack_bytes", tcps_rcvackbyte);
      t("tcp_dupe_acks", tcps_rcvdupack);
      t("tcp_unsent_data_acks", tcps_rcvacktoomuch);
      t("tcp_insequence_packets", tcps_rcvpack);
      t("tcp_completely_dupe_acks", tcps_rcvduppack);
      t("tcp_OOO_packets", tcps_rcvoopack);
      t("tcp_bad_cksum", tcps_rcvbadsum);
      t("tcp_memory_discard", tcps_rcvmemdrop);
      t("tcp_conn_request", tcps_connattempt);
      t("tcp_conn_accept", tcps_accepts);
      t("tcp_bad_syn", tcps_badsyn);
      t("tcp_queue_overflow", tcps_listendrop);
      t("tcp_ignored_rsts", tcps_badrst);
      t("tcp_rex_timeout", tcps_rexmttimeo);
      t("tcp_rex_timeout_conn_drop", tcps_timeoutdrop);
      t("tcp_persit_timeout", tcps_persisttimeo);
      t("tcp_persit_timeout_drop", tcps_persistdrop);
    }
  }

  if (ignorelist_match (ignorelist, "udp") == 0) {
    len = sizeof (udpstat);
    if (sysctlbyname("net.inet.udp.stats", &udpstat, &len, NULL, 0) > 0) {
      ERROR ("netstat plugin (udpstat): sysctlbyname failed.");
    } else {
  
      u("udp_dgrams", udps_ipackets);
      u("udp_cksum_error", udps_badsum);
    }
  }

  return(0);
}

#endif /* HAVE_SYSCTLBYNAME */

#ifdef KERNEL_LINUX 
/* 
Linux's methods to get this data are .. well, they're stupid, but we've got to work with what we have
unless we want to go write/rewrite huge swaths of code. 
*/

struct entry {
	char *title;
	char *metric;
};

struct tabs {
	char *title;
	struct entry *tab;
	size_t size;
};

struct entry Iptab[] =
{
	{"InReceives", "ip_packets"},
	{"InHdrErrors", "ip_cksum_error"}
};

struct entry Iptab6[] =
{
	{"Ip6InReceives", "ip_packets"},
	{"Ip6InHdrErrors", "ip_cksum_error"}
};

struct entry Icmptab[] =
{
	{"InErrors", "icmp_errors"},
	{"OutDestUnreachs", "icmp_outbound_unreachable"},
	{"InDestUnreachs", "icmp_inbound_unreachable"},
	{"InSrcQuenchs", "icmp_inbound_source_quench"},
	{"InRedirects", "icmp_inbound_redirect"},
	{"InTimeExcds", "icmp_inbound_time_exceeded"}
};

struct entry Icmptab6[] =
{
	{"Icmp6InErrors", "icmp_errors"},
	{"Icmp6OutDestUnreachs", "icmp_outbound_unreachable"},
	{"Icmp6InDestUnreachs", "icmp_inbound_unreachable"},
	{"Icmp6InSrcQuenchs", "icmp_inbound_source_quench"},
	{"Icmp6InRedirects", "icmp_inbound_redirect"},
	{"Icmp6InTimeExcds", "icmp_inbound_time_exceeded"}
};

struct entry Tcptab[] =
{
	{"OutSegs", "tcp_packets"},
	{"RetransSegs", "tcp_bad_rex_packets"},
	{"InSegs", "tcp_packetss_received"},
	{"ActiveOpens", "tcp_conn_active_open"},
	{"PassiveOpens", "tcp_conn_passive_open"},
	{"SyncookiesFailed", "tcp_bad_syn"},
};

struct entry Udptab[] =
{
	{"InDatagrams", "udp_dgrams"},
	{"InErrors", "udp_cksum_error"},
};

struct entry Udptab6[] =
{
	{"Udp6InDatagrams", "udp_dgrams"},
	{"Udp6InErrors", "udp_cksum_error"},
};

struct tabs alltabs[] = 
{
	{"ip", Iptab, sizeof(Iptab)},
	{"tcp", Tcptab, sizeof(Tcptab)},
	{"icmp", Icmptab, sizeof(Icmptab)},
	{"udp", Udptab, sizeof(Udptab)},
	{"ip6", Iptab6, sizeof(Iptab6)},
	{"icmp6", Icmptab6, sizeof(Icmptab6)},
	{"udp6", Udptab6, sizeof(Udptab6)},
};

int cmpentries(const void *a, const void *b)
{
	return strcmp(((struct entry *) a)->title, ((struct entry *) b)->title);
}

void inittab(void)
{
    struct tabs *t;

    /* we sort at runtime because I'm lazy ;) */
    for (t = alltabs; t->title; t++)
        qsort(t->tab, t->size / sizeof(struct entry),
              sizeof(struct entry), cmpentries);
}

void parsedata(const char *type, char buf1[1024], char buf2[1024])
{

	char *np, *vp, *p, *q;
	int endflag = 0;
	struct tabs *t;
	struct entry *ent = NULL, key;
	counter_t vvp;
	
	np = strchr(buf1, ':');
	vp = strchr(buf2, ':');
	np++;
	vp++;

	for (t = alltabs; t->title; t++) {
		if (strcmp(type, t->title) == 0) {
			while(!endflag) {
				np += strspn(np, " ");
				vp += strspn(vp, " ");
		
				p = np+strcspn(np, " \t\n");
				q = vp+strcspn(vp, " \t\n");
				if (*p == '\0') {
					endflag = 1;
				}

				*p = '\0';
				if (*np != '\0') {
					key.title = np;
					ent = bsearch(&key, t->tab, t->size / sizeof(struct entry), sizeof(struct entry), cmpentries);
					if (ent) {
						vvp = (counter_t) strtoll(vp, &vp, 0);
						submit(type, ent->metric, vvp);
					} 
				}
				np = p + 1;
				vp = q + 1;
			}
		}
	}
}

static int netstat_read (void)
{

	FILE *f;
	char buf1[2048], buf2[2048];
	char buf3[2048], buf4[2048];
	char *bf1, *bf2, *sf1, *sf2;

	inittab();

	f = fopen("/proc/net/snmp", "r");
	if (!f) {
		ERROR ("Could not open /proc/net/snmp for read");
	} else {
		while (fgets(buf1, sizeof(buf1), f)) {
			if (!fgets(buf2, sizeof(buf2), f)) {
				break;
			}

			strncpy(buf3, buf1, 1024);
			strncpy(buf4, buf2, 1024);

			sf1 = strtok_r(buf1, ":", &bf1);
			sf2 = strtok_r(buf2, ":", &bf2);
			if (((!sf1) || (!sf2)) && (strcmp(sf1, sf2) != 0)) {
				ERROR ("Error while parsing /proc/net/snmp");
				return(0);
			}
			if (strcmp(sf1, "Ip") == 0 && ignorelist_match(ignorelist, "ip") == 0) {
				parsedata("ip", buf3, buf4);
			}
			if (strcmp(sf1, "Icmp") == 0 && ignorelist_match(ignorelist, "icmp") == 0) {
				parsedata("icmp", buf3, buf4);
			}
			if (strcmp(sf1, "Tcp") == 0 && ignorelist_match(ignorelist, "tcp") == 0) {
				parsedata("tcp", buf3, buf4);
			}
			if (strcmp(sf1, "Udp") == 0 && ignorelist_match(ignorelist, "udp") == 0) {
				parsedata("udp", buf3, buf4);
			}
		}
	}
	fclose(f);

#ifdef COLLECT_IPV6
	f = fopen("/proc/net/snmp6", "r");
	if (!f) {
		ERROR ("Could not open /proc/net/snmp6 for read");
	} else {
		while (fgets(buf1, sizeof(buf1), f)) {
			if (!fgets(buf2, sizeof(buf2), f)) {
				break;
			}

			strncpy(buf3, buf1, 1024);
			strncpy(buf4, buf2, 1024);

			sf1 = strtok_r(buf1, ":", &bf1);
			sf2 = strtok_r(buf2, ":", &bf2);
			if (((!sf1) || (!sf2)) && (strcmp(sf1, sf2) != 0)) {
				ERROR ("Error while parsing /proc/net/snmp");
				return(0);
			}
			if (strcmp(sf1, "Ip6") == 0 && ignorelist_match(ignorelist, "ip") == 0) {
				parsedata("ip6", buf3, buf4);
			}
			if (strcmp(sf1, "Icmp6") == 0 && ignorelist_match(ignorelist, "icmp") == 0) {
				parsedata("icmp", buf3, buf4);
			}
			if (strcmp(sf1, "Udp6") == 0 && ignorelist_match(ignorelist, "udp") == 0) {
				parsedata("udp6", buf3, buf4);
			}
		}
	}
	fclose(f);
#endif /* COLLECT_IPV6 */

	f = fopen("/proc/net/netstat", "r");
	if (!f) {
		ERROR ("Could not open /proc/net/netstat for read");
	} else {
		while (fgets(buf1, sizeof(buf1), f)) {
			if (!fgets(buf2, sizeof(buf2), f)) {
				break;
			}

			strncpy(buf3, buf1, 1024);
			strncpy(buf4, buf2, 1024);

			sf1 = strtok_r(buf1, ":", &bf1);
			sf2 = strtok_r(buf2, ":", &bf2);
			if (((!sf1) || (!sf2)) && (strcmp(sf1, sf2) != 0)) {
				ERROR ("Error while parsing /proc/net/snmp");
				return(0);
			}
			if (strcmp(sf1, "IpExt") == 0 && ignorelist_match(ignorelist, "ip") == 0) {
				parsedata("ip", buf3, buf4);
			}
			if (strcmp(sf1, "TcpExt") == 0 && ignorelist_match(ignorelist, "tcp") == 0) {
				parsedata("tcp", buf3, buf4);
			}
		}
	}
	fclose(f);

	return(0);
}
#endif /* KERNEL_LINUX */


void module_register (void)
{
  plugin_register_config ("netstat", netstat_config, config_keys, config_keys_num);
#if KERNEL_LINUX
  plugin_register_read ("netstat", netstat_read);
#elif HAVE_SYSCTLBYNAME
  plugin_register_read ("netstat", netstat_read);
#endif
} /* void module_register */
