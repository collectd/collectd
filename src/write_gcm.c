/**
 * collectd - src/write_gcm.c
 * Copyright (C) 2014  Google Inc.
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
 *   Zhihua Wen <zhihuawen at google.com>
 *   Florian Forster <octo at google.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_avltree.h"

#include <curl/curl.h>

#if HAVE_YAJL_YAJL_VERSION_H
# include <yajl/yajl_version.h>
#endif
#if defined(YAJL_MAJOR) && (YAJL_MAJOR > 1)
# define HAVE_YAJL_V2 1
# include <yajl/yajl_tree.h>
#else
# include <yajl/yajl_parse.h>
#endif

#if HAVE_LIBSSL
# include <openssl/err.h>
# include <openssl/pkcs12.h>
# include <openssl/evp.h>
# include <openssl/sha.h>
#endif

#if HAVE_PTHREAD_H
#include <pthread.h>
#endif

/*
 * Private variables
 */
/* Max send buffer size, since there will be only one writer thread and
 * monitoring api supports up to 100K bytes in one request, 64K is reasonable
 */
#define MAX_BUFFER_SIZE 65536
#define MAX_ENCODE_SIZE 2048
#define MAX_TIMESTAMP_SIZE 42

#define PLUGIN_INSTANCE_LABEL "plugin_instance"
#define TYPE_INSTANCE_LABEL "type_instance"
#define SERVICE_LABEL "cloud.googleapis.com/service"
#define RESOURCE_ID_LABEL "compute.googleapis.com/resource_id"
#define METRIC_LABEL_PREFIX "custom.cloudmonitoring.googleapis.com"
#define DEFAULT_SERVICE "compute.googleapis.com"

#define MONITORING_URL \
  "https://www.googleapis.com/cloudmonitoring/v2beta2"
/* TODO(zhihuawen) change the scope to monitoring when it's working */
#define MONITORING_SCOPE "https://www.googleapis.com/auth/monitoring.readonly"
//#define MONITORING_SCOPE "https://www.googleapis.com/auth/monitoring"
#define GCE_METADATA_FLAVOR "Metadata-Flavor: Google"
#define METADATA_PREFIX "http://169.254.169.254/"
#define METADATA_HEADER "X-Google-Metadata-Request: True"
#define METADATA_TOKEN_URL \
  METADATA_PREFIX "computeMetadata/v1/instance/service-accounts/default/token"
#define METADATA_SCOPE_URL                                    \
  METADATA_PREFIX                                             \
      "computeMetadata/v1/instance/service-accounts/default/" \
      "scopes"
#define METADATA_PROJECT_URL \
  METADATA_PREFIX "computeMetadata/v1/project/numeric-project-id"
#define METADATA_RESOURCE_ID_URL \
  METADATA_PREFIX "computeMetadata/v1/instance/id"

struct wg_metric_cache_s {
  cdtime_t end_time;
  char     end_time_str[MAX_TIMESTAMP_SIZE];
  cdtime_t start_time;
  char     start_time_str[MAX_TIMESTAMP_SIZE];
};
typedef struct wg_metric_cache_s wg_metric_cache_t;

struct wg_callback_s {
  int timeseries_count;
  char *authorization_header;
  char *project;
  char *resource_id;
  char *service;
  char *url;
  char *token_url;
  CURL *curl;
  struct curl_slist *headers;
  char curl_errbuf[CURL_ERROR_SIZE];
  char send_buffer[MAX_BUFFER_SIZE];
  size_t send_buffer_free;
  size_t send_buffer_fill;
  cdtime_t send_buffer_init_time;
  c_avl_tree_t *metric_name_tree;
  c_avl_tree_t *metric_buffer_tree;
  c_avl_tree_t *stream_key_tree;
  cdtime_t token_expire_time;
  pthread_mutex_t send_lock;
#if HAVE_LIBSSL
  EVP_PKEY *pkey;
  char *email;
#endif
};
typedef struct wg_callback_s wg_callback_t;

struct wg_memory_s {
  char *memory;
  size_t size;
};
typedef struct wg_memory_s wg_memory_t;

#if HAVE_LIBSSL
#define OAUTH_EXPIRATION_TIME TIME_T_TO_CDTIME_T(3600)

#define OAUTH_HEADER "{\"alg\":\"RS256\",\"typ\":\"JWT\"}"
#define OAUTH_SCOPE MONITORING_SCOPE
#define OAUTH_AUD "https://accounts.google.com/o/oauth2/token"
static const char OAUTH_CLAIM_FORMAT[] =
    "{"
      "\"iss\":\"%s\","
      "\"scope\":\"%s\","
      "\"aud\":\"%s\","
      "\"exp\":%lu,"
      "\"iat\":%lu"
    "}";
#endif

#define BUFFER_ADDF(...)                                                    \
  do {                                                                      \
    int status;                                                             \
    status = ssnprintf(buffer + offset, buffer_size - offset, __VA_ARGS__); \
    if (status < 1) {                                                       \
      return (-1);                                                          \
    } else if (((size_t)status) >= (buffer_size - offset)) {                \
      return (ENOMEM);                                                      \
    } else                                                                  \
      offset += ((size_t)status);                                           \
  } while (0)

#define BUFFER_ADD(str)                                                     \
  do {                                                                      \
    size_t len = strlen (str);                                              \
    if (len >= (buffer_size - offset))                                      \
      return (ENOMEM);                                                      \
    sstrncpy (buffer + offset, str, len + 1);                               \
    offset += len;                                                          \
  } while (0)

static char *wg_get_authorization_header(wg_callback_t *cb);

static size_t wg_write_memory_cb(void *contents, size_t size, size_t nmemb, /* {{{ */
                                 void *userp) {
  size_t realsize = size * nmemb;
  wg_memory_t *mem = (wg_memory_t *)userp;

  if (0x7FFFFFF0 < mem->size || 0x7FFFFFF0 - mem->size < realsize) {
    ERROR("integer overflow");
    return 0;
  }

  mem->memory = (char *)realloc((void *)mem->memory, mem->size + realsize + 1);
  if (mem->memory == NULL) {
    /* out of memory! */
    ERROR("wg_write_memory_cb: not enough memory (realloc returned NULL)");
    return 0;
  }

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;
  return realsize;
} /* }}} size_t wg_write_memory_cb */

static int wg_get_vl_value(char *buffer, size_t buffer_size, /* {{{ */
                           const data_set_t *ds, const value_list_t *vl,
                           int i) {
  size_t offset = 0;
  memset(buffer, 0, buffer_size);

  if (ds->ds[i].type == DS_TYPE_GAUGE) {
    if (isfinite(vl->values[i].gauge))
      BUFFER_ADDF("%f", vl->values[i].gauge);
    else {
      ERROR("write_gcm: can not take infinite value");
      return (-1);
    }
  } else if (ds->ds[i].type == DS_TYPE_COUNTER)
    BUFFER_ADDF("%llu", vl->values[i].counter);
  else if (ds->ds[i].type == DS_TYPE_DERIVE)
    BUFFER_ADDF("%" PRIi64, vl->values[i].derive);
  else if (ds->ds[i].type == DS_TYPE_ABSOLUTE)
    BUFFER_ADDF("%" PRIu64, vl->values[i].absolute);
  else {
    ERROR("write_gcm: Unknown data source type: %i", ds->ds[i].type);
    return (-1);
  }
  return (0);
} /* }}} int get_vl_value */

static int wg_create_metric_format(char *buffer, size_t buffer_size, /* {{{ */
    const char *metric_name,
    const data_set_t *ds, int i)
{
  size_t offset = 0;

  BUFFER_ADD ("{\"name\": \""); /* body begin */
  BUFFER_ADD (metric_name);
  BUFFER_ADD ("\",\"typeDescriptor\": {"); /* typeDescriptor begin */
  if (ds->ds[i].type == DS_TYPE_GAUGE)
    BUFFER_ADD ("\"metricType\": \"gauge\",");
  else
    BUFFER_ADD ("\"metricType\": \"cumulative\",");
  BUFFER_ADD("\"valueType\" : \"double\"}," /* typeDescriptor end */
      "\"labels\": [" /* labels begin */
        "{\"key\": \"" SERVICE_LABEL "\"},"
        "{\"key\": \"" RESOURCE_ID_LABEL "\"},"
        "{\"key\": \"" METRIC_LABEL_PREFIX "/" TYPE_INSTANCE_LABEL "\"},"
        "{\"key\": \"" METRIC_LABEL_PREFIX "/" PLUGIN_INSTANCE_LABEL "\"}"
      "]"); /* labels end */
  BUFFER_ADD("}"); /* body end */

  DEBUG ("write_gcm plugin: wg_create_metric_format() = %s", buffer);

  return (0);
} /* }}} int wg_create_metric_format */

static int wg_create_metric (const char *metric_name, /* {{{ */
    const data_set_t *ds, int i, const value_list_t *vl,
    wg_callback_t *cb) {
  char final_url[256];
  char buffer[2048];
  char const *authorization_header;
  struct curl_slist *headers = NULL;
  CURL *curl;
  char curl_errbuf[CURL_ERROR_SIZE];
  wg_memory_t res;
  long http_code = 0;
  int status;

  status = ssnprintf(final_url, sizeof(final_url),
      "%s/projects/%s/metricDescriptors",
      cb->url, cb->project);
  if ((status < 1) || ((size_t) status >= sizeof (final_url)))
    return (-1);

  status = wg_create_metric_format (buffer, sizeof (buffer),
      metric_name, ds, i);
  if (status != 0)
    return (status);

  authorization_header = wg_get_authorization_header (cb);
  if (authorization_header == NULL)
    return (-1);

  headers = curl_slist_append (headers, "Content-Type: application/json");
  headers = curl_slist_append (headers, authorization_header);

  curl = curl_easy_init();
  if (!curl)
  {
    ERROR("write_gcm plugin: curl_easy_init failed.");
    curl_slist_free_all (headers);
    return (-1);
  }

  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
  curl_easy_setopt(curl, CURLOPT_URL, final_url);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, buffer);

  res.size = 0;
  res.memory = NULL;
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wg_write_memory_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &res);

  status = curl_easy_perform (curl);
  if (status != CURLE_OK) {
    ERROR("write_gcm plugin: curl_easy_perform failed with status %i: %s",
        status, curl_errbuf);
    sfree (res.memory);
    curl_easy_cleanup(curl);
    curl_slist_free_all (headers);
    return (-1);
  }

  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
  if ((http_code < 200) || (http_code >= 300))
  {
    ERROR ("write_gcm plugin: POST request to %s failed: HTTP error %ld",
        final_url, http_code);
    INFO ("write_gcm plugin: Server replied: %s", res.memory);
    sfree (res.memory);
    curl_easy_cleanup(curl);
    curl_slist_free_all (headers);
    return (-1);
  }

  sfree (res.memory);
  curl_easy_cleanup(curl);
  curl_slist_free_all (headers);
  return (0);
} /* }}} int wg_create_metric */

static int wg_format_metric_name (char *buffer, size_t buffer_size, /* {{{ */
    data_set_t const *ds, value_list_t const *vl,
    int ds_index)
{
  size_t offset = 0;

  BUFFER_ADD (METRIC_LABEL_PREFIX "/");
  BUFFER_ADD (vl->plugin);
  BUFFER_ADD ("/");
  BUFFER_ADD (vl->type);

  if ((ds->ds_num > 1) || (strcasecmp(ds->ds[ds_index].name, "value") != 0))
  {
    BUFFER_ADD ("_");
    BUFFER_ADD (ds->ds[ds_index].name);
  }

  return (0);
} /* }}} int wg_format_metric_name */

static int value_to_timeseries(char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl,
    wg_callback_t *cb, int ds_index,
    wg_metric_cache_t *mb)
{
  /* Variables used by BUFFER_ADD */
  size_t buffer_size;
  size_t offset;

  char metric_name[6 * DATA_MAX_NAME_LEN];
  char *start_time_str;
  char *end_time_str;
  char double_value_str[128];
  int status;

  buffer_size = *ret_buffer_free + *ret_buffer_fill;
  offset = *ret_buffer_fill;

  /* TODO(octo): This assumes that project (a char*) holds a numberic id. */
  BUFFER_ADD("{"
             "\"timeseriesDesc\":{"
             "\"project\":");
  BUFFER_ADD(cb->project);
  BUFFER_ADD(",");

  status = wg_format_metric_name (metric_name, sizeof (metric_name),
      ds, vl, ds_index);
  if (status != 0)
    return (-1);

  /* if the metric name is not found in the cache, create this metric and add
   * the metric name to the cache */
  if (c_avl_get(cb->metric_name_tree, metric_name, NULL) != 0) {
    char *key = strdup (metric_name);

    status = wg_create_metric(metric_name, ds, ds_index, vl, cb);
    if (status != 0)
      return (-1);

    status = c_avl_insert (cb->metric_name_tree, key, NULL);
    if (status != 0)
      NOTICE ("write_gcm plugin: Adding metric \"%s\" to the cache failed. "
          "This will result in multiple metricDescriptors calls, but should "
          "not break stuff.", key);
  }

  BUFFER_ADD ("\"metric\":\"");
  BUFFER_ADD (metric_name);
  BUFFER_ADD ("\",");

  BUFFER_ADD ("\"labels\":{");
  if (vl->type_instance[0] != 0)
  {
    BUFFER_ADD ("\"" METRIC_LABEL_PREFIX "/" TYPE_INSTANCE_LABEL "\":\"");
    BUFFER_ADD (vl->type_instance);
    BUFFER_ADD ("\"");
  }
  if ((vl->type_instance[0] != 0) && (vl->plugin_instance[0] != 0))
  {
    BUFFER_ADD (",");
  }
  if (vl->plugin_instance[0] != 0)
  {
    BUFFER_ADD ("\"" METRIC_LABEL_PREFIX "/" PLUGIN_INSTANCE_LABEL "\":\"");
    BUFFER_ADD (vl->plugin_instance);
    BUFFER_ADD ("\"");
  }

  /* start and end time, for gauge metric, start = end, otherwise it's
   * cumulative metric,  end the current time and start = call back init time
   * - interval */
  if (ds->ds[ds_index].type == DS_TYPE_GAUGE)
    start_time_str = mb->end_time_str;
  else
    start_time_str = mb->start_time_str;
  end_time_str = mb->end_time_str;

  /* value in the point */
  status = wg_get_vl_value (double_value_str, sizeof (double_value_str),
      ds, vl, ds_index);
  if (status != 0)
    return (status);

  BUFFER_ADD("}"  /* labels */
             "}," /* timeseriesDesc */
             "\"point\": [{" /* point */
             "\"start\":\"");
  BUFFER_ADD(start_time_str);
  BUFFER_ADD("\",\"end\":\"");
  BUFFER_ADD(end_time_str);
  BUFFER_ADD("\",\"doubleValue\":");
  BUFFER_ADD(double_value_str);
  BUFFER_ADD("}]"   /* points */
             "},"); /* timeseries */

  *ret_buffer_fill = offset;
  *ret_buffer_free = buffer_size - offset;
  return (0);
} /* }}} int value_to_timeseries */

/* the caller of this function must make sure *ret_buffer_free >= 3 */
static int value_list_to_timeseries(char *buffer, /* {{{ */
    size_t *ret_buffer_fill, size_t *ret_buffer_free,
    const data_set_t *ds, const value_list_t *vl,
    wg_callback_t *cb)
{
  char identifier[6 * DATA_MAX_NAME_LEN];
  wg_metric_cache_t *mb;

  size_t buffer_fill;
  size_t buffer_free;

  int status;
  int i;

  if ((buffer == NULL) || (ret_buffer_fill == NULL) || (ret_buffer_free == NULL)
      || (ds == NULL) || (vl == NULL))
    return (EINVAL);

  buffer_fill = *ret_buffer_fill;
  buffer_free = *ret_buffer_free;

  if (buffer_free < 3)
    return (ENOMEM);

  status = FORMAT_VL (identifier, sizeof(identifier), vl);
  if (status != 0)
  {
    ERROR("write_gcm plugin: FORMAT_VL() failed.");
    return (-1);
  }

  status = c_avl_get (cb->metric_buffer_tree, identifier, (void *) &mb);
  if (status != 0)
  {
    char *key = strdup (identifier);

    mb = malloc (sizeof (*mb));
    memset (mb, 0, sizeof (*mb));

    status = c_avl_insert (cb->metric_buffer_tree, key, mb);
    if (status != 0)
    {
      ERROR ("write_gcm plugin: Adding metric %s to the cache failed.",
          identifier);
      sfree (key);
      sfree (mb);
      return (-1);
    }
  }
  assert (mb != NULL);

  if (mb->start_time == 0) /* new metric */
  {
    size_t len;

    len = cdtime_to_iso8601 (mb->start_time_str, sizeof (mb->start_time_str), vl->time);
    if (len == 0)
    {
      ERROR ("write_gcm plugin: cdtime_to_iso8601 failed.");
      return (-1);
    }
    sstrncpy (mb->end_time_str, mb->start_time_str, sizeof (mb->end_time_str));

    mb->start_time = vl->time;
    mb->end_time = vl->time;
  }
  else if (mb->end_time < vl->time) /* existing metric */
  {
    size_t len;

    len = cdtime_to_iso8601 (mb->end_time_str, sizeof (mb->end_time_str), vl->time);
    if (len == 0)
    {
      ERROR ("write_gcm plugin: cdtime_to_iso8601 failed.");
      return (-1);
    }

    mb->end_time = vl->time;
  }

  for (i = 0; i < ds->ds_num; i++) {
    status = value_to_timeseries(buffer, &buffer_fill, &buffer_free,
        ds, vl, cb, i, mb);
    if (status != 0)
      return (status);
  }

  cb->timeseries_count++;
  *ret_buffer_fill = buffer_fill;
  *ret_buffer_free = buffer_free;
  return (0);
} /* }}} int value_list_to_timeseries */

static int format_timeseries_initialize(char *buffer, /* {{{ */
                                        size_t *ret_buffer_fill,
                                        size_t *ret_buffer_free,
                                        wg_callback_t *cb) {
  size_t ret;
  size_t buffer_fill;
  size_t buffer_free;

  if ((buffer == NULL) || (ret_buffer_fill == NULL) ||
      (ret_buffer_free == NULL))
    return (-EINVAL);

  buffer_fill = *ret_buffer_fill;
  buffer_free = *ret_buffer_free;

  buffer_free = buffer_fill + buffer_free;
  buffer_fill = 0;

  if (buffer_free < 3)
    return (-ENOMEM);

  /* add the initial bracket and the common labels and the begining of
   * timeseries */
  ret = ssnprintf(buffer, buffer_free,
                  "{\"commonLabels\": {"
                  "\"" SERVICE_LABEL
                  "\": \"%s\","
                  "\"" RESOURCE_ID_LABEL
                  "\": \"%s\"},"
                  "\"timeseries\":[",
                  cb->service, cb->resource_id);

  if (ret < 1) {
    return (-1);
  } else if (ret >= buffer_free) {
    return (-ENOMEM);
  } else {
    buffer_fill += ret;
    buffer_free -= ret;
  }

  *ret_buffer_fill = buffer_fill;
  *ret_buffer_free = buffer_free;

  return (0);
} /* }}} int format_timeseries_initialize */

static int format_timeseries_finalize(char *buffer, /* {{{ */
                                      size_t *ret_buffer_fill,
                                      size_t *ret_buffer_free) {
  size_t pos;

  if ((buffer == NULL) || (ret_buffer_fill == NULL) ||
      (ret_buffer_free == NULL)) {
    return (-EINVAL);
  }

  if (*ret_buffer_free < 2) return (-ENOMEM);

  /* add ending square and curly bracket */
  pos = *ret_buffer_fill;
  if (pos <= 0) {
    return (-EINVAL);
  }

  // replace the last comma with ']'
  buffer[pos - 1] = ']';
  buffer[pos] = '}';
  buffer[pos + 1] = 0;

  (*ret_buffer_fill) += 1;
  (*ret_buffer_free) -= 1;

  return (0);
} /* }}} int format_timeseries_finalize */

static void wg_reset_buffer(wg_callback_t *cb) /* {{{ */
{
  cb->timeseries_count = 0;
  cb->send_buffer_free = sizeof (cb->send_buffer);
  cb->send_buffer_fill = 0;
  cb->send_buffer_init_time = cdtime();

  format_timeseries_initialize(cb->send_buffer, &cb->send_buffer_fill,
                               &cb->send_buffer_free, cb);
} /* }}} wg_reset_buffer */

#if HAVE_LIBSSL
static EVP_PKEY *wg_load_pkey(/* {{{ */
    char const *p12_filename, char const *p12_passphrase)
{
  FILE *fp;
  PKCS12 *p12;
  X509 *cert;
  STACK_OF(X509) *ca = NULL;
  EVP_PKEY *pkey = NULL;

  OpenSSL_add_all_algorithms();

  fp = fopen(p12_filename, "rb");
  if (fp == NULL)
  {
    char errbuf[1024];
    ERROR ("write_gcm plugin: Opening private key %s failed: %s",
        p12_filename, sstrerror (errno, errbuf, sizeof (errbuf)));
    return (NULL);
  }

  p12 = d2i_PKCS12_fp(fp, NULL);
  fclose(fp);
  if (p12 == NULL)
  {
    char errbuf[1024];
    ERR_error_string_n (ERR_get_error (), errbuf, sizeof (errbuf));
    ERROR ("write_gcm plugin: Reading private key %s failed: %s",
        p12_filename, errbuf);
    return (NULL);
  }

  if (PKCS12_parse(p12, p12_passphrase, &pkey, &cert, &ca) == 0)
  {
    char errbuf[1024];
    ERR_error_string_n (ERR_get_error (), errbuf, sizeof (errbuf));
    ERROR ("write_gcm plugin: Parsing private key %s failed: %s",
        p12_filename, errbuf);

    if (cert)
      X509_free(cert);
    if (ca)
      sk_X509_pop_free(ca, X509_free);
    PKCS12_free(p12);
    return (NULL);
  }

  return pkey;
} /* }}} EVP_PKEY *wg_load_pkey */

/* Base64-encodes "s" and stores the result in buffer.
 * Returns zero on success, non-zero otherwise. */
static int wg_b64_encode_n(char const *s, size_t s_size, /* {{{ */
    char *buffer, size_t buffer_size)
{
  BIO *b64;
  BUF_MEM *bptr;
  int status;
  size_t i;

  /* Set up the memory-base64 chain */
  b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  b64 = BIO_push(b64, BIO_new(BIO_s_mem()));

  /* Write data to the chain */
  BIO_write(b64, (void const *) s, s_size);
  status = BIO_flush(b64);
  if (status != 1)
  {
    ERROR ("write_gcm plugin: wg_b64_encode: BIO_flush() failed.");
    BIO_free_all(b64);
    return (-1);
  }

  /* Never fails */
  BIO_get_mem_ptr (b64, &bptr);

  if (buffer_size <= bptr->length) {
    ERROR ("write_gcm plugin: wg_b64_encode: Buffer too small.");
    BIO_free_all(b64);
    return (-1);
  }

  /* Copy data to buffer. */
  memcpy(buffer, bptr->data, bptr->length);
  buffer[bptr->length] = 0;

  /* replace + with -, / with _ and remove padding = at the end */
  for (i = 0; i < bptr->length; i++) {
    if (buffer[i] == '+') {
      buffer[i] = '-';
    } else if (buffer[i] == '/') {
      buffer[i] = '_';
    } else if (buffer[i] == '=') {
      buffer[i] = 0;
    }
  }

  BIO_free_all(b64);
  return (0);
} /* }}} int wg_b64_encode_n */

/* Base64-encodes "s" and stores the result in buffer.
 * Returns zero on success, non-zero otherwise. */
static int wg_b64_encode(char const *s, /* {{{ */
    char *buffer, size_t buffer_size)
{
  return (wg_b64_encode_n (s, strlen (s), buffer, buffer_size));
} /* }}} int wg_b64_encode */

static int wg_oauth_get_header(char *buffer, size_t buffer_size) /* {{{ */
{
  char header[] = OAUTH_HEADER;

  return wg_b64_encode (header, buffer, buffer_size);
} /* }}} int wg_oauth_get_header */

static int wg_oauth_get_claim(char *buffer, size_t buffer_size, /* {{{ */
    char const *iss,
    char const *scope,
    char const *aud,
    cdtime_t exp, cdtime_t iat)
{
  char claim[buffer_size];
  int status;

  /* create the claim set */
  status = ssnprintf(claim, sizeof(claim), OAUTH_CLAIM_FORMAT,
      iss, scope, aud,
      (unsigned long) CDTIME_T_TO_TIME_T (exp),
      (unsigned long) CDTIME_T_TO_TIME_T (iat));
  if (status < 1)
    return (-1);
  else if ((size_t)status >= sizeof(claim))
    return (ENOMEM);

  DEBUG("write_gcm plugin: wg_oauth_get_claim() = %s", claim);

  return wg_b64_encode (claim, buffer, buffer_size);
} /* }}} int wg_oauth_get_claim */

static int wg_oauth_get_signature (char *buffer, size_t buffer_size, /* {{{ */
    char const *header, char const *claim, EVP_PKEY *pkey)
{
  EVP_MD_CTX ctx;
  char payload[buffer_size];
  size_t payload_len;
  char signature[buffer_size];
  unsigned int signature_size;
  int status;

  /* Make the string to sign */
  payload_len = ssnprintf(payload, sizeof(payload), "%s.%s", header, claim);
  if (payload_len < 1) {
    return (-1);
  } else if (payload_len >= sizeof(payload)) {
    return (ENOMEM);
  }

  /* Create the signature */
  signature_size = EVP_PKEY_size(pkey);
  if (signature_size > sizeof (signature))
  {
    ERROR ("write_gcm plugin: Signature is too large (%u bytes).", signature_size);
    return (-1);
  }

  /* EVP_SignInit(3SSL) claims this is a void function, but in fact it returns
   * an int. We're not going to rely on this, though. */
  EVP_SignInit(&ctx, EVP_sha256());

  status = EVP_SignUpdate(&ctx, payload, payload_len);
  if (status != 1)
  {
    char errbuf[1024];
    ERR_error_string_n (ERR_get_error (), errbuf, sizeof (errbuf));
    ERROR ("write_gcm plugin: EVP_SignUpdate failed: %s", errbuf);

    EVP_MD_CTX_cleanup(&ctx);
    return (-1);
  }

  status = EVP_SignFinal (&ctx, (unsigned char *) signature, &signature_size, pkey);
  if (status != 1)
  {
    char errbuf[1024];
    ERR_error_string_n (ERR_get_error (), errbuf, sizeof (errbuf));
    ERROR ("write_gcm plugin: EVP_SignFinal failed: %s", errbuf);

    EVP_MD_CTX_cleanup(&ctx);
    return (-1);
  }

  EVP_MD_CTX_cleanup (&ctx);

  return wg_b64_encode_n(signature, (size_t) signature_size, buffer, buffer_size);
} /* }}} int wg_oauth_get_signature */

static int wg_oauth_get_jwt (char *buffer, size_t buffer_size, /* {{{ */
    char const *iss,
    char const *scope,
    char const *aud,
    cdtime_t exp, cdtime_t iat,
    EVP_PKEY *pkey)
{
  char header[buffer_size];
  char claim[buffer_size];
  char signature[buffer_size];
  int status;

  status = wg_oauth_get_header (header, sizeof (header));
  if (status != 0)
    return (-1);

  status = wg_oauth_get_claim (claim, sizeof (claim),
      iss, scope, aud, exp, iat);
  if (status != 0)
    return (-1);

  status = wg_oauth_get_signature (signature, sizeof (signature),
      header, claim, pkey);
  if (status != 0)
    return (-1);

  status = ssnprintf (buffer, buffer_size, "%s.%s.%s",
      header, claim, signature);
  if (status < 1)
    return (-1);
  else if (status >= buffer_size)
    return (ENOMEM);

  return (0);
} /* }}} int wg_oauth_get_jwt */

static int wg_get_token_request_body(wg_callback_t *cb, /* {{{ */
    char *buffer, size_t buffer_size)
{
  char jwt[MAX_ENCODE_SIZE];
  cdtime_t exp;
  cdtime_t iat;
  int status;

  iat = cdtime ();
  exp = iat + OAUTH_EXPIRATION_TIME;

  status = wg_oauth_get_jwt (jwt, sizeof (jwt),
      cb->email, OAUTH_SCOPE, OAUTH_AUD, exp, iat, cb->pkey);
  if (status != 0)
    return (-1);

  status = ssnprintf(buffer, buffer_size,
      "assertion=%s&grant_type=urn%%3Aietf%%3Aparams%%3Aoauth%%3Agrant-"
      "type%%3Ajwt-bearer",
      jwt);
  if (status < 1) {
    return (-1);
  } else if ((size_t)status >= buffer_size) {
    return (ENOMEM);
  }

  return (0);
} /* }}} int wg_get_token_request_body */
#endif

#if HAVE_YAJL_V2
static int wg_parse_token(const char *const input, char *buffer, /* {{{ */
                          const size_t buffer_size, cdtime_t *expire_time,
                          cdtime_t current_time) {
  int status = -1;
  int expire_in_seconds = 0;
  yajl_val root;
  yajl_val token_val;
  yajl_val expire_val;
  char errbuf[1024];
  const char *token_path[] = {"access_token", NULL};
  const char *expire_path[] = {"expires_in", NULL};

  root = yajl_tree_parse(input, errbuf, sizeof(errbuf));
  if (root == NULL) {
    ERROR("write_gcm plugin: wg_parse_token: parse error %s", errbuf);
    status = -1;
    goto bailout;
  }

  token_val = yajl_tree_get(root, token_path, yajl_t_string);
  if (token_val == NULL) {
    ERROR("write_gcm plugin: wg_parse_token: access token field not found");
    status = -1;
    goto bailout;
  }
  sstrncpy(buffer, YAJL_GET_STRING(token_val), buffer_size);

  expire_val = yajl_tree_get(root, expire_path, yajl_t_number);
  if (expire_val == NULL) {
    ERROR("write_gcm plugin: wg_parse_token: expire field found");
    status = -1;
    goto bailout;
  }
  expire_in_seconds = YAJL_GET_INTEGER(expire_val);
  DEBUG("wg_parse_token: expires_in %d", expire_in_seconds);

  *expire_time = current_time + DOUBLE_TO_CDTIME_T(expire_in_seconds) -
                 DOUBLE_TO_CDTIME_T(60);
  status = 0;
bailout:
  if (root) {
    yajl_tree_free(root);
  }
  return status;
} /* }}} int wg_parse_token */
#else /* !HAVE_YAJL_V2 */
typedef struct {
  char *map_key;
  char *access_token;
  long expires_in;
} wg_yajl_context_t;

static char *wg_yajl_strdup(unsigned char const *str, unsigned int len) /* {{{ */
{
  char *ret;

  ret = calloc (len + 1, sizeof (*ret));
  if (ret == NULL)
    return (NULL);

  memcpy (ret, str, len * sizeof (*ret));
  ret[len] = 0;
  return (ret);
} /* }}} char *wg_yajl_strdup */

static int wg_yajl_map_key(void *ctx, unsigned char const *key, unsigned int len) /* {{{ */
{
  wg_yajl_context_t *c = ctx;

  sfree (c->map_key);
  c->map_key = wg_yajl_strdup (key, len);
  return (1);
} /* }}} int wg_yajl_map_key */

static int wg_yajl_string(void *ctx, unsigned char const *str, unsigned int len) /* {{{ */
{
  wg_yajl_context_t *c = ctx;

  if ((c->map_key != NULL)
      && (strcmp ("access_token", c->map_key) == 0))
  {
    sfree (c->access_token);
    c->access_token = wg_yajl_strdup (str, len);
  }

  return (1);
} /* }}} wg_yajl_string */

static int wg_yajl_integer(void *ctx, long value) /* {{{ */
{
  wg_yajl_context_t *c = ctx;

  if ((c->map_key != NULL)
      && (strcmp ("expires_in", c->map_key) == 0))
  {
    c->expires_in = value;
  }

  return (1);
} /* }}} wg_yajl_integer */

static int wg_parse_token(char const *input, /* {{{ */
    char *buffer, size_t const buffer_size,
    cdtime_t *expire_time, cdtime_t current_time)
{
  yajl_callbacks cb = {
    .yajl_integer = wg_yajl_integer,
    .yajl_string = wg_yajl_string,
    .yajl_map_key = wg_yajl_map_key
  };
  yajl_parser_config cfg = {
    .allowComments = 1,
    .checkUTF8 = 1
  };
  wg_yajl_context_t context = {
    .map_key = NULL,
    .access_token = NULL,
    .expires_in = -1
  };

  yajl_handle handle = yajl_alloc (&cb, &cfg, /* alloc = */ NULL, &context);

  yajl_status status = yajl_parse (handle,
      (unsigned char const *) input,
      (unsigned int) strlen (input));
  if ((status != yajl_status_ok)
      && (status != yajl_status_insufficient_data))
  {
    ERROR ("write_gcm plugin: Parsing access token failed.");
    yajl_free (handle);
    return (-1);
  }

  status = yajl_parse_complete (handle);
  if (status != yajl_status_ok)
  {
    ERROR ("write_gcm plugin: Parsing access token failed.");
    yajl_free (handle);
    return (-1);
  }
  yajl_free (handle);

  if (context.access_token == NULL)
  {
    ERROR ("write_gcm plugin: Access token not found.");
    return (-1);
  }
  sstrncpy (buffer, context.access_token, buffer_size);

  if (context.expires_in == -1)
  {
    ERROR ("write_gcm plugin: Expiration time not found.");
    return (-1);
  }

  *expire_time = current_time
    + TIME_T_TO_CDTIME_T(context.expires_in)
    - TIME_T_TO_CDTIME_T(60);
  return (0);

} /* }}} int wg_parse_token */
#endif

static char *wg_get_authorization_header(wg_callback_t *cb) { /* {{{ */
  CURL *curl;
  char *temp;
  int status = 0;
  long http_code = 0;
  wg_memory_t data;
  char access_token[256];
  char authorization_header[256];
  char const *url;
#if HAVE_LIBSSL
  char token_request_body[1024];
#endif
  cdtime_t now;
  cdtime_t expire_time = 0;
  struct curl_slist *header = NULL;

  data.size = 0;
  data.memory = NULL;

  now = cdtime ();

  /* Check if we already have a token and if it is still valid. We will refresh
   * this 60 seconds early to avoid problems due to clock skew. */
  if ((cb->authorization_header != NULL) && (cb->token_expire_time > TIME_T_TO_CDTIME_T (60)))
  {
    expire_time = cb->token_expire_time - TIME_T_TO_CDTIME_T (60);
    if (expire_time > now)
      return (cb->authorization_header);
  }

  curl = curl_easy_init();
  if (curl == NULL)
  {
    ERROR("write_gcm plugin: wg_get_authorization_header: curl_easy_init failed.");
    return (NULL);
  }

  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, cb->curl_errbuf);

  /* get the access token from the metadata first */
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wg_write_memory_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);

#if HAVE_LIBSSL
  if (cb->email != NULL)
  {
    status = wg_get_token_request_body(cb,
        token_request_body, sizeof(token_request_body));
    if (status != 0) {
      ERROR("write_gcm plugin: Failed to get token using service account %s.",
            cb->email);
      status = -1;
      goto bailout;
    }
    url = "https://accounts.google.com/o/oauth2/token";
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, token_request_body);
  }
  else
#endif
  {
    url = cb->token_url;
    header = curl_slist_append(header, METADATA_HEADER);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header);
  }

  assert (url != NULL);
  curl_easy_setopt(curl, CURLOPT_URL, url);

  status = curl_easy_perform(curl);
  if (status != CURLE_OK) {
    ERROR("write_gcm plugin: curl_easy_perform failed with status %i: %s",
        status, cb->curl_errbuf);
    status = -1;
    goto bailout;
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if ((http_code < 200) || (http_code >= 300))
    {
      ERROR ("write_gcm plugin: POST request to %s failed: HTTP error %ld",
          url, http_code);
      if (data.memory != NULL)
        INFO ("write_gcm plugin: Server replied: %s", data.memory);
      status = -1;
      goto bailout;
    }
  }

  status = wg_parse_token(data.memory, access_token, sizeof(access_token),
                          &expire_time, now);
  if (status != 0)
    goto bailout;

  status = ssnprintf (authorization_header, sizeof (authorization_header),
      "Authorization: Bearer %s", access_token);
  if ((status < 1) || ((size_t) status >= sizeof (authorization_header)))
    goto bailout;

  temp = strdup(authorization_header);
  if (temp == NULL) {
    ERROR("write_gcm plugin: strdup failed.");
    goto bailout;
  }
  sfree(cb->authorization_header);
  cb->authorization_header = temp;
  cb->token_expire_time = expire_time;

  INFO ("write_gcm plugin: OAuth2 access token is valid for %.3fs",
      CDTIME_T_TO_DOUBLE (expire_time - now));

  sfree(data.memory);
  if (header != NULL)
    curl_slist_free_all(header);
  curl_easy_cleanup(curl);

  return (cb->authorization_header);

bailout:
  sfree(data.memory);
  if (curl) {
    curl_easy_cleanup(curl);
  }
  if (header) {
    curl_slist_free_all(header);
  }
  return (NULL);
} /* }}} char *wg_get_authorization_header */

static void wg_expire_token (wg_callback_t *cb) /* {{{ */
{
  if ((cb == NULL) || (cb->authorization_header == NULL))
    return;

  INFO ("write_gcm plugin: Unconditionally expiring access token.");

  sfree (cb->authorization_header);
  cb->token_expire_time = 0;
} /* }}} void wg_expire_token */

static int wg_send_buffer(wg_callback_t *cb) /* {{{ */
{
  int status = 0;
  long http_code = 0;
  char final_url[1024];
  char const *authorization_header;
  struct curl_slist *headers = NULL;

  status = ssnprintf(final_url, sizeof(final_url),
      "%s/projects/%s/timeseries:write", cb->url, cb->project);
  if ((status < 1) || ((size_t) status >= sizeof (final_url)))
    return (-1);

  authorization_header = wg_get_authorization_header (cb);
  if (authorization_header == NULL)
    return (-1);

  headers = curl_slist_append (headers, authorization_header);
  headers = curl_slist_append (headers, "Content-Type: application/json");

  curl_easy_setopt(cb->curl, CURLOPT_URL, final_url);
  curl_easy_setopt(cb->curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(cb->curl, CURLOPT_POST, 1L);
  curl_easy_setopt(cb->curl, CURLOPT_POSTFIELDS, cb->send_buffer);

  status = curl_easy_perform(cb->curl);
  if (status != CURLE_OK) {
    ERROR("write_gcm plugin: curl_easy_perform failed with status %i: %s",
        status, cb->curl_errbuf);
    curl_slist_free_all (headers);
    return (-1);
  }

  curl_easy_getinfo (cb->curl, CURLINFO_RESPONSE_CODE, &http_code);
  if ((http_code < 200) || (http_code >= 300))
  {
    ERROR ("write_gcm plugin: POST request to %s failed: HTTP error %ld",
        final_url, http_code);
    curl_slist_free_all (headers);
    return (-1);
  }

  curl_slist_free_all (headers);
  return (status);
} /* }}} wg_send_buffer */

static int wg_callback_init(wg_callback_t *cb) /* {{{ */
{
  if (cb->curl != NULL)
    return (0);

  cb->stream_key_tree = c_avl_create ((void *) strcmp);
  if (cb->stream_key_tree == NULL)
  {
    ERROR("write_gcm plugin: c_avl_create failed.");
    return (-1);
  }

  cb->curl = curl_easy_init();
  if (cb->curl == NULL)
  {
    ERROR("write_gcm plugin: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt(cb->curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(cb->curl, CURLOPT_USERAGENT,
                   PACKAGE_NAME "/" PACKAGE_VERSION);

  cb->headers = NULL;
  cb->headers = curl_slist_append(cb->headers, "Accept:  */*");
  cb->headers =
      curl_slist_append(cb->headers, "Content-Type: application/json");
  cb->headers = curl_slist_append(cb->headers, "Expect:");
  curl_easy_setopt(cb->curl, CURLOPT_HTTPHEADER, cb->headers);
  curl_easy_setopt(cb->curl, CURLOPT_ERRORBUFFER, cb->curl_errbuf);
  wg_reset_buffer(cb);

  return (0);
} /* }}} int wg_callback_init */

static int wg_flush_nolock(cdtime_t timeout, wg_callback_t *cb) /* {{{ */
{
  void *key;
  void *value;
  int status;

  DEBUG(
      "write_gcm plugin: wg_flush_nolock: timeout = %.3f; "
      "send_buffer_fill = %zu;",
      CDTIME_T_TO_DOUBLE(timeout), cb->send_buffer_fill);

  status = 0;

  /* Clear the stream_key cache. */
  while (c_avl_pick(cb->stream_key_tree, &key, &value) == 0) {
    sfree (key);
    assert (value == NULL);
  }

  if (cb->timeseries_count == 0)
  {
    cb->send_buffer_init_time = cdtime();
    return (0);
  }

  /* timeout == 0  => flush unconditionally */
  if (timeout > 0)
  {
    cdtime_t now = cdtime();

    if ((cb->send_buffer_init_time + timeout) > now)
      return (0);
  }

  status = format_timeseries_finalize(cb->send_buffer,
                                      &cb->send_buffer_fill, &cb->send_buffer_free);
  if (status != 0)
  {
    ERROR("write_gcm: wg_flush_nolock: format_timeseries_finalize failed.");
    wg_reset_buffer (cb);
    return (status);
  }

  status = wg_send_buffer(cb);
  if (status == 0)
  {
    wg_reset_buffer (cb);
    return (0);
  }

  /* Special case for "not authorized" errors. */
  if (status == 401)
  {
    WARNING ("write_gcm plugin: Sending buffer failed with \"Not Authorized\" "
        "error. Retrying with fresh access token.");

    /* Assume token expired. */
    wg_expire_token (cb);

    status = wg_send_buffer (cb);
    if (status != 0)
      ERROR ("write_gcm plugin: Sending buffer failed on retry with status %d. "
          "Giving up.", status);
  }
  else
  {
    ERROR ("write_gcm plugin: Sending buffer failed with status %d.", status);
  }

  wg_reset_buffer (cb);
  return (status);
} /* }}} wg_flush_nolock */

static int wg_flush(cdtime_t timeout, /* {{{ */
                    const char *identifier __attribute__((unused)),
                    user_data_t *user_data) {
  wg_callback_t *cb;
  int status;

  if (user_data == NULL) return (-EINVAL);

  cb = user_data->data;

  pthread_mutex_lock(&cb->send_lock);

  if (cb->curl == NULL) {
    status = wg_callback_init(cb);
    if (status != 0) {
      ERROR("write_gcm plugin: wg_callback_init failed.");
      pthread_mutex_unlock(&cb->send_lock);
      return (-1);
    }
  }

  status = wg_flush_nolock(timeout, cb);
  pthread_mutex_unlock(&cb->send_lock);

  return (status);
} /* }}} int wg_flush */

static void wg_cache_tree_free(c_avl_tree_t *tree) /* {{{ */
{
  char *name;
  wg_metric_cache_t *value;
  if (tree == NULL) {
    return;
  }

  while (c_avl_pick(tree, (void *)&name, (void *)&value) == 0) {
    sfree(name);
    sfree(value);
  }
  c_avl_destroy(tree);
} /* }}} void wg_cache_tree_free */

static void wg_callback_free(void *data) /* {{{ */
{
  wg_callback_t *cb;
  void *value;
  char *name;

  if (data == NULL) return;

  cb = data;

  wg_flush_nolock(/* timeout = */ 0, cb);

  if (cb->curl) {
    curl_easy_cleanup(cb->curl);
  }

  if (cb->headers) {
    curl_slist_free_all(cb->headers);
  }

#if HAVE_LIBSSL
  if (cb->pkey) {
    EVP_PKEY_free(cb->pkey);
  }
  sfree(cb->email);
#endif

  sfree(cb->project);
  sfree(cb->resource_id);
  sfree(cb->service);
  sfree(cb->url);
  sfree(cb->token_url);
  sfree(cb->authorization_header);
  wg_cache_tree_free(cb->metric_buffer_tree);

  if (cb->metric_name_tree) {
    while (c_avl_pick(cb->metric_name_tree, (void *)&name, &value) == 0) {
      sfree(name);
    }
    c_avl_destroy(cb->metric_name_tree);
    cb->metric_name_tree = NULL;
  }

  sfree(cb);

} /* }}} void wg_callback_free */

static int wg_write(const data_set_t *ds, const value_list_t *vl, /* {{{ */
                    user_data_t *user_data) {
  wg_callback_t *cb;
  char identifier[6 * DATA_MAX_NAME_LEN];
  char *key;
  int status;

  if (user_data == NULL)
    return (EINVAL);
  cb = user_data->data;

  pthread_mutex_lock(&cb->send_lock);

  if (cb->curl == NULL) {
    status = wg_callback_init(cb);
    if (status != 0) {
      ERROR("write_gcm plugin: wg_callback_init failed.");
      pthread_mutex_unlock(&cb->send_lock);
      return (-1);
    }
  }

  /*
   * Flush when:
   * - the stream_key_tree cache already contains the identifier. We must flush
   *   since Cloud Monitoring can not take more than 1 points from the same
   *   "stream" / metric in one request.
   * - out of memory while trying to add the current vl to the buffer. This is
   *   indicated by the buffer handling functions returning ENOMEM.
   */

  status = FORMAT_VL (identifier, sizeof (identifier), vl);
  if (status != 0)
  {
    pthread_mutex_unlock (&cb->send_lock);
    ERROR ("write_gcm plugin: FORMAT_VL failed.");
    return (-1);
  }

  assert (cb->stream_key_tree != NULL);
  status = c_avl_get (cb->stream_key_tree, identifier, NULL);
  if (status == 0)
  {
    DEBUG ("write_gcm plugin: Found %s in stream_key_tree.", identifier);
    wg_flush_nolock (/* timeout = */ 0, cb);
  }

  key = strdup (identifier);
  if (key == NULL)
  {
    pthread_mutex_unlock (&cb->send_lock);
    ERROR ("write_gcm plugin: strdup failed.");
    return (-1);
  }

  status = c_avl_insert (cb->stream_key_tree, key, NULL);
  if (status != 0)
  {
    pthread_mutex_unlock (&cb->send_lock);
    ERROR ("write_gcm plugin: c_avl_insert (\"%s\") failed.", key);
    sfree (key);
    return (-1);
  }

  status = value_list_to_timeseries(cb->send_buffer, &cb->send_buffer_fill,
      &cb->send_buffer_free, ds, vl, cb);
  if (status == ENOMEM)
  {
    wg_flush_nolock (/* timeout = */ 0, cb);
    status = value_list_to_timeseries(cb->send_buffer, &cb->send_buffer_fill,
        &cb->send_buffer_free, ds, vl, cb);
  }

  pthread_mutex_unlock(&cb->send_lock);
  return (status);
} /* }}} int wg_write */

static int wg_read_url(const char *url, const char *header, char **ret_body,
                       char **ret_header) /* {{{ */
{
  CURL *curl;
  int status = 0;
  long http_code = 0;
  char curl_errbuf[CURL_ERROR_SIZE];
  char *body_string;
  char *header_string;
  wg_memory_t output_body;
  wg_memory_t output_header;
  struct curl_slist *headers = NULL;

  curl = curl_easy_init();
  if (!curl) {
    ERROR("wg_read_url: curl_easy_init failed.");
    return (-1);
  }

  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
  output_body.size = 0;
  output_body.memory = NULL;
  output_header.size = 0;
  output_header.memory = NULL;

  /* set up the write function and write buffer  for both body and header*/
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  if (ret_body != NULL) {
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, wg_write_memory_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &output_body);
  }
  if (ret_header != NULL) {
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, wg_write_memory_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEHEADER, &output_header);
  }

  /*set the header if it's not null*/
  if (header) {
    headers = curl_slist_append(headers, METADATA_HEADER);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  }

  curl_easy_setopt(curl, CURLOPT_URL, url);
  status = curl_easy_perform(curl);
  if (status != CURLE_OK) {
    ERROR("wg_read_url: curl_easy_perform failed with status %i: %s", status,
          curl_errbuf);
    status = -1;
    goto bailout;
  } else {
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if ((http_code < 200) || (http_code >= 300))
    {
      ERROR ("write_gcm plugin: Reading %s failed: HTTP error %ld",
          url, http_code);
      status = -1;
      goto bailout;
    }
  }

  if (ret_body != NULL) {
    body_string = strdup(output_body.memory);
    if (body_string == NULL) {
      status = -1;
      goto bailout;
    }
    sfree(*ret_body);
    *ret_body = body_string;
  }

  if (ret_header != NULL) {
    header_string = strdup(output_header.memory);
    if (header_string == NULL) {
      status = -1;
      goto bailout;
    }
    sfree(*ret_header);
    *ret_header = header_string;
  }

bailout:
  sfree(output_body.memory);
  sfree(output_header.memory);
  curl_easy_cleanup(curl);
  if (headers)
    curl_slist_free_all(headers);
  return status;
} /* }}} int wg_read_url */

static char *wg_determine_project (void) /* {{{ */
{
  char *project = NULL;
  int status;

  status = wg_read_url (METADATA_PROJECT_URL, METADATA_HEADER,
      /* body = */ &project, /* header = */ NULL);
  if (status != 0)
  {
    WARNING ("write_gcm plugin: Unable to determine project number.");
    return (NULL);
  }

  return (project);
} /* }}} char *wg_determine_project */

static char *wg_determine_resource_id (void) /* {{{ */
{
  char *resource_id = NULL;
  int status;

  status = wg_read_url (METADATA_RESOURCE_ID_URL, METADATA_HEADER,
      /* body = */ &resource_id, /* header = */ NULL);
  if (status != 0)
  {
    WARNING ("write_gcm plugin: Unable to determine resource ID.");
    return (NULL);
  }

  return (resource_id);
} /* }}} char *wg_determine_resource_id */

static void wg_check_scope (void) /* {{{ */
{
  char *scope = NULL;
  int status;

  status = wg_read_url (METADATA_SCOPE_URL, METADATA_HEADER,
      /* body = */ &scope, /* header = */ NULL);
  if (status != 0)
  {
    WARNING ("write_gcm plugin: Unable to determine scope of this instance.");
    return;
  }

  if (strstr (scope, MONITORING_SCOPE) == NULL)
  {
    size_t scope_len;

    /* Strip trailing newline characers for printing. */
    scope_len = strlen (scope);
    while ((scope_len > 0) && (iscntrl ((int) scope[scope_len - 1])))
      scope[--scope_len] = 0;

    WARNING ("write_gcm plugin: The determined scope of this instance "
        "(\"%s\") does not contain the monitoring scope (\"%s\"). You need "
        "to add this scope to the list of scopes passed to gcutil with "
        "--service_account_scopes when creating the instance. "
        "Alternatively, to use this plugin on an instance which does not "
        "have this scope, use a Service Account.",
        scope, MONITORING_SCOPE);
  }

  sfree (scope);
} /* }}} void wg_check_scope */

static int wg_config(oconfig_item_t *ci) /* {{{ */
{
  wg_callback_t *cb;
  user_data_t user_data;
#if HAVE_LIBSSL
  char *p12_filename = NULL;
  char *p12_passphrase = NULL;
#endif
  int i;

  if (ci == NULL) {
    return (-1);
  }

  cb = malloc(sizeof(*cb));
  if (cb == NULL) {
    ERROR("write_gcm plugin: malloc failed.");
    return (-1);
  }
  memset(cb, 0, sizeof(*cb));
  cb->url = strdup(MONITORING_URL);
  cb->service = strdup(DEFAULT_SERVICE);

  cb->project = NULL;
  cb->authorization_header = NULL;
  cb->token_url = NULL;
  cb->curl = NULL;
  cb->resource_id = NULL;

#if HAVE_LIBSSL
  cb->pkey = NULL;
  cb->email = NULL;
#endif

  cb->metric_buffer_tree =
      c_avl_create((int (*)(const void *, const void *))strcmp);
  cb->metric_name_tree =
      c_avl_create((int (*)(const void *, const void *))strcmp);

  pthread_mutex_init(&cb->send_lock, /* attr = */ NULL);

  for (i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Project", child->key) == 0)
      cf_util_get_string(child, &cb->project);
    else if (strcasecmp("Url", child->key) == 0)
      cf_util_get_string(child, &cb->url);
    else if (strcasecmp("TokenUrl", child->key) == 0)
      cf_util_get_string(child, &cb->token_url);
    else if (strcasecmp("ResourceId", child->key) == 0)
      cf_util_get_string(child, &cb->resource_id);
    else if (strcasecmp("Service", child->key) == 0)
      cf_util_get_string(child, &cb->service);
#if HAVE_LIBSSL
    else if (strcasecmp("Email", child->key) == 0)
      cf_util_get_string(child, &cb->email);
    else if (strcasecmp("PrivateKeyFile", child->key) == 0)
      cf_util_get_string(child, &p12_filename);
    else if (strcasecmp("PrivateKeyPass", child->key) == 0)
      cf_util_get_string(child, &p12_passphrase);
#endif
    else {
      ERROR ("write_gcm plugin: Invalid configuration option: %s.",
          child->key);
      sfree (cb);
      return (-1);
    }
  }

#if HAVE_LIBSSL
  /* Check if user wants to use a service account. */
  if ((cb->email != NULL)
      || (p12_filename != NULL)
      || (p12_passphrase != NULL))
  {
    /* Check that *all* required fields are set. */
    if (cb->email == NULL)
    {
      ERROR ("write_gcm plugin: It appears you're trying to use a service "
          "account, but the \"Email\" option is missing.");
      sfree (cb);
      return (-1);
    }
    else if (p12_filename == NULL)
    {
      ERROR ("write_gcm plugin: It appears you're trying to use a service "
          "account, but the \"PrivateKeyFile\" option is missing.");
      sfree (cb);
      return (-1);
    }

    if (p12_passphrase == NULL)
    {
      WARNING ("write_gcm plugin: It appears you're trying to use a service "
          "account, but the \"PrivateKeyPass\" option is missing. "
          "Will assume \"notasecret\".");
      p12_passphrase = strdup ("notasecret");
    }

    if (cb->token_url)
    {
      ERROR ("write_gcm plugin: Both, \"Email\" and \"TokenUrl\" were "
          "specified. Now I don't know whether to use the service account "
          "or the instance token. Please remove one of the two options.");
      sfree (cb);
      return (-1);
    }

    cb->pkey = wg_load_pkey (p12_filename, p12_passphrase);
    if (cb->pkey == NULL)
    {
      ERROR ("write_gcm plugin: Loading private key from %s failed.", p12_filename);
      sfree (cb);
      return (-1);
    }

    sfree (p12_filename);
    sfree (p12_passphrase);
  }
  else
#endif /* HAVE_LIBSSL */
  {
    /* Not a service account, check the "scope" and warn the user if incorrect. */
    wg_check_scope ();

    if (cb->token_url == NULL)
      cb->token_url = strdup (METADATA_TOKEN_URL);
  }

  if (cb->project == NULL)
  {
    cb->project = wg_determine_project ();
    if (cb->project == NULL)
    {
      ERROR("write_gcm plugin: Unable to determine the project number. "
          "Please specify the \"Project\" option manually.");
      sfree (cb);
      return -1;
    }
  }

  if (cb->resource_id == NULL)
  {
    cb->resource_id = wg_determine_resource_id ();
    if (cb->resource_id == NULL)
    {
      ERROR("write_gcm plugin: Unable to determine the resource ID. "
          "Please specify the \"ResourceId\" option manually.");
      sfree (cb);
      return -1;
    }
  }

  DEBUG("write_gcm: Registering write callback with URL %s", cb->url);

  /* email -> pkey == (!email || pkey) */
  assert ((cb->email == NULL) || (cb->pkey != NULL));

  memset(&user_data, 0, sizeof(user_data));
  user_data.data = cb;
  user_data.free_func = NULL;
  plugin_register_flush("write_gcm", wg_flush, &user_data);

  user_data.free_func = wg_callback_free;
  plugin_register_write("write_gcm", wg_write, &user_data);

  return (0);
} /* }}} int wg_config */

static int wg_init(void) /* {{{ */
{
  /* Call this while collectd is still single-threaded to avoid
   * initialization issues in libgcrypt. */
  curl_global_init(CURL_GLOBAL_SSL);

#if HAVE_LIBSSL
  ERR_load_crypto_strings ();
#endif

  return (0);
} /* }}} int wg_init */

void module_register(void) /* {{{ */
{
  plugin_register_complex_config("write_gcm", wg_config);
  plugin_register_init("write_gcm", wg_init);
} /* }}} void module_register */
