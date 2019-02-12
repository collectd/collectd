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

#include "plugin.h"
#include "utils/common/common.h"
#include "utils/ignorelist/ignorelist.h"
#include "utils_complain.h"

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <fnmatch.h>

/*
 * Private data structes
 */
struct oid_s {
  oid oid[MAX_OID_LEN];
  size_t oid_len;
};
typedef struct oid_s oid_t;

struct instance_s {
  bool configured;
  oid_t oid;
  char *prefix;
  char *value;
};
typedef struct instance_s instance_t;

struct data_definition_s {
  char *name; /* used to reference this from the `Collect' option */
  char *type; /* used to find the data_set */
  bool is_table;
  instance_t type_instance;
  instance_t plugin_instance;
  instance_t host;
  oid_t filter_oid;
  ignorelist_t *ignorelist;
  char *plugin_name;
  oid_t *values;
  size_t values_len;
  double scale;
  double shift;
  struct data_definition_s *next;
  char **ignores;
  size_t ignores_len;
  bool invert_match;
};
typedef struct data_definition_s data_definition_t;

struct host_definition_s {
  char *name;
  char *address;
  int version;
  cdtime_t timeout;
  int retries;

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
  data_definition_t **data_list;
  int data_list_len;
};
typedef struct host_definition_s host_definition_t;

/* These two types are used to cache values in `csnmp_read_table' to handle
 * gaps in tables. */
struct csnmp_cell_char_s {
  oid_t suffix;
  char value[DATA_MAX_NAME_LEN];
  struct csnmp_cell_char_s *next;
};
typedef struct csnmp_cell_char_s csnmp_cell_char_t;

struct csnmp_cell_value_s {
  oid_t suffix;
  value_t value;
  struct csnmp_cell_value_s *next;
};
typedef struct csnmp_cell_value_s csnmp_cell_value_t;

typedef enum {
  OID_TYPE_SKIP = 0,
  OID_TYPE_VARIABLE,
  OID_TYPE_TYPEINSTANCE,
  OID_TYPE_PLUGININSTANCE,
  OID_TYPE_HOST,
  OID_TYPE_FILTER,
} csnmp_oid_type_t;

/*
 * Private variables
 */
static data_definition_t *data_head;

/*
 * Prototypes
 */
static int csnmp_read_host(user_data_t *ud);

/*
 * Private functions
 */
static void csnmp_oid_init(oid_t *dst, oid const *src, size_t n) {
  assert(n <= STATIC_ARRAY_SIZE(dst->oid));
  memcpy(dst->oid, src, sizeof(*src) * n);
  dst->oid_len = n;
}

static int csnmp_oid_compare(oid_t const *left, oid_t const *right) {
  return snmp_oid_compare(left->oid, left->oid_len, right->oid, right->oid_len);
}

static int csnmp_oid_suffix(oid_t *dst, oid_t const *src, oid_t const *root) {
  /* Make sure "src" is in "root"s subtree. */
  if (src->oid_len <= root->oid_len)
    return EINVAL;
  if (snmp_oid_ncompare(root->oid, root->oid_len, src->oid, src->oid_len,
                        /* n = */ root->oid_len) != 0)
    return EINVAL;

  memset(dst, 0, sizeof(*dst));
  dst->oid_len = src->oid_len - root->oid_len;
  memcpy(dst->oid, &src->oid[root->oid_len],
         dst->oid_len * sizeof(dst->oid[0]));
  return 0;
}

static int csnmp_oid_to_string(char *buffer, size_t buffer_size,
                               oid_t const *o) {
  char oid_str[MAX_OID_LEN][16];
  char *oid_str_ptr[MAX_OID_LEN];

  for (size_t i = 0; i < o->oid_len; i++) {
    snprintf(oid_str[i], sizeof(oid_str[i]), "%lu", (unsigned long)o->oid[i]);
    oid_str_ptr[i] = oid_str[i];
  }

  return strjoin(buffer, buffer_size, oid_str_ptr, o->oid_len, ".");
}

static void csnmp_host_close_session(host_definition_t *host) /* {{{ */
{
  if (host->sess_handle == NULL)
    return;

  snmp_sess_close(host->sess_handle);
  host->sess_handle = NULL;
} /* }}} void csnmp_host_close_session */

static void csnmp_host_definition_destroy(void *arg) /* {{{ */
{
  host_definition_t *hd;

  hd = arg;

  if (hd == NULL)
    return;

  if (hd->name != NULL) {
    DEBUG("snmp plugin: Destroying host definition for host `%s'.", hd->name);
  }

  csnmp_host_close_session(hd);

  sfree(hd->name);
  sfree(hd->address);
  sfree(hd->community);
  sfree(hd->username);
  sfree(hd->auth_passphrase);
  sfree(hd->priv_passphrase);
  sfree(hd->context);
  sfree(hd->data_list);

  sfree(hd);
} /* }}} void csnmp_host_definition_destroy */

static void csnmp_data_definition_destroy(data_definition_t *dd) {
  sfree(dd->name);
  sfree(dd->type);
  sfree(dd->plugin_name);
  sfree(dd->plugin_instance.prefix);
  sfree(dd->plugin_instance.value);
  sfree(dd->type_instance.prefix);
  sfree(dd->type_instance.value);
  sfree(dd->host.prefix);
  sfree(dd->host.value);
  sfree(dd->values);
  sfree(dd->ignores);
  ignorelist_free(dd->ignorelist);
  sfree(dd);
} /* void csnmp_data_definition_destroy */

/* Many functions to handle the configuration. {{{ */
/* First there are many functions which do configuration stuff. It's a big
 * bloated and messy, I'm afraid. */

/*
 * Callgraph for the config stuff:
 *  csnmp_config
 *  +-> call_snmp_init_once
 *  +-> csnmp_config_add_data
 *  !   +-> csnmp_config_configure_data_instance
 *  !   +-> csnmp_config_add_data_values
 *  +-> csnmp_config_add_host
 *      +-> csnmp_config_add_host_version
 *      +-> csnmp_config_add_host_collect
 *      +-> csnmp_config_add_host_auth_protocol
 *      +-> csnmp_config_add_host_priv_protocol
 *      +-> csnmp_config_add_host_security_level
 */
static void call_snmp_init_once(void) {
  static int have_init;

  if (have_init == 0)
    init_snmp(PACKAGE_NAME);
  have_init = 1;
} /* void call_snmp_init_once */

static int csnmp_config_configure_data_instance(instance_t *instance,
                                                oconfig_item_t *ci) {
  char buffer[DATA_MAX_NAME_LEN];

  int status = cf_util_get_string_buffer(ci, buffer, sizeof(buffer));
  if (status != 0)
    return status;

  instance->configured = true;

  if (strlen(buffer) == 0) {
    return 0;
  }

  instance->oid.oid_len = MAX_OID_LEN;

  if (!read_objid(buffer, instance->oid.oid, &instance->oid.oid_len)) {
    ERROR("snmp plugin: read_objid (%s) failed.", buffer);
    return -1;
  }

  return 0;
} /* int csnmp_config_configure_data_instance */

static int csnmp_config_add_data_values(data_definition_t *dd,
                                        oconfig_item_t *ci) {
  if (ci->values_num < 1) {
    WARNING("snmp plugin: `Values' needs at least one argument.");
    return -1;
  }

  for (int i = 0; i < ci->values_num; i++)
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING("snmp plugin: `Values' needs only string argument.");
      return -1;
    }

  sfree(dd->values);
  dd->values_len = 0;
  dd->values = malloc(sizeof(*dd->values) * ci->values_num);
  if (dd->values == NULL)
    return -1;
  dd->values_len = (size_t)ci->values_num;

  for (int i = 0; i < ci->values_num; i++) {
    dd->values[i].oid_len = MAX_OID_LEN;

    if (NULL == snmp_parse_oid(ci->values[i].value.string, dd->values[i].oid,
                               &dd->values[i].oid_len)) {
      ERROR("snmp plugin: snmp_parse_oid (%s) failed.",
            ci->values[i].value.string);
      free(dd->values);
      dd->values = NULL;
      dd->values_len = 0;
      return -1;
    }
  }

  return 0;
} /* int csnmp_config_configure_data_instance */

static int csnmp_config_add_data_blacklist(data_definition_t *dd,
                                           oconfig_item_t *ci) {
  if (ci->values_num < 1)
    return 0;

  for (int i = 0; i < ci->values_num; i++) {
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING("snmp plugin: `Ignore' needs only string argument.");
      return -1;
    }
  }

  for (int i = 0; i < ci->values_num; ++i) {
    if (strarray_add(&(dd->ignores), &(dd->ignores_len),
                     ci->values[i].value.string) != 0) {
      ERROR("snmp plugin: Can't allocate memory");
      strarray_free(dd->ignores, dd->ignores_len);
      return ENOMEM;
    }
  }
  return 0;
} /* int csnmp_config_add_data_blacklist */

static int csnmp_config_add_data_filter_values(data_definition_t *data,
                                               oconfig_item_t *ci) {
  if (ci->values_num < 1) {
    WARNING("snmp plugin: `FilterValues' needs at least one argument.");
    return -1;
  }

  for (int i = 0; i < ci->values_num; i++) {
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING("snmp plugin: All arguments to `FilterValues' must be strings.");
      return -1;
    }
    ignorelist_add(data->ignorelist, ci->values[i].value.string);
  }

  return 0;
} /* int csnmp_config_add_data_filter_values */

static int csnmp_config_add_data_filter_oid(data_definition_t *data,
                                            oconfig_item_t *ci) {

  char buffer[DATA_MAX_NAME_LEN];
  int status = cf_util_get_string_buffer(ci, buffer, sizeof(buffer));
  if (status != 0)
    return status;

  data->filter_oid.oid_len = MAX_OID_LEN;

  if (!read_objid(buffer, data->filter_oid.oid, &data->filter_oid.oid_len)) {
    ERROR("snmp plugin: read_objid (%s) failed.", buffer);
    return -1;
  }
  return 0;
} /* int csnmp_config_add_data_filter_oid */

static int csnmp_config_add_data(oconfig_item_t *ci) {
  data_definition_t *dd = calloc(1, sizeof(*dd));
  if (dd == NULL)
    return -1;

  int status = cf_util_get_string(ci, &dd->name);
  if (status != 0) {
    sfree(dd);
    return -1;
  }

  dd->scale = 1.0;
  dd->shift = 0.0;
  dd->ignores_len = 0;
  dd->ignores = NULL;

  dd->ignorelist = ignorelist_create(/* invert = */ 1);
  if (dd->ignorelist == NULL) {
    sfree(dd->name);
    sfree(dd);
    ERROR("snmp plugin: ignorelist_create() failed.");
    return ENOMEM;
  }

  dd->plugin_name = strdup("snmp");
  if (dd->plugin_name == NULL) {
    ERROR("snmp plugin: Can't allocate memory");
    return ENOMEM;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Type", option->key) == 0)
      status = cf_util_get_string(option, &dd->type);
    else if (strcasecmp("Table", option->key) == 0)
      status = cf_util_get_boolean(option, &dd->is_table);
    else if (strcasecmp("Plugin", option->key) == 0)
      status = cf_util_get_string(option, &dd->plugin_name);
    else if (strcasecmp("Instance", option->key) == 0) {
      if (dd->is_table) {
        /* Instance is OID */
        WARNING(
            "snmp plugin: data %s: Option `Instance' is deprecated, please use "
            "option `TypeInstanceOID'.",
            dd->name);
        status =
            csnmp_config_configure_data_instance(&dd->type_instance, option);
      } else {
        /* Instance is a simple string */
        WARNING(
            "snmp plugin: data %s: Option `Instance' is deprecated, please use "
            "option `TypeInstance'.",
            dd->name);
        status = cf_util_get_string(option, &dd->type_instance.value);
      }
    } else if (strcasecmp("InstancePrefix", option->key) == 0) {
      WARNING("snmp plugin: data %s: Option `InstancePrefix' is deprecated, "
              "please use option `TypeInstancePrefix'.",
              dd->name);
      status = cf_util_get_string(option, &dd->type_instance.prefix);
    } else if (strcasecmp("PluginInstance", option->key) == 0)
      status = cf_util_get_string(option, &dd->plugin_instance.value);
    else if (strcasecmp("TypeInstance", option->key) == 0)
      status = cf_util_get_string(option, &dd->type_instance.value);
    else if (strcasecmp("PluginInstanceOID", option->key) == 0)
      status =
          csnmp_config_configure_data_instance(&dd->plugin_instance, option);
    else if (strcasecmp("PluginInstancePrefix", option->key) == 0)
      status = cf_util_get_string(option, &dd->plugin_instance.prefix);
    else if (strcasecmp("TypeInstanceOID", option->key) == 0)
      status = csnmp_config_configure_data_instance(&dd->type_instance, option);
    else if (strcasecmp("TypeInstancePrefix", option->key) == 0)
      status = cf_util_get_string(option, &dd->type_instance.prefix);
    else if (strcasecmp("HostOID", option->key) == 0)
      status = csnmp_config_configure_data_instance(&dd->host, option);
    else if (strcasecmp("HostPrefix", option->key) == 0)
      status = cf_util_get_string(option, &dd->host.prefix);
    else if (strcasecmp("Values", option->key) == 0)
      status = csnmp_config_add_data_values(dd, option);
    else if (strcasecmp("Shift", option->key) == 0)
      status = cf_util_get_double(option, &dd->shift);
    else if (strcasecmp("Scale", option->key) == 0)
      status = cf_util_get_double(option, &dd->scale);
    else if (strcasecmp("Ignore", option->key) == 0)
      status = csnmp_config_add_data_blacklist(dd, option);
    else if (strcasecmp("InvertMatch", option->key) == 0)
      status = cf_util_get_boolean(option, &dd->invert_match);
    else if (strcasecmp("FilterOID", option->key) == 0) {
      status = csnmp_config_add_data_filter_oid(dd, option);
    } else if (strcasecmp("FilterValues", option->key) == 0) {
      status = csnmp_config_add_data_filter_values(dd, option);
    } else if (strcasecmp("FilterIgnoreSelected", option->key) == 0) {
      bool t;
      status = cf_util_get_boolean(option, &t);
      if (status == 0)
        ignorelist_set_invert(dd->ignorelist, /* invert = */ !t);
    } else {
      WARNING("snmp plugin: data %s: Option `%s' not allowed here.", dd->name,
              option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (ci->children) */

  while (status == 0) {
    if (dd->is_table) {
      /* Set type_instance to SUBID by default */
      if (!dd->plugin_instance.configured && !dd->host.configured)
        dd->type_instance.configured = true;

      if (dd->plugin_instance.value && dd->plugin_instance.configured) {
        WARNING(
            "snmp plugin: data %s: Option `PluginInstance' will be ignored.",
            dd->name);
      }
      if (dd->type_instance.value && dd->type_instance.configured) {
        WARNING("snmp plugin: data %s: Option `TypeInstance' will be ignored.",
                dd->name);
      }
      if (dd->type_instance.prefix && !dd->type_instance.configured) {
        WARNING("snmp plugin: data %s: Option `TypeInstancePrefix' will be "
                "ignored.",
                dd->name);
      }
      if (dd->plugin_instance.prefix && !dd->plugin_instance.configured) {
        WARNING("snmp plugin: data %s: Option `PluginInstancePrefix' will be "
                "ignored.",
                dd->name);
      }
      if (dd->host.prefix && !dd->host.configured) {
        WARNING("snmp plugin: data %s: Option `HostPrefix' will be ignored.",
                dd->name);
      }
    } else {
      if (dd->plugin_instance.oid.oid_len > 0) {
        WARNING("snmp plugin: data %s: Option `PluginInstanceOID' will be "
                "ignored.",
                dd->name);
      }
      if (dd->type_instance.oid.oid_len > 0) {
        WARNING(
            "snmp plugin: data %s: Option `TypeInstanceOID' will be ignored.",
            dd->name);
      }
      if (dd->type_instance.prefix) {
        WARNING("snmp plugin: data %s: Option `TypeInstancePrefix' is ignored "
                "when `Table' "
                "set to `false'.",
                dd->name);
      }
      if (dd->plugin_instance.prefix) {
        WARNING("snmp plugin: data %s: Option `PluginInstancePrefix' is "
                "ignored when "
                "`Table' set to `false'.",
                dd->name);
      }
      if (dd->host.prefix) {
        WARNING(
            "snmp plugin: data %s: Option `HostPrefix' is ignored when `Table' "
            "set to `false'.",
            dd->name);
      }
    }

    if (dd->type == NULL) {
      WARNING("snmp plugin: `Type' not given for data `%s'", dd->name);
      status = -1;
      break;
    }
    if (dd->values == NULL) {
      WARNING("snmp plugin: No `Value' given for data `%s'", dd->name);
      status = -1;
      break;
    }

    break;
  } /* while (status == 0) */

  if (status != 0) {
    csnmp_data_definition_destroy(dd);
    return -1;
  }

  DEBUG("snmp plugin: dd = { name = %s, type = %s, is_table = %s, values_len = "
        "%" PRIsz ",",
        dd->name, dd->type, (dd->is_table) ? "true" : "false", dd->values_len);

  DEBUG("snmp plugin:        plugin_instance = %s, type_instance = %s,",
        dd->plugin_instance.value, dd->type_instance.value);

  DEBUG("snmp plugin:        type_instance_by_oid = %s, plugin_instance_by_oid "
        "= %s }",
        (dd->type_instance.oid.oid_len > 0)
            ? "true"
            : ((dd->type_instance.configured) ? "SUBID" : "false"),
        (dd->plugin_instance.oid.oid_len > 0)
            ? "true"
            : ((dd->plugin_instance.configured) ? "SUBID" : "false"));

  if (data_head == NULL)
    data_head = dd;
  else {
    data_definition_t *last;
    last = data_head;
    while (last->next != NULL)
      last = last->next;
    last->next = dd;
  }

  return 0;
} /* int csnmp_config_add_data */

static int csnmp_config_add_host_version(host_definition_t *hd,
                                         oconfig_item_t *ci) {
  int version;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER)) {
    WARNING("snmp plugin: The `Version' config option needs exactly one number "
            "argument.");
    return -1;
  }

  version = (int)ci->values[0].value.number;
  if ((version < 1) || (version > 3)) {
    WARNING("snmp plugin: `Version' must either be `1', `2', or `3'.");
    return -1;
  }

  hd->version = version;

  return 0;
} /* int csnmp_config_add_host_version */

static int csnmp_config_add_host_collect(host_definition_t *host,
                                         oconfig_item_t *ci) {
  data_definition_t *data;
  data_definition_t **data_list;
  int data_list_len;

  if (ci->values_num < 1) {
    WARNING("snmp plugin: `Collect' needs at least one argument.");
    return -1;
  }

  for (int i = 0; i < ci->values_num; i++)
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING("snmp plugin: All arguments to `Collect' must be strings.");
      return -1;
    }

  data_list_len = host->data_list_len + ci->values_num;
  data_list =
      realloc(host->data_list, sizeof(data_definition_t *) * data_list_len);
  if (data_list == NULL)
    return -1;
  host->data_list = data_list;

  for (int i = 0; i < ci->values_num; i++) {
    for (data = data_head; data != NULL; data = data->next)
      if (strcasecmp(ci->values[i].value.string, data->name) == 0)
        break;

    if (data == NULL) {
      WARNING("snmp plugin: No such data configured: `%s'",
              ci->values[i].value.string);
      continue;
    }

    DEBUG("snmp plugin: Collect: host = %s, data[%i] = %s;", host->name,
          host->data_list_len, data->name);

    host->data_list[host->data_list_len] = data;
    host->data_list_len++;
  } /* for (values_num) */

  return 0;
} /* int csnmp_config_add_host_collect */

static int csnmp_config_add_host_auth_protocol(host_definition_t *hd,
                                               oconfig_item_t *ci) {
  char buffer[4];
  int status;

  status = cf_util_get_string_buffer(ci, buffer, sizeof(buffer));
  if (status != 0)
    return status;

  if (strcasecmp("MD5", buffer) == 0) {
    hd->auth_protocol = usmHMACMD5AuthProtocol;
    hd->auth_protocol_len = sizeof(usmHMACMD5AuthProtocol) / sizeof(oid);
  } else if (strcasecmp("SHA", buffer) == 0) {
    hd->auth_protocol = usmHMACSHA1AuthProtocol;
    hd->auth_protocol_len = sizeof(usmHMACSHA1AuthProtocol) / sizeof(oid);
  } else {
    WARNING("snmp plugin: The `AuthProtocol' config option must be `MD5' or "
            "`SHA'.");
    return -1;
  }

  DEBUG("snmp plugin: host = %s; host->auth_protocol = %s;", hd->name,
        hd->auth_protocol == usmHMACMD5AuthProtocol ? "MD5" : "SHA");

  return 0;
} /* int csnmp_config_add_host_auth_protocol */

static int csnmp_config_add_host_priv_protocol(host_definition_t *hd,
                                               oconfig_item_t *ci) {
  char buffer[4];
  int status;

  status = cf_util_get_string_buffer(ci, buffer, sizeof(buffer));
  if (status != 0)
    return status;

  if (strcasecmp("AES", buffer) == 0) {
    hd->priv_protocol = usmAESPrivProtocol;
    hd->priv_protocol_len = sizeof(usmAESPrivProtocol) / sizeof(oid);
  } else if (strcasecmp("DES", buffer) == 0) {
    hd->priv_protocol = usmDESPrivProtocol;
    hd->priv_protocol_len = sizeof(usmDESPrivProtocol) / sizeof(oid);
  } else {
    WARNING("snmp plugin: The `PrivProtocol' config option must be `AES' or "
            "`DES'.");
    return -1;
  }

  DEBUG("snmp plugin: host = %s; host->priv_protocol = %s;", hd->name,
        hd->priv_protocol == usmAESPrivProtocol ? "AES" : "DES");

  return 0;
} /* int csnmp_config_add_host_priv_protocol */

static int csnmp_config_add_host_security_level(host_definition_t *hd,
                                                oconfig_item_t *ci) {
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
  else {
    WARNING("snmp plugin: The `SecurityLevel' config option must be "
            "`noAuthNoPriv', `authNoPriv', or `authPriv'.");
    return -1;
  }

  DEBUG("snmp plugin: host = %s; host->security_level = %d;", hd->name,
        hd->security_level);

  return 0;
} /* int csnmp_config_add_host_security_level */

static int csnmp_config_add_host(oconfig_item_t *ci) {
  host_definition_t *hd;
  int status = 0;

  /* Registration stuff. */
  cdtime_t interval = 0;
  char cb_name[DATA_MAX_NAME_LEN];

  hd = calloc(1, sizeof(*hd));
  if (hd == NULL)
    return -1;
  hd->version = 2;
  C_COMPLAIN_INIT(&hd->complaint);

  status = cf_util_get_string(ci, &hd->name);
  if (status != 0) {
    sfree(hd);
    return status;
  }

  hd->sess_handle = NULL;

  /* These mean that we have not set a timeout or retry value */
  hd->timeout = 0;
  hd->retries = -1;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Address", option->key) == 0)
      status = cf_util_get_string(option, &hd->address);
    else if (strcasecmp("Community", option->key) == 0)
      status = cf_util_get_string(option, &hd->community);
    else if (strcasecmp("Version", option->key) == 0)
      status = csnmp_config_add_host_version(hd, option);
    else if (strcasecmp("Timeout", option->key) == 0)
      status = cf_util_get_cdtime(option, &hd->timeout);
    else if (strcasecmp("Retries", option->key) == 0)
      status = cf_util_get_int(option, &hd->retries);
    else if (strcasecmp("Collect", option->key) == 0)
      status = csnmp_config_add_host_collect(hd, option);
    else if (strcasecmp("Interval", option->key) == 0)
      status = cf_util_get_cdtime(option, &interval);
    else if (strcasecmp("Username", option->key) == 0)
      status = cf_util_get_string(option, &hd->username);
    else if (strcasecmp("AuthProtocol", option->key) == 0)
      status = csnmp_config_add_host_auth_protocol(hd, option);
    else if (strcasecmp("PrivacyProtocol", option->key) == 0)
      status = csnmp_config_add_host_priv_protocol(hd, option);
    else if (strcasecmp("AuthPassphrase", option->key) == 0)
      status = cf_util_get_string(option, &hd->auth_passphrase);
    else if (strcasecmp("PrivacyPassphrase", option->key) == 0)
      status = cf_util_get_string(option, &hd->priv_passphrase);
    else if (strcasecmp("SecurityLevel", option->key) == 0)
      status = csnmp_config_add_host_security_level(hd, option);
    else if (strcasecmp("Context", option->key) == 0)
      status = cf_util_get_string(option, &hd->context);
    else {
      WARNING(
          "snmp plugin: csnmp_config_add_host: Option `%s' not allowed here.",
          option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (ci->children) */

  while (status == 0) {
    if (hd->address == NULL) {
      WARNING("snmp plugin: `Address' not given for host `%s'", hd->name);
      status = -1;
      break;
    }
    if (hd->community == NULL && hd->version < 3) {
      WARNING("snmp plugin: `Community' not given for host `%s'", hd->name);
      status = -1;
      break;
    }
    if (hd->version == 3) {
      if (hd->username == NULL) {
        WARNING("snmp plugin: `Username' not given for host `%s'", hd->name);
        status = -1;
        break;
      }
      if (hd->security_level == 0) {
        WARNING("snmp plugin: `SecurityLevel' not given for host `%s'",
                hd->name);
        status = -1;
        break;
      }
      if (hd->security_level == SNMP_SEC_LEVEL_AUTHNOPRIV ||
          hd->security_level == SNMP_SEC_LEVEL_AUTHPRIV) {
        if (hd->auth_protocol == NULL) {
          WARNING("snmp plugin: `AuthProtocol' not given for host `%s'",
                  hd->name);
          status = -1;
          break;
        }
        if (hd->auth_passphrase == NULL) {
          WARNING("snmp plugin: `AuthPassphrase' not given for host `%s'",
                  hd->name);
          status = -1;
          break;
        }
      }
      if (hd->security_level == SNMP_SEC_LEVEL_AUTHPRIV) {
        if (hd->priv_protocol == NULL) {
          WARNING("snmp plugin: `PrivacyProtocol' not given for host `%s'",
                  hd->name);
          status = -1;
          break;
        }
        if (hd->priv_passphrase == NULL) {
          WARNING("snmp plugin: `PrivacyPassphrase' not given for host `%s'",
                  hd->name);
          status = -1;
          break;
        }
      }
    }

    break;
  } /* while (status == 0) */

  if (status != 0) {
    csnmp_host_definition_destroy(hd);
    return -1;
  }

  DEBUG("snmp plugin: hd = { name = %s, address = %s, community = %s, version "
        "= %i }",
        hd->name, hd->address, hd->community, hd->version);

  snprintf(cb_name, sizeof(cb_name), "snmp-%s", hd->name);

  status = plugin_register_complex_read(
      /* group = */ NULL, cb_name, csnmp_read_host, interval,
      &(user_data_t){
          .data = hd, .free_func = csnmp_host_definition_destroy,
      });
  if (status != 0) {
    ERROR("snmp plugin: Registering complex read function failed.");
    return -1;
  }

  return 0;
} /* int csnmp_config_add_host */

static int csnmp_config(oconfig_item_t *ci) {
  call_snmp_init_once();

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Data", child->key) == 0)
      csnmp_config_add_data(child);
    else if (strcasecmp("Host", child->key) == 0)
      csnmp_config_add_host(child);
    else {
      WARNING("snmp plugin: Ignoring unknown config option `%s'.", child->key);
    }
  } /* for (ci->children) */

  return 0;
} /* int csnmp_config */

/* }}} End of the config stuff. Now the interesting part begins */

static void csnmp_host_open_session(host_definition_t *host) {
  struct snmp_session sess;
  int error;

  if (host->sess_handle != NULL)
    csnmp_host_close_session(host);

  snmp_sess_init(&sess);
  sess.peername = host->address;
  switch (host->version) {
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

  if (host->version == 3) {
    sess.securityName = host->username;
    sess.securityNameLen = strlen(host->username);
    sess.securityLevel = host->security_level;

    if (sess.securityLevel == SNMP_SEC_LEVEL_AUTHNOPRIV ||
        sess.securityLevel == SNMP_SEC_LEVEL_AUTHPRIV) {
      sess.securityAuthProto = host->auth_protocol;
      sess.securityAuthProtoLen = host->auth_protocol_len;
      sess.securityAuthKeyLen = USM_AUTH_KU_LEN;
      error = generate_Ku(sess.securityAuthProto, sess.securityAuthProtoLen,
                          (u_char *)host->auth_passphrase,
                          strlen(host->auth_passphrase), sess.securityAuthKey,
                          &sess.securityAuthKeyLen);
      if (error != SNMPERR_SUCCESS) {
        ERROR("snmp plugin: host %s: Error generating Ku from auth_passphrase. "
              "(Error %d)",
              host->name, error);
      }
    }

    if (sess.securityLevel == SNMP_SEC_LEVEL_AUTHPRIV) {
      sess.securityPrivProto = host->priv_protocol;
      sess.securityPrivProtoLen = host->priv_protocol_len;
      sess.securityPrivKeyLen = USM_PRIV_KU_LEN;
      error = generate_Ku(sess.securityAuthProto, sess.securityAuthProtoLen,
                          (u_char *)host->priv_passphrase,
                          strlen(host->priv_passphrase), sess.securityPrivKey,
                          &sess.securityPrivKeyLen);
      if (error != SNMPERR_SUCCESS) {
        ERROR("snmp plugin: host %s: Error generating Ku from priv_passphrase. "
              "(Error %d)",
              host->name, error);
      }
    }

    if (host->context != NULL) {
      sess.contextName = host->context;
      sess.contextNameLen = strlen(host->context);
    }
  } else /* SNMPv1/2 "authenticates" with community string */
  {
    sess.community = (u_char *)host->community;
    sess.community_len = strlen(host->community);
  }

  /* Set timeout & retries, if they have been changed from the default */
  if (host->timeout != 0) {
    /* net-snmp expects microseconds */
    sess.timeout = CDTIME_T_TO_US(host->timeout);
  }
  if (host->retries >= 0) {
    sess.retries = host->retries;
  }

  /* snmp_sess_open will copy the `struct snmp_session *'. */
  host->sess_handle = snmp_sess_open(&sess);

  if (host->sess_handle == NULL) {
    char *errstr = NULL;

    snmp_error(&sess, NULL, NULL, &errstr);

    ERROR("snmp plugin: host %s: snmp_sess_open failed: %s", host->name,
          (errstr == NULL) ? "Unknown problem" : errstr);
    sfree(errstr);
  }
} /* void csnmp_host_open_session */

/* TODO: Check if negative values wrap around. Problem: negative temperatures.
 */
static value_t csnmp_value_list_to_value(const struct variable_list *vl,
                                         int type, double scale, double shift,
                                         const char *host_name,
                                         const char *data_name) {
  value_t ret;
  uint64_t tmp_unsigned = 0;
  int64_t tmp_signed = 0;
  bool defined = 1;
  /* Set to true when the original SNMP type appears to have been signed. */
  bool prefer_signed = 0;

  if ((vl->type == ASN_INTEGER) || (vl->type == ASN_UINTEGER) ||
      (vl->type == ASN_COUNTER)
#ifdef ASN_TIMETICKS
      || (vl->type == ASN_TIMETICKS)
#endif
      || (vl->type == ASN_GAUGE)) {
    tmp_unsigned = (uint32_t)*vl->val.integer;
    tmp_signed = (int32_t)*vl->val.integer;

    if (vl->type == ASN_INTEGER)
      prefer_signed = 1;

    DEBUG("snmp plugin: Parsed int32 value is %" PRIu64 ".", tmp_unsigned);
  } else if (vl->type == ASN_COUNTER64) {
    tmp_unsigned = (uint32_t)vl->val.counter64->high;
    tmp_unsigned = tmp_unsigned << 32;
    tmp_unsigned += (uint32_t)vl->val.counter64->low;
    tmp_signed = (int64_t)tmp_unsigned;
    DEBUG("snmp plugin: Parsed int64 value is %" PRIu64 ".", tmp_unsigned);
  } else if (vl->type == ASN_OCTET_STR) {
    /* We'll handle this later.. */
  } else {
    char oid_buffer[1024] = {0};

    snprint_objid(oid_buffer, sizeof(oid_buffer) - 1, vl->name,
                  vl->name_length);

#ifdef ASN_NULL
    if (vl->type == ASN_NULL)
      INFO("snmp plugin: OID \"%s\" is undefined (type ASN_NULL)", oid_buffer);
    else
#endif
      WARNING("snmp plugin: I don't know the ASN type #%i "
              "(OID: \"%s\", data block \"%s\", host block \"%s\")",
              (int)vl->type, oid_buffer,
              (data_name != NULL) ? data_name : "UNKNOWN",
              (host_name != NULL) ? host_name : "UNKNOWN");

    defined = 0;
  }

  if (vl->type == ASN_OCTET_STR) {
    int status = -1;

    if (vl->val.string != NULL) {
      char string[64];
      size_t string_length;

      string_length = sizeof(string) - 1;
      if (vl->val_len < string_length)
        string_length = vl->val_len;

      /* The strings we get from the Net-SNMP library may not be null
       * terminated. That is why we're using `memcpy' here and not `strcpy'.
       * `string_length' is set to `vl->val_len' which holds the length of the
       * string.  -octo */
      memcpy(string, vl->val.string, string_length);
      string[string_length] = 0;

      status = parse_value(string, &ret, type);
      if (status != 0) {
        ERROR("snmp plugin: host %s: csnmp_value_list_to_value: Parsing string "
              "as %s failed: %s",
              (host_name != NULL) ? host_name : "UNKNOWN",
              DS_TYPE_TO_STRING(type), string);
      }
    }

    if (status != 0) {
      switch (type) {
      case DS_TYPE_COUNTER:
      case DS_TYPE_DERIVE:
      case DS_TYPE_ABSOLUTE:
        memset(&ret, 0, sizeof(ret));
        break;

      case DS_TYPE_GAUGE:
        ret.gauge = NAN;
        break;

      default:
        ERROR("snmp plugin: csnmp_value_list_to_value: Unknown "
              "data source type: %i.",
              type);
        ret.gauge = NAN;
      }
    }
  } /* if (vl->type == ASN_OCTET_STR) */
  else if (type == DS_TYPE_COUNTER) {
    ret.counter = tmp_unsigned;
  } else if (type == DS_TYPE_GAUGE) {
    if (!defined)
      ret.gauge = NAN;
    else if (prefer_signed)
      ret.gauge = (scale * tmp_signed) + shift;
    else
      ret.gauge = (scale * tmp_unsigned) + shift;
  } else if (type == DS_TYPE_DERIVE) {
    if (prefer_signed)
      ret.derive = (derive_t)tmp_signed;
    else
      ret.derive = (derive_t)tmp_unsigned;
  } else if (type == DS_TYPE_ABSOLUTE) {
    ret.absolute = (absolute_t)tmp_unsigned;
  } else {
    ERROR("snmp plugin: csnmp_value_list_to_value: Unknown data source "
          "type: %i.",
          type);
    ret.gauge = NAN;
  }

  return ret;
} /* value_t csnmp_value_list_to_value */

/* csnmp_strvbcopy_hexstring converts the bit string contained in "vb" to a hex
 * representation and writes it to dst. Returns zero on success and ENOMEM if
 * dst is not large enough to hold the string. dst is guaranteed to be
 * nul-terminated. */
static int csnmp_strvbcopy_hexstring(char *dst, /* {{{ */
                                     const struct variable_list *vb,
                                     size_t dst_size) {
  char *buffer_ptr;
  size_t buffer_free;

  dst[0] = 0;

  buffer_ptr = dst;
  buffer_free = dst_size;

  for (size_t i = 0; i < vb->val_len; i++) {
    int status;

    status = snprintf(buffer_ptr, buffer_free, (i == 0) ? "%02x" : ":%02x",
                      (unsigned int)vb->val.bitstring[i]);
    assert(status >= 0);

    if (((size_t)status) >= buffer_free) /* truncated */
    {
      dst[dst_size - 1] = '\0';
      return ENOMEM;
    } else /* if (status < buffer_free) */
    {
      buffer_ptr += (size_t)status;
      buffer_free -= (size_t)status;
    }
  }

  return 0;
} /* }}} int csnmp_strvbcopy_hexstring */

/* csnmp_strvbcopy copies the octet string or bit string contained in vb to
 * dst. If non-printable characters are detected, it will switch to a hex
 * representation of the string. Returns zero on success, EINVAL if vb does not
 * contain a string and ENOMEM if dst is not large enough to contain the
 * string. */
static int csnmp_strvbcopy(char *dst, /* {{{ */
                           const struct variable_list *vb, size_t dst_size) {
  char *src;
  size_t num_chars;

  if (vb->type == ASN_OCTET_STR)
    src = (char *)vb->val.string;
  else if (vb->type == ASN_BIT_STR)
    src = (char *)vb->val.bitstring;
  else if (vb->type == ASN_IPADDRESS) {
    return snprintf(dst, dst_size,
                    "%" PRIu8 ".%" PRIu8 ".%" PRIu8 ".%" PRIu8 "",
                    (uint8_t)vb->val.string[0], (uint8_t)vb->val.string[1],
                    (uint8_t)vb->val.string[2], (uint8_t)vb->val.string[3]);
  } else {
    dst[0] = 0;
    return EINVAL;
  }

  num_chars = dst_size - 1;
  if (num_chars > vb->val_len)
    num_chars = vb->val_len;

  for (size_t i = 0; i < num_chars; i++) {
    /* Check for control characters. */
    if ((unsigned char)src[i] < 32)
      return csnmp_strvbcopy_hexstring(dst, vb, dst_size);
    dst[i] = src[i];
  }
  dst[num_chars] = 0;
  dst[dst_size - 1] = '\0';

  if (dst_size <= vb->val_len)
    return ENOMEM;

  return 0;
} /* }}} int csnmp_strvbcopy */

static csnmp_cell_char_t *csnmp_get_char_cell(const struct variable_list *vb,
                                              const oid_t *root_oid,
                                              const host_definition_t *hd,
                                              const data_definition_t *dd) {

  if (vb == NULL)
    return NULL;

  csnmp_cell_char_t *il = calloc(1, sizeof(*il));
  if (il == NULL) {
    ERROR("snmp plugin: calloc failed.");
    return NULL;
  }
  il->next = NULL;

  oid_t vb_name;
  csnmp_oid_init(&vb_name, vb->name, vb->name_length);

  if (csnmp_oid_suffix(&il->suffix, &vb_name, root_oid) != 0) {
    sfree(il);
    return NULL;
  }

  /* Get value */
  if ((vb->type == ASN_OCTET_STR) || (vb->type == ASN_BIT_STR) ||
      (vb->type == ASN_IPADDRESS)) {

    csnmp_strvbcopy(il->value, vb, sizeof(il->value));

  } else {
    value_t val = csnmp_value_list_to_value(
        vb, DS_TYPE_COUNTER,
        /* scale = */ 1.0, /* shift = */ 0.0, hd->name, dd->name);
    snprintf(il->value, sizeof(il->value), "%" PRIu64, (uint64_t)val.counter);
  }

  return il;
} /* csnmp_cell_char_t csnmp_get_char_cell */

static void csnmp_cells_append(csnmp_cell_char_t **head,
                               csnmp_cell_char_t **tail,
                               csnmp_cell_char_t *il) {
  if (*head == NULL)
    *head = il;
  else
    (*tail)->next = il;
  *tail = il;
} /* void csnmp_cells_append */

static bool csnmp_ignore_instance(csnmp_cell_char_t *cell,
                                  const data_definition_t *dd) {
  bool is_matched = 0;
  for (uint32_t i = 0; i < dd->ignores_len; i++) {
    int status = fnmatch(dd->ignores[i], cell->value, 0);
    if (status == 0) {
      if (!dd->invert_match) {
        return 1;
      } else {
        is_matched = 1;
        break;
      }
    }
  }
  if (dd->invert_match && !is_matched) {
    return 1;
  }
  return 0;
} /* bool csnmp_ignore_instance */

static void csnmp_cell_replace_reserved_chars(csnmp_cell_char_t *cell) {
  for (char *ptr = cell->value; *ptr != '\0'; ptr++) {
    if ((*ptr > 0) && (*ptr < 32))
      *ptr = ' ';
    else if (*ptr == '/')
      *ptr = '_';
  }
} /* void csnmp_cell_replace_reserved_chars */

static int csnmp_dispatch_table(host_definition_t *host,
                                data_definition_t *data,
                                csnmp_cell_char_t *type_instance_cells,
                                csnmp_cell_char_t *plugin_instance_cells,
                                csnmp_cell_char_t *hostname_cells,
                                csnmp_cell_char_t *filter_cells,
                                csnmp_cell_value_t **value_cells) {
  const data_set_t *ds;
  value_list_t vl = VALUE_LIST_INIT;

  csnmp_cell_char_t *type_instance_cell_ptr = type_instance_cells;
  csnmp_cell_char_t *plugin_instance_cell_ptr = plugin_instance_cells;
  csnmp_cell_char_t *hostname_cell_ptr = hostname_cells;
  csnmp_cell_char_t *filter_cell_ptr = filter_cells;
  csnmp_cell_value_t *value_cell_ptr[data->values_len];

  size_t i;
  bool have_more;
  oid_t current_suffix;

  ds = plugin_get_ds(data->type);
  if (!ds) {
    ERROR("snmp plugin: DataSet `%s' not defined.", data->type);
    return -1;
  }
  assert(ds->ds_num == data->values_len);
  assert(data->values_len > 0);

  for (i = 0; i < data->values_len; i++)
    value_cell_ptr[i] = value_cells[i];

  sstrncpy(vl.plugin, data->plugin_name, sizeof(vl.plugin));
  sstrncpy(vl.type, data->type, sizeof(vl.type));

  have_more = 1;
  while (have_more) {
    bool suffix_skipped = 0;

    /* Determine next suffix to handle. */
    if (type_instance_cells != NULL) {
      if (type_instance_cell_ptr == NULL) {
        have_more = 0;
        continue;
      }

      memcpy(&current_suffix, &type_instance_cell_ptr->suffix,
             sizeof(current_suffix));
    } else {
      /* no instance configured */
      csnmp_cell_value_t *ptr = value_cell_ptr[0];
      if (ptr == NULL) {
        have_more = 0;
        continue;
      }

      memcpy(&current_suffix, &ptr->suffix, sizeof(current_suffix));
    }

    /*
    char oid_buffer[1024] = {0};
    snprint_objid(oid_buffer, sizeof(oid_buffer) - 1, current_suffix.oid,
                          current_suffix.oid_len);
    DEBUG("SNMP PLUGIN: SUFFIX %s", oid_buffer);
    */

    /* Update plugin_instance_cell_ptr to point expected suffix */
    if (plugin_instance_cells != NULL) {
      while ((plugin_instance_cell_ptr != NULL) &&
             (csnmp_oid_compare(&plugin_instance_cell_ptr->suffix,
                                &current_suffix) < 0))
        plugin_instance_cell_ptr = plugin_instance_cell_ptr->next;

      if (plugin_instance_cell_ptr == NULL) {
        have_more = 0;
        continue;
      }

      if (csnmp_oid_compare(&plugin_instance_cell_ptr->suffix,
                            &current_suffix) > 0) {
        /* This suffix is missing in the subtree. Indicate this with the
         * "suffix_skipped" flag and try the next instance / suffix. */
        suffix_skipped = 1;
      }
    }

    /* Update hostname_cell_ptr to point expected suffix */
    if (hostname_cells != NULL) {
      while (
          (hostname_cell_ptr != NULL) &&
          (csnmp_oid_compare(&hostname_cell_ptr->suffix, &current_suffix) < 0))
        hostname_cell_ptr = hostname_cell_ptr->next;

      if (hostname_cell_ptr == NULL) {
        have_more = 0;
        continue;
      }

      if (csnmp_oid_compare(&hostname_cell_ptr->suffix, &current_suffix) > 0) {
        /* This suffix is missing in the subtree. Indicate this with the
         * "suffix_skipped" flag and try the next instance / suffix. */
        suffix_skipped = 1;
      }
    }

    /* Update filter_cell_ptr to point expected suffix */
    if (filter_cells != NULL) {
      while ((filter_cell_ptr != NULL) &&
             (csnmp_oid_compare(&filter_cell_ptr->suffix, &current_suffix) < 0))
        filter_cell_ptr = filter_cell_ptr->next;

      if (filter_cell_ptr == NULL) {
        have_more = 0;
        continue;
      }

      if (csnmp_oid_compare(&filter_cell_ptr->suffix, &current_suffix) > 0) {
        /* This suffix is missing in the subtree. Indicate this with the
         * "suffix_skipped" flag and try the next instance / suffix. */
        suffix_skipped = 1;
      }
    }

    /* Update all the value_cell_ptr to point at the entry with the same
     * trailing partial OID */
    for (i = 0; i < data->values_len; i++) {
      while (
          (value_cell_ptr[i] != NULL) &&
          (csnmp_oid_compare(&value_cell_ptr[i]->suffix, &current_suffix) < 0))
        value_cell_ptr[i] = value_cell_ptr[i]->next;

      if (value_cell_ptr[i] == NULL) {
        have_more = 0;
        break;
      } else if (csnmp_oid_compare(&value_cell_ptr[i]->suffix,
                                   &current_suffix) > 0) {
        /* This suffix is missing in the subtree. Indicate this with the
         * "suffix_skipped" flag and try the next instance / suffix. */
        suffix_skipped = 1;
        break;
      }
    } /* for (i = 0; i < columns; i++) */

    if (!have_more)
      break;

    /* Matching the values failed. Start from the beginning again. */
    if (suffix_skipped) {
      if (type_instance_cells != NULL)
        type_instance_cell_ptr = type_instance_cell_ptr->next;
      else
        value_cell_ptr[0] = value_cell_ptr[0]->next;

      continue;
    }

/* if we reach this line, all value_cell_ptr[i] are non-NULL and are set
 * to the same subid. type_instance_cell_ptr is either NULL or points to the
 * same subid, too. */
#if COLLECT_DEBUG
    for (i = 1; i < data->values_len; i++) {
      assert(value_cell_ptr[i] != NULL);
      assert(csnmp_oid_compare(&value_cell_ptr[i - 1]->suffix,
                               &value_cell_ptr[i]->suffix) == 0);
    }
    assert((type_instance_cell_ptr == NULL) ||
           (csnmp_oid_compare(&type_instance_cell_ptr->suffix,
                              &value_cell_ptr[0]->suffix) == 0));
    assert((plugin_instance_cell_ptr == NULL) ||
           (csnmp_oid_compare(&plugin_instance_cell_ptr->suffix,
                              &value_cell_ptr[0]->suffix) == 0));
    assert((hostname_cell_ptr == NULL) ||
           (csnmp_oid_compare(&hostname_cell_ptr->suffix,
                              &value_cell_ptr[0]->suffix) == 0));
    assert((filter_cell_ptr == NULL) ||
           (csnmp_oid_compare(&filter_cell_ptr->suffix,
                              &value_cell_ptr[0]->suffix) == 0));
#endif

    /* Check the value in filter column */
    if (filter_cell_ptr &&
        ignorelist_match(data->ignorelist, filter_cell_ptr->value) != 0) {
      if (type_instance_cells != NULL)
        type_instance_cell_ptr = type_instance_cell_ptr->next;
      else
        value_cell_ptr[0] = value_cell_ptr[0]->next;

      continue;
    }

    /* set vl.host */
    if (data->host.configured) {
      char temp[DATA_MAX_NAME_LEN];
      if (hostname_cell_ptr == NULL)
        csnmp_oid_to_string(temp, sizeof(temp), &current_suffix);
      else
        sstrncpy(temp, hostname_cell_ptr->value, sizeof(temp));

      if (data->host.prefix == NULL)
        sstrncpy(vl.host, temp, sizeof(vl.host));
      else
        snprintf(vl.host, sizeof(vl.host), "%s%s", data->host.prefix, temp);
    } else {
      sstrncpy(vl.host, host->name, sizeof(vl.host));
    }

    /* set vl.type_instance */
    if (data->type_instance.configured) {
      char temp[DATA_MAX_NAME_LEN];
      if (type_instance_cell_ptr == NULL)
        csnmp_oid_to_string(temp, sizeof(temp), &current_suffix);
      else
        sstrncpy(temp, type_instance_cell_ptr->value, sizeof(temp));

      if (data->type_instance.prefix == NULL)
        sstrncpy(vl.type_instance, temp, sizeof(vl.type_instance));
      else
        snprintf(vl.type_instance, sizeof(vl.type_instance), "%s%s",
                 data->type_instance.prefix, temp);
    } else if (data->type_instance.value) {
      sstrncpy(vl.type_instance, data->type_instance.value,
               sizeof(vl.type_instance));
    }

    /* set vl.plugin_instance */
    if (data->plugin_instance.configured) {
      char temp[DATA_MAX_NAME_LEN];
      if (plugin_instance_cell_ptr == NULL)
        csnmp_oid_to_string(temp, sizeof(temp), &current_suffix);
      else
        sstrncpy(temp, plugin_instance_cell_ptr->value, sizeof(temp));

      if (data->plugin_instance.prefix == NULL)
        sstrncpy(vl.plugin_instance, temp, sizeof(vl.plugin_instance));
      else
        snprintf(vl.plugin_instance, sizeof(vl.plugin_instance), "%s%s",
                 data->plugin_instance.prefix, temp);
    } else if (data->plugin_instance.value) {
      sstrncpy(vl.plugin_instance, data->plugin_instance.value,
               sizeof(vl.plugin_instance));
    }

    vl.values_len = data->values_len;
    value_t values[vl.values_len];
    vl.values = values;

    for (i = 0; i < data->values_len; i++)
      vl.values[i] = value_cell_ptr[i]->value;

    plugin_dispatch_values(&vl);

    /* prevent leakage of pointer to local variable. */
    vl.values_len = 0;
    vl.values = NULL;

    if (type_instance_cells != NULL)
      type_instance_cell_ptr = type_instance_cell_ptr->next;
    else
      value_cell_ptr[0] = value_cell_ptr[0]->next;
  } /* while (have_more) */

  return 0;
} /* int csnmp_dispatch_table */

static int csnmp_read_table(host_definition_t *host, data_definition_t *data) {
  struct snmp_pdu *req;
  struct snmp_pdu *res = NULL;
  struct variable_list *vb;

  const data_set_t *ds;

  size_t oid_list_len = data->values_len;

  if (data->type_instance.oid.oid_len > 0)
    oid_list_len++;

  if (data->plugin_instance.oid.oid_len > 0)
    oid_list_len++;

  if (data->host.oid.oid_len > 0)
    oid_list_len++;

  if (data->filter_oid.oid_len > 0)
    oid_list_len++;

  /* Holds the last OID returned by the device. We use this in the GETNEXT
   * request to proceed. */
  oid_t oid_list[oid_list_len];
  /* Set to false when an OID has left its subtree so we don't re-request it
   * again. */
  csnmp_oid_type_t oid_list_todo[oid_list_len];

  int status;
  size_t i;

  /* `value_list_head' and `value_cells_tail' implement a linked list for each
   * value. `instance_cells_head' and `instance_cells_tail' implement a linked
   * list of instance names. This is used to jump gaps in the table. */
  csnmp_cell_char_t *type_instance_cells_head = NULL;
  csnmp_cell_char_t *type_instance_cells_tail = NULL;
  csnmp_cell_char_t *plugin_instance_cells_head = NULL;
  csnmp_cell_char_t *plugin_instance_cells_tail = NULL;
  csnmp_cell_char_t *hostname_cells_head = NULL;
  csnmp_cell_char_t *hostname_cells_tail = NULL;
  csnmp_cell_char_t *filter_cells_head = NULL;
  csnmp_cell_char_t *filter_cells_tail = NULL;
  csnmp_cell_value_t **value_cells_head;
  csnmp_cell_value_t **value_cells_tail;

  DEBUG("snmp plugin: csnmp_read_table (host = %s, data = %s)", host->name,
        data->name);

  if (host->sess_handle == NULL) {
    DEBUG("snmp plugin: csnmp_read_table: host->sess_handle == NULL");
    return -1;
  }

  ds = plugin_get_ds(data->type);
  if (!ds) {
    ERROR("snmp plugin: DataSet `%s' not defined.", data->type);
    return -1;
  }

  if (ds->ds_num != data->values_len) {
    ERROR("snmp plugin: DataSet `%s' requires %" PRIsz
          " values, but config talks "
          "about %" PRIsz,
          data->type, ds->ds_num, data->values_len);
    return -1;
  }
  assert(data->values_len > 0);

  for (i = 0; i < data->values_len; i++)
    oid_list_todo[i] = OID_TYPE_VARIABLE;

  /* We need a copy of all the OIDs, because GETNEXT will destroy them. */
  memcpy(oid_list, data->values, data->values_len * sizeof(oid_t));

  if (data->type_instance.oid.oid_len > 0) {
    memcpy(oid_list + i, &data->type_instance.oid, sizeof(oid_t));
    oid_list_todo[i] = OID_TYPE_TYPEINSTANCE;
    i++;
  }

  if (data->plugin_instance.oid.oid_len > 0) {
    memcpy(oid_list + i, &data->plugin_instance.oid, sizeof(oid_t));
    oid_list_todo[i] = OID_TYPE_PLUGININSTANCE;
    i++;
  }

  if (data->host.oid.oid_len > 0) {
    memcpy(oid_list + i, &data->host.oid, sizeof(oid_t));
    oid_list_todo[i] = OID_TYPE_HOST;
    i++;
  }

  if (data->filter_oid.oid_len > 0) {
    memcpy(oid_list + i, &data->filter_oid, sizeof(oid_t));
    oid_list_todo[i] = OID_TYPE_FILTER;
    i++;
  }

  /* We're going to construct n linked lists, one for each "value".
   * value_cells_head will contain pointers to the heads of these linked lists,
   * value_cells_tail will contain pointers to the tail of the lists. */
  value_cells_head = calloc(data->values_len, sizeof(*value_cells_head));
  value_cells_tail = calloc(data->values_len, sizeof(*value_cells_tail));
  if ((value_cells_head == NULL) || (value_cells_tail == NULL)) {
    ERROR("snmp plugin: csnmp_read_table: calloc failed.");
    sfree(value_cells_head);
    sfree(value_cells_tail);
    return -1;
  }

  status = 0;
  while (status == 0) {
    req = snmp_pdu_create(SNMP_MSG_GETNEXT);
    if (req == NULL) {
      ERROR("snmp plugin: snmp_pdu_create failed.");
      status = -1;
      break;
    }

    size_t oid_list_todo_num = 0;
    size_t var_idx[oid_list_len];
    memset(var_idx, 0, sizeof(var_idx));

    for (i = 0; i < oid_list_len; i++) {
      /* Do not rerequest already finished OIDs */
      if (!oid_list_todo[i])
        continue;
      snmp_add_null_var(req, oid_list[i].oid, oid_list[i].oid_len);
      var_idx[oid_list_todo_num] = i;
      oid_list_todo_num++;
    }

    if (oid_list_todo_num == 0) {
      /* The request is still empty - so we are finished */
      DEBUG("snmp plugin: all variables have left their subtree");
      snmp_free_pdu(req);
      status = 0;
      break;
    }

    res = NULL;
    status = snmp_sess_synch_response(host->sess_handle, req, &res);

    /* snmp_sess_synch_response always frees our req PDU */
    req = NULL;

    if ((status != STAT_SUCCESS) || (res == NULL)) {
      char *errstr = NULL;

      snmp_sess_error(host->sess_handle, NULL, NULL, &errstr);

      c_complain(LOG_ERR, &host->complaint,
                 "snmp plugin: host %s: snmp_sess_synch_response failed: %s",
                 host->name, (errstr == NULL) ? "Unknown problem" : errstr);

      if (res != NULL)
        snmp_free_pdu(res);
      res = NULL;

      sfree(errstr);
      csnmp_host_close_session(host);

      status = -1;
      break;
    }

    status = 0;
    assert(res != NULL);
    c_release(LOG_INFO, &host->complaint,
              "snmp plugin: host %s: snmp_sess_synch_response successful.",
              host->name);

    vb = res->variables;
    if (vb == NULL) {
      status = -1;
      break;
    }

    if (res->errstat != SNMP_ERR_NOERROR) {
      if (res->errindex != 0) {
        /* Find the OID which caused error */
        for (i = 1, vb = res->variables; vb != NULL && i != res->errindex;
             vb = vb->next_variable, i++)
          /* do nothing */;
      }

      if ((res->errindex == 0) || (vb == NULL)) {
        ERROR("snmp plugin: host %s; data %s: response error: %s (%li) ",
              host->name, data->name, snmp_errstring(res->errstat),
              res->errstat);
        status = -1;
        break;
      }

      char oid_buffer[1024] = {0};
      snprint_objid(oid_buffer, sizeof(oid_buffer) - 1, vb->name,
                    vb->name_length);
      NOTICE("snmp plugin: host %s; data %s: OID `%s` failed: %s", host->name,
             data->name, oid_buffer, snmp_errstring(res->errstat));

      /* Get value index from todo list and skip OID found */
      assert(res->errindex <= oid_list_todo_num);
      i = var_idx[res->errindex - 1];
      assert(i < oid_list_len);
      oid_list_todo[i] = 0;

      snmp_free_pdu(res);
      res = NULL;
      continue;
    }

    for (vb = res->variables, i = 0; (vb != NULL);
         vb = vb->next_variable, i++) {
      /* Calculate value index from todo list */
      while ((i < oid_list_len) && !oid_list_todo[i]) {
        i++;
      }
      if (i >= oid_list_len) {
        break;
      }

      /* An instance is configured and the res variable we process is the
       * instance value */
      if (oid_list_todo[i] == OID_TYPE_TYPEINSTANCE) {
        if ((vb->type == SNMP_ENDOFMIBVIEW) ||
            (snmp_oid_ncompare(data->type_instance.oid.oid,
                               data->type_instance.oid.oid_len, vb->name,
                               vb->name_length,
                               data->type_instance.oid.oid_len) != 0)) {
          DEBUG("snmp plugin: host = %s; data = %s; TypeInstance left its "
                "subtree.",
                host->name, data->name);
          oid_list_todo[i] = 0;
          continue;
        }

        /* Allocate a new `csnmp_cell_char_t', insert the instance name and
         * add it to the list */
        csnmp_cell_char_t *cell =
            csnmp_get_char_cell(vb, &data->type_instance.oid, host, data);
        if (cell == NULL) {
          ERROR("snmp plugin: host %s: csnmp_get_char_cell() failed.",
                host->name);
          status = -1;
          break;
        }

        if (csnmp_ignore_instance(cell, data)) {
          sfree(cell);
        } else {
          csnmp_cell_replace_reserved_chars(cell);

          DEBUG("snmp plugin: il->type_instance = `%s';", cell->value);
          csnmp_cells_append(&type_instance_cells_head,
                             &type_instance_cells_tail, cell);
        }
      } else if (oid_list_todo[i] == OID_TYPE_PLUGININSTANCE) {
        if ((vb->type == SNMP_ENDOFMIBVIEW) ||
            (snmp_oid_ncompare(data->plugin_instance.oid.oid,
                               data->plugin_instance.oid.oid_len, vb->name,
                               vb->name_length,
                               data->plugin_instance.oid.oid_len) != 0)) {
          DEBUG("snmp plugin: host = %s; data = %s; TypeInstance left its "
                "subtree.",
                host->name, data->name);
          oid_list_todo[i] = 0;
          continue;
        }

        /* Allocate a new `csnmp_cell_char_t', insert the instance name and
         * add it to the list */
        csnmp_cell_char_t *cell =
            csnmp_get_char_cell(vb, &data->plugin_instance.oid, host, data);
        if (cell == NULL) {
          ERROR("snmp plugin: host %s: csnmp_get_char_cell() failed.",
                host->name);
          status = -1;
          break;
        }

        csnmp_cell_replace_reserved_chars(cell);

        DEBUG("snmp plugin: il->plugin_instance = `%s';", cell->value);
        csnmp_cells_append(&plugin_instance_cells_head,
                           &plugin_instance_cells_tail, cell);
      } else if (oid_list_todo[i] == OID_TYPE_HOST) {
        if ((vb->type == SNMP_ENDOFMIBVIEW) ||
            (snmp_oid_ncompare(data->host.oid.oid, data->host.oid.oid_len,
                               vb->name, vb->name_length,
                               data->host.oid.oid_len) != 0)) {
          DEBUG("snmp plugin: host = %s; data = %s; Host left its subtree.",
                host->name, data->name);
          oid_list_todo[i] = 0;
          continue;
        }

        /* Allocate a new `csnmp_cell_char_t', insert the instance name and
         * add it to the list */
        csnmp_cell_char_t *cell =
            csnmp_get_char_cell(vb, &data->host.oid, host, data);
        if (cell == NULL) {
          ERROR("snmp plugin: host %s: csnmp_get_char_cell() failed.",
                host->name);
          status = -1;
          break;
        }

        csnmp_cell_replace_reserved_chars(cell);

        DEBUG("snmp plugin: il->hostname = `%s';", cell->value);
        csnmp_cells_append(&hostname_cells_head, &hostname_cells_tail, cell);
      } else if (oid_list_todo[i] == OID_TYPE_FILTER) {
        if ((vb->type == SNMP_ENDOFMIBVIEW) ||
            (snmp_oid_ncompare(data->filter_oid.oid, data->filter_oid.oid_len,
                               vb->name, vb->name_length,
                               data->filter_oid.oid_len) != 0)) {
          DEBUG("snmp plugin: host = %s; data = %s; Host left its subtree.",
                host->name, data->name);
          oid_list_todo[i] = 0;
          continue;
        }

        /* Allocate a new `csnmp_cell_char_t', insert the instance name and
         * add it to the list */
        csnmp_cell_char_t *cell =
            csnmp_get_char_cell(vb, &data->filter_oid, host, data);
        if (cell == NULL) {
          ERROR("snmp plugin: host %s: csnmp_get_char_cell() failed.",
                host->name);
          status = -1;
          break;
        }

        csnmp_cell_replace_reserved_chars(cell);

        DEBUG("snmp plugin: il->filter = `%s';", cell->value);
        csnmp_cells_append(&filter_cells_head, &filter_cells_tail, cell);
      } else /* The variable we are processing is a normal value */
      {
        assert(oid_list_todo[i] == OID_TYPE_VARIABLE);

        csnmp_cell_value_t *vt;
        oid_t vb_name;
        oid_t suffix;
        int ret;

        csnmp_oid_init(&vb_name, vb->name, vb->name_length);

        /* Calculate the current suffix. This is later used to check that the
         * suffix is increasing. This also checks if we left the subtree */
        ret = csnmp_oid_suffix(&suffix, &vb_name, data->values + i);
        if (ret != 0) {
          DEBUG("snmp plugin: host = %s; data = %s; i = %" PRIsz "; "
                "Value probably left its subtree.",
                host->name, data->name, i);
          oid_list_todo[i] = 0;
          continue;
        }

        /* Make sure the OIDs returned by the agent are increasing. Otherwise
         * our table matching algorithm will get confused. */
        if ((value_cells_tail[i] != NULL) &&
            (csnmp_oid_compare(&suffix, &value_cells_tail[i]->suffix) <= 0)) {
          DEBUG("snmp plugin: host = %s; data = %s; i = %" PRIsz "; "
                "Suffix is not increasing.",
                host->name, data->name, i);
          oid_list_todo[i] = 0;
          continue;
        }

        vt = calloc(1, sizeof(*vt));
        if (vt == NULL) {
          ERROR("snmp plugin: calloc failed.");
          status = -1;
          break;
        }

        vt->value =
            csnmp_value_list_to_value(vb, ds->ds[i].type, data->scale,
                                      data->shift, host->name, data->name);
        memcpy(&vt->suffix, &suffix, sizeof(vt->suffix));
        vt->next = NULL;

        if (value_cells_tail[i] == NULL)
          value_cells_head[i] = vt;
        else
          value_cells_tail[i]->next = vt;
        value_cells_tail[i] = vt;
      }

      /* Copy OID to oid_list[i] */
      memcpy(oid_list[i].oid, vb->name, sizeof(oid) * vb->name_length);
      oid_list[i].oid_len = vb->name_length;

    } /* for (vb = res->variables ...) */

    if (res != NULL)
      snmp_free_pdu(res);
    res = NULL;
  } /* while (status == 0) */

  if (res != NULL)
    snmp_free_pdu(res);
  res = NULL;

  if (status == 0)
    csnmp_dispatch_table(host, data, type_instance_cells_head,
                         plugin_instance_cells_head, hostname_cells_head,
                         filter_cells_head, value_cells_head);

  /* Free all allocated variables here */
  while (type_instance_cells_head != NULL) {
    csnmp_cell_char_t *next = type_instance_cells_head->next;
    sfree(type_instance_cells_head);
    type_instance_cells_head = next;
  }

  while (plugin_instance_cells_head != NULL) {
    csnmp_cell_char_t *next = plugin_instance_cells_head->next;
    sfree(plugin_instance_cells_head);
    plugin_instance_cells_head = next;
  }

  while (hostname_cells_head != NULL) {
    csnmp_cell_char_t *next = hostname_cells_head->next;
    sfree(hostname_cells_head);
    hostname_cells_head = next;
  }

  while (filter_cells_head != NULL) {
    csnmp_cell_char_t *next = filter_cells_head->next;
    sfree(filter_cells_head);
    filter_cells_head = next;
  }

  for (i = 0; i < data->values_len; i++) {
    while (value_cells_head[i] != NULL) {
      csnmp_cell_value_t *next = value_cells_head[i]->next;
      sfree(value_cells_head[i]);
      value_cells_head[i] = next;
    }
  }

  sfree(value_cells_head);
  sfree(value_cells_tail);

  return 0;
} /* int csnmp_read_table */

static int csnmp_read_value(host_definition_t *host, data_definition_t *data) {
  struct snmp_pdu *req;
  struct snmp_pdu *res = NULL;
  struct variable_list *vb;

  const data_set_t *ds;
  value_list_t vl = VALUE_LIST_INIT;

  int status;
  size_t i;

  DEBUG("snmp plugin: csnmp_read_value (host = %s, data = %s)", host->name,
        data->name);

  if (host->sess_handle == NULL) {
    DEBUG("snmp plugin: csnmp_read_value: host->sess_handle == NULL");
    return -1;
  }

  ds = plugin_get_ds(data->type);
  if (!ds) {
    ERROR("snmp plugin: DataSet `%s' not defined.", data->type);
    return -1;
  }

  if (ds->ds_num != data->values_len) {
    ERROR("snmp plugin: DataSet `%s' requires %" PRIsz
          " values, but config talks "
          "about %" PRIsz,
          data->type, ds->ds_num, data->values_len);
    return -1;
  }

  vl.values_len = ds->ds_num;
  vl.values = malloc(sizeof(*vl.values) * vl.values_len);
  if (vl.values == NULL)
    return -1;
  for (i = 0; i < vl.values_len; i++) {
    if (ds->ds[i].type == DS_TYPE_COUNTER)
      vl.values[i].counter = 0;
    else
      vl.values[i].gauge = NAN;
  }

  sstrncpy(vl.host, host->name, sizeof(vl.host));
  sstrncpy(vl.plugin, data->plugin_name, sizeof(vl.plugin));
  sstrncpy(vl.type, data->type, sizeof(vl.type));
  if (data->type_instance.value)
    sstrncpy(vl.type_instance, data->type_instance.value,
             sizeof(vl.type_instance));
  if (data->plugin_instance.value)
    sstrncpy(vl.plugin_instance, data->plugin_instance.value,
             sizeof(vl.plugin_instance));

  req = snmp_pdu_create(SNMP_MSG_GET);
  if (req == NULL) {
    ERROR("snmp plugin: snmp_pdu_create failed.");
    sfree(vl.values);
    return -1;
  }

  for (i = 0; i < data->values_len; i++)
    snmp_add_null_var(req, data->values[i].oid, data->values[i].oid_len);

  status = snmp_sess_synch_response(host->sess_handle, req, &res);

  if ((status != STAT_SUCCESS) || (res == NULL)) {
    char *errstr = NULL;

    snmp_sess_error(host->sess_handle, NULL, NULL, &errstr);
    ERROR("snmp plugin: host %s: snmp_sess_synch_response failed: %s",
          host->name, (errstr == NULL) ? "Unknown problem" : errstr);

    if (res != NULL)
      snmp_free_pdu(res);

    sfree(errstr);
    sfree(vl.values);
    csnmp_host_close_session(host);

    return -1;
  }

  for (vb = res->variables; vb != NULL; vb = vb->next_variable) {
#if COLLECT_DEBUG
    char buffer[1024];
    snprint_variable(buffer, sizeof(buffer), vb->name, vb->name_length, vb);
    DEBUG("snmp plugin: Got this variable: %s", buffer);
#endif /* COLLECT_DEBUG */

    for (i = 0; i < data->values_len; i++)
      if (snmp_oid_compare(data->values[i].oid, data->values[i].oid_len,
                           vb->name, vb->name_length) == 0)
        vl.values[i] =
            csnmp_value_list_to_value(vb, ds->ds[i].type, data->scale,
                                      data->shift, host->name, data->name);
  } /* for (res->variables) */

  snmp_free_pdu(res);

  DEBUG("snmp plugin: -> plugin_dispatch_values (&vl);");
  plugin_dispatch_values(&vl);
  sfree(vl.values);

  return 0;
} /* int csnmp_read_value */

static int csnmp_read_host(user_data_t *ud) {
  host_definition_t *host;
  int status;
  int success;
  int i;

  host = ud->data;

  if (host->sess_handle == NULL)
    csnmp_host_open_session(host);

  if (host->sess_handle == NULL)
    return -1;

  success = 0;
  for (i = 0; i < host->data_list_len; i++) {
    data_definition_t *data = host->data_list[i];

    if (data->is_table)
      status = csnmp_read_table(host, data);
    else
      status = csnmp_read_value(host, data);

    if (status == 0)
      success++;
  }

  if (success == 0)
    return -1;

  return 0;
} /* int csnmp_read_host */

static int csnmp_init(void) {
  call_snmp_init_once();

  return 0;
} /* int csnmp_init */

static int csnmp_shutdown(void) {
  data_definition_t *data_this;
  data_definition_t *data_next;

  /* When we get here, the read threads have been stopped and all the
   * `host_definition_t' will be freed. */
  DEBUG("snmp plugin: Destroying all data definitions.");

  data_this = data_head;
  data_head = NULL;
  while (data_this != NULL) {
    data_next = data_this->next;

    csnmp_data_definition_destroy(data_this);

    data_this = data_next;
  }

  return 0;
} /* int csnmp_shutdown */

void module_register(void) {
  plugin_register_complex_config("snmp", csnmp_config);
  plugin_register_init("snmp", csnmp_init);
  plugin_register_shutdown("snmp", csnmp_shutdown);
} /* void module_register */
