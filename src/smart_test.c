/**
 * collectd - src/smart_test.c
 * Copyright (C) 2018  Intel Corporation. All rights reserved.
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

#include "smart.c"
#include "testing.h"

int VENDOR_ID = 0x8086;
char *CORRET_DEV_PATH = "/dev/nvme0n1";

int ioctl(int __fd, unsigned long int __request, ...) {
  va_list valist;
  va_start(valist, __request);
  struct nvme_admin_cmd *admin_cmd = va_arg(valist, struct nvme_admin_cmd *);

  if (admin_cmd->opcode == NVME_ADMIN_IDENTIFY) {
    // icotl ask about vid
    __le16 *vid = (__le16 *)admin_cmd->addr;
    *vid = VENDOR_ID;
    return 0;
  } else if (admin_cmd->opcode == NVME_ADMIN_GET_LOG_PAGE) {
    // icotl ask about smart attributies
    if (admin_cmd->cdw10 == NVME_SMART_INTEL_CDW10) {
      // set intel specyfic attrubiuties
      struct nvme_additional_smart_log *intel_smart_log =
          (struct nvme_additional_smart_log *)admin_cmd->addr;
      intel_smart_log->program_fail_cnt.norm = 100;
      return 0;
    } else if (admin_cmd->cdw10 == NVME_SMART_CDW10) {
      // set global smart attrubiuties
      union nvme_smart_log *smart_log = (union nvme_smart_log *)admin_cmd->addr;
      smart_log->data.critical_warning = 0;
      return 0;
    }
    return -1; // no mock func
  }
  return -1; // no mock func
};

int open(const char *__path, int __oflag, ...) {
  if (__path == CORRET_DEV_PATH) {
    return 0;
  }
  return -1;
}

DEF_TEST(x) {
  int ret;

  ret = get_vendor_id(CORRET_DEV_PATH, "stub");
  EXPECT_EQ_INT(VENDOR_ID, ret);

  VENDOR_ID = 0x144D;
  ret = get_vendor_id(CORRET_DEV_PATH, "stub");
  EXPECT_EQ_INT(VENDOR_ID, ret);
  VENDOR_ID = 0x8086;

  // incorrect with DEV_PATH
  ret = get_vendor_id("dev/nvme0nXX", "stub");
  EXPECT_EQ_INT(-1, ret);

  ret = smart_read_nvme_intel_disk(CORRET_DEV_PATH, "stub");
  EXPECT_EQ_INT(0, ret);

  // incorrect with DEV_PATH
  ret = smart_read_nvme_intel_disk("dev/nvme0nXX", "stub");
  EXPECT_EQ_INT(-1, ret);

  CORRET_DEV_PATH = "dev/sda0";

  ret = smart_read_nvme_disk(CORRET_DEV_PATH, "stub");
  EXPECT_EQ_INT(0, ret);

  // incorrect with DEV_PATH
  ret = smart_read_nvme_disk("/dev/sdaXX", "stub");
  EXPECT_EQ_INT(-1, ret);

  return 0;
}

int main(void) {
  RUN_TEST(x);
  END_TEST;
}
