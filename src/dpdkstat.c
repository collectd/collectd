/*-
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
 */

#include "collectd.h"

#include "common.h" /* auxiliary functions */
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */
#include "utils_time.h"

#include <getopt.h>
#include <poll.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/queue.h>

#include <rte_atomic.h>
#include <rte_branch_prediction.h>
#include <rte_common.h>
#include <rte_config.h>
#include <rte_debug.h>
#include <rte_debug.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_per_lcore.h>
#include <rte_string_fns.h>
#include <rte_tailq.h>
#include <rte_version.h>

#define DPDK_DEFAULT_RTE_CONFIG "/var/run/.rte_config"
#define DPDK_MAX_ARGC 8
#define DPDKSTAT_MAX_BUFFER_SIZE (4096 * 4)
#define DPDK_SHM_NAME "dpdk_collectd_stats_shm"
#define ERR_BUF_SIZE 1024
#define REINIT_SHM 1
#define RESET 1
#define NO_RESET 0

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
            ->raw_data[ctx->num_xstats * sizeof(struct rte_eth_xstat)];        \
  } while (0)
#endif

enum DPDK_HELPER_ACTION {
  DPDK_HELPER_ACTION_COUNT_STATS,
  DPDK_HELPER_ACTION_SEND_STATS,
};

enum DPDK_HELPER_STATUS {
  DPDK_HELPER_NOT_INITIALIZED = 0,
  DPDK_HELPER_WAITING_ON_PRIMARY,
  DPDK_HELPER_INITIALIZING_EAL,
  DPDK_HELPER_ALIVE_SENDING_STATS,
  DPDK_HELPER_GRACEFUL_QUIT,
};

struct dpdk_config_s {
  /* General DPDK params */
  char coremask[DATA_MAX_NAME_LEN];
  char memory_channels[DATA_MAX_NAME_LEN];
  char socket_memory[DATA_MAX_NAME_LEN];
  char process_type[DATA_MAX_NAME_LEN];
  char file_prefix[DATA_MAX_NAME_LEN];
  cdtime_t interval;
  uint32_t eal_initialized;
  uint32_t enabled_port_mask;
  char port_name[RTE_MAX_ETHPORTS][DATA_MAX_NAME_LEN];
  uint32_t eal_argc;
  /* Helper info */
  int collectd_reinit_shm;
  pid_t helper_pid;
  sem_t sema_helper_get_stats;
  sem_t sema_stats_in_shm;
  int helper_pipes[2];
  enum DPDK_HELPER_STATUS helper_status;
  enum DPDK_HELPER_ACTION helper_action;
  /* xstats info */
  uint32_t num_ports;
  uint32_t num_xstats;
  cdtime_t port_read_time[RTE_MAX_ETHPORTS];
  uint32_t num_stats_in_port[RTE_MAX_ETHPORTS];
  struct rte_eth_link link_status[RTE_MAX_ETHPORTS];
#if RTE_VERSION < RTE_VERSION_16_07
  struct rte_eth_xstats *xstats;
#else
  struct rte_eth_xstat *xstats;
  struct rte_eth_xstat_name *xnames;
#endif
  char *raw_data;
  /* rte_eth_xstats from here on until the end of the SHM */
};
typedef struct dpdk_config_s dpdk_config_t;

static int g_configured;
static dpdk_config_t *g_configuration;

static void dpdk_config_init_default(void);
static int dpdk_config(oconfig_item_t *ci);
static int dpdk_helper_init_eal(void);
static int dpdk_helper_run(void);
static int dpdk_helper_spawn(enum DPDK_HELPER_ACTION action);
static int dpdk_init(void);
static int dpdk_read(user_data_t *ud);
static int dpdk_shm_cleanup(void);
static int dpdk_shm_init(size_t size);

/* Write the default configuration to the g_configuration instances */
static void dpdk_config_init_default(void) {
  g_configuration->interval = plugin_get_interval();
  if (g_configuration->interval == cf_get_default_interval())
    WARNING("dpdkstat: No time interval was configured, default value %" PRIu64
            " ms is set",
            CDTIME_T_TO_MS(g_configuration->interval));
  /* Default is all ports enabled */
  g_configuration->enabled_port_mask = ~0;
  g_configuration->eal_argc = DPDK_MAX_ARGC;
  g_configuration->eal_initialized = 0;
  ssnprintf(g_configuration->coremask, DATA_MAX_NAME_LEN, "%s", "0xf");
  ssnprintf(g_configuration->memory_channels, DATA_MAX_NAME_LEN, "%s", "1");
  ssnprintf(g_configuration->process_type, DATA_MAX_NAME_LEN, "%s",
            "secondary");
  ssnprintf(g_configuration->file_prefix, DATA_MAX_NAME_LEN, "%s",
            DPDK_DEFAULT_RTE_CONFIG);

  for (int i = 0; i < RTE_MAX_ETHPORTS; i++)
    g_configuration->port_name[i][0] = 0;
}

static int dpdk_config(oconfig_item_t *ci) {
  int port_counter = 0;
  /* Allocate g_configuration and
   * initialize a POSIX SHared Memory (SHM) object.
   */
  int err = dpdk_shm_init(sizeof(dpdk_config_t));
  if (err) {
    char errbuf[ERR_BUF_SIZE];
    ERROR("dpdkstat: error in shm_init, %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    return -1;
  }

  /* Set defaults for config, overwritten by loop if config item exists */
  dpdk_config_init_default();

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Coremask", child->key) == 0) {
      cf_util_get_string_buffer(child, g_configuration->coremask,
                                sizeof(g_configuration->coremask));
      DEBUG("dpdkstat:COREMASK %s ", g_configuration->coremask);
    } else if (strcasecmp("MemoryChannels", child->key) == 0) {
      cf_util_get_string_buffer(child, g_configuration->memory_channels,
                                sizeof(g_configuration->memory_channels));
      DEBUG("dpdkstat:Memory Channels %s ", g_configuration->memory_channels);
    } else if (strcasecmp("SocketMemory", child->key) == 0) {
      cf_util_get_string_buffer(child, g_configuration->socket_memory,
                                sizeof(g_configuration->memory_channels));
      DEBUG("dpdkstat: socket mem %s ", g_configuration->socket_memory);
    } else if (strcasecmp("ProcessType", child->key) == 0) {
      cf_util_get_string_buffer(child, g_configuration->process_type,
                                sizeof(g_configuration->process_type));
      DEBUG("dpdkstat: proc type %s ", g_configuration->process_type);
    } else if ((strcasecmp("FilePrefix", child->key) == 0) &&
               (child->values[0].type == OCONFIG_TYPE_STRING)) {
      ssnprintf(g_configuration->file_prefix, DATA_MAX_NAME_LEN,
                "/var/run/.%s_config", child->values[0].value.string);
      DEBUG("dpdkstat: file prefix %s ", g_configuration->file_prefix);
    } else if ((strcasecmp("EnabledPortMask", child->key) == 0) &&
               (child->values[0].type == OCONFIG_TYPE_NUMBER)) {
      g_configuration->enabled_port_mask =
          (uint32_t)child->values[0].value.number;
      DEBUG("dpdkstat: Enabled Port Mask %u",
            g_configuration->enabled_port_mask);
    } else if (strcasecmp("PortName", child->key) == 0) {
      cf_util_get_string_buffer(
          child, g_configuration->port_name[port_counter],
          sizeof(g_configuration->port_name[port_counter]));
      DEBUG("dpdkstat: Port %d Name: %s ", port_counter,
            g_configuration->port_name[port_counter]);
      port_counter++;
    } else {
      WARNING("dpdkstat: The config option \"%s\" is unknown.", child->key);
    }
  }                 /* End for (int i = 0; i < ci->children_num; i++)*/
  g_configured = 1; /* Bypass configuration in dpdk_shm_init(). */

  return 0;
}

/*
 * Allocate g_configuration and initialize SHared Memory (SHM)
 * for config and helper process
 */
static int dpdk_shm_init(size_t size) {
  /*
   * Check if SHM is already configured: when config items are provided, the
   * config function initializes SHM. If there is no config, then init() will
   * just return.
   */
  if (g_configuration)
    return 0;

  char errbuf[ERR_BUF_SIZE];

  /* Create and open a new object, or open an existing object. */
  int fd = shm_open(DPDK_SHM_NAME, O_CREAT | O_TRUNC | O_RDWR, 0666);
  if (fd < 0) {
    WARNING("dpdkstat:Failed to open %s as SHM:%s", DPDK_SHM_NAME,
            sstrerror(errno, errbuf, sizeof(errbuf)));
    goto fail;
  }
  /* Set the size of the shared memory object. */
  int ret = ftruncate(fd, size);
  if (ret != 0) {
    WARNING("dpdkstat:Failed to resize SHM:%s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
    goto fail_close;
  }
  /* Map the shared memory object into this process' virtual address space. */
  g_configuration = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (g_configuration == MAP_FAILED) {
    WARNING("dpdkstat:Failed to mmap SHM:%s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
    goto fail_close;
  }
  /*
   * Close the file descriptor, the shared memory object still exists
   * and can only be removed by calling shm_unlink().
   */
  close(fd);

  /* Initialize g_configuration. */
  memset(g_configuration, 0, size);

  /* Initialize the semaphores for SHM use */
  int err = sem_init(&g_configuration->sema_helper_get_stats, 1, 0);
  if (err) {
    ERROR("dpdkstat semaphore init failed: %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    goto close;
  }
  err = sem_init(&g_configuration->sema_stats_in_shm, 1, 0);
  if (err) {
    ERROR("dpdkstat semaphore init failed: %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    goto close;
  }

  g_configuration->xstats = NULL;

  return 0;

fail_close:
  close(fd);
fail:
  /* Reset to zero, as it was set to MAP_FAILED aka: (void *)-1. Avoid
   * an issue if collectd attempts to run this plugin failure.
   */
  g_configuration = 0;
  return -1;
}

static int dpdk_re_init_shm() {
  dpdk_config_t temp_config;
  memcpy(&temp_config, g_configuration, sizeof(dpdk_config_t));
  DEBUG("dpdkstat: %s: ports %" PRIu32 ", xstats %" PRIu32, __func__,
        temp_config.num_ports, temp_config.num_xstats);

  size_t shm_xstats_size =
      sizeof(dpdk_config_t) +
      (DPDK_STATS_CTX_GET_XSTAT_SIZE * g_configuration->num_xstats);
  DEBUG("=== SHM new size for %" PRIu32 " xstats", g_configuration->num_xstats);

  int err = dpdk_shm_cleanup();
  if (err) {
    ERROR("dpdkstat: Error in shm_cleanup in %s", __func__);
    return err;
  }
  err = dpdk_shm_init(shm_xstats_size);
  if (err) {
    WARNING("dpdkstat: Error in shm_init in %s", __func__);
    return err;
  }
  /* If the XML config() function has been run, don't re-initialize defaults */
  if (!g_configured)
    dpdk_config_init_default();

  memcpy(g_configuration, &temp_config, sizeof(dpdk_config_t));
  g_configuration->collectd_reinit_shm = 0;
  g_configuration->raw_data = (char *)(g_configuration + 1);
  DPDK_STATS_CTX_INIT(g_configuration);
  return 0;
}

static int dpdk_init(void) {
  int err = dpdk_shm_init(sizeof(dpdk_config_t));
  if (err) {
    ERROR("dpdkstat: %s : error %d in shm_init()", __func__, err);
    return err;
  }

  /* If the XML config() function has been run, dont re-initialize defaults */
  if (!g_configured) {
    dpdk_config_init_default();
  }

  return 0;
}

static int dpdk_helper_stop(int reset) {
  g_configuration->helper_status = DPDK_HELPER_GRACEFUL_QUIT;
  if (reset) {
    g_configuration->eal_initialized = 0;
    g_configuration->num_ports = 0;
    g_configuration->xstats = NULL;
    g_configuration->num_xstats = 0;
    for (int i = 0; i < RTE_MAX_ETHPORTS; i++)
      g_configuration->num_stats_in_port[i] = 0;
  }
  close(g_configuration->helper_pipes[1]);
  int err = kill(g_configuration->helper_pid, SIGKILL);
  if (err) {
    char errbuf[ERR_BUF_SIZE];
    WARNING("dpdkstat: error sending kill to helper: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
  }

  return 0;
}

static int dpdk_helper_spawn(enum DPDK_HELPER_ACTION action) {
  char errbuf[ERR_BUF_SIZE];
  g_configuration->eal_initialized = 0;
  g_configuration->helper_action = action;
  /*
   * Create a pipe for helper stdout back to collectd. This is necessary for
   * logging EAL failures, as rte_eal_init() calls rte_panic().
   */
  if (pipe(g_configuration->helper_pipes) != 0) {
    DEBUG("dpdkstat: Could not create helper pipe: %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    return -1;
  }

  int pipe0_flags = fcntl(g_configuration->helper_pipes[0], F_GETFL, 0);
  int pipe1_flags = fcntl(g_configuration->helper_pipes[1], F_GETFL, 0);
  if (pipe0_flags == -1 || pipe1_flags == -1) {
    WARNING("dpdkstat: Failed setting up pipe flags: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
  }
  int pipe0_err = fcntl(g_configuration->helper_pipes[0], F_SETFL,
                        pipe1_flags | O_NONBLOCK);
  int pipe1_err = fcntl(g_configuration->helper_pipes[1], F_SETFL,
                        pipe0_flags | O_NONBLOCK);
  if (pipe0_err == -1 || pipe1_err == -1) {
    WARNING("dpdkstat: Failed setting up pipes: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
  }

  pid_t pid = fork();
  if (pid > 0) {
    close(g_configuration->helper_pipes[1]);
    g_configuration->helper_pid = pid;
    DEBUG("dpdkstat: helper pid %li", (long)g_configuration->helper_pid);
    /* Kick helper once its alive to have it start processing */
    sem_post(&g_configuration->sema_helper_get_stats);
  } else if (pid == 0) {
    /* Replace stdout with a pipe to collectd. */
    close(g_configuration->helper_pipes[0]);
    close(STDOUT_FILENO);
    dup2(g_configuration->helper_pipes[1], STDOUT_FILENO);
    dpdk_helper_run();
    exit(0);
  } else {
    ERROR("dpdkstat: Failed to fork helper process: %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    return -1;
  }
  return 0;
}

/*
 * Initialize the DPDK EAL, if this returns, EAL is successfully initialized.
 * On failure, the EAL prints an error message, and the helper process exits.
 */
static int dpdk_helper_init_eal(void) {
  g_configuration->helper_status = DPDK_HELPER_INITIALIZING_EAL;
  char *argp[(g_configuration->eal_argc) + 1];
  int i = 0;

  argp[i++] = "collectd-dpdk";
  if (strcasecmp(g_configuration->coremask, "") != 0) {
    argp[i++] = "-c";
    argp[i++] = g_configuration->coremask;
  }
  if (strcasecmp(g_configuration->memory_channels, "") != 0) {
    argp[i++] = "-n";
    argp[i++] = g_configuration->memory_channels;
  }
  if (strcasecmp(g_configuration->socket_memory, "") != 0) {
    argp[i++] = "--socket-mem";
    argp[i++] = g_configuration->socket_memory;
  }
  if (strcasecmp(g_configuration->file_prefix, "") != 0 &&
      strcasecmp(g_configuration->file_prefix, DPDK_DEFAULT_RTE_CONFIG) != 0) {
    argp[i++] = "--file-prefix";
    argp[i++] = g_configuration->file_prefix;
  }
  if (strcasecmp(g_configuration->process_type, "") != 0) {
    argp[i++] = "--proc-type";
    argp[i++] = g_configuration->process_type;
  }
  g_configuration->eal_argc = i;

  g_configuration->eal_initialized = 1;
  int ret = rte_eal_init(g_configuration->eal_argc, argp);
  if (ret < 0) {
    g_configuration->eal_initialized = 0;
    return ret;
  }
  return 0;
}

static int dpdk_helper_run(void) {
  char errbuf[ERR_BUF_SIZE];
  pid_t ppid = getppid();
  g_configuration->helper_status = DPDK_HELPER_WAITING_ON_PRIMARY;

  while (1) {
    /* sem_timedwait() to avoid blocking forever */
    cdtime_t now = cdtime();
    cdtime_t safety_period = MS_TO_CDTIME_T(1500);
    int ret =
        sem_timedwait(&g_configuration->sema_helper_get_stats,
                      &CDTIME_T_TO_TIMESPEC(now + safety_period +
                                            g_configuration->interval * 2));

    if (ret == -1 && errno == ETIMEDOUT) {
      ERROR("dpdkstat-helper: sem timedwait()"
            " timeout, did collectd terminate?");
      dpdk_helper_stop(RESET);
    }
    /* Parent PID change means collectd died so quit the helper process. */
    if (ppid != getppid()) {
      WARNING("dpdkstat-helper: parent PID changed, quitting.");
      dpdk_helper_stop(RESET);
    }

    /* Checking for DPDK primary process. */
    if (!rte_eal_primary_proc_alive(g_configuration->file_prefix)) {
      if (g_configuration->eal_initialized) {
        WARNING("dpdkstat-helper: no primary alive but EAL initialized:"
                " quitting.");
        dpdk_helper_stop(RESET);
      }
      g_configuration->helper_status = DPDK_HELPER_WAITING_ON_PRIMARY;
      /* Back to start of while() - waiting for primary process */
      continue;
    }

    if (!g_configuration->eal_initialized) {
      /* Initialize EAL. */
      int ret = dpdk_helper_init_eal();
      if (ret != 0) {
        WARNING("ERROR INITIALIZING EAL");
        dpdk_helper_stop(RESET);
      }
    }

    g_configuration->helper_status = DPDK_HELPER_ALIVE_SENDING_STATS;

    uint8_t nb_ports = rte_eth_dev_count();
    if (nb_ports == 0) {
      DEBUG("dpdkstat-helper: No DPDK ports available. "
            "Check bound devices to DPDK driver.");
      dpdk_helper_stop(RESET);
    }

    if (nb_ports > RTE_MAX_ETHPORTS)
      nb_ports = RTE_MAX_ETHPORTS;

    int len = 0, enabled_port_count = 0, num_xstats = 0;
    for (uint8_t i = 0; i < nb_ports; i++) {
      if (!(g_configuration->enabled_port_mask & (1 << i)))
        continue;

      if (g_configuration->helper_action == DPDK_HELPER_ACTION_COUNT_STATS) {
#if RTE_VERSION >= RTE_VERSION_16_07
        len = rte_eth_xstats_get_names(i, NULL, 0);
#else
        len = rte_eth_xstats_get(i, NULL, 0);
#endif
        if (len < 0) {
          ERROR("dpdkstat-helper: Cannot get xstats count on port %" PRIu8, i);
          break;
        }
        num_xstats += len;
        g_configuration->num_stats_in_port[enabled_port_count] = len;
        enabled_port_count++;
        continue;
      } else {
        len = g_configuration->num_stats_in_port[enabled_port_count];
        g_configuration->port_read_time[enabled_port_count] = cdtime();
        ret = rte_eth_xstats_get(
            i, g_configuration->xstats + num_xstats,
            g_configuration->num_stats_in_port[enabled_port_count]);
        if (ret < 0 || ret != len) {
          DEBUG("dpdkstat-helper: Error reading xstats on port %" PRIu8
                " len = %d",
                i, len);
          break;
        }
#if RTE_VERSION >= RTE_VERSION_16_07
        ret = rte_eth_xstats_get_names(i, g_configuration->xnames + num_xstats,
                                       len);
        if (ret < 0 || ret != len) {
          ERROR("dpdkstat-helper: Error reading xstat names (port=%d; len=%d)",
                i, len);
          break;
        }
#endif
        num_xstats += g_configuration->num_stats_in_port[enabled_port_count];
        enabled_port_count++;
      }
    } /* for (nb_ports) */

    if (g_configuration->helper_action == DPDK_HELPER_ACTION_COUNT_STATS) {
      g_configuration->num_ports = enabled_port_count;
      g_configuration->num_xstats = num_xstats;
      DEBUG("dpdkstat-helper ports: %" PRIu32 ", num stats: %" PRIu32,
            g_configuration->num_ports, g_configuration->num_xstats);
      /* Exit, allowing collectd to re-init SHM to the right size */
      g_configuration->collectd_reinit_shm = REINIT_SHM;
      dpdk_helper_stop(NO_RESET);
    }
    /* Now kick collectd send thread to send the stats */
    int err = sem_post(&g_configuration->sema_stats_in_shm);
    if (err) {
      WARNING("dpdkstat: error posting semaphore to helper %s",
              sstrerror(errno, errbuf, sizeof(errbuf)));
      dpdk_helper_stop(RESET);
    }
  } /* while(1) */

  return 0;
}

static void dpdk_submit_xstats(const char *dev_name, int count,
                               uint32_t counters, cdtime_t port_read_time) {
  for (uint32_t j = 0; j < counters; j++) {
    value_list_t vl = VALUE_LIST_INIT;
    char *counter_name;
    char *type_end;

    vl.values = &(value_t){.derive = (derive_t)DPDK_STATS_XSTAT_GET_VALUE(
                               g_configuration, count + j)};
    vl.values_len = 1; /* Submit stats one at a time */
    vl.time = port_read_time;
    sstrncpy(vl.plugin, "dpdkstat", sizeof(vl.plugin));
    sstrncpy(vl.plugin_instance, dev_name, sizeof(vl.plugin_instance));
    counter_name = DPDK_STATS_XSTAT_GET_NAME(g_configuration, count + j);
    if (counter_name == NULL) {
      WARNING("dpdkstat: Failed to get counter name.");
      return;
    }

    type_end = strrchr(counter_name, '_');

    if ((type_end != NULL) &&
        (strncmp(counter_name, "rx_", strlen("rx_")) == 0)) {
      if (strncmp(type_end, "_errors", strlen("_errors")) == 0) {
        sstrncpy(vl.type, "if_rx_errors", sizeof(vl.type));
      } else if (strncmp(type_end, "_dropped", strlen("_dropped")) == 0) {
        sstrncpy(vl.type, "if_rx_dropped", sizeof(vl.type));
      } else if (strncmp(type_end, "_bytes", strlen("_bytes")) == 0) {
        sstrncpy(vl.type, "if_rx_octets", sizeof(vl.type));
      } else if (strncmp(type_end, "_packets", strlen("_packets")) == 0) {
        sstrncpy(vl.type, "if_rx_packets", sizeof(vl.type));
      } else if (strncmp(type_end, "_placement", strlen("_placement")) == 0) {
        sstrncpy(vl.type, "if_rx_errors", sizeof(vl.type));
      } else if (strncmp(type_end, "_buff", strlen("_buff")) == 0) {
        sstrncpy(vl.type, "if_rx_errors", sizeof(vl.type));
      } else {
        /* Does not fit obvious type: use a more generic one */
        sstrncpy(vl.type, "derive", sizeof(vl.type));
      }

    } else if ((type_end != NULL) &&
               (strncmp(counter_name, "tx_", strlen("tx_"))) == 0) {
      if (strncmp(type_end, "_errors", strlen("_errors")) == 0) {
        sstrncpy(vl.type, "if_tx_errors", sizeof(vl.type));
      } else if (strncmp(type_end, "_dropped", strlen("_dropped")) == 0) {
        sstrncpy(vl.type, "if_tx_dropped", sizeof(vl.type));
      } else if (strncmp(type_end, "_bytes", strlen("_bytes")) == 0) {
        sstrncpy(vl.type, "if_tx_octets", sizeof(vl.type));
      } else if (strncmp(type_end, "_packets", strlen("_packets")) == 0) {
        sstrncpy(vl.type, "if_tx_packets", sizeof(vl.type));
      } else {
        /* Does not fit obvious type: use a more generic one */
        sstrncpy(vl.type, "derive", sizeof(vl.type));
      }
    } else if ((type_end != NULL) &&
               (strncmp(counter_name, "flow_", strlen("flow_"))) == 0) {

      if (strncmp(type_end, "_filters", strlen("_filters")) == 0) {
        sstrncpy(vl.type, "operations", sizeof(vl.type));
      } else if (strncmp(type_end, "_errors", strlen("_errors")) == 0) {
        sstrncpy(vl.type, "errors", sizeof(vl.type));
      } else if (strncmp(type_end, "_filters", strlen("_filters")) == 0) {
        sstrncpy(vl.type, "filter_result", sizeof(vl.type));
      }
    } else if ((type_end != NULL) &&
               (strncmp(counter_name, "mac_", strlen("mac_"))) == 0) {
      if (strncmp(type_end, "_errors", strlen("_errors")) == 0) {
        sstrncpy(vl.type, "errors", sizeof(vl.type));
      }
    } else {
      /* Does not fit obvious type, or strrchr error:
       *   use a more generic type */
      sstrncpy(vl.type, "derive", sizeof(vl.type));
    }

    sstrncpy(vl.type_instance, counter_name, sizeof(vl.type_instance));
    plugin_dispatch_values(&vl);
  }
}

static int dpdk_read(user_data_t *ud) {
  int ret = 0;

  /*
   * Check if SHM flag is set to be re-initialized. AKA DPDK ports have been
   * counted, so re-init SHM to be large enough to fit all the statistics.
   */
  if (g_configuration->collectd_reinit_shm) {
    DEBUG("dpdkstat: read() now reinit SHM then launching send-thread");
    dpdk_re_init_shm();
  }

  /*
   * Check if DPDK proc is alive, and has already counted port / stats. This
   * must be done in dpdk_read(), because the DPDK primary process may not be
   * alive at dpdk_init() time.
   */
  if (g_configuration->helper_status == DPDK_HELPER_NOT_INITIALIZED ||
      g_configuration->helper_status == DPDK_HELPER_GRACEFUL_QUIT) {
    int action = DPDK_HELPER_ACTION_SEND_STATS;
    if (g_configuration->num_xstats == 0)
      action = DPDK_HELPER_ACTION_COUNT_STATS;
    /* Spawn the helper thread to count stats or to read stats. */
    int err = dpdk_helper_spawn(action);
    if (err) {
      char errbuf[ERR_BUF_SIZE];
      ERROR("dpdkstat: error spawning helper %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
      return -1;
    }
  }

  pid_t ws = waitpid(g_configuration->helper_pid, NULL, WNOHANG);
  /*
   * Conditions under which to respawn helper:
   *  waitpid() fails, helper process died (or quit), so respawn
   */
  _Bool respawn_helper = 0;
  if (ws != 0) {
    respawn_helper = 1;
  }

  char buf[DPDKSTAT_MAX_BUFFER_SIZE];
  char out[DPDKSTAT_MAX_BUFFER_SIZE];

  /* non blocking check on helper logging pipe */
  struct pollfd fds = {
      .fd = g_configuration->helper_pipes[0], .events = POLLIN,
  };
  int data_avail = poll(&fds, 1, 0);
  if (data_avail < 0) {
    char errbuf[ERR_BUF_SIZE];
    if (errno != EINTR || errno != EAGAIN)
      ERROR("dpdkstats: poll(2) failed: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
  }
  while (data_avail) {
    int nbytes = read(g_configuration->helper_pipes[0], buf, sizeof(buf));
    if (nbytes <= 0)
      break;
    ssnprintf(out, nbytes, "%s", buf);
    DEBUG("dpdkstat: helper-proc: %s", out);
  }

  if (respawn_helper) {
    if (g_configuration->helper_pid)
      dpdk_helper_stop(RESET);
    dpdk_helper_spawn(DPDK_HELPER_ACTION_COUNT_STATS);
  }

  /* Kick helper process through SHM */
  sem_post(&g_configuration->sema_helper_get_stats);

  cdtime_t now = cdtime();
  ret = sem_timedwait(&g_configuration->sema_stats_in_shm,
                      &CDTIME_T_TO_TIMESPEC(now + g_configuration->interval));
  if (ret == -1) {
    if (errno == ETIMEDOUT)
      DEBUG(
          "dpdkstat: timeout in collectd thread: is a DPDK Primary running? ");
    return 0;
  }

  /* Dispatch the stats.*/
  uint32_t count = 0, port_num = 0;

  for (uint32_t i = 0; i < g_configuration->num_ports; i++) {
    char dev_name[64];
    cdtime_t port_read_time = g_configuration->port_read_time[i];
    uint32_t counters_num = g_configuration->num_stats_in_port[i];
    size_t ports_max = CHAR_BIT * sizeof(g_configuration->enabled_port_mask);
    for (size_t j = port_num; j < ports_max; j++) {
      if ((g_configuration->enabled_port_mask & (1 << j)) != 0)
        break;
      port_num++;
    }

    if (g_configuration->port_name[i][0] != 0)
      ssnprintf(dev_name, sizeof(dev_name), "%s",
                g_configuration->port_name[i]);
    else
      ssnprintf(dev_name, sizeof(dev_name), "port.%" PRIu32, port_num);
    dpdk_submit_xstats(dev_name, count, counters_num, port_read_time);
    count += counters_num;
    port_num++;
  } /* for each port */
  return 0;
}

static int dpdk_shm_cleanup(void) {
  int ret = munmap(g_configuration, sizeof(dpdk_config_t));
  g_configuration = 0;
  if (ret) {
    ERROR("dpdkstat: munmap returned %d", ret);
    return ret;
  }
  ret = shm_unlink(DPDK_SHM_NAME);
  if (ret) {
    ERROR("dpdkstat: shm_unlink returned %d", ret);
    return ret;
  }
  return 0;
}

static int dpdk_shutdown(void) {
  int ret = 0;
  char errbuf[ERR_BUF_SIZE];
  close(g_configuration->helper_pipes[1]);
  int err = kill(g_configuration->helper_pid, SIGKILL);
  if (err) {
    ERROR("dpdkstat: error sending sigkill to helper %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    ret = -1;
  }
  err = dpdk_shm_cleanup();
  if (err) {
    ERROR("dpdkstat: error cleaning up SHM: %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    ret = -1;
  }

  return ret;
}

void module_register(void) {
  plugin_register_complex_config("dpdkstat", dpdk_config);
  plugin_register_init("dpdkstat", dpdk_init);
  plugin_register_complex_read(NULL, "dpdkstat", dpdk_read, 0, NULL);
  plugin_register_shutdown("dpdkstat", dpdk_shutdown);
}
