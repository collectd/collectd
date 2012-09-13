/**
 * collectd - src/utils_cache.c
 * Copyright (C) 2007-2010  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_avltree.h"
#include "utils_cache.h"
#include "meta_data.h"

#include <assert.h>
#include <pthread.h>

typedef struct cache_entry_s
{
	char name[6 * DATA_MAX_NAME_LEN];
	int        values_num;
	gauge_t   *values_gauge;
	value_t   *values_raw;
	/* Time contained in the package
	 * (for calculating rates) */
	cdtime_t last_time;
	/* Time according to the local clock
	 * (for purging old entries) */
	cdtime_t last_update;
	/* Interval in which the data is collected
	 * (for purding old entries) */
	cdtime_t interval;
	int state;
	int hits;

	/*
	 * +-----+-----+-----+-----+-----+-----+-----+-----+-----+----
	 * !  0  !  1  !  2  !  3  !  4  !  5  !  6  !  7  !  8  ! ...
	 * +-----+-----+-----+-----+-----+-----+-----+-----+-----+----
	 * ! ds0 ! ds1 ! ds2 ! ds0 ! ds1 ! ds2 ! ds0 ! ds1 ! ds2 ! ...
	 * +-----+-----+-----+-----+-----+-----+-----+-----+-----+----
	 * !      t = 0      !      t = 1      !      t = 2      ! ...
	 * +-----------------+-----------------+-----------------+----
	 */
	gauge_t *history;
	size_t   history_index; /* points to the next position to write to. */
	size_t   history_length;

	meta_data_t *meta;
} cache_entry_t;

static c_avl_tree_t   *cache_tree = NULL;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

static int cache_compare (const cache_entry_t *a, const cache_entry_t *b)
{
  assert ((a != NULL) && (b != NULL));
  return (strcmp (a->name, b->name));
} /* int cache_compare */

static cache_entry_t *cache_alloc (int values_num)
{
  cache_entry_t *ce;

  ce = (cache_entry_t *) malloc (sizeof (cache_entry_t));
  if (ce == NULL)
  {
    ERROR ("utils_cache: cache_alloc: malloc failed.");
    return (NULL);
  }
  memset (ce, '\0', sizeof (cache_entry_t));
  ce->values_num = values_num;

  ce->values_gauge = calloc (values_num, sizeof (*ce->values_gauge));
  ce->values_raw   = calloc (values_num, sizeof (*ce->values_raw));
  if ((ce->values_gauge == NULL) || (ce->values_raw == NULL))
  {
    sfree (ce->values_gauge);
    sfree (ce->values_raw);
    sfree (ce);
    ERROR ("utils_cache: cache_alloc: calloc failed.");
    return (NULL);
  }

  ce->history = NULL;
  ce->history_length = 0;
  ce->meta = NULL;

  return (ce);
} /* cache_entry_t *cache_alloc */

static void cache_free (cache_entry_t *ce)
{
  if (ce == NULL)
    return;

  sfree (ce->values_gauge);
  sfree (ce->values_raw);
  sfree (ce->history);
  if (ce->meta != NULL)
  {
    meta_data_destroy (ce->meta);
    ce->meta = NULL;
  }
  sfree (ce);
} /* void cache_free */

static void uc_check_range (const data_set_t *ds, cache_entry_t *ce)
{
  int i;

  for (i = 0; i < ds->ds_num; i++)
  {
    if (isnan (ce->values_gauge[i]))
      continue;
    else if (ce->values_gauge[i] < ds->ds[i].min)
      ce->values_gauge[i] = NAN;
    else if (ce->values_gauge[i] > ds->ds[i].max)
      ce->values_gauge[i] = NAN;
  }
} /* void uc_check_range */

static int uc_insert (const data_set_t *ds, const value_list_t *vl,
    const char *key)
{
  int i;
  char *key_copy;
  cache_entry_t *ce;

  /* `cache_lock' has been locked by `uc_update' */

  key_copy = strdup (key);
  if (key_copy == NULL)
  {
    ERROR ("uc_insert: strdup failed.");
    return (-1);
  }

  ce = cache_alloc (ds->ds_num);
  if (ce == NULL)
  {
    sfree (key_copy);
    ERROR ("uc_insert: cache_alloc (%i) failed.", ds->ds_num);
    return (-1);
  }

  sstrncpy (ce->name, key, sizeof (ce->name));

  for (i = 0; i < ds->ds_num; i++)
  {
    switch (ds->ds[i].type)
    {
      case DS_TYPE_COUNTER:
	ce->values_gauge[i] = NAN;
	ce->values_raw[i].counter = vl->values[i].counter;
	break;

      case DS_TYPE_GAUGE:
	ce->values_gauge[i] = vl->values[i].gauge;
	ce->values_raw[i].gauge = vl->values[i].gauge;
	break;

      case DS_TYPE_DERIVE:
	ce->values_gauge[i] = NAN;
	ce->values_raw[i].derive = vl->values[i].derive;
	break;

      case DS_TYPE_ABSOLUTE:
	ce->values_gauge[i] = NAN;
	if (vl->interval > 0)
	  ce->values_gauge[i] = ((double) vl->values[i].absolute)
	    / CDTIME_T_TO_DOUBLE (vl->interval);
	ce->values_raw[i].absolute = vl->values[i].absolute;
	break;
	
      default:
	/* This shouldn't happen. */
	ERROR ("uc_insert: Don't know how to handle data source type %i.",
	    ds->ds[i].type);
	return (-1);
    } /* switch (ds->ds[i].type) */
  } /* for (i) */

  /* Prune invalid gauge data */
  uc_check_range (ds, ce);

  ce->last_time = vl->time;
  ce->last_update = cdtime ();
  ce->interval = vl->interval;
  ce->state = STATE_OKAY;

  if (c_avl_insert (cache_tree, key_copy, ce) != 0)
  {
    sfree (key_copy);
    ERROR ("uc_insert: c_avl_insert failed.");
    return (-1);
  }

  DEBUG ("uc_insert: Added %s to the cache.", key);
  return (0);
} /* int uc_insert */

int uc_init (void)
{
  if (cache_tree == NULL)
    cache_tree = c_avl_create ((int (*) (const void *, const void *))
	cache_compare);

  return (0);
} /* int uc_init */

int uc_check_timeout (void)
{
  cdtime_t now;
  cache_entry_t *ce;

  char **keys = NULL;
  cdtime_t *keys_time = NULL;
  cdtime_t *keys_interval = NULL;
  int keys_len = 0;

  char *key;
  c_avl_iterator_t *iter;

  int status;
  int i;
  
  pthread_mutex_lock (&cache_lock);

  now = cdtime ();

  /* Build a list of entries to be flushed */
  iter = c_avl_get_iterator (cache_tree);
  while (c_avl_iterator_next (iter, (void *) &key, (void *) &ce) == 0)
  {
    char **tmp;
    cdtime_t *tmp_time;

    /* If the entry is fresh enough, continue. */
    if ((now - ce->last_update) < (ce->interval * timeout_g))
      continue;

    /* If entry has not been updated, add to `keys' array */
    tmp = (char **) realloc ((void *) keys,
	(keys_len + 1) * sizeof (char *));
    if (tmp == NULL)
    {
      ERROR ("uc_check_timeout: realloc failed.");
      continue;
    }
    keys = tmp;

    tmp_time = realloc (keys_time, (keys_len + 1) * sizeof (*keys_time));
    if (tmp_time == NULL)
    {
      ERROR ("uc_check_timeout: realloc failed.");
      continue;
    }
    keys_time = tmp_time;

    tmp_time = realloc (keys_interval, (keys_len + 1) * sizeof (*keys_interval));
    if (tmp_time == NULL)
    {
      ERROR ("uc_check_timeout: realloc failed.");
      continue;
    }
    keys_interval = tmp_time;

    keys[keys_len] = strdup (key);
    if (keys[keys_len] == NULL)
    {
      ERROR ("uc_check_timeout: strdup failed.");
      continue;
    }
    keys_time[keys_len] = ce->last_time;
    keys_interval[keys_len] = ce->interval;

    keys_len++;
  } /* while (c_avl_iterator_next) */

  c_avl_iterator_destroy (iter);
  pthread_mutex_unlock (&cache_lock);

  if (keys_len == 0)
    return (0);

  /* Call the "missing" callback for each value. Do this before removing the
   * value from the cache, so that callbacks can still access the data stored,
   * including plugin specific meta data, rates, history, …. This must be done
   * without holding the lock, otherwise we will run into a deadlock if a
   * plugin calls the cache interface. */
  for (i = 0; i < keys_len; i++)
  {
    value_list_t vl = VALUE_LIST_INIT;

    vl.values = NULL;
    vl.values_len = 0;
    vl.meta = NULL;

    status = parse_identifier_vl (keys[i], &vl);
    if (status != 0)
    {
      ERROR ("uc_check_timeout: parse_identifier_vl (\"%s\") failed.", keys[i]);
      cache_free (ce);
      continue;
    }

    vl.time = keys_time[i];
    vl.interval = keys_interval[i];

    plugin_dispatch_missing (&vl);
  } /* for (i = 0; i < keys_len; i++) */

  /* Now actually remove all the values from the cache. We don't re-evaluate
   * the timestamp again, so in theory it is possible we remove a value after
   * it is updated here. */
  pthread_mutex_lock (&cache_lock);
  for (i = 0; i < keys_len; i++)
  {
    key = NULL;
    ce = NULL;

    status = c_avl_remove (cache_tree, keys[i],
	(void *) &key, (void *) &ce);
    if (status != 0)
    {
      ERROR ("uc_check_timeout: c_avl_remove (\"%s\") failed.", keys[i]);
      sfree (keys[i]);
      continue;
    }

    sfree (keys[i]);
    sfree (key);
    cache_free (ce);
  } /* for (i = 0; i < keys_len; i++) */
  pthread_mutex_unlock (&cache_lock);

  sfree (keys);
  sfree (keys_time);
  sfree (keys_interval);

  return (0);
} /* int uc_check_timeout */

int uc_update (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int status;
  int i;

  if (FORMAT_VL (name, sizeof (name), vl) != 0)
  {
    ERROR ("uc_update: FORMAT_VL failed.");
    return (-1);
  }

  pthread_mutex_lock (&cache_lock);

  status = c_avl_get (cache_tree, name, (void *) &ce);
  if (status != 0) /* entry does not yet exist */
  {
    status = uc_insert (ds, vl, name);
    pthread_mutex_unlock (&cache_lock);
    return (status);
  }

  assert (ce != NULL);
  assert (ce->values_num == ds->ds_num);

  if (ce->last_time >= vl->time)
  {
    pthread_mutex_unlock (&cache_lock);
    NOTICE ("uc_update: Value too old: name = %s; value time = %.3f; "
	"last cache update = %.3f;",
	name,
	CDTIME_T_TO_DOUBLE (vl->time),
	CDTIME_T_TO_DOUBLE (ce->last_time));
    return (-1);
  }

  for (i = 0; i < ds->ds_num; i++)
  {
    switch (ds->ds[i].type)
    {
      case DS_TYPE_COUNTER:
	{
	  counter_t diff;

	  /* check if the counter has wrapped around */
	  if (vl->values[i].counter < ce->values_raw[i].counter)
	  {
	    if (ce->values_raw[i].counter <= 4294967295U)
	      diff = (4294967295U - ce->values_raw[i].counter)
		+ vl->values[i].counter;
	    else
	      diff = (18446744073709551615ULL - ce->values_raw[i].counter)
		+ vl->values[i].counter;
	  }
	  else /* counter has NOT wrapped around */
	  {
	    diff = vl->values[i].counter - ce->values_raw[i].counter;
	  }

	  ce->values_gauge[i] = ((double) diff)
	    / (CDTIME_T_TO_DOUBLE (vl->time - ce->last_time));
	  ce->values_raw[i].counter = vl->values[i].counter;
	}
	break;

      case DS_TYPE_GAUGE:
	ce->values_raw[i].gauge = vl->values[i].gauge;
	ce->values_gauge[i] = vl->values[i].gauge;
	break;

      case DS_TYPE_DERIVE:
	{
	  derive_t diff;

	  diff = vl->values[i].derive - ce->values_raw[i].derive;

	  ce->values_gauge[i] = ((double) diff)
	    / (CDTIME_T_TO_DOUBLE (vl->time - ce->last_time));
	  ce->values_raw[i].derive = vl->values[i].derive;
	}
	break;

      case DS_TYPE_ABSOLUTE:
	ce->values_gauge[i] = ((double) vl->values[i].absolute)
	  / (CDTIME_T_TO_DOUBLE (vl->time - ce->last_time));
	ce->values_raw[i].absolute = vl->values[i].absolute;
	break;

      default:
	/* This shouldn't happen. */
	pthread_mutex_unlock (&cache_lock);
	ERROR ("uc_update: Don't know how to handle data source type %i.",
	    ds->ds[i].type);
	return (-1);
    } /* switch (ds->ds[i].type) */

    DEBUG ("uc_update: %s: ds[%i] = %lf", name, i, ce->values_gauge[i]);
  } /* for (i) */

  /* Update the history if it exists. */
  if (ce->history != NULL)
  {
    assert (ce->history_index < ce->history_length);
    for (i = 0; i < ce->values_num; i++)
    {
      size_t hist_idx = (ce->values_num * ce->history_index) + i;
      ce->history[hist_idx] = ce->values_gauge[i];
    }

    assert (ce->history_length > 0);
    ce->history_index = (ce->history_index + 1) % ce->history_length;
  }

  /* Prune invalid gauge data */
  uc_check_range (ds, ce);

  ce->last_time = vl->time;
  ce->last_update = cdtime ();
  ce->interval = vl->interval;

  pthread_mutex_unlock (&cache_lock);

  return (0);
} /* int uc_update */

int uc_get_rate_by_name (const char *name, gauge_t **ret_values, size_t *ret_values_num)
{
  gauge_t *ret = NULL;
  size_t ret_num = 0;
  cache_entry_t *ce = NULL;
  int status = 0;

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);

    /* remove missing values from getval */
    if (ce->state == STATE_MISSING)
    {
      status = -1;
    }
    else
    {
      ret_num = ce->values_num;
      ret = (gauge_t *) malloc (ret_num * sizeof (gauge_t));
      if (ret == NULL)
      {
        ERROR ("utils_cache: uc_get_rate_by_name: malloc failed.");
        status = -1;
      }
      else
      {
        memcpy (ret, ce->values_gauge, ret_num * sizeof (gauge_t));
      }
    }
  }
  else
  {
    DEBUG ("utils_cache: uc_get_rate_by_name: No such value: %s", name);
    status = -1;
  }

  pthread_mutex_unlock (&cache_lock);

  if (status == 0)
  {
    *ret_values = ret;
    *ret_values_num = ret_num;
  }

  return (status);
} /* gauge_t *uc_get_rate_by_name */

gauge_t *uc_get_rate (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  gauge_t *ret = NULL;
  size_t ret_num = 0;
  int status;

  if (FORMAT_VL (name, sizeof (name), vl) != 0)
  {
    ERROR ("utils_cache: uc_get_rate: FORMAT_VL failed.");
    return (NULL);
  }

  status = uc_get_rate_by_name (name, &ret, &ret_num);
  if (status != 0)
    return (NULL);

  /* This is important - the caller has no other way of knowing how many
   * values are returned. */
  if (ret_num != (size_t) ds->ds_num)
  {
    ERROR ("utils_cache: uc_get_rate: ds[%s] has %i values, "
	"but uc_get_rate_by_name returned %zu.",
	ds->type, ds->ds_num, ret_num);
    sfree (ret);
    return (NULL);
  }

  return (ret);
} /* gauge_t *uc_get_rate */

int uc_get_names (char ***ret_names, cdtime_t **ret_times, size_t *ret_number)
{
  c_avl_iterator_t *iter;
  char *key;
  cache_entry_t *value;

  char **names = NULL;
  cdtime_t *times = NULL;
  size_t number = 0;

  int status = 0;

  if ((ret_names == NULL) || (ret_number == NULL))
    return (-1);

  pthread_mutex_lock (&cache_lock);

  iter = c_avl_get_iterator (cache_tree);
  while (c_avl_iterator_next (iter, (void *) &key, (void *) &value) == 0)
  {
    char **temp;

    /* remove missing values when list values */
    if (value->state == STATE_MISSING)
      continue;

    if (ret_times != NULL)
    {
      cdtime_t *tmp_times;

      tmp_times = (cdtime_t *) realloc (times, sizeof (cdtime_t) * (number + 1));
      if (tmp_times == NULL)
      {
	status = -1;
	break;
      }
      times = tmp_times;
      times[number] = value->last_time;
    }

    temp = (char **) realloc (names, sizeof (char *) * (number + 1));
    if (temp == NULL)
    {
      status = -1;
      break;
    }
    names = temp;
    names[number] = strdup (key);
    if (names[number] == NULL)
    {
      status = -1;
      break;
    }
    number++;
  } /* while (c_avl_iterator_next) */

  c_avl_iterator_destroy (iter);
  pthread_mutex_unlock (&cache_lock);

  if (status != 0)
  {
    size_t i;
    
    for (i = 0; i < number; i++)
    {
      sfree (names[i]);
    }
    sfree (names);

    return (-1);
  }

  *ret_names = names;
  if (ret_times != NULL)
    *ret_times = times;
  *ret_number = number;

  return (0);
} /* int uc_get_names */

int uc_get_state (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = STATE_ERROR;

  if (FORMAT_VL (name, sizeof (name), vl) != 0)
  {
    ERROR ("uc_get_state: FORMAT_VL failed.");
    return (STATE_ERROR);
  }

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);
    ret = ce->state;
  }

  pthread_mutex_unlock (&cache_lock);

  return (ret);
} /* int uc_get_state */

int uc_set_state (const data_set_t *ds, const value_list_t *vl, int state)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = -1;

  if (FORMAT_VL (name, sizeof (name), vl) != 0)
  {
    ERROR ("uc_get_state: FORMAT_VL failed.");
    return (STATE_ERROR);
  }

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);
    ret = ce->state;
    ce->state = state;
  }

  pthread_mutex_unlock (&cache_lock);

  return (ret);
} /* int uc_set_state */

int uc_get_history_by_name (const char *name,
    gauge_t *ret_history, size_t num_steps, size_t num_ds)
{
  cache_entry_t *ce = NULL;
  size_t i;
  int status = 0;

  pthread_mutex_lock (&cache_lock);

  status = c_avl_get (cache_tree, name, (void *) &ce);
  if (status != 0)
  {
    pthread_mutex_unlock (&cache_lock);
    return (-ENOENT);
  }

  if (((size_t) ce->values_num) != num_ds)
  {
    pthread_mutex_unlock (&cache_lock);
    return (-EINVAL);
  }

  /* Check if there are enough values available. If not, increase the buffer
   * size. */
  if (ce->history_length < num_steps)
  {
    gauge_t *tmp;
    size_t i;

    tmp = realloc (ce->history, sizeof (*ce->history)
	* num_steps * ce->values_num);
    if (tmp == NULL)
    {
      pthread_mutex_unlock (&cache_lock);
      return (-ENOMEM);
    }

    for (i = ce->history_length * ce->values_num;
	i < (num_steps * ce->values_num);
	i++)
      tmp[i] = NAN;

    ce->history = tmp;
    ce->history_length = num_steps;
  } /* if (ce->history_length < num_steps) */

  /* Copy the values to the output buffer. */
  for (i = 0; i < num_steps; i++)
  {
    size_t src_index;
    size_t dst_index;

    if (i < ce->history_index)
      src_index = ce->history_index - (i + 1);
    else
      src_index = ce->history_length + ce->history_index - (i + 1);
    src_index = src_index * num_ds;

    dst_index = i * num_ds;

    memcpy (ret_history + dst_index, ce->history + src_index,
	sizeof (*ret_history) * num_ds);
  }

  pthread_mutex_unlock (&cache_lock);

  return (0);
} /* int uc_get_history_by_name */

int uc_get_history (const data_set_t *ds, const value_list_t *vl,
    gauge_t *ret_history, size_t num_steps, size_t num_ds)
{
  char name[6 * DATA_MAX_NAME_LEN];

  if (FORMAT_VL (name, sizeof (name), vl) != 0)
  {
    ERROR ("utils_cache: uc_get_history: FORMAT_VL failed.");
    return (-1);
  }

  return (uc_get_history_by_name (name, ret_history, num_steps, num_ds));
} /* int uc_get_history */

int uc_get_hits (const data_set_t *ds, const value_list_t *vl)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = STATE_ERROR;

  if (FORMAT_VL (name, sizeof (name), vl) != 0)
  {
    ERROR ("uc_get_state: FORMAT_VL failed.");
    return (STATE_ERROR);
  }

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);
    ret = ce->hits;
  }

  pthread_mutex_unlock (&cache_lock);

  return (ret);
} /* int uc_get_hits */

int uc_set_hits (const data_set_t *ds, const value_list_t *vl, int hits)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = -1;

  if (FORMAT_VL (name, sizeof (name), vl) != 0)
  {
    ERROR ("uc_get_state: FORMAT_VL failed.");
    return (STATE_ERROR);
  }

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);
    ret = ce->hits;
    ce->hits = hits;
  }

  pthread_mutex_unlock (&cache_lock);

  return (ret);
} /* int uc_set_hits */

int uc_inc_hits (const data_set_t *ds, const value_list_t *vl, int step)
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = -1;

  if (FORMAT_VL (name, sizeof (name), vl) != 0)
  {
    ERROR ("uc_get_state: FORMAT_VL failed.");
    return (STATE_ERROR);
  }

  pthread_mutex_lock (&cache_lock);

  if (c_avl_get (cache_tree, name, (void *) &ce) == 0)
  {
    assert (ce != NULL);
    ret = ce->hits;
    ce->hits = ret + step;
  }

  pthread_mutex_unlock (&cache_lock);

  return (ret);
} /* int uc_inc_hits */

/*
 * Meta data interface
 */
/* XXX: This function will acquire `cache_lock' but will not free it! */
static meta_data_t *uc_get_meta (const value_list_t *vl) /* {{{ */
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int status;

  status = FORMAT_VL (name, sizeof (name), vl);
  if (status != 0)
  {
    ERROR ("utils_cache: uc_get_meta: FORMAT_VL failed.");
    return (NULL);
  }

  pthread_mutex_lock (&cache_lock);

  status = c_avl_get (cache_tree, name, (void *) &ce);
  if (status != 0)
  {
    pthread_mutex_unlock (&cache_lock);
    return (NULL);
  }
  assert (ce != NULL);

  if (ce->meta == NULL)
    ce->meta = meta_data_create ();

  if (ce->meta == NULL)
    pthread_mutex_unlock (&cache_lock);

  return (ce->meta);
} /* }}} meta_data_t *uc_get_meta */

/* Sorry about this preprocessor magic, but it really makes this file much
 * shorter.. */
#define UC_WRAP(wrap_function) { \
  meta_data_t *meta; \
  int status; \
  meta = uc_get_meta (vl); \
  if (meta == NULL) return (-1); \
  status = wrap_function (meta, key); \
  pthread_mutex_unlock (&cache_lock); \
  return (status); \
}
int uc_meta_data_exists (const value_list_t *vl, const char *key)
  UC_WRAP (meta_data_exists)

int uc_meta_data_delete (const value_list_t *vl, const char *key)
  UC_WRAP (meta_data_delete)
#undef UC_WRAP

/* We need a new version of this macro because the following functions take
 * two argumetns. */
#define UC_WRAP(wrap_function) { \
  meta_data_t *meta; \
  int status; \
  meta = uc_get_meta (vl); \
  if (meta == NULL) return (-1); \
  status = wrap_function (meta, key, value); \
  pthread_mutex_unlock (&cache_lock); \
  return (status); \
}
int uc_meta_data_add_string (const value_list_t *vl,
    const char *key,
    const char *value)
  UC_WRAP(meta_data_add_string)
int uc_meta_data_add_signed_int (const value_list_t *vl,
    const char *key,
    int64_t value)
  UC_WRAP(meta_data_add_signed_int)
int uc_meta_data_add_unsigned_int (const value_list_t *vl,
    const char *key,
    uint64_t value)
  UC_WRAP(meta_data_add_unsigned_int)
int uc_meta_data_add_double (const value_list_t *vl,
    const char *key,
    double value)
  UC_WRAP(meta_data_add_double)
int uc_meta_data_add_boolean (const value_list_t *vl,
    const char *key,
    _Bool value)
  UC_WRAP(meta_data_add_boolean)

int uc_meta_data_get_string (const value_list_t *vl,
    const char *key,
    char **value)
  UC_WRAP(meta_data_get_string)
int uc_meta_data_get_signed_int (const value_list_t *vl,
    const char *key,
    int64_t *value)
  UC_WRAP(meta_data_get_signed_int)
int uc_meta_data_get_unsigned_int (const value_list_t *vl,
    const char *key,
    uint64_t *value)
  UC_WRAP(meta_data_get_unsigned_int)
int uc_meta_data_get_double (const value_list_t *vl,
    const char *key,
    double *value)
  UC_WRAP(meta_data_get_double)
int uc_meta_data_get_boolean (const value_list_t *vl,
    const char *key,
    _Bool *value)
  UC_WRAP(meta_data_get_boolean)
#undef UC_WRAP

/* vim: set sw=2 ts=8 sts=2 tw=78 : */
