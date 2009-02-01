/**
 * collectd - src/oracle.c
 * Copyright (C) 2008,2009  Florian octo Forster
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
 * Linking src/oracle.c ("the oracle plugin") statically or dynamically with
 * other modules is making a combined work based on the oracle plugin. Thus,
 * the terms and conditions of the GNU General Public License cover the whole
 * combination.
 *
 * In addition, as a special exception, the copyright holders of the oracle
 * plugin give you permission to combine the oracle plugin with free software
 * programs or libraries that are released under the GNU LGPL and with code
 * included in the standard release of the Oracle® Call Interface (OCI) under
 * the Oracle® Technology Network (OTN) License (or modified versions of such
 * code, with unchanged license). You may copy and distribute such a system
 * following the terms of the GNU GPL for the oracle plugin and the licenses of
 * the other code concerned.
 *
 * Note that people who make modified versions of the oracle plugin are not
 * obligated to grant this special exception for their modified versions; it is
 * their choice whether to do so. The GNU General Public License gives
 * permission to release a modified version without this exception; this
 * exception also makes it possible to release a modified version which carries
 * forward this exception. However, without this exception the OTN License does
 * not allow linking with code licensed under the GNU General Public License.
 *
 * Oracle® is a registered trademark of Oracle Corporation and/or its
 * affiliates. Other names may be trademarks of their respective owners.
 *
 * Authors:
 *   Florian octo Forster <octo at noris.net>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <oci.h>

/*
 * Data types
 */
struct o_result_s;
typedef struct o_result_s o_result_t;
struct o_result_s
{
  char    *type;
  char   **instances;
  size_t   instances_num;
  char   **values;
  size_t   values_num;

  o_result_t *next;
};

struct o_query_s
{
  char    *name;
  char    *statement;
  OCIStmt *oci_statement;

  o_result_t *results;
};
typedef struct o_query_s o_query_t;

struct o_database_s
{
  char *name;
  char *connect_id;
  char *username;
  char *password;

  o_query_t **queries;
  size_t      queries_num;

  OCISvcCtx *oci_service_context;
};
typedef struct o_database_s o_database_t;

/*
 * Global variables
 */
static o_query_t    **queries       = NULL;
static size_t         queries_num   = 0;
static o_database_t **databases     = NULL;
static size_t         databases_num = 0;

OCIEnv   *oci_env = NULL;
OCIError *oci_error = NULL;

/*
 * Functions
 */
static void o_report_error (const char *where, /* {{{ */
    const char *what, OCIError *eh)
{
  char buffer[2048];
  sb4 error_code;
  int status;

  status = OCIErrorGet (eh, /* record number = */ 1,
      /* sqlstate = */ NULL,
      &error_code,
      (text *) &buffer[0],
      (ub4) sizeof (buffer),
      OCI_HTYPE_ERROR);
  buffer[sizeof (buffer) - 1] = 0;

  if (status == OCI_SUCCESS)
  {
    size_t buffer_length;

    buffer_length = strlen (buffer);
    while ((buffer_length > 0) && (buffer[buffer_length - 1] < 32))
    {
      buffer_length--;
      buffer[buffer_length] = 0;
    }

    ERROR ("oracle plugin: %s: %s failed: %s",
        where, what, buffer);
  }
  else
  {
    ERROR ("oracle plugin: %s: %s failed. Additionally, OCIErrorGet failed with status %i.",
        where, what, status);
  }
} /* }}} void o_report_error */

static void o_result_free (o_result_t *r) /* {{{ */
{
  size_t i;

  if (r == NULL)
    return;

  sfree (r->type);

  for (i = 0; i < r->instances_num; i++)
    sfree (r->instances[i]);
  sfree (r->instances);

  for (i = 0; i < r->values_num; i++)
    sfree (r->values[i]);
  sfree (r->values);

  o_result_free (r->next);

  sfree (r);
} /* }}} void o_result_free */

static void o_query_free (o_query_t *q) /* {{{ */
{
  if (q == NULL)
    return;

  sfree (q->name);
  sfree (q->statement);

  o_result_free (q->results);

  sfree (q);
} /* }}} void o_query_free */

static void o_database_free (o_database_t *db) /* {{{ */
{
  if (db == NULL)
    return;

  sfree (db->name);
  sfree (db->connect_id);
  sfree (db->username);
  sfree (db->password);
  sfree (db->queries);

  sfree (db);
} /* }}} void o_database_free */

/* Configuration handling functions {{{
 *
 * <Plugin oracle>
 *   <Query "plugin_instance0">
 *     Statement "SELECT name, value FROM table"
 *     <Result>
 *       Type "gauge"
 *       InstancesFrom "name"
 *       ValuesFrom "value"
 *     </Result>
 *   </Query>
 *     
 *   <Database "plugin_instance1">
 *     ConnectID "db01"
 *     Username "oracle"
 *     Password "secret"
 *     Query "plugin_instance0"
 *   </Database>
 * </Plugin>
 */

static int o_config_set_string (char **ret_string, /* {{{ */
    oconfig_item_t *ci)
{
  char *string;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("oracle plugin: The `%s' config option "
        "needs exactly one string argument.", ci->key);
    return (-1);
  }

  string = strdup (ci->values[0].value.string);
  if (string == NULL)
  {
    ERROR ("oracle plugin: strdup failed.");
    return (-1);
  }

  if (*ret_string != NULL)
    free (*ret_string);
  *ret_string = string;

  return (0);
} /* }}} int o_config_set_string */

static int o_config_add_string (char ***ret_array, /* {{{ */
    size_t *ret_array_len, oconfig_item_t *ci)
{
  char **array;
  size_t array_len;
  int i;

  if (ci->values_num < 1)
  {
    WARNING ("oracle plugin: The `%s' config option "
        "needs at least one argument.", ci->key);
    return (-1);
  }

  for (i = 0; i < ci->values_num; i++)
  {
    if (ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("oracle plugin: Argument %i to the `%s' option "
          "is not a string.", i + 1, ci->key);
      return (-1);
    }
  }

  array_len = *ret_array_len;
  array = (char **) realloc (*ret_array,
      sizeof (char *) * (array_len + ci->values_num));
  if (array == NULL)
  {
    ERROR ("oracle plugin: realloc failed.");
    return (-1);
  }
  *ret_array = array;

  for (i = 0; i < ci->values_num; i++)
  {
    array[array_len] = strdup (ci->values[i].value.string);
    if (array[array_len] == NULL)
    {
      ERROR ("oracle plugin: strdup failed.");
      *ret_array_len = array_len;
      return (-1);
    }
    array_len++;
  }

  *ret_array_len = array_len;
  return (0);
} /* }}} int o_config_add_string */

static int o_config_add_query_result (o_query_t *q, /* {{{ */
    oconfig_item_t *ci)
{
  o_result_t *r;
  int status;
  int i;

  if (ci->values_num != 0)
  {
    WARNING ("oracle plugin: The `Result' block doesn't accept any arguments. "
        "Ignoring %i argument%s.",
        ci->values_num, (ci->values_num == 1) ? "" : "s");
  }

  r = (o_result_t *) malloc (sizeof (*r));
  if (r == NULL)
  {
    ERROR ("oracle plugin: malloc failed.");
    return (-1);
  }
  memset (r, 0, sizeof (*r));
  r->type = NULL;
  r->instances = NULL;
  r->values = NULL;
  r->next = NULL;

  /* Fill the `o_result_t' structure.. */
  status = 0;
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Type", child->key) == 0)
      status = o_config_set_string (&r->type, child);
    else if (strcasecmp ("InstancesFrom", child->key) == 0)
      status = o_config_add_string (&r->instances, &r->instances_num, child);
    else if (strcasecmp ("ValuesFrom", child->key) == 0)
      status = o_config_add_string (&r->values, &r->values_num, child);
    else
    {
      WARNING ("oracle plugin: Query `%s': Option `%s' not allowed here.",
          q->name, child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  /* Check that all necessary options have been given. */
  while (status == 0)
  {
    if (r->type == NULL)
    {
      WARNING ("oracle plugin: `Type' not given for "
          "result in query `%s'", q->name);
      status = -1;
    }
    if (r->instances == NULL)
    {
      WARNING ("oracle plugin: `InstancesFrom' not given for "
          "result in query `%s'", q->name);
      status = -1;
    }
    if (r->values == NULL)
    {
      WARNING ("oracle plugin: `ValuesFrom' not given for "
          "result in query `%s'", q->name);
      status = -1;
    }

    break;
  } /* while (status == 0) */

  /* If all went well, add this result to the list of results within the
   * query structure. */
  if (status == 0)
  {
    if (q->results == NULL)
    {
      q->results = r;
    }
    else
    {
      o_result_t *last;

      last = q->results;
      while (last->next != NULL)
        last = last->next;

      last->next = r;
    }
  }

  if (status != 0)
  {
    o_result_free (r);
    return (-1);
  }

  return (0);
} /* }}} int o_config_add_query_result */

static int o_config_add_query (oconfig_item_t *ci) /* {{{ */
{
  o_query_t *q;
  int status;
  int i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("oracle plugin: The `Query' block "
        "needs exactly one string argument.");
    return (-1);
  }

  q = (o_query_t *) malloc (sizeof (*q));
  if (q == NULL)
  {
    ERROR ("oracle plugin: malloc failed.");
    return (-1);
  }
  memset (q, 0, sizeof (*q));

  status = o_config_set_string (&q->name, ci);
  if (status != 0)
  {
    sfree (q);
    return (status);
  }

  /* Fill the `o_query_t' structure.. */
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Statement", child->key) == 0)
      status = o_config_set_string (&q->statement, child);
    else if (strcasecmp ("Result", child->key) == 0)
      status = o_config_add_query_result (q, child);
    else
    {
      WARNING ("oracle plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  /* Check that all necessary options have been given. */
  while (status == 0)
  {
    if (q->statement == NULL)
    {
      WARNING ("oracle plugin: `Statement' not given for query `%s'", q->name);
      status = -1;
    }
    if (q->results == NULL)
    {
      WARNING ("oracle plugin: No (valid) `Result' block given for query `%s'",
          q->name);
      status = -1;
    }

    break;
  } /* while (status == 0) */

  /* If all went well, add this query to the list of queries within the
   * database structure. */
  if (status == 0)
  {
    o_query_t **temp;

    temp = (o_query_t **) realloc (queries,
        sizeof (*queries) * (queries_num + 1));
    if (temp == NULL)
    {
      ERROR ("oracle plugin: realloc failed");
      status = -1;
    }
    else
    {
      queries = temp;
      queries[queries_num] = q;
      queries_num++;
    }
  }

  if (status != 0)
  {
    o_query_free (q);
    return (-1);
  }

  return (0);
} /* }}} int o_config_add_query */

static int o_config_add_database_query (o_database_t *db, /* {{{ */
    oconfig_item_t *ci)
{
  o_query_t *q;
  o_query_t **temp;
  size_t i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("oracle plugin: The `Query' config option "
        "needs exactly one string argument.");
    return (-1);
  }

  q = NULL;
  for (i = 0; i < queries_num; i++)
  {
    if (strcasecmp (queries[i]->name, ci->values[0].value.string) == 0)
    {
      q = queries[i];
      break;
    }
  }

  if (q == NULL)
  {
    WARNING ("oracle plugin: Database `%s': Unknown query `%s'. "
        "Please make sure that the <Query \"%s\"> block comes before "
        "the <Database \"%s\"> block.",
        db->name, ci->values[0].value.string,
        ci->values[0].value.string, db->name);
    return (-1);
  }

  temp = (o_query_t **) realloc (db->queries,
      sizeof (*db->queries) * (db->queries_num + 1));
  if (temp == NULL)
  {
    ERROR ("oracle plugin: realloc failed");
    return (-1);
  }
  else
  {
    db->queries = temp;
    db->queries[db->queries_num] = q;
    db->queries_num++;
  }

  return (0);
} /* }}} int o_config_add_database_query */

static int o_config_add_database (oconfig_item_t *ci) /* {{{ */
{
  o_database_t *db;
  int status;
  int i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("oracle plugin: The `Database' block "
        "needs exactly one string argument.");
    return (-1);
  }

  db = (o_database_t *) malloc (sizeof (*db));
  if (db == NULL)
  {
    ERROR ("oracle plugin: malloc failed.");
    return (-1);
  }
  memset (db, 0, sizeof (*db));

  status = o_config_set_string (&db->name, ci);
  if (status != 0)
  {
    sfree (db);
    return (status);
  }

  /* Fill the `o_database_t' structure.. */
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("ConnectID", child->key) == 0)
      status = o_config_set_string (&db->connect_id, child);
    else if (strcasecmp ("Username", child->key) == 0)
      status = o_config_set_string (&db->username, child);
    else if (strcasecmp ("Password", child->key) == 0)
      status = o_config_set_string (&db->password, child);
    else if (strcasecmp ("Query", child->key) == 0)
      status = o_config_add_database_query (db, child);
    else
    {
      WARNING ("oracle plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  /* Check that all necessary options have been given. */
  while (status == 0)
  {
    if (db->connect_id == NULL)
    {
      WARNING ("oracle plugin: `ConnectID' not given for query `%s'", db->name);
      status = -1;
    }
    if (db->username == NULL)
    {
      WARNING ("oracle plugin: `Username' not given for query `%s'", db->name);
      status = -1;
    }
    if (db->password == NULL)
    {
      WARNING ("oracle plugin: `Password' not given for query `%s'", db->name);
      status = -1;
    }

    break;
  } /* while (status == 0) */

  /* If all went well, add this query to the list of queries within the
   * database structure. */
  if (status == 0)
  {
    o_database_t **temp;

    temp = (o_database_t **) realloc (databases,
        sizeof (*databases) * (databases_num + 1));
    if (temp == NULL)
    {
      ERROR ("oracle plugin: realloc failed");
      status = -1;
    }
    else
    {
      databases = temp;
      databases[databases_num] = db;
      databases_num++;
    }
  }

  if (status != 0)
  {
    o_database_free (db);
    return (-1);
  }

  return (0);
} /* }}} int o_config_add_database */

static int o_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp ("Query", child->key) == 0)
      o_config_add_query (child);
    else if (strcasecmp ("Database", child->key) == 0)
      o_config_add_database (child);
    else
    {
      WARNING ("snmp plugin: Ignoring unknown config option `%s'.", child->key);
    }
  } /* for (ci->children) */

  return (0);
} /* }}} int o_config */

/* }}} End of configuration handling functions */

static int o_init (void) /* {{{ */
{
  int status;

  if (oci_env != NULL)
    return (0);

  status = OCIEnvCreate (&oci_env,
      /* mode = */ OCI_THREADED,
      /* context        = */ NULL,
      /* malloc         = */ NULL,
      /* realloc        = */ NULL,
      /* free           = */ NULL,
      /* user_data_size = */ 0,
      /* user_data_ptr  = */ NULL);
  if (status != 0)
  {
    ERROR ("oracle plugin: OCIEnvCreate failed with status %i.", status);
    return (-1);
  }

  status = OCIHandleAlloc (oci_env, (void *) &oci_error, OCI_HTYPE_ERROR,
      /* user_data_size = */ 0, /* user_data = */ NULL);
  if (status != OCI_SUCCESS)
  {
    ERROR ("oracle plugin: OCIHandleAlloc (OCI_HTYPE_ERROR) failed "
        "with status %i.", status);
    return (-1);
  }

  return (0);
} /* }}} int o_init */

static void o_submit (o_database_t *db, o_result_t *r, /* {{{ */
    const data_set_t *ds, char **buffer_instances, char **buffer_values)
{
  value_list_t vl = VALUE_LIST_INIT;
  size_t i;

  assert (((size_t) ds->ds_num) == r->values_num);

  vl.values = (value_t *) malloc (sizeof (value_t) * r->values_num);
  if (vl.values == NULL)
  {
    ERROR ("oracle plugin: malloc failed.");
    return;
  }
  vl.values_len = ds->ds_num;

  for (i = 0; i < r->values_num; i++)
  {
    char *endptr;

    endptr = NULL;
    errno = 0;
    if (ds->ds[i].type == DS_TYPE_COUNTER)
      vl.values[i].counter = (counter_t) strtoll (buffer_values[i],
          &endptr, /* base = */ 0);
    else if (ds->ds[i].type == DS_TYPE_GAUGE)
      vl.values[i].gauge = (gauge_t) strtod (buffer_values[i], &endptr);
    else
      errno = EINVAL;

    if ((endptr == buffer_values[i]) || (errno != 0))
    {
      WARNING ("oracle plugin: o_submit: Parsing `%s' as %s failed.",
          buffer_values[i],
          (ds->ds[i].type == DS_TYPE_COUNTER) ? "counter" : "gauge");
      vl.values[i].gauge = NAN;
    }
  }

  vl.time = time (NULL);
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "oracle", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, db->name, sizeof (vl.type_instance));
  sstrncpy (vl.type, r->type, sizeof (vl.type));
  strjoin (vl.type_instance, sizeof (vl.type_instance),
      buffer_instances, r->instances_num, "-");
  vl.type_instance[sizeof (vl.type_instance) - 1] = 0;

  plugin_dispatch_values (&vl);
} /* }}} void o_submit */

static int o_handle_query_result (o_database_t *db, /* {{{ */
    o_query_t *q, o_result_t *r,
    char **column_names, char **column_values, size_t column_num)
{
  const data_set_t *ds;
  char **instances;
  char **values;
  size_t i;

  instances = NULL;
  values = NULL;

#define BAIL_OUT(status) \
  sfree (instances); \
  sfree (values); \
  return (status)

  /* Read `ds' and check number of values {{{ */
  ds = plugin_get_ds (r->type);
  if (ds == NULL)
  {
    ERROR ("oracle plugin: o_handle_query_result (%s, %s): Type `%s' is not "
        "known by the daemon. See types.db(5) for details.",
        db->name, q->name, r->type);
    BAIL_OUT (-1);
  }

  if (((size_t) ds->ds_num) != r->values_num)
  {
    ERROR ("oracle plugin: o_handle_query_result (%s, %s): The type `%s' "
        "requires exactly %i value%s, but the configuration specifies %zu.",
        db->name, q->name, r->type,
        ds->ds_num, (ds->ds_num == 1) ? "" : "s",
        r->values_num);
    BAIL_OUT (-1);
  }
  /* }}} */

  /* Allocate `instances' and `values'. {{{ */
  instances = (char **) calloc (r->instances_num, sizeof (char *));
  if (instances == NULL)
  {
    ERROR ("oracle plugin: o_handle_query_result (%s, %s): malloc failed.",
        db->name, q->name);
    BAIL_OUT (-1);
  }

  values = (char **) calloc (r->values_num, sizeof (char *));
  if (values == NULL)
  {
    sfree (instances);
    ERROR ("oracle plugin: o_handle_query_result (%s, %s): malloc failed.",
        db->name, q->name);
    BAIL_OUT (-1);
  }
  /* }}} */

  /* Fill `instances' with pointers to the appropriate strings from
   * `column_values' */
  for (i = 0; i < r->instances_num; i++) /* {{{ */
  {
    size_t j;

    instances[i] = NULL;
    for (j = 0; j < column_num; j++)
    {
      if (strcasecmp (r->instances[i], column_names[j]) == 0)
      {
        instances[i] = column_values[j];
        break;
      }
    }

    if (instances[i] == NULL)
    {
      ERROR ("oracle plugin: o_handle_query_result (%s, %s): "
          "Cannot find column `%s'. Is the statement correct?",
          db->name, q->name, r->instances[i]);
      BAIL_OUT (-1);
    }
  } /* }}} */

  /* Fill `values' with pointers to the appropriate strings from
   * `column_values' */
  for (i = 0; i < r->values_num; i++) /* {{{ */
  {
    size_t j;

    values[i] = NULL;
    for (j = 0; j < column_num; j++)
    {
      if (strcasecmp (r->values[i], column_names[j]) == 0)
      {
        values[i] = column_values[j];
        break;
      }
    }

    if (values[i] == NULL)
    {
      ERROR ("oracle plugin: o_handle_query_result (%s, %s): "
          "Cannot find column `%s'. Is the statement correct?",
          db->name, q->name, r->values[i]);
      BAIL_OUT (-1);
    }
  } /* }}} */

  o_submit (db, r, ds, instances, values);

  BAIL_OUT (0);
#undef BAIL_OUT
} /* }}} int o_handle_query_result */

static int o_read_database_query (o_database_t *db, /* {{{ */
    o_query_t *q)
{
  char **column_names;
  char **column_values;
  size_t column_num;

  /* List of `OCIDefine' pointers. These defines map columns to the buffer
   * space declared above. */
  OCIDefine **oci_defines;

  int status;
  size_t i;

  /* Prepare the statement */
  if (q->oci_statement == NULL) /* {{{ */
  {
    status = OCIHandleAlloc (oci_env, (void *) &q->oci_statement,
        OCI_HTYPE_STMT, /* user_data_size = */ 0, /* user_data = */ NULL);
    if (status != OCI_SUCCESS)
    {
      o_report_error ("o_read_database_query", "OCIHandleAlloc", oci_error);
      q->oci_statement = NULL;
      return (-1);
    }

    status = OCIStmtPrepare (q->oci_statement, oci_error,
        (text *) q->statement, (ub4) strlen (q->statement),
        /* language = */ OCI_NTV_SYNTAX,
        /* mode     = */ OCI_DEFAULT);
    if (status != OCI_SUCCESS)
    {
      o_report_error ("o_read_database_query", "OCIStmtPrepare", oci_error);
      OCIHandleFree (q->oci_statement, OCI_HTYPE_STMT);
      q->oci_statement = NULL;
      return (-1);
    }
    assert (q->oci_statement != NULL);
  } /* }}} */

  /* Execute the statement */
  status = OCIStmtExecute (db->oci_service_context, /* {{{ */
      q->oci_statement,
      oci_error,
      /* iters = */ 0,
      /* rowoff = */ 0,
      /* snap_in = */ NULL, /* snap_out = */ NULL,
      /* mode = */ OCI_DEFAULT);
  if (status != OCI_SUCCESS)
  {
    o_report_error ("o_read_database_query", "OCIStmtExecute", oci_error);
    ERROR ("oracle plugin: o_read_database_query: "
        "Failing statement was: %s", q->statement);
    return (-1);
  } /* }}} */

  /* Acquire the number of columns returned. */
  do /* {{{ */
  {
    ub4 param_counter = 0;
    status = OCIAttrGet (q->oci_statement, OCI_HTYPE_STMT, /* {{{ */
        &param_counter, /* size pointer = */ NULL, 
        OCI_ATTR_PARAM_COUNT, oci_error);
    if (status != OCI_SUCCESS)
    {
      o_report_error ("o_read_database_query", "OCIAttrGet", oci_error);
      return (-1);
    } /* }}} */

    column_num = (size_t) param_counter;
  } while (0); /* }}} */

  /* Allocate the following buffers:
   * 
   *  +---------------+-----------------------------------+
   *  ! Name          ! Size                              !
   *  +---------------+-----------------------------------+
   *  ! column_names  ! column_num x DATA_MAX_NAME_LEN    !
   *  ! column_values ! column_num x DATA_MAX_NAME_LEN    !
   *  ! oci_defines   ! column_num x sizeof (OCIDefine *) !
   *  +---------------+-----------------------------------+
   *
   * {{{ */
#define NUMBER_BUFFER_SIZE 64

#define FREE_ALL \
  if (column_names != NULL) { \
    sfree (column_names[0]); \
    sfree (column_names); \
  } \
  if (column_values != NULL) { \
    sfree (column_values[0]); \
    sfree (column_values); \
  } \
  sfree (oci_defines)

#define ALLOC_OR_FAIL(ptr, ptr_size) \
  do { \
    size_t alloc_size = (size_t) ((ptr_size)); \
    (ptr) = malloc (alloc_size); \
    if ((ptr) == NULL) { \
      FREE_ALL; \
      ERROR ("oracle plugin: o_read_database_query: malloc failed."); \
      return (-1); \
    } \
    memset ((ptr), 0, alloc_size); \
  } while (0)

  /* Initialize everything to NULL so the above works. */
  column_names  = NULL;
  column_values = NULL;
  oci_defines   = NULL;

  ALLOC_OR_FAIL (column_names, column_num * sizeof (char *));
  ALLOC_OR_FAIL (column_names[0], column_num * DATA_MAX_NAME_LEN
      * sizeof (char));
  for (i = 1; i < column_num; i++)
    column_names[i] = column_names[i - 1] + DATA_MAX_NAME_LEN;

  ALLOC_OR_FAIL (column_values, column_num * sizeof (char *));
  ALLOC_OR_FAIL (column_values[0], column_num * DATA_MAX_NAME_LEN
      * sizeof (char));
  for (i = 1; i < column_num; i++)
    column_values[i] = column_values[i - 1] + DATA_MAX_NAME_LEN;

  ALLOC_OR_FAIL (oci_defines, column_num * sizeof (OCIDefine *));
  /* }}} End of buffer allocations. */

  /* ``Define'' the returned data, i. e. bind the columns to the buffers
   * allocated above. */
  for (i = 0; i < column_num; i++) /* {{{ */
  {
    char *column_name;
    size_t column_name_length;
    char column_name_copy[DATA_MAX_NAME_LEN];
    OCIParam *oci_param;

    oci_param = NULL;

    status = OCIParamGet (q->oci_statement, OCI_HTYPE_STMT, oci_error,
        (void *) &oci_param, (ub4) (i + 1));
    if (status != OCI_SUCCESS)
    {
      /* This is probably alright */
      DEBUG ("oracle plugin: o_read_database_query: status = %#x (= %i);", status, status);
      o_report_error ("o_read_database_query", "OCIParamGet", oci_error);
      status = OCI_SUCCESS;
      break;
    }

    column_name = NULL;
    column_name_length = 0;
    status = OCIAttrGet (oci_param, OCI_DTYPE_PARAM,
        &column_name, &column_name_length, OCI_ATTR_NAME, oci_error);
    if (status != OCI_SUCCESS)
    {
      o_report_error ("o_read_database_query", "OCIAttrGet (OCI_ATTR_NAME)",
          oci_error);
      continue;
    }

    /* Copy the name to column_names. Warning: The ``string'' returned by OCI
     * may not be null terminated! */
    memset (column_names[i], 0, DATA_MAX_NAME_LEN);
    if (column_name_length >= DATA_MAX_NAME_LEN)
      column_name_length = DATA_MAX_NAME_LEN - 1;
    memcpy (column_names[i], column_name, column_name_length);
    column_names[i][column_name_length] = 0;

    DEBUG ("oracle plugin: o_read_database_query: column_names[%zu] = %s; "
        "column_name_length = %zu;",
        i, column_name_copy, column_name_length);

    status = OCIDefineByPos (q->oci_statement,
        &oci_defines[i], oci_error, (ub4) (i + 1),
        column_values[i], DATA_MAX_NAME_LEN, SQLT_STR,
        NULL, NULL, NULL, OCI_DEFAULT);
    if (status != OCI_SUCCESS)
    {
      o_report_error ("o_read_database_query", "OCIDefineByPos", oci_error);
      continue;
    }
  } /* for (j = 1; j <= param_counter; j++) */
  /* }}} End of the ``define'' stuff. */

  /* Fetch and handle all the rows that matched the query. */
  while (42) /* {{{ */
  {
    o_result_t *r;

    status = OCIStmtFetch2 (q->oci_statement, oci_error,
        /* nrows = */ 1, /* orientation = */ OCI_FETCH_NEXT,
        /* fetch offset = */ 0, /* mode = */ OCI_DEFAULT);
    if (status == OCI_NO_DATA)
    {
      status = OCI_SUCCESS;
      break;
    }
    else if ((status != OCI_SUCCESS) && (status != OCI_SUCCESS_WITH_INFO))
    {
      o_report_error ("o_read_database_query", "OCIStmtFetch2", oci_error);
      break;
    }

    for (i = 0; i < column_num; i++)
    {
      DEBUG ("oracle plugin: o_read_database_query: [%zu] %s = %s;",
           i, column_names[i], column_values[i]);
    }

    for (r = q->results; r != NULL; r = r->next)
      o_handle_query_result (db, q, r, column_names, column_values, column_num);
  } /* }}} while (42) */

  /* DEBUG ("oracle plugin: o_read_database_query: This statement succeeded: %s", q->statement); */
  FREE_ALL;

  return (0);
#undef FREE_ALL
#undef ALLOC_OR_FAIL
} /* }}} int o_read_database_query */

static int o_read_database (o_database_t *db) /* {{{ */
{
  size_t i;
  int status;

  if (db->oci_service_context == NULL)
  {
    status = OCILogon (oci_env, oci_error,
        &db->oci_service_context,
        (OraText *) db->username, (ub4) strlen (db->username),
        (OraText *) db->password, (ub4) strlen (db->password),
        (OraText *) db->connect_id, (ub4) strlen (db->connect_id));
    if (status != OCI_SUCCESS)
    {
      o_report_error ("o_read_database", "OCILogon", oci_error);
      DEBUG ("oracle plugin: OCILogon (%s): db->oci_service_context = %p;",
          db->connect_id, db->oci_service_context);
      db->oci_service_context = NULL;
      return (-1);
    }
    assert (db->oci_service_context != NULL);
  }

  DEBUG ("oracle plugin: o_read_database: db->connect_id = %s; db->oci_service_context = %p;",
      db->connect_id, db->oci_service_context);

  for (i = 0; i < db->queries_num; i++)
    o_read_database_query (db, db->queries[i]);

  return (0);
} /* }}} int o_read_database */

static int o_read (void) /* {{{ */
{
  size_t i;

  for (i = 0; i < databases_num; i++)
    o_read_database (databases[i]);

  return (0);
} /* }}} int o_read */

static int o_shutdown (void) /* {{{ */
{
  size_t i;

  for (i = 0; i < databases_num; i++)
    if (databases[i]->oci_service_context != NULL)
    {
      OCIHandleFree (databases[i]->oci_service_context, OCI_HTYPE_SVCCTX);
      databases[i]->oci_service_context = NULL;
    }
  
  for (i = 0; i < queries_num; i++)
    if (queries[i]->oci_statement != NULL)
    {
      OCIHandleFree (queries[i]->oci_statement, OCI_HTYPE_STMT);
      queries[i]->oci_statement = NULL;
    }
  
  OCIHandleFree (oci_env, OCI_HTYPE_ENV);
  return (0);
} /* }}} int o_shutdown */

void module_register (void) /* {{{ */
{
  plugin_register_complex_config ("oracle", o_config);
  plugin_register_init ("oracle", o_init);
  plugin_register_read ("oracle", o_read);
  plugin_register_shutdown ("oracle", o_shutdown);
} /* }}} void module_register */

/*
 * vim: shiftwidth=2 softtabstop=2 et fdm=marker
 */
