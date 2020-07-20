/**
 * collectd - src/smart_test.c
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
 *   Slawomir Strehlau <slawomir.strehlau@intel.com>
 **/

#include "smart.c"
#include "testing.h"

#define INTEL_VID 0x8086

int VENDOR_ID = INTEL_VID;
const char *CORRECT_DEV_PATH = "/dev/nvme0n1";
const char *INCORRECT_DEV_PATH = "dev/nvme0nXX";

int ioctl(int __fd, unsigned long int __request, ...) {
  va_list valist;
  va_start(valist, __request);
  struct nvme_admin_cmd *admin_cmd = va_arg(valist, struct nvme_admin_cmd *);
  va_end(valist);
  void *addr = (void *)(unsigned long)admin_cmd->addr;

  if (admin_cmd->opcode == NVME_ADMIN_IDENTIFY) {
    // ioctl asked about vid
    __le16 *vid = (__le16 *)addr;
    *vid = VENDOR_ID;
    return 0;
  } else if (admin_cmd->opcode == NVME_ADMIN_GET_LOG_PAGE) {
    // ioctl asked about smart attributes
    if (admin_cmd->cdw10 == NVME_SMART_INTEL_CDW10) {
      // set intel specific attributes
      struct nvme_additional_smart_log *intel_smart_log =
          (struct nvme_additional_smart_log *)addr;
      intel_smart_log->program_fail_cnt.norm = 100;
      return 0;
    } else if (admin_cmd->cdw10 == NVME_SMART_CDW10) {
      // set generic smart attributes
      union nvme_smart_log *smart_log = (union nvme_smart_log *)addr;
      smart_log->data.critical_warning = 0;
      return 0;
    }
  }
  return -1; // functionality not mocked
};

int open(const char *__path, int __oflag, ...) {
  if (__path == CORRECT_DEV_PATH) {
    return 0;
  }
  return -1;
}

DEF_TEST(x) {
  int ret;

  ret = get_vendor_id(CORRECT_DEV_PATH, "stub");
  EXPECT_EQ_INT(VENDOR_ID, ret);

  VENDOR_ID = 0x0;
  ret = get_vendor_id(CORRECT_DEV_PATH, "stub");
  EXPECT_EQ_INT(VENDOR_ID, ret);
  VENDOR_ID = INTEL_VID;

  ret = get_vendor_id(INCORRECT_DEV_PATH, "stub");
  EXPECT_EQ_INT(-1, ret);

  ret = smart_read_nvme_intel_disk(CORRECT_DEV_PATH, "stub");
  EXPECT_EQ_INT(0, ret);

  ret = smart_read_nvme_intel_disk(INCORRECT_DEV_PATH, "stub");
  EXPECT_EQ_INT(-1, ret);

  ret = smart_read_nvme_disk(CORRECT_DEV_PATH, "stub");
  EXPECT_EQ_INT(0, ret);

  ret = smart_read_nvme_disk(INCORRECT_DEV_PATH, "stub");
  EXPECT_EQ_INT(-1, ret);

  return 0;
}

int main(void) {
  RUN_TEST(x);
  END_TEST;
}
