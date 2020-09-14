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

#include "distribution.h"

#include "utils/common/common.h"
#include "utils/curl_stats/curl_stats.h"

#include <liboconfig/oconfig.h>
#include <stdbool.h>
#include <stddef.h>

typedef struct {
  char *distribution_type;
  size_t num_buckets;
  double base;
  double factor;
  size_t num_boundaries;
  double *boundaries;
  size_t num_boundaries_from_array;
} distribution_specs_t;

typedef struct {
  metric_family_t *count_fam;
  metric_family_t *size_fam;
  metric_family_t *speed_fam;
  metric_family_t *time_fam;
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

static const int SIZE_ATTR = 0;
static const int SPEED_ATTR = 1;
static const int TIME_ATTR = 2;
static const int NUM_ATTR = 3;

/*
 * Private functions
 */
static distribution_t **
parse_metric_from_config(distribution_specs_t dists_specs[NUM_ATTR]) {
  const size_t MAX_NUM_BUCKETS = 1024;
  if (dists_specs == NULL) {
    return NULL;
  }

  /* idx = 0 <- per size attribute
   * idx = 1 <- per speed attribute
   * idx = 2 <- per time attribute */
  distribution_t **d = calloc(NUM_ATTR, sizeof(distribution_t *));

  if (d == NULL) {
    return NULL;
  }

  static const double default_linear_base_per_attr[] = {8.0, 16.0, 0.001};

  static const double default_exponential_base_per_attr[] = {2.0, 1.25, 1.1};

  static const double default_factor_per_attr[] = {2.0, 8.0, 0.001};

  for (int attr = 0; attr < NUM_ATTR; ++attr) {
    if (dists_specs[attr].distribution_type == NULL) {
      d[attr] = distribution_new_linear(MAX_NUM_BUCKETS,
                                        default_linear_base_per_attr[attr]);
    } else if (!strcasecmp(dists_specs[attr].distribution_type, "Linear")) {
      size_t num_buckets;

      if (dists_specs[attr].num_buckets == 0) {
        num_buckets = MAX_NUM_BUCKETS;
      } else {
        num_buckets = dists_specs[attr].num_buckets;
      }

      double base;
      if (dists_specs[attr].base == 0 && dists_specs[attr].factor == 0) {
        base = default_linear_base_per_attr[attr];
      } else if (dists_specs[attr].base == 0) {
        base = dists_specs[attr].factor;
      } else {
        base = dists_specs[attr].base;
      }

      d[attr] = distribution_new_linear(num_buckets, base);
    } else if (!strcasecmp(dists_specs[attr].distribution_type,
                           "Exponential")) {
      size_t num_buckets;

      if (dists_specs[attr].num_buckets == 0) {
        num_buckets = MAX_NUM_BUCKETS;
      } else {
        num_buckets = dists_specs[attr].num_buckets;
      }

      double base, factor;
      if (dists_specs[attr].base == 0 && dists_specs[attr].factor == 0) {
        base = default_exponential_base_per_attr[attr];
        factor = default_factor_per_attr[attr];
      } else if (dists_specs[attr].base == 0) {
        base = default_exponential_base_per_attr[attr];
        factor = dists_specs[attr].factor;
      } else if (dists_specs[attr].factor == 0) {
        base = dists_specs[attr].base;
        factor = default_factor_per_attr[attr];
      } else {
        base = dists_specs[attr].base;
        factor = dists_specs[attr].factor;
      }

      d[attr] = distribution_new_exponential(num_buckets, base, factor);
    } else if (!strcasecmp(dists_specs[attr].distribution_type, "Custom")) {
      if (dists_specs[attr].boundaries == NULL) {
        ERROR("curl_stats_from_config: Buckets boundaries for distribution "
              "type custom are required!");

        for (int i = 0; i < attr; ++i) {
          distribution_destroy(d[i]);
        }

        free(d);

        return NULL;
      }
      size_t num_boundaries;

      if (dists_specs[attr].num_boundaries == 0) {
        num_boundaries = dists_specs[attr].num_boundaries_from_array;
      } else if (dists_specs[attr].num_boundaries !=
                 dists_specs[attr].num_boundaries_from_array) {
        ERROR("curl_stats_from_config: Wrong number of buckets boundaries is "
              "provided. Required: %ld, received %ld!",
              dists_specs[attr].num_boundaries,
              dists_specs[attr].num_boundaries_from_array);

        for (int i = 0; i < attr; ++i) {
          distribution_destroy(d[i]);
        }

        free(d);

        return NULL;
      } else {
        num_boundaries = dists_specs[attr].num_boundaries;
      }

      double *boundaries = dists_specs[attr].boundaries;
      d[attr] = distribution_new_custom(num_boundaries, boundaries);
    } else {
      ERROR("curl_stats_from_config: distribution type: %s is not supported!",
            dists_specs[attr].distribution_type);

      for (int i = 0; i < attr; ++i) {
        distribution_destroy(d[i]);
      }

      free(d);

      return NULL;
    }

    if (d[attr] == NULL) {
      ERROR("curl_stats_from_config: Creating distribution failed!");

      for (int i = 0; i < attr; ++i) {
        distribution_destroy(d[i]);
      }

      free(d);

      return NULL;
    }
  }

  return d;
}

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

static int initialize_distributions_for_metrics(curl_stats_t *s,
                                                distribution_t *d[NUM_ATTR]) {
  for (size_t i = 0; i < s->metrics->size_fam->metric.num; ++i) {
    s->metrics->size_fam->metric.ptr[i].value.distribution =
        distribution_clone(d[SIZE_ATTR]);

    if (s->metrics->size_fam->metric.ptr[i].value.distribution == NULL) {
      return -1;
    }
  }

  for (size_t i = 0; i < s->metrics->speed_fam->metric.num; ++i) {
    s->metrics->speed_fam->metric.ptr[i].value.distribution =
        distribution_clone(d[SPEED_ATTR]);

    if (s->metrics->speed_fam->metric.ptr[i].value.distribution == NULL) {
      return -1;
    }
  }

  for (size_t i = 0; i < s->metrics->time_fam->metric.num; ++i) {
    s->metrics->time_fam->metric.ptr[i].value.distribution =
        distribution_clone(d[TIME_ATTR]);

    if (s->metrics->time_fam->metric.ptr[i].value.distribution == NULL) {
      return -1;
    }
  }

  return 0;
}

static int update_distribution_for_attribute(metric_family_t *fam,
                                             const char *name, double val,
                                             size_t offset) {
  if (strcasecmp(metric_label_get(&fam->metric.ptr[offset], "Attributes"),
                 name) != 0) {
    ERROR("curl_stats: updating distribution failed. Wrong attribute, wanted: "
          "%s, received: %s",
          metric_label_get(&fam->metric.ptr[offset], "Attributes"), name);
    return -1;
  }

  distribution_update(fam->metric.ptr[offset].value.distribution, val);
  return 0;
}

static int update_gauge_for_attribute(metric_family_t *fam, const char *name,
                                      double val, size_t offset) {
  if (strcasecmp(metric_label_get(&fam->metric.ptr[offset], "Attributes"),
                 name) != 0) {
    ERROR("curl_stats: updating gauge failed. Wrong attribute, wanted: %s, "
          "received: %s",
          metric_label_get(&fam->metric.ptr[offset], "Attributes"), name);
    return -1;
  }

  fam->metric.ptr[offset].value.gauge = val;

  return 0;
}

static int dispatch_gauge(CURL *curl, CURLINFO info, const char *name) {
  CURLcode code;
  gauge_t val;

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
      .type = METRIC_TYPE_GAUGE,
  };

  fam.metric.ptr[0].family = &fam;

  return plugin_dispatch_metric_family(&fam);
} /* dispatch_gauge */

/* dispatch a speed, in bytes/second */
static int dispatch_speed(CURL *curl, CURLINFO info, const char *name) {
  CURLcode code;
  gauge_t val;

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
      .type = METRIC_TYPE_GAUGE,
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
      .type = METRIC_TYPE_GAUGE,
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
#name, config_key, #metric_family_name, offsetof(curl_stats_t, name), 0,   \
        offsetof(attributes_metrics_t, metric_family_name), dispatcher,        \
        account_data, type, info                                               \
  }

    SPEC(total_time, "TotalTime", time_fam, dispatch_gauge, account_data_gauge,
         "duration", CURLINFO_TOTAL_TIME),
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
    SPEC(header_size, "HeaderSize", size_fam, dispatch_size, account_data_size,
         "bytes", CURLINFO_HEADER_SIZE),
    SPEC(request_size, "RequestSize", size_fam, dispatch_size,
         account_data_size, "bytes", CURLINFO_REQUEST_SIZE),
    SPEC(content_length_download, "ContentLengthDownload", size_fam,
         dispatch_gauge, account_data_gauge, "bytes",
         CURLINFO_CONTENT_LENGTH_DOWNLOAD),
    SPEC(content_length_upload, "ContentLengthUpload", size_fam, dispatch_gauge,
         account_data_gauge, "bytes", CURLINFO_CONTENT_LENGTH_UPLOAD),
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

static struct {
  const char *name;
  const char *config_key;
  size_t offset;
  const char *unit;
} metric_specs[] = {
#define SPEC(name, config_key, unit)                                           \
  { #name, config_key, offsetof(distribution_specs_t, name), unit }

    SPEC(distribution_type, "SizeDistributionType", "string"),
    SPEC(base, "SizeBase", "double"),
    SPEC(factor, "SizeFactor", "double"),
    SPEC(boundaries, "SizeBoundaries", "double*"),
    SPEC(num_buckets, "SizeNumBuckets", "size_t"),
    SPEC(num_boundaries, "SizeNumBoundaries", "size_t"),

    SPEC(distribution_type, "SpeedDistributionType", "string"),
    SPEC(base, "SpeedBase", "double"),
    SPEC(factor, "SpeedFactor", "double"),
    SPEC(boundaries, "SpeedBoundaries", "double*"),
    SPEC(num_buckets, "SpeedNumBuckets", "size_t"),
    SPEC(num_boundaries, "SpeedNumBoundaries", "size_t"),

    SPEC(distribution_type, "TimeDistributionType", "string"),
    SPEC(base, "TimeBase", "double"),
    SPEC(factor, "TimeFactor", "double"),
    SPEC(boundaries, "TimeBoundaries", "double*"),
    SPEC(num_buckets, "TimeNumBuckets", "size_t"),
    SPEC(num_boundaries, "TimeNumBoundaries", "size_t"),
#undef SPEC
};

static int append_metric_to_metric_family(curl_stats_t *s, size_t *idx,
                                          const char *name, const char *unit) {
  int status = -1;

  if (!strcasecmp("bytes", unit)) {
    status = metric_family_append(s->metrics->size_fam, "Attributes", name,
                                  (value_t){.distribution = NULL}, NULL);
    *idx = s->metrics->size_fam->metric.num - 1;
  } else if (!strcasecmp("bitrate", unit)) {
    status = metric_family_append(s->metrics->speed_fam, "Attributes", name,
                                  (value_t){.distribution = NULL}, NULL);
    *idx = s->metrics->speed_fam->metric.num - 1;
  } else if (!strcasecmp("duration", unit)) {
    status = metric_family_append(s->metrics->time_fam, "Attributes", name,
                                  (value_t){.distribution = NULL}, NULL);
    *idx = s->metrics->time_fam->metric.num - 1;
  } else if (!strcasecmp("count", unit)) {
    status = metric_family_append(s->metrics->count_fam, "Attributes", name,
                                  (value_t){.gauge = 0}, NULL);
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
char **curl_stats_get_enabled_attributes(curl_stats_t *s,
                                         size_t *num_enabled_attr) {
  int idx = 0;

  char **enabled_attributes =
      calloc(STATIC_ARRAY_SIZE(field_specs), sizeof(char *));

  if (enabled_attributes == NULL) {
    return NULL;
  }

  for (size_t field = 0; field < STATIC_ARRAY_SIZE(field_specs); ++field) {
    if (field_enabled(s, field_specs[field].config_offset)) {
      enabled_attributes[idx++] = strdup(field_specs[field].config_key);
    }
  }

  for (int i = 0; i < idx; ++i) {
    if (enabled_attributes[i] == NULL) {
      for (int j = 0; j < idx; ++j) {
        free(enabled_attributes[j]);
      }

      free(enabled_attributes);
      return NULL;
    }
  }

  *num_enabled_attr = idx;

  return enabled_attributes;
}

metric_family_t **
curl_stats_get_metric_families_for_attributes(curl_stats_t *s) {
  metric_family_t **fam = calloc(NUM_ATTR, sizeof(metric_family_t *));

  if (fam == NULL) {
    return NULL;
  }

  fam[SIZE_ATTR] = metric_family_clone(s->metrics->size_fam);
  fam[SPEED_ATTR] = metric_family_clone(s->metrics->speed_fam);
  fam[TIME_ATTR] = metric_family_clone(s->metrics->time_fam);

  if (fam[SIZE_ATTR] == NULL || fam[SPEED_ATTR] == NULL ||
      fam[TIME_ATTR] == NULL) {
    metric_family_metric_reset(fam[SIZE_ATTR]);
    metric_family_metric_reset(fam[SPEED_ATTR]);
    metric_family_metric_reset(fam[TIME_ATTR]);

    free(fam);
  }

  return fam;
}

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

  distribution_specs_t *dists_specs =
      calloc(NUM_ATTR, sizeof(distribution_specs_t));

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
      for (field = 0; field < STATIC_ARRAY_SIZE(metric_specs); ++field) {
        if (!strcasecmp(c->key, metric_specs[field].config_key)) {
          break;
        }
      }

      if (field >= STATIC_ARRAY_SIZE(metric_specs)) {
        ERROR("curl stats: Unknown field name %s", c->key);

        for (int j = 0; j < NUM_ATTR; ++j) {
          free(dists_specs[j].distribution_type);
        }

        free(dists_specs);
        curl_stats_destroy(s);
        return NULL;
      }

      size_t num_fields_per_attr = STATIC_ARRAY_SIZE(metric_specs) / NUM_ATTR;
      size_t attr_idx = field / num_fields_per_attr;

      if (!strcasecmp(metric_specs[field].unit, "size_t")) { /* read uint64_t */
        size_t value;

        if (cf_util_get_uint64(c, &value) != 0) {
          for (int j = 0; j < NUM_ATTR; ++j) {
            free(dists_specs[j].distribution_type);
          }

          free(dists_specs);
          curl_stats_destroy(s);
          return NULL;
        }

        *(size_t *)((char *)&dists_specs[attr_idx] +
                    metric_specs[field].offset) = value;
      } else if (!strcasecmp(metric_specs[field].unit,
                             "double*")) { /* read array of doubles */
        dists_specs[attr_idx].num_boundaries_from_array = c->values_num;

        double *boundaries = calloc(
            dists_specs[attr_idx].num_boundaries_from_array, sizeof(double));

        for (size_t j = 0; j < dists_specs[attr_idx].num_boundaries_from_array;
             ++j) {
          if (c->values[j].type != OCONFIG_TYPE_NUMBER) {
            ERROR("curl_stats_from_config: Wrong type for distribution custom "
                  "boundary. Required %d, received %d.",
                  OCONFIG_TYPE_NUMBER, c->values[j].type);

            for (int k = 0; k < NUM_ATTR; ++k) {
              free(dists_specs[k].distribution_type);
            }

            free(dists_specs);
            free(boundaries);
            curl_stats_destroy(s);
            return NULL;
          }

          boundaries[j] = c->values[j].value.number;
        }

        *(double **)((char *)&dists_specs[attr_idx] +
                     metric_specs[field].offset) = boundaries;
      } else if (!strcasecmp(metric_specs[field].unit,
                             "double")) { /* read double */
        double value;

        if (cf_util_get_double(c, &value) != 0) {
          for (int j = 0; j < NUM_ATTR; ++j) {
            free(dists_specs[j].distribution_type);
          }
          free(dists_specs);

          curl_stats_destroy(s);
          return NULL;
        }

        *(double *)((char *)&dists_specs[attr_idx] +
                    metric_specs[field].offset) = value;
      } else { /* read string */
        static const int MAX_BUFFER_LENGTH = 256;
        char buffer[MAX_BUFFER_LENGTH];

        if (cf_util_get_string_buffer(c, buffer, MAX_BUFFER_LENGTH) != 0) {
          for (int j = 0; j < NUM_ATTR; ++j) {
            free(dists_specs[j].distribution_type);
          }

          free(dists_specs);
          curl_stats_destroy(s);
          return NULL;
        }

        *(char **)((char *)&dists_specs[attr_idx] +
                   metric_specs[field].offset) = strdup(buffer);
      }

      continue;
    }

    if (cf_util_get_boolean(c, &enabled) != 0) {
      for (int j = 0; j < NUM_ATTR; ++j) {
        free(dists_specs[j].distribution_type);
      }

      free(dists_specs);
      curl_stats_destroy(s);
      return NULL;
    }

    if (enabled) {
      size_t offset = 0;
      enable_field(s, field_specs[field].config_offset);
      int status = append_metric_to_metric_family(
          s, &offset, field_specs[field].config_key, field_specs[field].type);

      if (status != 0) {
        ERROR("curl_stats_from_config: appending attribute: %s to metric "
              "family failed!",
              field_specs[field].config_key);
        for (int j = 0; j < NUM_ATTR; ++j) {
          free(dists_specs[j].distribution_type);
        }
        free(dists_specs);
        curl_stats_destroy(s);
        return NULL;
      }

      field_specs[field].metric_family_offset = offset;
    }
  }

  distribution_t **d;
  if ((d = parse_metric_from_config(dists_specs)) == NULL) {
    ERROR("curl_stats_from_config: parsing distributions from config failed!!");

    for (int i = 0; i < NUM_ATTR; ++i) {
      free(dists_specs[i].distribution_type);
    }

    free(dists_specs);
    curl_stats_destroy(s);
    return NULL;
  }

  if (initialize_distributions_for_metrics(s, d) != 0) {
    for (int i = 0; i < NUM_ATTR; ++i) {
      distribution_destroy(d[i]);
    }

    free(d);

    for (int i = 0; i < NUM_ATTR; ++i) {
      free(dists_specs[i].distribution_type);
    }
    free(dists_specs);

    curl_stats_destroy(s);
  }

  for (int i = 0; i < NUM_ATTR; ++i) {
    distribution_destroy(d[i]);
  }

  free(d);

  for (int i = 0; i < NUM_ATTR; ++i) {
    free(dists_specs[i].distribution_type);
  }
  free(dists_specs);

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

  /* TODO(bkjg): reset all distributions (wait for pull request with this
   * functionality to be merged) */

  return status;
} /* curl_stats_send_metric_to_daemon */