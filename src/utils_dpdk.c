/**
 * collectd - src/utils_dpdk.c
 * MIT License
 *
 * Copyright(c) 2016 Intel Corporation. All rights reserved.
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
 *   Maryam Tahhan <maryam.tahhan@intel.com>
 *   Harry van Haaren <harry.van.haaren@intel.com>
 *   Serhiy Pshyk <serhiyx.pshyk@intel.com>
 **/

#include "collectd.h"

#include <poll.h>
#include <semaphore.h>
#include <sys/mman.h>

#include <rte_config.h>
#include <rte_eal.h>

#include "common.h"
#include "utils_dpdk.h"

#define DPDK_DEFAULT_RTE_CONFIG "/var/run/.rte_config"
#define DPDK_EAL_ARGC 5
#define DPDK_MAX_BUFFER_SIZE (4096 * 4)
#define DPDK_CDM_DEFAULT_TIMEOUT 10000

enum DPDK_HELPER_STATUS {
  DPDK_HELPER_NOT_INITIALIZED = 0,
  DPDK_HELPER_INITIALIZING,
  DPDK_HELPER_WAITING_ON_PRIMARY,
  DPDK_HELPER_INITIALIZING_EAL,
  DPDK_HELPER_ALIVE_SENDING_EVENTS,
  DPDK_HELPER_GRACEFUL_QUIT,
};

#define DPDK_HELPER_TRACE(_name)                                               \
  DEBUG("%s:%s:%d pid=%lu", _name, __FUNCTION__, __LINE__, (long)getpid())

#define DPDK_HELPER_USE_PIPES

struct dpdk_helper_ctx_s {

  dpdk_eal_config_t eal_config;
  int eal_initialized;

  size_t shm_size;
  const char *shm_name;

  sem_t sema_cmd_start;
  sem_t sema_cmd_complete;
  cdtime_t cmd_wait_time;

  pid_t pid;
#ifdef DPDK_HELPER_USE_PIPES
  int pipes[2];
#endif /* DPDK_HELPER_USE_PIPES */
  int status;

  int cmd;
  int cmd_result;

  char priv_data[];
};

static int dpdk_shm_init(const char *name, size_t size, void **map);
static int dpdk_shm_cleanup(const char *name, size_t size, void *map);

static int dpdk_helper_spawn(dpdk_helper_ctx_t *phc);
static int dpdk_helper_worker(dpdk_helper_ctx_t *phc);
static int dpdk_helper_eal_init(dpdk_helper_ctx_t *phc);
static int dpdk_helper_cmd_wait(dpdk_helper_ctx_t *phc, pid_t ppid);
static int dpdk_helper_exit_command(dpdk_helper_ctx_t *phc,
                                    enum DPDK_HELPER_STATUS status);
static int dpdk_helper_exit(dpdk_helper_ctx_t *phc,
                            enum DPDK_HELPER_STATUS status);
static int dpdk_helper_status_check(dpdk_helper_ctx_t *phc);
static void dpdk_helper_config_default(dpdk_helper_ctx_t *phc);
static const char *dpdk_helper_status_str(enum DPDK_HELPER_STATUS status);

static void dpdk_helper_config_default(dpdk_helper_ctx_t *phc) {
  if (phc == NULL)
    return;

  DPDK_HELPER_TRACE(phc->shm_name);

  ssnprintf(phc->eal_config.coremask, DATA_MAX_NAME_LEN, "%s", "0xf");
  ssnprintf(phc->eal_config.memory_channels, DATA_MAX_NAME_LEN, "%s", "1");
  ssnprintf(phc->eal_config.process_type, DATA_MAX_NAME_LEN, "%s", "secondary");
  ssnprintf(phc->eal_config.file_prefix, DATA_MAX_NAME_LEN, "%s",
            DPDK_DEFAULT_RTE_CONFIG);
}

int dpdk_helper_eal_config_set(dpdk_helper_ctx_t *phc, dpdk_eal_config_t *ec) {
  if (phc == NULL) {
    ERROR("Invalid argument (phc)");
    return -EINVAL;
  }

  DPDK_HELPER_TRACE(phc->shm_name);

  if (ec == NULL) {
    ERROR("Invalid argument (ec)");
    return -EINVAL;
  }

  memcpy(&phc->eal_config, ec, sizeof(dpdk_eal_config_t));

  return 0;
}

int dpdk_helper_eal_config_get(dpdk_helper_ctx_t *phc, dpdk_eal_config_t *ec) {
  if (phc == NULL) {
    ERROR("Invalid argument (phc)");
    return -EINVAL;
  }

  DPDK_HELPER_TRACE(phc->shm_name);

  if (ec == NULL) {
    ERROR("Invalid argument (ec)");
    return -EINVAL;
  }

  memcpy(ec, &phc->eal_config, sizeof(dpdk_eal_config_t));

  return 0;
}

int dpdk_helper_eal_config_parse(dpdk_helper_ctx_t *phc, oconfig_item_t *ci) {
  DPDK_HELPER_TRACE(phc->shm_name);

  if (phc == NULL) {
    ERROR("Invalid argument (phc)");
    return -EINVAL;
  }

  if (ci == NULL) {
    ERROR("Invalid argument (ci)");
    return -EINVAL;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Coremask", child->key) == 0) {
      cf_util_get_string_buffer(child, phc->eal_config.coremask,
                                sizeof(phc->eal_config.coremask));
      DEBUG("dpdk_common: EAL:Coremask %s", phc->eal_config.coremask);
    } else if (strcasecmp("MemoryChannels", child->key) == 0) {
      cf_util_get_string_buffer(child, phc->eal_config.memory_channels,
                                sizeof(phc->eal_config.memory_channels));
      DEBUG("dpdk_common: EAL:Memory Channels %s",
            phc->eal_config.memory_channels);
    } else if (strcasecmp("SocketMemory", child->key) == 0) {
      cf_util_get_string_buffer(child, phc->eal_config.socket_memory,
                                sizeof(phc->eal_config.socket_memory));
      DEBUG("dpdk_common: EAL:Socket memory %s", phc->eal_config.socket_memory);
    } else if (strcasecmp("ProcessType", child->key) == 0) {
      cf_util_get_string_buffer(child, phc->eal_config.process_type,
                                sizeof(phc->eal_config.process_type));
      DEBUG("dpdk_common: EAL:Process type %s", phc->eal_config.process_type);
    } else if ((strcasecmp("FilePrefix", child->key) == 0) &&
               (child->values[0].type == OCONFIG_TYPE_STRING)) {
      ssnprintf(phc->eal_config.file_prefix, DATA_MAX_NAME_LEN,
                "/var/run/.%s_config", child->values[0].value.string);
      DEBUG("dpdk_common: EAL:File prefix %s", phc->eal_config.file_prefix);
    }
  }

  return 0;
}

static int dpdk_shm_init(const char *name, size_t size, void **map) {
  DPDK_HELPER_TRACE(name);

  char errbuf[ERR_BUF_SIZE];

  int fd = shm_open(name, O_CREAT | O_TRUNC | O_RDWR, 0666);
  if (fd < 0) {
    WARNING("dpdk_shm_init: Failed to open %s as SHM:%s\n", name,
            sstrerror(errno, errbuf, sizeof(errbuf)));
    goto fail;
  }

  int ret = ftruncate(fd, size);
  if (ret != 0) {
    WARNING("dpdk_shm_init: Failed to resize SHM:%s\n",
            sstrerror(errno, errbuf, sizeof(errbuf)));
    goto fail_close;
  }

  *map = mmap(0, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (*map == MAP_FAILED) {
    WARNING("dpdk_shm_init:Failed to mmap SHM:%s\n",
            sstrerror(errno, errbuf, sizeof(errbuf)));
    goto fail_close;
  }
  /*
   * Close the file descriptor, the shared memory object still exists
   * and can only be removed by calling shm_unlink().
   */
  close(fd);

  memset(*map, 0, size);

  return 0;

fail_close:
  close(fd);
fail:
  *map = NULL;
  return -1;
}

static int dpdk_shm_cleanup(const char *name, size_t size, void *map) {
  DPDK_HELPER_TRACE(name);

  int ret = munmap(map, size);
  if (ret) {
    ERROR("munmap returned %d\n", ret);
  }

  ret = shm_unlink(name);
  if (ret) {
    ERROR("shm_unlink returned %d\n", ret);
  }

  return 0;
}

inline void *dpdk_helper_priv_get(dpdk_helper_ctx_t *phc) {
  if (phc)
    return (void *)phc->priv_data;

  return NULL;
}

int dpdk_helper_data_size_get(dpdk_helper_ctx_t *phc) {
  if (phc == NULL) {
    DPDK_CHILD_LOG("Invalid argument(phc)\n");
    return -EINVAL;
  }

  return (phc->shm_size - sizeof(dpdk_helper_ctx_t));
}

int dpdk_helper_init(const char *name, size_t data_size,
                     dpdk_helper_ctx_t **pphc) {
  int err = 0;
  dpdk_helper_ctx_t *phc = NULL;
  size_t shm_size = sizeof(dpdk_helper_ctx_t) + data_size;
  char errbuf[ERR_BUF_SIZE];

  if (pphc == NULL) {
    ERROR("%s:Invalid argument(pphc)", __FUNCTION__);
    return -EINVAL;
  }

  if (name == NULL) {
    ERROR("%s:Invalid argument(name)", __FUNCTION__);
    return -EINVAL;
  }

  DPDK_HELPER_TRACE(name);

  /* Allocate dpdk_helper_ctx_t and
  * initialize a POSIX SHared Memory (SHM) object.
  */
  err = dpdk_shm_init(name, shm_size, (void **)&phc);
  if (err != 0) {
    return -errno;
  }

  err = sem_init(&phc->sema_cmd_start, 1, 0);
  if (err != 0) {
    ERROR("sema_cmd_start semaphore init failed: %s\n",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    dpdk_shm_cleanup(name, shm_size, (void *)phc);
    return -errno;
  }

  err = sem_init(&phc->sema_cmd_complete, 1, 0);
  if (err != 0) {
    ERROR("sema_cmd_complete semaphore init failed: %s\n",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    sem_destroy(&phc->sema_cmd_start);
    dpdk_shm_cleanup(name, shm_size, (void *)phc);
    return -errno;
  }

  phc->shm_size = shm_size;
  phc->shm_name = name;

  dpdk_helper_config_default(phc);

  *pphc = phc;

  return 0;
}

int dpdk_helper_shutdown(dpdk_helper_ctx_t *phc) {
  if (phc == NULL) {
    ERROR("%s:Invalid argument(phc)", __FUNCTION__);
    return -EINVAL;
  }

  DPDK_HELPER_TRACE(phc->shm_name);

#ifdef DPDK_HELPER_USE_PIPES
  close(phc->pipes[1]);
#endif

  if (phc->status != DPDK_HELPER_NOT_INITIALIZED) {
    dpdk_helper_exit_command(phc, DPDK_HELPER_GRACEFUL_QUIT);
  }

  sem_destroy(&phc->sema_cmd_start);
  sem_destroy(&phc->sema_cmd_complete);
  dpdk_shm_cleanup(phc->shm_name, phc->shm_size, (void *)phc);

  return 0;
}

static int dpdk_helper_spawn(dpdk_helper_ctx_t *phc) {
  char errbuf[ERR_BUF_SIZE];
  if (phc == NULL) {
    ERROR("Invalid argument(phc)");
    return -EINVAL;
  }

  DPDK_HELPER_TRACE(phc->shm_name);

  phc->eal_initialized = 0;
  phc->cmd_wait_time = MS_TO_CDTIME_T(DPDK_CDM_DEFAULT_TIMEOUT);

#ifdef DPDK_HELPER_USE_PIPES
  /*
   * Create a pipe for helper stdout back to collectd. This is necessary for
   * logging EAL failures, as rte_eal_init() calls rte_panic().
   */
  if (phc->pipes[1]) {
    DEBUG("dpdk_helper_spawn: collectd closing helper pipe %d", phc->pipes[1]);
  } else {
    DEBUG("dpdk_helper_spawn: collectd helper pipe %d, not closing",
          phc->pipes[1]);
  }

  if (pipe(phc->pipes) != 0) {
    DEBUG("dpdk_helper_spawn: Could not create helper pipe: %s\n",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    return -1;
  }

  int pipe0_flags = fcntl(phc->pipes[0], F_GETFL, 0);
  int pipe1_flags = fcntl(phc->pipes[1], F_GETFL, 0);
  if (pipe0_flags == -1 || pipe1_flags == -1) {
    WARNING("dpdk_helper_spawn: error setting up pipe flags: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
  }
  int pipe0_err = fcntl(phc->pipes[0], F_SETFL, pipe1_flags | O_NONBLOCK);
  int pipe1_err = fcntl(phc->pipes[1], F_SETFL, pipe0_flags | O_NONBLOCK);
  if (pipe0_err == -1 || pipe1_err == -1) {
    WARNING("dpdk_helper_spawn: error setting up pipes: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
  }
#endif /* DPDK_HELPER_USE_PIPES */

  pid_t pid = fork();
  if (pid > 0) {
    phc->pid = pid;
#ifdef DPDK_HELPER_USE_PIPES
    close(phc->pipes[1]);
#endif /* DPDK_HELPER_USE_PIPES */
    DEBUG("%s:dpdk_helper_spawn: helper pid %lu", phc->shm_name,
          (long)phc->pid);
  } else if (pid == 0) {
#ifdef DPDK_HELPER_USE_PIPES
    /* Replace stdout with a pipe to collectd. */
    close(phc->pipes[0]);
    close(STDOUT_FILENO);
    dup2(phc->pipes[1], STDOUT_FILENO);
#endif /* DPDK_HELPER_USE_PIPES */
    DPDK_CHILD_TRACE(phc->shm_name);
    dpdk_helper_worker(phc);
    exit(0);
  } else {
    ERROR("dpdk_helper_start: Failed to fork helper process: %s\n",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    return -1;
  }

  return 0;
}

static int dpdk_helper_exit(dpdk_helper_ctx_t *phc,
                            enum DPDK_HELPER_STATUS status) {
  DPDK_CHILD_LOG("%s:%s:%d %s\n", phc->shm_name, __FUNCTION__, __LINE__,
                 dpdk_helper_status_str(status));

#ifdef DPDK_HELPER_USE_PIPES
  close(phc->pipes[1]);
#endif /* DPDK_HELPER_USE_PIPES */

  phc->status = status;

  exit(0);

  return 0;
}

static int dpdk_helper_exit_command(dpdk_helper_ctx_t *phc,
                                    enum DPDK_HELPER_STATUS status) {
  char errbuf[ERR_BUF_SIZE];
  DPDK_HELPER_TRACE(phc->shm_name);

#ifdef DPDK_HELPER_USE_PIPES
  close(phc->pipes[1]);
#endif /* DPDK_HELPER_USE_PIPES */

  if (phc->status == DPDK_HELPER_ALIVE_SENDING_EVENTS) {
    phc->status = status;
    DEBUG("%s:%s:%d %s", phc->shm_name, __FUNCTION__, __LINE__,
          dpdk_helper_status_str(status));

    int ret = dpdk_helper_command(phc, DPDK_CMD_QUIT, NULL, 0);
    if (ret != 0) {
      DEBUG("%s:%s:%d kill helper (pid=%lu)", phc->shm_name, __FUNCTION__,
            __LINE__, (long)phc->pid);

      int err = kill(phc->pid, SIGKILL);
      if (err) {
        ERROR("%s error sending kill to helper: %s\n", __FUNCTION__,
              sstrerror(errno, errbuf, sizeof(errbuf)));
      }
    }
  } else {

    DEBUG("%s:%s:%d kill helper (pid=%lu)", phc->shm_name, __FUNCTION__,
          __LINE__, (long)phc->pid);

    int err = kill(phc->pid, SIGKILL);
    if (err) {
      ERROR("%s error sending kill to helper: %s\n", __FUNCTION__,
            sstrerror(errno, errbuf, sizeof(errbuf)));
    }
  }

  return 0;
}

static int dpdk_helper_eal_init(dpdk_helper_ctx_t *phc) {
  phc->status = DPDK_HELPER_INITIALIZING_EAL;
  DPDK_CHILD_LOG("%s:%s:%d DPDK_HELPER_INITIALIZING_EAL (start)\n",
                 phc->shm_name, __FUNCTION__, __LINE__);

  char *argp[DPDK_EAL_ARGC * 2 + 1];
  int argc = 0;

  /* EAL config must be initialized */
  assert(phc->eal_config.coremask[0] != 0);
  assert(phc->eal_config.memory_channels[0] != 0);
  assert(phc->eal_config.process_type[0] != 0);
  assert(phc->eal_config.file_prefix[0] != 0);

  argp[argc++] = "collectd-dpdk";

  argp[argc++] = "-c";
  argp[argc++] = phc->eal_config.coremask;

  argp[argc++] = "-n";
  argp[argc++] = phc->eal_config.memory_channels;

  if (strcasecmp(phc->eal_config.socket_memory, "") != 0) {
    argp[argc++] = "--socket-mem";
    argp[argc++] = phc->eal_config.socket_memory;
  }

  if (strcasecmp(phc->eal_config.file_prefix, DPDK_DEFAULT_RTE_CONFIG) != 0) {
    argp[argc++] = "--file-prefix";
    argp[argc++] = phc->eal_config.file_prefix;
  }

  argp[argc++] = "--proc-type";
  argp[argc++] = phc->eal_config.process_type;

  assert(argc <= (DPDK_EAL_ARGC * 2 + 1));

  int ret = rte_eal_init(argc, argp);

  if (ret < 0) {

    phc->eal_initialized = 0;

    DPDK_CHILD_LOG("dpdk_helper_eal_init: ERROR initializing EAL ret=%d\n",
                   ret);

    printf("dpdk_helper_eal_init: EAL arguments: ");
    for (int i = 0; i < argc; i++) {
      printf("%s ", argp[i]);
    }
    printf("\n");

    return ret;
  }

  phc->eal_initialized = 1;

  DPDK_CHILD_LOG("%s:%s:%d DPDK_HELPER_INITIALIZING_EAL (done)\n",
                 phc->shm_name, __FUNCTION__, __LINE__);

  return 0;
}

static int dpdk_helper_cmd_wait(dpdk_helper_ctx_t *phc, pid_t ppid) {
  DPDK_CHILD_TRACE(phc->shm_name);

  struct timespec ts;
  cdtime_t now = cdtime();
  cdtime_t cmd_wait_time = MS_TO_CDTIME_T(1500) + phc->cmd_wait_time * 2;
  ts = CDTIME_T_TO_TIMESPEC(now + cmd_wait_time);

  int ret = sem_timedwait(&phc->sema_cmd_start, &ts);
  DPDK_CHILD_LOG("%s:%s:%d pid=%lu got sema_cmd_start (ret=%d, errno=%d)\n",
                 phc->shm_name, __FUNCTION__, __LINE__, (long)getpid(), ret,
                 errno);

  if (phc->cmd == DPDK_CMD_QUIT) {
    DPDK_CHILD_LOG("%s:%s:%d pid=%lu exiting\n", phc->shm_name, __FUNCTION__,
                   __LINE__, (long)getpid());
    exit(0);
  } else if (ret == -1 && errno == ETIMEDOUT) {
    if (phc->status == DPDK_HELPER_ALIVE_SENDING_EVENTS) {
      DPDK_CHILD_LOG("%s:dpdk_helper_cmd_wait: sem timedwait()"
                     " timeout, did collectd terminate?\n",
                     phc->shm_name);
      dpdk_helper_exit(phc, DPDK_HELPER_GRACEFUL_QUIT);
    }
  }
#if COLLECT_DEBUG
  int val = 0;
  if (sem_getvalue(&phc->sema_cmd_start, &val) == 0)
    DPDK_CHILD_LOG("%s:%s:%d pid=%lu wait sema_cmd_start (value=%d)\n",
                   phc->shm_name, __FUNCTION__, __LINE__, (long)getpid(), val);
#endif

  /* Parent PID change means collectd died so quit the helper process. */
  if (ppid != getppid()) {
    DPDK_CHILD_LOG("dpdk_helper_cmd_wait: parent PID changed, quitting.\n");
    dpdk_helper_exit(phc, DPDK_HELPER_GRACEFUL_QUIT);
  }

  /* Checking for DPDK primary process. */
  if (!rte_eal_primary_proc_alive(phc->eal_config.file_prefix)) {
    if (phc->eal_initialized) {
      DPDK_CHILD_LOG(
          "%s:dpdk_helper_cmd_wait: no primary alive but EAL initialized:"
          " quitting.\n",
          phc->shm_name);
      dpdk_helper_exit(phc, DPDK_HELPER_NOT_INITIALIZED);
    }

    phc->status = DPDK_HELPER_WAITING_ON_PRIMARY;
    DPDK_CHILD_LOG("%s:%s:%d DPDK_HELPER_WAITING_ON_PRIMARY\n", phc->shm_name,
                   __FUNCTION__, __LINE__);

    return -1;
  }

  if (!phc->eal_initialized) {
    int ret = dpdk_helper_eal_init(phc);
    if (ret != 0) {
      DPDK_CHILD_LOG("Error initializing EAL\n");
      dpdk_helper_exit(phc, DPDK_HELPER_NOT_INITIALIZED);
    }
    phc->status = DPDK_HELPER_ALIVE_SENDING_EVENTS;
    DPDK_CHILD_LOG("%s:%s:%d DPDK_HELPER_ALIVE_SENDING_EVENTS\n", phc->shm_name,
                   __FUNCTION__, __LINE__);
    return -1;
  }

  return 0;
}

static int dpdk_helper_worker(dpdk_helper_ctx_t *phc) {
  DPDK_CHILD_TRACE(phc->shm_name);

  pid_t ppid = getppid();

  while (1) {
    if (dpdk_helper_cmd_wait(phc, ppid) == 0) {
      DPDK_CHILD_LOG("%s:%s:%d DPDK command handle (cmd=%d, pid=%lu)\n",
                     phc->shm_name, __FUNCTION__, __LINE__, phc->cmd,
                     (long)getpid());
      phc->cmd_result = dpdk_helper_command_handler(phc, phc->cmd);
    } else {
      phc->cmd_result = -1;
    }

    /* now kick collectd to get results */
    int err = sem_post(&phc->sema_cmd_complete);
    DPDK_CHILD_LOG("%s:%s:%d post sema_cmd_complete (pid=%lu)\n", phc->shm_name,
                   __FUNCTION__, __LINE__, (long)getpid());
    if (err) {
      char errbuf[ERR_BUF_SIZE];
      DPDK_CHILD_LOG("dpdk_helper_worker: error posting sema_cmd_complete "
                     "semaphore (%s)\n",
                     sstrerror(errno, errbuf, sizeof(errbuf)));
    }

#if COLLECT_DEBUG
    int val = 0;
    if (sem_getvalue(&phc->sema_cmd_complete, &val) == 0)
      DPDK_CHILD_LOG("%s:%s:%d pid=%lu sema_cmd_complete (value=%d)\n",
                     phc->shm_name, __FUNCTION__, __LINE__, (long)getpid(),
                     val);
#endif

  } /* while(1) */

  return 0;
}

static const char *dpdk_helper_status_str(enum DPDK_HELPER_STATUS status) {
  switch (status) {
  case DPDK_HELPER_ALIVE_SENDING_EVENTS:
    return "DPDK_HELPER_ALIVE_SENDING_EVENTS";
  case DPDK_HELPER_WAITING_ON_PRIMARY:
    return "DPDK_HELPER_WAITING_ON_PRIMARY";
  case DPDK_HELPER_INITIALIZING:
    return "DPDK_HELPER_INITIALIZING";
  case DPDK_HELPER_INITIALIZING_EAL:
    return "DPDK_HELPER_INITIALIZING_EAL";
  case DPDK_HELPER_GRACEFUL_QUIT:
    return "DPDK_HELPER_GRACEFUL_QUIT";
  case DPDK_HELPER_NOT_INITIALIZED:
    return "DPDK_HELPER_NOT_INITIALIZED";
  default:
    return "UNKNOWN";
  }
}

static int dpdk_helper_status_check(dpdk_helper_ctx_t *phc) {
  DEBUG("%s:%s:%d pid=%u %s", phc->shm_name, __FUNCTION__, __LINE__, getpid(),
        dpdk_helper_status_str(phc->status));
  char errbuf[ERR_BUF_SIZE];

  if (phc->status == DPDK_HELPER_GRACEFUL_QUIT) {
    return 0;
  } else if (phc->status == DPDK_HELPER_NOT_INITIALIZED) {
    phc->status = DPDK_HELPER_INITIALIZING;
    DEBUG("%s:%s:%d DPDK_HELPER_INITIALIZING", phc->shm_name, __FUNCTION__,
          __LINE__);
    int err = dpdk_helper_spawn(phc);
    if (err) {
      ERROR("dpdkstat: error spawning helper %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
    }
    return -1;
  }

  pid_t ws = waitpid(phc->pid, NULL, WNOHANG);
  if (ws != 0) {
    phc->status = DPDK_HELPER_INITIALIZING;
    DEBUG("%s:%s:%d DPDK_HELPER_INITIALIZING", phc->shm_name, __FUNCTION__,
          __LINE__);
    int err = dpdk_helper_spawn(phc);
    if (err) {
      ERROR("dpdkstat: error spawning helper %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
    }
    return -1;
  }

  if (phc->status == DPDK_HELPER_INITIALIZING_EAL) {
    return -1;
  }

  return 0;
}

#ifdef DPDK_HELPER_USE_PIPES
static void dpdk_helper_check_pipe(dpdk_helper_ctx_t *phc) {
  char buf[DPDK_MAX_BUFFER_SIZE];
  char out[DPDK_MAX_BUFFER_SIZE];

  /* non blocking check on helper logging pipe */
  struct pollfd fds = {
      .fd = phc->pipes[0], .events = POLLIN,
  };
  int data_avail = poll(&fds, 1, 0);
  if (data_avail < 0) {
    if (errno != EINTR || errno != EAGAIN) {
      char errbuf[ERR_BUF_SIZE];
      ERROR("%s: poll(2) failed: %s", phc->shm_name,
            sstrerror(errno, errbuf, sizeof(errbuf)));
    }
  }
  while (data_avail) {
    int nbytes = read(phc->pipes[0], buf, sizeof(buf));
    if (nbytes <= 0)
      break;
    sstrncpy(out, buf, nbytes);
    DEBUG("%s: helper process:\n%s", phc->shm_name, out);
  }
}
#endif /* DPDK_HELPER_USE_PIPES */

int dpdk_helper_command(dpdk_helper_ctx_t *phc, enum DPDK_CMD cmd, int *result,
                        cdtime_t cmd_wait_time) {
  if (phc == NULL) {
    ERROR("Invalid argument(phc)");
    return -EINVAL;
  }

  DEBUG("%s:%s:%d pid=%lu, cmd=%d", phc->shm_name, __FUNCTION__, __LINE__,
        (long)getpid(), cmd);

  phc->cmd_wait_time = cmd_wait_time;

  int ret = 0;

  ret = dpdk_helper_status_check(phc);

#ifdef DPDK_HELPER_USE_PIPES
  dpdk_helper_check_pipe(phc);
#endif /* DPDK_HELPER_USE_PIPES */

  if (ret != 0) {
    return ret;
  }

  DEBUG("%s: DPDK command execute (cmd=%d)", phc->shm_name, cmd);

  phc->cmd_result = 0;
  phc->cmd = cmd;

  /* kick helper to process command */
  int err = sem_post(&phc->sema_cmd_start);
  if (err) {
    char errbuf[ERR_BUF_SIZE];
    ERROR("dpdk_helper_worker: error posting sema_cmd_start semaphore (%s)\n",
          sstrerror(errno, errbuf, sizeof(errbuf)));
  }

#if COLLECT_DEBUG
  int val = 0;
  if (sem_getvalue(&phc->sema_cmd_start, &val) == 0)
    DEBUG("%s:dpdk_helper_command: post sema_cmd_start (value=%d)",
          phc->shm_name, val);
#endif

  if (phc->cmd != DPDK_CMD_QUIT) {

    /* wait for helper to complete processing */
    struct timespec ts;
    cdtime_t now = cdtime();

    if (phc->status != DPDK_HELPER_ALIVE_SENDING_EVENTS) {
      cmd_wait_time = MS_TO_CDTIME_T(DPDK_CDM_DEFAULT_TIMEOUT);
    }

    ts = CDTIME_T_TO_TIMESPEC(now + cmd_wait_time);
    ret = sem_timedwait(&phc->sema_cmd_complete, &ts);
    if (ret == -1 && errno == ETIMEDOUT) {
      DPDK_HELPER_TRACE(phc->shm_name);
      DEBUG("%s:sema_cmd_start: timeout in collectd thread: is a DPDK Primary "
            "running?",
            phc->shm_name);
      return -ETIMEDOUT;
    }

#if COLLECT_DEBUG
    val = 0;
    if (sem_getvalue(&phc->sema_cmd_complete, &val) == 0)
      DEBUG("%s:dpdk_helper_command: wait sema_cmd_complete (value=%d)",
            phc->shm_name, val);
#endif

    if (result) {
      *result = phc->cmd_result;
    }
  }

#ifdef DPDK_HELPER_USE_PIPES
  dpdk_helper_check_pipe(phc);
#endif /* DPDK_HELPER_USE_PIPES */

  DEBUG("%s: DPDK command complete (cmd=%d, result=%d)", phc->shm_name,
        phc->cmd, phc->cmd_result);

  return 0;
}

uint64_t strtoull_safe(const char *str, int *err) {
  uint64_t val = 0;
  char *endptr;
  int res = 0;

  val = strtoull(str, &endptr, 16);
  if (*endptr) {
    ERROR("%s Failed to parse the value %s, endptr=%c", __FUNCTION__, str,
          *endptr);
    res = -EINVAL;
  }
  if (err != NULL)
    *err = res;
  return val;
}

uint128_t str_to_uint128(const char *str, int len) {
  uint128_t lcore_mask;
  int err = 0;

  memset(&lcore_mask, 0, sizeof(uint128_t));

  if (len <= 2 || strncmp(str, "0x", 2) != 0) {
    ERROR("%s Value %s should be represened in hexadecimal format",
          __FUNCTION__, str);
    return lcore_mask;
  }

  if (len <= 18) {
    lcore_mask.low = strtoull_safe(str, &err);
    if (err)
      goto parse_out;
  } else {
    char low_str[DATA_MAX_NAME_LEN];
    char high_str[DATA_MAX_NAME_LEN];

    memset(high_str, 0, sizeof(high_str));
    memset(low_str, 0, sizeof(low_str));

    strncpy(high_str, str, len - 16);
    strncpy(low_str, str + len - 16, 16);

    lcore_mask.low = strtoull_safe(low_str, &err);
    if (err)
      goto parse_out;

    lcore_mask.high = strtoull_safe(high_str, &err);
    if (err) {
      lcore_mask.low = 0;
      goto parse_out;
    }
  }

parse_out:
  return lcore_mask;
}
