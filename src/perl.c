/**
 * collectd - src/perl.c
 * Copyright (C) 2007-2009  Sebastian Harl
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
 * Author:
 *   Sebastian Harl <sh at tokkee.org>
 **/

/*
 * This plugin embeds a Perl interpreter into collectd and provides an
 * interface for collectd plugins written in perl.
 */

/* do not automatically get the thread specific perl interpreter */
#define PERL_NO_GET_CONTEXT

#define DONT_POISON_SPRINTF_YET 1
#include "collectd.h"
#undef DONT_POISON_SPRINTF_YET

#include "configfile.h"

#if HAVE_STDBOOL_H
# include <stdbool.h>
#endif

#include <EXTERN.h>
#include <perl.h>

#if defined(COLLECT_DEBUG) && COLLECT_DEBUG && defined(__GNUC__) && __GNUC__
# pragma GCC poison sprintf
#endif

#include <XSUB.h>

/* Some versions of Perl define their own version of DEBUG... :-/ */
#ifdef DEBUG
# undef DEBUG
#endif /* DEBUG */

/* ... while we want the definition found in plugin.h. */
#include "plugin.h"
#include "common.h"

#include "filter_chain.h"

#include <pthread.h>

#if !defined(USE_ITHREADS)
# error "Perl does not support ithreads!"
#endif /* !defined(USE_ITHREADS) */

/* clear the Perl sub's stack frame
 * (this should only be used inside an XSUB) */
#define CLEAR_STACK_FRAME PL_stack_sp = PL_stack_base + *PL_markstack_ptr

#define PLUGIN_INIT     0
#define PLUGIN_READ     1
#define PLUGIN_WRITE    2
#define PLUGIN_SHUTDOWN 3
#define PLUGIN_LOG      4
#define PLUGIN_NOTIF    5
#define PLUGIN_FLUSH    6

#define PLUGIN_TYPES    7

#define PLUGIN_CONFIG   254
#define PLUGIN_DATASET  255

#define FC_MATCH  0
#define FC_TARGET 1

#define FC_TYPES  2

#define FC_CB_CREATE  0
#define FC_CB_DESTROY 1
#define FC_CB_EXEC    2

#define FC_CB_TYPES   3

#define log_debug(...) DEBUG ("perl: " __VA_ARGS__)
#define log_info(...) INFO ("perl: " __VA_ARGS__)
#define log_warn(...) WARNING ("perl: " __VA_ARGS__)
#define log_err(...) ERROR ("perl: " __VA_ARGS__)

/* this is defined in DynaLoader.a */
void boot_DynaLoader (PerlInterpreter *, CV *);

static XS (Collectd_plugin_register_ds);
static XS (Collectd_plugin_unregister_ds);
static XS (Collectd_plugin_dispatch_values);
static XS (Collectd__plugin_write);
static XS (Collectd__plugin_flush);
static XS (Collectd_plugin_dispatch_notification);
static XS (Collectd_plugin_log);
static XS (Collectd__fc_register);
static XS (Collectd_call_by_name);

/*
 * private data types
 */

typedef struct c_ithread_s {
	/* the thread's Perl interpreter */
	PerlInterpreter *interp;

	/* double linked list of threads */
	struct c_ithread_s *prev;
	struct c_ithread_s *next;
} c_ithread_t;

typedef struct {
	c_ithread_t *head;
	c_ithread_t *tail;

#if COLLECT_DEBUG
	/* some usage stats */
	int number_of_threads;
#endif /* COLLECT_DEBUG */

	pthread_mutex_t mutex;
} c_ithread_list_t;

/* name / user_data for Perl matches / targets */
typedef struct {
	char *name;
	SV   *user_data;
} pfc_user_data_t;

#define PFC_USER_DATA_FREE(data) \
	do { \
		sfree ((data)->name); \
		if (NULL != (data)->user_data) \
			sv_free ((data)->user_data); \
		sfree (data); \
	} while (0)

/*
 * Public variable
 */
extern char **environ;

/*
 * private variables
 */

/* if perl_threads != NULL perl_threads->head must
 * point to the "base" thread */
static c_ithread_list_t *perl_threads = NULL;

/* the key used to store each pthread's ithread */
static pthread_key_t perl_thr_key;

static int    perl_argc = 0;
static char **perl_argv = NULL;

static char base_name[DATA_MAX_NAME_LEN] = "";

static struct {
	char name[64];
	XS ((*f));
} api[] =
{
	{ "Collectd::plugin_register_data_set",   Collectd_plugin_register_ds },
	{ "Collectd::plugin_unregister_data_set", Collectd_plugin_unregister_ds },
	{ "Collectd::plugin_dispatch_values",     Collectd_plugin_dispatch_values },
	{ "Collectd::_plugin_write",              Collectd__plugin_write },
	{ "Collectd::_plugin_flush",              Collectd__plugin_flush },
	{ "Collectd::plugin_dispatch_notification",
		Collectd_plugin_dispatch_notification },
	{ "Collectd::plugin_log",                 Collectd_plugin_log },
	{ "Collectd::_fc_register",               Collectd__fc_register },
	{ "Collectd::call_by_name",               Collectd_call_by_name },
	{ "", NULL }
};

struct {
	char name[64];
	int  value;
} constants[] =
{
	{ "Collectd::TYPE_INIT",          PLUGIN_INIT },
	{ "Collectd::TYPE_READ",          PLUGIN_READ },
	{ "Collectd::TYPE_WRITE",         PLUGIN_WRITE },
	{ "Collectd::TYPE_SHUTDOWN",      PLUGIN_SHUTDOWN },
	{ "Collectd::TYPE_LOG",           PLUGIN_LOG },
	{ "Collectd::TYPE_NOTIF",         PLUGIN_NOTIF },
	{ "Collectd::TYPE_FLUSH",         PLUGIN_FLUSH },
	{ "Collectd::TYPE_CONFIG",        PLUGIN_CONFIG },
	{ "Collectd::TYPE_DATASET",       PLUGIN_DATASET },
	{ "Collectd::DS_TYPE_COUNTER",    DS_TYPE_COUNTER },
	{ "Collectd::DS_TYPE_GAUGE",      DS_TYPE_GAUGE },
	{ "Collectd::DS_TYPE_DERIVE",     DS_TYPE_DERIVE },
	{ "Collectd::DS_TYPE_ABSOLUTE",   DS_TYPE_ABSOLUTE },
	{ "Collectd::LOG_ERR",            LOG_ERR },
	{ "Collectd::LOG_WARNING",        LOG_WARNING },
	{ "Collectd::LOG_NOTICE",         LOG_NOTICE },
	{ "Collectd::LOG_INFO",           LOG_INFO },
	{ "Collectd::LOG_DEBUG",          LOG_DEBUG },
	{ "Collectd::FC_MATCH",           FC_MATCH },
	{ "Collectd::FC_TARGET",          FC_TARGET },
	{ "Collectd::FC_CB_CREATE",       FC_CB_CREATE },
	{ "Collectd::FC_CB_DESTROY",      FC_CB_DESTROY },
	{ "Collectd::FC_CB_EXEC",         FC_CB_EXEC },
	{ "Collectd::FC_MATCH_NO_MATCH",  FC_MATCH_NO_MATCH },
	{ "Collectd::FC_MATCH_MATCHES",   FC_MATCH_MATCHES },
	{ "Collectd::FC_TARGET_CONTINUE", FC_TARGET_CONTINUE },
	{ "Collectd::FC_TARGET_STOP",     FC_TARGET_STOP },
	{ "Collectd::FC_TARGET_RETURN",   FC_TARGET_RETURN },
	{ "Collectd::NOTIF_FAILURE",      NOTIF_FAILURE },
	{ "Collectd::NOTIF_WARNING",      NOTIF_WARNING },
	{ "Collectd::NOTIF_OKAY",         NOTIF_OKAY },
	{ "", 0 }
};

struct {
	char  name[64];
	char *var;
} g_strings[] =
{
	{ "Collectd::hostname_g", hostname_g },
	{ "", NULL }
};

/*
 * Helper functions for data type conversion.
 */

/*
 * data source:
 * [
 *   {
 *     name => $ds_name,
 *     type => $ds_type,
 *     min  => $ds_min,
 *     max  => $ds_max
 *   },
 *   ...
 * ]
 */
static int hv2data_source (pTHX_ HV *hash, data_source_t *ds)
{
	SV **tmp = NULL;

	if ((NULL == hash) || (NULL == ds))
		return -1;

	if (NULL != (tmp = hv_fetch (hash, "name", 4, 0))) {
		sstrncpy (ds->name, SvPV_nolen (*tmp), sizeof (ds->name));
	}
	else {
		log_err ("hv2data_source: No DS name given.");
		return -1;
	}

	if (NULL != (tmp = hv_fetch (hash, "type", 4, 0))) {
		ds->type = SvIV (*tmp);

		if ((DS_TYPE_COUNTER != ds->type)
				&& (DS_TYPE_GAUGE != ds->type)
				&& (DS_TYPE_DERIVE != ds->type)
				&& (DS_TYPE_ABSOLUTE != ds->type)) {
			log_err ("hv2data_source: Invalid DS type.");
			return -1;
		}
	}
	else {
		ds->type = DS_TYPE_COUNTER;
	}

	if (NULL != (tmp = hv_fetch (hash, "min", 3, 0)))
		ds->min = SvNV (*tmp);
	else
		ds->min = NAN;

	if (NULL != (tmp = hv_fetch (hash, "max", 3, 0)))
		ds->max = SvNV (*tmp);
	else
		ds->max = NAN;
	return 0;
} /* static int hv2data_source (HV *, data_source_t *) */

static int av2value (pTHX_ char *name, AV *array, value_t *value, int len)
{
	const data_set_t *ds;

	int i = 0;

	if ((NULL == name) || (NULL == array) || (NULL == value))
		return -1;

	if (av_len (array) < len - 1)
		len = av_len (array) + 1;

	if (0 >= len)
		return -1;

	ds = plugin_get_ds (name);
	if (NULL == ds) {
		log_err ("av2value: Unknown dataset \"%s\"", name);
		return -1;
	}

	if (ds->ds_num < len) {
		log_warn ("av2value: Value length exceeds data set length.");
		len = ds->ds_num;
	}

	for (i = 0; i < len; ++i) {
		SV **tmp = av_fetch (array, i, 0);

		if (NULL != tmp) {
			if (DS_TYPE_COUNTER == ds->ds[i].type)
				value[i].counter = SvIV (*tmp);
			else if (DS_TYPE_GAUGE == ds->ds[i].type)
				value[i].gauge = SvNV (*tmp);
			else if (DS_TYPE_DERIVE == ds->ds[i].type)
				value[i].derive = SvIV (*tmp);
			else if (DS_TYPE_ABSOLUTE == ds->ds[i].type)
				value[i].absolute = SvIV (*tmp);
		}
		else {
			return -1;
		}
	}
	return len;
} /* static int av2value (char *, AV *, value_t *, int) */

/*
 * value list:
 * {
 *   values => [ @values ],
 *   time   => $time,
 *   host   => $host,
 *   plugin => $plugin,
 *   plugin_instance => $pinstance,
 *   type_instance   => $tinstance,
 * }
 */
static int hv2value_list (pTHX_ HV *hash, value_list_t *vl)
{
	SV **tmp;

	if ((NULL == hash) || (NULL == vl))
		return -1;

	if (NULL == (tmp = hv_fetch (hash, "type", 4, 0))) {
		log_err ("hv2value_list: No type given.");
		return -1;
	}

	sstrncpy (vl->type, SvPV_nolen (*tmp), sizeof (vl->type));

	if ((NULL == (tmp = hv_fetch (hash, "values", 6, 0)))
			|| (! (SvROK (*tmp) && (SVt_PVAV == SvTYPE (SvRV (*tmp)))))) {
		log_err ("hv2value_list: No valid values given.");
		return -1;
	}

	{
		AV  *array = (AV *)SvRV (*tmp);
		int len    = av_len (array) + 1;

		if (len <= 0)
			return -1;

		vl->values     = (value_t *)smalloc (len * sizeof (value_t));
		vl->values_len = av2value (aTHX_ vl->type, (AV *)SvRV (*tmp),
				vl->values, len);

		if (-1 == vl->values_len) {
			sfree (vl->values);
			return -1;
		}
	}

	if (NULL != (tmp = hv_fetch (hash, "time", 4, 0)))
	{
		double t = SvNV (*tmp);
		vl->time = DOUBLE_TO_CDTIME_T (t);
	}

	if (NULL != (tmp = hv_fetch (hash, "interval", 8, 0)))
	{
		double t = SvNV (*tmp);
		vl->interval = DOUBLE_TO_CDTIME_T (t);
	}

	if (NULL != (tmp = hv_fetch (hash, "host", 4, 0)))
		sstrncpy (vl->host, SvPV_nolen (*tmp), sizeof (vl->host));
	else
		sstrncpy (vl->host, hostname_g, sizeof (vl->host));

	if (NULL != (tmp = hv_fetch (hash, "plugin", 6, 0)))
		sstrncpy (vl->plugin, SvPV_nolen (*tmp), sizeof (vl->plugin));

	if (NULL != (tmp = hv_fetch (hash, "plugin_instance", 15, 0)))
		sstrncpy (vl->plugin_instance, SvPV_nolen (*tmp),
				sizeof (vl->plugin_instance));

	if (NULL != (tmp = hv_fetch (hash, "type_instance", 13, 0)))
		sstrncpy (vl->type_instance, SvPV_nolen (*tmp),
				sizeof (vl->type_instance));
	return 0;
} /* static int hv2value_list (pTHX_ HV *, value_list_t *) */

static int av2data_set (pTHX_ AV *array, char *name, data_set_t *ds)
{
	int len, i;

	if ((NULL == array) || (NULL == name) || (NULL == ds))
		return -1;

	len = av_len (array);

	if (-1 == len) {
		log_err ("av2data_set: Invalid data set.");
		return -1;
	}

	ds->ds = (data_source_t *)smalloc ((len + 1) * sizeof (data_source_t));
	ds->ds_num = len + 1;

	for (i = 0; i <= len; ++i) {
		SV **elem = av_fetch (array, i, 0);

		if (NULL == elem) {
			log_err ("av2data_set: Failed to fetch data source %i.", i);
			return -1;
		}

		if (! (SvROK (*elem) && (SVt_PVHV == SvTYPE (SvRV (*elem))))) {
			log_err ("av2data_set: Invalid data source.");
			return -1;
		}

		if (-1 == hv2data_source (aTHX_ (HV *)SvRV (*elem), &ds->ds[i]))
			return -1;

		log_debug ("av2data_set: "
				"DS.name = \"%s\", DS.type = %i, DS.min = %f, DS.max = %f",
				ds->ds[i].name, ds->ds[i].type, ds->ds[i].min, ds->ds[i].max);
	}

	sstrncpy (ds->type, name, sizeof (ds->type));
	return 0;
} /* static int av2data_set (pTHX_ AV *, data_set_t *) */

/*
 * notification:
 * {
 *   severity => $severity,
 *   time     => $time,
 *   message  => $msg,
 *   host     => $host,
 *   plugin   => $plugin,
 *   type     => $type,
 *   plugin_instance => $instance,
 *   type_instance   => $type_instance,
 *   meta     => [ { name => <name>, value => <value> }, ... ]
 * }
 */
static int av2notification_meta (pTHX_ AV *array, notification_meta_t **meta)
{
	notification_meta_t **m = meta;

	int len = av_len (array);
	int i;

	for (i = 0; i <= len; ++i) {
		SV **tmp = av_fetch (array, i, 0);
		HV  *hash;

		if (NULL == tmp)
			return -1;

		if (! (SvROK (*tmp) && (SVt_PVHV == SvTYPE (SvRV (*tmp))))) {
			log_warn ("av2notification_meta: Skipping invalid "
					"meta information.");
			continue;
		}

		hash = (HV *)SvRV (*tmp);

		*m = (notification_meta_t *)smalloc (sizeof (**m));

		if (NULL == (tmp = hv_fetch (hash, "name", 4, 0))) {
			log_warn ("av2notification_meta: Skipping invalid "
					"meta information.");
			free (*m);
			continue;
		}
		sstrncpy ((*m)->name, SvPV_nolen (*tmp), sizeof ((*m)->name));

		if (NULL == (tmp = hv_fetch (hash, "value", 5, 0))) {
			log_warn ("av2notification_meta: Skipping invalid "
					"meta information.");
			free ((*m)->name);
			free (*m);
			continue;
		}

		if (SvNOK (*tmp)) {
			(*m)->nm_value.nm_double = SvNVX (*tmp);
			(*m)->type = NM_TYPE_DOUBLE;
		}
		else if (SvUOK (*tmp)) {
			(*m)->nm_value.nm_unsigned_int = SvUVX (*tmp);
			(*m)->type = NM_TYPE_UNSIGNED_INT;
		}
		else if (SvIOK (*tmp)) {
			(*m)->nm_value.nm_signed_int = SvIVX (*tmp);
			(*m)->type = NM_TYPE_SIGNED_INT;
		}
		else {
			(*m)->nm_value.nm_string = sstrdup (SvPV_nolen (*tmp));
			(*m)->type = NM_TYPE_STRING;
		}

		(*m)->next = NULL;
		m = &((*m)->next);
	}
	return 0;
} /* static int av2notification_meta (AV *, notification_meta_t *) */

static int hv2notification (pTHX_ HV *hash, notification_t *n)
{
	SV **tmp = NULL;

	if ((NULL == hash) || (NULL == n))
		return -1;

	if (NULL != (tmp = hv_fetch (hash, "severity", 8, 0)))
		n->severity = SvIV (*tmp);
	else
		n->severity = NOTIF_FAILURE;

	if (NULL != (tmp = hv_fetch (hash, "time", 4, 0)))
	{
		double t = SvNV (*tmp);
		n->time = DOUBLE_TO_CDTIME_T (t);
	}
	else
		n->time = cdtime ();

	if (NULL != (tmp = hv_fetch (hash, "message", 7, 0)))
		sstrncpy (n->message, SvPV_nolen (*tmp), sizeof (n->message));

	if (NULL != (tmp = hv_fetch (hash, "host", 4, 0)))
		sstrncpy (n->host, SvPV_nolen (*tmp), sizeof (n->host));
	else
		sstrncpy (n->host, hostname_g, sizeof (n->host));

	if (NULL != (tmp = hv_fetch (hash, "plugin", 6, 0)))
		sstrncpy (n->plugin, SvPV_nolen (*tmp), sizeof (n->plugin));

	if (NULL != (tmp = hv_fetch (hash, "plugin_instance", 15, 0)))
		sstrncpy (n->plugin_instance, SvPV_nolen (*tmp),
				sizeof (n->plugin_instance));

	if (NULL != (tmp = hv_fetch (hash, "type", 4, 0)))
		sstrncpy (n->type, SvPV_nolen (*tmp), sizeof (n->type));

	if (NULL != (tmp = hv_fetch (hash, "type_instance", 13, 0)))
		sstrncpy (n->type_instance, SvPV_nolen (*tmp),
				sizeof (n->type_instance));

	n->meta = NULL;
	while (NULL != (tmp = hv_fetch (hash, "meta", 4, 0))) {
		if (! (SvROK (*tmp) && (SVt_PVAV == SvTYPE (SvRV (*tmp))))) {
			log_warn ("hv2notification: Ignoring invalid meta information.");
			break;
		}

		if (0 != av2notification_meta (aTHX_ (AV *)SvRV (*tmp), &n->meta)) {
			plugin_notification_meta_free (n->meta);
			n->meta = NULL;
			return -1;
		}
		break;
	}
	return 0;
} /* static int hv2notification (pTHX_ HV *, notification_t *) */

static int data_set2av (pTHX_ data_set_t *ds, AV *array)
{
	int i = 0;

	if ((NULL == ds) || (NULL == array))
		return -1;

	av_extend (array, ds->ds_num);

	for (i = 0; i < ds->ds_num; ++i) {
		HV *source = newHV ();

		if (NULL == hv_store (source, "name", 4,
				newSVpv (ds->ds[i].name, 0), 0))
			return -1;

		if (NULL == hv_store (source, "type", 4, newSViv (ds->ds[i].type), 0))
			return -1;

		if (! isnan (ds->ds[i].min))
			if (NULL == hv_store (source, "min", 3,
					newSVnv (ds->ds[i].min), 0))
				return -1;

		if (! isnan (ds->ds[i].max))
			if (NULL == hv_store (source, "max", 3,
					newSVnv (ds->ds[i].max), 0))
				return -1;

		if (NULL == av_store (array, i, newRV_noinc ((SV *)source)))
			return -1;
	}
	return 0;
} /* static int data_set2av (data_set_t *, AV *) */

static int value_list2hv (pTHX_ value_list_t *vl, data_set_t *ds, HV *hash)
{
	AV *values = NULL;

	int i   = 0;
	int len = 0;

	if ((NULL == vl) || (NULL == ds) || (NULL == hash))
		return -1;

	len = vl->values_len;

	if (ds->ds_num < len) {
		log_warn ("value2av: Value length exceeds data set length.");
		len = ds->ds_num;
	}

	values = newAV ();
	av_extend (values, len - 1);

	for (i = 0; i < len; ++i) {
		SV *val = NULL;

		if (DS_TYPE_COUNTER == ds->ds[i].type)
			val = newSViv (vl->values[i].counter);
		else if (DS_TYPE_GAUGE == ds->ds[i].type)
			val = newSVnv (vl->values[i].gauge);
		else if (DS_TYPE_DERIVE == ds->ds[i].type)
			val = newSViv (vl->values[i].derive);
		else if (DS_TYPE_ABSOLUTE == ds->ds[i].type)
			val = newSViv (vl->values[i].absolute);

		if (NULL == av_store (values, i, val)) {
			av_undef (values);
			return -1;
		}
	}

	if (NULL == hv_store (hash, "values", 6, newRV_noinc ((SV *)values), 0))
		return -1;

	if (0 != vl->time)
	{
		double t = CDTIME_T_TO_DOUBLE (vl->time);
		if (NULL == hv_store (hash, "time", 4, newSVnv (t), 0))
			return -1;
	}

	{
		double t = CDTIME_T_TO_DOUBLE (vl->interval);
		if (NULL == hv_store (hash, "interval", 8, newSVnv (t), 0))
			return -1;
	}

	if ('\0' != vl->host[0])
		if (NULL == hv_store (hash, "host", 4, newSVpv (vl->host, 0), 0))
			return -1;

	if ('\0' != vl->plugin[0])
		if (NULL == hv_store (hash, "plugin", 6, newSVpv (vl->plugin, 0), 0))
			return -1;

	if ('\0' != vl->plugin_instance[0])
		if (NULL == hv_store (hash, "plugin_instance", 15,
				newSVpv (vl->plugin_instance, 0), 0))
			return -1;

	if ('\0' != vl->type[0])
		if (NULL == hv_store (hash, "type", 4, newSVpv (vl->type, 0), 0))
			return -1;

	if ('\0' != vl->type_instance[0])
		if (NULL == hv_store (hash, "type_instance", 13,
				newSVpv (vl->type_instance, 0), 0))
			return -1;
	return 0;
} /* static int value2av (value_list_t *, data_set_t *, HV *) */

static int notification_meta2av (pTHX_ notification_meta_t *meta, AV *array)
{
	int meta_num = 0;
	int i;

	while (meta) {
		++meta_num;
		meta = meta->next;
	}

	av_extend (array, meta_num);

	for (i = 0; NULL != meta; meta = meta->next, ++i) {
		HV *m = newHV ();
		SV *value;

		if (NULL == hv_store (m, "name", 4, newSVpv (meta->name, 0), 0))
			return -1;

		if (NM_TYPE_STRING == meta->type)
			value = newSVpv (meta->nm_value.nm_string, 0);
		else if (NM_TYPE_SIGNED_INT == meta->type)
			value = newSViv (meta->nm_value.nm_signed_int);
		else if (NM_TYPE_UNSIGNED_INT == meta->type)
			value = newSVuv (meta->nm_value.nm_unsigned_int);
		else if (NM_TYPE_DOUBLE == meta->type)
			value = newSVnv (meta->nm_value.nm_double);
		else if (NM_TYPE_BOOLEAN == meta->type)
			value = meta->nm_value.nm_boolean ? &PL_sv_yes : &PL_sv_no;
		else
			return -1;

		if (NULL == hv_store (m, "value", 5, value, 0)) {
			sv_free (value);
			return -1;
		}

		if (NULL == av_store (array, i, newRV_noinc ((SV *)m))) {
			hv_clear (m);
			hv_undef (m);
			return -1;
		}
	}
	return 0;
} /* static int notification_meta2av (notification_meta_t *, AV *) */

static int notification2hv (pTHX_ notification_t *n, HV *hash)
{
	if (NULL == hv_store (hash, "severity", 8, newSViv (n->severity), 0))
		return -1;

	if (0 != n->time)
	{
		double t = CDTIME_T_TO_DOUBLE (n->time);
		if (NULL == hv_store (hash, "time", 4, newSVnv (t), 0))
			return -1;
	}

	if ('\0' != *n->message)
		if (NULL == hv_store (hash, "message", 7, newSVpv (n->message, 0), 0))
			return -1;

	if ('\0' != *n->host)
		if (NULL == hv_store (hash, "host", 4, newSVpv (n->host, 0), 0))
			return -1;

	if ('\0' != *n->plugin)
		if (NULL == hv_store (hash, "plugin", 6, newSVpv (n->plugin, 0), 0))
			return -1;

	if ('\0' != *n->plugin_instance)
		if (NULL == hv_store (hash, "plugin_instance", 15,
				newSVpv (n->plugin_instance, 0), 0))
			return -1;

	if ('\0' != *n->type)
		if (NULL == hv_store (hash, "type", 4, newSVpv (n->type, 0), 0))
			return -1;

	if ('\0' != *n->type_instance)
		if (NULL == hv_store (hash, "type_instance", 13,
				newSVpv (n->type_instance, 0), 0))
			return -1;

	if (NULL != n->meta) {
		AV *meta = newAV ();
		if ((0 != notification_meta2av (aTHX_ n->meta, meta))
				|| (NULL == hv_store (hash, "meta", 4,
						newRV_noinc ((SV *)meta), 0))) {
			av_clear (meta);
			av_undef (meta);
			return -1;
		}
	}
	return 0;
} /* static int notification2hv (notification_t *, HV *) */

static int oconfig_item2hv (pTHX_ oconfig_item_t *ci, HV *hash)
{
	int i;

	AV *values;
	AV *children;

	if (NULL == hv_store (hash, "key", 3, newSVpv (ci->key, 0), 0))
		return -1;

	values = newAV ();
	if (0 < ci->values_num)
		av_extend (values, ci->values_num);

	if (NULL == hv_store (hash, "values", 6, newRV_noinc ((SV *)values), 0)) {
		av_clear (values);
		av_undef (values);
		return -1;
	}

	for (i = 0; i < ci->values_num; ++i) {
		SV *value;

		switch (ci->values[i].type) {
			case OCONFIG_TYPE_STRING:
				value = newSVpv (ci->values[i].value.string, 0);
				break;
			case OCONFIG_TYPE_NUMBER:
				value = newSVnv ((NV)ci->values[i].value.number);
				break;
			case OCONFIG_TYPE_BOOLEAN:
				value = ci->values[i].value.boolean ? &PL_sv_yes : &PL_sv_no;
				break;
			default:
				log_err ("oconfig_item2hv: Invalid value type %i.",
						ci->values[i].type);
				value = &PL_sv_undef;
		}

		if (NULL == av_store (values, i, value)) {
			sv_free (value);
			return -1;
		}
	}

	/* ignoring 'parent' member which is uninteresting in this case */

	children = newAV ();
	if (0 < ci->children_num)
		av_extend (children, ci->children_num);

	if (NULL == hv_store (hash, "children", 8, newRV_noinc ((SV *)children), 0)) {
		av_clear (children);
		av_undef (children);
		return -1;
	}

	for (i = 0; i < ci->children_num; ++i) {
		HV *child = newHV ();

		if (0 != oconfig_item2hv (aTHX_ ci->children + i, child)) {
			hv_clear (child);
			hv_undef (child);
			return -1;
		}

		if (NULL == av_store (children, i, newRV_noinc ((SV *)child))) {
			hv_clear (child);
			hv_undef (child);
			return -1;
		}
	}
	return 0;
} /* static int oconfig_item2hv (pTHX_ oconfig_item_t *, HV *) */

/*
 * Internal functions.
 */

static char *get_module_name (char *buf, size_t buf_len, const char *module) {
	int status = 0;
	if (base_name[0] == '\0')
		status = ssnprintf (buf, buf_len, "%s", module);
	else
		status = ssnprintf (buf, buf_len, "%s::%s", base_name, module);
	if ((status < 0) || ((unsigned int)status >= buf_len))
		return (NULL);
	return (buf);
} /* char *get_module_name */

/*
 * Add a plugin's data set definition.
 */
static int pplugin_register_data_set (pTHX_ char *name, AV *dataset)
{
	int ret = 0;

	data_set_t ds;

	if ((NULL == name) || (NULL == dataset))
		return -1;

	if (0 != av2data_set (aTHX_ dataset, name, &ds))
		return -1;

	ret = plugin_register_data_set (&ds);

	free (ds.ds);
	return ret;
} /* static int pplugin_register_data_set (char *, SV *) */

/*
 * Remove a plugin's data set definition.
 */
static int pplugin_unregister_data_set (char *name)
{
	if (NULL == name)
		return 0;
	return plugin_unregister_data_set (name);
} /* static int pplugin_unregister_data_set (char *) */

/*
 * Submit the values to the write functions.
 */
static int pplugin_dispatch_values (pTHX_ HV *values)
{
	value_list_t vl = VALUE_LIST_INIT;

	int ret = 0;

	if (NULL == values)
		return -1;

	if (0 != hv2value_list (aTHX_ values, &vl))
		return -1;

	ret = plugin_dispatch_values (&vl);

	sfree (vl.values);
	return ret;
} /* static int pplugin_dispatch_values (char *, HV *) */

/*
 * Submit the values to a single write function.
 */
static int pplugin_write (pTHX_ const char *plugin, AV *data_set, HV *values)
{
	data_set_t   ds;
	value_list_t vl = VALUE_LIST_INIT;

	int ret;

	if (NULL == values)
		return -1;

	if (0 != hv2value_list (aTHX_ values, &vl))
		return -1;

	if ((NULL != data_set)
			&& (0 != av2data_set (aTHX_ data_set, vl.type, &ds)))
		return -1;

	ret = plugin_write (plugin, NULL == data_set ? NULL : &ds, &vl);
	if (0 != ret)
		log_warn ("Dispatching value to plugin \"%s\" failed with status %i.",
				NULL == plugin ? "<any>" : plugin, ret);

	if (NULL != data_set)
		sfree (ds.ds);
	sfree (vl.values);
	return ret;
} /* static int pplugin_write (const char *plugin, HV *, HV *) */

/*
 * Dispatch a notification.
 */
static int pplugin_dispatch_notification (pTHX_ HV *notif)
{
	notification_t n;

	int ret;

	if (NULL == notif)
		return -1;

	memset (&n, 0, sizeof (n));

	if (0 != hv2notification (aTHX_ notif, &n))
		return -1;

	ret = plugin_dispatch_notification (&n);
	plugin_notification_meta_free (n.meta);
	return ret;
} /* static int pplugin_dispatch_notification (HV *) */

/*
 * Call all working functions of the given type.
 */
static int pplugin_call_all (pTHX_ int type, ...)
{
	int retvals = 0;

	va_list ap;
	int ret = 0;

	dSP;

	if ((type < 0) || (type >= PLUGIN_TYPES))
		return -1;

	va_start (ap, type);

	ENTER;
	SAVETMPS;

	PUSHMARK (SP);

	XPUSHs (sv_2mortal (newSViv ((IV)type)));

	if (PLUGIN_WRITE == type) {
		/*
		 * $_[0] = $plugin_type;
		 *
		 * $_[1] =
		 * [
		 *   {
		 *     name => $ds_name,
		 *     type => $ds_type,
		 *     min  => $ds_min,
		 *     max  => $ds_max
		 *   },
		 *   ...
		 * ];
		 *
		 * $_[2] =
		 * {
		 *   values => [ $v1, ... ],
		 *   time   => $time,
		 *   host   => $hostname,
		 *   plugin => $plugin,
		 *   type   => $type,
		 *   plugin_instance => $instance,
		 *   type_instance   => $type_instance
		 * };
		 */
		data_set_t   *ds;
		value_list_t *vl;

		AV *pds = newAV ();
		HV *pvl = newHV ();

		ds = va_arg (ap, data_set_t *);
		vl = va_arg (ap, value_list_t *);

		if (-1 == data_set2av (aTHX_ ds, pds)) {
			av_clear (pds);
			av_undef (pds);
			pds = (AV *)&PL_sv_undef;
			ret = -1;
		}

		if (-1 == value_list2hv (aTHX_ vl, ds, pvl)) {
			hv_clear (pvl);
			hv_undef (pvl);
			pvl = (HV *)&PL_sv_undef;
			ret = -1;
		}

		XPUSHs (sv_2mortal (newSVpv (ds->type, 0)));
		XPUSHs (sv_2mortal (newRV_noinc ((SV *)pds)));
		XPUSHs (sv_2mortal (newRV_noinc ((SV *)pvl)));
	}
	else if (PLUGIN_LOG == type) {
		/*
		 * $_[0] = $level;
		 *
		 * $_[1] = $message;
		 */
		XPUSHs (sv_2mortal (newSViv (va_arg (ap, int))));
		XPUSHs (sv_2mortal (newSVpv (va_arg (ap, char *), 0)));
	}
	else if (PLUGIN_NOTIF == type) {
		/*
		 * $_[0] =
		 * {
		 *   severity => $severity,
		 *   time     => $time,
		 *   message  => $msg,
		 *   host     => $host,
		 *   plugin   => $plugin,
		 *   type     => $type,
		 *   plugin_instance => $instance,
		 *   type_instance   => $type_instance
		 * };
		 */
		notification_t *n;
		HV *notif = newHV ();

		n = va_arg (ap, notification_t *);

		if (-1 == notification2hv (aTHX_ n, notif)) {
			hv_clear (notif);
			hv_undef (notif);
			notif = (HV *)&PL_sv_undef;
			ret = -1;
		}

		XPUSHs (sv_2mortal (newRV_noinc ((SV *)notif)));
	}
	else if (PLUGIN_FLUSH == type) {
		cdtime_t timeout;

		/*
		 * $_[0] = $timeout;
		 * $_[1] = $identifier;
		 */
		timeout = va_arg (ap, cdtime_t);

		XPUSHs (sv_2mortal (newSVnv (CDTIME_T_TO_DOUBLE (timeout))));
		XPUSHs (sv_2mortal (newSVpv (va_arg (ap, char *), 0)));
	}

	PUTBACK;

	retvals = call_pv ("Collectd::plugin_call_all", G_SCALAR);

	SPAGAIN;
	if (0 < retvals) {
		SV *tmp = POPs;
		if (! SvTRUE (tmp))
			ret = -1;
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

	va_end (ap);
	return ret;
} /* static int pplugin_call_all (int, ...) */

/*
 * collectd's perl interpreter based thread implementation.
 *
 * This has been inspired by Perl's ithreads introduced in version 5.6.0.
 */

/* must be called with perl_threads->mutex locked */
static void c_ithread_destroy (c_ithread_t *ithread)
{
	dTHXa (ithread->interp);

	assert (NULL != perl_threads);

	PERL_SET_CONTEXT (aTHX);
	log_debug ("Shutting down Perl interpreter %p...", aTHX);

#if COLLECT_DEBUG
	sv_report_used ();

	--perl_threads->number_of_threads;
#endif /* COLLECT_DEBUG */

	perl_destruct (aTHX);
	perl_free (aTHX);

	if (NULL == ithread->prev)
		perl_threads->head = ithread->next;
	else
		ithread->prev->next = ithread->next;

	if (NULL == ithread->next)
		perl_threads->tail = ithread->prev;
	else
		ithread->next->prev = ithread->prev;

	sfree (ithread);
	return;
} /* static void c_ithread_destroy (c_ithread_t *) */

static void c_ithread_destructor (void *arg)
{
	c_ithread_t *ithread = (c_ithread_t *)arg;
	c_ithread_t *t = NULL;

	if (NULL == perl_threads)
		return;

	pthread_mutex_lock (&perl_threads->mutex);

	for (t = perl_threads->head; NULL != t; t = t->next)
		if (t == ithread)
			break;

	/* the ithread no longer exists */
	if (NULL == t)
		return;

	c_ithread_destroy (ithread);

	pthread_mutex_unlock (&perl_threads->mutex);
	return;
} /* static void c_ithread_destructor (void *) */

/* must be called with perl_threads->mutex locked */
static c_ithread_t *c_ithread_create (PerlInterpreter *base)
{
	c_ithread_t *t = NULL;
	dTHXa (NULL);

	assert (NULL != perl_threads);

	t = (c_ithread_t *)smalloc (sizeof (c_ithread_t));
	memset (t, 0, sizeof (c_ithread_t));

	t->interp = (NULL == base)
		? NULL
		: perl_clone (base, CLONEf_KEEP_PTR_TABLE);

	aTHX = t->interp;

	if ((NULL != base) && (NULL != PL_endav)) {
		av_clear (PL_endav);
		av_undef (PL_endav);
		PL_endav = Nullav;
	}

#if COLLECT_DEBUG
	++perl_threads->number_of_threads;
#endif /* COLLECT_DEBUG */

	t->next = NULL;

	if (NULL == perl_threads->tail) {
		perl_threads->head = t;
		t->prev = NULL;
	}
	else {
		perl_threads->tail->next = t;
		t->prev = perl_threads->tail;
	}

	perl_threads->tail = t;

	pthread_setspecific (perl_thr_key, (const void *)t);
	return t;
} /* static c_ithread_t *c_ithread_create (PerlInterpreter *) */

/*
 * Filter chains implementation.
 */

static int fc_call (pTHX_ int type, int cb_type, pfc_user_data_t *data, ...)
{
	int retvals = 0;

	va_list ap;
	int ret = 0;

	notification_meta_t **meta  = NULL;
	AV                   *pmeta = NULL;

	dSP;

	if ((type < 0) || (type >= FC_TYPES))
		return -1;

	if ((cb_type < 0) || (cb_type >= FC_CB_TYPES))
		return -1;

	va_start (ap, data);

	ENTER;
	SAVETMPS;

	PUSHMARK (SP);

	XPUSHs (sv_2mortal (newSViv ((IV)type)));
	XPUSHs (sv_2mortal (newSVpv (data->name, 0)));
	XPUSHs (sv_2mortal (newSViv ((IV)cb_type)));

	if (FC_CB_CREATE == cb_type) {
		/*
		 * $_[0] = $ci;
		 * $_[1] = $user_data;
		 */
		oconfig_item_t *ci;
		HV *config = newHV ();

		ci = va_arg (ap, oconfig_item_t *);

		if (0 != oconfig_item2hv (aTHX_ ci, config)) {
			hv_clear (config);
			hv_undef (config);
			config = (HV *)&PL_sv_undef;
			ret = -1;
		}

		XPUSHs (sv_2mortal (newRV_noinc ((SV *)config)));
	}
	else if (FC_CB_DESTROY == cb_type) {
		/*
		 * $_[1] = $user_data;
		 */

		/* nothing to be done - the user data pointer
		 * is pushed onto the stack later */
	}
	else if (FC_CB_EXEC == cb_type) {
		/*
		 * $_[0] = $ds;
		 * $_[1] = $vl;
		 * $_[2] = $meta;
		 * $_[3] = $user_data;
		 */
		data_set_t   *ds;
		value_list_t *vl;

		AV *pds = newAV ();
		HV *pvl = newHV ();

		ds   = va_arg (ap, data_set_t *);
		vl   = va_arg (ap, value_list_t *);
		meta = va_arg (ap, notification_meta_t **);

		if (0 != data_set2av (aTHX_ ds, pds)) {
			av_clear (pds);
			av_undef (pds);
			pds = (AV *)&PL_sv_undef;
			ret = -1;
		}

		if (0 != value_list2hv (aTHX_ vl, ds, pvl)) {
			hv_clear (pvl);
			hv_undef (pvl);
			pvl = (HV *)&PL_sv_undef;
			ret = -1;
		}

		if (NULL != meta) {
			pmeta = newAV ();

			if (0 != notification_meta2av (aTHX_ *meta, pmeta)) {
				av_clear (pmeta);
				av_undef (pmeta);
				pmeta = (AV *)&PL_sv_undef;
				ret = -1;
			}
		}
		else {
			pmeta = (AV *)&PL_sv_undef;
		}

		XPUSHs (sv_2mortal (newRV_noinc ((SV *)pds)));
		XPUSHs (sv_2mortal (newRV_noinc ((SV *)pvl)));
		XPUSHs (sv_2mortal (newRV_noinc ((SV *)pmeta)));
	}

	XPUSHs (sv_2mortal (newRV_inc (data->user_data)));

	PUTBACK;

	retvals = call_pv ("Collectd::fc_call", G_SCALAR);

	if ((FC_CB_EXEC == cb_type) && (meta != NULL)) {
		assert (pmeta != NULL);

		plugin_notification_meta_free (*meta);
		av2notification_meta (aTHX_ pmeta, meta);
	}

	SPAGAIN;
	if (0 < retvals) {
		SV *tmp = POPs;

		/* the exec callbacks return a status, while
		 * the others return a boolean value */
		if (FC_CB_EXEC == cb_type)
			ret = SvIV (tmp);
		else if (! SvTRUE (tmp))
			ret = -1;
	}

	PUTBACK;
	FREETMPS;
	LEAVE;

	va_end (ap);
	return ret;
} /* static int fc_call (int, int, pfc_user_data_t *, ...) */

static int fc_create (int type, const oconfig_item_t *ci, void **user_data)
{
	pfc_user_data_t *data;

	int ret = 0;

	dTHX;

	if (NULL == perl_threads)
		return 0;

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}

	log_debug ("fc_create: c_ithread: interp = %p (active threads: %i)",
			aTHX, perl_threads->number_of_threads);

	if ((1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_warn ("A \"%s\" block expects a single string argument.",
				(FC_MATCH == type) ? "Match" : "Target");
		return -1;
	}

	data = (pfc_user_data_t *)smalloc (sizeof (*data));
	data->name      = sstrdup (ci->values[0].value.string);
	data->user_data = newSV (0);

	ret = fc_call (aTHX_ type, FC_CB_CREATE, data, ci);

	if (0 != ret)
		PFC_USER_DATA_FREE (data);
	else
		*user_data = data;
	return ret;
} /* static int fc_create (int, const oconfig_item_t *, void **) */

static int fc_destroy (int type, void **user_data)
{
	pfc_user_data_t *data = *(pfc_user_data_t **)user_data;

	int ret = 0;

	dTHX;

	if ((NULL == perl_threads) || (NULL == data))
		return 0;

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}

	log_debug ("fc_destroy: c_ithread: interp = %p (active threads: %i)",
			aTHX, perl_threads->number_of_threads);

	ret = fc_call (aTHX_ type, FC_CB_DESTROY, data);

	PFC_USER_DATA_FREE (data);
	*user_data = NULL;
	return ret;
} /* static int fc_destroy (int, void **) */

static int fc_exec (int type, const data_set_t *ds, const value_list_t *vl,
		notification_meta_t **meta, void **user_data)
{
	pfc_user_data_t *data = *(pfc_user_data_t **)user_data;

	dTHX;

	if (NULL == perl_threads)
		return 0;

	assert (NULL != data);

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}

	log_debug ("fc_exec: c_ithread: interp = %p (active threads: %i)",
			aTHX, perl_threads->number_of_threads);

	return fc_call (aTHX_ type, FC_CB_EXEC, data, ds, vl, meta);
} /* static int fc_exec (int, const data_set_t *, const value_list_t *,
		notification_meta_t **, void **) */

static int pmatch_create (const oconfig_item_t *ci, void **user_data)
{
	return fc_create (FC_MATCH, ci, user_data);
} /* static int pmatch_create (const oconfig_item_t *, void **) */

static int pmatch_destroy (void **user_data)
{
	return fc_destroy (FC_MATCH, user_data);
} /* static int pmatch_destroy (void **) */

static int pmatch_match (const data_set_t *ds, const value_list_t *vl,
		notification_meta_t **meta, void **user_data)
{
	return fc_exec (FC_MATCH, ds, vl, meta, user_data);
} /* static int pmatch_match (const data_set_t *, const value_list_t *,
		notification_meta_t **, void **) */

static match_proc_t pmatch = {
	pmatch_create, pmatch_destroy, pmatch_match
};

static int ptarget_create (const oconfig_item_t *ci, void **user_data)
{
	return fc_create (FC_TARGET, ci, user_data);
} /* static int ptarget_create (const oconfig_item_t *, void **) */

static int ptarget_destroy (void **user_data)
{
	return fc_destroy (FC_TARGET, user_data);
} /* static int ptarget_destroy (void **) */

static int ptarget_invoke (const data_set_t *ds, value_list_t *vl,
		notification_meta_t **meta, void **user_data)
{
	return fc_exec (FC_TARGET, ds, vl, meta, user_data);
} /* static int ptarget_invoke (const data_set_t *, value_list_t *,
		notification_meta_t **, void **) */

static target_proc_t ptarget = {
	ptarget_create, ptarget_destroy, ptarget_invoke
};

/*
 * Exported Perl API.
 */

/*
 * Collectd::plugin_register_data_set (type, dataset).
 *
 * type:
 *   type of the dataset
 *
 * dataset:
 *   dataset to be registered
 */
static XS (Collectd_plugin_register_ds)
{
	SV  *data = NULL;
	int ret   = 0;

	dXSARGS;

	log_warn ("Using plugin_register() to register new data-sets is "
			"deprecated - add new entries to a custom types.db instead.");

	if (2 != items) {
		log_err ("Usage: Collectd::plugin_register_data_set(type, dataset)");
		XSRETURN_EMPTY;
	}

	log_debug ("Collectd::plugin_register_data_set: "
			"type = \"%s\", dataset = \"%s\"",
			SvPV_nolen (ST (0)), SvPV_nolen (ST (1)));

	data = ST (1);

	if (SvROK (data) && (SVt_PVAV == SvTYPE (SvRV (data)))) {
		ret = pplugin_register_data_set (aTHX_ SvPV_nolen (ST (0)),
				(AV *)SvRV (data));
	}
	else {
		log_err ("Collectd::plugin_register_data_set: Invalid data.");
		XSRETURN_EMPTY;
	}

	if (0 == ret)
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd_plugin_register_ds) */

/*
 * Collectd::plugin_unregister_data_set (type).
 *
 * type:
 *   type of the dataset
 */
static XS (Collectd_plugin_unregister_ds)
{
	dXSARGS;

	if (1 != items) {
		log_err ("Usage: Collectd::plugin_unregister_data_set(type)");
		XSRETURN_EMPTY;
	}

	log_debug ("Collectd::plugin_unregister_data_set: type = \"%s\"",
			SvPV_nolen (ST (0)));

	if (0 == pplugin_unregister_data_set (SvPV_nolen (ST (0))))
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd_plugin_register_ds) */

/*
 * Collectd::plugin_dispatch_values (name, values).
 *
 * name:
 *   name of the plugin
 *
 * values:
 *   value list to submit
 */
static XS (Collectd_plugin_dispatch_values)
{
	SV *values     = NULL;

	int ret = 0;

	dXSARGS;

	if (1 != items) {
		log_err ("Usage: Collectd::plugin_dispatch_values(values)");
		XSRETURN_EMPTY;
	}

	log_debug ("Collectd::plugin_dispatch_values: values=\"%s\"",
			SvPV_nolen (ST (/* stack index = */ 0)));

	values = ST (/* stack index = */ 0);

	/* Make sure the argument is a hash reference. */
	if (! (SvROK (values) && (SVt_PVHV == SvTYPE (SvRV (values))))) {
		log_err ("Collectd::plugin_dispatch_values: Invalid values.");
		XSRETURN_EMPTY;
	}

	if (NULL == values)
		XSRETURN_EMPTY;

	ret = pplugin_dispatch_values (aTHX_ (HV *)SvRV (values));

	if (0 == ret)
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd_plugin_dispatch_values) */

/* Collectd::plugin_write (plugin, ds, vl).
 *
 * plugin:
 *   name of the plugin to call, may be 'undef'
 *
 * ds:
 *   data-set that describes the submitted values, may be 'undef'
 *
 * vl:
 *   value-list to be written
 */
static XS (Collectd__plugin_write)
{
	char *plugin;
	SV   *ds, *vl;
	AV   *ds_array;

	int ret;

	dXSARGS;

	if (3 != items) {
		log_err ("Usage: Collectd::plugin_write(plugin, ds, vl)");
		XSRETURN_EMPTY;
	}

	log_debug ("Collectd::plugin_write: plugin=\"%s\", ds=\"%s\", vl=\"%s\"",
			SvPV_nolen (ST (0)), SvOK (ST (1)) ? SvPV_nolen (ST (1)) : "",
			SvPV_nolen (ST (2)));

	if (! SvOK (ST (0)))
		plugin = NULL;
	else
		plugin = SvPV_nolen (ST (0));

	ds = ST (1);
	if (SvROK (ds) && (SVt_PVAV == SvTYPE (SvRV (ds))))
		ds_array = (AV *)SvRV (ds);
	else if (! SvOK (ds))
		ds_array = NULL;
	else {
		log_err ("Collectd::plugin_write: Invalid data-set.");
		XSRETURN_EMPTY;
	}

	vl = ST (2);
	if (! (SvROK (vl) && (SVt_PVHV == SvTYPE (SvRV (vl))))) {
		log_err ("Collectd::plugin_write: Invalid value-list.");
		XSRETURN_EMPTY;
	}

	ret = pplugin_write (aTHX_ plugin, ds_array, (HV *)SvRV (vl));

	if (0 == ret)
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd__plugin_write) */

/*
 * Collectd::_plugin_flush (plugin, timeout, identifier).
 *
 * plugin:
 *   name of the plugin to flush
 *
 * timeout:
 *   timeout to use when flushing the data
 *
 * identifier:
 *   data-set identifier to flush
 */
static XS (Collectd__plugin_flush)
{
	char *plugin  = NULL;
	int   timeout = -1;
	char *id      = NULL;

	dXSARGS;

	if (3 != items) {
		log_err ("Usage: Collectd::_plugin_flush(plugin, timeout, id)");
		XSRETURN_EMPTY;
	}

	if (SvOK (ST (0)))
		plugin = SvPV_nolen (ST (0));

	if (SvOK (ST (1)))
		timeout = (int)SvIV (ST (1));

	if (SvOK (ST (2)))
		id = SvPV_nolen (ST (2));

	log_debug ("Collectd::_plugin_flush: plugin = \"%s\", timeout = %i, "
			"id = \"%s\"", plugin, timeout, id);

	if (0 == plugin_flush (plugin, timeout, id))
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd__plugin_flush) */

/*
 * Collectd::plugin_dispatch_notification (notif).
 *
 * notif:
 *   notification to dispatch
 */
static XS (Collectd_plugin_dispatch_notification)
{
	SV *notif = NULL;

	int ret = 0;

	dXSARGS;

	if (1 != items) {
		log_err ("Usage: Collectd::plugin_dispatch_notification(notif)");
		XSRETURN_EMPTY;
	}

	log_debug ("Collectd::plugin_dispatch_notification: notif = \"%s\"",
			SvPV_nolen (ST (0)));

	notif = ST (0);

	if (! (SvROK (notif) && (SVt_PVHV == SvTYPE (SvRV (notif))))) {
		log_err ("Collectd::plugin_dispatch_notification: Invalid notif.");
		XSRETURN_EMPTY;
	}

	ret = pplugin_dispatch_notification (aTHX_ (HV *)SvRV (notif));

	if (0 == ret)
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd_plugin_dispatch_notification) */

/*
 * Collectd::plugin_log (level, message).
 *
 * level:
 *   log level (LOG_DEBUG, ... LOG_ERR)
 *
 * message:
 *   log message
 */
static XS (Collectd_plugin_log)
{
	dXSARGS;

	if (2 != items) {
		log_err ("Usage: Collectd::plugin_log(level, message)");
		XSRETURN_EMPTY;
	}

	plugin_log (SvIV (ST (0)), "%s", SvPV_nolen (ST (1)));
	XSRETURN_YES;
} /* static XS (Collectd_plugin_log) */

/*
 * Collectd::_fc_register (type, name)
 *
 * type:
 *   match | target
 *
 * name:
 *   name of the match
 */
static XS (Collectd__fc_register)
{
	int   type;
	char *name;

	int ret = 0;

	dXSARGS;

	if (2 != items) {
		log_err ("Usage: Collectd::_fc_register(type, name)");
		XSRETURN_EMPTY;
	}

	type = SvIV (ST (0));
	name = SvPV_nolen (ST (1));

	if (FC_MATCH == type)
		ret = fc_register_match (name, pmatch);
	else if (FC_TARGET == type)
		ret = fc_register_target (name, ptarget);

	if (0 == ret)
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd_fc_register) */

/*
 * Collectd::call_by_name (...).
 *
 * Call a Perl sub identified by its name passed through $Collectd::cb_name.
 */
static XS (Collectd_call_by_name)
{
	SV   *tmp  = NULL;
	char *name = NULL;

	if (NULL == (tmp = get_sv ("Collectd::cb_name", 0))) {
		sv_setpv (get_sv ("@", 1), "cb_name has not been set");
		CLEAR_STACK_FRAME;
		return;
	}

	name = SvPV_nolen (tmp);

	if (NULL == get_cv (name, 0)) {
		sv_setpvf (get_sv ("@", 1), "unknown callback \"%s\"", name);
		CLEAR_STACK_FRAME;
		return;
	}

	/* simply pass on the subroutine call without touching the stack,
	 * thus leaving any arguments and return values in place */
	call_pv (name, 0);
} /* static XS (Collectd_call_by_name) */

/*
 * Interface to collectd.
 */

static int perl_init (void)
{
	dTHX;

	if (NULL == perl_threads)
		return 0;

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}

	log_debug ("perl_init: c_ithread: interp = %p (active threads: %i)",
			aTHX, perl_threads->number_of_threads);
	return pplugin_call_all (aTHX_ PLUGIN_INIT);
} /* static int perl_init (void) */

static int perl_read (void)
{
	dTHX;

	if (NULL == perl_threads)
		return 0;

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}

	/* Assert that we're not running as the base thread. Otherwise, we might
	 * run into concurrency issues with c_ithread_create(). See
	 * https://github.com/collectd/collectd/issues/9 for details. */
	assert (aTHX != perl_threads->head->interp);

	log_debug ("perl_read: c_ithread: interp = %p (active threads: %i)",
			aTHX, perl_threads->number_of_threads);
	return pplugin_call_all (aTHX_ PLUGIN_READ);
} /* static int perl_read (void) */

static int perl_write (const data_set_t *ds, const value_list_t *vl,
		user_data_t __attribute__((unused)) *user_data)
{
	int status;
	dTHX;

	if (NULL == perl_threads)
		return 0;

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}

	/* Lock the base thread if this is not called from one of the read threads
	 * to avoid race conditions with c_ithread_create(). See
	 * https://github.com/collectd/collectd/issues/9 for details. */
	if (aTHX == perl_threads->head->interp)
		pthread_mutex_lock (&perl_threads->mutex);

	log_debug ("perl_write: c_ithread: interp = %p (active threads: %i)",
			aTHX, perl_threads->number_of_threads);
	status = pplugin_call_all (aTHX_ PLUGIN_WRITE, ds, vl);

	if (aTHX == perl_threads->head->interp)
		pthread_mutex_unlock (&perl_threads->mutex);

	return status;
} /* static int perl_write (const data_set_t *, const value_list_t *) */

static void perl_log (int level, const char *msg,
		user_data_t __attribute__((unused)) *user_data)
{
	dTHX;

	if (NULL == perl_threads)
		return;

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}

	/* Lock the base thread if this is not called from one of the read threads
	 * to avoid race conditions with c_ithread_create(). See
	 * https://github.com/collectd/collectd/issues/9 for details. */
	if (aTHX == perl_threads->head->interp)
		pthread_mutex_lock (&perl_threads->mutex);

	pplugin_call_all (aTHX_ PLUGIN_LOG, level, msg);

	if (aTHX == perl_threads->head->interp)
		pthread_mutex_unlock (&perl_threads->mutex);

	return;
} /* static void perl_log (int, const char *) */

static int perl_notify (const notification_t *notif,
		user_data_t __attribute__((unused)) *user_data)
{
	dTHX;

	if (NULL == perl_threads)
		return 0;

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}
	return pplugin_call_all (aTHX_ PLUGIN_NOTIF, notif);
} /* static int perl_notify (const notification_t *) */

static int perl_flush (cdtime_t timeout, const char *identifier,
		user_data_t __attribute__((unused)) *user_data)
{
	dTHX;

	if (NULL == perl_threads)
		return 0;

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}
	return pplugin_call_all (aTHX_ PLUGIN_FLUSH, timeout, identifier);
} /* static int perl_flush (const int) */

static int perl_shutdown (void)
{
	c_ithread_t *t = NULL;

	int ret = 0;

	dTHX;

	plugin_unregister_complex_config ("perl");

	if (NULL == perl_threads)
		return 0;

	if (NULL == aTHX) {
		c_ithread_t *t = NULL;

		pthread_mutex_lock (&perl_threads->mutex);
		t = c_ithread_create (perl_threads->head->interp);
		pthread_mutex_unlock (&perl_threads->mutex);

		aTHX = t->interp;
	}

	log_debug ("perl_shutdown: c_ithread: interp = %p (active threads: %i)",
			aTHX, perl_threads->number_of_threads);

	plugin_unregister_log ("perl");
	plugin_unregister_notification ("perl");
	plugin_unregister_init ("perl");
	plugin_unregister_read ("perl");
	plugin_unregister_write ("perl");
	plugin_unregister_flush ("perl");

	ret = pplugin_call_all (aTHX_ PLUGIN_SHUTDOWN);

	pthread_mutex_lock (&perl_threads->mutex);
	t = perl_threads->tail;

	while (NULL != t) {
		c_ithread_t *thr = t;

		/* the pointer has to be advanced before destroying
		 * the thread as this will free the memory */
		t = t->prev;

		c_ithread_destroy (thr);
	}

	pthread_mutex_unlock (&perl_threads->mutex);
	pthread_mutex_destroy (&perl_threads->mutex);

	sfree (perl_threads);

	pthread_key_delete (perl_thr_key);

	PERL_SYS_TERM ();

	plugin_unregister_shutdown ("perl");
	return ret;
} /* static void perl_shutdown (void) */

/*
 * Access functions for global variables.
 *
 * These functions implement the "magic" used to access
 * the global variables from Perl.
 */

static int g_pv_get (pTHX_ SV *var, MAGIC *mg)
{
	char *pv = mg->mg_ptr;
	sv_setpv (var, pv);
	return 0;
} /* static int g_pv_get (pTHX_ SV *, MAGIC *) */

static int g_pv_set (pTHX_ SV *var, MAGIC *mg)
{
	char *pv = mg->mg_ptr;
	sstrncpy (pv, SvPV_nolen (var), DATA_MAX_NAME_LEN);
	return 0;
} /* static int g_pv_set (pTHX_ SV *, MAGIC *) */

static int g_interval_get (pTHX_ SV *var, MAGIC *mg)
{
	cdtime_t *interval = (cdtime_t *)mg->mg_ptr;
	double nv;

	nv = CDTIME_T_TO_DOUBLE (*interval);

	sv_setnv (var, nv);
	return 0;
} /* static int g_interval_get (pTHX_ SV *, MAGIC *) */

static int g_interval_set (pTHX_ SV *var, MAGIC *mg)
{
	cdtime_t *interval = (cdtime_t *)mg->mg_ptr;
	double nv;

	nv = (double)SvNV (var);

	*interval = DOUBLE_TO_CDTIME_T (nv);
	return 0;
} /* static int g_interval_set (pTHX_ SV *, MAGIC *) */

static MGVTBL g_pv_vtbl = {
	g_pv_get, g_pv_set, NULL, NULL, NULL, NULL, NULL
#if HAVE_PERL_STRUCT_MGVTBL_SVT_LOCAL
		, NULL
#endif
};
static MGVTBL g_interval_vtbl = {
	g_interval_get, g_interval_set, NULL, NULL, NULL, NULL, NULL
#if HAVE_PERL_STRUCT_MGVTBL_SVT_LOCAL
		, NULL
#endif
};

/* bootstrap the Collectd module */
static void xs_init (pTHX)
{
	HV   *stash = NULL;
	SV   *tmp   = NULL;
	char *file  = __FILE__;

	int i = 0;

	dXSUB_SYS;

	/* enable usage of Perl modules using shared libraries */
	newXS ("DynaLoader::boot_DynaLoader", boot_DynaLoader, file);

	/* register API */
	for (i = 0; NULL != api[i].f; ++i)
		newXS (api[i].name, api[i].f, file);

	stash = gv_stashpv ("Collectd", 1);

	/* export "constants" */
	for (i = 0; '\0' != constants[i].name[0]; ++i)
		newCONSTSUB (stash, constants[i].name, newSViv (constants[i].value));

	/* export global variables
	 * by adding "magic" to the SV's representing the globale variables
	 * perl is able to automagically call the get/set function when
	 * accessing any such variable (this is basically the same as using
	 * tie() in Perl) */
	/* global strings */
	for (i = 0; '\0' != g_strings[i].name[0]; ++i) {
		tmp = get_sv (g_strings[i].name, 1);
		sv_magicext (tmp, NULL, PERL_MAGIC_ext, &g_pv_vtbl,
				g_strings[i].var, 0);
	}

	tmp = get_sv ("Collectd::interval_g", /* create = */ 1);
	sv_magicext (tmp, NULL, /* how = */ PERL_MAGIC_ext,
			/* vtbl = */ &g_interval_vtbl,
			/* name = */ (char *) &interval_g, /* namelen = */ 0);

	return;
} /* static void xs_init (pTHX) */

/* Initialize the global Perl interpreter. */
static int init_pi (int argc, char **argv)
{
	dTHXa (NULL);

	if (NULL != perl_threads)
		return 0;

	log_info ("Initializing Perl interpreter...");
#if COLLECT_DEBUG
	{
		int i = 0;

		for (i = 0; i < argc; ++i)
			log_debug ("argv[%i] = \"%s\"", i, argv[i]);
	}
#endif /* COLLECT_DEBUG */

	if (0 != pthread_key_create (&perl_thr_key, c_ithread_destructor)) {
		log_err ("init_pi: pthread_key_create failed");

		/* this must not happen - cowardly giving up if it does */
		return -1;
	}

#ifdef __FreeBSD__
	/* On FreeBSD, PERL_SYS_INIT3 expands to some expression which
	 * triggers a "value computed is not used" warning by gcc. */
	(void)
#endif
	PERL_SYS_INIT3 (&argc, &argv, &environ);

	perl_threads = (c_ithread_list_t *)smalloc (sizeof (c_ithread_list_t));
	memset (perl_threads, 0, sizeof (c_ithread_list_t));

	pthread_mutex_init (&perl_threads->mutex, NULL);
	/* locking the mutex should not be necessary at this point
	 * but let's just do it for the sake of completeness */
	pthread_mutex_lock (&perl_threads->mutex);

	perl_threads->head = c_ithread_create (NULL);
	perl_threads->tail = perl_threads->head;

	if (NULL == (perl_threads->head->interp = perl_alloc ())) {
		log_err ("init_pi: Not enough memory.");
		exit (3);
	}

	aTHX = perl_threads->head->interp;
	pthread_mutex_unlock (&perl_threads->mutex);

	perl_construct (aTHX);

	PL_exit_flags |= PERL_EXIT_DESTRUCT_END;

	if (0 != perl_parse (aTHX_ xs_init, argc, argv, NULL)) {
		SV *err = get_sv ("@", 1);
		log_err ("init_pi: Unable to bootstrap Collectd: %s",
				SvPV_nolen (err));

		perl_destruct (perl_threads->head->interp);
		perl_free (perl_threads->head->interp);
		sfree (perl_threads);

		pthread_key_delete (perl_thr_key);
		return -1;
	}

	/* Set $0 to "collectd" because perl_parse() has to set it to "-e". */
	sv_setpv (get_sv ("0", 0), "collectd");

	perl_run (aTHX);

	plugin_register_log ("perl", perl_log, /* user_data = */ NULL);
	plugin_register_notification ("perl", perl_notify,
			/* user_data = */ NULL);
	plugin_register_init ("perl", perl_init);

	plugin_register_read ("perl", perl_read);

	plugin_register_write ("perl", perl_write, /* user_data = */ NULL);
	plugin_register_flush ("perl", perl_flush, /* user_data = */ NULL);
	plugin_register_shutdown ("perl", perl_shutdown);
	return 0;
} /* static int init_pi (const char **, const int) */

/*
 * LoadPlugin "<Plugin>"
 */
static int perl_config_loadplugin (pTHX_ oconfig_item_t *ci)
{
	char module_name[DATA_MAX_NAME_LEN];

	char *value = NULL;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("LoadPlugin expects a single string argument.");
		return 1;
	}

	value = ci->values[0].value.string;

	if (NULL == get_module_name (module_name, sizeof (module_name), value)) {
		log_err ("Invalid module name %s", value);
		return (1);
	}

	if (0 != init_pi (perl_argc, perl_argv))
		return -1;

	assert (NULL != perl_threads);
	assert (NULL != perl_threads->head);

	aTHX = perl_threads->head->interp;

	log_debug ("perl_config: loading perl plugin \"%s\"", value);
	load_module (PERL_LOADMOD_NOIMPORT,
			newSVpv (module_name, strlen (module_name)), Nullsv);
	return 0;
} /* static int perl_config_loadplugin (oconfig_item_it *) */

/*
 * BaseName "<Name>"
 */
static int perl_config_basename (pTHX_ oconfig_item_t *ci)
{
	char *value = NULL;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("BaseName expects a single string argument.");
		return 1;
	}

	value = ci->values[0].value.string;

	log_debug ("perl_config: Setting plugin basename to \"%s\"", value);
	sstrncpy (base_name, value, sizeof (base_name));
	return 0;
} /* static int perl_config_basename (oconfig_item_it *) */

/*
 * EnableDebugger "<Package>"|""
 */
static int perl_config_enabledebugger (pTHX_ oconfig_item_t *ci)
{
	char *value = NULL;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("EnableDebugger expects a single string argument.");
		return 1;
	}

	if (NULL != perl_threads) {
		log_warn ("EnableDebugger has no effects if used after LoadPlugin.");
		return 1;
	}

	value = ci->values[0].value.string;

	perl_argv = (char **)realloc (perl_argv,
			(++perl_argc + 1) * sizeof (char *));

	if (NULL == perl_argv) {
		log_err ("perl_config: Not enough memory.");
		exit (3);
	}

	if ('\0' == value[0]) {
		perl_argv[perl_argc - 1] = "-d";
	}
	else {
		perl_argv[perl_argc - 1] = (char *)smalloc (strlen (value) + 4);
		sstrncpy (perl_argv[perl_argc - 1], "-d:", 4);
		sstrncpy (perl_argv[perl_argc - 1] + 3, value, strlen (value) + 1);
	}

	perl_argv[perl_argc] = NULL;
	return 0;
} /* static int perl_config_enabledebugger (oconfig_item_it *) */

/*
 * IncludeDir "<Dir>"
 */
static int perl_config_includedir (pTHX_ oconfig_item_t *ci)
{
	char *value = NULL;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("IncludeDir expects a single string argument.");
		return 1;
	}

	value = ci->values[0].value.string;

	if (NULL == aTHX) {
		perl_argv = (char **)realloc (perl_argv,
				(++perl_argc + 1) * sizeof (char *));

		if (NULL == perl_argv) {
			log_err ("perl_config: Not enough memory.");
			exit (3);
		}

		perl_argv[perl_argc - 1] = (char *)smalloc (strlen (value) + 3);
		sstrncpy(perl_argv[perl_argc - 1], "-I", 3);
		sstrncpy(perl_argv[perl_argc - 1] + 2, value, strlen (value) + 1);

		perl_argv[perl_argc] = NULL;
	}
	else {
		/* prepend the directory to @INC */
		av_unshift (GvAVn (PL_incgv), 1);
		av_store (GvAVn (PL_incgv), 0, newSVpv (value, strlen (value)));
	}
	return 0;
} /* static int perl_config_includedir (oconfig_item_it *) */

/*
 * <Plugin> block
 */
static int perl_config_plugin (pTHX_ oconfig_item_t *ci)
{
	int retvals = 0;
	int ret     = 0;

	char *plugin;
	HV   *config;

	dSP;

	if ((1 != ci->values_num) || (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("LoadPlugin expects a single string argument.");
		return 1;
	}

	plugin = ci->values[0].value.string;
	config = newHV ();

	if (0 != oconfig_item2hv (aTHX_ ci, config)) {
		hv_clear (config);
		hv_undef (config);

		log_err ("Unable to convert configuration to a Perl hash value.");
		config = (HV *)&PL_sv_undef;
	}

	ENTER;
	SAVETMPS;

	PUSHMARK (SP);

	XPUSHs (sv_2mortal (newSVpv (plugin, 0)));
	XPUSHs (sv_2mortal (newRV_noinc ((SV *)config)));

	PUTBACK;

	retvals = call_pv ("Collectd::_plugin_dispatch_config", G_SCALAR);

	SPAGAIN;
	if (0 < retvals) {
		SV *tmp = POPs;
		if (! SvTRUE (tmp))
			ret = 1;
	}
	else
		ret = 1;

	PUTBACK;
	FREETMPS;
	LEAVE;
	return ret;
} /* static int perl_config_plugin (oconfig_item_it *) */

static int perl_config (oconfig_item_t *ci)
{
	int status = 0;
	int i = 0;

	dTHXa (NULL);

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;
		int current_status = 0;

		if (NULL != perl_threads)
			aTHX = PERL_GET_CONTEXT;

		if (0 == strcasecmp (c->key, "LoadPlugin"))
			current_status = perl_config_loadplugin (aTHX_ c);
		else if (0 == strcasecmp (c->key, "BaseName"))
			current_status = perl_config_basename (aTHX_ c);
		else if (0 == strcasecmp (c->key, "EnableDebugger"))
			current_status = perl_config_enabledebugger (aTHX_ c);
		else if (0 == strcasecmp (c->key, "IncludeDir"))
			current_status = perl_config_includedir (aTHX_ c);
		else if (0 == strcasecmp (c->key, "Plugin"))
			current_status = perl_config_plugin (aTHX_ c);
		else
		{
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
			current_status = 0;
		}

		/* fatal error - it's up to perl_config_* to clean up */
		if (0 > current_status) {
			log_err ("Configuration failed with a fatal error - "
					"plugin disabled!");
			return current_status;
		}

		status += current_status;
	}
	return status;
} /* static int perl_config (oconfig_item_t *) */

void module_register (void)
{
	perl_argc = 4;
	perl_argv = (char **)smalloc ((perl_argc + 1) * sizeof (char *));

	/* default options for the Perl interpreter */
	perl_argv[0] = "";
	perl_argv[1] = "-MCollectd";
	perl_argv[2] = "-e";
	perl_argv[3] = "1";
	perl_argv[4] = NULL;

	plugin_register_complex_config ("perl", perl_config);
	return;
} /* void module_register (void) */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

