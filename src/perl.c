/**
 * collectd - src/perl.c
 * Copyright (C) 2007, 2008  Sebastian Harl
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

#define PLUGIN_DATASET  255

#define log_debug(...) DEBUG ("perl: " __VA_ARGS__)
#define log_info(...) INFO ("perl: " __VA_ARGS__)
#define log_warn(...) WARNING ("perl: " __VA_ARGS__)
#define log_err(...) ERROR ("perl: " __VA_ARGS__)

/* this is defined in DynaLoader.a */
void boot_DynaLoader (PerlInterpreter *, CV *);

static XS (Collectd_plugin_register_ds);
static XS (Collectd_plugin_unregister_ds);
static XS (Collectd_plugin_dispatch_values);
static XS (Collectd_plugin_flush_one);
static XS (Collectd_plugin_flush_all);
static XS (Collectd_plugin_dispatch_notification);
static XS (Collectd_plugin_log);
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
	{ "Collectd::plugin_flush_one",           Collectd_plugin_flush_one },
	{ "Collectd::plugin_flush_all",           Collectd_plugin_flush_all },
	{ "Collectd::plugin_dispatch_notification",
		Collectd_plugin_dispatch_notification },
	{ "Collectd::plugin_log",                 Collectd_plugin_log },
	{ "Collectd::call_by_name",               Collectd_call_by_name },
	{ "", NULL }
};

struct {
	char name[64];
	int  value;
} constants[] =
{
	{ "Collectd::TYPE_INIT",       PLUGIN_INIT },
	{ "Collectd::TYPE_READ",       PLUGIN_READ },
	{ "Collectd::TYPE_WRITE",      PLUGIN_WRITE },
	{ "Collectd::TYPE_SHUTDOWN",   PLUGIN_SHUTDOWN },
	{ "Collectd::TYPE_LOG",        PLUGIN_LOG },
	{ "Collectd::TYPE_NOTIF",      PLUGIN_NOTIF },
	{ "Collectd::TYPE_FLUSH",      PLUGIN_FLUSH },
	{ "Collectd::TYPE_DATASET",    PLUGIN_DATASET },
	{ "Collectd::DS_TYPE_COUNTER", DS_TYPE_COUNTER },
	{ "Collectd::DS_TYPE_GAUGE",   DS_TYPE_GAUGE },
	{ "Collectd::LOG_ERR",         LOG_ERR },
	{ "Collectd::LOG_WARNING",     LOG_WARNING },
	{ "Collectd::LOG_NOTICE",      LOG_NOTICE },
	{ "Collectd::LOG_INFO",        LOG_INFO },
	{ "Collectd::LOG_DEBUG",       LOG_DEBUG },
	{ "Collectd::NOTIF_FAILURE",   NOTIF_FAILURE },
	{ "Collectd::NOTIF_WARNING",   NOTIF_WARNING },
	{ "Collectd::NOTIF_OKAY",      NOTIF_OKAY },
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

struct {
	char  name[64];
	int  *var;
} g_integers[] =
{
	{ "Collectd::interval_g", &interval_g },
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
		strncpy (ds->name, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		ds->name[DATA_MAX_NAME_LEN - 1] = '\0';
	}
	else {
		log_err ("hv2data_source: No DS name given.");
		return -1;
	}

	if (NULL != (tmp = hv_fetch (hash, "type", 4, 0))) {
		ds->type = SvIV (*tmp);

		if ((DS_TYPE_COUNTER != ds->type) && (DS_TYPE_GAUGE != ds->type)) {
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
} /* static data_source_t *hv2data_source (HV *) */

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
			else
				value[i].gauge = SvNV (*tmp);
		}
		else {
			return -1;
		}
	}
	return len;
} /* static int av2value (char *, AV *, value_t *, int) */

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
		else
			val = newSVnv (vl->values[i].gauge);

		if (NULL == av_store (values, i, val)) {
			av_undef (values);
			return -1;
		}
	}

	if (NULL == hv_store (hash, "values", 6, newRV_noinc ((SV *)values), 0))
		return -1;

	if (0 != vl->time)
		if (NULL == hv_store (hash, "time", 4, newSViv (vl->time), 0))
			return -1;

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

	if ('\0' != vl->type_instance[0])
		if (NULL == hv_store (hash, "type_instance", 13,
				newSVpv (vl->type_instance, 0), 0))
			return -1;
	return 0;
} /* static int value2av (value_list_t *, data_set_t *, HV *) */

static int notification2hv (pTHX_ notification_t *n, HV *hash)
{
	if (NULL == hv_store (hash, "severity", 8, newSViv (n->severity), 0))
		return -1;

	if (0 != n->time)
		if (NULL == hv_store (hash, "time", 4, newSViv (n->time), 0))
			return -1;

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
	return 0;
} /* static int notification2hv (notification_t *, HV *) */

/*
 * Internal functions.
 */

static char *get_module_name (char *buf, size_t buf_len, const char *module) {
	int status = 0;
	if (base_name[0] == '\0')
		status = snprintf (buf, buf_len, "%s", module);
	else
		status = snprintf (buf, buf_len, "%s::%s", base_name, module);
	if ((status < 0) || ((unsigned int)status >= buf_len))
		return (NULL);
	buf[buf_len - 1] = '\0';
	return (buf);
} /* char *get_module_name */

/*
 * Add a plugin's data set definition.
 */
static int pplugin_register_data_set (pTHX_ char *name, AV *dataset)
{
	int len = -1;
	int ret = 0;
	int i   = 0;

	data_source_t *ds  = NULL;
	data_set_t    *set = NULL;

	if ((NULL == name) || (NULL == dataset))
		return -1;

	len = av_len (dataset);

	if (-1 == len)
		return -1;

	ds  = (data_source_t *)smalloc ((len + 1) * sizeof (data_source_t));
	set = (data_set_t *)smalloc (sizeof (data_set_t));

	for (i = 0; i <= len; ++i) {
		SV **elem = av_fetch (dataset, i, 0);

		if (NULL == elem)
			return -1;

		if (! (SvROK (*elem) && (SVt_PVHV == SvTYPE (SvRV (*elem))))) {
			log_err ("pplugin_register_data_set: Invalid data source.");
			return -1;
		}

		if (-1 == hv2data_source (aTHX_ (HV *)SvRV (*elem), &ds[i]))
			return -1;

		log_debug ("pplugin_register_data_set: "
				"DS.name = \"%s\", DS.type = %i, DS.min = %f, DS.max = %f",
				ds[i].name, ds[i].type, ds[i].min, ds[i].max);
	}

	strncpy (set->type, name, DATA_MAX_NAME_LEN);
	set->type[DATA_MAX_NAME_LEN - 1] = '\0';

	set->ds_num = len + 1;
	set->ds = ds;

	ret = plugin_register_data_set (set);

	free (ds);
	free (set);
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
 *
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
static int pplugin_dispatch_values (pTHX_ char *name, HV *values)
{
	value_list_t list = VALUE_LIST_INIT;
	value_t      *val = NULL;

	SV **tmp = NULL;

	int ret = 0;

	if ((NULL == name) || (NULL == values))
		return -1;

	if ((NULL == (tmp = hv_fetch (values, "values", 6, 0)))
			|| (! (SvROK (*tmp) && (SVt_PVAV == SvTYPE (SvRV (*tmp)))))) {
		log_err ("pplugin_dispatch_values: No valid values given.");
		return -1;
	}

	{
		AV  *array = (AV *)SvRV (*tmp);
		int len    = av_len (array) + 1;

		if (len <= 0)
			return -1;

		val = (value_t *)smalloc (len * sizeof (value_t));

		list.values_len = av2value (aTHX_ name, (AV *)SvRV (*tmp), val, len);
		list.values = val;

		if (-1 == list.values_len) {
			sfree (val);
			return -1;
		}
	}

	if (NULL != (tmp = hv_fetch (values, "time", 4, 0))) {
		list.time = (time_t)SvIV (*tmp);
	}
	else {
		list.time = time (NULL);
	}

	if (NULL != (tmp = hv_fetch (values, "host", 4, 0))) {
		strncpy (list.host, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		list.host[DATA_MAX_NAME_LEN - 1] = '\0';
	}
	else {
		sstrncpy (list.host, hostname_g, sizeof (list.host));
	}

	if (NULL != (tmp = hv_fetch (values, "plugin", 6, 0))) {
		strncpy (list.plugin, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		list.plugin[DATA_MAX_NAME_LEN - 1] = '\0';
	}

	if (NULL != (tmp = hv_fetch (values, "plugin_instance", 15, 0))) {
		strncpy (list.plugin_instance, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		list.plugin_instance[DATA_MAX_NAME_LEN - 1] = '\0';
	}

	if (NULL != (tmp = hv_fetch (values, "type_instance", 13, 0))) {
		strncpy (list.type_instance, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		list.type_instance[DATA_MAX_NAME_LEN - 1] = '\0';
	}

	ret = plugin_dispatch_values (name, &list);

	sfree (val);
	return ret;
} /* static int pplugin_dispatch_values (char *, HV *) */

/*
 * Dispatch a notification.
 *
 * notification:
 * {
 *   severity => $severity,
 *   time     => $time,
 *   message  => $msg,
 *   host     => $host,
 *   plugin   => $plugin,
 *   type     => $type,
 *   plugin_instance => $instance,
 *   type_instance   => $type_instance
 * }
 */
static int pplugin_dispatch_notification (pTHX_ HV *notif)
{
	notification_t n;

	SV **tmp = NULL;

	if (NULL == notif)
		return -1;

	memset (&n, 0, sizeof (n));

	if (NULL != (tmp = hv_fetch (notif, "severity", 8, 0)))
		n.severity = SvIV (*tmp);
	else
		n.severity = NOTIF_FAILURE;

	if (NULL != (tmp = hv_fetch (notif, "time", 4, 0)))
		n.time = (time_t)SvIV (*tmp);
	else
		n.time = time (NULL);

	if (NULL != (tmp = hv_fetch (notif, "message", 7, 0)))
		strncpy (n.message, SvPV_nolen (*tmp), sizeof (n.message));
	n.message[sizeof (n.message) - 1] = '\0';

	if (NULL != (tmp = hv_fetch (notif, "host", 4, 0)))
		strncpy (n.host, SvPV_nolen (*tmp), sizeof (n.host));
	else
		strncpy (n.host, hostname_g, sizeof (n.host));
	n.host[sizeof (n.host) - 1] = '\0';

	if (NULL != (tmp = hv_fetch (notif, "plugin", 6, 0)))
		strncpy (n.plugin, SvPV_nolen (*tmp), sizeof (n.plugin));
	n.plugin[sizeof (n.plugin) - 1] = '\0';

	if (NULL != (tmp = hv_fetch (notif, "plugin_instance", 15, 0)))
		strncpy (n.plugin_instance, SvPV_nolen (*tmp),
				sizeof (n.plugin_instance));
	n.plugin_instance[sizeof (n.plugin_instance) - 1] = '\0';

	if (NULL != (tmp = hv_fetch (notif, "type", 4, 0)))
		strncpy (n.type, SvPV_nolen (*tmp), sizeof (n.type));
	n.type[sizeof (n.type) - 1] = '\0';

	if (NULL != (tmp = hv_fetch (notif, "type_instance", 13, 0)))
		strncpy (n.type_instance, SvPV_nolen (*tmp), sizeof (n.type_instance));
	n.type_instance[sizeof (n.type_instance) - 1] = '\0';
	return plugin_dispatch_notification (&n);
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
			pds = Nullav;
			ret = -1;
		}

		if (-1 == value_list2hv (aTHX_ vl, ds, pvl)) {
			hv_clear (pvl);
			hv_undef (pvl);
			pvl = Nullhv;
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
			notif = Nullhv;
			ret = -1;
		}

		XPUSHs (sv_2mortal (newRV_noinc ((SV *)notif)));
	}
	else if (PLUGIN_FLUSH == type) {
		/*
		 * $_[0] = $timeout;
		 */
		XPUSHs (sv_2mortal (newSViv (va_arg (ap, int))));
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
	SV *values = NULL;

	int ret = 0;

	dXSARGS;

	if (2 != items) {
		log_err ("Usage: Collectd::plugin_dispatch_values(name, values)");
		XSRETURN_EMPTY;
	}

	log_debug ("Collectd::plugin_dispatch_values: "
			"name = \"%s\", values=\"%s\"",
			SvPV_nolen (ST (0)), SvPV_nolen (ST (1)));

	values = ST (1);

	if (! (SvROK (values) && (SVt_PVHV == SvTYPE (SvRV (values))))) {
		log_err ("Collectd::plugin_dispatch_values: Invalid values.");
		XSRETURN_EMPTY;
	}

	if ((NULL == ST (0)) || (NULL == values))
		XSRETURN_EMPTY;

	ret = pplugin_dispatch_values (aTHX_ SvPV_nolen (ST (0)),
			(HV *)SvRV (values));

	if (0 == ret)
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd_plugin_dispatch_values) */

/*
 * Collectd::plugin_flush_one (timeout, name).
 *
 * timeout:
 *   timeout to use when flushing the data
 *
 * name:
 *   name of the plugin to flush
 */
static XS (Collectd_plugin_flush_one)
{
	dXSARGS;

	if (2 != items) {
		log_err ("Usage: Collectd::plugin_flush_one(timeout, name)");
		XSRETURN_EMPTY;
	}

	log_debug ("Collectd::plugin_flush_one: timeout = %i, name = \"%s\"",
			(int)SvIV (ST (0)), SvPV_nolen (ST (1)));

	if (0 == plugin_flush_one ((int)SvIV (ST (0)), SvPV_nolen (ST (1))))
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd_plugin_flush_one) */

/*
 * Collectd::plugin_flush_all (timeout).
 *
 * timeout:
 *   timeout to use when flushing the data
 */
static XS (Collectd_plugin_flush_all)
{
	dXSARGS;

	if (1 != items) {
		log_err ("Usage: Collectd::plugin_flush_all(timeout)");
		XSRETURN_EMPTY;
	}

	log_debug ("Collectd::plugin_flush_all: timeout = %i", (int)SvIV (ST (0)));

	plugin_flush_all ((int)SvIV (ST (0)));
	XSRETURN_YES;
} /* static XS (Collectd_plugin_flush_all) */

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

	plugin_log (SvIV (ST (0)), SvPV_nolen (ST (1)));
	XSRETURN_YES;
} /* static XS (Collectd_plugin_log) */

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

	log_debug ("perl_read: c_ithread: interp = %p (active threads: %i)",
			aTHX, perl_threads->number_of_threads);
	return pplugin_call_all (aTHX_ PLUGIN_READ);
} /* static int perl_read (void) */

static int perl_write (const data_set_t *ds, const value_list_t *vl)
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

	log_debug ("perl_write: c_ithread: interp = %p (active threads: %i)",
			aTHX, perl_threads->number_of_threads);
	return pplugin_call_all (aTHX_ PLUGIN_WRITE, ds, vl);
} /* static int perl_write (const data_set_t *, const value_list_t *) */

static void perl_log (int level, const char *msg)
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

	pplugin_call_all (aTHX_ PLUGIN_LOG, level, msg);
	return;
} /* static void perl_log (int, const char *) */

static int perl_notify (const notification_t *notif)
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

static int perl_flush (const int timeout)
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
	return pplugin_call_all (aTHX_ PLUGIN_FLUSH, timeout);
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
	strncpy (pv, SvPV_nolen (var), DATA_MAX_NAME_LEN);
	pv[DATA_MAX_NAME_LEN - 1] = '\0';
	return 0;
} /* static int g_pv_set (pTHX_ SV *, MAGIC *) */

static int g_iv_get (pTHX_ SV *var, MAGIC *mg)
{
	int *iv = (int *)mg->mg_ptr;
	sv_setiv (var, *iv);
	return 0;
} /* static int g_iv_get (pTHX_ SV *, MAGIC *) */

static int g_iv_set (pTHX_ SV *var, MAGIC *mg)
{
	int *iv = (int *)mg->mg_ptr;
	*iv = (int)SvIV (var);
	return 0;
} /* static int g_iv_set (pTHX_ SV *, MAGIC *) */

static MGVTBL g_pv_vtbl = { g_pv_get, g_pv_set, NULL, NULL, NULL, NULL, NULL };
static MGVTBL g_iv_vtbl = { g_iv_get, g_iv_set, NULL, NULL, NULL, NULL, NULL };

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

	/* global integers */
	for (i = 0; '\0' != g_integers[i].name[0]; ++i) {
		tmp = get_sv (g_integers[i].name, 1);
		sv_magicext (tmp, NULL, PERL_MAGIC_ext, &g_iv_vtbl,
				(char *)g_integers[i].var, 0);
	}
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

	plugin_register_log ("perl", perl_log);
	plugin_register_notification ("perl", perl_notify);
	plugin_register_init ("perl", perl_init);

	plugin_register_read ("perl", perl_read);

	plugin_register_write ("perl", perl_write);
	plugin_register_flush ("perl", perl_flush);
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
	strncpy (base_name, value, sizeof (base_name));
	base_name[sizeof (base_name) - 1] = '\0';
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

static int perl_config (oconfig_item_t *ci)
{
	int i = 0;

	dTHXa (NULL);

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (NULL != perl_threads)
			aTHX = PERL_GET_CONTEXT;

		if (0 == strcasecmp (c->key, "LoadPlugin"))
			perl_config_loadplugin (aTHX_ c);
		else if (0 == strcasecmp (c->key, "BaseName"))
			perl_config_basename (aTHX_ c);
		else if (0 == strcasecmp (c->key, "EnableDebugger"))
			perl_config_enabledebugger (aTHX_ c);
		else if (0 == strcasecmp (c->key, "IncludeDir"))
			perl_config_includedir (aTHX_ c);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}
	return 0;
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

