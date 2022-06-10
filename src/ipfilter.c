/**
 * collectd - src/ipfilter.c
 * Copyright (C) 2022 Edgar Fuß, Mathematisches Institut der universität Bonn
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
 * 	Edgar Fuß <ef@math.uni-bonn.de>
 **/

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

#include <stdlib.h>
#include <limits.h>
#include <unistd.h>
#include <fcntl.h>
#include <strings.h>
#include <sys/ioctl.h>

/* reordering the #include's will make it fail to compile */
/* clang-format off */
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip_fil.h>
#include <netinet/ipl.h>
#include <netinet/ip_state.h>
/* clang-format on */

static const char *config_keys[] = {"Report"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
typedef uint64_t report_mask_t;
static report_mask_t report_mask = 0;
static int ipl, ipstate;
static ips_stat_t ipsst;

static void uint_gauge(void *x, value_t *v) {
	v->gauge = *(u_int *)x;
}

static void ulong_gauge(void *x, value_t *v) {
	v->gauge = *(u_long *)x;
}

static void uint_derive(void *x, value_t *v) {
	v->derive = *(u_int *)x;
}

static void ulong_derive(void *x, value_t *v) {
	v->derive = *(u_long *)x;
}

struct report {
	char *name;	/* config and type instance name */
	void *stat;	/* pointer to field in ipsst structure */
	char *type;	/* collectd type name */
	void (*conv)(void *, value_t *);	/* conversion function */
};
/* formatting will make the table much harder to read */
/* clang-format off */
struct report report_tab[] = {
#if (IPFILTER_VERSION >= 5000000) && (IPFILTER_VERSION < 6000000) /* IPFilter 5.x */
	{ "active",		&ipsst.iss_active,		"gauge",	uint_gauge },
	/* iss_active_proto */
	{ "add_bad",		&ipsst.iss_add_bad,		"packets",	ulong_derive },
	{ "add_dup",		&ipsst.iss_add_dup,		"packets",	ulong_derive },
	{ "add_locked",		&ipsst.iss_add_locked,		"packets",	ulong_derive },
	{ "add_oow",		&ipsst.iss_add_oow,		"packets",	ulong_derive },
	{ "bucket_full",	&ipsst.iss_bucket_full,		"packets",	ulong_derive },
	{ "check_bad",		&ipsst.iss_check_bad,		"packets",	ulong_derive },
	{ "check_miss",		&ipsst.iss_check_miss,		"packets",	ulong_derive },
	{ "check_nattag",	&ipsst.iss_check_nattag,	"packets",	ulong_derive },
	{ "check_notag",	&ipsst.iss_check_notag,		"packets",	ulong_derive },
	{ "clone_nomem",	&ipsst.iss_clone_nomem,		"packets",	ulong_derive },
	{ "cloned",		&ipsst.iss_cloned,		"packets",	ulong_derive },
	{ "expire",		&ipsst.iss_expire,		"packets",	ulong_derive },
	{ "fin",		&ipsst.iss_fin,			"packets",	ulong_derive },
	{ "flush_all",		&ipsst.iss_flush_all,		"packets",	ulong_derive },
	{ "flush_closing",	&ipsst.iss_flush_closing,	"packets",	ulong_derive },
	{ "flush_queue",	&ipsst.iss_flush_queue,		"packets",	ulong_derive },
	{ "flush_state",	&ipsst.iss_flush_state,		"packets",	ulong_derive },
	{ "flush_timeout",	&ipsst.iss_flush_timeout,	"packets",	ulong_derive },
	{ "hits",		&ipsst.iss_hits,		"packets",	ulong_derive },
	{ "icmp6_icmperr",	&ipsst.iss_icmp6_icmperr,	"packets",	ulong_derive },
	{ "icmp6_miss",		&ipsst.iss_icmp6_miss,		"packets",	ulong_derive },
	{ "icmp6_notinfo",	&ipsst.iss_icmp6_notinfo,	"packets",	ulong_derive },
	{ "icmp6_notquery",	&ipsst.iss_icmp6_notquery,	"packets",	ulong_derive },
	{ "icmp_bad",		&ipsst.iss_icmp_bad,		"packets",	ulong_derive },
	{ "icmp_banned",	&ipsst.iss_icmp_banned,		"packets",	ulong_derive },
	{ "icmp_headblock",	&ipsst.iss_icmp_headblock,	"packets",	ulong_derive },
	{ "icmp_hits",		&ipsst.iss_icmp_hits,		"packets",	ulong_derive },
	{ "icmp_icmperr",	&ipsst.iss_icmp_icmperr,	"packets",	ulong_derive },
	{ "icmp_miss",		&ipsst.iss_icmp_miss,		"packets",	ulong_derive },
	{ "icmp_notquery",	&ipsst.iss_icmp_notquery,	"packets",	ulong_derive },
	{ "icmp_short",		&ipsst.iss_icmp_short,		"packets",	ulong_derive },
	{ "icmp_toomany",	&ipsst.iss_icmp_toomany,	"packets",	ulong_derive },
	{ "inuse",		&ipsst.iss_inuse,		"gauge",	uint_gauge },
	/* iss_list */
	{ "log_fail",		&ipsst.iss_log_fail,		"packets",	ulong_derive },
	{ "log_ok",		&ipsst.iss_log_ok,		"packets",	ulong_derive },
	{ "lookup_badifp",	&ipsst.iss_lookup_badifp,	"packets",	ulong_derive },
	{ "lookup_badport",	&ipsst.iss_lookup_badport,	"packets",	ulong_derive },
	{ "lookup_miss",	&ipsst.iss_lookup_miss,		"packets",	ulong_derive },
	{ "max",		&ipsst.iss_max,			"packets",	ulong_derive },
	{ "max_ref",		&ipsst.iss_max_ref,		"packets",	ulong_derive },
	{ "max_track",		&ipsst.iss_max_track,		"packets",	ulong_derive },
	{ "miss_mask",		&ipsst.iss_miss_mask,		"packets",	ulong_derive },
	{ "nomem",		&ipsst.iss_nomem,		"packets",	ulong_derive },
	{ "oow",		&ipsst.iss_oow,			"packets",	ulong_derive },
	{ "orphan",		&ipsst.iss_orphan,		"gauge",	ulong_gauge },
	{ "tcp",		&ipsst.iss_proto[IPPROTO_TCP],	"packets",	ulong_derive },
	{ "udp",		&ipsst.iss_proto[IPPROTO_UDP],	"packets",	ulong_derive },
	{ "icmp",		&ipsst.iss_proto[IPPROTO_ICMP],	"packets",	ulong_derive },
	/* rest of iss_proto */
	{ "scan_block",		&ipsst.iss_scan_block,		"packets",	ulong_derive },
	{ "state_max",		&ipsst.iss_state_max,		"packets",	ulong_derive },
	{ "state_size",		&ipsst.iss_state_size,		"packets",	ulong_derive },
	/* iss_states */
	/* iss_table */
	{ "tcp_closing",	&ipsst.iss_tcp_closing,		"packets",	ulong_derive },
	{ "tcp_oow",		&ipsst.iss_tcp_oow,		"packets",	ulong_derive },
	{ "tcp_rstadd",		&ipsst.iss_tcp_rstadd,		"packets",	ulong_derive },
	{ "tcp_toosmall",	&ipsst.iss_tcp_toosmall,	"packets",	ulong_derive },
	{ "tcp_badopt",		&ipsst.iss_tcp_badopt,		"packets",	ulong_derive },
	{ "tcp_fsm",		&ipsst.iss_tcp_fsm,		"packets",	ulong_derive },
	{ "tcp_strict",		&ipsst.iss_tcp_strict,		"packets",	ulong_derive },
	/* iss_tcptab */
	{ "ticks",		&ipsst.iss_ticks,		"derive",	uint_derive },
	{ "wild",		&ipsst.iss_wild,		"gauge",	ulong_gauge },
	/* iss_winsack */
	/* iss_bucketlen */
	{ NULL,			NULL,				NULL,		NULL }
#elif (IPFILTER_VERSION >= 4000000) && (IPFILTER_VERSION < 5000000) /* IPFilter 4.x */
	{ "hits",		&ipsst.iss_hits,		"packets",	ulong_derive },
	{ "check_miss",		&ipsst.iss_miss,		"packets",	ulong_derive },
	{ "max",		&ipsst.iss_max,			"packets",	ulong_derive },
	{ "max_ref",		&ipsst.iss_maxref,		"packets",	ulong_derive },
	{ "tcp",		&ipsst.iss_tcp,			"packets",	ulong_derive },
	{ "udp",		&ipsst.iss_udp,			"packets",	ulong_derive },
	{ "icmp",		&ipsst.iss_icmp,		"packets",	ulong_derive },
	{ "nomem",		&ipsst.iss_nomem,		"packets",	ulong_derive },
	{ "expire",		&ipsst.iss_expire,		"packets",	ulong_derive },
	{ "fin",		&ipsst.iss_fin,			"packets",	ulong_derive },
	{ "active",		&ipsst.iss_active,		"gauge",	ulong_gauge },
	{ "logged",		&ipsst.iss_logged,		"packets",	ulong_derive },
	{ "log_fail",		&ipsst.iss_logfail,		"packets",	ulong_derive },
	{ "inuse",		&ipsst.iss_inuse,		"gauge",	ulong_gauge },
	{ "wild",		&ipsst.iss_wild,		"gauge",	ulong_gauge },
	{ "killed",		&ipsst.iss_killed,		"gauge",	ulong_gauge },
	{ "ticks",		&ipsst.iss_ticks,		"derive",	ulong_derive },
	{ "bucket_full",	&ipsst.iss_bucketfull,		"packets",	ulong_derive },
	{ "state_size",		&ipsst.iss_statesize,		"packets",	uint_derive },
	{ "state_max",		&ipsst.iss_statemax,		"packets",	uint_derive },
	/* iss_table */
	/* iss_list */
	/* iss_bucketlen */
	/* iss_tcptab */
	{ NULL,			NULL,				NULL,		uint_gauge /* suppress unused warning */ }
#else
#error "unknown IPFilter version"
#endif
};
/* clang-format on */

static int ipfilter_config(const char *key, const char *value) /* {{{ */ {
	char sep[] = " ,";
	char *w;

	if (STATIC_ARRAY_SIZE(report_tab) > sizeof(report_mask_t) * CHAR_BIT) {
		ERROR("ipfilter plugin: report_tab too large (report_mask_t too small)");
		return -1;
	}
	if (strcasecmp(key, "Report") == 0) {
		char *v = strdup(value);
		for (w = strtok(v, sep); w; w = strtok(NULL, sep)) {
			struct report *r;
			report_mask_t m;
			int found = 0;
			
			for (r = report_tab, m = 1; r->name; r++, m <<= 1) {
				if (m == 0) {
					ERROR("ipfilter plugin: too many reports");
					return 1;
				}
				if (strcmp(w, r->name) == 0) {
					found = 1; break;
				}
			}
			if (found) {
				report_mask |= m;
			} else {
				WARNING("ipfilter plugin: unknown report %s", w);
			}
		}
	} else
		return -1;

	return 0;
} /* }}} int ipfilter_config */

static int ipfilter_init(void) /* {{{ */ {
	ipfobj_t ipfo;
	struct friostat fio;

	if ((ipl = open(IPL_NAME, O_RDONLY)) == -1) {
		ERROR("ipfilter plugin: open(\"%s\": %s)", IPL_NAME, STRERRNO);
		return 1;
	}
	bzero(&ipfo, sizeof(ipfo));
	ipfo.ipfo_rev = IPFILTER_VERSION;
	ipfo.ipfo_type = IPFOBJ_IPFSTAT;
	ipfo.ipfo_ptr = &fio;
	ipfo.ipfo_size = sizeof(fio);
	if (ioctl(ipl, SIOCGETFS, &ipfo)) {
		ERROR("ipfilter plugin: ioctl(IPFSTAT): %s)", STRERRNO);
		return 1;
	}
	if (strncmp(IPL_VERSION, fio.f_version, sizeof(fio.f_version))) {
		ERROR("ipfilter plugin: version mismatch");
		return 1;
	}

	if ((ipstate = open(IPSTATE_NAME, O_RDONLY)) == -1) {
		ERROR("ipfilter plugin: open(\"%s\": %s)", IPSTATE_NAME, STRERRNO);
		return 1;
	}
	return 0;
} /* }}} int ipfilter_init */

static int ipfilter_shutdown(void) /* {{{ */ {
	close(ipl);
	close(ipstate);
	return 0;
} /* }}} int ipfilter_shutdown */

static int ipfilter_read(void) /* {{{ */ {
	ipfobj_t ipfo;
	struct report *r;
	report_mask_t m;
	value_list_t vl = VALUE_LIST_INIT;
	value_t values[1];

	bzero(&ipfo, sizeof(ipfo));
	ipfo.ipfo_rev = IPFILTER_VERSION;
	ipfo.ipfo_type = IPFOBJ_STATESTAT;
	ipfo.ipfo_ptr = &ipsst;
	ipfo.ipfo_size = sizeof(ips_stat_t);

	if ((ioctl(ipstate, SIOCGETFS, &ipfo) == -1)) {
		ERROR("ipfilter plugin: ioctl(STATESTAT): %s", STRERRNO);
		return 1;
	}

	vl.values = values;
	vl.values_len = 1;
	sstrncpy(vl.plugin, "ipfilter", sizeof(vl.plugin));
	for (r = report_tab, m = 1; r->name; r++, m <<= 1) {
		if (m == 0) {
			ERROR("ipfilter plugin: too many reports");
			return 1;
		}
		if (report_mask & m) {
			sstrncpy(vl.type, r->type, sizeof(vl.type));
			sstrncpy(vl.type_instance, r->name, sizeof(vl.type_instance));
			r->conv(r->stat, vl.values);
			plugin_dispatch_values(&vl);
		}
	}

	return 0;
} /* }}} int ipfilter_read */

void module_register(void) /* {{{ */ {
	plugin_register_init("ipfilter", ipfilter_init);
	plugin_register_shutdown("ipfilter", ipfilter_shutdown);
	plugin_register_config("ipfilter", ipfilter_config, config_keys, config_keys_num);
	plugin_register_read("ipfilter", ipfilter_read);
} /* }}} void module_register */
