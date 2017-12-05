/**
 * collectd - src/rrdtool.c
 * Copyright (C) 2006-2013  Florian octo Forster
 * Copyright (C) 2008-2008  Sebastian Harl
 * Copyright (C) 2009       Mariusz Gronczewski
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
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 *   Mariusz Gronczewski <xani666 at gmail.com>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_avltree.h"
#include "utils_random.h"
#include "utils_rrdcreate.h"

#include <rrd.h>

/*
 * Private types
 */
typedef struct rrd_cache_s {
  int values_num;
  char **values;
  cdtime_t first_value;
  cdtime_t last_value;
  int64_t random_variation;
  enum { FLAG_NONE = 0x00, FLAG_QUEUED = 0x01, FLAG_FLUSHQ = 0x02 } flags;
} rrd_cache_t;

enum rrd_queue_dir_e { QUEUE_INSERT_FRONT, QUEUE_INSERT_BACK };
typedef enum rrd_queue_dir_e rrd_queue_dir_t;

struct rrd_queue_s {
  char *filename;
  struct rrd_queue_s *next;
};
typedef struct rrd_queue_s rrd_queue_t;

/*
 * Private variables
 */
static const char *config_keys[] = {
    "CacheTimeout", "CacheFlush",      "CreateFilesAsync", "DataDir",
    "StepSize",     "HeartBeat",       "RRARows",          "RRATimespan",
    "XFF",          "WritesPerSecond", "RandomTimeout"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);

/* If datadir is zero, the daemon's basedir is used. If stepsize or heartbeat
 * is zero a default, depending on the `interval' member of the value list is
 * being used. */
static char *datadir = NULL;
static double write_rate = 0.0;
static rrdcreate_config_t rrdcreate_config = {
    /* stepsize = */ 0,
    /* heartbeat = */ 0,
    /* rrarows = */ 1200,
    /* xff = */ 0.1,

    /* timespans = */ NULL,
    /* timespans_num = */ 0,

    /* consolidation_functions = */ NULL,
    /* consolidation_functions_num = */ 0,

    /* async = */ 0};

/* XXX: If you need to lock both, cache_lock and queue_lock, at the same time,
 * ALWAYS lock `cache_lock' first! */
static cdtime_t cache_timeout = 0;
static cdtime_t cache_flush_timeout = 0;
static cdtime_t random_timeout = 0;
static cdtime_t cache_flush_last;
static c_avl_tree_t *cache = NULL;
static pthread_mutex_t cache_lock = PTHREAD_MUTEX_INITIALIZER;

static rrd_queue_t *queue_head = NULL;
static rrd_queue_t *queue_tail = NULL;
static rrd_queue_t *flushq_head = NULL;
static rrd_queue_t *flushq_tail = NULL;
static pthread_t queue_thread;
static int queue_thread_running = 1;
static pthread_mutex_t queue_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t queue_cond = PTHREAD_COND_INITIALIZER;

#if !HAVE_THREADSAFE_LIBRRD
static pthread_mutex_t librrd_lock = PTHREAD_MUTEX_INITIALIZER;
#endif

static int do_shutdown = 0;

#if HAVE_THREADSAFE_LIBRRD
static int srrd_update(char *filename, char *template, int argc,
                       const char **argv) {
  optind = 0; /* bug in librrd? */
  rrd_clear_error();

  int status = rrd_update_r(filename, template, argc, (void *)argv);
  if (status != 0) {
    WARNING("rrdtool plugin: rrd_update_r (%s) failed: %s", filename,
            rrd_get_error());
  }

  return status;
} /* int srrd_update */
/* #endif HAVE_THREADSAFE_LIBRRD */

#else  /* !HAVE_THREADSAFE_LIBRRD */
static int srrd_update(char *filename, char *template, int argc,
                       const char **argv) {
  int status;

  int new_argc;
  char **new_argv;

  assert(template == NULL);

  new_argc = 2 + argc;
  new_argv = malloc((new_argc + 1) * sizeof(*new_argv));
  if (new_argv == NULL) {
    ERROR("rrdtool plugin: malloc failed.");
    return -1;
  }

  new_argv[0] = "update";
  new_argv[1] = filename;

  memcpy(new_argv + 2, argv, argc * sizeof(char *));
  new_argv[new_argc] = NULL;

  pthread_mutex_lock(&librrd_lock);
  optind = 0; /* bug in librrd? */
  rrd_clear_error();

  status = rrd_update(new_argc, new_argv);
  pthread_mutex_unlock(&librrd_lock);

  if (status != 0) {
    WARNING("rrdtool plugin: rrd_update_r failed: %s: %s", filename,
            rrd_get_error());
  }

  sfree(new_argv);

  return status;
} /* int srrd_update */
#endif /* !HAVE_THREADSAFE_LIBRRD */

static int value_list_to_string_multiple(char *buffer, int buffer_len,
                                         const data_set_t *ds,
                                         const value_list_t *vl) {
  int offset;
  int status;
  time_t tt;

  memset(buffer, '\0', buffer_len);

  tt = CDTIME_T_TO_TIME_T(vl->time);
  status = snprintf(buffer, buffer_len, "%u", (unsigned int)tt);
  if ((status < 1) || (status >= buffer_len))
    return -1;
  offset = status;

  for (size_t i = 0; i < ds->ds_num; i++) {
    if ((ds->ds[i].type != DS_TYPE_COUNTER) &&
        (ds->ds[i].type != DS_TYPE_GAUGE) &&
        (ds->ds[i].type != DS_TYPE_DERIVE) &&
        (ds->ds[i].type != DS_TYPE_ABSOLUTE))
      return -1;

    if (ds->ds[i].type == DS_TYPE_COUNTER)
      status = snprintf(buffer + offset, buffer_len - offset, ":%llu",
                        vl->values[i].counter);
    else if (ds->ds[i].type == DS_TYPE_GAUGE)
      status = snprintf(buffer + offset, buffer_len - offset, ":" GAUGE_FORMAT,
                        vl->values[i].gauge);
    else if (ds->ds[i].type == DS_TYPE_DERIVE)
      status = snprintf(buffer + offset, buffer_len - offset, ":%" PRIi64,
                        vl->values[i].derive);
    else /*if (ds->ds[i].type == DS_TYPE_ABSOLUTE) */
      status = snprintf(buffer + offset, buffer_len - offset, ":%" PRIu64,
                        vl->values[i].absolute);

    if ((status < 1) || (status >= (buffer_len - offset)))
      return -1;

    offset += status;
  } /* for ds->ds_num */

  return 0;
} /* int value_list_to_string_multiple */

static int value_list_to_string(char *buffer, int buffer_len,
                                const data_set_t *ds, const value_list_t *vl) {
  int status;
  time_t tt;

  if (ds->ds_num != 1)
    return value_list_to_string_multiple(buffer, buffer_len, ds, vl);

  tt = CDTIME_T_TO_TIME_T(vl->time);
  switch (ds->ds[0].type) {
  case DS_TYPE_DERIVE:
    status = snprintf(buffer, buffer_len, "%u:%" PRIi64, (unsigned)tt,
                      vl->values[0].derive);
    break;
  case DS_TYPE_GAUGE:
    status = snprintf(buffer, buffer_len, "%u:" GAUGE_FORMAT, (unsigned)tt,
                      vl->values[0].gauge);
    break;
  case DS_TYPE_COUNTER:
    status = snprintf(buffer, buffer_len, "%u:%llu", (unsigned)tt,
                      vl->values[0].counter);
    break;
  case DS_TYPE_ABSOLUTE:
    status = snprintf(buffer, buffer_len, "%u:%" PRIu64, (unsigned)tt,
                      vl->values[0].absolute);
    break;
  default:
    return EINVAL;
  }

  if ((status < 1) || (status >= buffer_len))
    return ENOMEM;

  return 0;
} /* int value_list_to_string */

static int value_list_to_filename(char *buffer, size_t buffer_size,
                                  value_list_t const *vl) {
  char const suffix[] = ".rrd";
  int status;
  size_t len;

  if (datadir != NULL) {
    size_t datadir_len = strlen(datadir) + 1;

    if (datadir_len >= buffer_size)
      return ENOMEM;

    sstrncpy(buffer, datadir, buffer_size);
    buffer[datadir_len - 1] = '/';
    buffer[datadir_len] = 0;

    buffer += datadir_len;
    buffer_size -= datadir_len;
  }

  status = FORMAT_VL(buffer, buffer_size, vl);
  if (status != 0)
    return status;

  len = strlen(buffer);
  assert(len < buffer_size);
  buffer += len;
  buffer_size -= len;

  if (buffer_size <= sizeof(suffix))
    return ENOMEM;

  memcpy(buffer, suffix, sizeof(suffix));
  return 0;
} /* int value_list_to_filename */

static void *rrd_queue_thread(void __attribute__((unused)) * data) {
  struct timeval tv_next_update;
  struct timeval tv_now;

  gettimeofday(&tv_next_update, /* timezone = */ NULL);

  while (42) {
    rrd_queue_t *queue_entry;
    rrd_cache_t *cache_entry;
    char **values;
    int values_num;
    int status;

    values = NULL;
    values_num = 0;

    pthread_mutex_lock(&queue_lock);
    /* Wait for values to arrive */
    while (42) {
      struct timespec ts_wait;

      while ((flushq_head == NULL) && (queue_head == NULL) &&
             (do_shutdown == 0))
        pthread_cond_wait(&queue_cond, &queue_lock);

      if ((flushq_head == NULL) && (queue_head == NULL))
        break;

      /* Don't delay if there's something to flush */
      if (flushq_head != NULL)
        break;

      /* Don't delay if we're shutting down */
      if (do_shutdown != 0)
        break;

      /* Don't delay if no delay was configured. */
      if (write_rate <= 0.0)
        break;

      gettimeofday(&tv_now, /* timezone = */ NULL);
      status = timeval_cmp(tv_next_update, tv_now, NULL);
      /* We're good to go */
      if (status <= 0)
        break;

      /* We're supposed to wait a bit with this update, so we'll
       * wait for the next addition to the queue or to the end of
       * the wait period - whichever comes first. */
      ts_wait.tv_sec = tv_next_update.tv_sec;
      ts_wait.tv_nsec = 1000 * tv_next_update.tv_usec;

      status = pthread_cond_timedwait(&queue_cond, &queue_lock, &ts_wait);
      if (status == ETIMEDOUT)
        break;
    } /* while (42) */

    /* XXX: If you need to lock both, cache_lock and queue_lock, at
     * the same time, ALWAYS lock `cache_lock' first! */

    /* We're in the shutdown phase */
    if ((flushq_head == NULL) && (queue_head == NULL)) {
      pthread_mutex_unlock(&queue_lock);
      break;
    }

    if (flushq_head != NULL) {
      /* Dequeue the first flush entry */
      queue_entry = flushq_head;
      if (flushq_head == flushq_tail)
        flushq_head = flushq_tail = NULL;
      else
        flushq_head = flushq_head->next;
    } else /* if (queue_head != NULL) */
    {
      /* Dequeue the first regular entry */
      queue_entry = queue_head;
      if (queue_head == queue_tail)
        queue_head = queue_tail = NULL;
      else
        queue_head = queue_head->next;
    }

    /* Unlock the queue again */
    pthread_mutex_unlock(&queue_lock);

    /* We now need the cache lock so the entry isn't updated while
     * we make a copy of its values */
    pthread_mutex_lock(&cache_lock);

    status = c_avl_get(cache, queue_entry->filename, (void *)&cache_entry);

    if (status == 0) {
      values = cache_entry->values;
      values_num = cache_entry->values_num;

      cache_entry->values = NULL;
      cache_entry->values_num = 0;
      cache_entry->flags = FLAG_NONE;
    }

    pthread_mutex_unlock(&cache_lock);

    if (status != 0) {
      sfree(queue_entry->filename);
      sfree(queue_entry);
      continue;
    }

    /* Update `tv_next_update' */
    if (write_rate > 0.0) {
      gettimeofday(&tv_now, /* timezone = */ NULL);
      tv_next_update.tv_sec = tv_now.tv_sec;
      tv_next_update.tv_usec =
          tv_now.tv_usec + ((suseconds_t)(1000000 * write_rate));
      while (tv_next_update.tv_usec > 1000000) {
        tv_next_update.tv_sec++;
        tv_next_update.tv_usec -= 1000000;
      }
    }

    /* Write the values to the RRD-file */
    srrd_update(queue_entry->filename, NULL, values_num, (const char **)values);
    DEBUG("rrdtool plugin: queue thread: Wrote %i value%s to %s", values_num,
          (values_num == 1) ? "" : "s", queue_entry->filename);

    for (int i = 0; i < values_num; i++) {
      sfree(values[i]);
    }
    sfree(values);
    sfree(queue_entry->filename);
    sfree(queue_entry);
  } /* while (42) */

  pthread_exit((void *)0);
  return (void *)0;
} /* void *rrd_queue_thread */

static int rrd_queue_enqueue(const char *filename, rrd_queue_t **head,
                             rrd_queue_t **tail) {
  rrd_queue_t *queue_entry;

  queue_entry = malloc(sizeof(*queue_entry));
  if (queue_entry == NULL)
    return -1;

  queue_entry->filename = strdup(filename);
  if (queue_entry->filename == NULL) {
    free(queue_entry);
    return -1;
  }

  queue_entry->next = NULL;

  pthread_mutex_lock(&queue_lock);

  if (*tail == NULL)
    *head = queue_entry;
  else
    (*tail)->next = queue_entry;
  *tail = queue_entry;

  pthread_cond_signal(&queue_cond);
  pthread_mutex_unlock(&queue_lock);

  return 0;
} /* int rrd_queue_enqueue */

static int rrd_queue_dequeue(const char *filename, rrd_queue_t **head,
                             rrd_queue_t **tail) {
  rrd_queue_t *this;
  rrd_queue_t *prev;

  pthread_mutex_lock(&queue_lock);

  prev = NULL;
  this = *head;

  while (this != NULL) {
    if (strcmp(this->filename, filename) == 0)
      break;

    prev = this;
    this = this->next;
  }

  if (this == NULL) {
    pthread_mutex_unlock(&queue_lock);
    return -1;
  }

  if (prev == NULL)
    *head = this->next;
  else
    prev->next = this->next;

  if (this->next == NULL)
    *tail = prev;

  pthread_mutex_unlock(&queue_lock);

  sfree(this->filename);
  sfree(this);

  return 0;
} /* int rrd_queue_dequeue */

/* XXX: You must hold "cache_lock" when calling this function! */
static void rrd_cache_flush(cdtime_t timeout) {
  rrd_cache_t *rc;
  cdtime_t now;

  char **keys = NULL;
  int keys_num = 0;

  char *key;
  c_avl_iterator_t *iter;

  DEBUG("rrdtool plugin: Flushing cache, timeout = %.3f",
        CDTIME_T_TO_DOUBLE(timeout));

  now = cdtime();

  /* Build a list of entries to be flushed */
  iter = c_avl_get_iterator(cache);
  while (c_avl_iterator_next(iter, (void *)&key, (void *)&rc) == 0) {
    if (rc->flags != FLAG_NONE)
      continue;
    /* timeout == 0  =>  flush everything */
    else if ((timeout != 0) && ((now - rc->first_value) < timeout))
      continue;
    else if (rc->values_num > 0) {
      int status;

      status = rrd_queue_enqueue(key, &queue_head, &queue_tail);
      if (status == 0)
        rc->flags = FLAG_QUEUED;
    } else /* ancient and no values -> waste of memory */
    {
      char **tmp = realloc(keys, (keys_num + 1) * sizeof(char *));
      if (tmp == NULL) {
        ERROR("rrdtool plugin: realloc failed: %s", STRERRNO);
        c_avl_iterator_destroy(iter);
        sfree(keys);
        return;
      }
      keys = tmp;
      keys[keys_num] = key;
      keys_num++;
    }
  } /* while (c_avl_iterator_next) */
  c_avl_iterator_destroy(iter);

  for (int i = 0; i < keys_num; i++) {
    if (c_avl_remove(cache, keys[i], (void *)&key, (void *)&rc) != 0) {
      DEBUG("rrdtool plugin: c_avl_remove (%s) failed.", keys[i]);
      continue;
    }

    assert(rc->values == NULL);
    assert(rc->values_num == 0);

    sfree(rc);
    sfree(key);
    keys[i] = NULL;
  } /* for (i = 0..keys_num) */

  sfree(keys);

  cache_flush_last = now;
} /* void rrd_cache_flush */

static int rrd_cache_flush_identifier(cdtime_t timeout,
                                      const char *identifier) {
  rrd_cache_t *rc;
  cdtime_t now;
  int status;
  char key[2048];

  if (identifier == NULL) {
    rrd_cache_flush(timeout);
    return 0;
  }

  now = cdtime();

  if (datadir == NULL)
    snprintf(key, sizeof(key), "%s.rrd", identifier);
  else
    snprintf(key, sizeof(key), "%s/%s.rrd", datadir, identifier);
  key[sizeof(key) - 1] = 0;

  status = c_avl_get(cache, key, (void *)&rc);
  if (status != 0) {
    INFO("rrdtool plugin: rrd_cache_flush_identifier: "
         "c_avl_get (%s) failed. Does that file really exist?",
         key);
    return status;
  }

  if (rc->flags == FLAG_FLUSHQ) {
    status = 0;
  } else if (rc->flags == FLAG_QUEUED) {
    rrd_queue_dequeue(key, &queue_head, &queue_tail);
    status = rrd_queue_enqueue(key, &flushq_head, &flushq_tail);
    if (status == 0)
      rc->flags = FLAG_FLUSHQ;
  } else if ((now - rc->first_value) < timeout) {
    status = 0;
  } else if (rc->values_num > 0) {
    status = rrd_queue_enqueue(key, &flushq_head, &flushq_tail);
    if (status == 0)
      rc->flags = FLAG_FLUSHQ;
  }

  return status;
} /* int rrd_cache_flush_identifier */

static int64_t rrd_get_random_variation(void) {
  if (random_timeout == 0)
    return 0;

  return (int64_t)cdrand_range(-random_timeout, random_timeout);
} /* int64_t rrd_get_random_variation */

static int rrd_cache_insert(const char *filename, const char *value,
                            cdtime_t value_time) {
  rrd_cache_t *rc = NULL;
  int new_rc = 0;
  char **values_new;

  pthread_mutex_lock(&cache_lock);

  /* This shouldn't happen, but it did happen at least once, so we'll be
   * careful. */
  if (cache == NULL) {
    pthread_mutex_unlock(&cache_lock);
    WARNING("rrdtool plugin: cache == NULL.");
    return -1;
  }

  int status = c_avl_get(cache, filename, (void *)&rc);
  if ((status != 0) || (rc == NULL)) {
    rc = malloc(sizeof(*rc));
    if (rc == NULL) {
      pthread_mutex_unlock(&cache_lock);
      return -1;
    }
    rc->values_num = 0;
    rc->values = NULL;
    rc->first_value = 0;
    rc->last_value = 0;
    rc->random_variation = rrd_get_random_variation();
    rc->flags = FLAG_NONE;
    new_rc = 1;
  }

  assert(value_time > 0); /* plugin_dispatch() ensures this. */
  if (rc->last_value >= value_time) {
    pthread_mutex_unlock(&cache_lock);
    DEBUG("rrdtool plugin: (rc->last_value = %" PRIu64 ") "
          ">= (value_time = %" PRIu64 ")",
          rc->last_value, value_time);
    return -1;
  }

  values_new =
      realloc((void *)rc->values, (rc->values_num + 1) * sizeof(char *));
  if (values_new == NULL) {
    void *cache_key = NULL;

    c_avl_remove(cache, filename, &cache_key, NULL);
    pthread_mutex_unlock(&cache_lock);

    ERROR("rrdtool plugin: realloc failed: %s", STRERRNO);

    sfree(cache_key);
    sfree(rc->values);
    sfree(rc);
    return -1;
  }
  rc->values = values_new;

  rc->values[rc->values_num] = strdup(value);
  if (rc->values[rc->values_num] != NULL)
    rc->values_num++;

  if (rc->values_num == 1)
    rc->first_value = value_time;
  rc->last_value = value_time;

  /* Insert if this is the first value */
  if (new_rc == 1) {
    void *cache_key = strdup(filename);

    if (cache_key == NULL) {
      pthread_mutex_unlock(&cache_lock);

      ERROR("rrdtool plugin: strdup failed: %s", STRERRNO);

      sfree(rc->values[0]);
      sfree(rc->values);
      sfree(rc);
      return -1;
    }

    c_avl_insert(cache, cache_key, rc);
  }

  DEBUG("rrdtool plugin: rrd_cache_insert: file = %s; "
        "values_num = %i; age = %.3f;",
        filename, rc->values_num,
        CDTIME_T_TO_DOUBLE(rc->last_value - rc->first_value));

  if ((rc->last_value - rc->first_value) >=
      (cache_timeout + rc->random_variation)) {
    /* XXX: If you need to lock both, cache_lock and queue_lock, at
     * the same time, ALWAYS lock `cache_lock' first! */
    if (rc->flags == FLAG_NONE) {
      int status;

      status = rrd_queue_enqueue(filename, &queue_head, &queue_tail);
      if (status == 0)
        rc->flags = FLAG_QUEUED;

      rc->random_variation = rrd_get_random_variation();
    } else {
      DEBUG("rrdtool plugin: `%s' is already queued.", filename);
    }
  }

  if ((cache_timeout > 0) &&
      ((cdtime() - cache_flush_last) > cache_flush_timeout))
    rrd_cache_flush(cache_timeout + random_timeout);

  pthread_mutex_unlock(&cache_lock);

  return 0;
} /* int rrd_cache_insert */

static int rrd_cache_destroy(void) /* {{{ */
{
  void *key = NULL;
  void *value = NULL;

  int non_empty = 0;

  pthread_mutex_lock(&cache_lock);

  if (cache == NULL) {
    pthread_mutex_unlock(&cache_lock);
    return 0;
  }

  while (c_avl_pick(cache, &key, &value) == 0) {
    rrd_cache_t *rc;

    sfree(key);
    key = NULL;

    rc = value;
    value = NULL;

    if (rc->values_num > 0)
      non_empty++;

    for (int i = 0; i < rc->values_num; i++)
      sfree(rc->values[i]);
    sfree(rc->values);
    sfree(rc);
  }

  c_avl_destroy(cache);
  cache = NULL;

  if (non_empty > 0) {
    INFO("rrdtool plugin: %i cache %s had values when destroying the cache.",
         non_empty, (non_empty == 1) ? "entry" : "entries");
  } else {
    DEBUG("rrdtool plugin: No values have been lost "
          "when destroying the cache.");
  }

  pthread_mutex_unlock(&cache_lock);
  return 0;
} /* }}} int rrd_cache_destroy */

static int rrd_compare_numeric(const void *a_ptr, const void *b_ptr) {
  int a = *((int *)a_ptr);
  int b = *((int *)b_ptr);

  if (a < b)
    return -1;
  else if (a > b)
    return 1;
  else
    return 0;
} /* int rrd_compare_numeric */

static int rrd_write(const data_set_t *ds, const value_list_t *vl,
                     user_data_t __attribute__((unused)) * user_data) {

  if (do_shutdown)
    return 0;

  if (0 != strcmp(ds->type, vl->type)) {
    ERROR("rrdtool plugin: DS type does not match value list type");
    return -1;
  }

  char filename[PATH_MAX];
  if (value_list_to_filename(filename, sizeof(filename), vl) != 0)
    return -1;

  char values[32 * (ds->ds_num + 1)];
  if (value_list_to_string(values, sizeof(values), ds, vl) != 0)
    return -1;

  struct stat statbuf = {0};
  if (stat(filename, &statbuf) == -1) {
    if (errno == ENOENT) {
      if (cu_rrd_create_file(filename, ds, vl, &rrdcreate_config) != 0) {
        return -1;
      } else if (rrdcreate_config.async) {
        return 0;
      }
    } else {
      ERROR("rrdtool plugin: stat(%s) failed: %s", filename, STRERRNO);
      return -1;
    }
  } else if (!S_ISREG(statbuf.st_mode)) {
    ERROR("rrdtool plugin: stat(%s): Not a regular file!", filename);
    return -1;
  }

  return rrd_cache_insert(filename, values, vl->time);
} /* int rrd_write */

static int rrd_flush(cdtime_t timeout, const char *identifier,
                     __attribute__((unused)) user_data_t *user_data) {
  pthread_mutex_lock(&cache_lock);

  if (cache == NULL) {
    pthread_mutex_unlock(&cache_lock);
    return 0;
  }

  rrd_cache_flush_identifier(timeout, identifier);

  pthread_mutex_unlock(&cache_lock);
  return 0;
} /* int rrd_flush */

static int rrd_config(const char *key, const char *value) {
  if (strcasecmp("CacheTimeout", key) == 0) {
    double tmp = atof(value);
    if (tmp < 0) {
      fprintf(stderr, "rrdtool: `CacheTimeout' must "
                      "be greater than 0.\n");
      ERROR("rrdtool: `CacheTimeout' must "
            "be greater than 0.\n");
      return 1;
    }
    cache_timeout = DOUBLE_TO_CDTIME_T(tmp);
  } else if (strcasecmp("CacheFlush", key) == 0) {
    double tmp = atof(value);
    if (tmp < 0) {
      fprintf(stderr, "rrdtool: `CacheFlush' must "
                      "be greater than 0.\n");
      ERROR("rrdtool: `CacheFlush' must "
            "be greater than 0.\n");
      return 1;
    }
    cache_flush_timeout = DOUBLE_TO_CDTIME_T(tmp);
  } else if (strcasecmp("DataDir", key) == 0) {
    char *tmp;
    size_t len;

    tmp = strdup(value);
    if (tmp == NULL) {
      ERROR("rrdtool plugin: strdup failed.");
      return 1;
    }

    len = strlen(tmp);
    while ((len > 0) && (tmp[len - 1] == '/')) {
      len--;
      tmp[len] = 0;
    }

    if (len == 0) {
      ERROR("rrdtool plugin: Invalid \"DataDir\" option.");
      sfree(tmp);
      return 1;
    }

    if (datadir != NULL) {
      sfree(datadir);
    }

    datadir = tmp;
  } else if (strcasecmp("StepSize", key) == 0) {
    unsigned long temp = strtoul(value, NULL, 0);
    if (temp > 0)
      rrdcreate_config.stepsize = temp;
  } else if (strcasecmp("HeartBeat", key) == 0) {
    int temp = atoi(value);
    if (temp > 0)
      rrdcreate_config.heartbeat = temp;
  } else if (strcasecmp("CreateFilesAsync", key) == 0) {
    if (IS_TRUE(value))
      rrdcreate_config.async = 1;
    else
      rrdcreate_config.async = 0;
  } else if (strcasecmp("RRARows", key) == 0) {
    int tmp = atoi(value);
    if (tmp <= 0) {
      fprintf(stderr, "rrdtool: `RRARows' must "
                      "be greater than 0.\n");
      ERROR("rrdtool: `RRARows' must "
            "be greater than 0.\n");
      return 1;
    }
    rrdcreate_config.rrarows = tmp;
  } else if (strcasecmp("RRATimespan", key) == 0) {
    char *saveptr = NULL;
    char *dummy;
    char *ptr;
    char *value_copy;
    int *tmp_alloc;

    value_copy = strdup(value);
    if (value_copy == NULL)
      return 1;

    dummy = value_copy;
    while ((ptr = strtok_r(dummy, ", \t", &saveptr)) != NULL) {
      dummy = NULL;

      tmp_alloc = realloc(rrdcreate_config.timespans,
                          sizeof(int) * (rrdcreate_config.timespans_num + 1));
      if (tmp_alloc == NULL) {
        fprintf(stderr, "rrdtool: realloc failed.\n");
        ERROR("rrdtool: realloc failed.\n");
        free(value_copy);
        return 1;
      }
      rrdcreate_config.timespans = tmp_alloc;
      rrdcreate_config.timespans[rrdcreate_config.timespans_num] = atoi(ptr);
      if (rrdcreate_config.timespans[rrdcreate_config.timespans_num] != 0)
        rrdcreate_config.timespans_num++;
    } /* while (strtok_r) */

    qsort(/* base = */ rrdcreate_config.timespans,
          /* nmemb  = */ rrdcreate_config.timespans_num,
          /* size   = */ sizeof(rrdcreate_config.timespans[0]),
          /* compar = */ rrd_compare_numeric);

    free(value_copy);
  } else if (strcasecmp("XFF", key) == 0) {
    double tmp = atof(value);
    if ((tmp < 0.0) || (tmp >= 1.0)) {
      fprintf(stderr, "rrdtool: `XFF' must "
                      "be in the range 0 to 1 (exclusive).");
      ERROR("rrdtool: `XFF' must "
            "be in the range 0 to 1 (exclusive).");
      return 1;
    }
    rrdcreate_config.xff = tmp;
  } else if (strcasecmp("WritesPerSecond", key) == 0) {
    double wps = atof(value);

    if (wps < 0.0) {
      fprintf(stderr, "rrdtool: `WritesPerSecond' must be "
                      "greater than or equal to zero.");
      return 1;
    } else if (wps == 0.0) {
      write_rate = 0.0;
    } else {
      write_rate = 1.0 / wps;
    }
  } else if (strcasecmp("RandomTimeout", key) == 0) {
    double tmp;

    tmp = atof(value);
    if (tmp < 0.0) {
      fprintf(stderr, "rrdtool: `RandomTimeout' must "
                      "be greater than or equal to zero.\n");
      ERROR("rrdtool: `RandomTimeout' must "
            "be greater then or equal to zero.");
    } else {
      random_timeout = DOUBLE_TO_CDTIME_T(tmp);
    }
  } else {
    return -1;
  }
  return 0;
} /* int rrd_config */

static int rrd_shutdown(void) {
  pthread_mutex_lock(&cache_lock);
  rrd_cache_flush(0);
  pthread_mutex_unlock(&cache_lock);

  pthread_mutex_lock(&queue_lock);
  do_shutdown = 1;
  pthread_cond_signal(&queue_cond);
  pthread_mutex_unlock(&queue_lock);

  if ((queue_thread_running != 0) &&
      ((queue_head != NULL) || (flushq_head != NULL))) {
    INFO("rrdtool plugin: Shutting down the queue thread. "
         "This may take a while.");
  } else if (queue_thread_running != 0) {
    INFO("rrdtool plugin: Shutting down the queue thread.");
  }

  /* Wait for all the values to be written to disk before returning. */
  if (queue_thread_running != 0) {
    pthread_join(queue_thread, NULL);
    memset(&queue_thread, 0, sizeof(queue_thread));
    queue_thread_running = 0;
    DEBUG("rrdtool plugin: queue_thread exited.");
  }

  rrd_cache_destroy();

  return 0;
} /* int rrd_shutdown */

static int rrd_init(void) {
  static int init_once = 0;

  if (init_once != 0)
    return 0;
  init_once = 1;

  if (rrdcreate_config.heartbeat <= 0)
    rrdcreate_config.heartbeat = 2 * rrdcreate_config.stepsize;

  /* Set the cache up */
  pthread_mutex_lock(&cache_lock);

  cache = c_avl_create((int (*)(const void *, const void *))strcmp);
  if (cache == NULL) {
    pthread_mutex_unlock(&cache_lock);
    ERROR("rrdtool plugin: c_avl_create failed.");
    return -1;
  }

  cache_flush_last = cdtime();
  if (cache_timeout == 0) {
    random_timeout = 0;
    cache_flush_timeout = 0;
  } else if (cache_flush_timeout < cache_timeout) {
    INFO("rrdtool plugin: \"CacheFlush %.3f\" is less than \"CacheTimeout "
         "%.3f\". Adjusting \"CacheFlush\" to %.3f seconds.",
         CDTIME_T_TO_DOUBLE(cache_flush_timeout),
         CDTIME_T_TO_DOUBLE(cache_timeout),
         CDTIME_T_TO_DOUBLE(cache_timeout * 10));
    cache_flush_timeout = 10 * cache_timeout;
  }

  /* Assure that "cache_timeout + random_variation" is never negative. */
  if (random_timeout > cache_timeout) {
    INFO("rrdtool plugin: Adjusting \"RandomTimeout\" to %.3f seconds.",
         CDTIME_T_TO_DOUBLE(cache_timeout));
    random_timeout = cache_timeout;
  }

  pthread_mutex_unlock(&cache_lock);

  int status =
      plugin_thread_create(&queue_thread, /* attr = */ NULL, rrd_queue_thread,
                           /* args = */ NULL, "rrdtool queue");
  if (status != 0) {
    ERROR("rrdtool plugin: Cannot create queue-thread.");
    return -1;
  }
  queue_thread_running = 1;

  DEBUG("rrdtool plugin: rrd_init: datadir = %s; stepsize = %lu;"
        " heartbeat = %i; rrarows = %i; xff = %lf;",
        (datadir == NULL) ? "(null)" : datadir, rrdcreate_config.stepsize,
        rrdcreate_config.heartbeat, rrdcreate_config.rrarows,
        rrdcreate_config.xff);

  return 0;
} /* int rrd_init */

void module_register(void) {
  plugin_register_config("rrdtool", rrd_config, config_keys, config_keys_num);
  plugin_register_init("rrdtool", rrd_init);
  plugin_register_write("rrdtool", rrd_write, /* user_data = */ NULL);
  plugin_register_flush("rrdtool", rrd_flush, /* user_data = */ NULL);
  plugin_register_shutdown("rrdtool", rrd_shutdown);
}
