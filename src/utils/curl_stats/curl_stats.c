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
  metric_t *m;
};

static int parse_distribution_from_config(metric_t *m, oconfig_item_t *c) {
  if (m == NULL || c == NULL) {
    return -1;
  }

  if (c->values_num < 3) {
    ERROR("curl stats: Distribution parameters are not correctly specified ");
    return -1;
  }

  if (c->values[0].type != OCONFIG_TYPE_STRING) {
    ERROR("curl stats: OConfig type is not correctly specified. Required %d, "
          "received %d.",
          OCONFIG_TYPE_STRING, c->values[0].type);
    return -1;
  }

  if (!strcasecmp(c->values[0].value.string, "DISTRIBUTION_LINEAR")) {
    if (c->values_num != 3) {
      ERROR("curl stats: Wrong number of arguments for metric type "
            "linear distribution. Required %d, received %d.",
            2, c->values_num);
      return -1;
    }

    if (c->values[1].type != OCONFIG_TYPE_NUMBER ||
        c->values[2].type != OCONFIG_TYPE_NUMBER) {
      ERROR("curl stats: Wrong type of arguments for metric type "
            "linear distribution. Required %d, %d, received %d, %d.",
            OCONFIG_TYPE_NUMBER, OCONFIG_TYPE_NUMBER, c->values[1].type,
            c->values[2].type);
      return -1;
    }

    m->value.distribution = distribution_new_linear(c->values[1].value.number,
                                                    c->values[2].value.number);

  } else if (!strcasecmp(c->values[0].value.string,
                         "DISTRIBUTION_EXPONENTIAL")) {
    if (c->values_num != 4) {
      ERROR("curl stats: Wrong number of arguments for metric type "
            "exponential distribution. Required %d, received %d.",
            3, c->values_num);
      return -1;
    }

    if (c->values[1].type != OCONFIG_TYPE_NUMBER ||
        c->values[2].type != OCONFIG_TYPE_NUMBER ||
        c->values[3].type != OCONFIG_TYPE_NUMBER) {
      ERROR("curl stats: Wrong type of arguments for metric type "
            "exponential distribution. Required %d, %d, %d, received %d, %d, %d.",
            OCONFIG_TYPE_NUMBER, OCONFIG_TYPE_NUMBER, OCONFIG_TYPE_NUMBER,
            c->values[1].type, c->values[2].type, c->values[2].type);
      return -1;
    }

    m->value.distribution = distribution_new_exponential(
        c->values[1].value.number, c->values[2].value.number,
        c->values[2].value.number);
  } else if (!strcasecmp(c->values[0].value.string, "DISTRIBUTION_CUSTOM")) {
    if (c->values[1].type != OCONFIG_TYPE_NUMBER) {
      ERROR("curl stats: Number of boundaries in custom distribution have to "
            "be number. Required %d, received %d.",
            OCONFIG_TYPE_NUMBER, c->values[1].type);
      return -1;
    }

    if (c->values_num != c->values[1].value.number + 2) {
      ERROR("curl stats: Wrong number of arguments for metric type "
            "custom distribution. Required %lf, received %d.",
            c->values[1].value.number + 1, c->values_num);
      return -1;
    }

    size_t num_boundaries = (size_t)c->values[1].value.number;
    double boundaries[num_boundaries];
    for (size_t i = 2; i < num_boundaries; ++i) {
      if (c->values[i].type != OCONFIG_TYPE_NUMBER) {
        ERROR("curl stats: Wrong type of arguments for metric type "
              "custom distribution. Required %d, received %d.",
              OCONFIG_TYPE_NUMBER, c->values[i].type);
        return -1;
      }
      boundaries[i] = c->values[i].value.number;
    }

    m->value.distribution = distribution_new_custom(num_boundaries, boundaries);
  } else {
    ERROR("curl stats: Unknown distribution type: %s",
          c->values[0].value.string);
    return -1;
  }

  if (m->value.distribution == NULL) {
    ERROR("curl stats: Creating distribution failed");
    return -1;
  }

  return 0;
}

static int account_new_data_double(CURL *curl, CURLINFO info, metric_t *m) {
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
}

static int account_new_data_long(CURL *curl, CURLINFO info, metric_t *m) {
  CURLcode code;
  long raw;

  code = curl_easy_getinfo(curl, info, &raw);
  if (code != CURLE_OK)
    return -1;

  double val = (double)raw;

  if (m->family->type == METRIC_TYPE_DISTRIBUTION) {
    distribution_update(m->value.distribution, val * 8);
  } else {
    m->value.gauge = val * 8;
  }

  return 0;
}

static int send_metrics_to_the_daemon(metric_family_t *fam) {
  return plugin_dispatch_metric_family(fam);
}
/*
 * Private functions
 */
static int dispatch_gauge(CURL *curl, CURLINFO info, metric_t *m,
                          bool asynchronous) {
  if (asynchronous) {
    int status = account_new_data_double(curl, info, m);

    if (status != 0) {
      return -1;
    }

    return send_metrics_to_the_daemon(m->family);
  } else {
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
  }
} /* dispatch_gauge */

/* dispatch a speed, in bytes/second */
static int dispatch_speed(CURL *curl, CURLINFO info, metric_t *m,
                          bool asynchronous) {
  if (asynchronous) {
    int status = account_new_data_double(curl, info, m);

    if (status != 0) {
      return -1;
    }

    return send_metrics_to_the_daemon(m->family);
  } else {
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
  }
} /* dispatch_speed */

/* dispatch a size/count, reported as a long value */
static int dispatch_size(CURL *curl, CURLINFO info, metric_t *m,
                         bool asynchronous) {
  if (asynchronous) {
    int status = account_new_data_long(curl, info, m);

    if (status != 0) {
      return -1;
    }

    return send_metrics_to_the_daemon(m->family);
  } else {
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
  }
}

static struct {
  const char *name;
  const char *config_key;
  size_t offset;
  int (*dispatcher)(CURL *, CURLINFO, metric_t *, bool);
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
  const static int MAX_IDENTITY_LENGTH = 256;
  curl_stats_t *s;

  if (ci == NULL)
    return NULL;

  s = calloc(1, sizeof(*s));
  if (s == NULL)
    return NULL;

  char identity[MAX_IDENTITY_LENGTH];

  /* make identity of each metric unique */
  snprintf(identity, MAX_IDENTITY_LENGTH, "curl_stats_%p", s);

  oconfig_item_t *conf_distribution = NULL;

  metric_type_t metric_family_type = METRIC_TYPE_UNTYPED;
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
      if (!strcasecmp(c->key, "METRIC_TYPE_DISTRIBUTION")) {
        metric_family_type = METRIC_TYPE_DISTRIBUTION;
        conf_distribution = c;
        continue;
      } else if (!strcasecmp(c->key, "METRIC_TYPE_GAUGE")) {
        metric_family_type = METRIC_TYPE_GAUGE;
        continue;
      } else if (!strcasecmp(c->key, "METRIC_TYPE_COUNTER")) {
        metric_family_type = METRIC_TYPE_COUNTER;
        continue;
      } else if (!strcasecmp(c->key, "METRIC_TYPE_UNTYPED")) {
        metric_family_type = METRIC_TYPE_UNTYPED;
        continue;
      } else if (!strcasecmp(c->key, "Identity")) {
        if (cf_util_get_string_buffer(c, identity, MAX_IDENTITY_LENGTH) != 0) {
          ERROR("curl stats: Parsing identity failed");
          free(s);
          return NULL;
        }
        continue;
      } else {
        ERROR("curl stats: Unknown field name %s", c->key);
        free(s);
        return NULL;
      }
    }

    if (cf_util_get_boolean(c, &enabled) != 0) {
      free(s);
      return NULL;
    }
    if (enabled)
      enable_field(s, field_specs[field].offset);
  }

  s->m = metric_parse_identity(identity);

  if (s->m == NULL) {
    return NULL;
  }

  s->m->family->type = metric_family_type;
  int status = parse_distribution_from_config(s->m, conf_distribution);

  if (status != 0) {
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

    status = field_specs[field].dispatcher(curl, field_specs[field].info, s->m,
                                           asynchronous);

    if (status < 0) {
      return status;
    }
  }

  return 0;
} /* curl_stats_dispatch */