/**
 * This plug-in helps to monitor the GPS connected to a system.
 * It reads the data comming from GPSd.
 It look for the following parameters.
 */

#include "collectd.h"
#include "common.h"
#include "plugin.h"

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
} gps_definition_t;
static gps_definition_t gps_data_config;

typedef struct {
 int satellites;
 double vdop;
 double hdop;
} gpsDATA_t;
static gpsDATA_t gps_data_read;


static const char *config_keys[] =
{
  "Host",
  "Port",
  "Timeout"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);


// Thread items:
static pthread_t connector = (pthread_t) 0;
static pthread_mutex_t data_mutex = PTHREAD_MUTEX_INITIALIZER;


/**
 * Thread reading from GPSd.
 */
static void * gps_collectd_thread (void * pData)
{
  struct gps_data_t gps_data;

  while (1)
  {
    if (gps_open(gps_data_config.host, gps_data_config.port, &gps_data) < 0)
    {
      printf ("cannot connect to: %s:%s", gps_data_config.host, gps_data_config.port);
      sleep(60);
      continue;
    }

    gps_stream(&gps_data, WATCH_ENABLE | WATCH_JSON, NULL);

    while (1)
    {
      if (gps_waiting (&gps_data, gps_data_config.timeout))
      {
        if (gps_read (&gps_data) == -1)
        {
            WARNING ("incorrect data.\n");
        } 
        else {
          pthread_mutex_lock (&data_mutex);

          // Dop data:
          if (isnan(gps_data.dop.vdop) == 0)
          {
            gps_data_read.vdop = gps_data.dop.vdop;
          }
          if (isnan(gps_data.dop.hdop) == 0)
          {
            gps_data_read.hdop = gps_data.dop.hdop;
          }

          // Sat in view:
          if ((gps_data.set & LATLON_SET))
          {
            gps_data_read.satellites = gps_data.satellites_used;
          }

          pthread_mutex_unlock (&data_mutex);
        }
      }
    }
  }

  gps_stream(&gps_data, WATCH_DISABLE, NULL);
  gps_close(&gps_data);

  pthread_exit ((void *)0);
}


/**
 * Submit the data.
 */
static void gps_collectd_submit (const char *type, gauge_t value)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "gps", sizeof (vl.plugin));
  sstrncpy (vl.type, type, sizeof (vl.type));
  sstrncpy (vl.type_instance, "gps", sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
}


/**
 * Read the data and submit.
 */
static int gps_collectd_read ()
{
  pthread_mutex_lock (&data_mutex);
  gps_collectd_submit("gps_hdop", (gauge_t) gps_data_read.hdop);
  gps_collectd_submit("gps_vdop", (gauge_t) gps_data_read.vdop);
  gps_collectd_submit("gps_sat", (gauge_t) gps_data_read.satellites);
  printf ("gps: hdop=%1.3f, vdop=%1.3f, sat=%02d.\n", 
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
  if (strcasecmp (key, "Host") == 0) {
    if (gps_data_config.host != NULL) free (gps_data_config.host);
      gps_data_config.host = sstrdup (value);
  }
  if (strcasecmp (key, "Port") == 0) {
    if (gps_data_config.port != NULL) free (gps_data_config.port);
      gps_data_config.port = sstrdup (value);
  }
  if (strcasecmp (key, "Timeout") == 0) {
     gps_data_config.timeout = (int) strtol (value, NULL, 1000);
  }
  return (0);
}


/**
 * Init.
 */
static int gps_collectd_init (void)
{
  int err = 0;

  printf ("gps: will use %s:%s with timeout %d.\n", gps_data_config.host, gps_data_config.port, gps_data_config.timeout);

  err = plugin_thread_create (&connector, NULL, gps_collectd_thread, NULL);

  if (err != 0) {
    WARNING ("pthread_create() failed.");
    return (-1);
  }

  return (0);
}


/**
 * Shutdown.
 */
static int gps_collectd_shutdown (void)
{
  if (connector != ((pthread_t) 0)) {
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
  gps_data_config.host = sstrdup ("localhost");
  gps_data_config.port = sstrdup ("2947");
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

