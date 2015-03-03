/**
 * collectd - src/snmp.c
 * Copyright (C) 2007-2012  Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "utils_complain.h"

#include <pthread.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <fnmatch.h>

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
  _Bool is_table;
  instance_t instance;
  char *instance_prefix;
  oid_t *values;
  int values_len;
  double scale;
  double shift;
  struct data_definition_s *next;
  char **ignores;
  size_t ignores_len;
  int invert_match;
};
typedef struct data_definition_s data_definition_t;

struct host_definition_s
{
  char *name;
  char *address;
  int version;

  /* snmpv1/2 options */
  char *community;

  /* snmpv3 security options */
  char *username;
  oid *auth_protocol;
  size_t auth_protocol_len;
  char *auth_passphrase;
  oid *priv_protocol;
  size_t priv_protocol_len;
  char *priv_passphrase;
  int security_level;
  char *context;

  void *sess_handle;
  c_complain_t complaint;
  cdtime_t interval;
  data_definition_t **data_list;
  int data_list_len;
};
typedef struct host_definition_s host_definition_t;

/* These two types are used to cache values in `csnmp_read_table' to handle
 * gaps in tables. */
struct csnmp_list_instances_s
{
  oid_t suffix;
  char instance[DATA_MAX_NAME_LEN];
  struct csnmp_list_instances_s *next;
};
typedef struct csnmp_list_instances_s csnmp_list_instances_t;

struct csnmp_table_values_s
{
  oid_t suffix;
  value_t value;
  struct csnmp_table_values_s *next;
};
typedef struct csnmp_table_values_s csnmp_table_values_t;

/*
 * Private variables
 */
static data_definition_t *data_head = NULL;

/*
 * Prototypes
 */
static int csnmp_read_host (user_data_t *ud);

/*
 * Private functions
 */
static void csnmp_oid_init (oid_t *dst, oid const *src, size_t n)
{
  assert (n <= STATIC_ARRAY_SIZE (dst->oid));
  memcpy (dst->oid, src, sizeof (*src) * n);
  dst->oid_len = n;
}

static int csnmp_oid_compare (oid_t const *left, oid_t const *right)
{
  return (snmp_oid_compare (left->oid, left->oid_len,
        right->oid, right->oid_len));
}

static int csnmp_oid_suffix (oid_t *dst, oid_t const *src,
    oid_t const *root)
{
  /* Make sure "src" is in "root"s subtree. */
  if (src->oid_len <= root->oid_len)
    return (EINVAL);
  if (snmp_oid_ncompare (root->oid, root->oid_len,
        src->oid, src->oid_len,
        /* n = */ root->oid_len) != 0)
    return (EINVAL);

  memset (dst, 0, sizeof (*dst));
  dst->oid_len = src->oid_len - root->oid_len;
  memcpy (dst->oid, &src->oid[root->oid_len],
      dst->oid_len * sizeof (dst->oid[0]));
  return (0);
}

static int csnmp_oid_to_string (char *buffer, size_t buffer_size,
    oid_t const *o)
{
  char oid_str[MAX_OID_LEN][16];
  char *oid_str_ptr[MAX_OID_LEN];
  size_t i;

  for (i = 0; i < o->oid_len; i++)
  {
    ssnprintf (oid_str[i], sizeof (oid_str[i]), "%lu", (unsigned long) o->oid[i]);
    oid_str_ptr[i] = oid_str[i];
  }

  return (strjoin (buffer, buffer_size,
        oid_str_ptr, o->oid_len, /* separator = */ "."));
}

static void csnmp_host_close_session (host_definition_t *host) /* {{{ */
{
  if (host->sess_handle == NULL)
    return;

  snmp_sess_close (host->sess_handle);
  host->sess_handle = NULL;
} /* }}} void csnmp_host_close_session */

static void csnmp_host_definition_destroy (void *arg) /* {{{ */
{
  host_definition_t *hd;

  hd = arg;

  if (hd == NULL)
    return;

  if (hd->name != NULL)
  {
    DEBUG ("snmp plugin: Destroying host definition for host `%s'.",
        hd->name);
  }

  csnmp_host_close_session (hd);

  sfree (hd->name);
  sfree (hd->address);
  sfree (hd->community);
  sfree (hd->username);
  sfree (hd->auth_passphrase);
  sfree (hd->priv_passphrase);
  sfree (hd->context);
  sfree (hd->data_list);

  sfree (hd);
} /* }}} void csnmp_host_definition_destroy */

/* Many functions to handle the configuration. {{{ */
/* First there are many functions which do configuration stuff. It's a big
 * bloated and messy, I'm afraid. */

/*
 * Callgraph for the config stuff:
 *  csnmp_config
 *  +-> call_snmp_init_once
 *  +-> csnmp_config_add_data
 *  !   +-> csnmp_config_add_data_instance
 *  !   +-> csnmp_config_add_data_instance_prefix
 *  !   +-> csnmp_config_add_data_values
 *  +-> csnmp_config_add_host
 *      +-> csnmp_config_add_host_version
 *      +-> csnmp_config_add_host_collect
 *      +-> csnmp_config_add_host_auth_protocol
 *      +-> csnmp_config_add_host_priv_protocol
 *      +-> csnmp_config_add_host_security_level
 */
static void call_snmp_init_once (void)
{
  static int have_init = 0;

  if (have_init == 0)
    init_snmp (PACKAGE_NAME);
  have_init = 1;
} /* void call_snmp_init_once */

static int csnmp_config_add_data_instance (data_definition_t *dd, oconfig_item_t *ci)
{
  char buffer[DATA_MAX_NAME_LEN];
  int status;

  status = cf_util_get_string_buffer(ci, buffer, sizeof(buffer));
  if (status != 0)
    return status;

  if (dd->is_table)
  {
    /* Instance is an OID */
    dd->instance.oid.oid_len = MAX_OID_LEN;

    if (!read_objid (buffer,
          dd->instance.oid.oid, &dd->instance.oid.oid_len))
    {
      ERROR ("snmp plugin: read_objid (%s) failed.", buffer);
      return (-1);
    }
  }
  else
  {
    /* Instance is a simple string */
    sstrncpy (dd->instance.string, buffer,
        sizeof (dd->instance.string));
  }

  return (0);
} /* int csnmp_config_add_data_instance */

static int csnmp_config_add_data_instance_prefix (data_definition_t *dd,
    oconfig_item_t *ci)
{
  int status;

  if (!dd->is_table)
  {
    WARNING ("snmp plugin: data %s: InstancePrefix is ignored when `Table' "
        "is set to `false'.", dd->name);
    return (-1);
  }

  status = cf_util_get_string(ci, &dd->instance_prefix);
  return status;
} /* int csnmp_config_add_data_instance_prefix */

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

  sfree (dd->values);
  dd->values_len = 0;
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

static int csnmp_config_add_data_blacklist(data_definition_t *dd, oconfig_item_t *ci)
{
  int i;

  if (ci->values_num < 1)
    return (0);

  for (i = 0; i < ci->values_num; i++)
  {
    if (ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("snmp plugin: `Ignore' needs only string argument.");
      return (-1);
    }
  }

  dd->ignores_len = 0;
  dd->ignores = NULL;

  for (i = 0; i < ci->values_num; ++i)
  {
    if (strarray_add(&(dd->ignores), &(dd->ignores_len), ci->values[i].value.string) != 0)
    {
      ERROR("snmp plugin: Can't allocate memory");
      strarray_free(dd->ignores, dd->ignores_len);
      return (ENOMEM);
    }
  }
  return 0;
} /* int csnmp_config_add_data_blacklist */

static int csnmp_config_add_data_blacklist_match_inverted(data_definition_t *dd, oconfig_item_t *ci)
{
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_BOOLEAN))
  {
    WARNING ("snmp plugin: `InvertMatch' needs exactly one boolean argument.");
    return (-1);
  }

  dd->invert_match = ci->values[0].value.boolean ? 1 : 0;

  return (0);
} /* int csnmp_config_add_data_blacklist_match_inverted */

static int csnmp_config_add_data (oconfig_item_t *ci)
{
  data_definition_t *dd;
  int status = 0;
  int i;

  dd = (data_definition_t *) malloc (sizeof (data_definition_t));
  if (dd == NULL)
    return (-1);
  memset (dd, '\0', sizeof (data_definition_t));

  status = cf_util_get_string(ci, &dd->name);
  if (status != 0)
  {
    free (dd);
    return (-1);
  }

  dd->scale = 1.0;
  dd->shift = 0.0;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Type", option->key) == 0)
      status = cf_util_get_string(option, &dd->type);
    else if (strcasecmp ("Table", option->key) == 0)
      status = cf_util_get_boolean(option, &dd->is_table);
    else if (strcasecmp ("Instance", option->key) == 0)
      status = csnmp_config_add_data_instance (dd, option);
    else if (strcasecmp ("InstancePrefix", option->key) == 0)
      status = csnmp_config_add_data_instance_prefix (dd, option);
    else if (strcasecmp ("Values", option->key) == 0)
      status = csnmp_config_add_data_values (dd, option);
    else if (strcasecmp ("Shift", option->key) == 0)
      status = cf_util_get_double(option, &dd->shift);
    else if (strcasecmp ("Scale", option->key) == 0)
      status = cf_util_get_double(option, &dd->scale);
    else if (strcasecmp ("Ignore", option->key) == 0)
      status = csnmp_config_add_data_blacklist(dd, option);
    else if (strcasecmp ("InvertMatch", option->key) == 0)
      status = csnmp_config_add_data_blacklist_match_inverted(dd, option);
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
    sfree (dd->instance_prefix);
    sfree (dd->values);
    sfree (dd->ignores);
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
  if ((version < 1) || (version > 3))
  {
    WARNING ("snmp plugin: `Version' must either be `1', `2', or `3'.");
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

static int csnmp_config_add_host_auth_protocol (host_definition_t *hd, oconfig_item_t *ci)
{
  char buffer[4];
  int status;

  status = cf_util_get_string_buffer(ci, buffer, sizeof(buffer));
  if (status != 0)
    return status;

  if (strcasecmp("MD5", buffer) == 0) {
    hd->auth_protocol = usmHMACMD5AuthProtocol;
    hd->auth_protocol_len = sizeof(usmHMACMD5AuthProtocol)/sizeof(oid);
  }
  else if (strcasecmp("SHA", buffer) == 0) {
    hd->auth_protocol = usmHMACSHA1AuthProtocol;
    hd->auth_protocol_len = sizeof(usmHMACSHA1AuthProtocol)/sizeof(oid);
  }
  else
  {
    WARNING ("snmp plugin: The `AuthProtocol' config option must be `MD5' or `SHA'.");
    return (-1);
  }

  DEBUG ("snmp plugin: host = %s; host->auth_protocol = %s;",
      hd->name, hd->auth_protocol == usmHMACMD5AuthProtocol ? "MD5" : "SHA");

  return (0);
} /* int csnmp_config_add_host_auth_protocol */

static int csnmp_config_add_host_priv_protocol (host_definition_t *hd, oconfig_item_t *ci)
{
  char buffer[4];
  int status;

  status = cf_util_get_string_buffer(ci, buffer, sizeof(buffer));
  if (status != 0)
    return status;

  if (strcasecmp("AES", buffer) == 0)
  {
    hd->priv_protocol = usmAESPrivProtocol;
    hd->priv_protocol_len = sizeof(usmAESPrivProtocol)/sizeof(oid);
  }
  else if (strcasecmp("DES", buffer) == 0) {
    hd->priv_protocol = usmDESPrivProtocol;
    hd->priv_protocol_len = sizeof(usmDESPrivProtocol)/sizeof(oid);
  }
  else
  {
    WARNING ("snmp plugin: The `PrivProtocol' config option must be `AES' or `DES'.");
    return (-1);
  }

  DEBUG ("snmp plugin: host = %s; host->priv_protocol = %s;",
      hd->name, hd->priv_protocol == usmAESPrivProtocol ? "AES" : "DES");

  return (0);
} /* int csnmp_config_add_host_priv_protocol */

static int csnmp_config_add_host_security_level (host_definition_t *hd, oconfig_item_t *ci)
{
  char buffer[16];
  int status;

  status = cf_util_get_string_buffer(ci, buffer, sizeof(buffer));
  if (status != 0)
    return status;

  if (strcasecmp("noAuthNoPriv", buffer) == 0)
    hd->security_level = SNMP_SEC_LEVEL_NOAUTH;
  else if (strcasecmp("authNoPriv", buffer) == 0)
    hd->security_level = SNMP_SEC_LEVEL_AUTHNOPRIV;
  else if (strcasecmp("authPriv", buffer) == 0)
    hd->security_level = SNMP_SEC_LEVEL_AUTHPRIV;
  else
  {
    WARNING ("snmp plugin: The `SecurityLevel' config option must be `noAuthNoPriv', `authNoPriv', or `authPriv'.");
    return (-1);
  }

  DEBUG ("snmp plugin: host = %s; host->security_level = %d;",
      hd->name, hd->security_level);

  return (0);
} /* int csnmp_config_add_host_security_level */

static int csnmp_config_add_host (oconfig_item_t *ci)
{
  host_definition_t *hd;
  int status = 0;
  int i;

  /* Registration stuff. */
  char cb_name[DATA_MAX_NAME_LEN];
  user_data_t cb_data;
  struct timespec cb_interval;

  hd = (host_definition_t *) malloc (sizeof (host_definition_t));
  if (hd == NULL)
    return (-1);
  memset (hd, '\0', sizeof (host_definition_t));
  hd->version = 2;
  C_COMPLAIN_INIT (&hd->complaint);

  status = cf_util_get_string(ci, &hd->name);
  if (status != 0)
    return status;

  hd->sess_handle = NULL;
  hd->interval = 0;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *option = ci->children + i;
    status = 0;

    if (strcasecmp ("Address", option->key) == 0)
      status = cf_util_get_string(option, &hd->address);
    else if (strcasecmp ("Community", option->key) == 0)
      status = cf_util_get_string(option, &hd->community);
    else if (strcasecmp ("Version", option->key) == 0)
      status = csnmp_config_add_host_version (hd, option);
    else if (strcasecmp ("Collect", option->key) == 0)
      csnmp_config_add_host_collect (hd, option);
    else if (strcasecmp ("Interval", option->key) == 0)
      cf_util_get_cdtime (option, &hd->interval);
    else if (strcasecmp ("Username", option->key) == 0)
      status = cf_util_get_string(option, &hd->username);
    else if (strcasecmp ("AuthProtocol", option->key) == 0)
      status = csnmp_config_add_host_auth_protocol (hd, option);
    else if (strcasecmp ("PrivacyProtocol", option->key) == 0)
      status = csnmp_config_add_host_priv_protocol (hd, option);
    else if (strcasecmp ("AuthPassphrase", option->key) == 0)
      status = cf_util_get_string(option, &hd->auth_passphrase);
    else if (strcasecmp ("PrivacyPassphrase", option->key) == 0)
      status = cf_util_get_string(option, &hd->priv_passphrase);
    else if (strcasecmp ("SecurityLevel", option->key) == 0)
      status = csnmp_config_add_host_security_level (hd, option);
    else if (strcasecmp ("Context", option->key) == 0)
      status = cf_util_get_string(option, &hd->context);
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
    if (hd->community == NULL && hd->version < 3)
    {
      WARNING ("snmp plugin: `Community' not given for host `%s'", hd->name);
      status = -1;
      break;
    }
    if (hd->version == 3)
    {
      if (hd->username == NULL)
      {
        WARNING ("snmp plugin: `Username' not given for host `%s'", hd->name);
        status = -1;
        break;
      }
      if (hd->security_level == 0)
      {
        WARNING ("snmp plugin: `SecurityLevel' not given for host `%s'", hd->name);
        status = -1;
        break;
      }
      if (hd->security_level == SNMP_SEC_LEVEL_AUTHNOPRIV || hd->security_level == SNMP_SEC_LEVEL_AUTHPRIV)
      {
	if (hd->auth_protocol == NULL)
	{
	  WARNING ("snmp plugin: `AuthProtocol' not given for host `%s'", hd->name);
	  status = -1;
	  break;
	}
	if (hd->auth_passphrase == NULL)
	{
	  WARNING ("snmp plugin: `AuthPassphrase' not given for host `%s'", hd->name);
	  status = -1;
	  break;
	}
      }
      if (hd->security_level == SNMP_SEC_LEVEL_AUTHPRIV)
      {
	if (hd->priv_protocol == NULL)
	{
	  WARNING ("snmp plugin: `PrivacyProtocol' not given for host `%s'", hd->name);
	  status = -1;
	  break;
	}
	if (hd->priv_passphrase == NULL)
	{
	  WARNING ("snmp plugin: `PrivacyPassphrase' not given for host `%s'", hd->name);
	  status = -1;
	  break;
	}
      }
    }

    break;
  } /* while (status == 0) */

  if (status != 0)
  {
    csnmp_host_definition_destroy (hd);
    return (-1);
  }

  DEBUG ("snmp plugin: hd = { name = %s, address = %s, community = %s, version = %i }",
      hd->name, hd->address, hd->community, hd->version);

  ssnprintf (cb_name, sizeof (cb_name), "snmp-%s", hd->name);

  memset (&cb_data, 0, sizeof (cb_data));
  cb_data.data = hd;
  cb_data.free_func = csnmp_host_definition_destroy;

  CDTIME_T_TO_TIMESPEC (hd->interval, &cb_interval);

  status = plugin_register_complex_read (/* group = */ NULL, cb_name,
      csnmp_read_host, /* interval = */ &cb_interval,
      /* user_data = */ &cb_data);
  if (status != 0)
  {
    ERROR ("snmp plugin: Registering complex read function failed.");
    csnmp_host_definition_destroy (hd);
    return (-1);
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

/* }}} End of the config stuff. Now the interesting part begins */

static void csnmp_host_open_session (host_definition_t *host)
{
  struct snmp_session sess;
  int error;

  if (host->sess_handle != NULL)
    csnmp_host_close_session (host);

  snmp_sess_init (&sess);
  sess.peername = host->address;
  switch (host->version)
  {
    case 1:
      sess.version = SNMP_VERSION_1;
      break;
    case 3:
      sess.version = SNMP_VERSION_3;
      break;
    default:
      sess.version = SNMP_VERSION_2c;
      break;
  }

  if (host->version == 3)
  {
    sess.securityName = host->username;
    sess.securityNameLen = strlen (host->username);
    sess.securityLevel = host->security_level;

    if (sess.securityLevel == SNMP_SEC_LEVEL_AUTHNOPRIV || sess.securityLevel == SNMP_SEC_LEVEL_AUTHPRIV)
    {
      sess.securityAuthProto = host->auth_protocol;
      sess.securityAuthProtoLen = host->auth_protocol_len;
      sess.securityAuthKeyLen = USM_AUTH_KU_LEN;
      error = generate_Ku (sess.securityAuthProto,
	    sess.securityAuthProtoLen,
	    (u_char *) host->auth_passphrase,
	    strlen(host->auth_passphrase),
	    sess.securityAuthKey,
	    &sess.securityAuthKeyLen);
      if (error != SNMPERR_SUCCESS) {
	ERROR ("snmp plugin: host %s: Error generating Ku from auth_passphrase. (Error %d)", host->name, error);
      }
    }

    if (sess.securityLevel == SNMP_SEC_LEVEL_AUTHPRIV)
    {
      sess.securityPrivProto = host->priv_protocol;
      sess.securityPrivProtoLen = host->priv_protocol_len;
      sess.securityPrivKeyLen = USM_PRIV_KU_LEN;
      error = generate_Ku (sess.securityAuthProto,
	    sess.securityAuthProtoLen,
	    (u_char *) host->priv_passphrase,
	    strlen(host->priv_passphrase),
	    sess.securityPrivKey,
	    &sess.securityPrivKeyLen);
      if (error != SNMPERR_SUCCESS) {
	ERROR ("snmp plugin: host %s: Error generating Ku from priv_passphrase. (Error %d)", host->name, error);
      }
    }

    if (host->context != NULL)
    {
      sess.contextName = host->context;
      sess.contextNameLen = strlen (host->context);
    }
  }
  else /* SNMPv1/2 "authenticates" with community string */
  {
    sess.community = (u_char *) host->community;
    sess.community_len = strlen (host->community);
  }

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

/* TODO: Check if negative values wrap around. Problem: negative temperatures. */
static value_t csnmp_value_list_to_value (struct variable_list *vl, int type,
    double scale, double shift,
    const char *host_name, const char *data_name)
{
  value_t ret;
  uint64_t tmp_unsigned = 0;
  int64_t tmp_signed = 0;
  _Bool defined = 1;
  /* Set to true when the original SNMP type appears to have been signed. */
  _Bool prefer_signed = 0;

  if ((vl->type == ASN_INTEGER)
      || (vl->type == ASN_UINTEGER)
      || (vl->type == ASN_COUNTER)
#ifdef ASN_TIMETICKS
      || (vl->type == ASN_TIMETICKS)
#endif
      || (vl->type == ASN_GAUGE))
  {
    tmp_unsigned = (uint32_t) *vl->val.integer;
    tmp_signed = (int32_t) *vl->val.integer;

    if ((vl->type == ASN_INTEGER)
        || (vl->type == ASN_GAUGE))
      prefer_signed = 1;

    DEBUG ("snmp plugin: Parsed int32 value is %"PRIu64".", tmp_unsigned);
  }
  else if (vl->type == ASN_COUNTER64)
  {
    tmp_unsigned = (uint32_t) vl->val.counter64->high;
    tmp_unsigned = tmp_unsigned << 32;
    tmp_unsigned += (uint32_t) vl->val.counter64->low;
    tmp_signed = (int64_t) tmp_unsigned;
    DEBUG ("snmp plugin: Parsed int64 value is %"PRIu64".", tmp_unsigned);
  }
  else if (vl->type == ASN_OCTET_STR)
  {
    /* We'll handle this later.. */
  }
  else
  {
    char oid_buffer[1024];

    memset (oid_buffer, 0, sizeof (oid_buffer));
    snprint_objid (oid_buffer, sizeof (oid_buffer) - 1,
        vl->name, vl->name_length);

#ifdef ASN_NULL
    if (vl->type == ASN_NULL)
      INFO ("snmp plugin: OID \"%s\" is undefined (type ASN_NULL)",
          oid_buffer);
    else
#endif
      WARNING ("snmp plugin: I don't know the ASN type #%i "
               "(OID: \"%s\", data block \"%s\", host block \"%s\")",
          (int) vl->type, oid_buffer,
          (data_name != NULL) ? data_name : "UNKNOWN",
          (host_name != NULL) ? host_name : "UNKNOWN");

    defined = 0;
  }

  if (vl->type == ASN_OCTET_STR)
  {
    int status = -1;

    if (vl->val.string != NULL)
    {
      char string[64];
      size_t string_length;

      string_length = sizeof (string) - 1;
      if (vl->val_len < string_length)
        string_length = vl->val_len;

      /* The strings we get from the Net-SNMP library may not be null
       * terminated. That is why we're using `memcpy' here and not `strcpy'.
       * `string_length' is set to `vl->val_len' which holds the length of the
       * string.  -octo */
      memcpy (string, vl->val.string, string_length);
      string[string_length] = 0;

      status = parse_value (string, &ret, type);
      if (status != 0)
      {
        ERROR ("snmp plugin: host %s: csnmp_value_list_to_value: Parsing string as %s failed: %s",
            (host_name != NULL) ? host_name : "UNKNOWN",
            DS_TYPE_TO_STRING (type), string);
      }
    }

    if (status != 0)
    {
      switch (type)
      {
        case DS_TYPE_COUNTER:
        case DS_TYPE_DERIVE:
        case DS_TYPE_ABSOLUTE:
          memset (&ret, 0, sizeof (ret));
          break;

        case DS_TYPE_GAUGE:
          ret.gauge = NAN;
          break;

        default:
          ERROR ("snmp plugin: csnmp_value_list_to_value: Unknown "
              "data source type: %i.", type);
          ret.gauge = NAN;
      }
    }
  } /* if (vl->type == ASN_OCTET_STR) */
  else if (type == DS_TYPE_COUNTER)
  {
    ret.counter = tmp_unsigned;
  }
  else if (type == DS_TYPE_GAUGE)
  {
    if (!defined)
      ret.gauge = NAN;
    else if (prefer_signed)
      ret.gauge = (scale * tmp_signed) + shift;
    else
      ret.gauge = (scale * tmp_unsigned) + shift;
  }
  else if (type == DS_TYPE_DERIVE)
  {
    if (prefer_signed)
      ret.derive = (derive_t) tmp_signed;
    else
      ret.derive = (derive_t) tmp_unsigned;
  }
  else if (type == DS_TYPE_ABSOLUTE)
  {
    ret.absolute = (absolute_t) tmp_unsigned;
  }
  else
  {
    ERROR ("snmp plugin: csnmp_value_list_to_value: Unknown data source "
        "type: %i.", type);
    ret.gauge = NAN;
  }

  return (ret);
} /* value_t csnmp_value_list_to_value */

static int csnmp_strvbcopy_hexstring (char *dst, /* {{{ */
    const struct variable_list *vb, size_t dst_size)
{
  char *buffer_ptr;
  size_t buffer_free;
  size_t i;

  buffer_ptr = dst;
  buffer_free = dst_size;

  for (i = 0; i < vb->val_len; i++)
  {
    int status;

    status = snprintf (buffer_ptr, buffer_free,
        (i == 0) ? "%02x" : ":%02x", (unsigned int) vb->val.bitstring[i]);

    if (status >= buffer_free)
    {
      buffer_ptr += (buffer_free - 1);
      *buffer_ptr = 0;
      return (dst_size + (buffer_free - status));
    }
    else /* if (status < buffer_free) */
    {
      buffer_ptr += status;
      buffer_free -= status;
    }
  }

  return ((int) (dst_size - buffer_free));
} /* }}} int csnmp_strvbcopy_hexstring */

static int csnmp_strvbcopy (char *dst, /* {{{ */
    const struct variable_list *vb, size_t dst_size)
{
  char *src;
  size_t num_chars;
  size_t i;

  if (vb->type == ASN_OCTET_STR)
    src = (char *) vb->val.string;
  else if (vb->type == ASN_BIT_STR)
    src = (char *) vb->val.bitstring;
  else
  {
    dst[0] = 0;
    return (EINVAL);
  }

  num_chars = dst_size - 1;
  if (num_chars > vb->val_len)
    num_chars = vb->val_len;

  for (i = 0; i < num_chars; i++)
  {
    /* Check for control characters. */
    if ((unsigned char)src[i] < 32)
      return (csnmp_strvbcopy_hexstring (dst, vb, dst_size));
    dst[i] = src[i];
  }
  dst[num_chars] = 0;

  return ((int) vb->val_len);
} /* }}} int csnmp_strvbcopy */

static int csnmp_instance_list_add (csnmp_list_instances_t **head,
    csnmp_list_instances_t **tail,
    const struct snmp_pdu *res,
    const host_definition_t *hd, const data_definition_t *dd)
{
  csnmp_list_instances_t *il;
  struct variable_list *vb;
  oid_t vb_name;
  int status;
  uint32_t i;
  uint32_t is_matched;

  /* Set vb on the last variable */
  for (vb = res->variables;
      (vb != NULL) && (vb->next_variable != NULL);
      vb = vb->next_variable)
    /* do nothing */;
  if (vb == NULL)
    return (-1);

  csnmp_oid_init (&vb_name, vb->name, vb->name_length);

  il = malloc (sizeof (*il));
  if (il == NULL)
  {
    ERROR ("snmp plugin: malloc failed.");
    return (-1);
  }
  memset (il, 0, sizeof (*il));
  il->next = NULL;

  status = csnmp_oid_suffix (&il->suffix, &vb_name, &dd->instance.oid);
  if (status != 0)
  {
    sfree (il);
    return (status);
  }

  /* Get instance name */
  if ((vb->type == ASN_OCTET_STR) || (vb->type == ASN_BIT_STR))
  {
    char *ptr;

    csnmp_strvbcopy (il->instance, vb, sizeof (il->instance));
    is_matched = 0;
    for (i = 0; i < dd->ignores_len; i++)
    {
      status = fnmatch(dd->ignores[i], il->instance, 0);
      if (status == 0)
      {
        if (dd->invert_match == 0)
        {
          sfree(il);
          return 0;
        }
	else
	{
          is_matched = 1;
	  break;
	}
      }
    }
    if (dd->invert_match != 0 && is_matched == 0)
    {
      sfree(il);
      return 0;
    }
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
    value_t val = csnmp_value_list_to_value (vb, DS_TYPE_COUNTER,
        /* scale = */ 1.0, /* shift = */ 0.0, hd->name, dd->name);
    ssnprintf (il->instance, sizeof (il->instance),
        "%llu", val.counter);
  }

  /* TODO: Debugging output */

  if (*head == NULL)
    *head = il;
  else
    (*tail)->next = il;
  *tail = il;

  return (0);
} /* int csnmp_instance_list_add */

static int csnmp_dispatch_table (host_definition_t *host, data_definition_t *data,
    csnmp_list_instances_t *instance_list,
    csnmp_table_values_t **value_table)
{
  const data_set_t *ds;
  value_list_t vl = VALUE_LIST_INIT;

  csnmp_list_instances_t *instance_list_ptr;
  csnmp_table_values_t **value_table_ptr;

  int i;
  _Bool have_more;
  oid_t current_suffix;

  ds = plugin_get_ds (data->type);
  if (!ds)
  {
    ERROR ("snmp plugin: DataSet `%s' not defined.", data->type);
    return (-1);
  }
  assert (ds->ds_num == data->values_len);

  instance_list_ptr = instance_list;

  value_table_ptr = malloc (sizeof (*value_table_ptr) * data->values_len);
  if (value_table_ptr == NULL)
    return (-1);
  for (i = 0; i < data->values_len; i++)
    value_table_ptr[i] = value_table[i];

  vl.values_len = ds->ds_num;
  vl.values = malloc (sizeof (*vl.values) * vl.values_len);
  if (vl.values == NULL)
  {
    ERROR ("snmp plugin: malloc failed.");
    sfree (value_table_ptr);
    return (-1);
  }

  sstrncpy (vl.host, host->name, sizeof (vl.host));
  sstrncpy (vl.plugin, "snmp", sizeof (vl.plugin));

  vl.interval = host->interval;

  have_more = 1;
  memset (&current_suffix, 0, sizeof (current_suffix));
  while (have_more)
  {
    _Bool suffix_skipped = 0;

    /* Determine next suffix to handle. */
    if (instance_list != NULL)
    {
      if (instance_list_ptr == NULL)
      {
        have_more = 0;
        continue;
      }

      memcpy (&current_suffix, &instance_list_ptr->suffix, sizeof (current_suffix));
    }
    else /* no instance configured */
    {
      csnmp_table_values_t *ptr = value_table_ptr[0];
      if (ptr == NULL)
      {
        have_more = 0;
        continue;
      }

      memcpy (&current_suffix, &ptr->suffix, sizeof (current_suffix));
    }

    /* Update all the value_table_ptr to point at the entry with the same
     * trailing partial OID */
    for (i = 0; i < data->values_len; i++)
    {
      while ((value_table_ptr[i] != NULL)
          && (csnmp_oid_compare (&value_table_ptr[i]->suffix, &current_suffix) < 0))
        value_table_ptr[i] = value_table_ptr[i]->next;

      if (value_table_ptr[i] == NULL)
      {
        have_more = 0;
        break;
      }
      else if (csnmp_oid_compare (&value_table_ptr[i]->suffix, &current_suffix) > 0)
      {
        /* This suffix is missing in the subtree. Indicate this with the
         * "suffix_skipped" flag and try the next instance / suffix. */
        suffix_skipped = 1;
        break;
      }
    } /* for (i = 0; i < columns; i++) */

    if (!have_more)
      break;

    /* Matching the values failed. Start from the beginning again. */
    if (suffix_skipped)
    {
      if (instance_list != NULL)
        instance_list_ptr = instance_list_ptr->next;
      else
        value_table_ptr[0] = value_table_ptr[0]->next;

      continue;
    }

    /* if we reach this line, all value_table_ptr[i] are non-NULL and are set
     * to the same subid. instance_list_ptr is either NULL or points to the
     * same subid, too. */
#if COLLECT_DEBUG
    for (i = 1; i < data->values_len; i++)
    {
      assert (value_table_ptr[i] != NULL);
      assert (csnmp_oid_compare (&value_table_ptr[i-1]->suffix,
            &value_table_ptr[i]->suffix) == 0);
    }
    assert ((instance_list_ptr == NULL)
        || (csnmp_oid_compare (&instance_list_ptr->suffix,
            &value_table_ptr[0]->suffix) == 0));
#endif

    sstrncpy (vl.type, data->type, sizeof (vl.type));

    {
      char temp[DATA_MAX_NAME_LEN];

      if (instance_list_ptr == NULL)
        csnmp_oid_to_string (temp, sizeof (temp), &current_suffix);
      else
        sstrncpy (temp, instance_list_ptr->instance, sizeof (temp));

      if (data->instance_prefix == NULL)
        sstrncpy (vl.type_instance, temp, sizeof (vl.type_instance));
      else
        ssnprintf (vl.type_instance, sizeof (vl.type_instance), "%s%s",
            data->instance_prefix, temp);
    }

    for (i = 0; i < data->values_len; i++)
      vl.values[i] = value_table_ptr[i]->value;

    /* If we get here `vl.type_instance' and all `vl.values' have been set */
    plugin_dispatch_values (&vl);

    if (instance_list != NULL)
      instance_list_ptr = instance_list_ptr->next;
    else
      value_table_ptr[0] = value_table_ptr[0]->next;
  } /* while (have_more) */

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

  uint32_t oid_list_len = (uint32_t) (data->values_len + 1);
  /* Holds the last OID returned by the device. We use this in the GETNEXT
   * request to proceed. */
  oid_t oid_list[oid_list_len];
  /* Set to false when an OID has left its subtree so we don't re-request it
   * again. */
  _Bool oid_list_todo[oid_list_len];

  int status;
  int i;
  uint32_t j;

  /* `value_list_head' and `value_list_tail' implement a linked list for each
   * value. `instance_list_head' and `instance_list_tail' implement a linked list of
   * instance names. This is used to jump gaps in the table. */
  csnmp_list_instances_t *instance_list_head;
  csnmp_list_instances_t *instance_list_tail;
  csnmp_table_values_t **value_list_head;
  csnmp_table_values_t **value_list_tail;

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
  memcpy (oid_list, data->values, data->values_len * sizeof (oid_t));
  if (data->instance.oid.oid_len > 0)
    memcpy (oid_list + data->values_len, &data->instance.oid, sizeof (oid_t));
  else /* no InstanceFrom option specified. */
    oid_list_len--;

  for (j = 0; j < oid_list_len; j++)
    oid_list_todo[j] = 1;

  /* We're going to construct n linked lists, one for each "value".
   * value_list_head will contain pointers to the heads of these linked lists,
   * value_list_tail will contain pointers to the tail of the lists. */
  value_list_head = calloc (data->values_len, sizeof (*value_list_head));
  value_list_tail = calloc (data->values_len, sizeof (*value_list_tail));
  if ((value_list_head == NULL) || (value_list_tail == NULL))
  {
    ERROR ("snmp plugin: csnmp_read_table: calloc failed.");
    sfree (value_list_head);
    sfree (value_list_tail);
    return (-1);
  }

  instance_list_head = NULL;
  instance_list_tail = NULL;

  status = 0;
  while (status == 0)
  {
    int oid_list_todo_num;

    req = snmp_pdu_create (SNMP_MSG_GETNEXT);
    if (req == NULL)
    {
      ERROR ("snmp plugin: snmp_pdu_create failed.");
      status = -1;
      break;
    }

    oid_list_todo_num = 0;
    for (j = 0; j < oid_list_len; j++)
    {
      /* Do not rerequest already finished OIDs */
      if (!oid_list_todo[j])
        continue;
      oid_list_todo_num++;
      snmp_add_null_var (req, oid_list[j].oid, oid_list[j].oid_len);
    }

    if (oid_list_todo_num == 0)
    {
      /* The request is still empty - so we are finished */
      DEBUG ("snmp plugin: all variables have left their subtree");
      status = 0;
      break;
    }

    res = NULL;
    status = snmp_sess_synch_response (host->sess_handle, req, &res);
    if ((status != STAT_SUCCESS) || (res == NULL))
    {
      char *errstr = NULL;

      snmp_sess_error (host->sess_handle, NULL, NULL, &errstr);

      c_complain (LOG_ERR, &host->complaint,
          "snmp plugin: host %s: snmp_sess_synch_response failed: %s",
          host->name, (errstr == NULL) ? "Unknown problem" : errstr);

      if (res != NULL)
        snmp_free_pdu (res);
      res = NULL;

      /* snmp_synch_response already freed our PDU */
      req = NULL;
      sfree (errstr);
      csnmp_host_close_session (host);

      status = -1;
      break;
    }

    status = 0;
    assert (res != NULL);
    c_release (LOG_INFO, &host->complaint,
        "snmp plugin: host %s: snmp_sess_synch_response successful.",
        host->name);

    vb = res->variables;
    if (vb == NULL)
    {
      status = -1;
      break;
    }

    for (vb = res->variables, i = 0; (vb != NULL); vb = vb->next_variable, i++)
    {
      /* Calculate value index from todo list */
      while (!oid_list_todo[i] && (i < oid_list_len))
        i++;

      /* An instance is configured and the res variable we process is the
       * instance value (last index) */
      if ((data->instance.oid.oid_len > 0) && (i == data->values_len))
      {
        if ((vb->type == SNMP_ENDOFMIBVIEW)
            || (snmp_oid_ncompare (data->instance.oid.oid,
                data->instance.oid.oid_len,
                vb->name, vb->name_length,
                data->instance.oid.oid_len) != 0))
        {
          DEBUG ("snmp plugin: host = %s; data = %s; Instance left its subtree.",
              host->name, data->name);
          oid_list_todo[i] = 0;
          continue;
        }

        /* Allocate a new `csnmp_list_instances_t', insert the instance name and
         * add it to the list */
        if (csnmp_instance_list_add (&instance_list_head, &instance_list_tail,
              res, host, data) != 0)
        {
          ERROR ("snmp plugin: host %s: csnmp_instance_list_add failed.",
              host->name);
          status = -1;
          break;
        }
      }
      else /* The variable we are processing is a normal value */
      {
        csnmp_table_values_t *vt;
        oid_t vb_name;
        oid_t suffix;
        int ret;

        csnmp_oid_init (&vb_name, vb->name, vb->name_length);

        /* Calculate the current suffix. This is later used to check that the
         * suffix is increasing. This also checks if we left the subtree */
        ret = csnmp_oid_suffix (&suffix, &vb_name, data->values + i);
        if (ret != 0)
        {
          DEBUG ("snmp plugin: host = %s; data = %s; i = %i; "
              "Value probably left its subtree.",
              host->name, data->name, i);
          oid_list_todo[i] = 0;
          continue;
        }

        /* Make sure the OIDs returned by the agent are increasing. Otherwise our
         * table matching algorithm will get confused. */
        if ((value_list_tail[i] != NULL)
            && (csnmp_oid_compare (&suffix, &value_list_tail[i]->suffix) <= 0))
        {
          DEBUG ("snmp plugin: host = %s; data = %s; i = %i; "
              "Suffix is not increasing.",
              host->name, data->name, i);
          oid_list_todo[i] = 0;
          continue;
        }

        vt = malloc (sizeof (*vt));
        if (vt == NULL)
        {
          ERROR ("snmp plugin: malloc failed.");
          status = -1;
          break;
        }
        memset (vt, 0, sizeof (*vt));

        vt->value = csnmp_value_list_to_value (vb, ds->ds[i].type,
            data->scale, data->shift, host->name, data->name);
        memcpy (&vt->suffix, &suffix, sizeof (vt->suffix));
        vt->next = NULL;

        if (value_list_tail[i] == NULL)
          value_list_head[i] = vt;
        else
          value_list_tail[i]->next = vt;
        value_list_tail[i] = vt;
      }

      /* Copy OID to oid_list[i] */
      memcpy (oid_list[i].oid, vb->name, sizeof (oid) * vb->name_length);
      oid_list[i].oid_len = vb->name_length;

    } /* for (vb = res->variables ...) */

    if (res != NULL)
      snmp_free_pdu (res);
    res = NULL;
  } /* while (status == 0) */

  if (res != NULL)
    snmp_free_pdu (res);
  res = NULL;

  if (req != NULL)
    snmp_free_pdu (req);
  req = NULL;

  if (status == 0)
    csnmp_dispatch_table (host, data, instance_list_head, value_list_head);

  /* Free all allocated variables here */
  while (instance_list_head != NULL)
  {
    csnmp_list_instances_t *next = instance_list_head->next;
    sfree (instance_list_head);
    instance_list_head = next;
  }

  for (i = 0; i < data->values_len; i++)
  {
    while (value_list_head[i] != NULL)
    {
      csnmp_table_values_t *next = value_list_head[i]->next;
      sfree (value_list_head[i]);
      value_list_head[i] = next;
    }
  }

  sfree (value_list_head);
  sfree (value_list_tail);

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

  sstrncpy (vl.host, host->name, sizeof (vl.host));
  sstrncpy (vl.plugin, "snmp", sizeof (vl.plugin));
  sstrncpy (vl.type, data->type, sizeof (vl.type));
  sstrncpy (vl.type_instance, data->instance.string, sizeof (vl.type_instance));

  vl.interval = host->interval;

  req = snmp_pdu_create (SNMP_MSG_GET);
  if (req == NULL)
  {
    ERROR ("snmp plugin: snmp_pdu_create failed.");
    sfree (vl.values);
    return (-1);
  }

  for (i = 0; i < data->values_len; i++)
    snmp_add_null_var (req, data->values[i].oid, data->values[i].oid_len);

  res = NULL;
  status = snmp_sess_synch_response (host->sess_handle, req, &res);

  if ((status != STAT_SUCCESS) || (res == NULL))
  {
    char *errstr = NULL;

    snmp_sess_error (host->sess_handle, NULL, NULL, &errstr);
    ERROR ("snmp plugin: host %s: snmp_sess_synch_response failed: %s",
        host->name, (errstr == NULL) ? "Unknown problem" : errstr);

    if (res != NULL)
      snmp_free_pdu (res);
    res = NULL;

    sfree (errstr);
    csnmp_host_close_session (host);

    return (-1);
  }


  for (vb = res->variables; vb != NULL; vb = vb->next_variable)
  {
#if COLLECT_DEBUG
    char buffer[1024];
    snprint_variable (buffer, sizeof (buffer),
        vb->name, vb->name_length, vb);
    DEBUG ("snmp plugin: Got this variable: %s", buffer);
#endif /* COLLECT_DEBUG */

    for (i = 0; i < data->values_len; i++)
      if (snmp_oid_compare (data->values[i].oid, data->values[i].oid_len,
            vb->name, vb->name_length) == 0)
        vl.values[i] = csnmp_value_list_to_value (vb, ds->ds[i].type,
            data->scale, data->shift, host->name, data->name);
  } /* for (res->variables) */

  if (res != NULL)
    snmp_free_pdu (res);
  res = NULL;

  DEBUG ("snmp plugin: -> plugin_dispatch_values (&vl);");
  plugin_dispatch_values (&vl);
  sfree (vl.values);

  return (0);
} /* int csnmp_read_value */

static int csnmp_read_host (user_data_t *ud)
{
  host_definition_t *host;
  cdtime_t time_start;
  cdtime_t time_end;
  int status;
  int success;
  int i;

  host = ud->data;

  if (host->interval == 0)
    host->interval = plugin_get_interval ();

  time_start = cdtime ();

  if (host->sess_handle == NULL)
    csnmp_host_open_session (host);

  if (host->sess_handle == NULL)
    return (-1);

  success = 0;
  for (i = 0; i < host->data_list_len; i++)
  {
    data_definition_t *data = host->data_list[i];

    if (data->is_table)
      status = csnmp_read_table (host, data);
    else
      status = csnmp_read_value (host, data);

    if (status == 0)
      success++;
  }

  time_end = cdtime ();
  if ((time_end - time_start) > host->interval)
  {
    WARNING ("snmp plugin: Host `%s' should be queried every %.3f "
        "seconds, but reading all values takes %.3f seconds.",
        host->name,
        CDTIME_T_TO_DOUBLE (host->interval),
        CDTIME_T_TO_DOUBLE (time_end - time_start));
  }

  if (success == 0)
    return (-1);

  return (0);
} /* int csnmp_read_host */

static int csnmp_init (void)
{
  call_snmp_init_once ();

  return (0);
} /* int csnmp_init */

static int csnmp_shutdown (void)
{
  data_definition_t *data_this;
  data_definition_t *data_next;

  /* When we get here, the read threads have been stopped and all the
   * `host_definition_t' will be freed. */
  DEBUG ("snmp plugin: Destroying all data definitions.");

  data_this = data_head;
  data_head = NULL;
  while (data_this != NULL)
  {
    data_next = data_this->next;

    sfree (data_this->name);
    sfree (data_this->type);
    sfree (data_this->values);
    sfree (data_this->ignores);
    sfree (data_this);

    data_this = data_next;
  }

  return (0);
} /* int csnmp_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("snmp", csnmp_config);
  plugin_register_init ("snmp", csnmp_init);
  plugin_register_shutdown ("snmp", csnmp_shutdown);
} /* void module_register */

/*
 * vim: shiftwidth=2 softtabstop=2 tabstop=8 fdm=marker
 */
