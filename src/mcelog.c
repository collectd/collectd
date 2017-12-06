/*-
 * collectd - src/mcelog.c
 * MIT License
 *
 * Copyright(c) 2016-2017 Intel Corporation. All rights reserved.
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

#include "collectd.h"

#include "common.h"
#include "utils_llist.h"

#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#define MCELOG_PLUGIN "mcelog"
#define MCELOG_BUFF_SIZE 1024
#define MCELOG_POLL_TIMEOUT 1000 /* ms */
#define MCELOG_SOCKET_STR "SOCKET"
#define MCELOG_DIMM_NAME "DMI_NAME"
#define MCELOG_CORRECTED_ERR "corrected memory errors"
#define MCELOG_UNCORRECTED_ERR "uncorrected memory errors"
#define MCELOG_CORRECTED_ERR_TIMED "corrected memory timed errors"
#define MCELOG_UNCORRECTED_ERR_TIMED "uncorrected memory timed errors"
#define MCELOG_CORRECTED_ERR_TYPE_INS "corrected_memory_errors"
#define MCELOG_UNCORRECTED_ERR_TYPE_INS "uncorrected_memory_errors"

typedef struct mcelog_config_s {
  char logfile[PATH_MAX];     /* mcelog logfile */
  pthread_t tid;              /* poll thread id */
  llist_t *dimms_list;        /* DIMMs list */
  pthread_mutex_t dimms_lock; /* lock for dimms cache */
  _Bool persist;
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
  int corrected_err_total; /* x total*/
  int corrected_err_timed; /* x in 24h*/
  char corrected_err_timed_period[DATA_MAX_NAME_LEN / 2];
  int uncorrected_err_total; /* x total*/
  int uncorrected_err_timed; /* x in 24h*/
  char uncorrected_err_timed_period[DATA_MAX_NAME_LEN / 2];
  char location[DATA_MAX_NAME_LEN / 2];  /* SOCKET x CHANNEL x DIMM x*/
  char dimm_name[DATA_MAX_NAME_LEN / 2]; /* DMI_NAME "DIMM_F1" */
} mcelog_memory_rec_t;

static int socket_close(socket_adapter_t *self);
static int socket_write(socket_adapter_t *self, const char *msg,
                        const size_t len);
static int socket_reinit(socket_adapter_t *self);
static int socket_receive(socket_adapter_t *self, FILE **p_file);

static mcelog_config_t g_mcelog_config = {
    .logfile = "/var/log/mcelog", .persist = 0,
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

static _Bool mcelog_thread_running;
static _Bool mcelog_apply_defaults;

static void mcelog_free_dimms_list_records(llist_t *dimms_list) {

  for (llentry_t *e = llist_head(dimms_list); e != NULL; e = e->next) {
    sfree(e->key);
    sfree(e->value);
  }
}

/* Create or get dimm by dimm name/location */
static llentry_t *mcelog_dimm(const mcelog_memory_rec_t *rec,
                              llist_t *dimms_list) {

  char dimm_name[DATA_MAX_NAME_LEN];

  if (strlen(rec->dimm_name) > 0) {
    snprintf(dimm_name, sizeof(dimm_name), "%s_%s", rec->location,
             rec->dimm_name);
  } else
    sstrncpy(dimm_name, rec->location, sizeof(dimm_name));

  llentry_t *dimm_le = llist_search(g_mcelog_config.dimms_list, dimm_name);

  if (dimm_le != NULL)
    return dimm_le;

  /* allocate new linked list entry */
  mcelog_memory_rec_t *dimm_mr = calloc(1, sizeof(*dimm_mr));
  if (dimm_mr == NULL) {
    ERROR(MCELOG_PLUGIN ": Error allocating dimm memory item");
    return NULL;
  }
  char *p_name = strdup(dimm_name);
  if (p_name == NULL) {
    ERROR(MCELOG_PLUGIN ": strdup: error");
    free(dimm_mr);
    return NULL;
  }

  /* add new dimm */
  dimm_le = llentry_create(p_name, dimm_mr);
  if (dimm_le == NULL) {
    ERROR(MCELOG_PLUGIN ": llentry_create(): error");
    free(dimm_mr);
    free(p_name);
    return NULL;
  }
  pthread_mutex_lock(&g_mcelog_config.dimms_lock);
  llist_append(g_mcelog_config.dimms_list, dimm_le);
  pthread_mutex_unlock(&g_mcelog_config.dimms_lock);

  return dimm_le;
}

static void mcelog_update_dimm_stats(llentry_t *dimm,
                                     const mcelog_memory_rec_t *rec) {
  pthread_mutex_lock(&g_mcelog_config.dimms_lock);
  memcpy(dimm->value, rec, sizeof(mcelog_memory_rec_t));
  pthread_mutex_unlock(&g_mcelog_config.dimms_lock);
}

static int mcelog_config(oconfig_item_t *ci) {
  int use_logfile = 0, use_memory = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("McelogLogfile", child->key) == 0) {
      use_logfile = 1;
      if (use_memory) {
        ERROR(MCELOG_PLUGIN ": Invalid configuration option: \"%s\", Memory "
                            "option is already configured.",
              child->key);
        return -1;
      }
      if (cf_util_get_string_buffer(child, g_mcelog_config.logfile,
                                    sizeof(g_mcelog_config.logfile)) < 0) {
        ERROR(MCELOG_PLUGIN ": Invalid configuration option: \"%s\".",
              child->key);
        return -1;
      }
      memset(socket_adapter.unix_sock.sun_path, 0,
             sizeof(socket_adapter.unix_sock.sun_path));
    } else if (strcasecmp("Memory", child->key) == 0) {
      if (use_logfile) {
        ERROR(MCELOG_PLUGIN ": Invalid configuration option: \"%s\", Logfile "
                            "option is already configured.",
              child->key);
        return -1;
      }
      use_memory = 1;
      for (int j = 0; j < child->children_num; j++) {
        oconfig_item_t *mem_child = child->children + j;
        if (strcasecmp("McelogClientSocket", mem_child->key) == 0) {
          if (cf_util_get_string_buffer(
                  mem_child, socket_adapter.unix_sock.sun_path,
                  sizeof(socket_adapter.unix_sock.sun_path)) < 0) {
            ERROR(MCELOG_PLUGIN ": Invalid configuration option: \"%s\".",
                  mem_child->key);
            return -1;
          }
        } else if (strcasecmp("PersistentNotification", mem_child->key) == 0) {
          if (cf_util_get_boolean(mem_child, &g_mcelog_config.persist) < 0) {
            ERROR(MCELOG_PLUGIN ": Invalid configuration option: \"%s\".",
                  mem_child->key);
            return -1;
          }
        } else {
          ERROR(MCELOG_PLUGIN ": Invalid Memory configuration option: \"%s\".",
                mem_child->key);
          return -1;
        }
      }
      memset(g_mcelog_config.logfile, 0, sizeof(g_mcelog_config.logfile));
    } else {
      ERROR(MCELOG_PLUGIN ": Invalid configuration option: \"%s\".",
            child->key);
      return -1;
    }
  }

  if (!use_logfile && !use_memory)
    mcelog_apply_defaults = 1;

  return 0;
}

static int socket_close(socket_adapter_t *self) {
  int ret = 0;
  pthread_rwlock_rdlock(&self->lock);
  if (fcntl(self->sock_fd, F_GETFL) != -1) {
    if (shutdown(self->sock_fd, SHUT_RDWR) != 0) {
      ERROR(MCELOG_PLUGIN ": Socket shutdown failed: %s", STRERRNO);
      ret = -1;
    }
    if (close(self->sock_fd) != 0) {
      ERROR(MCELOG_PLUGIN ": Socket close failed: %s", STRERRNO);
      ret = -1;
    }
  }
  pthread_rwlock_unlock(&self->lock);
  return ret;
}

static int socket_write(socket_adapter_t *self, const char *msg,
                        const size_t len) {
  int ret = 0;
  pthread_rwlock_rdlock(&self->lock);
  if (swrite(self->sock_fd, msg, len) != 0)
    ret = -1;
  pthread_rwlock_unlock(&self->lock);
  return ret;
}

static void mcelog_dispatch_notification(notification_t *n) {
  if (!n) {
    ERROR(MCELOG_PLUGIN ": %s: NULL pointer", __FUNCTION__);
    return;
  }

  sstrncpy(n->host, hostname_g, sizeof(n->host));
  sstrncpy(n->type, "gauge", sizeof(n->type));
  plugin_dispatch_notification(n);
  if (n->meta)
    plugin_notification_meta_free(n->meta);
}

static int socket_reinit(socket_adapter_t *self) {
  int ret = -1;
  cdtime_t interval = plugin_get_interval();
  struct timeval socket_timeout = CDTIME_T_TO_TIMEVAL(interval);

  /* synchronization via write lock since sock_fd may be changed here */
  pthread_rwlock_wrlock(&self->lock);
  self->sock_fd =
      socket(PF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
  if (self->sock_fd < 0) {
    ERROR(MCELOG_PLUGIN ": Could not create a socket. %s", STRERRNO);
    pthread_rwlock_unlock(&self->lock);
    return ret;
  }

  /* Set socket timeout option */
  if (setsockopt(self->sock_fd, SOL_SOCKET, SO_SNDTIMEO, &socket_timeout,
                 sizeof(socket_timeout)) < 0)
    ERROR(MCELOG_PLUGIN ": Failed to set the socket timeout option.");

  /* downgrading to read lock due to possible recursive read locks
   * in self->close(self) call */
  pthread_rwlock_unlock(&self->lock);
  pthread_rwlock_rdlock(&self->lock);
  if (connect(self->sock_fd, (struct sockaddr *)&(self->unix_sock),
              sizeof(self->unix_sock)) < 0) {
    ERROR(MCELOG_PLUGIN ": Failed to connect to mcelog server. %s", STRERRNO);
    self->close(self);
    ret = -1;
  } else {
    ret = 0;
    mcelog_dispatch_notification(
        &(notification_t){.severity = NOTIF_OKAY,
                          .time = cdtime(),
                          .message = "Connected to mcelog server",
                          .plugin = MCELOG_PLUGIN,
                          .type_instance = "mcelog_status"});
  }
  pthread_rwlock_unlock(&self->lock);
  return ret;
}

static int mcelog_dispatch_mem_notifications(const mcelog_memory_rec_t *mr) {
  notification_t n = {.severity = NOTIF_WARNING,
                      .time = cdtime(),
                      .plugin = MCELOG_PLUGIN,
                      .type = "errors"};

  int dispatch_corrected_notifs = 0, dispatch_uncorrected_notifs = 0;

  if (mr == NULL)
    return -1;

  llentry_t *dimm = mcelog_dimm(mr, g_mcelog_config.dimms_list);
  if (dimm == NULL) {
    ERROR(MCELOG_PLUGIN
          ": Error adding/getting dimm memory item to/from cache");
    return -1;
  }
  mcelog_memory_rec_t *mr_old = dimm->value;
  if (!g_mcelog_config.persist) {

    if (mr_old->corrected_err_total != mr->corrected_err_total ||
        mr_old->corrected_err_timed != mr->corrected_err_timed)
      dispatch_corrected_notifs = 1;

    if (mr_old->uncorrected_err_total != mr->uncorrected_err_total ||
        mr_old->uncorrected_err_timed != mr->uncorrected_err_timed)
      dispatch_uncorrected_notifs = 1;

    if (!dispatch_corrected_notifs && !dispatch_uncorrected_notifs) {
      DEBUG("%s: No new notifications to dispatch", MCELOG_PLUGIN);
      return 0;
    }
  } else {
    dispatch_corrected_notifs = 1;
    dispatch_uncorrected_notifs = 1;
  }

  sstrncpy(n.host, hostname_g, sizeof(n.host));

  if (mr->dimm_name[0] != '\0')
    snprintf(n.plugin_instance, sizeof(n.plugin_instance), "%s_%s",
             mr->location, mr->dimm_name);
  else
    sstrncpy(n.plugin_instance, mr->location, sizeof(n.plugin_instance));

  if (dispatch_corrected_notifs &&
      (mr->corrected_err_total > 0 || mr->corrected_err_timed > 0)) {
    /* Corrected Error Notifications */
    plugin_notification_meta_add_signed_int(&n, MCELOG_CORRECTED_ERR,
                                            mr->corrected_err_total);
    plugin_notification_meta_add_signed_int(&n, MCELOG_CORRECTED_ERR_TIMED,
                                            mr->corrected_err_timed);
    snprintf(n.message, sizeof(n.message), MCELOG_CORRECTED_ERR);
    sstrncpy(n.type_instance, MCELOG_CORRECTED_ERR_TYPE_INS,
             sizeof(n.type_instance));
    plugin_dispatch_notification(&n);
    if (n.meta)
      plugin_notification_meta_free(n.meta);
    n.meta = NULL;
  }

  if (dispatch_uncorrected_notifs &&
      (mr->uncorrected_err_total > 0 || mr->uncorrected_err_timed > 0)) {
    /* Uncorrected Error Notifications */
    plugin_notification_meta_add_signed_int(&n, MCELOG_UNCORRECTED_ERR,
                                            mr->uncorrected_err_total);
    plugin_notification_meta_add_signed_int(&n, MCELOG_UNCORRECTED_ERR_TIMED,
                                            mr->uncorrected_err_timed);
    snprintf(n.message, sizeof(n.message), MCELOG_UNCORRECTED_ERR);
    sstrncpy(n.type_instance, MCELOG_UNCORRECTED_ERR_TYPE_INS,
             sizeof(n.type_instance));
    n.severity = NOTIF_FAILURE;
    plugin_dispatch_notification(&n);
    if (n.meta)
      plugin_notification_meta_free(n.meta);
    n.meta = NULL;
  }

  return 0;
}

static int mcelog_submit(const mcelog_memory_rec_t *mr) {

  if (!mr) {
    ERROR(MCELOG_PLUGIN ": %s: NULL pointer", __FUNCTION__);
    return -1;
  }

  llentry_t *dimm = mcelog_dimm(mr, g_mcelog_config.dimms_list);
  if (dimm == NULL) {
    ERROR(MCELOG_PLUGIN
          ": Error adding/getting dimm memory item to/from cache");
    return -1;
  }

  value_list_t vl = {
      .values_len = 1,
      .values = &(value_t){.derive = (derive_t)mr->corrected_err_total},
      .time = cdtime(),
      .plugin = MCELOG_PLUGIN,
      .type = "errors",
      .type_instance = MCELOG_CORRECTED_ERR_TYPE_INS};

  mcelog_update_dimm_stats(dimm, mr);

  if (mr->dimm_name[0] != '\0')
    snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s_%s",
             mr->location, mr->dimm_name);
  else
    sstrncpy(vl.plugin_instance, mr->location, sizeof(vl.plugin_instance));

  plugin_dispatch_values(&vl);

  snprintf(vl.type_instance, sizeof(vl.type_instance),
           "corrected_memory_errors_in_%s", mr->corrected_err_timed_period);
  vl.values = &(value_t){.derive = (derive_t)mr->corrected_err_timed};
  plugin_dispatch_values(&vl);

  sstrncpy(vl.type_instance, MCELOG_UNCORRECTED_ERR_TYPE_INS,
           sizeof(vl.type_instance));
  vl.values = &(value_t){.derive = (derive_t)mr->uncorrected_err_total};
  plugin_dispatch_values(&vl);

  snprintf(vl.type_instance, sizeof(vl.type_instance),
           "uncorrected_memory_errors_in_%s", mr->uncorrected_err_timed_period);
  vl.values = &(value_t){.derive = (derive_t)mr->uncorrected_err_timed};
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
      DEBUG(MCELOG_PLUGIN ": Got SOCKET INFO %s", memory_record->location);
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
          DEBUG(MCELOG_PLUGIN ": Got DIMM NAME %s", memory_record->dimm_name);
        }
      }
    }
    if (!strncmp(buf, MCELOG_CORRECTED_ERR, strlen(MCELOG_CORRECTED_ERR))) {
      /* Get next line*/
      if (fgets(buf, sizeof(buf), p_file) != NULL) {
        sscanf(buf, "\t%d total", &(memory_record->corrected_err_total));
        DEBUG(MCELOG_PLUGIN ": Got corrected error total %d",
              memory_record->corrected_err_total);
      }
      if (fgets(buf, sizeof(buf), p_file) != NULL) {
        sscanf(buf, "\t%d in %s", &(memory_record->corrected_err_timed),
               memory_record->corrected_err_timed_period);
        DEBUG(MCELOG_PLUGIN ": Got timed corrected errors %d in %s",
              memory_record->corrected_err_total,
              memory_record->corrected_err_timed_period);
      }
    }
    if (!strncmp(buf, MCELOG_UNCORRECTED_ERR, strlen(MCELOG_UNCORRECTED_ERR))) {
      if (fgets(buf, sizeof(buf), p_file) != NULL) {
        sscanf(buf, "\t%d total", &(memory_record->uncorrected_err_total));
        DEBUG(MCELOG_PLUGIN ": Got uncorrected error total %d",
              memory_record->uncorrected_err_total);
      }
      if (fgets(buf, sizeof(buf), p_file) != NULL) {
        sscanf(buf, "\t%d in %s", &(memory_record->uncorrected_err_timed),
               memory_record->uncorrected_err_timed_period);
        DEBUG(MCELOG_PLUGIN ": Got timed uncorrected errors %d in %s",
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
      ERROR("mcelog: poll failed: %s", STRERRNO);
    }
    pthread_rwlock_unlock(&self->lock);
    return res;
  }

  if (poll_fd.revents & (POLLERR | POLLHUP | POLLNVAL)) {
    /* connection is broken */
    ERROR(MCELOG_PLUGIN ": Connection to socket is broken");
    if (poll_fd.revents & (POLLERR | POLLHUP)) {
      mcelog_dispatch_notification(
          &(notification_t){.severity = NOTIF_FAILURE,
                            .time = cdtime(),
                            .message = "Connection to mcelog socket is broken.",
                            .plugin = MCELOG_PLUGIN,
                            .type_instance = "mcelog_status"});
    }
    pthread_rwlock_unlock(&self->lock);
    return -1;
  }

  if (!(poll_fd.revents & (POLLIN | POLLPRI))) {
    INFO(MCELOG_PLUGIN ": No data to read");
    pthread_rwlock_unlock(&self->lock);
    return 0;
  }

  if ((*pp_file = fdopen(dup(self->sock_fd), "r")) == NULL)
    res = -1;

  pthread_rwlock_unlock(&self->lock);
  return res;
}

static void *poll_worker(__attribute__((unused)) void *arg) {
  mcelog_thread_running = 1;
  FILE **pp_file = calloc(1, sizeof(*pp_file));
  if (pp_file == NULL) {
    ERROR("mcelog: memory allocation failed: %s", STRERRNO);
    pthread_exit((void *)1);
  }

  pthread_cleanup_push(poll_worker_cleanup, pp_file);

  while (1) {
    /* blocking call */
    int res = socket_adapter.receive(&socket_adapter, pp_file);
    if (res < 0) {
      socket_adapter.close(&socket_adapter);
      while (socket_adapter.reinit(&socket_adapter) != 0) {
        nanosleep(&CDTIME_T_TO_TIMESPEC(MS_TO_CDTIME_T(MCELOG_POLL_TIMEOUT)),
                  NULL);
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
      /* Check if location was successfully parsed */
      if (memory_record.location[0] == '\0') {
        memset(&memory_record, 0, sizeof(memory_record));
        continue;
      }

      if (mcelog_dispatch_mem_notifications(&memory_record) != 0)
        ERROR(MCELOG_PLUGIN ": Failed to submit memory errors notification");
      if (mcelog_submit(&memory_record) != 0)
        ERROR(MCELOG_PLUGIN ": Failed to submit memory errors");
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
  if (mcelog_apply_defaults) {
    INFO(MCELOG_PLUGIN
         ": No configuration selected defaulting to memory errors.");
    memset(g_mcelog_config.logfile, 0, sizeof(g_mcelog_config.logfile));
  }
  g_mcelog_config.dimms_list = llist_create();
  int err = pthread_mutex_init(&g_mcelog_config.dimms_lock, NULL);
  if (err < 0) {
    ERROR(MCELOG_PLUGIN ": plugin: failed to initialize cache lock");
    return -1;
  }

  if (socket_adapter.reinit(&socket_adapter) != 0) {
    ERROR(MCELOG_PLUGIN ": Cannot connect to client socket");
    return -1;
  }

  if (strlen(socket_adapter.unix_sock.sun_path)) {
    if (plugin_thread_create(&g_mcelog_config.tid, NULL, poll_worker, NULL,
                             NULL) != 0) {
      ERROR(MCELOG_PLUGIN ": Error creating poll thread.");
      return -1;
    }
  }
  return 0;
}

static int get_memory_machine_checks(void) {
  static const char dump[] = "dump all bios\n";
  int ret = socket_adapter.write(&socket_adapter, dump, sizeof(dump));
  if (ret != 0)
    ERROR(MCELOG_PLUGIN ": SENT DUMP REQUEST FAILED");
  else
    DEBUG(MCELOG_PLUGIN ": SENT DUMP REQUEST OK");
  return ret;
}

static int mcelog_read(__attribute__((unused)) user_data_t *ud) {
  DEBUG(MCELOG_PLUGIN ": %s", __FUNCTION__);

  if (get_memory_machine_checks() != 0)
    ERROR(MCELOG_PLUGIN ": MACHINE CHECK INFO NOT AVAILABLE");

  return 0;
}

static int mcelog_shutdown(void) {
  int ret = 0;
  if (mcelog_thread_running) {
    pthread_cancel(g_mcelog_config.tid);
    if (pthread_join(g_mcelog_config.tid, NULL) != 0) {
      ERROR(MCELOG_PLUGIN ": Stopping thread failed.");
      ret = -1;
    }
  }
  pthread_mutex_lock(&g_mcelog_config.dimms_lock);
  mcelog_free_dimms_list_records(g_mcelog_config.dimms_list);
  llist_destroy(g_mcelog_config.dimms_list);
  g_mcelog_config.dimms_list = NULL;
  pthread_mutex_unlock(&g_mcelog_config.dimms_lock);
  pthread_mutex_destroy(&g_mcelog_config.dimms_lock);
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
