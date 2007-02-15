/**
 * collectd - src/utils_avltree.h
 * Copyright (C) 2006,2007  Florian octo Forster
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

#ifndef UTILS_AVLTREE_H
#define UTILS_AVLTREE_H 1

struct avl_tree_s;
typedef struct avl_tree_s avl_tree_t;

struct avl_iterator_s;
typedef struct avl_iterator_s avl_iterator_t;

/*
 * NAME
 *   avl_create
 *
 * DESCRIPTION
 *   Allocates a new AVL-tree.
 *
 * PARAMETERS
 *   `compare'  The function-pointer `compare' is used to compare two keys. It
 *              has to return less than zero if it's first argument is smaller
 *              then the second argument, more than zero if the first argument
 *              is bigger than the second argument and zero if they are equal.
 *              If your keys are char-pointers, you can use the `strcmp'
 *              function from the libc here.
 *
 * RETURN VALUE
 *   A avl_tree_t-pointer upon success or NULL upon failure.
 */
avl_tree_t *avl_create (int (*compare) (const void *, const void *));


/*
 * NAME
 *   avl_destroy
 *
 * DESCRIPTION
 *   Deallocates an AVL-tree. Stored value- and key-pointer are lost, but of
 *   course not freed.
 */
void avl_destroy (avl_tree_t *t);

/*
 * NAME
 *   avl_insert
 *
 * DESCRIPTION
 *   Stores the key-value-pair in the AVL-tree pointed to by `t'.
 *
 * PARAMETERS
 *   `t'        AVL-tree to store the data in.
 *   `key'      Key used to store the value under. This is used to get back to
 *              the value again.
 *   `value'    Value to be stored.
 *
 * RETURN VALUE
 *   Zero upon success and non-zero upon failure and if the key is already
 *   stored in the tree.
 */
int avl_insert (avl_tree_t *t, void *key, void *value);

/*
 * NAME
 *   avl_remove
 *
 * DESCRIPTION
 *   Removes a key-value-pair from the tree t. The stored key and value may be
 *   returned in `rkey' and `rvalue'.
 *
 * PARAMETERS
 *   `t'	AVL-tree to remove key-value-pair from.
 *   `key'      Key to identify the entry.
 *   `rkey'     Pointer to a pointer in which to store the key. May be NULL.
 *   `rvalue'   Pointer to a pointer in which to store the value. May be NULL.
 *
 * RETURN VALUE
 *   Zero upon success or non-zero if the key isn't found in the tree.
 */
int avl_remove (avl_tree_t *t, void *key, void **rkey, void **rvalue);

/*
 * NAME
 *   avl_get
 *
 * DESCRIPTION
 *   Retrieve the `value' belonging to `key'.
 *
 * PARAMETERS
 *   `t'	AVL-tree to get the value from.
 *   `key'      Key to identify the entry.
 *   `value'    Pointer to a pointer in which to store the value. May be NULL.
 *
 * RETURN VALUE
 *   Zero upon success or non-zero if the key isn't found in the tree.
 */
int avl_get (avl_tree_t *t, const void *key, void **value);

/*
 * NAME
 *   avl_pick
 *
 * DESCRIPTION
 *   Remove a (pseudo-)random element from the tree and return it's `key' and
 *   `value'. Entries are not returned in any particular order. This function
 *   is intended for cache-flushes that don't care about the order but simply
 *   want to remove all elements, one at a time.
 *
 * PARAMETERS
 *   `t'	AVL-tree to get the value from.
 *   `key'      Pointer to a pointer in which to store the key.
 *   `value'    Pointer to a pointer in which to store the value.
 *
 * RETURN VALUE
 *   Zero upon success or non-zero if the tree is empty or key or value is
 *   NULL.
 */
int avl_pick (avl_tree_t *t, void **key, void **value);

#if 0
/* This code disabled until a need arises. */
avl_iterator_t *avl_get_iterator (avl_tree_t *t);
void *avl_iterator_next (avl_iterator_t *iter);
void *avl_iterator_prev (avl_iterator_t *iter);
void avl_iterator_destroy (avl_iterator_t *iter);
#endif

#endif /* UTILS_AVLTREE_H */
