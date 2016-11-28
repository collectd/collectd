/**
 * collectd - src/varnish.c
 * Copyright (C) 2010      Jérôme Renard
 * Copyright (C) 2010      Marc Fournier
 * Copyright (C) 2010-2012 Florian Forster
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
 *   Jérôme Renard <jerome.renard at gmail.com>
 *   Marc Fournier <marc.fournier at camptocamp.com>
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#if HAVE_VARNISH_V4
#include <vapi/vsc.h>
#include <vapi/vsm.h>
typedef struct VSC_C_main c_varnish_stats_t;
#endif

#if HAVE_VARNISH_V3
#include <varnishapi.h>
#include <vsc.h>
typedef struct VSC_C_main c_varnish_stats_t;
#endif

#if HAVE_VARNISH_V2
#include <varnishapi.h>
typedef struct varnish_stats c_varnish_stats_t;
#endif

/* {{{ user_config_s */
struct user_config_s {
  char *instance;

  _Bool collect_cache;
  _Bool collect_connections;
  _Bool collect_esi;
  _Bool collect_backend;
#ifdef HAVE_VARNISH_V3
  _Bool collect_dirdns;
#endif
  _Bool collect_fetch;
  _Bool collect_hcb;
  _Bool collect_objects;
#if HAVE_VARNISH_V2
  _Bool collect_purge;
#else
  _Bool collect_ban;
#endif
  _Bool collect_session;
  _Bool collect_shm;
  _Bool collect_sms;
#if HAVE_VARNISH_V2
  _Bool collect_sm;
  _Bool collect_sma;
#endif
  _Bool collect_struct;
  _Bool collect_totals;
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
  _Bool collect_uptime;
#endif
  _Bool collect_vcl;
  _Bool collect_workers;
#if HAVE_VARNISH_V4
  _Bool collect_vsm;
#endif
};
typedef struct user_config_s user_config_t; /* }}} */

static _Bool have_instance = 0;

static int varnish_submit(const char *plugin_instance, /* {{{ */
                          const char *category, const char *type,
                          const char *type_instance, value_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &value;
  vl.values_len = 1;

  sstrncpy(vl.plugin, "varnish", sizeof(vl.plugin));

  if (plugin_instance == NULL)
    plugin_instance = "default";
  ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s-%s",
            plugin_instance, category);

  sstrncpy(vl.type, type, sizeof(vl.type));

  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  return (plugin_dispatch_values(&vl));
} /* }}} int varnish_submit */

static int varnish_submit_gauge(const char *plugin_instance, /* {{{ */
                                const char *category, const char *type,
                                const char *type_instance,
                                uint64_t gauge_value) {
  return (varnish_submit(plugin_instance, category, type, type_instance,
                         (value_t){.gauge = (gauge_t)gauge_value}));
} /* }}} int varnish_submit_gauge */

static int varnish_submit_derive(const char *plugin_instance, /* {{{ */
                                 const char *category, const char *type,
                                 const char *type_instance,
                                 uint64_t derive_value) {
  return (varnish_submit(plugin_instance, category, type, type_instance,
                         (value_t){.derive = (derive_t)derive_value}));
} /* }}} int varnish_submit_derive */

#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
static int varnish_monitor(void *priv,
                           const struct VSC_point *const pt) /* {{{ */
{
  uint64_t val;
  const user_config_t *conf;
  const char *class;
  const char *name;

  if (pt == NULL)
    return (0);

  conf = priv;

#if HAVE_VARNISH_V4
  class = pt->section->fantom->type;
  name = pt->desc->name;

  if (strcmp(class, "MAIN") != 0)
    return (0);

#elif HAVE_VARNISH_V3
  class = pt->class;
  name = pt->name;

  if (strcmp(class, "") != 0)
    return (0);
#endif

  val = *(const volatile uint64_t *)pt->ptr;

  if (conf->collect_cache) {
    if (strcmp(name, "cache_hit") == 0)
      return varnish_submit_derive(conf->instance, "cache", "cache_result",
                                   "hit", val);
    else if (strcmp(name, "cache_miss") == 0)
      return varnish_submit_derive(conf->instance, "cache", "cache_result",
                                   "miss", val);
    else if (strcmp(name, "cache_hitpass") == 0)
      return varnish_submit_derive(conf->instance, "cache", "cache_result",
                                   "hitpass", val);
  }

  if (conf->collect_connections) {
    if (strcmp(name, "client_conn") == 0)
      return varnish_submit_derive(conf->instance, "connections", "connections",
                                   "accepted", val);
    else if (strcmp(name, "client_drop") == 0)
      return varnish_submit_derive(conf->instance, "connections", "connections",
                                   "dropped", val);
    else if (strcmp(name, "client_req") == 0)
      return varnish_submit_derive(conf->instance, "connections", "connections",
                                   "received", val);
  }

#ifdef HAVE_VARNISH_V3
  if (conf->collect_dirdns) {
    if (strcmp(name, "dir_dns_lookups") == 0)
      return varnish_submit_derive(conf->instance, "dirdns", "cache_operation",
                                   "lookups", val);
    else if (strcmp(name, "dir_dns_failed") == 0)
      return varnish_submit_derive(conf->instance, "dirdns", "cache_result",
                                   "failed", val);
    else if (strcmp(name, "dir_dns_hit") == 0)
      return varnish_submit_derive(conf->instance, "dirdns", "cache_result",
                                   "hits", val);
    else if (strcmp(name, "dir_dns_cache_full") == 0)
      return varnish_submit_derive(conf->instance, "dirdns", "cache_result",
                                   "cache_full", val);
  }
#endif

  if (conf->collect_esi) {
    if (strcmp(name, "esi_errors") == 0)
      return varnish_submit_derive(conf->instance, "esi", "total_operations",
                                   "error", val);
    else if (strcmp(name, "esi_parse") == 0)
      return varnish_submit_derive(conf->instance, "esi", "total_operations",
                                   "parsed", val);
    else if (strcmp(name, "esi_warnings") == 0)
      return varnish_submit_derive(conf->instance, "esi", "total_operations",
                                   "warning", val);
  }

  if (conf->collect_backend) {
    if (strcmp(name, "backend_conn") == 0)
      return varnish_submit_derive(conf->instance, "backend", "connections",
                                   "success", val);
    else if (strcmp(name, "backend_unhealthy") == 0)
      return varnish_submit_derive(conf->instance, "backend", "connections",
                                   "not-attempted", val);
    else if (strcmp(name, "backend_busy") == 0)
      return varnish_submit_derive(conf->instance, "backend", "connections",
                                   "too-many", val);
    else if (strcmp(name, "backend_fail") == 0)
      return varnish_submit_derive(conf->instance, "backend", "connections",
                                   "failures", val);
    else if (strcmp(name, "backend_reuse") == 0)
      return varnish_submit_derive(conf->instance, "backend", "connections",
                                   "reuses", val);
    else if (strcmp(name, "backend_toolate") == 0)
      return varnish_submit_derive(conf->instance, "backend", "connections",
                                   "was-closed", val);
    else if (strcmp(name, "backend_recycle") == 0)
      return varnish_submit_derive(conf->instance, "backend", "connections",
                                   "recycled", val);
    else if (strcmp(name, "backend_unused") == 0)
      return varnish_submit_derive(conf->instance, "backend", "connections",
                                   "unused", val);
    else if (strcmp(name, "backend_retry") == 0)
      return varnish_submit_derive(conf->instance, "backend", "connections",
                                   "retries", val);
    else if (strcmp(name, "backend_req") == 0)
      return varnish_submit_derive(conf->instance, "backend", "http_requests",
                                   "requests", val);
    else if (strcmp(name, "n_backend") == 0)
      return varnish_submit_gauge(conf->instance, "backend", "backends",
                                  "n_backends", val);
  }

  if (conf->collect_fetch) {
    if (strcmp(name, "fetch_head") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "head", val);
    else if (strcmp(name, "fetch_length") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "length", val);
    else if (strcmp(name, "fetch_chunked") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "chunked", val);
    else if (strcmp(name, "fetch_eof") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "eof", val);
    else if (strcmp(name, "fetch_bad") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "bad_headers", val);
    else if (strcmp(name, "fetch_close") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "close", val);
    else if (strcmp(name, "fetch_oldhttp") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "oldhttp", val);
    else if (strcmp(name, "fetch_zero") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "zero", val);
    else if (strcmp(name, "fetch_failed") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "failed", val);
    else if (strcmp(name, "fetch_1xx") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "no_body_1xx", val);
    else if (strcmp(name, "fetch_204") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "no_body_204", val);
    else if (strcmp(name, "fetch_304") == 0)
      return varnish_submit_derive(conf->instance, "fetch", "http_requests",
                                   "no_body_304", val);
  }

  if (conf->collect_hcb) {
    if (strcmp(name, "hcb_nolock") == 0)
      return varnish_submit_derive(conf->instance, "hcb", "cache_operation",
                                   "lookup_nolock", val);
    else if (strcmp(name, "hcb_lock") == 0)
      return varnish_submit_derive(conf->instance, "hcb", "cache_operation",
                                   "lookup_lock", val);
    else if (strcmp(name, "hcb_insert") == 0)
      return varnish_submit_derive(conf->instance, "hcb", "cache_operation",
                                   "insert", val);
  }

  if (conf->collect_objects) {
    if (strcmp(name, "n_expired") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "expired", val);
    else if (strcmp(name, "n_lru_nuked") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "lru_nuked", val);
    else if (strcmp(name, "n_lru_saved") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "lru_saved", val);
    else if (strcmp(name, "n_lru_moved") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "lru_moved", val);
    else if (strcmp(name, "n_deathrow") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "deathrow", val);
    else if (strcmp(name, "losthdr") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "header_overflow", val);
    else if (strcmp(name, "n_obj_purged") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "purged", val);
    else if (strcmp(name, "n_objsendfile") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "sent_sendfile", val);
    else if (strcmp(name, "n_objwrite") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "sent_write", val);
    else if (strcmp(name, "n_objoverflow") == 0)
      return varnish_submit_derive(conf->instance, "objects", "total_objects",
                                   "workspace_overflow", val);
  }

#if HAVE_VARNISH_V3
  if (conf->collect_ban) {
    if (strcmp(name, "n_ban") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "total", val);
    else if (strcmp(name, "n_ban_add") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "added", val);
    else if (strcmp(name, "n_ban_retire") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "deleted", val);
    else if (strcmp(name, "n_ban_obj_test") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "objects_tested", val);
    else if (strcmp(name, "n_ban_re_test") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "regexps_tested", val);
    else if (strcmp(name, "n_ban_dups") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "duplicate", val);
  }
#endif
#if HAVE_VARNISH_V4
  if (conf->collect_ban) {
    if (strcmp(name, "bans") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "total", val);
    else if (strcmp(name, "bans_added") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "added", val);
    else if (strcmp(name, "bans_obj") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "obj", val);
    else if (strcmp(name, "bans_req") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "req", val);
    else if (strcmp(name, "bans_completed") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "completed", val);
    else if (strcmp(name, "bans_deleted") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "deleted", val);
    else if (strcmp(name, "bans_tested") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "tested", val);
    else if (strcmp(name, "bans_dups") == 0)
      return varnish_submit_derive(conf->instance, "ban", "total_operations",
                                   "duplicate", val);
  }
#endif

  if (conf->collect_session) {
    if (strcmp(name, "sess_closed") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "closed", val);
    else if (strcmp(name, "sess_pipeline") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "pipeline", val);
    else if (strcmp(name, "sess_readahead") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "readahead", val);
    else if (strcmp(name, "sess_conn") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "accepted", val);
    else if (strcmp(name, "sess_drop") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "dropped", val);
    else if (strcmp(name, "sess_fail") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "failed", val);
    else if (strcmp(name, "sess_pipe_overflow") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "overflow", val);
    else if (strcmp(name, "sess_queued") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "queued", val);
    else if (strcmp(name, "sess_linger") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "linger", val);
    else if (strcmp(name, "sess_herd") == 0)
      return varnish_submit_derive(conf->instance, "session",
                                   "total_operations", "herd", val);
  }

  if (conf->collect_shm) {
    if (strcmp(name, "shm_records") == 0)
      return varnish_submit_derive(conf->instance, "shm", "total_operations",
                                   "records", val);
    else if (strcmp(name, "shm_writes") == 0)
      return varnish_submit_derive(conf->instance, "shm", "total_operations",
                                   "writes", val);
    else if (strcmp(name, "shm_flushes") == 0)
      return varnish_submit_derive(conf->instance, "shm", "total_operations",
                                   "flushes", val);
    else if (strcmp(name, "shm_cont") == 0)
      return varnish_submit_derive(conf->instance, "shm", "total_operations",
                                   "contention", val);
    else if (strcmp(name, "shm_cycles") == 0)
      return varnish_submit_derive(conf->instance, "shm", "total_operations",
                                   "cycles", val);
  }

  if (conf->collect_sms) {
    if (strcmp(name, "sms_nreq") == 0)
      return varnish_submit_derive(conf->instance, "sms", "total_requests",
                                   "allocator", val);
    else if (strcmp(name, "sms_nobj") == 0)
      return varnish_submit_gauge(conf->instance, "sms", "requests",
                                  "outstanding", val);
    else if (strcmp(name, "sms_nbytes") == 0)
      return varnish_submit_gauge(conf->instance, "sms", "bytes", "outstanding",
                                  val);
    else if (strcmp(name, "sms_balloc") == 0)
      return varnish_submit_derive(conf->instance, "sms", "total_bytes",
                                   "allocated", val);
    else if (strcmp(name, "sms_bfree") == 0)
      return varnish_submit_derive(conf->instance, "sms", "total_bytes", "free",
                                   val);
  }

  if (conf->collect_struct) {
    if (strcmp(name, "n_sess_mem") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "current_sessions",
                                  "sess_mem", val);
    else if (strcmp(name, "n_sess") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "current_sessions",
                                  "sess", val);
    else if (strcmp(name, "n_object") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "objects", "object",
                                  val);
    else if (strcmp(name, "n_vampireobject") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "objects",
                                  "vampireobject", val);
    else if (strcmp(name, "n_objectcore") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "objects",
                                  "objectcore", val);
    else if (strcmp(name, "n_waitinglist") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "objects",
                                  "waitinglist", val);
    else if (strcmp(name, "n_objecthead") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "objects",
                                  "objecthead", val);
    else if (strcmp(name, "n_smf") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "objects", "smf",
                                  val);
    else if (strcmp(name, "n_smf_frag") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "objects",
                                  "smf_frag", val);
    else if (strcmp(name, "n_smf_large") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "objects",
                                  "smf_large", val);
    else if (strcmp(name, "n_vbe_conn") == 0)
      return varnish_submit_gauge(conf->instance, "struct", "objects",
                                  "vbe_conn", val);
  }

  if (conf->collect_totals) {
    if (strcmp(name, "s_sess") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_sessions",
                                   "sessions", val);
    else if (strcmp(name, "s_req") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_requests",
                                   "requests", val);
    else if (strcmp(name, "s_pipe") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_operations",
                                   "pipe", val);
    else if (strcmp(name, "s_pass") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_operations",
                                   "pass", val);
    else if (strcmp(name, "s_fetch") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_operations",
                                   "fetches", val);
    else if (strcmp(name, "s_synth") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "synth", val);
    else if (strcmp(name, "s_req_hdrbytes") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "req_header", val);
    else if (strcmp(name, "s_req_bodybytes") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "req_body", val);
    else if (strcmp(name, "s_resp_hdrbytes") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "resp_header", val);
    else if (strcmp(name, "s_resp_bodybytes") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "resp_body", val);
    else if (strcmp(name, "s_pipe_hdrbytes") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "pipe_header", val);
    else if (strcmp(name, "s_pipe_in") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "pipe_in", val);
    else if (strcmp(name, "s_pipe_out") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "pipe_out", val);
    else if (strcmp(name, "n_purges") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_operations",
                                   "purges", val);
    else if (strcmp(name, "s_hdrbytes") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "header-bytes", val);
    else if (strcmp(name, "s_bodybytes") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_bytes",
                                   "body-bytes", val);
    else if (strcmp(name, "n_gzip") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_operations",
                                   "gzip", val);
    else if (strcmp(name, "n_gunzip") == 0)
      return varnish_submit_derive(conf->instance, "totals", "total_operations",
                                   "gunzip", val);
  }

  if (conf->collect_uptime) {
    if (strcmp(name, "uptime") == 0)
      return varnish_submit_gauge(conf->instance, "uptime", "uptime",
                                  "client_uptime", val);
  }

  if (conf->collect_vcl) {
    if (strcmp(name, "n_vcl") == 0)
      return varnish_submit_gauge(conf->instance, "vcl", "vcl", "total_vcl",
                                  val);
    else if (strcmp(name, "n_vcl_avail") == 0)
      return varnish_submit_gauge(conf->instance, "vcl", "vcl", "avail_vcl",
                                  val);
    else if (strcmp(name, "n_vcl_discard") == 0)
      return varnish_submit_gauge(conf->instance, "vcl", "vcl", "discarded_vcl",
                                  val);
    else if (strcmp(name, "vmods") == 0)
      return varnish_submit_gauge(conf->instance, "vcl", "objects", "vmod",
                                  val);
  }

  if (conf->collect_workers) {
    if (strcmp(name, "threads") == 0)
      return varnish_submit_gauge(conf->instance, "workers", "threads",
                                  "worker", val);
    else if (strcmp(name, "threads_created") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_threads",
                                   "created", val);
    else if (strcmp(name, "threads_failed") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_threads",
                                   "failed", val);
    else if (strcmp(name, "threads_limited") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_threads",
                                   "limited", val);
    else if (strcmp(name, "threads_destroyed") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_threads",
                                   "dropped", val);
    else if (strcmp(name, "thread_queue_len") == 0)
      return varnish_submit_derive(conf->instance, "workers", "queue_length",
                                   "threads", val);
    else if (strcmp(name, "n_wrk") == 0)
      return varnish_submit_gauge(conf->instance, "workers", "threads",
                                  "worker", val);
    else if (strcmp(name, "n_wrk_create") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_threads",
                                   "created", val);
    else if (strcmp(name, "n_wrk_failed") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_threads",
                                   "failed", val);
    else if (strcmp(name, "n_wrk_max") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_threads",
                                   "limited", val);
    else if (strcmp(name, "n_wrk_drop") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_threads",
                                   "dropped", val);
    else if (strcmp(name, "n_wrk_queue") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_requests",
                                   "queued", val);
    else if (strcmp(name, "n_wrk_overflow") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_requests",
                                   "overflowed", val);
    else if (strcmp(name, "n_wrk_queued") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_requests",
                                   "queued", val);
    else if (strcmp(name, "n_wrk_lqueue") == 0)
      return varnish_submit_derive(conf->instance, "workers", "total_requests",
                                   "queue_length", val);
  }

#if HAVE_VARNISH_V4
  if (conf->collect_vsm) {
    if (strcmp(name, "vsm_free") == 0)
      return varnish_submit_gauge(conf->instance, "vsm", "bytes", "free", val);
    else if (strcmp(name, "vsm_used") == 0)
      return varnish_submit_gauge(conf->instance, "vsm", "bytes", "used", val);
    else if (strcmp(name, "vsm_cooling") == 0)
      return varnish_submit_gauge(conf->instance, "vsm", "bytes", "cooling",
                                  val);
    else if (strcmp(name, "vsm_overflow") == 0)
      return varnish_submit_gauge(conf->instance, "vsm", "bytes", "overflow",
                                  val);
    else if (strcmp(name, "vsm_overflowed") == 0)
      return varnish_submit_derive(conf->instance, "vsm", "total_bytes",
                                   "overflowed", val);
  }
#endif

  return (0);

} /* }}} static int varnish_monitor */
#else /* if HAVE_VARNISH_V2 */
static void varnish_monitor(const user_config_t *conf, /* {{{ */
                            const c_varnish_stats_t *stats) {
  if (conf->collect_cache) {
    /* Cache hits */
    varnish_submit_derive(conf->instance, "cache", "cache_result", "hit",
                          stats->cache_hit);
    /* Cache misses */
    varnish_submit_derive(conf->instance, "cache", "cache_result", "miss",
                          stats->cache_miss);
    /* Cache hits for pass */
    varnish_submit_derive(conf->instance, "cache", "cache_result", "hitpass",
                          stats->cache_hitpass);
  }

  if (conf->collect_connections) {
    /* Client connections accepted */
    varnish_submit_derive(conf->instance, "connections", "connections",
                          "accepted", stats->client_conn);
    /* Connection dropped, no sess */
    varnish_submit_derive(conf->instance, "connections", "connections",
                          "dropped", stats->client_drop);
    /* Client requests received    */
    varnish_submit_derive(conf->instance, "connections", "connections",
                          "received", stats->client_req);
  }

  if (conf->collect_esi) {
    /* ESI parse errors (unlock)   */
    varnish_submit_derive(conf->instance, "esi", "total_operations", "error",
                          stats->esi_errors);
    /* Objects ESI parsed (unlock) */
    varnish_submit_derive(conf->instance, "esi", "total_operations", "parsed",
                          stats->esi_parse);
  }

  if (conf->collect_backend) {
    /* Backend conn. success       */
    varnish_submit_derive(conf->instance, "backend", "connections", "success",
                          stats->backend_conn);
    /* Backend conn. not attempted */
    varnish_submit_derive(conf->instance, "backend", "connections",
                          "not-attempted", stats->backend_unhealthy);
    /* Backend conn. too many      */
    varnish_submit_derive(conf->instance, "backend", "connections", "too-many",
                          stats->backend_busy);
    /* Backend conn. failures      */
    varnish_submit_derive(conf->instance, "backend", "connections", "failures",
                          stats->backend_fail);
    /* Backend conn. reuses        */
    varnish_submit_derive(conf->instance, "backend", "connections", "reuses",
                          stats->backend_reuse);
    /* Backend conn. was closed    */
    varnish_submit_derive(conf->instance, "backend", "connections",
                          "was-closed", stats->backend_toolate);
    /* Backend conn. recycles      */
    varnish_submit_derive(conf->instance, "backend", "connections", "recycled",
                          stats->backend_recycle);
    /* Backend conn. unused        */
    varnish_submit_derive(conf->instance, "backend", "connections", "unused",
                          stats->backend_unused);
    /* Backend requests mades      */
    varnish_submit_derive(conf->instance, "backend", "http_requests",
                          "requests", stats->backend_req);
    /* N backends                  */
    varnish_submit_gauge(conf->instance, "backend", "backends", "n_backends",
                         stats->n_backend);
  }

  if (conf->collect_fetch) {
    /* Fetch head                */
    varnish_submit_derive(conf->instance, "fetch", "http_requests", "head",
                          stats->fetch_head);
    /* Fetch with length         */
    varnish_submit_derive(conf->instance, "fetch", "http_requests", "length",
                          stats->fetch_length);
    /* Fetch chunked             */
    varnish_submit_derive(conf->instance, "fetch", "http_requests", "chunked",
                          stats->fetch_chunked);
    /* Fetch EOF                 */
    varnish_submit_derive(conf->instance, "fetch", "http_requests", "eof",
                          stats->fetch_eof);
    /* Fetch bad headers         */
    varnish_submit_derive(conf->instance, "fetch", "http_requests",
                          "bad_headers", stats->fetch_bad);
    /* Fetch wanted close        */
    varnish_submit_derive(conf->instance, "fetch", "http_requests", "close",
                          stats->fetch_close);
    /* Fetch pre HTTP/1.1 closed */
    varnish_submit_derive(conf->instance, "fetch", "http_requests", "oldhttp",
                          stats->fetch_oldhttp);
    /* Fetch zero len            */
    varnish_submit_derive(conf->instance, "fetch", "http_requests", "zero",
                          stats->fetch_zero);
    /* Fetch failed              */
    varnish_submit_derive(conf->instance, "fetch", "http_requests", "failed",
                          stats->fetch_failed);
  }

  if (conf->collect_hcb) {
    /* HCB Lookups without lock */
    varnish_submit_derive(conf->instance, "hcb", "cache_operation",
                          "lookup_nolock", stats->hcb_nolock);
    /* HCB Lookups with lock    */
    varnish_submit_derive(conf->instance, "hcb", "cache_operation",
                          "lookup_lock", stats->hcb_lock);
    /* HCB Inserts              */
    varnish_submit_derive(conf->instance, "hcb", "cache_operation", "insert",
                          stats->hcb_insert);
  }

  if (conf->collect_objects) {
    /* N expired objects             */
    varnish_submit_derive(conf->instance, "objects", "total_objects", "expired",
                          stats->n_expired);
    /* N LRU nuked objects           */
    varnish_submit_derive(conf->instance, "objects", "total_objects",
                          "lru_nuked", stats->n_lru_nuked);
    /* N LRU saved objects           */
    varnish_submit_derive(conf->instance, "objects", "total_objects",
                          "lru_saved", stats->n_lru_saved);
    /* N LRU moved objects           */
    varnish_submit_derive(conf->instance, "objects", "total_objects",
                          "lru_moved", stats->n_lru_moved);
    /* N objects on deathrow         */
    varnish_submit_derive(conf->instance, "objects", "total_objects",
                          "deathrow", stats->n_deathrow);
    /* HTTP header overflows         */
    varnish_submit_derive(conf->instance, "objects", "total_objects",
                          "header_overflow", stats->losthdr);
    /* Objects sent with sendfile    */
    varnish_submit_derive(conf->instance, "objects", "total_objects",
                          "sent_sendfile", stats->n_objsendfile);
    /* Objects sent with write       */
    varnish_submit_derive(conf->instance, "objects", "total_objects",
                          "sent_write", stats->n_objwrite);
    /* Objects overflowing workspace */
    varnish_submit_derive(conf->instance, "objects", "total_objects",
                          "workspace_overflow", stats->n_objoverflow);
  }

  if (conf->collect_purge) {
    /* N total active purges      */
    varnish_submit_derive(conf->instance, "purge", "total_operations", "total",
                          stats->n_purge);
    /* N new purges added         */
    varnish_submit_derive(conf->instance, "purge", "total_operations", "added",
                          stats->n_purge_add);
    /* N old purges deleted       */
    varnish_submit_derive(conf->instance, "purge", "total_operations",
                          "deleted", stats->n_purge_retire);
    /* N objects tested           */
    varnish_submit_derive(conf->instance, "purge", "total_operations",
                          "objects_tested", stats->n_purge_obj_test);
    /* N regexps tested against   */
    varnish_submit_derive(conf->instance, "purge", "total_operations",
                          "regexps_tested", stats->n_purge_re_test);
    /* N duplicate purges removed */
    varnish_submit_derive(conf->instance, "purge", "total_operations",
                          "duplicate", stats->n_purge_dups);
  }

  if (conf->collect_session) {
    /* Session Closed     */
    varnish_submit_derive(conf->instance, "session", "total_operations",
                          "closed", stats->sess_closed);
    /* Session Pipeline   */
    varnish_submit_derive(conf->instance, "session", "total_operations",
                          "pipeline", stats->sess_pipeline);
    /* Session Read Ahead */
    varnish_submit_derive(conf->instance, "session", "total_operations",
                          "readahead", stats->sess_readahead);
    /* Session Linger     */
    varnish_submit_derive(conf->instance, "session", "total_operations",
                          "linger", stats->sess_linger);
    /* Session herd       */
    varnish_submit_derive(conf->instance, "session", "total_operations", "herd",
                          stats->sess_herd);
  }

  if (conf->collect_shm) {
    /* SHM records                 */
    varnish_submit_derive(conf->instance, "shm", "total_operations", "records",
                          stats->shm_records);
    /* SHM writes                  */
    varnish_submit_derive(conf->instance, "shm", "total_operations", "writes",
                          stats->shm_writes);
    /* SHM flushes due to overflow */
    varnish_submit_derive(conf->instance, "shm", "total_operations", "flushes",
                          stats->shm_flushes);
    /* SHM MTX contention          */
    varnish_submit_derive(conf->instance, "shm", "total_operations",
                          "contention", stats->shm_cont);
    /* SHM cycles through buffer   */
    varnish_submit_derive(conf->instance, "shm", "total_operations", "cycles",
                          stats->shm_cycles);
  }

  if (conf->collect_sm) {
    /* allocator requests */
    varnish_submit_derive(conf->instance, "sm", "total_requests", "nreq",
                          stats->sm_nreq);
    /* outstanding allocations */
    varnish_submit_gauge(conf->instance, "sm", "requests", "outstanding",
                         stats->sm_nobj);
    /* bytes allocated */
    varnish_submit_derive(conf->instance, "sm", "total_bytes", "allocated",
                          stats->sm_balloc);
    /* bytes free */
    varnish_submit_derive(conf->instance, "sm", "total_bytes", "free",
                          stats->sm_bfree);
  }

  if (conf->collect_sma) {
    /* SMA allocator requests */
    varnish_submit_derive(conf->instance, "sma", "total_requests", "nreq",
                          stats->sma_nreq);
    /* SMA outstanding allocations */
    varnish_submit_gauge(conf->instance, "sma", "requests", "outstanding",
                         stats->sma_nobj);
    /* SMA outstanding bytes */
    varnish_submit_gauge(conf->instance, "sma", "bytes", "outstanding",
                         stats->sma_nbytes);
    /* SMA bytes allocated */
    varnish_submit_derive(conf->instance, "sma", "total_bytes", "allocated",
                          stats->sma_balloc);
    /* SMA bytes free */
    varnish_submit_derive(conf->instance, "sma", "total_bytes", "free",
                          stats->sma_bfree);
  }

  if (conf->collect_sms) {
    /* SMS allocator requests */
    varnish_submit_derive(conf->instance, "sms", "total_requests", "allocator",
                          stats->sms_nreq);
    /* SMS outstanding allocations */
    varnish_submit_gauge(conf->instance, "sms", "requests", "outstanding",
                         stats->sms_nobj);
    /* SMS outstanding bytes */
    varnish_submit_gauge(conf->instance, "sms", "bytes", "outstanding",
                         stats->sms_nbytes);
    /* SMS bytes allocated */
    varnish_submit_derive(conf->instance, "sms", "total_bytes", "allocated",
                          stats->sms_balloc);
    /* SMS bytes freed */
    varnish_submit_derive(conf->instance, "sms", "total_bytes", "free",
                          stats->sms_bfree);
  }

  if (conf->collect_struct) {
    /* N struct sess_mem       */
    varnish_submit_gauge(conf->instance, "struct", "current_sessions",
                         "sess_mem", stats->n_sess_mem);
    /* N struct sess           */
    varnish_submit_gauge(conf->instance, "struct", "current_sessions", "sess",
                         stats->n_sess);
    /* N struct object         */
    varnish_submit_gauge(conf->instance, "struct", "objects", "object",
                         stats->n_object);
    /* N struct objecthead     */
    varnish_submit_gauge(conf->instance, "struct", "objects", "objecthead",
                         stats->n_objecthead);
    /* N struct smf            */
    varnish_submit_gauge(conf->instance, "struct", "objects", "smf",
                         stats->n_smf);
    /* N small free smf         */
    varnish_submit_gauge(conf->instance, "struct", "objects", "smf_frag",
                         stats->n_smf_frag);
    /* N large free smf         */
    varnish_submit_gauge(conf->instance, "struct", "objects", "smf_large",
                         stats->n_smf_large);
    /* N struct vbe_conn        */
    varnish_submit_gauge(conf->instance, "struct", "objects", "vbe_conn",
                         stats->n_vbe_conn);
  }

  if (conf->collect_totals) {
    /* Total Sessions */
    varnish_submit_derive(conf->instance, "totals", "total_sessions",
                          "sessions", stats->s_sess);
    /* Total Requests */
    varnish_submit_derive(conf->instance, "totals", "total_requests",
                          "requests", stats->s_req);
    /* Total pipe */
    varnish_submit_derive(conf->instance, "totals", "total_operations", "pipe",
                          stats->s_pipe);
    /* Total pass */
    varnish_submit_derive(conf->instance, "totals", "total_operations", "pass",
                          stats->s_pass);
    /* Total fetch */
    varnish_submit_derive(conf->instance, "totals", "total_operations",
                          "fetches", stats->s_fetch);
    /* Total header bytes */
    varnish_submit_derive(conf->instance, "totals", "total_bytes",
                          "header-bytes", stats->s_hdrbytes);
    /* Total body byte */
    varnish_submit_derive(conf->instance, "totals", "total_bytes", "body-bytes",
                          stats->s_bodybytes);
  }

  if (conf->collect_vcl) {
    /* N vcl total     */
    varnish_submit_gauge(conf->instance, "vcl", "vcl", "total_vcl",
                         stats->n_vcl);
    /* N vcl available */
    varnish_submit_gauge(conf->instance, "vcl", "vcl", "avail_vcl",
                         stats->n_vcl_avail);
    /* N vcl discarded */
    varnish_submit_gauge(conf->instance, "vcl", "vcl", "discarded_vcl",
                         stats->n_vcl_discard);
  }

  if (conf->collect_workers) {
    /* worker threads */
    varnish_submit_gauge(conf->instance, "workers", "threads", "worker",
                         stats->n_wrk);
    /* worker threads created */
    varnish_submit_derive(conf->instance, "workers", "total_threads", "created",
                          stats->n_wrk_create);
    /* worker threads not created */
    varnish_submit_derive(conf->instance, "workers", "total_threads", "failed",
                          stats->n_wrk_failed);
    /* worker threads limited */
    varnish_submit_derive(conf->instance, "workers", "total_threads", "limited",
                          stats->n_wrk_max);
    /* dropped work requests */
    varnish_submit_derive(conf->instance, "workers", "total_threads", "dropped",
                          stats->n_wrk_drop);
    /* queued work requests */
    varnish_submit_derive(conf->instance, "workers", "total_requests", "queued",
                          stats->n_wrk_queue);
    /* overflowed work requests */
    varnish_submit_derive(conf->instance, "workers", "total_requests",
                          "overflowed", stats->n_wrk_overflow);
  }

} /* }}} void varnish_monitor */
#endif

#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
static int varnish_read(user_data_t *ud) /* {{{ */
{
  struct VSM_data *vd;
  const c_varnish_stats_t *stats;
  _Bool ok;

  user_config_t *conf;

  if ((ud == NULL) || (ud->data == NULL))
    return (EINVAL);

  conf = ud->data;

  vd = VSM_New();
#if HAVE_VARNISH_V3
  VSC_Setup(vd);
#endif

  if (conf->instance != NULL) {
    int status;

    status = VSM_n_Arg(vd, conf->instance);
    if (status < 0) {
      VSM_Delete(vd);
      ERROR("varnish plugin: VSM_n_Arg (\"%s\") failed "
            "with status %i.",
            conf->instance, status);
      return (-1);
    }
  }

#if HAVE_VARNISH_V3
  ok = (VSC_Open(vd, /* diag = */ 1) == 0);
#else /* if HAVE_VARNISH_V4 */
  ok = (VSM_Open(vd) == 0);
#endif
  if (!ok) {
    VSM_Delete(vd);
    ERROR("varnish plugin: Unable to open connection.");

    return (-1);
  }

#if HAVE_VARNISH_V3
  stats = VSC_Main(vd);
#else /* if HAVE_VARNISH_V4 */
  stats = VSC_Main(vd, NULL);
#endif
  if (!stats) {
    VSM_Delete(vd);
    ERROR("varnish plugin: Unable to get statistics.");

    return (-1);
  }

#if HAVE_VARNISH_V3
  VSC_Iter(vd, varnish_monitor, conf);
#else /* if HAVE_VARNISH_V4 */
  VSC_Iter(vd, NULL, varnish_monitor, conf);
#endif
  VSM_Delete(vd);

  return (0);
} /* }}} */
#else /* if HAVE_VARNISH_V2 */
static int varnish_read(user_data_t *ud) /* {{{ */
{
  const c_varnish_stats_t *stats;

  user_config_t *conf;

  if ((ud == NULL) || (ud->data == NULL))
    return (EINVAL);

  conf = ud->data;

  stats = VSL_OpenStats(conf->instance);
  if (stats == NULL) {
    ERROR("Varnish plugin : unable to load statistics");

    return (-1);
  }

  varnish_monitor(conf, stats);

  return (0);
} /* }}} */
#endif

static void varnish_config_free(void *ptr) /* {{{ */
{
  user_config_t *conf = ptr;

  if (conf == NULL)
    return;

  sfree(conf->instance);
  sfree(conf);
} /* }}} */

static int varnish_config_apply_default(user_config_t *conf) /* {{{ */
{
  if (conf == NULL)
    return (EINVAL);

  conf->collect_backend = 1;
  conf->collect_cache = 1;
  conf->collect_connections = 1;
#ifdef HAVE_VARNISH_V3
  conf->collect_dirdns = 0;
#endif
  conf->collect_esi = 0;
  conf->collect_fetch = 0;
  conf->collect_hcb = 0;
  conf->collect_objects = 0;
#if HAVE_VARNISH_V2
  conf->collect_purge = 0;
#else
  conf->collect_ban = 0;
#endif
  conf->collect_session = 0;
  conf->collect_shm = 1;
#if HAVE_VARNISH_V2
  conf->collect_sm = 0;
  conf->collect_sma = 0;
#endif
  conf->collect_sms = 0;
  conf->collect_struct = 0;
  conf->collect_totals = 0;
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
  conf->collect_uptime = 0;
#endif
  conf->collect_vcl = 0;
  conf->collect_workers = 0;
#if HAVE_VARNISH_V4
  conf->collect_vsm = 0;
#endif

  return (0);
} /* }}} int varnish_config_apply_default */

static int varnish_init(void) /* {{{ */
{
  user_config_t *conf;

  if (have_instance)
    return (0);

  conf = calloc(1, sizeof(*conf));
  if (conf == NULL)
    return (ENOMEM);

  /* Default settings: */
  conf->instance = NULL;

  varnish_config_apply_default(conf);

  plugin_register_complex_read(
      /* group = */ "varnish",
      /* name      = */ "varnish/localhost",
      /* callback  = */ varnish_read,
      /* interval  = */ 0, &(user_data_t){
                               .data = conf, .free_func = varnish_config_free,
                           });

  return (0);
} /* }}} int varnish_init */

static int varnish_config_instance(const oconfig_item_t *ci) /* {{{ */
{
  user_config_t *conf;
  char callback_name[DATA_MAX_NAME_LEN];

  conf = calloc(1, sizeof(*conf));
  if (conf == NULL)
    return (ENOMEM);
  conf->instance = NULL;

  varnish_config_apply_default(conf);

  if (ci->values_num == 1) {
    int status;

    status = cf_util_get_string(ci, &conf->instance);
    if (status != 0) {
      sfree(conf);
      return (status);
    }
    assert(conf->instance != NULL);

    if (strcmp("localhost", conf->instance) == 0) {
      sfree(conf->instance);
      conf->instance = NULL;
    }
  } else if (ci->values_num > 1) {
    WARNING("Varnish plugin: \"Instance\" blocks accept only "
            "one argument.");
    sfree(conf);
    return (EINVAL);
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("CollectCache", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_cache);
    else if (strcasecmp("CollectConnections", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_connections);
    else if (strcasecmp("CollectESI", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_esi);
    else if (strcasecmp("CollectDirectorDNS", child->key) == 0)
#ifdef HAVE_VARNISH_V3
      cf_util_get_boolean(child, &conf->collect_dirdns);
#else
      WARNING("Varnish plugin: \"%s\" is available for Varnish %s only.",
              child->key, "v3");
#endif
    else if (strcasecmp("CollectBackend", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_backend);
    else if (strcasecmp("CollectFetch", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_fetch);
    else if (strcasecmp("CollectHCB", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_hcb);
    else if (strcasecmp("CollectObjects", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_objects);
    else if (strcasecmp("CollectPurge", child->key) == 0)
#if HAVE_VARNISH_V2
      cf_util_get_boolean(child, &conf->collect_purge);
#else
      WARNING("Varnish plugin: \"%s\" is available for Varnish %s only.",
              child->key, "v2");
#endif
    else if (strcasecmp("CollectBan", child->key) == 0)
#if HAVE_VARNISH_V2
      WARNING("Varnish plugin: \"%s\" is not available for Varnish %s.",
              child->key, "v2");
#else
      cf_util_get_boolean(child, &conf->collect_ban);
#endif
    else if (strcasecmp("CollectSession", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_session);
    else if (strcasecmp("CollectSHM", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_shm);
    else if (strcasecmp("CollectSMS", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_sms);
    else if (strcasecmp("CollectSMA", child->key) == 0)
#if HAVE_VARNISH_V2
      cf_util_get_boolean(child, &conf->collect_sma);
#else
      WARNING("Varnish plugin: \"%s\" is available for Varnish %s only.",
              child->key, "v2");
#endif
    else if (strcasecmp("CollectSM", child->key) == 0)
#if HAVE_VARNISH_V2
      cf_util_get_boolean(child, &conf->collect_sm);
#else
      WARNING("Varnish plugin: \"%s\" is available for Varnish %s only.",
              child->key, "v2");
#endif
    else if (strcasecmp("CollectStruct", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_struct);
    else if (strcasecmp("CollectTotals", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_totals);
    else if (strcasecmp("CollectUptime", child->key) == 0)
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
      cf_util_get_boolean(child, &conf->collect_uptime);
#else
      WARNING("Varnish plugin: \"%s\" is available for Varnish %s only.",
              child->key, "v3 and v4");
#endif
    else if (strcasecmp("CollectVCL", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_vcl);
    else if (strcasecmp("CollectWorkers", child->key) == 0)
      cf_util_get_boolean(child, &conf->collect_workers);
    else if (strcasecmp("CollectVSM", child->key) == 0)
#if HAVE_VARNISH_V4
      cf_util_get_boolean(child, &conf->collect_vsm);
#else
      WARNING("Varnish plugin: \"%s\" is available for Varnish %s only.",
              child->key, "v4");
#endif
    else {
      WARNING("Varnish plugin: Ignoring unknown "
              "configuration option: \"%s\". Did "
              "you forget to add an <Instance /> "
              "block around the configuration?",
              child->key);
    }
  }

  if (!conf->collect_cache && !conf->collect_connections &&
      !conf->collect_esi && !conf->collect_backend
#ifdef HAVE_VARNISH_V3
      && !conf->collect_dirdns
#endif
      && !conf->collect_fetch && !conf->collect_hcb && !conf->collect_objects
#if HAVE_VARNISH_V2
      && !conf->collect_purge
#else
      && !conf->collect_ban
#endif
      && !conf->collect_session && !conf->collect_shm && !conf->collect_sms
#if HAVE_VARNISH_V2
      && !conf->collect_sma && !conf->collect_sm
#endif
      && !conf->collect_struct && !conf->collect_totals
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
      && !conf->collect_uptime
#endif
      && !conf->collect_vcl && !conf->collect_workers
#if HAVE_VARNISH_V4
      && !conf->collect_vsm
#endif
      ) {
    WARNING("Varnish plugin: No metric has been configured for "
            "instance \"%s\". Disabling this instance.",
            (conf->instance == NULL) ? "localhost" : conf->instance);
    sfree(conf);
    return (EINVAL);
  }

  ssnprintf(callback_name, sizeof(callback_name), "varnish/%s",
            (conf->instance == NULL) ? "localhost" : conf->instance);

  plugin_register_complex_read(
      /* group = */ "varnish",
      /* name      = */ callback_name,
      /* callback  = */ varnish_read,
      /* interval  = */ 0, &(user_data_t){
                               .data = conf, .free_func = varnish_config_free,
                           });

  have_instance = 1;

  return (0);
} /* }}} int varnish_config_instance */

static int varnish_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Instance", child->key) == 0)
      varnish_config_instance(child);
    else {
      WARNING("Varnish plugin: Ignoring unknown "
              "configuration option: \"%s\"",
              child->key);
    }
  }

  return (0);
} /* }}} int varnish_config */

void module_register(void) /* {{{ */
{
  plugin_register_complex_config("varnish", varnish_config);
  plugin_register_init("varnish", varnish_init);
} /* }}} */

/* vim: set sw=8 noet fdm=marker : */
