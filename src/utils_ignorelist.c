/**
 * collectd - src/utils_ignorelist.c
 * Copyright (C) 2006 Lubos Stanek <lubek at users.sourceforge.net>
 * Copyright (C) 2008 Florian Forster <octo at verplant.org>
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; either
 * version 2 of the Licence, or any later version.
 *
 * This program is distributed in the hope that it will be use-
 * ful, but WITHOUT ANY WARRANTY; without even the implied war-
 * ranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public Licence for more details.
 *
 * You should have received a copy of the GNU General Public
 * Licence along with this program; if not, write to the Free
 * Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139,
 * USA.
 *
 * Authors:
 *   Lubos Stanek <lubek at users.sourceforge.net>
 *   Florian Forster <octo at verplant.org>
 **/
/**
 * ignorelist handles plugin's list of configured collectable
 * entries with global ignore action
 **/
/**
 * Usage:
 * 
 * Define plugin's global pointer variable of type ignorelist_t:
 *   ignorelist_t *myconfig_ignore;
 * If you know the state of the global ignore (IgnoreSelected),
 * allocate the variable with:
 *   myconfig_ignore = ignorelist_create (YourKnownIgnore);
 * If you do not know the state of the global ignore,
 * initialize the global variable and set the ignore flag later:
 *   myconfig_ignore = ignorelist_init ();
 * Append single entries in your cf_register'ed callback function:
 *   ignorelist_add (myconfig_ignore, newentry);
 * When you hit the IgnoreSelected config option,
 * offer it to the list:
 *   ignorelist_ignore (myconfig_ignore, instantly_got_value_of_ignore);
 * That is all for the ignorelist initialization.
 * Later during read and write (plugin's registered functions) get
 * the information whether this entry would be collected or not:
 *   if (ignorelist_match (myconfig_ignore, thisentry))
 *     return;
 **/

#if HAVE_CONFIG_H
# include "config.h"
#endif

#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

/*
 * private prototypes
 */
struct ignorelist_item_s
{
#if HAVE_REGEX_H
	regex_t *rmatch;	/* regular expression entry identification */
#endif
	char *smatch;		/* string entry identification */
	struct ignorelist_item_s *next;
};
typedef struct ignorelist_item_s ignorelist_item_t;

struct ignorelist_s
{
	int ignore;		/* ignore entries */
	ignorelist_item_t *head;	/* pointer to the first entry */
};

/* *** *** *** ********************************************* *** *** *** */
/* *** *** *** *** *** ***   private functions   *** *** *** *** *** *** */
/* *** *** *** ********************************************* *** *** *** */

static inline void ignorelist_append (ignorelist_t *il, ignorelist_item_t *item)
{
	assert ((il != NULL) && (item != NULL));

	item->next = il->head;
	il->head = item;
}

#if HAVE_REGEX_H
static int ignorelist_append_regex(ignorelist_t *il, const char *entry)
{
	int rcompile;
	regex_t *regtemp;
	int errsize;
	char *regerr = NULL;
	ignorelist_item_t *new;

	/* create buffer */
	if ((regtemp = malloc(sizeof(regex_t))) == NULL)
	{
		ERROR ("cannot allocate new config entry");
		return (1);
	}
	memset (regtemp, '\0', sizeof(regex_t));

	/* compile regex */
	if ((rcompile = regcomp (regtemp, entry, REG_EXTENDED)) != 0)
	{
		/* prepare message buffer */
		errsize = regerror(rcompile, regtemp, NULL, 0);
		if (errsize)
			regerr = smalloc(errsize);
		/* get error message */
		if (regerror (rcompile, regtemp, regerr, errsize))
		{
			fprintf (stderr, "Cannot compile regex %s: %i/%s",
					entry, rcompile, regerr);
			ERROR ("Cannot compile regex %s: %i/%s",
					entry, rcompile, regerr);
		}
		else
		{
			fprintf (stderr, "Cannot compile regex %s: %i",
					entry, rcompile);
			ERROR ("Cannot compile regex %s: %i",
					entry, rcompile);
		}

		if (errsize)
			sfree (regerr);
		regfree (regtemp);
		return (1);
	}
	DEBUG("regex compiled: %s - %i", entry, rcompile);

	/* create new entry */
	if ((new = malloc(sizeof(ignorelist_item_t))) == NULL)
	{
		ERROR ("cannot allocate new config entry");
		regfree (regtemp);
		return (1);
	}
	memset (new, '\0', sizeof(ignorelist_item_t));
	new->rmatch = regtemp;

	/* append new entry */
	ignorelist_append (il, new);

	return (0);
} /* int ignorelist_append_regex(ignorelist_t *il, const char *entry) */
#endif

static int ignorelist_append_string(ignorelist_t *il, const char *entry)
{
	ignorelist_item_t *new;

	/* create new entry */
	if ((new = malloc(sizeof(ignorelist_item_t))) == NULL )
	{
		ERROR ("cannot allocate new entry");
		return (1);
	}
	memset (new, '\0', sizeof(ignorelist_item_t));
	new->smatch = sstrdup(entry);

	/* append new entry */
	ignorelist_append (il, new);

	return (0);
} /* int ignorelist_append_string(ignorelist_t *il, const char *entry) */

#if HAVE_REGEX_H
/*
 * check list for entry regex match
 * return 1 if found
 */
static int ignorelist_match_regex (ignorelist_item_t *item, const char *entry)
{
	assert ((item != NULL) && (item->rmatch != NULL)
			&& (entry != NULL) && (strlen (entry) > 0));

	/* match regex */
	if (regexec (item->rmatch, entry, 0, NULL, 0) == 0)
		return (1);

	return (0);
} /* int ignorelist_match_regex (ignorelist_item_t *item, const char *entry) */
#endif

/*
 * check list for entry string match
 * return 1 if found
 */
static int ignorelist_match_string (ignorelist_item_t *item, const char *entry)
{
	assert ((item != NULL) && (item->smatch != NULL)
			&& (entry != NULL) && (strlen (entry) > 0));

	if (strcmp (entry, item->smatch) == 0)
		return (1);

	return (0);
} /* int ignorelist_match_string (ignorelist_item_t *item, const char *entry) */


/* *** *** *** ******************************************** *** *** *** */
/* *** *** *** *** *** ***   public functions   *** *** *** *** *** *** */
/* *** *** *** ******************************************** *** *** *** */

/*
 * create the ignorelist_t with known ignore state
 * return pointer to ignorelist_t
 */
ignorelist_t *ignorelist_create (int invert)
{
	ignorelist_t *il;

	/* smalloc exits if it failes */
	il = (ignorelist_t *) smalloc (sizeof (ignorelist_t));
	memset (il, '\0', sizeof (ignorelist_t));

	/*
	 * ->ignore == 0  =>  collect
	 * ->ignore == 1  =>  ignore
	 */
	il->ignore = invert ? 0 : 1;

	return (il);
} /* ignorelist_t *ignorelist_create (int ignore) */

/*
 * free memory used by ignorelist_t
 */
void ignorelist_free (ignorelist_t *il)
{
	ignorelist_item_t *this;
	ignorelist_item_t *next;

	if (il == NULL)
		return;

	for (this = il->head; this != NULL; this = next)
	{
		next = this->next;
#if HAVE_REGEX_H
		if (this->rmatch != NULL)
		{
			regfree (this->rmatch);
			this->rmatch = NULL;
		}
#endif
		if (this->smatch != NULL)
		{
			sfree (this->smatch);
			this->smatch = NULL;
		}
		sfree (this);
	}

	sfree (il);
	il = NULL;
} /* void ignorelist_destroy (ignorelist_t *il) */

/*
 * set ignore state of the ignorelist_t
 */
void ignorelist_set_invert (ignorelist_t *il, int invert)
{
	if (il == NULL)
	{
		DEBUG("ignore call with ignorelist_t == NULL");
		return;
	}

	il->ignore = invert ? 0 : 1;
} /* void ignorelist_set_invert (ignorelist_t *il, int ignore) */

/*
 * append entry into ignorelist_t
 * return 1 for success
 */
int ignorelist_add (ignorelist_t *il, const char *entry)
{
	int ret;
	size_t entry_len;

	if (il == NULL)
	{
		DEBUG ("add called with ignorelist_t == NULL");
		return (1);
	}

	entry_len = strlen (entry);

	/* append nothing */
	if (entry_len == 0)
	{
		DEBUG("not appending: empty entry");
		return (1);
	}

#if HAVE_REGEX_H
	/* regex string is enclosed in "/.../" */
	if ((entry_len > 2) && (entry[0] == '/') && entry[entry_len - 1] == '/')
	{
		char *entry_copy;
		size_t entry_copy_size;

		/* We need to copy `entry' since it's const */
		entry_copy_size = entry_len - 1;
		entry_copy = smalloc (entry_copy_size);
		sstrncpy (entry_copy, entry + 1, entry_copy_size);

		DEBUG("I'm about to add regex entry: %s", entry_copy);
		ret = ignorelist_append_regex(il, entry_copy);
		sfree (entry_copy);
	}
	else
#endif
	{
		DEBUG("to add entry: %s", entry);
		ret = ignorelist_append_string(il, entry);
	}

	return (ret);
} /* int ignorelist_add (ignorelist_t *il, const char *entry) */

/*
 * check list for entry
 * return 1 for ignored entry
 */
int ignorelist_match (ignorelist_t *il, const char *entry)
{
	ignorelist_item_t *traverse;

	/* if no entries, collect all */
	if ((il == NULL) || (il->head == NULL))
		return (0);

	if ((entry == NULL) || (strlen (entry) == 0))
		return (0);

	/* traverse list and check entries */
	for (traverse = il->head; traverse != NULL; traverse = traverse->next)
	{
#if HAVE_REGEX_H
		if (traverse->rmatch != NULL)
		{
			if (ignorelist_match_regex (traverse, entry))
				return (il->ignore);
		}
		else
#endif
		{
			if (ignorelist_match_string (traverse, entry))
				return (il->ignore);
		}
	} /* for traverse */

	return (1 - il->ignore);
} /* int ignorelist_match (ignorelist_t *il, const char *entry) */

