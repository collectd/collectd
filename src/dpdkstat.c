/*-
 * collectd - src/dpdkstat.c
 * MIT License
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
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
 */

#include "collectd.h"
#include "common.h" /* auxiliary functions */
#include "plugin.h" /* plugin_register_*, plugin_dispatch_values */
#include "utils_time.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <signal.h>
#include <stdarg.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/mman.h>
#include <sys/queue.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <poll.h>
#include <unistd.h>
#include <string.h>

#include <rte_config.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_common.h>
#include <rte_debug.h>
#include <rte_malloc.h>
#include <rte_memory.h>
#include <rte_memzone.h>
#include <rte_launch.h>
#include <rte_tailq.h>
#include <rte_lcore.h>
#include <rte_per_lcore.h>
#include <rte_debug.h>
#include <rte_log.h>
#include <rte_atomic.h>
#include <rte_branch_prediction.h>
#include <rte_string_fns.h>


#define DATA_MAX_NAME_LEN        64
#define DPDKSTAT_MAX_BUFFER_SIZE (4096*4)
#define DPDK_SHM_NAME "dpdk_collectd_stats_shm"
#define REINIT_SHM 1
#define RESET 1
#define NO_RESET 0

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
  uint32_t eal_argc;
  /* Helper info */
  int   collectd_reinit_shm;
  pid_t helper_pid;
  sem_t sema_helper_get_stats;
  sem_t sema_stats_in_shm;
  int   helper_pipes[2];
  enum DPDK_HELPER_STATUS helper_status;
  enum DPDK_HELPER_ACTION helper_action;
  /* xstats info */
  uint32_t num_ports;
  uint32_t num_xstats;
  cdtime_t port_read_time[RTE_MAX_ETHPORTS];
  uint32_t num_stats_in_port[RTE_MAX_ETHPORTS];
  struct rte_eth_link link_status[RTE_MAX_ETHPORTS];
  struct rte_eth_xstats xstats;
  /* rte_eth_xstats from here on until the end of the SHM */
};
typedef struct dpdk_config_s dpdk_config_t;

static int g_configured = 0;
static dpdk_config_t *g_configuration = 0;

static int dpdk_config_init_default(void);
static int dpdk_config(oconfig_item_t *ci);
static int dpdk_helper_init_eal(void);
static int dpdk_helper_run(void);
static int dpdk_helper_spawn(enum DPDK_HELPER_ACTION action);
static int dpdk_init (void);
static int dpdk_read(user_data_t *ud);
static int dpdk_shm_cleanup(void);
static int dpdk_shm_init(size_t size);
void module_register(void);

/* Write the default configuration to the g_configuration instances */
static int dpdk_config_init_default(void)
{
    g_configuration->interval = plugin_get_interval();
    WARNING("dpdkstat: No time interval was configured, default value %lu ms is set\n",
             CDTIME_T_TO_MS(g_configuration->interval));
    g_configuration->enabled_port_mask = 0;
    g_configuration->eal_argc = 2;
    g_configuration->eal_initialized = 0;
    snprintf(g_configuration->coremask, DATA_MAX_NAME_LEN, "%s", "0xf");
    snprintf(g_configuration->memory_channels, DATA_MAX_NAME_LEN, "%s", "1");
    snprintf(g_configuration->process_type, DATA_MAX_NAME_LEN, "%s", "secondary");
    snprintf(g_configuration->file_prefix, DATA_MAX_NAME_LEN, "%s",
             "/var/run/.rte_config");
  return 0;
}

static int dpdk_config(oconfig_item_t *ci)
{
  int i = 0, ret = 0;

  /* Initialize a POSIX SHared Memory (SHM) object. */
  dpdk_shm_init(sizeof(dpdk_config_t));

  /* Set defaults for config, overwritten by loop if config item exists */
  ret = dpdk_config_init_default();
  if(ret != 0) {
    return -1;
  }

  for (i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Interval", child->key) == 0) {
      g_configuration->interval =
            DOUBLE_TO_CDTIME_T (child->values[0].value.number);
      DEBUG("dpdkstat: Plugin Read Interval %lu milliseconds\n",
            CDTIME_T_TO_MS(g_configuration->interval));
    } else if (strcasecmp("Coremask", child->key) == 0) {
      snprintf(g_configuration->coremask, DATA_MAX_NAME_LEN, "%s",
               child->values[0].value.string);
      DEBUG("dpdkstat:COREMASK %s \n", g_configuration->coremask);
      g_configuration->eal_argc+=1;
    } else if (strcasecmp("MemoryChannels", child->key) == 0) {
      snprintf(g_configuration->memory_channels, DATA_MAX_NAME_LEN, "%s",
               child->values[0].value.string);
      DEBUG("dpdkstat:Memory Channels %s \n", g_configuration->memory_channels);
      g_configuration->eal_argc+=1;
    } else if (strcasecmp("SocketMemory", child->key) == 0) {
      snprintf(g_configuration->socket_memory, DATA_MAX_NAME_LEN, "%s",
               child->values[0].value.string);
      DEBUG("dpdkstat: socket mem %s \n", g_configuration->socket_memory);
      g_configuration->eal_argc+=1;
    } else if (strcasecmp("ProcessType", child->key) == 0) {
      snprintf(g_configuration->process_type, DATA_MAX_NAME_LEN, "%s",
               child->values[0].value.string);
      DEBUG("dpdkstat: proc type %s \n", g_configuration->process_type);
      g_configuration->eal_argc+=1;
    } else if (strcasecmp("FilePrefix", child->key) == 0) {
      snprintf(g_configuration->file_prefix, DATA_MAX_NAME_LEN, "/var/run/.%s_config",
               child->values[0].value.string);
      DEBUG("dpdkstat: file prefix %s \n", g_configuration->file_prefix);
      if (strcasecmp(g_configuration->file_prefix, "/var/run/.rte_config") != 0) {
        g_configuration->eal_argc+=1;
      }
    } else {
      WARNING ("dpdkstat: The config option \"%s\" is unknown.",
               child->key);
    }
  } /* End for (i = 0; i < ci->children_num; i++)*/
  g_configured = 1; /* Bypass configuration in dpdk_shm_init(). */

  return 0;
}

/* Initialize SHared Memory (SHM) for config and helper process */
static int dpdk_shm_init(size_t size)
{
  /*
   * Check if SHM is already configured: when config items are provided, the
   * config function initializes SHM. If there is no config, then init() will
   * just return.
   */
  if(g_configuration)
    return 0;

  /* Create and open a new object, or open an existing object. */
  int fd = shm_open(DPDK_SHM_NAME, O_CREAT | O_TRUNC | O_RDWR, 0666);
  if (fd < 0) {
    WARNING("dpdkstat:Failed to open %s as SHM:%s\n", DPDK_SHM_NAME,
            strerror(errno));
    goto fail;
  }
  /* Set the size of the shared memory object. */
  int ret = ftruncate(fd, size);
  if (ret != 0) {
    WARNING("dpdkstat:Failed to resize SHM:%s\n", strerror(errno));
    goto fail_close;
  }
  /* Map the shared memory object into this process' virtual address space. */
  g_configuration = (dpdk_config_t *)
                    mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (g_configuration == MAP_FAILED) {
    WARNING("dpdkstat:Failed to mmap SHM:%s\n", strerror(errno));
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
  sem_init(&g_configuration->sema_helper_get_stats, 1, 0);
  sem_init(&g_configuration->sema_stats_in_shm, 1, 0);
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

static int dpdk_re_init_shm()
{
  dpdk_config_t temp_config;
  memcpy(&temp_config,g_configuration, sizeof(dpdk_config_t));
  DEBUG("dpdkstat: %s: ports %d, xstats %d\n", __func__, temp_config.num_ports,
        temp_config.num_xstats);

  int shm_xstats_size = sizeof(dpdk_config_t) + (sizeof(struct rte_eth_xstats) *
                        g_configuration->num_xstats);
  DEBUG("=== SHM new size for %d xstats\n", g_configuration->num_xstats);

  int err = dpdk_shm_cleanup();
  if (err)
    ERROR("dpdkstat: Error in shm_cleanup in %s\n", __func__);

  err = dpdk_shm_init(shm_xstats_size);
  if (err)
    ERROR("dpdkstat: Error in shm_init in %s\n", __func__);

  /* If the XML config() function has been run, dont re-initialize defaults */
  if(!g_configured)
    dpdk_config_init_default();

  memcpy(g_configuration,&temp_config, sizeof(dpdk_config_t));
  g_configuration->collectd_reinit_shm = 0;

  return 0;
}

static int dpdk_init (void)
{
  int ret = 0;
  int err = dpdk_shm_init(sizeof(dpdk_config_t));
  if (err)
    ERROR("dpdkstat: %s : error %d in shm_init()", __func__, err);

  /* If the XML config() function has been run, dont re-initialize defaults */
  if(!g_configured) {
    ret = dpdk_config_init_default();
    if (ret != 0) {
      return -1;
    }
  }

  plugin_register_complex_read (NULL, "dpdkstat", dpdk_read,
                                g_configuration->interval, NULL);
  return 0;
}

static int dpdk_helper_exit(int reset)
{
  g_configuration->helper_status = DPDK_HELPER_GRACEFUL_QUIT;
  if(reset) {
    g_configuration->eal_initialized = 0;
    g_configuration->num_ports = 0;
    memset(&g_configuration->xstats, 0, g_configuration->num_xstats* sizeof(struct rte_eth_xstats));
    g_configuration->num_xstats = 0;
    int i =0;
    for(;i < RTE_MAX_ETHPORTS; i++)
      g_configuration->num_stats_in_port[i] = 0;
  }
  close(g_configuration->helper_pipes[1]);
  kill(g_configuration->helper_pid, SIGKILL);
  return 0;
}

static int dpdk_helper_spawn(enum DPDK_HELPER_ACTION action)
{
  g_configuration->eal_initialized = 0;
  g_configuration->helper_action = action;
  /*
   * Create a pipe for helper stdout back to collectd. This is necessary for
   * logging EAL failures, as rte_eal_init() calls rte_panic().
   */
  if(g_configuration->helper_pipes[1]) {
    DEBUG("dpdkstat: collectd closing helper pipe %d\n",
          g_configuration->helper_pipes[1]);
  } else {
    DEBUG("dpdkstat: collectd helper pipe %d, not closing\n",
          g_configuration->helper_pipes[1]);
  }
  if(pipe(g_configuration->helper_pipes) != 0) {
    DEBUG("dpdkstat: Could not create helper pipe: %s\n", strerror(errno));
    return -1;
  }

  int pipe0_flags = fcntl(g_configuration->helper_pipes[1], F_GETFL, 0);
  int pipe1_flags = fcntl(g_configuration->helper_pipes[0], F_GETFL, 0);
  fcntl(g_configuration->helper_pipes[1], F_SETFL, pipe1_flags | O_NONBLOCK);
  fcntl(g_configuration->helper_pipes[0], F_SETFL, pipe0_flags | O_NONBLOCK);

  pid_t pid = fork();
  if (pid > 0) {
    close(g_configuration->helper_pipes[1]);
    g_configuration->helper_pid = pid;
    DEBUG("dpdkstat: helper pid %u\n", g_configuration->helper_pid);
    /* Kick helper once its alive to have it start processing */
    sem_post(&g_configuration->sema_helper_get_stats);
  } else if (pid == 0) {
    /* Replace stdout with a pipe to collectd. */
    close(g_configuration->helper_pipes[0]);
    close(STDOUT_FILENO);
    dup2(g_configuration->helper_pipes[1], STDOUT_FILENO);
    dpdk_helper_run();
  } else {
    ERROR("dpdkstat: Failed to fork helper process: %s\n", strerror(errno));
    return -1;
  }
  return 0;
}

/*
 * Initialize the DPDK EAL, if this returns, EAL is successfully initialized.
 * On failure, the EAL prints an error message, and the helper process exits.
 */
static int dpdk_helper_init_eal(void)
{
  g_configuration->helper_status = DPDK_HELPER_INITIALIZING_EAL;
  char *argp[(g_configuration->eal_argc) + 1];
  int i = 0;

  argp[i++] = "collectd-dpdk";
  if(strcasecmp(g_configuration->coremask, "") != 0) {
    argp[i++] = "-c";
    argp[i++] = g_configuration->coremask;
  }
  if(strcasecmp(g_configuration->memory_channels, "") != 0) {
    argp[i++] = "-n";
    argp[i++] = g_configuration->memory_channels;
  }
  if(strcasecmp(g_configuration->socket_memory, "") != 0) {
    argp[i++] = "--socket-mem";
    argp[i++] = g_configuration->socket_memory;
  }
  if(strcasecmp(g_configuration->file_prefix, "") != 0 &&
     strcasecmp(g_configuration->file_prefix, "/var/run/.rte_config") != 0) {
    argp[i++] = "--file-prefix";
    argp[i++] = g_configuration->file_prefix;
  }
  if(strcasecmp(g_configuration->process_type, "") != 0) {
    argp[i++] = "--proc-type";
    argp[i++] = g_configuration->process_type;
  }
  g_configuration->eal_argc = i;

  g_configuration->eal_initialized = 1;
  int ret = rte_eal_init(g_configuration->eal_argc, argp);
  if (ret < 0) {
    g_configuration->eal_initialized = 0;
    printf("dpdkstat: ERROR initializing EAL ret = %d\n", ret);
    printf("dpdkstat: EAL arguments: ");
    for (i=0; i< g_configuration->eal_argc; i++) {
      printf("%s ", argp[i]);
    }
    printf("\n");
    return -1;
  }
  return 0;
}

static int dpdk_helper_run (void)
{
  pid_t ppid = getppid();
  g_configuration->helper_status = DPDK_HELPER_WAITING_ON_PRIMARY;

   while(1) {
    /* sem_timedwait() to avoid blocking forever */
    struct timespec ts;
    cdtime_t now = cdtime();
    cdtime_t half_sec = MS_TO_CDTIME_T(1500);
    CDTIME_T_TO_TIMESPEC(now + half_sec + g_configuration->interval *2, &ts);
    int ret = sem_timedwait(&g_configuration->sema_helper_get_stats, &ts);

    if(ret == -1 && errno == ETIMEDOUT) {
      ERROR("dpdkstat-helper: sem timedwait()"
             " timeout, did collectd terminate?\n");
      dpdk_helper_exit(RESET);
    }

    /* Parent PID change means collectd died so quit the helper process. */
    if (ppid != getppid()) {
      WARNING("dpdkstat-helper: parent PID changed, quitting.\n");
      dpdk_helper_exit(RESET);
    }

    /* Checking for DPDK primary process. */
    if (!rte_eal_primary_proc_alive(g_configuration->file_prefix)) {
      if(g_configuration->eal_initialized) {
        WARNING("dpdkstat-helper: no primary alive but EAL initialized:"
              " quitting.\n");
        dpdk_helper_exit(RESET);
      }
      g_configuration->helper_status = DPDK_HELPER_WAITING_ON_PRIMARY;
      /* Back to start of while() - waiting for primary process */
      continue;
    }

    if(!g_configuration->eal_initialized) {
      /* Initialize EAL. */
      int ret = dpdk_helper_init_eal();
      if(ret != 0)
        dpdk_helper_exit(RESET);
    }

    g_configuration->helper_status = DPDK_HELPER_ALIVE_SENDING_STATS;

    uint8_t nb_ports;
    nb_ports = rte_eth_dev_count();
    if (nb_ports == 0) {
      DEBUG("dpdkstat-helper: No DPDK ports available. "
              "Check bound devices to DPDK driver.\n");
      return 0;
    }

    if (nb_ports > RTE_MAX_ETHPORTS)
      nb_ports = RTE_MAX_ETHPORTS;
    /* If no port mask was specified enable all ports*/
    if (g_configuration->enabled_port_mask == 0)
      g_configuration->enabled_port_mask = 0xffff;

    int i, len = 0, enabled_port_count = 0, num_xstats = 0;
    for (i = 0; i < nb_ports; i++) {
      if (g_configuration->enabled_port_mask & (1 << i)) {
        if(g_configuration->helper_action == DPDK_HELPER_ACTION_COUNT_STATS) {
          len = rte_eth_xstats_get(i, NULL, 0);
          if (len < 0) {
            ERROR("dpdkstat-helper: Cannot get xstats count\n");
            return -1;
          }
          num_xstats += len;
          g_configuration->num_stats_in_port[enabled_port_count] = len;
          enabled_port_count++;
          continue;
        } else {
          len = g_configuration->num_stats_in_port[enabled_port_count];
          g_configuration->port_read_time[enabled_port_count] = cdtime();
          ret = rte_eth_xstats_get(i, &g_configuration->xstats + num_xstats,
                                   g_configuration->num_stats_in_port[i]);
          if (ret < 0 || ret != len) {
            DEBUG("dpdkstat-helper: Error reading xstats on port %d len = %d\n",
                  i, len);
            return -1;
          }
          num_xstats += g_configuration->num_stats_in_port[i];
        }
      } /* if (enabled_port_mask) */
    } /* for (nb_ports) */

    if(g_configuration->helper_action == DPDK_HELPER_ACTION_COUNT_STATS) {
      g_configuration->num_ports  = enabled_port_count;
      g_configuration->num_xstats = num_xstats;
      DEBUG("dpdkstat-helper ports: %d, num stats: %d\n",
            g_configuration->num_ports, g_configuration->num_xstats);
      /* Exit, allowing collectd to re-init SHM to the right size */
      g_configuration->collectd_reinit_shm = REINIT_SHM;
      dpdk_helper_exit(NO_RESET);
    }
    /* Now kick collectd send thread to send the stats */
    sem_post(&g_configuration->sema_stats_in_shm);
  } /* while(1) */

  return 0;
}

static int dpdk_read (user_data_t *ud)
{
  int ret = 0;

  /*
   * Check if SHM flag is set to be re-initialized. AKA DPDK ports have been
   * counted, so re-init SHM to be large enough to fit all the statistics.
   */
  if(g_configuration->collectd_reinit_shm) {
    DEBUG("dpdkstat: read() now reinit SHM then launching send-thread\n");
    dpdk_re_init_shm();
  }

  /*
   * Check if DPDK proc is alive, and has already counted port / stats. This
   * must be done in dpdk_read(), because the DPDK primary process may not be
   * alive at dpdk_init() time.
   */
  if(g_configuration->helper_status == DPDK_HELPER_NOT_INITIALIZED ||
     g_configuration->helper_status == DPDK_HELPER_GRACEFUL_QUIT) {
      int action = DPDK_HELPER_ACTION_SEND_STATS;
      if(g_configuration->num_xstats == 0)
        action = DPDK_HELPER_ACTION_COUNT_STATS;
      /* Spawn the helper thread to count stats or to read stats. */
      dpdk_helper_spawn(action);
    }

  int exit_status;
  pid_t ws = waitpid(g_configuration->helper_pid, &exit_status, WNOHANG);
  /*
   * Conditions under which to respawn helper:
   *  waitpid() fails, helper process died (or quit), so respawn
   */
  int respawn_helper = 0;
  if(ws != 0) {
    respawn_helper = 1;
  }

  char buf[DPDKSTAT_MAX_BUFFER_SIZE];
  char out[DPDKSTAT_MAX_BUFFER_SIZE];

  /* non blocking check on helper logging pipe */
  struct pollfd fds;
  fds.fd = g_configuration->helper_pipes[0];
  fds.events = POLLIN;
  int data_avail = poll(&fds, 1, 0);
  while(data_avail) {
    int nbytes = read(g_configuration->helper_pipes[0], buf, sizeof(buf));
    if(nbytes <= 0)
      break;
    snprintf( out, nbytes, "%s", buf);
    DEBUG("dpdkstat: helper-proc: %s\n", out);
  }

  if(respawn_helper) {
    if (g_configuration->helper_pid)
      dpdk_helper_exit(RESET);
    dpdk_helper_spawn(DPDK_HELPER_ACTION_COUNT_STATS);
  }

  struct timespec helper_kick_time;
  clock_gettime(CLOCK_REALTIME, &helper_kick_time);
  /* Kick helper process through SHM */
  sem_post(&g_configuration->sema_helper_get_stats);

  struct timespec ts;
  cdtime_t now = cdtime();
  CDTIME_T_TO_TIMESPEC(now + g_configuration->interval, &ts);
  ret = sem_timedwait(&g_configuration->sema_stats_in_shm, &ts);
  if(ret == -1 && errno == ETIMEDOUT) {
    DEBUG("dpdkstat: timeout in collectd thread: is a DPDK Primary running? \n");
    return 0;
  }

  /* Dispatch the stats.*/
    int i, j, count = 0;

    for (i = 0; i < g_configuration->num_ports; i++) {
      cdtime_t time = g_configuration->port_read_time[i];
      char dev_name[64];
      int len = g_configuration->num_stats_in_port[i];
      snprintf(dev_name, sizeof(dev_name), "port.%d", i);
      struct rte_eth_xstats *xstats = (&g_configuration->xstats);
      xstats += count; /* pointer arithmetic to jump to each stats struct */
      for (j = 0; j < len; j++) {
        value_t dpdkstat_values[1];
        value_list_t dpdkstat_vl = VALUE_LIST_INIT;

        dpdkstat_values[0].counter = xstats[j].value;
        dpdkstat_vl.values = dpdkstat_values;
        dpdkstat_vl.values_len = 1; /* Submit stats one at a time */
        dpdkstat_vl.time = time;
        sstrncpy (dpdkstat_vl.host, hostname_g, sizeof (dpdkstat_vl.host));
        sstrncpy (dpdkstat_vl.plugin, "dpdkstat", sizeof (dpdkstat_vl.plugin));
        sstrncpy (dpdkstat_vl.plugin_instance, dev_name,
                  sizeof (dpdkstat_vl.plugin_instance));
        sstrncpy (dpdkstat_vl.type, "counter",
                  sizeof (dpdkstat_vl.type));
        sstrncpy (dpdkstat_vl.type_instance, xstats[j].name,
                  sizeof (dpdkstat_vl.type_instance));
        plugin_dispatch_values (&dpdkstat_vl);
      }
      count += len;
    } /* for each port */
  return 0;
}

static int dpdk_shm_cleanup(void)
{
  int ret = munmap(g_configuration, sizeof(dpdk_config_t));
  g_configuration = 0;
  if(ret) {
    WARNING("dpdkstat: munmap returned %d\n", ret);
    return ret;
  }
  ret = shm_unlink(DPDK_SHM_NAME);
  if(ret) {
    WARNING("dpdkstat: shm_unlink returned %d\n", ret);
    return ret;
  }
  return 0;
}

static int dpdk_shutdown (void)
{
  close(g_configuration->helper_pipes[1]);
  kill(g_configuration->helper_pid, SIGKILL);
  int ret = dpdk_shm_cleanup();

  return ret;
}

void module_register (void)
{
  plugin_register_complex_config ("dpdkstat", dpdk_config);
  plugin_register_init ("dpdkstat", dpdk_init);
  plugin_register_shutdown ("dpdkstat", dpdk_shutdown);
}
