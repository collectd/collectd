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
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_time.h"
#include "configfile.h"

#define GPS_DEFAULT_HOST    "localhost"
#define GPS_DEFAULT_PORT    "2947"
#define GPS_DEFAULT_TIMEOUT TIME_T_TO_CDTIME_T (15)
#define GPS_DEFAULT_PAUSE   TIME_T_TO_CDTIME_T (1)

#include <gps.h>
#include <pthread.h>

typedef struct {
  char *host;
  char *port;
  cdtime_t timeout;
  cdtime_t pause;
} cgps_config_t;

typedef struct {
  gauge_t sats_used;
  gauge_t sats_visible;
  gauge_t hdop;
  gauge_t vdop;
} cgps_data_t;

// Thread items:
static pthread_t connector = (pthread_t) 0;

static cgps_config_t config;

static cgps_data_t      data = {NAN, NAN, NAN, NAN};
static pthread_mutex_t  data_lock = PTHREAD_MUTEX_INITIALIZER;

/**
 * Thread reading from gpsd.
 */
static void * gps_collectd_thread (void * pData)
{
  struct gps_data_t conn;

  while (1)
  {
    int status = gps_open (config.host, config.port, &conn);
    if (status < 0)
    {
      WARNING ("gps plugin: Connecting to %s:%s failed: %s",
               config.host, config.port, gps_errstr (status));
      sleep (60);
      continue;
    }

    gps_stream (&conn, WATCH_ENABLE | WATCH_JSON | WATCH_NEWSTYLE, NULL);
    gps_send (&conn, "?WATCH={\"enable\":true,\"json\":true,\"nmea\":false}\r\n");

    while (1)
    {
      long timeout_us = CDTIME_T_TO_US (config.timeout);
      if (!gps_waiting (&conn, (int) timeout_us))
      {
        struct timespec pause_ns;
        CDTIME_T_TO_TIMESPEC (config.pause, &pause_ns);
        nanosleep (&pause_ns, NULL);
        continue;
      }

      if (gps_read (&conn) == -1)
      {
        WARNING ("gps plugin: incorrect data!");
        continue;
      }

      pthread_mutex_lock (&data_lock);

      // Number of sats in view:
      data.sats_used = (gauge_t) conn.satellites_used;
      data.sats_visible = (gauge_t) conn.satellites_visible;

      // dilution of precision:
      data.vdop = NAN; data.hdop = NAN;
      if (data.sats_used > 0)
      {
        data.hdop = conn.dop.hdop;
        data.vdop = conn.dop.vdop;
      }


      DEBUG ("gps plugin: %.0f sats used (of %.0f visible), hdop = %.3f, vdop = %.3f",
             data.sats_used, data.sats_visible, data.hdop, data.vdop);

      pthread_mutex_unlock (&data_lock);
    }
  }

  gps_stream (&conn, WATCH_DISABLE, /* data = */ NULL);
  gps_close (&conn);

  pthread_exit ((void *) 0);
}

/**
 * Submit a piece of the data.
 */
static void cgps_submit (const char *type, gauge_t value, const char *type_instance)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "gps", sizeof (vl.plugin));
  sstrncpy (vl.type, type, sizeof (vl.type));
  sstrncpy (vl.type_instance, type_instance, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
}

/**
 * Read the data and submit by piece.
 */
static int cgps_read ()
{
  cgps_data_t data_copy;

  pthread_mutex_lock (&data_lock);
  data_copy = data;
  pthread_mutex_unlock (&data_lock);

  cgps_submit ("dilution_of_precision", data_copy.hdop, "horizontal");
  cgps_submit ("dilution_of_precision", data_copy.vdop, "vertical");
  cgps_submit ("satellites", data_copy.sats_used, "used");
  cgps_submit ("satellites", data_copy.sats_visible, "visible");

  return (0);
}

/**
 * Read configuration.
 */
static int cgps_config (oconfig_item_t *ci)
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Host", child->key) == 0)
      cf_util_get_string (child, &config.host);
    else if (strcasecmp ("Port", child->key) == 0)
      cf_util_get_string (child, &config.port);
    else if (strcasecmp ("Timeout", child->key) == 0)
      cf_util_get_cdtime (child, &config.timeout);
    else if (strcasecmp ("Pause", child->key) == 0)
      cf_util_get_cdtime (child, &config.pause);
    else
      WARNING ("gps plugin: Ignoring unknown config option \"%s\".", child->key);
  }

  return 0;
}

/**
 * Init.
 */
static int cgps_init (void)
{
  int status;

  DEBUG ("gps plugin: config{host: \"%s\", port: \"%s\", timeout: %.3f, pause: %.3f}",
         config.host, config.port,
         CDTIME_T_TO_DOUBLE (config.timeout), CDTIME_T_TO_DOUBLE (config.pause));

  status = plugin_thread_create (&connector, NULL, gps_collectd_thread, NULL);
  if (status != 0)
  {
    ERROR ("gps plugin: pthread_create() failed.");
    return (-1);
  }

  return (0);
}

/**
 * Shutdown.
 */
static int cgps_shutdown (void)
{
  if (connector != ((pthread_t) 0))
  {
    pthread_kill (connector, SIGTERM);
    connector = (pthread_t) 0;
  }

  sfree (config.port);
  sfree (config.host);

  return (0);
}

/**
 * Register the module.
 */
void module_register (void)
{
  config.host = sstrdup (GPS_DEFAULT_HOST);
  config.port = sstrdup (GPS_DEFAULT_PORT);
  config.timeout = GPS_DEFAULT_TIMEOUT;
  config.pause = GPS_DEFAULT_PAUSE;

  plugin_register_complex_config ("gps", cgps_config);
  plugin_register_init ("gps", cgps_init);
  plugin_register_read ("gps", cgps_read);
  plugin_register_shutdown ("gps", cgps_shutdown);
}
