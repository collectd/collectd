/**
 * collectd - src/utils_llist.h
 * Copyright (C) 2006 Florian Forster <octo at verplant.org>
 *
 * This program is free software; you can redistribute it and/
 * or modify it under the terms of the GNU General Public Li-
 * cence as published by the Free Software Foundation; only
 * version 2 of the Licence is applicable.
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
 *   Florian Forster <octo at verplant.org>
 */

#ifndef UTILS_LLIST_H
#define UTILS_LLIST_H 1

/*
 * Data types
 */
struct llentry_s
{
	char *key;
	void *value;
	struct llentry_s *next;
};
typedef struct llentry_s llentry_t;

struct llist_s;
typedef struct llist_s llist_t;

/*
 * Functions
 */
llist_t *llist_create (void);
void llist_destroy (llist_t *l);

llentry_t *llentry_create (char *key, void *value);
void llentry_destroy (llentry_t *e);

void llist_append (llist_t *l, llentry_t *e);
void llist_prepend (llist_t *l, llentry_t *e);
void llist_remove (llist_t *l, llentry_t *e);

int llist_size (llist_t *l);

llentry_t *llist_search (llist_t *l, const char *key);
llentry_t *llist_search_custom (llist_t *l,
		int (*compare) (llentry_t *, void *), void *user_data);

llentry_t *llist_head (llist_t *l);
llentry_t *llist_tail (llist_t *l);

#endif /* UTILS_LLIST_H */
