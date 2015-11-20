/**
 * collectd - src/utils_format_json_test.c
 * Copyright (C) 2015       Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 */

#include "testing.h"
#include "collectd.h"
#include "utils_format_json.h"
#include "common.h" /* for STATIC_ARRAY_SIZE */

#include <yajl/yajl_parse.h>

struct label_s
{
  char *key;
  char *value;
};
typedef struct label_s label_t;

struct test_case_s
{
  label_t *expected_labels;
  size_t   expected_labels_num;

  label_t *current_label;
};
typedef struct test_case_s test_case_t;

static int test_map_key (void *ctx, unsigned char const *key, size_t key_len)
{
  test_case_t *c = ctx;
  size_t i;

  c->current_label = NULL;
  for (i = 0; i < c->expected_labels_num; i++)
  {
    label_t *l = c->expected_labels + i;
    if ((strlen (l->key) == key_len)
        && (strncmp (l->key, (char const *) key, key_len) == 0))
    {
      c->current_label = l;
      break;
    }
  }

  return 1; /* continue */
}

static int expect_label (char const *name, char const *got, char const *want)
{
  _Bool ok = (strcmp (got, want) == 0);
  char msg[1024];

  if (ok)
    snprintf (msg, sizeof (msg), "label[\"%s\"] = \"%s\"", name, got);
  else
    snprintf (msg, sizeof (msg), "label[\"%s\"] = \"%s\", want \"%s\"", name, got, want);

  OK1 (ok, msg);
  return 0;
}

static int test_string (void *ctx, unsigned char const *value, size_t value_len)
{
  test_case_t *c = ctx;

  if (c->current_label != NULL)
  {
    label_t *l = c->current_label;
    char *got;
    int status;

    got = malloc (value_len + 1);
    memmove (got, value, value_len);
    got[value_len] = 0;

    status = expect_label (l->key, got, l->value);

    free (got);

    if (status != 0)
      return 0; /* abort */
  }

  return 1; /* continue */
}

static int expect_json_labels (char *json, label_t *labels, size_t labels_num)
{
  yajl_callbacks funcs = {
    .yajl_string = test_string,
    .yajl_map_key = test_map_key,
  };

  test_case_t c = { labels, labels_num, NULL };

  yajl_handle hndl;
  CHECK_NOT_NULL (hndl = yajl_alloc (&funcs, NULL, &c));
  OK (yajl_parse (hndl, (unsigned char *) json, strlen (json)) == yajl_status_ok);

  yajl_free (hndl);
  return 0;
}

DEF_TEST(notification)
{
  label_t labels[] = {
    {"summary", "this is a message"},
    {"alertname", "collectd_unit_test"},
    {"instance", "example.com"},
    {"service", "collectd"},
    {"unit", "case"},
  };

  /* 1448284606.125 ^= 1555083754651779072 */
  notification_t n = { NOTIF_WARNING, 1555083754651779072, "this is a message",
    "example.com", "unit", "", "test", "case", NULL };

  char got[1024];
  CHECK_ZERO (format_json_notification (got, sizeof (got), &n));
  // printf ("got = \"%s\";\n", got);

  return expect_json_labels (got, labels, STATIC_ARRAY_SIZE (labels));
}

int main (void)
{
  RUN_TEST(notification);

  END_TEST;
}
