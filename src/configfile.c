/**
 * collectd - src/configfile.c
 * Copyright (C) 2005  Florian octo Forster
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

/* TODO
 * make internal-only functions `static' */

#include "collectd.h"

#include "libconfig/libconfig.h"

#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "network.h"
#include "utils_debug.h"

#define SHORTOPT_NONE 0

#define ERR_NOT_NESTED "Sections cannot be nested.\n"
#define ERR_SECTION_ONLY "`%s' can only be used as section.\n"
#define ERR_NEEDS_ARG "Section `%s' needs an argument.\n"
#define ERR_NEEDS_SECTION "`%s' can only be used within a section.\n"

#ifdef HAVE_LIBRRD
extern int operating_mode;
#else
static int operating_mode = MODE_CLIENT;
#endif

typedef struct cf_callback
{
	char  *type;
	int  (*callback) (char *, char *);
	char **keys;
	int    keys_num;
	struct cf_callback *next;
} cf_callback_t;

static cf_callback_t *first_callback = NULL;

typedef struct cf_mode_item
{
	char *key;
	char *value;
	int   mode;
} cf_mode_item_t;

/* TODO
 * - LogFile
 */
static cf_mode_item_t cf_mode_list[] =
{
	{"MulticastTTL",NULL, MODE_CLIENT                           },
	{"PIDFile",     NULL, MODE_CLIENT | MODE_SERVER | MODE_LOCAL},
	{"DataDir",     NULL, MODE_CLIENT | MODE_SERVER | MODE_LOCAL},
	{"LogFile",     NULL, MODE_CLIENT | MODE_SERVER | MODE_LOCAL}
};
static int cf_mode_num = 4;

static int nesting_depth = 0;
static char *current_module = NULL;

/* `cf_register' needs this prototype */
int cf_callback_plugin_dispatch (const char *, const char *, const char *,
		const char *, lc_flags_t, void *);

/*
 * Functions to handle register/unregister, search, and other plugin related
 * stuff
 */
cf_callback_t *cf_search (char *type)
{
	cf_callback_t *cf_cb;

	if (type == NULL)
		return (NULL);

	for (cf_cb = first_callback; cf_cb != NULL; cf_cb = cf_cb->next)
		if (strcasecmp (cf_cb->type, type) == 0)
			break;

	return (cf_cb);
}

int cf_dispatch (char *type, const char *orig_key, const char *orig_value)
{
	cf_callback_t *cf_cb;
	char *key;
	char *value;
	int ret;
	int i;

	DBG ("type = %s, key = %s, value = %s", type, orig_key, orig_value);

	if ((cf_cb = cf_search (type)) == NULL)
	{
		syslog (LOG_WARNING, "Plugin `%s' did not register a callback.\n", type);
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
		syslog (LOG_WARNING, "Plugin `%s' did not register for value `%s'.\n", type, key);

	free (key);
	free (value);

	return (ret);
}

void cf_unregister (char *type)
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
}

void cf_register (char *type,
		int (*callback) (char *, char *),
		char **keys, int keys_num)
{
	cf_callback_t *cf_cb;
	char buf[64];
	int i;

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

	for (i = 0; i < keys_num; i++)
	{
		if (snprintf (buf, 64, "Plugin.%s", keys[i]) < 64)
		{
			/* This may be called multiple times for the same
			 * `key', but apparently `lc_register_*' can handle
			 * it.. */
			lc_register_callback (buf, SHORTOPT_NONE,
					LC_VAR_STRING, cf_callback_plugin_dispatch,
					NULL);
		}
		else
		{
			DBG ("Key was truncated: `%s'", keys[i]);
		}
	}
}

/*
 * Other query functions
 */
char *cf_get_option (const char *key, char *def)
{
	int i;

	for (i = 0; i < cf_mode_num; i++)
	{
		if ((cf_mode_list[i].mode & operating_mode) == 0)
			continue;

		if (strcasecmp (cf_mode_list[i].key, key) != 0)
			continue;

		if (cf_mode_list[i].value != NULL)
			return (cf_mode_list[i].value);
		return (def);
	}

	return (NULL);
}

/* * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * *
 * Functions for the actual parsing                                    *
 * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * * */

/*
 * `cf_callback_mode'
 *   Chose the `operating_mode'
 *
 * Mode `value'
 */
int cf_callback_mode (const char *shortvar, const char *var,
		const char *arguments, const char *value, lc_flags_t flags,
		void *extra)
{
	DBG ("shortvar = %s, var = %s, arguments = %s, value = %s, ...",
			shortvar, var, arguments, value);

	if (strcasecmp (value, "Client") == 0)
		operating_mode = MODE_CLIENT;
	else if (strcasecmp (value, "Server") == 0)
		operating_mode = MODE_SERVER;
	else if (strcasecmp (value, "Local") == 0)
		operating_mode = MODE_LOCAL;
	else
	{
		syslog (LOG_ERR, "Invalid value for config option `Mode': `%s'", value);
		return (LC_CBRET_ERROR);
	}

	return (LC_CBRET_OKAY);
}

/*
 * `cf_callback_mode_plugindir'
 *   Change the plugin directory
 *
 * <Mode xxx>
 *   PluginDir `value'
 * </Mode>
 */
int cf_callback_mode_plugindir (const char *shortvar, const char *var,
		const char *arguments, const char *value, lc_flags_t flags,
		void *extra)
{
	DBG ("shortvar = %s, var = %s, arguments = %s, value = %s, ...",
			shortvar, var, arguments, value);

	plugin_set_dir (value);

	return (LC_CBRET_OKAY);
}

int cf_callback_mode_option (const char *shortvar, const char *var,
		const char *arguments, const char *value, lc_flags_t flags,
		void *extra)
{
	cf_mode_item_t *item;

	DBG ("shortvar = %s, var = %s, arguments = %s, value = %s, ...",
			shortvar, var, arguments, value);

	if (extra == NULL)
	{
		fprintf (stderr, "No extra..?\n");
		return (LC_CBRET_ERROR);
	}

	item = (cf_mode_item_t *) extra;

	if (strcasecmp (item->key, shortvar))
	{
		fprintf (stderr, "Wrong extra..\n");
		return (LC_CBRET_ERROR);
	}

	if ((operating_mode & item->mode) == 0)
	{
		fprintf (stderr, "Option `%s' is not valid in this mode!\n", shortvar);
		return (LC_CBRET_ERROR);
	}

	if (item->value != NULL)
	{
		free (item->value);
		item->value = NULL;
	}

	if ((item->value = strdup (value)) == NULL)
	{
		perror ("strdup");
		return (LC_CBRET_ERROR);
	}

	return (LC_CBRET_OKAY);
}

/*
 * `cf_callback_mode_loadmodule':
 *   Load a plugin.
 *
 * <Mode xxx>
 *   LoadPlugin `value'
 * </Mode>
 */
int cf_callback_mode_loadmodule (const char *shortvar, const char *var,
		const char *arguments, const char *value, lc_flags_t flags,
		void *extra)
{
	DBG ("shortvar = %s, var = %s, arguments = %s, value = %s, ...",
			shortvar, var, arguments, value);

	if (plugin_load (value))
		syslog (LOG_ERR, "plugin_load (%s): failed to load plugin", value);

	/* Return `okay' even if there was an error, because it's not a syntax
	 * problem.. */
	return (LC_CBRET_OKAY);
}

/*
 * `cf_callback_mode_switch'
 *   Change the contents of the global variable `operating_mode'
 *
 *   This should be command line options. One *can* do this in the config
 *   files, but I will not document this. Don't whine abount it not working as
 *   you expect if you do it anyways.
 */
int cf_callback_mode_switch (const char *shortvar, const char *var,
		const char *arguments, const char *value, lc_flags_t flags,
		void *extra)
{
	DBG ("shortvar = %s, var = %s, arguments = %s, value = %s, ...",
			shortvar, var, arguments, value);

	if (strcasecmp (shortvar, "Client") == 0)
		operating_mode = MODE_CLIENT;
	else if (strcasecmp (shortvar, "Local") == 0)
		operating_mode = MODE_LOCAL;
	else if (strcasecmp (shortvar, "Server") == 0)
		operating_mode = MODE_SERVER;
	else
	{
		fprintf (stderr, "cf_callback_mode_switch: Wrong mode!\n");
		return (LC_CBRET_ERROR);
	}

	return (LC_CBRET_OKAY);
}

int cf_callback_socket (const char *shortvar, const char *var,
		const char *arguments, const char *value, lc_flags_t flags,
		void *extra)
{
	char *buffer;

	char *fields[3];
	int   numfields;

	char *node;
	char *service = NET_DEFAULT_PORT;

	DBG ("shortvar = %s, var = %s, arguments = %s, value = %s, ...",
			shortvar, var, arguments, value);

	buffer = strdup (value);
	if (buffer == NULL)
		return (LC_CBRET_ERROR);

	numfields = strsplit (buffer, fields, 3);

	if ((numfields != 1) && (numfields != 2))
	{
		syslog (LOG_ERR, "Invalid number of arguments to `%s'",
				shortvar);
		free (buffer);
		return (LC_CBRET_ERROR);
	}

	node = fields[0];
	if (numfields == 2)
		service = fields[1];

	/* Still return `LC_CBRET_OKAY' because this is not an syntax error */
	if (network_create_socket (node, service) < 1)
		syslog (LOG_ERR, "network_create_socket (%s, %s) failed",
				node, service);

	free (buffer);

	return (LC_CBRET_OKAY);
}

/*
 * `cf_callback_plugin'
 *   Start/end section `plugin'
 *
 * <Plugin `arguments'>
 *   ...
 * </Plugin>
 */
int cf_callback_plugin (const char *shortvar, const char *var,
		const char *arguments, const char *value, lc_flags_t flags,
		void *extra)
{
	DBG ("shortvar = %s, var = %s, arguments = %s, value = %s, ...",
			shortvar, var, arguments, value);

	if (flags == LC_FLAGS_SECTIONSTART)
	{
		if (nesting_depth != 0)
		{
			fprintf (stderr, ERR_NOT_NESTED);
			return (LC_CBRET_ERROR);
		}

		if (arguments == NULL)
		{
			fprintf (stderr, ERR_NEEDS_ARG, shortvar);
			return (LC_CBRET_ERROR);
		}

		if ((current_module = strdup (arguments)) == NULL)
		{
			perror ("strdup");
			return (LC_CBRET_ERROR);
		}

		nesting_depth++;

		if (cf_search (current_module) != NULL)
			return (LC_CBRET_OKAY);
		else
			return (LC_CBRET_IGNORESECTION);
	}
	else if (flags == LC_FLAGS_SECTIONEND)
	{
		if (current_module != NULL)
		{
			free (current_module);
			current_module = NULL;
		}

		nesting_depth--;

		return (LC_CBRET_OKAY);
	}
	else
	{
		fprintf (stderr, ERR_SECTION_ONLY, shortvar);
		return (LC_CBRET_ERROR);
	}
}

/*
 * `cf_callback_plugin_dispatch'
 *   Send options within `plugin' sections to the plugin that requests it.
 *
 * <Plugin `current_module'>
 *   `var' `value'
 * </Plugin>
 */
int cf_callback_plugin_dispatch (const char *shortvar, const char *var,
		const char *arguments, const char *value, lc_flags_t flags,
		void *extra)
{
	DBG ("shortvar = %s, var = %s, arguments = %s, value = %s, ...",
			shortvar, var, arguments, value);

	if ((nesting_depth == 0) || (current_module == NULL))
	{
		fprintf (stderr, ERR_NEEDS_SECTION, shortvar);
		return (LC_CBRET_ERROR);
	}

	/* Send the data to the plugin */
	if (cf_dispatch (current_module, shortvar, value) < 0)
		return (LC_CBRET_ERROR);

	return (LC_CBRET_OKAY);
}

void cf_init (void)
{
	static int run_once = 0;
	int i;

	if (run_once != 0)
		return;
	run_once = 1;

	lc_register_callback ("Mode", SHORTOPT_NONE, LC_VAR_STRING,
			cf_callback_mode, NULL);
	lc_register_callback ("Plugin", SHORTOPT_NONE, LC_VAR_SECTION,
			cf_callback_plugin, NULL);

	lc_register_callback ("PluginDir", SHORTOPT_NONE,
			LC_VAR_STRING, cf_callback_mode_plugindir, NULL);
	lc_register_callback ("LoadPlugin", SHORTOPT_NONE,
			LC_VAR_STRING, cf_callback_mode_loadmodule, NULL);

	lc_register_callback ("Listen", SHORTOPT_NONE,
			LC_VAR_STRING, cf_callback_socket, NULL);
	lc_register_callback ("Server", SHORTOPT_NONE,
			LC_VAR_STRING, cf_callback_socket, NULL);

	for (i = 0; i < cf_mode_num; i++)
	{
		cf_mode_item_t *item;

		item = &cf_mode_list[i];

		lc_register_callback (item->key, SHORTOPT_NONE, LC_VAR_STRING,
				cf_callback_mode_option, (void *) item);
	}
}

int cf_read (char *filename)
{
	cf_init ();

	if (filename == NULL)
		filename = CONFIGFILE;

	DBG ("Starting to parse file `%s'", filename);

	/* int lc_process_file(const char *appname, const char *pathname, lc_conf_type_t type); */
	if (lc_process_file ("collectd", filename, LC_CONF_APACHE))
	{
		syslog (LOG_ERR, "lc_process_file (%s): %s", filename, lc_geterrstr ());
		return (-1);
	}

	DBG ("Done parsing file `%s'", filename);

	/* free memory and stuff */
	lc_cleanup ();

	return (0);
}
