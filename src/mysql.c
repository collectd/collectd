/**
 * collectd - src/mysql.c
 * Copyright (C) 2006-2010  Florian octo Forster
 * Copyright (C) 2008       Mirko Buffoni
 * Copyright (C) 2009       Doug MacEachern
 * Copyright (C) 2009       Sebastian tokkee Harl
 * Copyright (C) 2009       Rodolphe Quiédeville
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
 *   Florian octo Forster <octo at collectd.org>
 *   Mirko Buffoni <briareos at eswat.org>
 *   Doug MacEachern <dougm at hyperic.com>
 *   Sebastian tokkee Harl <sh at tokkee.org>
 *   Rodolphe Quiédeville <rquiedeville at bearstech.com>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include "mysql_plugin.h"

/* Forward declaration of report source handlers */
static int mysql_compatible_read(mysql_database_t *db, const llist_t *reports,
                                 void *userdata);
static int mysql_compatible_reports(llist_t *reports);

/* Functions from mysql_status_global.c */
extern int mysql_reports_config(oconfig_item_t *ci, llist_t *reports);
extern void mysql_reports_config_free(void *reportconfig);
extern int mysql_reports_init(const llist_t *reports);
extern int mysql_reports_db_init(mysql_database_t *db, const llist_t *reports,
                                 void **userdata);
extern int mysql_reports_status_read(mysql_database_t *db,
                                     const llist_t *reports, void *userdata);
extern int mysql_reports_innodb_metrics_read(mysql_database_t *db,
                                             const llist_t *reports,
                                             void *userdata);
extern void mysql_reports_db_destroy(mysql_database_t *db,
                                     const llist_t *reports, void *userdata);

/* All supported Report Sources */
static mysql_report_source_decl_t report_sources_decl[] = {
    {
        .option_name = NULL,
        .config_cb = NULL,
        .config_free = NULL,
        .default_reports = mysql_compatible_reports,
        .db_init_cb = NULL,
        .db_read_cb = mysql_compatible_read,
        .db_destroy_cb = NULL,
    },
    {
        .option_name = "GlobalStatusReport",
        .config_cb = mysql_reports_config,
        .config_free = mysql_reports_config_free,
        .source_init_cb = mysql_reports_init,
        .db_init_cb = mysql_reports_db_init,
        .db_read_cb = mysql_reports_status_read,
        .db_destroy_cb = mysql_reports_db_destroy,
    },
    {
        .option_name = "InnoDBMetricsReport",
        .config_cb = mysql_reports_config,
        .config_free = mysql_reports_config_free,
        .source_init_cb = mysql_reports_init,
        .db_init_cb = mysql_reports_db_init,
        .db_read_cb = mysql_reports_innodb_metrics_read,
        .db_destroy_cb = mysql_reports_db_destroy,
    }

};

/* Type for registration of report sources and their reports */
struct mysql_report_source_s;
typedef struct mysql_report_source_s mysql_report_source_t;

struct mysql_report_source_s {
  mysql_report_source_decl_t *decl;
  llist_t *reports; /* linked list of mysql_report_t values */
  mysql_report_source_t *next;
};

/* Database initialization queue */
static mysql_database_t *databases_first = NULL;
static mysql_database_t *databases_last = NULL;
/* All supported Report Sources */
static mysql_report_source_t *report_sources = NULL;

static MYSQL *getconnection(mysql_database_t *db);

static void mysql_find_report_by_name(const char *name,
                                      mysql_report_source_t **src_ret,
                                      mysql_report_t **report_ret) {

  for (mysql_report_source_t *src = report_sources; src; src = src->next) {
    llentry_t *le = llist_head(src->reports);
    while (le != NULL) {
      if (strcasecmp(le->key, name) == 0) {
        if (src_ret != NULL)
          *src_ret = src;
        if (report_ret != NULL)
          *report_ret = le->value;
        return;
      }
      le = le->next;
    }
  };
} /* void mysql_find_report_by_name */

mysql_report_t *mysql_add_report(llist_t *reports, const char *name) {
  if (reports == NULL || name == NULL) {
    ERROR("mysql plugin: mysql_add_report: invalid input.");
    return NULL;
  }

  mysql_report_source_t *type = NULL;
  mysql_find_report_by_name(name, &type, NULL);
  if (type) {
    ERROR("mysql plugin: mysql_add_report: Report `%s' already added.", name);
    return NULL;
  };

#if 0
  //When called from 'default_reports' callback, conflicts between reports of
  //the new type are not checked, because type is not globally registered yet.
  {
    llentry_t *le = llist_head(reports);
    
    while (le != NULL) {
      if (strcasecmp(le->key, name) == 0) {
        ERROR("mysql plugin: mysql_add_report: Report `%s' already added.", name);
        return NULL;
      }
      le = le->next;
    };
  }
#endif

  mysql_report_t *report = calloc(1, sizeof(mysql_report_t));
  if (report == NULL) {
    ERROR("mysql plugin: mysql_add_report: calloc failed.");
    return NULL;
  }

  report->name = strdup(name);
  if (report->name == NULL) {
    ERROR("mysql plugin: mysql_add_report: strdup failed.");
    return NULL;
  }

  llentry_t *le = llentry_create(report->name, report);
  if (le == NULL) {
    ERROR("mysql plugin: mysql_add_report: llentry_create failed.");
    return NULL;
  }

  llist_append(reports, le);
  return report;
} /* mysql_report_t *mysql_add_report */

static void mysql_db_report_source_free(mysql_db_report_source_t *source) {
  llist_destroy(source->reports);
  sfree(source);
} /* void mysql_db_report_source_free */

/* Configuration handling functions {{{
 *
 * Existing options (and their defaults):
 *
 *   MasterStats false
 *   SlaveStats false
 *   SlaveNotifications false
 *   WsrepStats false
 *   InnodbStats false
 *
 * Usage schema:
 *
 * <Plugin mysql>
 *   <GlobalStatusReport "TableCache">
 *      ... some configuration ...
 *   </GlobalStatusReport>
 *   <PerformanceSchemaReport "ReportName">
 *      ... some configuration ...
 *   </PerformanceSchemaReport>
 *   <InnoDBReport "InnoDB">
 *      ... some configuration ...
 *   </InnoDBReport>
 *   ...
 *   <Database "plugin_instance1">
 *     Host "localhost"
 *     Port 3306
 *     ...
 *     DisableReport compatible
 *     Report InnoDB
 *     ...
 *   </Database>
 * </Plugin>
 */

llist_t *c_mysql_clone_reports_list(llist_t *src) {
  if (src == NULL)
    return NULL;

  llist_t *dst = llist_create();
  if (dst == NULL) {
    ERROR("mysql plugin: mysql_clone_reports_list: llist_create failed.");
    return NULL;
  }

  llentry_t *le = llist_head(src);
  while (le != NULL) {
    mysql_report_t *report = le->value;

    if (report->def) { // Enabled by default
      llentry_t *newle = llentry_create(le->key, report);
      if (newle == NULL) {
        ERROR("mysql plugin: mysql_clone_reports_list: llentry_create failed.");
        llist_destroy(dst);
        return NULL;
      }
      llist_append(dst, newle);
    }
    le = le->next;
  }
  return dst;
} /* llist_t* c_mysql_clone_reports_list */

static int c_mysql_db_add_sources(mysql_database_t *db) {
  mysql_db_report_source_t *last_source = NULL;

  for (mysql_report_source_t *g_source = report_sources; g_source;
       g_source = g_source->next) {

    mysql_db_report_source_t *source =
        calloc(1, sizeof(mysql_db_report_source_t));
    if (source == NULL) {
      ERROR("mysql plugin: calloc failed.");
      return -1;
    };

    source->decl = g_source->decl;
    source->reports = c_mysql_clone_reports_list(g_source->reports);
    if (source->reports == NULL)
      return -1;

    if (last_source)
      last_source->next = source;
    else
      db->reportsources = source;

    last_source = source;
  };
  return 0;
};

static int c_mysql_db_init_sources(mysql_database_t *db) {
  mysql_db_report_source_t *prev = NULL;

  for (mysql_db_report_source_t *source = db->reportsources; source;
       source = source->next) {
    int ret = 0;

    llentry_t *le = llist_head(source->reports);
    while (le != NULL) {
      llentry_t *next = le->next;
      mysql_report_t *report = le->value;

      if (report->broken) {
        WARNING("mysql plugin: Removed broken report `%s' from instance `%s'.",
                report->name, db->instance);
        llist_remove(source->reports, le);
      }

      le = next;
    }

    if (llist_size(source->reports) > 0) {
      if (source->decl->db_init_cb) {
        ret = source->decl->db_init_cb(db, source->reports, &source->userdata);
        if (ret != 0) {
          // TODO: Remove XXX
          ERROR("mysql plugin: Failed to init source XXX for instance `%s'.",
                db->instance);
        }
      }
      if (ret == 0) {
        prev = source;
        continue;
      }
    };

    /* Remove the source from database report sources */
    if (prev)
      prev->next = source->next;
    else
      db->reportsources = source->next;

    mysql_db_report_source_free(source);
  }
  if (prev == NULL) {
    WARNING("mysql plugin: No reports are enabled for database `%s'.",
            db->instance);
    return -1;
  }

  return 0;
}

static int c_mysql_read_sources(user_data_t *ud) {
  mysql_database_t *db;

  if ((ud == NULL) || (ud->data == NULL)) {
    ERROR("mysql plugin: mysql_read: Invalid user data.");
    return (-1);
  }

  db = (mysql_database_t *)ud->data;

  /* An error message will have been printed in this case */
  if (getconnection(db) == NULL)
    return (-1);

  for (mysql_db_report_source_t *source = db->reportsources; source;
       source = source->next) {
    int ret = source->decl->db_read_cb(db, source->reports, source->userdata);
    if (ret != 0) {
      // TODO: Remove XXX ?
      ERROR("mysql plugin: mysql_read: Source XXX failed for instance `%s'.",
            db->instance);
    }
  }

  DEBUG("mysql plugin: mysql_read: Done all the work.");
  return (0);
} /* int c_mysql_read_sources */

static int mysql_enable_report(mysql_database_t *db, const char *name) {
  mysql_report_source_t *g_source = NULL;
  mysql_report_t *g_report = NULL;

  mysql_find_report_by_name(name, &g_source, &g_report);
  if (g_source == NULL || g_report == NULL) {
    ERROR("mysql plugin: Report `%s' not found. Failed to enable report.",
          name);
    return -1;
  }

  mysql_db_report_source_t *source = NULL;
  for (source = db->reportsources; source; source = source->next) {
    if (source->decl == g_source->decl)
      break;
  }
  if (source == NULL) {
    ERROR("mysql plugin: mysql_enable_report: Internal error.");
    return -1;
  }

  llentry_t *le = llist_head(source->reports);
  while (le != NULL) {
    if (strcasecmp(le->key, name) == 0) {
      // TODO: Logging
      DEBUG("mysql plugin: mysql_enable_report: Report `%s' already enabled "
            "for instance `%s'.",
            name, db->instance);
      return 0;
    }
    le = le->next;
  };

  llentry_t *newle = llentry_create(g_report->name, g_report);
  if (newle == NULL) {
    ERROR("mysql plugin: mysql_enable_report: llentry_create failed.");
    return -1;
  }

  llist_append(source->reports, newle);

  return 0;
} /* int mysql_enable_report */

static int mysql_disable_report(mysql_database_t *db, const char *name) {
  mysql_db_report_source_t *source = NULL;
  llentry_t *le = NULL;

  for (source = db->reportsources; source; source = source->next) {
    le = llist_head(source->reports);
    while (le != NULL) {
      if (strcasecmp(le->key, name) == 0)
        break;

      le = le->next;
    }
    if (le != NULL)
      break;
  };

  if (le == NULL) {
    // TODO: Logging
    DEBUG("mysql plugin: mysql_disable_report: No report `%s' found in "
          "instance `%s'.",
          name, db->instance);
    return 0;
  }

  llist_remove(source->reports, le);

  return 0;
} /* int c_mysql_disable_report */

static void mysql_database_free(void *arg) /* {{{ */
{
  mysql_database_t *db;

  DEBUG("mysql plugin: mysql_database_free (arg = %p);", arg);

  db = arg;

  if (db == NULL)
    return;

  for (mysql_db_report_source_t *source = db->reportsources; source; /* */) {
    mysql_db_report_source_t *next = source->next;

    if (source->decl->db_destroy_cb != NULL)
      source->decl->db_destroy_cb(db, source->reports, source->userdata);

    mysql_db_report_source_free(source);
    source = next;
  };

  if (db->con != NULL)
    mysql_close(db->con);

  sfree(db->alias);
  sfree(db->host);
  sfree(db->user);
  sfree(db->pass);
  sfree(db->socket);
  sfree(db->instance);
  sfree(db->database);
  sfree(db->key);
  sfree(db->cert);
  sfree(db->ca);
  sfree(db->capath);
  sfree(db->cipher);
  sfree(db);
} /* }}} void mysql_database_free */

static int mysql_config_database(oconfig_item_t *ci) /* {{{ */
{
  mysql_database_t *db;
  int status = 0;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("mysql plugin: The `Database' block "
            "needs exactly one string argument.");
    return (-1);
  }

  db = calloc(1, sizeof(*db));
  if (db == NULL) {
    ERROR("mysql plugin: calloc failed.");
    return (-1);
  }

  /* initialize all the pointers */
  db->alias = NULL;
  db->host = NULL;
  db->user = NULL;
  db->pass = NULL;
  db->database = NULL;
  db->key = NULL;
  db->cert = NULL;
  db->ca = NULL;
  db->capath = NULL;
  db->cipher = NULL;

  db->socket = NULL;
  db->con = NULL;
  db->timeout = 0;

  /* trigger a notification, if it's not running */
  db->slave_io_running = 1;
  db->slave_sql_running = 1;

  status = cf_util_get_string(ci, &db->instance);
  if (status != 0) {
    sfree(db);
    return (status);
  }
  assert(db->instance != NULL);

  /* Add all Report Sources to Database */
  status = c_mysql_db_add_sources(db);
  if (status != 0) {
    mysql_database_free(db);
    return status;
  };

  /* Fill the `mysql_database_t' structure.. */
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Alias", child->key) == 0)
      status = cf_util_get_string(child, &db->alias);
    else if (strcasecmp("Host", child->key) == 0)
      status = cf_util_get_string(child, &db->host);
    else if (strcasecmp("User", child->key) == 0)
      status = cf_util_get_string(child, &db->user);
    else if (strcasecmp("Password", child->key) == 0)
      status = cf_util_get_string(child, &db->pass);
    else if (strcasecmp("Port", child->key) == 0) {
      status = cf_util_get_port_number(child);
      if (status > 0) {
        db->port = status;
        status = 0;
      }
    } else if (strcasecmp("Socket", child->key) == 0)
      status = cf_util_get_string(child, &db->socket);
    else if (strcasecmp("Database", child->key) == 0)
      status = cf_util_get_string(child, &db->database);
    else if (strcasecmp("SSLKey", child->key) == 0)
      status = cf_util_get_string(child, &db->key);
    else if (strcasecmp("SSLCert", child->key) == 0)
      status = cf_util_get_string(child, &db->cert);
    else if (strcasecmp("SSLCA", child->key) == 0)
      status = cf_util_get_string(child, &db->ca);
    else if (strcasecmp("SSLCAPath", child->key) == 0)
      status = cf_util_get_string(child, &db->capath);
    else if (strcasecmp("SSLCipher", child->key) == 0)
      status = cf_util_get_string(child, &db->cipher);
    else if (strcasecmp("ConnectTimeout", child->key) == 0)
      status = cf_util_get_int(child, &db->timeout);
    else if (strcasecmp("MasterStats", child->key) == 0)
      status = cf_util_get_boolean(child, &db->master_stats);
    else if (strcasecmp("SlaveStats", child->key) == 0)
      status = cf_util_get_boolean(child, &db->slave_stats);
    else if (strcasecmp("SlaveNotifications", child->key) == 0)
      status = cf_util_get_boolean(child, &db->slave_notif);
    else if (strcasecmp("InnodbStats", child->key) == 0)
      status = cf_util_get_boolean(child, &db->innodb_stats);
    else if (strcasecmp("WsrepStats", child->key) == 0)
      status = cf_util_get_boolean(child, &db->wsrep_stats);
    else if (strcasecmp("Report", child->key) == 0) {
      char *name;
      status = cf_util_get_string(child, &name);
      if (status == 0) {
        status = mysql_enable_report(db, name);
        sfree(name);
      }
    } else if (strcasecmp("DisableReport", child->key) == 0) {
      char *report;
      status = cf_util_get_string(child, &report);
      if (status == 0) {
        status = mysql_disable_report(db, report);
        sfree(report);
      }
    } else {
      WARNING("mysql plugin: Option `%s' not allowed here.", child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status != 0) {
    ERROR("mysql plugin: Error in database `%s' configuration. Skipped it.",
          db->instance);
    mysql_database_free(db);
    return status;
  }

  if (databases_last == NULL) {
    databases_first = databases_last = db;
  } else {
    databases_last->next = db;
    databases_last = db;
  }

  return (0);
} /* }}} int mysql_config_database */

static int mysql_config(oconfig_item_t *ci) /* {{{ */
{
  if (ci == NULL)
    return (EINVAL);

  // Preinit
  // TODO: Move to function
  // TODO: Remove check 'report_sources == NULL' so other modules can touch
  // that.
  if (report_sources == NULL) {
    int status = 0;

    mysql_report_source_t *last = NULL;
    for (size_t i = 0; i < STATIC_ARRAY_SIZE(report_sources_decl); i++) {
      mysql_report_source_t *source = calloc(1, sizeof(mysql_report_source_t));
      if (source == NULL) {
        ERROR("mysql plugin: calloc failed.");
        status = -1;
        break;
      };

      source->decl = &report_sources_decl[i];

      source->reports = llist_create();
      if (source->reports == NULL) {
        ERROR("mysql plugin: list creation failed.");
        status = -1;
        break;
      }

      if (source->decl->default_reports) {
        status = source->decl->default_reports(source->reports);
        if (status != 0) {
          //TODO: XXX
          ERROR("mysql plugin: default_reports failed for id %zu.", i);
          break;
        }
      }

      if (last == NULL)
        report_sources = source;
      else
        last->next = source;

      last = source;
    };

    if (status != 0) {
      // TODO: Add shutdown callback + add free-ing of report_sources;
      return status;
    }
  }

  /* Fill the `mysql_database_t' structure.. */
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Database", child->key) == 0)
      mysql_config_database(child);
    else {
      int found = 0;
      for (mysql_report_source_t *g_source = report_sources; g_source;
           g_source = g_source->next) {

        if (g_source->decl->config_cb == NULL)
          continue;

        if (strcasecmp(g_source->decl->option_name, child->key) == 0) {
          found = 1;
          // TODO: check ret
          g_source->decl->config_cb(child, g_source->reports);
          break;
        };
      };
      if (!found)
        // TODO: Restore loglevel
        ERROR("mysql plugin: Option \"%s\" not allowed here.", child->key);
    }
  }

  return (0);
} /* }}} int mysql_config */

/* }}} End of configuration handling functions */

static int c_mysql_init() {
  for (mysql_report_source_t *g_source = report_sources; g_source;
       g_source = g_source->next) {

    if (g_source->decl->source_init_cb == NULL)
      continue;

    // TODO: check ret
    g_source->decl->source_init_cb(g_source->reports);
  }

  if (databases_first == NULL)
    return -1;

  for (mysql_database_t *db = databases_first; db; db = db->next) {
    int ret = c_mysql_db_init_sources(db);

    if (ret != 0) {
      mysql_database_free(db);
      continue;
    }

    /* If all went well, register this database for reading */
    char cb_name[DATA_MAX_NAME_LEN];

    DEBUG("mysql plugin: Registering new read callback: %s",
          (db->database != NULL) ? db->database : "<default>");

    if (db->instance != NULL)
      ssnprintf(cb_name, sizeof(cb_name), "mysql-%s", db->instance);
    else
      sstrncpy(cb_name, "mysql", sizeof(cb_name));

    user_data_t ud = {.data = db, .free_func = mysql_database_free};

    plugin_register_complex_read(/* group = */ NULL, cb_name,
                                 c_mysql_read_sources,
                                 /* interval = */ 0, &ud);
  }

  /* No future use of these variables */
  databases_first = databases_last = NULL;

  return 0;
} /* int c_mysql_init */

static int c_mysql_shutdown() {

  for (mysql_report_source_t *g_source = report_sources; g_source; /* */) {
    mysql_report_source_t *next = g_source->next;

    // if (g_source->decl->source_shutdown_cb != NULL) {
    //  g_source->decl->source_shutdown_cb(g_source->reports);
    //}

    llentry_t *le = llist_head(g_source->reports);
    while (le != NULL) {
      mysql_report_t *report = le->value;

      if (g_source->decl->config_free != NULL)
        g_source->decl->config_free(report->config);

      sfree(report->name);
      sfree(report);

      le = le->next;
    }
    llist_destroy(g_source->reports);
    sfree(g_source);
    g_source = next;
  }
  return 0;
} /* int c_mysql_shutdown */

static MYSQL *getconnection(mysql_database_t *db) {
  const char *cipher;

  if (db->is_connected) {
    int status;

    status = mysql_ping(db->con);
    if (status == 0)
      return (db->con);

    WARNING("mysql plugin: Lost connection to instance \"%s\": %s",
            db->instance, mysql_error(db->con));
  }
  db->is_connected = 0;

  if (db->con == NULL) {
    db->con = mysql_init(NULL);
    if (db->con == NULL) {
      ERROR("mysql plugin: mysql_init failed: %s", mysql_error(db->con));
      return (NULL);
    }
  }

  /* Configure TCP connect timeout (default: 0) */
  db->con->options.connect_timeout = db->timeout;

  mysql_ssl_set(db->con, db->key, db->cert, db->ca, db->capath, db->cipher);

  if (mysql_real_connect(db->con, db->host, db->user, db->pass, db->database,
                         db->port, db->socket, 0) == NULL) {
    ERROR("mysql plugin: Failed to connect to database %s "
          "at server %s: %s",
          (db->database != NULL) ? db->database : "<none>",
          (db->host != NULL) ? db->host : "localhost", mysql_error(db->con));
    return (NULL);
  }

  cipher = mysql_get_ssl_cipher(db->con);

  INFO("mysql plugin: Successfully connected to database %s "
       "at server %s with cipher %s "
       "(server version: %s, protocol version: %d) ",
       (db->database != NULL) ? db->database : "<none>",
       mysql_get_host_info(db->con), (cipher != NULL) ? cipher : "<none>",
       mysql_get_server_info(db->con), mysql_get_proto_info(db->con));

  db->is_connected = 1;
  return (db->con);
} /* static MYSQL *getconnection (mysql_database_t *db) */

static void set_host(mysql_database_t *db, char *buf, size_t buflen) {
  if (db->alias)
    sstrncpy(buf, db->alias, buflen);
  else if ((db->host == NULL) || (strcmp("", db->host) == 0) ||
           (strcmp("127.0.0.1", db->host) == 0) ||
           (strcmp("localhost", db->host) == 0))
    sstrncpy(buf, hostname_g, buflen);
  else
    sstrncpy(buf, db->host, buflen);
} /* void set_host */

void submit(const char *type, const char *type_instance, value_t *values,
            size_t values_len, mysql_database_t *db) {
  value_list_t vl = VALUE_LIST_INIT;

  vl.values = values;
  vl.values_len = values_len;

  set_host(db, vl.host, sizeof(vl.host));

  sstrncpy(vl.plugin, "mysql", sizeof(vl.plugin));

  /* Assured by "mysql_config_database" */
  assert(db->instance != NULL);
  sstrncpy(vl.plugin_instance, db->instance, sizeof(vl.plugin_instance));

  sstrncpy(vl.type, type, sizeof(vl.type));
  if (type_instance != NULL)
    sstrncpy(vl.type_instance, type_instance, sizeof(vl.type_instance));

  plugin_dispatch_values(&vl);
} /* submit */

void counter_submit(const char *type, const char *type_instance, derive_t value,
                    mysql_database_t *db) {
  value_t values[1];

  values[0].derive = value;
  submit(type, type_instance, values, STATIC_ARRAY_SIZE(values), db);
} /* void counter_submit */

void gauge_submit(const char *type, const char *type_instance, gauge_t value,
                  mysql_database_t *db) {
  value_t values[1];

  values[0].gauge = value;
  submit(type, type_instance, values, STATIC_ARRAY_SIZE(values), db);
} /* void gauge_submit */

void derive_submit(const char *type, const char *type_instance, derive_t value,
                   mysql_database_t *db) {
  value_t values[1];

  values[0].derive = value;
  submit(type, type_instance, values, STATIC_ARRAY_SIZE(values), db);
} /* void derive_submit */

static void traffic_submit(derive_t rx, derive_t tx, mysql_database_t *db) {
  value_t values[2];

  values[0].derive = rx;
  values[1].derive = tx;

  submit("mysql_octets", NULL, values, STATIC_ARRAY_SIZE(values), db);
} /* void traffic_submit */

MYSQL_RES *exec_query(MYSQL *con, const char *query) {
  MYSQL_RES *res;

  int query_len = strlen(query);

  if (mysql_real_query(con, query, query_len)) {
    ERROR("mysql plugin: Failed to execute query: %s", mysql_error(con));
    INFO("mysql plugin: SQL query was: %s", query);
    return (NULL);
  }

  res = mysql_store_result(con);
  if (res == NULL) {
    ERROR("mysql plugin: Failed to store query result: %s", mysql_error(con));
    INFO("mysql plugin: SQL query was: %s", query);
    return (NULL);
  }

  return (res);
} /* exec_query */

static int mysql_read_master_stats(mysql_database_t *db, MYSQL *con) {
  MYSQL_RES *res;
  MYSQL_ROW row;

  const char *query;
  int field_num;
  unsigned long long position;

  query = "SHOW MASTER STATUS";

  res = exec_query(con, query);
  if (res == NULL)
    return (-1);

  row = mysql_fetch_row(res);
  if (row == NULL) {
    ERROR("mysql plugin: Failed to get master statistics: "
          "`%s' did not return any rows.",
          query);
    mysql_free_result(res);
    return (-1);
  }

  field_num = mysql_num_fields(res);
  if (field_num < 2) {
    ERROR("mysql plugin: Failed to get master statistics: "
          "`%s' returned less than two columns.",
          query);
    mysql_free_result(res);
    return (-1);
  }

  position = atoll(row[1]);
  counter_submit("mysql_log_position", "master-bin", position, db);

  row = mysql_fetch_row(res);
  if (row != NULL)
    WARNING("mysql plugin: `%s' returned more than one row - "
            "ignoring further results.",
            query);

  mysql_free_result(res);

  return (0);
} /* mysql_read_master_stats */

static int mysql_read_slave_stats(mysql_database_t *db, MYSQL *con) {
  MYSQL_RES *res;
  MYSQL_ROW row;

  const char *query;
  int field_num;

  /* WTF? libmysqlclient does not seem to provide any means to
   * translate a column name to a column index ... :-/ */
  const int READ_MASTER_LOG_POS_IDX = 6;
  const int SLAVE_IO_RUNNING_IDX = 10;
  const int SLAVE_SQL_RUNNING_IDX = 11;
  const int EXEC_MASTER_LOG_POS_IDX = 21;
  const int SECONDS_BEHIND_MASTER_IDX = 32;

  query = "SHOW SLAVE STATUS";

  res = exec_query(con, query);
  if (res == NULL)
    return (-1);

  row = mysql_fetch_row(res);
  if (row == NULL) {
    ERROR("mysql plugin: Failed to get slave statistics: "
          "`%s' did not return any rows.",
          query);
    mysql_free_result(res);
    return (-1);
  }

  field_num = mysql_num_fields(res);
  if (field_num < 33) {
    ERROR("mysql plugin: Failed to get slave statistics: "
          "`%s' returned less than 33 columns.",
          query);
    mysql_free_result(res);
    return (-1);
  }

  if (db->slave_stats) {
    unsigned long long counter;
    double gauge;

    counter = atoll(row[READ_MASTER_LOG_POS_IDX]);
    counter_submit("mysql_log_position", "slave-read", counter, db);

    counter = atoll(row[EXEC_MASTER_LOG_POS_IDX]);
    counter_submit("mysql_log_position", "slave-exec", counter, db);

    if (row[SECONDS_BEHIND_MASTER_IDX] != NULL) {
      gauge = atof(row[SECONDS_BEHIND_MASTER_IDX]);
      gauge_submit("time_offset", NULL, gauge, db);
    }
  }

  if (db->slave_notif) {
    notification_t n = {0,  cdtime(),      "", "",  "mysql",
                        "", "time_offset", "", NULL};

    char *io, *sql;

    io = row[SLAVE_IO_RUNNING_IDX];
    sql = row[SLAVE_SQL_RUNNING_IDX];

    set_host(db, n.host, sizeof(n.host));

    /* Assured by "mysql_config_database" */
    assert(db->instance != NULL);
    sstrncpy(n.plugin_instance, db->instance, sizeof(n.plugin_instance));

    if (((io == NULL) || (strcasecmp(io, "yes") != 0)) &&
        (db->slave_io_running)) {
      n.severity = NOTIF_WARNING;
      ssnprintf(n.message, sizeof(n.message),
                "slave I/O thread not started or not connected to master");
      plugin_dispatch_notification(&n);
      db->slave_io_running = 0;
    } else if (((io != NULL) && (strcasecmp(io, "yes") == 0)) &&
               (!db->slave_io_running)) {
      n.severity = NOTIF_OKAY;
      ssnprintf(n.message, sizeof(n.message),
                "slave I/O thread started and connected to master");
      plugin_dispatch_notification(&n);
      db->slave_io_running = 1;
    }

    if (((sql == NULL) || (strcasecmp(sql, "yes") != 0)) &&
        (db->slave_sql_running)) {
      n.severity = NOTIF_WARNING;
      ssnprintf(n.message, sizeof(n.message), "slave SQL thread not started");
      plugin_dispatch_notification(&n);
      db->slave_sql_running = 0;
    } else if (((sql != NULL) && (strcasecmp(sql, "yes") == 0)) &&
               (!db->slave_sql_running)) {
      n.severity = NOTIF_OKAY;
      ssnprintf(n.message, sizeof(n.message), "slave SQL thread started");
      plugin_dispatch_notification(&n);
      db->slave_sql_running = 1;
    }
  }

  row = mysql_fetch_row(res);
  if (row != NULL)
    WARNING("mysql plugin: `%s' returned more than one row - "
            "ignoring further results.",
            query);

  mysql_free_result(res);

  return (0);
} /* mysql_read_slave_stats */

static int mysql_read_innodb_stats(mysql_database_t *db, MYSQL *con) {
  MYSQL_RES *res;
  MYSQL_ROW row;

  const char *query;
  struct {
    const char *key;
    const char *type;
    int ds_type;
  } metrics[] = {
      {"metadata_mem_pool_size", "bytes", DS_TYPE_GAUGE},
      {"lock_deadlocks", "mysql_locks", DS_TYPE_DERIVE},
      {"lock_timeouts", "mysql_locks", DS_TYPE_DERIVE},
      {"lock_row_lock_current_waits", "mysql_locks", DS_TYPE_DERIVE},
      {"buffer_pool_size", "bytes", DS_TYPE_GAUGE},

      {"os_log_bytes_written", "operations", DS_TYPE_DERIVE},
      {"os_log_pending_fsyncs", "operations", DS_TYPE_DERIVE},
      {"os_log_pending_writes", "operations", DS_TYPE_DERIVE},

      {"trx_rseg_history_len", "gauge", DS_TYPE_GAUGE},

      {"adaptive_hash_searches", "operations", DS_TYPE_DERIVE},

      {"file_num_open_files", "gauge", DS_TYPE_GAUGE},

      {"ibuf_merges_insert", "operations", DS_TYPE_DERIVE},
      {"ibuf_merges_delete_mark", "operations", DS_TYPE_DERIVE},
      {"ibuf_merges_delete", "operations", DS_TYPE_DERIVE},
      {"ibuf_merges_discard_insert", "operations", DS_TYPE_DERIVE},
      {"ibuf_merges_discard_delete_mark", "operations", DS_TYPE_DERIVE},
      {"ibuf_merges_discard_delete", "operations", DS_TYPE_DERIVE},
      {"ibuf_merges_discard_merges", "operations", DS_TYPE_DERIVE},
      {"ibuf_size", "bytes", DS_TYPE_GAUGE},

      {"innodb_activity_count", "gauge", DS_TYPE_GAUGE},

      {"innodb_rwlock_s_spin_waits", "operations", DS_TYPE_DERIVE},
      {"innodb_rwlock_x_spin_waits", "operations", DS_TYPE_DERIVE},
      {"innodb_rwlock_s_spin_rounds", "operations", DS_TYPE_DERIVE},
      {"innodb_rwlock_x_spin_rounds", "operations", DS_TYPE_DERIVE},
      {"innodb_rwlock_s_os_waits", "operations", DS_TYPE_DERIVE},
      {"innodb_rwlock_x_os_waits", "operations", DS_TYPE_DERIVE},

      {"dml_reads", "operations", DS_TYPE_DERIVE},
      {"dml_inserts", "operations", DS_TYPE_DERIVE},
      {"dml_deletes", "operations", DS_TYPE_DERIVE},
      {"dml_updates", "operations", DS_TYPE_DERIVE},

      {NULL, NULL, 0}};

  query = "SELECT name, count, type FROM information_schema.innodb_metrics "
          "WHERE status = 'enabled'";

  res = exec_query(con, query);
  if (res == NULL)
    return (-1);

  while ((row = mysql_fetch_row(res))) {
    int i;
    char *key;
    unsigned long long val;

    key = row[0];
    val = atoll(row[1]);

    for (i = 0; metrics[i].key != NULL && strcmp(metrics[i].key, key) != 0; i++)
      ;

    if (metrics[i].key == NULL)
      continue;

    switch (metrics[i].ds_type) {
    case DS_TYPE_COUNTER:
      counter_submit(metrics[i].type, key, (counter_t)val, db);
      break;
    case DS_TYPE_GAUGE:
      gauge_submit(metrics[i].type, key, (gauge_t)val, db);
      break;
    case DS_TYPE_DERIVE:
      derive_submit(metrics[i].type, key, (derive_t)val, db);
      break;
    }
  }

  mysql_free_result(res);
  return (0);
}

static int mysql_read_wsrep_stats(mysql_database_t *db, MYSQL *con) {
  MYSQL_RES *res;
  MYSQL_ROW row;

  const char *query;
  struct {
    const char *key;
    const char *type;
    int ds_type;
  } metrics[] = {

      {"wsrep_apply_oooe", "operations", DS_TYPE_DERIVE},
      {"wsrep_apply_oool", "operations", DS_TYPE_DERIVE},
      {"wsrep_causal_reads", "operations", DS_TYPE_DERIVE},
      {"wsrep_commit_oooe", "operations", DS_TYPE_DERIVE},
      {"wsrep_commit_oool", "operations", DS_TYPE_DERIVE},
      {"wsrep_flow_control_recv", "operations", DS_TYPE_DERIVE},
      {"wsrep_flow_control_sent", "operations", DS_TYPE_DERIVE},
      {"wsrep_flow_control_paused", "operations", DS_TYPE_DERIVE},
      {"wsrep_local_bf_aborts", "operations", DS_TYPE_DERIVE},
      {"wsrep_local_cert_failures", "operations", DS_TYPE_DERIVE},
      {"wsrep_local_commits", "operations", DS_TYPE_DERIVE},
      {"wsrep_local_replays", "operations", DS_TYPE_DERIVE},
      {"wsrep_received", "operations", DS_TYPE_DERIVE},
      {"wsrep_replicated", "operations", DS_TYPE_DERIVE},

      {"wsrep_received_bytes", "total_bytes", DS_TYPE_DERIVE},
      {"wsrep_replicated_bytes", "total_bytes", DS_TYPE_DERIVE},

      {"wsrep_apply_window", "gauge", DS_TYPE_GAUGE},
      {"wsrep_commit_window", "gauge", DS_TYPE_GAUGE},

      {"wsrep_cluster_size", "gauge", DS_TYPE_GAUGE},
      {"wsrep_cert_deps_distance", "gauge", DS_TYPE_GAUGE},

      {"wsrep_local_recv_queue", "queue_length", DS_TYPE_GAUGE},
      {"wsrep_local_send_queue", "queue_length", DS_TYPE_GAUGE},

      {NULL, NULL, 0}

  };

  query = "SHOW GLOBAL STATUS LIKE 'wsrep_%'";

  res = exec_query(con, query);
  if (res == NULL)
    return (-1);

  row = mysql_fetch_row(res);
  if (row == NULL) {
    ERROR("mysql plugin: Failed to get wsrep statistics: "
          "`%s' did not return any rows.",
          query);
    mysql_free_result(res);
    return (-1);
  }

  while ((row = mysql_fetch_row(res))) {
    int i;
    char *key;
    unsigned long long val;

    key = row[0];
    val = atoll(row[1]);

    for (i = 0; metrics[i].key != NULL && strcmp(metrics[i].key, key) != 0; i++)
      ;

    if (metrics[i].key == NULL)
      continue;

    switch (metrics[i].ds_type) {
    case DS_TYPE_GAUGE:
      gauge_submit(metrics[i].type, key, (gauge_t)val, db);
      break;
    case DS_TYPE_DERIVE:
      derive_submit(metrics[i].type, key, (derive_t)val, db);
      break;
    }
  }

  mysql_free_result(res);
  return (0);
} /* mysql_read_wsrep_stats */

static int mysql_compatible_read(mysql_database_t *db, const llist_t *reports,
                                 void *userdata) {
  MYSQL *con;
  MYSQL_RES *res;
  MYSQL_ROW row;
  const char *query;

  derive_t qcache_hits = 0;
  derive_t qcache_inserts = 0;
  derive_t qcache_not_cached = 0;
  derive_t qcache_lowmem_prunes = 0;
  gauge_t qcache_queries_in_cache = NAN;

  gauge_t threads_running = NAN;
  gauge_t threads_connected = NAN;
  gauge_t threads_cached = NAN;
  derive_t threads_created = 0;

  unsigned long long traffic_incoming = 0ULL;
  unsigned long long traffic_outgoing = 0ULL;
  unsigned long mysql_version = 0ULL;

  con = db->con;
  mysql_version = mysql_get_server_version(con);

  query = "SHOW STATUS";
  if (mysql_version >= 50002)
    query = "SHOW GLOBAL STATUS";

  res = exec_query(con, query);
  if (res == NULL)
    return (-1);

  while ((row = mysql_fetch_row(res))) {
    char *key;
    unsigned long long val;

    key = row[0];
    val = atoll(row[1]);

    if (strncmp(key, "Com_", strlen("Com_")) == 0) {
      if (val == 0ULL)
        continue;

      /* Ignore `prepared statements' */
      if (strncmp(key, "Com_stmt_", strlen("Com_stmt_")) != 0)
        counter_submit("mysql_commands", key + strlen("Com_"), val, db);
    } else if (strncmp(key, "Handler_", strlen("Handler_")) == 0) {
      if (val == 0ULL)
        continue;

      counter_submit("mysql_handler", key + strlen("Handler_"), val, db);
    } else if (strncmp(key, "Qcache_", strlen("Qcache_")) == 0) {
      if (strcmp(key, "Qcache_hits") == 0)
        qcache_hits = (derive_t)val;
      else if (strcmp(key, "Qcache_inserts") == 0)
        qcache_inserts = (derive_t)val;
      else if (strcmp(key, "Qcache_not_cached") == 0)
        qcache_not_cached = (derive_t)val;
      else if (strcmp(key, "Qcache_lowmem_prunes") == 0)
        qcache_lowmem_prunes = (derive_t)val;
      else if (strcmp(key, "Qcache_queries_in_cache") == 0)
        qcache_queries_in_cache = (gauge_t)val;
    } else if (strncmp(key, "Bytes_", strlen("Bytes_")) == 0) {
      if (strcmp(key, "Bytes_received") == 0)
        traffic_incoming += val;
      else if (strcmp(key, "Bytes_sent") == 0)
        traffic_outgoing += val;
    } else if (strncmp(key, "Threads_", strlen("Threads_")) == 0) {
      if (strcmp(key, "Threads_running") == 0)
        threads_running = (gauge_t)val;
      else if (strcmp(key, "Threads_connected") == 0)
        threads_connected = (gauge_t)val;
      else if (strcmp(key, "Threads_cached") == 0)
        threads_cached = (gauge_t)val;
      else if (strcmp(key, "Threads_created") == 0)
        threads_created = (derive_t)val;
    } else if (strncmp(key, "Table_locks_", strlen("Table_locks_")) == 0) {
      counter_submit("mysql_locks", key + strlen("Table_locks_"), val, db);
    } else if (db->innodb_stats &&
               strncmp(key, "Innodb_", strlen("Innodb_")) == 0) {
      /* buffer pool */
      if (strcmp(key, "Innodb_buffer_pool_pages_data") == 0)
        gauge_submit("mysql_bpool_pages", "data", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_pages_dirty") == 0)
        gauge_submit("mysql_bpool_pages", "dirty", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_pages_flushed") == 0)
        counter_submit("mysql_bpool_counters", "pages_flushed", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_pages_free") == 0)
        gauge_submit("mysql_bpool_pages", "free", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_pages_misc") == 0)
        gauge_submit("mysql_bpool_pages", "misc", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_pages_total") == 0)
        gauge_submit("mysql_bpool_pages", "total", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_read_ahead_rnd") == 0)
        counter_submit("mysql_bpool_counters", "read_ahead_rnd", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_read_ahead") == 0)
        counter_submit("mysql_bpool_counters", "read_ahead", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_read_ahead_evicted") == 0)
        counter_submit("mysql_bpool_counters", "read_ahead_evicted", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_read_requests") == 0)
        counter_submit("mysql_bpool_counters", "read_requests", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_reads") == 0)
        counter_submit("mysql_bpool_counters", "reads", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_wait_free") == 0)
        counter_submit("mysql_bpool_counters", "wait_free", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_write_requests") == 0)
        counter_submit("mysql_bpool_counters", "write_requests", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_bytes_data") == 0)
        gauge_submit("mysql_bpool_bytes", "data", val, db);
      else if (strcmp(key, "Innodb_buffer_pool_bytes_dirty") == 0)
        gauge_submit("mysql_bpool_bytes", "dirty", val, db);

      /* data */
      if (strcmp(key, "Innodb_data_fsyncs") == 0)
        counter_submit("mysql_innodb_data", "fsyncs", val, db);
      else if (strcmp(key, "Innodb_data_read") == 0)
        counter_submit("mysql_innodb_data", "read", val, db);
      else if (strcmp(key, "Innodb_data_reads") == 0)
        counter_submit("mysql_innodb_data", "reads", val, db);
      else if (strcmp(key, "Innodb_data_writes") == 0)
        counter_submit("mysql_innodb_data", "writes", val, db);
      else if (strcmp(key, "Innodb_data_written") == 0)
        counter_submit("mysql_innodb_data", "written", val, db);

      /* double write */
      else if (strcmp(key, "Innodb_dblwr_writes") == 0)
        counter_submit("mysql_innodb_dblwr", "writes", val, db);
      else if (strcmp(key, "Innodb_dblwr_pages_written") == 0)
        counter_submit("mysql_innodb_dblwr", "written", val, db);
      else if (strcmp(key, "Innodb_dblwr_page_size") == 0)
        gauge_submit("mysql_innodb_dblwr", "page_size", val, db);

      /* log */
      else if (strcmp(key, "Innodb_log_waits") == 0)
        counter_submit("mysql_innodb_log", "waits", val, db);
      else if (strcmp(key, "Innodb_log_write_requests") == 0)
        counter_submit("mysql_innodb_log", "write_requests", val, db);
      else if (strcmp(key, "Innodb_log_writes") == 0)
        counter_submit("mysql_innodb_log", "writes", val, db);
      else if (strcmp(key, "Innodb_os_log_fsyncs") == 0)
        counter_submit("mysql_innodb_log", "fsyncs", val, db);
      else if (strcmp(key, "Innodb_os_log_written") == 0)
        counter_submit("mysql_innodb_log", "written", val, db);

      /* pages */
      else if (strcmp(key, "Innodb_pages_created") == 0)
        counter_submit("mysql_innodb_pages", "created", val, db);
      else if (strcmp(key, "Innodb_pages_read") == 0)
        counter_submit("mysql_innodb_pages", "read", val, db);
      else if (strcmp(key, "Innodb_pages_written") == 0)
        counter_submit("mysql_innodb_pages", "written", val, db);

      /* row lock */
      else if (strcmp(key, "Innodb_row_lock_time") == 0)
        counter_submit("mysql_innodb_row_lock", "time", val, db);
      else if (strcmp(key, "Innodb_row_lock_waits") == 0)
        counter_submit("mysql_innodb_row_lock", "waits", val, db);

      /* rows */
      else if (strcmp(key, "Innodb_rows_deleted") == 0)
        counter_submit("mysql_innodb_rows", "deleted", val, db);
      else if (strcmp(key, "Innodb_rows_inserted") == 0)
        counter_submit("mysql_innodb_rows", "inserted", val, db);
      else if (strcmp(key, "Innodb_rows_read") == 0)
        counter_submit("mysql_innodb_rows", "read", val, db);
      else if (strcmp(key, "Innodb_rows_updated") == 0)
        counter_submit("mysql_innodb_rows", "updated", val, db);
    } else if (strncmp(key, "Select_", strlen("Select_")) == 0) {
      counter_submit("mysql_select", key + strlen("Select_"), val, db);
    } else if (strncmp(key, "Sort_", strlen("Sort_")) == 0) {
      if (strcmp(key, "Sort_merge_passes") == 0)
        counter_submit("mysql_sort_merge_passes", NULL, val, db);
      else if (strcmp(key, "Sort_rows") == 0)
        counter_submit("mysql_sort_rows", NULL, val, db);
      else if (strcmp(key, "Sort_range") == 0)
        counter_submit("mysql_sort", "range", val, db);
      else if (strcmp(key, "Sort_scan") == 0)
        counter_submit("mysql_sort", "scan", val, db);

    } else if (strncmp(key, "Slow_queries", strlen("Slow_queries")) == 0) {
      counter_submit("mysql_slow_queries", NULL, val, db);
    }
  }
  mysql_free_result(res);
  res = NULL;

  if ((qcache_hits != 0) || (qcache_inserts != 0) || (qcache_not_cached != 0) ||
      (qcache_lowmem_prunes != 0)) {
    derive_submit("cache_result", "qcache-hits", qcache_hits, db);
    derive_submit("cache_result", "qcache-inserts", qcache_inserts, db);
    derive_submit("cache_result", "qcache-not_cached", qcache_not_cached, db);
    derive_submit("cache_result", "qcache-prunes", qcache_lowmem_prunes, db);

    gauge_submit("cache_size", "qcache", qcache_queries_in_cache, db);
  }

  if (threads_created != 0) {
    gauge_submit("threads", "running", threads_running, db);
    gauge_submit("threads", "connected", threads_connected, db);
    gauge_submit("threads", "cached", threads_cached, db);

    derive_submit("total_threads", "created", threads_created, db);
  }

  traffic_submit(traffic_incoming, traffic_outgoing, db);

  if (mysql_version >= 50600 && db->innodb_stats)
    mysql_read_innodb_stats(db, con);

  if (db->master_stats)
    mysql_read_master_stats(db, con);

  if ((db->slave_stats) || (db->slave_notif))
    mysql_read_slave_stats(db, con);

  if (db->wsrep_stats)
    mysql_read_wsrep_stats(db, con);

  return (0);
} /* int mysql_compatible_read */

static int mysql_compatible_reports(llist_t *reports) {
  mysql_report_t *report = mysql_add_report(reports, "compatible");
  if (report == NULL)
    return -1;

  report->def = 1;

  return 0;
} /* int mysql_compatible_reports */

void module_register(void) {
  plugin_register_complex_config("mysql", mysql_config);
  plugin_register_init("mysql", c_mysql_init);
  plugin_register_shutdown("mysql", c_mysql_shutdown);
} /* void module_register */
