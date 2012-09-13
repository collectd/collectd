/**
 * collectd - src/utils_heap.c
 * Copyright (C) 2009  Florian octo Forster
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

#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>

#include "utils_heap.h"

struct c_heap_s
{
  pthread_mutex_t lock;
  int (*compare) (const void *, const void *);

  void **list;
  size_t list_len; /* # entries used */
  size_t list_size; /* # entries allocated */
};

enum reheap_direction
{
  DIR_UP,
  DIR_DOWN
};

static void reheap (c_heap_t *h, size_t root, enum reheap_direction dir)
{
  size_t left;
  size_t right;
  size_t min;
  int status;

  /* Calculate the positions of the children */
  left = (2 * root) + 1;
  if (left >= h->list_len)
    left = 0;

  right = (2 * root) + 2;
  if (right >= h->list_len)
    right = 0;

  /* Check which one of the children is smaller. */
  if ((left == 0) && (right == 0))
    return;
  else if (left == 0)
    min = right;
  else if (right == 0)
    min = left;
  else
  {
    status = h->compare (h->list[left], h->list[right]);
    if (status > 0)
      min = right;
    else
      min = left;
  }

  status = h->compare (h->list[root], h->list[min]);
  if (status <= 0)
  {
    /* We didn't need to change anything, so the rest of the tree should be
     * okay now. */
    return;
  }
  else /* if (status > 0) */
  {
    void *tmp;

    tmp = h->list[root];
    h->list[root] = h->list[min];
    h->list[min] = tmp;
  }

  if ((dir == DIR_UP) && (root == 0))
    return;

  if (dir == DIR_UP)
    reheap (h, (root - 1) / 2, dir);
  else if (dir == DIR_DOWN)
    reheap (h, min, dir);
} /* void reheap */

c_heap_t *c_heap_create (int (*compare) (const void *, const void *))
{
  c_heap_t *h;

  if (compare == NULL)
    return (NULL);

  h = malloc (sizeof (*h));
  if (h == NULL)
    return (NULL);

  memset (h, 0, sizeof (*h));
  pthread_mutex_init (&h->lock, /* attr = */ NULL);
  h->compare = compare;
  
  h->list = NULL;
  h->list_len = 0;
  h->list_size = 0;

  return (h);
} /* c_heap_t *c_heap_create */

void c_heap_destroy (c_heap_t *h)
{
  if (h == NULL)
    return;

  h->list_len = 0;
  h->list_size = 0;
  free (h->list);
  h->list = NULL;

  pthread_mutex_destroy (&h->lock);

  free (h);
} /* void c_heap_destroy */

int c_heap_insert (c_heap_t *h, void *ptr)
{
  size_t index;

  if ((h == NULL) || (ptr == NULL))
    return (-EINVAL);

  pthread_mutex_lock (&h->lock);

  assert (h->list_len <= h->list_size);
  if (h->list_len == h->list_size)
  {
    void **tmp;

    tmp = realloc (h->list, (h->list_size + 16) * sizeof (*h->list));
    if (tmp == NULL)
    {
      pthread_mutex_unlock (&h->lock);
      return (-ENOMEM);
    }

    h->list = tmp;
    h->list_size += 16;
  }

  /* Insert the new node as a leaf. */
  index = h->list_len;
  h->list[index] = ptr;
  h->list_len++;

  /* Reorganize the heap from bottom up. */
  reheap (h, /* parent of this node = */ (index - 1) / 2, DIR_UP);
  
  pthread_mutex_unlock (&h->lock);
  return (0);
} /* int c_heap_insert */

void *c_heap_get_root (c_heap_t *h)
{
  void *ret = NULL;

  if (h == NULL)
    return (NULL);

  pthread_mutex_lock (&h->lock);

  if (h->list_len == 0)
  {
    pthread_mutex_unlock (&h->lock);
    return (NULL);
  }
  else if (h->list_len == 1)
  {
    ret = h->list[0];
    h->list[0] = NULL;
    h->list_len = 0;
  }
  else /* if (h->list_len > 1) */
  {
    ret = h->list[0];
    h->list[0] = h->list[h->list_len - 1];
    h->list[h->list_len - 1] = NULL;
    h->list_len--;

    reheap (h, /* root = */ 0, DIR_DOWN);
  }

  /* free some memory */
  if ((h->list_len + 32) < h->list_size)
  {
    void **tmp;

    tmp = realloc (h->list, (h->list_len + 16) * sizeof (*h->list));
    if (tmp != NULL)
    {
      h->list = tmp;
      h->list_size = h->list_len + 16;
    }
  }

  pthread_mutex_unlock (&h->lock);

  return (ret);
} /* void *c_heap_get_root */

/* vim: set sw=2 sts=2 et fdm=marker : */
