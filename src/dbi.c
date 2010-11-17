/**
 * collectd - src/dbi.c
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
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_db_query.h"

#include <dbi/dbi.h>

/*
 * Data types
 */
struct cdbi_driver_option_s /* {{{ */
{
  char *key;
  char *value;
};
typedef struct cdbi_driver_option_s cdbi_driver_option_t; /* }}} */

struct cdbi_database_s /* {{{ */
{
  char *name;
  char *select_db;

  char *driver;
  cdbi_driver_option_t *driver_options;
  size_t driver_options_num;

  udb_query_preparation_area_t **q_prep_areas;
  udb_query_t **queries;
  size_t        queries_num;

  dbi_conn connection;
};
typedef struct cdbi_database_s cdbi_database_t; /* }}} */

/*
 * Global variables
 */
static udb_query_t     **queries       = NULL;
static size_t            queries_num   = 0;
static cdbi_database_t **databases     = NULL;
static size_t            databases_num = 0;

/*
 * Functions
 */
static const char *cdbi_strerror (dbi_conn conn, /* {{{ */
    char *buffer, size_t buffer_size)
{
  const char *msg;
  int status;

  if (conn == NULL)
  {
    sstrncpy (buffer, "connection is NULL", buffer_size);
    return (buffer);
  }

  msg = NULL;
  status = dbi_conn_error (conn, &msg);
  if ((status >= 0) && (msg != NULL))
    ssnprintf (buffer, buffer_size, "%s (status %i)", msg, status);
  else
    ssnprintf (buffer, buffer_size, "dbi_conn_error failed with status %i",
        status);

  return (buffer);
} /* }}} const char *cdbi_conn_error */

static int cdbi_result_get_field (dbi_result res, /* {{{ */
    unsigned int index, char *buffer, size_t buffer_size)
{
  unsigned short src_type;

  src_type = dbi_result_get_field_type_idx (res, index);
  if (src_type == DBI_TYPE_ERROR)
  {
    ERROR ("dbi plugin: cdbi_result_get: "
        "dbi_result_get_field_type_idx failed.");
    return (-1);
  }

  if (src_type == DBI_TYPE_INTEGER)
  {
    long long value;

    value = dbi_result_get_longlong_idx (res, index);
    ssnprintf (buffer, buffer_size, "%lli", value);
  }
  else if (src_type == DBI_TYPE_DECIMAL)
  {
    double value;

    value = dbi_result_get_double_idx (res, index);
    ssnprintf (buffer, buffer_size, "%63.15g", value);
  }
  else if (src_type == DBI_TYPE_STRING)
  {
    const char *value;
    
    value = dbi_result_get_string_idx (res, index);
    if (value == NULL)
      sstrncpy (buffer, "", buffer_size);
    else if (strcmp ("ERROR", value) == 0)
      return (-1);
    else
      sstrncpy (buffer, value, buffer_size);
  }
  /* DBI_TYPE_BINARY */
  /* DBI_TYPE_DATETIME */
  else
  {
    const char *field_name;

    field_name = dbi_result_get_field_name (res, index);
    if (field_name == NULL)
      field_name = "<unknown>";

    ERROR ("dbi plugin: Column `%s': Don't know how to handle "
        "source type %hu.",
        field_name, src_type);
    return (-1);
  }

  return (0);
} /* }}} int cdbi_result_get_field */

static void cdbi_database_free (cdbi_database_t *db) /* {{{ */
{
  size_t i;

  if (db == NULL)
    return;

  sfree (db->name);
  sfree (db->driver);

  for (i = 0; i < db->driver_options_num; i++)
  {
    sfree (db->driver_options[i].key);
    sfree (db->driver_options[i].value);
  }
  sfree (db->driver_options);

  if (db->q_prep_areas)
    for (i = 0; i < db->queries_num; ++i)
      udb_query_delete_preparation_area (db->q_prep_areas[i]);
  free (db->q_prep_areas);

  sfree (db);
} /* }}} void cdbi_database_free */

/* Configuration handling functions {{{
 *
 * <Plugin dbi>
 *   <Query "plugin_instance0">
 *     Statement "SELECT name, value FROM table"
 *     <Result>
 *       Type "gauge"
 *       InstancesFrom "name"
 *       ValuesFrom "value"
 *     </Result>
 *     ...
 *   </Query>
 *     
 *   <Database "plugin_instance1">
 *     Driver "mysql"
 *     DriverOption "hostname" "localhost"
 *     ...
 *     Query "plugin_instance0"
 *   </Database>
 * </Plugin>
 */

static int cdbi_config_set_string (char **ret_string, /* {{{ */
    oconfig_item_t *ci)
{
  char *string;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("dbi plugin: The `%s' config option "
        "needs exactly one string argument.", ci->key);
    return (-1);
  }

  string = strdup (ci->values[0].value.string);
  if (string == NULL)
  {
    ERROR ("dbi plugin: strdup failed.");
    return (-1);
  }

  if (*ret_string != NULL)
    free (*ret_string);
  *ret_string = string;

  return (0);
} /* }}} int cdbi_config_set_string */

static int cdbi_config_add_database_driver_option (cdbi_database_t *db, /* {{{ */
    oconfig_item_t *ci)
{
  cdbi_driver_option_t *option;

  if ((ci->values_num != 2)
      || (ci->values[0].type != OCONFIG_TYPE_STRING)
      || (ci->values[1].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("dbi plugin: The `DriverOption' config option "
        "needs exactly two string arguments.");
    return (-1);
  }

  option = (cdbi_driver_option_t *) realloc (db->driver_options,
      sizeof (*option) * (db->driver_options_num + 1));
  if (option == NULL)
  {
    ERROR ("dbi plugin: realloc failed");
    return (-1);
  }

  db->driver_options = option;
  option = db->driver_options + db->driver_options_num;

  option->key = strdup (ci->values[0].value.string);
  if (option->key == NULL)
  {
    ERROR ("dbi plugin: strdup failed.");
    return (-1);
  }

  option->value = strdup (ci->values[1].value.string);
  if (option->value == NULL)
  {
    ERROR ("dbi plugin: strdup failed.");
    sfree (option->key);
    return (-1);
  }

  db->driver_options_num++;
  return (0);
} /* }}} int cdbi_config_add_database_driver_option */

static int cdbi_config_add_database (oconfig_item_t *ci) /* {{{ */
{
  cdbi_database_t *db;
  int status;
  int i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("dbi plugin: The `Database' block "
        "needs exactly one string argument.");
    return (-1);
  }

  db = (cdbi_database_t *) malloc (sizeof (*db));
  if (db == NULL)
  {
    ERROR ("dbi plugin: malloc failed.");
    return (-1);
  }
  memset (db, 0, sizeof (*db));

  status = cdbi_config_set_string (&db->name, ci);
  if (status != 0)
  {
    sfree (db);
    return (status);
  }

  /* Fill the `cdbi_database_t' structure.. */
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Driver", child->key) == 0)
      status = cdbi_config_set_string (&db->driver, child);
    else if (strcasecmp ("DriverOption", child->key) == 0)
      status = cdbi_config_add_database_driver_option (db, child);
    else if (strcasecmp ("SelectDB", child->key) == 0)
      status = cdbi_config_set_string (&db->select_db, child);
    else if (strcasecmp ("Query", child->key) == 0)
      status = udb_query_pick_from_list (child, queries, queries_num,
          &db->queries, &db->queries_num);
    else
    {
      WARNING ("dbi plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  /* Check that all necessary options have been given. */
  while (status == 0)
  {
    if (db->driver == NULL)
    {
      WARNING ("dbi plugin: `Driver' not given for database `%s'", db->name);
      status = -1;
    }
    if (db->driver_options_num == 0)
    {
      WARNING ("dbi plugin: No `DriverOption' given for database `%s'. "
          "This will likely not work.", db->name);
    }

    break;
  } /* while (status == 0) */

  while ((status == 0) && (db->queries_num > 0))
  {
    db->q_prep_areas = (udb_query_preparation_area_t **) calloc (
        db->queries_num, sizeof (*db->q_prep_areas));

    if (db->q_prep_areas == NULL)
    {
      WARNING ("dbi plugin: malloc failed");
      status = -1;
      break;
    }

    for (i = 0; i < db->queries_num; ++i)
    {
      db->q_prep_areas[i]
        = udb_query_allocate_preparation_area (db->queries[i]);

      if (db->q_prep_areas[i] == NULL)
      {
        WARNING ("dbi plugin: udb_query_allocate_preparation_area failed");
        status = -1;
        break;
      }
    }

    break;
  }

  /* If all went well, add this database to the global list of databases. */
  if (status == 0)
  {
    cdbi_database_t **temp;

    temp = (cdbi_database_t **) realloc (databases,
        sizeof (*databases) * (databases_num + 1));
    if (temp == NULL)
    {
      ERROR ("dbi plugin: realloc failed");
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
    cdbi_database_free (db);
    return (-1);
  }

  return (0);
} /* }}} int cdbi_config_add_database */

static int cdbi_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp ("Query", child->key) == 0)
      udb_query_create (&queries, &queries_num, child,
          /* callback = */ NULL);
    else if (strcasecmp ("Database", child->key) == 0)
      cdbi_config_add_database (child);
    else
    {
      WARNING ("snmp plugin: Ignoring unknown config option `%s'.", child->key);
    }
  } /* for (ci->children) */

  return (0);
} /* }}} int cdbi_config */

/* }}} End of configuration handling functions */

static int cdbi_init (void) /* {{{ */
{
  static int did_init = 0;
  int status;

  if (did_init != 0)
    return (0);

  if (queries_num == 0)
  {
    ERROR ("dbi plugin: No <Query> blocks have been found. Without them, "
        "this plugin can't do anything useful, so we will returns an error.");
    return (-1);
  }

  if (databases_num == 0)
  {
    ERROR ("dbi plugin: No <Database> blocks have been found. Without them, "
        "this plugin can't do anything useful, so we will returns an error.");
    return (-1);
  }

  status = dbi_initialize (NULL);
  if (status < 0)
  {
    ERROR ("dbi plugin: cdbi_init: dbi_initialize failed with status %i.",
        status);
    return (-1);
  }
  else if (status == 0)
  {
    ERROR ("dbi plugin: `dbi_initialize' could not load any drivers. Please "
        "install at least one `DBD' or check your installation.");
    return (-1);
  }
  DEBUG ("dbi plugin: cdbi_init: dbi_initialize reports %i driver%s.",
      status, (status == 1) ? "" : "s");

  return (0);
} /* }}} int cdbi_init */

static int cdbi_read_database_query (cdbi_database_t *db, /* {{{ */
    udb_query_t *q, udb_query_preparation_area_t *prep_area)
{
  const char *statement;
  dbi_result res;
  size_t column_num;
  char **column_names;
  char **column_values;
  int status;
  size_t i;

  /* Macro that cleans up dynamically allocated memory and returns the
   * specified status. */
#define BAIL_OUT(status) \
  if (column_names != NULL) { sfree (column_names[0]); sfree (column_names); } \
  if (column_values != NULL) { sfree (column_values[0]); sfree (column_values); } \
  if (res != NULL) { dbi_result_free (res); res = NULL; } \
  return (status)

  column_names = NULL;
  column_values = NULL;
  res = NULL;

  statement = udb_query_get_statement (q);
  assert (statement != NULL);

  res = dbi_conn_query (db->connection, statement);
  if (res == NULL)
  {
    char errbuf[1024];
    ERROR ("dbi plugin: cdbi_read_database_query (%s, %s): "
        "dbi_conn_query failed: %s",
        db->name, udb_query_get_name (q),
        cdbi_strerror (db->connection, errbuf, sizeof (errbuf)));
    BAIL_OUT (-1);
  }
  else /* Get the number of columns */
  {
    unsigned int db_status;

    db_status = dbi_result_get_numfields (res);
    if (db_status == DBI_FIELD_ERROR)
    {
      char errbuf[1024];
      ERROR ("dbi plugin: cdbi_read_database_query (%s, %s): "
          "dbi_result_get_numfields failed: %s",
          db->name, udb_query_get_name (q),
          cdbi_strerror (db->connection, errbuf, sizeof (errbuf)));
      BAIL_OUT (-1);
    }

    column_num = (size_t) db_status;
    DEBUG ("cdbi_read_database_query (%s, %s): There are %zu columns.",
        db->name, udb_query_get_name (q), column_num);
  }

  /* Allocate `column_names' and `column_values'. {{{ */
  column_names = (char **) calloc (column_num, sizeof (char *));
  if (column_names == NULL)
  {
    ERROR ("dbi plugin: malloc failed.");
    BAIL_OUT (-1);
  }

  column_names[0] = (char *) calloc (column_num,
      DATA_MAX_NAME_LEN * sizeof (char));
  if (column_names[0] == NULL)
  {
    ERROR ("dbi plugin: malloc failed.");
    BAIL_OUT (-1);
  }
  for (i = 1; i < column_num; i++)
    column_names[i] = column_names[i - 1] + DATA_MAX_NAME_LEN;

  column_values = (char **) calloc (column_num, sizeof (char *));
  if (column_values == NULL)
  {
    ERROR ("dbi plugin: malloc failed.");
    BAIL_OUT (-1);
  }

  column_values[0] = (char *) calloc (column_num,
      DATA_MAX_NAME_LEN * sizeof (char));
  if (column_values[0] == NULL)
  {
    ERROR ("dbi plugin: malloc failed.");
    BAIL_OUT (-1);
  }
  for (i = 1; i < column_num; i++)
    column_values[i] = column_values[i - 1] + DATA_MAX_NAME_LEN;
  /* }}} */

  /* Copy the field names to `column_names' */
  for (i = 0; i < column_num; i++) /* {{{ */
  {
    const char *column_name;

    column_name = dbi_result_get_field_name (res, (unsigned int) (i + 1));
    if (column_name == NULL)
    {
      ERROR ("dbi plugin: cdbi_read_database_query (%s, %s): "
          "Cannot retrieve name of field %zu.",
          db->name, udb_query_get_name (q), i + 1);
      BAIL_OUT (-1);
    }

    sstrncpy (column_names[i], column_name, DATA_MAX_NAME_LEN);
  } /* }}} for (i = 0; i < column_num; i++) */

  udb_query_prepare_result (q, prep_area, hostname_g,
      /* plugin = */ "dbi", db->name,
      column_names, column_num, /* interval = */ 0);

  /* 0 = error; 1 = success; */
  status = dbi_result_first_row (res); /* {{{ */
  if (status != 1)
  {
    char errbuf[1024];
    ERROR ("dbi plugin: cdbi_read_database_query (%s, %s): "
        "dbi_result_first_row failed: %s. Maybe the statement didn't "
        "return any rows?",
        db->name, udb_query_get_name (q),
        cdbi_strerror (db->connection, errbuf, sizeof (errbuf)));
    udb_query_finish_result (q, prep_area);
    BAIL_OUT (-1);
  } /* }}} */

  /* Iterate over all rows and call `udb_query_handle_result' with each list of
   * values. */
  while (42) /* {{{ */
  {
    status = 0;
    /* Copy the value of the columns to `column_values' */
    for (i = 0; i < column_num; i++) /* {{{ */
    {
      status = cdbi_result_get_field (res, (unsigned int) (i + 1),
          column_values[i], DATA_MAX_NAME_LEN);

      if (status != 0)
      {
        ERROR ("dbi plugin: cdbi_read_database_query (%s, %s): "
            "cdbi_result_get_field (%zu) failed.",
            db->name, udb_query_get_name (q), i + 1);
        status = -1;
        break;
      }
    } /* }}} for (i = 0; i < column_num; i++) */

    /* If all values were copied successfully, call `udb_query_handle_result'
     * to dispatch the row to the daemon. */
    if (status == 0) /* {{{ */
    {
      status = udb_query_handle_result (q, prep_area, column_values);
      if (status != 0)
      {
        ERROR ("dbi plugin: cdbi_read_database_query (%s, %s): "
            "udb_query_handle_result failed.",
            db->name, udb_query_get_name (q));
      }
    } /* }}} */

    /* Get the next row from the database. */
    status = dbi_result_next_row (res); /* {{{ */
    if (status != 1)
    {
      if (dbi_conn_error (db->connection, NULL) != 0)
      {
        char errbuf[1024];
        WARNING ("dbi plugin: cdbi_read_database_query (%s, %s): "
            "dbi_result_next_row failed: %s.",
            db->name, udb_query_get_name (q),
            cdbi_strerror (db->connection, errbuf, sizeof (errbuf)));
      }
      break;
    } /* }}} */
  } /* }}} while (42) */

  /* Tell the db query interface that we're done with this query. */
  udb_query_finish_result (q, prep_area);

  /* Clean up and return `status = 0' (success) */
  BAIL_OUT (0);
#undef BAIL_OUT
} /* }}} int cdbi_read_database_query */

static int cdbi_connect_database (cdbi_database_t *db) /* {{{ */
{
  dbi_driver driver;
  dbi_conn connection;
  size_t i;
  int status;

  if (db->connection != NULL)
  {
    status = dbi_conn_ping (db->connection);
    if (status != 0) /* connection is alive */
      return (0);

    dbi_conn_close (db->connection);
    db->connection = NULL;
  }

  driver = dbi_driver_open (db->driver);
  if (driver == NULL)
  {
    ERROR ("dbi plugin: cdbi_connect_database: dbi_driver_open (%s) failed.",
        db->driver);
    INFO ("dbi plugin: Maybe the driver isn't installed? "
        "Known drivers are:");
    for (driver = dbi_driver_list (NULL);
        driver != NULL;
        driver = dbi_driver_list (driver))
    {
      INFO ("dbi plugin: * %s", dbi_driver_get_name (driver));
    }
    return (-1);
  }

  connection = dbi_conn_open (driver);
  if (connection == NULL)
  {
    ERROR ("dbi plugin: cdbi_connect_database: dbi_conn_open (%s) failed.",
        db->driver);
    return (-1);
  }

  /* Set all the driver options. Because this is a very very very generic
   * interface, the error handling is kind of long. If an invalid option is
   * encountered, it will get a list of options understood by the driver and
   * report that as `INFO'. This way, users hopefully don't have too much
   * trouble finding out how to configure the plugin correctly.. */
  for (i = 0; i < db->driver_options_num; i++)
  {
    DEBUG ("dbi plugin: cdbi_connect_database (%s): "
        "key = %s; value = %s;",
        db->name,
        db->driver_options[i].key,
        db->driver_options[i].value);

    status = dbi_conn_set_option (connection,
        db->driver_options[i].key, db->driver_options[i].value);
    if (status != 0)
    {
      char errbuf[1024];
      const char *opt;

      ERROR ("dbi plugin: cdbi_connect_database (%s): "
          "dbi_conn_set_option (%s, %s) failed: %s.",
          db->name,
          db->driver_options[i].key, db->driver_options[i].value,
          cdbi_strerror (connection, errbuf, sizeof (errbuf)));

      INFO ("dbi plugin: This is a list of all options understood "
          "by the `%s' driver:", db->driver);
      for (opt = dbi_conn_get_option_list (connection, NULL);
          opt != NULL;
          opt = dbi_conn_get_option_list (connection, opt))
      {
        INFO ("dbi plugin: * %s", opt);
      }

      dbi_conn_close (connection);
      return (-1);
    }
  } /* for (i = 0; i < db->driver_options_num; i++) */

  status = dbi_conn_connect (connection);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("dbi plugin: cdbi_connect_database (%s): "
        "dbi_conn_connect failed: %s",
        db->name, cdbi_strerror (connection, errbuf, sizeof (errbuf)));
    dbi_conn_close (connection);
    return (-1);
  }

  if (db->select_db != NULL)
  {
    status = dbi_conn_select_db (connection, db->select_db);
    if (status != 0)
    {
      char errbuf[1024];
      WARNING ("dbi plugin: cdbi_connect_database (%s): "
          "dbi_conn_select_db (%s) failed: %s. Check the `SelectDB' option.",
          db->name, db->select_db,
          cdbi_strerror (connection, errbuf, sizeof (errbuf)));
      dbi_conn_close (connection);
      return (-1);
    }
  }

  db->connection = connection;
  return (0);
} /* }}} int cdbi_connect_database */

static int cdbi_read_database (cdbi_database_t *db) /* {{{ */
{
  size_t i;
  int success;
  int status;

  unsigned int db_version;

  status = cdbi_connect_database (db);
  if (status != 0)
    return (status);
  assert (db->connection != NULL);

  db_version = dbi_conn_get_engine_version (db->connection);
  /* TODO: Complain if `db_version == 0' */

  success = 0;
  for (i = 0; i < db->queries_num; i++)
  {
    /* Check if we know the database's version and if so, if this query applies
     * to that version. */
    if ((db_version != 0)
        && (udb_query_check_version (db->queries[i], db_version) == 0))
      continue;

    status = cdbi_read_database_query (db,
        db->queries[i], db->q_prep_areas[i]);
    if (status == 0)
      success++;
  }

  if (success == 0)
  {
    ERROR ("dbi plugin: All queries failed for database `%s'.", db->name);
    return (-1);
  }

  return (0);
} /* }}} int cdbi_read_database */

static int cdbi_read (void) /* {{{ */
{
  size_t i;
  int success = 0;
  int status;

  for (i = 0; i < databases_num; i++)
  {
    status = cdbi_read_database (databases[i]);
    if (status == 0)
      success++;
  }

  if (success == 0)
  {
    ERROR ("dbi plugin: No database could be read. Will return an error so "
        "the plugin will be delayed.");
    return (-1);
  }

  return (0);
} /* }}} int cdbi_read */

static int cdbi_shutdown (void) /* {{{ */
{
  size_t i;

  for (i = 0; i < databases_num; i++)
  {
    if (databases[i]->connection != NULL)
    {
      dbi_conn_close (databases[i]->connection);
      databases[i]->connection = NULL;
    }
    cdbi_database_free (databases[i]);
  }
  sfree (databases);
  databases_num = 0;

  udb_query_free (queries, queries_num);
  queries = NULL;
  queries_num = 0;

  return (0);
} /* }}} int cdbi_shutdown */

void module_register (void) /* {{{ */
{
  plugin_register_complex_config ("dbi", cdbi_config);
  plugin_register_init ("dbi", cdbi_init);
  plugin_register_read ("dbi", cdbi_read);
  plugin_register_shutdown ("dbi", cdbi_shutdown);
} /* }}} void module_register */

/*
 * vim: shiftwidth=2 softtabstop=2 et fdm=marker
 */
