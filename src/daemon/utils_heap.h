/**
 * collectd - src/utils_heap.h
 * Copyright (C) 2009       Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#ifndef UTILS_HEAP_H
#define UTILS_HEAP_H 1

struct c_heap_s;
typedef struct c_heap_s c_heap_t;

/*
 * NAME
 *   c_heap_create
 *
 * DESCRIPTION
 *   Allocates a new heap.
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
 *   A c_heap_t-pointer upon success or NULL upon failure.
 */
c_heap_t *c_heap_create (int (*compare) (const void *, const void *));

/*
 * NAME
 *   c_heap_destroy
 *
 * DESCRIPTION
 *   Deallocates a heap. Stored value- and key-pointer are lost, but of course
 *   not freed.
 */
void c_heap_destroy (c_heap_t *h);

/*
 * NAME
 *   c_heap_insert
 *
 * DESCRIPTION
 *   Stores the key-value-pair in the heap pointed to by `h'.
 *
 * PARAMETERS
 *   `h'        Heap to store the data in.
 *   `ptr'      Value to be stored. This is typically a pointer to a data
 *              structure. The data structure is of course *not* copied and may
 *              not be free'd before the pointer has been removed from the heap
 *              again.
 *
 * RETURN VALUE
 *   Zero upon success, non-zero otherwise. It's less than zero if an error
 *   occurred or greater than zero if the key is already stored in the tree.
 */
int c_heap_insert (c_heap_t *h, void *ptr);

/*
 * NAME
 *   c_heap_get_root
 *
 * DESCRIPTION
 *   Removes the value at the root of the heap and returns both, key and value.
 *
 * PARAMETERS
 *   `h'           Heap to remove key-value-pair from.
 *
 * RETURN VALUE
 *   The pointer passed to `c_heap_insert' or NULL if there are no more
 *   elements in the heap (or an error occurred).
 */
void *c_heap_get_root (c_heap_t *h);

#endif /* UTILS_HEAP_H */
/* vim: set sw=2 sts=2 et : */
