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
 *   Manoj Srivastava <srivasta at google.com>
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
  gauge_t values_gauge;
  value_t values_raw;
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
  identity_t *identity;

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

static cache_entry_t *cache_alloc() {
  cache_entry_t *ce;

  ce = calloc(1, sizeof(*ce));
  if (ce == NULL) {
    ERROR("utils_cache: cache_alloc: calloc failed.");
    return NULL;
  }

  ce->values_gauge = 0;
  ce->values_raw = (value_t){.gauge = 0};
  ce->history = NULL;
  ce->history_length = 0;
  ce->meta = NULL;
  ce->identity = NULL;

  return ce;
} /* cache_entry_t *cache_alloc */

static void cache_free(cache_entry_t *ce) {
  if (ce == NULL)
    return;

  sfree(ce->history);

  if (ce->identity != NULL) {
    identity_destroy(ce->identity);
  }

  /* We have non-exclusive ownership of the metadata */
  if (ce->meta != NULL) {
    meta_data_destroy(ce->meta);
    ce->meta = NULL;
  }
  sfree(ce);
} /* void cache_free */

static int uc_insert(metric_t const *metric_p, char const *key) {
  /* `cache_lock' has been locked by `uc_update' */

  char *key_copy = strdup(key);
  if (key_copy == NULL) {
    ERROR("uc_insert: strdup failed.");
    return -1;
  }

  cache_entry_t *ce = cache_alloc();
  if (ce == NULL) {
    sfree(key_copy);
    ERROR("uc_insert: cache_alloc failed.");
    return -1;
  }

  sstrncpy(ce->name, key, sizeof(ce->name));

  switch (metric_p->value_type) {
  case DS_TYPE_COUNTER:
    ce->values_gauge = NAN;
    ce->values_raw.counter = metric_p->value.counter;
    break;

  case DS_TYPE_GAUGE:
    ce->values_gauge = metric_p->value.gauge;
    ce->values_raw.gauge = metric_p->value.gauge;
    break;

  case DS_TYPE_DERIVE:
    ce->values_gauge = NAN;
    ce->values_raw.derive = metric_p->value.derive;
    break;

  default:
    /* This shouldn't happen. */
    ERROR("uc_insert: Don't know how to handle data source type %i.",
          metric_p->value_type);
    sfree(key_copy);
    cache_free(ce);
    return -1;
  } /* switch (ds->ds[i].type) */

  ce->last_time = metric_p->time;
  ce->last_update = cdtime();
  ce->interval = metric_p->interval;
  ce->state = STATE_UNKNOWN;
  ce->meta = meta_data_clone(metric_p->meta);

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
    metric_t metric = {
        .time = expired[i].time,
        .interval = expired[i].interval,
    };

    metric.identity = identity_parse(expired[i].key);
    if (metric.identity == NULL) {
      ERROR("uc_check_timeout: parse_identifier_vl (\"%s\") failed: %s",
            expired[i].key, STRERRNO);
      continue;
    }

    plugin_dispatch_missing(&metric);

    if (expired[i].callbacks_mask)
      plugin_dispatch_cache_event(CE_VALUE_EXPIRED, expired[i].callbacks_mask,
                                  expired[i].key, &metric);
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

int uc_update(const metric_t *metric_p) {
  char *metric_name = plugin_format_metric(metric_p);
  if (metric_name == NULL) {
    ERROR("uc_update: plugin_format_metric failed.");
    return -1;
  }

  pthread_mutex_lock(&cache_lock);

  cache_entry_t *ce = NULL;
  int status = c_avl_get(cache_tree, metric_name, (void *)&ce);
  if (status != 0) /* entry does not yet exist */
  {
    status = uc_insert(metric_p, metric_name);
    pthread_mutex_unlock(&cache_lock);

    if (status == 0)
      plugin_dispatch_cache_event(CE_VALUE_NEW, 0 /* mask */, metric_name,
                                  metric_p);

    sfree(metric_name);
    return status;
  }

  assert(ce != NULL);
  ce->identity = identity_clone(metric_p->identity);
  ce->meta = meta_data_clone(metric_p->meta);
  if (ce->last_time >= metric_p->time) {
    pthread_mutex_unlock(&cache_lock);
    NOTICE("uc_update: Value too old: name = %s; value time = %.3f; "
           "last cache update = %.3f;",
           metric_name, CDTIME_T_TO_DOUBLE(metric_p->time),
           CDTIME_T_TO_DOUBLE(ce->last_time));
    sfree(metric_name);
    return -1;
  }

  switch (metric_p->value_type) {
  case DS_TYPE_COUNTER: {
    counter_t diff =
        counter_diff(ce->values_raw.counter, metric_p->value.counter);
    ce->values_gauge =
        ((double)diff) / (CDTIME_T_TO_DOUBLE(metric_p->time - ce->last_time));
    ce->values_raw.counter = metric_p->value.counter;
  } break;

  case DS_TYPE_GAUGE:
    ce->values_raw.gauge = metric_p->value.gauge;
    ce->values_gauge = metric_p->value.gauge;
    break;

  case DS_TYPE_DERIVE: {
    derive_t diff = metric_p->value.derive - ce->values_raw.derive;

    ce->values_gauge =
        ((double)diff) / (CDTIME_T_TO_DOUBLE(metric_p->time - ce->last_time));
    ce->values_raw.derive = metric_p->value.derive;
  } break;

  default:
    /* This shouldn't happen. */
    pthread_mutex_unlock(&cache_lock);
    ERROR("uc_update: Don't know how to handle data source type %i.",
          metric_p->value_type);
    sfree(metric_name);
    return -1;
  } /* switch (metric_p->ds->type) */

  DEBUG("uc_update: %s: ds[%" PRIsz "] = %lf", name, i, ce->values_gauge);

  /* Update the history if it exists. TODO: Does history need to be an array? */
  if (ce->history != NULL) {
    assert(ce->history_index < ce->history_length);
    ce->history[0] = ce->values_gauge;

    assert(ce->history_length > 0);
    ce->history_index = (ce->history_index + 1) % ce->history_length;
  }

  ce->last_time = metric_p->time;
  ce->last_update = cdtime();
  ce->interval = metric_p->interval;

  /* Check if cache entry has registered callbacks */
  unsigned long callbacks_mask = ce->callbacks_mask;

  pthread_mutex_unlock(&cache_lock);

  if (callbacks_mask) {
    plugin_dispatch_cache_event(CE_VALUE_UPDATE, callbacks_mask, metric_name,
                                metric_p);
  }

  sfree(metric_name);
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

int uc_get_rate_by_name(const char *name, gauge_t *ret_values) {
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
      *ret_values = ce->values_gauge;
    }
  } else {
    DEBUG("utils_cache: uc_get_rate_by_name: No such value: %s", name);
    status = -1;
  }

  pthread_mutex_unlock(&cache_lock);

  return status;
} /* gauge_t *uc_get_rate_by_name */

int uc_get_rate(const metric_t *metric_p, gauge_t *ret) {
  char *metric_string_p = NULL;
  int status = 0;

  metric_string_p = plugin_format_metric(metric_p);

  if (metric_string_p == NULL) {
    ERROR("utils_cache: uc_get_rate: plugin_format_metric failed.");
    return -1;
  }

  status = uc_get_rate_by_name(metric_string_p, ret);
  if (status != 0) {
    return status;
  }

  return status;
} /* gauge_t *uc_get_rate */

gauge_t *uc_get_rate_vl(const data_set_t *ds, const value_list_t *vl) {
  char name[6 * DATA_MAX_NAME_LEN];
  metrics_list_t *ml = NULL;
  int retval = 0;

  if (FORMAT_VL(name, sizeof(name), vl) != 0) {
    ERROR("utils_cache: uc_get_rate: FORMAT_VL failed.");
    return NULL;
  }
  gauge_t *ret = (gauge_t *)malloc(ds->ds_num * sizeof(*ret));
  if (ret == NULL) {
    WARNING("uc_get_rate_vl: failed to allocate memory.");
    return NULL;
  }
  retval = plugin_convert_values_to_metrics(vl, &ml);
  if (retval != 0) {
    WARNING("uc_get_rate_vl: Could not parse value list %s.", name);
    return NULL;
  }
  metrics_list_t *index_p = ml;
  int i = 0;
  while (index_p != NULL) {
    retval = uc_get_rate(&index_p->metric, &ret[i]);
    index_p = index_p->next_p;
    ++i;
  }
  destroy_metrics_list(ml);
  return ret;
}

int uc_get_value_by_name(const char *name, value_t *ret_values) {
  cache_entry_t *ce = NULL;
  int status = 0;

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);

    /* remove missing values from getval */
    if (ce->state == STATE_MISSING) {
      status = -1;
    } else {
      *ret_values = ce->values_raw;
    }
  } else {
    DEBUG("utils_cache: uc_get_value_by_name: No such value: %s", name);
    status = -1;
  }

  pthread_mutex_unlock(&cache_lock);

  return (status);
} /* int uc_get_value_by_name */

int uc_get_value(const metric_t *metric_p, value_t *ret) {
  char *metric_string_p = NULL;
  int status;

  metric_string_p = plugin_format_metric(metric_p);
  if (metric_string_p == NULL) {
    ERROR("utils_cache: uc_get_value: plugin_format_metric failed.");
    return -1;
  }

  status = uc_get_value_by_name(metric_string_p, ret);
  if (status != 0) {
    sfree(metric_string_p);
    return status;
  }

  sfree(metric_string_p);
  return status;
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

int uc_get_state(const metric_t *metric_p) {
  char *metric_string_p = NULL;
  cache_entry_t *ce = NULL;
  int ret = STATE_ERROR;

  metric_string_p = plugin_format_metric(metric_p);
  if (metric_string_p == NULL) {
    ERROR("uc_get_state: plugin_format_metric failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, metric_string_p, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->state;
  }

  pthread_mutex_unlock(&cache_lock);
  sfree(metric_string_p);

  return ret;
} /* int uc_get_state */

int uc_set_state(const metric_t *metric_p, int state) {
  char *metric_string_p = NULL;
  cache_entry_t *ce = NULL;
  int ret = -1;

  metric_string_p = plugin_format_metric(metric_p);
  if (metric_string_p == NULL) {
    ERROR("uc_set_state: plugin_format_metric failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, metric_string_p, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->state;
    ce->state = state;
  }

  pthread_mutex_unlock(&cache_lock);
  sfree(metric_string_p);
  return ret;
} /* int uc_set_state */

int uc_get_history_by_name(const char *name, gauge_t *ret_history,
                           size_t num_steps) {
  cache_entry_t *ce = NULL;
  int status = 0;

  pthread_mutex_lock(&cache_lock);

  status = c_avl_get(cache_tree, name, (void *)&ce);
  if (status != 0) {
    pthread_mutex_unlock(&cache_lock);
    return -ENOENT;
  }
  /* Check if there are enough values available. If not, increase the buffer
   * size. */
  if (ce->history_length < num_steps) {
    gauge_t *tmp;

    tmp = realloc(ce->history, sizeof(*ce->history) * num_steps);
    if (tmp == NULL) {
      pthread_mutex_unlock(&cache_lock);
      return -ENOMEM;
    }

    for (size_t i = ce->history_length; i < num_steps; i++)
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

    dst_index = i;

    memcpy(ret_history + dst_index, ce->history + src_index,
           sizeof(*ret_history));
  }

  pthread_mutex_unlock(&cache_lock);

  return 0;
} /* int uc_get_history_by_name */

int uc_get_history(const metric_t *metric_p, gauge_t *ret_history,
                   size_t num_steps) {
  char *metric_string_p = NULL;

  metric_string_p = plugin_format_metric(metric_p);
  if (metric_string_p == NULL) {
    ERROR("utils_cache: uc_get_history: plugin_format_metric failed.");
    return -1;
  }
  sfree(metric_string_p);
  return uc_get_history_by_name(metric_string_p, ret_history, num_steps);
} /* int uc_get_history */

int uc_get_hits(const metric_t *metric_p) {
  char *metric_string_p = NULL;
  cache_entry_t *ce = NULL;
  int ret = STATE_ERROR;

  metric_string_p = plugin_format_metric(metric_p);
  if (metric_string_p == NULL) {
    ERROR("uc_get_hits: plugin_format_metric failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, metric_string_p, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->hits;
  }

  pthread_mutex_unlock(&cache_lock);
  sfree(metric_string_p);
  return ret;
} /* int uc_get_hits */

int uc_get_hits_vl(const data_set_t *ds, const value_list_t *vl) {
  int retval = 0;
  metrics_list_t *ml = NULL;
  retval = plugin_convert_values_to_metrics(vl, &ml);
  if (retval != 0) {
    WARNING("uc_get_hits_vl: Could not parse value list.");
    return -1;
  }
  /* The assumption here is that all components of the metrics in a value list
   * will have the same number of hits in the cache */
  retval = uc_get_hits(&ml->metric);
  destroy_metrics_list(ml);
  return retval;
}

int uc_set_hits(const metric_t *metric_p, int hits) {
  char *metric_string_p = NULL;
  cache_entry_t *ce = NULL;
  int ret = -1;

  metric_string_p = plugin_format_metric(metric_p);
  if (metric_string_p == NULL) {
    ERROR("uc_set_hits: plugin_format_metric failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, metric_string_p, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->hits;
    ce->hits = hits;
  }

  pthread_mutex_unlock(&cache_lock);

  sfree(metric_string_p);
  return ret;
} /* int uc_set_hits */

int uc_set_hits_vl(const data_set_t *ds, const value_list_t *vl, int hits) {
  int retval = 0;
  metrics_list_t *ml = NULL;
  retval = plugin_convert_values_to_metrics(vl, &ml);
  if (retval != 0) {
    WARNING("uc_set_hits_vl: Could not parse value list.");
    return -1;
  }
  metrics_list_t *index_p = ml;
  int i = 0;
  while (index_p != NULL) {
    retval = uc_set_hits(&index_p->metric, hits);
    index_p = index_p->next_p;
    ++i;
  }
  destroy_metrics_list(ml);
  return retval;
}

int uc_inc_hits(const metric_t *metric_p, int step) {
  char *metric_string_p = NULL;
  cache_entry_t *ce = NULL;
  int ret = -1;

  metric_string_p = plugin_format_metric(metric_p);
  if (metric_string_p == NULL) {
    ERROR("uc_inc_hits: plugin_format_metric failed.");
    return STATE_ERROR;
  }

  pthread_mutex_lock(&cache_lock);

  if (c_avl_get(cache_tree, metric_string_p, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->hits;
    ce->hits = ret + step;
  }

  pthread_mutex_unlock(&cache_lock);

  sfree(metric_string_p);
  return ret;
} /* int uc_inc_hits */

int uc_inc_hits_vl(const data_set_t *ds, const value_list_t *vl, int step) {
  int retval = 0;
  metrics_list_t *ml = NULL;
  retval = plugin_convert_values_to_metrics(vl, &ml);
  if (retval != 0) {
    WARNING("uc_inc_hits_vl: Could not parse value list.");
    return -1;
  }
  metrics_list_t *index_p = ml;
  int i = 0;
  while (index_p != NULL) {
    retval = uc_inc_hits(&index_p->metric, step);
    index_p = index_p->next_p;
    ++i;
  }
  destroy_metrics_list(ml);
  return retval;
}

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

int uc_iterator_get_values(uc_iter_t *iter, value_t *ret_values) {
  if ((iter == NULL) || (iter->entry == NULL) || (ret_values == NULL))
    return -1;

  *ret_values = iter->entry->values_raw;
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
static meta_data_t *uc_get_meta(const metric_t *metric_p) /* {{{ */
{
  char *metric_string_p = NULL;
  cache_entry_t *ce = NULL;
  int status = 0;

  metric_string_p = plugin_format_metric(metric_p);
  if (metric_string_p == NULL) {
    ERROR("utils_cache: uc_get_meta: metric_string_p failed.");
    return NULL;
  }

  pthread_mutex_lock(&cache_lock);

  status = c_avl_get(cache_tree, metric_string_p, (void *)&ce);
  if (status != 0) {
    pthread_mutex_unlock(&cache_lock);
    return NULL;
  }
  assert(ce != NULL);

  if (ce->meta == NULL) {
    pthread_mutex_unlock(&cache_lock);
    return NULL;
  }

  if (ce->meta == NULL)
    ce->meta = meta_data_create();

  sfree(metric_string_p);
  return ce->meta;
} /* }}} meta_data_t *uc_get_meta */

/* Sorry about this preprocessor magic, but it really makes this file much
 * shorter.. */
#define UC_WRAP(wrap_function)                                                 \
  {                                                                            \
    meta_data_t *meta;                                                         \
    int status;                                                                \
    meta = uc_get_meta(metric_p);                                              \
    if (meta == NULL)                                                          \
      return -1;                                                               \
    status = wrap_function(meta, key);                                         \
    pthread_mutex_unlock(&cache_lock);                                         \
    return status;                                                             \
  }

int uc_meta_data_exists(const metric_t *metric_p, const char *key)
    UC_WRAP(meta_data_exists);

int uc_meta_data_delete(const metric_t *metric_p, const char *key)
    UC_WRAP(meta_data_delete);

/* The second argument is called `toc` in the API, but the macro expects
 * `key`. */
int uc_meta_data_toc(const metric_t *metric_p, char ***key)
    UC_WRAP(meta_data_toc);
#undef UC_WRAP

/* We need a new version of this macro because the following functions take
 * two argumetns. gratituous semicolons added for formatting sanity*/
#define UC_WRAP(wrap_function)                                                 \
  {                                                                            \
    meta_data_t *meta;                                                         \
    int status;                                                                \
    meta = uc_get_meta(metric_p);                                              \
    if (meta == NULL)                                                          \
      return -1;                                                               \
    status = wrap_function(meta, key, value);                                  \
    pthread_mutex_unlock(&cache_lock);                                         \
    return status;                                                             \
  }
int uc_meta_data_add_string(const metric_t *metric_p, const char *key,
                            const char *value) UC_WRAP(meta_data_add_string);

int uc_meta_data_add_signed_int(const metric_t *metric_p, const char *key,
                                int64_t value)
    UC_WRAP(meta_data_add_signed_int);

int uc_meta_data_add_unsigned_int(const metric_t *metric_p, const char *key,
                                  uint64_t value)
    UC_WRAP(meta_data_add_unsigned_int);

int uc_meta_data_add_double(const metric_t *metric_p, const char *key,
                            double value) UC_WRAP(meta_data_add_double);
int uc_meta_data_add_boolean(const metric_t *metric_p, const char *key,
                             bool value) UC_WRAP(meta_data_add_boolean);

int uc_meta_data_get_string(const metric_t *metric_p, const char *key,
                            char **value) UC_WRAP(meta_data_get_string);

int uc_meta_data_get_signed_int(const metric_t *metric_p, const char *key,
                                int64_t *value)
    UC_WRAP(meta_data_get_signed_int);

int uc_meta_data_get_unsigned_int(const metric_t *metric_p, const char *key,
                                  uint64_t *value)
    UC_WRAP(meta_data_get_unsigned_int);

int uc_meta_data_get_double(const metric_t *metric_p, const char *key,
                            double *value) UC_WRAP(meta_data_get_double);

int uc_meta_data_get_boolean(const metric_t *metric_p, const char *key,
                             bool *value) UC_WRAP(meta_data_get_boolean);
#undef UC_WRAP

int uc_meta_data_get_signed_int_vl(const value_list_t *vl, const char *key,
                                   int64_t *value) {
  metrics_list_t *ml = NULL;
  int retval = (plugin_convert_values_to_metrics(vl, &ml));
  if (retval != 0) {
    return -1;
  }
  /* All metric values from the values list share the same metadata */
  retval = uc_meta_data_get_signed_int(&ml->metric, key, value);
  destroy_metrics_list(ml);
  return retval;
}

int uc_meta_data_get_unsigned_int_vl(const value_list_t *vl, const char *key,
                                     uint64_t *value) {
  metrics_list_t *ml = NULL;
  int retval = (plugin_convert_values_to_metrics(vl, &ml));
  if (retval != 0) {
    return -1;
  }
  /* All metric values from the values list share the same metadata */
  retval = uc_meta_data_get_unsigned_int(&ml->metric, key, value);
  destroy_metrics_list(ml);
  return retval;
}
int uc_meta_data_add_signed_int_vl(const value_list_t *vl, const char *key,
                                   int64_t value) {
  metrics_list_t *ml = NULL;
  int retval = (plugin_convert_values_to_metrics(vl, &ml));
  if (retval != 0 || ml == NULL || ml->metric.meta == NULL) {
    return -1;
  }

  /* All metric values from the values list share the same metadata */
  retval = meta_data_add_signed_int(ml->metric.meta, key, value);
  destroy_metrics_list(ml);
  return retval;
}
