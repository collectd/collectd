/**
 * collectd - src/utils_curl_stats.c
 * Copyright (C) 2015       Sebastian 'tokkee' Harl
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
 *   Sebastian Harl <sh@tokkee.org>
 *   Barbara 'bkjg' Kaczorowska <bkjg at google.com>
 **/

#include "collectd.h"

#include "utils/common/common.h"
#include "utils/curl_stats/curl_stats.h"

#include <liboconfig/oconfig.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
  metric_family_t *time_fam;
  metric_family_t *size_fam;
  metric_family_t *speed_fam;
  metric_family_t *count_fam;
} attributes_metrics_t;

struct curl_stats_s {
  bool total_time;
  bool namelookup_time;
  bool connect_time;
  bool pretransfer_time;
  bool size_upload;
  bool size_download;
  bool speed_download;
  bool speed_upload;
  bool header_size;
  bool request_size;
  bool content_length_download;
  bool content_length_upload;
  bool starttransfer_time;
  bool redirect_time;
  bool redirect_count;
  bool num_connects;
  bool appconnect_time;
  attributes_metrics_t *metrics;
};

/*
 * Private functions
 */
static int initialize_attributes_metric_families(curl_stats_t *s) {
  s->metrics = calloc(1, sizeof(attributes_metrics_t));

  if (s->metrics == NULL) {
    return -1;
  }

  s->metrics->count_fam = calloc(1, sizeof(metric_family_t));
  s->metrics->size_fam = calloc(1, sizeof(metric_family_t));
  s->metrics->speed_fam = calloc(1, sizeof(metric_family_t));
  s->metrics->time_fam = calloc(1, sizeof(metric_family_t));

  if (s->metrics->count_fam == NULL || s->metrics->size_fam == NULL ||
      s->metrics->speed_fam == NULL || s->metrics->time_fam == NULL) {
    free(s->metrics->count_fam);
    free(s->metrics->size_fam);
    free(s->metrics->speed_fam);
    free(s->metrics->time_fam);
    free(s->metrics);
    return -1;
  }

  s->metrics->count_fam->name = "Count";
  s->metrics->size_fam->name = "Size";
  s->metrics->speed_fam->name = "Speed";
  s->metrics->time_fam->name = "Time";

  s->metrics->count_fam->type = METRIC_TYPE_GAUGE;
  s->metrics->size_fam->type = METRIC_TYPE_DISTRIBUTION;
  s->metrics->speed_fam->type = METRIC_TYPE_DISTRIBUTION;
  s->metrics->time_fam->type = METRIC_TYPE_DISTRIBUTION;

  return 0;
}

static int update_distribution_for_attribute(metric_family_t *fam,
                                             const char *name, double val,
                                             size_t offset) {
  if (strcasecmp(metric_label_get(&fam->metric.ptr[offset], "Attributes"),
                 name) != 0) {
    /* error */
    return -1;
  }

  distribution_update(fam->metric.ptr[offset].value.distribution, val);
  return 0;
}

static int update_gauge_for_attribute(metric_family_t *fam, const char *name,
                                      double val, size_t offset) {
  if (strcasecmp(metric_label_get(&fam->metric.ptr[offset], "Attributes"),
                 name) != 0) {
    /* error */
    return -1;
  }

  fam->metric.ptr[offset].value.gauge = val;

  return 0;
}

static int dispatch_gauge(CURL *curl, CURLINFO info, const char *name) {
  CURLcode code;
  double val;

  code = curl_easy_getinfo(curl, info, &val);
  if (code != CURLE_OK)
    return -1;

  metric_family_t fam = {
      .metric =
          {
              .num = 1,
              .ptr = (metric_t[]){{
                  .value.gauge = val,
              }},
          },
      .name = (char *)name,
  };

  fam.metric.ptr[0].family = &fam;

  return plugin_dispatch_metric_family(&fam);
} /* dispatch_gauge */

/* dispatch a speed, in bytes/second */
static int dispatch_speed(CURL *curl, CURLINFO info, const char *name) {
  CURLcode code;
  double val;

  code = curl_easy_getinfo(curl, info, &val);
  if (code != CURLE_OK)
    return -1;

  metric_family_t fam = {
      .metric =
          {
              .num = 1,
              .ptr = (metric_t[]){{
                  .value.gauge = val * 8,
              }},
          },
      .name = (char *)name,
  };

  fam.metric.ptr[0].family = &fam;

  return plugin_dispatch_metric_family(&fam);
} /* dispatch_speed */

/* dispatch a size/count, reported as a long value */
static int dispatch_size(CURL *curl, CURLINFO info, const char *name) {
  CURLcode code;
  long raw;

  code = curl_easy_getinfo(curl, info, &raw);
  if (code != CURLE_OK)
    return -1;

  metric_family_t fam = {
      .metric =
          {
              .num = 1,
              .ptr = (metric_t[]){{
                  .value.gauge = (double)raw,
              }},
          },
      .name = (char *)name,
  };

  fam.metric.ptr[0].family = &fam;

  return plugin_dispatch_metric_family(&fam);
} /* dispatch_size */

static int account_data_gauge(CURL *curl, CURLINFO info,
                              attributes_metrics_t *attr_m, size_t attr_offset,
                              const char *name, size_t metric_offset) {
  CURLcode code;
  double val;

  code = curl_easy_getinfo(curl, info, &val);
  if (code != CURLE_OK)
    return -1;

  if (attr_offset == offsetof(attributes_metrics_t, count_fam)) {
    return update_gauge_for_attribute(
        (metric_family_t *)((char *)attr_m + attr_offset), name, val,
        metric_offset);
  }

  return update_distribution_for_attribute(
      (metric_family_t *)((char *)attr_m + attr_offset), name, val,
      metric_offset);
} /* account_data_gauge */

static int account_data_speed(CURL *curl, CURLINFO info,
                              attributes_metrics_t *attr_m, size_t attr_offset,
                              const char *name, size_t metric_offset) {
  CURLcode code;
  double val;

  code = curl_easy_getinfo(curl, info, &val);
  if (code != CURLE_OK)
    return -1;

  return update_distribution_for_attribute(
      (metric_family_t *)((char *)attr_m + attr_offset), name, val * 8,
      metric_offset);
} /* account_data_speed */

static int account_data_size(CURL *curl, CURLINFO info,
                             attributes_metrics_t *attr_m, size_t attr_offset,
                             const char *name, size_t metric_offset) {
  CURLcode code;
  long raw;

  code = curl_easy_getinfo(curl, info, &raw);
  if (code != CURLE_OK)
    return -1;

  return update_distribution_for_attribute(
      (metric_family_t *)((char *)attr_m + attr_offset), name, (double)raw,
      metric_offset);
} /* account_data_size */

static struct {
  const char *name;
  const char *config_key;
  const char *metric_family_name;
  size_t config_offset;
  size_t attr_offset;
  size_t metric_family_offset;

  int (*dispatcher)(CURL *, CURLINFO, const char *);
  int (*account_data)(CURL *, CURLINFO, attributes_metrics_t *, size_t,
                      const char *, size_t);

  const char *type;
  CURLINFO info;
} field_specs[] = {
#define SPEC(name, config_key, metric_family_name, dispatcher, account_data,   \
             type, info)                                                       \
  {                                                                            \
#name, config_key, #metric_family_name, offsetof(curl_stats_t, name), 0,    \
        offsetof(attributes_metrics_t, metric_family_name), dispatcher,        \
        account_data, type, info                                               \
  }

    SPEC(total_time, "TotalTime", time_fam, dispatch_gauge,
         account_data_gauge, "duration", CURLINFO_TOTAL_TIME),
    SPEC(namelookup_time, "NamelookupTime", time_fam, dispatch_gauge,
         account_data_gauge, "duration", CURLINFO_NAMELOOKUP_TIME),
    SPEC(connect_time, "ConnectTime", time_fam, dispatch_gauge,
         account_data_gauge, "duration", CURLINFO_CONNECT_TIME),
    SPEC(pretransfer_time, "PretransferTime", time_fam, dispatch_gauge,
         account_data_gauge, "duration", CURLINFO_PRETRANSFER_TIME),
    SPEC(size_upload, "SizeUpload", size_fam, dispatch_gauge,
         account_data_gauge, "bytes", CURLINFO_SIZE_UPLOAD),
    SPEC(size_download, "SizeDownload", time_fam, dispatch_gauge,
         account_data_gauge, "bytes", CURLINFO_SIZE_DOWNLOAD),
    SPEC(speed_download, "SpeedDownload", speed_fam, dispatch_speed,
         account_data_speed, "bitrate", CURLINFO_SPEED_DOWNLOAD),
    SPEC(speed_upload, "SpeedUpload", speed_fam, dispatch_speed,
         account_data_speed, "bitrate", CURLINFO_SPEED_UPLOAD),
    SPEC(header_size, "HeaderSize", size_fam, dispatch_size,
         account_data_size, "bytes", CURLINFO_HEADER_SIZE),
    SPEC(request_size, "RequestSize", size_fam, dispatch_size,
         account_data_size, "bytes", CURLINFO_REQUEST_SIZE),
    SPEC(content_length_download, "ContentLengthDownload", size_fam,
         dispatch_gauge, account_data_gauge, "bytes",
         CURLINFO_CONTENT_LENGTH_DOWNLOAD),
    SPEC(content_length_upload, "ContentLengthUpload", size_fam,
         dispatch_gauge, account_data_gauge, "bytes",
         CURLINFO_CONTENT_LENGTH_UPLOAD),
    SPEC(starttransfer_time, "StarttransferTime", time_fam, dispatch_gauge,
         account_data_gauge, "duration", CURLINFO_STARTTRANSFER_TIME),
    SPEC(redirect_time, "RedirectTime", time_fam, dispatch_gauge,
         account_data_gauge, "duration", CURLINFO_REDIRECT_TIME),
    SPEC(redirect_count, "RedirectCount", count_fam, dispatch_size,
         account_data_size, "count", CURLINFO_REDIRECT_COUNT),
    SPEC(num_connects, "NumConnects", count_fam, dispatch_size,
         account_data_size, "count", CURLINFO_NUM_CONNECTS),
#ifdef HAVE_CURLINFO_APPCONNECT_TIME
    SPEC(appconnect_time, "AppconnectTime", time_fam, dispatch_gauge,
         account_data_gauge, "duration", CURLINFO_APPCONNECT_TIME),
#endif

#undef SPEC
};

/* TODO(bkjg): add initializing the distribution */
static int append_metric_to_metric_family(curl_stats_t *s, size_t *idx,
                                          const char *name, const char *unit) {
  value_t v = {0};
  int status = -1;

  /* TODO(bkjg): what should the arguments for distribution look like? */
  if (!strcasecmp("bytes", unit)) {
    status =
        metric_family_append(s->metrics->size_fam, "Attributes", name, v, NULL);
    *idx = s->metrics->size_fam->metric.num - 1;
  } else if (!strcasecmp("bitrate", unit)) {
    status = metric_family_append(s->metrics->speed_fam, "Attributes", name, v,
                                  NULL);
    *idx = s->metrics->speed_fam->metric.num - 1;
  } else if (!strcasecmp("duration", unit)) {
    status =
        metric_family_append(s->metrics->time_fam, "Attributes", name, v, NULL);
    *idx = s->metrics->time_fam->metric.num - 1;
  } else if (!strcasecmp("count", unit)) {
    status = metric_family_append(s->metrics->count_fam, "Attributes", name, v,
                                  NULL);
    *idx = s->metrics->count_fam->metric.num - 1;
  }

  return status;
}

static void enable_field(curl_stats_t *s, size_t offset) {
  *(bool *)((char *)s + offset) = true;
} /* enable_field */

static bool field_enabled(curl_stats_t *s, size_t offset) {
  return *(bool *)((char *)s + offset);
} /* field_enabled */

/*
 * Public API
 */
curl_stats_t *curl_stats_from_config(oconfig_item_t *ci) {
  curl_stats_t *s;

  if (ci == NULL)
    return NULL;

  s = calloc(1, sizeof(*s));
  if (s == NULL)
    return NULL;

  if (initialize_attributes_metric_families(s) != 0) {
    free(s);
    return NULL;
  }

  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *c = ci->children + i;
    size_t field;

    bool enabled = 0;

    for (field = 0; field < STATIC_ARRAY_SIZE(field_specs); ++field) {
      if (!strcasecmp(c->key, field_specs[field].config_key))
        break;
      if (!strcasecmp(c->key, field_specs[field].name))
        break;
    }

    if (field >= STATIC_ARRAY_SIZE(field_specs)) {
      ERROR("curl stats: Unknown field name %s", c->key);
      free(s);
      return NULL;
    }

    if (cf_util_get_boolean(c, &enabled) != 0) {
      free(s);
      return NULL;
    }

    if (enabled) {
      size_t offset = 0;
      enable_field(s, field_specs[field].config_offset);
      int status = append_metric_to_metric_family(
          s, &offset, field_specs[field].config_key, field_specs[field].type);

      if (status != 0) {
        /* error */
        /* TODO(bkjg): free all data */
        return NULL;
      }

      field_specs[field].metric_family_offset = offset;
    }
  }

  return s;
} /* curl_stats_from_config */

void curl_stats_destroy(curl_stats_t *s) {
  if (s != NULL) {
    metric_family_metric_reset(s->metrics->count_fam);
    metric_family_metric_reset(s->metrics->size_fam);
    metric_family_metric_reset(s->metrics->speed_fam);
    metric_family_metric_reset(s->metrics->time_fam);
    free(s->metrics->count_fam);
    free(s->metrics->size_fam);
    free(s->metrics->speed_fam);
    free(s->metrics->time_fam);
    free(s->metrics);
    free(s);
  }
} /* curl_stats_destroy */

/* TODO: delete unused arguments when all plugins will migrate to the new metric
 * data structure */
int curl_stats_dispatch(curl_stats_t *s, CURL *curl, const char *hostname,
                        const char *plugin, const char *plugin_instance) {
  if (s == NULL)
    return 0;
  if (curl == NULL) {
    ERROR("curl stats: dispatch() called with missing argument: curl=%p", curl);
    return -1;
  }

  for (size_t field = 0; field < STATIC_ARRAY_SIZE(field_specs); ++field) {
    int status;

    if (!field_enabled(s, field_specs[field].config_offset))
      continue;

    status = field_specs[field].dispatcher(curl, field_specs[field].info,
                                           field_specs[field].type);

    if (status < 0) {
      return status;
    }
  }

  return 0;
} /* curl_stats_dispatch */

int curl_stats_account_data(curl_stats_t *s, CURL *curl) {
  if (s == NULL)
    return 0;
  if (curl == NULL) {
    ERROR("curl stats: dispatch() called with missing argument: curl=%p", curl);
    return -1;
  }

  for (size_t field = 0; field < STATIC_ARRAY_SIZE(field_specs); ++field) {
    int status;

    if (!field_enabled(s, field_specs[field].config_offset))
      continue;

    status = field_specs[field].account_data(
        curl, field_specs[field].info, s->metrics,
        field_specs[field].attr_offset, field_specs[field].name,
        field_specs[field].metric_family_offset);

    if (status < 0) {
      return status;
    }
  }

  return 0;
} /* curl_stats_account_data */

int curl_stats_send_metric_to_daemon(curl_stats_t *s) {
  int status;

  status = plugin_dispatch_metric_family(s->metrics->count_fam);
  status |= plugin_dispatch_metric_family(s->metrics->size_fam);
  status |= plugin_dispatch_metric_family(s->metrics->speed_fam);
  status |= plugin_dispatch_metric_family(s->metrics->time_fam);

  /* TODO(bkjg): should I here reset the all metrics??? */

  return status;
} /* curl_stats_send_metric_to_daemon */