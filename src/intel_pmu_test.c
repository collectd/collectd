/**
 * collectd - src/logparser_test.c
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Pawel Tomaszewski <pawelx.tomaszewski@intel.com>
 **/

#include "intel_pmu.c"
#include "testing.h"

// Helper functions
// static int ctx_entl_size = 0;

intel_pmu_ctx_t* stub_pmu_init() {
  intel_pmu_ctx_t* pmu_ctx = calloc(1, sizeof(*pmu_ctx));
  intel_pmu_entity_t* entl = calloc(1, sizeof(*entl));

  pmu_ctx->entl = entl;

  return pmu_ctx;
}

void stub_pmu_teardown(intel_pmu_ctx_t* pmu_ctx) {
  free(pmu_ctx->entl);
  free(pmu_ctx);
}

DEF_TEST(pmu_config_hw_events__all_events) {
  // setup
  intel_pmu_ctx_t* pmu_ctx = stub_pmu_init();
  intel_pmu_entity_t* ent = pmu_ctx->entl;

  oconfig_value_t values[] = {
      {.value.string = "All", .type = OCONFIG_TYPE_STRING},
  };
  oconfig_item_t config_item = {
      .key = "HardwareEvents",
      .values = values,
      .values_num = STATIC_ARRAY_SIZE(values),
  };

  // check
  int result = pmu_config_hw_events(&config_item, ent);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(1, ent->all_events);

  // cleanup
  stub_pmu_teardown(pmu_ctx);
  return 0;
}

DEF_TEST(pmu_config_hw_events__few_events) {
  // setup
  intel_pmu_ctx_t* pmu_ctx = stub_pmu_init();
  intel_pmu_entity_t* ent = pmu_ctx->entl;

  oconfig_value_t values[] = {
      {.value.string = "event0", .type = OCONFIG_TYPE_STRING},
      {.value.string = "event1", .type = OCONFIG_TYPE_STRING},
      {.value.string = "event2", .type = OCONFIG_TYPE_STRING},
      {.value.string = "event3", .type = OCONFIG_TYPE_STRING},
      {.value.string = "event4", .type = OCONFIG_TYPE_STRING}
  };
  oconfig_item_t config_item = {
      .key = "HardwareEvents",
      .values = values,
      .values_num = STATIC_ARRAY_SIZE(values),
  };

  // check
  int result = pmu_config_hw_events(&config_item, ent);
  EXPECT_EQ_INT(0, result);
  EXPECT_EQ_INT(config_item.values_num, ent->hw_events_count);
  for(int i = 0; i < ent->hw_events_count; i++) {
    EXPECT_EQ_STR(values[i].value.string, ent->hw_events[i]);
  }

  // cleanup
  stub_pmu_teardown(pmu_ctx);
  return 0;
}

DEF_TEST(config_cores_parse) {

  return 0;
}

int main(void) {
  RUN_TEST(pmu_config_hw_events__all_events);
  RUN_TEST(pmu_config_hw_events__few_events);
  RUN_TEST(config_cores_parse);

  END_TEST;
}