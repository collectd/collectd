/**
 * collectd - src/ovs_events.c
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

#include "collectd.h"

#include "common.h" /* auxiliary functions */
#include "utils_llist.h"

#include "utils_ovs.h" /* OVS helpers */

#define OVS_EVENTS_DEFAULT_NODE "localhost" /* default OVS DB node */
#define OVS_EVENTS_DEFAULT_SERVICE "6640"   /* default OVS DB service */
#define OVS_EVENTS_IFACE_NAME_SIZE 128
#define OVS_EVENTS_IFACE_UUID_SIZE 64
#define OVS_EVENTS_EXT_IFACE_ID_SIZE 64
#define OVS_EVENTS_EXT_VM_UUID_SIZE 64
#define OVS_EVENTS_PLUGIN "ovs_events"
#define OVS_EVENTS_LOCK(lock)                                                  \
  for (int __i = ovs_events_lock(lock); __i != 0; __i = ovs_events_unlock(lock))

/* Link status type */
enum ovs_events_link_status_e { DOWN, UP };
typedef enum ovs_events_link_status_e ovs_events_link_status_t;

/* Interface info */
struct ovs_events_iface_info_s {
  char name[OVS_EVENTS_IFACE_NAME_SIZE];           /* interface name */
  char uuid[OVS_EVENTS_IFACE_UUID_SIZE];           /* interface UUID */
  char ext_iface_id[OVS_EVENTS_EXT_IFACE_ID_SIZE]; /* external interface id */
  char ext_vm_uuid[OVS_EVENTS_EXT_VM_UUID_SIZE];   /* external VM UUID */
  ovs_events_link_status_t link_status;            /* interface link status */
};
typedef struct ovs_events_iface_info_s ovs_events_iface_info_t;

/* OVS events instance configuration */
struct ovs_events_inst_config_s {
  char *select_params;                    /* OVS DB select parameter request */
  char node[OVS_DB_ADDR_NODE_SIZE];       /* OVS DB node */
  char service[OVS_DB_ADDR_SERVICE_SIZE]; /* OVS DB service */
  char unix_path[OVS_DB_ADDR_UNIX_SIZE];  /* OVS DB unix socket path */
  _Bool dispatch_values;                  /* Dispatch values? */
  llist_t *list_iface;                    /* OVS interface list */
};
typedef struct ovs_events_inst_config_s ovs_events_inst_config_t;

/* OVS events instance context */
struct ovs_events_inst_s {
  char name[DATA_MAX_NAME_LEN];     /* Instance name */
  ovs_db_t *ovs_db;                 /* pointer to OVS DB instance */
  pthread_mutex_t mutex;            /* mutex to lock the context */
  ovs_events_inst_config_t config;  /* OVS DB instance config */
  char reg_name[DATA_MAX_NAME_LEN]; /* Register callback name */
};
typedef struct ovs_events_inst_s ovs_events_inst_t;

/* OVS events context type */
struct ovs_events_ctx_s {
  ovs_t *ovs_obj;          /* pointer to OVS object */
  _Bool send_notification; /* sent notification to collectd? */
  llist_t *list_inst;      /* List of configured OVS instances */
};
typedef struct ovs_events_ctx_s ovs_events_ctx_t;

/*
 * Private variables
 */
static ovs_events_ctx_t ovs_events_ctx = {
    .send_notification = 1}; /* send notification by default */

/* Forward declaration */
static int ovs_events_plugin_read(user_data_t *u);

/* This function is used only by "OVS_EVENTS_LOCK" define (see above).
 * It always returns 1 when context is locked.
 */
static int ovs_events_lock(pthread_mutex_t *lock) {
  pthread_mutex_lock(lock);
  return 1;
}

/* This function is used only by "OVS_EVENTS_LOCK" define (see above).
 * It always returns 0 when context is unlocked.
 */
static int ovs_events_unlock(pthread_mutex_t *lock) {
  pthread_mutex_unlock(lock);
  return 0;
}

/* Get OVS DB select parameter request based on rfc7047,
 * "Transact" & "Select" section
 */
static char *ovs_events_get_select_params(llist_t *list_iface) {
  size_t buff_size = 0;
  size_t buff_off = 0;
  char *opt_buff = NULL;
  static const char params_fmt[] = "[\"Open_vSwitch\"%s]";
  static const char option_fmt[] =
      ",{\"op\":\"select\",\"table\":\"Interface\","
      "\"where\":[[\"name\",\"==\",\"%s\"]],"
      "\"columns\":[\"link_state\",\"external_ids\","
      "\"name\",\"_uuid\"]}";
  static const char default_opt[] =
      ",{\"op\":\"select\",\"table\":\"Interface\","
      "\"where\":[],\"columns\":[\"link_state\","
      "\"external_ids\",\"name\",\"_uuid\"]}";
  /* setup OVS DB interface condition */
  for (llentry_t *le = llist_head(list_iface); le != NULL; le = le->next) {
    /* allocate new buffer (format size + ifname len is good enough) */
    buff_size += sizeof(option_fmt) + strlen(le->key);
    char *new_buff = realloc(opt_buff, buff_size);
    if (new_buff == NULL) {
      sfree(opt_buff);
      return NULL;
    }
    opt_buff = new_buff;
    int ret = ssnprintf(opt_buff + buff_off, buff_size - buff_off, option_fmt,
                        le->key);
    if (ret < 0) {
      sfree(opt_buff);
      return NULL;
    }
    buff_off += ret;
  }
  /* if no interfaces are configured, use default params */
  if (opt_buff == NULL)
    if ((opt_buff = strdup(default_opt)) == NULL)
      return NULL;

  /* allocate memory for OVS DB select params */
  size_t params_size = sizeof(params_fmt) + strlen(opt_buff);
  char *params_buff = calloc(1, params_size);
  if (params_buff == NULL) {
    sfree(opt_buff);
    return NULL;
  }

  /* create OVS DB select params */
  if (ssnprintf(params_buff, params_size, params_fmt, opt_buff) < 0)
    sfree(params_buff);

  sfree(opt_buff);
  return params_buff;
}

/* Release memory allocated for configuration data */
static void ovs_events_config_free() {
  for (llentry_t *inst_le = llist_head(ovs_events_ctx.list_inst);
       inst_le != NULL; inst_le = inst_le->next) {
    ovs_events_inst_t *inst = inst_le->value;
    /* release memory allocated for interfaces */
    for (llentry_t *iface_le = llist_head(inst->config.list_iface);
         iface_le != NULL; iface_le = iface_le->next) {
      sfree(iface_le->key);
    }
    llist_destroy(inst->config.list_iface);
    pthread_mutex_destroy(&inst->mutex);
    inst->config.list_iface = NULL;
    sfree(inst->config.select_params);
    sfree(inst_le->key);
    sfree(inst);
  }
  llist_destroy(ovs_events_ctx.list_inst);
  ovs_events_ctx.list_inst = NULL;
}

/* Post initialization of OVS DB configuration */
static int ovs_events_inst_post_init(ovs_events_inst_config_t *config) {
  if (config == NULL)
    return -1;

  /* set default configuration */
  if (config->node[0] == '\0')
    sstrncpy(config->node, OVS_EVENTS_DEFAULT_NODE, sizeof(config->node));
  if (config->service[0] == '\0')
    sstrncpy(config->service, OVS_EVENTS_DEFAULT_SERVICE,
             sizeof(config->service));

  /* generate OVS DB select condition based on list of configured interfaces */
  config->select_params = ovs_events_get_select_params(config->list_iface);
  if (config->select_params == NULL)
    return -1;

  return 0;
}

/* Parse/process "Interfaces" configuration option. Returns 0 if success
 * otherwise -1 (error)
 */
static int ovs_events_config_get_interfaces(const oconfig_item_t *ci,
                                            llist_t *list_iface) {
  for (int j = 0; j < ci->values_num; j++) {
    /* check interface name type */
    if (ci->values[j].type != OCONFIG_TYPE_STRING) {
      ERROR(OVS_EVENTS_PLUGIN ": given interface name is not a string [idx=%d]",
            j);
      return -1;
    }
    /* add new interface */
    llentry_t *new_le =
        llentry_create(strdup(ci->values[j].value.string), NULL);
    if (new_le == NULL) {
      ERROR(OVS_EVENTS_PLUGIN ": llentry_create(): error");
      return -1;
    }
    llist_append(list_iface, new_le);
    DEBUG(OVS_EVENTS_PLUGIN ": found monitored interface \"%s\"", new_le->key);
  }
  return 0;
}

/* Parse plugin OVS instance group configuration */
static int ovs_events_get_instance_config(oconfig_item_t *ci,
                                          ovs_events_inst_config_t *config) {
  ovs_events_inst_config_t inst_conf = {0};

  /* sanity check */
  if (ci == NULL || config == NULL)
    return -1;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Address", child->key) == 0) {
      if (cf_util_get_string_buffer(child, inst_conf.node,
                                    sizeof(inst_conf.node)) != 0)
        return -1;
    } else if (strcasecmp("Port", child->key) == 0) {
      char *service = NULL;
      if (cf_util_get_service(child, &service) != 0) {
        return -1;
      }
      strncpy(inst_conf.service, service, sizeof(inst_conf.service));
      sfree(service);
    } else if (strcasecmp("Socket", child->key) == 0) {
      if (cf_util_get_string_buffer(child, inst_conf.unix_path,
                                    sizeof(inst_conf.unix_path)) != 0) {
        return -1;
      }
    } else if (strcasecmp("DispatchValues", child->key) == 0) {
      if (cf_util_get_boolean(child, &inst_conf.dispatch_values) != 0) {
        return -1;
      }
    } else if (strcasecmp("Interfaces", child->key) == 0) {
      inst_conf.list_iface = llist_create();
      if (ovs_events_config_get_interfaces(child, inst_conf.list_iface) != 0) {
        llist_destroy(inst_conf.list_iface);
        inst_conf.list_iface = NULL;
        return -1;
      }
    } else {
      ERROR(OVS_EVENTS_PLUGIN ": option '%s' is not allowed here", child->key);
      return -1;
    }
  }
  /* set default configuration */
  if (ovs_events_inst_post_init(&inst_conf) < 0) {
    llist_destroy(inst_conf.list_iface);
    inst_conf.list_iface = NULL;
    return -1;
  }
  *config = inst_conf;
  return 0;
}

/* Create a new instance and add it to the plugin context */
static ovs_events_inst_t *ovs_events_instance_add(const char *name) {
  /* check for duplicate instance */
  if (llist_search(ovs_events_ctx.list_inst, name) != NULL)
    return NULL;

  /* create new OVS DB instances */
  ovs_events_inst_t *inst = calloc(1, sizeof(*inst));
  if (inst == NULL)
    return NULL;

  /* add new OVS DB instance to the list */
  llentry_t *new_le = llentry_create(strdup(name), inst);
  if (new_le == NULL) {
    sfree(inst);
    return NULL;
  }
  llist_append(ovs_events_ctx.list_inst, new_le);
  return inst;
}

/* Add default OVS DB instance configuration */
static int ovs_events_inst_add_default() {

  /* create new instance */
  ovs_events_inst_t *inst = ovs_events_instance_add(hostname_g);
  if (inst == NULL)
    return -1;

  /* set default configuration */
  if (ovs_events_inst_post_init(&inst->config) < 0)
    return -1;

  /* set instance and read register callback names */
  ssnprintf(inst->reg_name, sizeof(inst->reg_name), "%s/%s", OVS_EVENTS_PLUGIN,
            hostname_g);
  strncpy(inst->name, hostname_g, sizeof(inst->name));
  return 0;
}

/* Pre-init configuration */
static int ovs_events_config_pre_init() {
  if (ovs_events_ctx.list_inst != NULL)
    /* already initialized */
    return 0;

  /* create OVS DB instances list */
  if ((ovs_events_ctx.list_inst = llist_create()) == NULL) {
    ERROR(OVS_EVENTS_PLUGIN ": create OVS DB instances list failed");
    return -1;
  }
  return 0;
}

/* Parse plugin configuration file and store the config
 * in allocated memory. Returns negative value in case of error.
 */
static int ovs_events_plugin_config(oconfig_item_t *ci) {
  /* pre-init configuration */
  if (ovs_events_config_pre_init() != 0) {
    ERROR(OVS_EVENTS_PLUGIN ": pre-init configuration failed");
    return -1;
  }
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("SendNotification", child->key) == 0) {
      if (cf_util_get_boolean(child, &ovs_events_ctx.send_notification) != 0) {
        ovs_events_config_free();
        return -1;
      }
    } else if (strcasecmp("Instance", child->key) == 0) {
      char inst_name[DATA_MAX_NAME_LEN];
      /* get the instance name */
      if (cf_util_get_string_buffer(child, inst_name, sizeof(inst_name)) != 0) {
        ERROR(OVS_EVENTS_PLUGIN ": parse OVS DB instance name failed");
        ovs_events_config_free();
        return -1;
      }
      /* create new instance */
      ovs_events_inst_t *inst = ovs_events_instance_add(inst_name);
      if (inst == NULL) {
        ERROR(OVS_EVENTS_PLUGIN ": %s: Create new OVS DB instance failed "
                                "or the instance already exists",
              inst_name);
        ovs_events_config_free();
        return -1;
      }
      /* parse OVS instance configuration */
      if (ovs_events_get_instance_config(child, &inst->config)) {
        ERROR(OVS_EVENTS_PLUGIN ": parse OVS DB instance failed");
        ovs_events_config_free();
        return -1;
      }
      /* set instance and read register callback names */
      ssnprintf(inst->reg_name, sizeof(inst->reg_name), "%s/%s",
                OVS_EVENTS_PLUGIN, inst_name);
      strncpy(inst->name, inst_name, sizeof(inst->name));
    } else {
      ERROR(OVS_EVENTS_PLUGIN ": option '%s' is not allowed here", child->key);
      ovs_events_config_free();
      return -1;
    }
  }
  /* Check and warn about invalid configuration */
  for (llentry_t *le = llist_head(ovs_events_ctx.list_inst); le != NULL;
       le = le->next) {
    ovs_events_inst_t *inst = le->value;
    if (!ovs_events_ctx.send_notification && !inst->config.dispatch_values) {
      WARNING(OVS_EVENTS_PLUGIN
              ": %s: send notification and dispatch values "
              "options are disabled. No information will be dispatched by "
              "the OVS DB instance. Please check your configuration",
              inst->name);
    }
  }
  return 0;
}

/* Dispatch OVS interface link status event to collectd */
static void ovs_events_dispatch_notification(const char *hostname,
                                 const ovs_events_iface_info_t *ifinfo) {
  const char *msg_link_status = NULL;
  notification_t n = {
      NOTIF_FAILURE, cdtime(), "", "", OVS_EVENTS_PLUGIN, "", "", "", NULL};

  /* convert link status to message string */
  switch (ifinfo->link_status) {
  case UP:
    msg_link_status = "UP";
    n.severity = NOTIF_OKAY;
    break;
  case DOWN:
    msg_link_status = "DOWN";
    n.severity = NOTIF_WARNING;
    break;
  default:
    ERROR(OVS_EVENTS_PLUGIN ": unknown interface link status");
    return;
  }

  /* add interface metadata to the notification */
  if (plugin_notification_meta_add_string(&n, "uuid", ifinfo->uuid) < 0) {
    ERROR(OVS_EVENTS_PLUGIN ": add interface uuid meta data failed");
    return;
  }

  if (strlen(ifinfo->ext_vm_uuid) > 0) {
    if (plugin_notification_meta_add_string(&n, "vm-uuid",
                                            ifinfo->ext_vm_uuid) < 0) {
      ERROR(OVS_EVENTS_PLUGIN ": add interface vm-uuid meta data failed");
      return;
    }
  }

  if (strlen(ifinfo->ext_iface_id) > 0) {
    if (plugin_notification_meta_add_string(&n, "iface-id",
                                            ifinfo->ext_iface_id) < 0) {
      ERROR(OVS_EVENTS_PLUGIN ": add interface iface-id meta data failed");
      return;
    }
  }

  /* fill the notification data */
  ssnprintf(n.message, sizeof(n.message),
            "link state of \"%s\" interface has been changed to \"%s\"",
            ifinfo->name, msg_link_status);
  sstrncpy(n.host, hostname, sizeof(n.host));
  sstrncpy(n.plugin_instance, ifinfo->name, sizeof(n.plugin_instance));
  sstrncpy(n.type, "gauge", sizeof(n.type));
  sstrncpy(n.type_instance, "link_status", sizeof(n.type_instance));
  plugin_dispatch_notification(&n);
}

/* Dispatch OVS interface link status value to collectd */
static void ovs_events_link_status_submit(const char *hostname,
                              const ovs_events_iface_info_t *ifinfo) {
  value_list_t vl = VALUE_LIST_INIT;
  meta_data_t *meta = NULL;

  /* add interface metadata to the submit value */
  if ((meta = meta_data_create()) != NULL) {
    if (meta_data_add_string(meta, "uuid", ifinfo->uuid) < 0)
      ERROR(OVS_EVENTS_PLUGIN ": add interface uuid meta data failed");

    if (strlen(ifinfo->ext_vm_uuid) > 0)
      if (meta_data_add_string(meta, "vm-uuid", ifinfo->ext_vm_uuid) < 0)
        ERROR(OVS_EVENTS_PLUGIN ": add interface vm-uuid meta data failed");

    if (strlen(ifinfo->ext_iface_id) > 0)
      if (meta_data_add_string(meta, "iface-id", ifinfo->ext_iface_id) < 0)
        ERROR(OVS_EVENTS_PLUGIN ": add interface iface-id meta data failed");
    vl.meta = meta;
  } else
    ERROR(OVS_EVENTS_PLUGIN ": create metadata failed");

  vl.time = cdtime();
  vl.values = &(value_t){.gauge = (gauge_t)ifinfo->link_status};
  vl.values_len = 1;
  sstrncpy(vl.host, hostname, sizeof(vl.host));
  sstrncpy(vl.plugin, OVS_EVENTS_PLUGIN, sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, ifinfo->name, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, "gauge", sizeof(vl.type));
  sstrncpy(vl.type_instance, "link_status", sizeof(vl.type_instance));
  plugin_dispatch_values(&vl);
  meta_data_destroy(meta);
}

/* Dispatch OVS DB terminate connection event to collectd */
static void ovs_events_dispatch_terminate_notification(const char *hostname,
                                                       const char *msg) {
  notification_t n = {
      NOTIF_FAILURE, cdtime(), "", "", OVS_EVENTS_PLUGIN, "", "", "", NULL};
  sstrncpy(n.message, msg, sizeof(n.message));
  sstrncpy(n.host, hostname, sizeof(n.host));
  plugin_dispatch_notification(&n);
}

/* Get OVS DB interface information and stores it into
 * ovs_events_iface_info_t structure */
static int ovs_events_get_iface_info(yajl_val jobject,
                                     ovs_events_iface_info_t *ifinfo) {
  yajl_val jexternal_ids = NULL;
  yajl_val jvalue = NULL;
  yajl_val juuid = NULL;
  const char *state = NULL;

  /* check YAJL type */
  if (!YAJL_IS_OBJECT(jobject))
    return -1;

  /* zero the interface info structure */
  memset(ifinfo, 0, sizeof(*ifinfo));

  /* try to find external_ids, name and link_state fields */
  jexternal_ids = ovs_utils_get_value_by_key(jobject, "external_ids");
  if (jexternal_ids == NULL || ifinfo == NULL)
    return -1;

  /* get iface-id from external_ids field */
  jvalue = ovs_utils_get_map_value(jexternal_ids, "iface-id");
  if (jvalue != NULL && YAJL_IS_STRING(jvalue))
    sstrncpy(ifinfo->ext_iface_id, YAJL_GET_STRING(jvalue),
             sizeof(ifinfo->ext_iface_id));

  /* get vm-uuid from external_ids field */
  jvalue = ovs_utils_get_map_value(jexternal_ids, "vm-uuid");
  if (jvalue != NULL && YAJL_IS_STRING(jvalue))
    sstrncpy(ifinfo->ext_vm_uuid, YAJL_GET_STRING(jvalue),
             sizeof(ifinfo->ext_vm_uuid));

  /* get interface uuid */
  jvalue = ovs_utils_get_value_by_key(jobject, "_uuid");
  if (jvalue == NULL || !YAJL_IS_ARRAY(jvalue) ||
      YAJL_GET_ARRAY(jvalue)->len != 2)
    return -1;
  juuid = YAJL_GET_ARRAY(jvalue)->values[1];
  if (juuid == NULL || !YAJL_IS_STRING(juuid))
    return -1;
  sstrncpy(ifinfo->uuid, YAJL_GET_STRING(juuid), sizeof(ifinfo->uuid));

  /* get interface name */
  jvalue = ovs_utils_get_value_by_key(jobject, "name");
  if (jvalue == NULL || !YAJL_IS_STRING(jvalue))
    return -1;
  sstrncpy(ifinfo->name, YAJL_GET_STRING(jvalue), sizeof(ifinfo->name));

  /* get OVS DB interface link status */
  jvalue = ovs_utils_get_value_by_key(jobject, "link_state");
  if (jvalue != NULL && ((state = YAJL_GET_STRING(jvalue)) != NULL)) {
    /* convert OVS table link state to link status */
    if (strcmp(state, "up") == 0)
      ifinfo->link_status = UP;
    else if (strcmp(state, "down") == 0)
      ifinfo->link_status = DOWN;
  }
  return 0;
}

/* Process OVS DB update table event. It handles link status update event(s)
 * and dispatches the value(s) to collectd if interface name matches one of
 * interfaces specified in configuration file.
 */
static void ovs_events_table_update_cb(yajl_val jupdates, void *user_data) {
  yajl_val jnew_val = NULL;
  yajl_val jupdate = NULL;
  yajl_val jrow_update = NULL;
  ovs_events_iface_info_t ifinfo;

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
    ERROR(OVS_EVENTS_PLUGIN ": unexpected OVS DB update event received");
    return;
  }
  /* verify if this is a table event */
  jupdate = YAJL_GET_OBJECT(jupdates)->values[0];
  if (!YAJL_IS_OBJECT(jupdate)) {
    ERROR(OVS_EVENTS_PLUGIN ": unexpected table update event received");
    return;
  }
  /* go through all row updates  */
  for (size_t row_index = 0; row_index < YAJL_GET_OBJECT(jupdate)->len;
       ++row_index) {
    jrow_update = YAJL_GET_OBJECT(jupdate)->values[row_index];

    /* check row update */
    jnew_val = ovs_utils_get_value_by_key(jrow_update, "new");
    if (jnew_val == NULL) {
      ERROR(OVS_EVENTS_PLUGIN ": unexpected row update received");
      return;
    }
    /* get OVS DB interface information */
    if (ovs_events_get_iface_info(jnew_val, &ifinfo) < 0) {
      ERROR(OVS_EVENTS_PLUGIN
            " :unexpected interface information data received");
      return;
    }
    ovs_events_inst_t *inst = user_data;
    if (llist_head(inst->config.list_iface) == NULL ||
        llist_search(inst->config.list_iface, ifinfo.name) != NULL) {
      DEBUG("name=%s, uuid=%s, ext_iface_id=%s, ext_vm_uuid=%s", ifinfo.name,
            ifinfo.uuid, ifinfo.ext_iface_id, ifinfo.ext_vm_uuid);
      /* dispatch notification */
      ovs_events_dispatch_notification(inst->name, &ifinfo);
    }
  }
}

/* OVS DB reply callback. It parses reply, receives
 * interface information and dispatches the info to
 * collectd
 */
static void ovs_events_poll_result_cb(yajl_val jresult, yajl_val jerror,
                                      void *user_data) {
  yajl_val *jvalues = NULL;
  yajl_val jvalue = NULL;
  ovs_events_iface_info_t ifinfo;

  if (!YAJL_IS_NULL(jerror)) {
    ERROR(OVS_EVENTS_PLUGIN "error received by OVS DB server");
    return;
  }

  /* result should be an array */
  if (!YAJL_IS_ARRAY(jresult)) {
    ERROR(OVS_EVENTS_PLUGIN "invalid data (array is expected)");
    return;
  }

  /* go through all rows and get interface info */
  jvalues = YAJL_GET_ARRAY(jresult)->values;
  for (size_t i = 0; i < YAJL_GET_ARRAY(jresult)->len; i++) {
    jvalue = ovs_utils_get_value_by_key(jvalues[i], "rows");
    if (jvalue == NULL || !YAJL_IS_ARRAY(jvalue)) {
      ERROR(OVS_EVENTS_PLUGIN "invalid data (array of rows is expected)");
      return;
    }
    /* get interfaces info */
    for (size_t j = 0; j < YAJL_GET_ARRAY(jvalue)->len; j++) {
      if (ovs_events_get_iface_info(YAJL_GET_ARRAY(jvalue)->values[j],
                                    &ifinfo) < 0) {
        ERROR(OVS_EVENTS_PLUGIN
              "unexpected interface information data received");
        return;
      }
      DEBUG("name=%s, uuid=%s, ext_iface_id=%s, ext_vm_uuid=%s", ifinfo.name,
            ifinfo.uuid, ifinfo.ext_iface_id, ifinfo.ext_vm_uuid);
      ovs_events_inst_t *inst = user_data;
      ovs_events_link_status_submit(inst->name, &ifinfo);
    }
  }
}

/* Setup OVS DB table callback. It subscribes to OVS DB 'Interface' table
 * to receive link status event(s).
 */
static void ovs_events_conn_initialize(ovs_db_t *pdb, void *user_data) {
  const char tb_name[] = "Interface";
  const char *columns[] = {"_uuid", "external_ids", "name", "link_state", NULL};

  /* register update link status event if needed */
  if (ovs_events_ctx.send_notification) {
    int ret = ovs_db_table_cb_register(pdb, tb_name, columns,
                                       ovs_events_table_update_cb, NULL,
                                       OVS_DB_TABLE_CB_FLAG_MODIFY);
    if (ret < 0) {
      ERROR(OVS_EVENTS_PLUGIN ": register OVS DB update callback failed");
      return;
    }
  }
  ovs_events_inst_t *inst = user_data;
  OVS_EVENTS_LOCK(&inst->mutex) { inst->ovs_db = pdb; }
  DEBUG(OVS_EVENTS_PLUGIN ": %s: OVS DB connection has been initialized",
        inst->name);
}

/* OVS DB terminate connection notification callback */
static void ovs_events_conn_terminate(ovs_db_t *pdb, void *user_data) {
  char *msg = "OVS DB connection has been lost";
  ovs_events_inst_t *inst = user_data;
  if (ovs_events_ctx.send_notification)
    ovs_events_dispatch_terminate_notification(inst->name, msg);
  WARNING(OVS_EVENTS_PLUGIN ": %s: %s", inst->name, msg);
  OVS_EVENTS_LOCK(&inst->mutex) { inst->ovs_db = NULL; }
}

/* Read OVS DB interface link status callback */
static int ovs_events_plugin_read(user_data_t *u) {
  int status = 0;
  ovs_events_inst_t *inst = u->data;
  OVS_EVENTS_LOCK(&inst->mutex) {
    if (inst->ovs_db) {
      if (ovs_db_send_request(inst->ovs_db, "transact",
                              inst->config.select_params,
                              ovs_events_poll_result_cb) < 0) {
        ERROR(OVS_EVENTS_PLUGIN ": get interface info failed");
        status = -1;
      }
    }
  }
  return status;
}

/* Initialize OVS plugin */
static int ovs_events_plugin_init(void) {
  size_t inst_num = llist_size(ovs_events_ctx.list_inst);
  ovs_db_inst_t insts[inst_num ? inst_num : 1];

  /* pre-init configuration */
  if (ovs_events_config_pre_init() < 0) {
    ERROR(OVS_EVENTS_PLUGIN ": pre-init configuration failed");
    return -1;
  }
  /* if no instance configuration is provided, add default one */
  if (!inst_num) {
    DEBUG("Adding default OVS DB configuration");
    if (ovs_events_inst_add_default() < 0) {
      ERROR(OVS_EVENTS_PLUGIN
            ": add default OVS DB instance configuration failed");
      ovs_events_config_free();
      return -1;
    }
  }
  /* fill the OVS DB information */
  size_t index = 0;
  for (llentry_t *le = llist_head(ovs_events_ctx.list_inst); le != NULL;
       le = le->next) {
    ovs_events_inst_t *inst = le->value;
    DEBUG(OVS_EVENTS_PLUGIN ": instance=%s, address=%s, service=%s, unix=%s",
          le->key, inst->config.node, inst->config.service,
          inst->config.unix_path);
    /* init OVS DB instance context */
    if (pthread_mutex_init(&inst->mutex, NULL) != 0) {
      ERROR(OVS_EVENTS_PLUGIN ": init OVS DB instance mutex failed");
      ovs_events_config_free();
      return -1;
    }
    inst->ovs_db = NULL;
    /* register read callback for the instance */
    if (inst->config.dispatch_values) {
      plugin_register_complex_read(
          NULL, inst->reg_name, ovs_events_plugin_read, 0,
          &(user_data_t){.data = inst, .free_func = NULL});
    }
    /* prepare list of OVS DB instances */
    strncpy(insts[index].unix_path, inst->config.unix_path,
            sizeof(inst->config.unix_path));
    strncpy(insts[index].service, inst->config.service,
            sizeof(inst->config.service));
    strncpy(insts[index].node, inst->config.node, sizeof(inst->config.node));

    insts[index].cb.post_conn_init = ovs_events_conn_initialize;
    insts[index].cb.post_conn_terminate = ovs_events_conn_terminate;
    insts[index].user_data.free_func = NULL;
    insts[index].user_data.data = inst;
    index++;
  }

  /* initialize OVS DB */
  ovs_t *ovs_obj = ovs_init(insts, STATIC_ARRAY_SIZE(insts));
  if (ovs_obj == NULL) {
    ERROR(OVS_EVENTS_PLUGIN ": init OVS object failed");
    ovs_events_config_free();
    return -1;
  }

  DEBUG(OVS_EVENTS_PLUGIN ": plugin has been initialized");
  ovs_events_ctx.ovs_obj = ovs_obj;
  return 0;
}

/* Shutdown OVS plugin */
static int ovs_events_plugin_shutdown(void) {
  /* destroy OVS DB */
  if (ovs_destroy(ovs_events_ctx.ovs_obj))
    ERROR(OVS_EVENTS_PLUGIN ": OVS object destroy failed");

  /* release memory allocated for config */
  ovs_events_config_free();

  DEBUG(OVS_EVENTS_PLUGIN ": plugin has been destroyed");
  ovs_events_ctx.ovs_obj = NULL;
  return 0;
}

/* Register OVS plugin callbacks */
void module_register(void) {
  plugin_register_complex_config(OVS_EVENTS_PLUGIN, ovs_events_plugin_config);
  plugin_register_init(OVS_EVENTS_PLUGIN, ovs_events_plugin_init);
  plugin_register_shutdown(OVS_EVENTS_PLUGIN, ovs_events_plugin_shutdown);
}
