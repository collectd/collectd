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

/* TODO(bkjg): consider making this structure static with creation of the
 * variable in the compile time */
typedef struct {
  char *metric_type;
  char *distribution_type;
  char *metric_identity;
  double base;
  double factor;
  double *boundaries;
  size_t num_buckets;
  size_t num_boundaries;
  size_t num_boundaries_from_array;
} metric_spec_t;

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
  char *metric_type;
  char *distribution_type;
  char *metric_identity;
  double base;
  double factor;
  double *boundaries;
  size_t num_buckets;
  size_t num_boundaries;
  metric_t *m;
};

static metric_t *parse_metric_from_config(metric_spec_t *m_spec) {
  if (m_spec == NULL) {
    return NULL;
  }

  metric_t *m = metric_parse_identity(m_spec->metric_identity);

  if (m == NULL) {
    return NULL;
  }

  if (m_spec->metric_type != NULL) {
    if (!strcasecmp(m_spec->metric_type, "DISTRIBUTION")) {
      m->family->type = METRIC_TYPE_DISTRIBUTION;
    } else if (!strcasecmp(m_spec->metric_type, "GAUGE")) {
      m->family->type = METRIC_TYPE_GAUGE;
    } else if (!strcasecmp(m_spec->metric_type, "COUNTER")) {
      m->family->type = METRIC_TYPE_COUNTER;
    } else if (!strcasecmp(m_spec->metric_type, "UNTYPED")) {
      m->family->type = METRIC_TYPE_UNTYPED;
    } else {
      ERROR("curl_stats_from_config: Unknown metric type %s.",
            m_spec->metric_type);
      free(m);
      return NULL;
    }
  }

  if (m->family->type == METRIC_TYPE_DISTRIBUTION) {
    if (m_spec->distribution_type == NULL) {
      ERROR("curl_stats_from_config: Metric type is distribution. Missing "
            "distribution type specification.");
      free(m);
      return NULL;
    }
  }

  if (!strcasecmp(m_spec->distribution_type, "LINEAR")) {
    if (m_spec->num_buckets == 0 ||
        (m_spec->base == 0 && m_spec->factor == 0)) {
      ERROR("curl_stats_from_config: Missing arguments for metric type linear "
            "distribution");
      free(m);
      return NULL;
    }

    if (m_spec->base != 0) {
      m->value.distribution =
          distribution_new_linear(m_spec->num_buckets, m_spec->base);
    } else {
      m->value.distribution =
          distribution_new_linear(m_spec->num_buckets, m_spec->factor);
    }
  } else if (!strcasecmp(m_spec->distribution_type, "EXPONENTIAL")) {
    if (m_spec->num_buckets == 0 || m_spec->base == 0 || m_spec->factor == 0) {
      ERROR("curl_stats_from_config: Missing arguments for metric type "
            "exponential distribution");
      free(m);
      return NULL;
    }

    m->value.distribution = distribution_new_exponential(
        m_spec->num_buckets, m_spec->base, m_spec->factor);
  } else if (!strcasecmp(m_spec->distribution_type, "CUSTOM")) {
    if (m_spec->num_boundaries == 0) {
      ERROR("curl_stats_from_config: Missing arguments for metric type "
            "exponential distribution");
      free(m);
      return NULL;
    }

    if (m_spec->num_boundaries != m_spec->num_boundaries_from_array) {
      ERROR("curl_stats_from_config: Wrong number of boundaries for custom "
            "distribution");
      free(m);
      return NULL;
    }

    m->value.distribution =
        distribution_new_custom(m_spec->num_boundaries, m_spec->boundaries);
  } else {
    ERROR("curl_stats_from_config: Unknown distribution type: %s",
          m_spec->distribution_type);
    free(m);
    return NULL;
  }

  if (m->value.distribution == NULL) {
    ERROR("curl stats: Creating distribution failed");
    free(m);
    return NULL;
  }

  return m;
}
/*
 * Private functions
 */
static int dispatch_gauge(CURL *curl, CURLINFO info, metric_t *m) {
  CURLcode code;
  double val;

  code = curl_easy_getinfo(curl, info, &val);
  if (code != CURLE_OK)
    return -1;

  if (m->family->type == METRIC_TYPE_DISTRIBUTION) {
    distribution_update(m->value.distribution, val);
  } else {
    m->value.gauge = val;
  }
  /* TODO(bkjg): "This library should optionally be able to update
   * distribution_t instead of calling plugin_dispatch_values." */
  return plugin_dispatch_metric_family(m->family);
} /* dispatch_gauge */

/* dispatch a speed, in bytes/second */
static int dispatch_speed(CURL *curl, CURLINFO info, metric_t *m) {
  CURLcode code;
  double val;

  code = curl_easy_getinfo(curl, info, &val);
  if (code != CURLE_OK)
    return -1;

  if (m->family->type == METRIC_TYPE_DISTRIBUTION) {
    distribution_update(m->value.distribution, val * 8);
  } else {
    m->value.gauge = val * 8;
  }

  /* TODO(bkjg): "This library should optionally be able to update
   * distribution_t instead of calling plugin_dispatch_values." */
  return plugin_dispatch_metric_family(m->family);
} /* dispatch_speed */

/* dispatch a size/count, reported as a long value */
static int dispatch_size(CURL *curl, CURLINFO info, metric_t *m) {
  CURLcode code;
  long raw;

  code = curl_easy_getinfo(curl, info, &raw);
  if (code != CURLE_OK)
    return -1;

  if (m->family->type == METRIC_TYPE_DISTRIBUTION) {
    distribution_update(m->value.distribution, (double)raw);
  } else {
    m->value.gauge = (double)raw;
  }

  /* TODO(bkjg): "This library should optionally be able to update
   * distribution_t instead of calling plugin_dispatch_values." */
  return plugin_dispatch_metric_family(m->family);
} /* dispatch_size */

static struct {
  const char *name;
  const char *config_key;
  size_t offset;
  int (*dispatcher)(CURL *, CURLINFO, metric_t *);
  const char *type;
  CURLINFO info;
} field_specs[] = {
#define SPEC(name, config_key, dispatcher, type, info)                         \
  { #name, config_key, offsetof(curl_stats_t, name), dispatcher, type, info }

    SPEC(total_time, "TotalTime", dispatch_gauge, "duration",
         CURLINFO_TOTAL_TIME),
    SPEC(namelookup_time, "NamelookupTime", dispatch_gauge, "duration",
         CURLINFO_NAMELOOKUP_TIME),
    SPEC(connect_time, "ConnectTime", dispatch_gauge, "duration",
         CURLINFO_CONNECT_TIME),
    SPEC(pretransfer_time, "PretransferTime", dispatch_gauge, "duration",
         CURLINFO_PRETRANSFER_TIME),
    SPEC(size_upload, "SizeUpload", dispatch_gauge, "bytes",
         CURLINFO_SIZE_UPLOAD),
    SPEC(size_download, "SizeDownload", dispatch_gauge, "bytes",
         CURLINFO_SIZE_DOWNLOAD),
    SPEC(speed_download, "SpeedDownload", dispatch_speed, "bitrate",
         CURLINFO_SPEED_DOWNLOAD),
    SPEC(speed_upload, "SpeedUpload", dispatch_speed, "bitrate",
         CURLINFO_SPEED_UPLOAD),
    SPEC(header_size, "HeaderSize", dispatch_size, "bytes",
         CURLINFO_HEADER_SIZE),
    SPEC(request_size, "RequestSize", dispatch_size, "bytes",
         CURLINFO_REQUEST_SIZE),
    SPEC(content_length_download, "ContentLengthDownload", dispatch_gauge,
         "bytes", CURLINFO_CONTENT_LENGTH_DOWNLOAD),
    SPEC(content_length_upload, "ContentLengthUpload", dispatch_gauge, "bytes",
         CURLINFO_CONTENT_LENGTH_UPLOAD),
    SPEC(starttransfer_time, "StarttransferTime", dispatch_gauge, "duration",
         CURLINFO_STARTTRANSFER_TIME),
    SPEC(redirect_time, "RedirectTime", dispatch_gauge, "duration",
         CURLINFO_REDIRECT_TIME),
    SPEC(redirect_count, "RedirectCount", dispatch_size, "count",
         CURLINFO_REDIRECT_COUNT),
    SPEC(num_connects, "NumConnects", dispatch_size, "count",
         CURLINFO_NUM_CONNECTS),
#ifdef HAVE_CURLINFO_APPCONNECT_TIME
    SPEC(appconnect_time, "AppconnectTime", dispatch_gauge, "duration",
         CURLINFO_APPCONNECT_TIME),
#endif

#undef SPEC
};

static struct {
  const char *name;
  const char *config_key;
  size_t offset;
  int (*configure)(const char *, const void *, metric_t);
} metric_specs[] = {
#define SPEC(name, config_key)                                                 \
  { #name, config_key, offsetof(metric_spec, name) }

    SPEC(metric_type, "MetricType"),
    SPEC(distribution_type, "DistributionType"),
    SPEC(metric_identity, "MetricIdentity"),
    SPEC(base, "Base"),
    SPEC(factor, "Factor"),
    SPEC(boundaries, "Boundaries"),
    SPEC(num_buckets, "NumBuckets"),
    SPEC(num_boundaries, "NumBoundaries"),
#undef SPEC
};

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
  const static int MAX_BUFFER_LENGTH = 256;
  curl_stats_t *s;

  if (ci == NULL)
    return NULL;

  s = calloc(1, sizeof(*s));
  if (s == NULL)
    return NULL;

  char identity[MAX_BUFFER_LENGTH];

  /* make identity of each metric unique */
  snprintf(identity, MAX_BUFFER_LENGTH, "curl_stats_%p", s);

  metric_spec_t *m_spec;
  m_spec = calloc(1, sizeof(metric_spec_t));

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
      /* TODO(bkjg): create an array with names of metric types */
      /* TODO(bkjg): check if only one type of distribution is specified (??) */
      for (field = 0; field < STATIC_ARRAY_SIZE(metric_specs); ++field) {
        if (!strcasecmp(c->key, metric_specs[field].config_key)) {
          break;
        }

        if (!strcasecmp(c->key, metric_specs[field].name)) {
          break;
        }
      }

      if (field >= STATIC_ARRAY_SIZE(metric_specs)) {
        ERROR("curl stats: Unknown field name %s", c->key);
        free(s);
        return NULL;
      }

      if (field > 5) { /* read uint64_t */
        size_t value;

        if (cf_util_get_uint64(c, &value) != 0) {
          free(s);
          return NULL;
        }

        *(size_t *)((char *)m_spec + metric_specs[field].offset) = value;
      } else if (field == 5) {
        m_spec->num_boundaries_from_array = c->values_num;

        double *boundaries =
            calloc(m_spec->num_boundaries_from_array, sizeof(double));

        for (size_t j = 0; j < m_spec->num_boundaries_from_array; ++j) {
          if (c->values->type != OCONFIG_TYPE_NUMBER) {
            ERROR("curl_stats_from_config: Wrong type for distribution custom "
                  "boundary. Required %d, received %d.",
                  OCONFIG_TYPE_NUMBER, c->values->type);
            /* TODO(bkjg): here should be function for destroying metric_spec_t
             */
            free(m_spec);
            free(s);
            return NULL;
          }

          boundaries[j] = c->values[j].value.number;
        }

        *(double **)((char *)m_spec + metric_specs[field].offset) = boundaries;
      } else if (field > 2) { /* read double */
        double value;

        if (cf_util_get_double(c, &value) != 0) {
          free(s);
          return NULL;
        }

        *(double *)((char *)m_spec + metric_specs[field].offset) = value;
      } else { /* read char * */
        char buffer[MAX_BUFFER_LENGTH];

        if (cf_util_get_string_buffer(c, buffer, MAX_BUFFER_LENGTH) != 0) {
          free(s);
          return NULL;
        }

        *(char **)((char *)m_spec + metric_specs[field].offset) =
            strdup(buffer);
      }

      if (m_spec->metric_identity == NULL) {
        m_spec->metric_identity = identity;
      }

      continue;
    }

    if (cf_util_get_boolean(c, &enabled) != 0) {
      free(s);
      return NULL;
    }
    if (enabled)
      enable_field(s, field_specs[field].offset);
  }

  s->m = parse_metric_from_config(m_spec);

  /* TODO(bkjg): create function for destroying metric_spec_t structure */
  free(m_spec->distribution_type);
  free(m_spec->metric_type);
  if (m_spec->metric_identity != identity) {
    free(m_spec->metric_identity);
  }
  free(m_spec);

  if (s->m == NULL) {
    metric_family_free(s->m->family);
    free(s);
    return NULL;
  }

  return s;
} /* curl_stats_from_config */

void curl_stats_destroy(curl_stats_t *s) {
  if (s != NULL) {
    metric_family_free(s->m->family);
    free(s);
  }
} /* curl_stats_destroy */

/* TODO: delete unused arguments when all plugins will migrate to the new metric
 * data structure */
int curl_stats_dispatch(curl_stats_t *s, CURL *curl, const char *hostname,
                        const char *plugin, const char *plugin_instance,
                        bool asynchronous) {
  if (s == NULL)
    return 0;
  if (curl == NULL) {
    ERROR("curl stats: dispatch() called with missing argument: curl=%p", curl);
    return -1;
  }

  for (size_t field = 0; field < STATIC_ARRAY_SIZE(field_specs); ++field) {
    int status;

    if (!field_enabled(s, field_specs[field].offset))
      continue;

    status = field_specs[field].dispatcher(curl, field_specs[field].info, s->m);

    if (status < 0) {
      return status;
    }
  }

  return 0;
} /* curl_stats_dispatch */

int curl_stats_account_data(curl_stats_t *s, CURL *curl) {}

static int curl_stats_account_data_gauge(CURL *curl, CURLINFO info,
                                         metric_t *m) {
  CURLcode code;
  double val;

  code = curl_easy_getinfo(curl, info, &val);
  if (code != CURLE_OK)
    return -1;

  if (m->family->type == METRIC_TYPE_DISTRIBUTION) {
    distribution_update(m->value.distribution, val);
  } else {
    m->value.gauge = val;
  }

  return 0;
} /* curl_stats_account_data_gauge */

static int curl_stats_account_data_speed(CURL *curl, CURLINFO info,
                                         metric_t *m) {
  CURLcode code;
  double val;

  code = curl_easy_getinfo(curl, info, &val);
  if (code != CURLE_OK)
    return -1;

  if (m->family->type == METRIC_TYPE_DISTRIBUTION) {
    distribution_update(m->value.distribution, val * 8);
  } else {
    m->value.gauge = val * 8;
  }

  return 0;
} /* curl_stats_account_data_speed */

static int curl_stats_account_data_size(CURL *curl, CURLINFO info,
                                        metric_t *m) {
  CURLcode code;
  long raw;

  code = curl_easy_getinfo(curl, info, &raw);
  if (code != CURLE_OK)
    return -1;

  if (m->family->type == METRIC_TYPE_DISTRIBUTION) {
    distribution_update(m->value.distribution, (double)raw);
  } else {
    m->value.gauge = (double)raw;
  }

  return 0;
} /* curl_stats_account_data_size */

int curl_stats_send_metric_to_daemon(curl_stats_t *s) {
  return plugin_dispatch_metric_family(s->m->family);;
} /* curl_stats_send_metric_to_daemon */
