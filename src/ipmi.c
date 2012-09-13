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
 *   Florian octo Forster <octo at verplant.org>
 *   Peter Holik <peter at holik.at>
 *   Bruno Prémont <bonbons at linux-vserver.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_ignorelist.h"

#include <pthread.h>

#include <OpenIPMI/ipmiif.h>
#include <OpenIPMI/ipmi_err.h>
#include <OpenIPMI/ipmi_posix.h>
#include <OpenIPMI/ipmi_conn.h>
#include <OpenIPMI/ipmi_smi.h>

/*
 * Private data types
 */
struct c_ipmi_sensor_list_s;
typedef struct c_ipmi_sensor_list_s c_ipmi_sensor_list_t;

struct c_ipmi_sensor_list_s
{
  ipmi_sensor_id_t sensor_id;
  char sensor_name[DATA_MAX_NAME_LEN];
  char sensor_type[DATA_MAX_NAME_LEN];
  int sensor_not_present;
  c_ipmi_sensor_list_t *next;
};

/*
 * Module global variables
 */
static pthread_mutex_t sensor_list_lock = PTHREAD_MUTEX_INITIALIZER;
static c_ipmi_sensor_list_t *sensor_list = NULL;

static int c_ipmi_init_in_progress = 0;
static int c_ipmi_active = 0;
static pthread_t thread_id = (pthread_t) 0;

static const char *config_keys[] =
{
	"Sensor",
	"IgnoreSelected",
	"NotifySensorAdd",
	"NotifySensorRemove",
	"NotifySensorNotPresent"
};
static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static ignorelist_t *ignorelist = NULL;

static int c_ipmi_nofiy_add = 0;
static int c_ipmi_nofiy_remove = 0;
static int c_ipmi_nofiy_notpresent = 0;

/*
 * Misc private functions
 */
static void c_ipmi_error (const char *func, int status)
{
  char errbuf[4096];

  memset (errbuf, 0, sizeof (errbuf));

  if (IPMI_IS_OS_ERR (status))
  {
    sstrerror (IPMI_GET_OS_ERR (status), errbuf, sizeof (errbuf));
  }
  else if (IPMI_IS_IPMI_ERR (status))
  {
    ipmi_get_error_string (IPMI_GET_IPMI_ERR (status), errbuf, sizeof (errbuf));
  }

  if (errbuf[0] == 0)
  {
    ssnprintf (errbuf, sizeof (errbuf), "Unknown error %#x", status);
  }
  errbuf[sizeof (errbuf) - 1] = 0;

  ERROR ("ipmi plugin: %s failed: %s", func, errbuf);
} /* void c_ipmi_error */

/*
 * Sensor handlers
 */
/* Prototype for sensor_list_remove, so sensor_read_handler can call it. */
static int sensor_list_remove (ipmi_sensor_t *sensor);

static void sensor_read_handler (ipmi_sensor_t *sensor,
    int err,
    enum ipmi_value_present_e value_present,
    unsigned int __attribute__((unused)) raw_value,
    double value,
    ipmi_states_t __attribute__((unused)) *states,
    void *user_data)
{
  value_t values[1];
  value_list_t vl = VALUE_LIST_INIT;

  c_ipmi_sensor_list_t *list_item = (c_ipmi_sensor_list_t *)user_data;

  if (err != 0)
  {
    if ((err & 0xff) == IPMI_NOT_PRESENT_CC)
    {
      if (list_item->sensor_not_present == 0)
      {
        list_item->sensor_not_present = 1;

        INFO ("ipmi plugin: sensor_read_handler: sensor %s "
            "not present.", list_item->sensor_name);

        if (c_ipmi_nofiy_notpresent)
        {
          notification_t n = { NOTIF_WARNING, cdtime (), "", "", "ipmi",
            "", "", "", NULL };

          sstrncpy (n.host, hostname_g, sizeof (n.host));
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
      INFO ("ipmi plugin: sensor_read_handler: Sensor %s not ready",
          list_item->sensor_name);
    }
    else
    {
      if (IPMI_IS_IPMI_ERR(err))
        INFO ("ipmi plugin: sensor_read_handler: Removing sensor %s, "
            "because it failed with IPMI error %#x.",
            list_item->sensor_name, IPMI_GET_IPMI_ERR(err));
      else if (IPMI_IS_OS_ERR(err))
        INFO ("ipmi plugin: sensor_read_handler: Removing sensor %s, "
            "because it failed with OS error %#x.",
            list_item->sensor_name, IPMI_GET_OS_ERR(err));
      else if (IPMI_IS_RMCPP_ERR(err))
        INFO ("ipmi plugin: sensor_read_handler: Removing sensor %s, "
            "because it failed with RMCPP error %#x.",
            list_item->sensor_name, IPMI_GET_RMCPP_ERR(err));
      else if (IPMI_IS_SOL_ERR(err))
        INFO ("ipmi plugin: sensor_read_handler: Removing sensor %s, "
            "because it failed with RMCPP error %#x.",
            list_item->sensor_name, IPMI_GET_SOL_ERR(err));
      else
        INFO ("ipmi plugin: sensor_read_handler: Removing sensor %s, "
            "because it failed with error %#x. of class %#x",
            list_item->sensor_name, err & 0xff, err & 0xffffff00);
      sensor_list_remove (sensor);
    }
    return;
  }
  else if (list_item->sensor_not_present == 1)
  {
    list_item->sensor_not_present = 0;

    INFO ("ipmi plugin: sensor_read_handler: sensor %s present.",
        list_item->sensor_name);

    if (c_ipmi_nofiy_notpresent)
    {
      notification_t n = { NOTIF_OKAY, cdtime (), "", "", "ipmi",
        "", "", "", NULL };

      sstrncpy (n.host, hostname_g, sizeof (n.host));
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
    INFO ("ipmi plugin: sensor_read_handler: Removing sensor %s, "
        "because it provides %s. If you need this sensor, "
        "please file a bug report.",
        list_item->sensor_name,
        (value_present == IPMI_RAW_VALUE_PRESENT)
        ? "only the raw value"
        : "no value");
    sensor_list_remove (sensor);
    return;
  }

  values[0].gauge = value;

  vl.values = values;
  vl.values_len = 1;

  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "ipmi", sizeof (vl.plugin));
  sstrncpy (vl.type, list_item->sensor_type, sizeof (vl.type));
  sstrncpy (vl.type_instance, list_item->sensor_name, sizeof (vl.type_instance));

  plugin_dispatch_values (&vl);
} /* void sensor_read_handler */

static int sensor_list_add (ipmi_sensor_t *sensor)
{
  ipmi_sensor_id_t sensor_id;
  c_ipmi_sensor_list_t *list_item;
  c_ipmi_sensor_list_t *list_prev;

  char buffer[DATA_MAX_NAME_LEN];
  const char *entity_id_string;
  char sensor_name[DATA_MAX_NAME_LEN];
  char *sensor_name_ptr;
  int sensor_type;
  const char *type;
  ipmi_entity_t *ent = ipmi_sensor_get_entity(sensor);

  sensor_id = ipmi_sensor_convert_to_id (sensor);

  memset (buffer, 0, sizeof (buffer));
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

  /* Both `ignorelist' and `plugin_instance' may be NULL. */
  if (ignorelist_match (ignorelist, sensor_name_ptr) != 0)
    return (0);

  /* FIXME: Use rate unit or base unit to scale the value */

  sensor_type = ipmi_sensor_get_sensor_type (sensor);
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
        const char *sensor_type_str;

        sensor_type_str = ipmi_sensor_get_sensor_type_string (sensor);
        INFO ("ipmi plugin: sensor_list_add: Ignore sensor %s, "
            "because I don't know how to handle its type (%#x, %s). "
            "If you need this sensor, please file a bug report.",
            sensor_name_ptr, sensor_type, sensor_type_str);
        return (-1);
      }
  } /* switch (sensor_type) */

  pthread_mutex_lock (&sensor_list_lock);

  list_prev = NULL;
  for (list_item = sensor_list;
      list_item != NULL;
      list_item = list_item->next)
  {
    if (ipmi_cmp_sensor_id (sensor_id, list_item->sensor_id) == 0)
      break;
    list_prev = list_item;
  } /* for (list_item) */

  if (list_item != NULL)
  {
    pthread_mutex_unlock (&sensor_list_lock);
    return (0);
  }

  list_item = (c_ipmi_sensor_list_t *) calloc (1, sizeof (c_ipmi_sensor_list_t));
  if (list_item == NULL)
  {
    pthread_mutex_unlock (&sensor_list_lock);
    return (-1);
  }

  list_item->sensor_id = ipmi_sensor_convert_to_id (sensor);

  if (list_prev != NULL)
    list_prev->next = list_item;
  else
    sensor_list = list_item;

  sstrncpy (list_item->sensor_name, sensor_name_ptr,
            sizeof (list_item->sensor_name));
  sstrncpy (list_item->sensor_type, type, sizeof (list_item->sensor_type));

  pthread_mutex_unlock (&sensor_list_lock);

  if (c_ipmi_nofiy_add && (c_ipmi_init_in_progress == 0))
  {
    notification_t n = { NOTIF_OKAY, cdtime (), "", "", "ipmi",
                         "", "", "", NULL };

    sstrncpy (n.host, hostname_g, sizeof (n.host));
    sstrncpy (n.type_instance, list_item->sensor_name,
              sizeof (n.type_instance));
    sstrncpy (n.type, list_item->sensor_type, sizeof (n.type));
    ssnprintf (n.message, sizeof (n.message),
              "sensor %s added", list_item->sensor_name);

    plugin_dispatch_notification (&n);
  }

  return (0);
} /* int sensor_list_add */

static int sensor_list_remove (ipmi_sensor_t *sensor)
{
  ipmi_sensor_id_t sensor_id;
  c_ipmi_sensor_list_t *list_item;
  c_ipmi_sensor_list_t *list_prev;

  sensor_id = ipmi_sensor_convert_to_id (sensor);

  pthread_mutex_lock (&sensor_list_lock);

  list_prev = NULL;
  for (list_item = sensor_list;
      list_item != NULL;
      list_item = list_item->next)
  {
    if (ipmi_cmp_sensor_id (sensor_id, list_item->sensor_id) == 0)
      break;
    list_prev = list_item;
  } /* for (list_item) */

  if (list_item == NULL)
  {
    pthread_mutex_unlock (&sensor_list_lock);
    return (-1);
  }

  if (list_prev == NULL)
    sensor_list = list_item->next;
  else
    list_prev->next = list_item->next;

  list_prev = NULL;
  list_item->next = NULL;

  pthread_mutex_unlock (&sensor_list_lock);

  if (c_ipmi_nofiy_remove && c_ipmi_active)
  {
    notification_t n = { NOTIF_WARNING, cdtime (), "", "",
                         "ipmi", "", "", "", NULL };

    sstrncpy (n.host, hostname_g, sizeof (n.host));
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

static int sensor_list_read_all (void)
{
  c_ipmi_sensor_list_t *list_item;

  pthread_mutex_lock (&sensor_list_lock);

  for (list_item = sensor_list;
      list_item != NULL;
      list_item = list_item->next)
  {
    ipmi_sensor_id_get_reading (list_item->sensor_id,
        sensor_read_handler, /* user data = */ list_item);
  } /* for (list_item) */

  pthread_mutex_unlock (&sensor_list_lock);

  return (0);
} /* int sensor_list_read_all */

static int sensor_list_remove_all (void)
{
  c_ipmi_sensor_list_t *list_item;

  pthread_mutex_lock (&sensor_list_lock);

  list_item = sensor_list;
  sensor_list = NULL;

  pthread_mutex_unlock (&sensor_list_lock);

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
    void __attribute__((unused)) *user_data)
{
  /* TODO: Ignore sensors we cannot read */

  if ((op == IPMI_ADDED) || (op == IPMI_CHANGED))
  {
    /* Will check for duplicate entries.. */
    sensor_list_add (sensor);
  }
  else if (op == IPMI_DELETED)
  {
    sensor_list_remove (sensor);
  }
} /* void entity_sensor_update_handler */

/*
 * Domain handlers
 */
static void domain_entity_update_handler (enum ipmi_update_e op,
    ipmi_domain_t __attribute__((unused)) *domain,
    ipmi_entity_t *entity,
    void __attribute__((unused)) *user_data)
{
  int status;

  if (op == IPMI_ADDED)
  {
    status = ipmi_entity_add_sensor_update_handler (entity,
        entity_sensor_update_handler, /* user data = */ NULL);
    if (status != 0)
    {
      c_ipmi_error ("ipmi_entity_add_sensor_update_handler", status);
    }
  }
  else if (op == IPMI_DELETED)
  {
    status = ipmi_entity_remove_sensor_update_handler (entity,
        entity_sensor_update_handler, /* user data = */ NULL);
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
      "user_data = %p);\n",
      (void *) domain, err, conn_num, port_num, still_connected, user_data);

  status = ipmi_domain_add_entity_update_handler (domain,
      domain_entity_update_handler, /* user data = */ NULL);
  if (status != 0)
  {
    c_ipmi_error ("ipmi_domain_add_entity_update_handler", status);
  }
} /* void domain_connection_change_handler */

static int thread_init (os_handler_t **ret_os_handler)
{
  os_handler_t *os_handler;
  ipmi_open_option_t open_option[1];
  ipmi_con_t *smi_connection = NULL;
  ipmi_domain_id_t domain_id;
  int status;

  os_handler = ipmi_posix_thread_setup_os_handler (SIGUSR2);
  if (os_handler == NULL)
  {
    ERROR ("ipmi plugin: ipmi_posix_thread_setup_os_handler failed.");
    return (-1);
  }

  ipmi_init (os_handler);

  status = ipmi_smi_setup_con (/* if_num = */ 0,
      os_handler,
      /* user data = */ NULL,
      &smi_connection);
  if (status != 0)
  {
    c_ipmi_error ("ipmi_smi_setup_con", status);
    return (-1);
  }

  memset (open_option, 0, sizeof (open_option));
  open_option[0].option = IPMI_OPEN_OPTION_ALL;
  open_option[0].ival = 1;

  status = ipmi_open_domain ("mydomain", &smi_connection, /* num_con = */ 1,
      domain_connection_change_handler, /* user data = */ NULL,
      /* domain_fully_up_handler = */ NULL, /* user data = */ NULL,
      open_option, sizeof (open_option) / sizeof (open_option[0]),
      &domain_id);
  if (status != 0)
  {
    c_ipmi_error ("ipmi_open_domain", status);
    return (-1);
  }

  *ret_os_handler = os_handler;
  return (0);
} /* int thread_init */

static void *thread_main (void __attribute__((unused)) *user_data)
{
  int status;
  os_handler_t *os_handler = NULL;

  status = thread_init (&os_handler);
  if (status != 0)
  {
    ERROR ("ipmi plugin: thread_init failed.\n");
    return ((void *) -1);
  }

  while (c_ipmi_active != 0)
  {
    struct timeval tv = { 1, 0 };
    os_handler->perform_one_op (os_handler, &tv);
  }

  ipmi_posix_thread_free_os_handler (os_handler);

  return ((void *) 0);
} /* void *thread_main */

static int c_ipmi_config (const char *key, const char *value)
{
  if (ignorelist == NULL)
    ignorelist = ignorelist_create (/* invert = */ 1);
  if (ignorelist == NULL)
    return (1);

  if (strcasecmp ("Sensor", key) == 0)
  {
    ignorelist_add (ignorelist, value);
  }
  else if (strcasecmp ("IgnoreSelected", key) == 0)
  {
    int invert = 1;
    if (IS_TRUE (value))
      invert = 0;
    ignorelist_set_invert (ignorelist, invert);
  }
  else if (strcasecmp ("NotifySensorAdd", key) == 0)
  {
    if (IS_TRUE (value))
      c_ipmi_nofiy_add = 1;
  }
  else if (strcasecmp ("NotifySensorRemove", key) == 0)
  {
    if (IS_TRUE (value))
      c_ipmi_nofiy_remove = 1;
  }
  else if (strcasecmp ("NotifySensorNotPresent", key) == 0)
  {
    if (IS_TRUE (value))
      c_ipmi_nofiy_notpresent = 1;
  }
  else
  {
    return (-1);
  }

  return (0);
} /* int c_ipmi_config */

static int c_ipmi_init (void)
{
  int status;

  /* Don't send `ADD' notifications during startup (~ 1 minute) */
  time_t iv = CDTIME_T_TO_TIME_T (interval_g);
  c_ipmi_init_in_progress = 1 + (60 / iv);

  c_ipmi_active = 1;

  status = pthread_create (&thread_id, /* attr = */ NULL, thread_main,
      /* user data = */ NULL);
  if (status != 0)
  {
    c_ipmi_active = 0;
    thread_id = (pthread_t) 0;
    ERROR ("ipmi plugin: pthread_create failed.");
    return (-1);
  }

  return (0);
} /* int c_ipmi_init */

static int c_ipmi_read (void)
{
  if ((c_ipmi_active == 0) || (thread_id == (pthread_t) 0))
  {
    INFO ("ipmi plugin: c_ipmi_read: I'm not active, returning false.");
    return (-1);
  }

  sensor_list_read_all ();

  if (c_ipmi_init_in_progress > 0)
    c_ipmi_init_in_progress--;
  else
    c_ipmi_init_in_progress = 0;

  return (0);
} /* int c_ipmi_read */

static int c_ipmi_shutdown (void)
{
  c_ipmi_active = 0;

  if (thread_id != (pthread_t) 0)
  {
    pthread_join (thread_id, NULL);
    thread_id = (pthread_t) 0;
  }

  sensor_list_remove_all ();

  return (0);
} /* int c_ipmi_shutdown */

void module_register (void)
{
  plugin_register_config ("ipmi", c_ipmi_config,
      config_keys, config_keys_num);
  plugin_register_init ("ipmi", c_ipmi_init);
  plugin_register_read ("ipmi", c_ipmi_read);
  plugin_register_shutdown ("ipmi", c_ipmi_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 ts=8 fdm=marker et : */
