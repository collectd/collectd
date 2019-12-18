/*
 * collectd - src/dcpmm.c
 * MIT License
 *
 * Copyright (C) 2019 Intel Corporation. All rights reserved.
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
 *   Hari TG <hari.tg at intel.com>
 */

#include "collectd.h"
#include "utils/common/common.h"

#include "pmw_api.h"

#define PLUGIN_NAME "dcpmm"
#define PRINT_BOOL(s) (s ? "true" : "false")

int num_nvdimms;
int skip_stop = 0;
bool enable_dispatch_all = false;
cdtime_t interval = 0;
PMWATCH_OP_BUF pmw_output_buf;
PMWATCH_CONFIG_NODE pmwatch_config;

static void add_metric(const char *plugin_inst, const char *type,
                       const char *type_inst, gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;

  sstrncpy(vl.plugin, PLUGIN_NAME, sizeof(vl.plugin));
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.type, type, sizeof(vl.type));

  if (plugin_inst != NULL) {
    sstrncpy(vl.plugin_instance, plugin_inst, sizeof(vl.plugin_instance));
  }

  if (type_inst != NULL) {
    sstrncpy(vl.type_instance, type_inst, sizeof(vl.type_instance));
  }

  plugin_dispatch_values(&vl);

#if COLLECT_DEBUG

  notification_t n = {
      .severity = NOTIF_OKAY, .time = cdtime(), .plugin = PLUGIN_NAME};

  if (strncmp(type_inst, "read_hit_ratio", strlen("read_hit_ratio")) == 0 ||
      strncmp(type_inst, "write_hit_ratio", strlen("write_hit_ratio")) == 0 ||
      strncmp(type_inst, "media_temperature", strlen("media_temperature")) ==
          0 ||
      strncmp(type_inst, "controller_temperature",
              strlen("controller_temperature")) == 0) {
    snprintf(n.message, sizeof(n.message), "Value: %0.2f", value);
  } else {
    snprintf(n.message, sizeof(n.message), "Value: %0.0f", value);
  }
  sstrncpy(n.host, hostname_g, sizeof(n.host));
  sstrncpy(n.type, type, sizeof(n.type));
  sstrncpy(n.type_instance, type_inst, sizeof(n.type_instance));
  sstrncpy(n.plugin_instance, plugin_inst, sizeof(n.plugin_instance));

  plugin_dispatch_notification(&n);

#endif /* COLLECT_DEBUG */

  return;
} /* void add_metric  */

static int dcpmm_read(__attribute__((unused)) user_data_t *ud) {
  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);

  int i, ret = 0;
  char dimm_num[16];

  ret = PMWAPIRead(&pmw_output_buf);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to read data from the collection.");

    return ret;
  }

  for (i = 0; i < num_nvdimms; i++) {
    snprintf(dimm_num, sizeof(dimm_num), "%d", i);

    if (pmwatch_config.collect_perf_metrics) {
      add_metric(dimm_num, "timestamp", "epoch",
                 PMWATCH_OP_BUF_EPOCH(&pmw_output_buf[i]));
      add_metric(dimm_num, "timestamp", "tsc_cycles",
                 PMWATCH_OP_BUF_TIMESTAMP(&pmw_output_buf[i]));
      add_metric(dimm_num, "media", "total_bytes_read",
                 PMWATCH_OP_BUF_TOTAL_BYTES_READ(&pmw_output_buf[i]));
      add_metric(dimm_num, "media", "total_bytes_written",
                 PMWATCH_OP_BUF_TOTAL_BYTES_WRITTEN(&pmw_output_buf[i]));
      add_metric(dimm_num, "media", "read_64B_ops_rcvd",
                 PMWATCH_OP_BUF_BYTES_READ(&pmw_output_buf[i]));
      add_metric(dimm_num, "media", "write_64B_ops_rcvd",
                 PMWATCH_OP_BUF_BYTES_WRITTEN(&pmw_output_buf[i]));
      add_metric(dimm_num, "media", "media_read_ops",
                 PMWATCH_OP_BUF_MEDIA_READ(&pmw_output_buf[i]));
      add_metric(dimm_num, "media", "media_write_ops",
                 PMWATCH_OP_BUF_MEDIA_WRITE(&pmw_output_buf[i]));
      add_metric(dimm_num, "controller", "host_reads",
                 PMWATCH_OP_BUF_HOST_READS(&pmw_output_buf[i]));
      add_metric(dimm_num, "controller", "host_writes",
                 PMWATCH_OP_BUF_HOST_WRITES(&pmw_output_buf[i]));
      add_metric(dimm_num, "buffer", "read_hit_ratio",
                 PMWATCH_OP_BUF_READ_HIT_RATIO(&pmw_output_buf[i]));
      add_metric(dimm_num, "buffer", "write_hit_ratio",
                 PMWATCH_OP_BUF_WRITE_HIT_RATIO(&pmw_output_buf[i]));
    }

    if (pmwatch_config.collect_health) {
      if (pmwatch_config.collect_perf_metrics && !enable_dispatch_all) {
        continue;
      }
      add_metric(dimm_num, "timestamp", "epoch",
                 PMWATCH_OP_BUF_EPOCH(&pmw_output_buf[i]));
      add_metric(dimm_num, "timestamp", "tsc_cycles",
                 PMWATCH_OP_BUF_TIMESTAMP(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "health_status",
                 PMWATCH_OP_BUF_HEALTH_STATUS(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "lifespan_remaining",
                 PMWATCH_OP_BUF_PERCENTAGE_REMAINING(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "lifespan_used",
                 PMWATCH_OP_BUF_PERCENTAGE_USED(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "power_on_time",
                 PMWATCH_OP_POWER_ON_TIME(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "uptime",
                 PMWATCH_OP_BUF_UPTIME(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "last_shutdown_time",
                 PMWATCH_OP_BUF_LAST_SHUTDOWN_TIME(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "media_temperature",
                 PMWATCH_OP_BUF_MEDIA_TEMP(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "controller_temperature",
                 PMWATCH_OP_BUF_CONTROLLER_TEMP(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "max_media_temperature",
                 PMWATCH_OP_BUF_MAX_MEDIA_TEMP(&pmw_output_buf[i]));
      add_metric(dimm_num, "health", "max_controller_temperature",
                 PMWATCH_OP_BUF_MAX_CONTROLLER_TEMP(&pmw_output_buf[i]));
    }
  }

  return 0;
} /* int dcpmm_read */

static int dcpmm_stop(void) {
  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);

  int ret = 0;

  if (skip_stop) {
    DEBUG(PLUGIN_NAME ": %s:%d skipping stop function", __FUNCTION__, __LINE__);

    return ret;
  }

  ret = PMWAPIStop();
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to stop the collection.");
  }

  return ret;
} /* int dcpmm_stop */

static int dcpmm_shutdown(void) {
  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);

  int ret = 0;

  free(pmw_output_buf);

  ret = dcpmm_stop();

  return ret;
} /* int dcpmm_shutdown */

static int dcpmm_init(void) {
  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);

  int ret = 0;

  ret = PMWAPIGetDIMMCount(&num_nvdimms);
  if (ret != 0) {
    ERROR(PLUGIN_NAME
          ": Failed to obtain count of Intel(R) Optane DCPMM. "
          "A common cause for this is collectd running without "
          "root privileges. Ensure that collectd is running with "
          "root privileges. Also, make sure that Intel(R) Optane DC "
          "Persistent Memory is available in the system.");
    skip_stop = 1;

    return ret;
  }

  ret = PMWAPIStart(pmwatch_config);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to start the collection. "
                      "A common cause for this is collectd running without "
                      "root privileges. Ensure that collectd is running with "
                      "root privileges.");
    skip_stop = 1;

    return ret;
  }

  pmw_output_buf =
      (PMWATCH_OP_BUF)calloc(num_nvdimms, sizeof(PMWATCH_OP_BUF_NODE));
  if (pmw_output_buf == NULL) {
    ERROR(PLUGIN_NAME ": Memory allocation for output buffer failed.");
    dcpmm_stop();
    skip_stop = 1;
    ret = 1;
  }

  return ret;
} /* int dcpmm_init */

static int dcpmm_config(oconfig_item_t *ci) {
  DEBUG(PLUGIN_NAME ": %s:%d", __FUNCTION__, __LINE__);

  int ret = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strncasecmp("Interval", child->key, strlen("Interval")) == 0) {
      ret = cf_util_get_cdtime(child, &interval);
      if (!ret) {
        ret = cf_util_get_double(child, &pmwatch_config.interval);
      }
    } else if (strncasecmp("CollectHealth", child->key,
                           strlen("CollectHealth")) == 0) {
      ret = cf_util_get_boolean(child, &pmwatch_config.collect_health);

    } else if (strncasecmp("CollectPerfMetrics", child->key,
                           strlen("CollectPerfMetrics")) == 0) {
      ret = cf_util_get_boolean(child, &pmwatch_config.collect_perf_metrics);
    } else if (strncasecmp("EnableDispatchAll", child->key,
                           strlen("EnableDispatchAll")) == 0) {
      ret = cf_util_get_boolean(child, &enable_dispatch_all);
    } else {
      ERROR(PLUGIN_NAME ": Unkown configuration parameter %s.", child->key);
      ret = 1;
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Failed to parse configuration parameters");
      return ret;
    }
  }

  DEBUG("%s Config: Interval %.2f ; CollectHealth %s ; CollectdPerfMetrics %s "
        "; EnableDispatchAll %s",
        PLUGIN_NAME, pmwatch_config.interval,
        PRINT_BOOL(pmwatch_config.collect_health),
        PRINT_BOOL(pmwatch_config.collect_perf_metrics),
        PRINT_BOOL(enable_dispatch_all));

  if (!pmwatch_config.collect_health && !pmwatch_config.collect_perf_metrics) {
    ERROR(PLUGIN_NAME ": CollectdHealth and CollectPerfMetrics are disabled. "
                      "Enable atleast one.");
    return 1;
  }

  plugin_register_complex_read(NULL, PLUGIN_NAME, dcpmm_read, interval, NULL);

  return 0;
} /* int dcpmm_config */

void module_register(void) {
  plugin_register_init(PLUGIN_NAME, dcpmm_init);
  plugin_register_complex_config(PLUGIN_NAME, dcpmm_config);
  plugin_register_shutdown(PLUGIN_NAME, dcpmm_shutdown);
} /* void module_register */
