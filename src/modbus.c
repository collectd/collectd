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

#include "configfile.h"
#include "plugin.h"
#include "utils/common/common.h"

#include <modbus.h>
#include <netdb.h>
#include <sys/socket.h>

#ifndef LIBMODBUS_VERSION_CHECK
/* Assume version 2.0.3 */
#define LEGACY_LIBMODBUS 1
#else
/* Assume version 2.9.2 */
#endif

#ifndef MODBUS_TCP_DEFAULT_PORT
#ifdef MODBUS_TCP_PORT
#define MODBUS_TCP_DEFAULT_PORT MODBUS_TCP_PORT
#else
#define MODBUS_TCP_DEFAULT_PORT 502
#endif
#endif

/*
 * <Data "data_name">
 *   RegisterBase 1234
 *   RegisterCmd ReadHolding
 *   RegisterType float
 *   Type gauge
 *   Instance "..."
 * </Data>
 *
 * <Host "name">
 *   Address "addr"
 *   Port "1234"
 *   # Or:
 *   # Device "/dev/ttyUSB0"
 *   # Baudrate 38400
 *   # (Assumes 8N1)
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
{ REG_TYPE_INT16,
  REG_TYPE_INT32,
  REG_TYPE_INT32_CDAB,
  REG_TYPE_UINT16,
  REG_TYPE_UINT32,
  REG_TYPE_UINT32_CDAB,
  REG_TYPE_INT64,
  REG_TYPE_UINT64,
  REG_TYPE_FLOAT,
  REG_TYPE_FLOAT_CDAB,
  REG_TYPE_DOUBLE,
}; /* }}} */

enum mb_mreg_type_e /* {{{ */
{ MREG_HOLDING,
  MREG_INPUT }; /* }}} */
typedef enum mb_register_type_e mb_register_type_t;
typedef enum mb_mreg_type_e mb_mreg_type_t;

/* TCP or RTU depending on what is specified in host config block */
enum mb_conntype_e /* {{{ */
{ MBCONN_TCP,
  MBCONN_RTU }; /* }}} */
typedef enum mb_conntype_e mb_conntype_t;

enum mb_uarttype_e /* {{{ */
{ UARTTYPE_RS232,
  UARTTYPE_RS422,
  UARTTYPE_RS485 }; /* }}} */
typedef enum mb_uarttype_e mb_uarttype_t;

struct mb_data_s;
typedef struct mb_data_s mb_data_t;
struct mb_data_s /* {{{ */
{
  char *name;
  int register_base;
  mb_register_type_t register_type;
  mb_mreg_type_t modbus_register_type;
  char type[DATA_MAX_NAME_LEN];
  char instance[DATA_MAX_NAME_LEN];
  double scale;
  double shift;

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
  char node[NI_MAXHOST]; /* TCP hostname or RTU serial device */
  /* char service[NI_MAXSERV]; */
  int port;               /* for Modbus/TCP */
  int baudrate;           /* for Modbus/RTU */
  mb_uarttype_t uarttype; /* UART type for Modbus/RTU */
  mb_conntype_t conntype;

  mb_slave_t *slaves;
  size_t slaves_num;

#if LEGACY_LIBMODBUS
  modbus_param_t connection;
#else
  modbus_t *connection;
#endif
  bool is_connected;
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
static mb_data_t *data_definitions;

/*
 * Functions
 */
static mb_data_t *data_get_by_name(mb_data_t *src, /* {{{ */
                                   const char *name) {
  if (name == NULL)
    return NULL;

  for (mb_data_t *ptr = src; ptr != NULL; ptr = ptr->next)
    if (strcasecmp(ptr->name, name) == 0)
      return ptr;

  return NULL;
} /* }}} mb_data_t *data_get_by_name */

static int data_append(mb_data_t **dst, mb_data_t *src) /* {{{ */
{
  mb_data_t *ptr;

  if ((dst == NULL) || (src == NULL))
    return EINVAL;

  ptr = *dst;

  if (ptr == NULL) {
    *dst = src;
    return 0;
  }

  while (ptr->next != NULL)
    ptr = ptr->next;

  ptr->next = src;

  return 0;
} /* }}} int data_append */

/* Copy a single mb_data_t and append it to another list. */
static int data_copy(mb_data_t **dst, const mb_data_t *src) /* {{{ */
{
  mb_data_t *tmp;
  int status;

  if ((dst == NULL) || (src == NULL))
    return EINVAL;

  tmp = malloc(sizeof(*tmp));
  if (tmp == NULL)
    return ENOMEM;
  memcpy(tmp, src, sizeof(*tmp));
  tmp->name = NULL;
  tmp->next = NULL;

  tmp->name = strdup(src->name);
  if (tmp->name == NULL) {
    sfree(tmp);
    return ENOMEM;
  }

  status = data_append(dst, tmp);
  if (status != 0) {
    sfree(tmp->name);
    sfree(tmp);
    return status;
  }

  return 0;
} /* }}} int data_copy */

/* Lookup a single mb_data_t instance, copy it and append the copy to another
 * list. */
static int data_copy_by_name(mb_data_t **dst, mb_data_t *src, /* {{{ */
                             const char *name) {
  mb_data_t *ptr;

  if ((dst == NULL) || (src == NULL) || (name == NULL))
    return EINVAL;

  ptr = data_get_by_name(src, name);
  if (ptr == NULL)
    return ENOENT;

  return data_copy(dst, ptr);
} /* }}} int data_copy_by_name */

/* Read functions */

static int mb_submit(mb_host_t *host, mb_slave_t *slave, /* {{{ */
                     mb_data_t *data, value_t value) {
  value_list_t vl = VALUE_LIST_INIT;

  if ((host == NULL) || (slave == NULL) || (data == NULL))
    return EINVAL;

  if (slave->instance[0] == 0)
    ssnprintf(slave->instance, sizeof(slave->instance), "slave_%i", slave->id);

  vl.values = &value;
  vl.values_len = 1;
  sstrncpy(vl.host, host->host, sizeof(vl.host));
  sstrncpy(vl.plugin, "modbus", sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, slave->instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, data->type, sizeof(vl.type));
  sstrncpy(vl.type_instance, data->instance, sizeof(vl.type_instance));

  return plugin_dispatch_values(&vl);
} /* }}} int mb_submit */

static float mb_register_to_float(uint16_t hi, uint16_t lo) /* {{{ */
{
  union {
    uint8_t b[4];
    uint16_t s[2];
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

  return conv.f;
} /* }}} float mb_register_to_float */

static double mb_register_to_double(uint16_t b0, uint16_t b1, uint16_t b2,
                                    uint16_t b3) /* {{{ */
{
  union {
    uint8_t b[8];
    double d;
  } conv;

#if BYTE_ORDER == LITTLE_ENDIAN
  /* little endian */
  conv.b[0] = b3 & 0x00ff;
  conv.b[1] = (b3 >> 8) & 0x00ff;
  conv.b[2] = b2 & 0x00ff;
  conv.b[3] = (b2 >> 8) & 0x00ff;
  conv.b[4] = b1 & 0x00ff;
  conv.b[5] = (b1 >> 8) & 0x00ff;
  conv.b[6] = b0 & 0x00ff;
  conv.b[7] = (b0 >> 8) & 0x00ff;
#else
  conv.b[7] = b3 & 0x00ff;
  conv.b[6] = (b3 >> 8) & 0x00ff;
  conv.b[5] = b2 & 0x00ff;
  conv.b[4] = (b2 >> 8) & 0x00ff;
  conv.b[3] = b1 & 0x00ff;
  conv.b[2] = (b1 >> 8) & 0x00ff;
  conv.b[1] = b0 & 0x00ff;
  conv.b[0] = (b0 >> 8) & 0x00ff;
#endif

  return (conv.d);
} /* }}} double mb_register_to_double */

#if LEGACY_LIBMODBUS
/* Version 2.0.3 */
static int mb_init_connection(mb_host_t *host) /* {{{ */
{
  int status;

  if (host == NULL)
    return EINVAL;

#if COLLECT_DEBUG
  modbus_set_debug(&host->connection, 1);
#endif

  /* We'll do the error handling ourselves. */
  modbus_set_error_handling(&host->connection, NOP_ON_ERROR);

  if (host->conntype == MBCONN_TCP) {
    if ((host->port < 1) || (host->port > 65535))
      host->port = MODBUS_TCP_DEFAULT_PORT;

    DEBUG("Modbus plugin: Trying to connect to \"%s\", port %i.", host->node,
          host->port);

    modbus_init_tcp(&host->connection,
                    /* host = */ host->node,
                    /* port = */ host->port);
  } else /* MBCONN_RTU */
  {
    DEBUG("Modbus plugin: Trying to connect to \"%s\".", host->node);

    modbus_init_rtu(&host->connection,
                    /* device = */ host->node,
                    /* baudrate = */ host->baudrate, 'N', 8, 1, 0);
  }

  status = modbus_connect(&host->connection);
  if (status != 0) {
    ERROR("Modbus plugin: modbus_connect (%s, %i) failed with status %i.",
          host->node, host->port ? host->port : host->baudrate, status);
    return status;
  }

  host->is_connected = true;
  return 0;
} /* }}} int mb_init_connection */
/* #endif LEGACY_LIBMODBUS */

#else /* if !LEGACY_LIBMODBUS */
/* Version 2.9.2 */
static int mb_init_connection(mb_host_t *host) /* {{{ */
{
  int status;

  if (host == NULL)
    return EINVAL;

  if (host->connection != NULL)
    return 0;

  if (host->conntype == MBCONN_TCP) {
    if ((host->port < 1) || (host->port > 65535))
      host->port = MODBUS_TCP_DEFAULT_PORT;

    DEBUG("Modbus plugin: Trying to connect to \"%s\", port %i.", host->node,
          host->port);

    host->connection = modbus_new_tcp(host->node, host->port);
    if (host->connection == NULL) {
      ERROR("Modbus plugin: Creating new Modbus/TCP object failed.");
      return -1;
    }
  } else {
    DEBUG("Modbus plugin: Trying to connect to \"%s\", baudrate %i.",
          host->node, host->baudrate);

    host->connection = modbus_new_rtu(host->node, host->baudrate, 'N', 8, 1);
    if (host->connection == NULL) {
      ERROR("Modbus plugin: Creating new Modbus/RTU object failed.");
      return -1;
    }
  }

#if COLLECT_DEBUG
  modbus_set_debug(host->connection, 1);
#endif

  /* We'll do the error handling ourselves. */
  modbus_set_error_recovery(host->connection, 0);

  status = modbus_connect(host->connection);
  if (status != 0) {
    ERROR("Modbus plugin: modbus_connect (%s, %i) failed with status %i.",
          host->node, host->port ? host->port : host->baudrate, status);
    modbus_free(host->connection);
    host->connection = NULL;
    return status;
  }

#if defined(linux) && LIBMODBUS_VERSION_CHECK(2, 9, 4)
  switch (host->uarttype) {
  case UARTTYPE_RS485:
    if (modbus_rtu_set_serial_mode(host->connection, MODBUS_RTU_RS485))
      DEBUG("Modbus plugin: Setting RS485 mode failed.");
    break;
  case UARTTYPE_RS422:
    /* libmodbus doesn't say anything about full-duplex symmetric RS422 UART */
    break;
  case UARTTYPE_RS232:
    break;
  default:
    DEBUG("Modbus plugin: Invalid UART type!.");
  }
#endif /* defined(linux) && LIBMODBUS_VERSION_CHECK(2, 9, 4) */

  return 0;
} /* }}} int mb_init_connection */
#endif /* !LEGACY_LIBMODBUS */

#define CAST_TO_VALUE_T(ds, vt, raw, scale, shift)                             \
  do {                                                                         \
    if ((ds)->ds[0].type == DS_TYPE_COUNTER)                                   \
      (vt).counter = (((counter_t)(raw)*scale) + shift);                       \
    else if ((ds)->ds[0].type == DS_TYPE_GAUGE)                                \
      (vt).gauge = (((gauge_t)(raw)*scale) + shift);                           \
    else if ((ds)->ds[0].type == DS_TYPE_DERIVE)                               \
      (vt).derive = (((derive_t)(raw)*scale) + shift);                         \
    else /* if (ds->ds[0].type == DS_TYPE_ABSOLUTE) */                         \
      (vt).absolute = (((absolute_t)(raw)*scale) + shift);                     \
  } while (0)

static int mb_read_data(mb_host_t *host, mb_slave_t *slave, /* {{{ */
                        mb_data_t *data) {
  uint16_t values[4] = {0};
  int values_num;
  const data_set_t *ds;
  int status = 0;

  if ((host == NULL) || (slave == NULL) || (data == NULL))
    return EINVAL;

  ds = plugin_get_ds(data->type);
  if (ds == NULL) {
    ERROR("Modbus plugin: Type \"%s\" is not defined.", data->type);
    return -1;
  }

  if (ds->ds_num != 1) {
    ERROR("Modbus plugin: The type \"%s\" has %" PRIsz " data sources. "
          "I can only handle data sets with only one data source.",
          data->type, ds->ds_num);
    return -1;
  }

  if ((ds->ds[0].type != DS_TYPE_GAUGE) &&
      (data->register_type != REG_TYPE_INT32) &&
      (data->register_type != REG_TYPE_INT32_CDAB) &&
      (data->register_type != REG_TYPE_UINT32) &&
      (data->register_type != REG_TYPE_UINT32_CDAB) &&
      (data->register_type != REG_TYPE_INT64) &&
      (data->register_type != REG_TYPE_UINT64)) {
    NOTICE(
        "Modbus plugin: The data source of type \"%s\" is %s, not gauge. "
        "This will most likely result in problems, because the register type "
        "is not UINT32 or UINT64.",
        data->type, DS_TYPE_TO_STRING(ds->ds[0].type));
  }

  if ((data->register_type == REG_TYPE_INT32) ||
      (data->register_type == REG_TYPE_INT32_CDAB) ||
      (data->register_type == REG_TYPE_UINT32) ||
      (data->register_type == REG_TYPE_UINT32_CDAB) ||
      (data->register_type == REG_TYPE_FLOAT) ||
      (data->register_type == REG_TYPE_FLOAT_CDAB))
    values_num = 2;
  else if ((data->register_type == REG_TYPE_INT64) ||
           (data->register_type == REG_TYPE_UINT64) ||
           (data->register_type == REG_TYPE_DOUBLE))
    values_num = 4;
  else
    values_num = 1;

  if (host->connection == NULL) {
    status = EBADF;
  } else if (host->conntype == MBCONN_TCP) {
    /* getpeername() is used only to determine if the socket is connected, not
     * because we're really interested in the peer's IP address. */
    if (getpeername(modbus_get_socket(host->connection),
                    (void *)&(struct sockaddr_storage){0},
                    &(socklen_t){sizeof(struct sockaddr_storage)}) != 0)
      status = errno;
  }

  if ((status == EBADF) || (status == ENOTSOCK) || (status == ENOTCONN)) {
    status = mb_init_connection(host);
    if (status != 0) {
      ERROR("Modbus plugin: mb_init_connection (%s/%s) failed. ", host->host,
            host->node);
      host->is_connected = false;
      host->connection = NULL;
      return -1;
    }
  } else if (status != 0) {
#if LEGACY_LIBMODBUS
    modbus_close(&host->connection);
#else
    modbus_close(host->connection);
    modbus_free(host->connection);
#endif
  }

#if LEGACY_LIBMODBUS
/* Version 2.0.3: Pass the connection struct as a pointer and pass the slave
 * id to each call of "read_holding_registers". */
#define modbus_read_registers(ctx, addr, nb, dest)                             \
  read_holding_registers(&(ctx), slave->id, (addr), (nb), (dest))
#else /* if !LEGACY_LIBMODBUS */
  /* Version 2.9.2: Set the slave id once before querying the registers. */
  status = modbus_set_slave(host->connection, slave->id);
  if (status != 0) {
    ERROR("Modbus plugin: modbus_set_slave (%i) failed with status %i.",
          slave->id, status);
    return -1;
  }
#endif
  if (data->modbus_register_type == MREG_INPUT) {
    status = modbus_read_input_registers(host->connection,
                                         /* start_addr = */ data->register_base,
                                         /* num_registers = */ values_num,
                                         /* buffer = */ values);
  } else {
    status = modbus_read_registers(host->connection,
                                   /* start_addr = */ data->register_base,
                                   /* num_registers = */ values_num,
                                   /* buffer = */ values);
  }
  if (status != values_num) {
    ERROR("Modbus plugin: modbus read function (%s/%s) failed. "
          " status = %i, start_addr = %i, values_num = %i. Giving up.",
          host->host, host->node, status, data->register_base, values_num);
#if LEGACY_LIBMODBUS
    modbus_close(&host->connection);
#else
    modbus_close(host->connection);
    modbus_free(host->connection);
#endif
    host->connection = NULL;
    return -1;
  }

  DEBUG("Modbus plugin: mb_read_data: Success! "
        "modbus_read_registers returned with status %i.",
        status);

  if (data->register_type == REG_TYPE_FLOAT) {
    float float_value;
    value_t vt;

    float_value = mb_register_to_float(values[0], values[1]);
    DEBUG("Modbus plugin: mb_read_data: "
          "Returned float value is %g",
          (double)float_value);

    CAST_TO_VALUE_T(ds, vt, float_value, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else if (data->register_type == REG_TYPE_FLOAT_CDAB) {
    float float_value;
    value_t vt;

    float_value = mb_register_to_float(values[1], values[0]);
    DEBUG("Modbus plugin: mb_read_data: "
          "Returned float value is %g",
          (double)float_value);

    CAST_TO_VALUE_T(ds, vt, float_value, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else if (data->register_type == REG_TYPE_DOUBLE) {
    double double_value;
    value_t vt;

    double_value =
        mb_register_to_double(values[0], values[1], values[2], values[3]);
    DEBUG("Modbus plugin: mb_read_data: "
          "Returned double value is %g",
          double_value);

    CAST_TO_VALUE_T(ds, vt, double_value, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else if (data->register_type == REG_TYPE_INT32) {
    union {
      uint32_t u32;
      int32_t i32;
    } v;
    value_t vt;

    v.u32 = (((uint32_t)values[0]) << 16) | ((uint32_t)values[1]);
    DEBUG("Modbus plugin: mb_read_data: "
          "Returned int32 value is %" PRIi32,
          v.i32);

    CAST_TO_VALUE_T(ds, vt, v.i32, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else if (data->register_type == REG_TYPE_INT32_CDAB) {
    union {
      uint32_t u32;
      int32_t i32;
    } v;
    value_t vt;

    v.u32 = (((uint32_t)values[1]) << 16) | ((uint32_t)values[0]);
    DEBUG("Modbus plugin: mb_read_data: "
          "Returned int32 value is %" PRIi32,
          v.i32);

    CAST_TO_VALUE_T(ds, vt, v.i32, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else if (data->register_type == REG_TYPE_INT16) {
    union {
      uint16_t u16;
      int16_t i16;
    } v;
    value_t vt;

    v.u16 = values[0];

    DEBUG("Modbus plugin: mb_read_data: "
          "Returned int16 value is %" PRIi16,
          v.i16);

    CAST_TO_VALUE_T(ds, vt, v.i16, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else if (data->register_type == REG_TYPE_UINT32) {
    uint32_t v32;
    value_t vt;

    v32 = (((uint32_t)values[0]) << 16) | ((uint32_t)values[1]);
    DEBUG("Modbus plugin: mb_read_data: "
          "Returned uint32 value is %" PRIu32,
          v32);

    CAST_TO_VALUE_T(ds, vt, v32, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else if (data->register_type == REG_TYPE_UINT32_CDAB) {
    uint32_t v32;
    value_t vt;

    v32 = (((uint32_t)values[1]) << 16) | ((uint32_t)values[0]);
    DEBUG("Modbus plugin: mb_read_data: "
          "Returned uint32 value is %" PRIu32,
          v32);

    CAST_TO_VALUE_T(ds, vt, v32, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else if (data->register_type == REG_TYPE_UINT64) {
    uint64_t v64;
    value_t vt;

    v64 = (((uint64_t)values[0]) << 48) | (((uint64_t)values[1]) << 32) |
          (((uint64_t)values[2]) << 16) | (((uint64_t)values[3]));
    DEBUG("Modbus plugin: mb_read_data: "
          "Returned uint64 value is %" PRIu64,
          v64);

    CAST_TO_VALUE_T(ds, vt, v64, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else if (data->register_type == REG_TYPE_INT64) {
    union {
      uint64_t u64;
      int64_t i64;
    } v;
    value_t vt;

    v.u64 = (((uint64_t)values[0]) << 48) | (((uint64_t)values[1]) << 32) |
            (((uint64_t)values[2]) << 16) | ((uint64_t)values[3]);
    DEBUG("Modbus plugin: mb_read_data: "
          "Returned uint64 value is %" PRIi64,
          v.i64);

    CAST_TO_VALUE_T(ds, vt, v.i64, data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  } else /* if (data->register_type == REG_TYPE_UINT16) */
  {
    value_t vt;

    DEBUG("Modbus plugin: mb_read_data: "
          "Returned uint16 value is %" PRIu16,
          values[0]);

    CAST_TO_VALUE_T(ds, vt, values[0], data->scale, data->shift);
    mb_submit(host, slave, data, vt);
  }

  return 0;
} /* }}} int mb_read_data */

static int mb_read_slave(mb_host_t *host, mb_slave_t *slave) /* {{{ */
{
  int success;
  int status;

  if ((host == NULL) || (slave == NULL))
    return EINVAL;

  success = 0;
  for (mb_data_t *data = slave->collect; data != NULL; data = data->next) {
    status = mb_read_data(host, slave, data);
    if (status == 0)
      success++;
  }

  if (success == 0)
    return -1;
  else
    return 0;
} /* }}} int mb_read_slave */

static int mb_read(user_data_t *user_data) /* {{{ */
{
  mb_host_t *host;
  int success;
  int status;

  if ((user_data == NULL) || (user_data->data == NULL))
    return EINVAL;

  host = user_data->data;

  success = 0;
  for (size_t i = 0; i < host->slaves_num; i++) {
    status = mb_read_slave(host, host->slaves + i);
    if (status == 0)
      success++;
  }

  if (success == 0)
    return -1;
  else
    return 0;
} /* }}} int mb_read */

/* Free functions */

static void data_free_one(mb_data_t *data) /* {{{ */
{
  if (data == NULL)
    return;

  sfree(data->name);
  sfree(data);
} /* }}} void data_free_one */

static void data_free_all(mb_data_t *data) /* {{{ */
{
  mb_data_t *next;

  if (data == NULL)
    return;

  next = data->next;
  data_free_one(data);

  data_free_all(next);
} /* }}} void data_free_all */

static void slaves_free_all(mb_slave_t *slaves, size_t slaves_num) /* {{{ */
{
  if (slaves == NULL)
    return;

  for (size_t i = 0; i < slaves_num; i++)
    data_free_all(slaves[i].collect);
  sfree(slaves);
} /* }}} void slaves_free_all */

static void host_free(void *void_host) /* {{{ */
{
  mb_host_t *host = void_host;

  if (host == NULL)
    return;

  slaves_free_all(host->slaves, host->slaves_num);
  sfree(host);
} /* }}} void host_free */

/* Config functions */

static int mb_config_add_data(oconfig_item_t *ci) /* {{{ */
{
  mb_data_t data = {0};
  int status;

  data.name = NULL;
  data.register_type = REG_TYPE_UINT16;
  data.next = NULL;
  data.scale = 1;
  data.shift = 0;

  status = cf_util_get_string(ci, &data.name);
  if (status != 0)
    return status;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Type", child->key) == 0)
      status = cf_util_get_string_buffer(child, data.type, sizeof(data.type));
    else if (strcasecmp("Instance", child->key) == 0)
      status = cf_util_get_string_buffer(child, data.instance,
                                         sizeof(data.instance));
    else if (strcasecmp("Scale", child->key) == 0)
      status = cf_util_get_double(child, &data.scale);
    else if (strcasecmp("Shift", child->key) == 0)
      status = cf_util_get_double(child, &data.shift);
    else if (strcasecmp("RegisterBase", child->key) == 0)
      status = cf_util_get_int(child, &data.register_base);
    else if (strcasecmp("RegisterType", child->key) == 0) {
      char tmp[16];
      status = cf_util_get_string_buffer(child, tmp, sizeof(tmp));
      if (status != 0)
        /* do nothing */;
      else if (strcasecmp("Int16", tmp) == 0)
        data.register_type = REG_TYPE_INT16;
      else if (strcasecmp("Int32", tmp) == 0)
        data.register_type = REG_TYPE_INT32;
      else if (strcasecmp("Int32LE", tmp) == 0)
        data.register_type = REG_TYPE_INT32_CDAB;
      else if (strcasecmp("Uint16", tmp) == 0)
        data.register_type = REG_TYPE_UINT16;
      else if (strcasecmp("Uint32", tmp) == 0)
        data.register_type = REG_TYPE_UINT32;
      else if (strcasecmp("Uint32LE", tmp) == 0)
        data.register_type = REG_TYPE_UINT32_CDAB;
      else if (strcasecmp("Float", tmp) == 0)
        data.register_type = REG_TYPE_FLOAT;
      else if (strcasecmp("FloatLE", tmp) == 0)
        data.register_type = REG_TYPE_FLOAT_CDAB;
      else if (strcasecmp("Uint64", tmp) == 0)
        data.register_type = REG_TYPE_UINT64;
      else if (strcasecmp("Int64", tmp) == 0)
        data.register_type = REG_TYPE_INT64;
      else if (strcasecmp("Double", tmp) == 0)
        data.register_type = REG_TYPE_DOUBLE;
      else {
        ERROR("Modbus plugin: The register type \"%s\" is unknown.", tmp);
        status = -1;
      }
    } else if (strcasecmp("RegisterCmd", child->key) == 0) {
#if LEGACY_LIBMODBUS
      ERROR("Modbus plugin: RegisterCmd parameter can not be used "
            "with your libmodbus version");
#else
      char tmp[16];
      status = cf_util_get_string_buffer(child, tmp, sizeof(tmp));
      if (status != 0)
        /* do nothing */;
      else if (strcasecmp("ReadHolding", tmp) == 0)
        data.modbus_register_type = MREG_HOLDING;
      else if (strcasecmp("ReadInput", tmp) == 0)
        data.modbus_register_type = MREG_INPUT;
      else {
        ERROR("Modbus plugin: The modbus_register_type \"%s\" is unknown.",
              tmp);
        status = -1;
      }
#endif
    } else {
      ERROR("Modbus plugin: Unknown configuration option: %s", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  assert(data.name != NULL);
  if (data.type[0] == 0) {
    ERROR("Modbus plugin: Data block \"%s\": No type has been specified.",
          data.name);
    status = -1;
  }

  if (status == 0)
    data_copy(&data_definitions, &data);

  sfree(data.name);

  return status;
} /* }}} int mb_config_add_data */

static int mb_config_set_host_address(mb_host_t *host, /* {{{ */
                                      const char *address) {
  struct addrinfo *ai_list;
  int status;

  if ((host == NULL) || (address == NULL))
    return EINVAL;

  struct addrinfo ai_hints = {
      /* XXX: libmodbus can only handle IPv4 addresses. */
      .ai_family = AF_INET,
      .ai_flags = AI_ADDRCONFIG};

  status = getaddrinfo(address, /* service = */ NULL, &ai_hints, &ai_list);
  if (status != 0) {
    ERROR("Modbus plugin: getaddrinfo failed: %s",
          (status == EAI_SYSTEM) ? STRERRNO : gai_strerror(status));
    return status;
  }

  for (struct addrinfo *ai_ptr = ai_list; ai_ptr != NULL;
       ai_ptr = ai_ptr->ai_next) {
    status = getnameinfo(ai_ptr->ai_addr, ai_ptr->ai_addrlen, host->node,
                         sizeof(host->node),
                         /* service = */ NULL, /* length = */ 0,
                         /* flags = */ NI_NUMERICHOST);
    if (status == 0)
      break;
  } /* for (ai_ptr) */

  freeaddrinfo(ai_list);

  if (status != 0)
    ERROR("Modbus plugin: Unable to translate node name: \"%s\"", address);
  else /* if (status == 0) */
  {
    DEBUG("Modbus plugin: mb_config_set_host_address: %s -> %s", address,
          host->node);
  }

  return status;
} /* }}} int mb_config_set_host_address */

static int mb_config_add_slave(mb_host_t *host, oconfig_item_t *ci) /* {{{ */
{
  mb_slave_t *slave;
  int status;

  if ((host == NULL) || (ci == NULL))
    return EINVAL;

  slave = realloc(host->slaves, sizeof(*slave) * (host->slaves_num + 1));
  if (slave == NULL)
    return ENOMEM;
  host->slaves = slave;
  slave = host->slaves + host->slaves_num;
  memset(slave, 0, sizeof(*slave));
  slave->collect = NULL;

  status = cf_util_get_int(ci, &slave->id);
  if (status != 0)
    return status;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Instance", child->key) == 0)
      status = cf_util_get_string_buffer(child, slave->instance,
                                         sizeof(slave->instance));
    else if (strcasecmp("Collect", child->key) == 0) {
      char buffer[1024];
      status = cf_util_get_string_buffer(child, buffer, sizeof(buffer));
      if (status == 0)
        data_copy_by_name(&slave->collect, data_definitions, buffer);
      status = 0; /* continue after failure. */
    } else {
      ERROR("Modbus plugin: Unknown configuration option: %s", child->key);
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
    data_free_all(slave->collect);

  return status;
} /* }}} int mb_config_add_slave */

static int mb_config_add_host(oconfig_item_t *ci) /* {{{ */
{
  cdtime_t interval = 0;
  mb_host_t *host;
  int status;

  host = calloc(1, sizeof(*host));
  if (host == NULL)
    return ENOMEM;
  host->slaves = NULL;

  status = cf_util_get_string_buffer(ci, host->host, sizeof(host->host));
  if (status != 0) {
    sfree(host);
    return status;
  }
  if (host->host[0] == 0) {
    sfree(host);
    return EINVAL;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    status = 0;

    if (strcasecmp("Address", child->key) == 0) {
      char buffer[NI_MAXHOST];
      status = cf_util_get_string_buffer(child, buffer, sizeof(buffer));
      if (status == 0)
        status = mb_config_set_host_address(host, buffer);
      if (status == 0)
        host->conntype = MBCONN_TCP;
    } else if (strcasecmp("Port", child->key) == 0) {
      host->port = cf_util_get_port_number(child);
      if (host->port <= 0)
        status = -1;
    } else if (strcasecmp("Device", child->key) == 0) {
      status = cf_util_get_string_buffer(child, host->node, sizeof(host->node));
      if (status == 0) {
        host->conntype = MBCONN_RTU;
        host->uarttype = UARTTYPE_RS232;
      }
    } else if (strcasecmp("Baudrate", child->key) == 0)
      status = cf_util_get_int(child, &host->baudrate);
    else if (strcasecmp("UARTType", child->key) == 0) {
#if defined(linux) && !LEGACY_LIBMODBUS && LIBMODBUS_VERSION_CHECK(2, 9, 4)
      char buffer[NI_MAXHOST];
      status = cf_util_get_string_buffer(child, buffer, sizeof(buffer));
      if (status != 0)
        break;
      if (strncmp(buffer, "RS485", 6) == 0)
        host->uarttype = UARTTYPE_RS485;
      else if (strncmp(buffer, "RS422", 6) == 0)
        host->uarttype = UARTTYPE_RS422;
      else if (strncmp(buffer, "RS232", 6) == 0)
        host->uarttype = UARTTYPE_RS232;
      else {
        ERROR("Modbus plugin: The UARTType \"%s\" is unknown.", buffer);
        status = -1;
        break;
      }
#else
      ERROR("Modbus plugin: Option `UARTType' not supported. Please "
            "upgrade libmodbus to at least 2.9.4");
      return -1;
#endif
    } else if (strcasecmp("Interval", child->key) == 0)
      status = cf_util_get_cdtime(child, &interval);
    else if (strcasecmp("Slave", child->key) == 0)
      /* Don't set status: Gracefully continue if a slave fails. */
      mb_config_add_slave(host, child);
    else {
      ERROR("Modbus plugin: Unknown configuration option: %s", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (i = 0; i < ci->children_num; i++) */

  assert(host->host[0] != 0);
  if (host->node[0] == 0) {
    ERROR("Modbus plugin: Data block \"%s\": No address or device has been "
          "specified.",
          host->host);
    status = -1;
  }
  if (host->conntype == MBCONN_RTU && !host->baudrate) {
    ERROR("Modbus plugin: Data block \"%s\": No serial baudrate has been "
          "specified.",
          host->host);
    status = -1;
  }
  if ((host->conntype == MBCONN_TCP && host->baudrate) ||
      (host->conntype == MBCONN_RTU && host->port)) {
    ERROR("Modbus plugin: Data block \"%s\": You've mixed up RTU and TCP "
          "options.",
          host->host);
    status = -1;
  }

  if (status == 0) {
    char name[1024];

    ssnprintf(name, sizeof(name), "modbus-%s", host->host);

    plugin_register_complex_read(/* group = */ NULL, name,
                                 /* callback = */ mb_read,
                                 /* interval = */ interval,
                                 &(user_data_t){
                                     .data = host,
                                     .free_func = host_free,
                                 });
  } else {
    host_free(host);
  }

  return status;
} /* }}} int mb_config_add_host */

static int mb_config(oconfig_item_t *ci) /* {{{ */
{
  if (ci == NULL)
    return EINVAL;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Data", child->key) == 0)
      mb_config_add_data(child);
    else if (strcasecmp("Host", child->key) == 0)
      mb_config_add_host(child);
    else
      ERROR("Modbus plugin: Unknown configuration option: %s", child->key);
  }

  return 0;
} /* }}} int mb_config */

/* ========= */

static int mb_shutdown(void) /* {{{ */
{
  data_free_all(data_definitions);
  data_definitions = NULL;

  return 0;
} /* }}} int mb_shutdown */

void module_register(void) {
  plugin_register_complex_config("modbus", mb_config);
  plugin_register_shutdown("modbus", mb_shutdown);
} /* void module_register */
