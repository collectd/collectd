/**
 * collectd - src/utils_cache.c
 * Copyright (C) 2007-2010  Florian octo Forster
 * Copyright (C) 2016       Sebastian tokkee Harl
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
 *   Sebastian tokkee Harl <sh at tokkee.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils/metadata/meta_data.h"
#include "utils_cache.h"

#include <assert.h>

typedef struct cache_entry_s {
  char name[6 * DATA_MAX_NAME_LEN];
  size_t values_num;
  gauge_t *values_gauge;
  value_t *values_raw;
  /* Time contained in the package
   * (for calculating rates) */
  cdtime_t last_time;
  /* Time according to the local clock
   * (for purging old entries) */
  cdtime_t last_update;
  /* Interval in which the data is collected
   * (for purging old entries) */
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
  size_t history_index; /* points to the next position to write to. */
  size_t history_length;

  meta_data_t *meta;
  unsigned long callbacks_mask;
} cache_entry_t;

struct uc_iter_s {
  c_avl_iterator_t *iter;

  char *name;
  cache_entry_t *entry;
};

static c_avl_tree_t *cache_tree;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

static int cache_compare(const cache_entry_t *a, const cache_entry_t *b) {
#if COLLECT_DEBUG
  assert((a != NULL) && (b != NULL));
#endif
  return strcmp(a->name, b->name);
} /* int cache_compare */

static cache_entry_t *cache_alloc(size_t values_num) {
  cache_entry_t *ce;

  ce = calloc(1, sizeof(*ce));
  if (ce == NULL) {
    ERROR("utils_cache: cache_alloc: calloc failed.");
    return NULL;
  }
  ce->values_num = values_num;

  ce->values_gauge = calloc(values_num, sizeof(*ce->values_gauge));
  ce->values_raw = calloc(values_num, sizeof(*ce->values_raw));
  if ((ce->values_gauge == NULL) || (ce->values_raw == NULL)) {
    sfree(ce->values_gauge);
    sfree(ce->values_raw);
    sfree(ce);
    ERROR("utils_cache: cache_alloc: calloc failed.");
    return NULL;
  }

  ce->history = NULL;
  ce->history_length = 0;
  ce->meta = NULL;

  return ce;
} /* cache_entry_t *cache_alloc */

static void cache_free(cache_entry_t *ce) {
  if (ce == NULL)
    return;

  sfree(ce->values_gauge);
  sfree(ce->values_raw);
  sfree(ce->history);
  if (ce->meta != NULL) {
    meta_data_destroy(ce->meta);
    ce->meta = NULL;
  }
  sfree(ce);
} /* void cache_free */

static void uc_check_range(const data_set_t *ds, cache_entry_t *ce) {
  for (size_t i = 0; i < ds->ds_num; i++) {
    if (isnan(ce->values_gauge[i]))
      continue;
    else if (ce->values_gauge[i] < ds->ds[i].min)
      ce->values_gauge[i] = NAN;
    else if (ce->values_gauge[i] > ds->ds[i].max)
      ce->values_gauge[i] = NAN;
  }
} /* void uc_check_range */

static int uc_insert(const data_set_t *ds, const value_list_t *vl,
                     const char *key) {
  /* `cache_lock' has been locked by `uc_update' */

  char *key_copy = strdup(key);
  if (key_copy == NULL) {
    ERROR("uc_insert: strdup failed.");
    return -1;
  }

  cache_entry_t *ce = cache_alloc(ds->ds_num);
  if (ce == NULL) {
    sfree(key_copy);
    ERROR("uc_insert: cache_alloc (%" PRIsz ") failed.", ds->ds_num);
    return -1;
  }

  sstrncpy(ce->name, key, sizeof(ce->name));

  for (size_t i = 0; i < ds->ds_num; i++) {
    switch (ds->ds[i].type) {
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
        ce->values_gauge[i] =
            ((double)vl->values[i].absolute) / CDTIME_T_TO_DOUBLE(vl->interval);
      ce->values_raw[i].absolute = vl->values[i].absolute;
      break;

    default:
      /* This shouldn't happen. */
      ERROR("uc_insert: Don't know how to handle data source type %i.",
            ds->ds[i].type);
      sfree(key_copy);
      cache_free(ce);
      return -1;
    } /* switch (ds->ds[i].type) */
  }   /* for (i) */

  /* Prune invalid gauge data */
  uc_check_range(ds, ce);

  ce->last_time = vl->time;
  ce->last_update = cdtime();
  ce->interval = vl->interval;
  ce->state = STATE_UNKNOWN;

  if (vl->meta != NULL) {
    ce->meta = meta_data_clone(vl->meta);
  }

  if (c_avl_insert(cache_tree, key_copy, ce) != 0) {
    sfree(key_copy);
    ERROR("uc_insert: c_avl_insert failed.");
    return -1;
  }

  DEBUG("uc_insert: Added %s to the cache.", key);
  return 0;
} /* int uc_insert */

int uc_init(void) {
  if (cache_tree == NULL)
    cache_tree =
        c_avl_create((int (*)(const void *, const void *))cache_compare);

  return 0;
} /* int uc_init */

int uc_check_timeout(void) {
  struct {
    char *key;
    cdtime_t time;
    cdtime_t interval;
    unsigned long callbacks_mask;
  } *expired = NULL;
  size_t expired_num = 0;

  pthread_mutex_lock(&cache_lock);
  cdtime_t now = cdtime();

  /* Build a list of entries to be flushed */
  c_avl_iterator_t *iter = c_avl_get_iterator(cache_tree);
  char *key = NULL;
  cache_entry_t *ce = NULL;
  while (c_avl_iterator_next(iter, (void *)&key, (void *)&ce) == 0) {
    /* If the entry is fresh enough, continue. */
    if ((now - ce->last_update) < (ce->interval * timeout_g))
      continue;

    void *tmp = realloc(expired, (expired_num + 1) * sizeof(*expired));
    if (tmp == NULL) {
      ERROR("uc_check_timeout: realloc failed.");
      continue;
    }
    expired = tmp;

    expired[expired_num].key = strdup(key);
    expired[expired_num].time = ce->last_time;
    expired[expired_num].interval = ce->interval;
    expired[expired_num].callbacks_mask = ce->callbacks_mask;

    if (expired[expired_num].key == NULL) {
      ERROR("uc_check_timeout: strdup failed.");
      continue;
    }

    expired_num++;
  } /* while (c_avl_iterator_next) */

  c_avl_iterator_destroy(iter);
  pthread_mutex_unlock(&cache_lock);

  if (expired_num == 0) {
    sfree(expired);
    return 0;
  }

  /* Call the "missing" callback for each value. Do this before removing the
   * value from the cache, so that callbacks can still access the data stored,
   * including plugin specific meta data, rates, history, …. This must be done
   * without holding the lock, otherwise we will run into a deadlock if a
   * plugin calls the cache interface. */
  for (size_t i = 0; i < expired_num; i++) {
    value_list_t vl = {
        .time = expired[i].time,
        .interval = expired[i].interval,
    };

    if (parse_identifier_vl(expired[i].key, &vl) != 0) {
      ERROR("uc_check_timeout: parse_identifier_vl (\"%s\") failed.",
            expired[i].key);
      continue;
    }

    plugin_dispatch_missing(&vl);

    if (expired[i].callbacks_mask)
      plugin_dispatch_cache_event(CE_VALUE_EXPIRED, expired[i].callbacks_mask,
                                  expired[i].key, &vl);
  } /* for (i = 0; i < expired_num; i++) */

  /* Now actually remove all the values from the cache. We don't re-evaluate
   * the timestamp again, so in theory it is possible we remove a value after
   * it is updated here. */
  pthread_mutex_lock(&cache_lock);
  for (size_t i = 0; i < expired_num; i++) {
    char *key = NULL;
    cache_entry_t *value = NULL;

    if (c_avl_remove(cache_tree, expired[i].key, (void *)&key,
                     (void *)&value) != 0) {
      ERROR("uc_check_timeout: c_avl_remove (\"%s\") failed.", expired[i].key);
      sfree(expired[i].key);
      continue;
    }
    sfree(key);
    cache_free(value);

    sfree(expired[i].key);
  } /* for (i = 0; i < expired_num; i++) */
  pthread_mutex_unlock(&cache_lock);

  sfree(expired);
  return 0;
} /* int uc_check_timeout */

int uc_update(const data_set_t *ds, const value_list_t *vl) {
  char name[6 * DATA_MAX_NAME_LEN];

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("uc_update: FORMAT_VL failed.");
    return -1;
  }

  pthread_mutex_lock(&cache_lock);

  cache_entry_t *ce = NULL;
  int status = c_avl_get(cache_tree, name, (void *)&ce);
  if (status != 0) /* entry does not yet exist */
  {
    status = uc_insert(ds, vl, name);
    pthread_mutex_unlock(&cache_lock);

    if (status == 0)
      plugin_dispatch_cache_event(CE_VALUE_NEW, 0 /* mask */, name, vl);

    return status;
  }

  assert(ce != NULL);
  assert(ce->values_num == ds->ds_num);

  if (ce->last_time >= vl->time) {
    pthread_mutex_unlock(&cache_lock);
    NOTICE("uc_update: Value too old: name = %s; value time = %.3f; "
           "last cache update = %.3f;",
           name, CDTIME_T_TO_DOUBLE(vl->time),
           CDTIME_T_TO_DOUBLE(ce->last_time));
    return -1;
  }

  for (size_t i = 0; i < ds->ds_num; i++) {
    switch (ds->ds[i].type) {
    case DS_TYPE_COUNTER: {
      counter_t diff =
          counter_diff(ce->values_raw[i].counter, vl->values[i].counter);
      ce->values_gauge[i] =
          ((double)diff) / (CDTIME_T_TO_DOUBLE(vl->time - ce->last_time));
      ce->values_raw[i].counter = vl->values[i].counter;
    } break;

    case DS_TYPE_GAUGE:
      ce->values_raw[i].gauge = vl->values[i].gauge;
      ce->values_gauge[i] = vl->values[i].gauge;
      break;

    case DS_TYPE_DERIVE: {
      derive_t diff = vl->values[i].derive - ce->values_raw[i].derive;

      ce->values_gauge[i] =
          ((double)diff) / (CDTIME_T_TO_DOUBLE(vl->time - ce->last_time));
      ce->values_raw[i].derive = vl->values[i].derive;
    } break;

    case DS_TYPE_ABSOLUTE:
      ce->values_gauge[i] = ((double)vl->values[i].absolute) /
                            (CDTIME_T_TO_DOUBLE(vl->time - ce->last_time));
      ce->values_raw[i].absolute = vl->values[i].absolute;
      break;

    default:
      /* This shouldn't happen. */
      pthread_mutex_unlock(&cache_lock);
      ERROR("uc_update: Don't know how to handle data source type %i.",
            ds->ds[i].type);
      return -1;
    } /* switch (ds->ds[i].type) */

    DEBUG("uc_update: %s: ds[%" PRIsz "] = %lf", name, i, ce->values_gauge[i]);
  } /* for (i) */

  /* Update the history if it exists. */
  if (ce->history != NULL) {
    assert(ce->history_index < ce->history_length);
    for (size_t i = 0; i < ce->values_num; i++) {
      size_t hist_idx = (ce->values_num * ce->history_index) + i;
      ce->history[hist_idx] = ce->values_gauge[i];
    }

    assert(ce->history_length > 0);
    ce->history_index = (ce->history_index + 1) % ce->history_length;
  }

  /* Prune invalid gauge data */
  uc_check_range(ds, ce);

  ce->last_time = vl->time;
  ce->last_update = cdtime();
  ce->interval = vl->interval;

  /* Check if cache entry has registered callbacks */
  unsigned long callbacks_mask = ce->callbacks_mask;

  pthread_mutex_unlock(&cache_lock);

  if (callbacks_mask)
    plugin_dispatch_cache_event(CE_VALUE_UPDATE, callbacks_mask, name, vl);

  return 0;
} /* int uc_update */

int uc_set_callbacks_mask(const char *name, unsigned long mask) {
  pthread_mutex_lock(&cache_lock);
  cache_entry_t *ce = NULL;
  int status = c_avl_get(cache_tree, name, (void *)&ce);
  if (status != 0) { /* Ouch, just created entry disappeared ?! */
    ERROR("uc_set_callbacks_mask: Couldn't find %s entry!", name);
    pthread_mutex_unlock(&cache_lock);
    return -1;
  }
  DEBUG("uc_set_callbacks_mask: set mask for \"%s\" to %lu.", name, mask);
  ce->callbacks_mask = mask;
  pthread_mutex_unlock(&cache_lock);
  return 0;
}

int uc_get_rate_by_name(const char *name, gauge_t **ret_values,
                        size_t *ret_values_num) {
  gauge_t *ret = NULL;
  size_t ret_num = 0;
  cache_entry_t *ce = NULL;
  int status = 0;

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);

    /* remove missing values from getval */
    if (ce->state == STATE_MISSING) {
      DEBUG("utils_cache: uc_get_rate_by_name: requested metric \"%s\" is in "
            "state \"missing\".",
            name);
      status = -1;
    } else {
      ret_num = ce->values_num;
      ret = malloc(ret_num * sizeof(*ret));
      if (ret == NULL) {
        ERROR("utils_cache: uc_get_rate_by_name: malloc failed.");
        status = -1;
      } else {
        memcpy(ret, ce->values_gauge, ret_num * sizeof(gauge_t));
      }
    }
  } else {
    DEBUG("utils_cache: uc_get_rate_by_name: No such value: %s", name);
    status = -1;
  }

  pthread_mutex_unlock(&cache_lock);

  if (status == 0) {
    *ret_values = ret;
    *ret_values_num = ret_num;
  }

  return status;
} /* gauge_t *uc_get_rate_by_name */

gauge_t *uc_get_rate(const data_set_t *ds, const value_list_t *vl) {
  char name[6 * DATA_MAX_NAME_LEN];
  gauge_t *ret = NULL;
  size_t ret_num = 0;
  int status;

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("utils_cache: uc_get_rate: FORMAT_VL failed.");
    return NULL;
  }

  status = uc_get_rate_by_name(name, &ret, &ret_num);
  if (status != 0)
    return NULL;

  /* This is important - the caller has no other way of knowing how many
   * values are returned. */
  if (ret_num != ds->ds_num) {
    ERROR("utils_cache: uc_get_rate: ds[%s] has %" PRIsz " values, "
          "but uc_get_rate_by_name returned %" PRIsz ".",
          ds->type, ds->ds_num, ret_num);
    sfree(ret);
    return NULL;
  }

  return ret;
} /* gauge_t *uc_get_rate */

int uc_get_value_by_name(const char *name, value_t **ret_values,
                         size_t *ret_values_num) {
  value_t *ret = NULL;
  size_t ret_num = 0;
  cache_entry_t *ce = NULL;
  int status = 0;

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);

    /* remove missing values from getval */
    if (ce->state == STATE_MISSING) {
      status = -1;
    } else {
      ret_num = ce->values_num;
      ret = malloc(ret_num * sizeof(*ret));
      if (ret == NULL) {
        ERROR("utils_cache: uc_get_value_by_name: malloc failed.");
        status = -1;
      } else {
        memcpy(ret, ce->values_raw, ret_num * sizeof(value_t));
      }
    }
  } else {
    DEBUG("utils_cache: uc_get_value_by_name: No such value: %s", name);
    status = -1;
  }

  pthread_mutex_unlock(&cache_lock);

  if (status == 0) {
    *ret_values = ret;
    *ret_values_num = ret_num;
  }

  return (status);
} /* int uc_get_value_by_name */

value_t *uc_get_value(const data_set_t *ds, const value_list_t *vl) {
  char name[6 * DATA_MAX_NAME_LEN];
  value_t *ret = NULL;
  size_t ret_num = 0;
  int status;

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("utils_cache: uc_get_value: FORMAT_VL failed.");
    return (NULL);
  }

  status = uc_get_value_by_name(name, &ret, &ret_num);
  if (status != 0)
    return (NULL);

  /* This is important - the caller has no other way of knowing how many
   * values are returned. */
  if (ret_num != (size_t)ds->ds_num) {
    ERROR("utils_cache: uc_get_value: ds[%s] has %" PRIsz " values, "
          "but uc_get_value_by_name returned %" PRIsz ".",
          ds->type, ds->ds_num, ret_num);
    sfree(ret);
    return (NULL);
  }

  return (ret);
} /* value_t *uc_get_value */

size_t uc_get_size(void) {
  size_t size_arrays = 0;

  pthread_mutex_lock(&cache_lock);
  size_arrays = (size_t)c_avl_size(cache_tree);
  pthread_mutex_unlock(&cache_lock);

  return size_arrays;
}

int uc_get_names(char ***ret_names, cdtime_t **ret_times, size_t *ret_number) {
  c_avl_iterator_t *iter;
  char *key;
  cache_entry_t *value;

  char **names = NULL;
  cdtime_t *times = NULL;
  size_t number = 0;
  size_t size_arrays = 0;

  int status = 0;

  if ((ret_names == NULL) || (ret_number == NULL))
    return -1;

  pthread_mutex_lock(&cache_lock);

  size_arrays = (size_t)c_avl_size(cache_tree);
  if (size_arrays < 1) {
    /* Handle the "no values" case here, to avoid the error message when
     * calloc() returns NULL. */
    pthread_mutex_unlock(&cache_lock);
    return 0;
  }

  names = calloc(size_arrays, sizeof(*names));
  times = calloc(size_arrays, sizeof(*times));
  if ((names == NULL) || (times == NULL)) {
    ERROR("uc_get_names: calloc failed.");
    sfree(names);
    sfree(times);
    pthread_mutex_unlock(&cache_lock);
    return ENOMEM;
  }

  iter = c_avl_get_iterator(cache_tree);
  while (c_avl_iterator_next(iter, (void *)&key, (void *)&value) == 0) {
    /* remove missing values when list values */
    if (value->state == STATE_MISSING)
      continue;

    /* c_avl_size does not return a number smaller than the number of elements
     * returned by c_avl_iterator_next. */
    assert(number < size_arrays);

    if (ret_times != NULL)
      times[number] = value->last_time;

    names[number] = strdup(key);
    if (names[number] == NULL) {
      status = -1;
      break;
    }

    number++;
  } /* while (c_avl_iterator_next) */

  c_avl_iterator_destroy(iter);
  pthread_mutex_unlock(&cache_lock);

  if (status != 0) {
    for (size_t i = 0; i < number; i++) {
      sfree(names[i]);
    }
    sfree(names);
    sfree(times);

    return -1;
  }

  *ret_names = names;
  if (ret_times != NULL)
    *ret_times = times;
  else
    sfree(times);
  *ret_number = number;

  return 0;
} /* int uc_get_names */

int uc_get_state(const data_set_t *ds, const value_list_t *vl) {
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = STATE_ERROR;

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("uc_get_state: FORMAT_VL failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->state;
  }

  pthread_mutex_unlock(&cache_lock);

  return ret;
} /* int uc_get_state */

int uc_set_state(const data_set_t *ds, const value_list_t *vl, int state) {
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = -1;

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("uc_set_state: FORMAT_VL failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->state;
    ce->state = state;
  }

  pthread_mutex_unlock(&cache_lock);

  return ret;
} /* int uc_set_state */

int uc_get_history_by_name(const char *name, gauge_t *ret_history,
                           size_t num_steps, size_t num_ds) {
  cache_entry_t *ce = NULL;
  int status = 0;

  pthread_mutex_lock(&cache_lock);

  status = c_avl_get(cache_tree, name, (void *)&ce);
  if (status != 0) {
    pthread_mutex_unlock(&cache_lock);
    return -ENOENT;
  }

  if (((size_t)ce->values_num) != num_ds) {
    pthread_mutex_unlock(&cache_lock);
    return -EINVAL;
  }

  /* Check if there are enough values available. If not, increase the buffer
   * size. */
  if (ce->history_length < num_steps) {
    gauge_t *tmp;

    tmp =
        realloc(ce->history, sizeof(*ce->history) * num_steps * ce->values_num);
    if (tmp == NULL) {
      pthread_mutex_unlock(&cache_lock);
      return -ENOMEM;
    }

    for (size_t i = ce->history_length * ce->values_num;
         i < (num_steps * ce->values_num); i++)
      tmp[i] = NAN;

    ce->history = tmp;
    ce->history_length = num_steps;
  } /* if (ce->history_length < num_steps) */

  /* Copy the values to the output buffer. */
  for (size_t i = 0; i < num_steps; i++) {
    size_t src_index;
    size_t dst_index;

    if (i < ce->history_index)
      src_index = ce->history_index - (i + 1);
    else
      src_index = ce->history_length + ce->history_index - (i + 1);
    src_index = src_index * num_ds;

    dst_index = i * num_ds;

    memcpy(ret_history + dst_index, ce->history + src_index,
           sizeof(*ret_history) * num_ds);
  }

  pthread_mutex_unlock(&cache_lock);

  return 0;
} /* int uc_get_history_by_name */

int uc_get_history(const data_set_t *ds, const value_list_t *vl,
                   gauge_t *ret_history, size_t num_steps, size_t num_ds) {
  char name[6 * DATA_MAX_NAME_LEN];

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("utils_cache: uc_get_history: FORMAT_VL failed.");
    return -1;
  }

  return uc_get_history_by_name(name, ret_history, num_steps, num_ds);
} /* int uc_get_history */

int uc_get_hits(const data_set_t *ds, const value_list_t *vl) {
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = STATE_ERROR;

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("uc_get_hits: FORMAT_VL failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->hits;
  }

  pthread_mutex_unlock(&cache_lock);

  return ret;
} /* int uc_get_hits */

int uc_set_hits(const data_set_t *ds, const value_list_t *vl, int hits) {
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = -1;

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("uc_set_hits: FORMAT_VL failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->hits;
    ce->hits = hits;
  }

  pthread_mutex_unlock(&cache_lock);

  return ret;
} /* int uc_set_hits */

int uc_inc_hits(const data_set_t *ds, const value_list_t *vl, int step) {
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int ret = -1;

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("uc_inc_hits: FORMAT_VL failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->hits;
    ce->hits = ret + step;
  }

  pthread_mutex_unlock(&cache_lock);

  return ret;
} /* int uc_inc_hits */

/*
 * Iterator interface
 */
uc_iter_t *uc_get_iterator(void) {
  uc_iter_t *iter = calloc(1, sizeof(*iter));
  if (iter == NULL)
    return NULL;

  pthread_mutex_lock(&cache_lock);

  iter->iter = c_avl_get_iterator(cache_tree);
  if (iter->iter == NULL) {
    free(iter);
    return NULL;
  }

  return iter;
} /* uc_iter_t *uc_get_iterator */

int uc_iterator_next(uc_iter_t *iter, char **ret_name) {
  int status;

  if (iter == NULL)
    return -1;

  while ((status = c_avl_iterator_next(iter->iter, (void *)&iter->name,
                                       (void *)&iter->entry)) == 0) {
    if (iter->entry->state == STATE_MISSING)
      continue;

    break;
  }
  if (status != 0) {
    iter->name = NULL;
    iter->entry = NULL;
    return -1;
  }

  if (ret_name != NULL)
    *ret_name = iter->name;

  return 0;
} /* int uc_iterator_next */

void uc_iterator_destroy(uc_iter_t *iter) {
  if (iter == NULL)
    return;

  c_avl_iterator_destroy(iter->iter);
  pthread_mutex_unlock(&cache_lock);

  free(iter);
} /* void uc_iterator_destroy */

int uc_iterator_get_time(uc_iter_t *iter, cdtime_t *ret_time) {
  if ((iter == NULL) || (iter->entry == NULL) || (ret_time == NULL))
    return -1;

  *ret_time = iter->entry->last_time;
  return 0;
} /* int uc_iterator_get_name */

int uc_iterator_get_values(uc_iter_t *iter, value_t **ret_values,
                           size_t *ret_num) {
  if ((iter == NULL) || (iter->entry == NULL) || (ret_values == NULL) ||
      (ret_num == NULL))
    return -1;
  *ret_values =
      calloc(iter->entry->values_num, sizeof(*iter->entry->values_raw));
  if (*ret_values == NULL)
    return -1;
  for (size_t i = 0; i < iter->entry->values_num; ++i)
    (*ret_values)[i] = iter->entry->values_raw[i];

  *ret_num = iter->entry->values_num;

  return 0;
} /* int uc_iterator_get_values */

int uc_iterator_get_interval(uc_iter_t *iter, cdtime_t *ret_interval) {
  if ((iter == NULL) || (iter->entry == NULL) || (ret_interval == NULL))
    return -1;

  *ret_interval = iter->entry->interval;
  return 0;
} /* int uc_iterator_get_name */

int uc_iterator_get_meta(uc_iter_t *iter, meta_data_t **ret_meta) {
  if ((iter == NULL) || (iter->entry == NULL) || (ret_meta == NULL))
    return -1;

  *ret_meta = meta_data_clone(iter->entry->meta);

  return 0;
} /* int uc_iterator_get_meta */

/*
 * Meta data interface
 */
/* XXX: This function will acquire `cache_lock' but will not free it! */
static meta_data_t *uc_get_meta(const value_list_t *vl) /* {{{ */
{
  char name[6 * DATA_MAX_NAME_LEN];
  cache_entry_t *ce = NULL;
  int status;

  status = FORMAT_VL(name, sizeof(name), vl);
  if (status != 0) {
    ERROR("utils_cache: uc_get_meta: FORMAT_VL failed.");
    return NULL;
  }

  pthread_mutex_lock(&cache_lock);

  status = c_avl_get(cache_tree, name, (void *)&ce);
  if (status != 0) {
    pthread_mutex_unlock(&cache_lock);
    return NULL;
  }
  assert(ce != NULL);

  if (ce->meta == NULL)
    ce->meta = meta_data_create();

  if (ce->meta == NULL)
    pthread_mutex_unlock(&cache_lock);

  return ce->meta;
} /* }}} meta_data_t *uc_get_meta */

/* Sorry about this preprocessor magic, but it really makes this file much
 * shorter.. */
#define UC_WRAP(wrap_function)                                                 \
  {                                                                            \
    meta_data_t *meta;                                                         \
    int status;                                                                \
    meta = uc_get_meta(vl);                                                    \
    if (meta == NULL)                                                          \
      return -1;                                                               \
    status = wrap_function(meta, key);                                         \
    pthread_mutex_unlock(&cache_lock);                                         \
    return status;                                                             \
  }
int uc_meta_data_exists(const value_list_t *vl, const char *key)
    UC_WRAP(meta_data_exists)

        int uc_meta_data_delete(const value_list_t *vl, const char *key)
            UC_WRAP(meta_data_delete)

    /* The second argument is called `toc` in the API, but the macro expects
     * `key`. */
    int uc_meta_data_toc(const value_list_t *vl,
                         char ***key) UC_WRAP(meta_data_toc)

#undef UC_WRAP

/* We need a new version of this macro because the following functions take
 * two argumetns. */
#define UC_WRAP(wrap_function)                                                 \
  {                                                                            \
    meta_data_t *meta;                                                         \
    int status;                                                                \
    meta = uc_get_meta(vl);                                                    \
    if (meta == NULL)                                                          \
      return -1;                                                               \
    status = wrap_function(meta, key, value);                                  \
    pthread_mutex_unlock(&cache_lock);                                         \
    return status;                                                             \
  }
        int uc_meta_data_add_string(const value_list_t *vl, const char *key,
                                    const char *value)
            UC_WRAP(meta_data_add_string) int uc_meta_data_add_signed_int(
                const value_list_t *vl, const char *key, int64_t value)
                UC_WRAP(meta_data_add_signed_int) int uc_meta_data_add_unsigned_int(
                    const value_list_t *vl, const char *key, uint64_t value)
                    UC_WRAP(meta_data_add_unsigned_int) int uc_meta_data_add_double(
                        const value_list_t *vl, const char *key, double value)
                        UC_WRAP(meta_data_add_double) int uc_meta_data_add_boolean(
                            const value_list_t *vl, const char *key,
                            bool value) UC_WRAP(meta_data_add_boolean)

                            int uc_meta_data_get_string(const value_list_t *vl,
                                                        const char *key,
                                                        char **value)
                                UC_WRAP(meta_data_get_string) int uc_meta_data_get_signed_int(
                                    const value_list_t *vl, const char *key,
                                    int64_t *value)
                                    UC_WRAP(meta_data_get_signed_int) int uc_meta_data_get_unsigned_int(
                                        const value_list_t *vl, const char *key,
                                        uint64_t *value)
                                        UC_WRAP(meta_data_get_unsigned_int) int uc_meta_data_get_double(
                                            const value_list_t *vl,
                                            const char *key, double *value)
                                            UC_WRAP(meta_data_get_double) int uc_meta_data_get_boolean(
                                                const value_list_t *vl,
                                                const char *key, bool *value)
                                                UC_WRAP(meta_data_get_boolean)
#undef UC_WRAP
