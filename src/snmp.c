/**
 * collectd - src/snmp.c
 * Copyright (C) 2007  Florian octo Forster
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
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <pthread.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

/*
 * Private data structes
 */
struct oid_s
{
  oid oid[MAX_OID_LEN];
  size_t oid_len;
};
typedef struct oid_s oid_t;

union instance_u
{
  char  string[DATA_MAX_NAME_LEN];
  oid_t oid;
};
typedef union instance_u instance_t;

struct data_definition_s
{
  char *name; /* used to reference this from the `Collect' option */
  char *type; /* used to find the data_set */
  int is_table;
  instance_t instance;
  oid_t *values;
  int values_len;
  struct data_definition_s *next;
};
typedef struct data_definition_s data_definition_t;

struct host_definition_s
{
  char *name;
  char *address;
  char *community;
  int version;
  void *sess_handle;
  int16_t skip_num;
  int16_t skip_left;
  data_definition_t **data_list;
  int data_list_len;
  enum          /****************************************************/
  {             /* This host..                                      */
    STATE_IDLE, /* - just sits there until `skip_left < interval_g' */
    STATE_WAIT, /* - waits to be queried.                           */
    STATE_BUSY  /* - is currently being queried.                    */
  } state;      /****************************************************/
  struct host_definition_s *next;
};
typedef struct host_definition_s host_definition_t;

/* These two types are used to cache values in `csnmp_read_table' to handle
 * gaps in tables. */
struct csnmp_list_instances_s
{
  oid subid;
  char instance[DATA_MAX_NAME_LEN];
  struct csnmp_list_instances_s *next;
};
typedef struct csnmp_list_instances_s csnmp_list_instances_t;

struct csnmp_table_values_s
{
  oid subid;
  value_t value;
  struct csnmp_table_values_s *next;
};
typedef struct csnmp_table_values_s csnmp_table_values_t;

/*
 * Private variables
 */
static int do_shutdown = 0;

pthread_t *threads = NULL;
int threads_num = 0;

static data_definition_t *data_head = NULL;
static host_definition_t *host_head = NULL;

static pthread_mutex_t host_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t  host_cond = PTHREAD_COND_INITIALIZER;

/*
 * Private functions
 */
/* First there are many functions which do configuration stuff. It's a big
 * bloated and messy, I'm afraid. */

/*
 * Callgraph for the config stuff:
 *  csnmp_config
 *  +-> call_snmp_init_once
 *  +-> csnmp_config_add_data
 *  !   +-> csnmp_config_add_data_type
 *  !   +-> csnmp_config_add_data_table
 *  !   +-> csnmp_config_add_data_instance
 *  !   +-> csnmp_config_add_data_values
 *  +-> csnmp_config_add_host
 *      +-> csnmp_config_add_host_address
 *      +-> csnmp_config_add_host_community
 *      +-> csnmp_config_add_host_version
 *      +-> csnmp_config_add_host_collect
 *      +-> csnmp_config_add_host_interval
 */
static void call_snmp_init_once (void)
{
  static int have_init = 0;

  if (have_init == 0)
    init_snmp (PACKAGE_NAME);
  have_init = 1;
} /* void call_snmp_init_once */

static int csnmp_config_add_data_type (data_definition_t *dd, oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("snmp plugin: `Type' needs exactly one string argument.");
    return (-1);
  }

  if (dd->type != NULL)
    free (dd->type);

  dd->type = strdup (ci->values[0].value.string);
  if (dd->type == NULL)
    return (-1);

  return (0);
} /* int csnmp_config_add_data_type */

static int csnmp_config_add_data_table (data_definition_t *dd, oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    WARNING ("snmp plugin: `Table' needs exactly one boolean argument.");
    return (-1);
  }

  dd->is_table = ci->values[0].value.boolean ? 1 : 0;

  return (0);
} /* int csnmp_config_add_data_table */

static int csnmp_config_add_data_instance (data_definition_t *dd, oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("snmp plugin: `Instance' needs exactly one string argument.");
    return (-1);
  }

  if (dd->is_table)
  {
    /* Instance is an OID */
    dd->instance.oid.oid_len = MAX_OID_LEN;

    if (!read_objid (ci->values[0].value.string,
	  dd->instance.oid.oid, &dd->instance.oid.oid_len))
    {
      ERROR ("snmp plugin: read_objid (%s) failed.",
	  ci->values[0].value.string);
      return (-1);
    }
  }
  else
  {
    /* Instance is a simple string */
    strncpy (dd->instance.string, ci->values[0].value.string, DATA_MAX_NAME_LEN - 1);
  }

  return (0);
} /* int csnmp_config_add_data_instance */

static int csnmp_config_add_data_values (data_definition_t *dd, oconfig_item_t *ci)
{
  int i;

  if (ci->values_num < 1)
  {
    WARNING ("snmp plugin: `Values' needs at least one argument.");
    return (-1);
  }

  for (i = 0; i < ci->values_num; i++)
    if (ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("snmp plugin: `Values' needs only string argument.");
      return (-1);
    }

  if (dd->values != NULL)
    free (dd->values);
  dd->values = (oid_t *) malloc (sizeof (oid_t) * ci->values_num);
  if (dd->values == NULL)
    return (-1);
  dd->values_len = ci->values_num;

  for (i = 0; i < ci->values_num; i++)
  {
    dd->values[i].oid_len = MAX_OID_LEN;

    if (NULL == snmp_parse_oid (ci->values[i].value.string,
	  dd->values[i].oid, &dd->values[i].oid_len))
    {
      ERROR ("snmp plugin: snmp_parse_oid (%s) failed.",
	  ci->values[i].value.string);
      free (dd->values);
      dd->values = NULL;
      dd->values_len = 0;
      return (-1);
    }
  }

  return (0);
} /* int csnmp_config_add_data_instance */

static int csnmp_config_add_data (oconfig_item_t *ci)
{
  data_definition_t *dd;
  int status = 0;
  int i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("snmp plugin: The `Data' config option needs exactly one string argument.");
    return (-1);
  }

  dd = (data_definition_t *) malloc (sizeof (data_definition_t));
  if (dd == NULL)
    return (-1);
  memset (dd, '\0', sizeof (data_definition_t));

  dd->name = strdup (ci->values[0].value.string);
  if (dd->name == NULL)
  {
    free (dd);
    return (-1);
  }

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Type", option->key) == 0)
      status = csnmp_config_add_data_type (dd, option);
    else if (strcasecmp ("Table", option->key) == 0)
      status = csnmp_config_add_data_table (dd, option);
    else if (strcasecmp ("Instance", option->key) == 0)
      status = csnmp_config_add_data_instance (dd, option);
    else if (strcasecmp ("Values", option->key) == 0)
      status = csnmp_config_add_data_values (dd, option);
    else
    {
      WARNING ("snmp plugin: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (ci->children) */

  while (status == 0)
  {
    if (dd->type == NULL)
    {
      WARNING ("snmp plugin: `Type' not given for data `%s'", dd->name);
      status = -1;
      break;
    }
    if (dd->values == NULL)
    {
      WARNING ("snmp plugin: No `Value' given for data `%s'", dd->name);
      status = -1;
      break;
    }

    break;
  } /* while (status == 0) */

  if (status != 0)
  {
    sfree (dd->name);
    sfree (dd->values);
    sfree (dd);
    return (-1);
  }

  DEBUG ("snmp plugin: dd = { name = %s, type = %s, is_table = %s, values_len = %i }",
      dd->name, dd->type, (dd->is_table != 0) ? "true" : "false", dd->values_len);

  if (data_head == NULL)
    data_head = dd;
  else
  {
    data_definition_t *last;
    last = data_head;
    while (last->next != NULL)
      last = last->next;
    last->next = dd;
  }

  return (0);
} /* int csnmp_config_add_data */

static int csnmp_config_add_host_address (host_definition_t *hd, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("snmp plugin: The `Address' config option needs exactly one string argument.");
    return (-1);
  }

  if (hd->address == NULL)
    free (hd->address);

  hd->address = strdup (ci->values[0].value.string);
  if (hd->address == NULL)
    return (-1);

  DEBUG ("snmp plugin: host = %s; host->address = %s;",
      hd->name, hd->address);

  return (0);
} /* int csnmp_config_add_host_address */

static int csnmp_config_add_host_community (host_definition_t *hd, oconfig_item_t *ci)
{
  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("snmp plugin: The `Community' config option needs exactly one string argument.");
    return (-1);
  }

  if (hd->community == NULL)
    free (hd->community);

  hd->community = strdup (ci->values[0].value.string);
  if (hd->community == NULL)
    return (-1);

  DEBUG ("snmp plugin: host = %s; host->community = %s;",
      hd->name, hd->community);

  return (0);
} /* int csnmp_config_add_host_community */

static int csnmp_config_add_host_version (host_definition_t *hd, oconfig_item_t *ci)
{
  int version;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    WARNING ("snmp plugin: The `Version' config option needs exactly one number argument.");
    return (-1);
  }

  version = (int) ci->values[0].value.number;
  if ((version != 1) && (version != 2))
  {
    WARNING ("snmp plugin: `Version' must either be `1' or `2'.");
    return (-1);
  }

  hd->version = version;

  return (0);
} /* int csnmp_config_add_host_address */

static int csnmp_config_add_host_collect (host_definition_t *host,
    oconfig_item_t *ci)
{
  data_definition_t *data;
  data_definition_t **data_list;
  int data_list_len;
  int i;

  if (ci->values_num < 1)
  {
    WARNING ("snmp plugin: `Collect' needs at least one argument.");
    return (-1);
  }

  for (i = 0; i < ci->values_num; i++)
    if (ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("snmp plugin: All arguments to `Collect' must be strings.");
      return (-1);
    }

  data_list_len = host->data_list_len + ci->values_num;
  data_list = (data_definition_t **) realloc (host->data_list,
      sizeof (data_definition_t *) * data_list_len);
  if (data_list == NULL)
    return (-1);
  host->data_list = data_list;

  for (i = 0; i < ci->values_num; i++)
  {
    for (data = data_head; data != NULL; data = data->next)
      if (strcasecmp (ci->values[i].value.string, data->name) == 0)
	break;

    if (data == NULL)
    {
      WARNING ("snmp plugin: No such data configured: `%s'",
	  ci->values[i].value.string);
      continue;
    }

    DEBUG ("snmp plugin: Collect: host = %s, data[%i] = %s;",
	host->name, host->data_list_len, data->name);

    host->data_list[host->data_list_len] = data;
    host->data_list_len++;
  } /* for (values_num) */

  return (0);
} /* int csnmp_config_add_host_collect */

static int csnmp_config_add_host_interval (host_definition_t *hd, oconfig_item_t *ci)
{
  int interval;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_NUMBER))
  {
    WARNING ("snmp plugin: The `Interval' config option needs exactly one number argument.");
    return (-1);
  }

  interval = (int) ci->values[0].value.number;
  hd->skip_num = interval;
  if (hd->skip_num < 0)
    hd->skip_num = 0;

  return (0);
} /* int csnmp_config_add_host_interval */

static int csnmp_config_add_host (oconfig_item_t *ci)
{
  host_definition_t *hd;
  int status = 0;
  int i;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("snmp plugin: `Host' needs exactly one string argument.");
    return (-1);
  }

  hd = (host_definition_t *) malloc (sizeof (host_definition_t));
  if (hd == NULL)
    return (-1);
  memset (hd, '\0', sizeof (host_definition_t));
  hd->version = 2;

  hd->name = strdup (ci->values[0].value.string);
  if (hd->name == NULL)
  {
    free (hd);
    return (-1);
  }

  hd->sess_handle = NULL;
  hd->skip_num = 0;
  hd->skip_left = 0;
  hd->state = STATE_IDLE;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Address", option->key) == 0)
      status = csnmp_config_add_host_address (hd, option);
    else if (strcasecmp ("Community", option->key) == 0)
      status = csnmp_config_add_host_community (hd, option);
    else if (strcasecmp ("Version", option->key) == 0)
      status = csnmp_config_add_host_version (hd, option);
    else if (strcasecmp ("Collect", option->key) == 0)
      csnmp_config_add_host_collect (hd, option);
    else if (strcasecmp ("Interval", option->key) == 0)
      csnmp_config_add_host_interval (hd, option);
    else
    {
      WARNING ("snmp plugin: csnmp_config_add_host: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (ci->children) */

  while (status == 0)
  {
    if (hd->address == NULL)
    {
      WARNING ("snmp plugin: `Address' not given for host `%s'", hd->name);
      status = -1;
      break;
    }
    if (hd->community == NULL)
    {
      WARNING ("snmp plugin: `Community' not given for host `%s'", hd->name);
      status = -1;
      break;
    }

    break;
  } /* while (status == 0) */

  if (status != 0)
  {
    sfree (hd->name);
    sfree (hd);
    return (-1);
  }

  DEBUG ("snmp plugin: hd = { name = %s, address = %s, community = %s, version = %i }",
      hd->name, hd->address, hd->community, hd->version);

  if (host_head == NULL)
    host_head = hd;
  else
  {
    host_definition_t *last;
    last = host_head;
    while (last->next != NULL)
      last = last->next;
    last->next = hd;
  }

  return (0);
} /* int csnmp_config_add_host */

static int csnmp_config (oconfig_item_t *ci)
{
  int i;

  call_snmp_init_once ();

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp ("Data", child->key) == 0)
      csnmp_config_add_data (child);
    else if (strcasecmp ("Host", child->key) == 0)
      csnmp_config_add_host (child);
    else
    {
      WARNING ("snmp plugin: Ignoring unknown config option `%s'.", child->key);
    }
  } /* for (ci->children) */

  return (0);
} /* int csnmp_config */

/* End of the config stuff. Now the interesting part begins */

static void csnmp_host_close_session (host_definition_t *host)
{
  int status;

  if (host->sess_handle == NULL)
    return;

  status = snmp_sess_close (host->sess_handle);

  if (status != 0)
  {
    char *errstr = NULL;

    snmp_sess_error (host->sess_handle, NULL, NULL, &errstr);

    ERROR ("snmp plugin: host %s: snmp_sess_close failed: %s",
	host->name, (errstr == NULL) ? "Unknown problem" : errstr);
    sfree (errstr);
  }

  host->sess_handle = NULL;
} /* void csnmp_host_close_session */

static void csnmp_host_open_session (host_definition_t *host)
{
  struct snmp_session sess;

  if (host->sess_handle != NULL)
    csnmp_host_close_session (host);

  snmp_sess_init (&sess);
  sess.peername = host->address;
  sess.community = (u_char *) host->community;
  sess.community_len = strlen (host->community);
  sess.version = (host->version == 1) ? SNMP_VERSION_1 : SNMP_VERSION_2c;

  /* snmp_sess_open will copy the `struct snmp_session *'. */
  host->sess_handle = snmp_sess_open (&sess);

  if (host->sess_handle == NULL)
  {
    char *errstr = NULL;

    snmp_error (&sess, NULL, NULL, &errstr);

    ERROR ("snmp plugin: host %s: snmp_sess_open failed: %s",
	host->name, (errstr == NULL) ? "Unknown problem" : errstr);
    sfree (errstr);
  }
} /* void csnmp_host_open_session */

static value_t csnmp_value_list_to_value (struct variable_list *vl, int type)
{
  value_t ret;
  uint64_t temp = 0;
  int defined = 1;

  if ((vl->type == ASN_INTEGER)
      || (vl->type == ASN_UINTEGER)
      || (vl->type == ASN_COUNTER)
#ifdef ASN_TIMETICKS
      || (vl->type == ASN_TIMETICKS)
#endif
      || (vl->type == ASN_GAUGE))
  {
    temp = (uint32_t) *vl->val.integer;
    DEBUG ("snmp plugin: Parsed int32 value is %llu.", temp);
  }
  else if (vl->type == ASN_COUNTER64)
  {
    temp = (uint32_t) vl->val.counter64->high;
    temp = temp << 32;
    temp += (uint32_t) vl->val.counter64->low;
    DEBUG ("snmp plugin: Parsed int64 value is %llu.", temp);
  }
  else
  {
    WARNING ("snmp plugin: I don't know the ASN type `%i'", (int) vl->type);
    defined = 0;
  }

  if (type == DS_TYPE_COUNTER)
  {
    ret.counter = temp;
  }
  else if (type == DS_TYPE_GAUGE)
  {
    ret.gauge = NAN;
    if (defined != 0)
      ret.gauge = temp;
  }

  return (ret);
} /* value_t csnmp_value_list_to_value */

static int csnmp_dispatch_table (host_definition_t *host, data_definition_t *data,
    csnmp_list_instances_t *instance_list,
    csnmp_table_values_t **value_table)
{
  const data_set_t *ds;
  value_list_t vl = VALUE_LIST_INIT;

  csnmp_list_instances_t *instance_list_ptr;
  csnmp_table_values_t **value_table_ptr;

  int i;

  ds = plugin_get_ds (data->type);
  if (!ds)
  {
    ERROR ("snmp plugin: DataSet `%s' not defined.", data->type);
    return (-1);
  }
  assert (ds->ds_num == data->values_len);

  value_table_ptr = (csnmp_table_values_t **) malloc (sizeof (csnmp_table_values_t *)
      * data->values_len);
  if (value_table_ptr == NULL)
    return (-1);
  for (i = 0; i < data->values_len; i++)
    value_table_ptr[i] = value_table[i];

  vl.values_len = ds->ds_num;
  vl.values = (value_t *) malloc (sizeof (value_t) * vl.values_len);
  if (vl.values == NULL)
  {
    sfree (value_table_ptr);
    return (-1);
  }

  strncpy (vl.host, host->name, sizeof (vl.host));
  vl.host[sizeof (vl.host) - 1] = '\0';
  strcpy (vl.plugin, "snmp");

  vl.interval = host->skip_num;
  vl.time = time (NULL);

  for (instance_list_ptr = instance_list;
      instance_list_ptr != NULL;
      instance_list_ptr = instance_list_ptr->next)
  {
    strncpy (vl.type_instance, instance_list_ptr->instance, sizeof (vl.type_instance));
    vl.type_instance[sizeof (vl.type_instance) - 1] = '\0';

    for (i = 0; i < data->values_len; i++)
    {
      while ((value_table_ptr[i] != NULL)
	  && (value_table_ptr[i]->subid < instance_list_ptr->subid))
	value_table_ptr[i] = value_table_ptr[i]->next;
      if ((value_table_ptr[i] == NULL)
	  || (value_table_ptr[i]->subid != instance_list_ptr->subid))
	break;
      vl.values[i] = value_table_ptr[i]->value;
    } /* for (data->values_len) */

    /* If the for-loop was aborted early, not all subid's match. */
    if (i < data->values_len)
    {
      DEBUG ("snmp plugin: host = %s; data = %s; i = %i; "
	  "Skipping SUBID %i",
	  host->name, data->name, i, instance_list_ptr->subid);
      continue;
    }

    /* If we get here `vl.type_instance' and all `vl.values' have been set */
    plugin_dispatch_values (data->type, &vl);
  } /* for (instance_list) */

  sfree (vl.values);
  sfree (value_table_ptr);

  return (0);
} /* int csnmp_dispatch_table */

static int csnmp_read_table (host_definition_t *host, data_definition_t *data)
{
  struct snmp_pdu *req;
  struct snmp_pdu *res;
  struct variable_list *vb;

  const data_set_t *ds;
  oid_t *oid_list;
  uint32_t oid_list_len;

  int status;
  int i;

  /* `value_table' and `value_table_ptr' implement a linked list for each
   * value. `instance_list' and `instance_list_ptr' implement a linked list of
   * instance names. This is used to jump gaps in the table. */
  csnmp_list_instances_t *instance_list;
  csnmp_list_instances_t *instance_list_ptr;
  csnmp_table_values_t **value_table;
  csnmp_table_values_t **value_table_ptr;

  DEBUG ("snmp plugin: csnmp_read_table (host = %s, data = %s)",
      host->name, data->name);

  if (host->sess_handle == NULL)
  {
    DEBUG ("snmp plugin: csnmp_read_table: host->sess_handle == NULL");
    return (-1);
  }

  ds = plugin_get_ds (data->type);
  if (!ds)
  {
    ERROR ("snmp plugin: DataSet `%s' not defined.", data->type);
    return (-1);
  }

  if (ds->ds_num != data->values_len)
  {
    ERROR ("snmp plugin: DataSet `%s' requires %i values, but config talks about %i",
	data->type, ds->ds_num, data->values_len);
    return (-1);
  }

  /* We need a copy of all the OIDs, because GETNEXT will destroy them. */
  oid_list_len = data->values_len + 1;
  oid_list = (oid_t *) malloc (sizeof (oid_t) * (oid_list_len));
  if (oid_list == NULL)
    return (-1);
  memcpy (oid_list, &data->instance.oid, sizeof (oid_t));
  for (i = 0; i < data->values_len; i++)
    memcpy (oid_list + (i + 1), data->values + i, sizeof (oid_t));

  /* Allocate the `value_table' */
  value_table = (csnmp_table_values_t **) malloc (sizeof (csnmp_table_values_t *)
      * 2 * data->values_len);
  if (value_table == NULL)
  {
    sfree (oid_list);
    return (-1);
  }
  memset (value_table, '\0', sizeof (csnmp_table_values_t *) * 2 * data->values_len);
  value_table_ptr = value_table + data->values_len;
  
  instance_list = NULL;
  instance_list_ptr = NULL;

  status = 0;
  while (status == 0)
  {
    csnmp_list_instances_t *il;

    req = snmp_pdu_create (SNMP_MSG_GETNEXT);
    if (req == NULL)
    {
      ERROR ("snmp plugin: snmp_pdu_create failed.");
      status = -1;
      break;
    }

    for (i = 0; i < oid_list_len; i++)
      snmp_add_null_var (req, oid_list[i].oid, oid_list[i].oid_len);

    status = snmp_sess_synch_response (host->sess_handle, req, &res);

    if (status != STAT_SUCCESS)
    {
      char *errstr = NULL;

      snmp_sess_error (host->sess_handle, NULL, NULL, &errstr);
      ERROR ("snmp plugin: host %s: snmp_sess_synch_response failed: %s",
	  host->name, (errstr == NULL) ? "Unknown problem" : errstr);
      csnmp_host_close_session (host);

      status = -1;
      break;
    }
    status = 0;
    assert (res != NULL);

    vb = res->variables;
    if (vb == NULL)
    {
      status = -1;
      break;
    }

    /* Check if we left the subtree */
    if (snmp_oid_ncompare (data->instance.oid.oid, data->instance.oid.oid_len,
	  vb->name, vb->name_length,
	  data->instance.oid.oid_len) != 0)
      break;

    /* Allocate a new `csnmp_list_instances_t', insert the instance name and
     * add it to the list */
    il = (csnmp_list_instances_t *) malloc (sizeof (csnmp_list_instances_t));
    if (il == NULL)
    {
      status = -1;
      break;
    }
    il->subid = vb->name[vb->name_length - 1];
    il->next = NULL;

    /* Get instance name */
    if ((vb->type == ASN_OCTET_STR) || (vb->type == ASN_BIT_STR))
    {
      char *ptr;
      size_t instance_len;

      instance_len = sizeof (il->instance) - 1;
      if (instance_len > vb->val_len)
	instance_len = vb->val_len;

      strncpy (il->instance, (char *) ((vb->type == ASN_OCTET_STR)
	    ? vb->val.string
	    : vb->val.bitstring),
	  instance_len);
      il->instance[instance_len] = '\0';

      for (ptr = il->instance; *ptr != '\0'; ptr++)
      {
	if ((*ptr > 0) && (*ptr < 32))
	  *ptr = ' ';
	else if (*ptr == '/')
	  *ptr = '_';
      }
      DEBUG ("snmp plugin: il->instance = `%s';", il->instance);
    }
    else
    {
      value_t val = csnmp_value_list_to_value (vb, DS_TYPE_COUNTER);
      snprintf (il->instance, sizeof (il->instance),
	  "%llu", val.counter);
    }
    il->instance[sizeof (il->instance) - 1] = '\0';
    DEBUG ("snmp plugin: data = `%s'; il->instance = `%s';",
	data->name, il->instance);

    if (instance_list_ptr == NULL)
      instance_list = il;
    else
      instance_list_ptr->next = il;
    instance_list_ptr = il;

    /* Copy OID to oid_list[0] */
    memcpy (oid_list[0].oid, vb->name, sizeof (oid) * vb->name_length);
    oid_list[0].oid_len = vb->name_length;

    for (i = 0; i < data->values_len; i++)
    {
      csnmp_table_values_t *vt;

      vb = vb->next_variable;
      if (vb == NULL)
      {
	status = -1;
	break;
      }

      /* Check if we left the subtree */
      if (snmp_oid_ncompare (data->values[i].oid,
	    data->values[i].oid_len,
	    vb->name, vb->name_length,
	    data->values[i].oid_len) != 0)
      {
	DEBUG ("snmp plugin: host = %s; data = %s; Value %i left its subtree.",
	    host->name, data->name, i);
	continue;
      }

      if ((value_table_ptr[i] != NULL)
	  && (vb->name[vb->name_length - 1] <= value_table_ptr[i]->subid))
      {
	DEBUG ("snmp plugin: host = %s; data = %s; i = %i; SUBID is not increasing.",
	    host->name, data->name, i);
	continue;
      }

      vt = (csnmp_table_values_t *) malloc (sizeof (csnmp_table_values_t));
      if (vt != NULL)
      {
	vt->subid = vb->name[vb->name_length - 1];
	vt->value = csnmp_value_list_to_value (vb, ds->ds[i].type);
	vt->next = NULL;

	if (value_table_ptr[i] == NULL)
	  value_table[i] = vt;
	else
	  value_table_ptr[i]->next = vt;
	value_table_ptr[i] = vt;
      }

      /* Copy OID to oid_list[i + 1] */
      memcpy (oid_list[i + 1].oid, vb->name, sizeof (oid) * vb->name_length);
      oid_list[i + 1].oid_len = vb->name_length;
    } /* for (i = data->values_len) */

    if (res != NULL)
      snmp_free_pdu (res);
    res = NULL;
  } /* while (status == 0) */

  if (status == 0)
    csnmp_dispatch_table (host, data, instance_list, value_table);

  /* Free all allocated variables here */
  while (instance_list != NULL)
  {
    instance_list_ptr = instance_list->next;
    sfree (instance_list);
    instance_list = instance_list_ptr;
  }

  for (i = 0; i < data->values_len; i++)
  {
    csnmp_table_values_t *tmp;
    while (value_table[i] != NULL)
    {
      tmp = value_table[i]->next;
      sfree (value_table[i]);
      value_table[i] = tmp;
    }
  }

  sfree (value_table);
  sfree (oid_list);

  return (0);
} /* int csnmp_read_table */

static int csnmp_read_value (host_definition_t *host, data_definition_t *data)
{
  struct snmp_pdu *req;
  struct snmp_pdu *res;
  struct variable_list *vb;

  const data_set_t *ds;
  value_list_t vl = VALUE_LIST_INIT;

  int status;
  int i;

  DEBUG ("snmp plugin: csnmp_read_value (host = %s, data = %s)",
      host->name, data->name);

  if (host->sess_handle == NULL)
  {
    DEBUG ("snmp plugin: csnmp_read_table: host->sess_handle == NULL");
    return (-1);
  }

  ds = plugin_get_ds (data->type);
  if (!ds)
  {
    ERROR ("snmp plugin: DataSet `%s' not defined.", data->type);
    return (-1);
  }

  if (ds->ds_num != data->values_len)
  {
    ERROR ("snmp plugin: DataSet `%s' requires %i values, but config talks about %i",
	data->type, ds->ds_num, data->values_len);
    return (-1);
  }

  vl.values_len = ds->ds_num;
  vl.values = (value_t *) malloc (sizeof (value_t) * vl.values_len);
  if (vl.values == NULL)
    return (-1);
  for (i = 0; i < vl.values_len; i++)
  {
    if (ds->ds[i].type == DS_TYPE_COUNTER)
      vl.values[i].counter = 0;
    else
      vl.values[i].gauge = NAN;
  }

  strncpy (vl.host, host->name, sizeof (vl.host));
  vl.host[sizeof (vl.host) - 1] = '\0';
  strcpy (vl.plugin, "snmp");
  strncpy (vl.type_instance, data->instance.string, sizeof (vl.type_instance));
  vl.type_instance[sizeof (vl.type_instance) - 1] = '\0';

  vl.interval = host->skip_num;

  req = snmp_pdu_create (SNMP_MSG_GET);
  if (req == NULL)
  {
    ERROR ("snmp plugin: snmp_pdu_create failed.");
    sfree (vl.values);
    return (-1);
  }

  for (i = 0; i < data->values_len; i++)
    snmp_add_null_var (req, data->values[i].oid, data->values[i].oid_len);
  status = snmp_sess_synch_response (host->sess_handle, req, &res);

  if (status != STAT_SUCCESS)
  {
    char *errstr = NULL;

    snmp_sess_error (host->sess_handle, NULL, NULL, &errstr);
    ERROR ("snmp plugin: host %s: snmp_sess_synch_response failed: %s",
	host->name, (errstr == NULL) ? "Unknown problem" : errstr);
    csnmp_host_close_session (host);
    sfree (errstr);

    return (-1);
  }

  vl.time = time (NULL);

  for (vb = res->variables; vb != NULL; vb = vb->next_variable)
  {
    char buffer[1024];
    snprint_variable (buffer, sizeof (buffer),
	vb->name, vb->name_length, vb);
    DEBUG ("snmp plugin: Got this variable: %s", buffer);

    for (i = 0; i < data->values_len; i++)
      if (snmp_oid_compare (data->values[i].oid, data->values[i].oid_len,
	    vb->name, vb->name_length) == 0)
	vl.values[i] = csnmp_value_list_to_value (vb, ds->ds[i].type);
  } /* for (res->variables) */

  snmp_free_pdu (res);

  DEBUG ("snmp plugin: -> plugin_dispatch_values (%s, &vl);", data->type);
  plugin_dispatch_values (data->type, &vl);
  sfree (vl.values);

  return (0);
} /* int csnmp_read_value */

static int csnmp_read_host (host_definition_t *host)
{
  int i;

  DEBUG ("snmp plugin: csnmp_read_host (%s);", host->name);

  if (host->sess_handle == NULL)
    csnmp_host_open_session (host);

  if (host->sess_handle == NULL)
    return (-1);

  for (i = 0; i < host->data_list_len; i++)
  {
    data_definition_t *data = host->data_list[i];

    if (data->is_table)
      csnmp_read_table (host, data);
    else
      csnmp_read_value (host, data);
  }

  return (0);
} /* int csnmp_read_host */

static void *csnmp_read_thread (void *data)
{
  host_definition_t *host;

  pthread_mutex_lock (&host_lock);
  while (do_shutdown == 0)
  {
    pthread_cond_wait (&host_cond, &host_lock);

    for (host = host_head; host != NULL; host = host->next)
    {
      if (do_shutdown != 0)
	break;
      if (host->state != STATE_WAIT)
	continue;

      host->state = STATE_BUSY;
      pthread_mutex_unlock (&host_lock);
      csnmp_read_host (host);
      pthread_mutex_lock (&host_lock);
      host->state = STATE_IDLE;
    } /* for (host) */
  } /* while (do_shutdown == 0) */
  pthread_mutex_unlock (&host_lock);

  pthread_exit ((void *) 0);
  return ((void *) 0);
} /* void *csnmp_read_thread */

static int csnmp_init (void)
{
  host_definition_t *host;
  int i;

  if (host_head == NULL)
  {
    NOTICE ("snmp plugin: No host has been defined.");
    return (-1);
  }

  call_snmp_init_once ();

  threads_num = 0;
  for (host = host_head; host != NULL; host = host->next)
  {
    threads_num++;
    /* We need to initialize `skip_num' here, because `interval_g' isn't
     * initialized during `configure'. */
    host->skip_left = interval_g;
    if (host->skip_num == 0)
    {
      host->skip_num = interval_g;
    }
    else if (host->skip_num < interval_g)
    {
      host->skip_num = interval_g;
      WARNING ("snmp plugin: Data for host `%s' will be collected every %i seconds.",
	  host->name, host->skip_num);
    }

    csnmp_host_open_session (host);
  } /* for (host) */

  /* Now start the reading threads */
  if (threads_num > 3)
  {
    threads_num = 3 + ((threads_num - 3) / 10);
    if (threads_num > 10)
      threads_num = 10;
  }

  threads = (pthread_t *) malloc (threads_num * sizeof (pthread_t));
  if (threads == NULL)
  {
    ERROR ("snmp plugin: malloc failed.");
    return (-1);
  }
  memset (threads, '\0', threads_num * sizeof (pthread_t));

  for (i = 0; i < threads_num; i++)
      pthread_create (threads + i, NULL, csnmp_read_thread, (void *) 0);

  return (0);
} /* int csnmp_init */

static int csnmp_read (void)
{
  host_definition_t *host;
  time_t now;

  if (host_head == NULL)
  {
    INFO ("snmp plugin: No hosts configured.");
    return (-1);
  }

  now = time (NULL);

  pthread_mutex_lock (&host_lock);
  for (host = host_head; host != NULL; host = host->next)
  {
    if (host->state != STATE_IDLE)
      continue;

    host->skip_left -= interval_g;
    if (host->skip_left >= interval_g)
      continue;

    host->state = STATE_WAIT;

    host->skip_left = host->skip_num;
  } /* for (host) */

  pthread_cond_broadcast (&host_cond);
  pthread_mutex_unlock (&host_lock);

  return (0);
} /* int csnmp_read */

static int csnmp_shutdown (void)
{
  host_definition_t *host_this;
  host_definition_t *host_next;

  data_definition_t *data_this;
  data_definition_t *data_next;

  int i;

  pthread_mutex_lock (&host_lock);
  do_shutdown = 1;
  pthread_cond_broadcast (&host_cond);
  pthread_mutex_unlock (&host_lock);

  for (i = 0; i < threads_num; i++)
    pthread_join (threads[i], NULL);

  /* Now that all the threads have exited, let's free all the global variables.
   * This isn't really neccessary, I guess, but I think it's good stile to do
   * so anyway. */
  host_this = host_head;
  host_head = NULL;
  while (host_this != NULL)
  {
    host_next = host_this->next;

    csnmp_host_close_session (host_this);

    sfree (host_this->name);
    sfree (host_this->address);
    sfree (host_this->community);
    sfree (host_this->data_list);
    sfree (host_this);

    host_this = host_next;
  }

  data_this = data_head;
  data_head = NULL;
  while (data_this != NULL)
  {
    data_next = data_this->next;

    sfree (data_this->name);
    sfree (data_this->type);
    sfree (data_this->values);
    sfree (data_this);

    data_this = data_next;
  }

  return (0);
} /* int csnmp_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("snmp", csnmp_config);
  plugin_register_init ("snmp", csnmp_init);
  plugin_register_read ("snmp", csnmp_read);
  plugin_register_shutdown ("snmp", csnmp_shutdown);
} /* void module_register */

/*
 * vim: shiftwidth=2 softtabstop=2 tabstop=8
 */
