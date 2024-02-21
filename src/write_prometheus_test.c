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

#include "write_prometheus.c" /* sic */

#include "testing.h"

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

    prometheus_metric_family_t pfam = {
        .name = cases[i].name,
        .type = cases[i].type,
        .unit = cases[i].unit,
    };

    format_metric_family_name(&got, &pfam);
    EXPECT_EQ_STR(cases[i].want, got.ptr);

    STRBUF_DESTROY(got);
  }

  return 0;
}

DEF_TEST(format_metric_family) {
  hostname_set("example.com");

  struct {
    char const *name;
    prometheus_metric_family_t pfam;
    char const *want;
  } cases[] = {
      {
          .name = "metrics is empty",
          .pfam =
              {
                  .name = "unit.test",
              },
          .want = NULL,
      },
      {
          .name = "metric without labels",
          .pfam =
              {
                  .name = "unit.test",
                  .type = METRIC_TYPE_COUNTER,
                  .metrics =
                      &(prometheus_metric_t){
                          .value =
                              (value_t){
                                  .counter = 42,
                              },
                      },
                  .metrics_num = 1,
              },
          .want = "# HELP unit_test_total\n"
                  "# TYPE unit_test_total counter\n"
                  "unit_test_total{job=\"example.com\",instance=\"\"} 42\n"
                  "\n",
      },
      {
          .name = "metric with one label",
          .pfam =
              {
                  .name = "unittest",
                  .type = METRIC_TYPE_GAUGE,
                  .metrics =
                      &(prometheus_metric_t){
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
                  .metrics_num = 1,
              },
          .want = "# HELP unittest\n"
                  "# TYPE unittest gauge\n"
                  "unittest{job=\"example.com\",instance=\"\",foo=\"bar\"} 42\n"
                  "\n",
      },
      {
          .name = "invalid characters are replaced",
          .pfam =
              {
                  .name = "unit.test",
                  .type = METRIC_TYPE_UNTYPED,
                  .metrics =
                      &(prometheus_metric_t){
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
                  .metrics_num = 1,
              },
          .want = "# HELP unit_test\n"
                  "# TYPE unit_test untyped\n"
                  "unit_test{job=\"example.com\",instance=\"\",metric_name="
                  "\"unit.test\"} 42\n"
                  "\n",
      },
      {
          .name = "most resource attributes are ignored",
          .pfam =
              {
                  .name = "unit.test",
                  .type = METRIC_TYPE_UNTYPED,
                  .metrics =
                      &(prometheus_metric_t){
                          .resource =
                              {
                                  .ptr =
                                      (label_pair_t[]){
                                          {"service.instance.id",
                                           "service instance id"},
                                          {"service.name", "service name"},
                                          {"zzz.all.other.attributes",
                                           "are ignored"},
                                      },
                                  .num = 3,
                              },
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
                  .metrics_num = 1,
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
    format_metric_family(&got, &cases[i].pfam);
    EXPECT_EQ_STR(cases[i].want, got.ptr);

    STRBUF_DESTROY(got);
  }

  return 0;
}

DEF_TEST(target_info) {
  hostname_set("example.com");

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
                  "target_info{job=\"example.com\",instance=\"\",foo=\"bar\"} "
                  "1\n\n",
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
                  "target_info{job=\"example.com\",instance=\"\",foo=\"bar\"} "
                  "1\n\n",
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
                  "target_info{job=\"unittest\",instance=\"\"} 1\n\n",
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
                  "target_info{job=\"example.com\",instance=\"42\"} 1\n\n",
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

    prometheus_metric_t pms[cases[i].resources_num];
    for (size_t j = 0; j < cases[i].resources_num; j++) {
      pms[j] = (prometheus_metric_t){.resource = cases[i].resources[j]};
    }
    prometheus_metric_family_t pfam = {
        .metrics = pms,
        .metrics_num = cases[i].resources_num,
    };

    strbuf_t got = STRBUF_CREATE;
    target_info(&got, (prometheus_metric_family_t const *[]){&pfam}, 1);
    EXPECT_EQ_STR(cases[i].want, got.ptr);

    STRBUF_DESTROY(got);
  }

  return 0;
}

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
	    "# collectd/write_prometheus " PACKAGE_VERSION " at example.com\n",
          // clang-format on
      },
      {
          .name = "multiple data points of one metric",
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
                                      .time = TIME_T_TO_CDTIME_T(100),
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
                                      .time = TIME_T_TO_CDTIME_T(110),
                                      .value.counter = 62,
                                  },
                              .num = 1,
                          },
                  },
              },
          .fams_num = 2,
          // clang-format off
          .want =
            "# HELP target_info Target metadata\n"
	    "# TYPE target_info gauge\n"
	    "target_info{job=\"name1\",instance=\"instance1\",host_name=\"example.org\"} 1\n"
	    "\n"
	    "# HELP unit_test_total\n"
	    "# TYPE unit_test_total counter\n"
	    "unit_test_total{job=\"name1\",instance=\"instance1\"} 62 110000\n"
	    "\n"
	    "# collectd/write_prometheus " PACKAGE_VERSION " at example.com\n",
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
          .fams_num = 2,
          // clang-format off
          .want =
            "# HELP target_info Target metadata\n"
	    "# TYPE target_info gauge\n"
	    "target_info{job=\"name1\",instance=\"instance2\",host_name=\"example.net\"} 1\n"
	    "target_info{job=\"name1\",instance=\"instance1\",host_name=\"example.org\"} 1\n"
	    "\n"
	    "# HELP unit_test_total\n"
	    "# TYPE unit_test_total counter\n"
	    "unit_test_total{job=\"name1\",instance=\"instance2\"} 23\n"
	    "unit_test_total{job=\"name1\",instance=\"instance1\"} 42\n"
	    "\n"
	    "# collectd/write_prometheus " PACKAGE_VERSION " at example.com\n",
          // clang-format on
      },
      {
          .name = "job defaults to hostname_g, instance defaults to an empty "
                  "string",
          .fams =
              &(metric_family_t){
                  .name = "unit.test",
                  .type = METRIC_TYPE_GAUGE,
                  .resource =
                      {
                          .ptr =
                              (label_pair_t[]){
                                  {"host.name", "example.org"},
                              },
                          .num = 1,
                      },
                  .metric =
                      {
                          .ptr =
                              &(metric_t){
                                  .value.gauge = 42,
                              },
                          .num = 1,
                      },
              },
          .fams_num = 1,
          // clang-format off
          .want =
            "# HELP target_info Target metadata\n"
	    "# TYPE target_info gauge\n"
	    "target_info{job=\"example.com\",instance=\"\",host_name=\"example.org\"} 1\n"
	    "\n"
	    "# HELP unit_test\n"
	    "# TYPE unit_test gauge\n"
	    "unit_test{job=\"example.com\",instance=\"\"} 42\n"
	    "\n"
	    "# collectd/write_prometheus " PACKAGE_VERSION " at example.com\n",
          // clang-format on
      },
  };

  for (size_t i = 0; i < STATIC_ARRAY_SIZE(cases); i++) {
    printf("# Case %zu: %s\n", i, cases[i].name);

    CHECK_ZERO(alloc_metrics());

    for (size_t j = 0; j < cases[i].fams_num; j++) {
      metric_family_t *fam = cases[i].fams + j;

      for (size_t k = 0; k < fam->metric.num; k++) {
        metric_t *m = fam->metric.ptr + k;
        m->family = fam;
      }

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
