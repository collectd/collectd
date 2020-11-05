/**
 * collectd - src/ras_test.c
 * MIT License
 *
 * Copyright (C) 2020  Intel Corporation. All rights reserved.
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
 *   Bartlomiej Kotlowski <bartlomiej.kotlowski@intel.com>
 **/

#include "ras.c"
#include "testing.h"

#define NPROCS 2

void clear_ras_metrics_server() {
  nprocs = NPROCS;
  if (ras_metrics_server.per_CPU != NULL) {
    free(ras_metrics_server.per_CPU);
  }
  ras_metrics_server.ras_cache_l2_errors = 0;
  ras_metrics_server.ras_upi_errors = 0;

  ras_metrics_server.per_CPU = (struct ras_metrics_per_CPU *) calloc(
      nprocs, sizeof(struct ras_metrics_per_CPU));
}

DEF_TEST(classify_entries) {
  int CPU = 0;
  clear_ras_metrics_server();
  classify_entries(CPU, "Unclassified", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_unclassified_mce_errors,
                   1);
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_base_errors,
                   1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Internal unclassified", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_unclassified_mce_errors,
                   1);
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_base_errors,
                   1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Microcode ROM parity error", "foo");
  EXPECT_EQ_UINT64(
      ras_metrics_server.per_CPU[CPU].ras_microcode_rom_parity_errors, 1);
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_base_errors,
                   1);

  clear_ras_metrics_server();
  classify_entries(CPU, "External error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_external_mce_errors, 1);
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_base_errors,
                   1);

  clear_ras_metrics_server();
  classify_entries(CPU, "FRC error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_frc_errors, 1);
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_base_errors,
                   1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Internal parity error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_internal_parity_error,
                   1);
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_base_errors,
                   1);

  clear_ras_metrics_server();
  classify_entries(CPU, "SMM Handler Code Access Violation", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU]
                       .ras_smm_handler_code_access_violation_errors,
                   1);
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_base_errors,
                   1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Internal Timer error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_internal_timer_errors,
                   1);
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_base_errors,
                   1);

  clear_ras_metrics_server();
  classify_entries(CPU, "BUS Error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_bus_errors, 1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_bus_errors, 0);

  clear_ras_metrics_server();
  classify_entries(CPU, "BUS", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_processor_bus_errors, 0);

  clear_ras_metrics_server();
  classify_entries(CPU, "Memory read error", "Uncorrected_error");
  EXPECT_EQ_UINT64(
      ras_metrics_server.per_CPU[CPU].ras_memory_read_uncorrectable_errors, 1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Memory read error", "Corrected_error");
  EXPECT_EQ_UINT64(
      ras_metrics_server.per_CPU[CPU].ras_memory_read_corrected_errors, 1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Memory read error", "foo");
  EXPECT_EQ_UINT64(
      ras_metrics_server.per_CPU[CPU].ras_memory_read_uncorrectable_errors, 0);
  EXPECT_EQ_UINT64(
      ras_metrics_server.per_CPU[CPU].ras_memory_read_corrected_errors, 0);

  clear_ras_metrics_server();
  classify_entries(CPU, "Memory write error", "Uncorrected_error");
  EXPECT_EQ_UINT64(
      ras_metrics_server.per_CPU[CPU].ras_memory_write_uncorrectable_errors, 1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Memory write error", "Corrected_error");
  EXPECT_EQ_UINT64(
      ras_metrics_server.per_CPU[CPU].ras_memory_write_corrected_errors, 1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Memory write error", "foo");
  EXPECT_EQ_UINT64(
      ras_metrics_server.per_CPU[CPU].ras_memory_write_uncorrectable_errors, 0);
  EXPECT_EQ_UINT64(
      ras_metrics_server.per_CPU[CPU].ras_memory_write_corrected_errors, 0);

  clear_ras_metrics_server();
  classify_entries(CPU, "CACHE Level-1 Error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_cache_l0_l1_errors, 1);
  EXPECT_EQ_UINT64(ras_metrics_server.ras_cache_l2_errors, 0);

  clear_ras_metrics_server();
  classify_entries(CPU, "CACHE Level-1 Error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_cache_l0_l1_errors, 1);
  EXPECT_EQ_UINT64(ras_metrics_server.ras_cache_l2_errors, 0);

  clear_ras_metrics_server();
  classify_entries(CPU, "CACHE Level-2 Error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_cache_l0_l1_errors, 0);
  EXPECT_EQ_UINT64(ras_metrics_server.ras_cache_l2_errors, 1);

  clear_ras_metrics_server();
  classify_entries(CPU, "CACHE Level-3 Error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_cache_l0_l1_errors, 0);
  EXPECT_EQ_UINT64(ras_metrics_server.ras_cache_l2_errors, 0);

  clear_ras_metrics_server();
  classify_entries(CPU, "CACHE Level-0", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_cache_l0_l1_errors, 0);
  EXPECT_EQ_UINT64(ras_metrics_server.ras_cache_l2_errors, 0);

  clear_ras_metrics_server();
  classify_entries(CPU, "Instruction TLB Error", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_tlb_instruction_errors,
                   1);

  clear_ras_metrics_server();
  classify_entries(CPU, "Instruction TLB", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.per_CPU[CPU].ras_tlb_instruction_errors,
                   0);

  clear_ras_metrics_server();
  classify_entries(CPU, "UPI:", "foo");
  EXPECT_EQ_UINT64(ras_metrics_server.ras_upi_errors, 1);

  free(ras_metrics_server.per_CPU);
  return 0;
}

DEF_TEST(safe_incremented_counter) {
  unsigned long long value;

  value = 0;
  safe_incremented_counter(&value);
  EXPECT_EQ_UINT64(1, value);

  value = LONG_MAX;
  safe_incremented_counter(&value);
  EXPECT_EQ_UINT64(LONG_MAX + 1, value);

  value = ULLONG_MAX;
  safe_incremented_counter(&value);
  EXPECT_EQ_UINT64(ULLONG_MAX, value);

  return 0;
}

DEF_TEST(convert_to_number) {
  bool ret;
  long int number;

  ret = convert_to_number("0", &number);
  EXPECT_EQ_INT(0, number);
  EXPECT_EQ_INT(1, ret ? 1 : 0);

  ret = convert_to_number("123", &number);
  EXPECT_EQ_INT(123, number);
  EXPECT_EQ_INT(1, ret ? 1 : 0);

  // convert max int
  ret = convert_to_number("2147483647", &number);
  EXPECT_EQ_INT(1, ret ? 1 : 0);
  EXPECT_EQ_INT(2147483647, number);

  switch (sizeof(long int)) {
    case 4:
      // convert over max long int
      ret = convert_to_number("2147483648", &number);
      EXPECT_EQ_INT(0, ret ? 1 : 0);
      break;

    case 8:
      // convert max long int
      ret = convert_to_number("9223372036854775807", &number);
      EXPECT_EQ_INT(1, ret ? 1 : 0);
      EXPECT_EQ_INT(1, (number == LONG_MAX) ? 1 : 0);
      // convert over max long int
      ret = convert_to_number("9223372036854775809", &number);
      EXPECT_EQ_INT(0, ret ? 1 : 0);

      break;
  }
  // convert max int
  ret = convert_to_number("foo", &number);
  EXPECT_EQ_INT(0, ret ? 1 : 0);

  ret = convert_to_number("-1", &number);
  EXPECT_EQ_INT(0, ret ? 1 : 0);

  return 0;
}

int main(void) {
  RUN_TEST(classify_entries);
  RUN_TEST(safe_incremented_counter);
  RUN_TEST(convert_to_number);
  END_TEST;
}
