/**
 * collectd - src/aggregation_test.c
 * Copyright (C) 2026 Nelson Yen
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#define plugin_dispatch_values aggregation_test_dispatch_values
#define plugin_get_ds aggregation_test_get_ds
#define uc_get_rate aggregation_test_get_rate
#include "aggregation.c" /* sic */
#undef plugin_dispatch_values
#undef plugin_get_ds
#undef uc_get_rate
#include "testing.h"

static value_list_t dispatched;
static value_t dispatched_value;

const data_set_t *aggregation_test_get_ds(const char *name) {
  static data_source_t output_source = {
      .name = "value",
      .type = DS_TYPE_GAUGE,
  };
  static data_set_t output = {
      .type = "single_gauge",
      .ds_num = 1,
      .ds = &output_source,
  };

  return strcmp(name, output.type) == 0 ? &output : NULL;
}

gauge_t *aggregation_test_get_rate(__attribute__((unused)) data_set_t const *ds,
                                   __attribute__((unused))
                                   value_list_t const *vl) {
  gauge_t *rate = calloc(2, sizeof(*rate));
  rate[0] = 10.0;
  rate[1] = 20.0;
  return rate;
}

int aggregation_test_dispatch_values(value_list_t const *vl) {
  dispatched = *vl;
  dispatched_value = vl->values[0];
  dispatched.values = &dispatched_value;
  return 0;
}

static data_set_t multi_ds = {
    .type = "multiple",
    .ds_num = 2,
    .ds =
        (data_source_t[]){
            {.name = "rx", .type = DS_TYPE_DERIVE},
            {.name = "tx", .type = DS_TYPE_DERIVE},
        },
};

DEF_TEST(get_data_source_single) {
  data_set_t ds = {
      .type = "single",
      .ds_num = 1,
      .ds = &(data_source_t){.name = "value", .type = DS_TYPE_GAUGE},
  };
  aggregation_t agg = {0};
  size_t index = SIZE_MAX;

  EXPECT_EQ_INT(0, agg_get_data_source(&ds, &agg, &index));
  EXPECT_EQ_INT(0, index);
  return 0;
}

DEF_TEST(get_data_source_multiple) {
  aggregation_t agg = {0};
  size_t index = SIZE_MAX;

  EXPECT_EQ_INT(EINVAL, agg_get_data_source(&multi_ds, &agg, &index));

  agg.data_source = "tx";
  EXPECT_EQ_INT(0, agg_get_data_source(&multi_ds, &agg, &index));
  EXPECT_EQ_INT(1, index);
  return 0;
}

DEF_TEST(get_data_source_missing) {
  aggregation_t agg = {.data_source = "missing"};
  size_t index = SIZE_MAX;

  EXPECT_EQ_INT(ENOENT, agg_get_data_source(&multi_ds, &agg, &index));
  return 0;
}

DEF_TEST(aggregate_selected_data_source) {
  data_set_t ds = {
      .type = "multiple_gauge",
      .ds_num = 2,
      .ds =
          (data_source_t[]){
              {.name = "first", .type = DS_TYPE_GAUGE},
              {.name = "second", .type = DS_TYPE_GAUGE},
          },
  };
  value_list_t vl = VALUE_LIST_INIT;
  sstrncpy(vl.host, "example.com", sizeof(vl.host));
  sstrncpy(vl.plugin, "example", sizeof(vl.plugin));
  sstrncpy(vl.type, ds.type, sizeof(vl.type));

  aggregation_t agg = {
      .data_source = "second",
      .set_type = "single_gauge",
      .calc_average = true,
  };

  agg_instance_t *inst = agg_instance_create(&ds, &vl, &agg);
  CHECK_NOT_NULL(inst);
  EXPECT_EQ_INT(0, agg_instance_update(inst, &ds, &vl));
  EXPECT_EQ_INT(0, agg_instance_read(inst, TIME_T_TO_CDTIME_T(1)));
  EXPECT_EQ_STR("single_gauge", dispatched.type);
  EXPECT_EQ_DOUBLE(20.0, dispatched.values[0].gauge);

  agg_instance_destroy(inst);
  free(inst);
  return 0;
}

int main(void) {
  RUN_TEST(get_data_source_single);
  RUN_TEST(get_data_source_multiple);
  RUN_TEST(get_data_source_missing);
  RUN_TEST(aggregate_selected_data_source);

  END_TEST;
}
