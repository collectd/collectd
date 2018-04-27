/*
 * collectd - src/sigrok.c
 * Copyright (C) 2013 Bert Vermeulen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authors:
 *   Bert Vermeulen <bert at biot.com>
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <glib.h>
#include <libsigrok/libsigrok.h>

/* Minimum interval between dispatches coming from this plugin. The RRD
 * plugin, at least, complains when written to with sub-second intervals.*/
#define DEFAULT_MIN_DISPATCH_INTERVAL TIME_T_TO_CDTIME_T(0)

static pthread_t sr_thread;
static int sr_thread_running = FALSE;
GSList *config_devices;
static int num_devices;
static int loglevel = SR_LOG_WARN;
static struct sr_context *sr_ctx;

struct config_device {
  char *name;
  char *driver;
  char *conn;
  char *serialcomm;
  struct sr_dev_inst *sdi;
  cdtime_t min_dispatch_interval;
  cdtime_t last_dispatch;
};

static int sigrok_log_callback(void *cb_data __attribute__((unused)),
                               int msg_loglevel, const char *format,
                               va_list args) {
  char s[512];

  if (msg_loglevel <= loglevel) {
    vsnprintf(s, 512, format, args);
    plugin_log(LOG_INFO, "sigrok plugin: %s", s);
  }

  return 0;
}

static int sigrok_config_device(oconfig_item_t *ci) {
  struct config_device *cfdev;

  if (!(cfdev = calloc(1, sizeof(*cfdev)))) {
    ERROR("sigrok plugin: calloc failed.");
    return -1;
  }
  if (cf_util_get_string(ci, &cfdev->name)) {
    free(cfdev);
    WARNING("sigrok plugin: Invalid device name.");
    return -1;
  }
  cfdev->min_dispatch_interval = DEFAULT_MIN_DISPATCH_INTERVAL;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *item = ci->children + i;
    if (!strcasecmp(item->key, "driver"))
      cf_util_get_string(item, &cfdev->driver);
    else if (!strcasecmp(item->key, "conn"))
      cf_util_get_string(item, &cfdev->conn);
    else if (!strcasecmp(item->key, "serialcomm"))
      cf_util_get_string(item, &cfdev->serialcomm);
    else if (!strcasecmp(item->key, "minimuminterval"))
      cf_util_get_cdtime(item, &cfdev->min_dispatch_interval);
    else
      WARNING("sigrok plugin: Invalid keyword \"%s\".", item->key);
  }

  config_devices = g_slist_append(config_devices, cfdev);

  return 0;
}

static int sigrok_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *item = ci->children + i;
    if (strcasecmp("LogLevel", item->key) == 0) {
      int status;
      int tmp = -1;

      status = cf_util_get_int(item, &tmp);
      if (status != 0)
        continue;
      else if ((tmp < 0) || (tmp > 5)) {
        ERROR("sigrok plugin: The \"LogLevel\" "
              "configuration option expects "
              "an integer between 0 and 5 "
              "(inclusive); you provided %i.",
              tmp);
        continue;
      }
      loglevel = tmp;
    } else if (!strcasecmp(item->key, "Device"))
      sigrok_config_device(item);
    else
      WARNING("sigrok plugin: Invalid keyword \"%s\".", item->key);
  }

  return 0;
}

static const char *sigrok_value_type(const struct sr_datafeed_analog *analog) {
  const char *s;

  if (analog->mq == SR_MQ_VOLTAGE)
    s = "voltage";
  else if (analog->mq == SR_MQ_CURRENT)
    s = "current";
  else if (analog->mq == SR_MQ_FREQUENCY)
    s = "frequency";
  else if (analog->mq == SR_MQ_POWER)
    s = "power";
  else if (analog->mq == SR_MQ_TEMPERATURE)
    s = "temperature";
  else if (analog->mq == SR_MQ_RELATIVE_HUMIDITY)
    s = "humidity";
  else if (analog->mq == SR_MQ_SOUND_PRESSURE_LEVEL)
    s = "spl";
  else
    s = "gauge";

  return s;
}

static void sigrok_feed_callback(const struct sr_dev_inst *sdi,
                                 const struct sr_datafeed_packet *packet,
                                 void *cb_data) {
  const struct sr_datafeed_analog *analog;
  struct config_device *cfdev;
  value_list_t vl = VALUE_LIST_INIT;

  /* Find this device's configuration. */
  cfdev = NULL;
  for (GSList *l = config_devices; l; l = l->next) {
    cfdev = l->data;
    if (cfdev->sdi == sdi) {
      /* Found it. */
      break;
    }
    cfdev = NULL;
  }

  if (!cfdev) {
    ERROR("sigrok plugin: Received data from driver \"%s\" but "
          "can't find a configuration / device matching "
          "it.",
          sdi->driver->name);
    return;
  }

  if (packet->type == SR_DF_END) {
    /* TODO: try to restart acquisition after a delay? */
    WARNING("sigrok plugin: acquisition for \"%s\" ended.", cfdev->name);
    return;
  }

  if (packet->type != SR_DF_ANALOG)
    return;

  if ((cfdev->min_dispatch_interval != 0) &&
      ((cdtime() - cfdev->last_dispatch) < cfdev->min_dispatch_interval))
    return;

  /* Ignore all but the first sample on the first probe. */
  analog = packet->payload;
  vl.values = &(value_t){.gauge = analog->data[0]};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "sigrok", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, cfdev->name, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, sigrok_value_type(analog), sizeof(vl.type));

  plugin_dispatch_values(&vl);
  cfdev->last_dispatch = cdtime();
}

static void sigrok_free_drvopts(struct sr_config *src) {
  g_variant_unref(src->data);
  g_free(src);
}

static int sigrok_init_driver(struct config_device *cfdev,
                              struct sr_dev_driver *drv) {
  struct sr_config *src;
  GSList *devlist, *drvopts;
  char hwident[512];

  if (sr_driver_init(sr_ctx, drv) != SR_OK)
    /* Error was logged by libsigrok. */
    return -1;

  drvopts = NULL;
  if (cfdev->conn) {
    if (!(src = malloc(sizeof(*src))))
      return -1;
    src->key = SR_CONF_CONN;
    src->data = g_variant_new_string(cfdev->conn);
    drvopts = g_slist_append(drvopts, src);
  }
  if (cfdev->serialcomm) {
    if (!(src = malloc(sizeof(*src))))
      return -1;
    src->key = SR_CONF_SERIALCOMM;
    src->data = g_variant_new_string(cfdev->serialcomm);
    drvopts = g_slist_append(drvopts, src);
  }
  devlist = sr_driver_scan(drv, drvopts);
  g_slist_free_full(drvopts, (GDestroyNotify)sigrok_free_drvopts);
  if (!devlist) {
    /* Not an error, but the user should know about it. */
    WARNING("sigrok plugin: No device found for \"%s\".", cfdev->name);
    return 0;
  }

  if (g_slist_length(devlist) > 1) {
    INFO("sigrok plugin: %d sigrok devices for device entry "
         "\"%s\": must be 1.",
         g_slist_length(devlist), cfdev->name);
    return -1;
  }
  cfdev->sdi = devlist->data;
  g_slist_free(devlist);
  snprintf(hwident, sizeof(hwident), "%s %s %s",
           cfdev->sdi->vendor ? cfdev->sdi->vendor : "",
           cfdev->sdi->model ? cfdev->sdi->model : "",
           cfdev->sdi->version ? cfdev->sdi->version : "");
  INFO("sigrok plugin: Device \"%s\" is a %s", cfdev->name, hwident);

  if (sr_dev_open(cfdev->sdi) != SR_OK)
    return -1;

  if (sr_session_dev_add(cfdev->sdi) != SR_OK)
    return -1;

  return 1;
}

static void *sigrok_read_thread(void *arg __attribute__((unused))) {
  struct sr_dev_driver *drv, **drvlist;
  GSList *l;
  struct config_device *cfdev;
  int ret, i;

  sr_log_callback_set(sigrok_log_callback, NULL);
  sr_log_loglevel_set(loglevel);

  if ((ret = sr_init(&sr_ctx)) != SR_OK) {
    ERROR("sigrok plugin: Failed to initialize libsigrok: %s.",
          sr_strerror(ret));
    return NULL;
  }

  if (!sr_session_new())
    return NULL;

  num_devices = 0;
  drvlist = sr_driver_list();
  for (l = config_devices; l; l = l->next) {
    cfdev = l->data;
    drv = NULL;
    for (i = 0; drvlist[i]; i++) {
      if (!strcmp(drvlist[i]->name, cfdev->driver)) {
        drv = drvlist[i];
        break;
      }
    }
    if (!drv) {
      ERROR("sigrok plugin: Unknown driver \"%s\".", cfdev->driver);
      return NULL;
    }

    if ((ret = sigrok_init_driver(cfdev, drv)) < 0)
      /* Error was already logged. */
      return NULL;

    num_devices += ret;
  }

  if (num_devices > 0) {
    /* Do this only when we're sure there's hardware to talk to. */
    if (sr_session_datafeed_callback_add(sigrok_feed_callback, NULL) != SR_OK)
      return NULL;

    /* Start acquisition on all devices. */
    if (sr_session_start() != SR_OK)
      return NULL;

    /* Main loop, runs forever. */
    sr_session_run();

    sr_session_stop();
    sr_session_dev_remove_all();
  }

  sr_session_destroy();

  sr_exit(sr_ctx);

  pthread_exit(NULL);
  sr_thread_running = FALSE;

  return NULL;
}

static int sigrok_init(void) {
  int status;

  if (sr_thread_running) {
    ERROR("sigrok plugin: Thread already running.");
    return -1;
  }

  status = plugin_thread_create(&sr_thread, NULL, sigrok_read_thread, NULL,
                                "sigrok read");
  if (status != 0) {
    ERROR("sigrok plugin: Failed to create thread: %s.", STRERRNO);
    return -1;
  }
  sr_thread_running = TRUE;

  return 0;
}

static int sigrok_shutdown(void) {
  struct config_device *cfdev;
  GSList *l;

  if (sr_thread_running) {
    pthread_cancel(sr_thread);
    pthread_join(sr_thread, NULL);
  }

  for (l = config_devices; l; l = l->next) {
    cfdev = l->data;
    free(cfdev->name);
    free(cfdev->driver);
    free(cfdev->conn);
    free(cfdev->serialcomm);
    free(cfdev);
  }
  g_slist_free(config_devices);

  return 0;
}

void module_register(void) {
  plugin_register_complex_config("sigrok", sigrok_config);
  plugin_register_init("sigrok", sigrok_init);
  plugin_register_shutdown("sigrok", sigrok_shutdown);
}
