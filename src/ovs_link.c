/**
 * collectd - src/ovs_link.c
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

#include "common.h"             /* auxiliary functions */
#include "utils_ovs.h"          /* OVS helpers */

#define OVS_LINK_PLUGIN "ovs_link"
#define OVS_LINK_DEFAULT_OVS_DB_SERVER_URL "tcp:127.0.0.1:6640"
#define OVS_LINK_CTX_LOCK for (int __i = ovs_link_ctx_lock(); __i != 0 ; \
                               __i = ovs_link_ctx_unlock())
#define OVS_LINK_CONFIG_ERROR(option) do { \
  ERROR(OVS_LINK_PLUGIN ": read '%s' config option failed", option); \
  goto failure; } while (0)

/* Link status type */
enum ovs_link_link_status_e {DOWN, UP, UNKNOWN};
typedef enum ovs_link_link_status_e ovs_link_link_status_t;

/* Interface info */
struct ovs_link_interface_info_s {
  char *name;                   /* interface name */
  ovs_link_link_status_t link_status;   /* link status */
  struct ovs_link_interface_info_s *next;       /* next interface info */
};
typedef struct ovs_link_interface_info_s ovs_link_interface_info_t;

/* OVS link configuration data */
struct ovs_link_config_s {
  _Bool send_notification;      /* sent notification to collectd? */
  char *ovs_db_server_url;      /* OVS DB server URL */
};
typedef struct ovs_link_config_s ovs_link_config_t;

/* OVS link context type */
struct ovs_link_ctx_s {
  pthread_mutex_t mutex;        /* mutex to lock the context */
  pthread_mutexattr_t mutex_attr;       /* context mutex attribute */
  ovs_db_t *ovs_db;             /* pointer to OVS DB instance */
  ovs_link_config_t config;     /* plugin config */
  ovs_link_interface_info_t *ifaces;    /* interface info */
};
typedef struct ovs_link_ctx_s ovs_link_ctx_t;

/*
 * Private variables
 */
static ovs_link_ctx_t ovs_link_ctx = {
  .mutex = PTHREAD_MUTEX_INITIALIZER,
  .config = {
             .send_notification = 0,    /* do not send notification */
             .ovs_db_server_url = NULL},        /* use default OVS DB URL */
  .ovs_db = NULL,
  .ifaces = NULL};

/* This function is used only by "OVS_LINK_CTX_LOCK" define (see above).
 * It always returns 1 when context is locked.
 */
static inline int
ovs_link_ctx_lock()
{
  pthread_mutex_lock(&ovs_link_ctx.mutex);
  return (1);
}

/* This function is used only by "OVS_LINK_CTX_LOCK" define (see above).
 * It always returns 0 when context is unlocked.
 */
static inline int
ovs_link_ctx_unlock()
{
  pthread_mutex_unlock(&ovs_link_ctx.mutex);
  return (0);
}

/* Update link status in OVS link context (cache) */
static void
ovs_link_link_status_update(const char *name, ovs_link_link_status_t status)
{
  OVS_LINK_CTX_LOCK {
    for (ovs_link_interface_info_t *iface = ovs_link_ctx.ifaces; iface;
         iface = iface->next)
      if (strcmp(iface->name, name) == 0)
        iface->link_status = status;
  }
}

/* Check if given interface name exists in configuration file. It
 * returns 1 if exists otherwise 0. If no interfaces are configured,
 * 1 is returned
 */
static int
ovs_link_config_iface_exists(const char *ifname)
{
  int rc = 0;
  OVS_LINK_CTX_LOCK {
    if (!(rc = (ovs_link_ctx.ifaces == NULL))) {
      for (ovs_link_interface_info_t *iface = ovs_link_ctx.ifaces; iface;
           iface = iface->next)
        if (rc = (strcmp(ifname, iface->name) == 0))
          break;
    }
  }
  return rc;
}

/* Release memory allocated for configuration data */
static void
ovs_link_config_free()
{
  ovs_link_interface_info_t *del_iface = NULL;
  OVS_LINK_CTX_LOCK {
    sfree(ovs_link_ctx.config.ovs_db_server_url);
    while (ovs_link_ctx.ifaces) {
      del_iface = ovs_link_ctx.ifaces;
      ovs_link_ctx.ifaces = ovs_link_ctx.ifaces->next;
      free(del_iface->name);
      free(del_iface);
    }
  }
}

/* Parse plugin configuration file and store the config
 * in allocated memory. Returns negative value in case of error.
 */
static int
ovs_link_plugin_config(oconfig_item_t *ci)
{
  ovs_link_interface_info_t *new_iface;
  char *if_name;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("SendNotification", child->key) == 0) {
      if (cf_util_get_boolean(child,
                              &ovs_link_ctx.config.send_notification) < 0)
        OVS_LINK_CONFIG_ERROR(child->key);
    } else if (strcasecmp("OvsDbServerUrl", child->key) == 0) {
      if (cf_util_get_string(child,
                             &ovs_link_ctx.config.ovs_db_server_url) < 0)
        OVS_LINK_CONFIG_ERROR(child->key);
    } else if (strcasecmp("Interfaces", child->key) == 0) {
      for (int j = 0; j < child->values_num; j++) {
        /* check value type */
        if (child->values[j].type != OCONFIG_TYPE_STRING) {
          ERROR(OVS_LINK_PLUGIN
                ": given interface name is not a string [idx=%d]", j);
          goto failure;
        }
        /* get value */
        if ((if_name = strdup(child->values[j].value.string)) == NULL) {
          ERROR(OVS_LINK_PLUGIN " strdup() copy interface name fail");
          goto failure;
        }
        if ((new_iface = malloc(sizeof(*new_iface))) == NULL) {
          ERROR(OVS_LINK_PLUGIN ": malloc () copy interface name fail");
          goto failure;
        } else {
          /* store interface name */
          new_iface->name = if_name;
          new_iface->link_status = UNKNOWN;
          new_iface->next = ovs_link_ctx.ifaces;
          ovs_link_ctx.ifaces = new_iface;
          DEBUG(OVS_LINK_PLUGIN ": found monitored interface \"%s\"",
                if_name);
        }
      }
    } else {
      ERROR(OVS_LINK_PLUGIN ": option '%s' is not allowed here", child->key);
      goto failure;
    }
  }
  return (0);

failure:
  ovs_link_config_free();
  return (-1);
}

/* Dispatch OVS interface link status event to collectd */
static int
ovs_link_dispatch_notification(const char *link_name,
                               ovs_link_link_status_t link_status)
{
  const char *msg_link_status = NULL;
  notification_t n = {NOTIF_FAILURE, cdtime(), "", "", OVS_LINK_PLUGIN,
                      "", "", "", NULL};

  /* convert link status to message string */
  switch (link_status) {
  case UP:
    msg_link_status = "UP";
    n.severity = NOTIF_OKAY;
    break;
  case DOWN:
    msg_link_status = "DOWN";
    n.severity = NOTIF_WARNING;
    break;
  default:
    msg_link_status = "UNKNOWN";;
    break;
  }

  /* fill the notification data */
  ssnprintf(n.message, sizeof(n.message),
            "link state of \"%s\" interface has been changed to \"%s\"",
            link_name, msg_link_status);
  sstrncpy(n.host, hostname_g, sizeof(n.host));
  sstrncpy(n.plugin_instance, link_name, sizeof(n.plugin_instance));
  sstrncpy(n.type, "gauge", sizeof(n.type));
  sstrncpy(n.type_instance, "link_status", sizeof(n.type_instance));
  return plugin_dispatch_notification(&n);
}

/* Dispatch OVS interface link status value to collectd */
static void
ovs_link_link_status_submit(const char *link_name,
                            ovs_link_link_status_t link_status)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  values[0].gauge = (gauge_t) link_status;
  vl.time = cdtime();
  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);
  sstrncpy(vl.host, hostname_g, sizeof(vl.host));
  sstrncpy(vl.plugin, OVS_LINK_PLUGIN, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, link_name, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "gauge", sizeof(vl.type));
  sstrncpy(vl.type_instance, "link_status", sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);
}

/* Dispatch OVS DB terminate connection event to collectd */
static void
ovs_link_dispatch_terminate_notification(const char *msg)
{
  notification_t n = {NOTIF_FAILURE, cdtime(), "", "", OVS_LINK_PLUGIN,
                      "", "", "", NULL};
  ssnprintf(n.message, sizeof(n.message), msg);
  sstrncpy(n.host, hostname_g, sizeof(n.host));
  plugin_dispatch_notification(&n);
}

/* Process OVS DB update table event. It handles link status update event(s)
 * and dispatches the value(s) to collectd if interface name matches one of
 * interfaces specified in configuration file.
 */
static void
ovs_link_table_update_cb(yajl_val jupdates)
{
  yajl_val jnew_val = NULL;
  yajl_val jupdate = NULL;
  yajl_val jrow_update = NULL;
  yajl_val jlink_name = NULL;
  yajl_val jlink_state = NULL;
  const char *link_name = NULL;
  const char *link_state = NULL;
  ovs_link_link_status_t link_status = UNKNOWN;

  /* JSON "Interface" table update example:
   * ---------------------------------
   * {"Interface":
   *  {
   *   "9adf1db2-29ca-4140-ab22-ae347a4484de":
   *    {
   *     "new":
   *      {
   *       "name":"br0",
   *       "link_state":"up"
   *      },
   *     "old":
   *      {
   *       "link_state":"down"
   *      }
   *    }
   *  }
   * }
   */
  if (!YAJL_IS_OBJECT(jupdates) || !(YAJL_GET_OBJECT(jupdates)->len > 0)) {
    ERROR(OVS_LINK_PLUGIN ": unexpected OVS DB update event received");
    return;
  }
  /* verify if this is a table event */
  jupdate = YAJL_GET_OBJECT(jupdates)->values[0];
  if (!YAJL_IS_OBJECT(jupdate)) {
    ERROR(OVS_LINK_PLUGIN ": unexpected table update event received");
    return;
  }
  /* go through all row updates  */
  for (int row_index = 0; row_index < YAJL_GET_OBJECT(jupdate)->len;
       ++row_index) {
    jrow_update = YAJL_GET_OBJECT(jupdate)->values[row_index];

    /* check row update */
    jnew_val = ovs_utils_get_value_by_key(jrow_update, "new");
    if (jnew_val == NULL) {
      ERROR(OVS_LINK_PLUGIN ": unexpected row update received");
      return;
    }
    /* get link status update */
    jlink_name = ovs_utils_get_value_by_key(jnew_val, "name");
    jlink_state = ovs_utils_get_value_by_key(jnew_val, "link_state");
    if (jlink_name && jlink_state) {
      link_name = YAJL_GET_STRING(jlink_name);
      if (link_name && ovs_link_config_iface_exists(link_name)) {
        /* convert OVS table link state to link status */
        if (YAJL_IS_STRING(jlink_state)) {
          link_state = YAJL_GET_STRING(jlink_state);
          if (strcmp(link_state, "up") == 0)
            link_status = UP;
          else if (strcmp(link_state, "down") == 0)
            link_status = DOWN;
        }
        /* update link status in cache */
        ovs_link_link_status_update(link_name, link_status);
        if (ovs_link_ctx.config.send_notification)
          /* dispatch notification */
          ovs_link_dispatch_notification(link_name, link_status);
      }
    }
  }
}

/* Process OVS DB result table callback. It handles init link status value
 * and dispatches the value(s) to collectd. The logic to handle init status
 * is same as 'ovs_link_table_update_cb'.
 */
static void
ovs_link_table_result_cb(yajl_val jresult, yajl_val jerror)
{
  (void)jerror;
  /* jerror is not used as it is the same all the time
     (rfc7047, "Monitor" section, return value) */
  ovs_link_table_update_cb(jresult);
}

/* Setup OVS DB table callback. It subscribes to OVS DB 'Interface' table
 * to receive link status event(s).
 */
static void
ovs_link_conn_initialize(ovs_db_t *pdb)
{
  int ret = 0;
  const char tb_name[] = "Interface";
  const char *columns[] = {"name", "link_state", NULL};

  /* register the update callback */
  ret = ovs_db_table_cb_register(pdb, tb_name, columns,
                                 ovs_link_table_update_cb,
                                 ovs_link_table_result_cb,
                                 OVS_DB_TABLE_CB_FLAG_MODIFY |
                                 OVS_DB_TABLE_CB_FLAG_INITIAL);
  if (ret < 0) {
    ERROR(OVS_LINK_PLUGIN ": register OVS DB update callback failed");
    return;
  }

  DEBUG(OVS_LINK_PLUGIN ": OVS DB has been initialized");
}

/* OVS DB terminate connection notification callback */
static void
ovs_link_conn_terminate()
{
  const char msg[] = "OVS DB connection has been lost";
  if (ovs_link_ctx.config.send_notification)
    ovs_link_dispatch_terminate_notification(msg);
  WARNIG(OVS_LINK_PLUGIN ": %s", msg);
  OVS_LINK_CTX_LOCK {
    /* update link status to UNKNOWN */
    for (ovs_link_interface_info_t *iface = ovs_link_ctx.ifaces; iface;
         iface = iface->next)
      ovs_link_link_status_update(iface->name, UNKNOWN);
  }
}

/* Read OVS link status plugin callback */
static int
ovs_link_plugin_read(user_data_t *ud)
{
  (void)ud;                     /* unused argument */
  OVS_LINK_CTX_LOCK {
    for (ovs_link_interface_info_t *iface = ovs_link_ctx.ifaces; iface;
         iface = iface->next)
      /* submit link status value */
      ovs_link_link_status_submit(iface->name, iface->link_status);
  }
  return (0);
}

/* Initialize OVS plugin */
static int
ovs_link_plugin_init(void)
{
  ovs_db_t *ovs_db = NULL;
  ovs_db_callback_t cb = {.post_conn_init = ovs_link_conn_initialize,
                          .post_conn_terminate = ovs_link_conn_terminate};

  /* Initialize the context mutex */
  if (pthread_mutexattr_init(&ovs_link_ctx.mutex_attr) != 0) {
    ERROR(OVS_LINK_PLUGIN ": init context mutex attribute failed");
    return (-1);
  }
  pthread_mutexattr_settype(&ovs_link_ctx.mutex_attr,
                            PTHREAD_MUTEX_RECURSIVE);
  if (pthread_mutex_init(&ovs_link_ctx.mutex, &ovs_link_ctx.mutex_attr) != 0) {
    ERROR(OVS_LINK_PLUGIN ": init context mutex failed");
    goto ovs_link_failure;
  }

  /* set default OVS DB url */
  if (ovs_link_ctx.config.ovs_db_server_url == NULL)
    if ((ovs_link_ctx.config.ovs_db_server_url =
         strdup(OVS_LINK_DEFAULT_OVS_DB_SERVER_URL)) == NULL) {
      ERROR(OVS_LINK_PLUGIN ": fail to set default OVS DB URL");
      goto ovs_link_failure;
    }
  DEBUG(OVS_LINK_PLUGIN ": OVS DB url = %s",
        ovs_link_ctx.config.ovs_db_server_url);

  /* initialize OVS DB */
  ovs_db = ovs_db_init(ovs_link_ctx.config.ovs_db_server_url, &cb);
  if (ovs_db == NULL) {
    ERROR(OVS_LINK_PLUGIN ": fail to connect to OVS DB server");
    goto ovs_link_failure;
  }

  /* store OVS DB handler */
  OVS_LINK_CTX_LOCK {
    ovs_link_ctx.ovs_db = ovs_db;
  }

  DEBUG(OVS_LINK_PLUGIN ": plugin has been initialized");
  return (0);

ovs_link_failure:
  ERROR(OVS_LINK_PLUGIN ": plugin initialize failed");
  /* release allocated memory */
  ovs_link_config_free();
  /* destroy context mutex */
  pthread_mutexattr_destroy(&ovs_link_ctx.mutex_attr);
  pthread_mutex_destroy(&ovs_link_ctx.mutex);
  return (-1);
}

/* Shutdown OVS plugin */
static int
ovs_link_plugin_shutdown(void)
{
  /* release memory allocated for config */
  ovs_link_config_free();

  /* destroy OVS DB */
  if (ovs_db_destroy(ovs_link_ctx.ovs_db))
    ERROR(OVS_LINK_PLUGIN ": OVSDB object destroy failed");

  /* destroy context mutex */
  pthread_mutexattr_destroy(&ovs_link_ctx.mutex_attr);
  pthread_mutex_destroy(&ovs_link_ctx.mutex);

  DEBUG(OVS_LINK_PLUGIN ": plugin has been destroyed");
  return (0);
}

/* Register OVS plugin callbacks */
void
module_register(void)
{
  plugin_register_complex_config(OVS_LINK_PLUGIN, ovs_link_plugin_config);
  plugin_register_init(OVS_LINK_PLUGIN, ovs_link_plugin_init);
  plugin_register_complex_read(NULL, OVS_LINK_PLUGIN, ovs_link_plugin_read,
                               0, NULL);
  plugin_register_shutdown(OVS_LINK_PLUGIN, ovs_link_plugin_shutdown);
}
