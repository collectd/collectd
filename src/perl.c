/**
 * collectd - src/perl.c
 * Copyright (C) 2007  Sebastian Harl
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

#include "collectd.h"

#include "configfile.h"

#include <EXTERN.h>
#include <perl.h>

#include <XSUB.h>

/* Some versions of Perl define their own version of DEBUG... :-/ */
#ifdef DEBUG
# undef DEBUG
#endif /* DEBUG */

/* ... while we want the definition found in plugin.h. */
#include "plugin.h"
#include "common.h"

#define PLUGIN_INIT     0
#define PLUGIN_READ     1
#define PLUGIN_WRITE    2
#define PLUGIN_SHUTDOWN 3
#define PLUGIN_LOG      4

#define PLUGIN_TYPES    5

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
static XS (Collectd_plugin_log);

/*
 * private variables
 */

static PerlInterpreter *perl = NULL;

static int  perl_argc   = 0;
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
	{ "Collectd::plugin_log",                 Collectd_plugin_log },
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
	{ "Collectd::TYPE_DATASET",    PLUGIN_DATASET },
	{ "Collectd::DS_TYPE_COUNTER", DS_TYPE_COUNTER },
	{ "Collectd::DS_TYPE_GAUGE",   DS_TYPE_GAUGE },
	{ "Collectd::LOG_ERR",         LOG_ERR },
	{ "Collectd::LOG_WARNING",     LOG_WARNING },
	{ "Collectd::LOG_NOTICE",      LOG_NOTICE },
	{ "Collectd::LOG_INFO",        LOG_INFO },
	{ "Collectd::LOG_DEBUG",       LOG_DEBUG },
	{ "", 0 }
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
static int hv2data_source (HV *hash, data_source_t *ds)
{
	SV **tmp = NULL;

	if ((NULL == hash) || (NULL == ds))
		return -1;

	if (NULL != (tmp = Perl_hv_fetch (perl, hash, "name", 4, 0))) {
		strncpy (ds->name, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		ds->name[DATA_MAX_NAME_LEN - 1] = '\0';
	}
	else {
		log_err ("hv2data_source: No DS name given.");
		return -1;
	}

	if (NULL != (tmp = Perl_hv_fetch (perl, hash, "type", 4, 0))) {
		ds->type = SvIV (*tmp);

		if ((DS_TYPE_COUNTER != ds->type) && (DS_TYPE_GAUGE != ds->type)) {
			log_err ("hv2data_source: Invalid DS type.");
			return -1;
		}
	}
	else {
		ds->type = DS_TYPE_COUNTER;
	}

	if (NULL != (tmp = Perl_hv_fetch (perl, hash, "min", 3, 0)))
		ds->min = SvNV (*tmp);
	else
		ds->min = NAN;

	if (NULL != (tmp = Perl_hv_fetch (perl, hash, "max", 3, 0)))
		ds->max = SvNV (*tmp);
	else
		ds->max = NAN;
	return 0;
} /* static data_source_t *hv2data_source (HV *) */

static int av2value (char *name, AV *array, value_t *value, int len)
{
	const data_set_t *ds;

	int i = 0;

	if ((NULL == name) || (NULL == array) || (NULL == value))
		return -1;

	if (Perl_av_len (perl, array) < len - 1)
		len = Perl_av_len (perl, array) + 1;

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
		SV **tmp = Perl_av_fetch (perl, array, i, 0);

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

static int data_set2av (data_set_t *ds, AV *array)
{
	int i = 0;

	if ((NULL == ds) || (NULL == array))
		return -1;

	Perl_av_extend (perl, array, ds->ds_num);

	for (i = 0; i < ds->ds_num; ++i) {
		HV *source = Perl_newHV (perl);

		if (NULL == Perl_hv_store (perl, source, "name", 4,
				Perl_newSVpv (perl, ds->ds[i].name, 0), 0))
			return -1;

		if (NULL == Perl_hv_store (perl, source, "type", 4,
				Perl_newSViv (perl, ds->ds[i].type), 0))
			return -1;

		if (! isnan (ds->ds[i].min))
			if (NULL == Perl_hv_store (perl, source, "min", 3,
					Perl_newSVnv (perl, ds->ds[i].min), 0))
				return -1;

		if (! isnan (ds->ds[i].max))
			if (NULL == Perl_hv_store (perl, source, "max", 3,
					Perl_newSVnv (perl, ds->ds[i].max), 0))
				return -1;

		if (NULL == Perl_av_store (perl, array, i,
				Perl_newRV_noinc (perl, (SV *)source)))
			return -1;
	}
	return 0;
} /* static int data_set2av (data_set_t *, AV *) */

static int value_list2hv (value_list_t *vl, data_set_t *ds, HV *hash)
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

	values = Perl_newAV (perl);
	Perl_av_extend (perl, values, len - 1);

	for (i = 0; i < len; ++i) {
		SV *val = NULL;

		if (DS_TYPE_COUNTER == ds->ds[i].type)
			val = Perl_newSViv (perl, vl->values[i].counter);
		else
			val = Perl_newSVnv (perl, vl->values[i].gauge);

		if (NULL == Perl_av_store (perl, values, i, val)) {
			Perl_av_undef (perl, values);
			return -1;
		}
	}

	if (NULL == Perl_hv_store (perl, hash, "values", 6,
			Perl_newRV_noinc (perl, (SV *)values), 0))
		return -1;

	if (0 != vl->time)
		if (NULL == Perl_hv_store (perl, hash, "time", 4,
				Perl_newSViv (perl, vl->time), 0))
			return -1;

	if ('\0' != vl->host[0])
		if (NULL == Perl_hv_store (perl, hash, "host", 4,
				Perl_newSVpv (perl, vl->host, 0), 0))
			return -1;

	if ('\0' != vl->plugin[0])
		if (NULL == Perl_hv_store (perl, hash, "plugin", 6,
				Perl_newSVpv (perl, vl->plugin, 0), 0))
			return -1;

	if ('\0' != vl->plugin_instance[0])
		if (NULL == Perl_hv_store (perl, hash, "plugin_instance", 15,
				Perl_newSVpv (perl, vl->plugin_instance, 0), 0))
			return -1;

	if ('\0' != vl->type_instance[0])
		if (NULL == Perl_hv_store (perl, hash, "type_instance", 13,
				Perl_newSVpv (perl, vl->type_instance, 0), 0))
			return -1;
	return 0;
} /* static int value2av (value_list_t *, data_set_t *, HV *) */

/*
 * Internal functions.
 */

static char *get_module_name (char *buf, size_t buf_len, const char *module) {
	int status = 0;
	if (base_name[0] == '\0')
		status = snprintf (buf, buf_len, "%s", module);
	else
		status = snprintf (buf, buf_len, "%s::%s", base_name, module);
	if ((status < 0) || (status >= buf_len))
		return (NULL);
	buf[buf_len - 1] = '\0';
	return (buf);
} /* char *get_module_name */

/*
 * Add a plugin's data set definition.
 */
static int pplugin_register_data_set (char *name, AV *dataset)
{
	int len = -1;
	int i   = 0;

	data_source_t *ds  = NULL;
	data_set_t    *set = NULL;

	if ((NULL == name) || (NULL == dataset))
		return -1;

	len = Perl_av_len (perl, dataset);

	if (-1 == len)
		return -1;

	ds  = (data_source_t *)smalloc ((len + 1) * sizeof (data_source_t));
	set = (data_set_t *)smalloc (sizeof (data_set_t));

	for (i = 0; i <= len; ++i) {
		SV **elem = Perl_av_fetch (perl, dataset, i, 0);

		if (NULL == elem)
			return -1;

		if (! (SvROK (*elem) && (SVt_PVHV == SvTYPE (SvRV (*elem))))) {
			log_err ("pplugin_register_data_set: Invalid data source.");
			return -1;
		}

		if (-1 == hv2data_source ((HV *)SvRV (*elem), &ds[i]))
			return -1;

		log_debug ("pplugin_register_data_set: "
				"DS.name = \"%s\", DS.type = %i, DS.min = %f, DS.max = %f",
				ds[i].name, ds[i].type, ds[i].min, ds[i].max);
	}

	strncpy (set->type, name, DATA_MAX_NAME_LEN);
	set->type[DATA_MAX_NAME_LEN - 1] = '\0';

	set->ds_num = len + 1;
	set->ds = ds;
	return plugin_register_data_set (set);
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
static int pplugin_dispatch_values (char *name, HV *values)
{
	value_list_t list = VALUE_LIST_INIT;
	value_t      *val = NULL;

	SV **tmp = NULL;

	int ret = 0;

	if ((NULL == name) || (NULL == values))
		return -1;

	if ((NULL == (tmp = Perl_hv_fetch (perl, values, "values", 6, 0)))
			|| (! (SvROK (*tmp) && (SVt_PVAV == SvTYPE (SvRV (*tmp)))))) {
		log_err ("pplugin_dispatch_values: No valid values given.");
		return -1;
	}

	{
		AV  *array = (AV *)SvRV (*tmp);
		int len    = Perl_av_len (perl, array) + 1;

		if (len <= 0)
			return -1;

		val = (value_t *)smalloc (len * sizeof (value_t));

		list.values_len = av2value (name, (AV *)SvRV (*tmp), val, len);
		list.values = val;

		if (-1 == list.values_len) {
			sfree (val);
			return -1;
		}
	}

	if (NULL != (tmp = Perl_hv_fetch (perl, values, "time", 4, 0))) {
		list.time = (time_t)SvIV (*tmp);
	}
	else {
		list.time = time (NULL);
	}

	if (NULL != (tmp = Perl_hv_fetch (perl, values, "host", 4, 0))) {
		strncpy (list.host, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		list.host[DATA_MAX_NAME_LEN - 1] = '\0';
	}
	else {
		strcpy (list.host, hostname_g);
	}

	if (NULL != (tmp = Perl_hv_fetch (perl, values, "plugin", 6, 0))) {
		strncpy (list.plugin, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		list.plugin[DATA_MAX_NAME_LEN - 1] = '\0';
	}

	if (NULL != (tmp = Perl_hv_fetch (perl, values,
			"plugin_instance", 15, 0))) {
		strncpy (list.plugin_instance, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		list.plugin_instance[DATA_MAX_NAME_LEN - 1] = '\0';
	}

	if (NULL != (tmp = Perl_hv_fetch (perl, values, "type_instance", 13, 0))) {
		strncpy (list.type_instance, SvPV_nolen (*tmp), DATA_MAX_NAME_LEN);
		list.type_instance[DATA_MAX_NAME_LEN - 1] = '\0';
	}

	ret = plugin_dispatch_values (name, &list);

	sfree (val);
	return ret;
} /* static int pplugin_dispatch_values (char *, HV *) */

/*
 * Call all working functions of the given type.
 */
static int pplugin_call_all (int type, ...)
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

	XPUSHs (sv_2mortal (Perl_newSViv (perl, (IV)type)));

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

		AV *pds = Perl_newAV (perl);
		HV *pvl = Perl_newHV (perl);

		ds = va_arg (ap, data_set_t *);
		vl = va_arg (ap, value_list_t *);

		if (-1 == data_set2av (ds, pds))
			return -1;

		if (-1 == value_list2hv (vl, ds, pvl))
			return -1;

		XPUSHs (sv_2mortal (Perl_newSVpv (perl, ds->type, 0)));
		XPUSHs (sv_2mortal (Perl_newRV_noinc (perl, (SV *)pds)));
		XPUSHs (sv_2mortal (Perl_newRV_noinc (perl, (SV *)pvl)));
	}
	else if (PLUGIN_LOG == type) {
		/*
		 * $_[0] = $level;
		 *
		 * $_[1] = $message;
		 */
		XPUSHs (sv_2mortal (Perl_newSViv (perl, va_arg (ap, int))));
		XPUSHs (sv_2mortal (Perl_newSVpv (perl, va_arg (ap, char *), 0)));
	}

	PUTBACK;

	retvals = Perl_call_pv (perl, "Collectd::plugin_call_all", G_SCALAR);

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
		ret = pplugin_register_data_set (SvPV_nolen (ST (0)),
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

	if (0 == pplugin_unregister_data_set (SvPV_nolen (ST (1))))
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

	ret = pplugin_dispatch_values (SvPV_nolen (ST (0)), (HV *)SvRV (values));

	if (0 == ret)
		XSRETURN_YES;
	else
		XSRETURN_EMPTY;
} /* static XS (Collectd_plugin_dispatch_values) */

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
 * Interface to collectd.
 */

static int perl_init (void)
{
	if (NULL == perl)
		return 0;

	PERL_SET_CONTEXT (perl);
	return pplugin_call_all (PLUGIN_INIT);
} /* static int perl_init (void) */

static int perl_read (void)
{
	if (NULL == perl)
		return 0;

	PERL_SET_CONTEXT (perl);
	return pplugin_call_all (PLUGIN_READ);
} /* static int perl_read (void) */

static int perl_write (const data_set_t *ds, const value_list_t *vl)
{
	if (NULL == perl)
		return 0;

	PERL_SET_CONTEXT (perl);
	return pplugin_call_all (PLUGIN_WRITE, ds, vl);
} /* static int perl_write (const data_set_t *, const value_list_t *) */

static void perl_log (int level, const char *msg)
{
	if (NULL == perl)
		return;

	PERL_SET_CONTEXT (perl);
	pplugin_call_all (PLUGIN_LOG, level, msg);
	return;
} /* static void perl_log (int, const char *) */

static int perl_shutdown (void)
{
	int ret = 0;

	plugin_unregister_complex_config ("perl");

	if (NULL == perl)
		return 0;

	plugin_unregister_log ("perl");
	plugin_unregister_init ("perl");
	plugin_unregister_read ("perl");
	plugin_unregister_write ("perl");

	PERL_SET_CONTEXT (perl);
	ret = pplugin_call_all (PLUGIN_SHUTDOWN);

#if COLLECT_DEBUG
	Perl_sv_report_used (perl);
#endif /* COLLECT_DEBUG */

	perl_destruct (perl);
	perl_free (perl);
	perl = NULL;

	PERL_SYS_TERM ();

	plugin_unregister_shutdown ("perl");
	return ret;
} /* static void perl_shutdown (void) */

/* bootstrap the Collectd module */
static void xs_init (pTHX)
{
	HV   *stash = NULL;
	char *file  = __FILE__;

	int i = 0;

	dXSUB_SYS;

	/* enable usage of Perl modules using shared libraries */
	Perl_newXS (perl, "DynaLoader::boot_DynaLoader", boot_DynaLoader, file);

	/* register API */
	for (i = 0; NULL != api[i].f; ++i)
		Perl_newXS (perl, api[i].name, api[i].f, file);

	stash = Perl_gv_stashpv (perl, "Collectd", 1);

	/* export "constants" */
	for (i = 0; '\0' != constants[i].name[0]; ++i)
		Perl_newCONSTSUB (perl, stash, constants[i].name,
				Perl_newSViv (perl, constants[i].value));
	return;
} /* static void xs_init (pTHX) */

/* Initialize the global Perl interpreter. */
static int init_pi (int argc, char **argv)
{
	if (NULL != perl)
		return 0;

	log_info ("Initializing Perl interpreter...");
#if COLLECT_DEBUG
	{
		int i = 0;

		for (i = 0; i < argc; ++i)
			log_debug ("argv[%i] = \"%s\"", i, argv[i]);
	}
#endif /* COLLECT_DEBUG */

	PERL_SYS_INIT3 (&argc, &argv, &environ);

	if (NULL == (perl = perl_alloc ())) {
		log_err ("module_register: Not enough memory.");
		exit (3);
	}
	perl_construct (perl);

	PL_exit_flags |= PERL_EXIT_DESTRUCT_END;

	if (0 != perl_parse (perl, xs_init, argc, argv, NULL)) {
		log_err ("module_register: Unable to bootstrap Collectd.");
		exit (1);
	}
	perl_run (perl);

	plugin_register_log ("perl", perl_log);
	plugin_register_init ("perl", perl_init);

	plugin_register_read ("perl", perl_read);

	plugin_register_write ("perl", perl_write);
	plugin_register_shutdown ("perl", perl_shutdown);
	return 0;
} /* static int init_pi (const char **, const int) */

/*
 * LoadPlugin "<Plugin>"
 */
static int perl_config_loadplugin (oconfig_item_t *ci)
{
	char module_name[DATA_MAX_NAME_LEN];

	char *value = NULL;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type))
		return 1;

	value = ci->values[0].value.string;

	if (NULL == get_module_name (module_name, sizeof (module_name), value)) {
		log_err ("Invalid module name %s", value);
		return (1);
	}

	init_pi (perl_argc, perl_argv);

	log_debug ("perl_config: loading perl plugin \"%s\"", value);
	Perl_load_module (perl, PERL_LOADMOD_NOIMPORT,
			Perl_newSVpv (perl, module_name, strlen (module_name)),
			Nullsv);
	return 0;
} /* static int perl_config_loadplugin (oconfig_item_it *) */

/*
 * BaseName "<Name>"
 */
static int perl_config_basename (oconfig_item_t *ci)
{
	char *value = NULL;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type))
		return 1;

	value = ci->values[0].value.string;

	log_debug ("perl_config: Setting plugin basename to \"%s\"", value);
	strncpy (base_name, value, sizeof (base_name));
	base_name[sizeof (base_name) - 1] = '\0';
	return 0;
} /* static int perl_config_basename (oconfig_item_it *) */

/*
 * EnableDebugger "<Package>"|""
 */
static int perl_config_enabledebugger (oconfig_item_t *ci)
{
	char *value = NULL;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type))
		return 1;

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
static int perl_config_includedir (oconfig_item_t *ci)
{
	char *value = NULL;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type))
		return 1;

	value = ci->values[0].value.string;

	if (NULL == perl) {
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
		Perl_av_unshift (perl, GvAVn (PL_incgv), 1);
		Perl_av_store (perl, GvAVn (PL_incgv),
				0, Perl_newSVpv (perl, value, strlen (value)));
	}
	return 0;
} /* static int perl_config_includedir (oconfig_item_it *) */

static int perl_config (oconfig_item_t *ci)
{
	int i = 0;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "LoadPlugin"))
			perl_config_loadplugin (c);
		else if (0 == strcasecmp (c->key, "BaseName"))
			perl_config_basename (c);
		else if (0 == strcasecmp (c->key, "EnableDebugger"))
			perl_config_enabledebugger (c);
		else if (0 == strcasecmp (c->key, "IncludeDir"))
			perl_config_includedir (c);
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

