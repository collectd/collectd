/**
 * collectd - src/configfile.c
 * Copyright (C) 2005-2008  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"

#include "liboconfig/oconfig.h"

#include "common.h"
#include "plugin.h"
#include "configfile.h"

#define ESCAPE_NULL(str) ((str) == NULL ? "(null)" : (str))

/*
 * Private types
 */
typedef struct cf_callback
{
	const char  *type;
	int  (*callback) (const char *, const char *);
	const char **keys;
	int    keys_num;
	struct cf_callback *next;
} cf_callback_t;

typedef struct cf_complex_callback_s
{
	char *type;
	int (*callback) (oconfig_item_t *);
	struct cf_complex_callback_s *next;
} cf_complex_callback_t;

typedef struct cf_value_map_s
{
	char *key;
	int (*func) (const oconfig_item_t *);
} cf_value_map_t;

typedef struct cf_global_option_s
{
	char *key;
	char *value;
	char *def;
} cf_global_option_t;

/*
 * Prototypes of callback functions
 */
static int dispatch_value_plugindir (const oconfig_item_t *ci);
static int dispatch_value_loadplugin (const oconfig_item_t *ci);

/*
 * Private variables
 */
static cf_callback_t *first_callback = NULL;
static cf_complex_callback_t *complex_callback_head = NULL;

static cf_value_map_t cf_value_map[] =
{
	{"PluginDir",  dispatch_value_plugindir},
	{"LoadPlugin", dispatch_value_loadplugin}
};
static int cf_value_map_num = STATIC_ARRAY_LEN (cf_value_map);

static cf_global_option_t cf_global_options[] =
{
	{"BaseDir",   NULL, PKGLOCALSTATEDIR},
	{"PIDFile",   NULL, PIDFILE},
	{"Hostname",  NULL, NULL},
	{"Interval",  NULL, "10"},
	{"ReadThreads", NULL, "5"},
	{"TypesDB",   NULL, PLUGINDIR"/types.db"} /* FIXME: Configure path */
};
static int cf_global_options_num = STATIC_ARRAY_LEN (cf_global_options);

/*
 * Functions to handle register/unregister, search, and other plugin related
 * stuff
 */
static cf_callback_t *cf_search (const char *type)
{
	cf_callback_t *cf_cb;

	if (type == NULL)
		return (NULL);

	for (cf_cb = first_callback; cf_cb != NULL; cf_cb = cf_cb->next)
		if (strcasecmp (cf_cb->type, type) == 0)
			break;

	return (cf_cb);
}

static int cf_dispatch (const char *type, const char *orig_key,
		const char *orig_value)
{
	cf_callback_t *cf_cb;
	char *key;
	char *value;
	int ret;
	int i;

	DEBUG ("type = %s, key = %s, value = %s",
			ESCAPE_NULL(type),
			ESCAPE_NULL(orig_key),
			ESCAPE_NULL(orig_value));

	if ((cf_cb = cf_search (type)) == NULL)
	{
		WARNING ("Found a configuration for the `%s' plugin, but "
				"the plugin isn't loaded or didn't register "
				"a configuration callback.", type);
		return (-1);
	}

	if ((key = strdup (orig_key)) == NULL)
		return (1);
	if ((value = strdup (orig_value)) == NULL)
	{
		free (key);
		return (2);
	}

	ret = -1;

	for (i = 0; i < cf_cb->keys_num; i++)
	{
		if (strcasecmp (cf_cb->keys[i], key) == 0)
		{
			ret = (*cf_cb->callback) (key, value);
			break;
		}
	}

	if (i >= cf_cb->keys_num)
		WARNING ("Plugin `%s' did not register for value `%s'.", type, key);

	free (key);
	free (value);

	DEBUG ("return (%i)", ret);

	return (ret);
} /* int cf_dispatch */

static int dispatch_global_option (const oconfig_item_t *ci)
{
	if (ci->values_num != 1)
		return (-1);
	if (ci->values[0].type == OCONFIG_TYPE_STRING)
		return (global_option_set (ci->key, ci->values[0].value.string));
	else if (ci->values[0].type == OCONFIG_TYPE_NUMBER)
	{
		char tmp[128];
		snprintf (tmp, sizeof (tmp), "%lf", ci->values[0].value.number);
		tmp[127] = '\0';
		return (global_option_set (ci->key, tmp));
	}

	return (-1);
} /* int dispatch_global_option */

static int dispatch_value_plugindir (const oconfig_item_t *ci)
{
	assert (strcasecmp (ci->key, "PluginDir") == 0);
	
	if (ci->values_num != 1)
		return (-1);
	if (ci->values[0].type != OCONFIG_TYPE_STRING)
		return (-1);

	plugin_set_dir (ci->values[0].value.string);
	return (0);
}

static int dispatch_value_loadplugin (const oconfig_item_t *ci)
{
	assert (strcasecmp (ci->key, "LoadPlugin") == 0);

	if (ci->values_num != 1)
		return (-1);
	if (ci->values[0].type != OCONFIG_TYPE_STRING)
		return (-1);

	return (plugin_load (ci->values[0].value.string));
} /* int dispatch_value_loadplugin */

static int dispatch_value_plugin (const char *plugin, oconfig_item_t *ci)
{
	char  buffer[4096];
	char *buffer_ptr;
	int   buffer_free;
	int i;

	buffer_ptr = buffer;
	buffer_free = sizeof (buffer);

	for (i = 0; i < ci->values_num; i++)
	{
		int status = -1;

		if (ci->values[i].type == OCONFIG_TYPE_STRING)
			status = snprintf (buffer_ptr, buffer_free, " %s",
					ci->values[i].value.string);
		else if (ci->values[i].type == OCONFIG_TYPE_NUMBER)
			status = snprintf (buffer_ptr, buffer_free, " %lf",
					ci->values[i].value.number);
		else if (ci->values[i].type == OCONFIG_TYPE_BOOLEAN)
			status = snprintf (buffer_ptr, buffer_free, " %s",
					ci->values[i].value.boolean
					? "true" : "false");

		if ((status < 0) || (status >= buffer_free))
			return (-1);
		buffer_free -= status;
		buffer_ptr  += status;
	}
	/* skip the initial space */
	buffer_ptr = buffer + 1;

	return (cf_dispatch (plugin, ci->key, buffer_ptr));
} /* int plugin_conf_dispatch */

static int dispatch_value (const oconfig_item_t *ci)
{
	int ret = -2;
	int i;

	for (i = 0; i < cf_value_map_num; i++)
		if (strcasecmp (cf_value_map[i].key, ci->key) == 0)
		{
			ret = cf_value_map[i].func (ci);
			break;
		}

	for (i = 0; i < cf_global_options_num; i++)
		if (strcasecmp (cf_global_options[i].key, ci->key) == 0)
		{
			ret = dispatch_global_option (ci);
			break;
		}

	return (ret);
} /* int dispatch_value */

static int dispatch_block_plugin (oconfig_item_t *ci)
{
	int i;
	char *name;

	cf_complex_callback_t *cb;

	if (strcasecmp (ci->key, "Plugin") != 0)
		return (-1);
	if (ci->values_num < 1)
		return (-1);
	if (ci->values[0].type != OCONFIG_TYPE_STRING)
		return (-1);

	name = ci->values[0].value.string;

	/* Check for a complex callback first */
	for (cb = complex_callback_head; cb != NULL; cb = cb->next)
		if (strcasecmp (name, cb->type) == 0)
			return (cb->callback (ci));

	/* Hm, no complex plugin found. Dispatch the values one by one */
	for (i = 0; i < ci->children_num; i++)
	{
		if (ci->children[i].children == NULL)
			dispatch_value_plugin (name, ci->children + i);
		else
			{DEBUG ("No nested config blocks allow for this plugin.");}
	}

	return (0);
}


static int dispatch_block (oconfig_item_t *ci)
{
	if (strcasecmp (ci->key, "Plugin") == 0)
		return (dispatch_block_plugin (ci));

	return (0);
}

#define CF_MAX_DEPTH 8
static oconfig_item_t *cf_read_file (const char *file, int depth);

static int cf_include_all (oconfig_item_t *root, int depth)
{
	int i;

	for (i = 0; i < root->children_num; i++)
	{
		oconfig_item_t *new;
		oconfig_item_t *old;

		/* Ignore all blocks, including `Include' blocks. */
		if (root->children[i].children_num != 0)
			continue;

		if (strcasecmp (root->children[i].key, "Include") != 0)
			continue;

		old = root->children + i;

		if ((old->values_num != 1)
				|| (old->values[0].type != OCONFIG_TYPE_STRING))
		{
			ERROR ("configfile: `Include' needs exactly one string argument.");
			continue;
		}

		new = cf_read_file (old->values[0].value.string, depth + 1);
		if (new == NULL)
			continue;

		/* There are more children now. We need to expand
		 * root->children. */
		if (new->children_num > 1)
		{
			oconfig_item_t *temp;

			DEBUG ("configfile: Resizing root-children from %i to %i elements.",
					root->children_num,
					root->children_num + new->children_num - 1);

			temp = (oconfig_item_t *) realloc (root->children,
					sizeof (oconfig_item_t)
					* (root->children_num + new->children_num - 1));
			if (temp == NULL)
			{
				ERROR ("configfile: realloc failed.");
				oconfig_free (new);
				continue;
			}
			root->children = temp;
		}

		/* Clean up the old include directive while we still have a
		 * valid pointer */
		DEBUG ("configfile: Cleaning up `old'");
		/* sfree (old->values[0].value.string); */
		sfree (old->values);

		/* If there are trailing children and the number of children
		 * changes, we need to move the trailing ones either one to the
		 * front or (new->num - 1) to the back */
		if (((root->children_num - i) > 1)
				&& (new->children_num != 1))
		{
			DEBUG ("configfile: Moving trailing children.");
			memmove (root->children + i + new->children_num,
					root->children + i + 1,
					sizeof (oconfig_item_t)
					* (root->children_num - (i + 1)));
		}

		/* Now copy the new children to where the include statement was */
		if (new->children_num > 0)
		{
			DEBUG ("configfile: Copying new children.");
			memcpy (root->children + i,
					new->children,
					sizeof (oconfig_item_t)
					* new->children_num);
		}

		/* Adjust the number of children and the position in the list. */
		root->children_num = root->children_num + new->children_num - 1;
		i = i + new->children_num - 1;

		/* Clean up the `new' struct. We set `new->children' to NULL so
		 * the stuff we've just copied pointers to isn't freed by
		 * `oconfig_free' */
		DEBUG ("configfile: Cleaning up `new'");
		sfree (new->values); /* should be NULL anyway */
		sfree (new);
		new = NULL;
	} /* for (i = 0; i < root->children_num; i++) */

	return (0);
} /* int cf_include_all */

static oconfig_item_t *cf_read_file (const char *file, int depth)
{
	oconfig_item_t *root;

	if (depth >= CF_MAX_DEPTH)
	{
		ERROR ("configfile: Not including `%s' because the maximum nesting depth has been reached.",
				file);
		return (NULL);
	}

	root = oconfig_parse_file (file);
	if (root == NULL)
	{
		ERROR ("configfile: Cannot read file `%s'.", file);
		return (NULL);
	}

	cf_include_all (root, depth);

	return (root);
} /* oconfig_item_t *cf_read_file */

/* 
 * Public functions
 */
int global_option_set (const char *option, const char *value)
{
	int i;

	DEBUG ("option = %s; value = %s;", option, value);

	for (i = 0; i < cf_global_options_num; i++)
		if (strcasecmp (cf_global_options[i].key, option) == 0)
			break;

	if (i >= cf_global_options_num)
		return (-1);

	sfree (cf_global_options[i].value);

	if (value != NULL)
		cf_global_options[i].value = strdup (value);
	else
		cf_global_options[i].value = NULL;

	return (0);
}

const char *global_option_get (const char *option)
{
	int i;

	for (i = 0; i < cf_global_options_num; i++)
		if (strcasecmp (cf_global_options[i].key, option) == 0)
			break;

	if (i >= cf_global_options_num)
		return (NULL);
	
	return ((cf_global_options[i].value != NULL)
			? cf_global_options[i].value
			: cf_global_options[i].def);
} /* char *global_option_get */

void cf_unregister (const char *type)
{
	cf_callback_t *this, *prev;

	for (prev = NULL, this = first_callback;
			this != NULL;
			prev = this, this = this->next)
		if (strcasecmp (this->type, type) == 0)
		{
			if (prev == NULL)
				first_callback = this->next;
			else
				prev->next = this->next;

			free (this);
			break;
		}
} /* void cf_unregister */

void cf_unregister_complex (const char *type)
{
	cf_complex_callback_t *this, *prev;

	for (prev = NULL, this = complex_callback_head;
			this != NULL;
			prev = this, this = this->next)
		if (strcasecmp (this->type, type) == 0)
		{
			if (prev == NULL)
				complex_callback_head = this->next;
			else
				prev->next = this->next;

			sfree (this->type);
			sfree (this);
			break;
		}
} /* void cf_unregister */

void cf_register (const char *type,
		int (*callback) (const char *, const char *),
		const char **keys, int keys_num)
{
	cf_callback_t *cf_cb;

	/* Remove this module from the list, if it already exists */
	cf_unregister (type);

	/* This pointer will be free'd in `cf_unregister' */
	if ((cf_cb = (cf_callback_t *) malloc (sizeof (cf_callback_t))) == NULL)
		return;

	cf_cb->type     = type;
	cf_cb->callback = callback;
	cf_cb->keys     = keys;
	cf_cb->keys_num = keys_num;

	cf_cb->next = first_callback;
	first_callback = cf_cb;
} /* void cf_register */

int cf_register_complex (const char *type, int (*callback) (oconfig_item_t *))
{
	cf_complex_callback_t *new;

	new = (cf_complex_callback_t *) malloc (sizeof (cf_complex_callback_t));
	if (new == NULL)
		return (-1);

	new->type = strdup (type);
	if (new->type == NULL)
	{
		sfree (new);
		return (-1);
	}

	new->callback = callback;
	new->next = NULL;

	if (complex_callback_head == NULL)
	{
		complex_callback_head = new;
	}
	else
	{
		cf_complex_callback_t *last = complex_callback_head;
		while (last->next != NULL)
			last = last->next;
		last->next = new;
	}

	return (0);
} /* int cf_register_complex */

int cf_read (char *filename)
{
	oconfig_item_t *conf;
	int i;

	conf = cf_read_file (filename, 0 /* depth */);
	if (conf == NULL)
	{
		ERROR ("Unable to read config file %s.", filename);
		return (-1);
	}

	for (i = 0; i < conf->children_num; i++)
	{
		if (conf->children[i].children == NULL)
			dispatch_value (conf->children + i);
		else
			dispatch_block (conf->children + i);
	}

	return (0);
} /* int cf_read */
