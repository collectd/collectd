/**
 * collectd - src/config_list.c
 * Copyright (C) 2006 Lubos Stanek <lubek at users.sourceforge.net>
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
 *   if (ignorelist_ignored (myconfig_ignore, thisentry))
 *     return;
 **/

#include "common.h"
#include "utils_debug.h"
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
	int num;		/* number of entries */
	ignorelist_item_t *next;	/* pointer to the first entry */
};

/* *** *** *** ********************************************* *** *** *** */
/* *** *** *** *** *** ***   private functions   *** *** *** *** *** *** */
/* *** *** *** ********************************************* *** *** *** */

#if HAVE_REGEX_H
static int ignorelist_regappend(ignorelist_t *conflist, const char *entry)
{
	int rcompile;
	regex_t *regtemp;
	int errsize;
	char *regerr = NULL;
	ignorelist_item_t *new;

	/* create buffer */
	if ((regtemp = malloc(sizeof(regex_t))) == NULL)
	{
		syslog (LOG_ERR, "cannot allocate new config entry");
		return (0);
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
		if (regerror(rcompile, regtemp, regerr, errsize))
			syslog (LOG_ERR, "cannot compile regex %s: %i/%s",
					entry, rcompile, regerr);
		else
			syslog (LOG_ERR, "cannot compile regex %s: %i",
					entry, rcompile);
		if (errsize)
			sfree (regerr);
		regfree (regtemp);
		return (0);
	}
	DBG("regex compiled: %s - %i", entry, rcompile);

	/* create new entry */
	if ((new = malloc(sizeof(ignorelist_item_t))) == NULL)
	{
		syslog (LOG_ERR, "cannot allocate new config entry");
		regfree (regtemp);
		return (0);
	}
	memset (new, '\0', sizeof(ignorelist_item_t));
	new->rmatch = regtemp;

	/* append new entry */
	if (conflist->next == NULL)
	{
		conflist->next=new;
	}
	else
	{
		new->next=conflist->next;
		conflist->next=new;		
	}
	conflist->num++;
	return (1);
} /* int ignorelist_regappend(ignorelist_t *conflist, const char *entry) */
#endif

static int ignorelist_strappend(ignorelist_t *conflist, const char *entry)
{
	ignorelist_item_t *new;

	/* create new entry */
	if ((new = malloc(sizeof(ignorelist_item_t))) == NULL )
	{
		syslog (LOG_ERR, "cannot allocate new entry");
		return (0);
	}
	memset (new, '\0', sizeof(ignorelist_item_t));
	new->smatch = sstrdup(entry);

	/* append new entry */
	if (conflist->next == NULL)
	{
		conflist->next=new;
	}
	else
	{
		new->next=conflist->next;
		conflist->next=new;		
	}
	conflist->num++;
	return (1);
} /* int ignorelist_strappend(ignorelist_t *conflist, const char *entry) */

#if HAVE_REGEX_H
/*
 * check list for entry regex match
 * return 1 if found
 */
static int ignorelist_item_rmatch (ignorelist_item_t *confentry, const char *entry)
{
	if (confentry == NULL)
		return (0);

	if (strlen (entry) == 0)
		return (0);

	if (confentry->rmatch == NULL)
		return (0);

	/* match regex */
	if (regexec (confentry->rmatch, entry, 0, NULL, 0) == 0)
		return (1);

	return (0);
} /* int ignorelist_item_rmatch (ignorelist_item_t *confentry, const char *entry) */
#endif

/*
 * check list for entry string match
 * return 1 if found
 */
static int ignorelist_item_smatch (ignorelist_item_t *confentry, const char *entry)
{
	if (confentry == NULL)
		return (0);

	if (strlen (entry) == 0)
		return (0);

	if ((confentry->smatch != NULL && strcmp (entry, confentry->smatch) == 0))
		return (1);

	return (0);
} /* int ignorelist_item_smatch (ignorelist_item_t *confentry, const char *entry) */


/* *** *** *** ******************************************** *** *** *** */
/* *** *** *** *** *** ***   public functions   *** *** *** *** *** *** */
/* *** *** *** ******************************************** *** *** *** */

/*
 * create the ignorelist_t with known ignore state
 * return pointer to ignorelist_t
 */
ignorelist_t *ignorelist_create (int ignore)
{
	ignorelist_t *conflist;

	if ((conflist = smalloc (sizeof (ignorelist_t))) == NULL)
	{
		syslog(LOG_ERR, "not enough memory to allocate ignorelist");
		return (NULL);
	}
	DBG("ignorelist created 0x%p, ignore %i", (void *) conflist, ignore);
	memset (conflist, '\0', sizeof (ignorelist_t));

	if (ignore)
		conflist->ignore = ignore;

	return (conflist);
} /* ignorelist_t *ignorelist_create (int ignore) */

/*
 * create ignorelist_t and initialize the ignore state to 0
 * return pointer to ignorelist_t
 */
ignorelist_t *ignorelist_init (void)
{
	return (ignorelist_create (0));
} /* ignorelist_t *ignorelist_init (void)  */


/*
 * free memory used by ignorelist_t
 */
void ignorelist_free (ignorelist_t *conflist)
{
	ignorelist_item_t *this;
	ignorelist_item_t *next;

	DBG ("(conflist = 0x%p)", (void *) conflist);

	if (conflist == NULL)
		return;

	for (this = conflist->next; this != NULL; this = next)
	{
		DBG ("free - confentry = 0x%p, numlist %i", (void *) this, conflist->num);
		next = this->next;
		conflist->num--;
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
#if COLLECTD_DEBUG
	if (conflist->num != 0)
		DBG ("after free numlist: %i", conflist->num);
#endif
	conflist->num = 0;
	sfree (conflist);
	conflist = NULL;
} /* void ignorelist_destroy (ignorelist_t *conflist) */

/*
 * set ignore state of the ignorelist_t
 */
void ignorelist_ignore (ignorelist_t *conflist, int ignore)
{
	if (conflist == NULL)
	{
		DBG("ignore call with ignorelist_t == NULL");
		return;
	}

	conflist->ignore = ignore;
} /* void ignorelist_ignore (ignorelist_t *conflist, int ignore) */

/*
 * get number of entries in the ignorelist_t
 * return int number
 */
int ignorelist_num (ignorelist_t *conflist)
{
	if (conflist == NULL)
	{
		DBG("get num called with ignorelist_t == NULL");
		return (0);
	}

	return (conflist->num);
} /* int ignorelist_num (ignorelist_t *conflist) */

/*
 * append entry into ignorelist_t
 * return 1 for success
 */
int ignorelist_add (ignorelist_t *conflist, const char *entry)
{
#if HAVE_REGEX_H
	char *entrytemp;
#endif
	int restemp;

	if (conflist == NULL)
	{
		DBG("add called with ignorelist_t == NULL");
		return (0);
	}

	/* append nothing, report success */
	if (strlen(entry) == 0)
	{
		DBG("not appending: empty entry");
		return (1);
	}

#if HAVE_REGEX_H
	/* regex string is enclosed in "/.../" */
	if (entry[0] == '/' && strlen(entry) > 2 && entry[strlen(entry) - 1] == '/')
	{
		entrytemp = smalloc(strlen(entry) - 2);
		sstrncpy(entrytemp, &entry[1], strlen(entry) - 1);
		DBG("to add regex entry: %s", entrytemp);
		restemp = ignorelist_regappend(conflist, entrytemp);
		sfree (entrytemp);
	}
	else
#endif
	{
		DBG("to add entry: %s", entry);
		restemp = ignorelist_strappend(conflist, entry);
	}
	return (restemp);
} /* int ignorelist_add (ignorelist_t *conflist, const char *entry) */

/*
 * check list for entry
 * return 1 for ignored entry
 */
int ignorelist_ignored (ignorelist_t *conflist, const char *entry)
{
	ignorelist_item_t *traverse;

	/* if no entries, collect all */
	if (ignorelist_num(conflist) == 0)
		return (0);

	/* traverse list and check entries */
	traverse = conflist->next;
	while (traverse != NULL)
	{
#if HAVE_REGEX_H
		if (traverse->rmatch != NULL)
		{
			if (ignorelist_item_rmatch (traverse, entry))
				return (conflist->ignore);
		}
		else
#endif
		{
			if (ignorelist_item_smatch (traverse, entry))
				return (conflist->ignore);
		}
		traverse = traverse->next;
	}

	return (1 - conflist->ignore);
} /* int ignorelist_ignored (ignorelist_t *conflist, const char *entry) */

