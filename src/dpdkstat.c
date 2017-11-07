/*
 * collectd - src/dpdkstat.c
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

#include "collectd.h"

#include "common.h"
#include "utils_dpdk.h"

#include <rte_config.h>
#include <rte_ethdev.h>

#define DPDK_STATS_PLUGIN "dpdkstat"
#define DPDK_STATS_NAME "dpdk_collectd_stats"

#define DPDK_STATS_TRACE()                                                     \
  DEBUG("%s:%s:%d pid=%u", DPDK_STATS_PLUGIN, __FUNCTION__, __LINE__, getpid())

struct dpdk_stats_config_s {
  cdtime_t interval;
  uint32_t enabled_port_mask;
  char port_name[RTE_MAX_ETHPORTS][DATA_MAX_NAME_LEN];
};
typedef struct dpdk_stats_config_s dpdk_stats_config_t;

#define RTE_VERSION_16_07 RTE_VERSION_NUM(16, 7, 0, 16)

#if RTE_VERSION < RTE_VERSION_16_07
#define DPDK_STATS_XSTAT_GET_VALUE(ctx, index) ctx->xstats[index].value
#define DPDK_STATS_XSTAT_GET_NAME(ctx, index) ctx->xstats[index].name
#define DPDK_STATS_CTX_GET_XSTAT_SIZE sizeof(struct rte_eth_xstats)
#define DPDK_STATS_CTX_INIT(ctx)                                               \
  do {                                                                         \
    ctx->xstats = (struct rte_eth_xstats *)&ctx->raw_data[0];                  \
  } while (0)
#else
#define DPDK_STATS_XSTAT_GET_VALUE(ctx, index) ctx->xstats[index].value
#define DPDK_STATS_XSTAT_GET_NAME(ctx, index) ctx->xnames[index].name
#define DPDK_STATS_CTX_GET_XSTAT_SIZE                                          \
  (sizeof(struct rte_eth_xstat) + sizeof(struct rte_eth_xstat_name))
#define DPDK_STATS_CTX_INIT(ctx)                                               \
  do {                                                                         \
    ctx->xstats = (struct rte_eth_xstat *)&ctx->raw_data[0];                   \
    ctx->xnames =                                                              \
        (struct rte_eth_xstat_name *)&ctx                                      \
            ->raw_data[ctx->stats_count * sizeof(struct rte_eth_xstat)];       \
  } while (0)
#endif

struct dpdk_stats_ctx_s {
  dpdk_stats_config_t config;
  uint32_t stats_count;
  uint32_t ports_count;
  cdtime_t port_read_time[RTE_MAX_ETHPORTS];
  uint32_t port_stats_count[RTE_MAX_ETHPORTS];
#if RTE_VERSION < RTE_VERSION_16_07
  struct rte_eth_xstats *xstats;
#else
  struct rte_eth_xstat *xstats;
  struct rte_eth_xstat_name *xnames;
#endif
  char raw_data[];
};
typedef struct dpdk_stats_ctx_s dpdk_stats_ctx_t;

typedef enum {
  DPDK_STAT_STATE_OKAY = 0,
  DPDK_STAT_STATE_CFG_ERR,
} dpdk_stat_cfg_status;

#define DPDK_STATS_CTX_GET(a) ((dpdk_stats_ctx_t *)dpdk_helper_priv_get(a))

dpdk_helper_ctx_t *g_hc = NULL;
static char g_shm_name[DATA_MAX_NAME_LEN] = DPDK_STATS_NAME;
static dpdk_stat_cfg_status g_state = DPDK_STAT_STATE_OKAY;

static int dpdk_stats_reinit_helper();
static void dpdk_stats_default_config(void) {
  dpdk_stats_ctx_t *ec = DPDK_STATS_CTX_GET(g_hc);

  ec->config.interval = plugin_get_interval();
  for (int i = 0; i < RTE_MAX_ETHPORTS; i++) {
    ec->config.port_name[i][0] = 0;
  }
  /* Enable all ports by default */
  ec->config.enabled_port_mask = ~0;
}

static int dpdk_stats_preinit(void) {
  DPDK_STATS_TRACE();

  if (g_hc != NULL) {
    /* already initialized if config callback was called before init callback */
    DEBUG("dpdk_stats_preinit: helper already initialized");
    return 0;
  }

  int ret = dpdk_helper_init(g_shm_name, sizeof(dpdk_stats_ctx_t), &g_hc);
  if (ret != 0) {
    ERROR("%s: failed to initialize %s helper(error: %s)", DPDK_STATS_PLUGIN,
          g_shm_name, STRERRNO);
    return ret;
  }

  dpdk_stats_default_config();
  return ret;
}

static int dpdk_stats_config(oconfig_item_t *ci) {
  DPDK_STATS_TRACE();

  int ret = dpdk_stats_preinit();
  if (ret) {
    g_state = DPDK_STAT_STATE_CFG_ERR;
    return 0;
  }

  dpdk_stats_ctx_t *ctx = DPDK_STATS_CTX_GET(g_hc);

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("EnabledPortMask", child->key) == 0)
      ret = cf_util_get_int(child, (int *)&ctx->config.enabled_port_mask);
    else if (strcasecmp("SharedMemObj", child->key) == 0) {
      ret = cf_util_get_string_buffer(child, g_shm_name, sizeof(g_shm_name));
      if (ret == 0)
        ret = dpdk_stats_reinit_helper();
    } else if (strcasecmp("EAL", child->key) == 0)
      ret = dpdk_helper_eal_config_parse(g_hc, child);
    else if (strcasecmp("PortName", child->key) != 0) {
      ERROR(DPDK_STATS_PLUGIN ": unrecognized configuration option %s",
            child->key);
      ret = -1;
    }

    if (ret != 0) {
      g_state = DPDK_STAT_STATE_CFG_ERR;
      return 0;
    }
  }

  DEBUG(DPDK_STATS_PLUGIN ": Enabled Port Mask 0x%X",
        ctx->config.enabled_port_mask);
  DEBUG(DPDK_STATS_PLUGIN ": Shared memory object %s", g_shm_name);

  int port_num = 0;

  /* parse port names after EnabledPortMask was parsed */
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("PortName", child->key) == 0) {

      while (!(ctx->config.enabled_port_mask & (1 << port_num)))
        port_num++;

      if (cf_util_get_string_buffer(child, ctx->config.port_name[port_num],
                                    sizeof(ctx->config.port_name[port_num]))) {
        g_state = DPDK_STAT_STATE_CFG_ERR;
        return 0;
      }

      DEBUG(DPDK_STATS_PLUGIN ": Port %d Name: %s", port_num,
            ctx->config.port_name[port_num]);

      port_num++;
    }
  }

  return 0;
}

static int dpdk_helper_stats_get(dpdk_helper_ctx_t *phc) {
  int len = 0;
  int ret = 0;
  int stats = 0;
  dpdk_stats_ctx_t *ctx = DPDK_STATS_CTX_GET(phc);

  /* get stats from DPDK */
  for (uint8_t i = 0; i < ctx->ports_count; i++) {
    if (!(ctx->config.enabled_port_mask & (1 << i)))
      continue;

    ctx->port_read_time[i] = cdtime();
    /* Store available stats array length for port */
    len = ctx->port_stats_count[i];

    ret = rte_eth_xstats_get(i, &ctx->xstats[stats], len);
    if (ret < 0 || ret > len) {
      DPDK_CHILD_LOG(DPDK_STATS_PLUGIN
                     ": Error reading stats (port=%d; len=%d, ret=%d)\n",
                     i, len, ret);
      ctx->port_stats_count[i] = 0;
      return -1;
    }
#if RTE_VERSION >= RTE_VERSION_16_07
    ret = rte_eth_xstats_get_names(i, &ctx->xnames[stats], len);
    if (ret < 0 || ret > len) {
      DPDK_CHILD_LOG(DPDK_STATS_PLUGIN
                     ": Error reading stat names (port=%d; len=%d ret=%d)\n",
                     i, len, ret);
      ctx->port_stats_count[i] = 0;
      return -1;
    }
#endif
    ctx->port_stats_count[i] = ret;
    stats += ctx->port_stats_count[i];
  }

  assert(stats <= ctx->stats_count);
  return 0;
}

static int dpdk_helper_stats_count_get(dpdk_helper_ctx_t *phc) {
  uint8_t ports = dpdk_helper_eth_dev_count();
  if (ports == 0)
    return -ENODEV;

  dpdk_stats_ctx_t *ctx = DPDK_STATS_CTX_GET(phc);
  ctx->ports_count = ports;

  int len = 0;
  int stats_count = 0;
  for (int i = 0; i < ports; i++) {
    if (!(ctx->config.enabled_port_mask & (1 << i)))
      continue;
#if RTE_VERSION >= RTE_VERSION_16_07
    len = rte_eth_xstats_get_names(i, NULL, 0);
#else
    len = rte_eth_xstats_get(i, NULL, 0);
#endif
    if (len < 0) {
      DPDK_CHILD_LOG("%s: Cannot get stats count\n", DPDK_STATS_PLUGIN);
      return -1;
    }
    ctx->port_stats_count[i] = len;
    stats_count += len;
  }

  DPDK_CHILD_LOG("%s:%s:%d stats_count=%d\n", DPDK_STATS_PLUGIN, __FUNCTION__,
                 __LINE__, stats_count);

  return stats_count;
}

static int dpdk_stats_get_size(dpdk_helper_ctx_t *phc) {
  return dpdk_helper_data_size_get(phc) - sizeof(dpdk_stats_ctx_t);
}

int dpdk_helper_command_handler(dpdk_helper_ctx_t *phc, enum DPDK_CMD cmd) {
  /* this function is called from helper context */

  if (phc == NULL) {
    DPDK_CHILD_LOG("%s: Invalid argument(phc)\n", DPDK_STATS_PLUGIN);
    return -EINVAL;
  }

  if (cmd != DPDK_CMD_GET_STATS) {
    DPDK_CHILD_LOG("%s: Unknown command (cmd=%d)\n", DPDK_STATS_PLUGIN, cmd);
    return -EINVAL;
  }

  int stats_count = dpdk_helper_stats_count_get(phc);
  if (stats_count < 0) {
    return stats_count;
  }

  DPDK_STATS_CTX_GET(phc)->stats_count = stats_count;
  int stats_size = stats_count * DPDK_STATS_CTX_GET_XSTAT_SIZE;

  if (dpdk_stats_get_size(phc) < stats_size) {
    DPDK_CHILD_LOG(
        DPDK_STATS_PLUGIN
        ":%s:%d not enough space for stats (available=%d, needed=%d)\n",
        __FUNCTION__, __LINE__, (int)dpdk_stats_get_size(phc), stats_size);
    return -ENOBUFS;
  }

  return dpdk_helper_stats_get(phc);
}

static void dpdk_stats_resolve_cnt_type(char *cnt_type, size_t cnt_type_len,
                                        const char *cnt_name) {
  char *type_end;
  type_end = strrchr(cnt_name, '_');

  if ((type_end != NULL) && (strncmp(cnt_name, "rx_", strlen("rx_")) == 0)) {
    if (strstr(type_end, "bytes") != NULL) {
      sstrncpy(cnt_type, "if_rx_octets", cnt_type_len);
    } else if (strstr(type_end, "error") != NULL) {
      sstrncpy(cnt_type, "if_rx_errors", cnt_type_len);
    } else if (strstr(type_end, "dropped") != NULL) {
      sstrncpy(cnt_type, "if_rx_dropped", cnt_type_len);
    } else if (strstr(type_end, "packets") != NULL) {
      sstrncpy(cnt_type, "if_rx_packets", cnt_type_len);
    } else if (strstr(type_end, "_placement") != NULL) {
      sstrncpy(cnt_type, "if_rx_errors", cnt_type_len);
    } else if (strstr(type_end, "_buff") != NULL) {
      sstrncpy(cnt_type, "if_rx_errors", cnt_type_len);
    } else {
      /* Does not fit obvious type: use a more generic one */
      sstrncpy(cnt_type, "derive", cnt_type_len);
    }

  } else if ((type_end != NULL) &&
             (strncmp(cnt_name, "tx_", strlen("tx_"))) == 0) {
    if (strstr(type_end, "bytes") != NULL) {
      sstrncpy(cnt_type, "if_tx_octets", cnt_type_len);
    } else if (strstr(type_end, "error") != NULL) {
      sstrncpy(cnt_type, "if_tx_errors", cnt_type_len);
    } else if (strstr(type_end, "dropped") != NULL) {
      sstrncpy(cnt_type, "if_tx_dropped", cnt_type_len);
    } else if (strstr(type_end, "packets") != NULL) {
      sstrncpy(cnt_type, "if_tx_packets", cnt_type_len);
    } else {
      /* Does not fit obvious type: use a more generic one */
      sstrncpy(cnt_type, "derive", cnt_type_len);
    }
  } else if ((type_end != NULL) &&
             (strncmp(cnt_name, "flow_", strlen("flow_"))) == 0) {

    if (strstr(type_end, "_filters") != NULL) {
      sstrncpy(cnt_type, "operations", cnt_type_len);
    } else if (strstr(type_end, "error") != NULL)
      sstrncpy(cnt_type, "errors", cnt_type_len);

  } else if ((type_end != NULL) &&
             (strncmp(cnt_name, "mac_", strlen("mac_"))) == 0) {
    if (strstr(type_end, "error") != NULL) {
      sstrncpy(cnt_type, "errors", cnt_type_len);
    }
  } else {
    /* Does not fit obvious type, or strrchr error:
     *   use a more generic type */
    sstrncpy(cnt_type, "derive", cnt_type_len);
  }
}

static void dpdk_stats_counter_submit(const char *plugin_instance,
                                      const char *cnt_name, derive_t value,
                                      cdtime_t port_read_time) {
  value_list_t vl = VALUE_LIST_INIT;
  vl.values = &(value_t){.derive = value};
  vl.values_len = 1;
  vl.time = port_read_time;
  sstrncpy(vl.plugin, DPDK_STATS_PLUGIN, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  dpdk_stats_resolve_cnt_type(vl.type, sizeof(vl.type), cnt_name);
  sstrncpy(vl.type_instance, cnt_name, sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);
}

static int dpdk_stats_counters_dispatch(dpdk_helper_ctx_t *phc) {
  dpdk_stats_ctx_t *ctx = DPDK_STATS_CTX_GET(phc);

  /* dispatch stats values to collectd */

  DEBUG("%s:%s:%d ports=%u", DPDK_STATS_PLUGIN, __FUNCTION__, __LINE__,
        ctx->ports_count);

  int stats_count = 0;

  for (int i = 0; i < ctx->ports_count; i++) {
    if (!(ctx->config.enabled_port_mask & (1 << i)))
      continue;

    char dev_name[64];
    if (ctx->config.port_name[i][0] != 0) {
      snprintf(dev_name, sizeof(dev_name), "%s", ctx->config.port_name[i]);
    } else {
      snprintf(dev_name, sizeof(dev_name), "port.%d", i);
    }

    DEBUG(" === Dispatch stats for port %d (name=%s; stats_count=%d)", i,
          dev_name, ctx->port_stats_count[i]);

    for (int j = 0; j < ctx->port_stats_count[i]; j++) {
      const char *cnt_name = DPDK_STATS_XSTAT_GET_NAME(ctx, stats_count);
      if (cnt_name == NULL)
        WARNING("dpdkstat: Invalid counter name");
      else
        dpdk_stats_counter_submit(
            dev_name, cnt_name,
            (derive_t)DPDK_STATS_XSTAT_GET_VALUE(ctx, stats_count),
            ctx->port_read_time[i]);
      stats_count++;

      assert(stats_count <= ctx->stats_count);
    }
  }

  return 0;
}

static int dpdk_stats_reinit_helper() {
  DPDK_STATS_TRACE();

  dpdk_stats_ctx_t *ctx = DPDK_STATS_CTX_GET(g_hc);

  size_t data_size = sizeof(dpdk_stats_ctx_t) +
                     (ctx->stats_count * DPDK_STATS_CTX_GET_XSTAT_SIZE);

  DEBUG("%s:%d helper reinit (new_size=%zu)", __FUNCTION__, __LINE__,
        data_size);

  dpdk_stats_ctx_t tmp_ctx;
  dpdk_eal_config_t tmp_eal;

  memcpy(&tmp_ctx, ctx, sizeof(dpdk_stats_ctx_t));
  dpdk_helper_eal_config_get(g_hc, &tmp_eal);

  dpdk_helper_shutdown(g_hc);

  g_hc = NULL;

  int ret;
  ret = dpdk_helper_init(g_shm_name, data_size, &g_hc);
  if (ret != 0) {
    ERROR("%s: failed to initialize %s helper(error: %s)", DPDK_STATS_PLUGIN,
          g_shm_name, STRERRNO);
    return ret;
  }

  ctx = DPDK_STATS_CTX_GET(g_hc);
  memcpy(ctx, &tmp_ctx, sizeof(dpdk_stats_ctx_t));
  DPDK_STATS_CTX_INIT(ctx);
  dpdk_helper_eal_config_set(g_hc, &tmp_eal);

  return ret;
}

static int dpdk_stats_read(user_data_t *ud) {
  DPDK_STATS_TRACE();

  int ret = 0;

  if (g_hc == NULL) {
    ERROR("dpdk stats plugin not initialized");
    return -EINVAL;
  }

  dpdk_stats_ctx_t *ctx = DPDK_STATS_CTX_GET(g_hc);

  int result = 0;
  ret = dpdk_helper_command(g_hc, DPDK_CMD_GET_STATS, &result,
                            ctx->config.interval);
  if (ret != 0) {
    return 0;
  }

  if (result == -ENOBUFS) {
    dpdk_stats_reinit_helper();
  } else if (result == -ENODEV) {
    dpdk_helper_shutdown(g_hc);
  } else if (result == 0) {
    dpdk_stats_counters_dispatch(g_hc);
  }

  return 0;
}

static int dpdk_stats_shutdown(void) {
  DPDK_STATS_TRACE();

  dpdk_helper_shutdown(g_hc);
  g_hc = NULL;

  return 0;
}

static int dpdk_stats_init(void) {
  DPDK_STATS_TRACE();
  int ret = 0;

  if (g_state != DPDK_STAT_STATE_OKAY) {
    dpdk_stats_shutdown();
    return -1;
  }

  ret = dpdk_stats_preinit();
  if (ret != 0) {
    return ret;
  }

  return 0;
}

void module_register(void) {
  plugin_register_init(DPDK_STATS_PLUGIN, dpdk_stats_init);
  plugin_register_complex_config(DPDK_STATS_PLUGIN, dpdk_stats_config);
  plugin_register_complex_read(NULL, DPDK_STATS_PLUGIN, dpdk_stats_read, 0,
                               NULL);
  plugin_register_shutdown(DPDK_STATS_PLUGIN, dpdk_stats_shutdown);
}
