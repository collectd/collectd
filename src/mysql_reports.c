#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_llist.h"

#include "mysql_plugin.h"

#include <limits.h>

#ifndef ULLONG_MAX
#define ULLONG_MAX 18446744073709551615ULL
#endif

enum elt_type_t {
  t_none = 0,
  t_prefix, /* Find variable by prefix */
  t_variable,
  t_ratio,
  t_delta_ratio,
  t_first_variable,
  t_next_variable /* Used for t_ratio, t_delta_ratio too */
};

/* Report configuration */
struct elt_s {
  enum elt_type_t elt_type;
  char *name; /* Name or prefix */
  char *ignore_prefix;

  char *type;
  char *type_instance;
  int ds_type;
};
typedef struct elt_s elt_t;

struct config_s {
  size_t elts_num;
  elt_t **elts;
};
typedef struct config_s config_t;

/* Database metrics */
struct metric_s {
  enum elt_type_t elt_type;
  char *name;
  char *ignore_prefix;

  char *type;
  char *type_instance;
  int ds_type;

  unsigned long long value;
  unsigned long long prev_value;
  unsigned int found;
};
typedef struct metric_s metric_t;

struct db_config_s {
  size_t metrics_num;
  metric_t *metrics;
};
typedef struct db_config_s db_config_t;

// TODO: remove fwd declaration
void mysql_reports_config_free(void *reportconfig);

static void mr_elt_free(elt_t *elt) {
  sfree(elt->name);
  sfree(elt->ignore_prefix);

  sfree(elt->type);
  sfree(elt->type_instance);

  sfree(elt);
} /* void mr_config_elt_free */

// clang-format off
/* Configuration example (Reported types are for example only):
 *
 * <Plugin mysql>
 *   <GlobalStatusReport "Statements">
 *     #Variable "VARIABLE" "TYPE" ["TYPE_INSTANCE"]
 *     #Used to report MySQL status variable as a metric with given type and
 *     #type instance (if specified).
 *     #
 *     Variable "Sort_rows"         "mysql_sort_rows"
 *     Variable "Sort_range"        "mysql_sort"       "range"
 *     Variable "Sort_scan"         "mysql_sort"       "scan"
 *     Variable "Slow_queries"      "mysql_slow_queries"
 *
 *     #Prefix "PREFIX" "TYPE" ["IGNORE_PREFIX"]
 *     #Used to report MySQL status variables, which match PREFIX, as a metric
 *     #with given type. Type instance is set from variable name with prefix
 *     #cut off.With use of IGNORE_PREFIX some variables can be skipped from
 *     #report.
 *     #
 *     Prefix "Com_" "mysql_commands" "Com_stmt_"
 *     Prefix "Select_" "mysql_select"
 *
 *     #VariablesRatio "VARIABLE_1" "VARIABLE_2" "TYPE" ["TYPE_INSTANCE"]
 *     #The reported value will be calculated as variables ratio.
 *     #VARIABLE_1 / VARIABLE_2
 *     #
 *     #Not a useful example.
 *     #VariablesRatio "Threadpool_idle_threads" "Threadpool_threads" "ratio"
 *
 *     #VariablesDeltaRatio "VARIABLE_1" "VARIABLE_2" "TYPE" ["TYPE_INSTANCE"]
 *     #The reported value will be calculated as a ratio of first variable
 *     #change to second variable change.
 *     #VARIABLE_1_DELTA / VARIABLE_2_DELTA
 *     #
 *     VariablesDeltaRatio "Key_reads" "Key_read_requests" "keycache_misses"
 *
 *     #TwoVariables "VARIABLE_1" "VARIABLE_2" "TYPE" ["TYPE_INSTANCE"]
 *     #Used to report complex type of two datasources.
 *     #
 *     TwoVariables "Bytes_received" "Bytes_sent" "mysql_octets"
 *     TwoVariables "Innodb_data_read" "Innodb_data_written" "disk_octets" "innodb"
 *   </GlobalStatusReport>
 *   ...
 * </Plugin>
 **/
// clang-format on

static int mr_config_add_variable(const char *reportname, oconfig_item_t *ci,
                                  config_t *config) {
  if ((ci->values_num < 2) || (ci->values_num > 3) ||
      (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING) ||
      ((ci->values_num == 3) && (ci->values[2].type != OCONFIG_TYPE_STRING))) {
    WARNING("mysql plugin: Report \"%s\": The `%s' option "
            "requires two or three string arguments.",
            reportname, ci->key);
    return -1;
  }

  elt_t *elt = calloc(1, sizeof(*elt));
  if (elt == NULL) {
    ERROR("mysql plugin: mr_config_add_variable: calloc failed.");
    return -1;
  }

  elt->elt_type = t_variable;

  elt->name = strdup(ci->values[0].value.string);
  if (elt->name == NULL) {
    ERROR("mysql plugin: mr_config_add_variable: strdup failed.");
    sfree(elt);
    return -1;
  }

  elt->type = strdup(ci->values[1].value.string);
  if (elt->type == NULL) {
    ERROR("mysql plugin: mr_config_add_variable: strdup failed.");
    sfree(elt->name);
    sfree(elt);
    return -1;
  }

  if (ci->values_num > 2) {
    elt->type_instance = strdup(ci->values[2].value.string);
    if (elt->type_instance == NULL) {
      ERROR("mysql plugin: mr_config_add_variable: strdup failed.");
      sfree(elt->name);
      sfree(elt->type);
      sfree(elt);
      return -1;
    }
  }

  elt_t **tmp =
      realloc(config->elts, sizeof(*config->elts) * (config->elts_num + 1));
  if (tmp == NULL) {
    ERROR("mysql plugin: mr_config_add_variable: realloc failed.");
    mr_elt_free(elt);
    return -1;
  }

  config->elts = tmp;
  config->elts[config->elts_num] = elt;
  config->elts_num++;

  return 0;
} /* int mr_config_add_variable */

static int mr_config_add_prefix(const char *reportname, oconfig_item_t *ci,
                                config_t *config) {
  if ((ci->values_num < 2) || (ci->values_num > 3) ||
      (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING) ||
      ((ci->values_num == 3) && (ci->values[2].type != OCONFIG_TYPE_STRING))) {
    WARNING("mysql plugin: Report \"%s\": The `%s' option "
            "requires two or three string arguments.",
            reportname, ci->key);
    return -1;
  }

  elt_t *elt = calloc(1, sizeof(*elt));
  if (elt == NULL) {
    ERROR("mysql plugin: mr_config_add_prefix: elt calloc failed.");
    return -1;
  }

  elt->elt_type = t_prefix;

  elt->name = strdup(ci->values[0].value.string);
  if (elt->name == NULL) {
    ERROR("mysql plugin: mr_config_add_prefix: strdup failed.");
    sfree(elt);
    return -1;
  }

  elt->type = strdup(ci->values[1].value.string);
  if (elt->type == NULL) {
    ERROR("mysql plugin: mr_config_add_prefix: strdup failed.");
    sfree(elt->name);
    sfree(elt);
    return -1;
  }

  if (ci->values_num > 2) {
    elt->ignore_prefix = strdup(ci->values[2].value.string);
    if (elt->ignore_prefix == NULL) {
      ERROR("mysql plugin: mr_config_add_prefix: strdup failed.");
      sfree(elt->type);
      sfree(elt->name);
      sfree(elt);
      return -1;
    }
  }

  elt_t **tmp =
      realloc(config->elts, sizeof(*config->elts) * (config->elts_num + 1));
  if (tmp == NULL) {
    ERROR("mysql plugin: mr_config_add_prefix: realloc failed.");
    mr_elt_free(elt);
    return -1;
  }

  config->elts = tmp;
  config->elts[config->elts_num] = elt;
  config->elts_num++;

  return 0;
} /* int mr_config_add_prefix */

static int mr_config_add_two_variables(const char *reportname,
                                       oconfig_item_t *ci, config_t *config,
                                       enum elt_type_t first_elt_type) {
  if ((ci->values_num < 3) || (ci->values_num > 4) ||
      (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING) ||
      ((ci->values_num == 3) && (ci->values[2].type != OCONFIG_TYPE_STRING))) {
    WARNING("mysql plugin: Report \"%s\": The `%s' option "
            "requires three or four string arguments.",
            reportname, ci->key);
    return -1;
  }

  elt_t *elt1 = calloc(1, sizeof(*elt1));
  elt_t *elt2 = calloc(1, sizeof(*elt2));
  if (elt1 == NULL || elt2 == NULL) {
    ERROR("mysql plugin: mr_config_add_two_variables: calloc failed.");
    sfree(elt1);
    sfree(elt2);
    return -1;
  }

  elt1->elt_type = first_elt_type;
  elt2->elt_type =
      t_next_variable; /* The second elem is always t_next_variable */

  elt1->name = strdup(ci->values[0].value.string);
  elt2->name = strdup(ci->values[1].value.string);
  if (elt1->name == NULL || elt2->name == NULL) {
    ERROR("mysql plugin: mr_config_add_two_variables: strdup failed.");
    sfree(elt1->name);
    sfree(elt2->name);
    sfree(elt1);
    sfree(elt2);
    return -1;
  }

  /* For second variable types from first variable are used */
  elt1->type = strdup(ci->values[2].value.string);
  if (elt1->type == NULL) {
    ERROR("mysql plugin: mr_config_add_two_variables: strdup failed.");
    sfree(elt1->name);
    sfree(elt2->name);
    sfree(elt1);
    sfree(elt2);
    return -1;
  }

  if (ci->values_num > 3) {
    elt1->type_instance = strdup(ci->values[3].value.string);

    if (elt1->type_instance == NULL) {
      ERROR("mysql plugin: mr_config_add_two_variables: strdup failed.");
      sfree(elt1->type);
      sfree(elt1->name);
      sfree(elt2->name);
      sfree(elt1);
      sfree(elt2);
      return -1;
    }
  }

  elt_t **tmp =
      realloc(config->elts, sizeof(*config->elts) * (config->elts_num + 2));
  if (tmp == NULL) {
    ERROR("mysql plugin: mr_config_add_two_variables: realloc failed.");
    mr_elt_free(elt1);
    mr_elt_free(elt2);
    return -1;
  }

  config->elts = tmp;
  config->elts[config->elts_num] = elt1;
  config->elts[config->elts_num + 1] = elt2;
  config->elts_num += 2;

  return 0;
} /* int mr_config_add_two_variables */

int mysql_reports_config(oconfig_item_t *ci, llist_t *reports) {
  if (ci == NULL || reports == NULL)
    return EINVAL;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("mysql plugin: The `%s' block "
            "requires exactly one string argument.",
            ci->key);
    return -1;
  }
  const char *reportname = ci->values[0].value.string;

  config_t *config = calloc(1, sizeof(*config));
  if (config == NULL) {
    ERROR("mysql plugin: mysql_reports_config: config calloc failed.");
    return -1;
  }

  /* Fill the configuration structure */
  int status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Variable", option->key) == 0) {
      status = mr_config_add_variable(reportname, option, config);
    } else if (strcasecmp("Prefix", option->key) == 0) {
      status = mr_config_add_prefix(reportname, option, config);
    } else if (strcasecmp("TwoVariables", option->key) == 0) {
      status = mr_config_add_two_variables(reportname, option, config,
                                           t_first_variable);
    } else if (strcasecmp("VariablesRatio", option->key) == 0) {
      status = mr_config_add_two_variables(reportname, option, config, t_ratio);
    } else if (strcasecmp("VariablesDeltaRatio", option->key) == 0) {
      status = mr_config_add_two_variables(reportname, option, config,
                                           t_delta_ratio);
    } else {
      WARNING("mysql plugin: Report \"%s\": Option `%s' not allowed here.",
              reportname, option->key);
      status = -1;
    }
  }

  if (status != 0) {
    mysql_reports_config_free(config);
    return status;
  }

  mysql_report_t *report = mysql_add_report(reports, reportname);
  if (report == NULL) {
    ERROR("mysql plugin: mysql_add_report failed for `%s'.", reportname);
    mysql_reports_config_free(config);
    return -1;
  }

  report->config = config;
  report->def = 1;

  return 0;
} /* int mysql_reports_config */

void mysql_reports_config_free(void *reportconfig) {
  config_t *config = reportconfig;

  if (config == NULL)
    return;

  for (size_t i = 0; i < config->elts_num; i++) {
    elt_t *elt = config->elts[i];
    mr_elt_free(elt);
  }

  sfree(config);
} /* void mysql_reports_config_free */

int mysql_reports_init(const llist_t *reports) {
  llentry_t *le = llist_head(reports);
  while (le != NULL) {
    mysql_report_t *report = le->value;
    config_t *config = report->config;

    for (int i = 0; i < config->elts_num; i++) {
      elt_t *elt = config->elts[i];

      if (elt->elt_type == t_next_variable)
        continue; /* It uses type from prev variable */

      if (elt->type == NULL) {
        ERROR("mysql plugin: Missing reported type in report `%s'.",
              report->name);
        report->broken = 1;
        continue;
      }

      const data_set_t *ds = plugin_get_ds(elt->type);
      if (ds == NULL) {
        ERROR("mysql plugin: Type `%s', used in report `%s', not defined.",
              elt->type, report->name);
        report->broken = 1;
        continue;
      }
      if (elt->elt_type == t_first_variable) {
        if (ds->ds_num != 2) {
          ERROR("mysql plugin: The type `%s', used in report `%s', should "
                "have one data source. (But %zu found)",
                elt->type, report->name, ds->ds_num);
          report->broken = 1;
          continue;
        }
      } else if (ds->ds_num != 1) {
        ERROR("mysql plugin: The type `%s', used in report `%s', should "
              "have one data source. (But %zu found)",
              elt->type, report->name, ds->ds_num);
        report->broken = 1;
        continue;
      }
      elt->ds_type = ds->ds->type;
    }
    le = le->next;
  }

  return 0;
} /* int mysql_reports_init */

// int mysql_reports_shutdown(const llist_t *reports) {
//  llentry_t *le = llist_head(reports);
//  while (le != NULL) {
//    mysql_report_t *report = le->value;
//
//    mysql_reports_config_free(report->config);
//    report->config = NULL;
//
//    le = le->next;
//  }
//}

int mysql_reports_db_init(mysql_database_t *db, const llist_t *reports,
                          void **userdata) {

  db_config_t *db_config = calloc(1, sizeof(*db_config));
  if (db_config == NULL) {
    ERROR("mysql plugin: mysql_reports_db_init: calloc failed.");
    return -1;
  }

  llentry_t *le = llist_head(reports);
  while (le != NULL) {
    mysql_report_t *report = le->value;
    config_t *config = report->config;

    metric_t *tmp = realloc(db_config->metrics,
                            sizeof(*db_config->metrics) *
                                (db_config->metrics_num + config->elts_num));
    if (tmp == NULL) {
      sfree(db_config->metrics);
      sfree(db_config);
      return -1;
    }
    db_config->metrics = tmp;

    for (int i = 0; i < config->elts_num; i++) {
      int j = i + db_config->metrics_num;

      db_config->metrics[j].elt_type = config->elts[i]->elt_type;
      db_config->metrics[j].name = config->elts[i]->name;
      db_config->metrics[j].ignore_prefix = config->elts[i]->ignore_prefix;

      db_config->metrics[j].type = config->elts[i]->type;
      db_config->metrics[j].type_instance = config->elts[i]->type_instance;
      db_config->metrics[j].ds_type = config->elts[i]->ds_type;

      /* Init other metric fields */
      db_config->metrics[j].found = 0;
      db_config->metrics[j].value = ULLONG_MAX;
      db_config->metrics[j].prev_value = ULLONG_MAX;
    }
    db_config->metrics_num += config->elts_num;

    le = le->next;
  }

  *userdata = db_config;

  return 0;
} /* int mysql_reports_db_init */

void mysql_reports_db_destroy(mysql_database_t *db, const llist_t *reports,
                              void *userdata) {
  db_config_t *db_config = userdata;
  sfree(db_config);
} /* void mysql_reports_db_destroy */

static void metric_submit(const metric_t *metric, const char *type_instance,
                          mysql_database_t *db) {
  switch (metric->ds_type) {
  case DS_TYPE_COUNTER:
    counter_submit(metric->type, type_instance, (counter_t)metric->value, db);
    break;
  case DS_TYPE_GAUGE:
    gauge_submit(metric->type, type_instance, (gauge_t)metric->value, db);
    break;
  case DS_TYPE_DERIVE:
    derive_submit(metric->type, type_instance, (derive_t)metric->value, db);
    break;
  }
} /* void metric_submit */

static int submit_query(mysql_database_t *db, db_config_t *db_config,
                        const char *query) {
  MYSQL *con = db->con;

  MYSQL_RES *res = exec_query(con, query);
  if (res == NULL)
    return -1;

  MYSQL_ROW row;
  while ((row = mysql_fetch_row(res))) {
    char *key = row[0];
    unsigned long long val = atoll(row[1]);

    for (int i = 0; i < db_config->metrics_num; i++) {
      metric_t *metric = db_config->metrics + i;

      if (metric->elt_type == t_prefix) {
        if (strncmp(key, metric->name, strlen(metric->name)) != 0)
          continue;

        if (metric->ignore_prefix != NULL &&
            strncmp(key, metric->ignore_prefix,
                    strlen(metric->ignore_prefix)) == 0)
          continue;

        metric->value = val;
        metric_submit(metric, key + strlen(metric->name), db);
        continue;
      }

      /* All other elt_types are compared by full variable name */
      if (strcmp(metric->name, key) == 0) {
        metric->value = val;
        metric->found = 1;
        continue;
      }
    }
  };
  mysql_free_result(res);

  for (int i = 0; i < db_config->metrics_num; i++) {
    metric_t *metric = db_config->metrics + i;

    /* This also skips t_prefix */
    if (metric->found == 0)
      continue;

    if (metric->elt_type == t_variable) {
      metric_submit(metric, metric->type_instance, db);
      metric->found = 0;
      continue;
    }

    /* Next elt should exist */
    assert(i < db_config->metrics_num - 1);
    /* t_next_variable elt should be skipped */
    assert(metric->elt_type != t_next_variable);

    metric_t *next_metric = metric + 1;
    assert(next_metric->elt_type == t_next_variable);
    i++;

    if (next_metric->found == 0) {
      metric->found = 0;
      continue;
    }

    metric->found = 0;
    next_metric->found = 0;

    if (metric->elt_type == t_first_variable) {
      value_t values[2];
      switch (metric->ds_type) {
      case DS_TYPE_COUNTER:
      case DS_TYPE_DERIVE:
        values[0].derive = (derive_t)metric->value;
        values[1].derive = (derive_t)next_metric->value;
        break;
      case DS_TYPE_GAUGE:
        values[0].gauge = (gauge_t)metric->value;
        values[1].gauge = (gauge_t)next_metric->value;
        break;
      }
      submit(metric->type, metric->type_instance, values,
             STATIC_ARRAY_SIZE(values), db);
    } else if (metric->elt_type == t_ratio) {
      gauge_t ratio = NAN;

      if (next_metric->value > 0)
        ratio = metric->value / next_metric->value;

      gauge_submit(metric->type, metric->type_instance, ratio, db);
    } else if (metric->elt_type == t_delta_ratio) {
      /* We expect constantly grown values.
       * Handle statistics reset, MySQL restart, etc.
       * Also handles ULLONG_MAX defaults.
       */
      if (metric->prev_value > metric->value ||
          next_metric->prev_value > next_metric->value) {
        metric->prev_value = metric->value;
        next_metric->prev_value = next_metric->value;
        continue;
      };

      long long d1 = metric->value - metric->prev_value;
      long long d2 = next_metric->value - next_metric->prev_value;

      metric->prev_value = metric->value;
      next_metric->prev_value = next_metric->value;

      if (d2 == 0) {
        if (d1 == 0)
          gauge_submit(metric->type, metric->type_instance, 0, db);
        else {
          ERROR("mysql plugin: Instance `%s': Delta between `%s' values is "
                "zero, while non-zero delta of `%s' values!",
                db->instance, next_metric->name, metric->name);
          gauge_submit(metric->type, metric->type_instance, NAN, db);
        }
        continue;
      }

      gauge_submit(metric->type, metric->type_instance, (gauge_t)(d1 / d2), db);
    } else {
      assert(0);
    };
  }
  return 0;
} /* int mr_submit_query */

int mysql_reports_status_read(mysql_database_t *db, const llist_t *reports,
                              void *userdata) {

  db_config_t *db_config = userdata;

  if (db_config == NULL)
    return -1;

  MYSQL *con = db->con;
  unsigned long mysql_version = mysql_get_server_version(con);

  const char *query = "SHOW STATUS";
  if (mysql_version >= 50002)
    query = "SHOW GLOBAL STATUS";

  return submit_query(db, db_config, query);
} /* int mysql_reports_status_read */

int mysql_reports_innodb_metrics_read(mysql_database_t *db,
                                      const llist_t *reports, void *userdata) {

  db_config_t *db_config = userdata;

  if (db_config == NULL)
    return -1;

  const char *query =
      "SELECT name, count FROM information_schema.innodb_metrics "
      "WHERE status = 'enabled'";

  return submit_query(db, db_config, query);
} /* int mysql_reports_innodb_metrics_read */