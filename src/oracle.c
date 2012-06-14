/**
 * collectd - src/oracle.c
 * Copyright (C) 2008,2009  noris network AG
 * Copyright (C) 2012       Florian octo Forster
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
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_db_query.h"

#include <oci.h>

/*
 * Data types
 */
struct o_database_s
{
  char *name;
  char *host;
  char *connect_id;
  char *username;
  char *password;

  udb_query_preparation_area_t **q_prep_areas;
  udb_query_t **queries;
  size_t        queries_num;

  OCISvcCtx *oci_service_context;
};
typedef struct o_database_s o_database_t;

/*
 * Global variables
 */
static udb_query_t  **queries       = NULL;
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
  unsigned int record_number;

  /* An operation may cause / return multiple errors. Loop until we have
   * handled all errors available (with a fail-save limit of 16). */
  for (record_number = 1; record_number <= 16; record_number++)
  {
    memset (buffer, 0, sizeof (buffer));
    error_code = -1;

    status = OCIErrorGet (eh, (ub4) record_number,
        /* sqlstate = */ NULL,
        &error_code,
        (text *) &buffer[0],
        (ub4) sizeof (buffer),
        OCI_HTYPE_ERROR);
    buffer[sizeof (buffer) - 1] = 0;

    if (status == OCI_NO_DATA)
      return;

    if (status == OCI_SUCCESS)
    {
      size_t buffer_length;

      buffer_length = strlen (buffer);
      while ((buffer_length > 0) && (buffer[buffer_length - 1] < 32))
      {
        buffer_length--;
        buffer[buffer_length] = 0;
      }

      ERROR ("oracle plugin: %s: %s failed: %s", where, what, buffer);
    }
    else
    {
      ERROR ("oracle plugin: %s: %s failed. Additionally, OCIErrorGet failed with status %i.",
          where, what, status);
      return;
    }
  }
} /* }}} void o_report_error */

static void o_database_free (o_database_t *db) /* {{{ */
{
  size_t i;

  if (db == NULL)
    return;

  sfree (db->name);
  sfree (db->connect_id);
  sfree (db->username);
  sfree (db->password);
  sfree (db->queries);

  if (db->q_prep_areas != NULL)
    for (i = 0; i < db->queries_num; ++i)
      udb_query_delete_preparation_area (db->q_prep_areas[i]);
  free (db->q_prep_areas);

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
  db->name = NULL;
  db->host = NULL;
  db->connect_id = NULL;
  db->username = NULL;
  db->password = NULL;

  status = cf_util_get_string (ci, &db->name);
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
      status = cf_util_get_string (child, &db->connect_id);
    else if (strcasecmp ("Host", child->key) == 0)
      status = cf_util_get_string (child, &db->host);
    else if (strcasecmp ("Username", child->key) == 0)
      status = cf_util_get_string (child, &db->username);
    else if (strcasecmp ("Password", child->key) == 0)
      status = cf_util_get_string (child, &db->password);
    else if (strcasecmp ("Query", child->key) == 0)
      status = udb_query_pick_from_list (child, queries, queries_num,
          &db->queries, &db->queries_num);
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

  while ((status == 0) && (db->queries_num > 0))
  {
    db->q_prep_areas = (udb_query_preparation_area_t **) calloc (
        db->queries_num, sizeof (*db->q_prep_areas));

    if (db->q_prep_areas == NULL)
    {
      WARNING ("oracle plugin: malloc failed");
      status = -1;
      break;
    }

    for (i = 0; i < db->queries_num; ++i)
    {
      db->q_prep_areas[i]
        = udb_query_allocate_preparation_area (db->queries[i]);

      if (db->q_prep_areas[i] == NULL)
      {
        WARNING ("oracle plugin: udb_query_allocate_preparation_area failed");
        status = -1;
        break;
      }
    }

    break;
  }

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
      udb_query_create (&queries, &queries_num, child,
          /* callback = */ NULL);
    else if (strcasecmp ("Database", child->key) == 0)
      o_config_add_database (child);
    else
    {
      WARNING ("oracle plugin: Ignoring unknown config option `%s'.", child->key);
    }

    if (queries_num > 0)
    {
      DEBUG ("oracle plugin: o_config: queries_num = %zu; queries[0] = %p; udb_query_get_user_data (queries[0]) = %p;",
          queries_num, (void *) queries[0], udb_query_get_user_data (queries[0]));
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

static int o_read_database_query (o_database_t *db, /* {{{ */
    udb_query_t *q, udb_query_preparation_area_t *prep_area)
{
  char **column_names;
  char **column_values;
  size_t column_num;

  OCIStmt *oci_statement;

  /* List of `OCIDefine' pointers. These defines map columns to the buffer
   * space declared above. */
  OCIDefine **oci_defines;

  int status;
  size_t i;

  oci_statement = udb_query_get_user_data (q);

  /* Prepare the statement */
  if (oci_statement == NULL) /* {{{ */
  {
    const char *statement;

    statement = udb_query_get_statement (q);
    assert (statement != NULL);

    status = OCIHandleAlloc (oci_env, (void *) &oci_statement,
        OCI_HTYPE_STMT, /* user_data_size = */ 0, /* user_data = */ NULL);
    if (status != OCI_SUCCESS)
    {
      o_report_error ("o_read_database_query", "OCIHandleAlloc", oci_error);
      oci_statement = NULL;
      return (-1);
    }

    status = OCIStmtPrepare (oci_statement, oci_error,
        (text *) statement, (ub4) strlen (statement),
        /* language = */ OCI_NTV_SYNTAX,
        /* mode     = */ OCI_DEFAULT);
    if (status != OCI_SUCCESS)
    {
      o_report_error ("o_read_database_query", "OCIStmtPrepare", oci_error);
      OCIHandleFree (oci_statement, OCI_HTYPE_STMT);
      oci_statement = NULL;
      return (-1);
    }
    udb_query_set_user_data (q, oci_statement);

    DEBUG ("oracle plugin: o_read_database_query (%s, %s): "
        "Successfully allocated statement handle.",
        db->name, udb_query_get_name (q));
  } /* }}} */

  assert (oci_statement != NULL);

  /* Execute the statement */
  status = OCIStmtExecute (db->oci_service_context, /* {{{ */
      oci_statement,
      oci_error,
      /* iters = */ 0,
      /* rowoff = */ 0,
      /* snap_in = */ NULL, /* snap_out = */ NULL,
      /* mode = */ OCI_DEFAULT);
  if (status != OCI_SUCCESS)
  {
    DEBUG ("oracle plugin: o_read_database_query: status = %i (%#x)", status, status);
    o_report_error ("o_read_database_query", "OCIStmtExecute", oci_error);
    ERROR ("oracle plugin: o_read_database_query: "
        "Failing statement was: %s", udb_query_get_statement (q));
    return (-1);
  } /* }}} */

  /* Acquire the number of columns returned. */
  do /* {{{ */
  {
    ub4 param_counter = 0;
    status = OCIAttrGet (oci_statement, OCI_HTYPE_STMT, /* {{{ */
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
    ub4 column_name_length;
    OCIParam *oci_param;

    oci_param = NULL;

    status = OCIParamGet (oci_statement, OCI_HTYPE_STMT, oci_error,
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
      OCIDescriptorFree (oci_param, OCI_DTYPE_PARAM);
      o_report_error ("o_read_database_query", "OCIAttrGet (OCI_ATTR_NAME)",
          oci_error);
      continue;
    }

    OCIDescriptorFree (oci_param, OCI_DTYPE_PARAM);
    oci_param = NULL;

    /* Copy the name to column_names. Warning: The ``string'' returned by OCI
     * may not be null terminated! */
    memset (column_names[i], 0, DATA_MAX_NAME_LEN);
    if (column_name_length >= DATA_MAX_NAME_LEN)
      column_name_length = DATA_MAX_NAME_LEN - 1;
    memcpy (column_names[i], column_name, column_name_length);
    column_names[i][column_name_length] = 0;

    DEBUG ("oracle plugin: o_read_database_query: column_names[%zu] = %s; "
        "column_name_length = %"PRIu32";",
        i, column_names[i], (uint32_t) column_name_length);

    status = OCIDefineByPos (oci_statement,
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

  status = udb_query_prepare_result (q, prep_area,
      (db->host != NULL) ? db->host : hostname_g,
      /* plugin = */ "oracle", db->name, column_names, column_num,
      /* interval = */ 0);
  if (status != 0)
  {
    ERROR ("oracle plugin: o_read_database_query (%s, %s): "
        "udb_query_prepare_result failed.",
        db->name, udb_query_get_name (q));
    FREE_ALL;
    return (-1);
  }

  /* Fetch and handle all the rows that matched the query. */
  while (42) /* {{{ */
  {
    status = OCIStmtFetch2 (oci_statement, oci_error,
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

    status = udb_query_handle_result (q, prep_area, column_values);
    if (status != 0)
    {
      WARNING ("oracle plugin: o_read_database_query (%s, %s): "
          "udb_query_handle_result failed.",
          db->name, udb_query_get_name (q));
    }
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

  if (db->oci_service_context != NULL)
  {
    OCIServer *server_handle;
    ub4 connection_status;

    server_handle = NULL;
    status = OCIAttrGet ((void *) db->oci_service_context, OCI_HTYPE_SVCCTX, 
        (void *) &server_handle, /* size pointer = */ NULL,
        OCI_ATTR_SERVER, oci_error);
    if (status != OCI_SUCCESS)
    {
      o_report_error ("o_read_database", "OCIAttrGet", oci_error);
      return (-1);
    }

    if (server_handle == NULL)
    {
      connection_status = OCI_SERVER_NOT_CONNECTED;
    }
    else /* if (server_handle != NULL) */
    {
      connection_status = 0;
      status = OCIAttrGet ((void *) server_handle, OCI_HTYPE_SERVER,
          (void *) &connection_status, /* size pointer = */ NULL,
          OCI_ATTR_SERVER_STATUS, oci_error);
      if (status != OCI_SUCCESS)
      {
        o_report_error ("o_read_database", "OCIAttrGet", oci_error);
        return (-1);
      }
    }

    if (connection_status != OCI_SERVER_NORMAL)
    {
      INFO ("oracle plugin: Connection to %s lost. Trying to reconnect.",
          db->name);
      OCIHandleFree (db->oci_service_context, OCI_HTYPE_SVCCTX);
      db->oci_service_context = NULL;
    }
  } /* if (db->oci_service_context != NULL) */

  if (db->oci_service_context == NULL)
  {
    status = OCILogon (oci_env, oci_error,
        &db->oci_service_context,
        (OraText *) db->username, (ub4) strlen (db->username),
        (OraText *) db->password, (ub4) strlen (db->password),
        (OraText *) db->connect_id, (ub4) strlen (db->connect_id));
    if ((status != OCI_SUCCESS) && (status != OCI_SUCCESS_WITH_INFO))
    {
      o_report_error ("o_read_database", "OCILogon", oci_error);
      DEBUG ("oracle plugin: OCILogon (%s): db->oci_service_context = %p;",
          db->connect_id, db->oci_service_context);
      db->oci_service_context = NULL;
      return (-1);
    }
    else if (status == OCI_SUCCESS_WITH_INFO)
    {
      /* TODO: Print NOTIFY message. */
    }
    assert (db->oci_service_context != NULL);
  }

  DEBUG ("oracle plugin: o_read_database: db->connect_id = %s; db->oci_service_context = %p;",
      db->connect_id, db->oci_service_context);

  for (i = 0; i < db->queries_num; i++)
    o_read_database_query (db, db->queries[i], db->q_prep_areas[i]);

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
  {
    OCIStmt *oci_statement;

    oci_statement = udb_query_get_user_data (queries[i]);
    if (oci_statement != NULL)
    {
      OCIHandleFree (oci_statement, OCI_HTYPE_STMT);
      udb_query_set_user_data (queries[i], NULL);
    }
  }
  
  OCIHandleFree (oci_env, OCI_HTYPE_ENV);
  oci_env = NULL;

  udb_query_free (queries, queries_num);
  queries = NULL;
  queries_num = 0;

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
