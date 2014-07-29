/**
 * collectd - src/notify_dbi.c
 * Copyright (C) 2008,2009  Florian octo Forster
 * Copyright (C) 2012 Manuel Sanmartin
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
 *   Manuel Sanmartin <manuel.luis at gmail.com>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"
#include "configfile.h"
#include "utils_subst.h"

#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include <dbi/dbi.h>

/*
 * Data types
 */
struct notify_dbi_driver_option_s /* {{{ */
{
  char *key;
  char *value;
};
typedef struct notify_dbi_driver_option_s notify_dbi_driver_option_t;
/* }}} */

struct notify_dbi_query_s /* {{{ */
{
  int severity;
  char *query;
};
typedef struct notify_dbi_query_s notify_dbi_query_t;
/* }}} */

struct notify_dbi_database_s /* {{{ */
{
  char *name;
  char *select_db;

  char *driver;
  notify_dbi_driver_option_t *driver_options;
  size_t driver_options_num;

  notify_dbi_query_t *queries;
  size_t queries_num;

  dbi_conn connection;

  pthread_mutex_t dbi_lock;
};
typedef struct notify_dbi_database_s notify_dbi_database_t;
/* }}} */

/*
 * Global variables
 */
static notify_dbi_database_t **databases = NULL;
static size_t databases_num = 0;

/*
 * Functions
 */
static const char *notify_dbi_strerror (dbi_conn conn, /* {{{ */
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
} /* }}} const char *notify_dbi_conn_error */

static void notify_dbi_database_free (notify_dbi_database_t *db) /* {{{ */
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

  for (i = 0; i < db->queries_num; i++)
    sfree (db->queries[i].query);
  sfree (db->queries);

  sfree (db);
} /* }}} void notify_dbi_database_free */

/* Configuration handling functions {{{
 *
 * <Plugin notify_dbi>
 *   <Database "mysql">
 *     Driver "mysql"
 *     DriverOption "host" "127.0.0.1"
 *     DriverOption "port" "3306"
 *     DriverOption "username" "collectd"
 *     DriverOption "password" "collectd"
 *     DriverOption "dbname" "collectd"
 *
 *     Query "Failure" "Warning" "Ok" "INSERT INTO alert_history (host, plugin, plugin_instance, type, type_instance, time, severity, message) VALUES ('%{host}', '%{plugin}', '%{plugin_instance}', '%{type}', '%{type_instance}', '%{time}', '%{severity}', '%{message}')"
 *     Query "Failure" "Warning" "DELETE FROM alert where host = '%{host}' and plugin = '%{plugin}' and plugin_instance = '%{plugin_instance}' and type = '%{type}' and type_instance = '%{type_instance}'"
 *     Query "Failure" "Warning" "INSERT INTO alert (host, plugin, plugin_instance, type, type_instance, time, severity, message) VALUES ('%{host}', '%{plugin}', '%{plugin_instance}', '%{type}', '%{type_instance}', '%{time}', '%{severity}', '%{message}')"
 *     Query "Ok" "DELETE FROM alert where host = '%{host}' and plugin = '%{plugin}' and plugin_instance = '%{plugin_instance}' and type = '%{type}' and type_instance = '%{type_instance}'"
 *   </Database>
 * </Plugin>
 *
 */

static int notify_dbi_config_set_string (char **ret_string, /* {{{ */
    oconfig_item_t *ci)
{
  char *string;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("notify_dbi plugin: The `%s' config option "
        "needs exactly one string argument.", ci->key);
    return (-1);
  }

  string = strdup (ci->values[0].value.string);
  if (string == NULL)
  {
    ERROR ("notify_dbi plugin: strdup failed.");
    return (-1);
  }

  if (*ret_string != NULL)
    free (*ret_string);
  *ret_string = string;

  return (0);
} /* }}} int notify_dbi_config_set_string */

static int notify_dbi_config_add_database_driver_option (notify_dbi_database_t *db, /* {{{ */
    oconfig_item_t *ci)
{
  notify_dbi_driver_option_t *option;

  if ((ci->values_num != 2)
      || (ci->values[0].type != OCONFIG_TYPE_STRING)
      || (ci->values[1].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("notify_dbi plugin: The `DriverOption' config option "
        "needs exactly two string arguments.");
    return (-1);
  }

  option = (notify_dbi_driver_option_t *) realloc (db->driver_options,
      sizeof (*option) * (db->driver_options_num + 1));
  if (option == NULL)
  {
    ERROR ("notify_dbi plugin: realloc failed");
    return (-1);
  }

  db->driver_options = option;
  option = db->driver_options + db->driver_options_num;

  option->key = strdup (ci->values[0].value.string);
  if (option->key == NULL)
  {
    ERROR ("notify_dbi plugin: strdup failed.");
    return (-1);
  }

  option->value = strdup (ci->values[1].value.string);
  if (option->value == NULL)
  {
    ERROR ("notify_dbi plugin: strdup failed.");
    sfree (option->key);
    return (-1);
  }

  db->driver_options_num++;
  return (0);
} /* }}} int notify_dbi_config_add_database_driver_option */

int notify_dbi_config_add_database_query (notify_dbi_database_t *db, /* {{{ */
    oconfig_item_t *ci)
{
  size_t i;
  int severity;
  char *query;
  notify_dbi_query_t  *tmp_list;

  if (ci->values_num < 2)
  {
    WARNING ("notify_dbi plugin: The `Query' config option "
        "needs two or more string arguments.");
    return (-1);
  }

  severity = 0;
  query = NULL;

  for (i = 0; i < ci->values_num; i++)
  {
    if (ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      WARNING ("notify_dbi plugin: The arguments of `Query' config option "
          "must be strings.");
      return (-1);
    }

    if (i < (ci->values_num-1))
    {
      if (!strcasecmp(ci->values[i].value.string, "failure"))
      {
        severity |= NOTIF_FAILURE;
      }
      else if (!strcasecmp(ci->values[i].value.string, "warning"))
      {
        severity |= NOTIF_WARNING;
      }
      else if (!strcasecmp(ci->values[i].value.string, "ok"))
      {
        severity |= NOTIF_OKAY;
      }
      else
      {
        WARNING ("notify_dbi plugin: Unknow severity in `Query' config option: "
          "`%s'", ci->values[i].value.string);
        return (-1);
      }
    }
    else
    {
      query = strdup(ci->values[i].value.string);
    }
  }

  tmp_list = (notify_dbi_query_t *) realloc(db->queries, (db->queries_num+1)
    * sizeof(notify_dbi_query_t));
  if (tmp_list == NULL)
  {
    sfree(query);
    ERROR ("notify_dbi plugin: realloc failed.");
    return (-ENOMEM);
  }

  tmp_list[db->queries_num].query = query;
  tmp_list[db->queries_num].severity = severity;
  db->queries = tmp_list;
  db->queries_num++;

  return (0);
} /* }}} int notify_dbi_config_add_database_query */

static int notify_dbi_config_add_database (oconfig_item_t *ci) /* {{{ */
{
  notify_dbi_database_t *db;
  int status;
  int i;

  if ((ci->values_num != 1)
      || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("notify_dbi plugin: The `Database' block "
        "needs exactly one string argument.");
    return (-1);
  }

  db = (notify_dbi_database_t *) malloc (sizeof (*db));
  if (db == NULL)
  {
    ERROR ("notify_dbi plugin: malloc failed.");
    return (-1);
  }
  memset (db, 0, sizeof (*db));

  pthread_mutex_init (&db->dbi_lock, /* attr = */ NULL);

  status = notify_dbi_config_set_string (&db->name, ci);
  if (status != 0)
  {
    sfree (db);
    return (status);
  }

  /* Fill the `notify_dbi_database_t' structure.. */
  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Driver", child->key) == 0)
      status = notify_dbi_config_set_string (&db->driver, child);
    else if (strcasecmp ("DriverOption", child->key) == 0)
      status = notify_dbi_config_add_database_driver_option (db, child);
    else if (strcasecmp ("SelectDB", child->key) == 0)
      status = notify_dbi_config_set_string (&db->select_db, child);
    else if (strcasecmp ("Query", child->key) == 0)
      status = notify_dbi_config_add_database_query (db, child);
    else
    {
      WARNING ("notify_dbi plugin: Option `%s' not allowed here.", child->key);
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
      WARNING ("notify_dbi plugin: `Driver' not given for database `%s'", db->name);
      status = -1;
    }
    if (db->driver_options_num == 0)
    {
      WARNING ("notify_dbi plugin: No `DriverOption' given for database `%s'. "
          "This will likely not work.", db->name);
    }

    break;
  } /* while (status == 0) */

  /* If all went well, add this database to the global list of databases. */
  if (status == 0)
  {
    notify_dbi_database_t **temp;

    temp = (notify_dbi_database_t **) realloc (databases,
        sizeof (*databases) * (databases_num + 1));
    if (temp == NULL)
    {
      ERROR ("notify_dbi plugin: realloc failed");
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
    notify_dbi_database_free (db);
    return (-1);
  }

  return (0);
} /* }}} int notify_dbi_config_add_database */

static int notify_dbi_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp ("Database", child->key) == 0)
      notify_dbi_config_add_database (child);
    else
    {
      WARNING ("notify_dbi plugin: Ignoring unknown config option `%s'.", child->key);
    }
  } /* for (ci->children) */

  return (0);
} /* }}} int notify_dbi_config */

/* }}} End of configuration handling functions */

static int notify_dbi_init (void) /* {{{ */
{
  static int did_init = 0;
  int status;

  if (did_init != 0)
    return (0);

  if (databases_num == 0)
  {
    ERROR ("notify_dbi plugin: No <Database> blocks have been found. Without them, "
        "this plugin can't do anything useful, so we will returns an error.");
    return (-1);
  }

  status = dbi_initialize (NULL);
  if (status < 0)
  {
    ERROR ("notify_dbi plugin: notify_dbi_init: dbi_initialize failed with status %i.",
        status);
    return (-1);
  }
  else if (status == 0)
  {
    ERROR ("notify_dbi plugin: `dbi_initialize' could not load any drivers. Please "
        "install at least one `DBD' or check your installation.");
    return (-1);
  }
  DEBUG ("notify_dbi plugin: notify_dbi_init: dbi_initialize reports %i driver%s.",
      status, (status == 1) ? "" : "s");

  return (0);
} /* }}} int notify_dbi_init */

static notification_meta_t *notify_dbi_notification_meta_get ( /* {{{ */
  notification_meta_t *meta, char *name)
{
  while (meta != NULL) {
    if (strcmp(meta->name, name) == 0)
      return meta;
    meta = meta->next;
  }
  return NULL;
} /* }}} notification_meta_t *notify_dbi_notification_meta_get */

static int notify_dbi_notification_database_query (notify_dbi_database_t *db, /* {{{ */
    const char *statement, const notification_t *n)
{
  dbi_result res;
  int status;
  char query[4096];
  char temp[4096];
  char time_buffer[12];
  char severity_buffer[8];
  char data_buffer[64];
  char value_buffer[32];
  notification_meta_t *meta;

  res = NULL;
  assert (statement != NULL);

  status = ssnprintf (time_buffer, sizeof(time_buffer), "%u",
    (unsigned int)CDTIME_T_TO_TIME_T(n->time));
  if ((status < 1) || (status >= sizeof(time_buffer)))
    return (-1);

  if (n->severity & NOTIF_FAILURE)
    sstrncpy(severity_buffer, "FAILURE", sizeof(severity_buffer));
  else if (n->severity & NOTIF_WARNING)
    sstrncpy(severity_buffer, "WARNING", sizeof(severity_buffer));
  else if (n->severity & NOTIF_OKAY)
    sstrncpy(severity_buffer, "OKAY", sizeof(severity_buffer));

  meta = notify_dbi_notification_meta_get(n->meta, "DataSource");
  if (meta != NULL && meta->type == NM_TYPE_STRING)
    sstrncpy(data_buffer, (char *)meta->nm_value.nm_string, sizeof(data_buffer));
  else
    sstrncpy(data_buffer, "NULL", sizeof(data_buffer));

  meta = notify_dbi_notification_meta_get(n->meta, "CurrentValue");
  if (meta != NULL && meta->type == NM_TYPE_DOUBLE)
  {
    status = ssnprintf(value_buffer, sizeof(value_buffer), "%f",
      meta->nm_value.nm_double);
    if ((status < 0) || ((size_t) status >= sizeof (value_buffer)))
      WARNING ("notify_dbi plugin: truncate notification CurrentValue.");
  }
  else
    sstrncpy(value_buffer, "NULL", sizeof(value_buffer));

  sstrncpy(query, statement, sizeof(query));

#define REPLACE_FIELD(t,v) \
  if (subst_string (temp, sizeof (temp), query, t, v) != NULL) \
    sstrncpy (query, temp, sizeof (query));

  REPLACE_FIELD ("%{severity}", severity_buffer);
  REPLACE_FIELD ("%{time}", time_buffer);
  REPLACE_FIELD ("%{host}", n->host);
  REPLACE_FIELD ("%{plugin}", n->plugin);
  REPLACE_FIELD ("%{plugin_instance}", n->plugin_instance);
  REPLACE_FIELD ("%{type}", n->type);
  REPLACE_FIELD ("%{type_instance}", n->type_instance);
  REPLACE_FIELD ("%{message}", n->message);
  REPLACE_FIELD ("%{data_source}", data_buffer);
  REPLACE_FIELD ("%{value}", value_buffer);
#undef REPLACE_FIELD

  res = dbi_conn_query (db->connection, query);
  if (res == NULL)
  {
    char errbuf[1024];
    ERROR ("notify_dbi plugin: notify_dbi_notification_database_query (%s, %s): "
        "dbi_conn_query failed: %s",
        db->name, query,
        notify_dbi_strerror (db->connection, errbuf, sizeof (errbuf)));
    return (-1);
  }

  /* Clean up and return `status = 0' (success) */
  if (res != NULL)
  {
    dbi_result_free (res);
    res = NULL;
  }
  return (0);
} /* }}} int notify_dbi_read_database_query */

static int notify_dbi_connect_database (notify_dbi_database_t *db) /* {{{ */
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
    ERROR ("notify_dbi plugin: notify_dbi_connect_database: dbi_driver_open (%s) failed.",
        db->driver);
    INFO ("notify_dbi plugin: Maybe the driver isn't installed? "
        "Known drivers are:");
    for (driver = dbi_driver_list (NULL);
        driver != NULL;
        driver = dbi_driver_list (driver))
    {
      INFO ("notify_dbi plugin: * %s", dbi_driver_get_name (driver));
    }
    return (-1);
  }

  connection = dbi_conn_open (driver);
  if (connection == NULL)
  {
    ERROR ("notify_dbi plugin: notify_dbi_connect_database: dbi_conn_open (%s) failed.",
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
    DEBUG ("notify_dbi plugin: notify_dbi_connect_database (%s): "
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

      ERROR ("notify_dbi plugin: notify_dbi_connect_database (%s): "
          "dbi_conn_set_option (%s, %s) failed: %s.",
          db->name,
          db->driver_options[i].key, db->driver_options[i].value,
          notify_dbi_strerror (connection, errbuf, sizeof (errbuf)));

      INFO ("notify_dbi plugin: This is a list of all options understood "
          "by the `%s' driver:", db->driver);
      for (opt = dbi_conn_get_option_list (connection, NULL);
          opt != NULL;
          opt = dbi_conn_get_option_list (connection, opt))
      {
        INFO ("notify_dbi plugin: * %s", opt);
      }

      dbi_conn_close (connection);
      return (-1);
    }
  } /* for (i = 0; i < db->driver_options_num; i++) */

  status = dbi_conn_connect (connection);
  if (status != 0)
  {
    char errbuf[1024];
    ERROR ("notify_dbi plugin: notify_dbi_connect_database (%s): "
        "dbi_conn_connect failed: %s",
        db->name, notify_dbi_strerror (connection, errbuf, sizeof (errbuf)));
    dbi_conn_close (connection);
    return (-1);
  }

  if (db->select_db != NULL)
  {
    status = dbi_conn_select_db (connection, db->select_db);
    if (status != 0)
    {
      char errbuf[1024];
      WARNING ("notify_dbi plugin: notify_dbi_connect_database (%s): "
          "dbi_conn_select_db (%s) failed: %s. Check the `SelectDB' option.",
          db->name, db->select_db,
          notify_dbi_strerror (connection, errbuf, sizeof (errbuf)));
      dbi_conn_close (connection);
      return (-1);
    }
  }

  db->connection = connection;
  return (0);
} /* }}} int notify_dbi_connect_database */

static int notify_dbi_notification_database (notify_dbi_database_t *db, /* {{{ */
    const notification_t *n)
{
  size_t i;
  int success;
  int status;

  status = notify_dbi_connect_database (db);
  if (status != 0)
    return (status);
  assert (db->connection != NULL);

  success = 0;
  for (i = 0; i < db->queries_num; i++)
  {
    if (db->queries[i].severity & n->severity)
    {
      status = notify_dbi_notification_database_query (db,
          db->queries[i].query, n);
      if (status == 0)
        success++;
    }
  }

  if (success == 0)
  {
    ERROR ("notify_dbi plugin: All queries failed for database `%s'.", db->name);
    return (-1);
  }

  return (0);
} /* }}} int notify_dbi_notification_database */

static int notify_dbi_notification (const notification_t *n, /* {{{ */
    user_data_t __attribute__((unused)) *user_data)
{
  size_t i;
  int success = 0;
  int status;

  for (i = 0; i < databases_num; i++)
  {
    pthread_mutex_lock (&databases[i]->dbi_lock);
    status = notify_dbi_notification_database (databases[i], n);
    pthread_mutex_unlock (&databases[i]->dbi_lock);
    if (status == 0)
      success++;
  }

  if (success == 0)
  {
    ERROR ("notify_dbi plugin: No database could be read. Will return an error so "
        "the plugin will be delayed.");
    return (-1);
  }

  return (0);
} /* }}} int notify_dbi_notification */

static int notify_dbi_shutdown (void) /* {{{ */
{
  size_t i;

  for (i = 0; i < databases_num; i++)
  {
    pthread_mutex_lock (&databases[i]->dbi_lock);
    if (databases[i]->connection != NULL)
    {
      dbi_conn_close (databases[i]->connection);
      databases[i]->connection = NULL;
    }
    notify_dbi_database_free (databases[i]);
    pthread_mutex_unlock (&databases[i]->dbi_lock);
  }
  sfree (databases);
  databases_num = 0;

  return (0);
} /* }}} int notify_dbi_shutdown */

void module_register (void) /* {{{ */
{
  plugin_register_complex_config ("notify_dbi", notify_dbi_config);
  plugin_register_init ("notify_dbi", notify_dbi_init);
  plugin_register_notification ("notify_dbi", notify_dbi_notification,
    /* user_data = */ NULL);
  plugin_register_shutdown ("notify_dbi", notify_dbi_shutdown);
} /* }}} void module_register */

/*
 * vim: shiftwidth=2 softtabstop=2 et fdm=marker
 */
