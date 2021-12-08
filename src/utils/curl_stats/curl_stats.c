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
 **/

#include "collectd.h"

#include "utils/common/common.h"
#include "utils/curl_stats/curl_stats.h"

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
};

/*
 * Private functions
 */

static int dispatch_gauge(CURL *curl, CURLINFO info, value_list_t *vl) {
  CURLcode code;
  value_t v;

  code = curl_easy_getinfo(curl, info, &v.gauge);
  if (code != CURLE_OK)
    return -1;

  vl->values = &v;
  vl->values_len = 1;

  return plugin_dispatch_values(vl);
} /* dispatch_gauge */

/* dispatch a speed, in bytes/second */
static int dispatch_speed(CURL *curl, CURLINFO info, value_list_t *vl) {
  CURLcode code;
  value_t v;

  code = curl_easy_getinfo(curl, info, &v.gauge);
  if (code != CURLE_OK)
    return -1;

  v.gauge *= 8;

  vl->values = &v;
  vl->values_len = 1;

  return plugin_dispatch_values(vl);
} /* dispatch_speed */

/* dispatch a size/count, reported as a long value */
static int dispatch_size(CURL *curl, CURLINFO info, value_list_t *vl) {
  CURLcode code;
  value_t v;
  long raw;

  code = curl_easy_getinfo(curl, info, &raw);
  if (code != CURLE_OK)
    return -1;

  v.gauge = (double)raw;

  vl->values = &v;
  vl->values_len = 1;

  return plugin_dispatch_values(vl);
} /* dispatch_size */

static struct {
  const char *name;
  const char *config_key;
  size_t offset;

  int (*dispatcher)(CURL *, CURLINFO, value_list_t *);
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
  curl_stats_t *s;

  if (ci == NULL)
    return NULL;

  s = calloc(1, sizeof(*s));
  if (s == NULL)
    return NULL;

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
    if (enabled)
      enable_field(s, field_specs[field].offset);
  }

  return s;
} /* curl_stats_from_config */

void curl_stats_destroy(curl_stats_t *s) {
  if (s != NULL)
    free(s);
} /* curl_stats_destroy */

int curl_stats_dispatch(curl_stats_t *s, CURL *curl, const char *hostname,
                        const char *plugin, const char *plugin_instance) {
  value_list_t vl = VALUE_LIST_INIT;

  if (s == NULL)
    return 0;
  if ((curl == NULL) || (plugin == NULL)) {
    ERROR("curl stats: dispatch() called with missing arguments "
          "(curl=%p; plugin=%s)",
          curl, plugin == NULL ? "<NULL>" : plugin);
    return -1;
  }

  if (hostname != NULL)
    sstrncpy(vl.host, hostname, sizeof(vl.host));
  sstrncpy(vl.plugin, plugin, sizeof(vl.plugin));
  if (plugin_instance != NULL)
    sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));

  for (size_t field = 0; field < STATIC_ARRAY_SIZE(field_specs); ++field) {
    int status;

    if (!field_enabled(s, field_specs[field].offset))
      continue;

    sstrncpy(vl.type, field_specs[field].type, sizeof(vl.type));
    sstrncpy(vl.type_instance, field_specs[field].name,
             sizeof(vl.type_instance));

    vl.values = NULL;
    vl.values_len = 0;
    status = field_specs[field].dispatcher(curl, field_specs[field].info, &vl);
    if (status < 0)
      return status;
  }

  return 0;
} /* curl_stats_dispatch */
