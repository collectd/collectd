/*-
 * collectd - src/mcelog.c
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

 * Authors:
 *   Maryam Tahhan <maryam.tahhan@intel.com>
 *   Volodymyr Mytnyk <volodymyrx.mytnyk@intel.com>
 *   Taras Chornyi <tarasx.chornyi@intel.com>
 *   Krzysztof Matczak <krzysztofx.matczak@intel.com>
 */

#include "common.h"
#include "collectd.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MCELOG_PLUGIN "mcelog"
#define MCELOG_BUFF_SIZE 1024
#define MCELOG_POLL_TIMEOUT 1000 /* ms */
#define MCELOG_SOCKET_STR "SOCKET"
#define MCELOG_DIMM_NAME "DMI_NAME"
#define MCELOG_CORRECTED_ERR "corrected memory errors:"
#define MCELOG_UNCORRECTED_ERR "uncorrected memory errors:"

typedef struct mcelog_config_s {
  char logfile[PATH_MAX]; /* mcelog logfile */
  pthread_t tid;          /* poll thread id */
} mcelog_config_t;

typedef struct socket_adapter_s socket_adapter_t;

struct socket_adapter_s {
  int sock_fd;                  /* mcelog server socket fd */
  struct sockaddr_un unix_sock; /* mcelog client socket */
  pthread_rwlock_t lock;
  /* function pointers for socket operations */
  int (*write)(socket_adapter_t *self, const char *msg, const size_t len);
  int (*reinit)(socket_adapter_t *self);
  int (*receive)(socket_adapter_t *self, FILE **p_file);
  int (*close)(socket_adapter_t *self);
};

typedef struct mcelog_memory_rec_s {
  int corrected_err_total;           /* x total*/
  int corrected_err_timed;           /* x in 24h*/
  char corrected_err_timed_period[DATA_MAX_NAME_LEN];
  int uncorrected_err_total; /* x total*/
  int uncorrected_err_timed; /* x in 24h*/
  char uncorrected_err_timed_period[DATA_MAX_NAME_LEN];
  char location[DATA_MAX_NAME_LEN];  /* SOCKET x CHANNEL x DIMM x*/
  char dimm_name[DATA_MAX_NAME_LEN]; /* DMI_NAME "DIMM_F1" */
} mcelog_memory_rec_t;

static int socket_close(socket_adapter_t *self);
static int socket_write(socket_adapter_t *self, const char *msg,
                        const size_t len);
static int socket_reinit(socket_adapter_t *self);
static int socket_receive(socket_adapter_t *self, FILE **p_file);

static mcelog_config_t g_mcelog_config = {
    .logfile = "/var/log/mcelog", .tid = 0,
};

static socket_adapter_t socket_adapter = {
    .sock_fd = -1,
    .unix_sock =
        {
            .sun_family = AF_UNIX, .sun_path = "/var/run/mcelog-client",
        },
    .lock = PTHREAD_RWLOCK_INITIALIZER,
    .close = socket_close,
    .write = socket_write,
    .reinit = socket_reinit,
    .receive = socket_receive,
};

static _Bool mcelog_thread_running = 0;

static int mcelog_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("McelogClientSocket", child->key) == 0) {
      if (cf_util_get_string_buffer(child, socket_adapter.unix_sock.sun_path,
                                    sizeof(socket_adapter.unix_sock.sun_path)) <
          0) {
        ERROR("%s: Invalid configuration option: \"%s\".", MCELOG_PLUGIN,
              child->key);
        return -1;
      }
    } else if (strcasecmp("McelogLogfile", child->key) == 0) {
      if (cf_util_get_string_buffer(child, g_mcelog_config.logfile,
                                    sizeof(g_mcelog_config.logfile)) < 0) {
        ERROR("%s: Invalid configuration option: \"%s\".", MCELOG_PLUGIN,
              child->key);
        return -1;
      }
    } else {
      ERROR("%s: Invalid configuration option: \"%s\".", MCELOG_PLUGIN,
            child->key);
      return -1;
    }
  }
  return (0);
}

static int socket_close(socket_adapter_t *self) {
  int ret = 0;
  pthread_rwlock_rdlock(&self->lock);
  if (fcntl(self->sock_fd, F_GETFL) != -1) {
    if (shutdown(self->sock_fd, SHUT_RDWR) != 0) {
      char errbuf[MCELOG_BUFF_SIZE];
      ERROR("%s: Socket shutdown failed: %s", MCELOG_PLUGIN,
            sstrerror(errno, errbuf, sizeof(errbuf)));
      ret = -1;
    }
    close(self->sock_fd);
  }
  pthread_rwlock_unlock(&self->lock);
  return ret;
}

static int socket_write(socket_adapter_t *self, const char *msg,
                        const size_t len) {
  int ret = 0;
  pthread_rwlock_rdlock(&self->lock);
  if (swrite(self->sock_fd, msg, len) < 0)
    ret = -1;
  pthread_rwlock_unlock(&self->lock);
  return ret;
}

static int socket_reinit(socket_adapter_t *self) {
  char errbuff[MCELOG_BUFF_SIZE];
  int ret = -1;
  cdtime_t interval = plugin_get_interval();
  struct timeval socket_timeout = CDTIME_T_TO_TIMEVAL(interval);

  /* synchronization via write lock since sock_fd may be changed here */
  pthread_rwlock_wrlock(&self->lock);
  self->sock_fd = socket(PF_UNIX, SOCK_STREAM|SOCK_CLOEXEC|SOCK_NONBLOCK, 0);
  if (self->sock_fd < 0) {
    ERROR("%s: Could not create a socket. %s", MCELOG_PLUGIN,
          sstrerror(errno, errbuff, sizeof(errbuff)));
    pthread_rwlock_unlock(&self->lock);
    return ret;
  }

  /* Set socket timeout option */
  if (setsockopt(self->sock_fd, SOL_SOCKET, SO_SNDTIMEO,
                 &socket_timeout, sizeof(socket_timeout)) < 0)
    ERROR("%s: Failed to set the socket timeout option.", MCELOG_PLUGIN);

  /* downgrading to read lock due to possible recursive read locks
   * in self->close(self) call */
  pthread_rwlock_unlock(&self->lock);
  pthread_rwlock_rdlock(&self->lock);
  if (connect(self->sock_fd, (struct sockaddr *)&(self->unix_sock),
              sizeof(self->unix_sock)) < 0) {
    ERROR("%s: Failed to connect to mcelog server. %s", MCELOG_PLUGIN,
          sstrerror(errno, errbuff, sizeof(errbuff)));
    self->close(self);
    ret = -1;
  } else
    ret = 0;

  pthread_rwlock_unlock(&self->lock);
  return ret;
}

static void mcelog_dispatch_notification(notification_t n) {
  sstrncpy(n.host, hostname_g, sizeof(n.host));
  sstrncpy(n.type, "gauge", sizeof(n.type));
  plugin_dispatch_notification(&n);
}

static int mcelog_prepare_notification(notification_t *n,
                                       mcelog_memory_rec_t mr) {
  if (n == NULL)
    return (-1);

  if (plugin_notification_meta_add_string(n, MCELOG_SOCKET_STR, mr.location) <
      0) {
    ERROR("%s: add memory location meta data failed", MCELOG_PLUGIN);
    return (-1);
  }
  if (strlen(mr.dimm_name) > 0)
    if (plugin_notification_meta_add_string(n, MCELOG_DIMM_NAME, mr.dimm_name) <
        0) {
      ERROR("%s: add DIMM name meta data failed", MCELOG_PLUGIN);
      return (-1);
    }
  if (plugin_notification_meta_add_signed_int(n, MCELOG_CORRECTED_ERR,
                                              mr.corrected_err_total) < 0) {
    ERROR("%s: add corrected errors meta data failed", MCELOG_PLUGIN);
    return (-1);
  }
  if (plugin_notification_meta_add_signed_int(
          n, "corrected memory timed errors", mr.corrected_err_timed) < 0) {
    ERROR("%s: add corrected timed errors meta data failed", MCELOG_PLUGIN);
    return (-1);
  }
  if (plugin_notification_meta_add_string(n, "corrected errors time period",
                                          mr.corrected_err_timed_period) < 0) {
    ERROR("%s: add corrected errors period meta data failed", MCELOG_PLUGIN);
    return (-1);
  }
  if (plugin_notification_meta_add_signed_int(n, MCELOG_UNCORRECTED_ERR,
                                              mr.uncorrected_err_total) < 0) {
    ERROR("%s: add corrected errors meta data failed", MCELOG_PLUGIN);
    return (-1);
  }
  if (plugin_notification_meta_add_signed_int(
          n, "uncorrected memory timed errors", mr.uncorrected_err_timed) < 0) {
    ERROR("%s: add corrected timed errors meta data failed", MCELOG_PLUGIN);
    return (-1);
  }
  if (plugin_notification_meta_add_string(n, "uncorrected errors time period",
                                          mr.uncorrected_err_timed_period) <
      0) {
    ERROR("%s: add corrected errors period meta data failed", MCELOG_PLUGIN);
    return (-1);
  }

  return (0);
}

static int mcelog_submit(mcelog_memory_rec_t mr) {

  value_list_t vl = VALUE_LIST_INIT;
  vl.values_len = 1;
  vl.time = cdtime();

  sstrncpy(vl.plugin, MCELOG_PLUGIN, sizeof(vl.plugin));
  sstrncpy(vl.type, "errors", sizeof(vl.type));
  if (strlen(mr.dimm_name) > 0) {
    ssnprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s_%s",
              mr.location, mr.dimm_name);
  } else
    sstrncpy(vl.plugin_instance, mr.location, sizeof(vl.plugin_instance));

  sstrncpy(vl.type_instance, "corrected_memory_errors",
           sizeof(vl.type_instance));
  vl.values = &(value_t){.derive = (derive_t)mr.corrected_err_total};
  plugin_dispatch_values(&vl);

  ssnprintf(vl.type_instance, sizeof(vl.type_instance),
            "corrected_memory_errors_in_%s", mr.corrected_err_timed_period);
  vl.values = &(value_t){.derive = (derive_t)mr.corrected_err_timed};
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type_instance, "uncorrected_memory_errors",
           sizeof(vl.type_instance));
  vl.values = &(value_t){.derive = (derive_t)mr.uncorrected_err_total};
  plugin_dispatch_values(&vl);

  ssnprintf(vl.type_instance, sizeof(vl.type_instance),
            "uncorrected_memory_errors_in_%s", mr.uncorrected_err_timed_period);
  vl.values = &(value_t){.derive = (derive_t)mr.uncorrected_err_timed};
  plugin_dispatch_values(&vl);

  return 0;
}

static int parse_memory_info(FILE *p_file, mcelog_memory_rec_t *memory_record) {
  char buf[DATA_MAX_NAME_LEN] = {0};
  while (fgets(buf, sizeof(buf), p_file)) {
    /* Got empty line or "done" */
    if ((!strncmp("\n", buf, strlen(buf))) ||
        (!strncmp(buf, "done\n", strlen(buf))))
      return 1;
    if (strlen(buf) < 5)
      continue;
    if (!strncmp(buf, MCELOG_SOCKET_STR, strlen(MCELOG_SOCKET_STR))) {
      sstrncpy(memory_record->location, buf, strlen(buf));
      /* replace spaces with '_' */
      for (size_t i = 0; i < strlen(memory_record->location); i++)
        if (memory_record->location[i] == ' ')
          memory_record->location[i] = '_';
      DEBUG("%s: Got SOCKET INFO %s", MCELOG_PLUGIN, memory_record->location);
    }
    if (!strncmp(buf, MCELOG_DIMM_NAME, strlen(MCELOG_DIMM_NAME))) {
      char *name = NULL;
      char *saveptr = NULL;
      name = strtok_r(buf, "\"", &saveptr);
      if (name != NULL && saveptr != NULL) {
        name = strtok_r(NULL, "\"", &saveptr);
        if (name != NULL) {
          sstrncpy(memory_record->dimm_name, name,
                   sizeof(memory_record->dimm_name));
          DEBUG("%s: Got DIMM NAME %s", MCELOG_PLUGIN,
                memory_record->dimm_name);
        }
      }
    }
    if (!strncmp(buf, MCELOG_CORRECTED_ERR, strlen(MCELOG_CORRECTED_ERR))) {
      /* Get next line*/
      if (fgets(buf, sizeof(buf), p_file) != NULL) {
        sscanf(buf, "\t%d total", &(memory_record->corrected_err_total));
        DEBUG("%s: Got corrected error total %d", MCELOG_PLUGIN,
              memory_record->corrected_err_total);
      }
      if (fgets(buf, sizeof(buf), p_file) != NULL) {
        sscanf(buf, "\t%d in %s", &(memory_record->corrected_err_timed),
               memory_record->corrected_err_timed_period);
        DEBUG("%s: Got timed corrected errors %d in %s", MCELOG_PLUGIN,
              memory_record->corrected_err_total,
              memory_record->corrected_err_timed_period);
      }
    }
    if (!strncmp(buf, MCELOG_UNCORRECTED_ERR, strlen(MCELOG_UNCORRECTED_ERR))) {
      if (fgets(buf, sizeof(buf), p_file) != NULL) {
        sscanf(buf, "\t%d total", &(memory_record->uncorrected_err_total));
        DEBUG("%s: Got uncorrected error total %d", MCELOG_PLUGIN,
              memory_record->uncorrected_err_total);
      }
      if (fgets(buf, sizeof(buf), p_file) != NULL) {
        sscanf(buf, "\t%d in %s", &(memory_record->uncorrected_err_timed),
               memory_record->uncorrected_err_timed_period);
        DEBUG("%s: Got timed uncorrected errors %d in %s", MCELOG_PLUGIN,
              memory_record->uncorrected_err_total,
              memory_record->uncorrected_err_timed_period);
      }
    }
    memset(buf, 0, sizeof(buf));
  }
  /* parsing definitely finished */
  return 0;
}

static void poll_worker_cleanup(void *arg) {
  mcelog_thread_running = 0;
  FILE *p_file = *((FILE **)arg);
  if (p_file != NULL)
    fclose(p_file);
  free(arg);
}

static int socket_receive(socket_adapter_t *self, FILE **pp_file) {
  int res = -1;
  pthread_rwlock_rdlock(&self->lock);
  struct pollfd poll_fd = {
      .fd = self->sock_fd, .events = POLLIN | POLLPRI,
  };

  if ((res = poll(&poll_fd, 1, MCELOG_POLL_TIMEOUT)) <= 0) {
    if (res != 0 && errno != EINTR) {
      char errbuf[MCELOG_BUFF_SIZE];
      ERROR("mcelog: poll failed: %s",
            sstrerror(errno, errbuf, sizeof(errbuf)));
    }
    pthread_rwlock_unlock(&self->lock);
    return res;
  }

  if (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
    /* connection is broken */
    ERROR("%s: Connection to socket is broken", MCELOG_PLUGIN);
    if (poll_fd.revents & (POLLERR | POLLHUP)) {
      notification_t n = {
          NOTIF_FAILURE, cdtime(), "", "", MCELOG_PLUGIN, "", "", "", NULL};
      ssnprintf(n.message, sizeof(n.message),
                "Connection to mcelog socket is broken.");
      sstrncpy(n.type_instance, "mcelog_status", sizeof(n.type_instance));
      mcelog_dispatch_notification(n);
    }
    pthread_rwlock_unlock(&self->lock);
    return -1;
  }

  if (!(poll_fd.revents & (POLLIN | POLLPRI))) {
    INFO("%s: No data to read", MCELOG_PLUGIN);
    pthread_rwlock_unlock(&self->lock);
    return 0;
  }

  if ((*pp_file = fdopen(dup(self->sock_fd), "r")) == NULL)
    res = -1;

  pthread_rwlock_unlock(&self->lock);
  return res;
}

static void *poll_worker(__attribute__((unused)) void *arg) {
  char errbuf[MCELOG_BUFF_SIZE];
  mcelog_thread_running = 1;
  FILE **pp_file = calloc(1, sizeof(*pp_file));
  if (pp_file == NULL) {
    ERROR("mcelog: memory allocation failed: %s",
          sstrerror(errno, errbuf, sizeof(errbuf)));
    pthread_exit((void *)1);
  }

  pthread_cleanup_push(poll_worker_cleanup, pp_file);

  while (1) {
    int res = 0;
    /* blocking call */
    res = socket_adapter.receive(&socket_adapter, pp_file);
    if (res < 0) {
      socket_adapter.close(&socket_adapter);
      if (socket_adapter.reinit(&socket_adapter) != 0) {
        socket_adapter.close(&socket_adapter);
        usleep(MCELOG_POLL_TIMEOUT);
      }
      continue;
    }
    /* timeout or no data to read */
    else if (res == 0)
      continue;

    if (*pp_file == NULL)
      continue;

    mcelog_memory_rec_t memory_record = {0};
    while (parse_memory_info(*pp_file, &memory_record)) {
      notification_t n = {NOTIF_OKAY, cdtime(), "", "",  MCELOG_PLUGIN,
                          "",         "",       "", NULL};
      ssnprintf(n.message, sizeof(n.message), "Got memory errors info.");
      sstrncpy(n.type_instance, "memory_erros", sizeof(n.type_instance));
      if (mcelog_prepare_notification(&n, memory_record) == 0)
        mcelog_dispatch_notification(n);
      if (mcelog_submit(memory_record) != 0)
        ERROR("%s: Failed to submit memory errors", MCELOG_PLUGIN);
      memset(&memory_record, 0, sizeof(memory_record));
    }

    fclose(*pp_file);
    *pp_file = NULL;
  }

  mcelog_thread_running = 0;
  pthread_cleanup_pop(1);
  return NULL;
}

static int mcelog_init(void) {
  if (socket_adapter.reinit(&socket_adapter) != 0) {
    ERROR("%s: Cannot connect to client socket", MCELOG_PLUGIN);
    return -1;
  }

  if (plugin_thread_create(&g_mcelog_config.tid, NULL, poll_worker, NULL,
                           NULL) != 0) {
    ERROR("%s: Error creating poll thread.", MCELOG_PLUGIN);
    return -1;
  }
  return 0;
}

static int get_memory_machine_checks(void) {
  static const char dump[] = "dump all bios\n";
  int ret = socket_adapter.write(&socket_adapter, dump, sizeof(dump));
  if (ret != 0)
    ERROR("%s: SENT DUMP REQUEST FAILED", MCELOG_PLUGIN);
  else
    DEBUG("%s: SENT DUMP REQUEST OK", MCELOG_PLUGIN);
  return ret;
}

static int mcelog_read(__attribute__((unused)) user_data_t *ud) {
  DEBUG("%s: %s", MCELOG_PLUGIN, __FUNCTION__);

  if (get_memory_machine_checks() != 0)
    ERROR("%s: MACHINE CHECK INFO NOT AVAILABLE", MCELOG_PLUGIN);

  return 0;
}

static int mcelog_shutdown(void) {
  int ret = 0;
  if (mcelog_thread_running) {
    pthread_cancel(g_mcelog_config.tid);
    if (pthread_join(g_mcelog_config.tid, NULL) != 0) {
      ERROR("%s: Stopping thread failed.", MCELOG_PLUGIN);
      ret = -1;
    }
  }

  ret = socket_adapter.close(&socket_adapter) || ret;
  pthread_rwlock_destroy(&(socket_adapter.lock));
  return -ret;
}

void module_register(void) {
  plugin_register_complex_config(MCELOG_PLUGIN, mcelog_config);
  plugin_register_init(MCELOG_PLUGIN, mcelog_init);
  plugin_register_complex_read(NULL, MCELOG_PLUGIN, mcelog_read, 0, NULL);
  plugin_register_shutdown(MCELOG_PLUGIN, mcelog_shutdown);
}
