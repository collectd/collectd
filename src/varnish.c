/**
 * collectd - src/varnish.c
 * Copyright (C) 2010 Jerome Renard
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
 *   Jerome Renard <jerome.renard@gmail.com>
 **/

/**
 * Current list of what is monitored and what is not monitored (yet)
 *
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
 * n_wrk                N worker threads                          N
 * n_wrk_create         N worker threads created                  N
 * n_wrk_failed         N worker threads not created              N
 * n_wrk_max            N worker threads limited                  N
 * n_wrk_queue          N queued work requests                    N
 * n_wrk_overflow       N overflowed work requests                N
 * n_wrk_drop           N dropped work requests                   N
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
 * s_sess               Total Sessions                            N
 * s_req                Total Requests                            N
 * s_pipe               Total pipe                                N
 * s_pass               Total pass                                N
 * s_fetch              Total fetch                               N
 * s_hdrbytes           Total header bytes                        N
 * s_bodybytes          Total body bytes                          N
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
 */
#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <varnish/varnishapi.h>

#define USER_CONFIG_INIT {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}
#define SET_MONITOR_FLAG(name, flag, value) if((strcasecmp(name, key) == 0) && IS_TRUE(value)) user_config.flag = 1

/* {{{ user_config_s */
struct user_config_s {
	_Bool monitor_cache;
	_Bool monitor_connections;
	_Bool monitor_esi;
	_Bool monitor_backend;
	_Bool monitor_fetch;
	_Bool monitor_hcb;
	_Bool monitor_shm;
	_Bool monitor_sma;
	_Bool monitor_sms;
	_Bool monitor_sm;
};
typedef struct user_config_s user_config_t; /* }}} */

/* {{{ Configuration directives */
static user_config_t user_config = USER_CONFIG_INIT;

static const char *config_keys[] =
{
  "MonitorCache",
  "MonitorConnections",
  "MonitorESI",
  "MonitorBackend",
  "MonitorFetch",
  "MonitorHCB",
  "MonitorSHM",
  "MonitorSMA",
  "MonitorSMS",
  "MonitorSM"
};

static int config_keys_num = STATIC_ARRAY_SIZE (config_keys); /* }}} */

static int varnish_config(const char *key, const char *value) /* {{{ */
{
	SET_MONITOR_FLAG("MonitorCache"      , monitor_cache      , value);
	SET_MONITOR_FLAG("MonitorConnections", monitor_connections, value);
	SET_MONITOR_FLAG("MonitorESI"        , monitor_esi        , value);
	SET_MONITOR_FLAG("MonitorBackend"    , monitor_backend    , value);
	SET_MONITOR_FLAG("MonitorFetch"      , monitor_fetch      , value);
	SET_MONITOR_FLAG("MonitorHCB"        , monitor_hcb        , value);
	SET_MONITOR_FLAG("MonitorSHM"        , monitor_shm        , value);
	SET_MONITOR_FLAG("MonitorSMA"        , monitor_sma        , value);
	SET_MONITOR_FLAG("MonitorSMS"        , monitor_sms        , value);
	SET_MONITOR_FLAG("MonitorSM"         , monitor_sm         , value);

	return (0);
} /* }}} */

static void varnish_submit(const char *type, const char *type_instance, gauge_t value) /* {{{ */
{
	value_t values[1];
	value_list_t vl = VALUE_LIST_INIT;

	values[0].gauge = value;
	vl.values_len = 1;
	vl.values = values;

	sstrncpy(vl.host         , hostname_g   , sizeof(vl.host));
	sstrncpy(vl.plugin       , "varnish"    , sizeof(vl.plugin));
	sstrncpy(vl.type         , type         , sizeof(vl.type));
	sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

	plugin_dispatch_values(&vl);
} /* }}} */

static void varnish_monitor(struct varnish_stats *VSL_stats) /* {{{ */
{
	if(user_config.monitor_cache)
	{
		varnish_submit("varnish_cache_ratio", "cache_hit"    , VSL_stats->cache_hit);     /* Cache hits          */
		varnish_submit("varnish_cache_ratio", "cache_miss"   , VSL_stats->cache_miss);    /* Cache misses        */
		varnish_submit("varnish_cache_ratio", "cache_hitpass", VSL_stats->cache_hitpass); /* Cache hits for pass */
	}

	if(user_config.monitor_connections)
	{
		varnish_submit("varnish_connections", "client_connections-accepted", VSL_stats->client_conn); /* Client connections accepted */
		varnish_submit("varnish_connections", "client_connections-dropped" , VSL_stats->client_drop); /* Connection dropped, no sess */
		varnish_submit("varnish_connections", "client_connections-received", VSL_stats->client_req);  /* Client requests received    */
	}

	if(user_config.monitor_esi)
	{
		varnish_submit("varnish_esi", "esi_parsed", VSL_stats->esi_parse);  /* Objects ESI parsed (unlock) */
		varnish_submit("varnish_esi", "esi_errors", VSL_stats->esi_errors); /* ESI parse errors (unlock)   */
	}

	if(user_config.monitor_backend)
	{
		varnish_submit("varnish_backend_connections", "backend_connections-success"      , VSL_stats->backend_conn);      /* Backend conn. success       */
		varnish_submit("varnish_backend_connections", "backend_connections-not-attempted", VSL_stats->backend_unhealthy); /* Backend conn. not attempted */
		varnish_submit("varnish_backend_connections", "backend_connections-too-many"     , VSL_stats->backend_busy);      /* Backend conn. too many      */
		varnish_submit("varnish_backend_connections", "backend_connections-failures"     , VSL_stats->backend_fail);      /* Backend conn. failures      */
		varnish_submit("varnish_backend_connections", "backend_connections-reuses"       , VSL_stats->backend_reuse);     /* Backend conn. reuses        */
		varnish_submit("varnish_backend_connections", "backend_connections-was-closed"   , VSL_stats->backend_toolate);   /* Backend conn. was closed    */
		varnish_submit("varnish_backend_connections", "backend_connections-recycles"     , VSL_stats->backend_recycle);   /* Backend conn. recycles      */
		varnish_submit("varnish_backend_connections", "backend_connections-unused"       , VSL_stats->backend_unused);    /* Backend conn. unused        */
	}

	if(user_config.monitor_fetch)
	{
		varnish_submit("varnish_fetch", "fetch_head"       , VSL_stats->fetch_head);    /* Fetch head                */
		varnish_submit("varnish_fetch", "fetch_length"     , VSL_stats->fetch_length);  /* Fetch with length         */
		varnish_submit("varnish_fetch", "fetch_chunked"    , VSL_stats->fetch_chunked); /* Fetch chunked             */
		varnish_submit("varnish_fetch", "fetch_eof"        , VSL_stats->fetch_eof);     /* Fetch EOF                 */
		varnish_submit("varnish_fetch", "fetch_bad-headers", VSL_stats->fetch_bad);     /* Fetch bad headers         */
		varnish_submit("varnish_fetch", "fetch_close"      , VSL_stats->fetch_close);   /* Fetch wanted close        */
		varnish_submit("varnish_fetch", "fetch_oldhttp"    , VSL_stats->fetch_oldhttp); /* Fetch pre HTTP/1.1 closed */
		varnish_submit("varnish_fetch", "fetch_zero"       , VSL_stats->fetch_zero);    /* Fetch zero len            */
		varnish_submit("varnish_fetch", "fetch_failed"     , VSL_stats->fetch_failed);  /* Fetch failed              */
	}

	if(user_config.monitor_hcb)
	{
		varnish_submit("varnish_hcb", "hcb_nolock", VSL_stats->hcb_nolock); /* HCB Lookups without lock */
		varnish_submit("varnish_hcb", "hcb_lock"  , VSL_stats->hcb_lock);   /* HCB Lookups with lock    */
		varnish_submit("varnish_hcb", "hcb_insert", VSL_stats->hcb_insert); /* HCB Inserts              */
	}

	if(user_config.monitor_shm)
	{
		varnish_submit("varnish_shm", "shm_records"   , VSL_stats->shm_records); /* SHM records                 */
		varnish_submit("varnish_shm", "shm_writes"    , VSL_stats->shm_writes);  /* SHM writes                  */
		varnish_submit("varnish_shm", "shm_flushes"   , VSL_stats->shm_flushes); /* SHM flushes due to overflow */
		varnish_submit("varnish_shm", "shm_contention", VSL_stats->shm_cont);    /* SHM MTX contention          */
		varnish_submit("varnish_shm", "shm_cycles"    , VSL_stats->shm_cycles);  /* SHM cycles through buffer   */
	}

	if(user_config.monitor_sma)
	{
		varnish_submit("varnish_sma", "sma_req"   , VSL_stats->sma_nreq);   /* SMA allocator requests      */
		varnish_submit("varnish_sma", "sma_nobj"  , VSL_stats->sma_nobj);   /* SMA outstanding allocations */
		varnish_submit("varnish_sma", "sma_nbytes", VSL_stats->sma_nbytes); /* SMA outstanding bytes       */
		varnish_submit("varnish_sma", "sma_balloc", VSL_stats->sma_balloc); /* SMA bytes allocated         */
		varnish_submit("varnish_sma", "sma_bfree" , VSL_stats->sma_bfree);  /* SMA bytes free              */
	}

	if(user_config.monitor_sms)
	{
		varnish_submit("varnish_sms", "sms_nreq"  , VSL_stats->sms_nreq);   /* SMS allocator requests      */
		varnish_submit("varnish_sms", "sms_nobj"  , VSL_stats->sms_nobj);   /* SMS outstanding allocations */
		varnish_submit("varnish_sms", "sms_nbytes", VSL_stats->sms_nbytes); /* SMS outstanding bytes       */
		varnish_submit("varnish_sms", "sms_balloc", VSL_stats->sms_balloc); /* SMS bytes allocated         */
		varnish_submit("varnish_sms", "sms_bfree" , VSL_stats->sms_bfree);  /* SMS bytes freed             */
	}

	if(user_config.monitor_sm)
	{
		varnish_submit("varnish_sm", "sm_nreq"  , VSL_stats->sm_nreq);   /* allocator requests      */
		varnish_submit("varnish_sm", "sm_nobj"  , VSL_stats->sm_nobj);   /* outstanding allocations */
		varnish_submit("varnish_sm", "sm_balloc", VSL_stats->sm_balloc); /* bytes allocated         */
		varnish_submit("varnish_sm", "sm_bfree" , VSL_stats->sm_bfree);  /* bytes free              */
	}
} /* }}} */

static int varnish_read(void) /* {{{ */
{
	struct varnish_stats *VSL_stats;
	const char *varnish_instance_name = NULL;

	if ((VSL_stats = VSL_OpenStats(varnish_instance_name)) == NULL)
	{
		ERROR("Varnish plugin : unable to load statistics");

		return (-1);
	}

	varnish_monitor(VSL_stats);

    return (0);
} /* }}} */

void module_register (void) /* {{{ */
{
	plugin_register_config("varnish", varnish_config, config_keys, config_keys_num);
	plugin_register_read("varnish", varnish_read);
} /* }}} */

/* vim: set sw=8 noet fdm=marker : */
