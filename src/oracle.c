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

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_db_query.h"

#include <oci.h>

/*
 * Data types
 */
struct o_database_s {
  char *name;
  char *host;
  char *connect_id;
  char *username;
  char *password;
  char *password_cmd;
  char *plugin_name;

  udb_query_preparation_area_t **q_prep_areas;
  udb_query_t **queries;
  size_t queries_num;

  OCISvcCtx *oci_service_context;
};
typedef struct o_database_s o_database_t;

struct o_sqlexec_s {
  char *query_name;
  time_t prev_start_time;
  struct o_sqlexec_s *next;
};

typedef struct o_sqlexec_s o_sqlexec_t;

/*
  The hash table for storing the query execution details based on hash index.
*/

struct sqlexec_hashtab_s {
  int size;
  int count;
  o_sqlexec_t **sqlexecs;
};

typedef struct sqlexec_hashtab_s sqlexec_hashtab_t;

/*
 * Global variables
 */
static udb_query_t **queries = NULL;
static size_t queries_num = 0;
static o_database_t **databases = NULL;
static size_t databases_num = 0;

OCIEnv *oci_env = NULL;
OCIError *oci_error = NULL;

/*
 * Functions
 */

// This for initiating the structure o_sqlexec_t with the key as query name
// and value as previous execution time.
static o_sqlexec_t *new_sqlexec(const char *k, time_t v) {
  o_sqlexec_t *i = malloc(sizeof(o_sqlexec_t));
  i->query_name = strdup(k);
  i->prev_start_time = v;
  i->next = NULL;
  return i;
} /* }}} o_sqlexec_t */

// This function is for initiating the hash table which holds the array of
// query execution details structure and index of the array is getting generated
// using hash function applied on the query name key of that structure.

static sqlexec_hashtab_t *ht_new(int queries_num) {
  sqlexec_hashtab_t *ht = malloc(sizeof(sqlexec_hashtab_t));

  ht->size = queries_num;
  ht->count = 0;
  ht->sqlexecs = calloc((size_t)ht->size, sizeof(o_sqlexec_t *));
  return ht;
} /* }}} sqlexec_hashtab_t */

// This function is for deleting the query execution structure (o_sqlexec_t)
// from the array.

static void free_sqlexec_node(o_sqlexec_t *i) {
  free(i->query_name);
  free(i);
} /* }}} void free_sqlexec_node */

void free_sqlexec(o_sqlexec_t *sq) {
  o_sqlexec_t *temp = NULL;
  while (sq != NULL) {
    temp = sq;
    sq = sq->next;
    free(temp);
  }
} /* }}} void del_sqlexec */

// This function deallocates the memory area from hash table.
void free_sqlexec_hashtab_t(sqlexec_hashtab_t *ht) {
  for (int i = 0; i < ht->size; i++) {
    o_sqlexec_t *sqlexec = ht->sqlexecs[i];
    if (sqlexec != NULL) {
      free_sqlexec(sqlexec);
    }
  }
  free(ht->sqlexecs);
  free(ht->count);
  free(ht->size);
  free(ht);
} /* }}} void free_sqlexec_hashtab_t */

// This function is generating hash key index for the sqlexec_hashtab_t
static int sqlexec_get_hash(const char *s, const int num_buckets) {
  int total = 0;
  const int len_s = strlen(s);

  for (int i = 0; i < len_s; i++)
    total += (int)s[i];
  return (total + len_s) % num_buckets;
} /* }}} int sqlexec_get_hash */

// This function is inserting the o_sqlexec_t struct in hash table array
// with the index generated using hash functions on the struct query_name key.
// For hash collisions, this function is storing the elements in the linked
// list.

void ht_insert(sqlexec_hashtab_t *ht, const char *key, time_t value) {
  o_sqlexec_t *sqlexec = new_sqlexec(key, value);
  int index = sqlexec_get_hash(key, ht->size);
  o_sqlexec_t *cur_sqlexec = ht->sqlexecs[index];
  o_sqlexec_t *prev_sqlexec = cur_sqlexec;
  while (cur_sqlexec != NULL) {
    if (strcmp(cur_sqlexec->query_name, key) == 0) {
      free_sqlexec_node(cur_sqlexec);
      cur_sqlexec = sqlexec;
      prev_sqlexec->next = cur_sqlexec;
      ht->count++;
      return;
    }
    prev_sqlexec = cur_sqlexec;
    cur_sqlexec = cur_sqlexec->next;
  }
  ht->sqlexecs[index] = sqlexec;
  ht->count++;
} /* }}} void ht_insert */

// This function returns the previous execution time for a given query name.
time_t sqlexec_search(sqlexec_hashtab_t *ht, const char *key) {
  int index = sqlexec_get_hash(key, ht->size);
  o_sqlexec_t *sqlexec = ht->sqlexecs[index];
  while (sqlexec != NULL) {
    if (strcmp(sqlexec->query_name, key) == 0) {
      return sqlexec->prev_start_time;
    }
    sqlexec = sqlexec->next;
  }
  return 0;
} /* }}} time_t sqlexec_search */

static void o_report_error(const char *where, /* {{{ */
                           const char *db_name, const char *query_name,
                           const char *what, OCIError *eh) {
  char buffer[2048];
  sb4 error_code;
  int status;

  if (db_name == NULL)
    db_name = "(none)";
  if (query_name == NULL)
    query_name = "(none)";

  /* An operation may cause / return multiple errors. Loop until we have
   * handled all errors available (with a fail-save limit of 16). */
  for (unsigned int record_number = 1; record_number <= 16; record_number++) {
    memset(buffer, 0, sizeof(buffer));
    error_code = -1;

    status = OCIErrorGet(eh, (ub4)record_number,
                         /* sqlstate = */ NULL, &error_code, (text *)&buffer[0],
                         (ub4)sizeof(buffer), OCI_HTYPE_ERROR);
    buffer[sizeof(buffer) - 1] = 0;

    if (status == OCI_NO_DATA)
      return;

    if (status == OCI_SUCCESS) {
      size_t buffer_length;

      buffer_length = strlen(buffer);
      while ((buffer_length > 0) && (buffer[buffer_length - 1] < 32)) {
        buffer_length--;
        buffer[buffer_length] = 0;
      }

      ERROR("oracle plugin: %s (db = %s, query = %s): %s failed: %s", where,
            db_name, query_name, what, buffer);
    } else {
      ERROR("oracle plugin: %s (db = %s, query = %s): %s failed. "
            "Additionally, OCIErrorGet failed with status %i.",
            where, db_name, query_name, what, status);
      return;
    }
  }
} /* }}} void o_report_error */

static void o_database_free(o_database_t *db) /* {{{ */
{
  if (db == NULL)
    return;

  sfree(db->name);
  sfree(db->connect_id);
  sfree(db->username);
  sfree(db->password);
  sfree(db->password_cmd);
  sfree(db->queries);
  sfree(db->plugin_name);

  if (db->q_prep_areas != NULL)
    for (size_t i = 0; i < db->queries_num; ++i)
      udb_query_delete_preparation_area(db->q_prep_areas[i]);
  free(db->q_prep_areas);

  sfree(db);
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

static int o_config_add_database(oconfig_item_t *ci) /* {{{ */
{
  o_database_t *db;
  int status;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("oracle plugin: The `Database' block "
            "needs exactly one string argument.");
    return -1;
  }

  db = calloc(1, sizeof(*db));
  if (db == NULL) {
    ERROR("oracle plugin: calloc failed.");
    return -1;
  }
  db->name = NULL;
  db->host = NULL;
  db->connect_id = NULL;
  db->username = NULL;
  db->password = NULL;
  db->password_cmd = NULL;
  db->plugin_name = NULL;

  status = cf_util_get_string(ci, &db->name);
  if (status != 0) {
    sfree(db);
    return status;
  }

  /* Fill the `o_database_t' structure.. */
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("ConnectID", child->key) == 0)
      status = cf_util_get_string(child, &db->connect_id);
    else if (strcasecmp("Host", child->key) == 0)
      status = cf_util_get_string(child, &db->host);
    else if (strcasecmp("Username", child->key) == 0)
      status = cf_util_get_string(child, &db->username);
    else if (strcasecmp("Password", child->key) == 0)
      status = cf_util_get_string(child, &db->password);
    else if (strcasecmp("PasswordCommand", child->key) == 0)
      status = cf_util_get_string(child, &db->password_cmd);
    else if (strcasecmp("Plugin", child->key) == 0)
      status = cf_util_get_string(child, &db->plugin_name);
    else if (strcasecmp("Query", child->key) == 0)
      status = udb_query_pick_from_list(child, queries, queries_num,
                                        &db->queries, &db->queries_num);
    else {
      WARNING("oracle plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  /* Check that all necessary options have been given. */
  while (status == 0) {
    if (db->connect_id == NULL) {
      WARNING("oracle plugin: `ConnectID' not given for query `%s'", db->name);
      status = -1;
    }
    if (db->username == NULL) {
      WARNING("oracle plugin: `Username' not given for query `%s'", db->name);
      status = -1;
    }
    if (db->password == NULL && db->password_cmd == NULL) {
      WARNING(
          "oracle plugin: no `Password' or `PasswordCommand' for query `%s'",
          db->name);
      status = -1;
    }

    break;
  } /* while (status == 0) */

  while ((status == 0) && (db->queries_num > 0)) {
    db->q_prep_areas = (udb_query_preparation_area_t **)calloc(
        db->queries_num, sizeof(*db->q_prep_areas));

    if (db->q_prep_areas == NULL) {
      WARNING("oracle plugin: calloc failed");
      status = -1;
      break;
    }

    for (int i = 0; i < db->queries_num; ++i) {
      db->q_prep_areas[i] = udb_query_allocate_preparation_area(db->queries[i]);

      if (db->q_prep_areas[i] == NULL) {
        WARNING("oracle plugin: udb_query_allocate_preparation_area failed");
        status = -1;
        break;
      }
    }

    break;
  }

  /* If all went well, add this query to the list of queries within the
   * database structure. */
  if (status == 0) {
    o_database_t **temp;

    temp = realloc(databases, sizeof(*databases) * (databases_num + 1));
    if (temp == NULL) {
      ERROR("oracle plugin: realloc failed");
      status = -1;
    } else {
      databases = temp;
      databases[databases_num] = db;
      databases_num++;
    }
  }

  if (status != 0) {
    o_database_free(db);
    return -1;
  }

  return 0;
} /* }}} int o_config_add_database */

static int o_config(oconfig_item_t *ci) /* {{{ */
{
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Query", child->key) == 0)
      udb_query_create(&queries, &queries_num, child,
                       /* callback = */ NULL);
    else if (strcasecmp("Database", child->key) == 0)
      o_config_add_database(child);
    else {
      WARNING("oracle plugin: Ignoring unknown config option `%s'.",
              child->key);
    }

    if (queries_num > 0) {
      DEBUG("oracle plugin: o_config: queries_num = %zu; queries[0] = %p; "
            "udb_query_get_user_data (queries[0]) = %p;",
            queries_num, (void *)queries[0],
            udb_query_get_user_data(queries[0]));
    }
  } /* for (ci->children) */

  return 0;
} /* }}} int o_config */

/* }}} End of configuration handling functions */

static int o_init(void) /* {{{ */
{
  int status;

  if (oci_env != NULL)
    return 0;

  status = OCIEnvCreate(&oci_env,
                        /* mode = */ OCI_THREADED,
                        /* context        = */ NULL,
                        /* malloc         = */ NULL,
                        /* realloc        = */ NULL,
                        /* free           = */ NULL,
                        /* user_data_size = */ 0,
                        /* user_data_ptr  = */ NULL);
  if (status != 0) {
    ERROR("oracle plugin: OCIEnvCreate failed with status %i.", status);
    return -1;
  }

  status = OCIHandleAlloc(oci_env, (void *)&oci_error, OCI_HTYPE_ERROR,
                          /* user_data_size = */ 0, /* user_data = */ NULL);
  if (status != OCI_SUCCESS) {
    ERROR("oracle plugin: OCIHandleAlloc (OCI_HTYPE_ERROR) failed "
          "with status %i.",
          status);
    return -1;
  }

  return 0;
} /* }}} int o_init */

static int o_read_database_query(o_database_t *db, /* {{{ */
                                 udb_query_t *q,
                                 udb_query_preparation_area_t *prep_area,
                                 sqlexec_hashtab_t *ht) {
  char **column_names;
  char **column_values;
  size_t column_num;
  unsigned int oci_interval;
  double time_diff;

  OCIStmt *oci_statement;
  char *key = malloc(sizeof(char) * 120);

  /* List of `OCIDefine' pointers. These defines map columns to the buffer
   * space declared above. */
  OCIDefine **oci_defines;

  int status;

  oci_statement = udb_query_get_user_data(q);
  oci_interval = udb_query_get_interval(q);

  /* Get the current timestamp */
  time_t current_time = time(NULL);
  DEBUG("oracle plugin: current sys time %ld(%s).", current_time,
        ctime(&current_time));

  /* Get the prev execution timestamp for the query */
  key = strcpy(key, db->name);
  key = strcat(key, "_");
  key = strcat(key, udb_query_get_name(q));
  time_t prev_start_time = sqlexec_search(ht, key);
  if (prev_start_time != 0) {
    time_diff = difftime(current_time, prev_start_time);
  } else {
    time_diff = oci_interval + 1;
  }

  if (time_diff < oci_interval) {
    return 0;
  }

  /* Update the hash table with the query name and the current time only for
   * queries which have non zero interval defined */
  if
    oci_interval > 0 { ht_insert(ht, key, current_time); }

  /* Prepare the statement */
  if (oci_statement == NULL) /* {{{ */
  {
    const char *statement;

    statement = udb_query_get_statement(q);
    assert(statement != NULL);

    status = OCIHandleAlloc(oci_env, (void *)&oci_statement, OCI_HTYPE_STMT,
                            /* user_data_size = */ 0, /* user_data = */ NULL);
    if (status != OCI_SUCCESS) {
      o_report_error("o_read_database_query", db->name, udb_query_get_name(q),
                     "OCIHandleAlloc", oci_error);
      oci_statement = NULL;
      return -1;
    }

    status = OCIStmtPrepare(oci_statement, oci_error, (text *)statement,
                            (ub4)strlen(statement),
                            /* language = */ OCI_NTV_SYNTAX,
                            /* mode     = */ OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
      o_report_error("o_read_database_query", db->name, udb_query_get_name(q),
                     "OCIStmtPrepare", oci_error);
      OCIHandleFree(oci_statement, OCI_HTYPE_STMT);
      oci_statement = NULL;
      return -1;
    }
    udb_query_set_user_data(q, oci_statement);

    DEBUG("oracle plugin: o_read_database_query (%s, %s): "
          "Successfully allocated statement handle.",
          db->name, udb_query_get_name(q));
  } /* }}} */

  assert(oci_statement != NULL);

  /* Execute the statement */
  status = OCIStmtExecute(db->oci_service_context, /* {{{ */
                          oci_statement, oci_error,
                          /* iters = */ 0,
                          /* rowoff = */ 0,
                          /* snap_in = */ NULL, /* snap_out = */ NULL,
                          /* mode = */ OCI_DEFAULT);
  if (status != OCI_SUCCESS) {
    o_report_error("o_read_database_query", db->name, udb_query_get_name(q),
                   "OCIStmtExecute", oci_error);
    return -1;
  } /* }}} */

  /* Acquire the number of columns returned. */
  do /* {{{ */
  {
    ub4 param_counter = 0;
    status = OCIAttrGet(oci_statement, OCI_HTYPE_STMT, /* {{{ */
                        &param_counter, /* size pointer = */ NULL,
                        OCI_ATTR_PARAM_COUNT, oci_error);
    if (status != OCI_SUCCESS) {
      o_report_error("o_read_database_query", db->name, udb_query_get_name(q),
                     "OCIAttrGet", oci_error);
      return -1;
    } /* }}} */

    column_num = (size_t)param_counter;
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

#define FREE_ALL                                                               \
  if (column_names != NULL) {                                                  \
    sfree(column_names[0]);                                                    \
    sfree(column_names);                                                       \
  }                                                                            \
  if (column_values != NULL) {                                                 \
    sfree(column_values[0]);                                                   \
    sfree(column_values);                                                      \
  }                                                                            \
  sfree(oci_defines)

#define ALLOC_OR_FAIL(ptr, ptr_size)                                           \
  do {                                                                         \
    size_t alloc_size = (size_t)((ptr_size));                                  \
    (ptr) = calloc(1, alloc_size);                                             \
    if ((ptr) == NULL) {                                                       \
      FREE_ALL;                                                                \
      ERROR("oracle plugin: o_read_database_query: calloc failed.");           \
      return -1;                                                               \
    }                                                                          \
  } while (0)

  /* Initialize everything to NULL so the above works. */
  column_names = NULL;
  column_values = NULL;
  oci_defines = NULL;

  ALLOC_OR_FAIL(column_names, column_num * sizeof(char *));
  ALLOC_OR_FAIL(column_names[0], column_num * DATA_MAX_NAME_LEN);
  for (size_t i = 1; i < column_num; i++)
    column_names[i] = column_names[i - 1] + DATA_MAX_NAME_LEN;

  ALLOC_OR_FAIL(column_values, column_num * sizeof(char *));
  ALLOC_OR_FAIL(column_values[0], column_num * DATA_MAX_NAME_LEN);
  for (size_t i = 1; i < column_num; i++)
    column_values[i] = column_values[i - 1] + DATA_MAX_NAME_LEN;

  ALLOC_OR_FAIL(oci_defines, column_num * sizeof(OCIDefine *));
  /* }}} End of buffer allocations. */

  /* ``Define'' the returned data, i. e. bind the columns to the buffers
   * allocated above. */
  for (size_t i = 0; i < column_num; i++) /* {{{ */
  {
    char *column_name;
    ub4 column_name_length;
    OCIParam *oci_param;

    oci_param = NULL;

    status = OCIParamGet(oci_statement, OCI_HTYPE_STMT, oci_error,
                         (void *)&oci_param, (ub4)(i + 1));
    if (status != OCI_SUCCESS) {
      /* This is probably alright */
      DEBUG("oracle plugin: o_read_database_query: status = %#x (= %i);",
            status, status);
      o_report_error("o_read_database_query", db->name, udb_query_get_name(q),
                     "OCIParamGet", oci_error);
      status = OCI_SUCCESS;
      break;
    }

    column_name = NULL;
    column_name_length = 0;
    status = OCIAttrGet(oci_param, OCI_DTYPE_PARAM, &column_name,
                        &column_name_length, OCI_ATTR_NAME, oci_error);
    if (status != OCI_SUCCESS) {
      OCIDescriptorFree(oci_param, OCI_DTYPE_PARAM);
      o_report_error("o_read_database_query", db->name, udb_query_get_name(q),
                     "OCIAttrGet (OCI_ATTR_NAME)", oci_error);
      continue;
    }

    OCIDescriptorFree(oci_param, OCI_DTYPE_PARAM);
    oci_param = NULL;

    /* Copy the name to column_names. Warning: The ``string'' returned by OCI
     * may not be null terminated! */
    memset(column_names[i], 0, DATA_MAX_NAME_LEN);
    if (column_name_length >= DATA_MAX_NAME_LEN)
      column_name_length = DATA_MAX_NAME_LEN - 1;
    memcpy(column_names[i], column_name, column_name_length);
    column_names[i][column_name_length] = 0;

    DEBUG("oracle plugin: o_read_database_query: column_names[%zu] = %s; "
          "column_name_length = %" PRIu32 ";",
          i, column_names[i], (uint32_t)column_name_length);

    status = OCIDefineByPos(oci_statement, &oci_defines[i], oci_error,
                            (ub4)(i + 1), column_values[i], DATA_MAX_NAME_LEN,
                            SQLT_STR, NULL, NULL, NULL, OCI_DEFAULT);
    if (status != OCI_SUCCESS) {
      o_report_error("o_read_database_query", db->name, udb_query_get_name(q),
                     "OCIDefineByPos", oci_error);
      continue;
    }
  } /* for (j = 1; j <= param_counter; j++) */
  /* }}} End of the ``define'' stuff. */

  status = udb_query_prepare_result(
      q, prep_area, (db->host != NULL) ? db->host : hostname_g,
      /* plugin = */ (db->plugin_name != NULL) ? db->plugin_name : "oracle",
      db->name, column_names, column_num,
      /* interval = */ 0);
  if (status != 0) {
    ERROR("oracle plugin: o_read_database_query (%s, %s): "
          "udb_query_prepare_result failed.",
          db->name, udb_query_get_name(q));
    FREE_ALL;
    return -1;
  }

  /* Fetch and handle all the rows that matched the query. */
  while (42) /* {{{ */
  {
    status = OCIStmtFetch2(oci_statement, oci_error,
                           /* nrows = */ 1, /* orientation = */ OCI_FETCH_NEXT,
                           /* fetch offset = */ 0, /* mode = */ OCI_DEFAULT);
    if (status == OCI_NO_DATA) {
      status = OCI_SUCCESS;
      break;
    } else if ((status != OCI_SUCCESS) && (status != OCI_SUCCESS_WITH_INFO)) {
      o_report_error("o_read_database_query", db->name, udb_query_get_name(q),
                     "OCIStmtFetch2", oci_error);
      break;
    }

    status = udb_query_handle_result(q, prep_area, column_values);
    if (status != 0) {
      WARNING("oracle plugin: o_read_database_query (%s, %s): "
              "udb_query_handle_result failed.",
              db->name, udb_query_get_name(q));
    }
  } /* }}} while (42) */

  /* DEBUG ("oracle plugin: o_read_database_query: This statement succeeded:
   * %s", q->statement); */
  FREE_ALL;

  return 0;
#undef FREE_ALL
#undef ALLOC_OR_FAIL
} /* }}} int o_read_database_query */

static int o_read_password_command(o_database_t *db) /* {{{ */
{
  const size_t pw_increment = 128;
  char cmdbuf[4096], *sp, *bp, *ep = cmdbuf + sizeof cmdbuf, *fmtval;
  char *pass, *nl;
  size_t remain, w, r, px;
  FILE *out;

  for (sp = db->password_cmd, bp = cmdbuf; bp < ep;) {
    switch (*sp) {
    case '\0':
      *bp++ = '\0';
      goto success;
    case '%':
      switch (sp[1]) {
      case 'u':
        fmtval = db->username;
        break;
      case 'n':
        fmtval = db->connect_id;
        break;
      case '%':
        fmtval = "%";
        break;
      default:
        ERROR("o_read_password_command '%s': invalid specifier %%'%c'",
              db->password_cmd, sp[1]);
        return -1;
      }
      remain = ep - bp;
      w = snprintf(bp, remain, "%s", fmtval);
      if (w < 0 || w >= remain)
        goto fail_bufsiz;
      bp += w;
      sp += 2;
      break;
    default:
      *bp++ = *sp++;
    }
  }
fail_bufsiz:
  ERROR("o_read_password_command '%s': command too long", db->password_cmd);
  return -1;
success:
  out = popen(cmdbuf, "r");
  if (!out) {
    ERROR("o_read_password_command: popen '%s': %s", cmdbuf, strerror(errno));
    return -1;
  }

  // read up to the first newline into pass.
  pass = smalloc(pw_increment + 1);
  for (px = 0;;) {
    r = fread(pass + px, 1, pw_increment, out);
    pass[px + r] = '\0';
    if ((nl = strchr(pass + px, '\n')) != NULL) {
      *nl = '\0';
      break;
    }
    if (r < pw_increment)
      break;
    px += r;
    pass = realloc(pass, px + pw_increment);
    if (!pass) {
      ERROR("o_read_password_command: realloc: %s", strerror(errno));
      fclose(out);
      return -1;
    }
  }
  db->password = pass;
  fclose(out);
  return 0;
} /* }}} int o_read_password_command /*/

static int o_read_database(o_database_t *db, sqlexec_hashtab_t *ht) /* {{{ */
{
  int status;

  if (db->oci_service_context != NULL) {
    OCIServer *server_handle;
    ub4 connection_status;

    server_handle = NULL;
    status = OCIAttrGet((void *)db->oci_service_context, OCI_HTYPE_SVCCTX,
                        (void *)&server_handle, /* size pointer = */ NULL,
                        OCI_ATTR_SERVER, oci_error);
    if (status != OCI_SUCCESS) {
      o_report_error("o_read_database", db->name, NULL, "OCIAttrGet",
                     oci_error);
      return -1;
    }

    if (server_handle == NULL) {
      connection_status = OCI_SERVER_NOT_CONNECTED;
    } else /* if (server_handle != NULL) */
    {
      connection_status = 0;
      status = OCIAttrGet((void *)server_handle, OCI_HTYPE_SERVER,
                          (void *)&connection_status, /* size pointer = */ NULL,
                          OCI_ATTR_SERVER_STATUS, oci_error);
      if (status != OCI_SUCCESS) {
        o_report_error("o_read_database", db->name, NULL, "OCIAttrGet",
                       oci_error);
        return -1;
      }
    }

    if (connection_status != OCI_SERVER_NORMAL) {
      INFO("oracle plugin: Connection to %s lost. Trying to reconnect.",
           db->name);
      OCIHandleFree(db->oci_service_context, OCI_HTYPE_SVCCTX);
      db->oci_service_context = NULL;
    }
  } /* if (db->oci_service_context != NULL) */

  if (db->password == NULL && db->password_cmd != NULL) {
    if ((status = o_read_password_command(db)) != 0)
      return status;
  }

  if (db->oci_service_context == NULL) {
    status = OCILogon(oci_env, oci_error, &db->oci_service_context,
                      (OraText *)db->username, (ub4)strlen(db->username),
                      (OraText *)db->password, (ub4)strlen(db->password),
                      (OraText *)db->connect_id, (ub4)strlen(db->connect_id));
    if ((status != OCI_SUCCESS) && (status != OCI_SUCCESS_WITH_INFO)) {
      char errfunc[256];

      snprintf(errfunc, sizeof(errfunc), "OCILogon(\"%s\")", db->connect_id);

      o_report_error("o_read_database", db->name, NULL, errfunc, oci_error);
      DEBUG("oracle plugin: OCILogon (%s): db->oci_service_context = %p;",
            db->connect_id, db->oci_service_context);
      db->oci_service_context = NULL;
      return -1;
    } else if (status == OCI_SUCCESS_WITH_INFO) {
      /* TODO: Print NOTIFY message. */
    }
    assert(db->oci_service_context != NULL);
  }

  DEBUG("oracle plugin: o_read_database: db->connect_id = %s; "
        "db->oci_service_context = %p;",
        db->connect_id, db->oci_service_context);

  for (size_t i = 0; i < db->queries_num; i++)
    o_read_database_query(db, db->queries[i], db->q_prep_areas[i], ht);

  return 0;
} /* }}} int o_read_database */

static int o_read(void) /* {{{ */
{
  size_t i;
  size_t records;
  sqlexec_hashtab_t *ht = ht_new((queries_num * databases_num) + 1);
  FILE *infile;
  infile = fopen("/dev/shm/collectd_oracle_query.stats", "r");
  if (infile != NULL) {
    while (1) {
      records = fread(ht, sizeof(*ht), 1, infile);
      if (feof(infile)) {
        break;
      }
    }
    fclose(infile);
  }

  for (i = 0; i < databases_num; i++) {
    o_read_database(databases[i], ht);
  }

  // write the sql execution hash table to the file so it can fetch details
  // and resume for the next collectd iteration.
  FILE *outfile;
  outfile = fopen("/dev/shm/collectd_oracle_query.stats", "w");
  if (outfile == NULL) {
    ERROR("o_write_file: error writing to file "
          "/dev/shm/collectd_oracle_query.stats");
    return -1;
  }
  records = fwrite(ht, sizeof(*ht), 1, outfile);
  fclose(outfile);
  free_sqlexec_hashtab_t(ht);
  return 0;
} /* }}} int o_read */

static int o_shutdown(void) /* {{{ */
{
  size_t i;

  for (i = 0; i < databases_num; i++)
    if (databases[i]->oci_service_context != NULL) {
      OCIHandleFree(databases[i]->oci_service_context, OCI_HTYPE_SVCCTX);
      databases[i]->oci_service_context = NULL;
    }

  for (i = 0; i < queries_num; i++) {
    OCIStmt *oci_statement;

    oci_statement = udb_query_get_user_data(queries[i]);
    if (oci_statement != NULL) {
      OCIHandleFree(oci_statement, OCI_HTYPE_STMT);
      udb_query_set_user_data(queries[i], NULL);
    }
  }

  OCIHandleFree(oci_env, OCI_HTYPE_ENV);
  oci_env = NULL;

  udb_query_free(queries, queries_num);
  queries = NULL;
  queries_num = 0;

  if (remove("/dev/shm/collectd_oracle_query.stats") != 0)
    ERROR("o_write_file: error removing file "
          "/dev/shm/collectd_oracle_query.stats");
  return 0;
} /* }}} int o_shutdown */

void module_register(void) /* {{{ */
{
  plugin_register_complex_config("oracle", o_config);
  plugin_register_init("oracle", o_init);
  plugin_register_read("oracle", o_read);
  plugin_register_shutdown("oracle", o_shutdown);
} /* }}} void module_register */
