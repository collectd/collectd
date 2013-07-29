/*
 * collectd - src/sigrok.c
 * Copyright (C) 2013 Bert Vermeulen <bert@biot.com>
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
 */

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>

#include <glib.h>
#include <libsigrok/libsigrok.h>

/* Minimum interval between dispatches coming from this plugin. The RRD
 * plugin, at least, complains when written to with sub-second intervals.*/
#define DEFAULT_MIN_DISPATCH_INTERVAL TIME_T_TO_CDTIME_T(1)

static pthread_t sr_thread;
static int sr_thread_running = FALSE;
GSList *config_devices;
static struct sr_session *session = NULL;
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


static int cd_logger(void *cb_data, int msg_loglevel, const char *format,
		va_list args)
{
	char s[512];

	if (msg_loglevel <= loglevel) {
		vsnprintf(s, 512, format, args);
		plugin_log(LOG_INFO, "sigrok: %s", s);
	}

	return 0;
}

static int sigrok_config_device(oconfig_item_t *ci)
{
	oconfig_item_t *item;
	struct config_device *cfdev;
	int ret, i;

	if (ci->values_num != 1 || ci->values[0].type != OCONFIG_TYPE_STRING) {
		ERROR("Invalid device name.");
		return 1;
	}

	if (!(cfdev = malloc(sizeof(struct config_device)))) {
		ERROR("malloc() failed.");
		return 1;
	}
	memset(cfdev, 0, sizeof(struct config_device));
	cf_util_get_string(ci, &cfdev->name);
	cfdev->min_dispatch_interval = DEFAULT_MIN_DISPATCH_INTERVAL;

	for (i = 0; i < ci->children_num; i++) {
		item = ci->children + i;
		if (item->values_num != 1) {
			ERROR("Missing value for '%s'.", item->key);
			return 1;
		}
		if (!strcasecmp(item->key, "driver"))
			ret = cf_util_get_string(item, &cfdev->driver);
		else if (!strcasecmp(item->key, "conn"))
			ret = cf_util_get_string(item, &cfdev->conn);
		else if (!strcasecmp(item->key, "serialcomm"))
			ret = cf_util_get_string(item, &cfdev->serialcomm);
		else if (!strcasecmp(item->key, "interval"))
			ret = cf_util_get_cdtime(item, &cfdev->min_dispatch_interval);
		if (ret) {
			ERROR("Invalid keyword '%s'.", item->key);
			return 1;
		}
	}

	config_devices = g_slist_append(config_devices, cfdev);

	return 0;
}

static int sigrok_config(oconfig_item_t *ci)
{
	oconfig_item_t *item;
	int tmp, i;

	for (i = 0; i < ci->children_num; i++) {
		item = ci->children + i;
		if (!strcasecmp(item->key, "loglevel")) {
			if (cf_util_get_int(item, &tmp) || tmp < 0 || tmp > 5) {
				ERROR("Invalid loglevel");
				return 1;
			}
			loglevel = tmp;
		} else if (!strcasecmp(item->key, "Device")) {
			if (sigrok_config_device(item) != 0)
				return 1;
		} else {
			ERROR("Invalid keyword '%s'.", item->key);
			return 1;
		}
	}

	return 0;
}

static void free_drvopts(struct sr_config *src)
{
	g_variant_unref(src->data);
	g_free(src);
}

static void sigrok_feed_callback(const struct sr_dev_inst *sdi,
		const struct sr_datafeed_packet *packet, void *cb_data)
{
	const struct sr_datafeed_analog *analog;
	struct config_device *cfdev;
	GSList *l;
	value_t *values;
	value_list_t vl = VALUE_LIST_INIT;
	int num_probes, s, p;

	if (packet->type == SR_DF_END) {
		/* TODO: try to restart acquisition after a delay? */
		INFO("oops! ended");
		return;
	}

	/* Find this device's configuration. */
	cfdev = NULL;
	for (l = config_devices; l; l = l->next) {
		cfdev = l->data;
		if (cfdev->sdi == sdi) {
			/* Found it. */
			break;
		}
		cfdev = NULL;
	}
	if (!cfdev) {
		ERROR("Unknown device instance in sigrok driver %s.", sdi->driver->name);
		return;
	}

	if (packet->type == SR_DF_ANALOG) {
		if (cdtime() - cfdev->last_dispatch < cfdev->min_dispatch_interval)
			return;

		analog = packet->payload;
		num_probes = g_slist_length(analog->probes);
		if (!(values = malloc(sizeof(value_t) * num_probes))) {
			ERROR("malloc() failed.");
			return;
		}
		for (s = 0; s < analog->num_samples; s++) {
			for (p = 0; p < num_probes; p++) {
				values[s + p].gauge = analog->data[s + p];
			}
		}
		vl.values = values;
		vl.values_len = num_probes;
		sstrncpy(vl.host, hostname_g, sizeof(vl.host));
		sstrncpy(vl.plugin, "sigrok", sizeof(vl.plugin));
		ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance),
				"%s", cfdev->name);
		sstrncpy(vl.type, "gauge", sizeof(vl.type));
		plugin_dispatch_values(&vl);

		cfdev->last_dispatch = cdtime();
	}

}

static int sigrok_init_driver(struct config_device *cfdev,
		struct sr_dev_driver *drv)
{
	struct sr_config *src;
	GSList *devlist, *drvopts;
	char hwident[512];

	if (sr_driver_init(sr_ctx, drv) != SR_OK)
		/* Error was logged by libsigrok. */
		return -1;

	drvopts = NULL;
	if (cfdev->conn) {
		if (!(src = malloc(sizeof(struct sr_config))))
			return -1;
		src->key = SR_CONF_CONN;
		src->data = g_variant_new_string(cfdev->conn);
		drvopts = g_slist_append(drvopts, src);
	}
	if (cfdev->serialcomm) {
		if (!(src = malloc(sizeof(struct sr_config))))
			return -1;
		src->key = SR_CONF_SERIALCOMM;
		src->data = g_variant_new_string(cfdev->serialcomm);
		drvopts = g_slist_append(drvopts, src);
	}
	devlist = sr_driver_scan(drv, drvopts);
	g_slist_free_full(drvopts, (GDestroyNotify)free_drvopts);
	if (!devlist)
		/* No devices found for this driver, not an error. */
		return 0;

	if (g_slist_length(devlist) > 1) {
		INFO("sigrok: %d sigrok devices for device entry '%s': must be 1.",
				g_slist_length(devlist), cfdev->name);
		return -1;
	}
	cfdev->sdi = devlist->data;
	g_slist_free(devlist);
	ssnprintf(hwident, sizeof(hwident), "%s %s %s",
			cfdev->sdi->vendor ? cfdev->sdi->vendor : "",
			cfdev->sdi->model ? cfdev->sdi->model : "",
			cfdev->sdi->version ? cfdev->sdi->version : "");
	INFO("sigrok: Device '%s' is a %s", cfdev->name, hwident);

	if (sr_dev_open(cfdev->sdi) != SR_OK)
		return -1;

	if (sr_session_dev_add(cfdev->sdi) != SR_OK)
		return -1;

	return 1;
}

static void *thread_init(void *arg __attribute__((unused)))
{
	struct sr_dev_driver *drv, **drvlist;
	GSList *l;
	struct config_device *cfdev;
	int ret, i;

	sr_log_callback_set(cd_logger, NULL);
	sr_log_loglevel_set(loglevel);

	if ((ret = sr_init(&sr_ctx)) != SR_OK) {
		ERROR("Failed to initialize libsigrok: %s.", sr_strerror(ret));
		return NULL;
	}

	if (!(session = sr_session_new()))
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
			ERROR("sigrok: Unknown driver '%s'.", cfdev->driver);
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

static int sigrok_init(void)
{
	int status;

	if (sr_thread_running) {
		ERROR("sigrok: Thread already running.");
		return -1;
	}

	if ((status = plugin_thread_create(&sr_thread, NULL, thread_init, NULL)) != 0) {
		ERROR("sigrok: Failed to create thread: %s.", strerror(status));
		return -1;
	}
	sr_thread_running = TRUE;

	return 0;
}

static int sigrok_shutdown(void)
{
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

void module_register(void)
{

	plugin_register_complex_config("sigrok", sigrok_config);
	plugin_register_init("sigrok", sigrok_init);
	plugin_register_shutdown("sigrok", sigrok_shutdown);

}
