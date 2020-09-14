/**
 * collectd - src/daemon/curl_stats_test.c
 * Copyright (C) 2020       Barbara 'bkjg' Kaczorowska
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
 *   Barbara 'bkjg' Kaczorowska <bkjg at google.com>
 */

#include "collectd.h"
#include "curl_stats.h"
#include "testing.h"

DEF_TEST(curl_stats_from_config) {
  struct {
    oconfig_item_t ci;
    size_t want_get_num_attr;
    char **want_get_enabled_attr;
    int *want_get_metric_type;
    size_t *want_get_num_metrics;
    distribution_t **want_get_distributions;
    size_t num_enabled_attr;
  } cases[] =
      {
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SizeDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "test",
                                          },
                                      },

                              },
                          },
                      .children_num = 1,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SpeedDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_NUMBER,
                                              .value.number = 5,
                                          },
                                      },

                              },
                          },
                      .children_num = 1,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SizeDistributionType",
                                  .values_num = 2,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "linear",
                                          },
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "exponential",
                                          },
                                      },

                              },
                          },
                      .children_num = 1,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "TimeDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "exponential",
                                          },
                                      },

                              },
                              {.key = "TimeBase",
                               .values_num = 1,
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_BOOLEAN,
                                   .value.boolean = 1,
                               }}},
                          },
                      .children_num = 2,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SpeedDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "custom",
                                          },
                                      },

                              },
                              {.key = "SizeFactor",
                               .values_num = 1,
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_NUMBER,
                                   .value.number = 5,
                               }}},
                          },
                      .children_num = 2,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "SpeedDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "custom",
                                          },
                                      },

                              },
                              {.key = "SizeFactor",
                               .values_num = 1,
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_NUMBER,
                                   .value.number = 5,
                               }}},
                              {.key = "SpeedBoundaries",
                               .values_num = 3,
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 5,
                                       },
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 7.8,
                                       },
                                       {
                                           .type = OCONFIG_TYPE_STRING,
                                           .value.string = "7",
                                       }}}},
                      .children_num = 3,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "TimeDistributionType",
                                  .values_num = 1,
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "exponential",
                                          },
                                      },

                              },
                              {.key = "TimeFactor",
                               .values_num = 1,
                               .values = (oconfig_value_t[]){{
                                   .type = OCONFIG_TYPE_NUMBER,
                                   .value.number = 5,
                               }}},
                              {.key = "TimeBase",
                               .values_num = 1,
                               .values =
                                   (oconfig_value_t[]){
                                       {
                                           .type = OCONFIG_TYPE_NUMBER,
                                           .value.number = 0.5,
                                       }}}},
                      .children_num = 3,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .key = "AnyAttribute",
                              },
                          },
                      .children_num = 1,
                  },
          },
          {
              .ci =
                  {
                      .children =
                          (oconfig_item_t[]){
                              {
                                  .values_num = 1,
                                  .key = "SizeDistributionType",
                                  .values =
                                      (oconfig_value_t[]){
                                          {
                                              .type = OCONFIG_TYPE_STRING,
                                              .value.string = "linear",
                                          }},
                              },
                          },
                      .children_num = 1,
                  },
          },
      };

  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    curl_stats_t *s;

    s = curl_stats_from_config(&cases[i].ci);

    if (cases[i].want_get_metric_type == NULL &&
        cases[i].want_get_enabled_attr == NULL) {
      EXPECT_EQ_PTR(NULL, s);
    } else {
      CHECK_NOT_NULL(s);

      size_t num_attr = 0;
      metric_family_t **fam =
          curl_stats_get_metric_families_for_attributes(s, &num_attr);

      EXPECT_EQ_UINT64(cases[i].want_get_num_attr, num_attr);

      for (size_t family = 0; family < num_attr; ++family) {
        for (size_t metric = 0; metric < cases[i].want_get_num_metrics[family];
             ++metric) {
          EXPECT_EQ_INT(cases[i].want_get_metric_type[metric],
                        fam[family]->type);

          EXPECT_EQ_UINT64(cases[i].want_get_num_metrics[family],
                           fam[family]->metric.num);

          if (fam[family]->type == METRIC_TYPE_DISTRIBUTION) {
            for (size_t j = 0; j < fam[family]->metric.num; ++j) {
              buckets_array_t buckets =
                  get_buckets(fam[family]->metric.ptr[j].value.distribution);
              buckets_array_t wanted_buckets =
                  get_buckets(cases[i].want_get_distributions[j]);

              EXPECT_EQ_UINT64(wanted_buckets.num_buckets, buckets.num_buckets);
              for (size_t k = 0; k < wanted_buckets.num_buckets; ++k) {
                EXPECT_EQ_DOUBLE(wanted_buckets.buckets[k].maximum,
                                 buckets.buckets[k].maximum);
                EXPECT_EQ_UINT64(wanted_buckets.buckets[k].bucket_counter,
                                 buckets.buckets[k].bucket_counter);
              }
            }
          }
        }
      }

      size_t num_enabled_attr = 0;
      char **enabled_attr =
          curl_stats_get_enabled_attributes(s, &num_enabled_attr);

      EXPECT_EQ_UINT64(cases[i].num_enabled_attr, num_enabled_attr);

      for (size_t attr = 0; attr < num_enabled_attr; ++attr) {
        EXPECT_EQ_STR(cases[i].want_get_enabled_attr[i], enabled_attr[i]);
      }

      for (size_t family = 0; family < num_attr; ++family) {
        metric_family_metric_reset(fam[family]);
      }

      free(fam);

      for (size_t attr = 0; attr < num_enabled_attr; ++attr) {
        free(enabled_attr[i]);
      }

      free(enabled_attr);
    }
  }

  return 0;
}

int main() {
  RUN_TEST(curl_stats_from_config);

  END_TEST;
}
