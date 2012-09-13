/**
 * collectd - src/table.c
 * Copyright (C) 2009  Sebastian Harl
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
 *   Sebastian Harl <sh at tokkee.org>
 **/

/*
 * This module provides generic means to parse and dispatch tabular data.
 */

#include "collectd.h"
#include "common.h"

#include "configfile.h"
#include "plugin.h"

#define log_err(...) ERROR ("table plugin: " __VA_ARGS__)
#define log_warn(...) WARNING ("table plugin: " __VA_ARGS__)

/*
 * private data types
 */

typedef struct {
	char  *type;
	char  *instance_prefix;
	int   *instances;
	size_t instances_num;
	int   *values;
	size_t values_num;

	const data_set_t *ds;
} tbl_result_t;

typedef struct {
	char *file;
	char *sep;
	char *instance;

	tbl_result_t *results;
	size_t        results_num;

	size_t max_colnum;
} tbl_t;

static void tbl_result_setup (tbl_result_t *res)
{
	res->type            = NULL;

	res->instance_prefix = NULL;
	res->instances       = NULL;
	res->instances_num   = 0;

	res->values          = NULL;
	res->values_num      = 0;

	res->ds              = NULL;
} /* tbl_result_setup */

static void tbl_result_clear (tbl_result_t *res)
{
	sfree (res->type);

	sfree (res->instance_prefix);
	sfree (res->instances);
	res->instances_num = 0;

	sfree (res->values);
	res->values_num = 0;

	res->ds = NULL;
} /* tbl_result_clear */

static void tbl_setup (tbl_t *tbl, char *file)
{
	tbl->file        = sstrdup (file);
	tbl->sep         = NULL;
	tbl->instance    = NULL;

	tbl->results     = NULL;
	tbl->results_num = 0;

	tbl->max_colnum  = 0;
} /* tbl_setup */

static void tbl_clear (tbl_t *tbl)
{
	size_t i;

	sfree (tbl->file);
	sfree (tbl->sep);
	sfree (tbl->instance);

	for (i = 0; i < tbl->results_num; ++i)
		tbl_result_clear (tbl->results + i);
	sfree (tbl->results);
	tbl->results_num = 0;

	tbl->max_colnum  = 0;
} /* tbl_clear */

static tbl_t *tables;
static size_t tables_num;

/*
 * configuration handling
 */

static int tbl_config_set_s (char *name, char **var, oconfig_item_t *ci)
{
	if ((1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("\"%s\" expects a single string argument.", name);
		return 1;
	}

	sfree (*var);
	*var = sstrdup (ci->values[0].value.string);
	return 0;
} /* tbl_config_set_separator */

static int tbl_config_append_array_i (char *name, int **var, size_t *len,
		oconfig_item_t *ci)
{
	int *tmp;

	size_t i;

	if (1 > ci->values_num) {
		log_err ("\"%s\" expects at least one argument.", name);
		return 1;
	}

	for (i = 0; i < ci->values_num; ++i) {
		if (OCONFIG_TYPE_NUMBER != ci->values[i].type) {
			log_err ("\"%s\" expects numerical arguments only.", name);
			return 1;
		}
	}

	*len += ci->values_num;
	tmp = (int *)realloc (*var, *len * sizeof (**var));
	if (NULL == tmp) {
		char errbuf[1024];
		log_err ("realloc failed: %s.",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return -1;
	}

	*var = tmp;

	for (i = *len - ci->values_num; i < *len; ++i)
		(*var)[i] = (int)ci->values[i].value.number;
	return 0;
} /* tbl_config_append_array_s */

static int tbl_config_result (tbl_t *tbl, oconfig_item_t *ci)
{
	tbl_result_t *res;

	int status = 0;
	size_t i;

	if (0 != ci->values_num) {
		log_err ("<Result> does not expect any arguments.");
		return 1;
	}

	res = (tbl_result_t *)realloc (tbl->results,
			(tbl->results_num + 1) * sizeof (*tbl->results));
	if (NULL == tbl) {
		char errbuf[1024];
		log_err ("realloc failed: %s.",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return -1;
	}

	tbl->results = res;
	++tbl->results_num;

	res = tbl->results + tbl->results_num - 1;
	tbl_result_setup (res);

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "Type"))
			tbl_config_set_s (c->key, &res->type, c);
		else if (0 == strcasecmp (c->key, "InstancePrefix"))
			tbl_config_set_s (c->key, &res->instance_prefix, c);
		else if (0 == strcasecmp (c->key, "InstancesFrom"))
			tbl_config_append_array_i (c->key,
					&res->instances, &res->instances_num, c);
		else if (0 == strcasecmp (c->key, "ValuesFrom"))
			tbl_config_append_array_i (c->key,
					&res->values, &res->values_num, c);
		else
			log_warn ("Ignoring unknown config key \"%s\" "
					" in <Result>.", c->key);
	}

	if (NULL == res->type) {
		log_err ("No \"Type\" option specified for <Result> "
				"in table \"%s\".", tbl->file);
		status = 1;
	}

	if (NULL == res->values) {
		log_err ("No \"ValuesFrom\" option specified for <Result> "
				"in table \"%s\".", tbl->file);
		status = 1;
	}

	if (0 != status) {
		tbl_result_clear (res);
		--tbl->results_num;
		return status;
	}
	return 0;
} /* tbl_config_result */

static int tbl_config_table (oconfig_item_t *ci)
{
	tbl_t *tbl;

	int status = 0;
	size_t i;

	if ((1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("<Table> expects a single string argument.");
		return 1;
	}

	tbl = (tbl_t *)realloc (tables, (tables_num + 1) * sizeof (*tables));
	if (NULL == tbl) {
		char errbuf[1024];
		log_err ("realloc failed: %s.",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return -1;
	}

	tables = tbl;
	++tables_num;

	tbl = tables + tables_num - 1;
	tbl_setup (tbl, ci->values[0].value.string);

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "Separator"))
			tbl_config_set_s (c->key, &tbl->sep, c);
		else if (0 == strcasecmp (c->key, "Instance"))
			tbl_config_set_s (c->key, &tbl->instance, c);
		else if (0 == strcasecmp (c->key, "Result"))
			tbl_config_result (tbl, c);
		else
			log_warn ("Ignoring unknown config key \"%s\" "
					"in <Table %s>.", c->key, tbl->file);
	}

	if (NULL == tbl->sep) {
		log_err ("Table \"%s\" does not specify any separator.", tbl->file);
		status = 1;
	}
	strunescape (tbl->sep, strlen (tbl->sep) + 1);

	if (NULL == tbl->instance) {
		tbl->instance = sstrdup (tbl->file);
		replace_special (tbl->instance, strlen (tbl->instance));
	}

	if (NULL == tbl->results) {
		log_err ("Table \"%s\" does not specify any (valid) results.",
				tbl->file);
		status = 1;
	}

	if (0 != status) {
		tbl_clear (tbl);
		--tables_num;
		return status;
	}

	for (i = 0; i < tbl->results_num; ++i) {
		tbl_result_t *res = tbl->results + i;
		size_t j;

		for (j = 0; j < res->instances_num; ++j)
			if (res->instances[j] > tbl->max_colnum)
				tbl->max_colnum = res->instances[j];

		for (j = 0; j < res->values_num; ++j)
			if (res->values[j] > tbl->max_colnum)
				tbl->max_colnum = res->values[j];
	}
	return 0;
} /* tbl_config_table */

static int tbl_config (oconfig_item_t *ci)
{
	size_t i;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "Table"))
			tbl_config_table (c);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}
	return 0;
} /* tbl_config */

/*
 * result handling
 */

static int tbl_prepare (tbl_t *tbl)
{
	size_t i;

	for (i = 0; i < tbl->results_num; ++i) {
		tbl_result_t *res = tbl->results + i;

		res->ds = plugin_get_ds (res->type);
		if (NULL == res->ds) {
			log_err ("Unknown type \"%s\". See types.db(5) for details.",
					res->type);
			return -1;
		}

		if (res->values_num != (size_t)res->ds->ds_num) {
			log_err ("Invalid type \"%s\". Expected %zu data source%s, "
					"got %i.", res->type, res->values_num,
					(1 == res->values_num) ? "" : "s",
					res->ds->ds_num);
			return -1;
		}
	}
	return 0;
} /* tbl_prepare */

static int tbl_finish (tbl_t *tbl)
{
	size_t i;

	for (i = 0; i < tbl->results_num; ++i)
		tbl->results[i].ds = NULL;
	return 0;
} /* tbl_finish */

static int tbl_result_dispatch (tbl_t *tbl, tbl_result_t *res,
		char **fields, size_t fields_num)
{
	value_list_t vl = VALUE_LIST_INIT;
	value_t values[res->values_num];

	size_t i;

	assert (NULL != res->ds);
	assert (res->values_num == res->ds->ds_num);

	for (i = 0; i < res->values_num; ++i) {
		char *value;

		assert (res->values[i] < fields_num);
		value = fields[res->values[i]];

		if (0 != parse_value (value, &values[i], res->ds->ds[i].type))
			return -1;
	}

	vl.values     = values;
	vl.values_len = STATIC_ARRAY_SIZE (values);

	sstrncpy (vl.host, hostname_g, sizeof (vl.host));
	sstrncpy (vl.plugin, "table", sizeof (vl.plugin));
	sstrncpy (vl.plugin_instance, tbl->instance, sizeof (vl.plugin_instance));
	sstrncpy (vl.type, res->type, sizeof (vl.type));

	if (0 == res->instances_num) {
		if (NULL != res->instance_prefix)
			sstrncpy (vl.type_instance, res->instance_prefix,
					sizeof (vl.type_instance));
	}
	else {
		char *instances[res->instances_num];
		char  instances_str[DATA_MAX_NAME_LEN];

		for (i = 0; i < res->instances_num; ++i) {
			assert (res->instances[i] < fields_num);
			instances[i] = fields[res->instances[i]];
		}

		strjoin (instances_str, sizeof (instances_str),
				instances, STATIC_ARRAY_SIZE (instances), "-");
		instances_str[sizeof (instances_str) - 1] = '\0';

		vl.type_instance[sizeof (vl.type_instance) - 1] = '\0';
		if (NULL == res->instance_prefix)
			strncpy (vl.type_instance, instances_str,
					sizeof (vl.type_instance));
		else
			snprintf (vl.type_instance, sizeof (vl.type_instance),
					"%s-%s", res->instance_prefix, instances_str);

		if ('\0' != vl.type_instance[sizeof (vl.type_instance) - 1]) {
			vl.type_instance[sizeof (vl.type_instance) - 1] = '\0';
			log_warn ("Truncated type instance: %s.", vl.type_instance);
		}
	}

	plugin_dispatch_values (&vl);
	return 0;
} /* tbl_result_dispatch */

static int tbl_parse_line (tbl_t *tbl, char *line, size_t len)
{
	char *fields[tbl->max_colnum + 1];
	char *ptr, *saveptr;

	size_t i;

	i = 0;
	ptr = line;
	saveptr = NULL;
	while (NULL != (fields[i] = strtok_r (ptr, tbl->sep, &saveptr))) {
		ptr = NULL;
		++i;

		if (i > tbl->max_colnum)
			break;
	}

	if (i <= tbl->max_colnum) {
		log_err ("Not enough columns in line "
				"(expected at least %zu, got %zu).",
				tbl->max_colnum + 1, i);
		return -1;
	}

	for (i = 0; i < tbl->results_num; ++i)
		if (0 != tbl_result_dispatch (tbl, tbl->results + i,
					fields, STATIC_ARRAY_SIZE (fields))) {
			log_err ("Failed to dispatch result.");
			continue;
		}
	return 0;
} /* tbl_parse_line */

static int tbl_read_table (tbl_t *tbl)
{
	FILE *fh;
	char  buf[4096];

	fh = fopen (tbl->file, "r");
	if (NULL == fh) {
		char errbuf[1024];
		log_err ("Failed to open file \"%s\": %s.", tbl->file,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return -1;
	}

	buf[sizeof (buf) - 1] = '\0';
	while (NULL != fgets (buf, sizeof (buf), fh)) {
		if ('\0' != buf[sizeof (buf) - 1]) {
			buf[sizeof (buf) - 1] = '\0';
			log_err ("Table %s: Truncated line: %s", tbl->file, buf);
		}

		if (0 != tbl_parse_line (tbl, buf, sizeof (buf))) {
			log_err ("Table %s: Failed to parse line: %s", tbl->file, buf);
			continue;
		}
	}

	if (0 != ferror (fh)) {
		char errbuf[1024];
		log_err ("Failed to read from file \"%s\": %s.", tbl->file,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		fclose (fh);
		return -1;
	}

	fclose (fh);
	return 0;
} /* tbl_read_table */

/*
 * collectd callbacks
 */

static int tbl_read (void)
{
	int status = -1;
	size_t i;

	if (0 == tables_num)
		return 0;

	for (i = 0; i < tables_num; ++i) {
		tbl_t *tbl = tables + i;

		if (0 != tbl_prepare (tbl)) {
			log_err ("Failed to prepare and parse table \"%s\".", tbl->file);
			continue;
		}

		if (0 == tbl_read_table (tbl))
			status = 0;

		tbl_finish (tbl);
	}
	return status;
} /* tbl_read */

static int tbl_shutdown (void)
{
	size_t i;

	for (i = 0; i < tables_num; ++i)
		tbl_clear (&tables[i]);
	sfree (tables);
	return 0;
} /* tbl_shutdown */

static int tbl_init (void)
{
	if (0 == tables_num)
		return 0;

	plugin_register_read ("table", tbl_read);
	plugin_register_shutdown ("table", tbl_shutdown);
	return 0;
} /* tbl_init */

void module_register (void)
{
	plugin_register_complex_config ("table", tbl_config);
	plugin_register_init ("table", tbl_init);
} /* module_register */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */
