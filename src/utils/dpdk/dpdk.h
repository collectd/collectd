/*
 * collectd - src/utils_dpdk.h
 * MIT License
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
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
 *   Maryam Tahhan <maryam.tahhan@intel.com>
 *   Harry van Haaren <harry.van.haaren@intel.com>
 *   Taras Chornyi <tarasx.chornyi@intel.com>
 *   Serhiy Pshyk <serhiyx.pshyk@intel.com>
 *   Krzysztof Matczak <krzysztofx.matczak@intel.com>
 */

#ifndef UTILS_DPDK_H
#define UTILS_DPDK_H

#include <rte_version.h>

#define ERR_BUF_SIZE 1024

enum DPDK_CMD {
  DPDK_CMD_NONE = 0,
  DPDK_CMD_QUIT,
  DPDK_CMD_INIT,
  DPDK_CMD_GET_STATS,
  DPDK_CMD_GET_EVENTS,
  __DPDK_CMD_LAST,
};

struct dpdk_eal_config_s {
  char coremask[DATA_MAX_NAME_LEN];
  char memory_channels[DATA_MAX_NAME_LEN];
  char socket_memory[DATA_MAX_NAME_LEN];
  char file_prefix[DATA_MAX_NAME_LEN];
  char log_level[DATA_MAX_NAME_LEN];
  char rte_driver_lib_path[PATH_MAX];
};
typedef struct dpdk_eal_config_s dpdk_eal_config_t;

struct uint128_s {
  u_int64_t high;
  u_int64_t low;
};
typedef struct uint128_s uint128_t;

typedef struct dpdk_helper_ctx_s dpdk_helper_ctx_t;

int dpdk_helper_init(const char *name, size_t data_size,
                     dpdk_helper_ctx_t **pphc);
void dpdk_helper_shutdown(dpdk_helper_ctx_t *phc);
int dpdk_helper_eal_config_parse(dpdk_helper_ctx_t *phc, oconfig_item_t *ci);
int dpdk_helper_eal_config_set(dpdk_helper_ctx_t *phc, dpdk_eal_config_t *ec);
int dpdk_helper_eal_config_get(dpdk_helper_ctx_t *phc, dpdk_eal_config_t *ec);
int dpdk_helper_command(dpdk_helper_ctx_t *phc, enum DPDK_CMD cmd, int *result,
                        cdtime_t cmd_wait_time);
void *dpdk_helper_priv_get(dpdk_helper_ctx_t *phc);
int dpdk_helper_data_size_get(dpdk_helper_ctx_t *phc);
uint8_t dpdk_helper_eth_dev_count(void);

/* forward declaration of handler function that is called by helper from
 * child process. not implemented in helper. must be provided by client. */
int dpdk_helper_command_handler(dpdk_helper_ctx_t *phc, enum DPDK_CMD cmd);

uint128_t str_to_uint128(const char *str, int len);

/* logging functions that should be used in child process */
#define DPDK_CHILD_LOG(...) fprintf(stdout, __VA_ARGS__)
#define DPDK_CHILD_TRACE(_name)                                                \
  fprintf(stdout, "%s:%s:%d pid=%u\n", _name, __FUNCTION__, __LINE__, getpid())

#endif /* UTILS_DPDK_H */
