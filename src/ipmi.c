/**
 * collectd - src/ipmi.c
 * Copyright (C) 2008-2009  Florian octo Forster
 * Copyright (C) 2008       Peter Holik
 * Copyright (C) 2009       Bruno Prémont
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 *   Peter Holik <peter at holik.at>
 *   Bruno Prémont <bonbons at linux-vserver.org>
 *   Pavel Rochnyak <pavel2000 ngs.ru>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#include <OpenIPMI/ipmiif.h>
#include <OpenIPMI/ipmi_err.h>
#include <OpenIPMI/ipmi_posix.h>
#include <OpenIPMI/ipmi_conn.h>
#include <OpenIPMI/ipmi_smi.h>
#include <OpenIPMI/ipmi_lan.h>
#include <OpenIPMI/ipmi_auth.h>

/*
 * Private data types
 */
struct c_ipmi_sensor_list_s;
typedef struct c_ipmi_sensor_list_s c_ipmi_sensor_list_t;

struct c_ipmi_instance_s
{
  char *name;
  ignorelist_t *ignorelist;
  _Bool notify_add;
  _Bool notify_remove;
  _Bool notify_notpresent;

  char *host;
  char *connaddr;
  char *username;
  char *password;
  unsigned int authtype;

  pthread_mutex_t sensor_list_lock;
  c_ipmi_sensor_list_t *sensor_list;

  _Bool active;
  pthread_t thread_id;
  int init_in_progress;

  struct c_ipmi_instance_s *next;
};
typedef struct c_ipmi_instance_s c_ipmi_instance_t;

struct c_ipmi_sensor_list_s
{
  ipmi_sensor_id_t sensor_id;
  char sensor_name[DATA_MAX_NAME_LEN];
  char sensor_type[DATA_MAX_NAME_LEN];
  int sensor_not_present;
  c_ipmi_sensor_list_t *next;
  c_ipmi_instance_t *instance;
  unsigned int use;
};

/*
 * Module global variables
 */
static os_handler_t *os_handler;
static c_ipmi_instance_t *instances = NULL;

/*
 * Misc private functions
 */
static void c_ipmi_error (const char *func, int status)
{
  char errbuf[4096] = { 0 };

  if (IPMI_IS_OS_ERR (status)
    || IPMI_IS_RMCPP_ERR (status)
    || IPMI_IS_IPMI_ERR(status))
  {
    ipmi_get_error_string (status, errbuf, sizeof (errbuf));
  }

  if (errbuf[0] == 0)
  {
    ssnprintf (errbuf, sizeof (errbuf), "Unknown error %#x", status);
  }
  errbuf[sizeof (errbuf) - 1] = 0;

  ERROR ("ipmi plugin: %s failed: %s", func, errbuf);
} /* void c_ipmi_error */

static void c_ipmi_log (os_handler_t *handler, const char *format, enum ipmi_log_type_e log_type, va_list ap)
{
  char msg[1024];

  vsnprintf(msg, sizeof(msg), format, ap);

  switch (log_type)
  {
          case IPMI_LOG_INFO         : INFO("ipmi plugin: %s", msg); break;
          case IPMI_LOG_WARNING      : NOTICE("ipmi plugin: %s", msg); break;
          case IPMI_LOG_SEVERE       : WARNING("ipmi plugin: %s", msg); break;
          case IPMI_LOG_FATAL        : ERROR("ipmi plugin: %s", msg); break;
          case IPMI_LOG_ERR_INFO     : ERROR("ipmi plugin: %s", msg); break;
#if COLLECT_DEBUG
          case IPMI_LOG_DEBUG_START  :
          case IPMI_LOG_DEBUG        :
            fprintf (stderr, "ipmi plugin: %s\n", msg);
            break;
          case IPMI_LOG_DEBUG_CONT   :
          case IPMI_LOG_DEBUG_END    :
            fprintf (stderr, "%s\n", msg);
            break;
#endif
  }
} /* void c_ipmi_log */

/*
 * Sensor handlers
 */
/* Prototype for sensor_list_remove, so sensor_read_handler can call it. */
static int sensor_list_remove (c_ipmi_instance_t *st, ipmi_sensor_t *sensor);

static void sensor_read_handler (ipmi_sensor_t *sensor,
    int err,
    enum ipmi_value_present_e value_present,
    unsigned int __attribute__((unused)) raw_value,
    double value,
    ipmi_states_t *states,
    void *user_data)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  c_ipmi_sensor_list_t *list_item = (c_ipmi_sensor_list_t *)user_data;
  c_ipmi_instance_t *st = list_item->instance;

  list_item->use--;

  if (err != 0)
  {
    if (IPMI_IS_IPMI_ERR(err) && IPMI_GET_IPMI_ERR(err) == IPMI_NOT_PRESENT_CC)
    {
      if (list_item->sensor_not_present == 0)
      {
        list_item->sensor_not_present = 1;

        INFO ("ipmi plugin: sensor_read_handler: sensor `%s` of `%s` "
            "not present.", list_item->sensor_name, st->name);

        if (st->notify_notpresent)
        {
          notification_t n = { NOTIF_WARNING, cdtime (), "", "", "ipmi",
            "", "", "", NULL };

          sstrncpy (n.host, (st->host != NULL) ? st->host : hostname_g,
              sizeof (n.host));
          sstrncpy (n.type_instance, list_item->sensor_name,
              sizeof (n.type_instance));
          sstrncpy (n.type, list_item->sensor_type, sizeof (n.type));
          ssnprintf (n.message, sizeof (n.message),
              "sensor %s not present", list_item->sensor_name);

          plugin_dispatch_notification (&n);
        }
      }
    }
    else if (IPMI_IS_IPMI_ERR(err) && IPMI_GET_IPMI_ERR(err) == IPMI_NOT_SUPPORTED_IN_PRESENT_STATE_CC)
    {
      INFO ("ipmi plugin: sensor_read_handler: Sensor `%s` of `%s` not ready.",
          list_item->sensor_name, st->name);
    }
    else if (IPMI_IS_IPMI_ERR(err) && IPMI_GET_IPMI_ERR(err) == IPMI_TIMEOUT_CC)
    {
      INFO ("ipmi plugin: sensor_read_handler: Sensor `%s` of `%s` timed out.",
          list_item->sensor_name, st->name);
    }
    else
    {
      char errbuf[128] = { 0 };
      ipmi_get_error_string (err, errbuf, sizeof (errbuf) - 1);

      if (IPMI_IS_IPMI_ERR(err))
        INFO ("ipmi plugin: sensor_read_handler: Sensor `%s` of `%s` failed: "
            "%s.",
            list_item->sensor_name, st->name, errbuf);
      else if (IPMI_IS_OS_ERR(err))
        INFO ("ipmi plugin: sensor_read_handler: Sensor `%s` of `%s` failed: "
            "%s (%#x).",
            list_item->sensor_name, st->name, errbuf, IPMI_GET_OS_ERR(err));
      else if (IPMI_IS_RMCPP_ERR(err))
        INFO ("ipmi plugin: sensor_read_handler: Sensor `%s` of `%s` failed: "
            "%s.",
            list_item->sensor_name, st->name, errbuf);
      else if (IPMI_IS_SOL_ERR(err))
        INFO ("ipmi plugin: sensor_read_handler: Sensor `%s` of `%s` failed: "
            "%s (%#x).",
            list_item->sensor_name, st->name, errbuf, IPMI_GET_SOL_ERR(err));
      else
        INFO ("ipmi plugin: sensor_read_handler: Sensor `%s` of `%s` failed "
            "with error %#x. of class %#x",
            list_item->sensor_name, st->name, err & 0xff, err & 0xffffff00);
    }
    return;
  }
  else if (list_item->sensor_not_present == 1)
  {
    list_item->sensor_not_present = 0;

    INFO ("ipmi plugin: sensor_read_handler: sensor `%s` of `%s` present.",
        list_item->sensor_name, st->name);

    if (st->notify_notpresent)
    {
      notification_t n = { NOTIF_OKAY, cdtime (), "", "", "ipmi",
        "", "", "", NULL };

      sstrncpy (n.host, (st->host != NULL) ? st->host : hostname_g, sizeof (n.host));
      sstrncpy (n.type_instance, list_item->sensor_name,
          sizeof (n.type_instance));
      sstrncpy (n.type, list_item->sensor_type, sizeof (n.type));
      ssnprintf (n.message, sizeof (n.message),
          "sensor %s present", list_item->sensor_name);

      plugin_dispatch_notification (&n);
    }
  }

  if (value_present != IPMI_BOTH_VALUES_PRESENT)
  {
    INFO ("ipmi plugin: sensor_read_handler: Removing sensor `%s` of `%s`, "
        "because it provides %s. If you need this sensor, "
        "please file a bug report.",
        list_item->sensor_name, st->name,
        (value_present == IPMI_RAW_VALUE_PRESENT)
        ? "only the raw value"
        : "no value");
    sensor_list_remove (st, sensor);
    return;
  }

  if (!ipmi_is_sensor_scanning_enabled(states))
  {
    DEBUG ("ipmi plugin: sensor_read_handler: Skipping sensor `%s` of `%s`, "
           "it is in 'scanning disabled' state.",
          list_item->sensor_name, st->name);
    return;
  }

  if (ipmi_is_initial_update_in_progress(states))
  {
    DEBUG ("ipmi plugin: sensor_read_handler: Skipping sensor `%s` of `%s`, "
           "it is in 'initial update in progress' state.",
          list_item->sensor_name, st->name);
    return;
  }

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;

  sstrncpy (vl.host, (st->host != NULL) ? st->host : hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "ipmi", sizeof (vl.plugin));
  sstrncpy (vl.type, list_item->sensor_type, sizeof (vl.type));
  sstrncpy (vl.type_instance, list_item->sensor_name, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void sensor_read_handler */

static int sensor_list_add (c_ipmi_instance_t *st, ipmi_sensor_t *sensor)
{
  ipmi_sensor_id_t sensor_id;
  c_ipmi_sensor_list_t *list_item;
  c_ipmi_sensor_list_t *list_prev;

  char buffer[DATA_MAX_NAME_LEN] = { 0 };
  const char *entity_id_string;
  char sensor_name[DATA_MAX_NAME_LEN];
  char *sensor_name_ptr;
  int sensor_type;
  const char *type;
  ipmi_entity_t *ent = ipmi_sensor_get_entity(sensor);

  sensor_id = ipmi_sensor_convert_to_id (sensor);

  ipmi_sensor_get_name (sensor, buffer, sizeof (buffer));
  buffer[sizeof (buffer) - 1] = 0;

  entity_id_string = ipmi_entity_get_entity_id_string (ent);

  if (entity_id_string == NULL)
    sstrncpy (sensor_name, buffer, sizeof (sensor_name));
  else
    ssnprintf (sensor_name, sizeof (sensor_name),
        "%s %s", buffer, entity_id_string);

  sstrncpy (buffer, sensor_name, sizeof (buffer));
  sensor_name_ptr = strstr (buffer, ").");
  if (sensor_name_ptr != NULL)
  {
    /* If name is something like "foo (123).bar",
     * change that to "bar (123)".
     * Both, sensor_name_ptr and sensor_id_ptr point to memory within the
     * `buffer' array, which holds a copy of the current `sensor_name'. */
    char *sensor_id_ptr;

    /* `sensor_name_ptr' points to ").bar". */
    sensor_name_ptr[1] = 0;
    /* `buffer' holds "foo (123)\0bar\0". */
    sensor_name_ptr += 2;
    /* `sensor_name_ptr' now points to "bar". */

    sensor_id_ptr = strstr (buffer, "(");
    if (sensor_id_ptr != NULL)
    {
      /* `sensor_id_ptr' now points to "(123)". */
      ssnprintf (sensor_name, sizeof (sensor_name),
          "%s %s", sensor_name_ptr, sensor_id_ptr);
    }
    /* else: don't touch sensor_name. */
  }
  sensor_name_ptr = sensor_name;

  DEBUG ("ipmi plugin: sensor_list_add: Found sensor `%s` of `%s`,"
         " Type: %#x"
         " Event reading type: %#x"
         " Direction: %#x"
         " Event support: %#x",
         sensor_name_ptr, st->name,
         ipmi_sensor_get_sensor_type (sensor),
         ipmi_sensor_get_event_reading_type(sensor),
         ipmi_sensor_get_sensor_direction(sensor),
         ipmi_sensor_get_event_support(sensor)
        );

  /* Both `ignorelist' and `sensor_name_ptr' may be NULL. */
  if (ignorelist_match (st->ignorelist, sensor_name_ptr) != 0)
    return (0);

  /* FIXME: Use rate unit or base unit to scale the value */

  sensor_type = ipmi_sensor_get_sensor_type (sensor);

  /*
   * ipmitool/lib/ipmi_sdr.c sdr_sensor_has_analog_reading() has a notice
   * about 'Threshold sensors' and 'analog readings'. Discrete sensor may
   * have analog data, but discrete sensors support is not implemented
   * in Collectd yet.
   *
   * ipmi_sensor_id_get_reading() supports only 'Threshold' sensors.
   * See lib/sensor.c:4842, stand_ipmi_sensor_get_reading() for details.
  */
  if (!ipmi_sensor_get_is_readable(sensor))
  {
    INFO ("ipmi plugin: sensor_list_add: Ignore sensor `%s` of `%s`, "
        "because it don't readable! Its type: (%#x, %s). ",
        sensor_name_ptr, st->name, sensor_type,
        ipmi_sensor_get_sensor_type_string (sensor));
    return (-1);
  }

  if (ipmi_sensor_get_event_reading_type(sensor) != IPMI_EVENT_READING_TYPE_THRESHOLD)
  {
    INFO ("ipmi plugin: sensor_list_add: Ignore sensor `%s` of `%s`, "
        "because it is discrete (%#x)! Its type: (%#x, %s). ",
        sensor_name_ptr, st->name, sensor_type,
        ipmi_sensor_get_event_reading_type(sensor),
        ipmi_sensor_get_sensor_type_string (sensor));
    return (-1);
  }

  switch (sensor_type)
  {
    case IPMI_SENSOR_TYPE_TEMPERATURE:
      type = "temperature";
      break;

    case IPMI_SENSOR_TYPE_VOLTAGE:
      type = "voltage";
      break;

    case IPMI_SENSOR_TYPE_CURRENT:
      type = "current";
      break;

    case IPMI_SENSOR_TYPE_FAN:
      type = "fanspeed";
      break;

    default:
      {
        INFO ("ipmi plugin: sensor_list_add: Ignore sensor `%s` of `%s`, "
            "because I don't know how to handle its type (%#x, %s). "
            "If you need this sensor, please file a bug report.",
            sensor_name_ptr, st->name, sensor_type,
            ipmi_sensor_get_sensor_type_string (sensor));
        return (-1);
      }
  } /* switch (sensor_type) */

  pthread_mutex_lock (&st->sensor_list_lock);

  list_prev = NULL;
  for (list_item = st->sensor_list;
      list_item != NULL;
      list_item = list_item->next)
  {
    if (ipmi_cmp_sensor_id (sensor_id, list_item->sensor_id) == 0)
      break;
    list_prev = list_item;
  } /* for (list_item) */

  if (list_item != NULL)
  {
    pthread_mutex_unlock (&st->sensor_list_lock);
    return (0);
  }

  list_item = (c_ipmi_sensor_list_t *) calloc (1, sizeof (c_ipmi_sensor_list_t));
  if (list_item == NULL)
  {
    pthread_mutex_unlock (&st->sensor_list_lock);
    return (-1);
  }

  list_item->instance = st;
  list_item->sensor_id = ipmi_sensor_convert_to_id (sensor);

  if (list_prev != NULL)
    list_prev->next = list_item;
  else
    st->sensor_list = list_item;

  sstrncpy (list_item->sensor_name, sensor_name_ptr,
            sizeof (list_item->sensor_name));
  sstrncpy (list_item->sensor_type, type, sizeof (list_item->sensor_type));

  pthread_mutex_unlock (&st->sensor_list_lock);

  if (st->notify_add && (st->init_in_progress == 0))
  {
    notification_t n = { NOTIF_OKAY, cdtime (), "", "", "ipmi",
                         "", "", "", NULL };

    sstrncpy (n.host, (st->host != NULL) ? st->host : hostname_g,
              sizeof (n.host));
    sstrncpy (n.type_instance, list_item->sensor_name,
              sizeof (n.type_instance));
    sstrncpy (n.type, list_item->sensor_type, sizeof (n.type));
    ssnprintf (n.message, sizeof (n.message),
              "sensor %s added", list_item->sensor_name);

    plugin_dispatch_notification (&n);
  }

  return (0);
} /* int sensor_list_add */

static int sensor_list_remove (c_ipmi_instance_t *st, ipmi_sensor_t *sensor)
{
  ipmi_sensor_id_t sensor_id;
  c_ipmi_sensor_list_t *list_item;
  c_ipmi_sensor_list_t *list_prev;

  sensor_id = ipmi_sensor_convert_to_id (sensor);

  pthread_mutex_lock (&st->sensor_list_lock);

  list_prev = NULL;
  for (list_item = st->sensor_list;
      list_item != NULL;
      list_item = list_item->next)
  {
    if (ipmi_cmp_sensor_id (sensor_id, list_item->sensor_id) == 0)
      break;
    list_prev = list_item;
  } /* for (list_item) */

  if (list_item == NULL)
  {
    pthread_mutex_unlock (&st->sensor_list_lock);
    return (-1);
  }

  if (list_prev == NULL)
    st->sensor_list = list_item->next;
  else
    list_prev->next = list_item->next;

  list_prev = NULL;
  list_item->next = NULL;

  pthread_mutex_unlock (&st->sensor_list_lock);

  if (st->notify_remove && st->active)
  {
    notification_t n = { NOTIF_WARNING, cdtime (), "", "",
                         "ipmi", "", "", "", NULL };

    sstrncpy (n.host, (st->host != NULL) ? st->host : hostname_g,
              sizeof (n.host));
    sstrncpy (n.type_instance, list_item->sensor_name,
              sizeof (n.type_instance));
    sstrncpy (n.type, list_item->sensor_type, sizeof (n.type));
    ssnprintf (n.message, sizeof (n.message),
              "sensor %s removed", list_item->sensor_name);

    plugin_dispatch_notification (&n);
  }

  free (list_item);
  return (0);
} /* int sensor_list_remove */

static int sensor_list_read_all (c_ipmi_instance_t *st)
{
  pthread_mutex_lock (&st->sensor_list_lock);

  for (c_ipmi_sensor_list_t *list_item = st->sensor_list;
      list_item != NULL;
      list_item = list_item->next)
  {
    DEBUG ("ipmi plugin: try read sensor `%s` of `%s`, use: %d",
        list_item->sensor_name, st->name, list_item->use);

    /* Reading already initiated */
    if (list_item->use)
      continue;

    list_item->use++;
    ipmi_sensor_id_get_reading (list_item->sensor_id,
        sensor_read_handler, /* user data = */ (void *)list_item);
  } /* for (list_item) */

  pthread_mutex_unlock (&st->sensor_list_lock);

  return (0);
} /* int sensor_list_read_all */

static int sensor_list_remove_all (c_ipmi_instance_t *st)
{
  c_ipmi_sensor_list_t *list_item;

  pthread_mutex_lock (&st->sensor_list_lock);

  list_item = st->sensor_list;
  st->sensor_list = NULL;

  pthread_mutex_unlock (&st->sensor_list_lock);

  while (list_item != NULL)
  {
    c_ipmi_sensor_list_t *list_next = list_item->next;

    free (list_item);

    list_item = list_next;
  } /* while (list_item) */

  return (0);
} /* int sensor_list_remove_all */

/*
 * Entity handlers
 */
static void entity_sensor_update_handler (enum ipmi_update_e op,
    ipmi_entity_t __attribute__((unused)) *entity,
    ipmi_sensor_t *sensor,
    void *user_data)
{
  c_ipmi_instance_t *st = (c_ipmi_instance_t *)user_data;

  if ((op == IPMI_ADDED) || (op == IPMI_CHANGED))
  {
    /* Will check for duplicate entries.. */
    sensor_list_add (st, sensor);
  }
  else if (op == IPMI_DELETED)
  {
    sensor_list_remove (st, sensor);
  }
} /* void entity_sensor_update_handler */

/*
 * Domain handlers
 */
static void domain_entity_update_handler (enum ipmi_update_e op,
    ipmi_domain_t __attribute__((unused)) *domain,
    ipmi_entity_t *entity,
    void *user_data)
{
  int status;
  c_ipmi_instance_t *st = (c_ipmi_instance_t *)user_data;

  if (op == IPMI_ADDED)
  {
    status = ipmi_entity_add_sensor_update_handler (entity,
        entity_sensor_update_handler, /* user data = */ (void *)st);
    if (status != 0)
    {
      c_ipmi_error ("ipmi_entity_add_sensor_update_handler", status);
    }
  }
  else if (op == IPMI_DELETED)
  {
    status = ipmi_entity_remove_sensor_update_handler (entity,
        entity_sensor_update_handler, /* user data = */ (void *)st);
    if (status != 0)
    {
      c_ipmi_error ("ipmi_entity_remove_sensor_update_handler", status);
    }
  }
} /* void domain_entity_update_handler */

static void domain_connection_change_handler (ipmi_domain_t *domain,
    int err,
    unsigned int conn_num,
    unsigned int port_num,
    int still_connected,
    void *user_data)
{
  int status;

  DEBUG ("domain_connection_change_handler (domain = %p, err = %i, "
      "conn_num = %u, port_num = %u, still_connected = %i, "
      "user_data = %p);",
      (void *) domain, err, conn_num, port_num, still_connected, user_data);

  if (err != 0)
    c_ipmi_error("domain_connection_change_handler", err);

  if (!still_connected)
    return;

  status = ipmi_domain_add_entity_update_handler (domain,
      domain_entity_update_handler, /* user data = */ user_data);
  if (status != 0)
  {
    c_ipmi_error ("ipmi_domain_add_entity_update_handler", status);
  }
} /* void domain_connection_change_handler */

static int c_ipmi_thread_init (c_ipmi_instance_t *st)
{
  ipmi_con_t *connection = NULL;
  ipmi_domain_id_t domain_id;
  int status;

  if (st->connaddr != NULL)
  {
    char *ip_addrs[1] = {NULL}, *ports[1] = {NULL};

    ip_addrs[0] = strdup(st->connaddr);
    ports[0] = strdup(IPMI_LAN_STD_PORT_STR);

    status = ipmi_ip_setup_con(
                    ip_addrs, ports, 1,
                    st->authtype,
                    (unsigned int)IPMI_PRIVILEGE_USER,
                    st->username, strlen(st->username),
                    st->password, strlen(st->password),
                    os_handler,
                    /* user data = */ NULL,
                    &connection);
    if (status != 0)
    {
      c_ipmi_error ("ipmi_ip_setup_con", status);
      return (-1);
    }
  }
  else
  {
    status = ipmi_smi_setup_con (/* if_num = */ 0,
        os_handler,
        /* user data = */ NULL,
        &connection);
    if (status != 0)
    {
      c_ipmi_error ("ipmi_smi_setup_con", status);
      return (-1);
    }
  }

  ipmi_open_option_t open_option[2] = {
    [0] = {
      .option = IPMI_OPEN_OPTION_ALL,
      { .ival = 1 }
    },
    [1] = {
      .option = IPMI_OPEN_OPTION_USE_CACHE,
      { .ival = 0 }
    }
  };

  /*
   * NOTE: Domain names must be unique. There is static `domains_list` common
   * to all threads inside lib/domain.c and some ops are done by name.
   */
  status = ipmi_open_domain (st->name, &connection, /* num_con = */ 1,
      domain_connection_change_handler, /* user data = */ (void *)st,
      /* domain_fully_up_handler = */ NULL, /* user data = */ NULL,
      open_option, sizeof (open_option) / sizeof (open_option[0]),
      &domain_id);
  if (status != 0)
  {
    c_ipmi_error ("ipmi_open_domain", status);
    return (-1);
  }

  return (0);
} /* int c_ipmi_thread_init */

static void *c_ipmi_thread_main (void *user_data)
{
  int status;
  c_ipmi_instance_t *st = (c_ipmi_instance_t *)user_data;

  status = c_ipmi_thread_init (st);
  if (status != 0)
  {
    ERROR ("ipmi plugin: c_ipmi_thread_init failed.");
    st->active = 0;
    return ((void *) -1);
  }

  while (st->active != 0)
  {
    struct timeval tv = { 1, 0 };
    os_handler->perform_one_op (os_handler, &tv);
  }

  return ((void *) 0);
} /* void *c_ipmi_thread_main */

static c_ipmi_instance_t *c_ipmi_init_instance()
{
  c_ipmi_instance_t *st;

  st = calloc (1, sizeof (*st));
  if (st == NULL)
  {
    ERROR ("ipmi plugin: calloc failed.");
    return (NULL);
  }

  st->name = strdup("main");
  if (st->name == NULL)
  {
    sfree (st);
    ERROR ("ipmi plugin: strdup() failed.");
    return (NULL);
  }

  st->ignorelist = ignorelist_create (/* invert = */ 1);
  if (st->ignorelist == NULL)
  {
    sfree (st->name);
    sfree (st);
    ERROR ("ipmi plugin: ignorelist_create() failed.");
    return (NULL);
  }

  st->sensor_list = NULL;
  pthread_mutex_init (&st->sensor_list_lock, /* attr = */ NULL);

  st->host = NULL;
  st->connaddr = NULL;
  st->username = NULL;
  st->password = NULL;
  st->authtype = IPMI_AUTHTYPE_DEFAULT;

  st->next = NULL;

  return st;
} /* c_ipmi_instance_t *c_ipmi_init_instance */

static void c_ipmi_free_instance (c_ipmi_instance_t *st)
{
  if (st == NULL)
    return;

  assert(st->next == NULL);

  sfree (st->name);
  sfree (st->host);
  sfree (st->connaddr);
  sfree (st->username);
  sfree (st->password);

  ignorelist_free(st->ignorelist);
  pthread_mutex_destroy (&st->sensor_list_lock);
  sfree (st);
} /* void c_ipmi_free_instance */

void c_ipmi_add_instance (c_ipmi_instance_t *instance)
{
  if (instances == NULL)
  {
    instances = instance;
    return;
  }

  c_ipmi_instance_t *last = instances;

  while (last->next != NULL)
    last = last->next;

  last->next = instance;

  return;
} /* void c_ipmi_add_instance */

static int c_ipmi_config_add_instance (oconfig_item_t *ci)
{
  int status = 0;
  c_ipmi_instance_t *st = c_ipmi_init_instance();
  if (st == NULL)
    return (ENOMEM);

  if (strcasecmp (ci->key, "Instance") == 0)
    status = cf_util_get_string (ci, &st->name);

  if (status != 0)
  {
    c_ipmi_free_instance(st);
    return (status);
  }

  for (int i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Sensor", child->key) == 0)
      ignorelist_add (st->ignorelist, ci->values[0].value.string);
    else if (strcasecmp ("IgnoreSelected", child->key) == 0)
    {
      if (ci->values[0].value.boolean)
        ignorelist_set_invert (st->ignorelist,  /* invert = */ 0);
      else
        ignorelist_set_invert (st->ignorelist,  /* invert = */ 1);
    }
    else if (strcasecmp ("NotifySensorAdd", child->key) == 0)
    {
      if (ci->values[0].value.boolean)
        st->notify_add = 1;
    }
    else if (strcasecmp ("NotifySensorRemove", child->key) == 0)
    {
      if (ci->values[0].value.boolean)
        st->notify_remove = 1;
    }
    else if (strcasecmp ("NotifySensorNotPresent", child->key) == 0)
    {
      if (ci->values[0].value.boolean)
        st->notify_notpresent = 1;
    }
    else if (strcasecmp ("Host", child->key) == 0)
      status = cf_util_get_string (child, &st->host);
    else if (strcasecmp ("Address", child->key) == 0)
      status = cf_util_get_string (child, &st->connaddr);
    else if (strcasecmp ("Username", child->key) == 0)
      status = cf_util_get_string (child, &st->username);
    else if (strcasecmp ("Password", child->key) == 0)
      status = cf_util_get_string (child, &st->password);
    else if (strcasecmp ("AuthType", child->key) == 0)
    {
      char tmp[8];
      status = cf_util_get_string_buffer(child, tmp, sizeof(tmp));
      if (status != 0)
        break;

      if (strcasecmp("MD5", tmp) == 0)
        st->authtype = IPMI_AUTHTYPE_MD5;
      else if (strcasecmp("rmcp+", tmp) == 0)
        st->authtype = IPMI_AUTHTYPE_RMCP_PLUS;
      else
        WARNING("ipmi plugin: The value \"%s\" is not valid for the "
                "\"AuthType\" option.", tmp);
    }
    else
    {
      WARNING ("ipmi plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status != 0)
  {
    c_ipmi_free_instance(st);
    return (status);
  }

  c_ipmi_add_instance(st);

  return (0);
} /* int c_ipmi_config_add_instance */

static int c_ipmi_config (oconfig_item_t *ci)
{
  _Bool have_instance_block = 0;

  for (int i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Instance", child->key) == 0)
    {
      c_ipmi_config_add_instance (child);
      have_instance_block = 1;
    }
    else if (!have_instance_block)
    {
      /* Non-instance option: Assume legacy configuration (without <Instance />
       * blocks) and call c_ipmi_config_add_instance with the <Plugin /> block.
       */
      return (c_ipmi_config_add_instance (ci));
    }
    else
      WARNING ("ipmi plugin: The configuration option "
          "\"%s\" is not allowed here. Did you "
          "forget to add an <Instance /> block "
          "around the configuration?",
          child->key);
  } /* for (ci->children) */

  return (0);
} /* int c_ipmi_config */

static int c_ipmi_read (user_data_t *user_data)
{
  c_ipmi_instance_t *st;
  st = user_data->data;

  if ((st->active == 0) || (st->thread_id == (pthread_t) 0))
  {
    INFO ("ipmi plugin: c_ipmi_read: I'm not active, returning false.");
    return (-1);
  }

  sensor_list_read_all (st);

  if (st->init_in_progress > 0)
    st->init_in_progress--;
  else
    st->init_in_progress = 0;

  return (0);
} /* int c_ipmi_read */

static int c_ipmi_init (void)
{
  int status;
  c_ipmi_instance_t *st;
  char callback_name[3*DATA_MAX_NAME_LEN];

  os_handler = ipmi_posix_thread_setup_os_handler (SIGIO);
  if (os_handler == NULL)
  {
    ERROR ("ipmi plugin: ipmi_posix_thread_setup_os_handler failed.");
    return (-1);
  }

  os_handler->set_log_handler(os_handler, c_ipmi_log);

  if (ipmi_init (os_handler) != 0)
  {
    ERROR ("ipmi plugin: ipmi_init() failed.");
    os_handler->free_os_handler (os_handler);
    return (-1);
  };

  /* Don't send `ADD' notifications during startup (~ 1 minute) */
  time_t iv = CDTIME_T_TO_TIME_T (plugin_get_interval ());

  if (instances == NULL)
  {
    /* No instances were configured, let's start a default instance. */
    st = c_ipmi_init_instance();
    if (st == NULL)
      return (ENOMEM);

    c_ipmi_add_instance (st);
  }

  st = instances;
  while (NULL != st) {
    /* The `st->name` is used as "domain name" for ipmi_open_domain().
     * That value should be unique, so we do plugin_register_complex_read()
     * at first as it checks the uniqueness. */
    ssnprintf (callback_name, sizeof (callback_name), "ipmi/%s", st->name);

    user_data_t ud = {
      .data = st,
    };

    status = plugin_register_complex_read (
        /* group     = */ "ipmi",
        /* name      = */ callback_name,
        /* callback  = */ c_ipmi_read,
        /* interval  = */ 0,
        /* user_data = */ &ud);

    if (status != 0)
    {
      st->active = 0;
      st = st->next;
      continue;
    }

    st->init_in_progress = 1 + (60 / iv);
    st->active = 1;

    status = plugin_thread_create (&st->thread_id, /* attr = */ NULL,
        c_ipmi_thread_main, /* user data = */ (void *) st);

    if (status != 0)
    {
      st->active = 0;
      st->thread_id = (pthread_t) 0;

      plugin_unregister_read(callback_name);

      ERROR ("ipmi plugin: pthread_create failed for `%s`.", callback_name);
    }

    st = st->next;
  }

  return (0);
} /* int c_ipmi_init */

static int c_ipmi_shutdown (void)
{
  c_ipmi_instance_t *st = instances;
  instances = NULL;

  while (st != NULL) {
    c_ipmi_instance_t *next = st->next;

    st->next = NULL;
    st->active = 0;

    if (st->thread_id != (pthread_t) 0)
    {
      pthread_join (st->thread_id, NULL);
      st->thread_id = (pthread_t) 0;
    }

    sensor_list_remove_all(st);
    c_ipmi_free_instance(st);

    st = next;
  }

  os_handler->free_os_handler (os_handler);

  return (0);
} /* int c_ipmi_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("ipmi", c_ipmi_config);
  plugin_register_init ("ipmi", c_ipmi_init);
  plugin_register_shutdown ("ipmi", c_ipmi_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 fdm=marker et : */
