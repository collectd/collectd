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
#include "configfile.h"

#if HAVE_VARNISH_V4
#include <varnish/vapi/vsm.h>
#include <varnish/vapi/vsc.h>
typedef struct VSC_C_main c_varnish_stats_t;
#endif

#if HAVE_VARNISH_V3
#include <varnish/varnishapi.h>
#include <varnish/vsc.h>
typedef struct VSC_C_main c_varnish_stats_t;
#endif

#if HAVE_VARNISH_V2
#include <varnish/varnishapi.h>
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

static int varnish_submit (const char *plugin_instance, /* {{{ */
		const char *category, const char *type, const char *type_instance, value_t value)
{
	value_list_t vl = VALUE_LIST_INIT;

	vl.values = &value;
	vl.values_len = 1;

	sstrncpy (vl.host, hostname_g, sizeof (vl.host));

	sstrncpy (vl.plugin, "varnish", sizeof (vl.plugin));

	if (plugin_instance == NULL)
		plugin_instance = "default";

	ssnprintf (vl.plugin_instance, sizeof (vl.plugin_instance),
		"%s-%s", plugin_instance, category);

	sstrncpy (vl.type, type, sizeof (vl.type));

	if (type_instance != NULL)
		sstrncpy (vl.type_instance, type_instance,
				sizeof (vl.type_instance));

	return (plugin_dispatch_values (&vl));
} /* }}} int varnish_submit */

static int varnish_submit_gauge (const char *plugin_instance, /* {{{ */
		const char *category, const char *type, const char *type_instance,
		uint64_t gauge_value)
{
	value_t value;

	value.gauge = (gauge_t) gauge_value;

	return (varnish_submit (plugin_instance, category, type, type_instance, value));
} /* }}} int varnish_submit_gauge */

static int varnish_submit_derive (const char *plugin_instance, /* {{{ */
		const char *category, const char *type, const char *type_instance,
		uint64_t derive_value)
{
	value_t value;

	value.derive = (derive_t) derive_value;

	return (varnish_submit (plugin_instance, category, type, type_instance, value));
} /* }}} int varnish_submit_derive */

static void varnish_monitor (const user_config_t *conf, /* {{{ */
		const c_varnish_stats_t *stats)
{
	if (conf->collect_cache)
	{
		/* Cache hits */
		varnish_submit_derive (conf->instance, "cache", "cache_result", "hit",     stats->cache_hit);
		/* Cache misses */
		varnish_submit_derive (conf->instance, "cache", "cache_result", "miss",    stats->cache_miss);
		/* Cache hits for pass */
		varnish_submit_derive (conf->instance, "cache", "cache_result", "hitpass", stats->cache_hitpass);
	}

	if (conf->collect_connections)
	{
#ifndef HAVE_VARNISH_V4
		/* Client connections accepted */
		varnish_submit_derive (conf->instance, "connections", "connections", "accepted", stats->client_conn);
		/* Connection dropped, no sess */
		varnish_submit_derive (conf->instance, "connections", "connections", "dropped" , stats->client_drop);
#endif
		/* Client requests received    */
		varnish_submit_derive (conf->instance, "connections", "connections", "received", stats->client_req);
	}

#ifdef HAVE_VARNISH_V3
	if (conf->collect_dirdns)
	{
		/* DNS director lookups */
		varnish_submit_derive (conf->instance, "dirdns", "cache_operation", "lookups",    stats->dir_dns_lookups);
		/* DNS director failed lookups */
		varnish_submit_derive (conf->instance, "dirdns", "cache_result",    "failed",     stats->dir_dns_failed);
		/* DNS director cached lookups hit */
		varnish_submit_derive (conf->instance, "dirdns", "cache_result",    "hits",       stats->dir_dns_hit);
		/* DNS director full dnscache */
		varnish_submit_derive (conf->instance, "dirdns", "cache_result",    "cache_full", stats->dir_dns_cache_full);
	}
#endif

	if (conf->collect_esi)
	{
		/* ESI parse errors (unlock)   */
		varnish_submit_derive (conf->instance, "esi", "total_operations", "error",   stats->esi_errors);
#if HAVE_VARNISH_V2
		/* Objects ESI parsed (unlock) */
		varnish_submit_derive (conf->instance, "esi", "total_operations", "parsed",  stats->esi_parse);
#else
		/* ESI parse warnings (unlock) */
		varnish_submit_derive (conf->instance, "esi", "total_operations", "warning", stats->esi_warnings);
#endif
	}

	if (conf->collect_backend)
	{
		/* Backend conn. success       */
		varnish_submit_derive (conf->instance, "backend", "connections", "success"      , stats->backend_conn);
		/* Backend conn. not attempted */
		varnish_submit_derive (conf->instance, "backend", "connections", "not-attempted", stats->backend_unhealthy);
		/* Backend conn. too many      */
		varnish_submit_derive (conf->instance, "backend", "connections", "too-many"     , stats->backend_busy);
		/* Backend conn. failures      */
		varnish_submit_derive (conf->instance, "backend", "connections", "failures"     , stats->backend_fail);
		/* Backend conn. reuses        */
		varnish_submit_derive (conf->instance, "backend", "connections", "reuses"       , stats->backend_reuse);
		/* Backend conn. was closed    */
		varnish_submit_derive (conf->instance, "backend", "connections", "was-closed"   , stats->backend_toolate);
		/* Backend conn. recycles      */
		varnish_submit_derive (conf->instance, "backend", "connections", "recycled"     , stats->backend_recycle);
#if HAVE_VARNISH_V2
		/* Backend conn. unused        */
		varnish_submit_derive (conf->instance, "backend", "connections", "unused"       , stats->backend_unused);
#else
		/* Backend conn. retry         */
		varnish_submit_derive (conf->instance, "backend", "connections", "retries"      , stats->backend_retry);
#endif
		/* Backend requests mades      */
		varnish_submit_derive (conf->instance, "backend", "http_requests", "requests"   , stats->backend_req);
		/* N backends                  */
		varnish_submit_gauge  (conf->instance, "backend", "backends", "n_backends"      , stats->n_backend);
	}

	if (conf->collect_fetch)
	{
		/* Fetch head                */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "head"       , stats->fetch_head);
		/* Fetch with length         */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "length"     , stats->fetch_length);
		/* Fetch chunked             */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "chunked"    , stats->fetch_chunked);
		/* Fetch EOF                 */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "eof"        , stats->fetch_eof);
		/* Fetch bad headers         */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "bad_headers", stats->fetch_bad);
		/* Fetch wanted close        */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "close"      , stats->fetch_close);
		/* Fetch pre HTTP/1.1 closed */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "oldhttp"    , stats->fetch_oldhttp);
		/* Fetch zero len            */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "zero"       , stats->fetch_zero);
		/* Fetch failed              */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "failed"     , stats->fetch_failed);
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
		/* Fetch no body (1xx)       */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "no_body_1xx", stats->fetch_1xx);
		/* Fetch no body (204)       */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "no_body_204", stats->fetch_204);
		/* Fetch no body (304)       */
		varnish_submit_derive (conf->instance, "fetch", "http_requests", "no_body_304", stats->fetch_304);
#endif
	}

	if (conf->collect_hcb)
	{
		/* HCB Lookups without lock */
		varnish_submit_derive (conf->instance, "hcb", "cache_operation", "lookup_nolock", stats->hcb_nolock);
		/* HCB Lookups with lock    */
		varnish_submit_derive (conf->instance, "hcb", "cache_operation", "lookup_lock",   stats->hcb_lock);
		/* HCB Inserts              */
		varnish_submit_derive (conf->instance, "hcb", "cache_operation", "insert",        stats->hcb_insert);
	}

	if (conf->collect_objects)
	{
		/* N expired objects             */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "expired",            stats->n_expired);
		/* N LRU nuked objects           */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "lru_nuked",          stats->n_lru_nuked);
#if HAVE_VARNISH_V2
		/* N LRU saved objects           */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "lru_saved",          stats->n_lru_saved);
#endif
		/* N LRU moved objects           */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "lru_moved",          stats->n_lru_moved);
#if HAVE_VARNISH_V2
		/* N objects on deathrow         */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "deathrow",           stats->n_deathrow);
#endif
		/* HTTP header overflows         */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "header_overflow",    stats->losthdr);
#if HAVE_VARNISH_V4
		/* N purged objects              */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "purged",             stats->n_obj_purged);
#else
		/* Objects sent with sendfile    */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "sent_sendfile",      stats->n_objsendfile);
		/* Objects sent with write       */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "sent_write",         stats->n_objwrite);
		/* Objects overflowing workspace */
		varnish_submit_derive (conf->instance, "objects", "total_objects", "workspace_overflow", stats->n_objoverflow);
#endif
	}

#if HAVE_VARNISH_V2
	if (conf->collect_purge)
	{
		/* N total active purges      */
		varnish_submit_derive (conf->instance, "purge", "total_operations", "total",            stats->n_purge);
		/* N new purges added         */
		varnish_submit_derive (conf->instance, "purge", "total_operations", "added",            stats->n_purge_add);
		/* N old purges deleted       */
		varnish_submit_derive (conf->instance, "purge", "total_operations", "deleted",          stats->n_purge_retire);
		/* N objects tested           */
		varnish_submit_derive (conf->instance, "purge", "total_operations", "objects_tested",   stats->n_purge_obj_test);
		/* N regexps tested against   */
		varnish_submit_derive (conf->instance, "purge", "total_operations", "regexps_tested",   stats->n_purge_re_test);
		/* N duplicate purges removed */
		varnish_submit_derive (conf->instance, "purge", "total_operations", "duplicate",        stats->n_purge_dups);
	}
#endif
#if HAVE_VARNISH_V3
	if (conf->collect_ban)
	{
		/* N total active bans      */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "total",          stats->n_ban);
		/* N new bans added         */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "added",          stats->n_ban_add);
		/* N old bans deleted       */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "deleted",        stats->n_ban_retire);
		/* N objects tested         */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "objects_tested", stats->n_ban_obj_test);
		/* N regexps tested against */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "regexps_tested", stats->n_ban_re_test);
		/* N duplicate bans removed */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "duplicate",      stats->n_ban_dups);
	}
#endif
#if HAVE_VARNISH_V4
	if (conf->collect_ban)
	{
		/* N total active bans      */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "total",          stats->bans);
		/* N new bans added         */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "added",          stats->bans_added);
		/* N bans using obj */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "obj",            stats->bans_obj);
		/* N bans using req */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "req",            stats->bans_req);
		/* N new bans completed     */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "completed",      stats->bans_completed);
		/* N old bans deleted       */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "deleted",        stats->bans_deleted);
		/* N objects tested         */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "tested",         stats->bans_tested);
		/* N duplicate bans removed */
		varnish_submit_derive (conf->instance, "ban", "total_operations", "duplicate",      stats->bans_dups);
	}
#endif

	if (conf->collect_session)
	{
		/* Session Closed     */
		varnish_submit_derive (conf->instance, "session", "total_operations", "closed",    stats->sess_closed);
		/* Session Pipeline   */
		varnish_submit_derive (conf->instance, "session", "total_operations", "pipeline",  stats->sess_pipeline);
		/* Session Read Ahead */
		varnish_submit_derive (conf->instance, "session", "total_operations", "readahead", stats->sess_readahead);
#if HAVE_VARNISH_V4
		/* Sessions accepted */
		varnish_submit_derive (conf->instance, "session", "total_operations", "accepted",  stats->sess_conn);
		/* Sessions dropped for thread */
		varnish_submit_derive (conf->instance, "session", "total_operations", "dropped",   stats->sess_drop);
		/* Sessions accept failure */
		varnish_submit_derive (conf->instance, "session", "total_operations", "failed",    stats->sess_fail);
		/* Sessions pipe overflow */
		varnish_submit_derive (conf->instance, "session", "total_operations", "overflow",  stats->sess_pipe_overflow);
		/* Sessions queued for thread */
		varnish_submit_derive (conf->instance, "session", "total_operations", "queued",    stats->sess_queued);
#else
		/* Session Linger     */
		varnish_submit_derive (conf->instance, "session", "total_operations", "linger",    stats->sess_linger);
#endif
		/* Session herd       */
		varnish_submit_derive (conf->instance, "session", "total_operations", "herd",      stats->sess_herd);
	}

	if (conf->collect_shm)
	{
		/* SHM records                 */
		varnish_submit_derive (conf->instance, "shm", "total_operations", "records"   , stats->shm_records);
		/* SHM writes                  */
		varnish_submit_derive (conf->instance, "shm", "total_operations", "writes"    , stats->shm_writes);
		/* SHM flushes due to overflow */
		varnish_submit_derive (conf->instance, "shm", "total_operations", "flushes"   , stats->shm_flushes);
		/* SHM MTX contention          */
		varnish_submit_derive (conf->instance, "shm", "total_operations", "contention", stats->shm_cont);
		/* SHM cycles through buffer   */
		varnish_submit_derive (conf->instance, "shm", "total_operations", "cycles"    , stats->shm_cycles);
	}

#if HAVE_VARNISH_V2
	if (conf->collect_sm)
	{
		/* allocator requests */
		varnish_submit_derive (conf->instance, "sm", "total_requests", "nreq",         stats->sm_nreq);
		/* outstanding allocations */
		varnish_submit_gauge (conf->instance,  "sm", "requests", "outstanding",        stats->sm_nobj);
		/* bytes allocated */
		varnish_submit_derive (conf->instance,  "sm", "total_bytes", "allocated",      stats->sm_balloc);
		/* bytes free */
		varnish_submit_derive (conf->instance,  "sm", "total_bytes", "free",           stats->sm_bfree);
	}

	if (conf->collect_sma)
	{
		/* SMA allocator requests */
		varnish_submit_derive (conf->instance, "sma", "total_requests", "nreq",    stats->sma_nreq);
		/* SMA outstanding allocations */
		varnish_submit_gauge (conf->instance,  "sma", "requests", "outstanding",   stats->sma_nobj);
		/* SMA outstanding bytes */
		varnish_submit_gauge (conf->instance,  "sma", "bytes", "outstanding",      stats->sma_nbytes);
		/* SMA bytes allocated */
		varnish_submit_derive (conf->instance,  "sma", "total_bytes", "allocated", stats->sma_balloc);
		/* SMA bytes free */
		varnish_submit_derive (conf->instance,  "sma", "total_bytes", "free" ,     stats->sma_bfree);
	}
#endif

	if (conf->collect_sms)
	{
		/* SMS allocator requests */
		varnish_submit_derive (conf->instance, "sms", "total_requests", "allocator", stats->sms_nreq);
		/* SMS outstanding allocations */
		varnish_submit_gauge (conf->instance,  "sms", "requests", "outstanding",     stats->sms_nobj);
		/* SMS outstanding bytes */
		varnish_submit_gauge (conf->instance,  "sms", "bytes", "outstanding",        stats->sms_nbytes);
		/* SMS bytes allocated */
		varnish_submit_derive (conf->instance,  "sms", "total_bytes", "allocated",   stats->sms_balloc);
		/* SMS bytes freed */
		varnish_submit_derive (conf->instance,  "sms", "total_bytes", "free",        stats->sms_bfree);
	}

	if (conf->collect_struct)
	{
#if !HAVE_VARNISH_V4
		/* N struct sess_mem       */
		varnish_submit_gauge (conf->instance, "struct", "current_sessions", "sess_mem",  stats->n_sess_mem);
		/* N struct sess           */
		varnish_submit_gauge (conf->instance, "struct", "current_sessions", "sess",      stats->n_sess);
#endif
		/* N struct object         */
		varnish_submit_gauge (conf->instance, "struct", "objects", "object",             stats->n_object);
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
		/* N unresurrected objects */
		varnish_submit_gauge (conf->instance, "struct", "objects", "vampireobject",      stats->n_vampireobject);
		/* N struct objectcore     */
		varnish_submit_gauge (conf->instance, "struct", "objects", "objectcore",         stats->n_objectcore);
		/* N struct waitinglist    */
		varnish_submit_gauge (conf->instance, "struct", "objects", "waitinglist",        stats->n_waitinglist);
#endif
		/* N struct objecthead     */
		varnish_submit_gauge (conf->instance, "struct", "objects", "objecthead",         stats->n_objecthead);
#ifdef HAVE_VARNISH_V2
		/* N struct smf            */
		varnish_submit_gauge (conf->instance, "struct", "objects", "smf",                stats->n_smf);
		/* N small free smf         */
		varnish_submit_gauge (conf->instance, "struct", "objects", "smf_frag",           stats->n_smf_frag);
		/* N large free smf         */
		varnish_submit_gauge (conf->instance, "struct", "objects", "smf_large",          stats->n_smf_large);
		/* N struct vbe_conn        */
		varnish_submit_gauge (conf->instance, "struct", "objects", "vbe_conn",           stats->n_vbe_conn);
#endif
	}

	if (conf->collect_totals)
	{
		/* Total Sessions */
		varnish_submit_derive (conf->instance, "totals", "total_sessions", "sessions",  stats->s_sess);
		/* Total Requests */
		varnish_submit_derive (conf->instance, "totals", "total_requests", "requests",  stats->s_req);
		/* Total pipe */
		varnish_submit_derive (conf->instance, "totals", "total_operations", "pipe",    stats->s_pipe);
		/* Total pass */
		varnish_submit_derive (conf->instance, "totals", "total_operations", "pass",    stats->s_pass);
		/* Total fetch */
		varnish_submit_derive (conf->instance, "totals", "total_operations", "fetches", stats->s_fetch);
#if HAVE_VARNISH_V4
		/* Total synth */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "synth",       stats->s_synth);
		/* Request header bytes */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "req_header",  stats->s_req_hdrbytes);
		/* Request body byte */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "req_body",    stats->s_req_bodybytes);
		/* Response header bytes */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "resp_header", stats->s_resp_hdrbytes);
		/* Response body byte */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "resp_body",   stats->s_resp_bodybytes);
		/* Pipe request header bytes */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "pipe_header", stats->s_pipe_hdrbytes);
		/* Piped bytes from client */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "pipe_in",     stats->s_pipe_in);
		/* Piped bytes to client */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "pipe_out",    stats->s_pipe_out);
		/* Number of purge operations */
		varnish_submit_derive (conf->instance, "totals", "total_operations", "purges", stats->n_purges);
#else
		/* Total header bytes */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "header-bytes", stats->s_hdrbytes);
		/* Total body byte */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "body-bytes",   stats->s_bodybytes);
#endif
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
		/* Gzip operations */
		varnish_submit_derive (conf->instance, "totals", "total_operations", "gzip",    stats->n_gzip);
		/* Gunzip operations */
		varnish_submit_derive (conf->instance, "totals", "total_operations", "gunzip",  stats->n_gunzip);
#endif
	}

#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
	if (conf->collect_uptime)
	{
		/* Client uptime */
		varnish_submit_gauge (conf->instance, "uptime", "uptime", "client_uptime", stats->uptime);
	}
#endif

	if (conf->collect_vcl)
	{
		/* N vcl total     */
		varnish_submit_gauge (conf->instance, "vcl", "vcl", "total_vcl",     stats->n_vcl);
		/* N vcl available */
		varnish_submit_gauge (conf->instance, "vcl", "vcl", "avail_vcl",     stats->n_vcl_avail);
		/* N vcl discarded */
		varnish_submit_gauge (conf->instance, "vcl", "vcl", "discarded_vcl", stats->n_vcl_discard);
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
		/* Loaded VMODs */
		varnish_submit_gauge (conf->instance, "vcl", "objects", "vmod",      stats->vmods);
#endif
	}

	if (conf->collect_workers)
	{
#ifdef HAVE_VARNISH_V4
		/* total number of threads */
		varnish_submit_gauge (conf->instance, "workers", "threads", "worker",             stats->threads);
		/* threads created */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "created",     stats->threads_created);
		/* thread creation failed */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "failed",      stats->threads_failed);
		/* threads hit max */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "limited",     stats->threads_limited);
		/* threads destroyed */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "dropped",     stats->threads_destroyed);
		/* length of session queue */
		varnish_submit_derive (conf->instance, "workers", "queue_length",  "threads",     stats->thread_queue_len);
#else
		/* worker threads */
		varnish_submit_gauge (conf->instance, "workers", "threads", "worker",             stats->n_wrk);
		/* worker threads created */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "created",     stats->n_wrk_create);
		/* worker threads not created */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "failed",      stats->n_wrk_failed);
		/* worker threads limited */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "limited",     stats->n_wrk_max);
		/* dropped work requests */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "dropped",     stats->n_wrk_drop);
#ifdef HAVE_VARNISH_V2
		/* queued work requests */
		varnish_submit_derive (conf->instance, "workers", "total_requests", "queued",     stats->n_wrk_queue);
		/* overflowed work requests */
		varnish_submit_derive (conf->instance, "workers", "total_requests", "overflowed", stats->n_wrk_overflow);
#else /* HAVE_VARNISH_V3 */
		/* queued work requests */
		varnish_submit_derive (conf->instance, "workers", "total_requests", "queued",       stats->n_wrk_queued);
		/* work request queue length */
		varnish_submit_derive (conf->instance, "workers", "total_requests", "queue_length", stats->n_wrk_lqueue);
#endif
#endif
	}

#if HAVE_VARNISH_V4
	if (conf->collect_vsm)
	{
		/* Free VSM space */
		varnish_submit_gauge (conf->instance, "vsm", "bytes", "free",              stats->vsm_free);
		/* Used VSM space */
		varnish_submit_gauge (conf->instance, "vsm", "bytes", "used",              stats->vsm_used);
		/* Cooling VSM space */
		varnish_submit_gauge (conf->instance, "vsm", "bytes", "cooling",           stats->vsm_cooling);
		/* Overflow VSM space */
		varnish_submit_gauge (conf->instance, "vsm", "bytes", "overflow",          stats->vsm_overflow);
		/* Total overflowed VSM space */
		varnish_submit_derive (conf->instance, "vsm", "total_bytes", "overflowed", stats->vsm_overflowed);
	}
#endif

} /* }}} void varnish_monitor */

#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
static int varnish_read (user_data_t *ud) /* {{{ */
{
	struct VSM_data *vd;
	const c_varnish_stats_t *stats;

	user_config_t *conf;

	if ((ud == NULL) || (ud->data == NULL))
		return (EINVAL);

	conf = ud->data;

	vd = VSM_New();
#if HAVE_VARNISH_V3
	VSC_Setup(vd);
#endif

	if (conf->instance != NULL)
	{
		int status;

		status = VSM_n_Arg (vd, conf->instance);
		if (status < 0)
		{
			ERROR ("varnish plugin: VSM_n_Arg (\"%s\") failed "
					"with status %i.",
					conf->instance, status);
			return (-1);
		}
	}

#if HAVE_VARNISH_V3
	if (VSC_Open (vd, /* diag = */ 1))
#else /* if HAVE_VARNISH_V4 */
	if (VSM_Open (vd))
#endif
	{
		ERROR ("varnish plugin: Unable to load statistics.");

		return (-1);
	}

#if HAVE_VARNISH_V3
	stats = VSC_Main(vd);
#else /* if HAVE_VARNISH_V4 */
	stats = VSC_Main(vd, NULL);
#endif

	varnish_monitor (conf, stats);
	VSM_Close (vd);

	return (0);
} /* }}} */
#else /* if HAVE_VARNISH_V2 */
static int varnish_read (user_data_t *ud) /* {{{ */
{
	const c_varnish_stats_t *stats;

	user_config_t *conf;

	if ((ud == NULL) || (ud->data == NULL))
		return (EINVAL);

	conf = ud->data;

	stats = VSL_OpenStats (conf->instance);
	if (stats == NULL)
	{
		ERROR ("Varnish plugin : unable to load statistics");

		return (-1);
	}

	varnish_monitor (conf, stats);

	return (0);
} /* }}} */
#endif

static void varnish_config_free (void *ptr) /* {{{ */
{
	user_config_t *conf = ptr;

	if (conf == NULL)
		return;

	sfree (conf->instance);
	sfree (conf);
} /* }}} */

static int varnish_config_apply_default (user_config_t *conf) /* {{{ */
{
	if (conf == NULL)
		return (EINVAL);

	conf->collect_backend     = 1;
	conf->collect_cache       = 1;
	conf->collect_connections = 1;
#ifdef HAVE_VARNISH_V3
	conf->collect_dirdns      = 0;
#endif
	conf->collect_esi         = 0;
	conf->collect_fetch       = 0;
	conf->collect_hcb         = 0;
	conf->collect_objects     = 0;
#if HAVE_VARNISH_V2
	conf->collect_purge       = 0;
#else
	conf->collect_ban         = 0;
#endif
	conf->collect_session     = 0;
	conf->collect_shm         = 1;
#if HAVE_VARNISH_V2
	conf->collect_sm          = 0;
	conf->collect_sma         = 0;
#endif
	conf->collect_sms         = 0;
	conf->collect_struct      = 0;
	conf->collect_totals      = 0;
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
	conf->collect_uptime      = 0;
#endif
	conf->collect_vcl         = 0;
	conf->collect_workers     = 0;
#if HAVE_VARNISH_V4
	conf->collect_vsm         = 0;
#endif

	return (0);
} /* }}} int varnish_config_apply_default */

static int varnish_init (void) /* {{{ */
{
	user_config_t *conf;
	user_data_t ud;

	if (have_instance)
		return (0);

	conf = malloc (sizeof (*conf));
	if (conf == NULL)
		return (ENOMEM);
	memset (conf, 0, sizeof (*conf));

	/* Default settings: */
	conf->instance = NULL;

	varnish_config_apply_default (conf);

	ud.data = conf;
	ud.free_func = varnish_config_free;

	plugin_register_complex_read (/* group = */ "varnish",
			/* name      = */ "varnish/localhost",
			/* callback  = */ varnish_read,
			/* interval  = */ NULL,
			/* user data = */ &ud);

	return (0);
} /* }}} int varnish_init */

static int varnish_config_instance (const oconfig_item_t *ci) /* {{{ */
{
	user_config_t *conf;
	user_data_t ud;
	char callback_name[DATA_MAX_NAME_LEN];
	int i;

	conf = malloc (sizeof (*conf));
	if (conf == NULL)
		return (ENOMEM);
	memset (conf, 0, sizeof (*conf));
	conf->instance = NULL;

	varnish_config_apply_default (conf);

	if (ci->values_num == 1)
	{
		int status;

		status = cf_util_get_string (ci, &conf->instance);
		if (status != 0)
		{
			sfree (conf);
			return (status);
		}
		assert (conf->instance != NULL);

		if (strcmp ("localhost", conf->instance) == 0)
		{
			sfree (conf->instance);
			conf->instance = NULL;
		}
	}
	else if (ci->values_num > 1)
	{
		WARNING ("Varnish plugin: \"Instance\" blocks accept only "
				"one argument.");
		return (EINVAL);
	}

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("CollectCache", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_cache);
		else if (strcasecmp ("CollectConnections", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_connections);
		else if (strcasecmp ("CollectESI", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_esi);
#ifdef HAVE_VARNISH_V3
		else if (strcasecmp ("CollectDirectorDNS", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_dirdns);
#endif
		else if (strcasecmp ("CollectBackend", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_backend);
		else if (strcasecmp ("CollectFetch", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_fetch);
		else if (strcasecmp ("CollectHCB", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_hcb);
		else if (strcasecmp ("CollectObjects", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_objects);
#if HAVE_VARNISH_V2
		else if (strcasecmp ("CollectPurge", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_purge);
#else
		else if (strcasecmp ("CollectBan", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_ban);
#endif
		else if (strcasecmp ("CollectSession", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_session);
		else if (strcasecmp ("CollectSHM", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_shm);
		else if (strcasecmp ("CollectSMS", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_sms);
#if HAVE_VARNISH_V2
		else if (strcasecmp ("CollectSMA", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_sma);
		else if (strcasecmp ("CollectSM", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_sm);
#endif
		else if (strcasecmp ("CollectStruct", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_struct);
		else if (strcasecmp ("CollectTotals", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_totals);
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
		else if (strcasecmp ("CollectUptime", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_uptime);
#endif
		else if (strcasecmp ("CollectVCL", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_vcl);
		else if (strcasecmp ("CollectWorkers", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_workers);
#if HAVE_VARNISH_V4
		else if (strcasecmp ("CollectVSM", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_vsm);
#endif
		else
		{
			WARNING ("Varnish plugin: Ignoring unknown "
					"configuration option: \"%s\". Did "
					"you forget to add an <Instance /> "
					"block around the configuration?",
					child->key);
		}
	}

	if (!conf->collect_cache
			&& !conf->collect_connections
			&& !conf->collect_esi
			&& !conf->collect_backend
#ifdef HAVE_VARNISH_V3
			&& !conf->collect_dirdns
#endif
			&& !conf->collect_fetch
			&& !conf->collect_hcb
			&& !conf->collect_objects
#if HAVE_VARNISH_V2
			&& !conf->collect_purge
#else
			&& !conf->collect_ban
#endif
			&& !conf->collect_session
			&& !conf->collect_shm
			&& !conf->collect_sms
#if HAVE_VARNISH_V2
			&& !conf->collect_sma
			&& !conf->collect_sm
#endif
			&& !conf->collect_struct
			&& !conf->collect_totals
#if HAVE_VARNISH_V3 || HAVE_VARNISH_V4
			&& !conf->collect_uptime
#endif
			&& !conf->collect_vcl
			&& !conf->collect_workers
#if HAVE_VARNISH_V4
			&& !conf->collect_vsm
#endif
	)
	{
		WARNING ("Varnish plugin: No metric has been configured for "
				"instance \"%s\". Disabling this instance.",
				(conf->instance == NULL) ? "localhost" : conf->instance);
		return (EINVAL);
	}

	ssnprintf (callback_name, sizeof (callback_name), "varnish/%s",
			(conf->instance == NULL) ? "localhost" : conf->instance);

	ud.data = conf;
	ud.free_func = varnish_config_free;

	plugin_register_complex_read (/* group = */ "varnish",
			/* name      = */ callback_name,
			/* callback  = */ varnish_read,
			/* interval  = */ NULL,
			/* user data = */ &ud);

	have_instance = 1;

	return (0);
} /* }}} int varnish_config_instance */

static int varnish_config (oconfig_item_t *ci) /* {{{ */
{
	int i;

	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if (strcasecmp ("Instance", child->key) == 0)
			varnish_config_instance (child);
		else
		{
			WARNING ("Varnish plugin: Ignoring unknown "
					"configuration option: \"%s\"",
					child->key);
		}
	}

	return (0);
} /* }}} int varnish_config */

void module_register (void) /* {{{ */
{
	plugin_register_complex_config ("varnish", varnish_config);
	plugin_register_init ("varnish", varnish_init);
} /* }}} */

/* vim: set sw=8 noet fdm=marker : */
