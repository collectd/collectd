/**
 * collectd - src/configfile.c
 * Copyright (C) 2005-2011  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian tokkee Harl <sh at tokkee.org>
 **/

#include "collectd.h"

#include "liboconfig/oconfig.h"

#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "types_list.h"
#include "filter_chain.h"

#if HAVE_WORDEXP_H
# include <wordexp.h>
#endif /* HAVE_WORDEXP_H */

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
static int dispatch_value_typesdb (const oconfig_item_t *ci);
static int dispatch_value_plugindir (const oconfig_item_t *ci);
static int dispatch_loadplugin (const oconfig_item_t *ci);

/*
 * Private variables
 */
static cf_callback_t *first_callback = NULL;
static cf_complex_callback_t *complex_callback_head = NULL;

static cf_value_map_t cf_value_map[] =
{
	{"TypesDB",    dispatch_value_typesdb},
	{"PluginDir",  dispatch_value_plugindir},
	{"LoadPlugin", dispatch_loadplugin}
};
static int cf_value_map_num = STATIC_ARRAY_LEN (cf_value_map);

static cf_global_option_t cf_global_options[] =
{
	{"BaseDir",     NULL, PKGLOCALSTATEDIR},
	{"PIDFile",     NULL, PIDFILE},
	{"Hostname",    NULL, NULL},
	{"FQDNLookup",  NULL, "true"},
	{"Interval",    NULL, "10"},
	{"ReadThreads", NULL, "5"},
	{"Timeout",     NULL, "2"},
	{"PreCacheChain",  NULL, "PreCache"},
	{"PostCacheChain", NULL, "PostCache"}
};
static int cf_global_options_num = STATIC_ARRAY_LEN (cf_global_options);

static int cf_default_typesdb = 1;

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
		if ((cf_cb->keys[i] != NULL)
				&& (strcasecmp (cf_cb->keys[i], key) == 0))
		{
			ret = (*cf_cb->callback) (key, value);
			break;
		}
	}

	if (i >= cf_cb->keys_num)
		WARNING ("Plugin `%s' did not register for value `%s'.", type, key);

	free (key);
	free (value);

	DEBUG ("cf_dispatch: return (%i)", ret);

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
		ssnprintf (tmp, sizeof (tmp), "%lf", ci->values[0].value.number);
		return (global_option_set (ci->key, tmp));
	}
	else if (ci->values[0].type == OCONFIG_TYPE_BOOLEAN)
	{
		if (ci->values[0].value.boolean)
			return (global_option_set (ci->key, "true"));
		else
			return (global_option_set (ci->key, "false"));
	}

	return (-1);
} /* int dispatch_global_option */

static int dispatch_value_typesdb (const oconfig_item_t *ci)
{
	int i = 0;

	assert (strcasecmp (ci->key, "TypesDB") == 0);

	cf_default_typesdb = 0;

	if (ci->values_num < 1) {
		ERROR ("configfile: `TypesDB' needs at least one argument.");
		return (-1);
	}

	for (i = 0; i < ci->values_num; ++i)
	{
		if (OCONFIG_TYPE_STRING != ci->values[i].type) {
			WARNING ("configfile: TypesDB: Skipping %i. argument which "
					"is not a string.", i + 1);
			continue;
		}

		read_types_list (ci->values[i].value.string);
	}
	return (0);
} /* int dispatch_value_typesdb */

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

static int dispatch_loadplugin (const oconfig_item_t *ci)
{
	int i;
	const char *name;
	unsigned int flags = 0;
	assert (strcasecmp (ci->key, "LoadPlugin") == 0);

	if (ci->values_num != 1)
		return (-1);
	if (ci->values[0].type != OCONFIG_TYPE_STRING)
		return (-1);

	name = ci->values[0].value.string;

	/*
	 * XXX: Magic at work:
	 *
	 * Some of the language bindings, for example the Python and Perl
	 * plugins, need to be able to export symbols to the scripts they run.
	 * For this to happen, the "Globals" flag needs to be set.
	 * Unfortunately, this technical detail is hard to explain to the
	 * average user and she shouldn't have to worry about this, ideally.
	 * So in order to save everyone's sanity use a different default for a
	 * handful of special plugins. --octo
	 */
	if ((strcasecmp ("Perl", name) == 0)
			|| (strcasecmp ("Python", name) == 0))
		flags |= PLUGIN_FLAGS_GLOBAL;

	for (i = 0; i < ci->children_num; ++i) {
		if (strcasecmp("Globals", ci->children[i].key) == 0)
			cf_util_get_flag (ci->children + i, &flags, PLUGIN_FLAGS_GLOBAL);
		else {
			WARNING("Ignoring unknown LoadPlugin option \"%s\" "
					"for plugin \"%s\"",
					ci->children[i].key, ci->values[0].value.string);
		}
	}

	return (plugin_load (name, (uint32_t) flags));
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
			status = ssnprintf (buffer_ptr, buffer_free, " %s",
					ci->values[i].value.string);
		else if (ci->values[i].type == OCONFIG_TYPE_NUMBER)
			status = ssnprintf (buffer_ptr, buffer_free, " %lf",
					ci->values[i].value.number);
		else if (ci->values[i].type == OCONFIG_TYPE_BOOLEAN)
			status = ssnprintf (buffer_ptr, buffer_free, " %s",
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
} /* int dispatch_value_plugin */

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
		{
			WARNING ("There is a `%s' block within the "
					"configuration for the %s plugin. "
					"The plugin either only expects "
					"\"simple\" configuration statements "
					"or wasn't loaded using `LoadPlugin'."
					" Please check your configuration.",
					ci->children[i].key, name);
		}
	}

	return (0);
}


static int dispatch_block (oconfig_item_t *ci)
{
	if (strcasecmp (ci->key, "LoadPlugin") == 0)
		return (dispatch_loadplugin (ci));
	else if (strcasecmp (ci->key, "Plugin") == 0)
		return (dispatch_block_plugin (ci));
	else if (strcasecmp (ci->key, "Chain") == 0)
		return (fc_configure (ci));

	return (0);
}

static int cf_ci_replace_child (oconfig_item_t *dst, oconfig_item_t *src,
		int offset)
{
	oconfig_item_t *temp;
	int i;

	assert (offset >= 0);
	assert (dst->children_num > offset);

	/* Free the memory used by the replaced child. Usually that's the
	 * `Include "blah"' statement. */
	temp = dst->children + offset;
	for (i = 0; i < temp->values_num; i++)
	{
		if (temp->values[i].type == OCONFIG_TYPE_STRING)
		{
			sfree (temp->values[i].value.string);
		}
	}
	sfree (temp->values);
	temp = NULL;

	/* If (src->children_num == 0) the array size is decreased. If offset
	 * is _not_ the last element, (offset < (dst->children_num - 1)), then
	 * we need to move the trailing elements before resizing the array. */
	if ((src->children_num == 0) && (offset < (dst->children_num - 1)))
	{
		int nmemb = dst->children_num - (offset + 1);
		memmove (dst->children + offset, dst->children + offset + 1,
				sizeof (oconfig_item_t) * nmemb);
	}

	/* Resize the memory containing the children to be big enough to hold
	 * all children. */
	temp = (oconfig_item_t *) realloc (dst->children,
			sizeof (oconfig_item_t)
			* (dst->children_num + src->children_num - 1));
	if (temp == NULL)
	{
		ERROR ("configfile: realloc failed.");
		return (-1);
	}
	dst->children = temp;

	/* If there are children behind the include statement, and they have
	 * not yet been moved because (src->children_num == 0), then move them
	 * to the end of the list, so that the new children have room before
	 * them. */
	if ((src->children_num > 0)
			&& ((dst->children_num - (offset + 1)) > 0))
	{
		int nmemb = dst->children_num - (offset + 1);
		int old_offset = offset + 1;
		int new_offset = offset + src->children_num;

		memmove (dst->children + new_offset,
				dst->children + old_offset,
				sizeof (oconfig_item_t) * nmemb);
	}

	/* Last but not least: If there are new children, copy them to the
	 * memory reserved for them. */
	if (src->children_num > 0)
	{
		memcpy (dst->children + offset,
				src->children,
				sizeof (oconfig_item_t) * src->children_num);
	}

	/* Update the number of children. */
	dst->children_num += (src->children_num - 1);

	return (0);
} /* int cf_ci_replace_child */

static int cf_ci_append_children (oconfig_item_t *dst, oconfig_item_t *src)
{
	oconfig_item_t *temp;

	if ((src == NULL) || (src->children_num == 0))
		return (0);

	temp = (oconfig_item_t *) realloc (dst->children,
			sizeof (oconfig_item_t)
			* (dst->children_num + src->children_num));
	if (temp == NULL)
	{
		ERROR ("configfile: realloc failed.");
		return (-1);
	}
	dst->children = temp;

	memcpy (dst->children + dst->children_num,
			src->children,
			sizeof (oconfig_item_t)
			* src->children_num);
	dst->children_num += src->children_num;

	return (0);
} /* int cf_ci_append_children */

#define CF_MAX_DEPTH 8
static oconfig_item_t *cf_read_generic (const char *path, int depth);

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

		new = cf_read_generic (old->values[0].value.string, depth + 1);
		if (new == NULL)
			continue;

		/* Now replace the i'th child in `root' with `new'. */
		cf_ci_replace_child (root, new, i);

		/* ... and go back to the new i'th child. */
		--i;

		sfree (new->values);
		sfree (new);
	} /* for (i = 0; i < root->children_num; i++) */

	return (0);
} /* int cf_include_all */

static oconfig_item_t *cf_read_file (const char *file, int depth)
{
	oconfig_item_t *root;

	assert (depth < CF_MAX_DEPTH);

	root = oconfig_parse_file (file);
	if (root == NULL)
	{
		ERROR ("configfile: Cannot read file `%s'.", file);
		return (NULL);
	}

	cf_include_all (root, depth);

	return (root);
} /* oconfig_item_t *cf_read_file */

static int cf_compare_string (const void *p1, const void *p2)
{
	return strcmp (*(const char **) p1, *(const char **) p2);
}

static oconfig_item_t *cf_read_dir (const char *dir, int depth)
{
	oconfig_item_t *root = NULL;
	DIR *dh;
	struct dirent *de;
	char **filenames = NULL;
	int filenames_num = 0;
	int status;
	int i;

	assert (depth < CF_MAX_DEPTH);

	dh = opendir (dir);
	if (dh == NULL)
	{
		char errbuf[1024];
		ERROR ("configfile: opendir failed: %s",
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (NULL);
	}

	root = (oconfig_item_t *) malloc (sizeof (oconfig_item_t));
	if (root == NULL)
	{
		ERROR ("configfile: malloc failed.");
		return (NULL);
	}
	memset (root, 0, sizeof (oconfig_item_t));

	while ((de = readdir (dh)) != NULL)
	{
		char   name[1024];
		char **tmp;

		if ((de->d_name[0] == '.') || (de->d_name[0] == 0))
			continue;

		status = ssnprintf (name, sizeof (name), "%s/%s",
				dir, de->d_name);
		if ((status < 0) || ((size_t) status >= sizeof (name)))
		{
			ERROR ("configfile: Not including `%s/%s' because its"
					" name is too long.",
					dir, de->d_name);
			for (i = 0; i < filenames_num; ++i)
				free (filenames[i]);
			free (filenames);
			free (root);
			return (NULL);
		}

		++filenames_num;
		tmp = (char **) realloc (filenames,
				filenames_num * sizeof (*filenames));
		if (tmp == NULL) {
			ERROR ("configfile: realloc failed.");
			for (i = 0; i < filenames_num - 1; ++i)
				free (filenames[i]);
			free (filenames);
			free (root);
			return (NULL);
		}
		filenames = tmp;

		filenames[filenames_num - 1] = sstrdup (name);
	}

	qsort ((void *) filenames, filenames_num, sizeof (*filenames),
			cf_compare_string);

	for (i = 0; i < filenames_num; ++i)
	{
		oconfig_item_t *temp;
		char *name = filenames[i];

		temp = cf_read_generic (name, depth);
		if (temp == NULL)
		{
			/* An error should already have been reported. */
			sfree (name);
			continue;
		}

		cf_ci_append_children (root, temp);
		sfree (temp->children);
		sfree (temp);

		free (name);
	}

	free(filenames);
	return (root);
} /* oconfig_item_t *cf_read_dir */

/* 
 * cf_read_generic
 *
 * Path is stat'ed and either cf_read_file or cf_read_dir is called
 * accordingly.
 *
 * There are two versions of this function: If `wordexp' exists shell wildcards
 * will be expanded and the function will include all matches found. If
 * `wordexp' (or, more precisely, it's header file) is not available the
 * simpler function is used which does not do any such expansion.
 */
#if HAVE_WORDEXP_H
static oconfig_item_t *cf_read_generic (const char *path, int depth)
{
	oconfig_item_t *root = NULL;
	int status;
	const char *path_ptr;
	wordexp_t we;
	size_t i;

	if (depth >= CF_MAX_DEPTH)
	{
		ERROR ("configfile: Not including `%s' because the maximum "
				"nesting depth has been reached.", path);
		return (NULL);
	}

	status = wordexp (path, &we, WRDE_NOCMD);
	if (status != 0)
	{
		ERROR ("configfile: wordexp (%s) failed.", path);
		return (NULL);
	}

	root = (oconfig_item_t *) malloc (sizeof (oconfig_item_t));
	if (root == NULL)
	{
		ERROR ("configfile: malloc failed.");
		return (NULL);
	}
	memset (root, '\0', sizeof (oconfig_item_t));

	/* wordexp() might return a sorted list already. That's not
	 * documented though, so let's make sure we get what we want. */
	qsort ((void *) we.we_wordv, we.we_wordc, sizeof (*we.we_wordv),
			cf_compare_string);

	for (i = 0; i < we.we_wordc; i++)
	{
		oconfig_item_t *temp;
		struct stat statbuf;

		path_ptr = we.we_wordv[i];

		status = stat (path_ptr, &statbuf);
		if (status != 0)
		{
			char errbuf[1024];
			WARNING ("configfile: stat (%s) failed: %s",
					path_ptr,
					sstrerror (errno, errbuf, sizeof (errbuf)));
			continue;
		}

		if (S_ISREG (statbuf.st_mode))
			temp = cf_read_file (path_ptr, depth);
		else if (S_ISDIR (statbuf.st_mode))
			temp = cf_read_dir (path_ptr, depth);
		else
		{
			WARNING ("configfile: %s is neither a file nor a "
					"directory.", path);
			continue;
		}

		if (temp == NULL) {
			oconfig_free (root);
			return (NULL);
		}

		cf_ci_append_children (root, temp);
		sfree (temp->children);
		sfree (temp);
	}

	wordfree (&we);

	if (root->children == NULL)
	{
		oconfig_free (root);
		return (NULL);
	}

	return (root);
} /* oconfig_item_t *cf_read_generic */
/* #endif HAVE_WORDEXP_H */

#else /* if !HAVE_WORDEXP_H */
static oconfig_item_t *cf_read_generic (const char *path, int depth)
{
	struct stat statbuf;
	int status;

	if (depth >= CF_MAX_DEPTH)
	{
		ERROR ("configfile: Not including `%s' because the maximum "
				"nesting depth has been reached.", path);
		return (NULL);
	}

	status = stat (path, &statbuf);
	if (status != 0)
	{
		char errbuf[1024];
		ERROR ("configfile: stat (%s) failed: %s",
				path,
				sstrerror (errno, errbuf, sizeof (errbuf)));
		return (NULL);
	}

	if (S_ISREG (statbuf.st_mode))
		return (cf_read_file (path, depth));
	else if (S_ISDIR (statbuf.st_mode))
		return (cf_read_dir (path, depth));

	ERROR ("configfile: %s is neither a file nor a directory.", path);
	return (NULL);
} /* oconfig_item_t *cf_read_generic */
#endif /* !HAVE_WORDEXP_H */

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

	conf = cf_read_generic (filename, 0 /* depth */);
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

	oconfig_free (conf);

	/* Read the default types.db if no `TypesDB' option was given. */
	if (cf_default_typesdb)
		read_types_list (PKGDATADIR"/types.db");

	return (0);
} /* int cf_read */

/* Assures the config option is a string, duplicates it and returns the copy in
 * "ret_string". If necessary "*ret_string" is freed first. Returns zero upon
 * success. */
int cf_util_get_string (const oconfig_item_t *ci, char **ret_string) /* {{{ */
{
	char *string;

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		ERROR ("cf_util_get_string: The %s option requires "
				"exactly one string argument.", ci->key);
		return (-1);
	}

	string = strdup (ci->values[0].value.string);
	if (string == NULL)
		return (-1);

	if (*ret_string != NULL)
		sfree (*ret_string);
	*ret_string = string;

	return (0);
} /* }}} int cf_util_get_string */

/* Assures the config option is a string and copies it to the provided buffer.
 * Assures null-termination. */
int cf_util_get_string_buffer (const oconfig_item_t *ci, char *buffer, /* {{{ */
		size_t buffer_size)
{
	if ((ci == NULL) || (buffer == NULL) || (buffer_size < 1))
		return (EINVAL);

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
	{
		ERROR ("cf_util_get_string_buffer: The %s option requires "
				"exactly one string argument.", ci->key);
		return (-1);
	}

	strncpy (buffer, ci->values[0].value.string, buffer_size);
	buffer[buffer_size - 1] = 0;

	return (0);
} /* }}} int cf_util_get_string_buffer */

/* Assures the config option is a number and returns it as an int. */
int cf_util_get_int (const oconfig_item_t *ci, int *ret_value) /* {{{ */
{
	if ((ci == NULL) || (ret_value == NULL))
		return (EINVAL);

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
	{
		ERROR ("cf_util_get_int: The %s option requires "
				"exactly one numeric argument.", ci->key);
		return (-1);
	}

	*ret_value = (int) ci->values[0].value.number;

	return (0);
} /* }}} int cf_util_get_int */

int cf_util_get_boolean (const oconfig_item_t *ci, _Bool *ret_bool) /* {{{ */
{
	if ((ci == NULL) || (ret_bool == NULL))
		return (EINVAL);

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
	{
		ERROR ("cf_util_get_boolean: The %s option requires "
				"exactly one boolean argument.", ci->key);
		return (-1);
	}

	*ret_bool = ci->values[0].value.boolean ? 1 : 0;

	return (0);
} /* }}} int cf_util_get_boolean */

int cf_util_get_flag (const oconfig_item_t *ci, /* {{{ */
		unsigned int *ret_value, unsigned int flag)
{
	int status;
	_Bool b;

	if (ret_value == NULL)
		return (EINVAL);

	b = 0;
	status = cf_util_get_boolean (ci, &b);
	if (status != 0)
		return (status);

	if (b)
	{
		*ret_value |= flag;
	}
	else
	{
		*ret_value &= ~flag;
	}

	return (0);
} /* }}} int cf_util_get_flag */

/* Assures that the config option is a string or a number if the correct range
 * of 1-65535. The string is then converted to a port number using
 * `service_name_to_port_number' and returned.
 * Returns the port number in the range [1-65535] or less than zero upon
 * failure. */
int cf_util_get_port_number (const oconfig_item_t *ci) /* {{{ */
{
	int tmp;

	if ((ci->values_num != 1)
			|| ((ci->values[0].type != OCONFIG_TYPE_STRING)
				&& (ci->values[0].type != OCONFIG_TYPE_NUMBER)))
	{
		ERROR ("cf_util_get_port_number: The \"%s\" option requires "
				"exactly one string argument.", ci->key);
		return (-1);
	}

	if (ci->values[0].type == OCONFIG_TYPE_STRING)
		return (service_name_to_port_number (ci->values[0].value.string));

	assert (ci->values[0].type == OCONFIG_TYPE_NUMBER);
	tmp = (int) (ci->values[0].value.number + 0.5);
	if ((tmp < 1) || (tmp > 65535))
	{
		ERROR ("cf_util_get_port_number: The \"%s\" option requires "
				"a service name or a port number. The number "
				"you specified, %i, is not in the valid "
				"range of 1-65535.",
				ci->key, tmp);
		return (-1);
	}

	return (tmp);
} /* }}} int cf_util_get_port_number */

int cf_util_get_service (const oconfig_item_t *ci, char **ret_string) /* {{{ */
{
	int port;
	char *service;
	int status;

	if (ci->values_num != 1)
	{
		ERROR ("cf_util_get_service: The %s option requires exactly "
				"one argument.", ci->key);
		return (-1);
	}

	if (ci->values[0].type == OCONFIG_TYPE_STRING)
		return (cf_util_get_string (ci, ret_string));
	if (ci->values[0].type != OCONFIG_TYPE_NUMBER)
	{
		ERROR ("cf_util_get_service: The %s option requires "
				"exactly one string or numeric argument.",
				ci->key);
	}

	port = 0;
	status = cf_util_get_int (ci, &port);
	if (status != 0)
		return (status);
	else if ((port < 1) || (port > 65535))
	{
		ERROR ("cf_util_get_service: The port number given "
				"for the %s option is out of "
				"range (%i).", ci->key, port);
		return (-1);
	}

	service = malloc (6);
	if (service == NULL)
	{
		ERROR ("cf_util_get_service: Out of memory.");
		return (-1);
	}
	ssnprintf (service, 6, "%i", port);

	sfree (*ret_string);
	*ret_string = service;

	return (0);
} /* }}} int cf_util_get_service */

int cf_util_get_cdtime (const oconfig_item_t *ci, cdtime_t *ret_value) /* {{{ */
{
	if ((ci == NULL) || (ret_value == NULL))
		return (EINVAL);

	if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
	{
		ERROR ("cf_util_get_cdtime: The %s option requires "
				"exactly one numeric argument.", ci->key);
		return (-1);
	}

	if (ci->values[0].value.number < 0.0)
	{
		ERROR ("cf_util_get_cdtime: The numeric argument of the %s "
				"option must not be negative.", ci->key);
		return (-1);
	}

	*ret_value = DOUBLE_TO_CDTIME_T (ci->values[0].value.number);

	return (0);
} /* }}} int cf_util_get_cdtime */

