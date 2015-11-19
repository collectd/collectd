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


#define GPS_DEFAULT_HOST    "localhost"
#define GPS_DEFAULT_PORT    "2947"
#define GPS_DEFAULT_TIMEOUT 15
#define GPS_DEFAULT_PAUSE   1


#if HAVE_GPS_H
#include <gps.h>
#endif

#if HAVE_LIBPTHREAD
#include <pthread.h>
#endif


typedef struct
{
  char *host;
  char *port;
  int timeout;
  int pause;
} gps_definition_t;
static gps_definition_t gps_data_config;


typedef struct {
 int satellites;
 double vdop;
 double hdop;
} gpsdata_t;
static gpsdata_t gps_data_read;


static const char *config_keys[] =
{
  "Host",
  "Port",
  "Timeout",
  "Pause"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);


// Thread items:
static pthread_t connector = (pthread_t) 0;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;


/**
 * Thread reading from gpsd.
 */
static void * gps_collectd_thread (void * pData)
{
  struct gps_data_t gps_data;

  while (1)
  {
    if (gps_open(gps_data_config.host, gps_data_config.port, &gps_data) < 0)
    {
      WARNING ("gps: cannot connect to: %s:%s", gps_data_config.host, gps_data_config.port);
      sleep(60);
      continue;
    }

    gps_stream(&gps_data, WATCH_ENABLE | WATCH_JSON | WATCH_NEWSTYLE, NULL);
    gps_send(&gps_data, "?WATCH={\"enable\":true,\"json\":true,\"nmea\":false}\r\n");

    while (1)
    {
      if (gps_waiting (&gps_data, gps_data_config.timeout ) )
      {
        DEBUG ("gps: reading\n");

        if (gps_read (&gps_data) == -1)
        {
            WARNING ("gps: incorrect data !\n");
        } 
        else {
          pthread_mutex_lock (&data_mutex);
          DEBUG ("gps: parsing\n");

          // Dop data:
          if (isnan(gps_data.dop.vdop) == 0)
          {
            DEBUG ("gps: isnan(gps_data.dop.vdop) == 0 [OK]\n");
            gps_data_read.vdop = gps_data.dop.vdop;
          }
          if (isnan(gps_data.dop.hdop) == 0)
          {
            DEBUG ("gps: isnan(gps_data.dop.hdop) == 0 [OK]\n");
            gps_data_read.hdop = gps_data.dop.hdop;
          }

          // Sat in view:
          if ((gps_data.set & LATLON_SET))
          {
            DEBUG ("gps: gps_data.set & LATLON_SET [OK] ... \n");
            gps_data_read.satellites = gps_data.satellites_used;
          }
 
          DEBUG ("gps: raw is hdop=%1.3f, vdop=%1.3f, sat-used=%02d, lat=%02.05f, lon=%03.05f\n", 
            gps_data.dop.hdop,
            gps_data.dop.vdop,
            gps_data.satellites_used,
            gps_data.fix.latitude,
            gps_data.fix.longitude
          );

          pthread_mutex_unlock (&data_mutex);
          sleep(gps_data_config.pause);
        }
      }
    }
  }

  gps_stream(&gps_data, WATCH_DISABLE, NULL);
  gps_close(&gps_data);

  pthread_exit ((void *)0);
}


/**
 * Submit a piece of the data.
 */
static void gps_collectd_submit (const char *type, gauge_t value, const char *type_instance)
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
static int gps_collectd_read ()
{
  pthread_mutex_lock (&data_mutex);
  gps_collectd_submit("dilution_of_precision", (gauge_t) gps_data_read.hdop, "horizontal");
  gps_collectd_submit("dilution_of_precision", (gauge_t) gps_data_read.vdop, "vertical");
  gps_collectd_submit("satellites", (gauge_t) gps_data_read.satellites, "gps");
  DEBUG ("gps: hdop=%1.3f, vdop=%1.3f, sat=%02d.\n", 
    gps_data_read.hdop,
    gps_data_read.vdop,
    gps_data_read.satellites
  );
  pthread_mutex_unlock (&data_mutex);
  return (0);
}


/**
 * Read configuration.
 */
static int gps_collectd_config (const char *key, const char *value)
{
  char *endptr = NULL;

  if (strcasecmp (key, "Host") == 0)
  {
    if (gps_data_config.host != NULL)
    {
      free (gps_data_config.host);
    }
    gps_data_config.host = sstrdup (value);
  }
  if (strcasecmp (key, "Port") == 0)
  {
    if (gps_data_config.port != NULL)
    {
      free (gps_data_config.port);
    }
    gps_data_config.port = sstrdup (value);
  }
  if (strcasecmp (key, "Timeout") == 0)
  {
    gps_data_config.timeout = (int) ( strtod(value, &endptr) * 1000 );
  DEBUG ("gps: will use pause %s - %d.\n", value, gps_data_config.timeout);
  }
  if (strcasecmp (key, "Pause") == 0)
  {
    gps_data_config.pause = (int) (strtod (value, &endptr));
  DEBUG ("gps: will use pause %s -  %d.\n", value, gps_data_config.pause);
  }
  return (0);
}


/**
 * Init.
 */
static int gps_collectd_init (void)
{
  int err = 0;

  DEBUG ("gps: will use %s:%s, timeout %d ms, pause %d sec.\n", gps_data_config.host, gps_data_config.port, gps_data_config.timeout, gps_data_config.pause);

  err = plugin_thread_create (&connector, NULL, gps_collectd_thread, NULL);

  if (err != 0)
  {
    ERROR ("gps: pthread_create() failed.");
    return (-1);
  }

  return (0);
}


/**
 * Shutdown.
 */
static int gps_collectd_shutdown (void)
{
  if (connector != ((pthread_t) 0))
  {
    pthread_kill (connector, SIGTERM);
    connector = (pthread_t) 0;
  }

  sfree (gps_data_config.port);
  sfree (gps_data_config.host);

  return (0);
}

/**
 * Register the module.
 */
void module_register (void)                                                                    
{
  gps_data_config.host = sstrdup (GPS_DEFAULT_HOST);
  gps_data_config.port = sstrdup (GPS_DEFAULT_PORT);
  gps_data_config.timeout = GPS_DEFAULT_TIMEOUT;
  gps_data_config.pause = GPS_DEFAULT_PAUSE;
  gps_data_read.hdop = 0;
  gps_data_read.vdop = 0;
  gps_data_read.satellites = 0;

  // Read the config params:
  plugin_register_config ("gps", gps_collectd_config, config_keys, config_keys_num);
  // Create the thread:
  plugin_register_init ("gps", gps_collectd_init);
  // Kill the thread and stop.
  plugin_register_shutdown ("gps", gps_collectd_shutdown);
  // Read plugin:
  plugin_register_read ("gps", gps_collectd_read);
}

