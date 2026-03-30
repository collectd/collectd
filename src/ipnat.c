/**
 * collectd - src/ipfnat.c
 * Copyright (C) 2026 Edgar Fuß, Mathematisches Institut der Universität Bonn
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

#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <unistd.h>

/* reordering the #include's will make it fail to compile */
/* clang-format off */
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/ip_fil.h>
#include <netinet/ip_nat.h>
#include <netinet/ipl.h>
/* clang-format on */

typedef uint64_t report_mask_t;

static void uint_gauge(void *x, value_t *v) { v->gauge = *(u_int *)x; }

#if 0
static void ulong_gauge(void *x, value_t *v) { v->gauge = *(u_long *)x; }
#endif

#if 0
static void uint_derive(void *x, value_t *v) { v->derive = *(u_int *)x; }
#endif

static void ulong_derive(void *x, value_t *v) { v->derive = *(u_long *)x; }

struct report {
  char *name;                      /* config and type instance name */
  void *stat;                      /* pointer to field in ns structure */
  char *type;                      /* collectd type name */
  void (*conv)(void *, value_t *); /* conversion function */
};

static natstat_t ns;

/* formatting will make the table much harder to read */
/* clang-format off */
struct report report_tab[] = {
	{ "active",		&ns.ns_active,		"gauge",	uint_gauge },
	/* addtrpnt */
	{ "divert_build",	&ns.ns_divert_build,	"derive",	ulong_derive },
	{ "expire",		&ns.ns_expire,		"derive",	ulong_derive },
	{ "flush_all",		&ns.ns_flush_all,	"derive",	ulong_derive },
	{ "flush_closing",	&ns.ns_flush_closing,	"derive",	ulong_derive },
	{ "flush_queue",	&ns.ns_flush_queue,	"derive",	ulong_derive },
	{ "flush_state",	&ns.ns_flush_state,	"derive",	ulong_derive },
	{ "flush_timeout",	&ns.ns_flush_timeout,	"derive",	ulong_derive },
	{ "hm_new",		&ns.ns_hm_new,		"derive",	ulong_derive },
	{ "hm_newfail",		&ns.ns_hm_newfail,	"derive",	ulong_derive },
	{ "hm_addref",		&ns.ns_hm_addref,	"derive",	ulong_derive },
	{ "hm_nullnp",		&ns.ns_hm_nullnp,	"derive",	ulong_derive },
	{ "log_ok",		&ns.ns_log_ok,		"packets",	ulong_derive },
	{ "log_fail",		&ns.ns_log_fail,	"packets",	ulong_derive },
	{ "hostmap_sz",		&ns.ns_hostmap_sz,	"gauge",	uint_gauge },
	{ "nattab_sz",		&ns.ns_nattab_sz,	"gauge",	uint_gauge },
	{ "nattab_max",		&ns.ns_nattab_max,	"gauge",	uint_gauge },
	{ "orphans",		&ns.ns_orphans,		"gauge",	uint_gauge },
	{ "rules",		&ns.ns_rules,		"gauge",	uint_gauge },
	{ "rules_map",		&ns.ns_rules_map,	"gauge",	uint_gauge },
	{ "rules_rdr",		&ns.ns_rules_rdr,	"gauge",	uint_gauge },
	{ "rultab_sz",		&ns.ns_rultab_sz,	"gauge",	uint_gauge },
	{ "rdrtab_sz",		&ns.ns_rdrtab_sz,	"gauge",	uint_gauge },
	/* u_32_t ticks */
	/* trpntab_sz */
	{ "wilds",		&ns.ns_wilds,		"gauge",	uint_gauge },
	{ NULL,			NULL,			NULL,		NULL }
};

#define REPORT_SIDE(SIDE) {\
	{ "added",		&ns.ns_side[SIDE].ns_added,		"packets",	ulong_derive },\
	{ "appr_fail",		&ns.ns_side[SIDE].ns_appr_fail,		"packets",	ulong_derive },\
	{ "badnat",		&ns.ns_side[SIDE].ns_badnat,		"packets",	ulong_derive },\
	{ "badnatnew",		&ns.ns_side[SIDE].ns_badnatnew,		"packets",	ulong_derive },\
	{ "badnextaddr",	&ns.ns_side[SIDE].ns_badnextaddr,	"packets",	ulong_derive },\
	{ "bucket_max",		&ns.ns_side[SIDE].ns_bucket_max,	"packets",	ulong_derive },\
	{ "clone_nomem",	&ns.ns_side[SIDE].ns_clone_nomem,	"packets",	ulong_derive },\
	{ "decap_bad",		&ns.ns_side[SIDE].ns_decap_bad,		"packets",	ulong_derive },\
	{ "decap_fail",		&ns.ns_side[SIDE].ns_decap_fail,	"packets",	ulong_derive },\
	{ "decap_pullup",	&ns.ns_side[SIDE].ns_decap_pullup,	"packets",	ulong_derive },\
	{ "divert_dup",		&ns.ns_side[SIDE].ns_divert_dup,	"packets",	ulong_derive },\
	{ "divert_exist",	&ns.ns_side[SIDE].ns_divert_exist,	"packets",	ulong_derive },\
	{ "drop",		&ns.ns_side[SIDE].ns_drop,		"packets",	ulong_derive },\
	{ "encap_dup",		&ns.ns_side[SIDE].ns_encap_dup,		"packets",	ulong_derive },\
	{ "encap_pullup",	&ns.ns_side[SIDE].ns_encap_pullup,	"packets",	ulong_derive },\
	{ "exhausted",		&ns.ns_side[SIDE].ns_exhausted,		"packets",	ulong_derive },\
	{ "icmp_address",	&ns.ns_side[SIDE].ns_icmp_address,	"packets",	ulong_derive },\
	{ "icmp_basic",		&ns.ns_side[SIDE].ns_icmp_basic,	"packets",	ulong_derive },\
	{ "icmp_mbuf",		&ns.ns_side[SIDE].ns_icmp_mbuf,		"packets",	ulong_derive },\
	{ "icmp_notfound",	&ns.ns_side[SIDE].ns_icmp_notfound,	"packets",	ulong_derive },\
	{ "icmp_rebuild",	&ns.ns_side[SIDE].ns_icmp_rebuild,	"packets",	ulong_derive },\
	{ "icmp_short",		&ns.ns_side[SIDE].ns_icmp_short,	"packets",	ulong_derive },\
	{ "icmp_size",		&ns.ns_side[SIDE].ns_icmp_size,		"packets",	ulong_derive },\
	{ "ifpaddrfail",	&ns.ns_side[SIDE].ns_ifpaddrfail,	"packets",	ulong_derive },\
	{ "ignored",		&ns.ns_side[SIDE].ns_ignored,		"packets",	ulong_derive },\
	{ "insert_fail",	&ns.ns_side[SIDE].ns_insert_fail,	"packets",	ulong_derive },\
	{ "inuse",		&ns.ns_side[SIDE].ns_inuse,		"packets",	ulong_derive },\
	{ "log",		&ns.ns_side[SIDE].ns_log,		"packets",	ulong_derive },\
	{ "lookup_miss",	&ns.ns_side[SIDE].ns_lookup_miss,	"packets",	ulong_derive },\
	{ "lookup_nowild",	&ns.ns_side[SIDE].ns_lookup_nowild,	"packets",	ulong_derive },\
	{ "new_ifpaddr",	&ns.ns_side[SIDE].ns_new_ifpaddr,	"packets",	ulong_derive },\
	{ "memfail",		&ns.ns_side[SIDE].ns_memfail,		"packets",	ulong_derive },\
	{ "table_max",		&ns.ns_side[SIDE].ns_table_max,		"packets",	ulong_derive },\
	{ "translated",		&ns.ns_side[SIDE].ns_translated,	"packets",	ulong_derive },\
	{ "unfinalised",	&ns.ns_side[SIDE].ns_unfinalised,	"packets",	ulong_derive },\
	{ "wrap",		&ns.ns_side[SIDE].ns_wrap,		"packets",	ulong_derive },\
	{ "xlate_null",		&ns.ns_side[SIDE].ns_xlate_null,	"packets",	ulong_derive },\
	{ "xlate_exists",	&ns.ns_side[SIDE].ns_xlate_exists,	"packets",	ulong_derive },\
	{ "ipf_proxy_fail",	&ns.ns_side[SIDE].ns_ipf_proxy_fail,	"packets",	ulong_derive },\
	/* uncreate[2] */\
	{ NULL,			NULL,					NULL,		NULL }\
}
/* clang-format on */

struct report report_tab_in[] = REPORT_SIDE(0);
struct report report_tab_out[] = REPORT_SIDE(1);

static const char *config_keys[] = {"Report", "ReportIn", "ReportOut"};
static report_mask_t report_mask = 0, report_mask_in = 0, report_mask_out = 0;

static int ipl, ipnat;

static report_mask_t
ipnat_findreport(const char *const w,
                 const struct report *const tab) /* {{{ */ {
  const struct report *r;
  report_mask_t m;

  for (r = tab, m = 1; r->name; r++, m <<= 1) {
    if (m == 0) {
      ERROR("ipnat plugin: too many reports");
      return 0;
    }
    if (strcmp(w, r->name) == 0) {
      return m;
    }
  }
  return 0;
} /* }}} int ipnat_findreport */

static int ipnat_config(const char *key, const char *value) /* {{{ */ {
  char sep[] = " ,";
  char *v, *w;
  report_mask_t m;

  if ((v = strdup(value)) == NULL) {
    ERROR("ipnat plugin: strdup() failed");
    return 1;
  }
  if (strcasecmp(key, "Report") == 0) {
    for (w = strtok(v, sep); w; w = strtok(NULL, sep)) {
      if ((m = ipnat_findreport(w, report_tab)) != 0) {
        report_mask |= m;
      } else if ((m = ipnat_findreport(w, report_tab_in)) != 0) {
        report_mask_in |= m;
        report_mask_out |= m;
      } else {
        WARNING("ipnat plugin: unknown report %s", w);
      }
    }
    free(v);
    return 0;
  } else if (strcasecmp(key, "ReportIn") == 0) {
    for (w = strtok(v, sep); w; w = strtok(NULL, sep)) {
      if ((m = ipnat_findreport(w, report_tab_in)) != 0) {
        report_mask_in |= m;
      } else {
        WARNING("ipnat plugin: unknown in report %s", w);
      }
    }
    free(v);
    return 0;
  } else if (strcasecmp(key, "ReportOut") == 0) {
    for (w = strtok(v, sep); w; w = strtok(NULL, sep)) {
      if ((m = ipnat_findreport(w, report_tab_out)) != 0) {
        report_mask_out |= m;
      } else {
        WARNING("ipnat plugin: unknown out report %s", w);
      }
    }
    free(v);
    return 0;
  } else {
    free(v);
    return 1;
  }
} /* }}} int ipnat_config */

static int ipnat_init(void) /* {{{ */ {
  ipfobj_t ipfo;
  struct friostat fio;

  if (STATIC_ARRAY_SIZE(report_tab) > sizeof(report_mask_t) * CHAR_BIT) {
    ERROR("ipnat plugin: report_tab too large (report_mask_t too small)");
    return 1;
  }
  if (STATIC_ARRAY_SIZE(report_tab_in) > sizeof(report_mask_t) * CHAR_BIT) {
    ERROR("ipnat plugin: report_tab_in too large (report_mask_t too small)");
    return 1;
  }

  if ((ipl = open(IPL_NAME, O_RDONLY)) == -1) {
    ERROR("ipnat plugin: open(\"%s\": %s)", IPL_NAME, STRERRNO);
    return 1;
  }
  bzero(&ipfo, sizeof(ipfo));
  ipfo.ipfo_rev = IPFILTER_VERSION;
  ipfo.ipfo_type = IPFOBJ_IPFSTAT;
  ipfo.ipfo_ptr = &fio;
  ipfo.ipfo_size = sizeof(fio);
  if (ioctl(ipl, SIOCGETFS, &ipfo)) {
    ERROR("ipnat plugin: ioctl(IPFSTAT): %s)", STRERRNO);
    return 1;
  }
  if (strncmp(IPL_VERSION, fio.f_version, sizeof(fio.f_version))) {
    ERROR("ipnat plugin: version mismatch");
    return 1;
  }

  if ((ipnat = open(IPNAT_NAME, O_RDONLY)) == -1) {
    ERROR("ipnat plugin: open(\"%s\": %s)", IPNAT_NAME, STRERRNO);
    return 1;
  }

  return 0;
} /* }}} int ipnat_init */

static int ipnat_shutdown(void) /* {{{ */ {
  close(ipl);
  close(ipnat);
  return 0;
} /* }}} int ipnat_shutdown */

static int ipnat_read(void) /* {{{ */ {
  ipfobj_t ipfo;
  struct report *r;
  report_mask_t m;
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[1];

  bzero(&ipfo, sizeof(ipfo));
  ipfo.ipfo_rev = IPFILTER_VERSION;
  ipfo.ipfo_type = IPFOBJ_NATSTAT;
  ipfo.ipfo_ptr = &ns;
  ipfo.ipfo_size = sizeof(ns);

  if ((ioctl(ipnat, SIOCGNATS, &ipfo) == -1)) {
    ERROR("ipnat plugin: ioctl(SIOCGNATS): %s", STRERRNO);
    return 1;
  }

  vl.values = values;
  vl.values_len = 1;
  sstrncpy(vl.plugin, "ipnat", sizeof(vl.plugin));

  for (r = report_tab, m = 1; r->name; r++, m <<= 1) {
    if (m == 0) {
      ERROR("ipnat plugin: internal: too many reports");
      return 1;
    }
    if (report_mask & m) {
      /* sstrncpy(vl.plugin_instance, "", sizeof(vl.plugin_instance)); */
      sstrncpy(vl.type, r->type, sizeof(vl.type));
      sstrncpy(vl.type_instance, r->name, sizeof(vl.type_instance));
      r->conv(r->stat, vl.values);
      plugin_dispatch_values(&vl);
    }
  }

  for (r = report_tab_in, m = 1; r->name; r++, m <<= 1) {
    if (m == 0) {
      ERROR("ipnat plugin: internal: too many in reports");
      return 1;
    }
    if (report_mask_in & m) {
      sstrncpy(vl.plugin_instance, "in", sizeof(vl.plugin_instance));
      sstrncpy(vl.type, r->type, sizeof(vl.type));
      sstrncpy(vl.type_instance, r->name, sizeof(vl.type_instance));
      r->conv(r->stat, vl.values);
      plugin_dispatch_values(&vl);
    }
  }

  for (r = report_tab_out, m = 1; r->name; r++, m <<= 1) {
    if (m == 0) {
      ERROR("ipnat plugin: internal: too many out reports");
      return 1;
    }
    if (report_mask_out & m) {
      sstrncpy(vl.plugin_instance, "out", sizeof(vl.plugin_instance));
      sstrncpy(vl.type, r->type, sizeof(vl.type));
      sstrncpy(vl.type_instance, r->name, sizeof(vl.type_instance));
      r->conv(r->stat, vl.values);
      plugin_dispatch_values(&vl);
    }
  }

  return 0;
} /* }}} int ipnat_read */

void module_register(void) /* {{{ */ {
  plugin_register_init("ipnat", ipnat_init);
  plugin_register_shutdown("ipnat", ipnat_shutdown);
  plugin_register_config("ipnat", ipnat_config, config_keys, STATIC_ARRAY_SIZE(config_keys));
  plugin_register_read("ipnat", ipnat_read);
} /* }}} void module_register */
