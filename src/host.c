/**
 * collectd - src/host.c
 * Copyright (C) 2019       Graham Leggett
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
 *   Graham Leggett <minfrin at sharp.fm>
 *
 **/

/*
 * This plugin keeps track of hosts that have been seen by collectd, and sends
 * notifications when a new host is seen, and when a host has not been seen.
 *
 */

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"

#include <lmdb.h>

#define PLUGIN_NAME "host"

#define DEFAULT_HOST_PATH "hosts"
#define DEFAULT_LOST_INTERVAL 60
#define DEFAULT_THREAD_INTERVAL 2
#define DEFAULT_DELAY_INTERVAL 60

static const char *config_keys[] = {"HOSTPATH", "LOSTINTERVAL", "DELAYINTERVAL"};
static int config_keys_num = STATIC_ARRAY_SIZE(config_keys);
static char *host_path = DEFAULT_HOST_PATH;
static int lost_interval = DEFAULT_LOST_INTERVAL;
static int delay_interval = DEFAULT_DELAY_INTERVAL;
static int thread_interval = DEFAULT_THREAD_INTERVAL;

static pthread_mutex_t host_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t host_cond = PTHREAD_COND_INITIALIZER;
static int host_thread_loop;
static pthread_t host_thread_id;

static MDB_env *env;

static void *host_thread(void *arg) /* {{{ */
{
  struct timeval tv_begin;
  struct timeval tv_now;
  struct timespec ts_wait;

  if (gettimeofday(&tv_begin, NULL) < 0) {
    ERROR("host plugin: gettimeofday failed: %s", STRERRNO);
    host_thread_loop = 0;
    return (void *)0;
  }

  pthread_mutex_lock(&host_lock);

  ts_wait.tv_sec = tv_begin.tv_sec + delay_interval + thread_interval;
  ts_wait.tv_nsec = 0;

  pthread_cond_timedwait(&host_cond, &host_lock, &ts_wait);

  while (host_thread_loop > 0) {
    MDB_txn *txn;
    MDB_dbi dbi;
    MDB_val key, data;
    MDB_cursor *cursor;
    int rc;

    if (gettimeofday(&tv_now, NULL) < 0) {
      ERROR("host plugin: gettimeofday failed: %s", STRERRNO);
      host_thread_loop = 0;
      break;
    }

    pthread_mutex_unlock(&host_lock);

    rc = mdb_txn_begin(env, NULL, 0, &txn);
    if (rc) {
      ERROR("mdb_txn_begin returned: %s (%d)", mdb_strerror(rc), rc);
      goto skip;
    }

    rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
    if (rc) {
      ERROR("mdb_dbi_open returned: %s (%d)", mdb_strerror(rc), rc);
      mdb_txn_abort(txn);
      goto skip;
    }

    rc = mdb_cursor_open(txn, dbi, &cursor);
    if (rc) {
      ERROR("mdb_cursor_open returned: %s (%d)", mdb_strerror(rc), rc);
      mdb_txn_abort(txn);
      goto skip;
    }

    while ((rc = mdb_cursor_get(cursor, &key, &data, MDB_NEXT)) == 0) {
      notification_t n;
      struct timeval *tv_then;
      const char *host;

      /* sanity check - remove any entries that have mismatched sizes,
       * this might happen after an upgrade.
       */

      if (key.mv_size != sizeof(n.host) ||
              data.mv_size != sizeof(struct timespec)) {

          rc = mdb_cursor_del(cursor, 0);
          if (rc) {
            ERROR("mdb_cursor_del returned: %s (%d)", mdb_strerror(rc), rc);
            mdb_cursor_close(cursor);
            mdb_txn_abort(txn);
            goto skip;
          }

          continue;
      }

      /* too old? if so, we've lost the host.
       *
       * also - stay silent for the first delay_interval after startup so we
       * don't send a flurry of stale alerts.
       *
       */

      host = key.mv_data;
      tv_then = data.mv_data;

      if (tv_now.tv_sec - tv_then->tv_sec >= lost_interval) {

        notification_t n;
        char message[NOTIF_MAX_MSG_LEN];

        rc = mdb_cursor_del(cursor, 0);
        if (rc) {
          ERROR("mdb_cursor_del returned: %s (%d)", mdb_strerror(rc), rc);
          mdb_cursor_close(cursor);
          mdb_txn_abort(txn);
          goto skip;
        }

        ssnprintf(message, sizeof(message), "Host not seen for %d seconds: %.*s",
                (int)(tv_now.tv_sec - tv_then->tv_sec), (int)sizeof(n.host), host);
        notification_init(&n, NOTIF_FAILURE, message, host, PLUGIN_NAME,
                          NULL, "host", "lost");
        n.time = cdtime();

        plugin_dispatch_notification(&n);

      }

    }
    mdb_cursor_close(cursor);

    rc = mdb_txn_commit(txn);
    if (rc) {
      ERROR("mdb_txn_commit returned: %s (%d)", mdb_strerror(rc), rc);
      goto skip;
    }

skip:
    pthread_mutex_lock(&host_lock);

    if (host_thread_loop <= 0)
      break;

    ts_wait.tv_sec = tv_now.tv_sec + thread_interval;
    ts_wait.tv_nsec = 0;

    pthread_cond_timedwait(&host_cond, &host_lock, &ts_wait);
    if (host_thread_loop <= 0)
      break;
  } /* while (host_thread_loop > 0) */

  pthread_mutex_unlock(&host_lock);

  return (void *)0;
} /* }}} void *host_thread */

static int start_thread(void) /* {{{ */
{
  int status;

  pthread_mutex_lock(&host_lock);

  if (host_thread_loop != 0) {
    pthread_mutex_unlock(&host_lock);
    return 0;
  }

  host_thread_loop = 1;
  status = plugin_thread_create(&host_thread_id, /* attr = */ NULL, host_thread,
                                /* arg = */ (void *)0, "host");
  if (status != 0) {
    host_thread_loop = 0;
    ERROR("ping plugin: Starting thread failed.");
    pthread_mutex_unlock(&host_lock);
    return -1;
  }

  pthread_mutex_unlock(&host_lock);
  return 0;
} /* }}} int start_thread */

static int stop_thread(void) /* {{{ */
{
  int status;

  pthread_mutex_lock(&host_lock);

  if (host_thread_loop == 0) {
    pthread_mutex_unlock(&host_lock);
    return -1;
  }

  host_thread_loop = 0;
  pthread_cond_broadcast(&host_cond);
  pthread_mutex_unlock(&host_lock);

  status = pthread_join(host_thread_id, /* return = */ NULL);
  if (status != 0) {
    ERROR("host plugin: Stopping thread failed.");
    status = -1;
  }

  pthread_mutex_lock(&host_lock);
  memset(&host_thread_id, 0, sizeof(host_thread_id));
  pthread_mutex_unlock(&host_lock);

  return status;
} /* }}} int stop_thread */

static int host_init (void) {
  int rc;

  if (host_path) {

    rc = mdb_env_create(&env);
    if (rc) {
      ERROR(PLUGIN_NAME " plugin: mdb_env_create failed: %s (%d)", mdb_strerror(rc), rc);
      return -1;
    }

    rc = mdb_env_open(env, host_path, MDB_NOSUBDIR, 0664);
    if (rc) {
      ERROR(PLUGIN_NAME " plugin: opening path '%s' failed: %s (%d)",
              host_path, mdb_strerror(rc), rc);
      mdb_env_close(env);
      env = NULL;
      return -1;
    }

    return start_thread();
  }

  return 0;
}

static int host_shutdown (void) {

    if (env) {

      INFO(PLUGIN_NAME " plugin: shutting down thread.");
      if (stop_thread() < 0)
        return -1;

      mdb_env_close(env);
    }

    return 0;
}

static int host_write(const data_set_t *ds, const value_list_t *vl,
                    __attribute__((unused)) user_data_t *user_data) {

  if (env) {
      MDB_txn *txn;
      MDB_dbi dbi;
      MDB_val key, data;
      struct timeval tv_now;
      int rc;
      int add = 0;

      rc = gettimeofday(&tv_now, /* struct timezone = */ NULL);
      if (rc != 0) {
        ERROR("gettimeofday failed: %s", STRERRNO);
        return -1;
      }

      /* first pass - do we exist at all? read only transaction makes this efficient */

      key.mv_size = sizeof(vl->host);
      key.mv_data = (void *)vl->host;

      rc = mdb_txn_begin(env, NULL, MDB_RDONLY, &txn);
      if (rc) {
        ERROR("mdb_txn_begin returned: %s (%d)", mdb_strerror(rc), rc);
        return -1;
      }

      rc = mdb_dbi_open(txn, NULL, 0, &dbi);
      if (MDB_NOTFOUND == rc) {

        /* brand new database, skip the get */

      }
      else if (rc) {

        /* error happened */

        ERROR("mdb_dbi_open returned: %s (%d)", mdb_strerror(rc), rc);
        mdb_txn_abort(txn);
        return -1;

      }
      else {

        /* existing database, do the get */

        rc = mdb_get(txn, dbi, &key, &data);

      }

      if (MDB_NOTFOUND == rc) {

        /* new host found, send a notification */

        add = 1;

      }
      else if (rc) {

        /* error happened */

        ERROR("mdb_get returned: %s (%d)", mdb_strerror(rc), rc);
        mdb_dbi_close(env, dbi);
        mdb_txn_abort(txn);
        return -1;

      }
      else {

          /* have we seen host recently? if so it is good enough, exit cheaply */

          struct timeval *tv_then = data.mv_data;

          if (data.mv_size == sizeof(struct timespec) &&
                  ((tv_then = data.mv_data)) &&
                  (tv_then->tv_sec < tv_now.tv_sec)) {
              mdb_dbi_close(env, dbi);
              mdb_txn_abort(txn);
              return 0;
          }

      }

      mdb_dbi_close(env, dbi);
      mdb_txn_abort(txn);

      /* second pass - update with the time last seen, this is more expensive, but rarer */

      rc = mdb_txn_begin(env, NULL, 0, &txn);
      if (rc) {
        ERROR("mdb_txn_begin returned: %s (%d)", mdb_strerror(rc), rc);
        return -1;
      }

      rc = mdb_dbi_open(txn, NULL, MDB_CREATE, &dbi);
      if (rc) {
        ERROR("mdb_dbi_open returned: %s (%d)", mdb_strerror(rc), rc);
        mdb_txn_abort(txn);
        return -1;
      }

      data.mv_size = sizeof(tv_now);
      data.mv_data = &tv_now;

      rc = mdb_put(txn, dbi, &key, &data, MDB_NOOVERWRITE);
      if (MDB_KEYEXIST == rc) {

          /* another thread beat us to it, do nothing */
          add = 0;

      }
      else if (rc) {
        ERROR("mdb_put returned: %s (%d)", mdb_strerror(rc), rc);
        mdb_dbi_close(env, dbi);
        mdb_txn_abort(txn);
        return -1;
      }

      rc = mdb_txn_commit(txn);
      if (rc) {
        ERROR("mdb_txn_commit returned: %s (%d)", mdb_strerror(rc), rc);
        mdb_dbi_close(env, dbi);
        return -1;
      }

      mdb_dbi_close(env, dbi);

      /* finally, handle the notification if needed */
      if (add) {
          notification_t n;
          char message[NOTIF_MAX_MSG_LEN];

          ssnprintf(message, sizeof(message), "Host is found: %.*s",
                  (int)sizeof(vl->host), vl->host);
          notification_init(&n, NOTIF_OKAY, message, vl->host, PLUGIN_NAME,
                            NULL, "host", "found");
          n.time = cdtime();

          plugin_dispatch_notification(&n);
      }

  }

  return 0;
}

static int host_config(const char *key, const char *value) {
  if (strcasecmp(key, "HOSTPATH") == 0) {
    if (value != NULL && strcmp(value, "") != 0) {
      host_path = strdup(value);
    } else {
      host_path = NULL;
    }
    return 0;
  }
  else if (strcasecmp(key, "LOSTINTERVAL") == 0) {
    if (value != NULL) {
      lost_interval = atoi(value);
    }
    return 0;
  }
  else if (strcasecmp(key, "DELAYINTERVAL") == 0) {
    if (value != NULL) {
      delay_interval = atoi(value);
    }
    return 0;
  }
  else
    return -1;
} /* int host_config */

void module_register(void) {
  plugin_register_init (PLUGIN_NAME, host_init);
  plugin_register_config(PLUGIN_NAME, host_config, config_keys, config_keys_num);
  /* If config is supplied, the global host_path will be set. */
  plugin_register_write(PLUGIN_NAME, host_write, NULL);
  plugin_register_shutdown (PLUGIN_NAME, host_shutdown);
}
