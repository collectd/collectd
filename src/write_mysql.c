/**
 * collectd - src/write_mysql.c
 * Copyright (C) 2011       Cyril Feraudet
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
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
 *   Cyril Feraudet <cyril at feraudet.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"
#ifdef HAVE_MYSQL_H
#include <mysql.h>
#elif defined(HAVE_MYSQL_MYSQL_H)
#include <mysql/mysql.h>
#endif
#include <pthread.h>
#include "utils_cache.h"
#include "utils_parse_option.h"
#include "utils_avltree.c"
#include <time.h>

static c_avl_tree_t *host_tree, *plugin_tree, *type_tree, *dataset_tree =
  NULL;

typedef struct dataset_s dataset_t;
struct dataset_s
{
  char name[DATA_MAX_NAME_LEN];
  int id;
  int type_id;
};

static const char *config_keys[] = {
  "Host",
  "User",
  "Passwd",
  "Database",
  "Port",
  "Replace"
};

static int config_keys_num = STATIC_ARRAY_SIZE (config_keys);

static char *host = "localhost";
static char *user = "root";
static char *passwd = "";
static char *database = "collectd";
static int  port = 0;
static int  replace = 1;

#define HOST_ITEM   0
#define PLUGIN_ITEM 1
#define TYPE_ITEM   2

static MYSQL *conn;
static MYSQL_BIND data_bind[8], notif_bind[8];
static MYSQL_STMT *data_stmt, *notif_stmt;

static pthread_mutex_t mutexdb;
static pthread_mutex_t mutexhost_tree, mutexplugin_tree, mutextype_tree,
  mutexdataset_tree;

static char data_query[1024];
static char notif_query[1024] =
  "INSERT INTO notification  (date,host_id,plugin_id,"
  "plugin_instance,type_id,type_instance,severity,message) VALUES "
  "(?,?,?,?,?,?,?,?)";

static int
write_mysql_config (const char *key, const char *value)
{
  if (strcasecmp ("Host", key) == 0)
    {
      host = strdup (value);
    }
  else if (strcasecmp ("User", key) == 0)
    {
      user = strdup (value);
    }
  else if (strcasecmp ("Passwd", key) == 0)
    {
      passwd = strdup (value);
    }
  else if (strcasecmp ("Database", key) == 0)
    {
      database = strdup (value);
    }
  else if (strcasecmp ("Port", key) == 0)
    {
      port = service_name_to_port_number (value);
    }
  else if (strcasecmp ("Replace", key) == 0)
    {
      if (IS_TRUE (value))
        {
          replace = 1;
        }
      else
        {
          replace = 0;
        }
    }
}

static int
write_mysql_init (void)
{
  my_bool my_true = 1;
  conn = mysql_init (NULL);
  if (!mysql_thread_safe ())
    {
      ERROR ("write_mysql plugin: mysqlclient Thread Safe OFF");
      return (-1);
    }
  else
    {
      DEBUG ("write_mysql plugin: mysqlclient Thread Safe ON");
    }
  if (mysql_real_connect (conn, host, user, passwd, database, port, NULL, 0)
      == NULL)
    {
      ERROR ("write_mysql plugin: Failed to connect to database %s "
	     " at server %s with user %s : %s", database, host, user,
	     mysql_error (conn));
    }
  char tmpquery[1024] = "%s INTO data "
    "(date,host_id,plugin_id,plugin_instance,type_id,type_instance,dataset_id,value)"
    "VALUES (?,?,?,?,?,?,?,?)";
  ssnprintf (data_query, sizeof (tmpquery), tmpquery, (replace == 1 ? "REPLACE" : "INSERT"));
  mysql_options (conn, MYSQL_OPT_RECONNECT, &my_true);
  data_stmt = mysql_stmt_init (conn);
  notif_stmt = mysql_stmt_init (conn);
  mysql_stmt_prepare (data_stmt, data_query, strlen (data_query));
  mysql_stmt_prepare (notif_stmt, notif_query, strlen (notif_query));
  host_tree = c_avl_create ((void *) strcmp);
  plugin_tree = c_avl_create ((void *) strcmp);
  type_tree = c_avl_create ((void *) strcmp);
  dataset_tree = c_avl_create ((void *) strcmp);
  return (0);
}


static int
add_item_id (const char *name, const int item)
{
  int *id = malloc (sizeof (int));
  char query[1024];
  pthread_mutex_t *mutex;
  c_avl_tree_t *tree;
  MYSQL_BIND param_bind[1], result_bind[1];
  MYSQL_STMT *stmt;
  ssnprintf (query, sizeof (query), "SELECT id FROM %s WHERE name = ?",
	     item == HOST_ITEM ? "host" : item ==
	     PLUGIN_ITEM ? "plugin" : "type");
  DEBUG ("write_mysql plugin: %s", query);
  memset (param_bind, '\0', sizeof (MYSQL_BIND));
  memset (result_bind, '\0', sizeof (MYSQL_BIND));
  param_bind[0].buffer_type = MYSQL_TYPE_STRING;
  param_bind[0].buffer = (char *) name;
  param_bind[0].buffer_length = strlen (name);
  result_bind[0].buffer_type = MYSQL_TYPE_LONG;
  result_bind[0].buffer = (void *) id;
  pthread_mutex_lock (&mutexdb);
  if (mysql_ping (conn) != 0)
    {
      ERROR
	("write_mysql plugin: add_item_id - Failed to re-connect to database : %s",
	 mysql_error (conn));
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  stmt = mysql_stmt_init (conn);
  if (mysql_stmt_prepare (stmt, query, strlen (query)) != 0)
    {
      ERROR
	("write_mysql plugin: add_item_id - Failed to prepare statement : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_bind_param (stmt, param_bind) != 0)
    {
      ERROR
	("write_mysql plugin: add_item_id - Failed to bind param to statement : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_bind_result (stmt, result_bind) != 0)
    {
      ERROR
	("write_mysql plugin: add_item_id - Failed to bind result to statement : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_execute (stmt) != 0)
    {
      ERROR
	("write_mysql plugin: add_item_id - Failed to execute re-prepared statement : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_store_result (stmt) != 0)
    {
      ERROR
	("write_mysql plugin: add_item_id - Failed to store result : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_fetch (stmt) == 0)
    {
      mysql_stmt_free_result (stmt);
      mysql_stmt_close (stmt);
      pthread_mutex_unlock (&mutexdb);
      DEBUG ("get %s_id from DB : %d (%s)",
	     item == HOST_ITEM ? "host" : item ==
	     PLUGIN_ITEM ? "plugin" : "type", *id, name);
    }
  else
    {
      mysql_stmt_free_result (stmt);
      mysql_stmt_close (stmt);
      stmt = mysql_stmt_init (conn);
      ssnprintf (query, sizeof (query), "INSERT INTO %s (name) VALUES (?)",
		 item == HOST_ITEM ? "host" : item ==
		 PLUGIN_ITEM ? "plugin" : "type");
      if (mysql_stmt_prepare (stmt, query, strlen (query)) != 0)
	{
	  ERROR
	    ("write_mysql plugin: add_item_id - Failed to prepare statement : %s / %s",
	     mysql_stmt_error (stmt), query);
	  pthread_mutex_unlock (&mutexdb);
	  return -1;
	}
      if (mysql_stmt_bind_param (stmt, param_bind) != 0)
	{
	  ERROR
	    ("write_mysql plugin: add_item_id - Failed to bind param to statement : %s / %s",
	     mysql_stmt_error (stmt), query);
	  pthread_mutex_unlock (&mutexdb);
	  return -1;
	}
      if (mysql_stmt_execute (stmt) != 0)
	{
	  ERROR
	    ("write_mysql plugin: add_item_id - Failed to execute re-prepared statement : %s / %s",
	     mysql_stmt_error (stmt), query);
	  pthread_mutex_unlock (&mutexdb);
	  return -1;
	}
      *id = mysql_stmt_insert_id (stmt);
      mysql_stmt_close (stmt);
      pthread_mutex_unlock (&mutexdb);
      DEBUG ("insert %s_id in DB : %d (%s)",
	     item == HOST_ITEM ? "host" : item ==
	     PLUGIN_ITEM ? "plugin" : "type", *id, name);
    }
  switch (item)
    {
    case HOST_ITEM:
      mutex = &mutexhost_tree;
      tree = host_tree;
      break;
    case PLUGIN_ITEM:
      mutex = &mutexplugin_tree;
      tree = plugin_tree;
      break;
    case TYPE_ITEM:
      mutex = &mutextype_tree;
      tree = type_tree;
      break;
    }
  pthread_mutex_lock (mutex);
  c_avl_insert (tree, strdup (name), (void *) id);
  pthread_mutex_unlock (mutex);
  return *id;
}

static int
add_dataset_id (data_source_t * ds, int type_id)
{
  int *id = malloc (sizeof (int));
  char tree_key[DATA_MAX_NAME_LEN * 2];
  dataset_t *newdataset;
  char *type;
  switch (ds->type)
    {
    case DS_TYPE_COUNTER:
      type = "COUNTER";
      break;
    case DS_TYPE_DERIVE:
      type = "DERIVE";
      break;
    case DS_TYPE_ABSOLUTE:
      type = "ABSOLUTE";
      break;
    default:
      type = "GAUGE";
      break;
    }
  char *query = "SELECT id FROM dataset WHERE name = ? AND type_id = ?";
  MYSQL_BIND param_bind[2], result_bind[1], param_bind2[5];
  MYSQL_STMT *stmt;
  memset (param_bind, '\0', sizeof (MYSQL_BIND) * 2);
  memset (param_bind2, '\0', sizeof (MYSQL_BIND) * 5);
  memset (result_bind, '\0', sizeof (MYSQL_BIND));
  param_bind[0].buffer_type = MYSQL_TYPE_STRING;
  param_bind[0].buffer = (char *) ds->name;
  param_bind[0].buffer_length = strlen (ds->name);
  param_bind[1].buffer_type = MYSQL_TYPE_LONG;
  param_bind[1].buffer = (void *) &type_id;
  param_bind2[0].buffer_type = MYSQL_TYPE_STRING;
  param_bind2[0].buffer = (char *) ds->name;
  param_bind2[0].buffer_length = strlen (ds->name);
  param_bind2[1].buffer_type = MYSQL_TYPE_LONG;
  param_bind2[1].buffer = (void *) &type_id;
  param_bind2[2].buffer_type = MYSQL_TYPE_STRING;
  param_bind2[2].buffer = (char *) &type;
  param_bind2[2].buffer_length = strlen (type);
  param_bind2[3].buffer_type = MYSQL_TYPE_DOUBLE;
  param_bind2[3].buffer = (void *) &ds->min;
  param_bind2[4].buffer_type = MYSQL_TYPE_DOUBLE;
  param_bind2[4].buffer = (void *) &ds->max;
  result_bind[0].buffer_type = MYSQL_TYPE_LONG;
  result_bind[0].buffer = (void *) id;
  pthread_mutex_lock (&mutexdb);
  if (mysql_ping (conn) != 0)
    {
      ERROR
	("write_mysql plugin: add_dataset_id - Failed to re-connect to database : %s",
	 mysql_error (conn));
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  stmt = mysql_stmt_init (conn);
  if (mysql_stmt_prepare (stmt, query, strlen (query)) != 0)
    {
      ERROR
	("write_mysql plugin: add_dataset_id - Failed to prepare statement : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_bind_param (stmt, param_bind) != 0)
    {
      ERROR
	("write_mysql plugin: add_dataset_id - Failed to bind param to statement : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_bind_result (stmt, result_bind) != 0)
    {
      ERROR
	("write_mysql plugin: add_dataset_id - Failed to bind result to statement : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_execute (stmt) != 0)
    {
      ERROR
	("write_mysql plugin: add_dataset_id - Failed to execute re-prepared statement : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_store_result (stmt) != 0)
    {
      ERROR
	("write_mysql plugin: add_dataset_id - Failed to store result : %s / %s",
	 mysql_stmt_error (stmt), query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_fetch (stmt) == 0)
    {
      mysql_stmt_free_result (stmt);
      mysql_stmt_close (stmt);
      pthread_mutex_unlock (&mutexdb);
      DEBUG ("get dataset_id from DB : %d (%s) (%d)", *id, ds->name, type_id);
    }
  else
    {
      mysql_stmt_free_result (stmt);
      mysql_stmt_close (stmt);
      stmt = mysql_stmt_init (conn);
      char *queryins =
	"INSERT INTO dataset (name,type_id,type,min,max) VALUES (?,?,?,?,?)";
      if (mysql_stmt_prepare (stmt, queryins, strlen (queryins)) != 0)
	{
	  ERROR
	    ("write_mysql plugin: add_dataset_id - Failed to prepare statement : %s / %s",
	     mysql_stmt_error (stmt), queryins);
	  pthread_mutex_unlock (&mutexdb);
	  return -1;
	}
      if (mysql_stmt_bind_param (stmt, param_bind2) != 0)
	{
	  ERROR
	    ("write_mysql plugin: add_dataset_id - Failed to bind param to statement : %s / %s",
	     mysql_stmt_error (stmt), queryins);
	  pthread_mutex_unlock (&mutexdb);
	  return -1;
	}
      if (mysql_stmt_execute (stmt) != 0)
	{
	  ERROR
	    ("write_mysql plugin: add_dataset_id - Failed to execute re-prepared statement : %s / %s",
	     mysql_stmt_error (stmt), queryins);
	  pthread_mutex_unlock (&mutexdb);
	  return -1;
	}
      *id = mysql_stmt_insert_id (stmt);
      mysql_stmt_close (stmt);
      pthread_mutex_unlock (&mutexdb);
      DEBUG ("insert dataset_id in DB : %d (%s) (%d)", *id, ds->name,
	     type_id);
    }
  pthread_mutex_unlock (&mutexdb);
  ssnprintf (tree_key, sizeof (tree_key), "%s_%d", ds->name, type_id);
  newdataset = malloc (sizeof (dataset_t));
  sstrncpy (newdataset->name, ds->name, sizeof (newdataset->name));
  newdataset->id = *id;
  newdataset->type_id = type_id;
  pthread_mutex_lock (&mutexdataset_tree);
  c_avl_insert (dataset_tree, strdup (tree_key), newdataset);
  pthread_mutex_unlock (&mutexdataset_tree);
  sfree (id);
  return newdataset->id;

}

static int
get_item_id (const char *name, const int item)
{
  int *id;
  pthread_mutex_t *mutex;
  c_avl_tree_t *tree;
  switch (item)
    {
    case HOST_ITEM:
      mutex = &mutexhost_tree;
      tree = host_tree;
      break;
    case PLUGIN_ITEM:
      mutex = &mutexplugin_tree;
      tree = plugin_tree;
      break;
    case TYPE_ITEM:
      mutex = &mutextype_tree;
      tree = type_tree;
      break;
    }
  if (strlen (name) == 0)
    {
      return -1;
    }
  pthread_mutex_lock (mutex);
  if (c_avl_get (tree, name, (void *) &id) == 0)
    {
      pthread_mutex_unlock (mutex);
      DEBUG ("get_item_id : get %s_id for %s from cache",
	     item == HOST_ITEM ? "host" : item ==
	     PLUGIN_ITEM ? "plugin" : "type", name);
      return *id;
    }
  else
    {
      pthread_mutex_unlock (mutex);
      DEBUG ("get_item_id : insert %s_id for %s into cache",
	     item == HOST_ITEM ? "host" : item ==
	     PLUGIN_ITEM ? "plugin" : "type", name);
      return add_item_id (name, item);
    }
}

static int
get_dataset_id (data_source_t * ds, int type_id)
{
  char tree_key[DATA_MAX_NAME_LEN * 2];
  dataset_t *newdataset;
  ssnprintf (tree_key, sizeof (tree_key), "%s_%d", ds->name, type_id);
  pthread_mutex_lock (&mutexdataset_tree);
  if (c_avl_get (dataset_tree, tree_key, (void *) &newdataset) == 0)
    {
      pthread_mutex_unlock (&mutexdataset_tree);
      DEBUG ("dataset_id from cache : %d | %s", newdataset->id, tree_key);
      return newdataset->id;
    }
  else
    {
      pthread_mutex_unlock (&mutexdataset_tree);
      return add_dataset_id (ds, type_id);
    }
}

static int
write_mysql_write (const data_set_t * ds, const value_list_t * vl,
		   user_data_t __attribute__ ((unused)) * user_data)
{
  int i;
  int host_id, plugin_id, type_id, aa;
  host_id = get_item_id ((char *) vl->host, HOST_ITEM);
  plugin_id = get_item_id ((char *) vl->plugin, PLUGIN_ITEM);
  type_id = get_item_id ((char *) vl->type, TYPE_ITEM);
  if (host_id == -1 || plugin_id == -1 || type_id == -1)
    {
      return -1;
    }
  gauge_t *rates = NULL;
  for (i = 0; i < ds->ds_num; i++)
    {
      int len;
      data_source_t *dso = ds->ds + i;
      int dataset_id = get_dataset_id (dso, type_id);
      if (dataset_id == -1)
	{
	  return -1;
	}
      MYSQL_TIME mysql_date;
      struct tm *time;
      time_t timet;
      timet = CDTIME_T_TO_TIME_T (vl->time);
      time = localtime (&timet);
      mysql_date.year = time->tm_year + 1900;
      mysql_date.month = time->tm_mon + 1;
      mysql_date.day = time->tm_mday;
      mysql_date.hour = time->tm_hour;
      mysql_date.minute = time->tm_min;
      mysql_date.second = time->tm_sec;
      pthread_mutex_lock (&mutexdb);
      memset (data_bind, '\0', sizeof (MYSQL_BIND) * 8);
      data_bind[0].buffer_type = MYSQL_TYPE_DATETIME;
      data_bind[0].buffer = (char *) &mysql_date;
      data_bind[1].buffer_type = MYSQL_TYPE_LONG;
      data_bind[1].buffer = (void *) &host_id;
      data_bind[2].buffer_type = MYSQL_TYPE_LONG;
      data_bind[2].buffer = (void *) &plugin_id;
      data_bind[3].buffer_type = MYSQL_TYPE_STRING;
      data_bind[3].buffer = (void *) vl->plugin_instance;
      data_bind[3].buffer_length = strlen (vl->plugin_instance);
      data_bind[4].buffer_type = MYSQL_TYPE_LONG;
      data_bind[4].buffer = (void *) &type_id;
      data_bind[5].buffer_type = MYSQL_TYPE_STRING;
      data_bind[5].buffer = (void *) vl->type_instance;
      data_bind[5].buffer_length = strlen (vl->type_instance);
      data_bind[6].buffer_type = MYSQL_TYPE_LONG;
      data_bind[6].buffer = (void *) &dataset_id;
      if (dso->type == DS_TYPE_GAUGE)
	{
	  data_bind[7].buffer_type = MYSQL_TYPE_DOUBLE;
	  data_bind[7].buffer = (void *) &(vl->values[i].gauge);

	}
      else
	{
	  if (rates == NULL)
	    {
	      rates = uc_get_rate (ds, vl);
	    }
	  if (rates == NULL || isnan (rates[i]))
	    {
	      pthread_mutex_unlock (&mutexdb);
	      sfree (rates);
	      continue;
	    }
	  data_bind[7].buffer_type = MYSQL_TYPE_DOUBLE;
	  data_bind[7].buffer = (void *) &(rates[i]);
	}
      if (mysql_ping (conn) != 0)
	{
	  ERROR
	    ("write_mysql plugin: write_mysql_write - Failed to re-connect to database : %s",
	     mysql_error (conn));
	  pthread_mutex_unlock (&mutexdb);
	  sfree (rates);
	  return -1;
	}
      if (mysql_stmt_bind_param (data_stmt, data_bind) != 0)
	{
	  ERROR
	    ("write_mysql plugin: Failed to bind param to statement : %s / %s",
	     mysql_stmt_error (data_stmt), data_query);
	  pthread_mutex_unlock (&mutexdb);
	  sfree (rates);
	  return -1;
	}
      if (mysql_stmt_execute (data_stmt) != 0)
	{
	  // Try to re-prepare statement
	  data_stmt = mysql_stmt_init (conn);
	  mysql_stmt_prepare (data_stmt, data_query, strlen (data_query));
	  mysql_stmt_bind_param (data_stmt, data_bind);
	  if (mysql_stmt_execute (data_stmt) != 0)
	    {
	      ERROR
		("write_mysql plugin: Failed to execute re-prepared statement : %s / %s",
		 mysql_stmt_error (data_stmt), data_query);
	      pthread_mutex_unlock (&mutexdb);
	      sfree (rates);
	      return -1;
	    }
	}
      sfree (rates);
      pthread_mutex_unlock (&mutexdb);
    }
  return (0);
}

static int
notify_write_mysql (const notification_t * n,
		    user_data_t __attribute__ ((unused)) * user_data)
{

  int host_id, plugin_id, type_id, len;
  char severity[32];
  host_id = get_item_id (n->host, HOST_ITEM);
  plugin_id = get_item_id (n->plugin, PLUGIN_ITEM);
  type_id = get_item_id (n->type, TYPE_ITEM);
  MYSQL_TIME mysql_date;
  struct tm *time;
  time_t timet;
  timet = CDTIME_T_TO_TIME_T (n->time);
  time = localtime (&timet);
  mysql_date.year = time->tm_year + 1900;
  mysql_date.month = time->tm_mon + 1;
  mysql_date.day = time->tm_mday;
  mysql_date.hour = time->tm_hour;
  mysql_date.minute = time->tm_min;
  mysql_date.second = time->tm_sec;
  pthread_mutex_lock (&mutexdb);
  memset (notif_bind, '\0', sizeof (MYSQL_BIND) * 8);
  notif_bind[0].buffer_type = MYSQL_TYPE_DATETIME;
  notif_bind[0].buffer = (char *) &mysql_date;
  notif_bind[0].is_null = 0;
  notif_bind[0].length = 0;
  notif_bind[1].buffer_type = MYSQL_TYPE_LONG;
  notif_bind[1].buffer = (void *) &host_id;
  notif_bind[2].buffer_type = MYSQL_TYPE_LONG;
  notif_bind[2].buffer = (void *) &plugin_id;
  notif_bind[3].buffer_type = MYSQL_TYPE_STRING;
  notif_bind[3].buffer = (void *) n->plugin_instance;
  notif_bind[3].buffer_length = strlen (n->plugin_instance);
  notif_bind[4].buffer_type = MYSQL_TYPE_LONG;
  notif_bind[4].buffer = (void *) &type_id;
  notif_bind[5].buffer_type = MYSQL_TYPE_STRING;
  notif_bind[5].buffer = (void *) n->type_instance;
  notif_bind[5].buffer_length = strlen (n->type_instance);
  notif_bind[6].buffer_type = MYSQL_TYPE_STRING;
  ssnprintf (severity, sizeof (severity), "%s",
	     (n->severity == NOTIF_FAILURE) ? "FAILURE"
	     : ((n->severity == NOTIF_WARNING) ? "WARNING"
		: ((n->severity == NOTIF_OKAY) ? "OKAY" : "UNKNOWN")));
  notif_bind[6].buffer = (void *) severity;
  notif_bind[6].buffer_length = strlen (severity);
  notif_bind[7].buffer_type = MYSQL_TYPE_VAR_STRING;
  notif_bind[7].buffer = (void *) n->message;
  notif_bind[7].buffer_length = strlen (n->message);
  if (mysql_ping (conn) != 0)
    {
      ERROR
	("write_mysql plugin: write_mysql_write - Failed to re-connect to database : %s",
	 mysql_error (conn));
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }
  if (mysql_stmt_bind_param (notif_stmt, notif_bind) != 0)
    {
      ERROR
	("write_mysql plugin: Failed to bind param to statement : %s / %s",
	 mysql_stmt_error (notif_stmt), notif_query);
      pthread_mutex_unlock (&mutexdb);
      return -1;
    }

  if (mysql_stmt_execute (notif_stmt) != 0)
    {
      // Try to re-prepare statement
      notif_stmt = mysql_stmt_init (conn);
      mysql_stmt_prepare (notif_stmt, notif_query, strlen (notif_query));
      mysql_stmt_bind_param (notif_stmt, notif_bind);
      if (mysql_stmt_execute (notif_stmt) != 0)
	{
	  ERROR
	    ("write_mysql plugin: Failed to execute re-prepared statement : %s / %s",
	     mysql_stmt_error (notif_stmt), notif_query);
	  pthread_mutex_unlock (&mutexdb);
	  return -1;
	}
    }
  pthread_mutex_unlock (&mutexdb);
  return 0;
}

static void
free_tree (c_avl_tree_t * tree)
{
  void *key = NULL;
  void *value = NULL;

  if (tree == NULL)
    {
      return;
    }
  while (c_avl_pick (tree, &key, &value) == 0)
    {
      sfree (key);
      sfree (value);
      key = NULL;
      value = NULL;
    }
  c_avl_destroy (tree);
  tree = NULL;
}

static int
write_mysql_shutdown (void)
{
  free_tree (host_tree);
  free_tree (plugin_tree);
  free_tree (type_tree);
  free_tree (dataset_tree);
  mysql_close (conn);
  return 0;
}

void
module_register (void)
{
  plugin_register_init ("write_mysql", write_mysql_init);
  plugin_register_config ("write_mysql", write_mysql_config,
			  config_keys, config_keys_num);
  plugin_register_write ("write_mysql", write_mysql_write, /* user_data = */
			 NULL);
  plugin_register_shutdown ("write_mysql", write_mysql_shutdown);
  plugin_register_notification ("write_mysql", notify_write_mysql,
				/* user_data = */ NULL);
}
