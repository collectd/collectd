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
#include "utils/strbuf/strbuf.h"
#include "utils_cache.h"

#include <assert.h>

typedef struct cache_entry_s {
  char name[6 * DATA_MAX_NAME_LEN];
  gauge_t values_gauge;
  /* First observed metric time */
  cdtime_t first_time;
  /* First observed metric value */
  value_t first_value;
  /* Last observed metric time (for calculating rates) */
  cdtime_t last_time;
  /* Last observed metric value (for calculating rates) */
  value_t last_value;
  /* Time according to the local clock (for purging old entries) */
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

static int cache_compare(void const *a, void const *b) {
  cache_entry_t const *ea = a;
  cache_entry_t const *eb = b;

#if COLLECT_DEBUG
  assert((a != NULL) && (b != NULL));
#endif
  return strcmp(ea->name, eb->name);
} /* int cache_compare */

static cache_entry_t *cache_alloc(void) {
  cache_entry_t *ce;

  ce = calloc(1, sizeof(*ce));
  if (ce == NULL) {
    ERROR("utils_cache: cache_alloc: calloc failed.");
    return NULL;
  }

  ce->values_gauge = 0;
  ce->last_value = (value_t){.gauge = 0};
  ce->history = NULL;
  ce->history_length = 0;
  ce->meta = NULL;

  return ce;
} /* cache_entry_t *cache_alloc */

static void cache_free(cache_entry_t *ce) {
  if (ce == NULL)
    return;

  sfree(ce->history);

  meta_data_destroy(ce->meta);
  ce->meta = NULL;

  sfree(ce);
} /* void cache_free */

static int uc_insert(metric_t const *m, char const *key) {
  // cache_lock has been locked by uc_update().
  // uc_init has been called by uc_update().

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

  *ce = (cache_entry_t){
      .first_time = m->time,
      .first_value = m->value,
      .last_time = m->time,
      .last_value = m->value,
      .last_update = cdtime(),
      .interval = m->interval,
      .state = STATE_UNKNOWN,
      .values_gauge = NAN,
  };
  sstrncpy(ce->name, key, sizeof(ce->name));

  if (m->family->type == METRIC_TYPE_GAUGE) {
    ce->values_gauge = m->value.gauge;
  }

  if (c_avl_insert(cache_tree, key_copy, ce) != 0) {
    ERROR("uc_insert: c_avl_insert failed.");
    sfree(key_copy);
    cache_free(ce);
    return -1;
  }

  DEBUG("uc_insert: Added %s to the cache.", key);
  return 0;
} /* int uc_insert */

static void uc_init(void) {
  if (cache_tree != NULL) {
    return;
  }
  cache_tree = c_avl_create(cache_compare);
}

int uc_check_timeout(void) {
  struct {
    char *key;
    cdtime_t time;
    cdtime_t interval;
    unsigned long callbacks_mask;
  } *expired = NULL;
  size_t expired_num = 0;

  pthread_mutex_lock(&cache_lock);
  uc_init();

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
    metric_t *m = metric_parse_identity(expired[i].key);
    if (m == NULL) {
      ERROR("uc_check_timeout: metric_parse_identity(\"%s\") failed: %s",
            expired[i].key, STRERRNO);
      continue;
    }

    int status = plugin_dispatch_missing(m->family);
    if (status != 0) {
      ERROR("uc_check_timeout: plugin_dispatch_missing(\"%s\") failed: %s",
            expired[i].key, STRERROR(status));
    }

    if (expired[i].callbacks_mask) {
      plugin_dispatch_cache_event(CE_VALUE_EXPIRED, expired[i].callbacks_mask,
                                  expired[i].key, m);
    }

    metric_family_free(m->family);
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

static int uc_update_rate(metric_t const *m, cache_entry_t *ce) {
  switch (m->family->type) {
  case METRIC_TYPE_GAUGE: {
    ce->values_gauge = m->value.gauge;
    return 0;
  }

  case METRIC_TYPE_COUNTER: {
    // Counter overflows and counter resets are signaled to plugins by resetting
    // "first_time". Since we can't distinguish between an overflow and a
    // reset, we still provide a non-NAN rate value. In case of a counter
    // reset, the rate value will likely be unreasonably huge.
    if (ce->last_value.counter > m->value.counter) {
      // counter reset
      ce->first_time = m->time;
      ce->first_value = m->value;
    }
    counter_t diff = counter_diff(ce->last_value.counter, m->value.counter);
    ce->values_gauge =
        ((double)diff) / CDTIME_T_TO_DOUBLE(m->time - ce->last_time);
    return 0;
  }

  case METRIC_TYPE_COUNTER_FP: {
    // For floating point counters, the logic is slightly different from
    // integer counters. Floating point counters don't have a (meaningful)
    // overflow, and we will always assume a counter reset.
    if (ce->last_value.counter_fp > m->value.counter_fp) {
      // counter reset
      ce->first_time = m->time;
      ce->first_value = m->value;
      ce->values_gauge = NAN;
      return 0;
    }
    gauge_t diff = m->value.counter_fp - ce->last_value.counter_fp;
    ce->values_gauge = diff / CDTIME_T_TO_DOUBLE(m->time - ce->last_time);
    return 0;
  }

  case METRIC_TYPE_UP_DOWN: {
    ce->values_gauge = (gauge_t)m->value.up_down;
    return 0;
  }

  case METRIC_TYPE_UP_DOWN_FP: {
    ce->values_gauge = (gauge_t)m->value.up_down_fp;
    return 0;
  }

  case METRIC_TYPE_UNTYPED:
    break;
  } /* switch (m->family->type) */

  /* This shouldn't happen. */
  ERROR("uc_update: invalid metric type: %d", m->family->type);
  return EINVAL;
}

static int uc_update_metric(metric_t const *m) {
  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("uc_update: metric_identity failed with status %d.", status);
    STRBUF_DESTROY(buf);
    return status;
  }

  pthread_mutex_lock(&cache_lock);
  uc_init();

  cache_entry_t *ce = NULL;
  status = c_avl_get(cache_tree, buf.ptr, (void *)&ce);
  if (status != 0) /* entry does not yet exist */
  {
    int status = uc_insert(m, buf.ptr);
    pthread_mutex_unlock(&cache_lock);

    if (status == 0) {
      plugin_dispatch_cache_event(CE_VALUE_NEW, 0 /* mask */, buf.ptr, m);
    }

    STRBUF_DESTROY(buf);
    return status;
  }

  assert(ce != NULL);
  if (ce->last_time >= m->time) {
    pthread_mutex_unlock(&cache_lock);
    NOTICE("uc_update: Value too old: name = %s; value time = %.3f; "
           "last cache update = %.3f;",
           buf.ptr, CDTIME_T_TO_DOUBLE(m->time),
           CDTIME_T_TO_DOUBLE(ce->last_time));
    STRBUF_DESTROY(buf);
    return -1;
  }

  int err = uc_update_rate(m, ce);
  if (err) {
    pthread_mutex_unlock(&cache_lock);
    STRBUF_DESTROY(buf);
    return err;
  }

  DEBUG("uc_update: %s = %f", buf.ptr, ce->values_gauge);

  /* Update the history if it exists. TODO: Does history need to be an array? */
  if (ce->history != NULL) {
    assert(ce->history_index < ce->history_length);
    ce->history[ce->history_index] = ce->values_gauge;

    assert(ce->history_length > 0);
    ce->history_index = (ce->history_index + 1) % ce->history_length;
  }

  ce->last_value = m->value;
  ce->last_time = m->time;
  ce->last_update = cdtime();
  ce->interval = m->interval;

  /* Check if cache entry has registered callbacks */
  unsigned long callbacks_mask = ce->callbacks_mask;

  pthread_mutex_unlock(&cache_lock);

  if (callbacks_mask) {
    plugin_dispatch_cache_event(CE_VALUE_UPDATE, callbacks_mask, buf.ptr, m);
  }

  STRBUF_DESTROY(buf);
  return 0;
} /* int uc_update_metric */

int uc_update(metric_family_t const *fam) {
  int ret = 0;
  for (size_t i = 0; i < fam->metric.num; i++) {
    int status = uc_update_metric(fam->metric.ptr + i);
    if (status != 0) {
      ERROR("uc_update: uc_update_metric failed: %s", STRERROR(status));
      ret = ret || status;
    }
  }

  return ret;
}

int uc_set_callbacks_mask(const char *name, unsigned long mask) {
  pthread_mutex_lock(&cache_lock);
  uc_init();

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
  uc_init();

  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);

    /* remove missing values from getval */
    if (ce->state == STATE_MISSING) {
      DEBUG("utils_cache: uc_get_rate_by_name: requested metric \"%s\" is in "
            "state \"missing\".",
            name);
      status = EAGAIN;
    } else {
      *ret_values = ce->values_gauge;
    }
  } else {
    DEBUG("utils_cache: uc_get_rate_by_name: No such value: %s", name);
    status = ENOENT;
  }

  pthread_mutex_unlock(&cache_lock);

  return status;
} /* gauge_t *uc_get_rate_by_name */

int uc_get_rate(metric_t const *m, gauge_t *ret) {
  if (m == NULL || m->family == NULL || ret == NULL) {
    return EINVAL;
  }
  switch (m->family->type) {
  case METRIC_TYPE_GAUGE:
    *ret = m->value.gauge;
    return 0;
  case METRIC_TYPE_COUNTER:
  case METRIC_TYPE_COUNTER_FP:
    break;
  case METRIC_TYPE_UP_DOWN:
    *ret = (gauge_t)m->value.up_down;
    return 0;
  case METRIC_TYPE_UP_DOWN_FP:
    *ret = (gauge_t)m->value.up_down_fp;
    return 0;
  case METRIC_TYPE_UNTYPED:
    return EINVAL;
  }

  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("uc_get_rate: metric_identity failed with status %d.", status);
    STRBUF_DESTROY(buf);
    return status;
  }

  status = uc_get_rate_by_name(buf.ptr, ret);
  STRBUF_DESTROY(buf);
  return status;
} /* gauge_t *uc_get_rate */

gauge_t *uc_get_rate_vl(const data_set_t *ds, const value_list_t *vl) {
  gauge_t *ret = calloc(ds->ds_num, sizeof(*ret));
  if (ret == NULL) {
    ERROR("uc_get_rate_vl: failed to allocate memory.");
    return NULL;
  }

  for (size_t i = 0; i < ds->ds_num; i++) {
    metric_family_t *fam = plugin_value_list_to_metric_family(vl, ds, i);
    if (fam == NULL) {
      int status = errno;
      free(ret);
      errno = status;
      return NULL;
    }

    int status = uc_get_rate(fam->metric.ptr, ret + i);
    if (status != 0) {
      free(ret);
      errno = status;
      return NULL;
    }

    metric_family_free(fam);
  }

  return ret;
}

int uc_get_value_by_name(const char *name, value_t *ret_values) {
  pthread_mutex_lock(&cache_lock);
  uc_init();

  cache_entry_t *ce = NULL;
  int status = 0;
  if (c_avl_get(cache_tree, name, (void *)&ce) == 0) {
    assert(ce != NULL);

    /* remove missing values from getval */
    if (ce->state == STATE_MISSING) {
      status = EAGAIN;
    } else {
      *ret_values = ce->last_value;
    }
  } else {
    DEBUG("utils_cache: uc_get_value_by_name: No such value: %s", name);
    status = ENOENT;
  }

  pthread_mutex_unlock(&cache_lock);
  return status;
} /* int uc_get_value_by_name */

int uc_get_value(metric_t const *m, value_t *ret) {
  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("uc_get_value: metric_identity failed with status %d.", status);
    STRBUF_DESTROY(buf);
    return status;
  }

  status = uc_get_value_by_name(buf.ptr, ret);
  STRBUF_DESTROY(buf);
  return status;
} /* value_t *uc_get_value */

static uc_first_metric_result_t uc_first_metric_by_name(const char *name) {
  uc_init();

  cache_entry_t *ce = NULL;
  int err = c_avl_get(cache_tree, name, (void *)&ce);
  if (err == ENOENT) {
    DEBUG("utils_cache: uc_first_metric: No such value: \"%s\"", name);
    return (uc_first_metric_result_t){.err = err};
  }
  if (err != 0) {
    ERROR("utils_cache: uc_first_metric: c_avl_get(\"%s\") failed: %s", name,
          STRERROR(err));
    return (uc_first_metric_result_t){.err = err};
  }

  return (uc_first_metric_result_t){
      .time = ce->first_time,
      .value = ce->first_value,
  };
}

uc_first_metric_result_t uc_first_metric(metric_t const *m) {
  strbuf_t id = STRBUF_CREATE;
  int err = metric_identity(&id, m);
  if (err != 0) {
    ERROR("uc_first_metric: metric_identity failed with status %d.", err);
    STRBUF_DESTROY(id);
    return (uc_first_metric_result_t){.err = err};
  }

  pthread_mutex_lock(&cache_lock);
  uc_first_metric_result_t res = uc_first_metric_by_name(id.ptr);
  pthread_mutex_unlock(&cache_lock);

  STRBUF_DESTROY(id);
  return res;
}

size_t uc_get_size(void) {
  pthread_mutex_lock(&cache_lock);
  uc_init();

  size_t size_arrays = (size_t)c_avl_size(cache_tree);

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
  uc_init();

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

int uc_get_state(metric_t const *m) {
  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("uc_get_state: metric_identity failed with status %d.", status);
    STRBUF_DESTROY(buf);
    return status;
  }

  pthread_mutex_lock(&cache_lock);
  uc_init();

  cache_entry_t *ce = NULL;
  int ret = STATE_ERROR;
  if (c_avl_get(cache_tree, buf.ptr, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->state;
  }

  pthread_mutex_unlock(&cache_lock);
  STRBUF_DESTROY(buf);
  return ret;
} /* int uc_get_state */

int uc_set_state(metric_t const *m, int state) {
  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("uc_set_state: metric_identity failed with status %d.", status);
    STRBUF_DESTROY(buf);
    return status;
  }

  pthread_mutex_lock(&cache_lock);
  uc_init();

  cache_entry_t *ce = NULL;
  int ret = -1;
  if (c_avl_get(cache_tree, buf.ptr, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->state;
    ce->state = state;
  }

  pthread_mutex_unlock(&cache_lock);
  STRBUF_DESTROY(buf);
  return ret;
} /* int uc_set_state */

int uc_get_history_by_name(const char *name, gauge_t *ret_history,
                           size_t num_steps) {
  cache_entry_t *ce = NULL;
  int status = 0;

  pthread_mutex_lock(&cache_lock);
  uc_init();

  status = c_avl_get(cache_tree, name, (void *)&ce);
  if (status != 0) {
    pthread_mutex_unlock(&cache_lock);
    return ENOENT;
  }
  /* Check if there are enough values available. If not, increase the buffer
   * size. */
  if (ce->history_length < num_steps) {
    gauge_t *tmp;

    tmp = realloc(ce->history, sizeof(*ce->history) * num_steps);
    if (tmp == NULL) {
      pthread_mutex_unlock(&cache_lock);
      return ENOMEM;
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

int uc_get_hits(metric_t const *m) {
  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("uc_update: metric_identity failed with status %d.", status);
    STRBUF_DESTROY(buf);
    return status;
  }

  pthread_mutex_lock(&cache_lock);
  uc_init();

  cache_entry_t *ce = NULL;
  int ret = STATE_ERROR;
  if (c_avl_get(cache_tree, buf.ptr, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->hits;
  }

  pthread_mutex_unlock(&cache_lock);
  STRBUF_DESTROY(buf);
  return ret;
} /* int uc_get_hits */

int uc_set_hits(metric_t const *m, int hits) {
  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("uc_set_hits: metric_identity failed with status %d.", status);
    STRBUF_DESTROY(buf);
    return status;
  }

  pthread_mutex_lock(&cache_lock);
  uc_init();

  cache_entry_t *ce = NULL;
  int ret = -1;
  if (c_avl_get(cache_tree, buf.ptr, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->hits;
    ce->hits = hits;
  }

  pthread_mutex_unlock(&cache_lock);
  STRBUF_DESTROY(buf);
  return ret;
} /* int uc_set_hits */

int uc_inc_hits(metric_t const *m, int step) {
  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("uc_inc_hits: metric_identity failed with status %d.", status);
    STRBUF_DESTROY(buf);
    return status;
  }

  pthread_mutex_lock(&cache_lock);
  uc_init();

  cache_entry_t *ce = NULL;
  int ret = -1;
  if (c_avl_get(cache_tree, buf.ptr, (void *)&ce) == 0) {
    assert(ce != NULL);
    ret = ce->hits;
    ce->hits += step;
  }

  pthread_mutex_unlock(&cache_lock);
  STRBUF_DESTROY(buf);
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
  uc_init();

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
    if (iter->entry->state == STATE_MISSING) {
      continue;
    }

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

  *ret_values = iter->entry->last_value;
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
/* XXX: Must hold cache_lock when calling this function! */
static meta_data_t *uc_get_meta(metric_t const *m) {
  uc_init();

  strbuf_t buf = STRBUF_CREATE;
  int status = metric_identity(&buf, m);
  if (status != 0) {
    ERROR("uc_update: metric_identity failed with status %d.", status);
    STRBUF_DESTROY(buf);
    errno = status;
    return NULL;
  }

  cache_entry_t *ce = NULL;
  status = c_avl_get(cache_tree, buf.ptr, (void *)&ce);
  STRBUF_DESTROY(buf);
  if (status != 0) {
    errno = status;
    return NULL;
  }
  assert(ce != NULL);

  if (ce->meta == NULL) {
    ce->meta = meta_data_create();
  }

  return ce->meta;
}

/* Sorry about this preprocessor magic, but it really makes this file much
 * shorter.. */
#define UC_WRAP(wrap_function)                                                 \
  do {                                                                         \
    pthread_mutex_lock(&cache_lock);                                           \
    errno = 0;                                                                 \
    meta_data_t *meta = uc_get_meta(m);                                        \
    if ((meta == NULL) && (errno != 0)) {                                      \
      pthread_mutex_unlock(&cache_lock);                                       \
      return errno;                                                            \
    }                                                                          \
    int ret = wrap_function(meta, key);                                        \
    pthread_mutex_unlock(&cache_lock);                                         \
    return ret;                                                                \
  } while (0)

int uc_meta_data_exists(metric_t const *m, const char *key) {
  UC_WRAP(meta_data_exists);
}
int uc_meta_data_delete(metric_t const *m, const char *key) {
  UC_WRAP(meta_data_delete);
}
/* The second argument is called `toc` in the API, but the macro expects
 * `key`. */
int uc_meta_data_toc(metric_t const *m, char ***key) { UC_WRAP(meta_data_toc); }
#undef UC_WRAP

/* We need a new version of this macro because the following functions take
 * two arguments. */
#define UC_WRAP(wrap_function)                                                 \
  do {                                                                         \
    pthread_mutex_lock(&cache_lock);                                           \
    errno = 0;                                                                 \
    meta_data_t *meta = uc_get_meta(m);                                        \
    if ((meta == NULL) && (errno != 0)) {                                      \
      pthread_mutex_unlock(&cache_lock);                                       \
      return errno;                                                            \
    }                                                                          \
    int ret = wrap_function(meta, key, value);                                 \
    pthread_mutex_unlock(&cache_lock);                                         \
    return ret;                                                                \
  } while (0)

int uc_meta_data_add_string(metric_t const *m, const char *key,
                            const char *value) {
  UC_WRAP(meta_data_add_string);
}
int uc_meta_data_add_signed_int(metric_t const *m, const char *key,
                                int64_t value) {
  UC_WRAP(meta_data_add_signed_int);
}
int uc_meta_data_add_unsigned_int(metric_t const *m, const char *key,
                                  uint64_t value) {
  UC_WRAP(meta_data_add_unsigned_int);
}
int uc_meta_data_add_double(metric_t const *m, const char *key, double value) {
  UC_WRAP(meta_data_add_double);
}
int uc_meta_data_add_boolean(metric_t const *m, const char *key, bool value) {
  UC_WRAP(meta_data_add_boolean);
}
int uc_meta_data_get_string(metric_t const *m, const char *key, char **value) {
  UC_WRAP(meta_data_get_string);
}
int uc_meta_data_get_signed_int(metric_t const *m, const char *key,
                                int64_t *value) {
  UC_WRAP(meta_data_get_signed_int);
}
int uc_meta_data_get_unsigned_int(metric_t const *m, const char *key,
                                  uint64_t *value) {
  UC_WRAP(meta_data_get_unsigned_int);
}
int uc_meta_data_get_double(metric_t const *m, const char *key, double *value) {
  UC_WRAP(meta_data_get_double);
}
int uc_meta_data_get_boolean(metric_t const *m, const char *key, bool *value) {
  UC_WRAP(meta_data_get_boolean);
}
#undef UC_WRAP
