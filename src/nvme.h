/**
 * collectd - src/nvme.h
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

#define NVME_NSID_ALL 0xffffffff
#define NVME_ADMIN_GET_LOG_PAGE 0x02
#define NVME_ADMIN_IDENTIFY 0x06

union nvme_smart_log {
  struct {
    __u8 critical_warning;
    __u8 temperature[2];
    __u8 avail_spare;
    __u8 spare_thresh;
    __u8 percent_used;
    __u8 endu_grp_crit_warn_sumry;
    __u8 rsvd1[25];
    __u8 data_units_read[16];
    __u8 data_units_written[16];
    __u8 host_commands_read[16];
    __u8 host_commands_written[16];
    __u8 ctrl_busy_time[16];
    __u8 power_cycles[16];
    __u8 power_on_hours[16];
    __u8 unsafe_shutdowns[16];
    __u8 media_errors[16];
    __u8 num_err_log_entries[16];
    __le32 warning_temp_time;
    __le32 critical_comp_time;
    __le16 temp_sensor[8];
    __le32 thm_temp1_trans_count;
    __le32 thm_temp2_trans_count;
    __le32 thm_temp1_total_time;
    __le32 thm_temp2_total_time;
    __u8 rsvd2[280];
  } data;
  __u8 raw[512];
};
