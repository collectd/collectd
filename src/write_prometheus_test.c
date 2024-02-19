/**
 * collectd - src/write_prometheus_test.c
 * Copyright (C) 2023       Florian octo Forster
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

#include "daemon/metric.h"
#include "testing.h"
#include "utils/common/common.h"

int format_label_name(strbuf_t *buf, char const *name);

DEF_TEST(format_label_name) {
  // Test cases are based on:
  // https://github.com/open-telemetry/opentelemetry-collector-contrib/blob/main/pkg/translator/prometheus/README.md
  struct {
    char *name;
    char *want;
  } cases[] = {
      {"name", "name"},           {"host.name", "host_name"},
      {"host_name", "host_name"}, {"name (of the host)", "name__of_the_host_"},
      {"2 cents", "key_2_cents"}, {"__name", "__name"},
      {"_name", "key_name"},      {"(name)", "key_name_"},
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("# Case %zu: %s\n", i, cases[i].name);
    strbuf_t got = STRBUF_CREATE;

    EXPECT_EQ_INT(0, format_label_name(&got, cases[i].name));
    EXPECT_EQ_STR(cases[i].want, got.ptr);

    STRBUF_DESTROY(got);
  }

  return 0;
}
void format_metric_family_name(strbuf_t *buf, metric_family_t const *fam);

DEF_TEST(format_metric_family_name) {
  // Test cases are based on:
  // https://github.com/open-telemetry/opentelemetry-collector-contrib/blob/main/pkg/translator/prometheus/README.md
  struct {
    char *name;
    metric_type_t type;
    char *unit;
    char *want;
  } cases[] = {
      {
          .name = "(lambda).function.executions(#)",
          .type = METRIC_TYPE_UNTYPED,
          .want = "lambda_function_executions",
      },
      {
          .name = "system.processes.created",
          .type = METRIC_TYPE_COUNTER,
          .want = "system_processes_created_total",
      },
      {
          .name = "system.filesystem.usage",
          .type = METRIC_TYPE_GAUGE,
          .unit = "By",
          .want = "system_filesystem_usage_bytes",
      },
      {
          .name = "system.network.dropped",
          .type = METRIC_TYPE_GAUGE,
          .unit = "{packets}",
          .want = "system_network_dropped",
      },
      {
          .name = "system.network.dropped",
          .type = METRIC_TYPE_GAUGE,
          .unit = "packets",
          .want = "system_network_dropped_packets",
      },
      {
          .name = "system.memory.utilization",
          .type = METRIC_TYPE_GAUGE,
          .unit = "1",
          .want = "system_memory_utilization_ratio",
      },
      {
          .name = "storage.filesystem.utilization",
          .type = METRIC_TYPE_GAUGE,
          .unit = "%",
          .want = "storage_filesystem_utilization_percent",
      },
      {
          .name = "astro.light.speed",
          .type = METRIC_TYPE_GAUGE,
          .unit = "m/s",
          .want = "astro_light_speed_m_s",
          /* Not yet supported. Should be:
          .want = "astro_light_speed_meters_per_second",
          */
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("# Case %zu: %s\n", i, cases[i].name);
    strbuf_t got = STRBUF_CREATE;

    metric_family_t fam = {
        .name = cases[i].name,
        .type = cases[i].type,
        .unit = cases[i].unit,
    };

    format_metric_family_name(&got, &fam);
    EXPECT_EQ_STR(cases[i].want, got.ptr);

    STRBUF_DESTROY(got);
  }

  return 0;
}

void format_metric_family(strbuf_t *buf, metric_family_t const *prom_fam);

DEF_TEST(format_metric_family) {
  struct {
    char const *name;
    metric_family_t fam;
    char const *want;
  } cases[] = {
      {
          .name = "metrics is empty",
          .fam =
              {
                  .name = "unit.test",
              },
          .want = NULL,
      },
      {
          .name = "metric without labels",
          .fam =
              {
                  .name = "unit.test",
                  .type = METRIC_TYPE_COUNTER,
                  .metric =
                      {
                          .ptr =
                              &(metric_t){
                                  .value =
                                      (value_t){
                                          .counter = 42,
                                      },
                              },
                          .num = 1,
                      },
              },
          .want = "# HELP unit_test_total\n"
                  "# TYPE unit_test_total counter\n"
                  "unit_test_total 42\n"
                  "\n",
      },
      {
          .name = "metric with one label",
          .fam =
              {
                  .name = "unittest",
                  .type = METRIC_TYPE_GAUGE,
                  .metric =
                      {
                          .ptr =
                              &(metric_t){
                                  .label =
                                      {
                                          .ptr =
                                              &(label_pair_t){
                                                  .name = "foo",
                                                  .value = "bar",
                                              },
                                          .num = 1,
                                      },
                                  .value =
                                      (value_t){
                                          .gauge = 42,
                                      },
                              },
                          .num = 1,
                      },
              },
          .want = "# HELP unittest\n"
                  "# TYPE unittest gauge\n"
                  "unittest{foo=\"bar\"} 42\n"
                  "\n",
      },
      {
          .name = "invalid characters are replaced",
          .fam =
              {
                  .name = "unit.test",
                  .type = METRIC_TYPE_UNTYPED,
                  .metric =
                      {
                          .ptr =
                              &(metric_t){
                                  .label =
                                      {
                                          .ptr =
                                              &(label_pair_t){
                                                  .name = "metric.name",
                                                  .value = "unit.test",
                                              },
                                          .num = 1,
                                      },
                                  .value =
                                      (value_t){
                                          .gauge = 42,
                                      },
                              },
                          .num = 1,
                      },
              },
          .want = "# HELP unit_test\n"
                  "# TYPE unit_test untyped\n"
                  "unit_test{metric_name=\"unit.test\"} 42\n"
                  "\n",
      },
      {
          .name = "most resource attributes are ignored",
          .fam =
              {
                  .name = "unit.test",
                  .type = METRIC_TYPE_UNTYPED,
                  .resource =
                      {
                          .ptr =
                              (label_pair_t[]){
                                  {"service.instance.id",
                                   "service instance id"},
                                  {"service.name", "service name"},
                                  {"zzz.all.other.attributes", "are ignored"},
                              },
                          .num = 3,
                      },
                  .metric =
                      {
                          .ptr =
                              &(metric_t){
                                  .label =
                                      {
                                          .ptr =
                                              (label_pair_t[]){
                                                  {"metric.name", "unit.test"},
                                              },
                                          .num = 1,
                                      },
                                  .value =
                                      (value_t){
                                          .gauge = 42,
                                      },
                              },
                          .num = 1,
                      },
              },
          .want = "# HELP unit_test\n"
                  "# TYPE unit_test untyped\n"
                  "unit_test{job=\"service name\",instance=\"service instance "
                  "id\",metric_name=\"unit.test\"} 42\n"
                  "\n",
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("# Case %zu: %s\n", i, cases[i].name);
    strbuf_t got = STRBUF_CREATE;

    metric_family_t *fam = &cases[i].fam;
    for (size_t j = 0; j < fam->metric.num; j++) {
      fam->metric.ptr[j].family = fam;
    }

    format_metric_family(&got, &cases[i].fam);
    EXPECT_EQ_STR(cases[i].want, got.ptr);

    STRBUF_DESTROY(got);
  }

  return 0;
}

void target_info(strbuf_t *buf, metric_family_t const **families,
                 size_t families_num);

DEF_TEST(target_info) {
  struct {
    char const *name;
    label_set_t *resources;
    size_t resources_num;
    char const *want;
  } cases[] = {
      {
          .name = "single resource attribute",
          .resources =
              (label_set_t[]){
                  {
                      .ptr = &(label_pair_t){"foo", "bar"},
                      .num = 1,
                  },
              },
          .resources_num = 1,
          .want = "# HELP target_info Target metadata\n"
                  "# TYPE target_info gauge\n"
                  "target_info{foo=\"bar\"} 1\n\n",
      },
      {
          .name = "identical resources get deduplicated",
          .resources =
              (label_set_t[]){
                  {
                      .ptr = &(label_pair_t){"foo", "bar"},
                      .num = 1,
                  },
                  {
                      .ptr = &(label_pair_t){"foo", "bar"},
                      .num = 1,
                  },
              },
          .resources_num = 2,
          .want = "# HELP target_info Target metadata\n"
                  "# TYPE target_info gauge\n"
                  "target_info{foo=\"bar\"} 1\n\n",
      },
      {
          .name = "service.name gets translated to job",
          .resources =
              (label_set_t[]){
                  {
                      .ptr = &(label_pair_t){"service.name", "unittest"},
                      .num = 1,
                  },
              },
          .resources_num = 1,
          .want = "# HELP target_info Target metadata\n"
                  "# TYPE target_info gauge\n"
                  "target_info{job=\"unittest\"} 1\n\n",
      },
      {
          .name = "service.instance.id gets translated to instance",
          .resources =
              (label_set_t[]){
                  {
                      .ptr = &(label_pair_t){"service.instance.id", "42"},
                      .num = 1,
                  },
              },
          .resources_num = 1,
          .want = "# HELP target_info Target metadata\n"
                  "# TYPE target_info gauge\n"
                  "target_info{instance=\"42\"} 1\n\n",
      },
      {
          .name = "multiple resources",
          .resources =
              (label_set_t[]){
                  {
                      .ptr =
                          (label_pair_t[]){
                              {"additional", "label"},
                              {"service.instance.id", "id:0"},
                              {"service.name", "unit.test"},
                          },
                      .num = 3,
                  },
                  {
                      .ptr =
                          (label_pair_t[]){
                              {"(additional)", "\"label\""},
                              {"service.instance.id", "id:1"},
                              {"service.name", "unit.test"},
                          },
                      .num = 3,
                  },
                  {
                      .ptr =
                          (label_pair_t[]){
                              {"42 additional", "label\n"},
                              {"service.instance.id", "id:2"},
                              {"service.name", "unit.test"},
                          },
                      .num = 3,
                  },
              },
          .resources_num = 3,
          // clang-format off
          .want =
"# HELP target_info Target metadata\n"
"# TYPE target_info gauge\n"
"target_info{job=\"unit.test\",instance=\"id:1\",key_additional_=\"\\\"label\\\"\"} 1\n"
"target_info{job=\"unit.test\",instance=\"id:2\",key_42_additional=\"label\\n\"} 1\n"
"target_info{job=\"unit.test\",instance=\"id:0\",additional=\"label\"} 1\n"
"\n",
          // clang-format on
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("# Case %zu: %s\n", i, cases[i].name);

    metric_family_t families[cases[i].resources_num];
    metric_family_t const *family_ptrs[cases[i].resources_num];
    for (size_t j = 0; j < cases[i].resources_num; j++) {
      families[j].resource = cases[i].resources[j];
      family_ptrs[j] = &families[j];
    }

    strbuf_t got = STRBUF_CREATE;
    target_info(&got, family_ptrs, cases[i].resources_num);
    EXPECT_EQ_STR(cases[i].want, got.ptr);

    STRBUF_DESTROY(got);
  }

  return 0;
}

void format_text(strbuf_t *buf);
int prom_write(metric_family_t const *fam, user_data_t *ud);
int alloc_metrics(void);
void free_metrics(void);

DEF_TEST(end_to_end) {
  hostname_set("example.com");

  struct {
    char const *name;
    metric_family_t *fams;
    size_t fams_num;
    char const *want;
  } cases[] = {
      {
          .name = "single metric",
          .fams =
              &(metric_family_t){
                  .name = "unit.test",
                  .type = METRIC_TYPE_COUNTER,
                  .resource =
                      {
                          .ptr =
                              (label_pair_t[]){
                                  {"host.name", "example.org"},
                                  {"service.instance.id", "instance1"},
                                  {"service.name", "name1"},
                              },
                          .num = 3,
                      },
                  .metric =
                      {
                          .ptr =
                              &(metric_t){
                                  .value.counter = 42,
                              },
                          .num = 1,
                      },
              },
          .fams_num = 1,
// clang-format off
          .want =
            "# HELP target_info Target metadata\n"
	    "# TYPE target_info gauge\n"
	    "target_info{job=\"name1\",instance=\"instance1\",host_name=\"example.org\"} 1\n"
	    "\n"
	    "# HELP unit_test_total\n"
	    "# TYPE unit_test_total counter\n"
	    "unit_test_total{job=\"name1\",instance=\"instance1\"} 42\n"
	    "\n"
	    "# collectd/write_prometheus " PACKAGE_VERSION
	    " at example.com\n",
// clang-format on
      },
      {
          .name = "multiple resources",
          .fams =
              (metric_family_t[]){
                  {
                      .name = "unit.test",
                      .type = METRIC_TYPE_COUNTER,
                      .resource =
                          {
                              .ptr =
                                  (label_pair_t[]){
                                      {"host.name", "example.org"},
                                      {"service.instance.id", "instance1"},
                                      {"service.name", "name1"},
                                  },
                              .num = 3,
                          },
                      .metric =
                          {
                              .ptr =
                                  &(metric_t){
                                      .value.counter = 42,
                                  },
                              .num = 1,
                          },
                  },
                  {
                      .name = "unit.test",
                      .type = METRIC_TYPE_COUNTER,
                      .resource =
                          {
                              .ptr =
                                  (label_pair_t[]){
                                      {"host.name", "example.net"},
                                      {"service.instance.id", "instance2"},
                                      {"service.name", "name1"},
                                  },
                              .num = 3,
                          },
                      .metric =
                          {
                              .ptr =
                                  &(metric_t){
                                      .value.counter = 23,
                                  },
                              .num = 1,
                          },
                  },
              },
          .fams_num = 1,
          // clang-format off
          .want =
            "# HELP target_info Target metadata\n"
	    "# TYPE target_info gauge\n"
	    "target_info{job=\"name1\",instance=\"instance1\",host_name=\"example.org\"} 1\n"
	    "target_info{job=\"name1\",instance=\"instance2\",host_name=\"example.net\"} 1\n"
	    "\n"
	    "# HELP unit_test_total\n"
	    "# TYPE unit_test_total counter\n"
	    "unit_test_total{job=\"name1\",instance=\"instance1\"} 42\n"
	    "unit_test_total{job=\"name1\",instance=\"instance2\"} 23\n"
	    "\n"
	    "# collectd/write_prometheus " PACKAGE_VERSION " at example.com\n",
          // clang-format on
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("# Case %zu: %s\n", i, cases[i].name);

    CHECK_ZERO(alloc_metrics());

    for (size_t j = 0; j < cases[i].fams_num; j++) {
      CHECK_ZERO(prom_write(cases[i].fams + j, NULL));
    }

    strbuf_t got = STRBUF_CREATE;
    format_text(&got);

    EXPECT_EQ_STR(cases[i].want, got.ptr);

    STRBUF_DESTROY(got);
    free_metrics();
  }

  return 0;
}

int main(void) {
  RUN_TEST(format_label_name);
  RUN_TEST(format_metric_family_name);
  RUN_TEST(format_metric_family);
  RUN_TEST(target_info);
  RUN_TEST(end_to_end);

  END_TEST;
}
