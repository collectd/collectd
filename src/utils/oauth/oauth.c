/**
 * collectd - src/utils_oauth.c
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
#include "utils/oauth/oauth.h"

#include <curl/curl.h>

#include <yajl/yajl_tree.h>
#include <yajl/yajl_version.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/pkcs12.h>
#include <openssl/sha.h>

/*
 * Private variables
 */
#define GOOGLE_TOKEN_URL "https://accounts.google.com/o/oauth2/token"

/* Max send buffer size, since there will be only one writer thread and
 * monitoring api supports up to 100K bytes in one request, 64K is reasonable
 */
#define MAX_BUFFER_SIZE 65536
#define MAX_ENCODE_SIZE 2048

struct oauth_s {
  char *url;
  char *iss;
  char *aud;
  char *scope;

  EVP_PKEY *key;

  char *token;
  cdtime_t valid_until;
};

struct memory_s {
  char *memory;
  size_t size;
};
typedef struct memory_s memory_t;

#define OAUTH_GRANT_TYPE "urn:ietf:params:oauth:grant-type:jwt-bearer"
#define OAUTH_EXPIRATION_TIME TIME_T_TO_CDTIME_T(3600)
#define OAUTH_HEADER "{\"alg\":\"RS256\",\"typ\":\"JWT\"}"

static const char OAUTH_CLAIM_FORMAT[] = "{"
                                         "\"iss\":\"%s\","
                                         "\"scope\":\"%s\","
                                         "\"aud\":\"%s\","
                                         "\"exp\":%lu,"
                                         "\"iat\":%lu"
                                         "}";

static size_t write_memory(void *contents, size_t size, size_t nmemb, /* {{{ */
                           void *userp) {
  size_t realsize = size * nmemb;
  memory_t *mem = (memory_t *)userp;
  char *tmp;

  if (0x7FFFFFF0 < mem->size || 0x7FFFFFF0 - mem->size < realsize) {
    ERROR("integer overflow");
    return 0;
  }

  tmp = (char *)realloc((void *)mem->memory, mem->size + realsize + 1);
  if (tmp == NULL) {
    /* out of memory! */
    ERROR("write_memory: not enough memory (realloc returned NULL)");
    return 0;
  }
  mem->memory = tmp;

  memcpy(&(mem->memory[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->memory[mem->size] = 0;

  return realsize;
} /* }}} size_t write_memory */

/* Base64-encodes "s" and stores the result in buffer.
 * Returns zero on success, non-zero otherwise. */
static int base64_encode_n(char const *s, size_t s_size, /* {{{ */
                           char *buffer, size_t buffer_size) {
  BIO *b64;
  BUF_MEM *bptr;
  int status;
  size_t i;

  /* Set up the memory-base64 chain */
  b64 = BIO_new(BIO_f_base64());
  BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
  b64 = BIO_push(b64, BIO_new(BIO_s_mem()));

  /* Write data to the chain */
  BIO_write(b64, (void const *)s, s_size);
  status = BIO_flush(b64);
  if (status != 1) {
    ERROR("utils_oauth: base64_encode: BIO_flush() failed.");
    BIO_free_all(b64);
    return -1;
  }

  /* Never fails */
  BIO_get_mem_ptr(b64, &bptr);

  if (buffer_size <= bptr->length) {
    ERROR("utils_oauth: base64_encode: Buffer too small.");
    BIO_free_all(b64);
    return -1;
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
  return 0;
} /* }}} int base64_encode_n */

/* Base64-encodes "s" and stores the result in buffer.
 * Returns zero on success, non-zero otherwise. */
static int base64_encode(char const *s, /* {{{ */
                         char *buffer, size_t buffer_size) {
  return base64_encode_n(s, strlen(s), buffer, buffer_size);
} /* }}} int base64_encode */

/* get_header returns the base64 encoded OAuth header. */
static int get_header(char *buffer, size_t buffer_size) /* {{{ */
{
  char header[] = OAUTH_HEADER;

  return base64_encode(header, buffer, buffer_size);
} /* }}} int get_header */

/* get_claim constructs an OAuth claim and returns it as base64 encoded string.
 */
static int get_claim(oauth_t *auth, char *buffer, size_t buffer_size) /* {{{ */
{
  char claim[buffer_size];
  cdtime_t exp;
  cdtime_t iat;
  int status;

  iat = cdtime();
  exp = iat + OAUTH_EXPIRATION_TIME;

  /* create the claim set */
  status =
      snprintf(claim, sizeof(claim), OAUTH_CLAIM_FORMAT, auth->iss, auth->scope,
               auth->aud, (unsigned long)CDTIME_T_TO_TIME_T(exp),
               (unsigned long)CDTIME_T_TO_TIME_T(iat));
  if (status < 1)
    return -1;
  else if ((size_t)status >= sizeof(claim))
    return ENOMEM;

  DEBUG("utils_oauth: get_claim() = %s", claim);

  return base64_encode(claim, buffer, buffer_size);
} /* }}} int get_claim */

/* get_signature signs header and claim with pkey and returns the signature in
 * buffer. */
static int get_signature(char *buffer, size_t buffer_size, /* {{{ */
                         char const *header, char const *claim,
                         EVP_PKEY *pkey) {
  char payload[buffer_size];
  size_t payload_len;
  char signature[buffer_size];
  unsigned int signature_size;
  int status;

  /* Make the string to sign */
  payload_len = snprintf(payload, sizeof(payload), "%s.%s", header, claim);
  if (payload_len < 1) {
    return -1;
  } else if (payload_len >= sizeof(payload)) {
    return ENOMEM;
  }

  /* Create the signature */
  signature_size = EVP_PKEY_size(pkey);
  if (signature_size > sizeof(signature)) {
    ERROR("utils_oauth: Signature is too large (%u bytes).", signature_size);
    return -1;
  }

  EVP_MD_CTX *ctx = EVP_MD_CTX_new();

  /* EVP_SignInit(3SSL) claims this is a void function, but in fact it returns
   * an int. We're not going to rely on this, though. */
  EVP_SignInit(ctx, EVP_sha256());

  status = EVP_SignUpdate(ctx, payload, payload_len);
  if (status != 1) {
    char errbuf[1024];
    ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
    ERROR("utils_oauth: EVP_SignUpdate failed: %s", errbuf);

    EVP_MD_CTX_free(ctx);
    return -1;
  }

  status =
      EVP_SignFinal(ctx, (unsigned char *)signature, &signature_size, pkey);
  if (status != 1) {
    char errbuf[1024];
    ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
    ERROR("utils_oauth: EVP_SignFinal failed: %s", errbuf);

    EVP_MD_CTX_free(ctx);
    return -1;
  }

  EVP_MD_CTX_free(ctx);

  return base64_encode_n(signature, (size_t)signature_size, buffer,
                         buffer_size);
} /* }}} int get_signature */

static int get_assertion(oauth_t *auth, char *buffer,
                         size_t buffer_size) /* {{{ */
{
  char header[buffer_size];
  char claim[buffer_size];
  char signature[buffer_size];
  int status;

  status = get_header(header, sizeof(header));
  if (status != 0)
    return -1;

  status = get_claim(auth, claim, sizeof(claim));
  if (status != 0)
    return -1;

  status =
      get_signature(signature, sizeof(signature), header, claim, auth->key);
  if (status != 0)
    return -1;

  status = snprintf(buffer, buffer_size, "%s.%s.%s", header, claim, signature);
  if (status < 1)
    return -1;
  else if ((size_t)status >= buffer_size)
    return ENOMEM;

  return 0;
} /* }}} int get_assertion */

int oauth_parse_json_token(char const *json, /* {{{ */
                           char *out_access_token, size_t access_token_size,
                           cdtime_t *expires_in) {
  time_t expire_in_seconds = 0;
  yajl_val root;
  yajl_val token_val;
  yajl_val expire_val;
  char errbuf[1024];
  const char *token_path[] = {"access_token", NULL};
  const char *expire_path[] = {"expires_in", NULL};

  root = yajl_tree_parse(json, errbuf, sizeof(errbuf));
  if (root == NULL) {
    ERROR("utils_oauth: oauth_parse_json_token: parse error %s", errbuf);
    return -1;
  }

  token_val = yajl_tree_get(root, token_path, yajl_t_string);
  if (token_val == NULL) {
    ERROR("utils_oauth: oauth_parse_json_token: access token field not found");
    yajl_tree_free(root);
    return -1;
  }
  sstrncpy(out_access_token, YAJL_GET_STRING(token_val), access_token_size);

  expire_val = yajl_tree_get(root, expire_path, yajl_t_number);
  if (expire_val == NULL) {
    ERROR("utils_oauth: oauth_parse_json_token: expire field found");
    yajl_tree_free(root);
    return -1;
  }
  expire_in_seconds = (time_t)YAJL_GET_INTEGER(expire_val);
  DEBUG("oauth_parse_json_token: expires_in %lu",
        (unsigned long)expire_in_seconds);

  *expires_in = TIME_T_TO_CDTIME_T(expire_in_seconds);
  yajl_tree_free(root);
  return 0;
} /* }}} int oauth_parse_json_token */

static int new_token(oauth_t *auth) /* {{{ */
{
  CURL *curl;
  char assertion[1024];
  char post_data[1024];
  memory_t data;
  char access_token[256];
  cdtime_t expires_in;
  cdtime_t now;
  char curl_errbuf[CURL_ERROR_SIZE];
  int status = 0;

  data.size = 0;
  data.memory = NULL;

  now = cdtime();

  status = get_assertion(auth, assertion, sizeof(assertion));
  if (status != 0) {
    ERROR("utils_oauth: Failed to get token using service account %s.",
          auth->iss);
    return -1;
  }

  snprintf(post_data, sizeof(post_data), "grant_type=%s&assertion=%s",
           OAUTH_GRANT_TYPE, assertion);

  curl = curl_easy_init();
  if (curl == NULL) {
    ERROR("utils_oauth: curl_easy_init failed.");
    return -1;
  }

  curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curl_errbuf);
  curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_memory);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &data);
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, post_data);
  curl_easy_setopt(curl, CURLOPT_URL, auth->url);

  status = curl_easy_perform(curl);
  if (status != CURLE_OK) {
    ERROR("utils_oauth: curl_easy_perform failed with status %i: %s", status,
          curl_errbuf);

    sfree(data.memory);
    curl_easy_cleanup(curl);

    return -1;
  } else {
    long http_code = 0;

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    if ((http_code < 200) || (http_code >= 300)) {
      ERROR("utils_oauth: POST request to %s failed: HTTP error %ld", auth->url,
            http_code);
      if (data.memory != NULL)
        INFO("utils_oauth: Server replied: %s", data.memory);

      sfree(data.memory);
      curl_easy_cleanup(curl);

      return -1;
    }
  }

  status = oauth_parse_json_token(data.memory, access_token,
                                  sizeof(access_token), &expires_in);
  if (status != 0) {
    sfree(data.memory);
    curl_easy_cleanup(curl);

    return -1;
  }

  sfree(auth->token);
  auth->token = strdup(access_token);
  if (auth->token == NULL) {
    ERROR("utils_oauth: strdup failed");
    auth->valid_until = 0;

    sfree(data.memory);
    curl_easy_cleanup(curl);
    return -1;
  }

  INFO("utils_oauth: OAuth2 access token is valid for %.3fs",
       CDTIME_T_TO_DOUBLE(expires_in));
  auth->valid_until = now + expires_in;

  sfree(data.memory);
  curl_easy_cleanup(curl);

  return 0;
} /* }}} int new_token */

static int renew_token(oauth_t *auth) /* {{{ */
{
  /* Renew OAuth token 30 seconds *before* it expires. */
  cdtime_t const slack = TIME_T_TO_CDTIME_T(30);

  if (auth->valid_until > (cdtime() + slack))
    return 0;

  return new_token(auth);
} /* }}} int renew_token */

static oauth_t *oauth_create(char const *url, char const *iss,
                             char const *scope, char const *aud,
                             EVP_PKEY *key) /* {{{ */
{
  oauth_t *auth;

  if ((url == NULL) || (iss == NULL) || (scope == NULL) || (aud == NULL) ||
      (key == NULL))
    return NULL;

  auth = malloc(sizeof(*auth));
  if (auth == NULL)
    return NULL;
  memset(auth, 0, sizeof(*auth));

  auth->url = strdup(url);
  auth->iss = strdup(iss);
  auth->scope = strdup(scope);
  auth->aud = strdup(aud);

  if ((auth->url == NULL) || (auth->iss == NULL) || (auth->scope == NULL) ||
      (auth->aud == NULL)) {
    oauth_destroy(auth);
    return NULL;
  }

  auth->key = key;

  return auth;
} /* }}} oauth_t *oauth_create */

/*
 * Public
 */
oauth_google_t oauth_create_google_json(char const *buffer, char const *scope) {
  char errbuf[1024];
  yajl_val root = yajl_tree_parse(buffer, errbuf, sizeof(errbuf));
  if (root == NULL) {
    ERROR("utils_oauth: oauth_create_google_json: parse error %s", errbuf);
    return (oauth_google_t){NULL};
  }

  yajl_val field_project =
      yajl_tree_get(root, (char const *[]){"project_id", NULL}, yajl_t_string);
  if (field_project == NULL) {
    ERROR("utils_oauth: oauth_create_google_json: project_id field not found");
    yajl_tree_free(root);
    return (oauth_google_t){NULL};
  }
  char const *project_id = YAJL_GET_STRING(field_project);

  yajl_val field_iss = yajl_tree_get(
      root, (char const *[]){"client_email", NULL}, yajl_t_string);
  if (field_iss == NULL) {
    ERROR(
        "utils_oauth: oauth_create_google_json: client_email field not found");
    yajl_tree_free(root);
    return (oauth_google_t){NULL};
  }

  yajl_val field_token_uri =
      yajl_tree_get(root, (char const *[]){"token_uri", NULL}, yajl_t_string);
  char const *token_uri = (field_token_uri != NULL)
                              ? YAJL_GET_STRING(field_token_uri)
                              : GOOGLE_TOKEN_URL;

  yajl_val field_priv_key =
      yajl_tree_get(root, (char const *[]){"private_key", NULL}, yajl_t_string);
  if (field_priv_key == NULL) {
    ERROR("utils_oauth: oauth_create_google_json: private_key field not found");
    yajl_tree_free(root);
    return (oauth_google_t){NULL};
  }

  BIO *bp = BIO_new_mem_buf(YAJL_GET_STRING(field_priv_key), -1);
  EVP_PKEY *pkey = PEM_read_bio_PrivateKey(bp, NULL, NULL, NULL);
  if (pkey == NULL) {
    char errbuf[1024];
    ERR_error_string_n(ERR_get_error(), errbuf, sizeof(errbuf));
    ERROR(
        "utils_oauth: oauth_create_google_json: parsing private key failed: %s",
        errbuf);
    BIO_free(bp);
    yajl_tree_free(root);
    return (oauth_google_t){NULL};
  }

  BIO_free(bp);

  oauth_t *oauth = oauth_create(token_uri, YAJL_GET_STRING(field_iss), scope,
                                token_uri, pkey);
  if (oauth == NULL) {
    yajl_tree_free(root);
    return (oauth_google_t){NULL};
  }

  oauth_google_t ret = {
      .project_id = strdup(project_id), .oauth = oauth,
  };

  yajl_tree_free(root);
  return ret;
} /* oauth_google_t oauth_create_google_json */

oauth_google_t oauth_create_google_file(char const *path,
                                        char const *scope) { /* {{{ */
  int fd = open(path, O_RDONLY);
  if (fd == -1)
    return (oauth_google_t){NULL};

  struct stat st = {0};
  if (fstat(fd, &st) != 0) {
    close(fd);
    return (oauth_google_t){NULL};
  }

  size_t buf_size = (size_t)st.st_size;
  char *buf = calloc(1, buf_size + 1);
  if (buf == NULL) {
    close(fd);
    return (oauth_google_t){NULL};
  }

  if (sread(fd, buf, buf_size) != 0) {
    free(buf);
    close(fd);
    return (oauth_google_t){NULL};
  }
  close(fd);
  buf[buf_size] = 0;

  oauth_google_t ret = oauth_create_google_json(buf, scope);

  free(buf);
  return ret;
} /* }}} oauth_google_t oauth_create_google_file */

/* oauth_create_google_default checks for JSON credentials in well-known
 * positions, similar to gcloud and other tools. */
oauth_google_t oauth_create_google_default(char const *scope) {
  char const *app_creds;
  if ((app_creds = getenv("GOOGLE_APPLICATION_CREDENTIALS")) != NULL) {
    oauth_google_t ret = oauth_create_google_file(app_creds, scope);
    if (ret.oauth == NULL) {
      ERROR("The environment variable GOOGLE_APPLICATION_CREDENTIALS is set to "
            "\"%s\" but that file could not be read.",
            app_creds);
    } else {
      return ret;
    }
  }

  char const *home;
  if ((home = getenv("HOME")) != NULL) {
    char path[PATH_MAX];
    snprintf(path, sizeof(path),
             "%s/.config/gcloud/application_default_credentials.json", home);

    oauth_google_t ret = oauth_create_google_file(path, scope);
    if (ret.oauth != NULL) {
      return ret;
    }
  }

  return (oauth_google_t){NULL};
} /* }}} oauth_google_t oauth_create_google_default */

void oauth_destroy(oauth_t *auth) /* {{{ */
{
  if (auth == NULL)
    return;

  sfree(auth->url);
  sfree(auth->iss);
  sfree(auth->scope);
  sfree(auth->aud);

  if (auth->key != NULL) {
    EVP_PKEY_free(auth->key);
    auth->key = NULL;
  }

  sfree(auth);
} /* }}} void oauth_destroy */

int oauth_access_token(oauth_t *auth, char *buffer,
                       size_t buffer_size) /* {{{ */
{
  int status;

  if (auth == NULL)
    return EINVAL;

  status = renew_token(auth);
  if (status != 0)
    return status;
  assert(auth->token != NULL);

  sstrncpy(buffer, auth->token, buffer_size);
  return 0;
} /* }}} int oauth_access_token */
