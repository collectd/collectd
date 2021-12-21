/**
 * collectd - src/intel-nvme.h
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
 * **/

#include <linux/types.h>

#ifdef __CHECKER__
#define __force __attribute__((force))
#else
#define __force
#endif

#define NVME_SMART_INTEL_CDW10 0x008000ca
#define INTEL_VENDOR_ID 0x8086

struct __attribute__((packed)) nvme_additional_smart_log_item {
  __u8 key;
  __u8 _kp[2];
  __u8 norm;
  __u8 _np;
  union __attribute__((packed)) {
    __u8 raw[6];
    struct __attribute__((packed)) wear_level {
      __le16 min;
      __le16 max;
      __le16 avg;
    } wear_level;
    struct __attribute__((packed)) thermal_throttle {
      __u8 pct;
      __u32 count;
    } thermal_throttle;
  };
  __u8 _rp;
};

struct nvme_additional_smart_log {
  struct nvme_additional_smart_log_item program_fail_cnt;
  struct nvme_additional_smart_log_item erase_fail_cnt;
  struct nvme_additional_smart_log_item wear_leveling_cnt;
  struct nvme_additional_smart_log_item e2e_err_cnt;
  struct nvme_additional_smart_log_item crc_err_cnt;
  struct nvme_additional_smart_log_item timed_workload_media_wear;
  struct nvme_additional_smart_log_item timed_workload_host_reads;
  struct nvme_additional_smart_log_item timed_workload_timer;
  struct nvme_additional_smart_log_item thermal_throttle_status;
  struct nvme_additional_smart_log_item retry_buffer_overflow_cnt;
  struct nvme_additional_smart_log_item pll_lock_loss_cnt;
  struct nvme_additional_smart_log_item nand_bytes_written;
  struct nvme_additional_smart_log_item host_bytes_written;
};
