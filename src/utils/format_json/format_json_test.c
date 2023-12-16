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

#include "collectd.h"

#include "testing.h"
#include "utils/common/common.h" /* for STATIC_ARRAY_SIZE */
#include "utils/format_json/format_json.h"

#include <yajl/yajl_common.h>
#include <yajl/yajl_parse.h>
#if HAVE_YAJL_YAJL_VERSION_H
#include <yajl/yajl_version.h>
#endif
#if YAJL_MAJOR > 1
#define HAVE_YAJL_V2 1
#endif

typedef struct {
  label_pair_t *expected_labels;
  size_t expected_labels_num;

  label_pair_t *current_label;
} test_case_t;

#if HAVE_YAJL_V2
static int test_map_key(void *ctx, unsigned char const *key, size_t key_len)
#else
static int test_map_key(void *ctx, unsigned char const *key,
                        unsigned int key_len)
#endif
{
  test_case_t *c = ctx;
  size_t i;

  c->current_label = NULL;
  for (i = 0; i < c->expected_labels_num; i++) {
    label_pair_t *l = c->expected_labels + i;
    if ((strlen(l->name) == key_len) &&
        (strncmp(l->name, (char const *)key, key_len) == 0)) {
      c->current_label = l;
      break;
    }
  }

  return 1; /* continue */
}

static int expect_label(char const *name, char const *got, char const *want) {
  bool ok = (strcmp(got, want) == 0);
  char msg[1024];

  if (ok)
    snprintf(msg, sizeof(msg), "label[\"%s\"] = \"%s\"", name, got);
  else
    snprintf(msg, sizeof(msg), "label[\"%s\"] = \"%s\", want \"%s\"", name, got,
             want);

  OK1(ok, msg);
  return 0;
}

#if HAVE_YAJL_V2
static int test_string(void *ctx, unsigned char const *value, size_t value_len)
#else
static int test_string(void *ctx, unsigned char const *value,
                       unsigned int value_len)
#endif
{
  test_case_t *c = ctx;

  if (c->current_label != NULL) {
    label_pair_t *l = c->current_label;
    char *got;
    int status;

    got = malloc(value_len + 1);
    memmove(got, value, value_len);
    got[value_len] = 0;

    status = expect_label(l->name, got, l->value);

    free(got);

    if (status != 0)
      return 0; /* abort */
  }

  return 1; /* continue */
}

static int expect_json_labels(char *json, label_pair_t *labels,
                              size_t labels_num) {
  yajl_callbacks funcs = {
      .yajl_string = test_string,
      .yajl_map_key = test_map_key,
  };

  test_case_t c = {labels, labels_num, NULL};

  yajl_handle hndl;
#if HAVE_YAJL_V2
  CHECK_NOT_NULL(hndl = yajl_alloc(&funcs, /* alloc = */ NULL, &c));
#else
  CHECK_NOT_NULL(
      hndl = yajl_alloc(&funcs, /* config = */ NULL, /* alloc = */ NULL, &c));
#endif
  OK(yajl_parse(hndl, (unsigned char *)json, strlen(json)) == yajl_status_ok);

  yajl_free(hndl);
  return 0;
}

DEF_TEST(notification) {
  label_pair_t labels[] = {
      {"summary", "this is a message"},
      {"alertname", "collectd_unit_test"},
      {"instance", "example.com"},
      {"service", "collectd"},
      {"unit", "case"},
  };

  /* 1448284606.125 ^= 1555083754651779072 */
  notification_t n = {NOTIF_WARNING,
                      1555083754651779072ULL,
                      "this is a message",
                      "example.com",
                      "unit",
                      "",
                      "test",
                      "case",
                      NULL};

  char got[1024];
  CHECK_ZERO(format_json_notification(got, sizeof(got), &n));
  // printf ("got = \"%s\";\n", got);

  return expect_json_labels(got, labels, STATIC_ARRAY_SIZE(labels));
}

DEF_TEST(metric_family) {
  struct {
    char const *identity;
    metric_type_t type;
    value_t value;
    cdtime_t time;
    cdtime_t interval;
    char const *want;
  } cases[] = {
    {
        .identity = "metric_name",
        .value.gauge = 42,
        .type = METRIC_TYPE_GAUGE,
        .want = "[{\"name\":\"metric_name\",\"type\":\"GAUGE\",\"metrics\":["
                "{\"value\":\"42\"}"
                "]}]",
    },
    {
        .identity =
            "metric_with_labels{sorted=\"true\",alphabetically=\"yes\"}",
        .value.gauge = 42,
        .type = METRIC_TYPE_GAUGE,
        .want = "[{\"name\":\"metric_with_labels\",\"type\":\"GAUGE\","
                "\"metrics\":["
                "{\"labels\":{\"alphabetically\":\"yes\",\"sorted\":\"true\"}"
                ",\"value\":\"42\"}"
                "]}]",
    },
    {
        .identity = "metric_with_time",
        .value.gauge = 42,
        .type = METRIC_TYPE_GAUGE,
        .time = MS_TO_CDTIME_T(1592987324125),
        .want =
            "[{\"name\":\"metric_with_time\",\"type\":\"GAUGE\",\"metrics\":["
            "{\"timestamp_ms\":\"1592987324125\",\"value\":\"42\"}"
            "]}]",
    },
#if 0
      {
          .identity = "derive_max",
          .value.derive = INT64_MAX,
          .type = METRIC_TYPE_COUNTER,
          .want = "[{\"name\":\"derive_max\",\"type\":\"COUNTER\",\"metrics\":["
                  "{\"value\":\"9223372036854775807\"}"
                  "]}]",
      },
      {
          .identity = "derive_min",
          .value = (value_t){.derive = INT64_MIN},
          .type = METRIC_TYPE_COUNTER,
          .want = "[{\"name\":\"derive_min\",\"type\":\"COUNTER\",\"metrics\":["
                  "{\"value\":\"-9223372036854775808\"}"
                  "]}]",
      },
#endif
    {
        .identity = "counter_max",
        .value = (value_t){.counter = UINT64_MAX},
        .type = METRIC_TYPE_COUNTER,
        .want = "[{\"name\":\"counter_max\",\"type\":\"COUNTER\",\"metrics\":["
                "{\"value\":\"18446744073709551615\"}"
                "]}]",
    },
  };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
    metric_t *m = NULL;
    CHECK_NOT_NULL(m = metric_parse_identity(cases[i].identity));

    m->family->type = cases[i].type;
    m->value = cases[i].value;
    m->time = cases[i].time;
    m->interval = cases[i].interval;

    strbuf_t buf = STRBUF_CREATE;
    CHECK_ZERO(format_json_metric_family(&buf, m->family, false));

    EXPECT_EQ_STR(cases[i].want, buf.ptr);
    STRBUF_DESTROY(buf);

    metric_family_free(m->family);
  }

  return 0;
}

DEF_TEST(metric_family_append) {
  strbuf_t buf = STRBUF_CREATE;

  metric_family_t fam = {
      .name = "first",
      .type = METRIC_TYPE_UNTYPED,
  };
  metric_family_metric_append(&fam, (metric_t){
                                        .value.gauge = 0,
                                    });
  metric_family_metric_append(&fam, (metric_t){
                                        .value.gauge = 1,
                                    });
  CHECK_ZERO(format_json_metric_family(&buf, &fam, false));
  EXPECT_EQ_STR("[{\"name\":\"first\",\"type\":\"UNTYPED\",\"metrics\":[{"
                "\"value\":\"0\"},{\"value\":\"1\"}]}]",
                buf.ptr);

  metric_family_metric_reset(&fam);

  fam = (metric_family_t){
      .name = "second",
      .type = METRIC_TYPE_GAUGE,
  };
  metric_family_metric_append(&fam, (metric_t){
                                        .value.gauge = 2,
                                    });

  CHECK_ZERO(format_json_metric_family(&buf, &fam, false));
  EXPECT_EQ_STR("[{\"name\":\"first\",\"type\":\"UNTYPED\",\"metrics\":[{"
                "\"value\":\"0\"},{\"value\":\"1\"}]},{\"name\":\"second\","
                "\"type\":\"GAUGE\",\"metrics\":[{\"value\":\"2\"}]}]",
                buf.ptr);

  metric_family_metric_reset(&fam);
  STRBUF_DESTROY(buf);

  return 0;
}

DEF_TEST(open_telemetry) {
  metric_family_t *fams[2] = {
      &(metric_family_t){
          .name = "unit.tests",
          .help = "Example gauge metric",
          .type = METRIC_TYPE_GAUGE,
      },
      &(metric_family_t){
          .name = "unit.test.count",
          .help = "Example counter metric",
          .type = METRIC_TYPE_COUNTER,
      },
  };

  metric_family_resource_attribute_update(fams[0], "service.name", "unit test");
  metric_family_resource_attribute_update(fams[1], "service.name", "unit test");

  CHECK_ZERO(metric_family_append(fams[0], "metric.label", "test label",
                                  (value_t){.gauge = 42}, NULL));
  CHECK_ZERO(metric_family_append(fams[1], "metric.label", "bar",
                                  (value_t){.counter = 31337}, NULL));

  resource_metrics_set_t set = {0};
  CHECK_ZERO(resource_metrics_add(&set, fams[0]));
  CHECK_ZERO(resource_metrics_add(&set, fams[1]));

  strbuf_t buf = STRBUF_CREATE;
  CHECK_ZERO(format_json_open_telemetry(&buf, &set));

  EXPECT_EQ_STR(
      "{\"resourceMetrics\":[{\"resource\":{\"attributes\":[{\"key\":\"service."
      "name\",\"value\":{\"stringValue\":\"unit "
      "test\"}}]},\"scopeMetrics\":[{\"scope\":{\"name\":\"collectd\","
      "\"version\":\"" PACKAGE_VERSION "\"},\"metrics\":[{\"name\":\"unit."
      "test.count\",\"description\":\"Example counter "
      "metric\",\"sum\":{\"dataPoints\":[{\"attributes\":[{\"key\":\"metric."
      "label\",\"value\":{\"stringValue\":\"bar\"}}],\"timeUnixNano\":0,"
      "\"asInt\":31337}],\"aggregationTemporality\":\"2\",\"isMonotonic\":true}"
      "},{\"name\":\"unit.tests\",\"description\":\"Example gauge "
      "metric\",\"gauge\":{\"dataPoints\":[{\"attributes\":[{\"key\":\"metric."
      "label\",\"value\":{\"stringValue\":\"test "
      "label\"}}],\"timeUnixNano\":0,\"asDouble\":42.0}]}}]}]}]}",
      buf.ptr);

  STRBUF_DESTROY(buf);
  resource_metrics_reset(&set);
  label_set_reset(&fams[0]->resource);
  label_set_reset(&fams[1]->resource);
  metric_family_metric_reset(fams[0]);
  metric_family_metric_reset(fams[1]);

  return 0;
}

int main(void) {
  RUN_TEST(notification);
  RUN_TEST(metric_family);
  RUN_TEST(metric_family_append);
  RUN_TEST(open_telemetry);

  END_TEST;
}
