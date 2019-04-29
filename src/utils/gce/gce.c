/**
 * collectd - src/utils_gce.c
 * ISC license
 *
 * Copyright (C) 2017  Florian Forster
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *
 * Authors:
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/gce/gce.h"
#include "utils/oauth/oauth.h"
#include "utils_time.h"

#include <curl/curl.h>

#ifndef GCP_METADATA_PREFIX
#define GCP_METADATA_PREFIX "http://metadata.google.internal/computeMetadata/v1"
#endif
#ifndef GCE_METADATA_HEADER
#define GCE_METADATA_HEADER "Metadata-Flavor: Google"
#endif

#ifndef GCE_INSTANCE_ID_URL
#define GCE_INSTANCE_ID_URL GCP_METADATA_PREFIX "/instance/id"
#endif
#ifndef GCE_PROJECT_NUM_URL
#define GCE_PROJECT_NUM_URL GCP_METADATA_PREFIX "/project/numeric-project-id"
#endif
#ifndef GCE_PROJECT_ID_URL
#define GCE_PROJECT_ID_URL GCP_METADATA_PREFIX "/project/project-id"
#endif
#ifndef GCE_ZONE_URL
#define GCE_ZONE_URL GCP_METADATA_PREFIX "/instance/zone"
#endif

#ifndef GCE_DEFAULT_SERVICE_ACCOUNT
#define GCE_DEFAULT_SERVICE_ACCOUNT "default"
#endif

#ifndef GCE_SCOPE_URL
#define GCE_SCOPE_URL_FORMAT                                                   \
  GCP_METADATA_PREFIX "/instance/service-accounts/%s/scopes"
#endif
#ifndef GCE_TOKEN_URL
#define GCE_TOKEN_URL_FORMAT                                                   \
  GCP_METADATA_PREFIX "/instance/service-accounts/%s/token"
#endif

struct blob_s {
  char *data;
  size_t size;
};
typedef struct blob_s blob_t;

static int on_gce = -1;

static char *token = NULL;
static char *token_email = NULL;
static cdtime_t token_valid_until = 0;
static pthread_mutex_t token_lock = PTHREAD_MUTEX_INITIALIZER;

static size_t write_callback(void *contents, size_t size, size_t nmemb,
                             void *ud) /* {{{ */
{
  size_t realsize = size * nmemb;
  blob_t *blob = ud;

  if ((0x7FFFFFF0 < blob->size) || (0x7FFFFFF0 - blob->size < realsize)) {
    ERROR("utils_gce: write_callback: integer overflow");
    return 0;
  }

  blob->data = realloc(blob->data, blob->size + realsize + 1);
  if (blob->data == NULL) {
    /* out of memory! */
    ERROR(
        "utils_gce: write_callback: not enough memory (realloc returned NULL)");
    return 0;
  }

  memcpy(blob->data + blob->size, contents, realsize);
  blob->size += realsize;
  blob->data[blob->size] = 0;

  return realsize;
} /* }}} size_t write_callback */

/* read_url will issue a GET request for the given URL, setting the magic GCE
 * metadata header in the process. On success, the response body is returned
 * and it's the caller's responsibility to free it. On failure, an error is
 * logged and NULL is returned. */
static char *read_url(char const *url) /* {{{ */
{
  CURL *curl = curl_easy_init();
  if (!curl) {
    ERROR("utils_gce: curl_easy_init failed.");
    return NULL;
  }

  struct curl_slist *headers = curl_slist_append(NULL, GCE_METADATA_HEADER);

  char curl_errbuf[CURL_ERROR_SIZE];
  blob_t blob = {0};
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &blob);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_URL, url);

  int status = curl_easy_perform(curl);
  if (status != CURLE_OK) {
    ERROR("utils_gce: fetching %s failed: %s", url, curl_errbuf);
    sfree(blob.data);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return NULL;
  }

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if ((http_code < 200) || (http_code >= 300)) {
    ERROR("write_gcm plugin: fetching %s failed: HTTP error %ld", url,
          http_code);
    sfree(blob.data);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    return NULL;
  }

  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  return blob.data;
} /* }}} char *read_url */

_Bool gce_check(void) /* {{{ */
{
  if (on_gce != -1)
    return on_gce == 1;

  DEBUG("utils_gce: Checking whether I'm running on GCE ...");

  CURL *curl = curl_easy_init();
  if (!curl) {
    ERROR("utils_gce: curl_easy_init failed.");
    return 0;
  }

  struct curl_slist *headers = curl_slist_append(NULL, GCE_METADATA_HEADER);

  char curl_errbuf[CURL_ERROR_SIZE];
  blob_t blob = {NULL, 0};
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEHEADER, &blob);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_URL, GCP_METADATA_PREFIX "/");

  int status = curl_easy_perform(curl);
  if ((status != CURLE_OK) || (blob.data == NULL) ||
      (strstr(blob.data, "Metadata-Flavor: Google") == NULL)) {
    DEBUG("utils_gce: ... no (%s)",
          (status != CURLE_OK)
              ? "curl_easy_perform failed"
              : (blob.data == NULL) ? "blob.data == NULL"
                                    : "Metadata-Flavor header not found");
    sfree(blob.data);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    on_gce = 0;
    return 0;
  }
  sfree(blob.data);

  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if ((http_code < 200) || (http_code >= 300)) {
    DEBUG("utils_gce: ... no (HTTP status %ld)", http_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);
    on_gce = 0;
    return 0;
  }

  DEBUG("utils_gce: ... yes");
  curl_easy_cleanup(curl);
  curl_slist_free_all(headers);
  on_gce = 1;
  return 1;
} /* }}} _Bool gce_check */

char *gce_project_id(void) /* {{{ */
{
  return read_url(GCE_PROJECT_ID_URL);
} /* }}} char *gce_project_id */

char *gce_instance_id(void) /* {{{ */
{
  return read_url(GCE_INSTANCE_ID_URL);
} /* }}} char *gce_instance_id */

char *gce_zone(void) /* {{{ */
{
  return read_url(GCE_ZONE_URL);
} /* }}} char *gce_instance_id */

char *gce_scope(char const *email) /* {{{ */
{
  char url[1024];

  snprintf(url, sizeof(url), GCE_SCOPE_URL_FORMAT,
           (email != NULL) ? email : GCE_DEFAULT_SERVICE_ACCOUNT);

  return read_url(url);
} /* }}} char *gce_scope */

int gce_access_token(char const *email, char *buffer,
                     size_t buffer_size) /* {{{ */
{
  char url[1024];
  char *json;
  cdtime_t now = cdtime();

  pthread_mutex_lock(&token_lock);

  if (email == NULL)
    email = GCE_DEFAULT_SERVICE_ACCOUNT;

  if ((token_email != NULL) && (strcmp(email, token_email) == 0) &&
      (token_valid_until > now)) {
    sstrncpy(buffer, token, buffer_size);
    pthread_mutex_unlock(&token_lock);
    return 0;
  }

  snprintf(url, sizeof(url), GCE_TOKEN_URL_FORMAT, email);
  json = read_url(url);
  if (json == NULL) {
    pthread_mutex_unlock(&token_lock);
    return -1;
  }

  char tmp[256];
  cdtime_t expires_in = 0;
  int status = oauth_parse_json_token(json, tmp, sizeof(tmp), &expires_in);
  sfree(json);
  if (status != 0) {
    pthread_mutex_unlock(&token_lock);
    return status;
  }

  sfree(token);
  token = strdup(tmp);

  sfree(token_email);
  token_email = strdup(email);

  /* let tokens expire a bit early */
  expires_in = (expires_in * 95) / 100;
  token_valid_until = now + expires_in;

  sstrncpy(buffer, token, buffer_size);
  pthread_mutex_unlock(&token_lock);
  return 0;
} /* }}} char *gce_token */
