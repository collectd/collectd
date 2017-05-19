/**
 * collectd - src/netlink2.c
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
 *   Red Hat NFVPE
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_complain.h"

#include <pthread.h>
#include <asm/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <netdb.h>
#include <yajl/yajl_tree.h>


/*
 * Private data types
 */

typedef struct {
    int head;
    int tail;
    int maxLen;
    char **buffer;
} circbuf_t;

/*
 * Private variables
 */

static int sysevent_thread_loop = 0;
static int sysevent_thread_error = 0;
static pthread_t sysevent_thread_id;
static pthread_mutex_t sysevent_lock = PTHREAD_MUTEX_INITIALIZER;
static int sock = -1;
static circbuf_t ring;

static char * listen_ip;
static char * listen_port;
static int listen_buffer_size;
static int buffer_length;

static const char * rsyslog_keys[3] = {"@timestamp", "@source_host", "@message"};
static const char * rsyslog_field_keys[4] = {"facility", "severity", "program", "processid"};


/*
 * Private functions
 */

static void *sysevent_thread(void *arg) /* {{{ */
{
  pthread_mutex_lock(&sysevent_lock);

  while (sysevent_thread_loop > 0) 
  {
    int status = 0;

    pthread_mutex_unlock(&sysevent_lock);

    if (sock == -1)
      return ((void*)0);

    //INFO("sysevent plugin: listening for events");

    // read here
    char buffer[listen_buffer_size];
    struct sockaddr_storage src_addr;
    socklen_t src_addr_len = sizeof(src_addr);

    memset(buffer, '\0', listen_buffer_size);

    ssize_t count = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*) &src_addr, &src_addr_len);

    if (count == -1) 
    {
        ERROR("sysevent plugin: failed to receive data: %s", strerror(errno));
        status = -1;
    } else if (count >= sizeof(buffer)) {
        WARNING("sysevent plugin: datagram too large for buffer: truncated");
    } else {
        //INFO("sysevent plugin: received");

        // 1. Parse message
        // 2. Acquire lock
        // 3. Push to buffer if there is room, otherwise report error?

        pthread_mutex_lock(&sysevent_lock);

        int next = ring.head + 1;
        if (next >= ring.maxLen)
          next = 0;

        if (next == ring.tail)
        {
          WARNING("sysevent plugin: ring buffer full");
        } else {

          yajl_val node;
          char errbuf[1024];

          errbuf[0] = 0;

          node = yajl_tree_parse((const char *) buffer, errbuf, sizeof(errbuf));

          if (node == NULL)
          {
            ERROR("sysevent plugin: fail to parse JSON: %s", errbuf);
          } else {

            // char json_val[listen_buffer_size];
            // const char * path[] = { "@timestamp", (const char *) 0 };
            // yajl_val v = yajl_tree_get(node, path, yajl_t_string);

            // memset(json_val, '\0', listen_buffer_size);

            // sprintf(json_val, "%s%c", YAJL_GET_STRING(v), '\0');

            INFO("sysevent plugin: writing %s", buffer);

            strncpy(ring.buffer[ring.head], buffer, sizeof(buffer));
            ring.head = next;
          }

          yajl_tree_free(node);

          // Send notification for kafka to intercept

          // notification_t n = {
          //     NOTIF_WARNING, cdtime(), "", "", "sysevent", "", "", "", NULL};

          // sstrncpy(n.host, hostname_g, sizeof(n.host));
          // ssnprintf(n.message, sizeof(n.message), buffer);

          // plugin_dispatch_notification(&n);
        }

        pthread_mutex_unlock(&sysevent_lock);
    }
    
    usleep(1000);

    pthread_mutex_lock(&sysevent_lock);

    if (status < 0)
    {
      WARNING("sysevent plugin: problem thread status: %d", status);
      sysevent_thread_error = 1;
      break;
    }
    
    if (sysevent_thread_loop <= 0)
      break;
  } /* while (sysevent_thread_loop > 0) */

  pthread_mutex_unlock(&sysevent_lock);

  // pthread_exit instead of return
  return ((void *)0);
} /* }}} void *sysevent_thread */

static int start_thread(void) /* {{{ */
{
  int status;

  pthread_mutex_lock(&sysevent_lock);

  if (sysevent_thread_loop != 0) {
    pthread_mutex_unlock(&sysevent_lock);
    return (0);
  }

  sysevent_thread_loop = 1;
  sysevent_thread_error = 0;

  INFO("sysevent plugin: starting thread");

  status = plugin_thread_create(&sysevent_thread_id, /* attr = */ NULL, sysevent_thread,
                                /* arg = */ (void *)0, "sysevent");
  if (status != 0) {
    sysevent_thread_loop = 0;
    ERROR("sysevent plugin: starting thread failed.");
    pthread_mutex_unlock(&sysevent_lock);
    return (-1);
  }

  pthread_mutex_unlock(&sysevent_lock);
  return (0);
} /* }}} int start_thread */

static int stop_thread(int shutdown) /* {{{ */
{
  int status;

  pthread_mutex_lock(&sysevent_lock);

  if (sysevent_thread_loop == 0) {
    pthread_mutex_unlock(&sysevent_lock);
    return (-1);
  }

  sysevent_thread_loop = 0;
  pthread_mutex_unlock(&sysevent_lock);

  if (shutdown == 1)
  {
    // Since the thread is blocking, calling pthread_join
    // doesn't actually succeed in stopping it.  It will stick around
    // until a message is received on the socket (at which 
    // it will realize that "sysevent_thread_loop" is 0 and will 
    // break out of the read loop and be allowed to die).  This is
    // fine when the process isn't supposed to be exiting, but in 
    // the case of a process shutdown, we don't want to have an
    // idle thread hanging around.  Calling pthread_cancel here in 
    // the case of a shutdown is just assures that the thread is 
    // gone and that the process has been fully terminated.

    INFO("sysevent plugin: Canceling thread for process shutdown");

    status = pthread_cancel(sysevent_thread_id);

    if (status != 0)
    {
      ERROR("sysevent plugin: Unable to cancel thread: %d (%s)", status, strerror(errno));
      status = -1;
    }
  } else {
    status = pthread_join(sysevent_thread_id, /* return = */ NULL);
    if (status != 0) {
      ERROR("sysevent plugin: Stopping thread failed.");
      status = -1;
    }
  }

  pthread_mutex_lock(&sysevent_lock);
  memset(&sysevent_thread_id, 0, sizeof(sysevent_thread_id));
  sysevent_thread_error = 0;
  pthread_mutex_unlock(&sysevent_lock);

  INFO("sysevent plugin: Finished requesting stop of thread");

  return (status);
} /* }}} int stop_thread */

static int sysevent_init(void) /* {{{ */
{
  ring.head = 0;
  ring.tail = 0;
  ring.maxLen = buffer_length;
  ring.buffer = (char **)malloc(buffer_length * sizeof(char *));

  for (int i = 0; i < buffer_length; i ++)
  {
    ring.buffer[i] = malloc(listen_buffer_size);
  }

  if (sock == -1)
  {
    const char* hostname = listen_ip;
    const char* portname = listen_port;
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = 0;
    hints.ai_flags= AI_PASSIVE|AI_ADDRCONFIG;
    struct addrinfo* res = 0;

    int err = getaddrinfo(hostname, portname, &hints, &res);

    if (err != 0) 
    {
        ERROR("sysevent plugin: failed to resolve local socket address (err=%d)",err);
        freeaddrinfo(res);
        return (-1);
    }

    sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == -1) 
    {
        ERROR("sysevent plugin: failed to open socket: %s", strerror(errno));
        freeaddrinfo(res);
        return (-1);
    }

    if (bind(sock, res->ai_addr, res->ai_addrlen) == -1) 
    {
        ERROR("sysevent plugin: failed to bind socket: %s", strerror(errno));
        freeaddrinfo(res);
        return (-1);
    }

    freeaddrinfo(res);
  }

  INFO("sysevent plugin: socket created and bound");

  return (start_thread());
} /* }}} int sysevent_init */

static int sysevent_config_add_listen(const oconfig_item_t *ci) /* {{{ */
{
  if (ci->values_num != 2 || ci->values[0].type != OCONFIG_TYPE_STRING ||
      ci->values[1].type != OCONFIG_TYPE_STRING) {
    ERROR("sysevent plugin: The `%s' config option needs "
          "two string arguments (ip and port).",
          ci->key);
    return (-1);
  }

  listen_ip = strdup(ci->values[0].value.string);
  listen_port = strdup(ci->values[1].value.string);

  return (0);
}

static int sysevent_config_add_buffer_size(const oconfig_item_t *ci) /* {{{ */
{
  int tmp = 0;

  if (cf_util_get_int(ci, &tmp) != 0)
    return (-1);
  else if ((tmp >= 1024) && (tmp <= 65535))
    listen_buffer_size = tmp;
  else {
    WARNING(
        "sysevent plugin: The `BufferSize' must be between 1024 and 65535.");
    return (-1);
  }

  return (0);
}

static int sysevent_config_add_buffer_length(const oconfig_item_t *ci) /* {{{ */
{
  int tmp = 0;

  if (cf_util_get_int(ci, &tmp) != 0)
    return (-1);
  else if ((tmp >= 3) && (tmp <= 1024))
    buffer_length = tmp;
  else {
    WARNING(
        "sysevent plugin: The `Bufferlength' must be between 3 and 1024.");
    return (-1);
  }

  return (0);
}

static int sysevent_config(oconfig_item_t *ci) /* {{{ */
{
  // TODO

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Listen", child->key) == 0)
      sysevent_config_add_listen(child);
    else if (strcasecmp("BufferSize", child->key) == 0)
      sysevent_config_add_buffer_size(child);
    else if (strcasecmp("BufferLength", child->key) == 0)
      sysevent_config_add_buffer_length(child);
    else {
      WARNING("sysevent plugin: Option `%s' is not allowed here.", child->key);
    }
  }

  return (0);
} /* }}} int sysevent_config */

// TODO
static void submit(const char *something, const char *type, /* {{{ */
                   gauge_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = &(value_t){.gauge = value};
  vl.values_len = 1;
  sstrncpy(vl.plugin, "sysevent", sizeof(vl.plugin));
  //sstrncpy(vl.type_instance, something, sizeof(vl.type_instance));
  sstrncpy(vl.type, type, sizeof(vl.type));

  yajl_val node;
  char errbuf[1024];

  errbuf[0] = 0;

  node = yajl_tree_parse((const char *) something, errbuf, sizeof(errbuf));

  // Create metadata to store JSON key-values
  meta_data_t * meta = meta_data_create();

  size_t i = 0;

  for (i = 0; i < sizeof(rsyslog_keys) / sizeof(*rsyslog_keys); i ++)
  {
    char json_val[listen_buffer_size];
    const char * key = (const char *) rsyslog_keys[i];
    const char * path[] = { key, (const char *) 0 };
    yajl_val v = yajl_tree_get(node, path, yajl_t_string);

    memset(json_val, '\0', listen_buffer_size);

    sprintf(json_val, "%s%c", YAJL_GET_STRING(v), '\0');

    INFO("sysevent plugin: adding jsonval: %s", json_val);

    meta_data_add_string(meta, rsyslog_keys[i], json_val);
  }

  for (i = 0; i < sizeof(rsyslog_field_keys) / sizeof(*rsyslog_field_keys); i ++)
  {
    char json_val[listen_buffer_size];
    const char * key = (const char *) rsyslog_field_keys[i];
    const char * path[] = { "@fields", key, (const char *) 0 };
    yajl_val v = yajl_tree_get(node, path, yajl_t_string);

    memset(json_val, '\0', listen_buffer_size);

    sprintf(json_val, "%s%c", YAJL_GET_STRING(v), '\0');

    INFO("sysevent plugin: adding jsonval: %s", json_val);

    meta_data_add_string(meta, rsyslog_field_keys[i], json_val);
  }

  vl.meta = meta;

  struct timeval tv;

  gettimeofday(&tv, NULL);

  unsigned long long millisecondsSinceEpoch =
  (unsigned long long)(tv.tv_sec) * 1000 +
  (unsigned long long)(tv.tv_usec) / 1000;

  INFO("sysevent plugin (%llu): dispatching something", millisecondsSinceEpoch);

  plugin_dispatch_values(&vl);
} /* }}} void sysevent_submit */

static int sysevent_read(void) /* {{{ */
{
  if (sysevent_thread_error != 0) 
  {
    ERROR("sysevent plugin: The sysevent thread had a problem (%d). Restarting it.", sysevent_thread_error);

    stop_thread(0);

    start_thread();

    return (-1);
  } /* if (sysevent_thread_error != 0) */

  pthread_mutex_lock(&sysevent_lock);

  while (ring.head != ring.tail)
  {
    int next = ring.tail + 1;

    if (next >= ring.maxLen)
      next = 0;

    INFO("sysevent plugin: reading %s", ring.buffer[ring.tail]);

    submit(ring.buffer[ring.tail], "gauge", 1);

    ring.tail = next;
  }

  pthread_mutex_unlock(&sysevent_lock);

  return (0);
} /* }}} int sysevent_read */

static int sysevent_shutdown(void) /* {{{ */
{
  int status;

  INFO("sysevent plugin: Shutting down thread.");
  if (stop_thread(1) < 0)
    return (-1);

  if (sock != -1)
  {
    status = close(sock);
    if (status != 0)
    {
      ERROR("sysevent plugin: failed to close socket %d: %d (%s)", sock, status, strerror(errno));
      return (-1);
    } else
      sock = -1;
  }

  free(listen_ip);
  free(listen_port);

  for (int i = 0; i < buffer_length; i ++)
  {
    free(ring.buffer[i]);
  }

  free(ring.buffer);

  return (0);
} /* }}} int sysevent_shutdown */

void module_register(void) {
  plugin_register_complex_config("sysevent", sysevent_config);
  plugin_register_init("sysevent", sysevent_init);
  plugin_register_read("sysevent", sysevent_read);
  plugin_register_shutdown("sysevent", sysevent_shutdown);
} /* void module_register */