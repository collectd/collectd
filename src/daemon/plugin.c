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
 **/

/* _GNU_SOURCE is needed in Linux to use pthread_setname_np */
#define _GNU_SOURCE

#include "collectd.h"

#include "common.h"
#include "configfile.h"
#include "filter_chain.h"
#include "plugin.h"
#include "utils_avltree.h"
#include "utils_cache.h"
#include "utils_complain.h"
#include "utils_heap.h"
#include "utils_llist.h"
#include "utils_random.h"
#include "utils_time.h"

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

struct write_queue_s;
typedef struct write_queue_s write_queue_t;
struct write_queue_s {
  value_list_t *vl;
  plugin_ctx_t ctx;
  write_queue_t *next;
};

struct flush_callback_s {
  char *name;
  cdtime_t timeout;
};
typedef struct flush_callback_s flush_callback_t;

/*
 * Private variables
 */
static c_avl_tree_t *plugins_loaded = NULL;

static llist_t *list_init;
static llist_t *list_write;
static llist_t *list_flush;
static llist_t *list_missing;
static llist_t *list_shutdown;
static llist_t *list_log;
static llist_t *list_notification;

static fc_chain_t *pre_cache_chain = NULL;
static fc_chain_t *post_cache_chain = NULL;

static c_avl_tree_t *data_sets;

static char *plugindir = NULL;

#ifndef DEFAULT_MAX_READ_INTERVAL
#define DEFAULT_MAX_READ_INTERVAL TIME_T_TO_CDTIME_T_STATIC(86400)
#endif
static c_heap_t *read_heap = NULL;
static llist_t *read_list;
static int read_loop = 1;
static pthread_mutex_t read_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t read_cond = PTHREAD_COND_INITIALIZER;
static pthread_t *read_threads = NULL;
static size_t read_threads_num = 0;
static cdtime_t max_read_interval = DEFAULT_MAX_READ_INTERVAL;

static write_queue_t *write_queue_head;
static write_queue_t *write_queue_tail;
static long write_queue_length = 0;
static _Bool write_loop = 1;
static pthread_mutex_t write_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t write_cond = PTHREAD_COND_INITIALIZER;
static pthread_t *write_threads = NULL;
static size_t write_threads_num = 0;

static pthread_key_t plugin_ctx_key;
static _Bool plugin_ctx_key_initialized = 0;

static long write_limit_high = 0;
static long write_limit_low = 0;

static derive_t stats_values_dropped = 0;
static _Bool record_statistics = 0;

/*
 * Static functions
 */
static int plugin_dispatch_values_internal(value_list_t *vl);

static const char *plugin_get_dir(void) {
  if (plugindir == NULL)
    return PLUGINDIR;
  else
    return plugindir;
}

static int plugin_update_internal_statistics(void) { /* {{{ */
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
  vl.values = &(value_t){.gauge = (gauge_t)stats_values_dropped};
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
  llentry_t *le;
  char *key;

  if (*list == NULL) {
    *list = llist_create();
    if (*list == NULL) {
      ERROR("plugin: register_callback: "
            "llist_create failed.");
      destroy_callback(cf);
      return -1;
    }
  }

  key = strdup(name);
  if (key == NULL) {
    ERROR("plugin: register_callback: strdup failed.");
    destroy_callback(cf);
    return -1;
  }

  le = llist_search(*list, name);
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
    callback_func_t *old_cf;

    old_cf = le->value;
    le->value = cf;

    WARNING("plugin: register_callback: "
            "a callback named `%s' already exists - "
            "overwriting the old entry!",
            name);

    destroy_callback(old_cf);
    sfree(key);
  }

  return 0;
} /* }}} int register_callback */

static void log_list_callbacks(llist_t **list, /* {{{ */
                               const char *comment) {
  char *str;
  int len;
  int i;
  llentry_t *le;
  int n;
  char **keys;

  n = llist_size(*list);
  if (n == 0) {
    INFO("%s [none]", comment);
    return;
  }

  keys = calloc(n, sizeof(char *));

  if (keys == NULL) {
    ERROR("%s: failed to allocate memory for list of callbacks", comment);

    return;
  }

  for (le = llist_head(*list), i = 0, len = 0; le != NULL; le = le->next, i++) {
    keys[i] = le->key;
    len += strlen(le->key) + 6;
  }
  str = malloc(len + 10);
  if (str == NULL) {
    ERROR("%s: failed to allocate memory for list of callbacks", comment);
  } else {
    *str = '\0';
    strjoin(str, len, keys, n, "', '");
    INFO("%s ['%s']", comment, str);
    sfree(str);
  }
  sfree(keys);
} /* }}} void log_list_callbacks */

static int create_register_callback(llist_t **list, /* {{{ */
                                    const char *name, void *callback,
                                    user_data_t const *ud) {
  callback_func_t *cf;

  cf = calloc(1, sizeof(*cf));
  if (cf == NULL) {
    free_userdata(ud);
    ERROR("plugin: create_register_callback: calloc failed.");
    return -1;
  }

  cf->cf_callback = callback;
  if (ud == NULL) {
    cf->cf_udata.data = NULL;
    cf->cf_udata.free_func = NULL;
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
static int plugin_load_file(char const *file, _Bool global) {
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

  void (*reg_handle)(void) = dlsym(dlh, "module_register");
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
      int (*callback)(void);

      callback = rf->rf_callback;
      status = (*callback)();
    } else {
      plugin_read_cb callback;

      assert(rf_type == RF_COMPLEX);

      callback = rf->rf_callback;
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

  read_threads = (pthread_t *)calloc(num, sizeof(pthread_t));
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
    snprintf(name, sizeof(name), "reader#%zu", read_threads_num);
    set_thread_name(read_threads[read_threads_num], name);

    read_threads_num++;
  } /* for (i) */
} /* }}} void start_read_threads */

static void stop_read_threads(void) {
  if (read_threads == NULL)
    return;

  INFO("collectd: Stopping %zu read threads.", read_threads_num);

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
  if (vl->interval == 0) {
    plugin_ctx_t ctx = plugin_get_ctx();

    if (ctx.interval != 0)
      vl->interval = ctx.interval;
    else {
      char name[6 * DATA_MAX_NAME_LEN];
      FORMAT_VL(name, sizeof(name), vl);
      ERROR("plugin_value_list_clone: Unable to determine "
            "interval from context for "
            "value list \"%s\". "
            "This indicates a broken plugin. "
            "Please report this problem to the "
            "collectd mailing list or at "
            "<http://collectd.org/bugs/>.",
            name);
      vl->interval = cf_get_default_interval();
    }
  }

  return vl;
} /* }}} value_list_t *plugin_value_list_clone */

static int plugin_write_enqueue(value_list_t const *vl) /* {{{ */
{
  write_queue_t *q;

  q = malloc(sizeof(*q));
  if (q == NULL)
    return ENOMEM;
  q->next = NULL;

  q->vl = plugin_value_list_clone(vl);
  if (q->vl == NULL) {
    sfree(q);
    return ENOMEM;
  }

  /* Store context of caller (read plugin); otherwise, it would not be
   * available to the write plugins when actually dispatching the
   * value-list later on. */
  q->ctx = plugin_get_ctx();

  pthread_mutex_lock(&write_lock);

  if (write_queue_tail == NULL) {
    write_queue_head = q;
    write_queue_tail = q;
    write_queue_length = 1;
  } else {
    write_queue_tail->next = q;
    write_queue_tail = q;
    write_queue_length += 1;
  }

  pthread_cond_signal(&write_cond);
  pthread_mutex_unlock(&write_lock);

  return 0;
} /* }}} int plugin_write_enqueue */

static value_list_t *plugin_write_dequeue(void) /* {{{ */
{
  write_queue_t *q;
  value_list_t *vl;

  pthread_mutex_lock(&write_lock);

  while (write_loop && (write_queue_head == NULL))
    pthread_cond_wait(&write_cond, &write_lock);

  if (write_queue_head == NULL) {
    pthread_mutex_unlock(&write_lock);
    return NULL;
  }

  q = write_queue_head;
  write_queue_head = q->next;
  write_queue_length -= 1;
  if (write_queue_head == NULL) {
    write_queue_tail = NULL;
    assert(0 == write_queue_length);
  }

  pthread_mutex_unlock(&write_lock);

  (void)plugin_set_ctx(q->ctx);

  vl = q->vl;
  sfree(q);
  return vl;
} /* }}} value_list_t *plugin_write_dequeue */

static void *plugin_write_thread(void __attribute__((unused)) * args) /* {{{ */
{
  while (write_loop) {
    value_list_t *vl = plugin_write_dequeue();
    if (vl == NULL)
      continue;

    plugin_dispatch_values_internal(vl);

    plugin_value_list_free(vl);
  }

  pthread_exit(NULL);
  return (void *)0;
} /* }}} void *plugin_write_thread */

static void start_write_threads(size_t num) /* {{{ */
{
  if (write_threads != NULL)
    return;

  write_threads = (pthread_t *)calloc(num, sizeof(pthread_t));
  if (write_threads == NULL) {
    ERROR("plugin: start_write_threads: calloc failed.");
    return;
  }

  write_threads_num = 0;
  for (size_t i = 0; i < num; i++) {
    int status = pthread_create(write_threads + write_threads_num,
                                /* attr = */ NULL, plugin_write_thread,
                                /* arg = */ NULL);
    if (status != 0) {
      ERROR("plugin: start_write_threads: pthread_create failed with status %i "
            "(%s).",
            status, STRERROR(status));
      return;
    }

    char name[THREAD_NAME_MAX];
    snprintf(name, sizeof(name), "writer#%zu", write_threads_num);
    set_thread_name(write_threads[write_threads_num], name);

    write_threads_num++;
  } /* for (i) */
} /* }}} void start_write_threads */

static void stop_write_threads(void) /* {{{ */
{
  write_queue_t *q;
  size_t i;

  if (write_threads == NULL)
    return;

  INFO("collectd: Stopping %zu write threads.", write_threads_num);

  pthread_mutex_lock(&write_lock);
  write_loop = 0;
  DEBUG("plugin: stop_write_threads: Signalling `write_cond'");
  pthread_cond_broadcast(&write_cond);
  pthread_mutex_unlock(&write_lock);

  for (i = 0; i < write_threads_num; i++) {
    if (pthread_join(write_threads[i], NULL) != 0) {
      ERROR("plugin: stop_write_threads: pthread_join failed.");
    }
    write_threads[i] = (pthread_t)0;
  }
  sfree(write_threads);
  write_threads_num = 0;

  pthread_mutex_lock(&write_lock);
  i = 0;
  for (q = write_queue_head; q != NULL;) {
    write_queue_t *q1 = q;
    plugin_value_list_free(q->vl);
    q = q->next;
    sfree(q1);
    i++;
  }
  write_queue_head = NULL;
  write_queue_tail = NULL;
  write_queue_length = 0;
  pthread_mutex_unlock(&write_lock);

  if (i > 0) {
    WARNING("plugin: %zu value list%s left after shutting down "
            "the write threads.",
            i, (i == 1) ? " was" : "s were");
  }
} /* }}} void stop_write_threads */

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

static _Bool plugin_is_loaded(char const *name) {
  int status;

  if (plugins_loaded == NULL)
    plugins_loaded =
        c_avl_create((int (*)(const void *, const void *))strcasecmp);
  assert(plugins_loaded != NULL);

  status = c_avl_get(plugins_loaded, name, /* ret_value = */ NULL);
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
int plugin_load(char const *plugin_name, _Bool global) {
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
    global = 1;

  /* `cpu' should not match `cpufreq'. To solve this we add `.so' to the
   * type when matching the filename */
  status = snprintf(typename, sizeof(typename), "%s.so", plugin_name);
  if ((status < 0) || ((size_t)status >= sizeof(typename))) {
    WARNING("plugin_load: Filename too long: \"%s.so\"", plugin_name);
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
int plugin_register_config(const char *name,
                           int (*callback)(const char *key, const char *val),
                           const char **keys, int keys_num) {
  cf_register(name, callback, keys, keys_num);
  return 0;
} /* int plugin_register_config */

int plugin_register_complex_config(const char *type,
                                   int (*callback)(oconfig_item_t *)) {
  return cf_register_complex(type, callback);
} /* int plugin_register_complex_config */

int plugin_register_init(const char *name, int (*callback)(void)) {
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
    WARNING("The read function \"%s\" is already registered. "
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

int plugin_register_read(const char *name, int (*callback)(void)) {
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

  status = plugin_insert_read(rf);
  if (status != 0) {
    sfree(rf->rf_name);
    sfree(rf);
  }

  return status;
} /* int plugin_register_read */

int plugin_register_complex_read(const char *group, const char *name,
                                 plugin_read_cb callback, cdtime_t interval,
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

  status = plugin_insert_read(rf);
  if (status != 0) {
    free_userdata(&rf->rf_udata);
    sfree(rf->rf_name);
    sfree(rf);
  }

  return status;
} /* int plugin_register_complex_read */

int plugin_register_write(const char *name, plugin_write_cb callback,
                          user_data_t const *ud) {
  return create_register_callback(&list_write, name, (void *)callback, ud);
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

int plugin_register_flush(const char *name, plugin_flush_cb callback,
                          user_data_t const *ud) {
  int status;
  plugin_ctx_t ctx = plugin_get_ctx();

  status = create_register_callback(&list_flush, name, (void *)callback, ud);
  if (status != 0)
    return status;

  if (ctx.flush_interval != 0) {
    char *flush_name;
    flush_callback_t *cb;

    flush_name = plugin_flush_callback_name(name);
    if (flush_name == NULL)
      return -1;

    cb = malloc(sizeof(*cb));
    if (cb == NULL) {
      ERROR("plugin_register_flush: malloc failed.");
      sfree(flush_name);
      return -1;
    }

    cb->name = strdup(name);
    if (cb->name == NULL) {
      ERROR("plugin_register_flush: strdup failed.");
      sfree(cb);
      sfree(flush_name);
      return -1;
    }
    cb->timeout = ctx.flush_timeout;

    status = plugin_register_complex_read(
        /* group     = */ "flush",
        /* name      = */ flush_name,
        /* callback  = */ plugin_flush_timeout_callback,
        /* interval  = */ ctx.flush_interval,
        /* user data = */ &(user_data_t){
            .data = cb, .free_func = plugin_flush_timeout_callback_free,
        });

    sfree(flush_name);
    return status;
  }

  return 0;
} /* int plugin_register_flush */

int plugin_register_missing(const char *name, plugin_missing_cb callback,
                            user_data_t const *ud) {
  return create_register_callback(&list_missing, name, (void *)callback, ud);
} /* int plugin_register_missing */

int plugin_register_shutdown(const char *name, int (*callback)(void)) {
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

int plugin_register_data_set(const data_set_t *ds) {
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

int plugin_register_log(const char *name, plugin_log_cb callback,
                        user_data_t const *ud) {
  return create_register_callback(&list_log, name, (void *)callback, ud);
} /* int plugin_register_log */

int plugin_register_notification(const char *name,
                                 plugin_notification_cb callback,
                                 user_data_t const *ud) {
  return create_register_callback(&list_notification, name, (void *)callback,
                                  ud);
} /* int plugin_register_log */

int plugin_unregister_config(const char *name) {
  cf_unregister(name);
  return 0;
} /* int plugin_unregister_config */

int plugin_unregister_complex_config(const char *name) {
  cf_unregister_complex(name);
  return 0;
} /* int plugin_unregister_complex_config */

int plugin_unregister_init(const char *name) {
  return plugin_unregister(list_init, name);
}

int plugin_unregister_read(const char *name) /* {{{ */
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

void plugin_log_available_writers(void) {
  log_list_callbacks(&list_write, "Available write targets:");
}

static int compare_read_func_group(llentry_t *e, void *ud) /* {{{ */
{
  read_func_t *rf = e->value;
  char *group = ud;

  return strcmp(rf->rf_group, (const char *)group);
} /* }}} int compare_read_func_group */

int plugin_unregister_read_group(const char *group) /* {{{ */
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

int plugin_unregister_write(const char *name) {
  return plugin_unregister(list_write, name);
}

int plugin_unregister_flush(const char *name) {
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

int plugin_unregister_missing(const char *name) {
  return plugin_unregister(list_missing, name);
}

int plugin_unregister_shutdown(const char *name) {
  return plugin_unregister(list_shutdown, name);
}

int plugin_unregister_data_set(const char *name) {
  data_set_t *ds;

  if (data_sets == NULL)
    return -1;

  if (c_avl_remove(data_sets, name, NULL, (void *)&ds) != 0)
    return -1;

  sfree(ds->ds);
  sfree(ds);

  return 0;
} /* int plugin_unregister_data_set */

int plugin_unregister_log(const char *name) {
  return plugin_unregister(list_log, name);
}

int plugin_unregister_notification(const char *name) {
  return plugin_unregister(list_notification, name);
}

int plugin_init_all(void) {
  char const *chain_name;
  llentry_t *le;
  int status;
  int ret = 0;

  /* Init the value cache */
  uc_init();

  if (IS_TRUE(global_option_get("CollectInternalStats"))) {
    record_statistics = 1;
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

  write_threads_num = global_option_get_long("WriteThreads",
                                             /* default = */ 5);
  if (write_threads_num < 1) {
    ERROR("WriteThreads must be positive.");
    write_threads_num = 5;
  }

  if ((list_init == NULL) && (read_heap == NULL))
    return ret;

  /* Calling all init callbacks before checking if read callbacks
   * are available allows the init callbacks to register the read
   * callback. */
  le = llist_head(list_init);
  while (le != NULL) {
    callback_func_t *cf;
    plugin_init_cb callback;
    plugin_ctx_t old_ctx;

    cf = le->value;
    old_ctx = plugin_set_ctx(cf->cf_ctx);
    callback = cf->cf_callback;
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

  start_write_threads((size_t)write_threads_num);

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
void plugin_read_all(void) {
  uc_check_timeout();

  return;
} /* void plugin_read_all */

/* Read function called when the `-T' command line argument is given. */
int plugin_read_all_once(void) {
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
      int (*callback)(void);

      callback = rf->rf_callback;
      status = (*callback)();
    } else {
      plugin_read_cb callback;

      callback = rf->rf_callback;
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

int plugin_write(const char *plugin, /* {{{ */
                 const data_set_t *ds, const value_list_t *vl) {
  llentry_t *le;
  int status;

  if (vl == NULL)
    return EINVAL;

  if (list_write == NULL)
    return ENOENT;

  if (ds == NULL) {
    ds = plugin_get_ds(vl->type);
    if (ds == NULL) {
      ERROR("plugin_write: Unable to lookup type `%s'.", vl->type);
      return ENOENT;
    }
  }

  if (plugin == NULL) {
    int success = 0;
    int failure = 0;

    le = llist_head(list_write);
    while (le != NULL) {
      callback_func_t *cf = le->value;
      plugin_write_cb callback;

      /* do not switch plugin context; rather keep the context (interval)
       * information of the calling read plugin */

      DEBUG("plugin: plugin_write: Writing values via %s.", le->key);
      callback = cf->cf_callback;
      status = (*callback)(ds, vl, &cf->cf_udata);
      if (status != 0)
        failure++;
      else
        success++;

      le = le->next;
    }

    if ((success == 0) && (failure != 0))
      status = -1;
    else
      status = 0;
  } else /* plugin != NULL */
  {
    callback_func_t *cf;
    plugin_write_cb callback;

    le = llist_head(list_write);
    while (le != NULL) {
      if (strcasecmp(plugin, le->key) == 0)
        break;

      le = le->next;
    }

    if (le == NULL)
      return ENOENT;

    cf = le->value;

    /* do not switch plugin context; rather keep the context (interval)
     * information of the calling read plugin */

    DEBUG("plugin: plugin_write: Writing values via %s.", le->key);
    callback = cf->cf_callback;
    status = (*callback)(ds, vl, &cf->cf_udata);
  }

  return status;
} /* }}} int plugin_write */

int plugin_flush(const char *plugin, cdtime_t timeout, const char *identifier) {
  llentry_t *le;

  if (list_flush == NULL)
    return 0;

  le = llist_head(list_flush);
  while (le != NULL) {
    callback_func_t *cf;
    plugin_flush_cb callback;
    plugin_ctx_t old_ctx;

    if ((plugin != NULL) && (strcmp(plugin, le->key) != 0)) {
      le = le->next;
      continue;
    }

    cf = le->value;
    old_ctx = plugin_set_ctx(cf->cf_ctx);
    callback = cf->cf_callback;

    (*callback)(timeout, identifier, &cf->cf_udata);

    plugin_set_ctx(old_ctx);

    le = le->next;
  }
  return 0;
} /* int plugin_flush */

int plugin_shutdown_all(void) {
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
  stop_write_threads();

  /* ask all plugins to write out the state they kept. */
  plugin_flush(/* plugin = */ NULL,
               /* timeout = */ 0,
               /* identifier = */ NULL);

  le = NULL;
  if (list_shutdown != NULL)
    le = llist_head(list_shutdown);

  while (le != NULL) {
    callback_func_t *cf;
    plugin_shutdown_cb callback;
    plugin_ctx_t old_ctx;

    cf = le->value;
    old_ctx = plugin_set_ctx(cf->cf_ctx);
    callback = cf->cf_callback;

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
  destroy_all_callbacks(&list_write);

  destroy_all_callbacks(&list_notification);
  destroy_all_callbacks(&list_shutdown);
  destroy_all_callbacks(&list_log);

  plugin_free_loaded();
  plugin_free_data_sets();
  return ret;
} /* void plugin_shutdown_all */

int plugin_dispatch_missing(const value_list_t *vl) /* {{{ */
{
  if (list_missing == NULL)
    return 0;

  llentry_t *le = llist_head(list_missing);
  while (le != NULL) {
    callback_func_t *cf = le->value;
    plugin_ctx_t old_ctx = plugin_set_ctx(cf->cf_ctx);
    plugin_missing_cb callback = cf->cf_callback;

    int status = (*callback)(vl, &cf->cf_udata);
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

static int plugin_dispatch_values_internal(value_list_t *vl) {
  int status;
  static c_complain_t no_write_complaint = C_COMPLAIN_INIT_STATIC;

  _Bool free_meta_data = 0;

  assert(vl != NULL);

  /* These fields are initialized by plugin_value_list_clone() if needed: */
  assert(vl->host[0] != 0);
  assert(vl->time != 0); /* The time is determined at _enqueue_ time. */
  assert(vl->interval != 0);

  if (vl->type[0] == 0 || vl->values == NULL || vl->values_len < 1) {
    ERROR("plugin_dispatch_values: Invalid value list "
          "from plugin %s.",
          vl->plugin);
    return -1;
  }

  /* Free meta data only if the calling function didn't specify any. In
   * this case matches and targets may add some and the calling function
   * may not expect (and therefore free) that data. */
  if (vl->meta == NULL)
    free_meta_data = 1;

  if (list_write == NULL)
    c_complain_once(LOG_WARNING, &no_write_complaint,
                    "plugin_dispatch_values: No write callback has been "
                    "registered. Please load at least one output plugin, "
                    "if you want the collected data to be stored.");

  if (data_sets == NULL) {
    ERROR("plugin_dispatch_values: No data sets registered. "
          "Could the types database be read? Check "
          "your `TypesDB' setting!");
    return -1;
  }

  data_set_t *ds = NULL;
  if (c_avl_get(data_sets, vl->type, (void *)&ds) != 0) {
    char ident[6 * DATA_MAX_NAME_LEN];

    FORMAT_VL(ident, sizeof(ident), vl);
    INFO("plugin_dispatch_values: Dataset not found: %s "
         "(from \"%s\"), check your types.db!",
         vl->type, ident);
    return -1;
  }

  DEBUG("plugin_dispatch_values: time = %.3f; interval = %.3f; "
        "host = %s; "
        "plugin = %s; plugin_instance = %s; "
        "type = %s; type_instance = %s;",
        CDTIME_T_TO_DOUBLE(vl->time), CDTIME_T_TO_DOUBLE(vl->interval),
        vl->host, vl->plugin, vl->plugin_instance, vl->type, vl->type_instance);

#if COLLECT_DEBUG
  assert(0 == strcmp(ds->type, vl->type));
#else
  if (0 != strcmp(ds->type, vl->type))
    WARNING("plugin_dispatch_values: (ds->type = %s) != (vl->type = %s)",
            ds->type, vl->type);
#endif

#if COLLECT_DEBUG
  assert(ds->ds_num == vl->values_len);
#else
  if (ds->ds_num != vl->values_len) {
    ERROR("plugin_dispatch_values: ds->type = %s: "
          "(ds->ds_num = %zu) != "
          "(vl->values_len = %zu)",
          ds->type, ds->ds_num, vl->values_len);
    return -1;
  }
#endif

  escape_slashes(vl->host, sizeof(vl->host));
  escape_slashes(vl->plugin, sizeof(vl->plugin));
  escape_slashes(vl->plugin_instance, sizeof(vl->plugin_instance));
  escape_slashes(vl->type, sizeof(vl->type));
  escape_slashes(vl->type_instance, sizeof(vl->type_instance));

  if (pre_cache_chain != NULL) {
    status = fc_process_chain(ds, vl, pre_cache_chain);
    if (status < 0) {
      WARNING("plugin_dispatch_values: Running the "
              "pre-cache chain failed with "
              "status %i (%#x).",
              status, status);
    } else if (status == FC_TARGET_STOP)
      return 0;
  }

  /* Update the value cache */
  uc_update(ds, vl);

  if (post_cache_chain != NULL) {
    status = fc_process_chain(ds, vl, post_cache_chain);
    if (status < 0) {
      WARNING("plugin_dispatch_values: Running the "
              "post-cache chain failed with "
              "status %i (%#x).",
              status, status);
    }
  } else
    fc_default_action(ds, vl);

  if ((free_meta_data != 0) && (vl->meta != NULL)) {
    meta_data_destroy(vl->meta);
    vl->meta = NULL;
  }

  return 0;
} /* int plugin_dispatch_values_internal */

static double get_drop_probability(void) /* {{{ */
{
  long pos;
  long size;
  long wql;

  pthread_mutex_lock(&write_lock);
  wql = write_queue_length;
  pthread_mutex_unlock(&write_lock);

  if (wql < write_limit_low)
    return 0.0;
  if (wql >= write_limit_high)
    return 1.0;

  pos = 1 + wql - write_limit_low;
  size = 1 + write_limit_high - write_limit_low;

  return (double)pos / (double)size;
} /* }}} double get_drop_probability */

static _Bool check_drop_value(void) /* {{{ */
{
  static cdtime_t last_message_time = 0;
  static pthread_mutex_t last_message_lock = PTHREAD_MUTEX_INITIALIZER;

  double p;
  double q;
  int status;

  if (write_limit_high == 0)
    return 0;

  p = get_drop_probability();
  if (p == 0.0)
    return 0;

  status = pthread_mutex_trylock(&last_message_lock);
  if (status == 0) {
    cdtime_t now;

    now = cdtime();
    if ((now - last_message_time) > TIME_T_TO_CDTIME_T(1)) {
      last_message_time = now;
      ERROR("plugin_dispatch_values: Low water mark "
            "reached. Dropping %.0f%% of metrics.",
            100.0 * p);
    }
    pthread_mutex_unlock(&last_message_lock);
  }

  if (p == 1.0)
    return 1;

  q = cdrand_d();
  if (q > p)
    return 1;
  else
    return 0;
} /* }}} _Bool check_drop_value */

int plugin_dispatch_values(value_list_t const *vl) {
  int status;
  static pthread_mutex_t statistics_lock = PTHREAD_MUTEX_INITIALIZER;

  if (check_drop_value()) {
    if (record_statistics) {
      pthread_mutex_lock(&statistics_lock);
      stats_values_dropped++;
      pthread_mutex_unlock(&statistics_lock);
    }
    return 0;
  }

  status = plugin_write_enqueue(vl);
  if (status != 0) {
    ERROR("plugin_dispatch_values: plugin_write_enqueue failed with status %i "
          "(%s).",
          status, STRERROR(status));
    return status;
  }

  return 0;
}

__attribute__((sentinel)) int
plugin_dispatch_multivalue(value_list_t const *template, /* {{{ */
                           _Bool store_percentage, int store_type, ...) {
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
    case DS_TYPE_ABSOLUTE:
      vl->values[0].absolute = va_arg(ap, absolute_t);
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

    status = plugin_write_enqueue(vl);
    if (status != 0)
      failed++;
  }
  va_end(ap);

  plugin_value_list_free(vl);
  return failed;
} /* }}} int plugin_dispatch_multivalue */

int plugin_dispatch_notification(const notification_t *notif) {
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

void plugin_log(int level, const char *format, ...) {
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
    callback_func_t *cf;
    plugin_log_cb callback;

    cf = le->value;
    callback = cf->cf_callback;

    /* do not switch plugin context; rather keep the context
     * (interval) information of the calling plugin */

    (*callback)(level, msg, &cf->cf_udata);

    le = le->next;
  }
} /* void plugin_log */

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

int parse_notif_severity(const char *severity) {
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

const data_set_t *plugin_get_ds(const char *name) {
  data_set_t *ds;

  if (data_sets == NULL) {
    ERROR("plugin_get_ds: No data sets are defined yet.");
    return NULL;
  }

  if (c_avl_get(data_sets, name, (void *)&ds) != 0) {
    DEBUG("No such dataset registered: %s", name);
    return NULL;
  }

  return ds;
} /* data_set_t *plugin_get_ds */

/*
 A semantic distinction in the signature names that follow:

 "add" functions place the metadata object at the end of the
 linked list of metadata objects belonging to the notification.

 "append" functions place the metadata object at the end of the
 linked list of metadata objects nested within another metadata
 object of NM_TYPE_NESTED.
*/

static int plugin_notification_meta_append(notification_t *n,
                                           notification_meta_t *m,
                                           const char *name,
                                           enum notification_meta_type_e type,
                                           const void *value) {
  // If n is passed and is not NULL, this metadata object will be append to the
  // end of n's linked list of metadata objects.
  //
  // If m is passed and is not NULL, and m is of type NM_TYPE_NESTED, then this
  // metadata object will either be m's nm_value or appended to the end of the
  // linked list starting with nm_value.  If m is not of type NM_TYPE_NESTED,
  // this metadata object will be append to the end of the linked list of which
  // m is a member.

  notification_meta_t *meta;
  notification_meta_t *tail;

  if ((n == NULL && m == NULL) || (name == NULL) ||
      (value == NULL && type != NM_TYPE_NESTED)) {
    ERROR("plugin_notification_meta_append: A pointer is NULL!");
    return -1;
  }

  if (n != NULL && m != NULL) {
    ERROR("plugin_notification_meta_append: Only a notification xor "
          "notification-meta object may be passed.");
    return -1;
  }

  meta = calloc(1, sizeof(*meta));
  if (meta == NULL) {
    ERROR("plugin_notification_meta_append: calloc failed.");
    return -1;
  }

  sstrncpy(meta->name, name, sizeof(meta->name));
  meta->type = type;

  switch (type) {
  case NM_TYPE_STRING: {
    meta->nm_value.nm_string = strdup((const char *)value);
    if (meta->nm_value.nm_string == NULL) {
      ERROR("plugin_notification_meta_append: strdup failed.");
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
    meta->nm_value.nm_boolean = *((_Bool *)value);
    break;
  }
  case NM_TYPE_NESTED: {
    // This nested object's associated value will be the first
    // of its nested children, with that first child being set by the
    // first metadata object appended to this nested object by
    // a later call to this function where this nested object is
    // passed as "m"
    meta->nm_value.nm_nested = NULL;
    break;
  }
  default: {
    ERROR("plugin_notification_meta_append: Unknown type: %i", type);
    sfree(meta);
    return -1;
  }
  } /* switch (type) */

  meta->next = NULL;

  tail = (m == NULL ? n->meta : m);

  if (m != NULL && tail != NULL && tail->type == NM_TYPE_NESTED) {
    // If the tail is a nested object and that nested object
    // has no associate value yet, set that associated value
    // to this meta object, as this meta object is the first
    // of the nested object's nested children.  Otherwise,
    // if the nested object already has a value, set the tail
    // to that metadata object value
    if (tail->nm_value.nm_nested == NULL) {
      tail->nm_value.nm_nested = meta;
      return 0;
    }

    tail = tail->nm_value.nm_nested;
  }

  while ((tail != NULL) && (tail->next != NULL))
    tail = tail->next;

  // tail will never be NULL if m was not NULL,
  // and if m is NULL then n is not NULL, so there is
  // no chance of n being NULL if tail is NULL
  if (tail == NULL)
    n->meta = meta;
  else
    tail->next = meta;

  return 0;
}

int plugin_notification_meta_append_string(notification_meta_t *m,
                                           const char *name,
                                           const char *value) {
  return plugin_notification_meta_append(NULL, m, name, NM_TYPE_STRING, value);
}

int plugin_notification_meta_add_string(notification_t *n, const char *name,
                                        const char *value) {
  return plugin_notification_meta_append(n, NULL, name, NM_TYPE_STRING, value);
}

int plugin_notification_meta_append_signed_int(notification_meta_t *m,
                                               const char *name,
                                               int64_t value) {
  return plugin_notification_meta_append(NULL, m, name, NM_TYPE_SIGNED_INT,
                                         &value);
}

int plugin_notification_meta_add_signed_int(notification_t *n, const char *name,
                                            int64_t value) {
  return plugin_notification_meta_append(n, NULL, name, NM_TYPE_SIGNED_INT,
                                         &value);
}

int plugin_notification_meta_append_unsigned_int(notification_meta_t *m,
                                                 const char *name,
                                                 uint64_t value) {
  return plugin_notification_meta_append(NULL, m, name, NM_TYPE_UNSIGNED_INT,
                                         &value);
}

int plugin_notification_meta_add_unsigned_int(notification_t *n,
                                              const char *name,
                                              uint64_t value) {
  return plugin_notification_meta_append(n, NULL, name, NM_TYPE_UNSIGNED_INT,
                                         &value);
}

int plugin_notification_meta_append_double(notification_meta_t *m,
                                           const char *name, double value) {
  return plugin_notification_meta_append(NULL, m, name, NM_TYPE_DOUBLE, &value);
}

int plugin_notification_meta_add_double(notification_t *n, const char *name,
                                        double value) {
  return plugin_notification_meta_append(n, NULL, name, NM_TYPE_DOUBLE, &value);
}

int plugin_notification_meta_append_boolean(notification_meta_t *m,
                                            const char *name, _Bool value) {
  return plugin_notification_meta_append(NULL, m, name, NM_TYPE_BOOLEAN,
                                         &value);
}

int plugin_notification_meta_add_boolean(notification_t *n, const char *name,
                                         _Bool value) {
  return plugin_notification_meta_append(n, NULL, name, NM_TYPE_BOOLEAN,
                                         &value);
}

int plugin_notification_meta_append_nested(notification_meta_t *m,
                                           const char *name) {
  return plugin_notification_meta_append(NULL, m, name, NM_TYPE_NESTED, NULL);
}

int plugin_notification_meta_add_nested(notification_t *n, const char *name) {
  return plugin_notification_meta_append(n, NULL, name, NM_TYPE_NESTED, NULL);
}

static int plugin_notification_meta_get_tail(notification_t *n,
                                             notification_meta_t *m,
                                             notification_meta_t **tail) {

  notification_meta_t *tmp_tail = NULL;

  if (n == NULL && m == NULL) {
    ERROR("plugin_notification_meta_get_tail: A pointer is NULL!");
    return -1;
  }

  if (n != NULL && m != NULL) {
    ERROR("plugin_notification_meta_get_tail: Only a notification xor "
          "notification-meta object may be passed.");
    return -1;
  }

  if (n != NULL)
    tmp_tail = n->meta;
  else {
    if (m->type != NM_TYPE_NESTED)
      tmp_tail = m->next;
    else
      tmp_tail = m->nm_value.nm_nested;
  }

  while ((tmp_tail != NULL) && (tmp_tail->next != NULL))
    tmp_tail = tmp_tail->next;

  *tail = tmp_tail;

  return 0;
}

int plugin_notification_meta_get_meta_tail(notification_t *n,
                                           notification_meta_t **tail) {
  return plugin_notification_meta_get_tail(n, NULL, tail);
}

int plugin_notification_meta_get_nested_tail(notification_meta_t *m,
                                             notification_meta_t **tail) {

  if (m == NULL) {
    ERROR("plugin_notification_meta_get_nested_tail: m is NULL!");
    return -1;
  }

  if (m->type != NM_TYPE_NESTED) {
    // If m isn't a nested metadata object, this function will
    // simply return the tail of m as opposed to the tail of
    // m's nested contents
    WARNING("plugin_notification_meta_get_nested_tail: metadata object '%s' is "
            "not of type NM_TYPE_NESTED.",
            m->name);
  }

  return plugin_notification_meta_get_tail(NULL, m, tail);
}

static int plugin_notification_meta_copy_nested(
    notification_t *dst, const notification_t *src,
    notification_meta_t *dst_nested, const notification_meta_t *src_nested) {
  assert(dst != NULL);
  assert(src != NULL);
  assert(dst != src);
  assert(src_nested != NULL);
  assert(src_nested != dst_nested);
  assert(src_nested->type == NM_TYPE_NESTED);
  assert(dst_nested == NULL || dst_nested->type == NM_TYPE_NESTED);

  notification_meta_t *nested;

  // If this function was passed a NULL dst_nested, then we're
  // dealing with a src_nested metadata object at the base level.
  // In this case, we can just add a nested metadata object to
  // the end of dst's metadata chain.
  //
  // On the other hand, if we received a non-NULL dst_nested
  // object, then this src_nested metadata object is itself a
  // child of a NM_TYPE_NESTED metadata object.  In this case,
  // we have to append a nested metadata object to the end of
  // dst_nested's metadata chain.

  if (dst_nested == NULL) {
    plugin_notification_meta_add_nested(dst, src_nested->name);
    plugin_notification_meta_get_meta_tail(dst, &nested);
  } else {
    plugin_notification_meta_append_nested(dst_nested, src_nested->name);
    plugin_notification_meta_get_nested_tail(dst_nested, &nested);
  }

  // Loop through src_nested's metadata chain (which will start
  // with its nm_nested value, if any).  If another
  // NM_TYPE_NESTED object is encountered, recurse and pass
  // the "nested" object created above as dst_nested.

  for (notification_meta_t *meta = src_nested->nm_value.nm_nested; meta != NULL;
       meta = meta->next) {
    if (meta->type == NM_TYPE_STRING)
      plugin_notification_meta_append_string(nested, meta->name,
                                             meta->nm_value.nm_string);
    else if (meta->type == NM_TYPE_SIGNED_INT)
      plugin_notification_meta_append_signed_int(nested, meta->name,
                                                 meta->nm_value.nm_signed_int);
    else if (meta->type == NM_TYPE_UNSIGNED_INT)
      plugin_notification_meta_append_unsigned_int(
          nested, meta->name, meta->nm_value.nm_unsigned_int);
    else if (meta->type == NM_TYPE_DOUBLE)
      plugin_notification_meta_append_double(nested, meta->name,
                                             meta->nm_value.nm_double);
    else if (meta->type == NM_TYPE_BOOLEAN)
      plugin_notification_meta_append_boolean(nested, meta->name,
                                              meta->nm_value.nm_boolean);
    else if (meta->type == NM_TYPE_NESTED)
      plugin_notification_meta_copy_nested(dst, src, nested, meta);
  }

  return 0;
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
    else if (meta->type == NM_TYPE_NESTED)
      // If this is a nested metadata object, call the nested copy
      // function, but pass dst_nested as NULL to indicate that this
      // object is nested at the base level.
      plugin_notification_meta_copy_nested(dst, src, NULL, meta);
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
    } else if (this->type == NM_TYPE_NESTED) {
      // Since the nm_value of this type is just a pointer to another
      // notification_meta_t, just recurse and pass that pointer (and
      // the value will be freed in that call)
      if (this->nm_value.nm_nested != NULL)
        plugin_notification_meta_free(this->nm_value.nm_nested);
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

void plugin_init_ctx(void) {
  pthread_key_create(&plugin_ctx_key, plugin_ctx_destructor);
  plugin_ctx_key_initialized = 1;
} /* void plugin_init_ctx */

plugin_ctx_t plugin_get_ctx(void) {
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

plugin_ctx_t plugin_set_ctx(plugin_ctx_t ctx) {
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

cdtime_t plugin_get_interval(void) {
  cdtime_t interval;

  interval = plugin_get_ctx().interval;
  if (interval > 0)
    return interval;

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

int plugin_thread_create(pthread_t *thread, const pthread_attr_t *attr,
                         void *(*start_routine)(void *), void *arg,
                         char const *name) {
  plugin_thread_t *plugin_thread;

  plugin_thread = malloc(sizeof(*plugin_thread));
  if (plugin_thread == NULL)
    return ENOMEM;

  plugin_thread->ctx = plugin_get_ctx();
  plugin_thread->start_routine = start_routine;
  plugin_thread->arg = arg;

  int ret = pthread_create(thread, attr, plugin_thread_start, plugin_thread);
  if (ret != 0) {
    sfree(plugin_thread);
    return ret;
  }

  if (name != NULL)
    set_thread_name(*thread, name);

  return 0;
} /* int plugin_thread_create */
