/**
 * collectd - src/mysql_plugin.h
 * Copyright (C) 2006-2017  Florian octo Forster
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
 *   Pavel Rochnyak <pavel2000 ngs.ru>
 **/

#ifdef HAVE_MYSQL_H
#include <mysql.h>
#elif defined(HAVE_MYSQL_MYSQL_H)
#include <mysql/mysql.h>
#endif

#include "utils_llist.h"

struct mysql_database_s;
typedef struct mysql_database_s mysql_database_t;

/* Type for reports source declaration. Fields, in order of usage/call:
 *
 * default_reports - The function to add predefined reports
 * option_name     - The name of configuration block to be passed to config_cb
 * config_cb       - The function to register and configure new report
 * source_init_cb  - The function to init source (check configuration)
 * db_init_cb      - The function to handle database reports registration
 * db_read_cb      - The function to gather all reports for database
 * db_destroy_cb   - The function to handle database unregistration:
 * config_free     - The function to free report configuration structure
 */
struct mysql_report_source_decl_s {
  const char *option_name;
  int (*config_cb)(oconfig_item_t *ci, llist_t *reports);
  void (*config_free)(void *reportconfig);
  int (*default_reports)(llist_t *reports);
  int (*source_init_cb)(const llist_t *reports);
  int (*db_init_cb)(mysql_database_t *db, const llist_t *reports,
                    void **userdata);
  int (*db_read_cb)(mysql_database_t *db, const llist_t *reports,
                    void *userdata);
  void (*db_destroy_cb)(mysql_database_t *db, const llist_t *reports,
                        void *userdata);
};
typedef struct mysql_report_source_decl_s mysql_report_source_decl_t;

/* Type for registered (or predefined) reports. */
struct mysql_report_s {
  char *name;
  void *config;
  _Bool def;    /* Register this report to Database by default */
  _Bool broken; /* Configuration broken, as detected in 'source init' phase */
};
typedef struct mysql_report_s mysql_report_t;

/* Type for registration of requested reports into Database.
 * That is done with use of grouping by report sources
 * The 'decl' is a backreference and 'reports' contains backreferences too.
 */
struct mysql_db_report_source_s;
typedef struct mysql_db_report_source_s mysql_db_report_source_t;

struct mysql_db_report_source_s {
  mysql_report_source_decl_t *decl;
  llist_t *reports; /* linked list of mysql_report_t values */
  void *userdata;
  mysql_db_report_source_t *next;
};

/* Registred databases */
struct mysql_database_s /* {{{ */
{
  char *instance;
  char *alias;
  char *host;
  char *user;
  char *pass;
  char *database;

  /* mysql_ssl_set params */
  char *key;
  char *cert;
  char *ca;
  char *capath;
  char *cipher;

  char *socket;
  int port;
  int timeout;

  _Bool master_stats;
  _Bool slave_stats;
  _Bool innodb_stats;
  _Bool wsrep_stats;

  _Bool slave_notif;
  _Bool slave_io_running;
  _Bool slave_sql_running;

  MYSQL *con;
  _Bool is_connected;

  mysql_db_report_source_t *reportsources; // Chain of report sources
  mysql_database_t *next;
};

/* */
mysql_report_t *mysql_add_report(llist_t *reports, const char *name);

MYSQL_RES *exec_query(MYSQL *con, const char *query);
void submit(const char *type, const char *type_instance, value_t *values,
            size_t values_len, mysql_database_t *db);
void counter_submit(const char *type, const char *type_instance, derive_t value,
                    mysql_database_t *db);
void gauge_submit(const char *type, const char *type_instance, gauge_t value,
                  mysql_database_t *db);
void derive_submit(const char *type, const char *type_instance, derive_t value,
                   mysql_database_t *db);
