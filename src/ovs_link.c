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
#define CONFIG_LOCK for (int __i = config_lock(); __i != 0 ; \
                         __i = config_unlock())

struct interface_s {
  char *name;                   /* interface name */
  struct interface_s *next;     /* next interface name */
};
typedef struct interface_s interface_t;

struct ovs_link_config_s {
  pthread_mutex_t mutex;        /* mutex to lock the config structure */
  char *ovs_db_server_url;      /* OVS DB server URL */
  ovs_db_t *ovs_db;             /* pointer to OVS DB instance */
  interface_t *ifaces;          /* interface names */
};
typedef struct ovs_link_config_s ovs_link_config_t;

/*
 * Private variables
 */
ovs_link_config_t config = {PTHREAD_MUTEX_INITIALIZER, NULL, NULL, NULL};

/* This function is used only by "CONFIG_LOCK" defined above.
 * It always returns 1 when the config is locked.
 */
static inline int
config_lock()
{
  pthread_mutex_lock(&config.mutex);
  return (1);
}

/* This function is used only by "CONFIG_LOCK" defined above.
 * It always returns 0 when config is unlocked.
 */
static inline int
config_unlock()
{
  pthread_mutex_unlock(&config.mutex);
  return (0);
}

/* Check if given interface name exists in configuration file. It
 * returns 1 if exists otherwise 0. If no interfaces are configured,
 * 1 is returned
 */
static int
ovs_link_config_iface_exists(const char *ifname)
{
  int rc = 0;
  CONFIG_LOCK {
    if (!(rc = (config.ifaces == NULL))) {
      for (interface_t *iface = config.ifaces; iface; iface = iface->next)
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
  interface_t *del_iface = NULL;
  CONFIG_LOCK {
    sfree(config.ovs_db_server_url);
    while (config.ifaces) {
      del_iface = config.ifaces;
      config.ifaces = config.ifaces->next;
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
  interface_t *new_iface;
  char *if_name;
  char *ovs_db_url;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("OvsDbServerUrl", child->key) == 0) {
      if (cf_util_get_string(child, &ovs_db_url) < 0) {
        ERROR(OVS_LINK_PLUGIN ": parse '%s' option failed", child->key);
        goto failure;
      } else
        config.ovs_db_server_url = ovs_db_url;
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
          new_iface->next = config.ifaces;
          CONFIG_LOCK {
            config.ifaces = new_iface;
          }
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
ovs_link_dispatch_notification(const char *link_name, const char *link_state)
{
  notification_t n = {NOTIF_FAILURE, time(NULL), "", "", OVS_LINK_PLUGIN,
                      "", "", "", NULL};

  /* fill the notification data */
  if (link_state != NULL)
    n.severity = ((strcmp(link_state, "up") == 0) ?
                  NOTIF_OKAY : NOTIF_WARNING);
  else
    link_state = "UNKNOWN";

  sstrncpy(n.host, hostname_g, sizeof(n.host));
  ssnprintf(n.message, sizeof(n.message),
            "link state of \"%s\" interface has been changed to \"%s\"",
            link_name, link_state);

  /* send the notification */
  return plugin_dispatch_notification(&n);
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
        /* dispatch notification */
        ovs_link_dispatch_notification(link_name,
                                       YAJL_GET_STRING(jlink_state));
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

/* Setup OVS DB table callback. It subscribes to 'Interface' tables
 * to receive link status events.
 */
static void
ovs_link_initialize(ovs_db_t *pdb)
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

/* Set default config values (update config) if some of them aren't
 * specified in configuration file
 */
static inline int
ovs_link_config_set_default()
{
  if (!config.ovs_db_server_url)
    config.ovs_db_server_url = strdup(OVS_LINK_DEFAULT_OVS_DB_SERVER_URL);
  return (config.ovs_db_server_url == NULL);
}

/* Initialize OVS plugin */
static int
ovs_link_plugin_init(void)
{
  ovs_db_t *ovs_db = NULL;
  ovs_db_callback_t cb = {.init_cb = ovs_link_initialize};

  if (ovs_link_config_set_default()) {
    ERROR(OVS_LINK_PLUGIN ": fail to make configuration");
    ovs_link_config_free();
    return (-1);
  }

  /* initialize OVS DB */
  if ((ovs_db = ovs_db_init(config.ovs_db_server_url, &cb)) == NULL) {
    ERROR(OVS_LINK_PLUGIN ": fail to connect to OVS DB server");
    ovs_link_config_free();
    return (-1);
  }

  /* store OVSDB handler */
  CONFIG_LOCK {
    config.ovs_db = ovs_db;
  }

  DEBUG(OVS_LINK_PLUGIN ": plugin has been initialized");
  return (0);
}

/* Shutdown OVS plugin */
static int
ovs_link_plugin_shutdown(void)
{
  /* release memory allocated for config */
  ovs_link_config_free();

  /* destroy OVS DB */
  if (ovs_db_destroy(config.ovs_db))
    ERROR(OVS_LINK_PLUGIN ": OVSDB object destroy failed");

  DEBUG(OVS_LINK_PLUGIN ": plugin has been destroyed");
  return (0);
}

/* Register OVS plugin callbacks */
void
module_register(void)
{
  plugin_register_complex_config(OVS_LINK_PLUGIN, ovs_link_plugin_config);
  plugin_register_init(OVS_LINK_PLUGIN, ovs_link_plugin_init);
  plugin_register_shutdown(OVS_LINK_PLUGIN, ovs_link_plugin_shutdown);
}
