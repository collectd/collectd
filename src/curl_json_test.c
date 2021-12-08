/**
 * collectd - src/curl_json.c
 * Copyright (C) 2017  Florian octo Forster
 *
 * Licensed under the same terms and conditions as src/curl_json.c.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "curl_json.c"

#include "testing.h"

static void test_submit(cj_t *db, cj_key_t *key, value_t *value) {
  /* hack: we repurpose db->curl to store received values. */
  c_avl_tree_t *values = (void *)db->curl;

  value_t *value_copy = calloc(1, sizeof(*value_copy));
  memmove(value_copy, value, sizeof(*value_copy));

  assert(c_avl_insert(values, key->path, value_copy) == 0);
}

static derive_t test_metric(cj_t *db, char const *path) {
  c_avl_tree_t *values = (void *)db->curl;

  value_t *ret = NULL;
  if (c_avl_get(values, path, (void *)&ret) == 0) {
    return ret->derive;
  }

  return -1;
}

static cj_t *test_setup(char *json, char *key_path) {
  cj_t *db = calloc(1, sizeof(*db));
  db->yajl = yajl_alloc(&ycallbacks,
#if HAVE_YAJL_V2
                        /* alloc funcs = */ NULL,
#else
                        /* alloc funcs = */ NULL, NULL,
#endif
                        /* context = */ (void *)db);

  /* hack; see above. */
  db->curl = (void *)cj_avl_create();

  cj_key_t *key = calloc(1, sizeof(*key));
  key->path = strdup(key_path);
  key->type = strdup("MAGIC");

  assert(cj_append_key(db, key) == 0);

  cj_tree_entry_t root = {0};
  root.type = TREE;
  root.tree = db->tree;
  db->state[0].entry = &root;

  cj_curl_callback(json, strlen(json), 1, db);
#if HAVE_YAJL_V2
  yajl_complete_parse(db->yajl);
#else
  yajl_parse_complete(db->yajl);
#endif

  db->state[0].entry = NULL;

  return db;
}

static void test_teardown(cj_t *db) {
  c_avl_tree_t *values = (void *)db->curl;
  db->curl = NULL;

  void *key;
  void *value;
  while (c_avl_pick(values, &key, &value) == 0) {
    /* key will be freed by cj_free. */
    free(value);
  }
  c_avl_destroy(values);

  yajl_free(db->yajl);
  db->yajl = NULL;

  cj_free(db);
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
      {"{\"a\":{\"b\":{\"c\":123}}", "a/b/c", 123},
      {"{\"x\":{\"y\":{\"z\":789}}", "x/*/z", 789},
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
    cj_t *db = test_setup(cases[i].json, cases[i].key_path);

    EXPECT_EQ_INT(cases[i].want, test_metric(db, cases[i].key_path));

    test_teardown(db);
  }

  return 0;
}

int main(void) {
  cj_submit = test_submit;

  RUN_TEST(parse);

  END_TEST;
}
