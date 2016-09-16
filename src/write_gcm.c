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
 *   Corey Kosak <kosak at google.com>
 **/

#include "collectd.h"
#include "common.h"
#include "daemon/collectd.h"
#include "daemon/utils_cache.h"
#include "daemon/utils_time.h"
#include "plugin.h"
#include "configfile.h"
#include "stackdriver-agent-keys.h"
#include "utils_avltree.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "curl/curl.h"
#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>

#include <yajl/yajl_gen.h>
#include <yajl/yajl_parse.h>
#if HAVE_YAJL_YAJL_VERSION_H
# include <yajl/yajl_version.h>
#endif

#if HAVE_PTHREAD_H
#include <pthread.h>
#endif

#include <inttypes.h>

//==============================================================================
//==============================================================================
//==============================================================================
// Settings that affect the behavior of this plugin.
//==============================================================================
//==============================================================================
//==============================================================================

static const char this_plugin_name[] = "write_gcm";

// Presence of this key in the metric meta_data causes the metric to be
// sent to the GCMv3 API instead of the Agent Translation Service.
static const char custom_metric_key[] = "stackdriver_metric_type";

static const char custom_metric_prefix[] = "custom.googleapis.com/";

static const char custom_metric_label_prefix[] = "label:";

// The special HTTP header that needs to be added to any call to the GCP
// metadata server.
static const char gcp_metadata_header[] = "Metadata-Flavor: Google";

// The Agent Translation Service endpoint. This is in printf format,
// with a single %s placeholder which holds the name of the project.
static const char agent_translation_service_default_format_string[] =
  "https://monitoring.googleapis.com/v3/projects/%s/collectdTimeSeries";

static const char custom_metrics_default_format_string[] =
  "https://monitoring.googleapis.com/v3/projects/%s/timeSeries";

// The application/JSON content header.
static const char json_content_type_header[] = "Content-Type: application/json";

// Used when we are in end-to-end test mode (-T from the command line) to
// indicate that some important error occurred during processing so that we can
// bubble it back up to the exit status of collectd.
static _Bool wg_some_error_occured_g = 0;

// The maximum number of entries we keep in our processing queue before flushing
// it. Ordinarily a flush happens every minute or so, but we also flush if the
// list size exceeds a certain value.
#define QUEUE_FLUSH_SIZE 100

// The maximum numbers of entries we keep in our queue before we start dropping
// entries. If the consumer thread gets way backed up, we won't keep more than
// this many items in our queue.
#define QUEUE_DROP_SIZE 1000

// Size of the JSON buffer sent to the server. At flush time we format a JSON
// message to send to the server.  We would like it to be no more than a certain
// number of bytes in size. We make this a 'soft' limit so that when the target
// is reached, there is a little bit of margin to close out the JSON message
// (finish the current array we are building, close out various records etc)
// so that we cam always try to send a valid JSON message.

// The "soft target" for the max size of our json messages.
#define JSON_SOFT_TARGET_SIZE 64000

// The maximum size of the project id (platform-defined).
#define MAX_PROJECT_ID_SIZE ((size_t) 64)

// The size of the URL buffer.
#define URL_BUFFER_SIZE ((size_t) 512)

//==============================================================================
//==============================================================================
//==============================================================================
// Misc utility functions.
//==============================================================================
//==============================================================================
//==============================================================================

static _Bool wg_end_to_end_test_mode() {
  return atoi(global_option_get("ReadThreads")) == -1;
}

// Prints data to a buffer. *buffer and *size are adjusted by the number of
// characters printed. Remaining arguments are the same as snprintf. Does not
// overwrite the bounds of the buffer under any circumstances. When successful,
// leaves *buffer pointing directly at a terminating NUl character (just like
// snprintf).
//
// This method is designed to allow the caller to do series of calls to
// bufprintf, and only check for errors at the end of the series rather than
// after every call. This leads to shorter, more readable code in exchange for
// wasted CPU effort in the event of an error. Since errors are expected to be
// rare, this is a worthwhile tradeoff.
//
// Two kinds of errors are possible, and are indicated by the value of *size.
// 1. If the buffer fills up (because the number of characters fed to it either
// reached or exceeded its capacity), *size will be 1. (This represents the
// fact that there is just enough space left for the terminating NUL). Note that
// a buffer which is exactly full is indistinguishable from a buffer that has
// overflowed. This distinction does not matter for our purposes, and it is far
// more convenient to treat the "buffer exactly full" case as though it was an
// overflow rather than separating it out.
//
// 2. If vsprintf returns an error, *size will be forced to 0.
//
// Callers who do not care about this distinction can just check for *size > 1
// as a success indication.
//
// Example usage:
//   char buffer[1024];
//   char *p = buffer;
//   size_t s = sizeof(buffer);
//   bufprintf(&p, &s, fmt, args...);
//   bufprintf(&p, &s, fmt, args...);  /* add more */
//   bufprintf(&p, &s, fmt, args...);  /* add yet more */
//   /* check for errors here */
//   if (s < 2) {
//     ERROR("error (s==0) or overflow (s==1)");
//     return -1;
//   }
static void bufprintf(char **buffer, size_t *size, const char *fmt, ...) {
  if (*size == 0) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  int result = vsnprintf(*buffer, *size, fmt, ap);
  va_end(ap);

  if (result < 0) {
    *size = 0;
    return;
  }
  // If the result was *size or more, the output was truncated. In that case,
  // adjust the pointer and size so they are pointing to the last byte (the
  // terminating NUL).
  if (result >= *size) {
    result = *size - 1;
  }
  *buffer += result;
  *size -= result;
}

// Some methods for manipulating value_t's in a type-neutral way.
static void wg_value_set_zero(int ds_type, value_t *value) {
  memset(value, 0, sizeof(*value));
}

// Calculates *dest = *a - *b. It is OK for the pointer operands to overlap.
static void wg_value_subtract(int ds_type, value_t *dest, const value_t *a,
    const value_t *b) {
  switch (ds_type) {
    case DS_TYPE_COUNTER: {
      dest->counter = a->counter - b->counter;
      return;
    }
    case DS_TYPE_GAUGE: {
      dest->gauge = a->gauge - b->gauge;
      return;
    }
    case DS_TYPE_DERIVE: {
      dest->derive = a->derive - b->derive;
      return;
    }
    case DS_TYPE_ABSOLUTE: {
      dest->absolute = a->absolute - b->absolute;
      return;
    }
    default: {
      assert(0);
    }
  }
}

// Returns true iff *a < *b. It is OK for the pointer operands to overlap.
static _Bool wg_value_less(int ds_type, const value_t *a, const value_t *b) {
  switch (ds_type) {
    case DS_TYPE_COUNTER: {
      return a->counter < b->counter;
    }
    case DS_TYPE_GAUGE: {
      return a->gauge < b->gauge;
    }
    case DS_TYPE_DERIVE: {
      return a->derive < b->derive;
    }
    case DS_TYPE_ABSOLUTE: {
      return a->absolute < b->absolute;
    }
    default: {
      assert(0);
    }
  }
}

static char *wg_read_all_bytes(const char *filename, const char *mode) {
  // Items to clean up at the end.
  char *result = NULL;
  char *buffer = NULL;
  FILE *f = NULL;

  f = fopen(filename, mode);
  if (f == NULL) {
    ERROR("write_gcm: wg_read_all_bytes: can't open \"%s\"", filename);
    goto leave;
  }
  if (fseek(f, 0L, SEEK_END) != 0) {
    ERROR("write_gcm: fseek failed");
    goto leave;
  }
  long size = ftell(f);
  if (size < 0) {
    ERROR("write_gcm: ftell failed");
    goto leave;
  }
  rewind(f);
  buffer = malloc(size + 1);
  if (buffer == NULL) {
    ERROR("write_gcm: wg_read_all_bytes: malloc failed");
    goto leave;
  }

  size_t bytes_read = fread(buffer, 1, size, f);
  if (bytes_read != size) {
    ERROR("write_gcm: wg_read_all_bytes: fread failed");
    goto leave;
  }

  buffer[size] = 0;
  result = buffer;
  buffer = NULL;

 leave:
  sfree(buffer);
  if (f != NULL) {
    fclose(f);
  }
  return result;
}

//==============================================================================
//==============================================================================
//==============================================================================
// Credential submodule.
//==============================================================================
//==============================================================================
//==============================================================================
typedef struct {
  char *email;
  char *project_id;
  EVP_PKEY *private_key;
} credential_ctx_t;

static credential_ctx_t *wg_credential_ctx_create_from_p12_file(
    const char *email, const char *key_file, const char *passphrase);
static credential_ctx_t *wg_credential_ctx_create_from_json_file(
    const char *cred_file);
static void wg_credential_ctx_destroy(credential_ctx_t *ctx);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
// Load the private key from 'filename'. Caller owns result.
static EVP_PKEY *wg_credential_contex_load_pkey(char const *filename,
                                                char const *passphrase);

static credential_ctx_t *wg_credential_ctx_create_from_p12_file(
    const char *email, const char *key_file, const char *passphrase) {
  credential_ctx_t *result = calloc(1, sizeof(*result));
  if (result == NULL) {
    ERROR("write_gcm: wg_credential_ctx_create_from_p12_file: calloc failed.");
    return NULL;
  }
  result->email = sstrdup(email);
  result->private_key = wg_credential_contex_load_pkey(key_file, passphrase);
  if (result->private_key == NULL) {
    wg_credential_ctx_destroy(result);
    return NULL;
  }
  return result;
}

int wg_extract_toplevel_json_string(const char *json, const char *key,
                                    char **result);

static credential_ctx_t *wg_credential_ctx_create_from_json_file(
    const char *cred_file) {
  // Things to clean up upon exit.
  credential_ctx_t *result = NULL;
  credential_ctx_t *ctx = NULL;
  char *creds = NULL;
  char *private_key_pem = NULL;
  PKCS8_PRIV_KEY_INFO *p8inf = NULL;
  BIO *in = NULL;

  ctx = calloc(1, sizeof(*ctx));
  if (ctx == NULL) {
    ERROR("write_gcm: wg_credential_ctx_create_from_cred_file: calloc failed.");
    goto leave;
  }

  creds = wg_read_all_bytes(cred_file, "r");
  if (creds == NULL) {
    ERROR("write_gcm: Failed to read application default credentials file %s",
          cred_file);
    goto leave;
  }

  if (wg_extract_toplevel_json_string(creds, "client_email", &ctx->email)
      != 0) {
    ERROR("write_gcm: Couldn't find 'client_email' entry in credentials file.");
    goto leave;
  }
  // use the client email to determine the project
  if (strstr(ctx->email, "@developer.gserviceaccount.com") != NULL) {
    // old style email address like projectnumber-hash@developer.gserviceaccount.com
    char *dash;
    dash = strstr(ctx->email, "-");
    if (dash != NULL) {
      char * project = sstrdup(ctx->email);
      dash = strstr(project, "-");
      *dash = '\0';
      ctx->project_id = project;
    }
  } else if (strstr(ctx->email, ".iam.gserviceaccount.com") != NULL) {
    // new style email address like string@project.iam.gserviceaccount.com
    char *at, *dot;
    at = strstr(ctx->email, "@");
    dot = strstr(ctx->email, ".iam.gserviceaccount.com");
    if (at != NULL && dot != NULL) {
      char *project = malloc(dot - at) + 1;
      project = strndup(at+1, dot - at - 1);
      ctx->project_id = project;
    }
  }
  if (strlen(ctx->project_id) > MAX_PROJECT_ID_SIZE) {
    ERROR("write_gcm: project id length (%zu) is larger than %zu characters",
          strlen(ctx->project_id), MAX_PROJECT_ID_SIZE);
    goto leave;
  }

  if (wg_extract_toplevel_json_string(creds, "private_key", &private_key_pem)
      != 0) {
    ERROR("write_gcm: Couldn't find 'private_key' entry in credentials file.");
    goto leave;
  }

  in = BIO_new_mem_buf((void*)private_key_pem, -1);
  if (in == NULL) {
    ERROR("write_gcm: BIO_new_mem_buf failed.");
    goto leave;
  }
  p8inf = PEM_read_bio_PKCS8_PRIV_KEY_INFO(in, NULL, NULL, NULL);
  if (p8inf == NULL) {
    ERROR("write_gcm: PEM_read_bio_PKCS8_PRIV_KEY_INFO failed.");
    goto leave;
  }
  ctx->private_key = EVP_PKCS82PKEY(p8inf);
  if (ctx->private_key == NULL) {
    ERROR("write_gcm: EVP_PKCS82PKEY failed.");
    goto leave;
  }
  INFO("write_gcm: json credentials parsed successfully. email=%s, "
       "project=%s", ctx->email, ctx->project_id);

  result = ctx;
  ctx = NULL;

 leave:
  if (p8inf != NULL) {
    PKCS8_PRIV_KEY_INFO_free(p8inf);
  }
  if (in != NULL) {
    BIO_free(in);
  }
  wg_credential_ctx_destroy(ctx);
  sfree(private_key_pem);
  sfree(creds);
  return result;
}

static void wg_credential_ctx_destroy(credential_ctx_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  if (ctx->private_key != NULL) {
    EVP_PKEY_free(ctx->private_key);
  }
  sfree(ctx->email);
  sfree(ctx->project_id);
  sfree(ctx);
}

static EVP_PKEY *wg_credential_contex_load_pkey(char const *filename,
                                                char const *passphrase) {
  OpenSSL_add_all_algorithms();
  FILE *fp = fopen(filename, "rb");
  if (fp == NULL) {
    ERROR("write_gcm: Failed to open private key file %s", filename);
    return NULL;
  }

  PKCS12 *p12 = d2i_PKCS12_fp(fp, NULL);
  fclose(fp);
  char err_buf[1024];
  if (p12 == NULL) {
    ERR_error_string_n(ERR_get_error(), err_buf, sizeof (err_buf));
    ERROR("write_gcm: Reading private key %s failed: %s", filename, err_buf);
    return NULL;
  }

  EVP_PKEY *pkey = NULL;
  X509 *cert = NULL;
  STACK_OF(X509) *ca = NULL;
  int result = PKCS12_parse(p12, passphrase, &pkey, &cert, &ca); // 0 is failure
  if (result == 0) {
    ERR_error_string_n(ERR_get_error(), err_buf, sizeof (err_buf));
    ERROR("write_gcm: Parsing private key %s failed: %s", filename, err_buf);
    PKCS12_free(p12);
    return NULL;
  }

  sk_X509_pop_free(ca, X509_free);
  X509_free(cert);
  PKCS12_free(p12);
  return pkey;
}

//==============================================================================
//==============================================================================
//==============================================================================
// CURL submodule.
//==============================================================================
//==============================================================================
//==============================================================================

// Does an HTTP GET or POST, with optional HTTP headers. The type of request is
// determined by 'body': if 'body' is NULL, does a GET, otherwise does a POST.
// If curl_easy_init() or curl_easy_perform() fail, returns -1.
// If they succeed but the HTTP response code is >= 400, returns -2.
// Otherwise returns 0.
static int wg_curl_get_or_post(char *response_buffer,
    size_t response_buffer_size, const char *url, const char *body,
    const char **headers, int num_headers);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
typedef struct {
  char *data;
  size_t size;
} wg_curl_write_ctx_t;

static size_t wg_curl_write_callback(char *ptr, size_t size, size_t nmemb,
                                     void *userdata);

static int wg_curl_get_or_post(char *response_buffer,
    size_t response_buffer_size, const char *url, const char *body,
    const char **headers, int num_headers) {
  DEBUG("write_gcm: Doing %s request: url %s, body %s, num_headers %d",
        body == NULL ? "GET" : "POST",
        url, body, num_headers);
  CURL *curl = curl_easy_init();
  if (curl == NULL) {
    ERROR("write_gcm: curl_easy_init failed");
    return -1;
  }
  const char *collectd_useragent = COLLECTD_USERAGENT;
  struct curl_slist *curl_headers = NULL;
  int i;
  for (i = 0; i < num_headers; ++i) {
    curl_headers = curl_slist_append(curl_headers, headers[i]);
  }
  wg_curl_write_ctx_t write_ctx = {
     .data = response_buffer,
     .size = response_buffer_size
  };

  curl_easy_setopt(curl, CURLOPT_URL, url);
  curl_easy_setopt(curl, CURLOPT_USERAGENT, collectd_useragent);
  if (curl_headers != NULL) {
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, curl_headers);
  }
  if (body != NULL) {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
  }
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &wg_curl_write_callback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_ctx);
  // http://stackoverflow.com/questions/9191668/error-longjmp-causes-uninitialized-stack-frame
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);  // 15 seconds.

  int result = -1;  // Pessimistically assume error.

  time_t start_time __attribute__ ((unused)) = cdtime();
  int curl_result = curl_easy_perform(curl);
  if (curl_result != CURLE_OK) {
    ERROR("write_gcm: curl_easy_perform() failed: %s",
            curl_easy_strerror(curl_result));
    goto leave;
  }
  DEBUG("write_gcm: Elapsed time for curl operation was %g seconds.",
        CDTIME_T_TO_DOUBLE(cdtime() - start_time));

  long response_code;
  curl_result = curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
  write_ctx.data[0] = 0;
  if (response_code >= 400) {
    WARNING("write_gcm: Unsuccessful HTTP request %ld: %s",
            response_code, response_buffer);
    result = -2;
    goto leave;
  }

  if (write_ctx.size < 2) {
    ERROR("write_gcm: wg_curl_get_or_post: The receive buffer overflowed.");
    DEBUG("write_gcm: wg_curl_get_or_post: Received data is: %s",
        response_buffer);
    goto leave;
  }

  result = 0;  // Success!

 leave:
  curl_slist_free_all(curl_headers);
  curl_easy_cleanup(curl);
  return result;
}

static size_t wg_curl_write_callback(char *ptr, size_t size, size_t nmemb,
                                     void *userdata) {
  wg_curl_write_ctx_t *ctx = userdata;
  if (ctx->size == 0) {
    return 0;
  }
  size_t requested_bytes = size * nmemb;
  size_t actual_bytes = requested_bytes;
  if (actual_bytes >= ctx->size) {
    actual_bytes = ctx->size - 1;
  }
  memcpy(ctx->data, ptr, actual_bytes);
  ctx->data += actual_bytes;
  ctx->size -= actual_bytes;

  // We lie about the number of bytes successfully transferred in order to
  // prevent curl from returning an error to our caller. Our caller is keeping
  // track of buffer consumption so it will independently know if the buffer
  // filled up; the only errors it wants to hear about from curl are the more
  // catastrophic ones.
  return requested_bytes;
}

//==============================================================================
//==============================================================================
//==============================================================================
// Hacky JSON parsing, suitable for use with libyajl v1 or v2.
// The only JSON parsing we need to do is to pull a string or number field out
// of a top-level JSON object. i.e. no nested arrays or maps. The code is not
// especially efficient, but this does not matter for our purposes.
//==============================================================================
//==============================================================================
//==============================================================================
// Extracts the string value from the top-level json object whose key is given
// by 'key'. Returns 0 on success, <0 on error.
int wg_extract_toplevel_json_string(const char *json, const char *key,
    char **result);

int wg_extract_toplevel_json_long_long(const char *json, const char *key,
    long long *result);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
typedef struct {
  const char *expected_key;
  size_t expected_key_len;
  int map_depth;
  int array_depth;
  _Bool consume_next;
  char *result;
} wg_parse_context_t;

static int wg_handle_null(void *arg) {
  wg_parse_context_t *ctx = (wg_parse_context_t*)arg;
  ctx->consume_next = 0;
  return 1;
}

static int wg_handle_bool(void *arg, int value) {
  wg_parse_context_t *ctx = (wg_parse_context_t*)arg;
  ctx->consume_next = 0;
  return 1;
}

#if defined(YAJL_MAJOR) && YAJL_MAJOR >= 2
typedef size_t wg_yajl_callback_size_t;
#else
typedef unsigned int wg_yajl_callback_size_t;
#endif

static int wg_handle_string(void *arg, const unsigned char *val,
    wg_yajl_callback_size_t length) {
  wg_parse_context_t *ctx = (wg_parse_context_t*)arg;
  if (!ctx->consume_next) {
    // This is not the string we're looking for.
    return 1;
  }
  if (ctx->result != NULL) {
    ERROR("write_gcm: Internal error: already consumed result?");
    return 0;
  }
  ctx->result = smalloc(length + 1);
  if (ctx->result == NULL) {
    ERROR("write_gcm: wg_handle_string: smalloc failed.");
    return 0;
  }
  memcpy(ctx->result, val, length);
  ctx->result[length] = 0;
  ctx->consume_next = 0;
  return 1;
}

static int wg_handle_number(void *arg, const char *data,
    wg_yajl_callback_size_t length) {
  return wg_handle_string(arg, (const unsigned char*)data, length);
}

static int wg_handle_start_map(void *arg) {
  wg_parse_context_t *ctx = (wg_parse_context_t*)arg;
  ++ctx->map_depth;
  ctx->consume_next = 0;
  return 1;
}

static int wg_handle_end_map(void *arg) {
  wg_parse_context_t *ctx = (wg_parse_context_t*)arg;
  --ctx->map_depth;
  ctx->consume_next = 0;
  return 1;
}

static int wg_handle_map_key(void *arg, const unsigned char *data,
    wg_yajl_callback_size_t length) {
  wg_parse_context_t *ctx = (wg_parse_context_t*)arg;
  if (ctx->map_depth == 1 &&
      ctx->array_depth == 0 &&
      length == ctx->expected_key_len &&
      strncmp(ctx->expected_key, (const char*)data, length) == 0) {
    ctx->consume_next = 1;
  } else {
    ctx->consume_next = 0;
  }
  return 1;
}

static int wg_handle_start_array(void *arg) {
  wg_parse_context_t *ctx = (wg_parse_context_t*)arg;
  ++ctx->array_depth;
  ctx->consume_next = 0;
  return 1;
}

static int wg_handle_end_array(void *arg) {
  wg_parse_context_t *ctx = (wg_parse_context_t*)arg;
  --ctx->array_depth;
  ctx->consume_next = 0;
  return 1;
}

char *wg_extract_toplevel_value(const char *json, const char *key) {
  char *result = NULL;  // Pessimistically assume error.
  yajl_callbacks callbacks = {
      .yajl_null = &wg_handle_null,
      .yajl_boolean = &wg_handle_bool,
      .yajl_number = &wg_handle_number,
      .yajl_string = &wg_handle_string,
      .yajl_start_map = &wg_handle_start_map,
      .yajl_map_key = &wg_handle_map_key,
      .yajl_end_map = &wg_handle_end_map,
      .yajl_start_array = &wg_handle_start_array,
      .yajl_end_array = &wg_handle_end_array
  };

  wg_parse_context_t context = {
      .expected_key = key,
      .expected_key_len = strlen(key)
  };
#if defined(YAJL_MAJOR) && YAJL_MAJOR >= 2
  yajl_handle handle = yajl_alloc(&callbacks, NULL, &context);
#else
  yajl_parser_config config = { 0, 1 };
  yajl_handle handle = yajl_alloc(&callbacks, &config, NULL, &context);
#endif
  if (yajl_parse(handle, (const unsigned char*)json, strlen(json))
      != yajl_status_ok) {
    ERROR("write_gcm: wg_extract_toplevel_value: error parsing JSON");
    goto leave;
  }
  int parse_result;
#if defined(YAJL_MAJOR) && YAJL_MAJOR >= 2
  parse_result = yajl_complete_parse(handle);
#else
  parse_result = yajl_parse_complete(handle);
#endif
  if (parse_result != yajl_status_ok) {
    ERROR("write_gcm: wg_extract_toplevel_value: error parsing JSON");
    goto leave;
  }
  if (context.result == NULL) {
    ERROR("write_gcm: wg_extract_toplevel_value failed: key was %s", key);
    goto leave;
  }

  result = context.result;
  context.result = NULL;

 leave:
  sfree(context.result);
  yajl_free(handle);
  return result;
}

int wg_extract_toplevel_json_string(const char *json, const char *key,
    char **result) {
  char *s = wg_extract_toplevel_value(json, key);
  if (s == NULL) {
    ERROR("write_gcm: wg_extract_toplevel_value failed.");
    return -1;
  }
  *result = s;
  return 0;
}

int wg_extract_toplevel_json_long_long(const char *json, const char *key,
    long long *result) {
  char *s = wg_extract_toplevel_value(json, key);
  if (s == NULL) {
    ERROR("write_gcm: wg_extract_toplevel_value failed.");
    return -1;
  }
  if (sscanf(s, "%lld", result) != 1) {
    ERROR("write_gcm: Can't parse '%s' as long long", s);
    sfree(s);
    return -1;
  }
  sfree(s);
  return 0;
}

//==============================================================================
//==============================================================================
//==============================================================================
// OAuth2 submodule.
//
// The main method in this module is wg_oauth2_get_auth_header(). The job of
// this method is to provide an authorization token for use in API calls.
// The value returned is preformatted for the caller's as an HTTP header in the
// following form:
// Authorization: Bearer ${access_token}
//
// There are two approaches the code takes in order to get ${access_token}.
// The easy route is to just ask the metadata server for a token.
// The harder route is to format and sign a request to the OAuth2 server and get
// a token that way.
// Which approach we take depends on the value of 'cred_ctx'. If it is NULL
// (i.e. if there are no user-supplied credentials), then we try the easy route.
// Otherwise we do the harder route.
//
// The reason we don't always do the easy case unconditionally is that the
// metadata server may not always be able to provide an auth token. Since you
// cannot add scopes to an existing VM, some people may want to go the harder
// route instead.
//
// Following is a detailed explanation of the easy route and the harder route.
//
//
// THE EASY ROUTE
//
// Make a GET request to the metadata server at the following URL:
// http://169.254.169.254/computeMetadata/v1beta1/instance/service-accounts/default/token
//
// If our call is successful, the server will respond with a json object looking
// like this:
// {
//  "access_token" : $THE_ACCESS_TOKEN
//  "token_type" : "Bearer",
//  "expires_in" : 3600
// }
//
// We extract $THE_ACCESS_TOKEN from the JSON response then insert it into an
// HTTP header string for the caller's convenience. That header string looks
// like this:
// Authorization: Bearer $THE_ACCESS_TOKEN
//
// We return this string (owned by caller) on success. Upon failure, we return
// NULL.
//
//
// THE HARDER ROUTE
//
// The algorithm used here is described in
// https://developers.google.com/identity/protocols/OAuth2ServiceAccount
// in the section "Preparing to make an authorized API call", under the tab
// "HTTP/Rest".
//
// There is more detail in the documentation, but what it boils down to is this:
//
// Make a POST request to https://www.googleapis.com/oauth2/v3/token
// with the body
// grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer&assertion=$JWT_HEADER.$CLAIM_SET.$SIGNATURE
//
// The trailing part of that body has three variables that need to be expanded.
// Namely, $JWT_HEADER, $CLAIM_SET, and $SIGNATURE, separated by periods.
//
// $JWT_HEADER is the base64url encoding of this constant JSON record:
// {"alg":"RS256","typ":"JWT"}
// Because this header is constant, its base64url encoding is also constant,
// and can be hardcoded as:
// eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9
//
// $CLAIM_SET is a base64url encoding of a JSON object with five fields:
// iss, scope, aud, exp, and iat.
// iss: Service account email. We get this from user in the config file.
// scope: Basically the requested scope (e.g. "permissions") for the token. For
//   our purposes, this is the constant string
//   "https://www.googleapis.com/auth/monitoring".
// aud: Assertion target. Since we are asking for an access token, this is the
//   constant string "https://www.googleapis.com/oauth2/v3/token". This is the
//   same as the URL we are posting to.
// iat: Time of the assertion (i.e. now) in units of "seconds from Unix epoch".
// exp: Expiration of assertion. For us this is 'iat' + 3600 seconds.
//
// $SIGNATURE is the base64url encoding of the signature of the string
// $JWT_HEADER.$CLAIM_SET
// where $JWT_HEADER and $CLAIM_SET are defined as above. Note that they are
// separated by the period character. The signature algorithm used should be
// SHA-256. The private key used to sign the data comes from the user. The
// private key to use is the one associated with the service account email
// address (i.e. the email address specified in the 'iss' field above).
//
// If our call is successful, the result will be the same as indicated above
// in the section entitled "THE EASY ROUTE".
//
//
// EXAMPLE USAGE
//
// char auth_header[256];
// if (wg_oauth2_get_auth_header(auth_header, sizeof(auth_header),
//                               oauth2_ctx, credential_ctx) != 0) {
//   return -1; // error
// }
// do_a_http_post_with(auth_header);
//
//==============================================================================
//==============================================================================
//==============================================================================

// Opaque to callers.
typedef struct oauth2_ctx_s oauth2_ctx_t;

// Either creates a new "Authorization: Bearer XXX" header or returns a cached
// one. Caller owns the returned string. Returns NULL if there is an error.
static int wg_oauth2_get_auth_header(char *result, size_t result_size,
                                     oauth2_ctx_t *ctx,
                                     const credential_ctx_t *cred_ctx);

// Allocate and construct an oauth2_ctx_t.
static oauth2_ctx_t *wg_oauth2_cxt_create();
// Deallocate and destroy an oauth2_ctx_t.
static void wg_oauth2_ctx_destroy(oauth2_ctx_t *);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
struct oauth2_ctx_s {
  pthread_mutex_t mutex;
  cdtime_t token_expire_time;
  char auth_header[256];
};

static int wg_oauth2_get_auth_header_nolock(oauth2_ctx_t *ctx,
    const credential_ctx_t *cred_ctx);

static int wg_oauth2_sign(unsigned char *signature, size_t sig_capacity,
                          unsigned int *actual_sig_size,
                          const char *buffer, size_t size, EVP_PKEY *pkey);

static void wg_oauth2_base64url_encode(char **buffer, size_t *buffer_size,
                                       const unsigned char *source,
                                       size_t source_size);

static int wg_oauth2_parse_result(char **result_buffer, size_t *result_size,
                                  time_t *expires_in, const char *json);

static int wg_oauth2_talk_to_server_and_store_result(oauth2_ctx_t *ctx,
    const char *url, const char *body, const char **headers, int num_headers,
    cdtime_t now);

static int wg_oauth2_get_auth_header(char *result, size_t result_size,
    oauth2_ctx_t *ctx, const credential_ctx_t *cred_ctx) {
  // Do the whole operation under lock so that there are no races with regard
  // to the token, we don't spam the server, etc.
  pthread_mutex_lock(&ctx->mutex);
  int error = wg_oauth2_get_auth_header_nolock(ctx, cred_ctx);
  if (error == 0) {
    sstrncpy(result, ctx->auth_header, result_size);
  }
  pthread_mutex_unlock(&ctx->mutex);
  return error;
}

static int wg_oauth2_get_auth_header_nolock(oauth2_ctx_t *ctx,
    const credential_ctx_t *cred_ctx) {
  // The URL to get the auth token from the metadata server.
  static const char gcp_metadata_fetch_auth_token[] =
    "http://169.254.169.254/computeMetadata/v1beta1/instance/service-accounts/default/token";

  cdtime_t now = cdtime();
  // Try to reuse an existing token. We build in a minute of slack in order to
  // avoid timing problems (clock skew, races, etc).
  if (ctx->token_expire_time > now + TIME_T_TO_CDTIME_T(60)) {
    // Token still valid!
    return 0;
  }
  // Retire the old token.
  ctx->token_expire_time = 0;
  ctx->auth_header[0] = 0;

  // If there are no user-supplied credentials, try to get the token from the
  // metadata server. This is THE EASY ROUTE as described in the documentation
  // for this method.
  const char *headers[] = { gcp_metadata_header };
  if (cred_ctx == NULL) {
    INFO("write_gcm: Asking metadata server for auth token");
    return wg_oauth2_talk_to_server_and_store_result(ctx,
        gcp_metadata_fetch_auth_token, NULL,
        headers, STATIC_ARRAY_SIZE(headers), now);
  }

  // If there are user-supplied credentials, format and sign a request to the
  // OAuth2 server. This is THE HARDER ROUTE as described in the documentation
  // for this submodule. This involves posting a body to a URL. The URL is
  // constant. The body needs to be constructed as described
  // in the comments for this submodule.
  const char *url = "https://www.googleapis.com/oauth2/v3/token";

  char body[2048];  // Should be big enough.
  char *bptr = body;
  size_t bsize = sizeof(body);

  bufprintf(&bptr, &bsize, "%s",
            "grant_type=urn%3Aietf%3Aparams%3Aoauth%3Agrant-type%3Ajwt-bearer"
            "&assertion=");

  // Save a pointer to the start of the jwt_header because we will need to
  // sign $JWT_HEADER.$CLAIM_SET shortly.
  const char *jwt_header_begin = bptr;

  // The body has three variables that need to be filled in: jwt_header,
  // claim_set, and signature.

  // 'jwt_header' is easy. It is the base64url encoding of
  // {"alg":"RS256","typ":"JWT"}
  // which is eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9
  // In addition, we're going to need a . separator shortly, so we add it now.
  bufprintf(&bptr, &bsize, "eyJhbGciOiJSUzI1NiIsInR5cCI6IkpXVCJ9.");

  // Build 'claim_set' and append its base64url encoding.
  {
    char claim_set[1024];
    unsigned long long iat = CDTIME_T_TO_TIME_T(now);
    unsigned long long exp = iat + 3600;  // + 1 hour.

    int result = snprintf(
        claim_set, sizeof(claim_set),
        "{"
        "\"iss\": \"%s\","
        "\"scope\": \"https://www.googleapis.com/auth/monitoring\","
        "\"aud\": \"%s\","
        "\"iat\": %llu,"
        "\"exp\": %llu"
        "}",
        cred_ctx->email,
        url,
        iat,
        exp);
    if (result < 0 || result >= sizeof(claim_set)) {
      ERROR("write_gcm: Error building claim_set.");
      return -1;
    }
    wg_oauth2_base64url_encode(&bptr, &bsize,
                               (unsigned char*)claim_set, result);
  }

  // Sign the bytes in the buffer that are in the range [jtw_header_start, bptr)
  // Referring to the above documentation, this refers to the part of the body
  // consisting of $JWT_HEADER.$CLAIM_SET
  {
    unsigned char signature[1024];
    unsigned int actual_sig_size;
    if (wg_oauth2_sign(signature, sizeof(signature), &actual_sig_size,
                       jwt_header_begin, bptr - jwt_header_begin,
                       cred_ctx->private_key) != 0) {
      ERROR("write_gcm: Can't sign.");
      return -1;
    }

    // Now that we have the signature, append a '.' and the base64url encoding
    // of 'signature' to the buffer.
    bufprintf(&bptr, &bsize, ".");
    wg_oauth2_base64url_encode(&bptr, &bsize, signature, actual_sig_size);
  }

  // Before using the buffer, check for overflow or error.
  if (bsize < 2) {
    ERROR("write_gcm: Buffer overflow or error while building oauth2 body");
    return -1;
  }
  return wg_oauth2_talk_to_server_and_store_result(ctx, url, body, NULL, 0,
      now);
}

static int wg_oauth2_talk_to_server_and_store_result(oauth2_ctx_t *ctx,
    const char *url, const char *body, const char **headers, int num_headers,
    cdtime_t now) {
  char response[2048];
  if (wg_curl_get_or_post(response, sizeof(response), url, body,
      headers, num_headers) != 0) {
    return -1;
  }

  // Fill ctx->auth_header with the string "Authorization: Bearer $TOKEN"
  char *resultp = ctx->auth_header;
  size_t result_size = sizeof(ctx->auth_header);
  bufprintf(&resultp, &result_size, "Authorization: Bearer ");
  time_t expires_in;
  if (wg_oauth2_parse_result(&resultp, &result_size, &expires_in,
                             response) != 0) {
    ERROR("write_gcm: wg_oauth2_parse_result failed");
    return -1;
  }

  if (result_size < 2) {
    ERROR("write_gcm: Error or buffer overflow when building auth_header");
    return -1;
  }
  ctx->token_expire_time = now + TIME_T_TO_CDTIME_T(expires_in);
  return 0;
}

static int wg_oauth2_sign(unsigned char *signature, size_t sig_capacity,
                          unsigned int *actual_sig_size,
                          const char *buffer, size_t size, EVP_PKEY *pkey) {
  if (sig_capacity < EVP_PKEY_size(pkey)) {
    ERROR("write_gcm: signature buffer not big enough.");
    return -1;
  }
  EVP_MD_CTX ctx;
  EVP_SignInit(&ctx, EVP_sha256());

  char err_buf[1024];
  if (EVP_SignUpdate(&ctx, buffer, size) == 0) {
    ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
    ERROR("write_gcm: EVP_SignUpdate failed: %s", err_buf);
    EVP_MD_CTX_cleanup(&ctx);
    return -1;
  }

  if (EVP_SignFinal(&ctx, signature, actual_sig_size, pkey) == 0) {
    ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
    ERROR ("write_gcm: EVP_SignFinal failed: %s", err_buf);
    EVP_MD_CTX_cleanup(&ctx);
    return -1;
  }
  if (EVP_MD_CTX_cleanup(&ctx) == 0) {
    ERR_error_string_n(ERR_get_error(), err_buf, sizeof(err_buf));
    ERROR ("write_gcm: EVP_MD_CTX_cleanup failed: %s", err_buf);
    return -1;
  }
  return 0;
}

static void wg_oauth2_base64url_encode(char **buffer, size_t *buffer_size,
                                       const unsigned char *source,
                                       size_t source_size) {
  const char *codes =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

  size_t i;
  unsigned int code_buffer = 0;
  int code_buffer_size = 0;
  for (i = 0; i < source_size; ++i) {
    code_buffer = (code_buffer << 8) | source[i];  // Add 8 bits to the right.
    code_buffer_size += 8;
    do {
      // Remove six bits from the left (there will never be more than 12).
      unsigned int next_code = (code_buffer >> (code_buffer_size - 6)) & 0x3f;
      code_buffer_size -= 6;
      // This is not fast, but we don't care much about performance here.
      bufprintf(buffer, buffer_size, "%c", codes[next_code]);
    } while (code_buffer_size >= 6);
  }
  // Flush code buffer. Our server does not want the trailing = or == characters
  // normally present in base64 encoding.
  if (code_buffer_size != 0) {
    code_buffer = (code_buffer << 8);
    code_buffer_size += 8;
    unsigned int next_code = (code_buffer >> (code_buffer_size - 6)) & 0x3f;
    bufprintf(buffer, buffer_size, "%c", codes[next_code]);
  }
}

static int wg_oauth2_parse_result(char **result_buffer, size_t *result_size,
                                  time_t *expires_in, const char *json) {
  long long temp;
  if (wg_extract_toplevel_json_long_long(json, "expires_in", &temp) != 0) {
    ERROR("write_gcm: Can't find expires_in in result.");
    return -1;
  }

  char *access_token;
  if (wg_extract_toplevel_json_string(json, "access_token", &access_token)
      != 0) {
    ERROR("write_gcm: Can't find access_token in result.");
    return -1;
  }

  *expires_in = (time_t)temp;
  bufprintf(result_buffer, result_size, "%s", access_token);
  sfree(access_token);
  return 0;
}

static oauth2_ctx_t *wg_oauth2_cxt_create() {
  oauth2_ctx_t *ctx = calloc(1, sizeof(*ctx));
  if (ctx == NULL) {
    ERROR("write_gcm: wg_oauth2_cxt_create: calloc failed.");
    return NULL;
  }
  pthread_mutex_init(&ctx->mutex, NULL);
  return ctx;
}

static void wg_oauth2_ctx_destroy(oauth2_ctx_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  pthread_mutex_destroy(&ctx->mutex);
  sfree(ctx);
}

//==============================================================================
//==============================================================================
//==============================================================================
// Submodule for holding the monitored data while we are waiting to send it
// upstream.
//==============================================================================
//==============================================================================
//==============================================================================
typedef enum {
  wg_typed_value_string, wg_typed_value_numeric, wg_typed_value_bool
} wg_typed_value_type_t;

// Holds data suitable for the google.monitoring.v3.TypedValue proto.
// Field names are always compile-time string constants, so we don't bother
// dynamically allocating them.
typedef struct {
  const char *field_name_static;
  wg_typed_value_type_t value_type;
  // The contents of this field depend on 'value_type':
  // wg_typed_value_string: the string
  // wg_typed_value_numeric: the string representation of the numeric value
  // wg_typed_value_false, wg_typed_value_true: NULL
  char *value_text;
  // If value_type is 'wg_typed_value_bool', this field holds the boolean value.
  _Bool bool_value;
} wg_typed_value_t;

// A type suitable for representing the MetadataEntry proto.
typedef struct wg_metadata_s {
  char *key;
  wg_typed_value_t value;
} wg_metadata_entry_t;

typedef struct {
  char host[DATA_MAX_NAME_LEN];
  char plugin[DATA_MAX_NAME_LEN];
  char plugin_instance[DATA_MAX_NAME_LEN];
  char type[DATA_MAX_NAME_LEN];
  char type_instance[DATA_MAX_NAME_LEN];

  int num_metadata_entries;
  wg_metadata_entry_t *metadata_entries;
} wg_payload_key_t;

// The element type of the 'values' array of wg_payload_t, defined below.
typedef struct {
  char name[DATA_MAX_NAME_LEN];
  int ds_type;
  value_t val;
} wg_payload_value_t;

// Our digested version of the collectd payload, in a form more suitable for
// sending to the upstream server.
typedef struct wg_payload_s {
  struct wg_payload_s *next;

  wg_payload_key_t key;
  cdtime_t start_time;
  cdtime_t end_time;

  int num_values;
  wg_payload_value_t *values;
} wg_payload_t;

// For derivative values (both DERIVE and COUNTER values in collectd), we need
// to remember certain information so that we can both properly adjust the
// 'start_time' field of wg_payload_value_t as well as adjusting the value
// itself. For a given key, the information we keep track of is:
// - start_time
// - baseline_value
// - previous_value
//
// Basically the algorithm is the following:
// For a given key, the first time a value is ever seen, it establishes the
// start time, baseline, and previous value. Furthermore, the value is absorbed
// (not sent upstream).
//
// For subsequent values on that key:
// - If the value is >= the previous value, then adjust value by subtracting
//   baseline, set previous value = this value, and send it upstream.
// - Otherwise (if the value is less than the previous value), reset start time,
//   set baseline to zero, and set previous value to this value. Note that
//   unlike the initial case, this value can be sent upstream (does not need to
//   be absorbed).
typedef struct {
  cdtime_t start_time;
  value_t *baselines;
  value_t *previous;
} wg_deriv_tracker_value_t;

static wg_payload_t *wg_payload_create(const data_set_t *ds,
    const value_list_t *vl);
static void wg_payload_destroy(wg_payload_t *list);

static int wg_typed_value_create_from_value_t_inline(wg_typed_value_t *result,
    int ds_type, value_t value, const char **dataSourceType_static);
static int wg_typed_value_create_from_meta_data_inline(wg_typed_value_t *item,
    meta_data_t *md, const char *key);
static void wg_typed_value_destroy_inline(wg_typed_value_t *item);
static int wg_typed_value_copy(wg_typed_value_t *dest,
    const wg_typed_value_t *src);

static int wg_metadata_entry_create_inline(wg_metadata_entry_t *result,
    meta_data_t *md, const char *key);
static void wg_metadata_entry_destroy_inline(wg_metadata_entry_t *item);
static int wg_metadata_entry_copy(wg_metadata_entry_t *dest,
    const wg_metadata_entry_t *src);

static int wg_payload_key_create_inline(wg_payload_key_t *item,
    const value_list_t *vl);
static void wg_payload_key_destroy_inline(wg_payload_key_t *item);
static wg_payload_key_t *wg_payload_key_clone(const wg_payload_key_t *item);

static void wg_payload_value_create_inline(wg_payload_value_t *result,
    const char *name, int ds_type, value_t value);
static void wg_payload_value_destroy_inline(wg_payload_value_t *item);

static wg_deriv_tracker_value_t *wg_deriv_tracker_value_create(int num_values);
static void wg_deriv_tracker_value_destroy(wg_deriv_tracker_value_t *item);

static c_avl_tree_t *wg_deriv_tree_create();
static void wg_deriv_tree_destroy(c_avl_tree_t *tree);

static int wg_payload_key_compare(const wg_payload_key_t *l,
    const wg_payload_key_t *r);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
static wg_payload_t *wg_payload_create(const data_set_t *ds,
    const value_list_t *vl) {
  // Items to clean up upon exit.
  wg_payload_t *build = NULL;
  wg_payload_t *result = NULL;

  build = calloc(1, sizeof(*build));
  if (build == NULL) {
    ERROR("write_gcm: wg_payload_create: calloc failed");
    goto leave;
  }
  build->next = NULL;
  if (wg_payload_key_create_inline(&build->key, vl) != 0) {
    ERROR("write_gcm: wg_payload_key_create_inline failed");
    goto leave;
  }

  build->start_time = vl->time;
  build->end_time = vl->time;

  build->num_values = vl->values_len;
  build->values = calloc(vl->values_len, sizeof(build->values[0]));
  assert(ds->ds_num == vl->values_len);
  int i;
  for (i = 0; i < ds->ds_num; ++i) {
    data_source_t *src = &ds->ds[i];
    wg_payload_value_create_inline(&build->values[i],
        src->name, src->type, vl->values[i]);
  }

  // Success!
  result = build;
  build = NULL;

 leave:
  wg_payload_destroy(build);
  return result;
}

static void wg_payload_destroy(wg_payload_t *list) {
  while (list != NULL) {
    wg_payload_t *next = list->next;
    int i;
    for (i = 0; i < list->num_values; ++i) {
      wg_payload_value_destroy_inline(&list->values[i]);
    }
    sfree(list->values);
    wg_payload_key_destroy_inline(&list->key);
    sfree(list);
    list = next;
  }
}

// Based on 'ds_type', determine the appropriate value for the corresponding
// CollectdValue.DataSourceType enum (stored here and transmitted in JSON as the
// string 'dataSourceType_static') and also populate the wg_typed_value_t
// structure (which itself corresponds to the proto
// google.monitoring.v3.TypedValue). 'dataSourceType_static' is so named to help
// us remember that it is a compile-time string constant which does not need to
// be copied/deallocated.
static int wg_typed_value_create_from_value_t_inline(wg_typed_value_t *result,
    int ds_type, value_t value, const char **dataSourceType_static) {
  char buffer[128];
  switch (ds_type) {
    case DS_TYPE_GAUGE: {
      if (!isfinite(value.gauge)) {
        ERROR("write_gcm: can not take infinite value");
        return -1;
      }
      *dataSourceType_static = "gauge";
      result->field_name_static = "doubleValue";
      snprintf(buffer, sizeof(buffer), "%f", value.gauge);
      break;
    }
    case DS_TYPE_COUNTER: {
      if (value.counter > INT64_MAX) {
        ERROR("write_gcm: Counter is too large for an int64.");
        return -1;
      }
      *dataSourceType_static = "counter";
      result->field_name_static = "int64Value";
      snprintf(buffer, sizeof(buffer), "%" PRIi64, (int64_t)value.counter);
      break;
    }
    case DS_TYPE_DERIVE: {
      *dataSourceType_static = "derive";
      result->field_name_static = "int64Value";
      snprintf(buffer, sizeof(buffer), "%" PRIi64, value.derive);
      break;
    }
    case DS_TYPE_ABSOLUTE: {
      // TODO: Reject such metrics as they are not supported.
      if (value.absolute > INT64_MAX) {
        ERROR("write_gcm: Absolute is too large for an int64.");
        return -1;
      }
      *dataSourceType_static = "absolute";
      result->field_name_static = "int64Value";
      snprintf(buffer, sizeof(buffer), "%" PRIi64, (int64_t)value.absolute);
      break;
    }
    default:
      ERROR("write_gcm: wg_get_vl_value: Unknown ds_type %i", ds_type);
      return -1;
  }
  result->value_text = sstrdup(buffer);
  result->value_type = wg_typed_value_numeric;
  return 0;
}

static int wg_typed_value_create_from_meta_data_inline(wg_typed_value_t *result,
    meta_data_t *md, const char *key) {
  int type = meta_data_type(md, key);
  char buffer[128];
  switch(type) {
    case MD_TYPE_STRING: {
      result->field_name_static = "stringValue";
      result->value_type = wg_typed_value_string;
      if (meta_data_get_string(md, key, &result->value_text) != 0) {
        return -1;
      }
      // Truncate all metadata entries to 512 characters.
      if (strlen(result->value_text) > 512) {
        result->value_text[512] = '\0';
      }
      return 0;
    }

    case MD_TYPE_SIGNED_INT: {
      result->field_name_static = "int64Value";
      int64_t intValue;
      if (meta_data_get_signed_int(md, key, &intValue) != 0) {
        return -1;
      }
      snprintf(buffer, sizeof(buffer), "%" PRIi64, intValue);
      result->value_type = wg_typed_value_numeric;
      result->value_text = sstrdup(buffer);
      return 0;
    }

    case MD_TYPE_UNSIGNED_INT: {
      // map unsigned to signed.
      result->field_name_static = "int64Value";
      uint64_t uintValue;
      if (meta_data_get_unsigned_int(md, key, &uintValue) != 0) {
        return -1;
      }
      if (uintValue > INT64_MAX) {
        WARNING("write_gcm: metadata uint64 value larger than INT64_MAX.");
        return -1;
      }
      snprintf(buffer, sizeof(buffer), "%" PRIi64, (int64_t)uintValue);
      result->value_type = wg_typed_value_numeric;
      result->value_text = sstrdup(buffer);
      return 0;
    }

    case MD_TYPE_DOUBLE: {
      result->field_name_static = "doubleValue";
      double doubleValue;
      if (meta_data_get_double(md, key, &doubleValue) != 0) {
        return -1;
      }
      snprintf(buffer, sizeof(buffer), "%f", doubleValue);
      result->value_type = wg_typed_value_numeric;
      result->value_text = sstrdup(buffer);
      return 0;
    }

    case MD_TYPE_BOOLEAN: {
      result->field_name_static = "boolValue";
      if (meta_data_get_boolean(md, key, &result->bool_value) != 0) {
        return -1;
      }
      result->value_type = wg_typed_value_bool;
      result->value_text = NULL;
      return 0;
    }

    default: {
      ERROR("write_gcm: Unrecognized meta_data type %d", type);
      return -1;
    }
  }
}

static void wg_typed_value_destroy_inline(wg_typed_value_t *item) {
  sfree(item->value_text);
}

static int wg_typed_value_copy(wg_typed_value_t *dest,
    const wg_typed_value_t *src) {
  dest->field_name_static = src->field_name_static;
  dest->value_type = src->value_type;
  dest->value_text = sstrdup(src->value_text);
  if (dest->value_text == NULL) {
    ERROR("write_gcm: wg_typed_value_copy: sstrdup failed");
    return -1;
  }
  dest->bool_value = src->bool_value;
  return 0;
}

static int wg_typed_value_compare(const wg_typed_value_t *lhs,
    const wg_typed_value_t *rhs) {
  int difference;
  difference = strcmp(lhs->field_name_static, rhs->field_name_static);
  if (difference != 0) return difference;
  difference = (int)lhs->value_type - (int)rhs->value_type;
  if (difference != 0) return difference;
  if (lhs->value_type == wg_typed_value_bool) {
    return (int)lhs->bool_value - (int)rhs->bool_value;
  }
  return strcmp(lhs->value_text, rhs->value_text);
}

static int wg_payload_key_create_inline(wg_payload_key_t *item,
    const value_list_t *vl) {
  // Items to clean up upon exit.
  char **toc = NULL;
  int toc_size = 0;
  int result = -1;  // Pessimistically assume error.

  sstrncpy(item->host, vl->host, sizeof(item->host));
  sstrncpy(item->plugin, vl->plugin, sizeof(item->plugin));
  sstrncpy(item->plugin_instance, vl->plugin_instance,
      sizeof(item->plugin_instance));
  sstrncpy(item->type, vl->type, sizeof(item->type));
  sstrncpy(item->type_instance, vl->type_instance,
      sizeof(item->type_instance));

  if (vl->meta != NULL) {
    toc_size = meta_data_toc(vl->meta, &toc);
    if (toc_size < 0) {
      ERROR("write_gcm: error reading metadata table of contents.");
      goto leave;
    }
    item->num_metadata_entries = toc_size;
    item->metadata_entries = calloc(toc_size,
        sizeof(item->metadata_entries[0]));
    int i;
    for (i = 0; i < toc_size; ++i) {
      if (wg_metadata_entry_create_inline(&item->metadata_entries[i],
          vl->meta, toc[i]) != 0) {
        ERROR("write_gcm: wg_metadata_entry_create_inline failed.");
        goto leave;
      }
    }
  }
  result = 0;  //Success!

 leave:
  if (toc != NULL) {
    strarray_free(toc, toc_size);
  }
  return result;
}

static void wg_payload_key_destroy_inline(wg_payload_key_t *item) {
  int i;
  for (i = 0; i < item->num_metadata_entries; ++i) {
    wg_metadata_entry_destroy_inline(&item->metadata_entries[i]);
  }
  sfree(item->metadata_entries);
}

static wg_payload_key_t *wg_payload_key_clone(const wg_payload_key_t *item) {
  // Items to clean up on exit.
  wg_payload_key_t *build = NULL;
  wg_payload_key_t *result = NULL;

  build = calloc(1, sizeof(*build));
  if (build == NULL) {
    ERROR("write_gcm: wg_payload_key_clone: calloc failed");
    goto leave;
  }

  sstrncpy(build->host, item->host, sizeof(build->host));
  sstrncpy(build->plugin, item->plugin, sizeof(build->plugin));
  sstrncpy(build->plugin_instance, item->plugin_instance,
      sizeof(build->plugin_instance));
  sstrncpy(build->type, item->type, sizeof(build->type));
  sstrncpy(build->type_instance, item->type_instance,
      sizeof(build->type_instance));

  build->num_metadata_entries = item->num_metadata_entries;
  if (build->num_metadata_entries != 0) {
    build->metadata_entries = calloc(build->num_metadata_entries,
        sizeof(build->metadata_entries[0]));
    if (build->metadata_entries == NULL) {
      ERROR("write_gcm: wg_payload_key_clone: 2nd calloc failed");
      goto leave;
    }
    int i;
    for (i = 0; i < build->num_metadata_entries; ++i) {
      if (wg_metadata_entry_copy(&build->metadata_entries[i],
          &item->metadata_entries[i]) != 0) {
        ERROR("write_gcm: wg_metadata_entry_copy failed");
        goto leave;
      }
    }
  }

  // Success!
  result = build;
  build = NULL;

 leave:
  if (build != NULL) {
    wg_payload_key_destroy_inline(build);
    sfree(build);
  }
  return result;
}

static int wg_metadata_entry_create_inline(wg_metadata_entry_t *item,
    meta_data_t *md, const char *key) {
  if (wg_typed_value_create_from_meta_data_inline(&item->value, md, key)
      != 0) {
    return -1;
  }
  item->key = sstrdup(key);
  if (item->key == NULL) {
    ERROR("write_gcm: wg_metadata_entry_create_inline: sstrdup failed");
    return -1;
  }
  return 0;
}

static void wg_metadata_entry_destroy_inline(wg_metadata_entry_t *entry) {
  sfree(entry->key);
  wg_typed_value_destroy_inline(&entry->value);
}

static int wg_metadata_entry_copy(wg_metadata_entry_t *dest,
    const wg_metadata_entry_t *src) {
  dest->key = sstrdup(src->key);
  if (dest->key == NULL) {
    ERROR("write_gcm: wg_metadata_entry_copy: sstrdup failed");
    return -1;
  }
  return wg_typed_value_copy(&dest->value, &src->value);
}

static int wg_metadata_entry_compare(const wg_metadata_entry_t *lhs,
    const wg_metadata_entry_t *rhs) {
  int difference = strcmp(lhs->key, rhs->key);
  if (difference != 0) {
    return difference;
  }
  return wg_typed_value_compare(&lhs->value, &rhs->value);
}

static void wg_payload_value_create_inline(wg_payload_value_t *item,
    const char *name, int ds_type, value_t value) {
  sstrncpy(item->name, name, sizeof(item->name));
  item->ds_type = ds_type;
  item->val = value;
}

static void wg_payload_value_destroy_inline(wg_payload_value_t *item) {
  // Nothing to do :-)
}

static wg_deriv_tracker_value_t *wg_deriv_tracker_value_create(int num_values) {
  // Items to clean up at exit.
  wg_deriv_tracker_value_t *result = NULL;  // Pessimistically assume failure.
  wg_deriv_tracker_value_t *build = NULL;

  build = calloc(1, sizeof(*build));
  if (build == NULL) {
    ERROR("write_gcm: wg_deriv_tracker_value_create: calloc failed");
    goto leave;
  }
  build->baselines = calloc(num_values, sizeof(*build->baselines));
  build->previous = calloc(num_values, sizeof(*build->baselines));
  if (build->baselines == NULL || build->previous == NULL) {
    ERROR("write_gcm: wg_deriv_tracker_value_create: calloc failed");
    goto leave;
  }

  // Success!
  result = build;
  build = NULL;

 leave:
  wg_deriv_tracker_value_destroy(build);
  return result;
}

static void wg_deriv_tracker_value_destroy(wg_deriv_tracker_value_t *value) {
  if (value == NULL) {
    return;
  }
  sfree(value->previous);
  sfree(value->baselines);
  sfree(value);
}

static c_avl_tree_t *wg_been_here_tree_create() {
  return c_avl_create(
      (int (*)(const void*, const void*))&wg_payload_key_compare);
}

static void wg_been_here_tree_destroy(c_avl_tree_t *tree) {
  if (tree == NULL) {
    return;
  }
  void *key;
  void *ignored;
  while (c_avl_pick(tree, &key, &ignored) == 0) {
    wg_payload_key_t *actual_key = (wg_payload_key_t*)key;
    wg_payload_key_destroy_inline(actual_key);
    sfree(actual_key);
    assert(ignored == NULL);
  }
  c_avl_destroy(tree);
}

static c_avl_tree_t *wg_deriv_tree_create() {
  return c_avl_create(
      (int (*)(const void*, const void*))&wg_payload_key_compare);
}

static void wg_deriv_tree_destroy(c_avl_tree_t *tree) {
  if (tree == NULL) {
    return;
  }
  void *key;
  void *value;
  while (c_avl_pick(tree, &key, &value) == 0) {
    wg_payload_key_t *actual_key = (wg_payload_key_t*)key;
    wg_deriv_tracker_value_t *actual_value = (wg_deriv_tracker_value_t *)value;
    wg_payload_key_destroy_inline(actual_key);
    sfree(actual_key);
    wg_deriv_tracker_value_destroy(actual_value);
  }
  c_avl_destroy(tree);
}

static int wg_payload_key_compare(const wg_payload_key_t *l,
    const wg_payload_key_t *r) {
  int difference;
  difference = strcmp(l->host, r->host);
  if (difference != 0) return difference;
  difference = strcmp(l->plugin, r->plugin);
  if (difference != 0) return difference;
  difference = strcmp(l->plugin_instance, r->plugin_instance);
  if (difference != 0) return difference;
  difference = strcmp(l->type, r->type);
  if (difference != 0) return difference;
  difference = strcmp(l->type_instance, r->type_instance);
  if (difference != 0) return difference;

  // The metadata keys are in canonical order, so comparing them is pretty easy.

  // No int32 overflow is possible here, so this is safe.
  difference = l->num_metadata_entries - r->num_metadata_entries;
  if (difference != 0) return difference;

  int i;
  for (i = 0; i < l->num_metadata_entries; ++i) {
    difference = wg_metadata_entry_compare(&l->metadata_entries[i],
        &r->metadata_entries[i]);
    if (difference != 0) return difference;
  }
  return 0;
}

//==============================================================================
//==============================================================================
//==============================================================================
// "Configbuilder" submodule. This holds the info extracted from the config
// file.
//==============================================================================
//==============================================================================
//==============================================================================
typedef struct {
  // "gcp" or "aws".
  // "gcp" expects project_id, instance_id, and zone (or will fetch them from
  // the metadata server.
  // "aws" expects project_id, instance_id, region, and account_id (or will
  // fetch them from the metadata server).
  char *cloud_provider;
  char *project_id;
  char *instance_id;
  char *zone;
  char *region;
  char *account_id;
  char *credentials_json_file;
  char *email;
  char *key_file;
  char *passphrase;
  char *json_log_file;
  char *agent_translation_service_format_string;
  char *custom_metrics_format_string;
  int throttling_low_water_mark;
  int throttling_high_water_mark;
  int throttling_chunk_interval_secs;
  int throttling_purge_interval_secs;
  _Bool pretty_print_json;
} wg_configbuilder_t;

// Builds a wg_configbuilder_t out of a config node.
static wg_configbuilder_t *wg_configbuilder_create(int children_num,
    const oconfig_item_t *children);
static void wg_configbuilder_destroy(wg_configbuilder_t *cb);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
static wg_configbuilder_t *wg_configbuilder_create(int children_num,
    const oconfig_item_t *children) {
  // Items to free on error.
  wg_configbuilder_t *cb = NULL;

  cb = calloc(1, sizeof(*cb));
  if (cb == NULL) {
    ERROR("write_gcm: wg_configbuilder_create. calloc failed.");
    goto error;
  }

  static const char *string_keys[] = {
      "CloudProvider",
      "Project",
      "Instance",
      "Zone",
      "Region",
      "Account",
      "CredentialsJSON",
      "Email",
      "PrivateKeyFile",
      "PrivateKeyPass",
      "JSONLogFile",
      "AgentTranslationServiceFormatString",
      "CustomMetricsDefaultFormatString",
  };
  char **string_locations[] = {
      &cb->cloud_provider,
      &cb->project_id,
      &cb->instance_id,
      &cb->zone,
      &cb->region,
      &cb->account_id,
      &cb->credentials_json_file,
      &cb->email,
      &cb->key_file,
      &cb->passphrase,
      &cb->json_log_file,
      &cb->agent_translation_service_format_string,
      &cb->custom_metrics_format_string,
  };
  static size_t string_limits[] = {  /* -1 means effectively unlimited */
      (size_t) -1,
      MAX_PROJECT_ID_SIZE,
      (size_t) -1,
      (size_t) -1,
      (size_t) -1,
      (size_t) -1,
      (size_t) -1,
      (size_t) -1,
      (size_t) -1,
      (size_t) -1,
      (size_t) -1,
      URL_BUFFER_SIZE - MAX_PROJECT_ID_SIZE,
      URL_BUFFER_SIZE - MAX_PROJECT_ID_SIZE,
  };
  static const char *int_keys[] = {
      "ThrottlingLowWaterMark",
      "ThrottlingHighWaterMark",
      "ThrottlingChunkInterval",
      "ThrottlingPurgeInterval",
  };
  int *int_locations[] = {
      &cb->throttling_low_water_mark,
      &cb->throttling_high_water_mark,
      &cb->throttling_chunk_interval_secs,
      &cb->throttling_purge_interval_secs,
  };
  static const char *bool_keys[] = {
      "PrettyPrintJSON",
  };
  _Bool *bool_locations[] = {
      &cb->pretty_print_json
  };

  assert(STATIC_ARRAY_SIZE(string_keys) == STATIC_ARRAY_SIZE(string_locations));
  assert(STATIC_ARRAY_SIZE(string_keys) == STATIC_ARRAY_SIZE(string_limits));
  assert(STATIC_ARRAY_SIZE(int_keys) == STATIC_ARRAY_SIZE(int_locations));
  assert(STATIC_ARRAY_SIZE(bool_keys) == STATIC_ARRAY_SIZE(bool_locations));

  // Set some defaults.
  cb->throttling_low_water_mark = 150000000;  // 150M
  cb->throttling_high_water_mark = 200000000;  // 200M
  cb->throttling_chunk_interval_secs = 30 * 60;  // 30 minutes
  cb->throttling_purge_interval_secs = 24 * 60 * 60;  // 24 hours

  int parse_errors = 0;
  int c, k;
  for (c = 0; c < children_num; ++c) {
    const oconfig_item_t *child = &children[c];
    for (k = 0; k < STATIC_ARRAY_SIZE(string_keys); ++k) {
      if (strcasecmp(child->key, string_keys[k]) == 0) {
        if (cf_util_get_string(child, string_locations[k]) != 0) {
          ERROR("write_gcm: cf_util_get_string failed for key %s",
                child->key);
          ++parse_errors;
        } else if (strlen(*string_locations[k]) > string_limits[k]) {
          ERROR("write_gcm: key %s cannot be longer than %zu characters",
                child->key, string_limits[k]);
          ++parse_errors;
        }
        break;
      }
    }
    if (k < STATIC_ARRAY_SIZE(string_keys)) {
      // Key matched some string and was either successful or a parse error.
      continue;
    }
    for (k = 0; k < STATIC_ARRAY_SIZE(int_keys); ++k) {
      if (strcasecmp(child->key, int_keys[k]) == 0) {
        if (cf_util_get_int(child, int_locations[k]) != 0) {
          ERROR("write_gcm: cf_util_get_int failed for key %s",
                child->key);
          ++parse_errors;
        }
        break;
      }
    }
    if (k < STATIC_ARRAY_SIZE(int_keys)) {
      // Key matched some int and was either successful or a parse error.
      continue;
    }
    for (k = 0; k < STATIC_ARRAY_SIZE(bool_keys); ++k) {
      if (strcasecmp(child->key, bool_keys[k]) == 0) {
        if (cf_util_get_boolean(child, bool_locations[k]) != 0) {
          ERROR("write_gcm: cf_util_get_boolean failed for key %s",
                child->key);
          ++parse_errors;
        }
        break;
      }
    }
    if (k < STATIC_ARRAY_SIZE(bool_keys)) {
      // Key matched some bool and was either successful or a parse error.
      continue;
    }
    ERROR("write_gcm: Invalid configuration option: %s.", child->key);
    ++parse_errors;
  }

  if (parse_errors > 0) {
    ERROR("write_gcm: There were %d parse errors reading config file.",
          parse_errors);
    goto error;
  }

  // Either all or none of 'email', 'key_file', and 'passphrase' must be set.
  int num_set = 0;
  if (cb->email != NULL) {
    ++num_set;
  }
  if (cb->key_file != NULL) {
    ++num_set;
  }
  if (cb->passphrase != NULL) {
    ++num_set;
  }
  if (num_set != 0 && num_set != 3) {
    ERROR("write_gcm: Error reading configuration. "
        "Either all of Email, PrivateKeyFile, and PrivateKeyPass must be set, "
        "or none of them must be set. However, the provided config file "
        "set %d of them.", num_set);
    goto error;
  }

  // 'email'/'key_file'/'passphrase' should not be set at the same time as
  // 'application_default_credentials_file'.
  if (num_set != 0 && cb->credentials_json_file != NULL) {
    ERROR("write_gcm: Error reading configuration. "
          "It is an error to set both CredentialsJSON and "
          "Email/PrivateKeyFile/PrivateKeyPass.");
  }

  // Success!
  return cb;

 error:
  wg_configbuilder_destroy(cb);
  return NULL;
}

static void wg_configbuilder_destroy(wg_configbuilder_t *cb) {
  if (cb == NULL) {
    return;
  }
  sfree(cb->agent_translation_service_format_string);
  sfree(cb->custom_metrics_format_string);
  sfree(cb->json_log_file);
  sfree(cb->passphrase);
  sfree(cb->key_file);
  sfree(cb->email);
  sfree(cb->account_id);
  sfree(cb->credentials_json_file);
  sfree(cb->region);
  sfree(cb->zone);
  sfree(cb->instance_id);
  sfree(cb->project_id);
  sfree(cb->cloud_provider);
  sfree(cb);
}

//==============================================================================
//==============================================================================
//==============================================================================
// "Monitored resource" submodule.
//==============================================================================
//==============================================================================
//==============================================================================
typedef struct {
  // e.g. "gce_instance" or "aws_instance".
  char *type;

  char *project_id;

  // The labels that are present depend on 'type'.
  // If 'type' is "gce_instance", then typically the following labels will be
  // present:
  // instance_id - the numeric instance id
  // zone - the zone, e.g. us-central1-a
  //
  // If 'type' is "aws_instance", then typically the following labels will be
  // present:
  // account_id - the GCP account id
  // instance_id - the AWS instance id
  // region - the AWS region
  int num_labels;
  char **keys;
  char **values;
} monitored_resource_t;

static monitored_resource_t *wg_monitored_resource_create(
    const wg_configbuilder_t *cb, const char *project_id);
static void wg_monitored_resource_destroy(monitored_resource_t *resource);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
static monitored_resource_t *wg_monitored_resource_create_for_gcp(
    const wg_configbuilder_t *cb, const char *project_id);
static monitored_resource_t *wg_monitored_resource_create_for_aws(
    const wg_configbuilder_t *cb, const char *project_id);

// Fetch 'resource' from the GCP metadata server.
static char *wg_get_from_gcp_metadata_server(const char *resource);

// Fetch 'resource' from the AWS metadata server.
static char *wg_get_from_aws_metadata_server(const char *resource);

// Fetches a resource (defined by the concatenation of 'base' and 'resource')
// from an AWS or GCE metadata server and returns it. Returns NULL upon error.
static char *wg_get_from_metadata_server(const char *base, const char *resource,
    const char **headers, int num_headers);

static char * detect_cloud_provider() {
  char * gcp_hostname = wg_get_from_gcp_metadata_server("instance/hostname");
  if (gcp_hostname != NULL) {
    sfree(gcp_hostname);
    return "gcp";
  }

  char * aws_hostname = wg_get_from_aws_metadata_server("meta-data/hostname");
  if (aws_hostname != NULL) {
    sfree(aws_hostname);
    return "aws";
  }
  ERROR("Unable to contact metadata server to detect cloud provider");
  return NULL;
}

static monitored_resource_t *wg_monitored_resource_create(
    const wg_configbuilder_t *cb, const char *project_id) {
  char *cloud_provider_to_use;
  if (cb->cloud_provider != NULL) {
    cloud_provider_to_use = cb->cloud_provider;
  } else {
    cloud_provider_to_use = detect_cloud_provider();
  }
  if (cloud_provider_to_use == NULL) {
    ERROR("write_gcm: Cloud provider not specified and autodetect failed.");
    return NULL;
  }
  if (strcasecmp(cloud_provider_to_use, "gcp") == 0) {
    return wg_monitored_resource_create_for_gcp(cb, project_id);
  }
  if (strcasecmp(cloud_provider_to_use, "aws") == 0) {
    return wg_monitored_resource_create_for_aws(cb, project_id);
  }
  ERROR("write_gcm: Cloud provider '%s' not recognized.",
        cloud_provider_to_use);
  return NULL;
}

static monitored_resource_t *monitored_resource_create_from_fields(
    const char *type, const char *project_id, ...) {
  monitored_resource_t *result = calloc(1, sizeof(*result));
  if (result == NULL) {
    ERROR("write_gcm: monitored_resource_create_from_fields: calloc failed.");
    return NULL;
  }
  result->type = sstrdup(type);
  result->project_id = sstrdup(project_id);
  // count keys/values
  va_list ap;
  va_start(ap, project_id);
  int num_labels = 0;
  while (1) {
    const char *nextKey = va_arg(ap, const char*);
    if (nextKey == NULL) {
      break;
    }
    const char *nextValue = va_arg(ap, const char*);
    (void)nextValue;  // unused
    ++num_labels;
  }
  va_end(ap);

  result->num_labels = num_labels;
  result->keys = calloc(num_labels, sizeof(result->keys[0]));
  result->values = calloc(num_labels, sizeof(result->values[0]));
  if (result->keys == NULL || result->values == NULL) {
    ERROR("write_gcm: monitored_resource_create_from_fields: calloc failed.");
    goto error;
  }

  va_start(ap, project_id);
  int i;
  for (i = 0; i < num_labels; ++i) {
    const char *nextKey = va_arg(ap, const char*);
    const char *nextValue = va_arg(ap, const char*);
    result->keys[i] = sstrdup(nextKey);
    result->values[i] = sstrdup(nextValue);
    if (result->keys[i] == NULL || result->values[i] == NULL) {
      ERROR("write_gcm: monitored_resource_create_from_fields: calloc failed.");
      va_end(ap);
      goto error;
    }
  }
  va_end(ap);
  return result;

 error:
  wg_monitored_resource_destroy(result);
  return NULL;
}

static void wg_monitored_resource_destroy(monitored_resource_t *resource) {
  if (resource == NULL) {
    return;
  }
  int i;
  if (resource->values != NULL) {
    for (i = 0; i < resource->num_labels; ++i) {
      sfree(resource->values[i]);
    }
    sfree(resource->values);
  }
  if (resource->keys != NULL) {
    for (i = 0; i < resource->num_labels; ++i) {
      sfree(resource->keys[i]);
    }
    sfree(resource->keys);
  }
  sfree(resource->project_id);
  sfree(resource->type);
  sfree(resource);
}

static monitored_resource_t *wg_monitored_resource_create_for_gcp(
    const wg_configbuilder_t *cb,  const char *project_id) {
  // Items to clean up upon leaving.
  monitored_resource_t *result = NULL;
  char *project_id_to_use = sstrdup(project_id);
  char *instance_id_to_use = sstrdup(cb->instance_id);
  char *zone_to_use = sstrdup(cb->zone);

  // For items not specified in the config file, try to get them from the
  // metadata server.
  if (project_id_to_use == NULL) {
    // This gets the string id of the project (not the numeric id).
    project_id_to_use = wg_get_from_gcp_metadata_server("project/project-id");
    if (project_id_to_use == NULL) {
      ERROR("write_gcm: Can't get project ID from GCP metadata server "
          " (and 'Project' not specified in the config file).");
      goto leave;
    }
  }

  if (instance_id_to_use == NULL) {
    // This gets the numeric instance id.
    instance_id_to_use = wg_get_from_gcp_metadata_server("instance/id");
    if (instance_id_to_use == NULL) {
      ERROR("write_gcm: Can't get instance ID from GCP metadata server "
          " (and 'Instance' not specified in the config file).");
      goto leave;
    }
  }

  if (zone_to_use == NULL) {
    // This gets the zone.
    char *verbose_zone =
        wg_get_from_gcp_metadata_server("instance/zone");
    if (verbose_zone == NULL) {
      ERROR("write_gcm: Can't get zone ID from GCP metadata server "
          " (and 'Zone' not specified in the config file).");
      goto leave;
    }
    // The zone comes back as projects/${numeric-id}/zones/${zone}
    // where ${zone} is e.g. us-central1-a

    const char *last_slash = strrchr(verbose_zone, '/');
    if (last_slash == NULL) {
      ERROR("write_gcm: Failed to parse zone.");
      sfree(verbose_zone);
      goto leave;
    }

    zone_to_use = sstrdup(last_slash + 1);
    sfree(verbose_zone);
    if (zone_to_use == NULL) {
      ERROR("write_gcm: wg_monitored_resource_populate_for_gcp: "
          "sstrdup failed.");
      goto leave;
    }
  }

  result = monitored_resource_create_from_fields(
      "gce_instance",
      project_id_to_use,
      /* keys/values */
      "instance_id", instance_id_to_use,
      "zone", zone_to_use,
      NULL);

 leave:
  sfree(zone_to_use);
  sfree(instance_id_to_use);
  sfree(project_id_to_use);
  return result;
}

static monitored_resource_t *wg_monitored_resource_create_for_aws(
    const wg_configbuilder_t *cb, const char *project_id) {
  // Items to clean up upon leaving.
  monitored_resource_t *result = NULL;
  char *project_id_to_use = sstrdup(project_id);
  char *region_to_use = sstrdup(cb->region);
  char *instance_id_to_use = sstrdup(cb->instance_id);
  char *account_id_to_use = sstrdup(cb->account_id);
  char *iid_document = NULL;
  char *aws_region = NULL;

  // GCP project id must be specified in the config file.
  if (project_id_to_use == NULL) {
    ERROR("write_gcm: Project was not specified in the config file.");
    goto leave;
  }

  // If any of these are unspecified, we will have to talk to the AWS identity
  // server.
  if (region_to_use == NULL || instance_id_to_use == NULL ||
      account_id_to_use == NULL) {
    iid_document = wg_get_from_aws_metadata_server(
        "dynamic/instance-identity/document");
    if (iid_document == NULL) {
      ERROR("write_gcm: Can't get dynamic data from metadata server");
      goto leave;
    }
  }

  if (region_to_use == NULL) {
    if (wg_extract_toplevel_json_string(iid_document, "region",
        &aws_region) != 0) {
      ERROR("write_gcm: Can't get region from GCP metadata server "
          " (and 'Region' not specified in the config file).");
      goto leave;
    }
    // The '5' is to hold space for "aws:" plus terminating NUL.
    region_to_use = malloc(strlen(aws_region) + 5);
    if (region_to_use == NULL) {
      ERROR("write_gcm: malloc region_to_use failed.");
      goto leave;
    }
    snprintf(region_to_use, strlen(aws_region) + 5, "aws:%s", aws_region);
  }

  if (instance_id_to_use == NULL) {
    if (wg_extract_toplevel_json_string(iid_document, "instanceId",
        &instance_id_to_use) != 0) {
      ERROR("write_gcm: Can't get instance ID from AWS metadata server "
          " (and 'Instance' not specified in the config file).");
      goto leave;
    }
  }

  if (account_id_to_use == NULL) {
    if (wg_extract_toplevel_json_string(iid_document, "accountId",
        &account_id_to_use) != 0) {
      ERROR("write_gcm: Can't get account ID from AWS metadata server "
          " (and 'Account' not specified in the config file).");
      goto leave;
    }
  }

  result = monitored_resource_create_from_fields(
      "aws_ec2_instance",
      project_id_to_use,
      /* keys/values */
      "region", region_to_use,
      "instance_id", instance_id_to_use,
      "aws_account", account_id_to_use,
      NULL);

 leave:
  sfree(aws_region);
  sfree(iid_document);
  sfree(account_id_to_use);
  sfree(instance_id_to_use);
  sfree(region_to_use);
  sfree(project_id_to_use);
  return result;
}

static char *wg_get_from_gcp_metadata_server(const char *resource) {
  const char *headers[] = { gcp_metadata_header };
  return wg_get_from_metadata_server(
      "http://169.254.169.254/computeMetadata/v1beta1/", resource,
      headers, STATIC_ARRAY_SIZE(headers));
}

static char *wg_get_from_aws_metadata_server(const char *resource) {
  return wg_get_from_metadata_server(
      "http://169.254.169.254/latest/", resource, NULL, 0);
}

static char *wg_get_from_metadata_server(const char *base, const char *resource,
    const char **headers, int num_headers) {
  char url[256];
  int result = snprintf(url, sizeof(url), "%s%s", base, resource);
  if (result < 0 || result >= sizeof(url)) {
    ERROR("write_gcm: buffer overflowed while building url");
    return NULL;
  }

  char buffer[2048];
  if (wg_curl_get_or_post(buffer, sizeof(buffer), url, NULL, headers,
      num_headers) != 0) {
    INFO("write_gcm: wg_get_from_metadata_server failed fetching %s", url);
    return NULL;
  }
  return sstrdup(buffer);
}

//==============================================================================
//==============================================================================
//==============================================================================
// Context submodule. Defines the master wg_context_t object, which holds the
// context for this plugin.
//==============================================================================
//==============================================================================
//==============================================================================
typedef struct {
  pthread_mutex_t mutex;
  // All of the below are guarded by 'mutex'.
  pthread_cond_t cond;
  wg_payload_t *head;
  wg_payload_t *tail;
  size_t size;
  // Set this to 1 to request that the consumer thread do a flush.
  int request_flush;
  // The consumer thread sets this to 1 when the last requested flush is
  // complete and there is no additional outstanding flush request.
  int flush_complete;
  _Bool request_terminate;
  pthread_t consumer_thread;
  _Bool consumer_thread_created;
} wg_queue_t;

typedef struct {
  size_t api_successes;
  size_t api_connectivity_failures;
  size_t api_errors;
} wg_stats_t;

typedef struct {
  _Bool pretty_print_json;
  FILE *json_log_file;
  monitored_resource_t *resource;
  char *agent_translation_service_url;
  char *custom_metrics_url;
  credential_ctx_t *cred_ctx;
  oauth2_ctx_t *oauth2_ctx;
  wg_queue_t *ats_queue;  // Agent translation service (deprecated)
  wg_stats_t *ats_stats;
  wg_queue_t *gsd_queue;  // Google Stackdriver (Custom metrics ingestion)
  wg_stats_t *gsd_stats;
} wg_context_t;

static wg_context_t *wg_context_create(const wg_configbuilder_t *cb);
static void wg_context_destroy(wg_context_t *context);

static wg_queue_t *wg_queue_create();
static void wg_queue_destroy(wg_queue_t *queue);

static wg_stats_t *wg_stats_create();
static void wg_stats_destroy(wg_stats_t *stats);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
static char * find_application_default_creds_path() {
  // first see if there is a file specified by $GOOGLE_APPLICATION_CREDENTIALS
  const char * env_creds_path = getenv("GOOGLE_APPLICATION_CREDENTIALS");
  if (env_creds_path != NULL && access(env_creds_path, R_OK) == 0) {
    return sstrdup(env_creds_path);
  }

  // next check for $HOME/.config/gcloud/application_default_credentials.json
  const char * home_path = getenv("HOME");
  if (home_path != NULL) {
    static char suffix[] = "/.config/gcloud/application_default_credentials.json";
    size_t bytes_needed = strlen(home_path) + sizeof(suffix);
    char *home_config_path = malloc(bytes_needed);
    if (home_config_path == NULL) {
      ERROR("write_gcm: find_application_default_creds_path: malloc failed");
      return NULL;
    }
    int result = snprintf(home_config_path, bytes_needed,
                          "%s%s", home_path, suffix);
    if (result > 0 && access(home_config_path, R_OK) == 0) {
      return home_config_path;
    }
    sfree(home_config_path);
  }

  // finally, check the system default path
  const char * system_default_path =
          "/etc/google/auth/application_default_credentials.json";
  if (access(system_default_path, R_OK) == 0) {
    return sstrdup(system_default_path);
  }

  return NULL;
}

static wg_context_t *wg_context_create(const wg_configbuilder_t *cb) {
  // Items to clean up on exit.
  wg_context_t *build = NULL;
  wg_context_t *result = NULL;
  char * cred_path = NULL;

  build = calloc(1, sizeof(*build));
  if (build == NULL) {
    ERROR("wg_context_create: calloc failed.");
    goto leave;
  }

  // Open the JSON log file if requested.
  if (cb->json_log_file != NULL) {
    build->json_log_file = fopen(cb->json_log_file, "a");
    if (build->json_log_file == NULL) {
      WARNING("write_gcm: Can't open log file %s. errno is %d. Continuing.",
          cb->json_log_file, errno);
    }
  }

  // Optionally create the subcontext holding the service account credentials.
  if (cb->credentials_json_file != NULL) {
    build->cred_ctx = wg_credential_ctx_create_from_json_file(
            cb->credentials_json_file);
    if (build->cred_ctx == NULL) {
      ERROR("write_gcm: wg_credential_ctx_create_from_json_file failed.");
      goto leave;
    }
  }

  if (cb->email != NULL && cb->key_file != NULL && cb->passphrase != NULL) {
    build->cred_ctx = wg_credential_ctx_create_from_p12_file(
            cb->email, cb->key_file, cb->passphrase);
    if (build->cred_ctx == NULL) {
      ERROR("write_gcm: wg_credential_context_create failed.");
      goto leave;
    }
  }

  // We don't have an explicit location for the creds specified. Let's check to
  // see if any of the paths for an application default creds file exists and
  // read that.
  if (build->cred_ctx == NULL) {
    cred_path = find_application_default_creds_path();
    if (cred_path) {
      build->cred_ctx = wg_credential_ctx_create_from_json_file(cred_path);
      if (build->cred_ctx == NULL) {
        ERROR("write_gcm: wg_credential_ctx_create_from_json_file failed to "
              "parse %s", cred_path);
        goto leave;
      }
    }
  }

  // If we got a project id from the credentials, use that one
  const char * project_id;
  if (build->cred_ctx != NULL && build->cred_ctx->project_id != NULL) {
    project_id = build->cred_ctx->project_id;
  } else {
    project_id = cb->project_id;
  }

  // Create the subcontext holding various pieces of server information.
  build->resource = wg_monitored_resource_create(cb, project_id);
  if (build->resource == NULL) {
    ERROR("write_gcm: wg_monitored_resource_create failed.");
    goto leave;
  }

  assert(sizeof(agent_translation_service_default_format_string)
         <= URL_BUFFER_SIZE - MAX_PROJECT_ID_SIZE);
  const char *ats_format_string_to_use =
      cb->agent_translation_service_format_string != NULL ?
          cb->agent_translation_service_format_string :
          agent_translation_service_default_format_string;

  char ats_url[URL_BUFFER_SIZE];
  int sprintf_result = snprintf(ats_url, sizeof(ats_url), ats_format_string_to_use,
      build->resource->project_id);
  if (sprintf_result < 0 || sprintf_result >= sizeof(ats_url)) {
    ERROR("write_gcm: overflowed url buffer");
    goto leave;
  }
  build->agent_translation_service_url = sstrdup(ats_url);

  assert(sizeof(custom_metrics_default_format_string)
         <= URL_BUFFER_SIZE - MAX_PROJECT_ID_SIZE);
  const char *cm_format_string_to_use =
    cb->custom_metrics_format_string != NULL ?
    cb->custom_metrics_format_string :
    custom_metrics_default_format_string;

  char cm_url[URL_BUFFER_SIZE];
  sprintf_result = snprintf(cm_url, sizeof(cm_url), cm_format_string_to_use,
      build->resource->project_id);
  if (sprintf_result < 0 || sprintf_result >= sizeof(cm_url)) {
    ERROR("write_gcm: overflowed url buffer");
    goto leave;
  }
  build->custom_metrics_url = sstrdup(cm_url);

  // Create the subcontext holding the oauth2 state.
  build->oauth2_ctx = wg_oauth2_cxt_create();
  if (build->oauth2_ctx == NULL) {
    ERROR("write_gcm: wg_oauth2_context_create failed.");
    goto leave;
  }

  // Create the queue contexts.
  build->ats_queue = wg_queue_create();
  if (build->ats_queue == NULL) {
    ERROR("write_gcm: wg_queue_create failed.");
    goto leave;
  }
  build->gsd_queue = wg_queue_create();
  if (build->gsd_queue == NULL) {
    ERROR("write_gcm: wg_queue_create failed.");
    goto leave;
  }

  // Create the stats context.
  build->ats_stats = wg_stats_create();
  if (build->ats_stats == NULL) {
    ERROR("%s: wg_stats_create failed.", this_plugin_name);
    goto leave;
  }
  build->gsd_stats = wg_stats_create();
  if (build->gsd_stats == NULL) {
    ERROR("%s: wg_stats_create failed.", this_plugin_name);
    goto leave;
  }

  build->pretty_print_json = cb->pretty_print_json;

  // Success!
  result = build;
  build = NULL;

 leave:
  sfree(cred_path);
  wg_context_destroy(build);
  return result;
}

static void wg_context_destroy(wg_context_t *ctx) {
  if (ctx == NULL) {
    return;
  }
  DEBUG("write_gcm: Tearing down context.");
  wg_queue_destroy(ctx->ats_queue);
  wg_stats_destroy(ctx->ats_stats);
  wg_queue_destroy(ctx->gsd_queue);
  wg_stats_destroy(ctx->gsd_stats);
  wg_oauth2_ctx_destroy(ctx->oauth2_ctx);
  wg_credential_ctx_destroy(ctx->cred_ctx);
  sfree(ctx->agent_translation_service_url);
  sfree(ctx->custom_metrics_url);
  wg_monitored_resource_destroy(ctx->resource);
  if (ctx->json_log_file != NULL) {
    fclose(ctx->json_log_file);
  }
  sfree(ctx);
}

static wg_queue_t *wg_queue_create() {
  wg_queue_t *queue = calloc(1, sizeof(*queue));
  if (queue == NULL) {
    ERROR("wg_queue_create: calloc failed.");
    return NULL;
  }

  // Create the mutex controlling access to the payload list and deriv tree.
  if (pthread_mutex_init(&queue->mutex, NULL) != 0) {
    ERROR("write_gcm: pthread_mutex_init failed: errno %d", errno);
    wg_queue_destroy(queue);
    return NULL;
  }

  if (pthread_cond_init(&queue->cond, NULL) != 0) {
    ERROR("write_gcm: pthread_cond_init failed: errno %d", errno);
    wg_queue_destroy(queue);
    return NULL;
  }

  queue->head = NULL;
  queue->tail = NULL;
  queue->size = 0;
  queue->request_flush = 0;
  queue->flush_complete = 0;
  queue->request_terminate = 0;
  queue->consumer_thread_created = 0;
  return queue;
}

static void wg_queue_destroy(wg_queue_t *queue) {
  if (queue == NULL) {
    return;
  }
  // Tear down consumer thread if necessary.
  pthread_mutex_lock(&queue->mutex);
  _Bool thread_exists = queue->consumer_thread_created;
  if (thread_exists) {
    queue->request_terminate = 1;
    pthread_cond_signal(&queue->cond);
  }
  pthread_mutex_unlock(&queue->mutex);
  if (thread_exists) {
    DEBUG("write_gcm: Waiting for consumer thread to terminate.");
    pthread_join(queue->consumer_thread, NULL);
    DEBUG("write_gcm: Consumer thread has successfully terminated.");
  }

  wg_payload_destroy(queue->head);
  pthread_cond_destroy(&queue->cond);
  pthread_mutex_destroy(&queue->mutex);

  sfree(queue);
}


static wg_stats_t *wg_stats_create() {
  wg_stats_t *stats = calloc(1, sizeof(*stats));
  if (stats == NULL) {
    ERROR("%s: wg_stats_create: calloc failed.", this_plugin_name);
    return NULL;
  }
  return stats;
}

static void wg_stats_destroy(wg_stats_t *stats) {
  sfree(stats);
}

//==============================================================================
//==============================================================================
//==============================================================================
// Build submodule for formatting the CreateCollectdTimeseriesRequest.
//==============================================================================
//==============================================================================
//==============================================================================
typedef struct {
  int error;
  yajl_gen gen;
} json_ctx_t;

// Formats some or all of the data in the payload_list as a
// CreateCollectdTimeseriesRequest.
// JSON_SOFT_TARGET_SIZE is used to signal to this routine to finish things up
// and close out the message. When the message has grown to be of size
// JSON_SOFT_TARGET_SIZE, the method stops adding new items to the
// 'collectdPayloads' part of the JSON message and closes things up. The purpose
// is to try to always make well-formed JSON messages, even if the incoming list
// is large. One consequence of this is that this routine is not guaranteed to
// empty out the list. Callers need to repeatedly call this routine (making
// fresh CreateCollectdTimeseriesRequest requests each time) until the list is
// exhausted. Upon success, the json argument is set to a json string (memory
// owned by caller), and 0 is returned.
static int wg_json_CreateCollectdTimeseriesRequest(_Bool pretty,
    const const monitored_resource_t *monitored_resource,
    const wg_payload_t *head, const wg_payload_t **new_head,
    char **json);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
static void wg_json_MonitoredResource(json_ctx_t *jc,
    const monitored_resource_t *resource);
static void wg_json_CollectdPayloads(json_ctx_t *jc,
    const wg_payload_t *head, const wg_payload_t **new_head);
static void wg_json_MetadataEntries(json_ctx_t *jc,
    wg_metadata_entry_t *metadata_entries, int num_entries);
static void wg_json_CollectdValues(json_ctx_t *jc, const wg_payload_t *element);
static void wg_json_TypedValue(json_ctx_t *jc, const wg_typed_value_t *tv);
static void wg_json_Timestamp(json_ctx_t *jc, cdtime_t time_stamp);

static void wg_json_map_open(json_ctx_t *jc);
static void wg_json_map_close(json_ctx_t *jc);
static void wg_json_array_open(json_ctx_t *jc);
static void wg_json_array_close(json_ctx_t *jc);
static void wg_json_string(json_ctx_t *jc, const char *s);
static void wg_json_number(json_ctx_t *jc, const char *number);
static void wg_json_uint64(json_ctx_t *jc, uint64_t value);
static void wg_json_bool(json_ctx_t *jc, _Bool value);

static json_ctx_t *wg_json_ctx_create(_Bool pretty);
static void wg_json_ctx_destroy(json_ctx_t *jc);

static void wg_json_RFC3339Timestamp(json_ctx_t *jc, cdtime_t time_stamp);

// From google/monitoring/v3/agent_service.proto
// message CreateCollectdTimeSeriesRequest {
//   string name = 5;
//   google.api.MonitoredResource resource = 2;
//   string collectd_version = 3;
//   repeated CollectdPayload collectd_payloads = 4;
// }
static int wg_json_CreateCollectdTimeseriesRequest(_Bool pretty,
    const monitored_resource_t *monitored_resource,
    const wg_payload_t *head, const wg_payload_t **new_head,
    char **json) {
  char name[256];
  int result = snprintf(name, sizeof(name), "project/%s",
      monitored_resource->project_id);
  if (result < 0 || result >= sizeof(name)) {
    ERROR("write_gcm: project_id %s doesn't fit in buffer.",
        monitored_resource->project_id);
    return (-ENOMEM);
  }

  json_ctx_t *jc = wg_json_ctx_create(pretty);
  if (jc == NULL) {
    ERROR("write_gcm: wg_json_ctx_create failed");
    return (-ENOMEM);
  }

  wg_json_map_open(jc);
  wg_json_string(jc, "name");
  wg_json_string(jc, name);

  wg_json_string(jc, "resource");
  wg_json_MonitoredResource(jc, monitored_resource);

  wg_json_string(jc, "collectdVersion");
  wg_json_string(jc, COLLECTD_USERAGENT);

  wg_json_string(jc, "collectdPayloads");
  wg_json_CollectdPayloads(jc, head, new_head);
  wg_json_map_close(jc);

  const unsigned char *buffer_address;
  wg_yajl_callback_size_t buffer_length;
  yajl_gen_get_buf(jc->gen, &buffer_address, &buffer_length);

  char *json_result = malloc(buffer_length + 1);
  if (json_result == NULL) {
    ERROR("write_gcm: malloc failed");
    wg_json_ctx_destroy(jc);
    return (-ENOMEM);
  }

  memcpy(json_result, buffer_address, buffer_length);
  json_result[buffer_length] = 0;
  wg_json_ctx_destroy(jc);

  *json = json_result;
  return 0;
}

// message Metric {
//   string type = 3;
//   map<string, string> labels = 2;
// }
static void wg_json_Metric(json_ctx_t *jc,
                           const wg_payload_t *element) {
  const char *metric_type = NULL;
  for (int i = 0; i < element->key.num_metadata_entries; ++i) {
    wg_metadata_entry_t *entry = &element->key.metadata_entries[i];
    if (strcmp(entry->key, custom_metric_key) == 0) {
      metric_type = entry->value.value_text;
    }
  }

  wg_json_map_open(jc);
  wg_json_string(jc, "type");
  wg_json_string(jc, metric_type);

  wg_json_string(jc, "labels");
  {
    wg_json_map_open(jc);
    for (int i = 0; i < element->key.num_metadata_entries; ++i) {
      wg_metadata_entry_t *entry = &element->key.metadata_entries[i];
      const char *key_pref = custom_metric_label_prefix;
      if (strncmp(entry->key, key_pref, strlen(key_pref)) == 0) {
        wg_json_string(jc, &entry->key[strlen(key_pref)]);
        wg_json_string(jc, entry->value.value_text);
      }
    }
    wg_json_map_close(jc);
  }

  wg_json_map_close(jc);
}

// message Point {
//   message TimeInterval {
//     google.protobuf.Timestamp start_time = 1;
//     google.protobuf.Timestamp end_time = 2;
//   }
//   TimeInterval interval = 1;
//   google.monitoring.v3.TypedValue value = 2;
// }
static void wg_json_Points(json_ctx_t *jc, const wg_payload_t *element) {

  wg_json_array_open(jc);

  assert(element->num_values == 1);
  const wg_payload_value_t *value = &element->values[0];
  assert(!strcmp(value->name, "value"));

  wg_typed_value_t typed_value;
  // We don't care what the name of the type is.
  const char *data_source_type_static;
  if (wg_typed_value_create_from_value_t_inline(&typed_value,
            value->ds_type, value->val, &data_source_type_static) != 0) {
    ERROR("write_gcm: wg_typed_value_create_from_value_t_inline failed for "
      "%s/%s/%s!.",
      element->key.plugin, element->key.type, value->name);
    goto leave;
  }
  wg_json_map_open(jc);

  wg_json_string(jc, "interval");
  {
    wg_json_map_open(jc);
    wg_json_string(jc, "startTime");
    wg_json_RFC3339Timestamp(jc, element->start_time);
    wg_json_string(jc, "endTime");
    wg_json_RFC3339Timestamp(jc, element->end_time);
    wg_json_map_close(jc);
  }

  wg_json_string(jc, "value");
  wg_json_TypedValue(jc, &typed_value);

  wg_json_map_close(jc);

  wg_typed_value_destroy_inline(&typed_value);

  leave:
  wg_json_array_close(jc);
}

// message TimeSeries {
//   google.api.MonitoredResource resource = 2;
//   google.api.Metric metric = 1;
//   google.api.MetricDescriptor.MetricKind metric_kind = 3;
//   google.api.MetricDescriptor.ValueType value_type = 4;
//   repeated Point points = 5;
// }
//
// Returns the number of Timeseries created.
static int wg_json_CreateTimeSeries(
    json_ctx_t *jc, const monitored_resource_t *resource,
    const wg_payload_t *head, const wg_payload_t **new_head) {
  int count = 0;

  wg_json_array_open(jc);

  for (; head != NULL && jc->error == 0; head = head->next, ++count) {
    // Also exit the loop if the message size has reached our target.
    const unsigned char *buffer_address;
    wg_yajl_callback_size_t buffer_length;
    yajl_gen_get_buf(jc->gen, &buffer_address, &buffer_length);
    if (buffer_length >= JSON_SOFT_TARGET_SIZE) {
      break;
    }

    DEBUG("wg_json_CreateTimeSeries: type: %s, typeInstance: %s",
        head->key.type, head->key.type_instance);
    // Validate ahead of time, easily avoid sending a partial timeseries.
    // If the metric doesn't match, we log an error and drop it.
    if (head->num_values != 1) {
      ERROR("write_gcm: plugin: %s, plugin_type: %s, metric_type: %s, "
            "type_instance: %s had more than one data source.",
            head->key.plugin, head->key.plugin_instance, head->key.type,
            head->key.type_instance);
      continue;
    }
    // TODO: Do we need this check?
    if (strcmp(head->values[0].name, "value") != 0) {
      ERROR("write_gcm: plugin: %s, plugin_type: %s, metric_type: %s, "
            "type_instance: %s data source was not called 'value'.",
            head->key.plugin, head->key.plugin_instance, head->key.type,
            head->key.type_instance);
      continue;
    }
    if (head->values[0].ds_type == DS_TYPE_ABSOLUTE) {
      ERROR("write_gcm: plugin: %s, plugin_type: %s, metric_type: %s, "
            "type_instance: %s type cannot be ABSOLUTE.",
            head->key.plugin, head->key.plugin_instance, head->key.type,
            head->key.type_instance);
      continue;
    }
    if (head->values[0].ds_type == DS_TYPE_GAUGE
        && !isfinite(head->values[0].val.gauge)) {
      DEBUG("write_gcm: plugin: %s, plugin_type: %s, metric_type: %s, "
            "type_instance: %s skipping non-finite gauge value %lf.",
            head->key.plugin, head->key.plugin_instance, head->key.type,
            head->key.type_instance, head->values[0].val.gauge);
      continue;
    }

    for (int i = 0; i < head->key.num_metadata_entries; ++i) {
      wg_metadata_entry_t *entry = &head->key.metadata_entries[i];
      if (strcmp(entry->key, custom_metric_key) == 0) {
        if (entry->value.value_type != wg_typed_value_string) {
          ERROR("write_gcm: plugin: %s, plugin_type: %s, metric_type: %s, "
                "type_instance: %s metric type must be string.",
                head->key.plugin, head->key.plugin_instance, head->key.type,
                head->key.type_instance);
          continue;
        }
        const char *pref = custom_metric_prefix;
        if (strncmp(entry->value.value_text, pref, strlen(pref)) != 0) {
          ERROR("write_gcm: plugin: %s, plugin_type: %s, metric_type: %s, "
                "type_instance: %s metric type %s is not a custom metric "
                "(should start with '%s').",
                head->key.plugin, head->key.plugin_instance, head->key.type,
                head->key.type_instance, entry->value.value_text, pref);
          continue;
        }
      }
      const char *key_pref = custom_metric_label_prefix;
      if (strncmp(entry->key, key_pref, strlen(key_pref)) == 0) {
        if (entry->value.value_type != wg_typed_value_string) {
          ERROR("write_gcm: plugin: %s, plugin_type: %s, metric_type: %s, "
                "type_instance: %s metric label %s is not a string.",
                head->key.plugin, head->key.plugin_instance, head->key.type,
                head->key.type_instance, entry->key);
        }
      }
    }

    wg_json_map_open(jc);

    wg_json_string(jc, "resource");
    wg_json_MonitoredResource(jc, resource);

    wg_json_string(jc, "metric");
    wg_json_Metric(jc, head);

    switch (head->values[0].ds_type) {
      case DS_TYPE_GAUGE:
      wg_json_string(jc, "metricKind");
      wg_json_string(jc, "GAUGE");
      wg_json_string(jc, "valueType");
      wg_json_string(jc, "DOUBLE");
      break;

      case DS_TYPE_DERIVE:
      case DS_TYPE_COUNTER:
      wg_json_string(jc, "metricKind");
      wg_json_string(jc, "CUMULATIVE");
      wg_json_string(jc, "valueType");
      wg_json_string(jc, "INT64");
      break;
    }

    wg_json_string(jc, "points");
    wg_json_Points(jc, head);

    wg_json_map_close(jc);
  }

  *new_head = head;

  wg_json_array_close(jc);

  return count;
}

// message CreateTimeSeriesRequest {
//   string name = 3;
//   repeated TimeSeries time_series = 2;
// }
static int wg_json_CreateTimeSeriesRequest(_Bool pretty,
    const monitored_resource_t *monitored_resource,
    const wg_payload_t *head, const wg_payload_t **new_head,
    char **json) {
  char name[256];
  int result = snprintf(name, sizeof(name), "project/%s",
      monitored_resource->project_id);
  if (result < 0 || result >= sizeof(name)) {
    ERROR("write_gcm: project_id %s doesn't fit in buffer.",
        monitored_resource->project_id);
    return (-ENOMEM);
  }

  json_ctx_t *jc = wg_json_ctx_create(pretty);
  if (jc == NULL) {
    ERROR("write_gcm: wg_json_ctx_create failed");
    return (-ENOMEM);
  }

  wg_json_map_open(jc);
  wg_json_string(jc, "timeSeries");
  int count = wg_json_CreateTimeSeries(jc, monitored_resource, head, new_head);
  wg_json_map_close(jc);
  if (count == 0) {  // Empty time series.
    wg_json_ctx_destroy(jc);
    *json = NULL;
    return 0;
  }

  const unsigned char *buffer_address;
  wg_yajl_callback_size_t buffer_length;
  yajl_gen_get_buf(jc->gen, &buffer_address, &buffer_length);

  char *json_result = malloc(buffer_length + 1);
  if (json_result == NULL) {
    ERROR("write_gcm: malloc failed");
    wg_json_ctx_destroy(jc);
    return (-ENOMEM);
  }

  memcpy(json_result, buffer_address, buffer_length);
  json_result[buffer_length] = 0;
  wg_json_ctx_destroy(jc);

  *json = json_result;
  return 0;
}

// From google/api/monitored_resource.proto
// message MonitoredResource {
//   string type = 1;
//   map<string, string> labels = 2;
// }
static void wg_json_MonitoredResource(json_ctx_t *jc,
    const monitored_resource_t *resource) {
  wg_json_map_open(jc);
  // type is hardcoded to "gce_instance" for now.
  wg_json_string(jc, "type");
  wg_json_string(jc, resource->type);

  wg_json_string(jc, "labels");
  {
    wg_json_map_open(jc);
    int i;
    for (i = 0; i < resource->num_labels; ++i) {
      wg_json_string(jc, resource->keys[i]);
      wg_json_string(jc, resource->values[i]);
    }
    wg_json_map_close(jc);
  }
  wg_json_map_close(jc);
}


// Array of CollectdPayload, where...
// message CollectdPayload {
//   repeated CollectdValue values = 1;
//   google.protobuf.Timestamp start_time = 2;
//   google.protobuf.Timestamp end_time = 3;
//   string plugin = 4;
//   string plugin_instance = 5;
//   string type = 6;
//   string type_instance = 7;
//   map<string, google.monitoring.v3.TypedValue> metadata = 8;
// }
static void wg_json_CollectdPayloads(json_ctx_t *jc,
    const wg_payload_t *head, const wg_payload_t **new_head) {
  wg_json_array_open(jc);
  while (head != NULL && jc->error == 0) {
    // Also exit the loop if the message size has reached our target.
    const unsigned char *buffer_address;
    wg_yajl_callback_size_t buffer_length;
    yajl_gen_get_buf(jc->gen, &buffer_address, &buffer_length);
    if (buffer_length >= JSON_SOFT_TARGET_SIZE) {
      break;
    }

    wg_json_map_open(jc);
    wg_json_string(jc, "startTime");
    wg_json_Timestamp(jc, head->start_time);

    wg_json_string(jc, "endTime");
    wg_json_Timestamp(jc, head->end_time);

    wg_json_string(jc, "plugin");
    wg_json_string(jc, head->key.plugin);

    wg_json_string(jc, "pluginInstance");
    wg_json_string(jc, head->key.plugin_instance);

    wg_json_string(jc, "type");
    wg_json_string(jc, head->key.type);

    wg_json_string(jc, "typeInstance");
    wg_json_string(jc, head->key.type_instance);

    wg_json_string(jc, "values");
    wg_json_CollectdValues(jc, head);

    // Optimization: omit the metadata entry altogether if it's empty.
    if (head->key.num_metadata_entries != 0) {
      wg_json_string(jc, "metadata");
      wg_json_MetadataEntries(jc, head->key.metadata_entries,
          head->key.num_metadata_entries);
    }
    wg_json_map_close(jc);

    head = head->next;
  }

  *new_head = head;

  wg_json_array_close(jc);
}

static void wg_json_MetadataEntries(json_ctx_t *jc,
    wg_metadata_entry_t *metadata_entries, int num_entries) {
  wg_json_map_open(jc);
  int i;
  for (i = 0; i < num_entries; ++i) {
    wg_metadata_entry_t *entry = &metadata_entries[i];
    wg_json_string(jc, entry->key);
    wg_json_TypedValue(jc, &entry->value);
  }
  wg_json_map_close(jc);
}

// Array of CollectdValue:
// message CollectdValue {
//   string data_source_name = 1;
//   enum DataSourceType {
//     UNSPECIFIED_DATA_SOURCE_TYPE = 0;
//     GAUGE = 1;
//     COUNTER = 2;
//     DERIVE = 3;
//     ABSOLUTE = 4;
//   }
//   DataSourceType data_source_type = 2;
//   google.monitoring.v3.TypedValue value = 3;
// }
static void wg_json_CollectdValues(json_ctx_t *jc,
    const wg_payload_t *element) {
  wg_json_array_open(jc);
  int i;
  for (i = 0; i < element->num_values; ++i) {
    const wg_payload_value_t *value = &element->values[i];

    wg_typed_value_t typed_value;
    const char *data_source_type_static;
    if (wg_typed_value_create_from_value_t_inline(&typed_value,
        value->ds_type, value->val, &data_source_type_static) != 0) {
      WARNING("write_gcm: wg_typed_value_create_from_value_t_inline failed for "
          "%s/%s/%s! Continuing.",
          element->key.plugin, element->key.type, value->name);
      continue;
    }
    wg_json_map_open(jc);
    wg_json_string(jc, "dataSourceType");
    wg_json_string(jc, data_source_type_static);

    wg_json_string(jc, "dataSourceName");
    wg_json_string(jc, value->name);

    wg_json_string(jc, "value");
    wg_json_TypedValue(jc, &typed_value);
    wg_json_map_close(jc);

    wg_typed_value_destroy_inline(&typed_value);
  }
  wg_json_array_close(jc);
}

// google.monitoring.v3.TypedValue:
// message TypedValue {
//   oneof value {
//     bool bool_value = 1;
//     int64 int64_value = 2;
//     double double_value = 3;
//     string string_value = 4 [enforce_utf8 = false];
//     Distribution distribution_value = 5;
//   }
// }
static void wg_json_TypedValue(json_ctx_t *jc, const wg_typed_value_t *tv) {
  wg_json_map_open(jc);
  wg_json_string(jc, tv->field_name_static);
  switch (tv->value_type) {
    case wg_typed_value_string: {
      wg_json_string(jc, tv->value_text);
      break;
    }
    case wg_typed_value_numeric: {
      wg_json_number(jc, tv->value_text);
      break;
    }
    case wg_typed_value_bool: {
      wg_json_bool(jc, tv->bool_value);
      break;
    }
    default: {
      assert(0);
    }
  }
  wg_json_map_close(jc);
}

static void wg_json_RFC3339Timestamp(json_ctx_t *jc, cdtime_t time_stamp) {
  char time_str[RFC3339NANO_SIZE];
  int status = rfc3339nano(time_str, sizeof(time_str), time_stamp);
  if (status != 0) {
    ERROR("Failed to encode time.");
    return;
  }
  wg_json_string(jc, time_str);
}

//message Timestamp {
//  int64 seconds = 1;
//  int32 nanos = 2;
//}
static void wg_json_Timestamp(json_ctx_t *jc, cdtime_t time_stamp) {
  uint64_t sec = CDTIME_T_TO_TIME_T(time_stamp);
  uint64_t ns = CDTIME_T_TO_NS(time_stamp % 1073741824);
  wg_json_map_open(jc);
  wg_json_string(jc, "seconds");
  wg_json_uint64(jc, sec);
  wg_json_string(jc, "nanos");
  wg_json_uint64(jc, ns);
  wg_json_map_close(jc);
}

static void wg_json_map_open(json_ctx_t *jc) {
  if (jc->error != 0) {
    return;
  }
  int result = yajl_gen_map_open(jc->gen);
  if (result != yajl_gen_status_ok) {
    ERROR("yajl_gen_map_open returned %d", result);
    jc->error = -1;
  }
}

static void wg_json_map_close(json_ctx_t *jc) {
  if (jc->error != 0) {
    return;
  }
  int result = yajl_gen_map_close(jc->gen);
  if (result != yajl_gen_status_ok) {
    ERROR("wg_json_map_close returned %d", result);
    jc->error = -1;
  }
}

static void wg_json_array_open(json_ctx_t *jc) {
  if (jc->error != 0) {
    return;
  }
  int result = yajl_gen_array_open(jc->gen);
  if (result != yajl_gen_status_ok) {
    ERROR("wg_json_array_open returned %d", result);
    jc->error = -1;
  }
}

static void wg_json_array_close(json_ctx_t *jc) {
  if (jc->error != 0) {
    return;
  }
  int result = yajl_gen_array_close(jc->gen);
  if (result != yajl_gen_status_ok) {
    ERROR("wg_json_array_close returned %d", result);
    jc->error = -1;
  }
}

static void wg_json_string(json_ctx_t *jc, const char *s) {
  if (jc->error != 0) {
    return;
  }
  if (s == NULL) {
    ERROR("write_gcm: wg_json_string passed NULL.");
    jc->error = -1;
    return;
  }

  int result = yajl_gen_string(jc->gen, (const unsigned char*)s, strlen(s));
  if (result != yajl_gen_status_ok) {
    ERROR("yajl_gen_string returned %d", result);
    jc->error = -1;
  }
}

static void wg_json_number(json_ctx_t *jc, const char *number) {
  if (jc->error != 0) {
    return;
  }
  if (number == NULL) {
    ERROR("write_gcm: wg_json_number passed NULL.");
    jc->error = -1;
    return;
  }

  int result = yajl_gen_number(jc->gen, number, strlen(number));
  if (result != yajl_gen_status_ok) {
    ERROR("yajl_gen_number returned %d", result);
    jc->error = -1;
  }
}

static void wg_json_uint64(json_ctx_t *jc, uint64_t value) {
  char buffer[32];
  snprintf(buffer, sizeof(buffer), "%" PRIu64, value);
  wg_json_number(jc, buffer);
}

static void wg_json_bool(json_ctx_t *jc, _Bool value) {
  if (jc->error != 0) {
    return;
  }
  int result = yajl_gen_bool(jc->gen, value);
  if (result != yajl_gen_status_ok) {
    ERROR("wg_json_bool returned %d", result);
    jc->error = -1;
  }
}

static json_ctx_t *wg_json_ctx_create(_Bool pretty) {
  json_ctx_t *jc = calloc(1, sizeof(*jc));
  if (jc == NULL) {
    ERROR("write_gcm: can't allocate jcon_ctx_t");
    return NULL;
  }
  jc->error = 0;
#if defined(YAJL_MAJOR) && YAJL_MAJOR >= 2
  jc->gen = yajl_gen_alloc(NULL);
  yajl_gen_config(jc->gen, yajl_gen_beautify, pretty);
  yajl_gen_config(jc->gen, yajl_gen_validate_utf8, 1);
#else
  yajl_gen_config config = { pretty, "  " };
  jc->gen = yajl_gen_alloc(&config, NULL);
#endif
  return jc;
}

static void wg_json_ctx_destroy(json_ctx_t *jc) {
  if (jc == NULL) {
    return;
  }
  yajl_gen_free(jc->gen);
  sfree(jc);
}

//==============================================================================
//==============================================================================
//==============================================================================
// The queue processor. A separate thread that consumes the items in the queue.
//==============================================================================
//==============================================================================
//==============================================================================
static void *wg_process_queue(wg_context_t *arg, wg_queue_t *queue,
                              wg_stats_t *stats);

//------------------------------------------------------------------------------
// Private implementation starts here.
//------------------------------------------------------------------------------
// Gets an "event" from the queue, where an event is composed of:
// - A linked list of payloads to process, and
// - A flag indicating whether the caller wants the processing thread to
//   terminate.
// Returns 0 on success, <0 on error.
static int wait_next_queue_event(wg_queue_t *queue, cdtime_t last_flush_time,
    _Bool *want_terminate, wg_payload_t **payloads);


// Update various stats and store them in the cache, to be picked up by the
// stackdriver_agent plugin.
static int wg_update_stats(const wg_stats_t *stats);

// "Rebases" derivative items in the list against their stored values. If this
// is the first time we've seen a derivative item, store it in the map and
// remove it from the list. Otherwise (if it is not the first time we've seen
// a derivative item), adjust its value and start_time based on what we've
// stored in the map value. Finally, if it is not a derivative item, leave it
// be. Modifies the list in place.
static int wg_rebase_cumulative_values(c_avl_tree_t *deriv_tree,
    wg_payload_t **list);

// If the item is not a derivative item, set *keep to 1 and return. Otherwise,
// if this is the first time we have seen it, set *keep to 0 and make a new
// entry in the deriv_tree. Otherwise, set *keep to 1 and adjust the item by the
// offset in the deriv_tree. Returns 0 on success, <0 on error.
static int wg_rebase_item(c_avl_tree_t *deriv_tree, wg_payload_t *payload,
    _Bool *keep);

// Transmit the items in the list to the upstream server by first breaking them
// up into segments, where all the items in the segments have distinct keys.
// This is necessary because the upstream server rejects submissions with
// duplicate keys/labels. (why?) Returns 0 on success, <0 on error.
// Takes ownership of 'list'.
static int wg_transmit_unique_segments(const wg_context_t *ctx,
    wg_queue_t *queue, wg_payload_t *list);

// Transmit a segment of the list, where it is guaranteed that all the items
// in the list have distinct keys. Returns 0 on success, <0 on error.
static int wg_transmit_unique_segment(const wg_context_t *ctx,
    wg_queue_t *queue, const wg_payload_t *list);

// Extracts as many distinct payloads as possible from the list, where the
// notion of "distinct" is as defined by wg_payload_key_compare. Creates two
// lists: the distinct payloads and the residual payloads. Relative ordering
// (within those two lists) is preserved; that is if A came before B in the
// original list, and if A and B are both in the distinct list (or both in the
// residual list) then A will be before B in the distinct (or residual) list.
// However, in a global sense reordering will be happening, as all the residual
// items will be considered "after" all the distinct items.
// The caller is expected to transmit the distinct payloads, then to call this
// method again with the residual payloads as input, repeating until there are
// no more residual payloads. The original list should be considered to be
// destroyed, and every payload that was on the original list will be either
// in the distinct list or in the residual list. This invariant holds even in
// the case of an error return. Returns 0 on successs, <0 on error.
// 'distinct_size' is for the purposes of debugging.
static int wg_extract_distinct_payloads(wg_payload_t *src,
    wg_payload_t **distinct_head, wg_payload_t **residual_head,
    int *distinct_size);

// Converts the data in the list into a CollectdTimeseriesRequest
// message (formatted in JSON format). If successful, sets *json to point to
// the resultant buffer (owned by caller), sets *new_list, and returns 0.
// Otherwise, returns <0. If successful, it is guaranteed that at least one
// element of *list has been processed. It is intended that the caller calls
// this method repeatedly until the list has been completely processsed.
// Returns 0 on success, <0 on error.
static int wg_format_some_of_list_ctr(
    const monitored_resource_t *monitored_resource, const wg_payload_t *list,
    const wg_payload_t **new_list, char **json, _Bool pretty);

static int wg_format_some_of_list_custom(
    const monitored_resource_t *monitored_resource, const wg_payload_t *list,
    const wg_payload_t **new_list, char **json, _Bool pretty);

// Look up an existing, or create a new, deriv_tracker_value_t in the tree.
// The key is derived from the payload. *created is set to 0 if the tracker was
// found in the tree; 1 if it was newly-created. Returns 0 on success, <0 on
// error.
static int wg_lookup_or_create_tracker_value(c_avl_tree_t *tree,
    const wg_payload_t *payload, wg_deriv_tracker_value_t **tracker,
    _Bool *created);

static void *wg_process_queue(wg_context_t *ctx, wg_queue_t *queue,
                              wg_stats_t *stats) {

  // Keeping track of the base values for derivative values.
  c_avl_tree_t *deriv_tree = wg_deriv_tree_create();
  if (deriv_tree == NULL) {
    ERROR("write_gcm: wg_deriv_tree_create failed");
    goto leave;
  }

  cdtime_t last_flush_time = cdtime();
  _Bool want_terminate = 0;
  while (!want_terminate) {
    wg_payload_t *payloads;
    if (wait_next_queue_event(queue, last_flush_time, &want_terminate,
        &payloads) != 0) {
      // Fatal.
      ERROR("write_gcm: wait_next_queue_event failed.");
      break;
    }
    last_flush_time = cdtime();
    if (wg_rebase_cumulative_values(deriv_tree, &payloads) != 0) {
      // Also fatal.
      ERROR("write_gcm: wg_rebase_cumulative_values failed.");
      wg_payload_destroy(payloads);
      break;
    }
    if (wg_transmit_unique_segments(ctx, queue, payloads) != 0) {
      // Not fatal. Connectivity problems? Server went away for a while?
      // Just drop the payloads on the floor and make a note of it.
      wg_some_error_occured_g = 1;
      WARNING("write_gcm: wg_transmit_unique_segments failed. Flushing.");
    }
    if (wg_update_stats(stats) != 0) {
      wg_some_error_occured_g = 1;
      WARNING("%s: wg_update_stats failed.", this_plugin_name);
      break;
    }
    payloads = NULL;
  }

 leave:
  wg_deriv_tree_destroy(deriv_tree);
  WARNING("write_gcm: Consumer thread is exiting.");
  return NULL;
}

static void *wg_process_ats_queue(void *arg) {
  wg_context_t *ctx = arg;
  wg_queue_t *queue = ctx->ats_queue;
  wg_stats_t *stats = ctx->ats_stats;
  return wg_process_queue(ctx, queue, stats);
}

static void *wg_process_gsd_queue(void *arg) {
  wg_context_t *ctx = arg;
  wg_queue_t *queue = ctx->gsd_queue;
  wg_stats_t *stats = ctx->gsd_stats;
  return wg_process_queue(ctx, queue, stats);
}

static int wg_rebase_cumulative_values(c_avl_tree_t *deriv_tree,
    wg_payload_t **list) {
  wg_payload_t *new_head = NULL;
  wg_payload_t *new_tail = NULL;
  wg_payload_t *item = *list;
  int some_error_occurred = 0;
  while (item != NULL) {
    wg_payload_t *next = item->next;
    item->next = NULL;  // Detach from the list.

    _Bool keep;
    if (wg_rebase_item(deriv_tree, item, &keep) != 0) {
      ERROR("write_gcm: wg_rebase_item failed.");
      // Finish processing the list (so that we can properly free the list's
      // memory), but remember that an error occurred.
      some_error_occurred = 1;
      keep = 0;
    }

    if (keep) {
      if (new_head == NULL) {
        new_head = item;
        new_tail = item;
      } else {
        new_tail->next = item;
        new_tail = item;
      }
    } else {
      wg_payload_destroy(item);
    }
    item = next;
  }
  *list = new_head;
  return some_error_occurred ? -1 : 0;
}

static int wg_rebase_item(c_avl_tree_t *deriv_tree, wg_payload_t *payload,
    _Bool *keep) {
  // Our system assumes that the values in the list are homogeneous: i.e. if
  // one value is a DERIVED (or COUNTER) then all the values in that list are
  // DERIVED (or COUNTER).
  int derived_count = 0;
  int counter_count = 0;
  int i;
  for (i = 0; i < payload->num_values; ++i) {
    int ds_type = payload->values[i].ds_type;
    if (ds_type == DS_TYPE_DERIVE) {
      ++derived_count;
    } else if (ds_type == DS_TYPE_COUNTER) {
      ++counter_count;
    }
  }
  if (derived_count == 0 && counter_count == 0) {
    *keep = 1;
    return 0;  // No DERIVED or COUNTER values, so nothing to do here.
  }
  // We know there's at least one DERIVED or COUNTER. Check that either (all the
  // items are DERIVED) or (all the items are COUNTER).
  if (derived_count != payload->num_values &&
      counter_count != payload->num_values) {
    ERROR("write_gcm: wg_rebase_cumulative_values: values must not have diverse"
        " types.");
    return -1;
  }

  // Get the appropriate tracker for this payload.
  wg_deriv_tracker_value_t *tracker;
  _Bool created;
  if (wg_lookup_or_create_tracker_value(deriv_tree, payload, &tracker, &created)
      != 0) {
    ERROR("write_gcm: wg_lookup_or_create_tracker_value failed.");
    return -1;
  }

  if (created) {
    // Establish the baseline.
    tracker->start_time = payload->start_time;
    int i;
    for (i = 0; i < payload->num_values; ++i) {
      tracker->baselines[i] = payload->values[i].val;
      tracker->previous[i] = payload->values[i].val;
    }
    // Having established the baseline, indicate to the caller not to add this
    // to the output list.
    *keep = 0;
    return 0;
  }

  // The list is nonempty and homogeneous, so taking the type of the first
  // element is sufficient.
  int ds_type = payload->values[0].ds_type;

  // If any of the counters have wrapped, then we need to reset the tracker
  // baseline and start_time.
  int some_counter_wrapped = 0;
  for (i = 0; i < payload->num_values; ++i) {
    if (wg_value_less(ds_type, &payload->values[i].val,
        &tracker->previous[i])) {
      some_counter_wrapped = 1;
      break;
    }
  }

  // If any counter wrapped, everybody resets.
  if (some_counter_wrapped) {
    tracker->start_time = payload->start_time;
    for (i = 0; i < payload->num_values; ++i) {
      wg_value_set_zero(ds_type, &tracker->baselines[i]);
    }
  }

  // Update the start_time according to the tracker, adjust the value according
  // to the baseline, and remember the previous value.
  payload->start_time = tracker->start_time;
  for (i = 0; i < payload->num_values; ++i) {
    wg_payload_value_t *v = &payload->values[i];
    tracker->previous[i] = v->val;
    // val -= baseline
    wg_value_subtract(ds_type, &v->val, &v->val, &tracker->baselines[i]);
  }
  *keep = 1;
  return 0;
}

static int wg_transmit_unique_segments(const wg_context_t *ctx,
    wg_queue_t *queue, wg_payload_t *list) {
  while (list != NULL) {
    wg_payload_t *distinct_list;
    wg_payload_t *residual_list;
    int distinct_size;
    if (wg_extract_distinct_payloads(list, &distinct_list, &residual_list,
        &distinct_size) != 0) {
      ERROR("write_gcm: wg_extract_distinct_payloads failed");
      wg_payload_destroy(distinct_list);
      wg_payload_destroy(residual_list);
      return -1;
    }
    DEBUG("write_gcm: next distinct segment has size %d", distinct_size);
    int result = wg_transmit_unique_segment(ctx, queue, distinct_list);
    if (result != 0) {
      ERROR("write_gcm: wg_transmit_unique_segment failed.");
      wg_payload_destroy(distinct_list);
      wg_payload_destroy(residual_list);
      return -1;
    }
    wg_payload_destroy(distinct_list);
    list = residual_list;
  }
  return 0;
}

static void wg_log_json_message(const wg_context_t *ctx, const char *fmt, ...) {
  if (ctx->json_log_file == NULL) {
    return;
  }
  va_list ap;
  va_start(ap, fmt);
  vfprintf(ctx->json_log_file, fmt, ap);
  va_end(ap);
  fflush(ctx->json_log_file);
}

static int wg_transmit_unique_segment(const wg_context_t *ctx,
    wg_queue_t *queue, const wg_payload_t *list) {
  if (list == NULL) {
    return 0;
  }

  // Variables to clean up at the end.
  char *json = NULL;
  int result = -1;  // Pessimistically assume failure.

  char auth_header[256];
  if (wg_oauth2_get_auth_header(auth_header, sizeof(auth_header),
      ctx->oauth2_ctx, ctx->cred_ctx) != 0) {
    ERROR("write_gcm: wg_oauth2_get_auth_header failed.");
    goto leave;
  }

  while (list != NULL) {
    // We can spend a lot of time here talking to the server. If the producer
    // thread wants to shut us down, check for this explicitly and bail out
    // early.
    pthread_mutex_lock(&queue->mutex);
    int want_terminate = queue->request_terminate;
    pthread_mutex_unlock(&queue->mutex);
    if (want_terminate) {
      ERROR("write_gcm: wg_transmit_unique_segment: "
          "Exiting early due to termination request.");
      goto leave;
    }

    // By the way, a successful response is an empty JSON record (i.e. "{}").
    // An unsuccessful response is a detailed error message from the API.
    char response[2048];
    const char *headers[] = { auth_header, json_content_type_header };

    // Leave the remainder here to send in a new request next loop iteration.
    const wg_payload_t *new_list;

    if (queue == ctx->ats_queue) {

      if (wg_format_some_of_list_ctr(ctx->resource, list, &new_list, &json,
          ctx->pretty_print_json) != 0) {
        ERROR("write_gcm: Error formatting list as JSON");
        goto leave;
      }

      wg_log_json_message(
          ctx, "Sending JSON (CollectdTimeseriesRequest):\n%s\n", json);

      int wg_result = wg_curl_get_or_post(response, sizeof(response),
        ctx->agent_translation_service_url, json,
        headers, STATIC_ARRAY_SIZE(headers));
      if (wg_result != 0) {
        wg_log_json_message(ctx, "Error %d from wg_curl_get_or_post\n",
                            wg_result);
        ERROR("%s: Error %d from wg_curl_get_or_post",
              this_plugin_name, wg_result);
        if (wg_result == -1) {
          ++ctx->ats_stats->api_connectivity_failures;
        } else {
          ++ctx->ats_stats->api_errors;
        }
        goto leave;
      }

      wg_log_json_message(
          ctx, "Server response (CollectdTimeseriesRequest):\n%s\n", response);
      // Since the response is expected to be valid JSON, we don't
      // look at the characters beyond the closing brace.
      if (strncmp(response, "{}", 2) != 0) {
        ++ctx->ats_stats->api_errors;
        goto leave;
      }
      ++ctx->ats_stats->api_successes;

    } else {

      assert(queue == ctx->gsd_queue);

      if (wg_format_some_of_list_custom(ctx->resource, list, &new_list, &json,
          ctx->pretty_print_json) != 0) {
        ERROR("write_gcm: Error formatting list as CreateTimeSeries request");
        goto leave;
      }

      if (json != NULL) {
        wg_log_json_message(
            ctx, "Sending JSON (TimeseriesRequest) to %s:\n%s\n",
            ctx->custom_metrics_url, json);

        if (wg_curl_get_or_post(response, sizeof(response),
            ctx->custom_metrics_url, json,
            headers, STATIC_ARRAY_SIZE(headers)) != 0) {
          wg_log_json_message(ctx, "Error contacting server.\n");
          ERROR("write_gcm: Error talking to the endpoint.");
          ++ctx->gsd_stats->api_connectivity_failures;
          goto leave;
        }

        // TODO: Validate API response properly.
        wg_log_json_message(
            ctx, "Server response (TimeseriesRequest):\n%s\n", response);
        // Since the response is expected to be valid JSON, we don't
        // look at the characters beyond the closing brace.
        if (strncmp(response, "{}", 2) != 0) {
          ERROR("%s: Expected non-empty JSON response: %s",
                this_plugin_name, response);
          ++ctx->gsd_stats->api_errors;
          goto leave;
        }
      } else {
        wg_log_json_message(
            ctx, "Not sending an empty CreateTimeSeries request.\n");
      }
      ++ctx->gsd_stats->api_successes;

    }

    sfree(json);
    json = NULL;

    list = new_list;
  }

  result = 0;

 leave:
  sfree(json);
  return result;
}

static int wg_format_some_of_list_ctr(
    const monitored_resource_t *monitored_resource, const wg_payload_t *list,
    const wg_payload_t **new_list, char **json, _Bool pretty) {
  char *result;
  if (wg_json_CreateCollectdTimeseriesRequest(
          pretty, monitored_resource, list, new_list, &result) != 0) {
    ERROR("write_gcm: wg_json_CreateCollectdTimeseriesRequest failed.");
    return -1;
  }
  if (list == *new_list) {
    ERROR("write_gcm: wg_format_some_of_list_ctr failed to make progress.");
    sfree(result);
    return -1;
  }
  *json = result;
  return 0;
}

static int wg_format_some_of_list_custom(
    const monitored_resource_t *monitored_resource, const wg_payload_t *list,
    const wg_payload_t **new_list, char **json, _Bool pretty) {
  char *result;
  if (wg_json_CreateTimeSeriesRequest(
          pretty, monitored_resource, list, new_list, &result) != 0) {
    ERROR("write_gcm: wg_json_CreateTimeSeriesRequest failed.");
    return -1;
  }
  if (list == *new_list) {
    ERROR("write_gcm: wg_format_some_of_list_custom failed to make progress.");
    sfree(result);
    return -1;
  }
  *json = result;
  return 0;
}

static int wg_extract_distinct_payloads(wg_payload_t *src,
    wg_payload_t **distinct_list, wg_payload_t **residual_list,
    int *distinct_size) {
  // Items to clean up.
  c_avl_tree_t *been_here_tree = NULL;
  wg_payload_t *distinct_head = NULL;
  wg_payload_t *distinct_tail = NULL;
  wg_payload_t *residual_head = NULL;
  wg_payload_t *residual_tail = NULL;
  int result = -1;  // Pessimistically assume failure.

  *distinct_size = 0;
  been_here_tree = wg_been_here_tree_create();
  if (been_here_tree == NULL) {
    ERROR("write_gcm: been_here_tree_create failed.");
    goto leave;
  }

  while (src != NULL) {
    // Pop from head of list.
    wg_payload_t *next = src->next;
    src->next = NULL;

    if (c_avl_get(been_here_tree, &src->key, NULL) == 0) {
      // Collision, so append to residual list.
      if (residual_head == NULL) {  // meaning residual_tail is also NULL.
        residual_head = src;
      } else {
        residual_tail->next = src;
      }
      residual_tail = src;
      src = next;
      continue;
    }

    // Otherwise, create a new entry in the tree, then append the item to the
    // distinct list.
    wg_payload_key_t *new_key = wg_payload_key_clone(&src->key);
    if (new_key == NULL) {
      ERROR("write_gcm: wg_payload_key_clone failed");
      goto leave;
    }

    // Tree takes ownership of 'new_key'.
    if (c_avl_insert(been_here_tree, new_key, NULL) != 0) {
      ERROR("write_gcm: c_avl_insert failed");
      wg_payload_key_destroy_inline(new_key);
      sfree(new_key);
      goto leave;
    }

    if (distinct_head == NULL) {  // meaning distinct_tail is also NULL.
      distinct_head = src;
    } else {
      distinct_tail->next = src;
    }
    distinct_tail = src;
    src = next;
    ++*distinct_size;
  }

  // Success!

  result = 0;

 leave:
  // Append remaining items (if any) to the residual list.
  if (residual_head == NULL) {  // meaning residual_tail is also NULL.
    residual_head = src;
  } else {
    residual_tail->next = src;
  }
  *distinct_list = distinct_head;
  *residual_list = residual_head;
  wg_been_here_tree_destroy(been_here_tree);
  return result;
}

static int wg_lookup_or_create_tracker_value(c_avl_tree_t *tree,
    const wg_payload_t *payload, wg_deriv_tracker_value_t **tracker_value,
    _Bool *created) {
  // Items to clean up upon exit.
  wg_deriv_tracker_value_t *value = NULL;

  if (c_avl_get(tree, &payload->key, (void**)tracker_value) == 0) {
    // tracker_value found!
    *created = 0;
    return 0;
  }

  // Couldn't find a tracker value. Need to create one.
  value = wg_deriv_tracker_value_create(payload->num_values);
  if (value == NULL) {
    ERROR("write_gcm: deriv_tracker_value_create failed.");
    goto error;
  }

  wg_payload_key_t *new_key = wg_payload_key_clone(&payload->key);
  // tree owns 'new_key' and 'value'.
  if (c_avl_insert(tree, new_key, value) == 0) {
    *tracker_value = value;
    *created = 1;
    return 0;
  }

  ERROR("write_gcm: Can't insert new entry into tree.");
  wg_payload_key_destroy_inline(new_key);
  sfree(new_key);

 error:
  wg_deriv_tracker_value_destroy(value);
  return -1;
}

static int wait_next_queue_event(wg_queue_t *queue, cdtime_t last_flush_time,
    _Bool *want_terminate, wg_payload_t **payloads) {
  cdtime_t next_flush_time = last_flush_time + plugin_get_interval();
  pthread_mutex_lock(&queue->mutex);
  if (!queue->flush_complete && !queue->request_flush) {
    queue->flush_complete = 1;
    pthread_cond_signal(&queue->cond);
  }
  while (1) {
    cdtime_t now = cdtime();
    if (queue->request_flush ||
        queue->request_terminate ||
        queue->size >= QUEUE_FLUSH_SIZE ||
        now > next_flush_time) {
      DEBUG("write_gcm: wait_next_queue_event: returning a queue of size %zd",
          queue->size);
      *payloads = queue->head;
      *want_terminate = queue->request_terminate;
      queue->head = NULL;
      queue->tail = NULL;
      queue->size = 0;
      queue->request_flush = 0;
      queue->request_terminate = 0;
      pthread_mutex_unlock(&queue->mutex);
      return 0;
    }
    pthread_cond_wait(&queue->cond, &queue->mutex);
  }
}

static int wg_update_stats(const wg_stats_t *stats)
{
  data_set_t ds = {};  // zero-fill
  value_list_t vl = {
      .plugin = "stackdriver_agent",
      .time = cdtime()
  };
  if (uc_update(&ds, &vl) != 0)
  {
    ERROR("%s: uc_update returned an error", this_plugin_name);
    return -1;
  }
  // The corresponding uc_meta_data_get calls are in stackdriver_agent.c.
  int res0 = uc_meta_data_add_unsigned_int(&vl, SAGT_API_REQUESTS_SUCCESS, stats->api_successes);
  int res1 = uc_meta_data_add_unsigned_int(&vl, SAGT_API_REQUESTS_CONNECTIVITY_FAILURES,
    stats->api_connectivity_failures);
  int res2 = uc_meta_data_add_unsigned_int(&vl, SAGT_API_REQUESTS_ERRORS, stats->api_errors);
  if (res0 != 0 || res1 != 0 || res2 != 0) {
    ERROR("%s: uc_meta_data_add returned an error", this_plugin_name);
    return -1;
  }
  return 0;
}

//==============================================================================
//==============================================================================
//==============================================================================
// Various collectd entry points.
//==============================================================================
//==============================================================================
//==============================================================================

static wg_configbuilder_t *wg_configbuilder_g = NULL;


// Transform incoming value_list into our "payload" format and append it to the
// work queue.
static int wg_write(const data_set_t *ds, const value_list_t *vl,
                    user_data_t *user_data) {
  assert(ds->ds_num > 0);
  wg_context_t *ctx = user_data->data;

  // Initially assume Agent Tranlation Service queue and processor
  const char *queue_name = "ATS";
  wg_queue_t *queue = ctx->ats_queue;
  static void *(*processor)(void *) = wg_process_ats_queue;

  // Unless it has a particular meta_data field in which case use the
  // Stackdriver one.
  if (vl->meta != NULL) {
    char **toc = NULL;
    int toc_size = meta_data_toc(vl->meta, &toc);
    if (toc_size < 0) {
      ERROR("write_gcm: wg_write: error reading metadata table of contents.");
      return -1;
    }
    for (int i = 0; i < toc_size; ++i) {
      if (!strcmp(toc[i], custom_metric_key)) {
        queue_name = "GSD";
        queue = ctx->gsd_queue;
        processor = wg_process_gsd_queue;
      }
    }
    strarray_free (toc, toc_size);
  }

  // Allocate the payload.
  wg_payload_t *payload = wg_payload_create(ds, vl);
  if (payload == NULL) {
    ERROR("write_gcm: wg_payload_create failed.");
    return -1;
  }

  pthread_mutex_lock(&queue->mutex);
  // One-time startup of the consumer thread.
  if (!queue->consumer_thread_created) {
    if (plugin_thread_create(&queue->consumer_thread, NULL, processor,
        ctx) != 0) {
      ERROR("write_gcm: plugin_thread_create failed");
      pthread_mutex_unlock(&queue->mutex);
      return -1;
    }
    queue->consumer_thread_created = 1;
  }

  // Backpressure. If queue is backed up then something has gone horribly wrong.
  // Maybe the queue processor died. If this happens we drop the item at the
  // head of the queue.
  if (queue->size >= QUEUE_DROP_SIZE) {
    wg_payload_t *to_remove = queue->head;
    queue->head = queue->head->next;
    if (queue->head == NULL) {
      queue->tail = NULL;
    }
    --queue->size;
    to_remove->next = NULL;
    wg_payload_destroy(to_remove);
  }

  // Append to queue.
  if (queue->head == NULL) {
    queue->head = payload;
    queue->tail = payload;
  } else {
    queue->tail->next = payload;
    queue->tail = payload;
  }
  ++queue->size;

  static cdtime_t next_message_time;
  cdtime_t now = cdtime();
  if (now >= next_message_time) {
    DEBUG("write_gcm: current %s queue size is %zd", queue_name, queue->size);
    next_message_time = now + TIME_T_TO_CDTIME_T(10);  // Report every 10 sec.
  }
  pthread_cond_signal(&queue->cond);
  pthread_mutex_unlock(&queue->mutex);
  return 0;
}

// Request a flush from the queue processor.
static int wg_flush(cdtime_t timeout,
                    const char *identifier __attribute__((unused)),
                    user_data_t *user_data) {
  wg_context_t *ctx = user_data->data;
  // Flush all queues in sequence.
  wg_queue_t *queues[] = { ctx->ats_queue, ctx->gsd_queue };
  for (int i = 0; i < STATIC_ARRAY_SIZE(queues) ; ++i) {
    wg_queue_t *queue = queues[i];

    pthread_mutex_lock(&queue->mutex);
    queue->request_flush = 1;
    queue->flush_complete = 0;
    pthread_cond_signal(&queue->cond);

    // If collectd is in the end-to-end test mode (command line option -T), then
    // wait for the flush to complete.
    if (wg_end_to_end_test_mode()) {
      while (!queue->flush_complete) {
        pthread_cond_wait(&queue->cond, &queue->mutex);
      }
    }

    pthread_mutex_unlock(&queue->mutex);
  }
  return 0;
}

static int wg_config(oconfig_item_t *ci) {
  wg_configbuilder_g = wg_configbuilder_create(ci->children_num, ci->children);
  if (wg_configbuilder_g == NULL) {
    ERROR("write_gcm: wg_config: wg_configbuilder_create failed");
    return -1;
  }

  return 0;
}

// If there is a config block, wg_config has been run by now (and therefore
// wg_configbuilder_g will be non-NULL).
static int wg_init(void) {
  // Items to clean up on exit.
  wg_context_t *ctx = NULL;
  int result = -1;  // Pessimistically assume failure.

  curl_global_init(CURL_GLOBAL_SSL);

  if (wg_configbuilder_g == NULL) {
    // If no config specified, make the default one.
    wg_configbuilder_g = wg_configbuilder_create(0, NULL);
    if (wg_configbuilder_g == NULL) {
      ERROR("write_gcm: wg_init: wg_configbuilder_create failed.");
      return -1;
    }
  }

  ctx = wg_context_create(wg_configbuilder_g);
  if (ctx == NULL) {
    ERROR("write_gcm: wg_init: wg_context_create failed.");
    goto leave;
  }

  user_data_t user_data = {
      .data = ctx,
      .free_func = NULL
  };
  if (plugin_register_flush(this_plugin_name, &wg_flush, &user_data) != 0) {
    ERROR("write_gcm: wg_init: plugin_register_flush failed");
    goto leave;
  }
  user_data.free_func = (void(*)(void*))&wg_context_destroy;
  if (plugin_register_write(this_plugin_name, &wg_write, &user_data) != 0) {
    ERROR("write_gcm: wg_init: plugin_register_write failed");
    goto leave;
  }

  ctx = NULL;
  result = 0;

 leave:
  wg_context_destroy(ctx);
  return result;
}

// In end-to-end test mode (-T from the command line), we return an error if
// this plugin has seen any errors during its operation (e.g. PERMISSION DENIED
// from the server).
static int wg_shutdown(void) {
  if (!wg_end_to_end_test_mode()) {
    return 0;
  }
  if (wg_some_error_occured_g) {
    return -1;
  }
  return 0;
}

//==============================================================================
//==============================================================================
//==============================================================================
// Collectd module initialization entry point.
//==============================================================================
//==============================================================================
//==============================================================================
void module_register(void) {
  INFO("write_gcm: inside module_register for %s", COLLECTD_USERAGENT);
  plugin_register_complex_config(this_plugin_name, &wg_config);
  plugin_register_init(this_plugin_name, &wg_init);
  plugin_register_shutdown(this_plugin_name, &wg_shutdown);
}
