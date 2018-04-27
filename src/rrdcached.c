/**
 * collectd - src/rrdcached.c
 * Copyright (C) 2008-2013  Florian octo Forster
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
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_rrdcreate.h"

#undef HAVE_CONFIG_H
#include <rrd.h>
#include <rrd_client.h>

/*
 * Private variables
 */
static char *datadir = NULL;
static char *daemon_address = NULL;
static _Bool config_create_files = 1;
static _Bool config_collect_stats = 1;
static rrdcreate_config_t rrdcreate_config = {
    /* stepsize = */ 0,
    /* heartbeat = */ 0,
    /* rrarows = */ 1200,
    /* xff = */ 0.1,

    /* timespans = */ NULL,
    /* timespans_num = */ 0,

    /* consolidation_functions = */ NULL,
    /* consolidation_functions_num = */ 0,

    /* async = */ 0};

/*
 * Prototypes.
 */
static int rc_write(const data_set_t *ds, const value_list_t *vl,
                    user_data_t __attribute__((unused)) * user_data);
static int rc_flush(__attribute__((unused)) cdtime_t timeout,
                    const char *identifier,
                    __attribute__((unused)) user_data_t *ud);

static int value_list_to_string(char *buffer, int buffer_len,
                                const data_set_t *ds, const value_list_t *vl) {
  int offset;
  int status;
  time_t t;

  assert(0 == strcmp(ds->type, vl->type));

  memset(buffer, '\0', buffer_len);

  t = CDTIME_T_TO_TIME_T(vl->time);
  status = snprintf(buffer, buffer_len, "%lu", (unsigned long)t);
  if ((status < 1) || (status >= buffer_len))
    return -1;
  offset = status;

  for (size_t i = 0; i < ds->ds_num; i++) {
    if ((ds->ds[i].type != DS_TYPE_COUNTER) &&
        (ds->ds[i].type != DS_TYPE_GAUGE) &&
        (ds->ds[i].type != DS_TYPE_DERIVE) &&
        (ds->ds[i].type != DS_TYPE_ABSOLUTE))
      return -1;

    if (ds->ds[i].type == DS_TYPE_COUNTER) {
      status = snprintf(buffer + offset, buffer_len - offset, ":%" PRIu64,
                        (uint64_t)vl->values[i].counter);
    } else if (ds->ds[i].type == DS_TYPE_GAUGE) {
      status = snprintf(buffer + offset, buffer_len - offset, ":%f",
                        vl->values[i].gauge);
    } else if (ds->ds[i].type == DS_TYPE_DERIVE) {
      status = snprintf(buffer + offset, buffer_len - offset, ":%" PRIi64,
                        vl->values[i].derive);
    } else /* if (ds->ds[i].type == DS_TYPE_ABSOLUTE) */ {
      status = snprintf(buffer + offset, buffer_len - offset, ":%" PRIu64,
                        vl->values[i].absolute);
    }

    if ((status < 1) || (status >= (buffer_len - offset)))
      return -1;

    offset += status;
  } /* for ds->ds_num */

  return 0;
} /* int value_list_to_string */

static int value_list_to_filename(char *buffer, size_t buffer_size,
                                  value_list_t const *vl) {
  char const suffix[] = ".rrd";
  int status;
  size_t len;

  if (datadir != NULL) {
    size_t datadir_len = strlen(datadir) + 1;

    if (datadir_len >= buffer_size)
      return ENOMEM;

    sstrncpy(buffer, datadir, buffer_size);
    buffer[datadir_len - 1] = '/';
    buffer[datadir_len] = 0;

    buffer += datadir_len;
    buffer_size -= datadir_len;
  }

  status = FORMAT_VL(buffer, buffer_size, vl);
  if (status != 0)
    return status;

  len = strlen(buffer);
  assert(len < buffer_size);
  buffer += len;
  buffer_size -= len;

  if (buffer_size <= sizeof(suffix))
    return ENOMEM;

  memcpy(buffer, suffix, sizeof(suffix));
  return 0;
} /* int value_list_to_filename */

static int rc_config_get_int_positive(oconfig_item_t const *ci, int *ret) {
  int status;
  int tmp = 0;

  status = cf_util_get_int(ci, &tmp);
  if (status != 0)
    return status;
  if (tmp < 0)
    return EINVAL;

  *ret = tmp;
  return 0;
} /* int rc_config_get_int_positive */

static int rc_config_get_xff(oconfig_item_t const *ci, double *ret) {
  double value;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER)) {
    ERROR("rrdcached plugin: The \"%s\" needs exactly one numeric argument "
          "in the range [0.0, 1.0)",
          ci->key);
    return EINVAL;
  }

  value = ci->values[0].value.number;
  if ((value >= 0.0) && (value < 1.0)) {
    *ret = value;
    return 0;
  }

  ERROR("rrdcached plugin: The \"%s\" needs exactly one numeric argument "
        "in the range [0.0, 1.0)",
        ci->key);
  return EINVAL;
} /* int rc_config_get_xff */

static int rc_config_add_timespan(int timespan) {
  int *tmp;

  if (timespan <= 0)
    return EINVAL;

  tmp = realloc(rrdcreate_config.timespans,
                sizeof(*rrdcreate_config.timespans) *
                    (rrdcreate_config.timespans_num + 1));
  if (tmp == NULL)
    return ENOMEM;
  rrdcreate_config.timespans = tmp;

  rrdcreate_config.timespans[rrdcreate_config.timespans_num] = timespan;
  rrdcreate_config.timespans_num++;

  return 0;
} /* int rc_config_add_timespan */

static int rc_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t const *child = ci->children + i;
    const char *key = child->key;
    int status = 0;

    if (strcasecmp("DataDir", key) == 0) {
      status = cf_util_get_string(child, &datadir);
      if (status == 0) {
        int len = strlen(datadir);

        while ((len > 0) && (datadir[len - 1] == '/')) {
          len--;
          datadir[len] = 0;
        }

        if (len <= 0)
          sfree(datadir);
      }
    } else if (strcasecmp("DaemonAddress", key) == 0)
      status = cf_util_get_string(child, &daemon_address);
    else if (strcasecmp("CreateFiles", key) == 0)
      status = cf_util_get_boolean(child, &config_create_files);
    else if (strcasecmp("CreateFilesAsync", key) == 0)
      status = cf_util_get_boolean(child, &rrdcreate_config.async);
    else if (strcasecmp("CollectStatistics", key) == 0)
      status = cf_util_get_boolean(child, &config_collect_stats);
    else if (strcasecmp("StepSize", key) == 0) {
      int tmp = -1;

      status = rc_config_get_int_positive(child, &tmp);
      if (status == 0)
        rrdcreate_config.stepsize = (unsigned long)tmp;
    } else if (strcasecmp("HeartBeat", key) == 0)
      status = rc_config_get_int_positive(child, &rrdcreate_config.heartbeat);
    else if (strcasecmp("RRARows", key) == 0)
      status = rc_config_get_int_positive(child, &rrdcreate_config.rrarows);
    else if (strcasecmp("RRATimespan", key) == 0) {
      int tmp = -1;
      status = rc_config_get_int_positive(child, &tmp);
      if (status == 0)
        status = rc_config_add_timespan(tmp);
    } else if (strcasecmp("XFF", key) == 0)
      status = rc_config_get_xff(child, &rrdcreate_config.xff);
    else {
      WARNING("rrdcached plugin: Ignoring invalid option %s.", key);
      continue;
    }

    if (status != 0)
      WARNING("rrdcached plugin: Handling the \"%s\" option failed.", key);
  }

  if (daemon_address != NULL) {
    plugin_register_write("rrdcached", rc_write, /* user_data = */ NULL);
    plugin_register_flush("rrdcached", rc_flush, /* user_data = */ NULL);
  }
  return 0;
} /* int rc_config */

static int try_reconnect(void) {
  int status;

  rrdc_disconnect();

  rrd_clear_error();
  status = rrdc_connect(daemon_address);
  if (status != 0) {
    ERROR("rrdcached plugin: Failed to reconnect to RRDCacheD "
          "at %s: %s (status=%d)",
          daemon_address, rrd_get_error(), status);
    return -1;
  }

  INFO("rrdcached plugin: Successfully reconnected to RRDCacheD "
       "at %s",
       daemon_address);
  return 0;
} /* int try_reconnect */

static int rc_read(void) {
  int status;
  rrdc_stats_t *head;
  _Bool retried = 0;

  value_list_t vl = VALUE_LIST_INIT;
  vl.values = &(value_t){.gauge = NAN};
  vl.values_len = 1;

  if (daemon_address == NULL)
    return -1;

  if (!config_collect_stats)
    return -1;

  if ((strncmp("unix:", daemon_address, strlen("unix:")) != 0) &&
      (daemon_address[0] != '/'))
    sstrncpy(vl.host, daemon_address, sizeof(vl.host));
  sstrncpy(vl.plugin, "rrdcached", sizeof(vl.plugin));

  rrd_clear_error();
  status = rrdc_connect(daemon_address);
  if (status != 0) {
    ERROR("rrdcached plugin: Failed to connect to RRDCacheD "
          "at %s: %s (status=%d)",
          daemon_address, rrd_get_error(), status);
    return -1;
  }

  while (42) {
    /* The RRD client lib does not provide any means for checking a
     * connection, hence we'll have to retry upon failed operations. */
    head = NULL;
    rrd_clear_error();
    status = rrdc_stats_get(&head);
    if (status == 0)
      break;

    if (!retried) {
      retried = 1;
      if (try_reconnect() == 0)
        continue;
      /* else: report the error and fail */
    }

    ERROR("rrdcached plugin: rrdc_stats_get failed: %s (status=%i).",
          rrd_get_error(), status);
    return -1;
  }

  for (rrdc_stats_t *ptr = head; ptr != NULL; ptr = ptr->next) {
    if (ptr->type == RRDC_STATS_TYPE_GAUGE)
      vl.values[0].gauge = (gauge_t)ptr->value.gauge;
    else if (ptr->type == RRDC_STATS_TYPE_COUNTER)
      vl.values[0].counter = (counter_t)ptr->value.counter;
    else
      continue;

    if (strcasecmp("QueueLength", ptr->name) == 0) {
      sstrncpy(vl.type, "queue_length", sizeof(vl.type));
      sstrncpy(vl.type_instance, "", sizeof(vl.type_instance));
    } else if (strcasecmp("UpdatesWritten", ptr->name) == 0) {
      sstrncpy(vl.type, "operations", sizeof(vl.type));
      sstrncpy(vl.type_instance, "write-updates", sizeof(vl.type_instance));
    } else if (strcasecmp("DataSetsWritten", ptr->name) == 0) {
      sstrncpy(vl.type, "operations", sizeof(vl.type));
      sstrncpy(vl.type_instance, "write-data_sets", sizeof(vl.type_instance));
    } else if (strcasecmp("TreeNodesNumber", ptr->name) == 0) {
      sstrncpy(vl.type, "gauge", sizeof(vl.type));
      sstrncpy(vl.type_instance, "tree_nodes", sizeof(vl.type_instance));
    } else if (strcasecmp("TreeDepth", ptr->name) == 0) {
      sstrncpy(vl.type, "gauge", sizeof(vl.type));
      sstrncpy(vl.type_instance, "tree_depth", sizeof(vl.type_instance));
    } else if (strcasecmp("FlushesReceived", ptr->name) == 0) {
      sstrncpy(vl.type, "operations", sizeof(vl.type));
      sstrncpy(vl.type_instance, "receive-flush", sizeof(vl.type_instance));
    } else if (strcasecmp("JournalBytes", ptr->name) == 0) {
      sstrncpy(vl.type, "counter", sizeof(vl.type));
      sstrncpy(vl.type_instance, "journal-bytes", sizeof(vl.type_instance));
    } else if (strcasecmp("JournalRotate", ptr->name) == 0) {
      sstrncpy(vl.type, "counter", sizeof(vl.type));
      sstrncpy(vl.type_instance, "journal-rotates", sizeof(vl.type_instance));
    } else if (strcasecmp("UpdatesReceived", ptr->name) == 0) {
      sstrncpy(vl.type, "operations", sizeof(vl.type));
      sstrncpy(vl.type_instance, "receive-update", sizeof(vl.type_instance));
    } else {
      DEBUG("rrdcached plugin: rc_read: Unknown statistic `%s'.", ptr->name);
      continue;
    }

    plugin_dispatch_values(&vl);
  } /* for (ptr = head; ptr != NULL; ptr = ptr->next) */

  rrdc_stats_free(head);

  return 0;
} /* int rc_read */

static int rc_init(void) {
  if (config_collect_stats)
    plugin_register_read("rrdcached", rc_read);

  return 0;
} /* int rc_init */

static int rc_write(const data_set_t *ds, const value_list_t *vl,
                    user_data_t __attribute__((unused)) * user_data) {
  char filename[PATH_MAX];
  char values[512];
  char *values_array[2];
  int status;
  _Bool retried = 0;

  if (daemon_address == NULL) {
    ERROR("rrdcached plugin: daemon_address == NULL.");
    plugin_unregister_write("rrdcached");
    return -1;
  }

  if (strcmp(ds->type, vl->type) != 0) {
    ERROR("rrdcached plugin: DS type does not match value list type");
    return -1;
  }

  if (value_list_to_filename(filename, sizeof(filename), vl) != 0) {
    ERROR("rrdcached plugin: value_list_to_filename failed.");
    return -1;
  }

  if (value_list_to_string(values, sizeof(values), ds, vl) != 0) {
    ERROR("rrdcached plugin: value_list_to_string failed.");
    return -1;
  }

  values_array[0] = values;
  values_array[1] = NULL;

  if (config_create_files) {
    struct stat statbuf;

    status = stat(filename, &statbuf);
    if (status != 0) {
      if (errno != ENOENT) {
        ERROR("rrdcached plugin: stat (%s) failed: %s", filename, STRERRNO);
        return -1;
      }

      status = cu_rrd_create_file(filename, ds, vl, &rrdcreate_config);
      if (status != 0) {
        ERROR("rrdcached plugin: cu_rrd_create_file (%s) failed.", filename);
        return -1;
      } else if (rrdcreate_config.async)
        return 0;
    }
  }

  rrd_clear_error();
  status = rrdc_connect(daemon_address);
  if (status != 0) {
    ERROR("rrdcached plugin: Failed to connect to RRDCacheD "
          "at %s: %s (status=%d)",
          daemon_address, rrd_get_error(), status);
    return -1;
  }

  while (42) {
    /* The RRD client lib does not provide any means for checking a
     * connection, hence we'll have to retry upon failed operations. */
    rrd_clear_error();
    status = rrdc_update(filename, /* values_num = */ 1, (void *)values_array);
    if (status == 0)
      break;

    if (!retried) {
      retried = 1;
      if (try_reconnect() == 0)
        continue;
      /* else: report the error and fail */
    }

    ERROR("rrdcached plugin: rrdc_update (%s, [%s], 1) failed: %s (status=%i)",
          filename, values_array[0], rrd_get_error(), status);
    return -1;
  }

  return 0;
} /* int rc_write */

static int rc_flush(__attribute__((unused)) cdtime_t timeout, /* {{{ */
                    const char *identifier,
                    __attribute__((unused)) user_data_t *ud) {
  char filename[PATH_MAX + 1];
  int status;
  _Bool retried = 0;

  if (identifier == NULL)
    return EINVAL;

  if (datadir != NULL)
    snprintf(filename, sizeof(filename), "%s/%s.rrd", datadir, identifier);
  else
    snprintf(filename, sizeof(filename), "%s.rrd", identifier);

  rrd_clear_error();
  status = rrdc_connect(daemon_address);
  if (status != 0) {
    ERROR("rrdcached plugin: Failed to connect to RRDCacheD "
          "at %s: %s (status=%d)",
          daemon_address, rrd_get_error(), status);
    return -1;
  }

  while (42) {
    /* The RRD client lib does not provide any means for checking a
     * connection, hence we'll have to retry upon failed operations. */
    rrd_clear_error();
    status = rrdc_flush(filename);
    if (status == 0)
      break;

    if (!retried) {
      retried = 1;
      if (try_reconnect() == 0)
        continue;
      /* else: report the error and fail */
    }

    ERROR("rrdcached plugin: rrdc_flush (%s) failed: %s (status=%i).", filename,
          rrd_get_error(), status);
    return -1;
  }
  DEBUG("rrdcached plugin: rrdc_flush (%s): Success.", filename);

  return 0;
} /* }}} int rc_flush */

static int rc_shutdown(void) {
  rrdc_disconnect();
  return 0;
} /* int rc_shutdown */

void module_register(void) {
  plugin_register_complex_config("rrdcached", rc_config);
  plugin_register_init("rrdcached", rc_init);
  plugin_register_shutdown("rrdcached", rc_shutdown);
} /* void module_register */
