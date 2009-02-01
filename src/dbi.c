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

#include <dbi/dbi.h>

/*
 * Data types
 */
struct cdbi_driver_option_s
{
  char *key;
  char *value;
};
typedef struct cdbi_driver_option_s cdbi_driver_option_t;

struct cdbi_result_s;
typedef struct cdbi_result_s cdbi_result_t;
struct cdbi_result_s
{
  char    *type;
  char   **instances;
  size_t   instances_num;
  char   **values;
  size_t   values_num;

  cdbi_result_t *next;
};

struct cdbi_query_s
{
  char    *name;
  char    *statement;

  cdbi_result_t *results;
};
typedef struct cdbi_query_s cdbi_query_t;

struct cdbi_database_s
{
  char *name;
  char *select_db;

  char *driver;
  cdbi_driver_option_t *driver_options;
  size_t driver_options_num;

  cdbi_query_t **queries;
  size_t      queries_num;

  dbi_conn connection;
};
typedef struct cdbi_database_s cdbi_database_t;

/*
 * Global variables
 */
static cdbi_query_t    **queries       = NULL;
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
    const char *name, int dst_type, value_t *ret_value)
{
  value_t value;
  unsigned int index;
  unsigned short src_type;
  dbi_conn connection;

  index = dbi_result_get_field_idx (res, name);
  if (index < 1)
  {
    ERROR ("dbi plugin: cdbi_result_get: No such column: %s.", name);
    return (-1);
  }

  src_type = dbi_result_get_field_type_idx (res, index);
  if (src_type == DBI_TYPE_ERROR)
  {
    ERROR ("dbi plugin: cdbi_result_get: "
        "dbi_result_get_field_type_idx failed.");
    return (-1);
  }

  if ((dst_type != DS_TYPE_COUNTER) && (dst_type != DS_TYPE_GAUGE))
  {
    ERROR ("dbi plugin: cdbi_result_get: Don't know how to handle "
        "destination type %i.", dst_type);
    return (-1);
  }

  if (src_type == DBI_TYPE_INTEGER)
  {
    if (dst_type == DS_TYPE_COUNTER)
      value.counter = dbi_result_get_ulonglong_idx (res, index);
    else
      value.gauge = (gauge_t) dbi_result_get_longlong_idx (res, index);
  }
  else if (src_type == DBI_TYPE_DECIMAL)
  {
    value.gauge = dbi_result_get_double_idx (res, index);
    if (dst_type == DS_TYPE_COUNTER)
      value.counter = (counter_t) round (value.gauge);
  }
  else if (src_type == DBI_TYPE_STRING)
  {
    const char *string = dbi_result_get_string_idx (res, index);
    char *endptr = NULL;

    if (string == NULL)
      value.gauge = NAN;
    else if (dst_type == DS_TYPE_COUNTER)
      value.counter = (counter_t) strtoll (string, &endptr, 0);
    else
      value.gauge = (gauge_t) strtod (string, &endptr);

    if (string == endptr)
    {
      ERROR ("dbi plugin: cdbi_result_get: Can't parse string as number: %s.",
          string);
      return (-1);
    }
  }
  else
  {
    ERROR ("dbi plugin: cdbi_result_get: Don't know how to handle "
        "source type %hu.", src_type);
    return (-1);
  }

  connection = dbi_result_get_conn (res);
  if (dbi_conn_error (connection, NULL) != 0)
  {
    char errbuf[1024];
    ERROR ("dbi plugin: cdbi_result_get: dbi_result_get_*_idx failed: %s.",
        cdbi_strerror (connection, errbuf, sizeof (errbuf)));
    return (-1);
  }

  *ret_value = value;
  return (0);
} /* }}} int cdbi_result_get_field */

static void cdbi_result_free (cdbi_result_t *r) /* {{{ */
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

  cdbi_result_free (r->next);

  sfree (r);
} /* }}} void cdbi_result_free */

static void cdbi_query_free (cdbi_query_t *q) /* {{{ */
{
  if (q == NULL)
    return;

  sfree (q->name);
  sfree (q->statement);

  cdbi_result_free (q->results);

  sfree (q);
} /* }}} void cdbi_query_free */

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

  sfree (db);
} /* }}} void cdbi_database_free */

static void cdbi_submit (cdbi_database_t *db, cdbi_result_t *r, /* {{{ */
    char **instances, value_t *values)
{
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = (int) r->values_num;
  vl.time = time (NULL);
  sstrncpy (vl.host, hostname_g, sizeof (vl.host));
  sstrncpy (vl.plugin, "dbi", sizeof (vl.plugin));
  sstrncpy (vl.plugin_instance, db->name, sizeof (vl.type_instance));
  sstrncpy (vl.type, r->type, sizeof (vl.type));
  strjoin (vl.type_instance, sizeof (vl.type_instance),
      instances, r->instances_num, "-");
  vl.type_instance[sizeof (vl.type_instance) - 1] = 0;

  plugin_dispatch_values (&vl);
} /* }}} void cdbi_submit */

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

static int cdbi_config_add_string (char ***ret_array, /* {{{ */
    size_t *ret_array_len, oconfig_item_t *ci)
{
  char **array;
  size_t array_len;
  int i;

  if (ci->values_num < 1)
  {
    WARNING ("dbi plugin: The `%s' config option "
        "needs at least one argument.", ci->key);
    return (-1);
  }

  for (i = 0; i < ci->values_num; i++)
  {
    if (ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("dbi plugin: Argument %i to the `%s' option "
          "is not a string.", i + 1, ci->key);
      return (-1);
    }
  }

  array_len = *ret_array_len;
  array = (char **) realloc (*ret_array,
      sizeof (char *) * (array_len + ci->values_num));
  if (array == NULL)
  {
    ERROR ("dbi plugin: realloc failed.");
    return (-1);
  }
  *ret_array = array;

  for (i = 0; i < ci->values_num; i++)
  {
    array[array_len] = strdup (ci->values[i].value.string);
    if (array[array_len] == NULL)
    {
      ERROR ("dbi plugin: strdup failed.");
      *ret_array_len = array_len;
      return (-1);
    }
    array_len++;
  }

  *ret_array_len = array_len;
  return (0);
} /* }}} int cdbi_config_add_string */

static int cdbi_config_add_query_result (cdbi_query_t *q, /* {{{ */
    oconfig_item_t *ci)
{
  cdbi_result_t *r;
  int status;
  int i;

  if (ci->values_num != 0)
  {
    WARNING ("dbi plugin: The `Result' block doesn't accept any arguments. "
        "Ignoring %i argument%s.",
        ci->values_num, (ci->values_num == 1) ? "" : "s");
  }

  r = (cdbi_result_t *) malloc (sizeof (*r));
  if (r == NULL)
  {
    ERROR ("dbi plugin: malloc failed.");
    return (-1);
  }
  memset (r, 0, sizeof (*r));
  r->type = NULL;
  r->instances = NULL;
  r->values = NULL;
  r->next = NULL;

  /* Fill the `cdbi_result_t' structure.. */
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Type", child->key) == 0)
      status = cdbi_config_set_string (&r->type, child);
    else if (strcasecmp ("InstancesFrom", child->key) == 0)
      status = cdbi_config_add_string (&r->instances, &r->instances_num, child);
    else if (strcasecmp ("ValuesFrom", child->key) == 0)
      status = cdbi_config_add_string (&r->values, &r->values_num, child);
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
    if (r->type == NULL)
    {
      WARNING ("dbi plugin: `Type' not given for "
          "result in query `%s'", q->name);
      status = -1;
    }
    if (r->instances == NULL)
    {
      WARNING ("dbi plugin: `InstancesFrom' not given for "
          "result in query `%s'", q->name);
      status = -1;
    }
    if (r->values == NULL)
    {
      WARNING ("dbi plugin: `ValuesFrom' not given for "
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
      cdbi_result_t *last;

      last = q->results;
      while (last->next != NULL)
        last = last->next;

      last->next = r;
    }
  }

  if (status != 0)
  {
    cdbi_result_free (r);
    return (-1);
  }

  return (0);
} /* }}} int cdbi_config_add_query_result */

static int cdbi_config_add_query (oconfig_item_t *ci) /* {{{ */
{
  cdbi_query_t *q;
  int status;
  int i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("dbi plugin: The `Query' block "
        "needs exactly one string argument.");
    return (-1);
  }

  q = (cdbi_query_t *) malloc (sizeof (*q));
  if (q == NULL)
  {
    ERROR ("dbi plugin: malloc failed.");
    return (-1);
  }
  memset (q, 0, sizeof (*q));

  status = cdbi_config_set_string (&q->name, ci);
  if (status != 0)
  {
    sfree (q);
    return (status);
  }

  /* Fill the `cdbi_query_t' structure.. */
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Statement", child->key) == 0)
      status = cdbi_config_set_string (&q->statement, child);
    else if (strcasecmp ("Result", child->key) == 0)
      status = cdbi_config_add_query_result (q, child);
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
    if (q->statement == NULL)
    {
      WARNING ("dbi plugin: `Statement' not given for query `%s'", q->name);
      status = -1;
    }
    if (q->results == NULL)
    {
      WARNING ("dbi plugin: No (valid) `Result' block given for query `%s'",
          q->name);
      status = -1;
    }

    break;
  } /* while (status == 0) */

  /* If all went well, add this query to the list of queries within the
   * database structure. */
  if (status == 0)
  {
    cdbi_query_t **temp;

    temp = (cdbi_query_t **) realloc (queries,
        sizeof (*queries) * (queries_num + 1));
    if (temp == NULL)
    {
      ERROR ("dbi plugin: realloc failed");
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
    cdbi_query_free (q);
    return (-1);
  }

  return (0);
} /* }}} int cdbi_config_add_query */

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

static int cdbi_config_add_database_query (cdbi_database_t *db, /* {{{ */
    oconfig_item_t *ci)
{
  cdbi_query_t *q;
  cdbi_query_t **temp;
  size_t i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("dbi plugin: The `Query' config option "
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
    WARNING ("dbi plugin: Database `%s': Unknown query `%s'. "
        "Please make sure that the <Query \"%s\"> block comes before "
        "the <Database \"%s\"> block.",
        db->name, ci->values[0].value.string,
        ci->values[0].value.string, db->name);
    return (-1);
  }

  temp = (cdbi_query_t **) realloc (db->queries,
      sizeof (*db->queries) * (db->queries_num + 1));
  if (temp == NULL)
  {
    ERROR ("dbi plugin: realloc failed");
    return (-1);
  }
  else
  {
    db->queries = temp;
    db->queries[db->queries_num] = q;
    db->queries_num++;
  }

  return (0);
} /* }}} int cdbi_config_add_database_query */

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
      status = cdbi_config_add_database_query (db, child);
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
      cdbi_config_add_query (child);
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
    cdbi_query_t *q)
{
  dbi_result res;
  int status;

  /* Macro that cleans up dynamically allocated memory and returns the
   * specified status. */
#define BAIL_OUT(status) \
  if (res != NULL) { dbi_result_free (res); res = NULL; } \
  return (status)

  res = dbi_conn_query (db->connection, q->statement);
  if (res == NULL)
  {
    char errbuf[1024];
    ERROR ("dbi plugin: cdbi_read_database_query (%s, %s): "
        "dbi_conn_query failed: %s",
        db->name, q->name,
        cdbi_strerror (db->connection, errbuf, sizeof (errbuf)));
    BAIL_OUT (-1);
  }

  /* 0 = error; 1 = success; */
  status = dbi_result_first_row (res);
  if (status != 1)
  {
    char errbuf[1024];
    ERROR ("dbi plugin: cdbi_read_database_query (%s, %s): "
        "dbi_result_first_row failed: %s. Maybe the statement didn't "
        "return any rows?",
        db->name, q->name, 
        cdbi_strerror (db->connection, errbuf, sizeof (errbuf)));
    BAIL_OUT (-1);
  }

  /* Iterate over all rows and use every result with each row. */
  while (42) /* {{{ */
  {
    cdbi_result_t *r;

    /* Iterate over all results, get the appropriate data_set, allocate memory
     * for the instance(s) and value(s), copy the values and finally call
     * `cdbi_submit' to create and dispatch a value_list. */
    for (r = q->results; r != NULL; r = r->next) /* {{{ */
    {
      const data_set_t *ds;
      char **instances;
      value_t *values;
      size_t i;

      instances = NULL;
      values = NULL;

      /* Macro to clean up dynamically allocated memory and continue with the
       * next iteration of the containing loop, i. e. the `for' loop iterating
       * over all `Result' sets. */
#define BAIL_OUT_CONTINUE \
      if (instances != NULL) { sfree (instances[0]); sfree (instances); } \
      sfree (values); \
      continue

      /* Read `ds' and check number of values {{{ */
      ds = plugin_get_ds (r->type);
      if (ds == NULL)
      {
        ERROR ("dbi plugin: cdbi_read_database_query: Query `%s': Type `%s' is not "
            "known by the daemon. See types.db(5) for details.",
            q->name, r->type);
        BAIL_OUT_CONTINUE;
      }

      if (((size_t) ds->ds_num) != r->values_num)
      {
        ERROR ("dbi plugin: cdbi_read_database_query: Query `%s': The type `%s' "
            "requires exactly %i value%s, but the configuration specifies %zu.",
            q->name, r->type,
            ds->ds_num, (ds->ds_num == 1) ? "" : "s",
            r->values_num);
        BAIL_OUT_CONTINUE;
      }
      /* }}} */

      /* Allocate `instances' and `values' {{{ */
      instances = (char **) malloc (sizeof (*instances) * r->instances_num);
      if (instances == NULL)
      {
        ERROR ("dbi plugin: malloc failed.");
        BAIL_OUT_CONTINUE;
      }

      instances[0] = (char *) malloc (r->instances_num * DATA_MAX_NAME_LEN);
      if (instances[0] == NULL)
      {
        ERROR ("dbi plugin: malloc failed.");
        BAIL_OUT_CONTINUE;
      }
      for (i = 1; i < r->instances_num; i++)
        instances[i] = instances[i - 1] + DATA_MAX_NAME_LEN;

      values = (value_t *) malloc (sizeof (*values) * r->values_num);
      if (values == NULL)
      {
        ERROR ("dbi plugin: malloc failed.");
        BAIL_OUT_CONTINUE;
      }
      /* }}} */

      /* Get instance names and values from the result: */
      for (i = 0; i < r->instances_num; i++) /* {{{ */
      {
        const char *inst;

        inst = dbi_result_get_string (res, r->instances[i]);
        if (dbi_conn_error (db->connection, NULL) != 0)
        {
          char errbuf[1024];
          ERROR ("dbi plugin: cdbi_read_database_query (%s, %s): "
              "dbi_result_get_string (%s) failed: %s",
              db->name, q->name, r->instances[i],
              cdbi_strerror (db->connection, errbuf, sizeof (errbuf)));
          BAIL_OUT_CONTINUE;
        }

        sstrncpy (instances[i], (inst == NULL) ? "" : inst, DATA_MAX_NAME_LEN);
        DEBUG ("dbi plugin: cdbi_read_database_query (%s, %s): "
            "instances[%zu] = %s;",
            db->name, q->name, i, instances[i]);
      } /* }}} for (i = 0; i < q->instances_num; i++) */

      for (i = 0; i < r->values_num; i++) /* {{{ */
      {
        status = cdbi_result_get_field (res, r->values[i], ds->ds[i].type,
            values + i);
        if (status != 0)
        {
          BAIL_OUT_CONTINUE;
        }

        if (ds->ds[i].type == DS_TYPE_COUNTER)
        {
          DEBUG ("dbi plugin: cdbi_read_database_query (%s, %s): values[%zu] = %llu;",
              db->name, q->name, i, values[i].counter);
        }
        else
        {
          DEBUG ("dbi plugin: cdbi_read_database_query (%s, %s): values[%zu] = %g;",
              db->name, q->name, i, values[i].gauge);
        }
      } /* }}} for (i = 0; i < q->values_num; i++) */

      /* Dispatch this row to the daemon. */
      cdbi_submit (db, r, instances, values);

      BAIL_OUT_CONTINUE;
#undef BAIL_OUT_CONTINUE
    } /* }}} for (r = q->results; r != NULL; r = r->next) */

    /* Get the next row from the database. */
    status = dbi_result_next_row (res);
    if (status != 1)
    {
      if (dbi_conn_error (db->connection, NULL) != 0)
      {
        char errbuf[1024];
        WARNING ("dbi plugin: cdbi_read_database_query (%s, %s): "
            "dbi_result_next_row failed: %s.",
            db->name, q->name,
            cdbi_strerror (db->connection, errbuf, sizeof (errbuf)));
      }
      break;
    }
  } /* }}} while (42) */

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

  status = cdbi_connect_database (db);
  if (status != 0)
    return (status);
  assert (db->connection != NULL);

  success = 0;
  for (i = 0; i < db->queries_num; i++)
  {
    status = cdbi_read_database_query (db, db->queries[i]);
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

  for (i = 0; i < queries_num; i++)
    cdbi_query_free (queries[i]);
  sfree (queries);
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
