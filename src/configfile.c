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

/*
 * FIXME:
 * - remove all (I mean *ALL*) calls to `fprintf': `stderr' will have been
 *   closed.
 */

#include "collectd.h"

#include "libconfig/libconfig.h"

#include "configfile.h"
#include "utils_debug.h"

#define SHORTOPT_NONE 0

#define ERR_NOT_NESTED "Sections cannot be nested.\n"
#define ERR_SECTION_ONLY "`%s' can only be used as section.\n"
#define ERR_NEEDS_ARG "Section `%s' needs an argument.\n"
#define ERR_NEEDS_SECTION "`%s' can only be used within a section.\n"

typedef struct cf_callback
{
	char  *type;
	int  (*callback) (char *, char *);
	char **keys;
	int    keys_num;
	struct cf_callback *next;
} cf_callback_t;

static cf_callback_t *first_callback = NULL;

static int nesting_depth = 0;
static char *current_module = NULL;

/* cf_register needs this prototype */
int cf_callback_general (const char *, const char *, const char *,
		const char *, lc_flags_t, void *);

/*
 * Functions to handle register/unregister, search, ...
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

	if ((cf_cb = cf_search (type)) == NULL)
	{
		fprintf (stderr, "Plugin `%s' did not register a callback.\n", type);
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
			ret = (*cf_cb->callback) (key, value);
	}

	if (i >= cf_cb->keys_num)
		fprintf (stderr, "Plugin `%s' did not register for value `%s'.\n", type, key);

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
		if (snprintf (buf, 64, "Module.%s", keys[i]) < 64)
		{
			/* This may be called multiple times for the same
			 * `key', but apparently `lc_register_*' can handle
			 * it.. */
			lc_register_callback (buf, SHORTOPT_NONE,
					LC_VAR_STRING, cf_callback_general,
					NULL);
		}
		else
		{
			DBG ("Key was truncated: `%s'", keys[i]);
		}
	}
}

/* 
 * Functions for the actual parsing
 */
int cf_callback_general (const char *shortvar, const char *var,
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

int cf_callback_section_mode (const char *shortvar, const char *var,
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

		nesting_depth++;

		if (((operating_mode == MODE_CLIENT)
					&& (strcasecmp (arguments, "Client") == 0))
				|| ((operating_mode == MODE_SERVER)
					&& (strcasecmp (arguments, "Server") == 0))
				|| ((operating_mode == MODE_LOCAL)
					&& (strcasecmp (arguments, "Local") == 0)))
		{
			return (LC_CBRET_OKAY);
		}
		else
		{
			return (LC_CBRET_IGNORESECTION);
		}
	}
	else if (flags == LC_FLAGS_SECTIONEND)
	{
		nesting_depth--;

		return (LC_CBRET_OKAY);
	}
	else
	{
		fprintf (stderr, ERR_SECTION_ONLY, shortvar);
		return (LC_CBRET_ERROR);
	}

}

int cf_callback_section_module (const char *shortvar, const char *var,
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
			current_module == NULL;
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

int cf_callback_loadmodule (const char *shortvar, const char *var,
		const char *arguments, const char *value, lc_flags_t flags,
		void *extra)
{
	DBG ("shortvar = %s, var = %s, arguments = %s, value = %s, ...",
			shortvar, var, arguments, value);

	if (nesting_depth == 0)
	{
		fprintf (stderr, ERR_NEEDS_SECTION, shortvar);
		return (LC_CBRET_ERROR);
	}

	/*
	 * TODO:
	 * - Write wrapper around `plugin_load' to resolve path/filename
	 * - Call this new, public function here
	 */
	DBG ("Implement me, idiot!");

	return (LC_CBRET_OKAY);
}

int cf_read (char *filename)
{
	if (filename == NULL)
		filename = CONFIGFILE;

	lc_register_callback ("Mode", SHORTOPT_NONE, LC_VAR_SECTION,
			cf_callback_section_mode, NULL);
	lc_register_callback ("Module", SHORTOPT_NONE, LC_VAR_SECTION,
			cf_callback_section_module, NULL);

	/*
	 * TODO:
	 * - Add more directives, such as `DefaultMode', `DataDir', `PIDFile', ...
	 */

	lc_register_callback ("Mode.LoadModule", SHORTOPT_NONE,
			LC_VAR_STRING, cf_callback_loadmodule,
			NULL);

	if (lc_process_file ("collectd", filename, LC_CONF_APACHE))
	{
		/* FIXME: Use syslog here */
		fprintf (stderr, "Error loading config file `%s': %s\n",
				filename, lc_geterrstr ());
		return (-1);
	}

	/* free memory and stuff */
	lc_cleanup ();

	return (0);
}
