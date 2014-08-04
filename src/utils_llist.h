/**
 * collectd - src/utils_llist.h
 * Copyright (C) 2006       Florian Forster <octo at collectd.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
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
