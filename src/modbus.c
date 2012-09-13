/**
 * collectd - src/modbus.c
 * Copyright (C) 2010,2011  noris network AG
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; only version 2.1 of the License is
 * applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Authors:
 *   Florian Forster <octo at noris.net>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"

#include <netdb.h>

#include <modbus/modbus.h>

#ifndef LIBMODBUS_VERSION_CHECK
/* Assume version 2.0.3 */
# define LEGACY_LIBMODBUS 1
#else
/* Assume version 2.9.2 */
#endif

#ifndef MODBUS_TCP_DEFAULT_PORT
# ifdef MODBUS_TCP_PORT
#  define MODBUS_TCP_DEFAULT_PORT MODBUS_TCP_PORT
# else
#  define MODBUS_TCP_DEFAULT_PORT 502
# endif
#endif

/*
 * <Data "data_name">
 *   RegisterBase 1234
 *   RegisterType float
 *   Type gauge
 *   Instance "..."
 * </Data>
 *
 * <Host "name">
 *   Address "addr"
 *   Port "1234"
 *   Interval 60
 *
 *   <Slave 1>
 *     Instance "foobar" # optional
 *     Collect "data_name"
 *   </Slave>
 * </Host>
 */

/*
 * Data structures
 */
enum mb_register_type_e /* {{{ */
{
  REG_TYPE_INT16,
  REG_TYPE_INT32,
  REG_TYPE_UINT16,
  REG_TYPE_UINT32,
  REG_TYPE_FLOAT
}; /* }}} */
typedef enum mb_register_type_e mb_register_type_t;

struct mb_data_s;
typedef struct mb_data_s mb_data_t;
struct mb_data_s /* {{{ */
{
  char *name;
  int register_base;
  mb_register_type_t register_type;
  char type[DATA_MAX_NAME_LEN];
  char instance[DATA_MAX_NAME_LEN];

  mb_data_t *next;
}; /* }}} */

struct mb_slave_s /* {{{ */
{
  int id;
  char instance[DATA_MAX_NAME_LEN];
  mb_data_t *collect;
}; /* }}} */
typedef struct mb_slave_s mb_slave_t;

struct mb_host_s /* {{{ */
{
  char host[DATA_MAX_NAME_LEN];
  char node[NI_MAXHOST];
  /* char service[NI_MAXSERV]; */
  int port;
  cdtime_t interval;

  mb_slave_t *slaves;
  size_t slaves_num;

#if LEGACY_LIBMODBUS
  modbus_param_t connection;
#else
  modbus_t *connection;
#endif
  _Bool is_connected;
  _Bool have_reconnected;
}; /* }}} */
typedef struct mb_host_s mb_host_t;

struct mb_data_group_s;
typedef struct mb_data_group_s mb_data_group_t;
struct mb_data_group_s /* {{{ */
{
  mb_data_t *registers;
  size_t registers_num;

  mb_data_group_t *next;
}; /* }}} */

/*
 * Global variables
 */
static mb_data_t *data_definitions = NULL;

/*
 * Functions
 */
static mb_data_t *data_get_by_name (mb_data_t *src, /* {{{ */
    const char *name)
{
  mb_data_t *ptr;

  if (name == NULL)
    return (NULL);

  for (ptr = src; ptr != NULL; ptr = ptr->next)
    if (strcasecmp (ptr->name, name) == 0)
      return (ptr);

  return (NULL);
} /* }}} mb_data_t *data_get_by_name */

static int data_append (mb_data_t **dst, mb_data_t *src) /* {{{ */
{
  mb_data_t *ptr;

  if ((dst == NULL) || (src == NULL))
    return (EINVAL);

  ptr = *dst;

  if (ptr == NULL)
  {
    *dst = src;
    return (0);
  }

  while (ptr->next != NULL)
    ptr = ptr->next;

  ptr->next = src;

  return (0);
} /* }}} int data_append */

/* Copy a single mb_data_t and append it to another list. */
static int data_copy (mb_data_t **dst, const mb_data_t *src) /* {{{ */
{
  mb_data_t *tmp;
  int status;

  if ((dst == NULL) || (src == NULL))
    return (EINVAL);

  tmp = malloc (sizeof (*tmp));
  if (tmp == NULL)
    return (ENOMEM);
  memcpy (tmp, src, sizeof (*tmp));
  tmp->name = NULL;
  tmp->next = NULL;

  tmp->name = strdup (src->name);
  if (tmp->name == NULL)
  {
    sfree (tmp);
    return (ENOMEM);
  }

  status = data_append (dst, tmp);
  if (status != 0)
  {
    sfree (tmp->name);
    sfree (tmp);
    return (status);
  }

  return (0);
} /* }}} int data_copy */

/* Lookup a single mb_data_t instance, copy it and append the copy to another
 * list. */
static int data_copy_by_name (mb_data_t **dst, mb_data_t *src, /* {{{ */
    const char *name)
{
  mb_data_t *ptr;

  if ((dst == NULL) || (src == NULL) || (name == NULL))
    return (EINVAL);

  ptr = data_get_by_name (src, name);
  if (ptr == NULL)
    return (ENOENT);

  return (data_copy (dst, ptr));
} /* }}} int data_copy_by_name */

/* Read functions */

static int mb_submit (mb_host_t *host, mb_slave_t *slave, /* {{{ */
    mb_data_t *data, value_t value)
{
  value_list_t vl = VALUE_LIST_INIT;

  if ((host == NULL) || (slave == NULL) || (data == NULL))
    return (EINVAL);

  if (host->interval <= 0)
    host->interval = interval_g;

  if (slave->instance[0] == 0)
    ssnprintf (slave->instance, sizeof (slave->instance), "slave_%i",
        slave->id);

  vl.values = &value;
  vl.values_len = 1;
  vl.interval = host->interval;
  sstrncpy (vl.host, host->host, sizeof (vl.host));
  sstrncpy (vl.plugin, "modbus", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, slave->instance, sizeof (vl.plugin_instance));
  sstrncpy (vl.type, data->type, sizeof (vl.type));
  sstrncpy (vl.type_instance, data->instance, sizeof (vl.type_instance));

  return (plugin_dispatch_values (&vl));
} /* }}} int mb_submit */

static float mb_register_to_float (uint16_t hi, uint16_t lo) /* {{{ */
{
  union
  {
    uint8_t b[4];
    float f;
  } conv;

#if BYTE_ORDER == LITTLE_ENDIAN
  /* little endian */
  conv.b[0] = lo & 0x00ff;
  conv.b[1] = (lo >> 8) & 0x00ff;
  conv.b[2] = hi & 0x00ff;
  conv.b[3] = (hi >> 8) & 0x00ff;
#else
  conv.b[3] = lo & 0x00ff;
  conv.b[2] = (lo >> 8) & 0x00ff;
  conv.b[1] = hi & 0x00ff;
  conv.b[0] = (hi >> 8) & 0x00ff;
#endif

  return (conv.f);
} /* }}} float mb_register_to_float */

#if LEGACY_LIBMODBUS
/* Version 2.0.3 */
static int mb_init_connection (mb_host_t *host) /* {{{ */
{
  int status;

  if (host == NULL)
    return (EINVAL);

  if (host->is_connected)
    return (0);

  /* Only reconnect once per interval. */
  if (host->have_reconnected)
    return (-1);

  modbus_set_debug (&host->connection, 1);

  /* We'll do the error handling ourselves. */
  modbus_set_error_handling (&host->connection, NOP_ON_ERROR);

  if ((host->port < 1) || (host->port > 65535))
    host->port = MODBUS_TCP_DEFAULT_PORT;

  DEBUG ("Modbus plugin: Trying to connect to \"%s\", port %i.",
      host->node, host->port);

  modbus_init_tcp (&host->connection,
      /* host = */ host->node,
      /* port = */ host->port);

  status = modbus_connect (&host->connection);
  if (status != 0)
  {
    ERROR ("Modbus plugin: modbus_connect (%s, %i) failed with status %i.",
        host->node, host->port, status);
    return (status);
  }

  host->is_connected = 1;
  host->have_reconnected = 1;
  return (0);
} /* }}} int mb_init_connection */
/* #endif LEGACY_LIBMODBUS */

#else /* if !LEGACY_LIBMODBUS */
/* Version 2.9.2 */
static int mb_init_connection (mb_host_t *host) /* {{{ */
{
  int status;

  if (host == NULL)
    return (EINVAL);

  if (host->connection != NULL)
    return (0);

  /* Only reconnect once per interval. */
  if (host->have_reconnected)
    return (-1);

  if ((host->port < 1) || (host->port > 65535))
    host->port = MODBUS_TCP_DEFAULT_PORT;

  DEBUG ("Modbus plugin: Trying to connect to \"%s\", port %i.",
      host->node, host->port);

  host->connection = modbus_new_tcp (host->node, host->port);
  if (host->connection == NULL)
  {
    host->have_reconnected = 1;
    ERROR ("Modbus plugin: Creating new Modbus/TCP object failed.");
    return (-1);
  }

  modbus_set_debug (host->connection, 1);

  /* We'll do the error handling ourselves. */
  modbus_set_error_recovery (host->connection, 0);

  status = modbus_connect (host->connection);
  if (status != 0)
  {
    ERROR ("Modbus plugin: modbus_connect (%s, %i) failed with status %i.",
        host->node, host->port, status);
    modbus_free (host->connection);
    host->connection = NULL;
    return (status);
  }

  host->have_reconnected = 1;
  return (0);
} /* }}} int mb_init_connection */
#endif /* !LEGACY_LIBMODBUS */

#define CAST_TO_VALUE_T(ds,vt,raw) do { \
  if ((ds)->ds[0].type == DS_TYPE_COUNTER) \
    (vt).counter = (counter_t) (raw); \
  else if ((ds)->ds[0].type == DS_TYPE_GAUGE) \
    (vt).gauge = (gauge_t) (raw); \
  else if ((ds)->ds[0].type == DS_TYPE_DERIVE) \
    (vt).derive = (derive_t) (raw); \
  else /* if (ds->ds[0].type == DS_TYPE_ABSOLUTE) */ \
    (vt).absolute = (absolute_t) (raw); \
} while (0)

static int mb_read_data (mb_host_t *host, mb_slave_t *slave, /* {{{ */
    mb_data_t *data)
{
  uint16_t values[2];
  int values_num;
  const data_set_t *ds;
  int status;
  int i;

  if ((host == NULL) || (slave == NULL) || (data == NULL))
    return (EINVAL);

  ds = plugin_get_ds (data->type);
  if (ds == NULL)
  {
    ERROR ("Modbus plugin: Type \"%s\" is not defined.", data->type);
    return (-1);
  }

  if (ds->ds_num != 1)
  {
    ERROR ("Modbus plugin: The type \"%s\" has %i data sources. "
        "I can only handle data sets with only one data source.",
        data->type, ds->ds_num);
    return (-1);
  }

  if ((ds->ds[0].type != DS_TYPE_GAUGE)
      && (data->register_type != REG_TYPE_INT32)
      && (data->register_type != REG_TYPE_UINT32))
  {
    NOTICE ("Modbus plugin: The data source of type \"%s\" is %s, not gauge. "
        "This will most likely result in problems, because the register type "
        "is not UINT32.", data->type, DS_TYPE_TO_STRING (ds->ds[0].type));
  }

  memset (values, 0, sizeof (values));
  if ((data->register_type == REG_TYPE_INT32)
      || (data->register_type == REG_TYPE_UINT32)
      || (data->register_type == REG_TYPE_FLOAT))
    values_num = 2;
  else
    values_num = 1;

#if LEGACY_LIBMODBUS
  /* Version 2.0.3: Pass the connection struct as a pointer and pass the slave
   * id to each call of "read_holding_registers". */
# define modbus_read_registers(ctx, addr, nb, dest) \
  read_holding_registers (&(ctx), slave->id, (addr), (nb), (dest))
#else /* if !LEGACY_LIBMODBUS */
  /* Version 2.9.2: Set the slave id once before querying the registers. */
  status = modbus_set_slave (host->connection, slave->id);
  if (status != 0)
  {
    ERROR ("Modbus plugin: modbus_set_slave (%i) failed with status %i.",
        slave->id, status);
    return (-1);
  }
#endif

  for (i = 0; i < 2; i++)
  {
    status = modbus_read_registers (host->connection,
        /* start_addr = */ data->register_base,
        /* num_registers = */ values_num, /* buffer = */ values);
    if (status > 0)
      break;

    if (host->is_connected)
    {
#if LEGACY_LIBMODBUS
      modbus_close (&host->connection);
      host->is_connected = 0;
#else
      modbus_close (host->connection);
      modbus_free (host->connection);
      host->connection = NULL;
#endif
    }

    /* If we already tried reconnecting this round, give up. */
    if (host->have_reconnected)
    {
      ERROR ("Modbus plugin: modbus_read_registers (%s) failed. "
          "Reconnecting has already been tried. Giving up.", host->host);
      return (-1);
    }

    /* Maybe the device closed the connection during the waiting interval.
     * Try re-establishing the connection. */
    status = mb_init_connection (host);
    if (status != 0)
    {
      ERROR ("Modbus plugin: modbus_read_registers (%s) failed. "
          "While trying to reconnect, connecting to \"%s\" failed. "
          "Giving up.",
          host->host, host->node);
      return (-1);
    }

    DEBUG ("Modbus plugin: Re-established connection to %s", host->host);

    /* try again */
    continue;
  } /* for (i = 0, 1) */

  DEBUG ("Modbus plugin: mb_read_data: Success! "
      "modbus_read_registers returned with status %i.", status);

  if (data->register_type == REG_TYPE_FLOAT)
  {
    float float_value;
    value_t vt;

    float_value = mb_register_to_float (values[0], values[1]);
    DEBUG ("Modbus plugin: mb_read_data: "
        "Returned float value is %g", (double) float_value);

    CAST_TO_VALUE_T (ds, vt, float_value);
    mb_submit (host, slave, data, vt);
  }
  else if (data->register_type == REG_TYPE_INT32)
  {
    union
    {
      uint32_t u32;
      int32_t  i32;
    } v;
    value_t vt;

    v.u32 = (((uint32_t) values[0]) << 16)
      | ((uint32_t) values[1]);
    DEBUG ("Modbus plugin: mb_read_data: "
        "Returned int32 value is %"PRIi32, v.i32);

    CAST_TO_VALUE_T (ds, vt, v.i32);
    mb_submit (host, slave, data, vt);
  }
  else if (data->register_type == REG_TYPE_INT16)
  {
    union
    {
      uint16_t u16;
      int16_t  i16;
    } v;
    value_t vt;

    v.u16 = values[0];

    DEBUG ("Modbus plugin: mb_read_data: "
        "Returned int16 value is %"PRIi16, v.i16);

    CAST_TO_VALUE_T (ds, vt, v.i16);
    mb_submit (host, slave, data, vt);
  }
  else if (data->register_type == REG_TYPE_UINT32)
  {
    uint32_t v32;
    value_t vt;

    v32 = (((uint32_t) values[0]) << 16)
      | ((uint32_t) values[1]);
    DEBUG ("Modbus plugin: mb_read_data: "
        "Returned uint32 value is %"PRIu32, v32);

    CAST_TO_VALUE_T (ds, vt, v32);
    mb_submit (host, slave, data, vt);
  }
  else /* if (data->register_type == REG_TYPE_UINT16) */
  {
    value_t vt;

    DEBUG ("Modbus plugin: mb_read_data: "
        "Returned uint16 value is %"PRIu16, values[0]);

    CAST_TO_VALUE_T (ds, vt, values[0]);
    mb_submit (host, slave, data, vt);
  }

  return (0);
} /* }}} int mb_read_data */

static int mb_read_slave (mb_host_t *host, mb_slave_t *slave) /* {{{ */
{
  mb_data_t *data;
  int success;
  int status;

  if ((host == NULL) || (slave == NULL))
    return (EINVAL);

  success = 0;
  for (data = slave->collect; data != NULL; data = data->next)
  {
    status = mb_read_data (host, slave, data);
    if (status == 0)
      success++;
  }

  if (success == 0)
    return (-1);
  else
    return (0);
} /* }}} int mb_read_slave */

static int mb_read (user_data_t *user_data) /* {{{ */
{
  mb_host_t *host;
  size_t i;
  int success;
  int status;

  if ((user_data == NULL) || (user_data->data == NULL))
    return (EINVAL);

  host = user_data->data;

  /* Clear the reconnect flag. */
  host->have_reconnected = 0;

  success = 0;
  for (i = 0; i < host->slaves_num; i++)
  {
    status = mb_read_slave (host, host->slaves + i);
    if (status == 0)
      success++;
  }

  if (success == 0)
    return (-1);
  else
    return (0);
} /* }}} int mb_read */

/* Free functions */

static void data_free_one (mb_data_t *data) /* {{{ */
{
  if (data == NULL)
    return;

  sfree (data->name);
  sfree (data);
} /* }}} void data_free_one */

static void data_free_all (mb_data_t *data) /* {{{ */
{
  mb_data_t *next;

  if (data == NULL)
    return;

  next = data->next;
  data_free_one (data);

  data_free_all (next);
} /* }}} void data_free_all */

static void slaves_free_all (mb_slave_t *slaves, size_t slaves_num) /* {{{ */
{
  size_t i;

  if (slaves == NULL)
    return;

  for (i = 0; i < slaves_num; i++)
    data_free_all (slaves[i].collect);
  sfree (slaves);
} /* }}} void slaves_free_all */

static void host_free (void *void_host) /* {{{ */
{
  mb_host_t *host = void_host;

  if (host == NULL)
    return;

  slaves_free_all (host->slaves, host->slaves_num);
  sfree (host);
} /* }}} void host_free */

/* Config functions */

static int mb_config_add_data (oconfig_item_t *ci) /* {{{ */
{
  mb_data_t data;
  int status;
  int i;

  memset (&data, 0, sizeof (data));
  data.name = NULL;
  data.register_type = REG_TYPE_UINT16;
  data.next = NULL;

  status = cf_util_get_string (ci, &data.name);
  if (status != 0)
    return (status);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;
    status = 0;

    if (strcasecmp ("Type", child->key) == 0)
      status = cf_util_get_string_buffer (child,
          data.type, sizeof (data.type));
    else if (strcasecmp ("Instance", child->key) == 0)
      status = cf_util_get_string_buffer (child,
          data.instance, sizeof (data.instance));
    else if (strcasecmp ("RegisterBase", child->key) == 0)
      status = cf_util_get_int (child, &data.register_base);
    else if (strcasecmp ("RegisterType", child->key) == 0)
    {
      char tmp[16];
      status = cf_util_get_string_buffer (child, tmp, sizeof (tmp));
      if (status != 0)
        /* do nothing */;
      else if (strcasecmp ("Int16", tmp) == 0)
        data.register_type = REG_TYPE_INT16;
      else if (strcasecmp ("Int32", tmp) == 0)
        data.register_type = REG_TYPE_INT32;
      else if (strcasecmp ("Uint16", tmp) == 0)
        data.register_type = REG_TYPE_UINT16;
      else if (strcasecmp ("Uint32", tmp) == 0)
        data.register_type = REG_TYPE_UINT32;
      else if (strcasecmp ("Float", tmp) == 0)
        data.register_type = REG_TYPE_FLOAT;
      else
      {
        ERROR ("Modbus plugin: The register type \"%s\" is unknown.", tmp);
        status = -1;
      }
    }
    else
    {
      ERROR ("Modbus plugin: Unknown configuration option: %s", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  assert (data.name != NULL);
  if (data.type[0] == 0)
  {
    ERROR ("Modbus plugin: Data block \"%s\": No type has been specified.",
        data.name);
    status = -1;
  }

  if (status == 0)
    data_copy (&data_definitions, &data);

  sfree (data.name);

  return (status);
} /* }}} int mb_config_add_data */

static int mb_config_set_host_address (mb_host_t *host, /* {{{ */
    const char *address)
{
  struct addrinfo *ai_list;
  struct addrinfo *ai_ptr;
  struct addrinfo  ai_hints;
  int status;

  if ((host == NULL) || (address == NULL))
    return (EINVAL);

  memset (&ai_hints, 0, sizeof (ai_hints));
#if AI_ADDRCONFIG
  ai_hints.ai_flags |= AI_ADDRCONFIG;
#endif
  /* XXX: libmodbus can only handle IPv4 addresses. */
  ai_hints.ai_family = AF_INET;
  ai_hints.ai_addr = NULL;
  ai_hints.ai_canonname = NULL;
  ai_hints.ai_next = NULL;

  ai_list = NULL;
  status = getaddrinfo (address, /* service = */ NULL,
      &ai_hints, &ai_list);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("Modbus plugin: getaddrinfo failed: %s",
        (status == EAI_SYSTEM)
        ? sstrerror (errno, errbuf, sizeof (errbuf))
        : gai_strerror (status));
    return (status);
  }

  for (ai_ptr = ai_list; ai_ptr != NULL; ai_ptr = ai_ptr->ai_next)
  {
    status = getnameinfo (ai_ptr->ai_addr, ai_ptr->ai_addrlen,
        host->node, sizeof (host->node),
        /* service = */ NULL, /* length = */ 0,
        /* flags = */ NI_NUMERICHOST);
    if (status == 0)
      break;
  } /* for (ai_ptr) */

  freeaddrinfo (ai_list);

  if (status != 0)
    ERROR ("Modbus plugin: Unable to translate node name: \"%s\"", address);
  else /* if (status == 0) */
  {
    DEBUG ("Modbus plugin: mb_config_set_host_address: %s -> %s",
        address, host->node);
  }

  return (status);
} /* }}} int mb_config_set_host_address */

static int mb_config_add_slave (mb_host_t *host, oconfig_item_t *ci) /* {{{ */
{
  mb_slave_t *slave;
  int status;
  int i;

  if ((host == NULL) || (ci == NULL))
    return (EINVAL);

  slave = realloc (host->slaves, sizeof (*slave) * (host->slaves_num + 1));
  if (slave == NULL)
    return (ENOMEM);
  host->slaves = slave;
  slave = host->slaves + host->slaves_num;
  memset (slave, 0, sizeof (*slave));
  slave->collect = NULL;

  status = cf_util_get_int (ci, &slave->id);
  if (status != 0)
    return (status);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;
    status = 0;

    if (strcasecmp ("Instance", child->key) == 0)
      status = cf_util_get_string_buffer (child,
          slave->instance, sizeof (slave->instance));
    else if (strcasecmp ("Collect", child->key) == 0)
    {
      char buffer[1024];
      status = cf_util_get_string_buffer (child, buffer, sizeof (buffer));
      if (status == 0)
        data_copy_by_name (&slave->collect, data_definitions, buffer);
      status = 0; /* continue after failure. */
    }
    else
    {
      ERROR ("Modbus plugin: Unknown configuration option: %s", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if ((status == 0) && (slave->collect == NULL))
    status = EINVAL;

  if (slave->id < 0)
    status = EINVAL;

  if (status == 0)
    host->slaves_num++;
  else /* if (status != 0) */
    data_free_all (slave->collect);

  return (status);
} /* }}} int mb_config_add_slave */

static int mb_config_add_host (oconfig_item_t *ci) /* {{{ */
{
  mb_host_t *host;
  int status;
  int i;

  host = malloc (sizeof (*host));
  if (host == NULL)
    return (ENOMEM);
  memset (host, 0, sizeof (*host));
  host->slaves = NULL;

  status = cf_util_get_string_buffer (ci, host->host, sizeof (host->host));
  if (status != 0)
    return (status);
  if (host->host[0] == 0)
    return (EINVAL);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;
    status = 0;

    if (strcasecmp ("Address", child->key) == 0)
    {
      char buffer[NI_MAXHOST];
      status = cf_util_get_string_buffer (child, buffer, sizeof (buffer));
      if (status == 0)
        status = mb_config_set_host_address (host, buffer);
    }
    else if (strcasecmp ("Port", child->key) == 0)
    {
      host->port = cf_util_get_port_number (child);
      if (host->port <= 0)
        status = -1;
    }
    else if (strcasecmp ("Interval", child->key) == 0)
      status = cf_util_get_cdtime (child, &host->interval);
    else if (strcasecmp ("Slave", child->key) == 0)
      /* Don't set status: Gracefully continue if a slave fails. */
      mb_config_add_slave (host, child);
    else
    {
      ERROR ("Modbus plugin: Unknown configuration option: %s", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  assert (host->host[0] != 0);
  if (host->host[0] == 0)
  {
    ERROR ("Modbus plugin: Data block \"%s\": No type has been specified.",
        host->host);
    status = -1;
  }

  if (status == 0)
  {
    user_data_t ud;
    char name[1024];
    struct timespec interval = { 0, 0 };

    ud.data = host;
    ud.free_func = host_free;

    ssnprintf (name, sizeof (name), "modbus-%s", host->host);

    CDTIME_T_TO_TIMESPEC (host->interval, &interval);

    plugin_register_complex_read (/* group = */ NULL, name,
        /* callback = */ mb_read,
        /* interval = */ (host->interval > 0) ? &interval : NULL,
        &ud);
  }
  else
  {
    host_free (host);
  }

  return (status);
} /* }}} int mb_config_add_host */

static int mb_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  if (ci == NULL)
    return (EINVAL);

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Data", child->key) == 0)
      mb_config_add_data (child);
    else if (strcasecmp ("Host", child->key) == 0)
      mb_config_add_host (child);
    else
      ERROR ("Modbus plugin: Unknown configuration option: %s", child->key);
  }

  return (0);
} /* }}} int mb_config */

/* ========= */

static int mb_shutdown (void) /* {{{ */
{
  data_free_all (data_definitions);
  data_definitions = NULL;

  return (0);
} /* }}} int mb_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("modbus", mb_config);
  plugin_register_shutdown ("modbus", mb_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
