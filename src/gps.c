/**
 * collectd - src/gps.c
 * Copyright (C) 2015  Nicolas JOURDEN
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
 *   Nicolas JOURDEN <nicolas.jourden at laposte.net>
 *   Florian octo Forster <octo at collectd.org>
 *   Marc Fournier <marc.fournier at camptocamp.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"
#include "utils_time.h"

#define CGPS_TRUE 1
#define CGPS_FALSE 0
#define CGPS_DEFAULT_HOST "localhost"
#define CGPS_DEFAULT_PORT "2947" /* DEFAULT_GPSD_PORT */
#define CGPS_DEFAULT_TIMEOUT MS_TO_CDTIME_T(15)
#define CGPS_DEFAULT_PAUSE_CONNECT TIME_T_TO_CDTIME_T(5)
#define CGPS_MAX_ERROR 100
#define CGPS_CONFIG "?WATCH={\"enable\":true,\"json\":true,\"nmea\":false}\r\n"

#include <gps.h>
#include <pthread.h>

typedef struct {
  char *host;
  char *port;
  cdtime_t timeout;
  cdtime_t pause_connect;
} cgps_config_t;

typedef struct {
  gauge_t sats_used;
  gauge_t sats_visible;
  gauge_t hdop;
  gauge_t vdop;
} cgps_data_t;

static cgps_config_t cgps_config_data;

static cgps_data_t cgps_data = {NAN, NAN, NAN, NAN};

static pthread_t cgps_thread_id;
static pthread_mutex_t cgps_data_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t cgps_thread_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cgps_thread_cond = PTHREAD_COND_INITIALIZER;
static int cgps_thread_shutdown = CGPS_FALSE;
static int cgps_thread_running = CGPS_FALSE;

/**
 * Non blocking pause for the thread.
 */
static int cgps_thread_pause(cdtime_t pTime) {
  cdtime_t until = cdtime() + pTime;

  pthread_mutex_lock(&cgps_thread_lock);
  pthread_cond_timedwait(&cgps_thread_cond, &cgps_thread_lock,
                         &CDTIME_T_TO_TIMESPEC(until));

  int ret = !cgps_thread_shutdown;

  pthread_mutex_unlock(&cgps_thread_lock);
  return ret;
}

/**
 * Thread reading from gpsd.
 */
static void *cgps_thread(void *pData) {
  struct gps_data_t gpsd_conn;
  unsigned int err_count;
  cgps_thread_running = CGPS_TRUE;

  while (CGPS_TRUE) {
    pthread_mutex_lock(&cgps_thread_lock);
    if (cgps_thread_shutdown == CGPS_TRUE) {
      goto quit;
    }
    pthread_mutex_unlock(&cgps_thread_lock);

    err_count = 0;

#if GPSD_API_MAJOR_VERSION > 4
    int status =
        gps_open(cgps_config_data.host, cgps_config_data.port, &gpsd_conn);
#else
    int status =
        gps_open_r(cgps_config_data.host, cgps_config_data.port, &gpsd_conn);
#endif
    if (status < 0) {
      WARNING("gps plugin: connecting to %s:%s failed: %s",
              cgps_config_data.host, cgps_config_data.port, gps_errstr(status));

      // Here we make a pause until a new tentative to connect, we check also if
      // the thread does not need to stop.
      if (cgps_thread_pause(cgps_config_data.pause_connect) == CGPS_FALSE) {
        goto quit;
      }

      continue;
    }

    gps_stream(&gpsd_conn, WATCH_ENABLE | WATCH_JSON | WATCH_NEWSTYLE, NULL);
    gps_send(&gpsd_conn, CGPS_CONFIG);

    while (CGPS_TRUE) {
      pthread_mutex_lock(&cgps_thread_lock);
      if (cgps_thread_shutdown == CGPS_TRUE) {
        goto stop;
      }
      pthread_mutex_unlock(&cgps_thread_lock);

#if GPSD_API_MAJOR_VERSION > 4
      long timeout_us = CDTIME_T_TO_US(cgps_config_data.timeout);
      if (!gps_waiting(&gpsd_conn, (int)timeout_us))
#else
      if (!gps_waiting(&gpsd_conn))
#endif
      {
        continue;
      }

#if GPSD_API_MAJOR_VERSION > 6
      if (gps_read(&gpsd_conn, NULL, 0) == -1)
#else
      if (gps_read(&gpsd_conn) == -1)
#endif
      {
        WARNING("gps plugin: incorrect data! (err_count: %d)", err_count);
        err_count++;

        if (err_count > CGPS_MAX_ERROR) {
          // Server is not responding ...
          if (gps_send(&gpsd_conn, CGPS_CONFIG) == -1) {
            WARNING("gps plugin: gpsd seems to be down, reconnecting");
            gps_close(&gpsd_conn);
            break;
          }
          // Server is responding ...
          else {
            err_count = 0;
          }
        }

        continue;
      }

      pthread_mutex_lock(&cgps_data_lock);

      // Number of sats in view:
      cgps_data.sats_used = (gauge_t)gpsd_conn.satellites_used;
      cgps_data.sats_visible = (gauge_t)gpsd_conn.satellites_visible;

      // dilution of precision:
      cgps_data.vdop = NAN;
      cgps_data.hdop = NAN;
      if (cgps_data.sats_used > 0) {
        cgps_data.hdop = gpsd_conn.dop.hdop;
        cgps_data.vdop = gpsd_conn.dop.vdop;
      }

      DEBUG("gps plugin: %.0f sats used (of %.0f visible), hdop = %.3f, vdop = "
            "%.3f",
            cgps_data.sats_used, cgps_data.sats_visible, cgps_data.hdop,
            cgps_data.vdop);

      pthread_mutex_unlock(&cgps_data_lock);
    }
  }

stop:
  DEBUG("gps plugin: thread closing gpsd connection ... ");
  gps_stream(&gpsd_conn, WATCH_DISABLE, NULL);
  gps_close(&gpsd_conn);
quit:
  DEBUG("gps plugin: thread shutting down ... ");
  cgps_thread_running = CGPS_FALSE;
  pthread_mutex_unlock(&cgps_thread_lock);
  pthread_exit(NULL);
}

/**
 * Submit a piece of the data.
 */
static void cgps_submit(const char *type, gauge_t value,
                        const char *type_instance) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "gps", sizeof(vl.plugin));
  sstrncpy(vl.type, type, sizeof(vl.type));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
}

/**
 * Read the data and submit by piece.
 */
static int cgps_read(void) {
  cgps_data_t data_copy;

  pthread_mutex_lock(&cgps_data_lock);
  data_copy = cgps_data;
  pthread_mutex_unlock(&cgps_data_lock);

  cgps_submit("dilution_of_precision", data_copy.hdop, "horizontal");
  cgps_submit("dilution_of_precision", data_copy.vdop, "vertical");
  cgps_submit("satellites", data_copy.sats_used, "used");
  cgps_submit("satellites", data_copy.sats_visible, "visible");

  return 0;
}

/**
 * Read configuration.
 */
static int cgps_config(oconfig_item_t *ci) {
  int i;

  for (i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Host", child->key) == 0)
      cf_util_get_string(child, &cgps_config_data.host);
    else if (strcasecmp("Port", child->key) == 0)
      cf_util_get_service(child, &cgps_config_data.port);
    else if (strcasecmp("Timeout", child->key) == 0)
      cf_util_get_cdtime(child, &cgps_config_data.timeout);
    else if (strcasecmp("PauseConnect", child->key) == 0)
      cf_util_get_cdtime(child, &cgps_config_data.pause_connect);
    else
      WARNING("gps plugin: Ignoring unknown config option \"%s\".", child->key);
  }

  // Controlling the value for timeout:
  // If set too high it blocks the reading (> 5 s), too low it gets not reading
  // (< 500 us).
  // To avoid any issues we replace "out of range" value by the default value.
  if (cgps_config_data.timeout > TIME_T_TO_CDTIME_T(5) ||
      cgps_config_data.timeout < US_TO_CDTIME_T(500)) {
    WARNING("gps plugin: timeout set to %.6f sec. setting to default (%.6f).",
            CDTIME_T_TO_DOUBLE(cgps_config_data.timeout),
            CDTIME_T_TO_DOUBLE(CGPS_DEFAULT_TIMEOUT));
    cgps_config_data.timeout = CGPS_DEFAULT_TIMEOUT;
  }

  return 0;
}

/**
 * Init.
 */
static int cgps_init(void) {
  int status;

  if (cgps_thread_running == CGPS_TRUE) {
    DEBUG("gps plugin: error gps thread already running ... ");
    return 0;
  }

  DEBUG("gps plugin: config{host: \"%s\", port: \"%s\", timeout: %.6f sec., "
        "pause connect: %.3f sec.}",
        cgps_config_data.host, cgps_config_data.port,
        CDTIME_T_TO_DOUBLE(cgps_config_data.timeout),
        CDTIME_T_TO_DOUBLE(cgps_config_data.pause_connect));

  status =
      plugin_thread_create(&cgps_thread_id, NULL, cgps_thread, NULL, "gps");
  if (status != 0) {
    ERROR("gps plugin: pthread_create() failed.");
    return -1;
  }

  return 0;
}

/**
 * Shutdown.
 */
static int cgps_shutdown(void) {
  void *res;

  pthread_mutex_lock(&cgps_thread_lock);
  cgps_thread_shutdown = CGPS_TRUE;
  pthread_cond_broadcast(&cgps_thread_cond);
  pthread_mutex_unlock(&cgps_thread_lock);

  pthread_join(cgps_thread_id, &res);
  free(res);

  // Clean mutex:
  pthread_mutex_destroy(&cgps_thread_lock);
  pthread_mutex_unlock(&cgps_data_lock);
  pthread_mutex_destroy(&cgps_data_lock);

  sfree(cgps_config_data.port);
  sfree(cgps_config_data.host);

  return 0;
}

/**
 * Register the module.
 */
void module_register(void) {
  cgps_config_data.host = sstrdup(CGPS_DEFAULT_HOST);
  cgps_config_data.port = sstrdup(CGPS_DEFAULT_PORT);
  cgps_config_data.timeout = CGPS_DEFAULT_TIMEOUT;
  cgps_config_data.pause_connect = CGPS_DEFAULT_PAUSE_CONNECT;

  plugin_register_complex_config("gps", cgps_config);
  plugin_register_init("gps", cgps_init);
  plugin_register_read("gps", cgps_read);
  plugin_register_shutdown("gps", cgps_shutdown);
}
