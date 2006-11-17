/**
 * collectd - src/config_list.h
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
 * configlist handles plugin's list of configured collectable
 * entries with global ignore action
 **/

#if !CONFIG_LIST_H
#define CONFIG_LIST_H 1

#include "common.h"

#if HAVE_REGEX_H
# include <regex.h>
#endif

/* public prototypes */

struct configlist_s;
typedef struct configlist_s configlist_t;

/*
 * create the configlist_t with known ignore state
 * return pointer to configlist_t
 */
configlist_t *configlist_create (int ignore);

/*
 * create configlist_t and initialize the ignore state to 0
 * return pointer to configlist_t
 */
configlist_t *configlist_init (void);

/*
 * free memory used by configlist_t
 */
void configlist_free (configlist_t *conflist);

/*
 * set ignore state of the configlist_t
 */
void configlist_ignore (configlist_t *conflist, int ignore);
/*
 * get number of entries in the configlist_t
 * return int number
 */
int configlist_num (configlist_t *conflist);

/*
 * append entry to configlist_t
 * return 1 for success
 */
int configlist_add (configlist_t *conflist, const char *entry);

/*
 * check list for entry
 * return 1 for ignored entry
 */
int configlist_ignored (configlist_t *conflist, const char *entry);

#endif /* !CONFIG_LIST_H */

