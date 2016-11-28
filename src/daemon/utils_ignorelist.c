/**
 * collectd - src/utils_ignorelist.c
 * Copyright (C) 2006 Lubos Stanek <lubek at users.sourceforge.net>
 * Copyright (C) 2008 Florian Forster <octo at collectd.org>
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Lubos Stanek <lubek at users.sourceforge.net>
 *   Florian Forster <octo at collectd.org>
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
#include "config.h"
#endif

#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

/*
 * private prototypes
 */
struct ignorelist_item_s {
#if HAVE_REGEX_H
  regex_t *rmatch; /* regular expression entry identification */
#endif
  char *smatch; /* string entry identification */
  struct ignorelist_item_s *next;
};
typedef struct ignorelist_item_s ignorelist_item_t;

struct ignorelist_s {
  int ignore;              /* ignore entries */
  ignorelist_item_t *head; /* pointer to the first entry */
};

/* *** *** *** ********************************************* *** *** *** */
/* *** *** *** *** *** ***   private functions   *** *** *** *** *** *** */
/* *** *** *** ********************************************* *** *** *** */

static inline void ignorelist_append(ignorelist_t *il,
                                     ignorelist_item_t *item) {
  assert((il != NULL) && (item != NULL));

  item->next = il->head;
  il->head = item;
}

#if HAVE_REGEX_H
static int ignorelist_append_regex(ignorelist_t *il, const char *re_str) {
  regex_t *re;
  ignorelist_item_t *entry;
  int status;

  re = calloc(1, sizeof(*re));
  if (re == NULL) {
    ERROR("ignorelist_append_regex: calloc failed.");
    return (ENOMEM);
  }

  status = regcomp(re, re_str, REG_EXTENDED);
  if (status != 0) {
    char errbuf[1024];
    (void)regerror(status, re, errbuf, sizeof(errbuf));
    ERROR("utils_ignorelist: regcomp failed: %s", errbuf);
    ERROR("ignorelist_append_regex: Compiling regular expression \"%s\" "
          "failed: %s",
          re_str, errbuf);
    sfree(re);
    return (status);
  }

  entry = calloc(1, sizeof(*entry));
  if (entry == NULL) {
    ERROR("ignorelist_append_regex: calloc failed.");
    regfree(re);
    sfree(re);
    return (ENOMEM);
  }
  entry->rmatch = re;

  ignorelist_append(il, entry);
  return (0);
} /* int ignorelist_append_regex */
#endif

static int ignorelist_append_string(ignorelist_t *il, const char *entry) {
  ignorelist_item_t *new;

  /* create new entry */
  if ((new = calloc(1, sizeof(*new))) == NULL) {
    ERROR("cannot allocate new entry");
    return (1);
  }
  new->smatch = sstrdup(entry);

  /* append new entry */
  ignorelist_append(il, new);

  return (0);
} /* int ignorelist_append_string(ignorelist_t *il, const char *entry) */

#if HAVE_REGEX_H
/*
 * check list for entry regex match
 * return 1 if found
 */
static int ignorelist_match_regex(ignorelist_item_t *item, const char *entry) {
  assert((item != NULL) && (item->rmatch != NULL) && (entry != NULL) &&
         (strlen(entry) > 0));

  /* match regex */
  if (regexec(item->rmatch, entry, 0, NULL, 0) == 0)
    return (1);

  return (0);
} /* int ignorelist_match_regex (ignorelist_item_t *item, const char *entry) */
#endif

/*
 * check list for entry string match
 * return 1 if found
 */
static int ignorelist_match_string(ignorelist_item_t *item, const char *entry) {
  assert((item != NULL) && (item->smatch != NULL) && (entry != NULL) &&
         (strlen(entry) > 0));

  if (strcmp(entry, item->smatch) == 0)
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
ignorelist_t *ignorelist_create(int invert) {
  ignorelist_t *il;

  il = calloc(1, sizeof(*il));
  if (il == NULL)
    return NULL;

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
void ignorelist_free(ignorelist_t *il) {
  ignorelist_item_t *this;
  ignorelist_item_t *next;

  if (il == NULL)
    return;

  for (this = il->head; this != NULL; this = next) {
    next = this->next;
#if HAVE_REGEX_H
    if (this->rmatch != NULL) {
      regfree(this->rmatch);
      sfree(this->rmatch);
      this->rmatch = NULL;
    }
#endif
    if (this->smatch != NULL) {
      sfree(this->smatch);
      this->smatch = NULL;
    }
    sfree(this);
  }

  sfree(il);
} /* void ignorelist_destroy (ignorelist_t *il) */

/*
 * set ignore state of the ignorelist_t
 */
void ignorelist_set_invert(ignorelist_t *il, int invert) {
  if (il == NULL) {
    DEBUG("ignore call with ignorelist_t == NULL");
    return;
  }

  il->ignore = invert ? 0 : 1;
} /* void ignorelist_set_invert (ignorelist_t *il, int ignore) */

/*
 * append entry into ignorelist_t
 * return 0 for success
 */
int ignorelist_add(ignorelist_t *il, const char *entry) {
  size_t len;

  if (il == NULL) {
    DEBUG("add called with ignorelist_t == NULL");
    return (1);
  }

  len = strlen(entry);

  /* append nothing */
  if (len == 0) {
    DEBUG("not appending: empty entry");
    return (1);
  }

#if HAVE_REGEX_H
  /* regex string is enclosed in "/.../" */
  if ((len > 2) && (entry[0] == '/') && entry[len - 1] == '/') {
    char *copy;
    int status;

    /* skip leading slash */
    copy = strdup(entry + 1);
    if (copy == NULL)
      return ENOMEM;

    /* trim trailing slash */
    copy[strlen(copy) - 1] = 0;

    status = ignorelist_append_regex(il, copy);
    sfree(copy);
    return status;
  }
#endif

  return ignorelist_append_string(il, entry);
} /* int ignorelist_add (ignorelist_t *il, const char *entry) */

/*
 * check list for entry
 * return 1 for ignored entry
 */
int ignorelist_match(ignorelist_t *il, const char *entry) {
  /* if no entries, collect all */
  if ((il == NULL) || (il->head == NULL))
    return (0);

  if ((entry == NULL) || (strlen(entry) == 0))
    return (0);

  /* traverse list and check entries */
  for (ignorelist_item_t *traverse = il->head; traverse != NULL;
       traverse = traverse->next) {
#if HAVE_REGEX_H
    if (traverse->rmatch != NULL) {
      if (ignorelist_match_regex(traverse, entry))
        return (il->ignore);
    } else
#endif
    {
      if (ignorelist_match_string(traverse, entry))
        return (il->ignore);
    }
  } /* for traverse */

  return (1 - il->ignore);
} /* int ignorelist_match (ignorelist_t *il, const char *entry) */
