/**
 * collectd - src/plugin.c
 * Copyright (C) 2005-2014  Florian octo Forster
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
 *   Sebastian Harl <sh at tokkee.org>
 *   Manoj Srivastava <srivasta at google.com>
 **/

/* _GNU_SOURCE is needed in Linux to use pthread_setname_np */
#define _GNU_SOURCE

#include "collectd.h"

#include "configfile.h"
#include "filter_chain.h"
#include "plugin.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils/heap/heap.h"
#include "utils_cache.h"
#include "utils_complain.h"
#include "utils_llist.h"
#include "utils_random.h"
#include "utils_time.h"

#ifdef WIN32
#define EXPORT __declspec(dllexport)
#include <sys/stat.h>
#include <unistd.h>
#else
#define EXPORT
#endif

#if HAVE_PTHREAD_NP_H
#include <pthread_np.h> /* for pthread_set_name_np(3) */
#endif

#include <dlfcn.h>

/*
 * Private structures
 */
struct callback_func_s {
  void *cf_callback;
  user_data_t cf_udata;
  plugin_ctx_t cf_ctx;
};
typedef struct callback_func_s callback_func_t;

#define RF_SIMPLE 0
#define RF_COMPLEX 1
#define RF_REMOVE 65535
struct read_func_s {
/* `read_func_t' "inherits" from `callback_func_t'.
 * The `rf_super' member MUST be the first one in this structure! */
#define rf_callback rf_super.cf_callback
#define rf_udata rf_super.cf_udata
#define rf_ctx rf_super.cf_ctx
  callback_func_t rf_super;
  char rf_group[DATA_MAX_NAME_LEN];
  char *rf_name;
  int rf_type;
  cdtime_t rf_interval;
  cdtime_t rf_effective_interval;
  cdtime_t rf_next_read;
};
typedef struct read_func_s read_func_t;

struct cache_event_func_s {
  plugin_cache_event_cb callback;
  char *name;
  user_data_t user_data;
  plugin_ctx_t plugin_ctx;
};
typedef struct cache_event_func_s cache_event_func_t;

typedef struct write_queue_elem_s {
  metric_family_t *family;
  plugin_ctx_t ctx;
  const char *plugin;
  long ref_count;
  struct write_queue_elem_s *next;
} write_queue_elem_t;

typedef struct write_queue_thread_s {
  bool loop;
  long queue_length;
  char *name;
  plugin_write_cb callback;
  user_data_t ud;
  pthread_t thread;
  write_queue_elem_t *head;
  struct write_queue_thread_s *next;
} write_queue_thread_t;

struct {
  pthread_mutex_t lock;
  pthread_cond_t cond;
  write_queue_elem_t *tail;
  write_queue_thread_t *threads;
} write_queue = {
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER,
    .tail = NULL,
    .threads = NULL,
};

struct flush_callback_s {
  char *name;
  cdtime_t timeout;
};
typedef struct flush_callback_s flush_callback_t;

/*
 * Private variables
 */
static c_avl_tree_t *plugins_loaded;

static llist_t *list_init;
static llist_t *list_flush;
static llist_t *list_missing;
static llist_t *list_shutdown;
static llist_t *list_log;
static llist_t *list_notification;

static size_t list_cache_event_num;
static cache_event_func_t list_cache_event[32];

static fc_chain_t *pre_cache_chain;
static fc_chain_t *post_cache_chain;

static c_avl_tree_t *data_sets;

static char *plugindir;

#ifndef DEFAULT_MAX_READ_INTERVAL
#define DEFAULT_MAX_READ_INTERVAL TIME_T_TO_CDTIME_T_STATIC(86400)
#endif
static c_heap_t *read_heap;
static llist_t *read_list;
static int read_loop = 1;
static pthread_mutex_t read_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t read_cond = PTHREAD_COND_INITIALIZER;
static pthread_t *read_threads;
static size_t read_threads_num;
static cdtime_t max_read_interval = DEFAULT_MAX_READ_INTERVAL;

static pthread_key_t plugin_ctx_key;
static bool plugin_ctx_key_initialized;

static long write_limit_high;
static long write_limit_low;

static pthread_mutex_t statistics_lock = PTHREAD_MUTEX_INITIALIZER;
static derive_t stats_values_dropped;
static bool record_statistics;

/*
 * Static functions
 */
static int plugin_dispatch_metric_internal(metric_family_t const *fam);

static const char *plugin_get_dir(void) {
  if (plugindir == NULL)
    return PLUGINDIR;
  else
    return plugindir;
}

static int plugin_update_internal_statistics(void) { /* {{{ */
  long write_queue_length = 0;

  pthread_mutex_lock(&write_queue.lock);
  for (write_queue_thread_t *thread = write_queue.threads; thread != NULL;
       thread = thread->next) {
    if (thread->queue_length > write_queue_length) {
      write_queue_length = thread->queue_length;
    }
  }
  pthread_mutex_unlock(&write_queue.lock);

  gauge_t copy_write_queue_length = (gauge_t)write_queue_length;

  /* Initialize `vl' */
  value_list_t vl = VALUE_LIST_INIT;
  sstrncpy(vl.plugin, "collectd", sizeof(vl.plugin));
  vl.interval = plugin_get_interval();

  /* Write queue */
  sstrncpy(vl.plugin_instance, "write_queue", sizeof(vl.plugin_instance));

  /* Write queue : queue length */
  vl.values = &(value_t){.gauge = copy_write_queue_length};
  vl.values_len = 1;
  sstrncpy(vl.type, "queue_length", sizeof(vl.type));
  vl.type_instance[0] = 0;
  plugin_dispatch_values(&vl);

  /* Write queue : Values dropped (queue length > low limit) */
  pthread_mutex_lock(&statistics_lock);
  vl.values = &(value_t){.gauge = (gauge_t)stats_values_dropped};
  pthread_mutex_unlock(&statistics_lock);
  vl.values_len = 1;
  sstrncpy(vl.type, "derive", sizeof(vl.type));
  sstrncpy(vl.type_instance, "dropped", sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);

  /* Cache */
  sstrncpy(vl.plugin_instance, "cache", sizeof(vl.plugin_instance));

  /* Cache : Nb entry in cache tree */
  vl.values = &(value_t){.gauge = (gauge_t)uc_get_size()};
  vl.values_len = 1;
  sstrncpy(vl.type, "cache_size", sizeof(vl.type));
  vl.type_instance[0] = 0;
  plugin_dispatch_values(&vl);

  return 0;
} /* }}} int plugin_update_internal_statistics */

static void free_userdata(user_data_t const *ud) /* {{{ */
{
  if (ud == NULL)
    return;

  if ((ud->data != NULL) && (ud->free_func != NULL)) {
    ud->free_func(ud->data);
  }
} /* }}} void free_userdata */

static void destroy_callback(callback_func_t *cf) /* {{{ */
{
  if (cf == NULL)
    return;
  free_userdata(&cf->cf_udata);
  sfree(cf);
} /* }}} void destroy_callback */

static void destroy_all_callbacks(llist_t **list) /* {{{ */
{
  llentry_t *le;

  if (*list == NULL)
    return;

  le = llist_head(*list);
  while (le != NULL) {
    llentry_t *le_next;

    le_next = le->next;

    sfree(le->key);
    destroy_callback(le->value);
    le->value = NULL;

    le = le_next;
  }

  llist_destroy(*list);
  *list = NULL;
} /* }}} void destroy_all_callbacks */

static void destroy_read_heap(void) /* {{{ */
{
  if (read_heap == NULL)
    return;

  while (42) {
    read_func_t *rf;

    rf = c_heap_get_root(read_heap);
    if (rf == NULL)
      break;
    sfree(rf->rf_name);
    destroy_callback((callback_func_t *)rf);
  }

  c_heap_destroy(read_heap);
  read_heap = NULL;
} /* }}} void destroy_read_heap */

static int register_callback(llist_t **list, /* {{{ */
                             const char *name, callback_func_t *cf) {

  if (*list == NULL) {
    *list = llist_create();
    if (*list == NULL) {
      ERROR("plugin: register_callback: "
            "llist_create failed.");
      destroy_callback(cf);
      return -1;
    }
  }

  char *key = strdup(name);
  if (key == NULL) {
    ERROR("plugin: register_callback: strdup failed.");
    destroy_callback(cf);
    return -1;
  }

  llentry_t *le = llist_search(*list, name);
  if (le == NULL) {
    le = llentry_create(key, cf);
    if (le == NULL) {
      ERROR("plugin: register_callback: "
            "llentry_create failed.");
      sfree(key);
      destroy_callback(cf);
      return -1;
    }

    llist_append(*list, le);
  } else {
    callback_func_t *old_cf = le->value;
    le->value = cf;

    P_WARNING("register_callback: "
              "a callback named `%s' already exists - "
              "overwriting the old entry!",
              name);

    destroy_callback(old_cf);
    sfree(key);
  }

  return 0;
} /* }}} int register_callback */

static int create_register_callback(llist_t **list, /* {{{ */
                                    const char *name, void *callback,
                                    user_data_t const *ud) {

  if (name == NULL || callback == NULL)
    return EINVAL;

  callback_func_t *cf = calloc(1, sizeof(*cf));
  if (cf == NULL) {
    free_userdata(ud);
    ERROR("plugin: create_register_callback: calloc failed.");
    return ENOMEM;
  }

  cf->cf_callback = callback;
  if (ud == NULL) {
    cf->cf_udata = (user_data_t){
        .data = NULL,
        .free_func = NULL,
    };
  } else {
    cf->cf_udata = *ud;
  }

  cf->cf_ctx = plugin_get_ctx();

  return register_callback(list, name, cf);
} /* }}} int create_register_callback */

static int plugin_unregister(llist_t *list, const char *name) /* {{{ */
{
  llentry_t *e;

  if (list == NULL)
    return -1;

  e = llist_search(list, name);
  if (e == NULL)
    return -1;

  llist_remove(list, e);

  sfree(e->key);
  destroy_callback(e->value);

  llentry_destroy(e);

  return 0;
} /* }}} int plugin_unregister */

/* plugin_load_file loads the shared object "file" and calls its
 * "module_register" function. Returns zero on success, non-zero otherwise. */
static int plugin_load_file(char const *file, bool global) {
  int flags = RTLD_NOW;
  if (global)
    flags |= RTLD_GLOBAL;

  void *dlh = dlopen(file, flags);
  if (dlh == NULL) {
    char errbuf[1024] = "";

    snprintf(errbuf, sizeof(errbuf),
             "dlopen(\"%s\") failed: %s. "
             "The most common cause for this problem is missing dependencies. "
             "Use ldd(1) to check the dependencies of the plugin / shared "
             "object.",
             file, dlerror());

    /* This error is printed to STDERR unconditionally. If list_log is NULL,
     * plugin_log() will also print to STDERR. We avoid duplicate output by
     * checking that the list of log handlers, list_log, is not NULL. */
    fprintf(stderr, "ERROR: %s\n", errbuf);
    if (list_log != NULL) {
      ERROR("%s", errbuf);
    }

    return ENOENT;
  }

  void (*reg_handle)(void) = (void *)dlsym(dlh, "module_register");
  if (reg_handle == NULL) {
    ERROR("Couldn't find symbol \"module_register\" in \"%s\": %s\n", file,
          dlerror());
    dlclose(dlh);
    return ENOENT;
  }

  (*reg_handle)();
  return 0;
}

static void *plugin_read_thread(void __attribute__((unused)) * args) {
  while (read_loop != 0) {
    read_func_t *rf;
    plugin_ctx_t old_ctx;
    cdtime_t start;
    cdtime_t now;
    cdtime_t elapsed;
    int status;
    int rf_type;
    int rc;

    /* Get the read function that needs to be read next.
     * We don't need to hold "read_lock" for the heap, but we need
     * to call c_heap_get_root() and pthread_cond_wait() in the
     * same protected block. */
    pthread_mutex_lock(&read_lock);
    rf = c_heap_get_root(read_heap);
    if (rf == NULL) {
      pthread_cond_wait(&read_cond, &read_lock);
      pthread_mutex_unlock(&read_lock);
      continue;
    }
    pthread_mutex_unlock(&read_lock);

    if (rf->rf_interval == 0) {
      /* this should not happen, because the interval is set
       * for each plugin when loading it
       * XXX: issue a warning? */
      rf->rf_interval = plugin_get_interval();
      rf->rf_effective_interval = rf->rf_interval;

      rf->rf_next_read = cdtime();
    }

    /* sleep until this entry is due,
     * using pthread_cond_timedwait */
    pthread_mutex_lock(&read_lock);
    /* In pthread_cond_timedwait, spurious wakeups are possible
     * (and really happen, at least on NetBSD with > 1 CPU), thus
     * we need to re-evaluate the condition every time
     * pthread_cond_timedwait returns. */
    rc = 0;
    while ((read_loop != 0) && (cdtime() < rf->rf_next_read) && rc == 0) {
      rc = pthread_cond_timedwait(&read_cond, &read_lock,
                                  &CDTIME_T_TO_TIMESPEC(rf->rf_next_read));
    }

    /* Must hold `read_lock' when accessing `rf->rf_type'. */
    rf_type = rf->rf_type;
    pthread_mutex_unlock(&read_lock);

    /* Check if we're supposed to stop.. This may have interrupted
     * the sleep, too. */
    if (read_loop == 0) {
      /* Insert `rf' again, so it can be free'd correctly */
      c_heap_insert(read_heap, rf);
      break;
    }

    /* The entry has been marked for deletion. The linked list
     * entry has already been removed by `plugin_unregister_read'.
     * All we have to do here is free the `read_func_t' and
     * continue. */
    if (rf_type == RF_REMOVE) {
      DEBUG("plugin_read_thread: Destroying the `%s' "
            "callback.",
            rf->rf_name);
      sfree(rf->rf_name);
      destroy_callback((callback_func_t *)rf);
      rf = NULL;
      continue;
    }

    DEBUG("plugin_read_thread: Handling `%s'.", rf->rf_name);

    start = cdtime();

    old_ctx = plugin_set_ctx(rf->rf_ctx);

    if (rf_type == RF_SIMPLE) {
      int (*callback)(void) = (void *)rf->rf_callback;

      status = (*callback)();
    } else {
      assert(rf_type == RF_COMPLEX);

      plugin_read_cb callback = (void *)rf->rf_callback;
      status = (*callback)(&rf->rf_udata);
    }

    plugin_set_ctx(old_ctx);

    /* If the function signals failure, we will increase the
     * intervals in which it will be called. */
    if (status != 0) {
      rf->rf_effective_interval *= 2;
      if (rf->rf_effective_interval > max_read_interval)
        rf->rf_effective_interval = max_read_interval;

      NOTICE("read-function of plugin `%s' failed. "
             "Will suspend it for %.3f seconds.",
             rf->rf_name, CDTIME_T_TO_DOUBLE(rf->rf_effective_interval));
    } else {
      /* Success: Restore the interval, if it was changed. */
      rf->rf_effective_interval = rf->rf_interval;
    }

    /* update the ``next read due'' field */
    now = cdtime();

    /* calculate the time spent in the read function */
    elapsed = (now - start);

    if (elapsed > rf->rf_effective_interval)
      WARNING(
          "plugin_read_thread: read-function of the `%s' plugin took %.3f "
          "seconds, which is above its read interval (%.3f seconds). You might "
          "want to adjust the `Interval' or `ReadThreads' settings.",
          rf->rf_name, CDTIME_T_TO_DOUBLE(elapsed),
          CDTIME_T_TO_DOUBLE(rf->rf_effective_interval));

    DEBUG("plugin_read_thread: read-function of the `%s' plugin took "
          "%.6f seconds.",
          rf->rf_name, CDTIME_T_TO_DOUBLE(elapsed));

    DEBUG("plugin_read_thread: Effective interval of the "
          "`%s' plugin is %.3f seconds.",
          rf->rf_name, CDTIME_T_TO_DOUBLE(rf->rf_effective_interval));

    /* Calculate the next (absolute) time at which this function
     * should be called. */
    rf->rf_next_read += rf->rf_effective_interval;

    /* Check, if `rf_next_read' is in the past. */
    if (rf->rf_next_read < now) {
      /* `rf_next_read' is in the past. Insert `now'
       * so this value doesn't trail off into the
       * past too much. */
      rf->rf_next_read = now;
    }

    DEBUG("plugin_read_thread: Next read of the `%s' plugin at %.3f.",
          rf->rf_name, CDTIME_T_TO_DOUBLE(rf->rf_next_read));

    /* Re-insert this read function into the heap again. */
    c_heap_insert(read_heap, rf);
  } /* while (read_loop) */

  pthread_exit(NULL);
  return (void *)0;
} /* void *plugin_read_thread */

#ifdef PTHREAD_MAX_NAMELEN_NP
#define THREAD_NAME_MAX PTHREAD_MAX_NAMELEN_NP
#else
#define THREAD_NAME_MAX 16
#endif

static void set_thread_name(pthread_t tid, char const *name) {
#if defined(HAVE_PTHREAD_SETNAME_NP) || defined(HAVE_PTHREAD_SET_NAME_NP)

  /* glibc limits the length of the name and fails if the passed string
   * is too long, so we truncate it here. */
  char n[THREAD_NAME_MAX];
  if (strlen(name) >= THREAD_NAME_MAX)
    WARNING("set_thread_name(\"%s\"): name too long", name);
  sstrncpy(n, name, sizeof(n));

#if defined(HAVE_PTHREAD_SETNAME_NP)
  int status = pthread_setname_np(tid, n);
  if (status != 0) {
    ERROR("set_thread_name(\"%s\"): %s", n, STRERROR(status));
  }
#else /* if defined(HAVE_PTHREAD_SET_NAME_NP) */
  pthread_set_name_np(tid, n);
#endif

#endif
}

static void start_read_threads(size_t num) /* {{{ */
{
  if (read_threads != NULL)
    return;

  read_threads = calloc(num, sizeof(*read_threads));
  if (read_threads == NULL) {
    ERROR("plugin: start_read_threads: calloc failed.");
    return;
  }

  read_threads_num = 0;
  for (size_t i = 0; i < num; i++) {
    int status = pthread_create(read_threads + read_threads_num,
                                /* attr = */ NULL, plugin_read_thread,
                                /* arg = */ NULL);
    if (status != 0) {
      ERROR("plugin: start_read_threads: pthread_create failed with status %i "
            "(%s).",
            status, STRERROR(status));
      return;
    }

    char name[THREAD_NAME_MAX];
    ssnprintf(name, sizeof(name), "reader#%" PRIu64,
              (uint64_t)read_threads_num);
    set_thread_name(read_threads[read_threads_num], name);

    read_threads_num++;
  } /* for (i) */
} /* }}} void start_read_threads */

static void stop_read_threads(void) {
  if (read_threads == NULL)
    return;

  INFO("collectd: Stopping %" PRIsz " read threads.", read_threads_num);

  pthread_mutex_lock(&read_lock);
  read_loop = 0;
  DEBUG("plugin: stop_read_threads: Signalling `read_cond'");
  pthread_cond_broadcast(&read_cond);
  pthread_mutex_unlock(&read_lock);

  for (size_t i = 0; i < read_threads_num; i++) {
    if (pthread_join(read_threads[i], NULL) != 0) {
      ERROR("plugin: stop_read_threads: pthread_join failed.");
    }
    read_threads[i] = (pthread_t)0;
  }
  sfree(read_threads);
  read_threads_num = 0;
} /* void stop_read_threads */

static void plugin_value_list_free(value_list_t *vl) /* {{{ */
{
  if (vl == NULL)
    return;

  meta_data_destroy(vl->meta);
  sfree(vl->values);
  sfree(vl);
} /* }}} void plugin_value_list_free */

static value_list_t *
plugin_value_list_clone(value_list_t const *vl_orig) /* {{{ */
{
  value_list_t *vl;

  if (vl_orig == NULL)
    return NULL;

  vl = malloc(sizeof(*vl));
  if (vl == NULL)
    return NULL;
  memcpy(vl, vl_orig, sizeof(*vl));

  if (vl->host[0] == 0)
    sstrncpy(vl->host, hostname_g, sizeof(vl->host));

  vl->values = calloc(vl_orig->values_len, sizeof(*vl->values));
  if (vl->values == NULL) {
    plugin_value_list_free(vl);
    return NULL;
  }
  memcpy(vl->values, vl_orig->values,
         vl_orig->values_len * sizeof(*vl->values));

  vl->meta = meta_data_clone(vl->meta);
  if ((vl_orig->meta != NULL) && (vl->meta == NULL)) {
    plugin_value_list_free(vl);
    return NULL;
  }

  if (vl->time == 0)
    vl->time = cdtime();

  /* Fill in the interval from the thread context, if it is zero. */
  if (vl->interval == 0)
    vl->interval = plugin_get_interval();

  return vl;
} /* }}} value_list_t *plugin_value_list_clone */

static void write_queue_ref_single(write_queue_elem_t *elem, long dir) {
  elem->ref_count += dir;

  assert(elem->ref_count >= 0);

  if (elem->ref_count == 0) {
    if (write_queue.tail == elem) {
      write_queue.tail = NULL;
      assert(elem->next == NULL);
    }

    metric_family_free(elem->family);
    sfree(elem);
  }
}

static void write_queue_ref_all(write_queue_elem_t *start, long dir) {
  while (start != NULL) {
    write_queue_elem_t *elem = start;
    start = elem->next;

    write_queue_ref_single(elem, dir);
  }
}

static int write_queue_enqueue(write_queue_elem_t *ins_head) {
  static c_complain_t no_write_complaint = C_COMPLAIN_INIT_STATIC;

  if (ins_head == NULL) {
    return EINVAL;
  }

  pthread_mutex_lock(&write_queue.lock);

  if (write_queue.threads == NULL) {
    c_complain_once(LOG_WARNING, &no_write_complaint,
                    "write_queue_enqueue: No write callback has been "
                    "registered. Please load at least one output plugin, "
                    "if you want the collected data to be stored.");

    /* Element in the ins_head queue already have zero reference count
     * but without any write threads there is noone to free them.
     * make sure they are freed by de-refing them 0 times. */
    write_queue_ref_all(ins_head, 0);
    pthread_mutex_unlock(&write_queue.lock);

    return ENOENT;
  }

  write_queue_elem_t *ins_tail = NULL;
  long num_elems = 0;

  /* More than one element may be enqueued at once. Count elements and find
   * local tail. */
  for (write_queue_elem_t *elem = ins_head; elem != NULL; elem = elem->next) {
    ins_tail = elem;
    num_elems++;
  }

  /* Add reference to new elements to existing queue (if there is one)
   * and update the tail. */
  if (write_queue.tail != NULL) {
    write_queue.tail->next = ins_head;
  }
  write_queue.tail = ins_tail;

  /* Iterate through all registered write plugins/threads to:
   * a) update their head pointer if their queue is currently empty.
   * b) find the thread with the longest queue to apply limits later. */
  write_queue_thread_t *slowest_thread = write_queue.threads;

  for (write_queue_thread_t *thread = write_queue.threads; thread != NULL;
       thread = thread->next) {
    if (thread->head == NULL) {
      thread->head = ins_head;
    }

    /* Mark the new elements as to be consumed by this thread */
    write_queue_ref_all(ins_head, 1);
    thread->queue_length += num_elems;

    if (thread->queue_length > slowest_thread->queue_length) {
      slowest_thread = thread;
    }
  }

  /* Enforce write_limit_high (unless it is infinite (e.g. == 0)) */
  while (write_limit_high != 0 &&
         slowest_thread->queue_length > write_limit_high) {
    /* Select a random element to drop between the last position in the slowest
     * thread's queue and queue positon "write_limit_low". This makes sure that
     * write plugins that do not let the queue get longer than "write_limit_low"
     * will never drop values, regardless of what other plugins do. */
    long drop_pos =
        cdrand_u() % (slowest_thread->queue_length - write_limit_low) +
        write_limit_low;

    /* Walk the queue and count elements until element number drop_pos is
     * found. to_drop will point to the element in question and to_spare
     * will point to one element before to_drop (it it exists). */
    write_queue_elem_t *to_spare = NULL;
    write_queue_elem_t *to_drop = slowest_thread->head;

    for (long queue_pos = slowest_thread->queue_length - 1;
         queue_pos > drop_pos; queue_pos--) {
      to_spare = to_drop;
      to_drop = to_drop->next;
    }

    /* Unlink to_drop from linked list if it is not the head (in which case
     * to_spare is NULL and it will be unlinked below) */
    if (to_spare != NULL) {
      to_spare->next = to_drop->next;
    }

    /* Iterate through all registered write plugins/threads to:
     * a) update the head if it references to_drop.
     * b) update the thread's queue length if to_drop was still in it's
     *    queue. */
    for (write_queue_thread_t *thread = write_queue.threads; thread != NULL;
         thread = thread->next) {

      if (thread->head == to_drop) {
        thread->head = to_drop->next;
      }

      /* Reduce reference count and queue length for every affected queue.
       * There may still be references held in write_threads if the element was
       * just de-queued in any and is currently used in the callback,
       * so freeing may be delayed until it is dropped there. */
      if (drop_pos < thread->queue_length) {
        thread->queue_length--;
        write_queue_ref_single(to_drop, -1);
      }
    }

    if (record_statistics) {
      pthread_mutex_lock(&statistics_lock);
      stats_values_dropped++;
      pthread_mutex_unlock(&statistics_lock);
    }
  }

  pthread_cond_broadcast(&write_queue.cond);
  pthread_mutex_unlock(&write_queue.lock);

  return 0;
}

static void *plugin_write_thread(void *args) /* {{{ */
{
  write_queue_thread_t *this_thread = args;

  DEBUG("plugin_write_thread (%s): start", this_thread->name);

  pthread_mutex_lock(&write_queue.lock);

  while (this_thread->loop) {
    write_queue_elem_t *elem = this_thread->head;

    if (elem == NULL) {
      pthread_cond_wait(&write_queue.cond, &write_queue.lock);
      continue;
    }

    /* Unlink early so that write_queue_enqueue can freely manipulate the
     * head while the lock is not held in the callback. */
    this_thread->head = elem->next;
    this_thread->queue_length--;

    DEBUG("plugin_write_thread(%s): de-queue %p (remaining queue length: %ld)",
          this_thread->name, elem, this_thread->queue_length);

    /* Should elem be written to all plugins or this plugin in particular? */
    if (elem->plugin == NULL ||
        strcasecmp(elem->plugin, this_thread->name) == 0) {
      pthread_mutex_unlock(&write_queue.lock);

      plugin_ctx_t ctx = elem->ctx;
      ctx.name = (char *)this_thread->name;
      plugin_set_ctx(ctx);

      /* TODO(lgo): do something with the return value? */
      this_thread->callback(elem->family, &this_thread->ud);

      pthread_mutex_lock(&write_queue.lock);
    }

    /* Free the element if it is not referenced by another queue or thread. */
    write_queue_ref_single(elem, -1);
  }

  DEBUG("plugin_write_thread(%s): teardown", this_thread->name);

  /* Drop references to all remaining queue elements */
  if (this_thread->head != NULL) {
    write_queue_ref_all(this_thread->head, -1);
    this_thread->head = NULL;
    this_thread->queue_length = 0;
  }

  pthread_mutex_unlock(&write_queue.lock);
  pthread_exit(NULL);

  return NULL;
} /* }}} void *plugin_write_thread */

/*
 * Public functions
 */
void plugin_set_dir(const char *dir) {
  sfree(plugindir);

  if (dir == NULL) {
    plugindir = NULL;
    return;
  }

  plugindir = strdup(dir);
  if (plugindir == NULL)
    ERROR("plugin_set_dir: strdup(\"%s\") failed", dir);
}

bool plugin_is_loaded(char const *name) {
  if (plugins_loaded == NULL)
    plugins_loaded =
        c_avl_create((int (*)(const void *, const void *))strcasecmp);
  assert(plugins_loaded != NULL);

  int status = c_avl_get(plugins_loaded, name, /* ret_value = */ NULL);
  return status == 0;
}

static int plugin_mark_loaded(char const *name) {
  char *name_copy;
  int status;

  name_copy = strdup(name);
  if (name_copy == NULL)
    return ENOMEM;

  status = c_avl_insert(plugins_loaded,
                        /* key = */ name_copy, /* value = */ NULL);
  return status;
}

static void plugin_free_loaded(void) {
  void *key;
  void *value;

  if (plugins_loaded == NULL)
    return;

  while (c_avl_pick(plugins_loaded, &key, &value) == 0) {
    sfree(key);
    assert(value == NULL);
  }

  c_avl_destroy(plugins_loaded);
  plugins_loaded = NULL;
}

#define BUFSIZE 512
#ifdef WIN32
#define SHLIB_SUFFIX ".dll"
#else
#define SHLIB_SUFFIX ".so"
#endif
int plugin_load(char const *plugin_name, bool global) {
  DIR *dh;
  const char *dir;
  char filename[BUFSIZE] = "";
  char typename[BUFSIZE];
  int ret;
  struct stat statbuf;
  struct dirent *de;
  int status;

  if (plugin_name == NULL)
    return EINVAL;

  /* Check if plugin is already loaded and don't do anything in this
   * case. */
  if (plugin_is_loaded(plugin_name))
    return 0;

  dir = plugin_get_dir();
  ret = 1;

  /*
   * XXX: Magic at work:
   *
   * Some of the language bindings, for example the Python and Perl
   * plugins, need to be able to export symbols to the scripts they run.
   * For this to happen, the "Globals" flag needs to be set.
   * Unfortunately, this technical detail is hard to explain to the
   * average user and she shouldn't have to worry about this, ideally.
   * So in order to save everyone's sanity use a different default for a
   * handful of special plugins. --octo
   */
  if ((strcasecmp("perl", plugin_name) == 0) ||
      (strcasecmp("python", plugin_name) == 0))
    global = true;

  /* `cpu' should not match `cpufreq'. To solve this we add SHLIB_SUFFIX to the
   * type when matching the filename */
  status = snprintf(typename, sizeof(typename), "%s" SHLIB_SUFFIX, plugin_name);
  if ((status < 0) || ((size_t)status >= sizeof(typename))) {
    WARNING("plugin_load: Filename too long: \"%s" SHLIB_SUFFIX "\"",
            plugin_name);
    return -1;
  }

  if ((dh = opendir(dir)) == NULL) {
    ERROR("plugin_load: opendir (%s) failed: %s", dir, STRERRNO);
    return -1;
  }

  while ((de = readdir(dh)) != NULL) {
    if (strcasecmp(de->d_name, typename))
      continue;

    status = snprintf(filename, sizeof(filename), "%s/%s", dir, de->d_name);
    if ((status < 0) || ((size_t)status >= sizeof(filename))) {
      WARNING("plugin_load: Filename too long: \"%s/%s\"", dir, de->d_name);
      continue;
    }

    if (lstat(filename, &statbuf) == -1) {
      WARNING("plugin_load: stat (\"%s\") failed: %s", filename, STRERRNO);
      continue;
    } else if (!S_ISREG(statbuf.st_mode)) {
      /* don't follow symlinks */
      WARNING("plugin_load: %s is not a regular file.", filename);
      continue;
    }

    status = plugin_load_file(filename, global);
    if (status == 0) {
      /* success */
      plugin_mark_loaded(plugin_name);
      ret = 0;
      INFO("plugin_load: plugin \"%s\" successfully loaded.", plugin_name);
      break;
    } else {
      ERROR("plugin_load: Load plugin \"%s\" failed with "
            "status %i.",
            plugin_name, status);
    }
  }

  closedir(dh);

  if (filename[0] == 0)
    ERROR("plugin_load: Could not find plugin \"%s\" in %s", plugin_name, dir);

  return ret;
}

/*
 * The `register_*' functions follow
 */
EXPORT int plugin_register_config(const char *name,
                                  int (*callback)(const char *key,
                                                  const char *val),
                                  const char **keys, int keys_num) {
  cf_register(name, callback, keys, keys_num);
  return 0;
} /* int plugin_register_config */

EXPORT int plugin_register_complex_config(const char *type,
                                          int (*callback)(oconfig_item_t *)) {
  return cf_register_complex(type, callback);
} /* int plugin_register_complex_config */

EXPORT int plugin_register_init(const char *name, int (*callback)(void)) {
  return create_register_callback(&list_init, name, (void *)callback, NULL);
} /* plugin_register_init */

static int plugin_compare_read_func(const void *arg0, const void *arg1) {
  const read_func_t *rf0;
  const read_func_t *rf1;

  rf0 = arg0;
  rf1 = arg1;

  if (rf0->rf_next_read < rf1->rf_next_read)
    return -1;
  else if (rf0->rf_next_read > rf1->rf_next_read)
    return 1;
  else
    return 0;
} /* int plugin_compare_read_func */

/* Add a read function to both, the heap and a linked list. The linked list if
 * used to look-up read functions, especially for the remove function. The heap
 * is used to determine which plugin to read next. */
static int plugin_insert_read(read_func_t *rf) {
  int status;
  llentry_t *le;

  rf->rf_next_read = cdtime();
  rf->rf_effective_interval = rf->rf_interval;

  pthread_mutex_lock(&read_lock);

  if (read_list == NULL) {
    read_list = llist_create();
    if (read_list == NULL) {
      pthread_mutex_unlock(&read_lock);
      ERROR("plugin_insert_read: read_list failed.");
      return -1;
    }
  }

  if (read_heap == NULL) {
    read_heap = c_heap_create(plugin_compare_read_func);
    if (read_heap == NULL) {
      pthread_mutex_unlock(&read_lock);
      ERROR("plugin_insert_read: c_heap_create failed.");
      return -1;
    }
  }

  le = llist_search(read_list, rf->rf_name);
  if (le != NULL) {
    pthread_mutex_unlock(&read_lock);
    P_WARNING("The read function \"%s\" is already registered. "
              "Check for duplicates in your configuration!",
              rf->rf_name);
    return EINVAL;
  }

  le = llentry_create(rf->rf_name, rf);
  if (le == NULL) {
    pthread_mutex_unlock(&read_lock);
    ERROR("plugin_insert_read: llentry_create failed.");
    return -1;
  }

  status = c_heap_insert(read_heap, rf);
  if (status != 0) {
    pthread_mutex_unlock(&read_lock);
    ERROR("plugin_insert_read: c_heap_insert failed.");
    llentry_destroy(le);
    return -1;
  }

  /* This does not fail. */
  llist_append(read_list, le);

  /* Wake up all the read threads. */
  pthread_cond_broadcast(&read_cond);
  pthread_mutex_unlock(&read_lock);
  return 0;
} /* int plugin_insert_read */

EXPORT int plugin_register_read(const char *name, int (*callback)(void)) {
  read_func_t *rf;
  int status;

  rf = calloc(1, sizeof(*rf));
  if (rf == NULL) {
    ERROR("plugin_register_read: calloc failed.");
    return ENOMEM;
  }

  rf->rf_callback = (void *)callback;
  rf->rf_udata.data = NULL;
  rf->rf_udata.free_func = NULL;
  rf->rf_ctx = plugin_get_ctx();
  rf->rf_group[0] = '\0';
  rf->rf_name = strdup(name);
  rf->rf_type = RF_SIMPLE;
  rf->rf_interval = plugin_get_interval();
  rf->rf_ctx.interval = rf->rf_interval;

  status = plugin_insert_read(rf);
  if (status != 0) {
    sfree(rf->rf_name);
    sfree(rf);
  }

  return status;
} /* int plugin_register_read */

EXPORT int plugin_register_complex_read(const char *group, const char *name,
                                        plugin_read_cb callback,
                                        cdtime_t interval,
                                        user_data_t const *user_data) {
  read_func_t *rf;
  int status;

  rf = calloc(1, sizeof(*rf));
  if (rf == NULL) {
    free_userdata(user_data);
    ERROR("plugin_register_complex_read: calloc failed.");
    return ENOMEM;
  }

  rf->rf_callback = (void *)callback;
  if (group != NULL)
    sstrncpy(rf->rf_group, group, sizeof(rf->rf_group));
  else
    rf->rf_group[0] = '\0';
  rf->rf_name = strdup(name);
  rf->rf_type = RF_COMPLEX;
  rf->rf_interval = (interval != 0) ? interval : plugin_get_interval();

  /* Set user data */
  if (user_data == NULL) {
    rf->rf_udata.data = NULL;
    rf->rf_udata.free_func = NULL;
  } else {
    rf->rf_udata = *user_data;
  }

  rf->rf_ctx = plugin_get_ctx();
  rf->rf_ctx.interval = rf->rf_interval;

  status = plugin_insert_read(rf);
  if (status != 0) {
    free_userdata(&rf->rf_udata);
    sfree(rf->rf_name);
    sfree(rf);
  }

  return status;
} /* int plugin_register_complex_read */

EXPORT int plugin_register_write(const char *name, plugin_write_cb callback,
                                 user_data_t const *user_data) {
  write_queue_thread_t *this_thread = calloc(1, sizeof(*this_thread));

  if (this_thread == NULL) {
    free_userdata(user_data);
    ERROR("plugin_register_write: calloc failed.");
    return ENOMEM;
  }

  this_thread->loop = true;
  this_thread->queue_length = 0;
  this_thread->name = strdup(name);
  this_thread->callback = callback;
  this_thread->head = NULL;

  // If no user_data is passed the data and free_func pointers will be NULL
  // due to the calloc() zero-initialization.
  if (user_data) {
    this_thread->ud = *user_data;
  }

  pthread_mutex_lock(&write_queue.lock);

  int status = pthread_create(&this_thread->thread, NULL, plugin_write_thread,
                              (void *)this_thread);

  if (status == 0) {
    char thread_name[THREAD_NAME_MAX];
    ssnprintf(thread_name, sizeof(thread_name), "writer_%s", name);
    set_thread_name(this_thread->thread, thread_name);

    this_thread->next = write_queue.threads;
    write_queue.threads = this_thread;
  } else {
    ERROR("plugin: plugin_register_write: pthread_create failed with status %i "
          "(%s).",
          status, STRERROR(status));

    free_userdata(user_data);
    sfree(this_thread);
  }

  pthread_mutex_unlock(&write_queue.lock);

  return status;
} /* int plugin_register_write */

static int plugin_flush_timeout_callback(user_data_t *ud) {
  flush_callback_t *cb = ud->data;

  return plugin_flush(cb->name, cb->timeout, NULL);
} /* static int plugin_flush_callback */

static void plugin_flush_timeout_callback_free(void *data) {
  flush_callback_t *cb = data;

  if (cb == NULL)
    return;

  sfree(cb->name);
  sfree(cb);
} /* static void plugin_flush_callback_free */

static char *plugin_flush_callback_name(const char *name) {
  const char *flush_prefix = "flush/";
  size_t prefix_size;
  char *flush_name;
  size_t name_size;

  prefix_size = strlen(flush_prefix);
  name_size = strlen(name);

  flush_name = malloc(name_size + prefix_size + 1);
  if (flush_name == NULL) {
    ERROR("plugin_flush_callback_name: malloc failed.");
    return NULL;
  }

  sstrncpy(flush_name, flush_prefix, prefix_size + 1);
  sstrncpy(flush_name + prefix_size, name, name_size + 1);

  return flush_name;
} /* static char *plugin_flush_callback_name */

EXPORT int plugin_register_flush(const char *name, plugin_flush_cb callback,
                                 user_data_t const *ud) {
  plugin_ctx_t ctx = plugin_get_ctx();

  int status =
      create_register_callback(&list_flush, name, (void *)callback, ud);
  if (status != 0) {
    return status;
  }

  if (ctx.flush_interval == 0) {
    return 0;
  }

  char *flush_name = plugin_flush_callback_name(name);
  if (flush_name == NULL) {
    return ENOMEM;
  }

  flush_callback_t *cb = calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("plugin_register_flush: malloc failed.");
    sfree(flush_name);
    return ENOMEM;
  }

  cb->name = strdup(name);
  if (cb->name == NULL) {
    ERROR("plugin_register_flush: strdup failed.");
    sfree(cb);
    sfree(flush_name);
    return ENOMEM;
  }
  cb->timeout = ctx.flush_timeout;

  status = plugin_register_complex_read(
      /* group     = */ "flush",
      /* name      = */ flush_name,
      /* callback  = */ plugin_flush_timeout_callback,
      /* interval  = */ ctx.flush_interval,
      /* user data = */
      &(user_data_t){
          .data = cb,
          .free_func = plugin_flush_timeout_callback_free,
      });

  sfree(flush_name);
  return status;
} /* int plugin_register_flush */

EXPORT int plugin_register_missing(const char *name, plugin_missing_cb callback,
                                   user_data_t const *ud) {
  return create_register_callback(&list_missing, name, (void *)callback, ud);
} /* int plugin_register_missing */

EXPORT int plugin_register_cache_event(const char *name,
                                       plugin_cache_event_cb callback,
                                       user_data_t const *ud) {

  if (name == NULL || callback == NULL)
    return EINVAL;

  char *name_copy = strdup(name);
  if (name_copy == NULL) {
    P_ERROR("plugin_register_cache_event: strdup failed.");
    free_userdata(ud);
    return ENOMEM;
  }

  if (list_cache_event_num >= 32) {
    P_ERROR("plugin_register_cache_event: Too much cache event callbacks tried "
            "to be registered.");
    free_userdata(ud);
    return ENOMEM;
  }

  for (size_t i = 0; i < list_cache_event_num; i++) {
    cache_event_func_t *cef = &list_cache_event[i];
    if (!cef->callback)
      continue;

    if (strcmp(name, cef->name) == 0) {
      P_ERROR("plugin_register_cache_event: a callback named `%s' already "
              "registered!",
              name);
      free_userdata(ud);
      return -1;
    }
  }

  user_data_t user_data;
  if (ud == NULL) {
    user_data = (user_data_t){
        .data = NULL,
        .free_func = NULL,
    };
  } else {
    user_data = *ud;
  }

  list_cache_event[list_cache_event_num] =
      (cache_event_func_t){.callback = callback,
                           .name = name_copy,
                           .user_data = user_data,
                           .plugin_ctx = plugin_get_ctx()};
  list_cache_event_num++;

  return 0;
} /* int plugin_register_cache_event */

EXPORT int plugin_register_shutdown(const char *name, int (*callback)(void)) {
  return create_register_callback(&list_shutdown, name, (void *)callback, NULL);
} /* int plugin_register_shutdown */

static void plugin_free_data_sets(void) {
  void *key;
  void *value;

  if (data_sets == NULL)
    return;

  while (c_avl_pick(data_sets, &key, &value) == 0) {
    data_set_t *ds = value;
    /* key is a pointer to ds->type */

    sfree(ds->ds);
    sfree(ds);
  }

  c_avl_destroy(data_sets);
  data_sets = NULL;
} /* void plugin_free_data_sets */

EXPORT int plugin_register_data_set(const data_set_t *ds) {
  data_set_t *ds_copy;

  if ((data_sets != NULL) && (c_avl_get(data_sets, ds->type, NULL) == 0)) {
    NOTICE("Replacing DS `%s' with another version.", ds->type);
    plugin_unregister_data_set(ds->type);
  } else if (data_sets == NULL) {
    data_sets = c_avl_create((int (*)(const void *, const void *))strcmp);
    if (data_sets == NULL)
      return -1;
  }

  ds_copy = malloc(sizeof(*ds_copy));
  if (ds_copy == NULL)
    return -1;
  memcpy(ds_copy, ds, sizeof(data_set_t));

  ds_copy->ds = malloc(sizeof(*ds_copy->ds) * ds->ds_num);
  if (ds_copy->ds == NULL) {
    sfree(ds_copy);
    return -1;
  }

  for (size_t i = 0; i < ds->ds_num; i++)
    memcpy(ds_copy->ds + i, ds->ds + i, sizeof(data_source_t));

  return c_avl_insert(data_sets, (void *)ds_copy->type, (void *)ds_copy);
} /* int plugin_register_data_set */

EXPORT int plugin_register_log(const char *name, plugin_log_cb callback,
                               user_data_t const *ud) {
  return create_register_callback(&list_log, name, (void *)callback, ud);
} /* int plugin_register_log */

EXPORT int plugin_register_notification(const char *name,
                                        plugin_notification_cb callback,
                                        user_data_t const *ud) {
  return create_register_callback(&list_notification, name, (void *)callback,
                                  ud);
} /* int plugin_register_log */

EXPORT int plugin_unregister_config(const char *name) {
  cf_unregister(name);
  return 0;
} /* int plugin_unregister_config */

EXPORT int plugin_unregister_complex_config(const char *name) {
  cf_unregister_complex(name);
  return 0;
} /* int plugin_unregister_complex_config */

EXPORT int plugin_unregister_init(const char *name) {
  return plugin_unregister(list_init, name);
}

EXPORT int plugin_unregister_read(const char *name) /* {{{ */
{
  llentry_t *le;
  read_func_t *rf;

  if (name == NULL)
    return -ENOENT;

  pthread_mutex_lock(&read_lock);

  if (read_list == NULL) {
    pthread_mutex_unlock(&read_lock);
    return -ENOENT;
  }

  le = llist_search(read_list, name);
  if (le == NULL) {
    pthread_mutex_unlock(&read_lock);
    WARNING("plugin_unregister_read: No such read function: %s", name);
    return -ENOENT;
  }

  llist_remove(read_list, le);

  rf = le->value;
  assert(rf != NULL);
  rf->rf_type = RF_REMOVE;

  pthread_mutex_unlock(&read_lock);

  llentry_destroy(le);

  DEBUG("plugin_unregister_read: Marked `%s' for removal.", name);

  return 0;
} /* }}} int plugin_unregister_read */

EXPORT void plugin_log_available_writers(void) {
  const char *sep = "' , '";
  size_t sep_len = strlen(sep);

  pthread_mutex_lock(&write_queue.lock);

  if (write_queue.threads == NULL) {
    INFO("Available write targets: [none]");
    return;
  }

  size_t total_len = 0;

  for (write_queue_thread_t *piv = write_queue.threads; piv != NULL;
       piv = piv->next) {
    total_len += strlen(piv->name);
    if (piv->next != NULL) {
      total_len += sep_len;
    }
  }

  char *str = malloc(total_len + 1);
  if (str == NULL) {
    ERROR("Available write targets: failed to allocate memory for list of "
          "writers");
    return;
  }

  char *cursor = str;

  for (write_queue_thread_t *piv = write_queue.threads; piv != NULL;
       piv = piv->next) {
    size_t name_len = strlen(piv->name);
    memcpy(cursor, piv->name, name_len);
    cursor += name_len;

    if (piv->next != NULL) {
      memcpy(cursor, sep, sep_len);
      cursor += sep_len;
    }
  }

  *cursor = '\0';

  pthread_mutex_unlock(&write_queue.lock);

  INFO("Available write targets: ['%s']", str);
  sfree(str);
}

static int compare_read_func_group(llentry_t *e, void *ud) /* {{{ */
{
  read_func_t *rf = e->value;
  char *group = ud;

  return strcmp(rf->rf_group, (const char *)group);
} /* }}} int compare_read_func_group */

EXPORT int plugin_unregister_read_group(const char *group) /* {{{ */
{
  llentry_t *le;
  read_func_t *rf;

  int found = 0;

  if (group == NULL)
    return -ENOENT;

  pthread_mutex_lock(&read_lock);

  if (read_list == NULL) {
    pthread_mutex_unlock(&read_lock);
    return -ENOENT;
  }

  while (42) {
    le = llist_search_custom(read_list, compare_read_func_group, (void *)group);

    if (le == NULL)
      break;

    ++found;

    llist_remove(read_list, le);

    rf = le->value;
    assert(rf != NULL);
    rf->rf_type = RF_REMOVE;

    llentry_destroy(le);

    DEBUG("plugin_unregister_read_group: "
          "Marked `%s' (group `%s') for removal.",
          rf->rf_name, group);
  }

  pthread_mutex_unlock(&read_lock);

  if (found == 0) {
    WARNING("plugin_unregister_read_group: No such "
            "group of read function: %s",
            group);
    return -ENOENT;
  }

  return 0;
} /* }}} int plugin_unregister_read_group */

EXPORT int plugin_unregister_write(const char *name) {
  pthread_mutex_lock(&write_queue.lock);

  /* Build to completely new thread lists. One with threads to_stop and another
   * with threads to_keep. If name is NULL to_keep will be empty and to_stop
   * will contain all threads. If name is NULL to_stop will contain the
   * relevant thread and to_keep will contain all remaining threads. */
  write_queue_thread_t *to_stop = NULL;
  write_queue_thread_t *to_keep = NULL;

  for (write_queue_thread_t *piv = write_queue.threads; piv != NULL;) {
    write_queue_thread_t *next = piv->next;

    if (name == NULL || strcasecmp(name, piv->name) == 0) {
      piv->loop = false;
      piv->next = to_stop;
      to_stop = piv;
    } else {
      piv->next = to_keep;
      to_keep = piv;
    }

    piv = next;
  }

  write_queue.threads = to_keep;

  pthread_cond_broadcast(&write_queue.cond);
  pthread_mutex_unlock(&write_queue.lock);

  /* Return error if the requested thread was not found */
  if (to_stop == NULL && name != NULL) {
    return ENOENT;
  }

  int status = 0;

  while (to_stop != NULL) {
    write_queue_thread_t *next = to_stop->next;

    int ret = pthread_join(to_stop->thread, NULL);

    if (ret != 0) {
      ERROR("plugin_unregister_write: pthread_join failed for %s.",
            to_stop->name);
      status = ret;
    }

    free_userdata(&to_stop->ud);
    sfree(to_stop->name);
    sfree(to_stop);

    to_free = next;
  }

  return status;
}

EXPORT int plugin_unregister_flush(const char *name) {
  plugin_ctx_t ctx = plugin_get_ctx();

  if (ctx.flush_interval != 0) {
    char *flush_name;

    flush_name = plugin_flush_callback_name(name);
    if (flush_name != NULL) {
      plugin_unregister_read(flush_name);
      sfree(flush_name);
    }
  }

  return plugin_unregister(list_flush, name);
}

EXPORT int plugin_unregister_missing(const char *name) {
  return plugin_unregister(list_missing, name);
}

EXPORT int plugin_unregister_cache_event(const char *name) {
  for (size_t i = 0; i < list_cache_event_num; i++) {
    cache_event_func_t *cef = &list_cache_event[i];
    if (!cef->callback)
      continue;
    if (strcmp(name, cef->name) == 0) {
      /* Mark callback as inactive, so mask in cache entries remains actual */
      cef->callback = NULL;
      sfree(cef->name);
      free_userdata(&cef->user_data);
    }
  }
  return 0;
}

static void destroy_cache_event_callbacks() {
  for (size_t i = 0; i < list_cache_event_num; i++) {
    cache_event_func_t *cef = &list_cache_event[i];
    if (!cef->callback)
      continue;
    cef->callback = NULL;
    sfree(cef->name);
    free_userdata(&cef->user_data);
  }
}

EXPORT int plugin_unregister_shutdown(const char *name) {
  return plugin_unregister(list_shutdown, name);
}

EXPORT int plugin_unregister_data_set(const char *name) {
  data_set_t *ds;

  if (data_sets == NULL)
    return -1;

  if (c_avl_remove(data_sets, name, NULL, (void *)&ds) != 0)
    return -1;

  sfree(ds->ds);
  sfree(ds);

  return 0;
} /* int plugin_unregister_data_set */

EXPORT int plugin_unregister_log(const char *name) {
  return plugin_unregister(list_log, name);
}

EXPORT int plugin_unregister_notification(const char *name) {
  return plugin_unregister(list_notification, name);
}

EXPORT int plugin_init_all(void) {
  char const *chain_name;
  llentry_t *le;
  int status;
  int ret = 0;

  /* Init the value cache */
  uc_init();

  if (IS_TRUE(global_option_get("CollectInternalStats"))) {
    record_statistics = true;
    plugin_register_read("collectd", plugin_update_internal_statistics);
  }

  chain_name = global_option_get("PreCacheChain");
  pre_cache_chain = fc_chain_get_by_name(chain_name);

  chain_name = global_option_get("PostCacheChain");
  post_cache_chain = fc_chain_get_by_name(chain_name);

  write_limit_high = global_option_get_long("WriteQueueLimitHigh",
                                            /* default = */ 0);
  if (write_limit_high < 0) {
    ERROR("WriteQueueLimitHigh must be positive or zero.");
    write_limit_high = 0;
  }

  write_limit_low =
      global_option_get_long("WriteQueueLimitLow",
                             /* default = */ write_limit_high / 2);
  if (write_limit_low < 0) {
    ERROR("WriteQueueLimitLow must be positive or zero.");
    write_limit_low = write_limit_high / 2;
  } else if (write_limit_low > write_limit_high) {
    ERROR("WriteQueueLimitLow must not be larger than "
          "WriteQueueLimitHigh.");
    write_limit_low = write_limit_high;
  }

  if ((list_init == NULL) && (read_heap == NULL))
    return ret;

  /* Calling all init callbacks before checking if read callbacks
   * are available allows the init callbacks to register the read
   * callback. */
  le = llist_head(list_init);
  while (le != NULL) {
    callback_func_t *cf = le->value;
    plugin_ctx_t old_ctx = plugin_set_ctx(cf->cf_ctx);
    plugin_init_cb callback = (void *)cf->cf_callback;
    status = (*callback)();
    plugin_set_ctx(old_ctx);

    if (status != 0) {
      ERROR("Initialization of plugin `%s' "
            "failed with status %i. "
            "Plugin will be unloaded.",
            le->key, status);
      /* Plugins that register read callbacks from the init
       * callback should take care of appropriate error
       * handling themselves. */
      /* FIXME: Unload _all_ functions */
      plugin_unregister_read(le->key);
      ret = -1;
    }

    le = le->next;
  }

  max_read_interval =
      global_option_get_time("MaxReadInterval", DEFAULT_MAX_READ_INTERVAL);

  /* Start read-threads */
  if (read_heap != NULL) {
    const char *rt;
    int num;

    rt = global_option_get("ReadThreads");
    num = atoi(rt);
    if (num != -1)
      start_read_threads((num > 0) ? ((size_t)num) : 5);
  }
  return ret;
} /* void plugin_init_all */

/* TODO: Rename this function. */
EXPORT void plugin_read_all(void) {
  uc_check_timeout();

  return;
} /* void plugin_read_all */

/* Read function called when the `-T' command line argument is given. */
EXPORT int plugin_read_all_once(void) {
  int status;
  int return_status = 0;

  if (read_heap == NULL) {
    NOTICE("No read-functions are registered.");
    return 0;
  }

  while (42) {
    read_func_t *rf;
    plugin_ctx_t old_ctx;

    rf = c_heap_get_root(read_heap);
    if (rf == NULL)
      break;

    old_ctx = plugin_set_ctx(rf->rf_ctx);

    if (rf->rf_type == RF_SIMPLE) {
      int (*callback)(void) = (void *)rf->rf_callback;
      status = (*callback)();
    } else {
      plugin_read_cb callback = (void *)rf->rf_callback;
      status = (*callback)(&rf->rf_udata);
    }

    plugin_set_ctx(old_ctx);

    if (status != 0) {
      NOTICE("read-function of plugin `%s' failed.", rf->rf_name);
      return_status = -1;
    }

    sfree(rf->rf_name);
    destroy_callback((void *)rf);
  }

  return return_status;
} /* int plugin_read_all_once */

EXPORT int plugin_write(const char *plugin, metric_family_t const *fam) {
  if (fam == NULL) {
    return EINVAL;
  }

  /* Create a copy of the metric_family_t so we can metric_family_free() it
   * ourself once it is processed. */
  metric_family_t *fam_copy = metric_family_clone(fam);
  if (fam_copy == NULL) {
    int status = errno;
    ERROR("plugin_write: metric_family_clone failed: %s", STRERROR(status));
    return status;
  }

  write_queue_elem_t *elem = calloc(1, sizeof(*elem));
  if (elem == NULL) {
    metric_family_free(fam_copy);
    return ENOMEM;
  }

  elem->family = fam_copy;
  elem->ctx = plugin_get_ctx();
  elem->plugin = plugin;
  elem->ref_count = 0;
  elem->next = NULL;

  return write_queue_enqueue(elem);
} /* }}} int plugin_write */

EXPORT int plugin_flush(const char *plugin, cdtime_t timeout,
                        const char *identifier) {
  llentry_t *le;

  if (list_flush == NULL)
    return 0;

  le = llist_head(list_flush);
  while (le != NULL) {
    if ((plugin != NULL) && (strcmp(plugin, le->key) != 0)) {
      le = le->next;
      continue;
    }

    callback_func_t *cf = le->value;
    plugin_ctx_t old_ctx = plugin_set_ctx(cf->cf_ctx);
    plugin_flush_cb callback = (void *)cf->cf_callback;

    (*callback)(timeout, identifier, &cf->cf_udata);

    plugin_set_ctx(old_ctx);

    le = le->next;
  }
  return 0;
} /* int plugin_flush */

EXPORT int plugin_shutdown_all(void) {
  llentry_t *le;
  int ret = 0; // Assume success.

  destroy_all_callbacks(&list_init);

  stop_read_threads();

  pthread_mutex_lock(&read_lock);
  llist_destroy(read_list);
  read_list = NULL;
  pthread_mutex_unlock(&read_lock);

  destroy_read_heap();

  /* blocks until all write threads have shut down. */
  plugin_unregister_write(NULL);

  /* ask all plugins to write out the state they kept. */
  plugin_flush(/* plugin = */ NULL,
               /* timeout = */ 0,
               /* identifier = */ NULL);

  le = NULL;
  if (list_shutdown != NULL)
    le = llist_head(list_shutdown);

  while (le != NULL) {
    callback_func_t *cf = le->value;
    plugin_ctx_t old_ctx = plugin_set_ctx(cf->cf_ctx);
    plugin_shutdown_cb callback = (void *)cf->cf_callback;

    /* Advance the pointer before calling the callback allows
     * shutdown functions to unregister themselves. If done the
     * other way around the memory `le' points to will be freed
     * after callback returns. */
    le = le->next;

    if ((*callback)() != 0)
      ret = -1;

    plugin_set_ctx(old_ctx);
  }

  /* Write plugins which use the `user_data' pointer usually need the
   * same data available to the flush callback. If this is the case, set
   * the free_function to NULL when registering the flush callback and to
   * the real free function when registering the write callback. This way
   * the data isn't freed twice. */
  destroy_all_callbacks(&list_flush);
  destroy_all_callbacks(&list_missing);
  destroy_cache_event_callbacks();

  destroy_all_callbacks(&list_notification);
  destroy_all_callbacks(&list_shutdown);
  destroy_all_callbacks(&list_log);

  plugin_free_loaded();
  plugin_free_data_sets();
  return ret;
} /* void plugin_shutdown_all */

EXPORT int plugin_dispatch_missing(metric_family_t const *fam) /* {{{ */
{
  if (list_missing == NULL)
    return 0;

  llentry_t *le = llist_head(list_missing);
  while (le != NULL) {
    callback_func_t *cf = le->value;
    plugin_ctx_t old_ctx = plugin_set_ctx(cf->cf_ctx);
    plugin_missing_cb callback = cf->cf_callback;

    int status = (*callback)(fam, &cf->cf_udata);
    plugin_set_ctx(old_ctx);
    if (status != 0) {
      if (status < 0) {
        ERROR("plugin_dispatch_missing: Callback function \"%s\" "
              "failed with status %i.",
              le->key, status);
        return status;
      } else {
        return 0;
      }
    }

    le = le->next;
  }
  return 0;
} /* int }}} plugin_dispatch_missing */

void plugin_dispatch_cache_event(enum cache_event_type_e event_type,
                                 unsigned long callbacks_mask, const char *name,
                                 metric_t const *m) {
  switch (event_type) {
  case CE_VALUE_NEW:
    callbacks_mask = 0;
    for (size_t i = 0; i < list_cache_event_num; i++) {
      cache_event_func_t *cef = &list_cache_event[i];
      plugin_cache_event_cb callback = cef->callback;

      if (!callback)
        continue;

      cache_event_t event = (cache_event_t){
          .type = event_type,
          .metric = m,
          .value_list_name = name,
          .ret = 0,
      };

      plugin_ctx_t old_ctx = plugin_set_ctx(cef->plugin_ctx);
      int status = (*callback)(&event, &cef->user_data);
      plugin_set_ctx(old_ctx);

      if (status != 0) {
        ERROR("plugin_dispatch_cache_event: Callback \"%s\" failed with status "
              "%i for event NEW.",
              cef->name, status);
      } else {
        if (event.ret) {
          DEBUG(
              "plugin_dispatch_cache_event: Callback \"%s\" subscribed to %s.",
              cef->name, name);
          callbacks_mask |= (1 << (i));
        } else {
          DEBUG("plugin_dispatch_cache_event: Callback \"%s\" ignores %s.",
                cef->name, name);
        }
      }
    }

    if (callbacks_mask)
      uc_set_callbacks_mask(name, callbacks_mask);

    break;
  case CE_VALUE_UPDATE:
  case CE_VALUE_EXPIRED:
    for (size_t i = 0; i < list_cache_event_num; i++) {
      cache_event_func_t *cef = &list_cache_event[i];
      plugin_cache_event_cb callback = cef->callback;

      if (!callback)
        continue;

      if (callbacks_mask && (1 << (i)) == 0)
        continue;

      cache_event_t event = (cache_event_t){
          .type = event_type,
          .metric = m,
          .value_list_name = name,
          .ret = 0,
      };

      plugin_ctx_t old_ctx = plugin_set_ctx(cef->plugin_ctx);
      int status = (*callback)(&event, &cef->user_data);
      plugin_set_ctx(old_ctx);

      if (status != 0) {
        ERROR("plugin_dispatch_cache_event: Callback \"%s\" failed with status "
              "%i for event %s.",
              cef->name, status,
              ((event_type == CE_VALUE_UPDATE) ? "UPDATE" : "EXPIRED"));
      }
    }
    break;
  }
  return;
}

static int plugin_dispatch_metric_internal(metric_family_t const *fam) {
  /**** Handle caching here !! ****/
  int status = 0;
  if (pre_cache_chain != NULL) {
    status = fc_process_chain(fam, pre_cache_chain);
    if (status < 0) {
      WARNING("plugin_dispatch_values: Running the "
              "pre-cache chain failed with "
              "status %i (%#x).",
              status, status);
    } else if (status == FC_TARGET_STOP)
      return 0;
  }

  /* Update the value cache */
  uc_update(fam);

  if (post_cache_chain != NULL) {
    status = fc_process_chain(fam, post_cache_chain);
    if (status < 0) {
      WARNING("plugin_dispatch_values: Running the "
              "post-cache chain failed with "
              "status %i (%#x).",
              status, status);
    }
  } else
    fc_default_action(fam);

  return 0;
} /* int plugin_dispatch_values_internal */

EXPORT int plugin_dispatch_metric_family(metric_family_t const *fam) {
  if ((fam == NULL) || (fam->metric.num == 0)) {
    return EINVAL;
  }

  /* Create a copy of the metric_family_t so we can modify the time and
   * interval without causing confusion when the callee later passes the same
   * fam again. */
  metric_family_t *fam_copy = metric_family_clone(fam);
  if (fam_copy == NULL) {
    int status = errno;
    ERROR("plugin_dispatch_metric_family: metric_family_clone failed: %s",
          STRERROR(status));
    return status;
  }

  cdtime_t time = cdtime();
  cdtime_t interval = plugin_get_interval();

  for (size_t i = 0; i < fam_copy->metric.num; i++) {
    if (fam_copy->metric.ptr[i].time == 0) {
      fam_copy->metric.ptr[i].time = time;
    }
    if (fam_copy->metric.ptr[i].interval == 0) {
      fam_copy->metric.ptr[i].interval = interval;
    }

    /* TODO(octo): set target labels here. */
  }

  int status = plugin_dispatch_metric_internal(fam_copy);
  if (status != 0) {
    ERROR(
        "plugin_dispatch_metric_family: plugin_dispatch_metric_internal failed "
        "with status %i (%s).",
        status, STRERROR(status));
  }

  metric_family_free(fam_copy);

  return status;
}

EXPORT int plugin_dispatch_values(value_list_t const *vl) {
  data_set_t const *ds = plugin_get_ds(vl->type);
  if (ds == NULL) {
    return EINVAL;
  }

  for (size_t i = 0; i < vl->values_len; i++) {
    metric_family_t *fam = plugin_value_list_to_metric_family(vl, ds, i);
    if (fam == NULL) {
      int status = errno;
      ERROR("plugin_dispatch_values: plugin_value_list_to_metric_family "
            "failed: %s",
            STRERROR(status));
      return status;
    }

    int status = plugin_dispatch_metric_family(fam);
    metric_family_free(fam);
    if (status != 0) {
      return status;
    }
  }

  return 0;
}

__attribute__((sentinel)) int
plugin_dispatch_multivalue(value_list_t const *template, /* {{{ */
                           bool store_percentage, int store_type, ...) {
  value_list_t *vl;
  int failed = 0;
  gauge_t sum = 0.0;
  va_list ap;

  assert(template->values_len == 1);

  /* Calculate sum for Gauge to calculate percent if needed */
  if (DS_TYPE_GAUGE == store_type) {
    va_start(ap, store_type);
    while (42) {
      char const *name;
      gauge_t value;

      name = va_arg(ap, char const *);
      if (name == NULL)
        break;

      value = va_arg(ap, gauge_t);
      if (!isnan(value))
        sum += value;
    }
    va_end(ap);
  }

  vl = plugin_value_list_clone(template);
  /* plugin_value_list_clone makes sure vl->time is set to non-zero. */
  if (store_percentage)
    sstrncpy(vl->type, "percent", sizeof(vl->type));

  va_start(ap, store_type);
  while (42) {
    char const *name;
    int status;

    /* Set the type instance. */
    name = va_arg(ap, char const *);
    if (name == NULL)
      break;
    sstrncpy(vl->type_instance, name, sizeof(vl->type_instance));

    /* Set the value. */
    switch (store_type) {
    case DS_TYPE_GAUGE:
      vl->values[0].gauge = va_arg(ap, gauge_t);
      if (store_percentage)
        vl->values[0].gauge *= sum ? (100.0 / sum) : NAN;
      break;
    case DS_TYPE_COUNTER:
      vl->values[0].counter = va_arg(ap, counter_t);
      break;
    case DS_TYPE_DERIVE:
      vl->values[0].derive = va_arg(ap, derive_t);
      break;
    default:
      ERROR("plugin_dispatch_multivalue: given store_type is incorrect.");
      failed++;
    }

    status = plugin_dispatch_values(vl);
    if (status != 0)
      failed++;
  }
  va_end(ap);

  plugin_value_list_free(vl);
  return failed;
} /* }}} int plugin_dispatch_multivalue */

EXPORT int plugin_dispatch_notification(const notification_t *notif) {
  llentry_t *le;
  /* Possible TODO: Add flap detection here */

  DEBUG("plugin_dispatch_notification: severity = %i; message = %s; "
        "time = %.3f; host = %s;",
        notif->severity, notif->message, CDTIME_T_TO_DOUBLE(notif->time),
        notif->host);

  /* Nobody cares for notifications */
  if (list_notification == NULL)
    return -1;

  le = llist_head(list_notification);
  while (le != NULL) {
    callback_func_t *cf;
    plugin_notification_cb callback;
    int status;

    /* do not switch plugin context; rather keep the context
     * (interval) information of the calling plugin */

    cf = le->value;
    callback = cf->cf_callback;
    status = (*callback)(notif, &cf->cf_udata);
    if (status != 0) {
      WARNING("plugin_dispatch_notification: Notification "
              "callback %s returned %i.",
              le->key, status);
    }

    le = le->next;
  }

  return 0;
} /* int plugin_dispatch_notification */

EXPORT void plugin_log(int level, const char *format, ...) {
  char msg[1024];
  va_list ap;
  llentry_t *le;

#if !COLLECT_DEBUG
  if (level >= LOG_DEBUG)
    return;
#endif

  va_start(ap, format);
  vsnprintf(msg, sizeof(msg), format, ap);
  msg[sizeof(msg) - 1] = '\0';
  va_end(ap);

  if (list_log == NULL) {
    fprintf(stderr, "%s\n", msg);
    return;
  }

  le = llist_head(list_log);
  while (le != NULL) {
    callback_func_t *cf = le->value;
    plugin_log_cb callback = (void *)cf->cf_callback;

    /* do not switch plugin context; rather keep the context
     * (interval) information of the calling plugin */

    (*callback)(level, msg, &cf->cf_udata);

    le = le->next;
  }
} /* void plugin_log */

void daemon_log(int level, const char *format, ...) {
  char msg[1024] = ""; // Size inherits from plugin_log()

  char const *name = plugin_get_ctx().name;
  if (name == NULL)
    name = "UNKNOWN";

  va_list ap;
  va_start(ap, format);
  vsnprintf(msg, sizeof(msg), format, ap);
  va_end(ap);

  plugin_log(level, "%s plugin: %s", name, msg);
} /* void daemon_log */

int parse_log_severity(const char *severity) {
  int log_level = -1;

  if ((0 == strcasecmp(severity, "emerg")) ||
      (0 == strcasecmp(severity, "alert")) ||
      (0 == strcasecmp(severity, "crit")) || (0 == strcasecmp(severity, "err")))
    log_level = LOG_ERR;
  else if (0 == strcasecmp(severity, "warning"))
    log_level = LOG_WARNING;
  else if (0 == strcasecmp(severity, "notice"))
    log_level = LOG_NOTICE;
  else if (0 == strcasecmp(severity, "info"))
    log_level = LOG_INFO;
#if COLLECT_DEBUG
  else if (0 == strcasecmp(severity, "debug"))
    log_level = LOG_DEBUG;
#endif /* COLLECT_DEBUG */

  return log_level;
} /* int parse_log_severity */

EXPORT int parse_notif_severity(const char *severity) {
  int notif_severity = -1;

  if (strcasecmp(severity, "FAILURE") == 0)
    notif_severity = NOTIF_FAILURE;
  else if (strcmp(severity, "OKAY") == 0)
    notif_severity = NOTIF_OKAY;
  else if ((strcmp(severity, "WARNING") == 0) ||
           (strcmp(severity, "WARN") == 0))
    notif_severity = NOTIF_WARNING;

  return notif_severity;
} /* int parse_notif_severity */

EXPORT const data_set_t *plugin_get_ds(const char *name) {
  data_set_t *ds;

  if (data_sets == NULL) {
    P_ERROR("plugin_get_ds: No data sets are defined yet.");
    return NULL;
  }

  if (c_avl_get(data_sets, name, (void *)&ds) != 0) {
    DEBUG("No such dataset registered: %s", name);
    return NULL;
  }

  return ds;
} /* data_set_t *plugin_get_ds */

static int plugin_notification_meta_add(notification_t *n, const char *name,
                                        enum notification_meta_type_e type,
                                        const void *value) {
  notification_meta_t *meta;
  notification_meta_t *tail;

  if ((n == NULL) || (name == NULL) || (value == NULL)) {
    ERROR("plugin_notification_meta_add: A pointer is NULL!");
    return -1;
  }

  meta = calloc(1, sizeof(*meta));
  if (meta == NULL) {
    ERROR("plugin_notification_meta_add: calloc failed.");
    return -1;
  }

  sstrncpy(meta->name, name, sizeof(meta->name));
  meta->type = type;

  switch (type) {
  case NM_TYPE_STRING: {
    meta->nm_value.nm_string = strdup((const char *)value);
    if (meta->nm_value.nm_string == NULL) {
      ERROR("plugin_notification_meta_add: strdup failed.");
      sfree(meta);
      return -1;
    }
    break;
  }
  case NM_TYPE_SIGNED_INT: {
    meta->nm_value.nm_signed_int = *((int64_t *)value);
    break;
  }
  case NM_TYPE_UNSIGNED_INT: {
    meta->nm_value.nm_unsigned_int = *((uint64_t *)value);
    break;
  }
  case NM_TYPE_DOUBLE: {
    meta->nm_value.nm_double = *((double *)value);
    break;
  }
  case NM_TYPE_BOOLEAN: {
    meta->nm_value.nm_boolean = *((bool *)value);
    break;
  }
  default: {
    ERROR("plugin_notification_meta_add: Unknown type: %i", type);
    sfree(meta);
    return -1;
  }
  } /* switch (type) */

  meta->next = NULL;
  tail = n->meta;
  while ((tail != NULL) && (tail->next != NULL))
    tail = tail->next;

  if (tail == NULL)
    n->meta = meta;
  else
    tail->next = meta;

  return 0;
} /* int plugin_notification_meta_add */

int plugin_notification_meta_add_string(notification_t *n, const char *name,
                                        const char *value) {
  return plugin_notification_meta_add(n, name, NM_TYPE_STRING, value);
}

int plugin_notification_meta_add_signed_int(notification_t *n, const char *name,
                                            int64_t value) {
  return plugin_notification_meta_add(n, name, NM_TYPE_SIGNED_INT, &value);
}

int plugin_notification_meta_add_unsigned_int(notification_t *n,
                                              const char *name,
                                              uint64_t value) {
  return plugin_notification_meta_add(n, name, NM_TYPE_UNSIGNED_INT, &value);
}

int plugin_notification_meta_add_double(notification_t *n, const char *name,
                                        double value) {
  return plugin_notification_meta_add(n, name, NM_TYPE_DOUBLE, &value);
}

int plugin_notification_meta_add_boolean(notification_t *n, const char *name,
                                         bool value) {
  return plugin_notification_meta_add(n, name, NM_TYPE_BOOLEAN, &value);
}

int plugin_notification_meta_copy(notification_t *dst,
                                  const notification_t *src) {
  assert(dst != NULL);
  assert(src != NULL);
  assert(dst != src);
  assert((src->meta == NULL) || (src->meta != dst->meta));

  for (notification_meta_t *meta = src->meta; meta != NULL; meta = meta->next) {
    if (meta->type == NM_TYPE_STRING)
      plugin_notification_meta_add_string(dst, meta->name,
                                          meta->nm_value.nm_string);
    else if (meta->type == NM_TYPE_SIGNED_INT)
      plugin_notification_meta_add_signed_int(dst, meta->name,
                                              meta->nm_value.nm_signed_int);
    else if (meta->type == NM_TYPE_UNSIGNED_INT)
      plugin_notification_meta_add_unsigned_int(dst, meta->name,
                                                meta->nm_value.nm_unsigned_int);
    else if (meta->type == NM_TYPE_DOUBLE)
      plugin_notification_meta_add_double(dst, meta->name,
                                          meta->nm_value.nm_double);
    else if (meta->type == NM_TYPE_BOOLEAN)
      plugin_notification_meta_add_boolean(dst, meta->name,
                                           meta->nm_value.nm_boolean);
  }

  return 0;
} /* int plugin_notification_meta_copy */

int plugin_notification_meta_free(notification_meta_t *n) {
  notification_meta_t *this;
  notification_meta_t *next;

  if (n == NULL) {
    ERROR("plugin_notification_meta_free: n == NULL!");
    return -1;
  }

  this = n;
  while (this != NULL) {
    next = this->next;

    if (this->type == NM_TYPE_STRING) {
      /* Assign to a temporary variable to work around nm_string's const
       * modifier. */
      void *tmp = (void *)this->nm_value.nm_string;

      sfree(tmp);
      this->nm_value.nm_string = NULL;
    }
    sfree(this);

    this = next;
  }

  return 0;
} /* int plugin_notification_meta_free */

static void plugin_ctx_destructor(void *ctx) {
  sfree(ctx);
} /* void plugin_ctx_destructor */

static plugin_ctx_t ctx_init = {/* interval = */ 0};

static plugin_ctx_t *plugin_ctx_create(void) {
  plugin_ctx_t *ctx;

  ctx = malloc(sizeof(*ctx));
  if (ctx == NULL) {
    ERROR("Failed to allocate plugin context: %s", STRERRNO);
    return NULL;
  }

  *ctx = ctx_init;
  assert(plugin_ctx_key_initialized);
  pthread_setspecific(plugin_ctx_key, ctx);
  DEBUG("Created new plugin context.");
  return ctx;
} /* int plugin_ctx_create */

EXPORT void plugin_init_ctx(void) {
  pthread_key_create(&plugin_ctx_key, plugin_ctx_destructor);
  plugin_ctx_key_initialized = true;
} /* void plugin_init_ctx */

EXPORT plugin_ctx_t plugin_get_ctx(void) {
  plugin_ctx_t *ctx;

  assert(plugin_ctx_key_initialized);
  ctx = pthread_getspecific(plugin_ctx_key);

  if (ctx == NULL) {
    ctx = plugin_ctx_create();
    /* this must no happen -- exit() instead? */
    if (ctx == NULL)
      return ctx_init;
  }

  return *ctx;
} /* plugin_ctx_t plugin_get_ctx */

EXPORT plugin_ctx_t plugin_set_ctx(plugin_ctx_t ctx) {
  plugin_ctx_t *c;
  plugin_ctx_t old;

  assert(plugin_ctx_key_initialized);
  c = pthread_getspecific(plugin_ctx_key);

  if (c == NULL) {
    c = plugin_ctx_create();
    /* this must no happen -- exit() instead? */
    if (c == NULL)
      return ctx_init;
  }

  old = *c;
  *c = ctx;

  return old;
} /* void plugin_set_ctx */

EXPORT cdtime_t plugin_get_interval(void) {
  cdtime_t interval;

  interval = plugin_get_ctx().interval;
  if (interval > 0)
    return interval;

  P_ERROR("plugin_get_interval: Unable to determine Interval from context.");

  return cf_get_default_interval();
} /* cdtime_t plugin_get_interval */

typedef struct {
  plugin_ctx_t ctx;
  void *(*start_routine)(void *);
  void *arg;
} plugin_thread_t;

static void *plugin_thread_start(void *arg) {
  plugin_thread_t *plugin_thread = arg;

  void *(*start_routine)(void *) = plugin_thread->start_routine;
  void *plugin_arg = plugin_thread->arg;

  plugin_set_ctx(plugin_thread->ctx);

  sfree(plugin_thread);

  return start_routine(plugin_arg);
} /* void *plugin_thread_start */

int plugin_thread_create(pthread_t *thread, void *(*start_routine)(void *),
                         void *arg, char const *name) {
  plugin_thread_t *plugin_thread;

  plugin_thread = malloc(sizeof(*plugin_thread));
  if (plugin_thread == NULL)
    return ENOMEM;

  plugin_thread->ctx = plugin_get_ctx();
  plugin_thread->start_routine = start_routine;
  plugin_thread->arg = arg;

  int ret = pthread_create(thread, NULL, plugin_thread_start, plugin_thread);
  if (ret != 0) {
    sfree(plugin_thread);
    return ret;
  }

  if (name != NULL)
    set_thread_name(*thread, name);

  return 0;
} /* int plugin_thread_create */
