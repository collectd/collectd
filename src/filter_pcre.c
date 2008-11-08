/**
 * collectd - src/filter_pcre.c
 * Copyright (C) 2008  Sebastian Harl
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
 * This module allows to filter and rewrite value lists based on
 * Perl-compatible regular expressions.
 */

#include "collectd.h"
#include "configfile.h"
#include "plugin.h"
#include "common.h"

#include "utils_subst.h"

#include <pcre.h>

#define log_err(...) ERROR ("filter_pcre: " __VA_ARGS__)
#define log_warn(...) WARNING ("filter_pcre: " __VA_ARGS__)

/*
 * private data types
 */

typedef struct {
	/* regular expression */
	pcre       *re;
	const char *re_str;

	/* extra information from studying the pattern */
	pcre_extra *extra;

	/* replacment text for string substitution */
	const char *replacement;
} c_pcre_t;

#define C_PCRE_INIT(regex) do { \
		(regex).re     = NULL; \
		(regex).re_str = NULL; \
		(regex).extra  = NULL; \
		(regex).replacement = NULL; \
	} while (0)

#define C_PCRE_FREE(regex) do { \
		pcre_free ((regex).re); \
		free ((void *)(regex).re_str); \
		pcre_free ((regex).extra); \
		free ((void *)(regex).replacement); \
		C_PCRE_INIT (regex); \
	} while (0)

typedef struct {
	c_pcre_t host;
	c_pcre_t plugin;
	c_pcre_t plugin_instance;
	c_pcre_t type;
	c_pcre_t type_instance;

	int action;
} regex_t;

typedef struct {
	int vec[30];
	int status;
} ovec_t;

typedef struct {
	ovec_t host;
	ovec_t plugin;
	ovec_t plugin_instance;
	ovec_t type;
	ovec_t type_instance;
} ovectors_t;

/*
 * private variables
 */

static regex_t *regexes     = NULL;
static int      regexes_num = 0;

/*
 * internal helper functions
 */

/* returns true if string matches the regular expression */
static int c_pcre_match (c_pcre_t *re, const char *string, ovec_t *ovec)
{
	if ((NULL == re) || (NULL == re->re))
		return 1;

	if (NULL == string)
		string = "";

	ovec->status = pcre_exec (re->re,
			/* extra       = */ re->extra,
			/* subject     = */ string,
			/* length      = */ strlen (string),
			/* startoffset = */ 0,
			/* options     = */ 0,
			/* ovector     = */ ovec->vec,
			/* ovecsize    = */ STATIC_ARRAY_SIZE (ovec->vec));

	if (0 <= ovec->status)
		return 1;

	if (PCRE_ERROR_NOMATCH != ovec->status)
		log_err ("PCRE matching of string \"%s\" failed with status %d",
				string, ovec->status);
	return 0;
} /* c_pcre_match */

static int c_pcre_subst (c_pcre_t *re, char *string, size_t strlen,
		ovec_t *ovec)
{
	char buffer[strlen];

	if ((NULL == re) || (NULL == re->replacement))
		return 0;

	assert (0 <= ovec->status);

	if (NULL == subst (buffer, sizeof (buffer), string,
				ovec->vec[0], ovec->vec[1], re->replacement)) {
		log_err ("Substitution in string \"%s\" (using regex \"%s\" and "
				"replacement string \"%s\") failed.",
				string, re->re_str, re->replacement);
		return -1;
	}

	sstrncpy (string, buffer, strlen);
	return 0;
} /* c_pcre_subst */

static regex_t *regex_new (void)
{
	regex_t *re;
	regex_t *temp;

	temp = (regex_t *) realloc (regexes, (regexes_num + 1)
			* sizeof (*regexes));
	if (NULL == temp) {
		log_err ("Out of memory.");
		return NULL;
	}
	regexes = temp;
	regexes_num++;

	re = regexes + (regexes_num - 1);

	C_PCRE_INIT (re->host);
	C_PCRE_INIT (re->plugin);
	C_PCRE_INIT (re->plugin_instance);
	C_PCRE_INIT (re->type);
	C_PCRE_INIT (re->type_instance);

	re->action = 0;
	return re;
} /* regex_new */

static void regex_delete (regex_t *re)
{
	if (NULL == re)
		return;

	C_PCRE_FREE (re->host);
	C_PCRE_FREE (re->plugin);
	C_PCRE_FREE (re->plugin_instance);
	C_PCRE_FREE (re->type);
	C_PCRE_FREE (re->type_instance);

	re->action = 0;
} /* regex_delete */

/* returns true if the value list matches the regular expression */
static int regex_match (regex_t *re, value_list_t *vl, ovectors_t *ovectors)
{
	int matches = 0;

	if (NULL == re)
		return 1;

	if (c_pcre_match (&re->host, vl->host, &ovectors->host))
		++matches;

	if (c_pcre_match (&re->plugin, vl->plugin, &ovectors->plugin))
		++matches;

	if (c_pcre_match (&re->plugin_instance, vl->plugin_instance,
				&ovectors->plugin_instance))
		++matches;

	if (c_pcre_match (&re->type, vl->type, &ovectors->type))
		++matches;

	if (c_pcre_match (&re->type_instance, vl->type_instance,
				&ovectors->type_instance))
		++matches;

	if (5 == matches)
		return 1;
	return 0;
} /* regex_match */

static int regex_subst (regex_t *re, value_list_t *vl, ovectors_t *ovectors)
{
	if (NULL == re)
		return 0;

	c_pcre_subst (&re->host, vl->host, sizeof (vl->host),
			&ovectors->host);
	c_pcre_subst (&re->plugin, vl->plugin, sizeof (vl->plugin),
			&ovectors->plugin);
	c_pcre_subst (&re->plugin_instance, vl->plugin_instance,
			sizeof (vl->plugin_instance), &ovectors->plugin_instance);
	c_pcre_subst (&re->type, vl->type, sizeof (vl->type),
			&ovectors->type);
	c_pcre_subst (&re->type_instance, vl->type_instance,
			sizeof (vl->type_instance), &ovectors->type_instance);
	return 0;
} /* regex_subst */

/*
 * interface to collectd
 */

static int c_pcre_filter (const data_set_t *ds, value_list_t *vl)
{
	int i;

	ovectors_t ovectors;

	for (i = 0; i < regexes_num; ++i)
		if (regex_match (regexes + i, vl, &ovectors)) {
			regex_subst (regexes + i, vl, &ovectors);
			return regexes[i].action;
		}
	return 0;
} /* c_pcre_filter */

static int c_pcre_shutdown (void)
{
	int i;

	plugin_unregister_filter ("filter_pcre");
	plugin_unregister_shutdown ("filter_pcre");

	for (i = 0; i < regexes_num; ++i)
		regex_delete (regexes + i);

	sfree (regexes);
	regexes_num = 0;
	return 0;
} /* c_pcre_shutdown */

static int config_set_regex (c_pcre_t *re, oconfig_item_t *ci)
{
	const char *pattern;
	const char *errptr;
	int erroffset;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("<RegEx>: %s expects a single string argument.", ci->key);
		return 1;
	}

	pattern = ci->values[0].value.string;

	re->re = pcre_compile (pattern,
			/* options   = */ 0,
			/* errptr    = */ &errptr,
			/* erroffset = */ &erroffset,
			/* tableptr  = */ NULL);

	if (NULL == re->re) {
		log_err ("<RegEx>: PCRE compilation of pattern \"%s\" failed "
				"at offset %d: %s", pattern, erroffset, errptr);
		return 1;
	}

	re->re_str = sstrdup (pattern);

	re->extra = pcre_study (re->re,
			/* options = */ 0,
			/* errptr  = */ &errptr);

	if (NULL != errptr) {
		log_err ("<RegEx>: PCRE studying of pattern \"%s\" failed: %s",
				pattern, errptr);
		return 1;
	}
	return 0;
} /* config_set_regex */

static int config_set_replacement (c_pcre_t *re, oconfig_item_t *ci)
{
	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("<RegEx>: %s expects a single string argument.", ci->key);
		return 1;
	}

	if (NULL == re->re) {
		log_err ("<RegEx>: %s without an appropriate regex (%s) "
				"is not allowed.", ci->key, ci->key + strlen ("Substitute"));
		return 1;
	}

	re->replacement = sstrdup (ci->values[0].value.string);
	return 0;
} /* config_set_replacement */

static int config_set_action (int *action, oconfig_item_t *ci)
{
	const char *action_str;

	if ((0 != ci->children_num) || (1 != ci->values_num)
			|| (OCONFIG_TYPE_STRING != ci->values[0].type)) {
		log_err ("<RegEx>: Action expects a single string argument.");
		return 1;
	}

	action_str = ci->values[0].value.string;

	if (0 == strcasecmp (action_str, "NoWrite"))
		*action |= FILTER_NOWRITE;
	else if (0 == strcasecmp (action_str, "NoThresholdCheck"))
		*action |= FILTER_NOTHRESHOLD_CHECK;
	else if (0 == strcasecmp (action_str, "Ignore"))
		*action |= FILTER_IGNORE;
	else
		log_warn ("<Regex>: Ignoring unknown action \"%s\".", action_str);
	return 0;
} /* config_set_action */

static int c_pcre_config_regex (oconfig_item_t *ci)
{
	regex_t *re;
	int i;

	if (0 != ci->values_num) {
		log_err ("<RegEx> expects no arguments.");
		return 1;
	}

	re = regex_new ();
	if (NULL == re)
		return -1;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;
		int status = 0;

		if (0 == strcasecmp (c->key, "Host"))
			status = config_set_regex (&re->host, c);
		else if (0 == strcasecmp (c->key, "Plugin"))
			status = config_set_regex (&re->plugin, c);
		else if (0 == strcasecmp (c->key, "PluginInstance"))
			status = config_set_regex (&re->plugin_instance, c);
		else if (0 == strcasecmp (c->key, "Type"))
			status = config_set_regex (&re->type, c);
		else if (0 == strcasecmp (c->key, "TypeInstance"))
			status = config_set_regex (&re->type_instance, c);
		else if (0 == strcasecmp (c->key, "Action"))
			status = config_set_action (&re->action, c);
		else if (0 == strcasecmp (c->key, "SubstituteHost"))
			status = config_set_replacement (&re->host, c);
		else if (0 == strcasecmp (c->key, "SubstitutePlugin"))
			status = config_set_replacement (&re->plugin, c);
		else if (0 == strcasecmp (c->key, "SubstitutePluginInstance"))
			status = config_set_replacement (&re->plugin_instance, c);
		else if (0 == strcasecmp (c->key, "SubstituteType"))
			status = config_set_replacement (&re->type, c);
		else if (0 == strcasecmp (c->key, "SubstituteTypeInstance"))
			status = config_set_replacement (&re->type_instance, c);
		else
			log_warn ("<RegEx>: Ignoring unknown config key \"%s\".", c->key);

		if (0 != status) {
			log_err ("Ignoring regular expression definition.");
			regex_delete (re);
			--regexes_num;
		}
	}
	return 0;
} /* c_pcre_config_regex */

static int c_pcre_config (oconfig_item_t *ci)
{
	int i;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *c = ci->children + i;

		if (0 == strcasecmp (c->key, "RegEx"))
			c_pcre_config_regex (c);
		else
			log_warn ("Ignoring unknown config key \"%s\".", c->key);
	}

	plugin_register_filter ("filter_pcre", c_pcre_filter);
	plugin_register_shutdown ("filter_pcre", c_pcre_shutdown);
	return 0;
} /* c_pcre_config */

void module_register (void)
{
	plugin_register_complex_config ("filter_pcre", c_pcre_config);
} /* module_register */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */

