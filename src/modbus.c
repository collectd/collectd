/**
 * collectd - src/modbus.c
 * Copyright (C) 2010  noris network AG
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
 *   Florian Forster <octo at noris.net>
 **/

#include "collectd.h"
#include "plugin.h"
#include "configfile.h"

#include <modbus/modbus.h>

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
enum mb_register_type_e
{
  REG_TYPE_UINT16,
  REG_TYPE_UINT32,
  REG_TYPE_FLOAT
};
typedef enum mb_register_type_e mb_register_type_t;

struct mb_data_s;
typedef struct mb_data_s mb_data_t;
struct mb_data_s
{
  char *name;
  int register_base;
  mb_register_type_t register_type;
  char type[DATA_MAX_NAME_LEN];
  char instance[DATA_MAX_NAME_LEN];

  mb_data_t *next;
};

struct mb_slave_s
{
  int id;
  char instance[DATA_MAX_NAME_LEN];
  mb_data_t *collect;
};
typedef struct mb_slave_s mb_slave_t;

struct mb_host_s
{
  char host[DATA_MAX_NAME_LEN];
  char node[NI_MAXHOST];
  char service[NI_MAXSERV];
  int interval;

  mb_slave_t *slaves;
  size_t slaves_num;

  modbus_param_t connection;
}

struct mb_data_group_s;
typedef struct mb_data_group_s mb_data_group_t;
struct mb_data_group_s
{
  mb_data_t *registers;
  size_t registers_num;

  mb_data_group_t *next;
};

/*
 * Global variables
 */
static mb_data_t *data_definitions = NULL;

/*
 * Functions
 */
static mb_data_t *data_get_by_name (const mb_data_t *src, const char *name) /* {{{ */
{
  mb_data_t *ptr;

  if (name == NULL)
    return (NULL);

  for (ptr = src; ptr != NULL; ptr = ptr->next)
    if (strcasecmp (ptr->name, name) == 0)
      return (ptr);

  return (NULL);
} /* }}} mb_data_t *data_get_by_name */

static int data_append (mb_data_t **dst, const mb_data_t *src) /* {{{ */
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

  tmp->name = sstrdup (src->name);
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
static int data_copy_by_name (mb_data_t **dst, const mb_data_t *src, /* {{{ */
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

static int mb_config_add_data (oconfig_item_t *ci) /* {{{ */
{
  mb_data_t data;
  int status;
  int i;

  memset (&data, 0, sizeof (data));

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

  /* TODO: Validate the struct. */

  data_copy (&data_definitions, &data);
  sfree (data.name);

  return (0);
} /* }}} int mb_config_add_data */

static void mb_free_internal (mb_internal_t *i) /* {{{ */
{
  if (i == NULL)
    return;

  free (i->connection);
  free (i);
} /* }}} void mb_free_internal */

static mb_internal_t *mb_alloc_internal (void) /* {{{ */
{
  mb_internal_t *ret;

  ret = malloc (sizeof (*ret));
  if (ret == NULL)
    return (NULL);
  memset (ret, 0, sizeof (*ret));

  ret->connection = calloc (1, sizeof (*ret->connection));
  if (ret->connection == NULL)
  {
    mb_free_internal (ret);
    return (NULL);
  }

  return (ret);
} /* }}} mb_internal_t *mb_alloc_internal */

static mb_internal_t *mb_init (void) /* {{{ */
{
  mb_internal_t *ret;
  int status;

  ret = mb_alloc_internal ();
  if (ret == NULL)
    return (NULL);

  modbus_set_debug (ret->connection, 1);

  /* We'll do the error handling ourselves. */
  modbus_set_error_handling (ret->connection, NOP_ON_ERROR);

  modbus_init_tcp (ret->connection,
      /* host = */ "172.18.20.30", /* FIXME: Only IP adresses allowed. -> convert hostnames. */
      /* post = */ MODBUS_TCP_DEFAULT_PORT); /* FIXME: Use configured port. */

  status = modbus_connect (ret->connection);
  printf ("mb_init: modbus_connect returned status %i\n", status);
  if (status != 0)
  {
    mb_free_internal (ret);
    return (NULL);
  }

  return (ret);
} /* }}} mb_internal_t *mb_init */

static float mb_register_to_float (uint16_t hi, uint16_t lo) /* {{{ */
{
  union
  {
    uint8_t b[4];
    float f;
  } conv;

#if 1
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

static int mb_read (mb_internal_t *dev, int register_addr, _Bool is_float) /* {{{ */
{
  int status;
  uint16_t values[2];
  int values_num;

  if (dev == NULL)
    return (EINVAL);

  printf ("mb_read (addr = %i, float = %s);\n", register_addr,
      is_float ? "true" : "false");

  memset (values, 0, sizeof (values));
  if (is_float)
    values_num = 2;
  else
    values_num = 1;

  status = read_holding_registers (dev->connection,
      /* slave = */ 1, /* start_addr = */ register_addr,
      /* num_registers = */ values_num, /* buffer = */ values);
  printf ("read_coil_status returned with status %i\n", status);
  if (status <= 0)
    return (EAGAIN);

  if (is_float)
  {
    float value = mb_register_to_float (values[0], values[1]);
    printf ("read_coil_status returned value %g (hi %#"PRIx16", lo %#"PRIx16")\n",
        value, values[0], values[1]);
  }
  else
  {
    printf ("read_coil_status returned value %"PRIu16"\n", values[0]);
  }

  return (0);
} /* }}} int mb_read */

static int mb_shutdown (mb_internal_t *dev) /* {{{ */
{
  if ((dev != NULL) && (dev->connection != NULL))
    modbus_close (dev->connection);
  mb_free_internal (dev);

  return (0);
} /* }}} int mb_shutdown */

int main (int argc, char **argv) /* {{{ */
{
  mb_internal_t *dev;
  /* int j; */

  argc = 0; argv = NULL;

  dev = mb_init ();

#if 0
  for (j = 1; j < argc; j++)
  {
    mb_read (dev, atoi (argv[j]));
  }
#endif
  mb_read (dev, 4096+3, /* is_float = */ 0);
  mb_read (dev, 2*3, /* is_float = */ 1);

  mb_shutdown (dev);

  exit (EXIT_SUCCESS);
} /* }}} int main */

void module_register (void)
{
  plugin_register_complex_config ("modbus", mb_config);
  plugin_register_init ("modbus", mv_init);
  plugin_register_shutdown ("modbus", mv_shutdown);
} /* void module_register */

/* vim: set sw=2 sts=2 et fdm=marker : */
