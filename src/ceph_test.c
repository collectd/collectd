/**
 * collectd - src/ceph_test.c
 * Copyright (C) 2015      Florian octo Forster
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "ceph.c" /* sic */
#include "testing.h"

struct case_s {
  const char *key;
  const char *value;
};
typedef struct case_s case_t;

struct test_s {
  case_t *cases;
  size_t cases_num;
};
typedef struct test_s test_t;

static int test_handler(void *user, char const *val, char const *key) {
  test_t *t = user;
  size_t i;

  char status[1024];
  _Bool ok;

  /* special case for latency metrics. */
  if (strcmp("filestore.example_latency", key) == 0)
    return RETRY_AVGCOUNT;

  snprintf(status, sizeof(status),
           "unexpected call: test_handler(\"%s\") = \"%s\"", key, val);
  ok = 0;

  for (i = 0; i < t->cases_num; i++) {
    if (strcmp(key, t->cases[i].key) != 0)
      continue;

    if (strcmp(val, t->cases[i].value) != 0) {
      snprintf(status, sizeof(status),
               "test_handler(\"%s\") = \"%s\", want \"%s\"", key, val,
               t->cases[i].value);
      ok = 0;
      break;
    }

    snprintf(status, sizeof(status), "test_handler(\"%s\") = \"%s\"", key, val);
    ok = 1;
    break;
  }

  OK1(ok, status);
  return ok ? 0 : -1;
}

DEF_TEST(traverse_json) {
  char const *json =
      "{\n"
      "    \"WBThrottle\": {\n"
      "        \"bytes_dirtied\": {\n"
      "            \"type\": 2,\n"
      "            \"description\": \"Dirty data\",\n"
      "            \"nick\": \"\"\n"
      "        },\n"
      "        \"bytes_wb\": {\n"
      "            \"type\": 2,\n"
      "            \"description\": \"Written data\",\n"
      "            \"nick\": \"\"\n"
      "        },\n"
      "        \"ios_dirtied\": {\n"
      "            \"type\": 2,\n"
      "            \"description\": \"Dirty operations\",\n"
      "            \"nick\": \"\"\n"
      "        },\n"
      "        \"ios_wb\": {\n"
      "            \"type\": 2,\n"
      "            \"description\": \"Written operations\",\n"
      "            \"nick\": \"\"\n"
      "        },\n"
      "        \"inodes_dirtied\": {\n"
      "            \"type\": 2,\n"
      "            \"description\": \"Entries waiting for write\",\n"
      "            \"nick\": \"\"\n"
      "        },\n"
      "        \"inodes_wb\": {\n"
      "            \"type\": 10,\n"
      "            \"description\": \"Written entries\",\n"
      "            \"nick\": \"\"\n"
      "        }\n"
      "    },\n"
      "    \"filestore\": {\n"
      "        \"journal_wr_bytes\": {\n"
      "            \"avgcount\": 23,\n"
      "            \"sum\": 3117\n"
      "        },\n"
      "        \"example_latency\": {\n"
      "            \"avgcount\": 42,\n"
      "            \"sum\": 4711\n"
      "        }\n"
      "    }\n"
      "}\n";
  case_t cases[] = {
      {"WBThrottle.bytes_dirtied.type", "2"},
      {"WBThrottle.bytes_wb.type", "2"},
      {"WBThrottle.ios_dirtied.type", "2"},
      {"WBThrottle.ios_wb.type", "2"},
      {"WBThrottle.inodes_dirtied.type", "2"},
      {"WBThrottle.inodes_wb.type", "10"},
      {"filestore.journal_wr_bytes", "3117"},
      {"filestore.example_latency.avgcount", "42"},
      {"filestore.example_latency.sum", "4711"},
  };
  test_t t = {cases, STATIC_ARRAY_SIZE(cases)};

  yajl_struct ctx = {test_handler, &t};

  yajl_handle hndl;
#if HAVE_YAJL_V2
  hndl = yajl_alloc(&callbacks, NULL, &ctx);
  CHECK_ZERO(
      traverse_json((const unsigned char *)json, (uint32_t)strlen(json), hndl));
  CHECK_ZERO(yajl_complete_parse(hndl));
#else
  hndl = yajl_alloc(&callbacks, NULL, NULL, &ctx);
  CHECK_ZERO(
      traverse_json((const unsigned char *)json, (uint32_t)strlen(json), hndl));
  CHECK_ZERO(yajl_parse_complete(hndl));
#endif

  yajl_free(hndl);
  return 0;
}

DEF_TEST(parse_keys) {
  struct {
    const char *str;
    const char *want;
  } cases[] = {
      {"WBThrottle.bytes_dirtied.description.bytes_wb.description.ios_dirtied."
       "description.ios_wb.type",
       "WBThrottle.bytesDirtied.description.bytesWb.description.iosDirt"},
      {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa:"
       "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb",
       "Aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"},
      {"foo:bar", "FooBar"},
      {"foo:bar+", "FooBarPlus"},
      {"foo:bar-", "FooBarMinus"},
      {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa+",
       "AaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaPlus"},
      {"aa.bb.cc.dd.ee.ff", "Aa.bb.cc.dd.ee.ff"},
      {"aa.bb.cc.dd.ee.ff.type", "Aa.bb.cc.dd.ee.ff"},
      {"aa.type", "Aa.type"},
      {"WBThrottle.bytes_dirtied.type", "WBThrottle.bytesDirtied"},
  };
  size_t i;

  for (i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    char got[64];

    CHECK_ZERO(parse_keys(got, sizeof(got), cases[i].str));
    EXPECT_EQ_STR(cases[i].want, got);
  }

  return 0;
}

int main(void) {
  RUN_TEST(traverse_json);
  RUN_TEST(parse_keys);

  END_TEST;
}

/* vim: set sw=2 sts=2 et : */
