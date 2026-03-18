/**
 * collectd - src/curl_json.c
 * Copyright (C) 2017  Florian octo Forster
 *
 * Licensed under the same terms and conditions as src/curl_json.c.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include <stdarg.h>

#include <curl/curl.h>

#undef curl_easy_getinfo
#undef curl_easy_setopt

static void test_curl_easy_cleanup(CURL *curl);
static CURLcode test_curl_easy_getinfo(CURL *curl, CURLINFO info, ...);
static CURL *test_curl_easy_init(void);
static CURLcode test_curl_easy_perform(CURL *curl);
static CURLcode test_curl_easy_setopt(CURL *curl, CURLoption option, ...);

#define curl_easy_cleanup test_curl_easy_cleanup
#define curl_easy_getinfo test_curl_easy_getinfo
#define curl_easy_init test_curl_easy_init
#define curl_easy_perform test_curl_easy_perform
#define curl_easy_setopt test_curl_easy_setopt

#include "curl_json.c"

#include "testing.h"

typedef struct {
  CURLcode perform_result;
  const char *perform_error;
  const char *response_body;
  long response_code;
} test_curl_spec_t;

typedef struct {
  int id;
  int cleanup_calls;
  int perform_calls;

  CURLcode perform_result;
  const char *perform_error;
  const char *response_body;
  long response_code;

  char *url;
  char *effective_url;
  char *error_buffer;
  void *write_data;
  curl_write_callback write_function;

  bool saw_error_buffer;
  bool saw_ipresolve;
  bool saw_timeout_ms;
  bool saw_url;
  bool saw_write_data;

  long ipresolve;
  long timeout_ms;
} test_curl_handle_t;

static struct {
  int cleanup_calls;
  int fail_init_call;
  int getinfo_calls;
  int init_calls;
  int setopt_calls;
  int unexpected_getinfo_calls;
  int unexpected_init_calls;
  int unexpected_setopt_calls;

  test_curl_spec_t specs[8];
  size_t specs_num;
  test_curl_handle_t *handles[8];
} test_curl;

static c_avl_tree_t *submitted_values;

static test_curl_handle_t *test_curl_handle(CURL *curl) {
  return (test_curl_handle_t *)curl;
}

static test_curl_handle_t *test_handle_at(size_t index) {
  if (index >= STATIC_ARRAY_SIZE(test_curl.handles))
    return NULL;
  return test_curl.handles[index];
}

static void test_curl_reset(void) {
  for (size_t i = 0; i < STATIC_ARRAY_SIZE(test_curl.handles); i++) {
    free(test_curl.handles[i]);
    test_curl.handles[i] = NULL;
  }

  memset(&test_curl, 0, sizeof(test_curl));
}

static void test_values_reset(void) {
  if (submitted_values == NULL)
    return;

  void *key = NULL;
  void *value = NULL;
  while (c_avl_pick(submitted_values, &key, &value) == 0) {
    /* key belongs to cj_free(). */
    free(value);
  }
  c_avl_destroy(submitted_values);
  submitted_values = NULL;
}

static void test_prepare(void) {
  test_values_reset();
  test_curl_reset();

  submitted_values = cj_avl_create();
  assert(submitted_values != NULL);
}

static void test_submit(cj_t *db, cj_key_t *key, value_t *value) {
  value_t *value_copy = calloc(1, sizeof(*value_copy));

  assert(db != NULL);
  assert(key != NULL);
  assert(value != NULL);
  assert(submitted_values != NULL);
  assert(value_copy != NULL);

  *value_copy = *value;
  assert(c_avl_insert(submitted_values, key->path, value_copy) == 0);
}

static derive_t test_metric(char const *path) {
  value_t *ret = NULL;

  if ((submitted_values != NULL) &&
      (c_avl_get(submitted_values, path, (void *)&ret) == 0)) {
    return ret->derive;
  }

  return -1;
}

static int test_read(cj_t *db) {
  user_data_t ud = {
      .data = db,
  };

  return cj_read(&ud);
}

static cj_t *test_parse_setup(char const *json, char const *key_path) {
  cj_t *db = calloc(1, sizeof(*db));
  yajl_status status;

  assert(db != NULL);

  db->yajl = yajl_alloc(&ycallbacks,
#if HAVE_YAJL_V2
                        /* alloc funcs = */ NULL,
#else
                        /* alloc funcs = */ NULL, NULL,
#endif
                        /* context = */ (void *)db);
  assert(db->yajl != NULL);

  cj_key_t *key = calloc(1, sizeof(*key));
  assert(key != NULL);
  key->path = strdup(key_path);
  key->type = strdup("MAGIC");
  assert(key->path != NULL);
  assert(key->type != NULL);
  assert(cj_append_key(db, key) == 0);

  cj_tree_entry_t root = {0};
  root.type = TREE;
  root.tree = db->tree;
  db->state[0].entry = &root;

  assert(cj_curl_callback((void *)json, 1, strlen(json), db) == strlen(json));
#if HAVE_YAJL_V2
  status = yajl_complete_parse(db->yajl);
#else
  status = yajl_parse_complete(db->yajl);
#endif
  assert(status == yajl_status_ok);

  db->state[0].entry = NULL;

  return db;
}

static cj_t *test_read_setup(char const *key_path) {
  cj_t *db = calloc(1, sizeof(*db));
  cj_key_t *key = calloc(1, sizeof(*key));

  assert(db != NULL);
  assert(key != NULL);

  db->url = strdup("http://example.invalid:18080/metrics.json");
  db->instance = strdup("default");
  db->address_family = CURL_IPRESOLVE_V4;
  db->timeout = 1234;
  assert(db->url != NULL);
  assert(db->instance != NULL);

  key->path = strdup(key_path);
  key->type = strdup("MAGIC");
  assert(key->path != NULL);
  assert(key->type != NULL);
  assert(cj_append_key(db, key) == 0);
  assert(cj_init_curl(db) == 0);

  return db;
}

static void test_teardown(cj_t *db) {
  if (db == NULL)
    return;

  if (db->yajl != NULL) {
    yajl_free(db->yajl);
    db->yajl = NULL;
  }

  test_values_reset();
  cj_free(db);
  test_curl_reset();
}

static CURL *test_curl_easy_init(void) {
  int call = ++test_curl.init_calls;

  if (test_curl.fail_init_call == call)
    return NULL;

  if ((size_t)call > STATIC_ARRAY_SIZE(test_curl.handles)) {
    test_curl.unexpected_init_calls++;
    return NULL;
  }

  test_curl_handle_t *handle = calloc(1, sizeof(*handle));
  assert(handle != NULL);

  handle->id = call;
  handle->perform_result = CURLE_OK;
  handle->response_code = 200;

  if ((size_t)call <= test_curl.specs_num) {
    test_curl_spec_t *spec = &test_curl.specs[call - 1];

    handle->perform_result = spec->perform_result;
    handle->perform_error = spec->perform_error;
    handle->response_body = spec->response_body;
    if (spec->response_code != 0)
      handle->response_code = spec->response_code;
  }

  test_curl.handles[call - 1] = handle;
  return (CURL *)handle;
}

static CURLcode test_curl_easy_setopt(CURL *curl, CURLoption option, ...) {
  va_list ap;
  test_curl_handle_t *handle = test_curl_handle(curl);

  assert(handle != NULL);

  test_curl.setopt_calls++;

  va_start(ap, option);
  switch (option) {
  case CURLOPT_NOSIGNAL:
  case CURLOPT_FOLLOWLOCATION:
  case CURLOPT_MAXREDIRS:
  case CURLOPT_HTTPAUTH:
  case CURLOPT_SSL_VERIFYPEER:
  case CURLOPT_SSL_VERIFYHOST:
    (void)va_arg(ap, long);
    break;
  case CURLOPT_WRITEFUNCTION:
    handle->write_function = va_arg(ap, curl_write_callback);
    break;
  case CURLOPT_WRITEDATA:
    handle->write_data = va_arg(ap, void *);
    handle->saw_write_data = true;
    break;
  case CURLOPT_USERAGENT:
#ifdef HAVE_CURLOPT_USERNAME
  case CURLOPT_USERNAME:
  case CURLOPT_PASSWORD:
#endif
  case CURLOPT_USERPWD:
  case CURLOPT_CAINFO:
  case CURLOPT_POSTFIELDS:
    (void)va_arg(ap, char *);
    break;
  case CURLOPT_ERRORBUFFER:
    handle->error_buffer = va_arg(ap, char *);
    handle->saw_error_buffer = true;
    break;
  case CURLOPT_HTTPHEADER:
    (void)va_arg(ap, struct curl_slist *);
    break;
  case CURLOPT_IPRESOLVE:
    handle->ipresolve = va_arg(ap, long);
    handle->saw_ipresolve = true;
    break;
#ifdef HAVE_CURLOPT_TIMEOUT_MS
  case CURLOPT_TIMEOUT_MS:
    handle->timeout_ms = va_arg(ap, long);
    handle->saw_timeout_ms = true;
    break;
#endif
  case CURLOPT_URL:
    handle->url = va_arg(ap, char *);
    handle->saw_url = true;
    break;
  default:
    test_curl.unexpected_setopt_calls++;
    va_end(ap);
    return CURLE_UNKNOWN_OPTION;
  }
  va_end(ap);

  return CURLE_OK;
}

static CURLcode test_curl_easy_perform(CURL *curl) {
  test_curl_handle_t *handle = test_curl_handle(curl);

  assert(handle != NULL);

  handle->perform_calls++;

  if (handle->perform_result != CURLE_OK) {
    if (handle->error_buffer != NULL) {
      ssnprintf(handle->error_buffer, CURL_ERROR_SIZE, "%s",
                (handle->perform_error != NULL) ? handle->perform_error
                                                : "mock perform failure");
    }
    return handle->perform_result;
  }

  if ((handle->write_function != NULL) && (handle->response_body != NULL)) {
    size_t len = strlen(handle->response_body);
    size_t written =
        handle->write_function((void *)handle->response_body, 1, len,
                               handle->write_data);
    if (written != len)
      return CURLE_WRITE_ERROR;
  }

  return CURLE_OK;
}

static CURLcode test_curl_easy_getinfo(CURL *curl, CURLINFO info, ...) {
  va_list ap;
  test_curl_handle_t *handle = test_curl_handle(curl);

  assert(handle != NULL);

  test_curl.getinfo_calls++;

  va_start(ap, info);
  switch (info) {
  case CURLINFO_EFFECTIVE_URL: {
    char **url = va_arg(ap, char **);
    *url = (handle->effective_url != NULL) ? handle->effective_url : handle->url;
  } break;
  case CURLINFO_RESPONSE_CODE: {
    long *response_code = va_arg(ap, long *);
    *response_code = handle->response_code;
  } break;
  default:
    test_curl.unexpected_getinfo_calls++;
    va_end(ap);
    return CURLE_UNKNOWN_OPTION;
  }
  va_end(ap);

  return CURLE_OK;
}

static void test_curl_easy_cleanup(CURL *curl) {
  test_curl_handle_t *handle = test_curl_handle(curl);

  assert(handle != NULL);

  handle->cleanup_calls++;
  test_curl.cleanup_calls++;
}

DEF_TEST(parse) {
  struct {
    char *json;
    char *key_path;
    derive_t want;
  } cases[] = {
      /* simple map */
      {"{\"foo\":42,\"bar\":23}", "foo", 42},
      {"{\"foo\":42,\"bar\":23}", "bar", 23},
      /* nested map */
      {"{\"a\":{\"b\":{\"c\":123}}}", "a/b/c", 123},
      {"{\"x\":{\"y\":{\"z\":789}}}", "x/*/z", 789},
      /* simple array */
      {"[10,11,12,13]", "0", 10},
      {"[10,11,12,13]", "1", 11},
      {"[10,11,12,13]", "2", 12},
      {"[10,11,12,13]", "3", 13},
      /* array index after non-numeric entry */
      {"[true,11]", "1", 11},
      {"[null,11]", "1", 11},
      {"[\"s\",11]", "1", 11},
      {"[{\"k\":\"v\"},11]", "1", 11},
      {"[[0,1,2],11]", "1", 11},
      /* nested array */
      {"[[0,1,2],[3,4,5],[6,7,8]]", "0/0", 0},
      {"[[0,1,2],[3,4,5],[6,7,8]]", "0/1", 1},
      {"[[0,1,2],[3,4,5],[6,7,8]]", "0/2", 2},
      {"[[0,1,2],[3,4,5],[6,7,8]]", "1/0", 3},
      {"[[0,1,2],[3,4,5],[6,7,8]]", "1/1", 4},
      {"[[0,1,2],[3,4,5],[6,7,8]]", "1/2", 5},
      {"[[0,1,2],[3,4,5],[6,7,8]]", "2/0", 6},
      {"[[0,1,2],[3,4,5],[6,7,8]]", "2/1", 7},
      {"[[0,1,2],[3,4,5],[6,7,8]]", "2/2", 8},
      /* testcase from #2266 */
      {"{\"a\":[[10,11,12,13,14]]}", "a/0/0", 10},
      {"{\"a\":[[10,11,12,13,14]]}", "a/0/1", 11},
      {"{\"a\":[[10,11,12,13,14]]}", "a/0/2", 12},
      {"{\"a\":[[10,11,12,13,14]]}", "a/0/3", 13},
      {"{\"a\":[[10,11,12,13,14]]}", "a/0/4", 14},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    test_prepare();

    cj_t *db = test_parse_setup(cases[i].json, cases[i].key_path);
    EXPECT_EQ_INT(cases[i].want, test_metric(cases[i].key_path));

    test_teardown(db);
  }

  return 0;
}

DEF_TEST(recreate_after_resolve_failure) {
  test_prepare();

  test_curl.specs[0] = (test_curl_spec_t){
      .perform_result = CURLE_COULDNT_RESOLVE_HOST,
      .perform_error = "mock resolve failure",
  };
  test_curl.specs[1] = (test_curl_spec_t){
      .perform_result = CURLE_OK,
      .response_body = "{\"value\":1}",
      .response_code = 200,
  };
  test_curl.specs_num = 2;

  cj_t *db = test_read_setup("value");

  EXPECT_EQ_INT(-1, test_read(db));
  EXPECT_EQ_INT(2, test_curl.init_calls);
  EXPECT_EQ_INT(1, test_curl.cleanup_calls);
  EXPECT_EQ_PTR((void *)test_handle_at(1), db->curl);

  test_curl_handle_t *initial = test_handle_at(0);
  test_curl_handle_t *replacement = test_handle_at(1);
  CHECK_NOT_NULL(initial);
  CHECK_NOT_NULL(replacement);

  EXPECT_EQ_INT(0, test_read(db));
  EXPECT_EQ_INT(1, test_metric("value"));
  EXPECT_EQ_INT(1, initial->cleanup_calls);
  EXPECT_EQ_INT(1, replacement->perform_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_init_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_setopt_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_getinfo_calls);

  test_teardown(db);
  return 0;
}

DEF_TEST(recreate_after_proxy_resolve_failure) {
  test_prepare();

  test_curl.specs[0] = (test_curl_spec_t){
      .perform_result = CURLE_COULDNT_RESOLVE_PROXY,
      .perform_error = "mock proxy resolve failure",
  };
  test_curl.specs[1] = (test_curl_spec_t){
      .perform_result = CURLE_OK,
      .response_body = "{\"value\":1}",
      .response_code = 200,
  };
  test_curl.specs_num = 2;

  cj_t *db = test_read_setup("value");

  EXPECT_EQ_INT(-1, test_read(db));
  EXPECT_EQ_INT(2, test_curl.init_calls);
  EXPECT_EQ_INT(1, test_curl.cleanup_calls);
  EXPECT_EQ_PTR((void *)test_handle_at(1), db->curl);

  test_curl_handle_t *replacement = test_handle_at(1);
  CHECK_NOT_NULL(replacement);

  EXPECT_EQ_INT(0, test_read(db));
  EXPECT_EQ_INT(1, test_metric("value"));
  EXPECT_EQ_INT(1, replacement->perform_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_init_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_setopt_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_getinfo_calls);

  test_teardown(db);
  return 0;
}

DEF_TEST(non_resolution_failure_keeps_handle) {
  test_prepare();

  test_curl.specs[0] = (test_curl_spec_t){
      .perform_result = CURLE_COULDNT_CONNECT,
      .perform_error = "mock connect failure",
  };
  test_curl.specs_num = 1;

  cj_t *db = test_read_setup("value");
  void *first_handle = db->curl;

  EXPECT_EQ_INT(-1, test_read(db));
  EXPECT_EQ_INT(1, test_curl.init_calls);
  EXPECT_EQ_INT(0, test_curl.cleanup_calls);
  EXPECT_EQ_PTR(first_handle, db->curl);
  test_curl_handle_t *initial = test_handle_at(0);
  CHECK_NOT_NULL(initial);
  EXPECT_EQ_INT(1, initial->perform_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_init_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_setopt_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_getinfo_calls);

  test_teardown(db);
  return 0;
}

DEF_TEST(recreate_failure_recovers_on_next_read) {
  test_prepare();

  test_curl.specs[0] = (test_curl_spec_t){
      .perform_result = CURLE_COULDNT_RESOLVE_HOST,
      .perform_error = "mock resolve failure",
  };
  test_curl.specs[2] = (test_curl_spec_t){
      .perform_result = CURLE_OK,
      .response_body = "{\"value\":1}",
      .response_code = 200,
  };
  test_curl.specs_num = 3;
  test_curl.fail_init_call = 2;

  cj_t *db = test_read_setup("value");

  EXPECT_EQ_INT(-1, test_read(db));
  EXPECT_EQ_INT(2, test_curl.init_calls);
  EXPECT_EQ_INT(1, test_curl.cleanup_calls);
  EXPECT_EQ_PTR(NULL, db->curl);

  test_curl.fail_init_call = 0;

  EXPECT_EQ_INT(0, test_read(db));
  EXPECT_EQ_INT(3, test_curl.init_calls);
  EXPECT_EQ_PTR((void *)test_handle_at(2), db->curl);
  test_curl_handle_t *replacement = test_handle_at(2);
  CHECK_NOT_NULL(replacement);
  EXPECT_EQ_INT(1, test_metric("value"));
  EXPECT_EQ_INT(0, test_curl.unexpected_init_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_setopt_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_getinfo_calls);

  test_teardown(db);
  return 0;
}

DEF_TEST(recreated_handle_reapplies_options) {
  test_prepare();

  test_curl.specs[0] = (test_curl_spec_t){
      .perform_result = CURLE_COULDNT_RESOLVE_HOST,
      .perform_error = "mock resolve failure",
  };
  test_curl.specs[1] = (test_curl_spec_t){
      .perform_result = CURLE_OK,
      .response_body = "{\"value\":1}",
      .response_code = 200,
  };
  test_curl.specs_num = 2;

  cj_t *db = test_read_setup("value");

  EXPECT_EQ_INT(-1, test_read(db));

  test_curl_handle_t *replacement = test_handle_at(1);
  CHECK_NOT_NULL(replacement);
  OK(replacement->saw_write_data);
  EXPECT_EQ_PTR(db, replacement->write_data);
  OK(replacement->saw_error_buffer);
  EXPECT_EQ_PTR(db->curl_errbuf, replacement->error_buffer);
  OK(replacement->saw_ipresolve);
  EXPECT_EQ_INT(CURL_IPRESOLVE_V4, replacement->ipresolve);
#ifdef HAVE_CURLOPT_TIMEOUT_MS
  OK(replacement->saw_timeout_ms);
  EXPECT_EQ_INT(1234, replacement->timeout_ms);
#endif
  EXPECT_EQ_INT(0, test_curl.unexpected_init_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_setopt_calls);
  EXPECT_EQ_INT(0, test_curl.unexpected_getinfo_calls);

  test_teardown(db);
  return 0;
}

int main(void) {
  cj_submit = test_submit;

  RUN_TEST(parse);
  RUN_TEST(recreate_after_resolve_failure);
  RUN_TEST(recreate_after_proxy_resolve_failure);
  RUN_TEST(non_resolution_failure_keeps_handle);
  RUN_TEST(recreate_failure_recovers_on_next_read);
  RUN_TEST(recreated_handle_reapplies_options);

  test_values_reset();
  test_curl_reset();

  END_TEST;
}
