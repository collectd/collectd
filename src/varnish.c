/**
 * collectd - src/varnish.c
 * Copyright (C) 2010 Jérôme Renard
 * Copyright (C) 2010 Marc Fournier
 * Copyright (C) 2010 Florian Forster
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
 *   Florian octo Forster <octo at verplant.org>
 **/

/**
 * Current list of what is monitored and what is not monitored (yet)
 * {{{
 * Field name           Description                           Monitored
 * ----------           -----------                           ---------
 * uptime               Child uptime                              N
 * client_conn          Client connections accepted               Y
 * client_drop          Connection dropped, no sess               Y
 * client_req           Client requests received                  Y
 * cache_hit            Cache hits                                Y
 * cache_hitpass        Cache hits for pass                       Y
 * cache_miss           Cache misses                              Y
 * backend_conn         Backend conn. success                     Y
 * backend_unhealthy    Backend conn. not attempted               Y
 * backend_busy         Backend conn. too many                    Y
 * backend_fail         Backend conn. failures                    Y
 * backend_reuse        Backend conn. reuses                      Y
 * backend_toolate      Backend conn. was closed                  Y
 * backend_recycle      Backend conn. recycles                    Y
 * backend_unused       Backend conn. unused                      Y
 * fetch_head           Fetch head                                Y
 * fetch_length         Fetch with Length                         Y
 * fetch_chunked        Fetch chunked                             Y
 * fetch_eof            Fetch EOF                                 Y
 * fetch_bad            Fetch had bad headers                     Y
 * fetch_close          Fetch wanted close                        Y
 * fetch_oldhttp        Fetch pre HTTP/1.1 closed                 Y
 * fetch_zero           Fetch zero len                            Y
 * fetch_failed         Fetch failed                              Y
 * n_sess_mem           N struct sess_mem                         N
 * n_sess               N struct sess                             N
 * n_object             N struct object                           N
 * n_vampireobject      N unresurrected objects                   N
 * n_objectcore         N struct objectcore                       N
 * n_objecthead         N struct objecthead                       N
 * n_smf                N struct smf                              N
 * n_smf_frag           N small free smf                          N
 * n_smf_large          N large free smf                          N
 * n_vbe_conn           N struct vbe_conn                         N
 * n_wrk                N worker threads                          Y
 * n_wrk_create         N worker threads created                  Y
 * n_wrk_failed         N worker threads not created              Y
 * n_wrk_max            N worker threads limited                  Y
 * n_wrk_queue          N queued work requests                    Y
 * n_wrk_overflow       N overflowed work requests                Y
 * n_wrk_drop           N dropped work requests                   Y
 * n_backend            N backends                                N
 * n_expired            N expired objects                         N
 * n_lru_nuked          N LRU nuked objects                       N
 * n_lru_saved          N LRU saved objects                       N
 * n_lru_moved          N LRU moved objects                       N
 * n_deathrow           N objects on deathrow                     N
 * losthdr              HTTP header overflows                     N
 * n_objsendfile        Objects sent with sendfile                N
 * n_objwrite           Objects sent with write                   N
 * n_objoverflow        Objects overflowing workspace             N
 * s_sess               Total Sessions                            Y
 * s_req                Total Requests                            Y
 * s_pipe               Total pipe                                Y
 * s_pass               Total pass                                Y
 * s_fetch              Total fetch                               Y
 * s_hdrbytes           Total header bytes                        Y
 * s_bodybytes          Total body bytes                          Y
 * sess_closed          Session Closed                            N
 * sess_pipeline        Session Pipeline                          N
 * sess_readahead       Session Read Ahead                        N
 * sess_linger          Session Linger                            N
 * sess_herd            Session herd                              N
 * shm_records          SHM records                               Y
 * shm_writes           SHM writes                                Y
 * shm_flushes          SHM flushes due to overflow               Y
 * shm_cont             SHM MTX contention                        Y
 * shm_cycles           SHM cycles through buffer                 Y
 * sm_nreq              allocator requests                        Y
 * sm_nobj              outstanding allocations                   Y
 * sm_balloc            bytes allocated                           Y
 * sm_bfree             bytes free                                Y
 * sma_nreq             SMA allocator requests                    Y
 * sma_nobj             SMA outstanding allocations               Y
 * sma_nbytes           SMA outstanding bytes                     Y
 * sma_balloc           SMA bytes allocated                       Y
 * sma_bfree            SMA bytes free                            Y
 * sms_nreq             SMS allocator requests                    Y
 * sms_nobj             SMS outstanding allocations               Y
 * sms_nbytes           SMS outstanding bytes                     Y
 * sms_balloc           SMS bytes allocated                       Y
 * sms_bfree            SMS bytes freed                           Y
 * backend_req          Backend requests made                     N
 * n_vcl                N vcl total                               N
 * n_vcl_avail          N vcl available                           N
 * n_vcl_discard        N vcl discarded                           N
 * n_purge              N total active purges                     N
 * n_purge_add          N new purges added                        N
 * n_purge_retire       N old purges deleted                      N
 * n_purge_obj_test     N objects tested                          N
 * n_purge_re_test      N regexps tested against                  N
 * n_purge_dups         N duplicate purges removed                N
 * hcb_nolock           HCB Lookups without lock                  Y
 * hcb_lock             HCB Lookups with lock                     Y
 * hcb_insert           HCB Inserts                               Y
 * esi_parse            Objects ESI parsed (unlock)               Y
 * esi_errors           ESI parse errors (unlock)                 Y
 * }}}
 */
#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <varnish/varnishapi.h>

#if HAVE_VARNISH_V3
# include <varnish/vsc.h>
typedef struct VSC_C_main c_varnish_stats_t;
#endif

#if HAVE_VARNISH_V2
typedef struct varnish_stats c_varnish_stats_t;
#endif

/* {{{ user_config_s */
struct user_config_s {
	char *instance;

	_Bool collect_cache;
	_Bool collect_connections;
	_Bool collect_esi;
	_Bool collect_backend;
	_Bool collect_fetch;
	_Bool collect_hcb;
	_Bool collect_shm;
	_Bool collect_sms;
#if HAVE_VARNISH_V2
	_Bool collect_sm;
	_Bool collect_sma;
#endif
	_Bool collect_totals;
	_Bool collect_workers;
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
		/* Client connections accepted */
		varnish_submit_derive (conf->instance, "connections", "connections", "accepted", stats->client_conn);
		/* Connection dropped, no sess */
		varnish_submit_derive (conf->instance, "connections", "connections", "dropped" , stats->client_drop);
		/* Client requests received    */
		varnish_submit_derive (conf->instance, "connections", "connections", "received", stats->client_req);
	}

	if (conf->collect_esi)
	{
		/* ESI parse errors (unlock)   */
		varnish_submit_derive (conf->instance, "esi", "total_operations", "error",  stats->esi_errors);
#if HAVE_VARNISH_V2
		/* Objects ESI parsed (unlock) */
		varnish_submit_derive (conf->instance, "esi", "total_operations", "parsed", stats->esi_parse);
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
#endif
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
		/* Total header bytes */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "header-bytes", stats->s_hdrbytes);
		/* Total body byte */
		varnish_submit_derive (conf->instance, "totals", "total_bytes", "body-bytes",   stats->s_bodybytes);
	}

	if (conf->collect_workers)
	{
		/* worker threads */
		varnish_submit_gauge (conf->instance, "workers", "threads", "worker",             stats->n_wrk);
		/* worker threads created */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "created",     stats->n_wrk_create);
		/* worker threads not created */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "failed",      stats->n_wrk_failed);
		/* worker threads limited */
		varnish_submit_derive (conf->instance, "workers", "total_threads", "limited",     stats->n_wrk_max);
		/* dropped work requests */
		varnish_submit_derive (conf->instance, "workers", "total_requests", "dropped",    stats->n_wrk_drop);
#ifdef HAVE_VARNISH_V2
		/* queued work requests */
		varnish_submit_derive (conf->instance, "workers", "total_requests", "queued",     stats->n_wrk_queue);
		/* overflowed work requests */
		varnish_submit_derive (conf->instance, "workers", "total_requests", "overflowed", stats->n_wrk_overflow);
#endif
	}
} /* }}} void varnish_monitor */

#if HAVE_VARNISH_V3
static int varnish_read (user_data_t *ud) /* {{{ */
{
	struct VSM_data *vd;
	const c_varnish_stats_t *stats;

	user_config_t *conf;

	if ((ud == NULL) || (ud->data == NULL))
		return (EINVAL);

	conf = ud->data;

	vd = VSM_New();
	VSC_Setup(vd);
	if (VSC_Open (vd, /* diag = */ 1))
	{
		ERROR ("varnish plugin: Unable to load statistics.");

		return (-1);
	}

	stats = VSC_Main(vd);

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
	conf->collect_esi         = 0;
	conf->collect_fetch       = 0;
	conf->collect_hcb         = 0;
	conf->collect_shm         = 1;
#if HAVE_VARNISH_V2
	conf->collect_sm          = 0;
	conf->collect_sma         = 0;
#endif
	conf->collect_sms         = 0;
	conf->collect_totals      = 0;

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
		else if (strcasecmp ("CollectBackend", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_backend);
		else if (strcasecmp ("CollectFetch", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_fetch);
		else if (strcasecmp ("CollectHCB", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_hcb);
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
		else if (strcasecmp ("CollectTotals", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_totals);
		else if (strcasecmp ("CollectWorkers", child->key) == 0)
			cf_util_get_boolean (child, &conf->collect_workers);
		else
		{
			WARNING ("Varnish plugin: Ignoring unknown "
					"configuration option: \"%s\"",
					child->key);
		}
	}

	if (!conf->collect_cache
			&& !conf->collect_connections
			&& !conf->collect_esi
			&& !conf->collect_backend
			&& !conf->collect_fetch
			&& !conf->collect_hcb
			&& !conf->collect_shm
			&& !conf->collect_sms
#if HAVE_VARNISH_V2
			&& !conf->collect_sma
			&& !conf->collect_sm
#endif
			&& !conf->collect_totals
			&& !conf->collect_workers)
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
