/*
 * collectd - src/dpdkevents.c
 * MIT License
 *
 * Copyright(c) 2017 Intel Corporation. All rights reserved.
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
 *   Serhiy Pshyk <serhiyx.pshyk@intel.com>
 *   Kim-Marie Jones <kim-marie.jones@intel.com>
 *   Krzysztof Matczak <krzysztofx@intel.com>
 */

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include "semaphore.h"
#include "sys/mman.h"
#include "utils_dpdk.h"
#include "utils_time.h"

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_keepalive.h>

#define DPDK_EVENTS_PLUGIN "dpdkevents"
#define DPDK_EVENTS_NAME "dpdk_collectd_events"
#define ETH_LINK_NA 0xFF

#define INT64_BIT_SIZE 64
#define KEEPALIVE_PLUGIN_INSTANCE "keepalive"
#define RTE_KEEPALIVE_SHM_NAME "/dpdk_keepalive_shm_name"

typedef struct dpdk_keepalive_shm_s {
  sem_t core_died;
  enum rte_keepalive_state core_state[RTE_KEEPALIVE_MAXCORES];
  uint64_t core_last_seen_times[RTE_KEEPALIVE_MAXCORES];
} dpdk_keepalive_shm_t;

typedef struct dpdk_ka_monitor_s {
  cdtime_t read_time;
  int lcore_state;
} dpdk_ka_monitor_t;

typedef struct dpdk_link_status_config_s {
  int enabled;
  _Bool send_updated;
  uint32_t enabled_port_mask;
  char port_name[RTE_MAX_ETHPORTS][DATA_MAX_NAME_LEN];
  _Bool notify;
} dpdk_link_status_config_t;

typedef struct dpdk_keep_alive_config_s {
  int enabled;
  _Bool send_updated;
  uint128_t lcore_mask;
  dpdk_keepalive_shm_t *shm;
  char shm_name[DATA_MAX_NAME_LEN];
  _Bool notify;
  int fd;
} dpdk_keep_alive_config_t;

typedef struct dpdk_events_config_s {
  cdtime_t interval;
  dpdk_link_status_config_t link_status;
  dpdk_keep_alive_config_t keep_alive;
} dpdk_events_config_t;

typedef struct dpdk_link_info_s {
  cdtime_t read_time;
  int status_updated;
  int link_status;
} dpdk_link_info_t;

typedef struct dpdk_events_ctx_s {
  dpdk_events_config_t config;
  uint32_t nb_ports;
  dpdk_link_info_t link_info[RTE_MAX_ETHPORTS];
  dpdk_ka_monitor_t core_info[RTE_KEEPALIVE_MAXCORES];
} dpdk_events_ctx_t;

typedef enum {
  DPDK_EVENTS_STATE_CFG_ERR = 1 << 0,
  DPDK_EVENTS_STATE_KA_CFG_ERR = 1 << 1,
  DPDK_EVENTS_STATE_LS_CFG_ERR = 1 << 2,

} dpdk_events_cfg_status;

#define DPDK_EVENTS_CTX_GET(a) ((dpdk_events_ctx_t *)dpdk_helper_priv_get(a))

#define DPDK_EVENTS_TRACE()                                                    \
  DEBUG("%s:%s:%d pid=%u", DPDK_EVENTS_PLUGIN, __FUNCTION__, __LINE__, getpid())

static dpdk_helper_ctx_t *g_hc;
static dpdk_events_cfg_status g_state;

static int dpdk_event_keep_alive_shm_open(void) {
  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(g_hc);
  char *shm_name;

  if (strlen(ec->config.keep_alive.shm_name)) {
    shm_name = ec->config.keep_alive.shm_name;
  } else {
    shm_name = RTE_KEEPALIVE_SHM_NAME;
    WARNING(DPDK_EVENTS_PLUGIN ": Keep alive shared memory identifier is not "
                               "specified, using default one: %s",
            shm_name);
  }

  int fd = shm_open(shm_name, O_RDONLY, 0);
  if (fd < 0) {
    ERROR(DPDK_EVENTS_PLUGIN ": Failed to open %s as SHM:%s. Is DPDK KA "
                             "primary application running?",
          shm_name, STRERRNO);
    return errno;
  }

  if (ec->config.keep_alive.fd != -1) {
    struct stat stat_old, stat_new;

    if (fstat(ec->config.keep_alive.fd, &stat_old) || fstat(fd, &stat_new)) {
      ERROR(DPDK_EVENTS_PLUGIN ": failed to get information about a file");
      close(fd);
      return -1;
    }

    /* Check if inode number has changed. If yes, then create a new mapping */
    if (stat_old.st_ino == stat_new.st_ino) {
      close(fd);
      return 0;
    }

    if (munmap(ec->config.keep_alive.shm, sizeof(dpdk_keepalive_shm_t)) != 0) {
      ERROR(DPDK_EVENTS_PLUGIN ": munmap KA monitor failed");
      close(fd);
      return -1;
    }

    close(ec->config.keep_alive.fd);
    ec->config.keep_alive.fd = -1;
  }

  ec->config.keep_alive.shm = (dpdk_keepalive_shm_t *)mmap(
      0, sizeof(*(ec->config.keep_alive.shm)), PROT_READ, MAP_SHARED, fd, 0);
  if (ec->config.keep_alive.shm == MAP_FAILED) {
    ERROR(DPDK_EVENTS_PLUGIN ": Failed to mmap KA SHM:%s", STRERRNO);
    close(fd);
    return errno;
  }
  ec->config.keep_alive.fd = fd;

  return 0;
}

static void dpdk_events_default_config(void) {
  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(g_hc);

  ec->config.interval = plugin_get_interval();

  /* In configless mode when no <Plugin/> section is defined in config file
   * both link_status and keep_alive should be enabled */

  /* Link Status */
  ec->config.link_status.enabled = 1;
  ec->config.link_status.enabled_port_mask = ~0;
  ec->config.link_status.send_updated = 1;
  ec->config.link_status.notify = 0;

  for (int i = 0; i < RTE_MAX_ETHPORTS; i++) {
    ec->config.link_status.port_name[i][0] = 0;
  }

  /* Keep Alive */
  ec->config.keep_alive.enabled = 1;
  ec->config.keep_alive.send_updated = 1;
  ec->config.keep_alive.notify = 0;
  /* by default enable 128 cores */
  memset(&ec->config.keep_alive.lcore_mask, 1,
         sizeof(ec->config.keep_alive.lcore_mask));
  memset(&ec->config.keep_alive.shm_name, 0,
         sizeof(ec->config.keep_alive.shm_name));
  ec->config.keep_alive.shm = MAP_FAILED;
  ec->config.keep_alive.fd = -1;
}

static int dpdk_events_preinit(void) {
  DPDK_EVENTS_TRACE();

  if (g_hc != NULL) {
    /* already initialized if config callback was called before init callback */
    DEBUG("dpdk_events_preinit: helper already initialized.");
    return 0;
  }

  int ret =
      dpdk_helper_init(DPDK_EVENTS_NAME, sizeof(dpdk_events_ctx_t), &g_hc);
  if (ret != 0) {
    ERROR(DPDK_EVENTS_PLUGIN ": failed to initialize %s helper(error: %s)",
          DPDK_EVENTS_NAME, strerror(ret));
    return ret;
  }

  dpdk_events_default_config();

  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(g_hc);
  for (int i = 0; i < RTE_MAX_ETHPORTS; i++) {
    ec->link_info[i].link_status = ETH_LINK_NA;
  }

  for (int i = 0; i < RTE_KEEPALIVE_MAXCORES; i++) {
    ec->core_info[i].lcore_state = ETH_LINK_NA;
  }

  return ret;
}

static int dpdk_events_link_status_config(dpdk_events_ctx_t *ec,
                                          oconfig_item_t *ci) {
  ec->config.link_status.enabled = 1;

  DEBUG(DPDK_EVENTS_PLUGIN ": Subscribed for Link Status Events.");

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("EnabledPortMask", child->key) == 0) {
      if (cf_util_get_int(child,
                          (int *)&ec->config.link_status.enabled_port_mask))
        return -1;
      DEBUG(DPDK_EVENTS_PLUGIN ": LinkStatus:Enabled Port Mask 0x%X",
            ec->config.link_status.enabled_port_mask);
    } else if (strcasecmp("SendEventsOnUpdate", child->key) == 0) {
      if (cf_util_get_boolean(child, &ec->config.link_status.send_updated))
        return -1;
      DEBUG(DPDK_EVENTS_PLUGIN ": LinkStatus:SendEventsOnUpdate %d",
            ec->config.link_status.send_updated);
    } else if (strcasecmp("SendNotification", child->key) == 0) {
      if (cf_util_get_boolean(child, &ec->config.link_status.notify))
        return -1;

      DEBUG(DPDK_EVENTS_PLUGIN ": LinkStatus:SendNotification %d",
            ec->config.link_status.notify);
    } else if (strcasecmp("PortName", child->key) != 0) {
      ERROR(DPDK_EVENTS_PLUGIN ": unrecognized configuration option %s.",
            child->key);
      return -1;
    }
  }

  int port_num = 0;

  /* parse port names after EnabledPortMask was parsed */
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("PortName", child->key) == 0) {
      while (!(ec->config.link_status.enabled_port_mask & (1 << port_num)))
        port_num++;

      if (cf_util_get_string_buffer(
              child, ec->config.link_status.port_name[port_num],
              sizeof(ec->config.link_status.port_name[port_num]))) {
        return -1;
      }
      DEBUG(DPDK_EVENTS_PLUGIN ": LinkStatus:Port %d Name: %s", port_num,
            ec->config.link_status.port_name[port_num]);
      port_num++;
    }
  }

  return 0;
}

static int dpdk_events_keep_alive_config(dpdk_events_ctx_t *ec,
                                         oconfig_item_t *ci) {
  ec->config.keep_alive.enabled = 1;
  DEBUG(DPDK_EVENTS_PLUGIN ": Subscribed for Keep Alive Events.");

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("SendEventsOnUpdate", child->key) == 0) {
      if (cf_util_get_boolean(child, &ec->config.keep_alive.send_updated))
        return -1;

      DEBUG(DPDK_EVENTS_PLUGIN ": KeepAlive:SendEventsOnUpdate %d",
            ec->config.keep_alive.send_updated);
    } else if (strcasecmp("LCoreMask", child->key) == 0) {
      char lcore_mask[DATA_MAX_NAME_LEN];

      if (cf_util_get_string_buffer(child, lcore_mask, sizeof(lcore_mask)))
        return -1;
      ec->config.keep_alive.lcore_mask =
          str_to_uint128(lcore_mask, strlen(lcore_mask));
      DEBUG(DPDK_EVENTS_PLUGIN ": KeepAlive:LCoreMask 0x%" PRIX64 "%" PRIX64 "",
            ec->config.keep_alive.lcore_mask.high,
            ec->config.keep_alive.lcore_mask.low);
    } else if (strcasecmp("KeepAliveShmName", child->key) == 0) {
      if (cf_util_get_string_buffer(child, ec->config.keep_alive.shm_name,
                                    sizeof(ec->config.keep_alive.shm_name)))
        return -1;

      DEBUG(DPDK_EVENTS_PLUGIN ": KeepAlive:KeepAliveShmName %s",
            ec->config.keep_alive.shm_name);
    } else if (strcasecmp("SendNotification", child->key) == 0) {
      if (cf_util_get_boolean(child, &ec->config.keep_alive.notify))
        return -1;

      DEBUG(DPDK_EVENTS_PLUGIN ": KeepAlive:SendNotification %d",
            ec->config.keep_alive.notify);
    } else {
      ERROR(DPDK_EVENTS_PLUGIN ": unrecognized configuration option %s.",
            child->key);
      return -1;
    }
  }

  return 0;
}

static int dpdk_events_config(oconfig_item_t *ci) {
  DPDK_EVENTS_TRACE();
  int ret = dpdk_events_preinit();
  if (ret) {
    g_state |= DPDK_EVENTS_STATE_CFG_ERR;
    return 0;
  }

  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(g_hc);

  /* Disabling link_status and keep_alive since <Plugin/> config section
   * specifies if those should be enabled */
  ec->config.keep_alive.enabled = ec->config.link_status.enabled = 0;
  memset(&ec->config.keep_alive.lcore_mask, 0,
         sizeof(ec->config.keep_alive.lcore_mask));

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("EAL", child->key) == 0)
      ret = dpdk_helper_eal_config_parse(g_hc, child);
    else if (strcasecmp("Event", child->key) == 0) {
      char event_type[DATA_MAX_NAME_LEN];

      if (cf_util_get_string_buffer(child, event_type, sizeof(event_type)))
        ret = -1;
      else if (strcasecmp(event_type, "link_status") == 0) {
        ret = dpdk_events_link_status_config(ec, child);
        if (ret) {
          g_state |= DPDK_EVENTS_STATE_LS_CFG_ERR;
          continue;
        }
      } else if (strcasecmp(event_type, "keep_alive") == 0) {
        ret = dpdk_events_keep_alive_config(ec, child);
        if (ret) {
          g_state |= DPDK_EVENTS_STATE_KA_CFG_ERR;
          continue;
        }
      } else {
        ERROR(DPDK_EVENTS_PLUGIN ": The selected event \"%s\" is unknown.",
              event_type);
        ret = -1;
      }
    } else {
      ERROR(DPDK_EVENTS_PLUGIN ": unrecognized configuration option %s.",
            child->key);
      ret = -1;
    }

    if (ret != 0) {
      g_state |= DPDK_EVENTS_STATE_CFG_ERR;
      return 0;
    }
  }

  if (g_state & DPDK_EVENTS_STATE_KA_CFG_ERR) {
    ERROR(DPDK_EVENTS_PLUGIN
          ": Invalid keep alive configuration. Event disabled.");
    ec->config.keep_alive.enabled = 0;
  }

  if (g_state & DPDK_EVENTS_STATE_LS_CFG_ERR) {
    ERROR(DPDK_EVENTS_PLUGIN
          ": Invalid link status configuration. Event disabled.");
    ec->config.link_status.enabled = 0;
  }

  if (!ec->config.keep_alive.enabled && !ec->config.link_status.enabled) {
    ERROR(DPDK_EVENTS_PLUGIN ": At least one type of events should be "
                             "configured for collecting. Plugin misconfigured");
    g_state |= DPDK_EVENTS_STATE_CFG_ERR;
    return 0;
  }

  return 0;
}

static int dpdk_helper_link_status_get(dpdk_helper_ctx_t *phc) {
  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(phc);

  /* get Link Status values from DPDK */
  uint8_t nb_ports = rte_eth_dev_count();
  if (nb_ports == 0) {
    DPDK_CHILD_LOG("dpdkevent-helper: No DPDK ports available. "
                   "Check bound devices to DPDK driver.\n");
    return -ENODEV;
  }
  ec->nb_ports = nb_ports > RTE_MAX_ETHPORTS ? RTE_MAX_ETHPORTS : nb_ports;

  for (int i = 0; i < ec->nb_ports; i++) {
    if (ec->config.link_status.enabled_port_mask & (1 << i)) {
      struct rte_eth_link link;
      ec->link_info[i].read_time = cdtime();
      rte_eth_link_get_nowait(i, &link);
      if ((link.link_status == ETH_LINK_NA) ||
          (link.link_status != ec->link_info[i].link_status)) {
        ec->link_info[i].link_status = link.link_status;
        ec->link_info[i].status_updated = 1;
        DPDK_CHILD_LOG(" === PORT %d Link Status: %s\n", i,
                       link.link_status ? "UP" : "DOWN");
      }
    }
  }

  return 0;
}

/* this function is called from helper context */
int dpdk_helper_command_handler(dpdk_helper_ctx_t *phc, enum DPDK_CMD cmd) {
  if (phc == NULL) {
    DPDK_CHILD_LOG(DPDK_EVENTS_PLUGIN ": Invalid argument(phc)\n");
    return -EINVAL;
  }

  if (cmd != DPDK_CMD_GET_EVENTS) {
    DPDK_CHILD_LOG(DPDK_EVENTS_PLUGIN ": Unknown command (cmd=%d)\n", cmd);
    return -EINVAL;
  }

  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(phc);
  int ret = 0;
  if (ec->config.link_status.enabled)
    ret = dpdk_helper_link_status_get(phc);

  return ret;
}

static void dpdk_events_notification_dispatch(int severity,
                                              const char *plugin_instance,
                                              cdtime_t time, const char *msg) {
  notification_t n = {
      .severity = severity, .time = time, .plugin = DPDK_EVENTS_PLUGIN};
  sstrncpy(n.host, hostname_g, sizeof(n.host));
  sstrncpy(n.plugin_instance, plugin_instance, sizeof(n.plugin_instance));
  sstrncpy(n.message, msg, sizeof(n.message));
  plugin_dispatch_notification(&n);
}

static void dpdk_events_gauge_submit(const char *plugin_instance,
                                     const char *type_instance, gauge_t value,
                                     cdtime_t time) {
  value_list_t vl = {.values = &(value_t){.gauge = value},
                     .values_len = 1,
                     .time = time,
                     .plugin = DPDK_EVENTS_PLUGIN,
                     .type = "gauge",
                     .meta = NULL};
  sstrncpy(vl.plugin_instance, plugin_instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);
}

static int dpdk_events_link_status_dispatch(dpdk_helper_ctx_t *phc) {
  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(phc);
  DEBUG(DPDK_EVENTS_PLUGIN ": %s:%d ports=%u", __FUNCTION__, __LINE__,
        ec->nb_ports);

  /* dispatch Link Status values to collectd */
  for (int i = 0; i < ec->nb_ports; i++) {
    if (ec->config.link_status.enabled_port_mask & (1 << i)) {
      if (!ec->config.link_status.send_updated ||
          ec->link_info[i].status_updated) {

        DEBUG(DPDK_EVENTS_PLUGIN ": Dispatch PORT %d Link Status: %s", i,
              ec->link_info[i].link_status ? "UP" : "DOWN");

        char dev_name[DATA_MAX_NAME_LEN];
        if (ec->config.link_status.port_name[i][0] != 0) {
          snprintf(dev_name, sizeof(dev_name), "%s",
                   ec->config.link_status.port_name[i]);
        } else {
          snprintf(dev_name, sizeof(dev_name), "port.%d", i);
        }

        if (ec->config.link_status.notify) {
          int sev = ec->link_info[i].link_status ? NOTIF_OKAY : NOTIF_WARNING;
          char msg[DATA_MAX_NAME_LEN];
          snprintf(msg, sizeof(msg), "Link Status: %s",
                   ec->link_info[i].link_status ? "UP" : "DOWN");
          dpdk_events_notification_dispatch(sev, dev_name,
                                            ec->link_info[i].read_time, msg);
        } else {
          dpdk_events_gauge_submit(dev_name, "link_status",
                                   (gauge_t)ec->link_info[i].link_status,
                                   ec->link_info[i].read_time);
        }
        ec->link_info[i].status_updated = 0;
      }
    }
  }

  return 0;
}

static void dpdk_events_keep_alive_dispatch(dpdk_helper_ctx_t *phc) {
  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(phc);

  /* dispatch Keep Alive values to collectd */
  for (int i = 0; i < RTE_KEEPALIVE_MAXCORES; i++) {
    if (i < INT64_BIT_SIZE) {
      if (!(ec->config.keep_alive.lcore_mask.low & ((uint64_t)1 << i)))
        continue;
    } else if (i >= INT64_BIT_SIZE && i < INT64_BIT_SIZE * 2) {
      if (!(ec->config.keep_alive.lcore_mask.high &
            ((uint64_t)1 << (i - INT64_BIT_SIZE))))
        continue;
    } else {
      WARNING(DPDK_EVENTS_PLUGIN
              ": %s:%d Core id %u is out of 0 to %u range, skipping",
              __FUNCTION__, __LINE__, i, INT64_BIT_SIZE * 2);
      continue;
    }

    char core_name[DATA_MAX_NAME_LEN];
    snprintf(core_name, sizeof(core_name), "lcore%u", i);

    if (!ec->config.keep_alive.send_updated ||
        (ec->core_info[i].lcore_state !=
         ec->config.keep_alive.shm->core_state[i])) {
      ec->core_info[i].lcore_state = ec->config.keep_alive.shm->core_state[i];
      ec->core_info[i].read_time = cdtime();

      if (ec->config.keep_alive.notify) {
        char msg[DATA_MAX_NAME_LEN];
        int sev;

        switch (ec->config.keep_alive.shm->core_state[i]) {
        case RTE_KA_STATE_ALIVE:
          sev = NOTIF_OKAY;
          snprintf(msg, sizeof(msg), "lcore %u Keep Alive Status: ALIVE", i);
          break;
        case RTE_KA_STATE_MISSING:
          snprintf(msg, sizeof(msg), "lcore %u Keep Alive Status: MISSING", i);
          sev = NOTIF_WARNING;
          break;
        case RTE_KA_STATE_DEAD:
          snprintf(msg, sizeof(msg), "lcore %u Keep Alive Status: DEAD", i);
          sev = NOTIF_FAILURE;
          break;
        case RTE_KA_STATE_UNUSED:
          snprintf(msg, sizeof(msg), "lcore %u Keep Alive Status: UNUSED", i);
          sev = NOTIF_OKAY;
          break;
        case RTE_KA_STATE_GONE:
          snprintf(msg, sizeof(msg), "lcore %u Keep Alive Status: GONE", i);
          sev = NOTIF_FAILURE;
          break;
        case RTE_KA_STATE_DOZING:
          snprintf(msg, sizeof(msg), "lcore %u Keep Alive Status: DOZING", i);
          sev = NOTIF_OKAY;
          break;
        case RTE_KA_STATE_SLEEP:
          snprintf(msg, sizeof(msg), "lcore %u Keep Alive Status: SLEEP", i);
          sev = NOTIF_OKAY;
          break;
        default:
          snprintf(msg, sizeof(msg), "lcore %u Keep Alive Status: UNKNOWN", i);
          sev = NOTIF_FAILURE;
        }

        dpdk_events_notification_dispatch(sev, KEEPALIVE_PLUGIN_INSTANCE,
                                          ec->core_info[i].read_time, msg);
      } else {
        dpdk_events_gauge_submit(KEEPALIVE_PLUGIN_INSTANCE, core_name,
                                 ec->config.keep_alive.shm->core_state[i],
                                 ec->core_info[i].read_time);
      }
    }
  }
}

static int dpdk_events_read(user_data_t *ud) {
  DPDK_EVENTS_TRACE();

  if (g_hc == NULL) {
    ERROR(DPDK_EVENTS_PLUGIN ": plugin not initialized.");
    return -1;
  }

  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(g_hc);
  int ls_ret = -1, ka_ret = -1;

  int cmd_res = 0;
  if (ec->config.link_status.enabled) {
    ls_ret = dpdk_helper_command(g_hc, DPDK_CMD_GET_EVENTS, &cmd_res,
                                 ec->config.interval);
    if (cmd_res == 0 && ls_ret == 0) {
      dpdk_events_link_status_dispatch(g_hc);
    }
  }

  if (ec->config.keep_alive.enabled) {
    ka_ret = dpdk_event_keep_alive_shm_open();
    if (ka_ret) {
      ERROR(DPDK_EVENTS_PLUGIN
            ": %s : error %d in dpdk_event_keep_alive_shm_open()",
            __FUNCTION__, ka_ret);
    } else
      dpdk_events_keep_alive_dispatch(g_hc);
  }

  if (!((cmd_res || ls_ret) == 0 || ka_ret == 0)) {
    ERROR(DPDK_EVENTS_PLUGIN ": Read failure for all enabled event types");
    return -1;
  }

  return 0;
}

static int dpdk_events_shutdown(void) {
  DPDK_EVENTS_TRACE();

  if (g_hc == NULL)
    return 0;

  dpdk_events_ctx_t *ec = DPDK_EVENTS_CTX_GET(g_hc);
  if (ec->config.keep_alive.enabled) {
    if (ec->config.keep_alive.fd != -1) {
      close(ec->config.keep_alive.fd);
      ec->config.keep_alive.fd = -1;
    }

    if (ec->config.keep_alive.shm != MAP_FAILED) {
      if (munmap(ec->config.keep_alive.shm, sizeof(dpdk_keepalive_shm_t))) {
        ERROR(DPDK_EVENTS_PLUGIN ": munmap KA monitor failed");
        return -1;
      }
      ec->config.keep_alive.shm = MAP_FAILED;
    }
  }

  dpdk_helper_shutdown(g_hc);
  g_hc = NULL;

  return 0;
}

static int dpdk_events_init(void) {
  DPDK_EVENTS_TRACE();

  if (g_state & DPDK_EVENTS_STATE_CFG_ERR) {
    dpdk_events_shutdown();
    return -1;
  }

  int ret = dpdk_events_preinit();
  if (ret)
    return ret;

  return 0;
}

void module_register(void) {
  plugin_register_init(DPDK_EVENTS_PLUGIN, dpdk_events_init);
  plugin_register_complex_config(DPDK_EVENTS_PLUGIN, dpdk_events_config);
  plugin_register_complex_read(NULL, DPDK_EVENTS_PLUGIN, dpdk_events_read, 0,
                               NULL);
  plugin_register_shutdown(DPDK_EVENTS_PLUGIN, dpdk_events_shutdown);
}
