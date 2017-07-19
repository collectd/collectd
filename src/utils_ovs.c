/**
 * collectd - src/utils_ovs.c
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
 *   Volodymyr Mytnyk <volodymyrx.mytnyk@intel.com>
 **/

/* clang-format off */
/*
 *                         OVS DB API internal architecture diagram
 * +------------------------------------------------------------------------------+
 * |OVS plugin      |OVS utils                                                    |
 * |                |     +------------------------+                              |
 * |                |     |      echo handler      |                JSON request/ |
 * |                |  +--+ (ovs_db_table_echo_cb) +<---+---------+ update event/ |
 * |                |  |  |                        |    |         | result        |
 * |                |  |  +------------------------+    |         |               |
 * |                |  |                                |    +----+---+--------+  |
 * |  +----------+  |  |  +------------------------+    |    |        |        |  |
 * |  |  update  |  |  |  |     update handler     |    |    |  YAJL  |  JSON  |  |
 * |  | callback +<-------+(ovs_db_table_update_cp)+<---+    | parser | reader |  |
 * |  +----------+  |  |  |                        |    |    |        |        |  |
 * |                |  |  +------------------------+    |    +--------+---+----+  |
 * |                |  |                                |                 ^       |
 * |  +----------+  |  |  +------------------------+    |                 |       |
 * |  |  result  |  |  |  |     result handler     |    |                 |       |
 * |  | callback +<-------+   (ovs_db_result_cb)   +<---+        JSON raw |       |
 * |  +----------+  |  |  |                        |               data   |       |
 * |                |  |  +------------------------+                      |       |
 * |                |  |                                                  |       |
 * |                |  |    +------------------+             +------------+----+  |
 * |  +----------+  |  |    |thread|           |             |thread|          |  |
 * |  |   init   |  |  |    |                  |  reconnect  |                 |  |
 * |  | callback +<---------+   EVENT WORKER   +<------------+   POLL WORKER   |  |
 * |  +----------+  |  |    +------------------+             +--------+--------+  |
 * |                |  |                                              ^           |
 * +----------------+-------------------------------------------------------------+
 *                     |                                              |
 *                 JSON|echo reply                                 raw|data
 *                     v                                              v
 * +-------------------+----------------------------------------------+-----------+
 * |                                 TCP/UNIX socket                              |
 * +-------------------------------------------------------------------------------
 */
/* clang-format on */

/* collectd headers */
#include "collectd.h"

#include "common.h"

/* private headers */
#include "utils_ovs.h"

/* system libraries */
#if HAVE_NETDB_H
#include <netdb.h>
#endif
#if HAVE_ARPA_INET_H
#include <arpa/inet.h>
#endif
#if HAVE_POLL_H
#include <poll.h>
#endif
#if HAVE_SYS_UN_H
#include <sys/un.h>
#endif

#include <semaphore.h>

#define OVS_ERROR(fmt, ...)                                                    \
  do {                                                                         \
    ERROR("ovs_utils: " fmt, ##__VA_ARGS__);                                   \
  } while (0)
#define OVS_DEBUG(fmt, ...)                                                    \
  do {                                                                         \
    DEBUG("%s:%d:%s(): " fmt, __FILE__, __LINE__, __FUNCTION__,                \
          ##__VA_ARGS__);                                                      \
  } while (0)

#define OVS_DB_POLL_TIMEOUT 1           /* poll receive timeout (sec) */
#define OVS_DB_POLL_READ_BLOCK_SIZE 512 /* read block size (bytes) */
#define OVS_DB_DEFAULT_DB_NAME "Open_vSwitch"

#define OVS_DB_EVENT_TERMINATE 0
#define OVS_DB_EVENT_CONN_ESTABLISHED 1
#define OVS_DB_EVENT_CONN_TERMINATED 2

#define OVS_DB_POLL_STATE_RUNNING 1
#define OVS_DB_POLL_STATE_EXITING 2

#define OVS_DB_SEND_REQ_TIMEOUT 5 /* send request timeout (sec) */

#define OVS_YAJL_CALL(func, ...)                                               \
  do {                                                                         \
    yajl_gen_ret = yajl_gen_status_ok;                                         \
    if ((yajl_gen_ret = func(__VA_ARGS__)) != yajl_gen_status_ok)              \
      goto yajl_gen_failure;                                                   \
  } while (0)
#define OVS_YAJL_ERROR_BUFFER_SIZE 1024
#define OVS_ERROR_BUFF_SIZE 512
#define OVS_UID_STR_SIZE 17 /* 64-bit HEX string len + '\0' */
#define OVS_QUEUE_LEN 128   /* OvS queue length */

/* JSON reader internal data */
struct ovs_json_reader_s {
  char *buff_ptr;
  size_t buff_size;
  size_t buff_offset;
  size_t json_offset;
};
typedef struct ovs_json_reader_s ovs_json_reader_t;

/* Result callback declaration */
struct ovs_result_cb_s {
  sem_t sync;
  ovs_db_result_cb_t call;
};
typedef struct ovs_result_cb_s ovs_result_cb_t;

/* Table callback declaration */
struct ovs_table_cb_s {
  ovs_db_table_cb_t call;
};
typedef struct ovs_table_cb_s ovs_table_cb_t;

/* Callback declaration */
struct ovs_callback_s {
  uint64_t uid;
  union {
    ovs_result_cb_t result;
    ovs_table_cb_t table;
  };
  struct ovs_callback_s *next;
  struct ovs_callback_s *prev;
};
typedef struct ovs_callback_s ovs_callback_t;

/* OVS DB event */
struct ovs_db_event_s {
  int id;        /* Event ID */
  ovs_db_t *pdb; /* OVS DB pointer */
};
typedef struct ovs_db_event_s ovs_db_event_t;

/* OVS event queue */
struct ovs_equeue_s {
  size_t size;
  size_t head;
  size_t tail;
  pthread_mutex_t lock;
  pthread_cond_t wait_event;
  ovs_db_event_t *queue;
};
typedef struct ovs_equeue_s ovs_equeue_t;

/* Event thread data declaration */
struct ovs_event_thread_s {
  pthread_t tid;
  ovs_equeue_t ev_queue;
};
typedef struct ovs_event_thread_s ovs_event_thread_t;

/* Poll thread data declaration */
struct ovs_poll_thread_s {
  pthread_t tid;
  pthread_mutex_t mutex;
  int state;
};
typedef struct ovs_poll_thread_s ovs_poll_thread_t;

/* OVS DB internal data */
struct ovs_db_s {
  int sock;                   /* OVS DB socket */
  ovs_json_reader_t *jreader; /* JSON reader instance */
  ovs_db_inst_t inst;         /* Instance data */
  pthread_mutex_t mutex;      /* OVS DB lock */
  ovs_callback_t *remote_cb;  /* Remote OVS DB callbacks */
  struct ovs_db_s *next;      /* Pointer to the next OVS DB */
};

/* OVS internal data */
struct ovs_s {
  pthread_mutex_t mutex;
  ovs_poll_thread_t poll_thread;
  ovs_event_thread_t event_thread;
  ovs_db_t *db_list;
  size_t db_num;
};

/* Global variables */
static uint64_t ovs_uid = 0;
static pthread_mutex_t ovs_uid_mutex = PTHREAD_MUTEX_INITIALIZER;

#if COLLECT_DEBUG
/* Convert event ID to string */
static const char *ovs_db_event_id_to_str(int event_id) {
  const char *event_id_map[] = {"OVS_DB_EVENT_TERMINATE",
                                "OVS_DB_EVENT_CONN_ESTABLISHED",
                                "OVS_DB_EVENT_CONN_TERMINATED"};
  if (event_id < 0 || event_id >= STATIC_ARRAY_SIZE(event_id_map))
    return "UNKNOWN";

  /* return event id string */
  return event_id_map[event_id];
}
#endif

/* Post an event to the OvS event queue. Returns zero on success and return
 * positive value if the queue is full.
 *
 * Possible events are:
 *  OVS_DB_EVENT_TERMINATE
 *  OVS_DB_EVENT_CONN_ESTABLISHED
 *  OVS_DB_EVENT_CONN_TERMINATED
 */
static int ovs_event_post(ovs_t *ovs, ovs_db_t *pdb, int event_id) {
  OVS_DEBUG("Sending event %s ...", ovs_db_event_id_to_str(event_id));
  ovs_equeue_t *ev_queue = &ovs->event_thread.ev_queue;

  pthread_mutex_lock(&ev_queue->lock);
  if ((ev_queue->head + 1) % ev_queue->size == ev_queue->tail) {
    /* the queue is full, so return a positive value to
     * inform caller about this */
    pthread_mutex_unlock(&ev_queue->lock);
    return 1;
  }
  /* post the event into OvS event queue */
  ev_queue->queue[ev_queue->head] =
      (ovs_db_event_t){.id = event_id, .pdb = pdb};
  ev_queue->head = (ev_queue->head + 1) % ev_queue->size;

  /* unlock the ovs_event_get() call and return success */
  pthread_cond_signal(&ev_queue->wait_event);
  pthread_mutex_unlock(&ev_queue->lock);
  return 0;
}

/* Get an event from the OvS event queue. If the event queue is empty, then
 * this call blocks until a new event becomes available */
static ovs_db_event_t ovs_event_get(ovs_t *ovs) {
  ovs_equeue_t *ev_queue = &ovs->event_thread.ev_queue;
  ovs_db_event_t event = {0};

  /* wait for incoming events */
  pthread_mutex_lock(&ev_queue->lock);
  while (ev_queue->head == ev_queue->tail)
    pthread_cond_wait(&ev_queue->wait_event, &ev_queue->lock);

  /* get event from the queue and zero it */
  event = ev_queue->queue[ev_queue->tail];
  ev_queue->queue[ev_queue->tail].pdb = NULL;
  ev_queue->queue[ev_queue->tail].id = 0;

  /* point to the next event in the queue */
  ev_queue->tail = (ev_queue->tail + 1) % ev_queue->size;
  pthread_mutex_unlock(&ev_queue->lock);
  return event;
}

/* Check if POLL thread is still running. Returns
 * 1 if running otherwise 0 is returned */
static _Bool ovs_db_poll_is_running(ovs_t *ovs) {
  int state = 0;
  pthread_mutex_lock(&ovs->poll_thread.mutex);
  state = ovs->poll_thread.state;
  pthread_mutex_unlock(&ovs->poll_thread.mutex);
  return state == OVS_DB_POLL_STATE_RUNNING;
}

/* Generate unique identifier (UID). It is used by OVS DB API
 * to set "id" field for any OVS DB JSON request. */
static uint64_t ovs_uid_generate() {
  uint64_t new_uid;
  pthread_mutex_lock(&ovs_uid_mutex);
  new_uid = ++ovs_uid;
  pthread_mutex_unlock(&ovs_uid_mutex);
  return new_uid;
}

/*
 * Callback API. These function are used to store
 * registered callbacks in OVS DB API.
 */

/* Add new callback into OVS DB object */
static void ovs_db_callback_add(ovs_db_t *pdb, ovs_callback_t *new_cb) {
  pthread_mutex_lock(&pdb->mutex);
  if (pdb->remote_cb)
    pdb->remote_cb->prev = new_cb;
  new_cb->next = pdb->remote_cb;
  new_cb->prev = NULL;
  pdb->remote_cb = new_cb;
  pthread_mutex_unlock(&pdb->mutex);
}

/* Remove callback from OVS DB object */
static void ovs_db_callback_remove(ovs_db_t *pdb, ovs_callback_t *del_cb) {
  pthread_mutex_lock(&pdb->mutex);
  ovs_callback_t *pre_cb = del_cb->prev;
  ovs_callback_t *next_cb = del_cb->next;

  if (next_cb)
    next_cb->prev = del_cb->prev;

  if (pre_cb)
    pre_cb->next = del_cb->next;
  else
    pdb->remote_cb = del_cb->next;

  free(del_cb);
  pthread_mutex_unlock(&pdb->mutex);
}

/* Remove all callbacks form OVS DB object */
static void ovs_db_callback_remove_all(ovs_db_t *pdb) {
  pthread_mutex_lock(&pdb->mutex);
  for (ovs_callback_t *del_cb = pdb->remote_cb; pdb->remote_cb;
       del_cb = pdb->remote_cb) {
    pdb->remote_cb = del_cb->next;
    free(del_cb);
  }
  pdb->remote_cb = NULL;
  pthread_mutex_unlock(&pdb->mutex);
}

/* Get/find callback in OVS DB object by UID. Returns pointer
 * to requested callback otherwise NULL is returned.
 *
 * IMPORTANT NOTE:
 *   The OVS DB mutex MUST be locked by the caller
 *   to make sure that returned callback is still valid.
 */
static ovs_callback_t *ovs_db_callback_get(ovs_db_t *pdb, uint64_t uid) {
  for (ovs_callback_t *cb = pdb->remote_cb; cb != NULL; cb = cb->next)
    if (cb->uid == uid)
      return cb;
  return NULL;
}

/* Send all requested data to the socket. Returns 0 if
 * ALL request data has been sent otherwise negative value
 * is returned */
static int ovs_db_data_send(const ovs_db_t *pdb, const char *data, size_t len) {
  ssize_t nbytes = 0;
  size_t rem = len;
  size_t off = 0;

  while (rem > 0) {
    if ((nbytes = send(pdb->sock, data + off, rem, 0)) <= 0)
      return -1;
    rem -= (size_t)nbytes;
    off += (size_t)nbytes;
  }
  return 0;
}

/*
 * YAJL (Yet Another JSON Library) helper functions
 * Documentation (https://lloyd.github.io/yajl/)
 */

/* Add null-terminated string into YAJL generator handle (JSON object).
 * Similar function to yajl_gen_string() but takes null-terminated string
 * instead of string and its length.
 *
 * jgen   - YAJL generator handle allocated by yajl_gen_alloc()
 * string - Null-terminated string
 */
static yajl_gen_status ovs_yajl_gen_tstring(yajl_gen hander,
                                            const char *string) {
  return yajl_gen_string(hander, (const unsigned char *)string, strlen(string));
}

/* Add YAJL value into YAJL generator handle (JSON object)
 *
 * jgen - YAJL generator handle allocated by yajl_gen_alloc()
 * jval - YAJL value usually returned by yajl_tree_get()
 */
static yajl_gen_status ovs_yajl_gen_val(yajl_gen jgen, yajl_val jval) {
  size_t array_len = 0;
  yajl_val *jvalues = NULL;
  yajl_val jobj_value = NULL;
  const char *obj_key = NULL;
  size_t obj_len = 0;
  yajl_gen_status yajl_gen_ret = yajl_gen_status_ok;

  if (jval == NULL)
    return yajl_gen_generation_complete;

  if (YAJL_IS_STRING(jval))
    OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, YAJL_GET_STRING(jval));
  else if (YAJL_IS_DOUBLE(jval))
    OVS_YAJL_CALL(yajl_gen_double, jgen, YAJL_GET_DOUBLE(jval));
  else if (YAJL_IS_INTEGER(jval))
    OVS_YAJL_CALL(yajl_gen_double, jgen, YAJL_GET_INTEGER(jval));
  else if (YAJL_IS_TRUE(jval))
    OVS_YAJL_CALL(yajl_gen_bool, jgen, 1);
  else if (YAJL_IS_FALSE(jval))
    OVS_YAJL_CALL(yajl_gen_bool, jgen, 0);
  else if (YAJL_IS_NULL(jval))
    OVS_YAJL_CALL(yajl_gen_null, jgen);
  else if (YAJL_IS_ARRAY(jval)) {
    /* create new array and add all elements into the array */
    array_len = YAJL_GET_ARRAY(jval)->len;
    jvalues = YAJL_GET_ARRAY(jval)->values;
    OVS_YAJL_CALL(yajl_gen_array_open, jgen);
    for (size_t i = 0; i < array_len; i++)
      OVS_YAJL_CALL(ovs_yajl_gen_val, jgen, jvalues[i]);
    OVS_YAJL_CALL(yajl_gen_array_close, jgen);
  } else if (YAJL_IS_OBJECT(jval)) {
    /* create new object and add all elements into the object */
    OVS_YAJL_CALL(yajl_gen_map_open, jgen);
    obj_len = YAJL_GET_OBJECT(jval)->len;
    for (size_t i = 0; i < obj_len; i++) {
      obj_key = YAJL_GET_OBJECT(jval)->keys[i];
      jobj_value = YAJL_GET_OBJECT(jval)->values[i];
      OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, obj_key);
      OVS_YAJL_CALL(ovs_yajl_gen_val, jgen, jobj_value);
    }
    OVS_YAJL_CALL(yajl_gen_map_close, jgen);
  } else {
    OVS_ERROR("%s() unsupported value type %d (skip)", __FUNCTION__,
              (int)(jval)->type);
    goto yajl_gen_failure;
  }
  return yajl_gen_status_ok;

yajl_gen_failure:
  OVS_ERROR("%s() error to generate value", __FUNCTION__);
  return yajl_gen_ret;
}

/* OVS DB echo request handler. When OVS DB sends
 * "echo" request to the client, client should generate
 * "echo" replay with the same content received in the
 * request */
static int ovs_db_table_echo_cb(const ovs_db_t *pdb, yajl_val jnode) {
  yajl_val jparams;
  yajl_val jid;
  yajl_gen jgen;
  size_t resp_len = 0;
  const char *resp = NULL;
  const char *params_path[] = {"params", NULL};
  const char *id_path[] = {"id", NULL};
  yajl_gen_status yajl_gen_ret;

  if ((jgen = yajl_gen_alloc(NULL)) == NULL)
    return -1;

  /* check & get request attributes */
  if ((jparams = yajl_tree_get(jnode, params_path, yajl_t_array)) == NULL ||
      ((jid = yajl_tree_get(jnode, id_path, yajl_t_any)) == NULL)) {
    OVS_ERROR("parse echo request failed");
    goto yajl_gen_failure;
  }

  /* generate JSON echo response */
  OVS_YAJL_CALL(yajl_gen_map_open, jgen);

  OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "result");
  OVS_YAJL_CALL(ovs_yajl_gen_val, jgen, jparams);

  OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "error");
  OVS_YAJL_CALL(yajl_gen_null, jgen);

  OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "id");
  OVS_YAJL_CALL(ovs_yajl_gen_val, jgen, jid);

  OVS_YAJL_CALL(yajl_gen_map_close, jgen);
  OVS_YAJL_CALL(yajl_gen_get_buf, jgen, (const unsigned char **)&resp,
                &resp_len);

  /* send the response */
  OVS_DEBUG("response: %s", resp);
  if (ovs_db_data_send(pdb, resp, resp_len) < 0) {
    OVS_ERROR("send echo reply failed");
    goto yajl_gen_failure;
  }
  /* clean up and return success */
  yajl_gen_clear(jgen);
  return 0;

yajl_gen_failure:
  /* release memory */
  yajl_gen_clear(jgen);
  return -1;
}

/* Get OVS DB registered callback by YAJL val. The YAJL
 * value should be YAJL string (UID). Returns NULL if
 * callback hasn't been found. See also ovs_db_callback_get()
 * description for addition info.
 */
static ovs_callback_t *ovs_db_table_callback_get(ovs_db_t *pdb, yajl_val jid) {
  char *endptr = NULL;
  const char *suid = NULL;
  uint64_t uid;

  if (jid && YAJL_IS_STRING(jid)) {
    suid = YAJL_GET_STRING(jid);
    uid = (uint64_t)strtoul(suid, &endptr, 16);
    if (*endptr == '\0' && uid)
      return ovs_db_callback_get(pdb, uid);
  }

  return NULL;
}

/* OVS DB table update event handler.
 * This callback is called by POLL thread if OVS DB
 * table update callback is received from the DB
 * server. Once registered callback found, it's called
 * by this handler. */
static int ovs_db_table_update_cb(ovs_db_t *pdb, yajl_val jnode) {
  ovs_callback_t *cb = NULL;
  yajl_val jvalue;
  yajl_val jparams;
  yajl_val jtable_updates;
  const char *params_path[] = {"params", NULL};
  const char *id_path[] = {"id", NULL};

  /* check & get request attributes */
  if ((jparams = yajl_tree_get(jnode, params_path, yajl_t_array)) == NULL ||
      (yajl_tree_get(jnode, id_path, yajl_t_null) == NULL)) {
    OVS_ERROR("invalid OVS DB request received");
    return -1;
  }

  /* check array length: [<json-value>, <table-updates>] */
  if ((YAJL_GET_ARRAY(jparams) == NULL) ||
      (YAJL_GET_ARRAY(jparams)->len != 2)) {
    OVS_ERROR("invalid OVS DB request received");
    return -1;
  }

  jvalue = YAJL_GET_ARRAY(jparams)->values[0];
  jtable_updates = YAJL_GET_ARRAY(jparams)->values[1];
  if ((!YAJL_IS_OBJECT(jtable_updates)) || (!YAJL_IS_STRING(jvalue))) {
    OVS_ERROR("invalid OVS DB request id or table update received");
    return -1;
  }

  /* find registered callback based on <json-value> */
  pthread_mutex_lock(&pdb->mutex);
  cb = ovs_db_table_callback_get(pdb, jvalue);
  if (cb == NULL || cb->table.call == NULL) {
    OVS_ERROR("No OVS DB table update callback found");
    pthread_mutex_unlock(&pdb->mutex);
    return -1;
  }

  /* call registered callback */
  cb->table.call(jtable_updates, pdb->inst.user_data.data);
  pthread_mutex_unlock(&pdb->mutex);
  return 0;
}

/* OVS DB result request handler.
 * This callback is called by POLL thread if OVS DB
 * result reply is received from the DB server.
 * Once registered callback found, it's called
 * by this handler. */
static int ovs_db_result_cb(ovs_db_t *pdb, yajl_val jnode) {
  ovs_callback_t *cb = NULL;
  yajl_val jresult;
  yajl_val jerror;
  yajl_val jid;
  const char *result_path[] = {"result", NULL};
  const char *error_path[] = {"error", NULL};
  const char *id_path[] = {"id", NULL};

  jresult = yajl_tree_get(jnode, result_path, yajl_t_any);
  jerror = yajl_tree_get(jnode, error_path, yajl_t_any);
  jid = yajl_tree_get(jnode, id_path, yajl_t_string);

  /* check & get result attributes */
  if (!jresult || !jerror || !jid)
    return -1;

  /* try to find registered callback */
  pthread_mutex_lock(&pdb->mutex);
  cb = ovs_db_table_callback_get(pdb, jid);
  if (cb != NULL && cb->result.call != NULL) {
    /* call registered callback */
    cb->result.call(jresult, jerror, pdb->inst.user_data.data);
    /* unlock owner of the reply */
    sem_post(&cb->result.sync);
  }

  pthread_mutex_unlock(&pdb->mutex);
  return 0;
}

/* Handle JSON data (one request) and call
 * appropriate event OVS DB handler. Currently,
 * update callback 'ovs_db_table_update_cb' and
 * result callback 'ovs_db_result_cb' is supported.
 */
static int ovs_db_json_data_process(ovs_db_t *pdb, const char *data,
                                    size_t len) {
  const char *method = NULL;
  char yajl_errbuf[OVS_YAJL_ERROR_BUFFER_SIZE];
  const char *method_path[] = {"method", NULL};
  const char *result_path[] = {"result", NULL};
  char *sjson = NULL;
  yajl_val jnode, jval;

  /* duplicate the data to make null-terminated string
   * required for yajl_tree_parse() */
  if ((sjson = calloc(1, len + 1)) == NULL)
    return -1;

  sstrncpy(sjson, data, len + 1);
  OVS_DEBUG("[len=%zu] %s", len, sjson);

  /* parse json data */
  jnode = yajl_tree_parse(sjson, yajl_errbuf, sizeof(yajl_errbuf));
  if (jnode == NULL) {
    OVS_ERROR("yajl_tree_parse() %s", yajl_errbuf);
    sfree(sjson);
    return -1;
  }

  /* get method name */
  if ((jval = yajl_tree_get(jnode, method_path, yajl_t_string)) != NULL) {
    if ((method = YAJL_GET_STRING(jval)) == NULL) {
      yajl_tree_free(jnode);
      sfree(sjson);
      return -1;
    }
    if (strcmp("echo", method) == 0) {
      /* echo request from the server */
      if (ovs_db_table_echo_cb(pdb, jnode) < 0)
        OVS_ERROR("handle echo request failed");
    } else if (strcmp("update", method) == 0) {
      /* update notification */
      if (ovs_db_table_update_cb(pdb, jnode) < 0)
        OVS_ERROR("handle update notification failed");
    }
  } else if ((jval = yajl_tree_get(jnode, result_path, yajl_t_any)) != NULL) {
    /* result notification */
    if (ovs_db_result_cb(pdb, jnode) < 0)
      OVS_ERROR("handle result reply failed");
  } else
    OVS_ERROR("cannot find method or result failed");

  /* release memory */
  yajl_tree_free(jnode);
  sfree(sjson);
  return 0;
}

/*
 * JSON reader implementation.
 *
 * This module process raw JSON data (byte stream) and
 * returns fully-fledged JSON data which can be processed
 * (parsed) by YAJL later.
 */

/* Allocate JSON reader instance */
static ovs_json_reader_t *ovs_json_reader_alloc() {
  ovs_json_reader_t *jreader = NULL;

  if ((jreader = calloc(1, sizeof(*jreader))) == NULL)
    return NULL;

  return jreader;
}

/* Push raw data into into the JSON reader for processing */
static int ovs_json_reader_push_data(ovs_json_reader_t *jreader,
                                     const char *data, size_t data_len) {
  char *new_buff = NULL;
  size_t available = jreader->buff_size - jreader->buff_offset;

  /* check/update required memory space */
  if (available < data_len) {
    OVS_DEBUG("Reallocate buffer [size=%d, available=%d required=%d]",
              (int)jreader->buff_size, (int)available, (int)data_len);

    /* allocate new chunk of memory */
    new_buff = realloc(jreader->buff_ptr, (jreader->buff_size + data_len));
    if (new_buff == NULL)
      return -1;

    /* point to new allocated memory */
    jreader->buff_ptr = new_buff;
    jreader->buff_size += data_len;
  }

  /* store input data */
  memcpy(jreader->buff_ptr + jreader->buff_offset, data, data_len);
  jreader->buff_offset += data_len;
  return 0;
}

/* Pop one fully-fledged JSON if already exists. Returns 0 if
 * completed JSON already exists otherwise negative value is
 * returned */
static int ovs_json_reader_pop(ovs_json_reader_t *jreader,
                               const char **json_ptr, size_t *json_len_ptr) {
  size_t nbraces = 0;
  size_t json_len = 0;
  char *json = NULL;

  /* search open/close brace */
  for (size_t i = jreader->json_offset; i < jreader->buff_offset; i++) {
    if (jreader->buff_ptr[i] == '{') {
      nbraces++;
    } else if (jreader->buff_ptr[i] == '}')
      if (nbraces)
        if (!(--nbraces)) {
          /* JSON data */
          *json_ptr = jreader->buff_ptr + jreader->json_offset;
          *json_len_ptr = json_len + 1;
          jreader->json_offset = i + 1;
          return 0;
        }

    /* increase JSON data length */
    if (nbraces)
      json_len++;
  }

  if (jreader->json_offset) {
    if (jreader->json_offset < jreader->buff_offset) {
      /* shift data to the beginning of the buffer
       * and zero rest of the buffer data */
      json = &jreader->buff_ptr[jreader->json_offset];
      json_len = jreader->buff_offset - jreader->json_offset;
      for (size_t i = 0; i < jreader->buff_size; i++)
        jreader->buff_ptr[i] = ((i < json_len) ? (json[i]) : (0));
      jreader->buff_offset = json_len;
    } else
      /* reset the buffer */
      jreader->buff_offset = 0;

    /* data is at the beginning of the buffer */
    jreader->json_offset = 0;
  }

  return -1;
}

/* Reset JSON reader. It is useful when start processing
 * new raw data. E.g.: in case of lost stream connection.
 */
static void ovs_json_reader_reset(ovs_json_reader_t *jreader) {
  if (jreader) {
    jreader->buff_offset = 0;
    jreader->json_offset = 0;
  }
}

/* Release internal data allocated for JSON reader */
static void ovs_json_reader_free(ovs_json_reader_t *jreader) {
  if (jreader) {
    free(jreader->buff_ptr);
    free(jreader);
  }
}

/* Reconnect to OVS DB and call the OVS DB post connection init callback
 * if connection has been established.
 */
static void ovs_db_reconnect(ovs_t *ovs, ovs_db_t *pdb) {
  const char *node_info = pdb->inst.node;
  struct addrinfo *result;

  if (pdb->inst.unix_path[0] != '\0') {
    /* use UNIX socket instead of INET address */
    node_info = pdb->inst.unix_path;
    result = calloc(1, sizeof(*result));
    struct sockaddr_un *sa_unix = calloc(1, sizeof(*sa_unix));
    if (result == NULL || sa_unix == NULL) {
      sfree(result);
      sfree(sa_unix);
      return;
    }
    result->ai_family = AF_UNIX;
    result->ai_socktype = SOCK_STREAM;
    result->ai_addrlen = sizeof(*sa_unix);
    result->ai_addr = (struct sockaddr *)sa_unix;
    sa_unix->sun_family = result->ai_family;
    sstrncpy(sa_unix->sun_path, pdb->inst.unix_path, sizeof(sa_unix->sun_path));
  } else {
    /* inet socket address */
    struct addrinfo hints;

    /* setup criteria for selecting the socket address */
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    /* get socket addresses */
    int ret = getaddrinfo(pdb->inst.node, pdb->inst.service, &hints, &result);
    if (ret != 0) {
      OVS_ERROR("getaddrinfo(): %s", gai_strerror(ret));
      return;
    }
  }
  /* try to connect to the server */
  for (struct addrinfo *rp = result; rp != NULL; rp = rp->ai_next) {
    char errbuff[OVS_ERROR_BUFF_SIZE];
    int sock = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
    if (sock < 0) {
      sstrerror(errno, errbuff, sizeof(errbuff));
      OVS_DEBUG("socket(): %s", errbuff);
      continue;
    }
    if (connect(sock, rp->ai_addr, rp->ai_addrlen) < 0) {
      close(sock);
      sstrerror(errno, errbuff, sizeof(errbuff));
      OVS_DEBUG("connect(): %s [family=%d]", errbuff, rp->ai_family);
    } else {
      /* send notification to event thread */
      pdb->sock = sock;
      if (ovs_event_post(ovs, pdb, OVS_DB_EVENT_CONN_ESTABLISHED) > 0)
        OVS_ERROR("Cannot send OVS_DB_EVENT_CONN_ESTABLISHED event."
                  "Event queue is full");
      break;
    }
  }

  if (pdb->sock < 0)
    OVS_ERROR("connect to \"%s\" failed", node_info);

  freeaddrinfo(result);
}

/* Handle poll() event */
static void ovs_db_process_poll_event(ovs_t *ovs, ovs_db_t *pdb, int events) {
  if (events & POLLNVAL) {
    /* invalid file descriptor, clean-up */
    ovs_db_callback_remove_all(pdb);
    ovs_json_reader_reset(pdb->jreader);
    /* setting poll FD to -1 tells poll() call to ignore this FD.
     * In that case poll() call will return timeout all the time */
    pdb->sock = -1;
  } else if ((events & POLLERR) || (events & POLLHUP)) {
    /* connection is broken */
    close(pdb->sock);
    if (ovs_event_post(ovs, pdb, OVS_DB_EVENT_CONN_TERMINATED) > 0)
      OVS_ERROR("Cannot send OVS_DB_EVENT_CONN_TERMINATED event."
                "Event queue is full");
    OVS_ERROR("poll() peer closed its end of the channel");
  } else if ((events & POLLIN) || (events & POLLPRI)) {
    /* read incoming data */
    char buff[OVS_DB_POLL_READ_BLOCK_SIZE];
    ssize_t nbytes = recv(pdb->sock, buff, sizeof(buff), 0);
    if (nbytes < 0) {
      char errbuff[OVS_ERROR_BUFF_SIZE];
      sstrerror(errno, errbuff, sizeof(errbuff));
      OVS_ERROR("recv(): %s", errbuff);
      /* read error? Try to reconnect */
      close(pdb->sock);
      return;
    } else if (nbytes == 0) {
      close(pdb->sock);
      if (ovs_event_post(ovs, pdb, OVS_DB_EVENT_CONN_TERMINATED) > 0)
        OVS_ERROR("Cannot send OVS_DB_EVENT_CONN_TERMINATED event."
                  "Event queue is full");
      OVS_ERROR("recv() peer has performed an orderly shutdown");
      return;
    }
    /* read incoming data */
    size_t json_len = 0;
    const char *json = NULL;
    OVS_DEBUG("recv(): received %zd bytes of data", nbytes);
    ovs_json_reader_push_data(pdb->jreader, buff, nbytes);
    while (!ovs_json_reader_pop(pdb->jreader, &json, &json_len))
      /* process JSON data */
      ovs_db_json_data_process(pdb, json, json_len);
  }
}

/* POLL worker thread.
 * It listens on OVS DB connection for incoming
 * requests/reply/events etc. Also, it reconnects to OVS DB
 * if connection has been lost.
 */
static void *ovs_poll_worker(void *arg) {
  ovs_t *ovs = (ovs_t *)arg; /* pointer to OVS object */
  struct pollfd poll_fd[ovs->db_num];
  ovs_db_t *poll_fd_map[ovs->db_num];

  /* init OVS DB poll() structures and create a [poll fd <-> ovs db] map */
  ovs_db_t *pdb = ovs->db_list;
  for (size_t i = 0; i < ovs->db_num; i++, pdb = pdb->next) {
    poll_fd[i].events = POLLIN | POLLPRI;
    poll_fd[i].fd = pdb->sock;
    poll_fd[i].revents = 0;
    poll_fd_map[i] = pdb;
  }
  while (ovs_db_poll_is_running(ovs)) {
    /* update poll() FDs */
    for (size_t i = 0; i < ovs->db_num; i++) {
      poll_fd[i].fd = poll_fd_map[i]->sock;
    }
    /* poll data from all instances */
    int poll_ret = poll(poll_fd, ovs->db_num, /* ms */
                        OVS_DB_POLL_TIMEOUT * 1000);
    if (poll_ret < 0) {
      char errbuff[OVS_ERROR_BUFF_SIZE];
      sstrerror(errno, errbuff, sizeof(errbuff));
      OVS_ERROR("poll(): %s", errbuff);
      break;
    } else if (poll_ret == 0) {
      OVS_DEBUG("poll(): timeout");
      /* try to restart OVS DB connections */
      for (ovs_db_t *pdb = ovs->db_list; pdb != NULL; pdb = pdb->next) {
        if (pdb->sock < 0)
          /* invalid fd, so try to reconnect */
          ovs_db_reconnect(ovs, pdb);
      }
      continue;
    }
    /* process poll event for each OVS DB */
    for (size_t i = 0; i < ovs->db_num; i++) {
      if (poll_fd[i].revents)
        ovs_db_process_poll_event(ovs, poll_fd_map[i], poll_fd[i].revents);
    }
  }
  OVS_DEBUG("poll thread has been completed");
  return NULL;
}

/* EVENT worker thread.
 * Perform task based on incoming events. This
 * task can be done asynchronously which allows to
 * handle OVS DB callback like 'init_cb'.
 */
static void *ovs_event_worker(void *arg) {
  ovs_t *ovs = (ovs_t *)arg;
  ovs_db_event_t event;

  /* unlock the event queue lock, previously locked on
   * the thread initialization */
  pthread_mutex_unlock(&ovs->event_thread.ev_queue.lock);
  while (1) {
    /* wait for incoming event */
    event = ovs_event_get(ovs);

    /* handle the event */
    OVS_DEBUG("Handling event %s", ovs_db_event_id_to_str(event.id));
    ovs_db_t *pdb = event.pdb;
    switch (event.id) {
    case OVS_DB_EVENT_CONN_ESTABLISHED:
      if (pdb->inst.cb.post_conn_init)
        pdb->inst.cb.post_conn_init(pdb, pdb->inst.user_data.data);
      break;
    case OVS_DB_EVENT_CONN_TERMINATED:
      if (pdb->inst.cb.post_conn_terminate)
        pdb->inst.cb.post_conn_terminate(pdb, pdb->inst.user_data.data);
      break;
    case OVS_DB_EVENT_TERMINATE:
      OVS_DEBUG("event thread has been terminated");
      return NULL;
    default:
      OVS_ERROR("unknown event received");
      return (void *)-1;
    }
  }

  OVS_DEBUG("event thread has been completed");
  return NULL;
}

/* Initialize EVENT thread */
static int ovs_db_event_thread_init(ovs_t *ovs) {
  ovs->event_thread.tid = (pthread_t)-1;

  /* init the OvS event queue */
  ovs_equeue_t *ev_queue = &ovs->event_thread.ev_queue;
  memset(ev_queue, 0, sizeof(*ev_queue));
  ev_queue->size = OVS_QUEUE_LEN + 1;
  ev_queue->queue = calloc(ev_queue->size, sizeof(*ev_queue->queue));
  if (ev_queue->queue == NULL)
    return -1;

  /* init event queue condition variable */
  if (pthread_cond_init(&ev_queue->wait_event, NULL)) {
    sfree(ev_queue->queue);
    return -1;
  }
  /* init event queue lock */
  if (pthread_mutex_init(&ev_queue->lock, NULL)) {
    pthread_cond_destroy(&ev_queue->wait_event);
    sfree(ev_queue->queue);
    return -1;
  }
  /* Hold the event thread mutex. It ensures that no events
   * will be lost while thread is still starting. Once event
   * thread is started and ready to accept events, it will release
   * the mutex */
  if (pthread_mutex_lock(&ev_queue->lock)) {
    pthread_mutex_destroy(&ev_queue->lock);
    pthread_cond_destroy(&ev_queue->wait_event);
    sfree(ev_queue->queue);
    return -1;
  }
  /* start event thread */
  pthread_t tid;
  if (plugin_thread_create(&tid, NULL, ovs_event_worker, ovs,
                           "utils_ovs:event") != 0) {
    pthread_mutex_unlock(&ev_queue->lock);
    pthread_mutex_destroy(&ev_queue->lock);
    pthread_cond_destroy(&ev_queue->wait_event);
    sfree(ev_queue->queue);
    return -1;
  }
  ovs->event_thread.tid = tid;
  return 0;
}

/* Destroy EVENT thread */
static int ovs_db_event_thread_destroy(ovs_t *ovs) {
  if (ovs->event_thread.tid == (pthread_t)-1)
    /* already destroyed */
    return 0;

  /* terminate the thread */
  if (ovs_event_post(ovs, NULL, OVS_DB_EVENT_TERMINATE) != 0) {
    OVS_ERROR("Send OVS_DB_EVENT_TERMINATE event failed");
    return -1;
  }
  if (pthread_join(ovs->event_thread.tid, NULL) != 0)
    return -1;

  /* destroy the OvS event queue */
  ovs_equeue_t *ev_queue = &ovs->event_thread.ev_queue;
  pthread_mutex_destroy(&ev_queue->lock);
  pthread_cond_destroy(&ev_queue->wait_event);
  ovs->event_thread.tid = (pthread_t)-1;
  sfree(ev_queue->queue);
  return 0;
}

/* Initialize POLL thread */
static int ovs_db_poll_thread_init(ovs_t *ovs) {
  ovs->poll_thread.tid = (pthread_t)-1;
  /* init event thread mutex */
  if (pthread_mutex_init(&ovs->poll_thread.mutex, NULL)) {
    return -1;
  }
  /* start poll thread */
  pthread_t tid;
  ovs->poll_thread.state = OVS_DB_POLL_STATE_RUNNING;
  if (plugin_thread_create(&tid, NULL, ovs_poll_worker, ovs,
                           "utils_ovs:poll") != 0) {
    pthread_mutex_destroy(&ovs->poll_thread.mutex);
    return -1;
  }
  ovs->poll_thread.tid = tid;
  return 0;
}

/* Destroy POLL thread */
static int ovs_db_poll_thread_destroy(ovs_t *ovs) {
  if (ovs->poll_thread.tid == (pthread_t)-1)
    /* already destroyed */
    return 0;
  /* change thread state */
  pthread_mutex_lock(&ovs->poll_thread.mutex);
  ovs->poll_thread.state = OVS_DB_POLL_STATE_EXITING;
  pthread_mutex_unlock(&ovs->poll_thread.mutex);
  /* join the thread */
  if (pthread_join(ovs->poll_thread.tid, NULL) != 0)
    return -1;
  pthread_mutex_destroy(&ovs->poll_thread.mutex);
  ovs->poll_thread.tid = (pthread_t)-1;
  return 0;
}

/* create a new OVS DB */
static ovs_db_t *ovs_db_create_new() {
  ovs_db_t *new_db = calloc(1, sizeof(*new_db));
  if (new_db == NULL)
    return NULL;

  /* init OVS DB mutex attributes */
  pthread_mutexattr_t mutex_attr;
  if (pthread_mutexattr_init(&mutex_attr)) {
    sfree(new_db);
    return NULL;
  }
  /* set OVS DB mutex as recursive */
  if (pthread_mutexattr_settype(&mutex_attr, PTHREAD_MUTEX_RECURSIVE)) {
    pthread_mutexattr_destroy(&mutex_attr);
    sfree(new_db);
    return NULL;
  }
  /* init OVS DB mutex */
  if (pthread_mutex_init(&new_db->mutex, &mutex_attr)) {
    pthread_mutexattr_destroy(&mutex_attr);
    sfree(new_db);
    return NULL;
  }
  /* destroy mutex attributes */
  pthread_mutexattr_destroy(&mutex_attr);

  /* create JSON reader instance */
  if ((new_db->jreader = ovs_json_reader_alloc()) == NULL) {
    pthread_mutex_destroy(&new_db->mutex);
    sfree(new_db);
    return NULL;
  }
  new_db->sock = -1;
  return new_db;
}

/* Destroy OVS DB */
static void ovs_db_destroy(ovs_db_t *pdb) {
  /* lock the structure */
  pthread_mutex_lock(&pdb->mutex);

  /* unsubscribe callbacks */
  ovs_db_callback_remove_all(pdb);

  /* close connection */
  if (pdb->sock >= 0)
    close(pdb->sock);

  /* release DB handler */
  pthread_mutex_unlock(&pdb->mutex);
  pthread_mutex_destroy(&pdb->mutex);

  /* release memory */
  if (pdb->inst.user_data.data != NULL &&
      pdb->inst.user_data.free_func != NULL) {
    pdb->inst.user_data.free_func(pdb->inst.user_data.data);
  }
  ovs_json_reader_free(pdb->jreader);
  sfree(pdb);
}

/*
 * Public OVS DB API implementation
 */

ovs_t *ovs_init(const ovs_db_inst_t *instances, size_t num) {
  /* sanity check */
  if (instances == NULL || !num)
    return NULL;

  /* allocate new OVS instance that will be returned to the caller */
  ovs_t *ovs = calloc(1, sizeof(*ovs));
  if (ovs == NULL)
    return NULL;

  /* init OVS mutex */
  if (pthread_mutex_init(&ovs->mutex, NULL)) {
    OVS_ERROR("init OVS mutex failed");
    sfree(ovs);
    return NULL;
  }
  /* create OVS DB instances */
  for (size_t i = 0; i < num; i++, instances++) {
    ovs_db_t *new_db = ovs_db_create_new();
    if (new_db == NULL) {
      OVS_ERROR("create new OVS DB failed");
      ovs_destroy(ovs);
      return NULL;
    }
    /* add new OVS DB to the OVS object */
    new_db->inst = *instances;
    new_db->next = ovs->db_list;
    ovs->db_list = new_db;
  }
  ovs->db_num = num;
  /* init event thread */
  if (ovs_db_event_thread_init(ovs) < 0) {
    ovs_destroy(ovs);
    return NULL;
  }
  /* init polling thread */
  if (ovs_db_poll_thread_init(ovs) < 0) {
    ovs_destroy(ovs);
    return NULL;
  }
  return ovs;
}

int ovs_db_send_request(ovs_db_t *pdb, const char *method, const char *params,
                        ovs_db_result_cb_t cb) {
  int ret = 0;
  yajl_gen_status yajl_gen_ret;
  yajl_val jparams;
  yajl_gen jgen;
  ovs_callback_t *new_cb = NULL;
  uint64_t uid;
  char uid_buff[OVS_UID_STR_SIZE];
  const char *req = NULL;
  size_t req_len = 0;
  struct timespec ts;

  /* sanity check */
  if (!pdb || !method || !params)
    return -1;

  if ((jgen = yajl_gen_alloc(NULL)) == NULL)
    return -1;

  /* try to parse params */
  if ((jparams = yajl_tree_parse(params, NULL, 0)) == NULL) {
    OVS_ERROR("params is not a JSON string");
    yajl_gen_clear(jgen);
    return -1;
  }

  /* generate method field */
  OVS_YAJL_CALL(yajl_gen_map_open, jgen);

  OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "method");
  OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, method);

  /* generate params field */
  OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "params");
  OVS_YAJL_CALL(ovs_yajl_gen_val, jgen, jparams);
  yajl_tree_free(jparams);

  /* generate id field */
  OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "id");
  uid = ovs_uid_generate();
  ssnprintf(uid_buff, sizeof(uid_buff), "%" PRIX64, uid);
  OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, uid_buff);

  OVS_YAJL_CALL(yajl_gen_map_close, jgen);

  if (cb) {
    /* register result callback */
    if ((new_cb = calloc(1, sizeof(*new_cb))) == NULL)
      goto yajl_gen_failure;

    /* add new callback to front */
    sem_init(&new_cb->result.sync, 0, 0);
    new_cb->result.call = cb;
    new_cb->uid = uid;
    ovs_db_callback_add(pdb, new_cb);
  }

  /* send the request */
  OVS_YAJL_CALL(yajl_gen_get_buf, jgen, (const unsigned char **)&req, &req_len);
  OVS_DEBUG("%s", req);
  if (!ovs_db_data_send(pdb, req, req_len)) {
    if (cb) {
      /* wait for result */
      clock_gettime(CLOCK_REALTIME, &ts);
      ts.tv_sec += OVS_DB_SEND_REQ_TIMEOUT;
      if (sem_timedwait(&new_cb->result.sync, &ts) < 0) {
        OVS_ERROR("%s() no replay received within %d sec", __FUNCTION__,
                  OVS_DB_SEND_REQ_TIMEOUT);
        ret = -1;
      }
    }
  } else {
    OVS_ERROR("ovs_db_data_send() failed");
    ret = -1;
  }

yajl_gen_failure:
  if (new_cb) {
    /* destroy callback */
    sem_destroy(&new_cb->result.sync);
    ovs_db_callback_remove(pdb, new_cb);
  }

  /* release memory */
  yajl_gen_clear(jgen);
  return (yajl_gen_ret != yajl_gen_status_ok) ? (-1) : ret;
}

int ovs_db_table_cb_register(ovs_db_t *pdb, const char *tb_name,
                             const char **tb_column,
                             ovs_db_table_cb_t update_cb,
                             ovs_db_result_cb_t result_cb, unsigned int flags) {
  yajl_gen jgen;
  yajl_gen_status yajl_gen_ret;
  ovs_callback_t *new_cb = NULL;
  char uid_str[OVS_UID_STR_SIZE];
  char *params;
  size_t params_len;
  int ovs_db_ret = 0;

  /* sanity check */
  if (pdb == NULL || tb_name == NULL || update_cb == NULL)
    return -1;

  /* allocate new update callback */
  if ((new_cb = calloc(1, sizeof(*new_cb))) == NULL)
    return -1;

  /* init YAJL generator */
  if ((jgen = yajl_gen_alloc(NULL)) == NULL) {
    sfree(new_cb);
    return -1;
  }

  /* add new callback to front */
  new_cb->table.call = update_cb;
  new_cb->uid = ovs_uid_generate();
  ovs_db_callback_add(pdb, new_cb);

  /* make update notification request
   * [<db-name>, <json-value>, <monitor-requests>] */
  OVS_YAJL_CALL(yajl_gen_array_open, jgen);
  {
    OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, OVS_DB_DEFAULT_DB_NAME);

    /* uid string <json-value> */
    ssnprintf(uid_str, sizeof(uid_str), "%" PRIX64, new_cb->uid);
    OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, uid_str);

    /* <monitor-requests> */
    OVS_YAJL_CALL(yajl_gen_map_open, jgen);
    {
      OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, tb_name);
      OVS_YAJL_CALL(yajl_gen_array_open, jgen);
      {
        /* <monitor-request> */
        OVS_YAJL_CALL(yajl_gen_map_open, jgen);
        {
          if (tb_column) {
            /* columns within the table to be monitored */
            OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "columns");
            OVS_YAJL_CALL(yajl_gen_array_open, jgen);
            for (; *tb_column; tb_column++)
              OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, *tb_column);
            OVS_YAJL_CALL(yajl_gen_array_close, jgen);
          }
          /* specify select option */
          OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "select");
          {
            OVS_YAJL_CALL(yajl_gen_map_open, jgen);
            {
              OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "initial");
              OVS_YAJL_CALL(yajl_gen_bool, jgen,
                            flags & OVS_DB_TABLE_CB_FLAG_INITIAL);
              OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "insert");
              OVS_YAJL_CALL(yajl_gen_bool, jgen,
                            flags & OVS_DB_TABLE_CB_FLAG_INSERT);
              OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "delete");
              OVS_YAJL_CALL(yajl_gen_bool, jgen,
                            flags & OVS_DB_TABLE_CB_FLAG_DELETE);
              OVS_YAJL_CALL(ovs_yajl_gen_tstring, jgen, "modify");
              OVS_YAJL_CALL(yajl_gen_bool, jgen,
                            flags & OVS_DB_TABLE_CB_FLAG_MODIFY);
            }
            OVS_YAJL_CALL(yajl_gen_map_close, jgen);
          }
        }
        OVS_YAJL_CALL(yajl_gen_map_close, jgen);
      }
      OVS_YAJL_CALL(yajl_gen_array_close, jgen);
    }
    OVS_YAJL_CALL(yajl_gen_map_close, jgen);
  }
  OVS_YAJL_CALL(yajl_gen_array_close, jgen);

  /* make a request to subscribe to given table */
  OVS_YAJL_CALL(yajl_gen_get_buf, jgen, (const unsigned char **)&params,
                &params_len);
  if (ovs_db_send_request(pdb, "monitor", params, result_cb) < 0) {
    OVS_ERROR("Failed to subscribe to \"%s\" table", tb_name);
    ovs_db_ret = -1;
  }

yajl_gen_failure:
  /* release memory */
  yajl_gen_clear(jgen);
  return ovs_db_ret;
}

int ovs_destroy(ovs_t *ovs) {
  int ovs_db_ret = 0;
  int ret = 0;

  /* sanity check */
  if (ovs == NULL)
    return -1;

  /* try to lock the structure before releasing */
  if ((ret = pthread_mutex_lock(&ovs->mutex))) {
    OVS_ERROR("pthread_mutex_lock() OVS mutex lock failed (%d)", ret);
    return -1;
  }
  /* stop poll thread */
  if (ovs_db_event_thread_destroy(ovs) < 0) {
    OVS_ERROR("destroy poll thread failed");
    ovs_db_ret = -1;
  }
  /* stop event thread */
  if (ovs_db_poll_thread_destroy(ovs) < 0) {
    OVS_ERROR("stop event thread failed");
    ovs_db_ret = -1;
  }
  /* destroy list of OVS DB instances */
  for (ovs_db_t *del_db = ovs->db_list; del_db != NULL; del_db = ovs->db_list) {
    ovs->db_list = del_db->next;
    ovs_db_destroy(del_db);
  }
  ovs->db_list = NULL;

  /* unlock and destroy the mutex */
  if ((ret = pthread_mutex_unlock(&ovs->mutex))) {
    OVS_ERROR("pthread_mutex_unlock() OVS mutex unlock failed (%d)", ret);
    ovs_db_ret = -1;
  }
  pthread_mutex_destroy(&ovs->mutex);
  return ovs_db_ret;
}

/*
 * Public OVS utils API implementation
 */

/* Get YAJL value by key from YAJL dictionary
 *
 * EXAMPLE:
 *  {
 *    "key_a" : <YAJL return value>
 *    "key_b" : <YAJL return value>
 *  }
 */
yajl_val ovs_utils_get_value_by_key(yajl_val jval, const char *key) {
  const char *obj_key = NULL;

  /* check params */
  if (!YAJL_IS_OBJECT(jval) || (key == NULL))
    return NULL;

  /* find a value by key */
  for (size_t i = 0; i < YAJL_GET_OBJECT(jval)->len; i++) {
    obj_key = YAJL_GET_OBJECT(jval)->keys[i];
    if (strcmp(obj_key, key) == 0)
      return YAJL_GET_OBJECT(jval)->values[i];
  }

  return NULL;
}

/* Get OVS DB map value by given map key
 *
 * FROM RFC7047:
 *
 *   <pair>
 *     A 2-element JSON array that represents a pair within a database
 *     map.  The first element is an <atom> that represents the key, and
 *     the second element is an <atom> that represents the value.
 *
 *   <map>
 *     A 2-element JSON array that represents a database map value.  The
 *     first element of the array must be the string "map", and the
 *     second element must be an array of zero or more <pair>s giving the
 *     values in the map.  All of the <pair>s must have the same key and
 *     value types.
 *
 * EXAMPLE:
 *  [
 *    "map", [
 *             [ "key_a", <YAJL value>], [ "key_b", <YAJL value>], ...
 *           ]
 *  ]
 */
yajl_val ovs_utils_get_map_value(yajl_val jval, const char *key) {
  size_t map_len = 0;
  size_t array_len = 0;
  yajl_val *map_values = NULL;
  yajl_val *array_values = NULL;
  const char *str_val = NULL;

  /* check YAJL array */
  if (!YAJL_IS_ARRAY(jval) || (key == NULL))
    return NULL;

  /* check a database map value (2-element, first one should be a string */
  array_len = YAJL_GET_ARRAY(jval)->len;
  array_values = YAJL_GET_ARRAY(jval)->values;
  if ((array_len != 2) || (!YAJL_IS_STRING(array_values[0])) ||
      (!YAJL_IS_ARRAY(array_values[1])))
    return NULL;

  /* check first element of the array */
  str_val = YAJL_GET_STRING(array_values[0]);
  if (strcmp("map", str_val) != 0)
    return NULL;

  /* try to find map value by map key */
  map_len = YAJL_GET_ARRAY(array_values[1])->len;
  map_values = YAJL_GET_ARRAY(array_values[1])->values;
  for (size_t i = 0; i < map_len; i++) {
    /* check YAJL array */
    if (!YAJL_IS_ARRAY(map_values[i]))
      break;

    /* check a database pair value (2-element, first one represents a key
     * and it should be a string in our case */
    array_len = YAJL_GET_ARRAY(map_values[i])->len;
    array_values = YAJL_GET_ARRAY(map_values[i])->values;
    if ((array_len != 2) || (!YAJL_IS_STRING(array_values[0])))
      break;

    /* return map value if given key equals map key */
    str_val = YAJL_GET_STRING(array_values[0]);
    if (strcmp(key, str_val) == 0)
      return array_values[1];
  }
  return NULL;
}
