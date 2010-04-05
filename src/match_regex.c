/**
 * collectd - src/match_regex.c
 * Copyright (C) 2008  Sebastian Harl
 * Copyright (C) 2008  Florian Forster
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
 *   Florian Forster <octo at verplant.org>
 **/

/*
 * This module allows to filter and rewrite value lists based on
 * Perl-compatible regular expressions.
 */

#include "collectd.h"
#include "filter_chain.h"

#include <sys/types.h>
#include <regex.h>

#define log_err(...) ERROR ("`regex' match: " __VA_ARGS__)
#define log_warn(...) WARNING ("`regex' match: " __VA_ARGS__)

/*
 * private data types
 */

struct mr_regex_s;
typedef struct mr_regex_s mr_regex_t;
struct mr_regex_s
{
	regex_t re;
	char *re_str;

	mr_regex_t *next;
};

struct mr_match_s;
typedef struct mr_match_s mr_match_t;
struct mr_match_s
{
	mr_regex_t *host;
	mr_regex_t *plugin;
	mr_regex_t *plugin_instance;
	mr_regex_t *type;
	mr_regex_t *type_instance;
	_Bool invert;
};

/*
 * internal helper functions
 */
static void mr_free_regex (mr_regex_t *r) /* {{{ */
{
	if (r == NULL)
		return;

	regfree (&r->re);
	memset (&r->re, 0, sizeof (r->re));
	free (r->re_str);

	if (r->next != NULL)
		mr_free_regex (r->next);
} /* }}} void mr_free_regex */

static void mr_free_match (mr_match_t *m) /* {{{ */
{
	if (m == NULL)
		return;

	mr_free_regex (m->host);
	mr_free_regex (m->plugin);
	mr_free_regex (m->plugin_instance);
	mr_free_regex (m->type);
	mr_free_regex (m->type_instance);

	free (m);
} /* }}} void mr_free_match */

static int mr_match_regexen (mr_regex_t *re_head, /* {{{ */
		const char *string)
{
	mr_regex_t *re;

	if (re_head == NULL)
		return (FC_MATCH_MATCHES);

	for (re = re_head; re != NULL; re = re->next)
	{
		int status;

		status = regexec (&re->re, string,
				/* nmatch = */ 0, /* pmatch = */ NULL,
				/* eflags = */ 0);
		if (status == 0)
		{
			DEBUG ("regex match: Regular expression `%s' matches `%s'.",
					re->re_str, string);
		}
		else
		{
			DEBUG ("regex match: Regular expression `%s' does not match `%s'.",
					re->re_str, string);
			return (FC_MATCH_NO_MATCH);
		}

	}

	return (FC_MATCH_MATCHES);
} /* }}} int mr_match_regexen */

static int mr_config_add_regex (mr_regex_t **re_head, /* {{{ */
		oconfig_item_t *ci)
{
	mr_regex_t *re;
	int status;

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		log_warn ("`%s' needs exactly one string argument.", ci->key);
		return (-1);
	}

	re = (mr_regex_t *) malloc (sizeof (*re));
	if (re == NULL)
	{
		log_err ("mr_config_add_regex: malloc failed.");
		return (-1);
	}
	memset (re, 0, sizeof (*re));
	re->next = NULL;

	re->re_str = strdup (ci->values[0].value.string);
	if (re->re_str == NULL)
	{
		free (re);
		log_err ("mr_config_add_regex: strdup failed.");
		return (-1);
	}

	status = regcomp (&re->re, re->re_str, REG_EXTENDED | REG_NOSUB);
	if (status != 0)
	{
		char errmsg[1024];
		regerror (status, &re->re, errmsg, sizeof (errmsg));
		errmsg[sizeof (errmsg) - 1] = 0;
		log_err ("Compiling regex `%s' for `%s' failed: %s.", 
				re->re_str, ci->key, errmsg);
		free (re->re_str);
		free (re);
		return (-1);
	}

	if (*re_head == NULL)
	{
		*re_head = re;
	}
	else
	{
		mr_regex_t *ptr;

		ptr = *re_head;
		while (ptr->next != NULL)
			ptr = ptr->next;

		ptr->next = re;
	}

	return (0);
} /* }}} int mr_config_add_regex */

static int mr_create (const oconfig_item_t *ci, void **user_data) /* {{{ */
{
	mr_match_t *m;
	int status;
	int i;

	m = (mr_match_t *) malloc (sizeof (*m));
	if (m == NULL)
	{
		log_err ("mr_create: malloc failed.");
		return (-ENOMEM);
	}
	memset (m, 0, sizeof (*m));
	
	m->invert = 0;

	status = 0;
	for (i = 0; i < ci->children_num; i++)
	{
		oconfig_item_t *child = ci->children + i;

		if ((strcasecmp ("Host", child->key) == 0)
				|| (strcasecmp ("Hostname", child->key) == 0))
			status = mr_config_add_regex (&m->host, child);
		else if (strcasecmp ("Plugin", child->key) == 0)
			status = mr_config_add_regex (&m->plugin, child);
		else if (strcasecmp ("PluginInstance", child->key) == 0)
			status = mr_config_add_regex (&m->plugin_instance, child);
		else if (strcasecmp ("Type", child->key) == 0)
			status = mr_config_add_regex (&m->type, child);
		else if (strcasecmp ("TypeInstance", child->key) == 0)
			status = mr_config_add_regex (&m->type_instance, child);
		else if (strcasecmp ("Invert", child->key) == 0)
			status = cf_util_get_boolean(child, &m->invert);
		else
		{
			log_err ("The `%s' configuration option is not understood and "
					"will be ignored.", child->key);
			status = 0;
		}

		if (status != 0)
			break;
	}

	/* Additional sanity-checking */
	while (status == 0)
	{
		if ((m->host == NULL)
				&& (m->plugin == NULL)
				&& (m->plugin_instance == NULL)
				&& (m->type == NULL)
				&& (m->type_instance == NULL))
		{
			log_err ("No (valid) regular expressions have been configured. "
					"This match will be ignored.");
			status = -1;
		}

		break;
	}

	if (status != 0)
	{
		mr_free_match (m);
		return (status);
	}

	*user_data = m;
	return (0);
} /* }}} int mr_create */

static int mr_destroy (void **user_data) /* {{{ */
{
	if ((user_data != NULL) && (*user_data != NULL))
		mr_free_match (*user_data);
	return (0);
} /* }}} int mr_destroy */

static int mr_match (const data_set_t __attribute__((unused)) *ds, /* {{{ */
		const value_list_t *vl,
		notification_meta_t __attribute__((unused)) **meta,
		void **user_data)
{
	mr_match_t *m;
	int match_value = FC_MATCH_MATCHES;
	int nomatch_value = FC_MATCH_NO_MATCH;

	if ((user_data == NULL) || (*user_data == NULL))
		return (-1);

	m = *user_data;

	if (m->invert)
	{
		match_value = FC_MATCH_NO_MATCH;
		nomatch_value = FC_MATCH_MATCHES;
	}

	if (mr_match_regexen (m->host, vl->host) == FC_MATCH_NO_MATCH)
		return (nomatch_value);
	if (mr_match_regexen (m->plugin, vl->plugin) == FC_MATCH_NO_MATCH)
		return (nomatch_value);
	if (mr_match_regexen (m->plugin_instance,
				vl->plugin_instance) == FC_MATCH_NO_MATCH)
		return (nomatch_value);
	if (mr_match_regexen (m->type, vl->type) == FC_MATCH_NO_MATCH)
		return (nomatch_value);
	if (mr_match_regexen (m->type_instance,
				vl->type_instance) == FC_MATCH_NO_MATCH)
		return (nomatch_value);

	return (match_value);
} /* }}} int mr_match */

void module_register (void)
{
	match_proc_t mproc;

	memset (&mproc, 0, sizeof (mproc));
	mproc.create  = mr_create;
	mproc.destroy = mr_destroy;
	mproc.match   = mr_match;
	fc_register_match ("regex", mproc);
} /* module_register */

/* vim: set sw=4 ts=4 tw=78 noexpandtab fdm=marker : */

