/**
 * collectd - src/utils_cache.c
 * Copyright (C) 2007  Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
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
 * Author:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_avltree.h"

#include <assert.h>
#include <pthread.h>

typedef struct cache_entry_s
{
	char name[6 * DATA_MAX_NAME_LEN];
	int        values_num;
	gauge_t   *values_gauge;
	counter_t *values_counter;
	time_t last_update;
} cache_entry_t;

static avl_tree_t     *cache_tree = NULL;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

static int cache_compare (const cache_entry_t *a, const cache_entry_t *b)
{
  assert ((a != NULL) && (b != NULL));
  return (strcmp (a->name, b->name));
} /* int cache_compare */

int uc_init (void)
{
  if (cache_tree == NULL)
    cache_tree = avl_create ((int (*) (const void *, const void *))
	cache_compare);

  return (0);
} /* int uc_init */

int uc_update (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;

  if (FORMAT_VL (name, sizeof (name), vl, ds) != 0)
  {
    ERROR ("uc_insert: FORMAT_VL failed.");
    return (-1);
  }

  pthread_mutex_lock (&cache_lock);

  if (avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    int i;

    assert (ce != NULL);
    assert (ce->values_num == ds->ds_num);

    if (ce->last_update >= vl->time)
    {
      pthread_mutex_unlock (&cache_lock);
      NOTICE ("uc_insert: Value too old: name = %s; value time = %u; "
	  "last cache update = %u;",
	  name, (unsigned int) vl->time, (unsigned int) ce->last_update);
      return (-1);
    }

    for (i = 0; i < ds->ds_num; i++)
    {
      if (ds->ds[i].type == DS_TYPE_COUNTER)
      {
	counter_t diff;

	/* check if the counter has wrapped around */
	if (vl->values[i].counter < ce->values_counter[i])
	{
	  if (ce->values_counter[i] <= 4294967295U)
	    diff = (4294967295U - ce->values_counter[i])
	      + vl->values[i].counter;
	  else
	    diff = (18446744073709551615ULL - ce->values_counter[i])
	      + vl->values[i].counter;
	}
	else /* counter has NOT wrapped around */
	{
	  diff = vl->values[i].counter - ce->values_counter[i];
	}

	ce->values_gauge[i] = ((double) diff)
	  / ((double) (vl->time - ce->last_update));
	ce->values_counter[i] = vl->values[i].counter;
      }
      else /* if (ds->ds[i].type == DS_TYPE_GAUGE) */
      {
	ce->values_gauge[i] = vl->values[i].gauge;
      }
      DEBUG ("uc_insert: %s: ds[%i] = %lf", name, i, ce->values_gauge[i]);
    } /* for (i) */

    ce->last_update = vl->time;
  }
  else /* key is not found */
  {
    int i;
    size_t ce_size = sizeof (cache_entry_t)
      + ds->ds_num * (sizeof (counter_t) + sizeof (gauge_t));
    char *key;
    
    key = strdup (name);
    if (key == NULL)
    {
      pthread_mutex_unlock (&cache_lock);
      ERROR ("uc_insert: strdup failed.");
      return (-1);
    }

    ce = (cache_entry_t *) malloc (ce_size);
    if (ce == NULL)
    {
      pthread_mutex_unlock (&cache_lock);
      ERROR ("uc_insert: malloc (%u) failed.", (unsigned int) ce_size);
      return (-1);
    }

    memset (ce, '\0', ce_size);

    strncpy (ce->name, name, sizeof (ce->name));
    ce->name[sizeof (ce->name) - 1] = '\0';

    ce->values_num = ds->ds_num;
    ce->values_gauge = (gauge_t *) (ce + 1);
    ce->values_counter = (counter_t *) (ce->values_gauge + ce->values_num);

    for (i = 0; i < ds->ds_num; i++)
    {
      if (ds->ds[i].type == DS_TYPE_COUNTER)
      {
	ce->values_gauge[i] = NAN;
	ce->values_counter[i] = vl->values[i].counter;
      }
      else /* if (ds->ds[i].type == DS_TYPE_GAUGE) */
      {
	ce->values_gauge[i] = vl->values[i].gauge;
      }
    } /* for (i) */

    ce->last_update = vl->time;

    if (avl_insert (cache_tree, key, ce) != 0)
    {
      pthread_mutex_unlock (&cache_lock);
      ERROR ("uc_insert: avl_insert failed.");
      return (-1);
    }

    DEBUG ("uc_insert: Added %s to the cache.", name);
  } /* if (key is not found) */

  pthread_mutex_unlock (&cache_lock);

  return (0);
} /* int uc_insert */

gauge_t *uc_get_rate (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  gauge_t *ret = NULL;
  cache_entry_t *ce = NULL;

  if (FORMAT_VL (name, sizeof (name), vl, ds) != 0)
  {
    ERROR ("uc_insert: FORMAT_VL failed.");
    return (NULL);
  }

  pthread_mutex_lock (&cache_lock);

  if (avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);
    assert (ce->values_num == ds->ds_num);

    ret = (gauge_t *) malloc (ce->values_num * sizeof (gauge_t));
    if (ret == NULL)
    {
      ERROR ("uc_get_rate: malloc failed.");
    }
    else
    {
      memcpy (ret, ce->values_gauge, ce->values_num * sizeof (gauge_t));
    }
  }

  pthread_mutex_unlock (&cache_lock);

  return (ret);
} /* gauge_t *uc_get_rate */

/* vim: set sw=2 ts=8 sts=2 tw=78 : */
