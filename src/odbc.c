/**
 * collectd - src/odbc.c
 * Copyright (C) 2008-2015  Florian octo Forster
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
#include "utils/db_query/db_query.h"

#include <sql.h>
#include <sqlext.h>

/*
 * Data types
 */

struct codbc_database_s {
  char *name;
  char *metric_prefix;
  label_set_t labels;

  char *conn;
  char *dsn;
  char *user;
  char *pass;

  char *ping_query;

  udb_query_preparation_area_t **q_prep_areas;
  udb_query_t **queries;
  size_t queries_num;

  SQLHDBC hdbc;
  SQLHENV henv;
};
typedef struct codbc_database_s codbc_database_t;

/*
 * Global variables
 */
static udb_query_t **queries;
static size_t queries_num;
static size_t databases_num;

static int codbc_read_database(user_data_t *ud);

/*
 * Functions
 */

static const char *codbc_strerror(SQLHANDLE hdl, SQLSMALLINT htype,
                                  char *buffer, size_t buffer_size) {
  SQLCHAR sqlstate[6];
  SQLINTEGER nerror;
  SQLCHAR emsg[256];
  SQLSMALLINT emsg_size = 0;
  SQLRETURN rc;

  rc = SQLGetDiagRec(htype, hdl, 1, sqlstate, &nerror, emsg, sizeof(emsg) - 1,
                     &emsg_size);
  if (rc != SQL_NO_DATA_FOUND) {
    emsg[emsg_size] = '\0';
    ssnprintf(buffer, buffer_size, "SqlState: %s ErrorCode: %d  %s\n", sqlstate,
              nerror, emsg);
    return buffer;
  }

  buffer[0] = '\0';
  return buffer;
}

static int codbc_disconnect(codbc_database_t *db) {
  SQLRETURN rc;

  if (db->hdbc != SQL_NULL_HDBC) {
    rc = SQLDisconnect(db->hdbc);
    if (rc != SQL_SUCCESS) {
      char errbuf[1024];
      ERROR("odbc plugin: unable to disconnect %s: %s", db->name,
            codbc_strerror(db->hdbc, SQL_HANDLE_DBC, errbuf, sizeof(errbuf)));
      return -1;
    }
    rc = SQLFreeHandle(SQL_HANDLE_DBC, db->hdbc);
    if (rc != SQL_SUCCESS) {
      ERROR("odbc plugin: unable to free connection handle %s", db->name);
      return -1;
    }
    db->hdbc = SQL_NULL_HDBC;
  }

  if (db->henv != SQL_NULL_HENV) {
    rc = SQLFreeHandle(SQL_HANDLE_ENV, db->henv);
    if (rc != SQL_SUCCESS) {
      ERROR("odbc plugin: unable to free environment handle %s", db->name);
      return -1;
    }
    db->henv = SQL_NULL_HENV;
  }

  return 0;
}

static unsigned int codbc_version(codbc_database_t *db) {
  SQLRETURN rc;
  SQLCHAR buffer[256];
  SQLSMALLINT len = 0;

  rc = SQLGetInfo(db->hdbc, SQL_DBMS_VER, buffer, sizeof(buffer) - 1, &len);
  if (rc != SQL_SUCCESS) {
    char errbuf[1024];
    ERROR("odbc plugin: SQLGetInfo failed in %s: %s", db->name,
          codbc_strerror(db->hdbc, SQL_HANDLE_DBC, errbuf, sizeof(errbuf)));
    return 0;
  }

  if (len > (sizeof(buffer) - 1)) {
    len = sizeof(buffer) - 1;
  }
  buffer[len] = '\0';

  unsigned int version = 0;
  unsigned int mult = 1;
  char *dot;
  char *start = (char *)buffer;
  int i;

  for (i = 0; (dot = strrchr(start, '.')) != NULL && i < 5; i++) {
    version += atoi(dot + 1) * mult;
    *dot = '\0';
    mult *= 100;
  }
  version += atoi(start) * mult;

  if (i == 5) {
    return 0;
  }

  return version;
}

static int codbc_ping(codbc_database_t *db) {
  SQLHSTMT hstmt = SQL_NULL_HSTMT;
  SQLRETURN rc;

  if (db->ping_query == NULL)
    return 1;

  rc = SQLAllocHandle(SQL_HANDLE_STMT, db->hdbc, &hstmt);
  if ((rc != SQL_SUCCESS) && (rc != SQL_SUCCESS_WITH_INFO)) {
    char errbuf[1024];
    ERROR("odbc plugin: SQLAllocHandle STMT failed in %s: %s", db->name,
          codbc_strerror(db->hdbc, SQL_HANDLE_DBC, errbuf, sizeof(errbuf)));
    return 0;
  }

  rc = SQLExecDirect(hstmt, (UCHAR *)(db->ping_query), SQL_NTS);
  if ((rc != SQL_SUCCESS) && (rc != SQL_SUCCESS_WITH_INFO)) {
    char errbuf[1024];
    ERROR("odbc plugin: Error executing pin in %s: %s", db->name,
          codbc_strerror(db->hdbc, SQL_HANDLE_DBC, errbuf, sizeof(errbuf)));
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    return 0;
  }

  while ((rc = SQLMoreResults(hstmt)) != SQL_NO_DATA)
    ;

  if (hstmt != SQL_NULL_HSTMT) {
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
  }

  return 1;
}

static int codbc_get_data(SQLHSTMT hstmt, SQLUSMALLINT idx, SQLSMALLINT type,
                          char *buffer, size_t buffer_size) {
  SQLRETURN rc;
  SQLLEN ptr;

  switch (type) {
  case SQL_TINYINT:
  case SQL_SMALLINT:
  case SQL_INTEGER: {
    long long value = 0;
    if (type == SQL_TINYINT) {
      SQLSMALLINT data;
      rc = SQLGetData(hstmt, idx, SQL_C_CHAR, &data, sizeof(data), &ptr);
      if (rc != SQL_SUCCESS) {
        char errbuf[1024];
        ERROR("odbc plugin: SQLGetData failed: %s",
              codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
        return -1;
      }
      value = data;
    } else if (type == SQL_SMALLINT) {
      SQLSMALLINT data;
      rc = SQLGetData(hstmt, idx, SQL_C_SHORT, &data, sizeof(data), &ptr);
      if (rc != SQL_SUCCESS) {
        char errbuf[1024];
        ERROR("odbc plugin: SQLGetData failed: %s",
              codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
        return -1;
      }
      value = data;
    } else if (type == SQL_INTEGER) {
      SQLINTEGER data;
      rc = SQLGetData(hstmt, idx, SQL_C_SHORT, &data, sizeof(data), &ptr);
      if (rc != SQL_SUCCESS) {
        char errbuf[1024];
        ERROR("odbc plugin: SQLGetData failed: %s",
              codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
        return -1;
      }
      value = data;
    }
    if (ptr == SQL_NULL_DATA) {
      strncpy(buffer, "", buffer_size);
    } else {
      ssnprintf(buffer, buffer_size, "%lli", value);
    }
    break;
  }
  case SQL_FLOAT:
  case SQL_REAL: {
    SQLREAL data;

    rc = SQLGetData(hstmt, idx, SQL_C_FLOAT, &data, sizeof(data), &ptr);
    if (rc != SQL_SUCCESS) {
      char errbuf[1024];
      ERROR("odbc plugin: SQLGetData failed: %s",
            codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
      return -1;
    }

    if (ptr == SQL_NULL_DATA) {
      strncpy(buffer, "", buffer_size);
    } else {
      ssnprintf(buffer, buffer_size, "%63.15g", (double)data);
    }
    break;
  }
  case SQL_DECIMAL:
  case SQL_NUMERIC:
  case SQL_BIGINT:
  case SQL_DOUBLE: {
    SQLDOUBLE data;

    rc = SQLGetData(hstmt, idx, SQL_C_DOUBLE, &data, sizeof(data), &ptr);
    if (rc != SQL_SUCCESS) {
      char errbuf[1024];
      ERROR("odbc plugin: SQLGetData failed: %s",
            codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
      return -1;
    }

    if (ptr == SQL_NULL_DATA) {
      strncpy(buffer, "", buffer_size);
    } else {
      ssnprintf(buffer, buffer_size, "%63.15g", (double)data);
    }
    break;
  }
  case SQL_WCHAR:
  case SQL_WVARCHAR:
  case SQL_WLONGVARCHAR:
  case SQL_CHAR:
  case SQL_LONGVARCHAR:
  case SQL_VARCHAR: {
    rc = SQLGetData(hstmt, idx, SQL_C_CHAR, buffer, buffer_size, &ptr);
    if (rc != SQL_SUCCESS) {
      char errbuf[1024];
      ERROR("odbc plugin: SQLGetData failed: %s",
            codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
      return -1;
    }
    if (ptr == SQL_NULL_DATA) {
      strncpy(buffer, "", buffer_size);
    }
    break;
  }
  default:
    ERROR("odbc plugin: Column %d: Don't know how to handle "
          "source type %hu.",
          idx, type);
    return -1;
  }

  return 0;
}

static void codbc_database_free(void *arg) {
  codbc_database_t *db = arg;
  if (db == NULL)
    return;

  codbc_disconnect(db);

  sfree(db->name);
  sfree(db->metric_prefix);
  sfree(db->conn);
  sfree(db->dsn);
  sfree(db->user);
  sfree(db->pass);
  sfree(db->ping_query);

  label_set_reset(&db->labels);

  if (db->q_prep_areas)
    for (size_t i = 0; i < db->queries_num; ++i)
      udb_query_delete_preparation_area(db->q_prep_areas[i]);
  sfree(db->q_prep_areas);
  /* N.B.: db->queries references objects "owned" by the global queries
   * variable. Free the array here, but not the content. */
  sfree(db->queries);

  sfree(db);
}

/* Configuration handling functions
 *
 * <Plugin odbc>
 *   <Query "query">
 *     Statement "SELECT name, value FROM table"
 *     <Result>
 *       Type "gauge"
 *       Metric "name"
 *       ValueFrom "value"
 *     </Result>
 *     ...
 *   </Query>
 *
 *   <Database "instance">
 *     Driver "mysql"
 *     Interval 120
 *     Connetion "ODBC connection string"
 *     Query "query"
 *   </Database>
 * </Plugin>
 */

static int codbc_config_add_database(oconfig_item_t *ci) {
  cdtime_t interval = 0;
  codbc_database_t *db;
  int status;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("odbc plugin: The `Database' block "
            "needs exactly one string argument.");
    return -1;
  }

  db = calloc(1, sizeof(*db));
  if (db == NULL) {
    ERROR("odbc plugin: calloc failed.");
    return -1;
  }

  db->hdbc = SQL_NULL_HDBC;
  db->henv = SQL_NULL_HENV;

  status = cf_util_get_string(ci, &db->name);
  if (status != 0) {
    sfree(db);
    return status;
  }

  /* Fill the `codbc_database_t' structure.. */
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("DSN", child->key) == 0)
      status = cf_util_get_string(child, &db->dsn);
    else if (strcasecmp("User", child->key) == 0)
      status = cf_util_get_string(child, &db->user);
    else if (strcasecmp("Password", child->key) == 0)
      status = cf_util_get_string(child, &db->pass);
    else if (strcasecmp("Label", child->key) == 0)
      status = cf_util_get_label(child, &db->labels);
    else if (strcasecmp("MetricPrefix", child->key) == 0)
      status = cf_util_get_string(child, &db->metric_prefix);
    else if (strcasecmp("Connection", child->key) == 0)
      status = cf_util_get_string(child, &db->conn);
    else if (strcasecmp("Query", child->key) == 0)
      status = udb_query_pick_from_list(child, queries, queries_num,
                                        &db->queries, &db->queries_num);
    else if (strcasecmp("PingQuery", child->key) == 0)
      status = cf_util_get_string(child, &db->ping_query);
    else if (strcasecmp("Interval", child->key) == 0)
      status = cf_util_get_cdtime(child, &interval);
    else {
      WARNING("odbc plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  /* Check that all necessary options have been given. */
  if (status == 0) {
    if ((db->dsn == NULL) && (db->conn == NULL)) {
      WARNING("odbc plugin: `DSN' or `Connection' not given for database `%s'",
              db->name);
      status = -1;
    }
    if ((db->dsn != NULL) && (db->conn != NULL)) {
      WARNING("odbc plugin: Only `DSN' or `Connection' can be given for "
              "database `%s'",
              db->name);
      status = -1;
    }
  }

  while ((status == 0) && (db->queries_num > 0)) {
    db->q_prep_areas = calloc(db->queries_num, sizeof(*db->q_prep_areas));
    if (db->q_prep_areas == NULL) {
      WARNING("odbc plugin: calloc failed");
      status = -1;
      break;
    }

    for (size_t i = 0; i < db->queries_num; ++i) {
      db->q_prep_areas[i] = udb_query_allocate_preparation_area(db->queries[i]);

      if (db->q_prep_areas[i] == NULL) {
        WARNING("odbc plugin: udb_query_allocate_preparation_area failed");
        status = -1;
        break;
      }
    }

    break;
  }

  if (status == 0) {
    databases_num++;
    char *name = ssnprintf_alloc("odbc:%s", db->name);
    plugin_register_complex_read(
        /* group = */ NULL,
        /* name = */ name ? name : db->name,
        /* callback = */ codbc_read_database,
        /* interval = */ interval,
        &(user_data_t){
            .data = db,
            .free_func = codbc_database_free,
        });
    sfree(name);
  } else {
    codbc_database_free(db);
    return -1;
  }

  return 0;
}

static int codbc_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Query", child->key) == 0)
      udb_query_create(&queries, &queries_num, child,
                       /* callback = */ NULL);
    else if (strcasecmp("Database", child->key) == 0)
      codbc_config_add_database(child);
    else {
      WARNING("odbc plugin: Ignoring unknown config option `%s'.", child->key);
    }
  }

  return 0;
}

static int codbc_init(void) {
  static int did_init;

  if (did_init != 0)
    return 0;

  if (queries_num == 0) {
    ERROR("odbc plugin: No <Query> blocks have been found. Without them, "
          "this plugin can't do anything useful, so we will return an error.");
    return -1;
  }

  if (databases_num == 0) {
    ERROR("odbc plugin: No <Database> blocks have been found. Without them, "
          "this plugin can't do anything useful, so we will return an error.");
    return -1;
  }

  did_init = 1;

  return 0;
}

static int codbc_read_database_query(codbc_database_t *db, udb_query_t *q,
                                     udb_query_preparation_area_t *prep_area) {
  const char *statement;
  size_t column_num;
  char **column_names = NULL;
  char **column_values = NULL;
  SQLSMALLINT *column_types = NULL;
  SQLHSTMT hstmt = SQL_NULL_HSTMT;
  SQLRETURN rc;
  int status;

  statement = udb_query_get_statement(q);
  assert(statement != NULL);

  rc = SQLAllocHandle(SQL_HANDLE_STMT, db->hdbc, &hstmt);
  if ((rc != SQL_SUCCESS) && (rc != SQL_SUCCESS_WITH_INFO)) {
    char errbuf[1024];
    ERROR("odbc plugin: SQLAllocHandle STMT failed in %s: %s", db->name,
          codbc_strerror(db->hdbc, SQL_HANDLE_DBC, errbuf, sizeof(errbuf)));
    status = -1;
    goto error;
  }

  rc = SQLExecDirect(hstmt, (UCHAR *)statement, SQL_NTS);
  if ((rc != SQL_SUCCESS) && (rc != SQL_SUCCESS_WITH_INFO)) {
    char errbuf[1024];
    ERROR("odbc plugin: SQLExecDirect failed in %s: %s", db->name,
          codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
    status = -1;
    goto error;
  } else /* Get the number of columns */
  {
    SQLSMALLINT columns;

    rc = SQLNumResultCols(hstmt, &columns);
    if (rc != SQL_SUCCESS) {
      char errbuf[1024];
      ERROR("odbc plugin: codbc_read_database_query (%s, %s): SQLNumResultCols "
            "failed : %s",
            db->name, udb_query_get_name(q),
            codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
      status = -1;
      goto error;
    }

    column_num = (size_t)columns;
    DEBUG("codbc_read_database_query (%s, %s): There are %" PRIsz " columns.",
          db->name, udb_query_get_name(q), column_num);
  }

  /* Allocate `column_names' and `column_values'. */
  column_names = calloc(column_num, sizeof(*column_names));
  if (column_names == NULL) {
    ERROR("odbc plugin: calloc failed.");
    status = -1;
    goto error;
  }

  column_names[0] = calloc(column_num, DATA_MAX_NAME_LEN);
  if (column_names[0] == NULL) {
    ERROR("odbc plugin: calloc failed.");
    status = -1;
    goto error;
  }
  for (size_t i = 1; i < column_num; i++) {
    column_names[i] = column_names[i - 1] + DATA_MAX_NAME_LEN;
  }

  column_values = calloc(column_num, sizeof(*column_values));
  if (column_values == NULL) {
    ERROR("odbc plugin: calloc failed.");
    status = -1;
    goto error;
  }

  column_values[0] = calloc(column_num, DATA_MAX_NAME_LEN);
  if (column_values[0] == NULL) {
    ERROR("odbc plugin: calloc failed.");
    status = -1;
    goto error;
  }
  for (size_t i = 1; i < column_num; i++)
    column_values[i] = column_values[i - 1] + DATA_MAX_NAME_LEN;

  column_types = calloc(column_num, sizeof(SQLSMALLINT));
  if (column_types == NULL) {
    ERROR("odbc plugin: calloc failed.");
    status = -1;
    goto error;
  }

  for (size_t i = 0; i < column_num; i++) {
    SQLSMALLINT column_name_len = 0;
    SQLCHAR *column_name = (SQLCHAR *)column_names[i];

    rc = SQLDescribeCol(hstmt, (SQLUSMALLINT)(i + 1), column_name,
                        DATA_MAX_NAME_LEN, &column_name_len, &(column_types[i]),
                        NULL, NULL, NULL);
    if (rc != SQL_SUCCESS) {
      char errbuf[1024];
      ERROR("odbc plugin: codbc_read_database_query (%s, %s): SQLDescribeCol "
            "%zu failed : %s",
            db->name, udb_query_get_name(q), i + 1,
            codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
      status = -1;
      goto error;
    }
    column_names[i][column_name_len] = '\0';
  }

  status =
      udb_query_prepare_result(q, prep_area, db->metric_prefix, &db->labels,
                               db->name, column_names, column_num);

  if (status != 0) {
    ERROR("odbc plugin: udb_query_prepare_result failed with status %i.",
          status);
    goto error;
  }

  /* Iterate over all rows and call `udb_query_handle_result' with each list of
   * values. */
  while (42) {
    rc = SQLFetch(hstmt);
    if (rc == SQL_NO_DATA) {
      break;
    }

    if (rc != SQL_SUCCESS) {
      char errbuf[1024];
      ERROR("odbc plugin: codbc_read_database_query (%s, %s): SQLFetch failed "
            ": %s",
            db->name, udb_query_get_name(q),
            codbc_strerror(hstmt, SQL_HANDLE_STMT, errbuf, sizeof(errbuf)));
      status = -1;
      goto error;
    }

    status = 0;
    /* Copy the value of the columns to `column_values' */
    for (size_t i = 0; i < column_num; i++) {
      status = codbc_get_data(hstmt, (SQLUSMALLINT)(i + 1), column_types[i],
                              column_values[i], DATA_MAX_NAME_LEN);

      if (status != 0) {
        ERROR("odbc plugin: codbc_read_database_query (%s, %s): "
              "codbc_result_get_field (%" PRIsz ") \"%s\" failed.",
              db->name, udb_query_get_name(q), i + 1, column_names[i]);
        status = -1;
        goto error;
      }
    }

    /* If all values were copied successfully, call `udb_query_handle_result'
     * to dispatch the row to the daemon. */
    status = udb_query_handle_result(q, prep_area, column_values);
    if (status != 0) {
      ERROR("odbc plugin: codbc_read_database_query (%s, %s): "
            "udb_query_handle_result failed.",
            db->name, udb_query_get_name(q));
      goto error;
    }
  }

  /* Tell the db query interface that we're done with this query. */
  udb_query_finish_result(q, prep_area);

  status = 0;

error:

  if (column_names != NULL) {
    sfree(column_names[0]);
    sfree(column_names);
  }

  if (column_values != NULL) {
    sfree(column_values[0]);
    sfree(column_values);
  }

  if (column_types != NULL) {
    sfree(column_types);
  }

  if (hstmt != SQL_NULL_HSTMT) {
    SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
  }

  return (status);
}

static int codbc_connect(codbc_database_t *db) {
  SQLCHAR buffer[256];
  SQLSMALLINT len;
  SQLRETURN rc = SQL_SUCCESS;
  int status;

  if (db->hdbc != SQL_NULL_HDBC) {
    status = codbc_ping(db);
    if (status != 0) /* connection is alive */
      return 0;

    codbc_disconnect(db);
  }

  rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &(db->henv));
  if (rc != SQL_SUCCESS) {
    ERROR("odbc plugin: codbc_connect(%s): "
          "Unable to allocate environment handle",
          db->name);
    return -1;
  }

  /* set options */
  rc = SQLSetEnvAttr(db->henv, SQL_ATTR_ODBC_VERSION, (void *)SQL_OV_ODBC3, 0);
  if (rc != SQL_SUCCESS) {
    ERROR("odbc plugin: codbc_connect(%s): Unable to set ODBC3 attribute",
          db->name);
    return -1;
  }

  rc = SQLAllocHandle(SQL_HANDLE_DBC, db->henv, &db->hdbc);
  if (rc != SQL_SUCCESS) {
    ERROR("odbc plugin: codbc_connect(%s): "
          "Unable to allocate connection handle",
          db->name);
    return -1;
  }

  if (db->conn != NULL) {
    rc = SQLDriverConnect(db->hdbc, NULL, (SQLCHAR *)db->conn, SQL_NTS, buffer,
                          sizeof(buffer), &len, SQL_DRIVER_COMPLETE);
    if (rc == SQL_SUCCESS_WITH_INFO) {
      buffer[len] = '\0';
      WARNING("odbc plugin: codbc_connect(%s): SQLDriverConnect "
              "reported the following diagnostics: %s",
              db->name, buffer);
    }
    if (rc != SQL_SUCCESS) {
      char errbuf[1024];
      ERROR("odbc plugin: codbc_connect(%s): SQLDriverConnect failed : %s",
            db->name,
            codbc_strerror(db->hdbc, SQL_HANDLE_DBC, errbuf, sizeof(errbuf)));
      codbc_disconnect(db);
      return -1;
    }
  } else {
    rc = SQLConnect(db->hdbc, (SQLCHAR *)db->dsn, SQL_NTS, (SQLCHAR *)db->user,
                    SQL_NTS, (SQLCHAR *)db->pass, SQL_NTS);
    if (rc != SQL_SUCCESS) {
      char errbuf[1024];
      ERROR("odbc plugin: codbc_connect(%s): SQLConnect failed: %s", db->name,
            codbc_strerror(db->hdbc, SQL_HANDLE_DBC, errbuf, sizeof(errbuf)));
      codbc_disconnect(db);
      return -1;
    }
  }

  return 0;
}

static int codbc_read_database(user_data_t *ud) {
  codbc_database_t *db = (codbc_database_t *)ud->data;
  unsigned int db_version;
  int success;
  int status;

  status = codbc_connect(db);
  if (status != 0)
    return status;
  assert(db->dsn != NULL || db->conn != NULL);

  db_version = codbc_version(db);

  success = 0;
  for (size_t i = 0; i < db->queries_num; i++) {
    /* Check if we know the database's version and if so, if this query applies
     * to that version. */
    if ((db_version != 0) &&
        (udb_query_check_version(db->queries[i], db_version) == 0))
      continue;

    status = codbc_read_database_query(db, db->queries[i], db->q_prep_areas[i]);
    if (status == 0)
      success++;
  }

  if (success == 0) {
    ERROR("odbc plugin: All queries failed for database `%s'.", db->name);
    return -1;
  }

  return 0;
}

static int codbc_shutdown(void) {
  databases_num = 0;
  udb_query_free(queries, queries_num);
  queries = NULL;
  queries_num = 0;

  return 0;
}

void module_register(void) {
  plugin_register_complex_config("odbc", codbc_config);
  plugin_register_init("odbc", codbc_init);
  plugin_register_shutdown("odbc", codbc_shutdown);
}
