/**
 * collectd - src/threshold.c
 * Copyright (C) 2007-2010  Florian Forster
 * Copyright (C) 2008-2009  Sebastian Harl
 * Copyright (C) 2009       Andrés J. Díaz
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
 * Author:
 *   Florian octo Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 *   Andrés J. Díaz <ajdiaz at connectical.com>
 **/

#include "collectd.h"

#include "plugin.h"
#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils_cache.h"
#include "utils_threshold.h"

/*
 * Threshold management
 * ====================
 * The following functions add, delete, search, etc. configured thresholds to
 * the underlying AVL trees.
 */

/*
 * int ut_threshold_add
 *
 * Adds a threshold configuration to the list of thresholds. The threshold_t
 * structure is copied and may be destroyed after this call. Returns zero on
 * success, non-zero otherwise.
 */
static int ut_threshold_add(const threshold_t *th) { /* {{{ */
  char name[6 * DATA_MAX_NAME_LEN];
  char *name_copy;
  threshold_t *th_copy;
  threshold_t *th_ptr;
  int status = 0;

  if (snprintf(
          name, sizeof(name), "%s/%s/%s/%s", (th->host == NULL) ? "" : th->host,
          (th->plugin == NULL) ? "" : th->plugin,
          (th->type == NULL) ? "" : th->type,
          (th->data_source == NULL) ? "" : th->data_source) > sizeof(name)) {
    ERROR("ut_threshold_add: format_name failed.");
    return -1;
  }

  name_copy = strdup(name);
  if (name_copy == NULL) {
    ERROR("ut_threshold_add: strdup failed.");
    return -1;
  }

  th_copy = malloc(sizeof(*th_copy));
  if (th_copy == NULL) {
    sfree(name_copy);
    ERROR("ut_threshold_add: malloc failed.");
    return -1;
  }
  memcpy(th_copy, th, sizeof(threshold_t));

  DEBUG("ut_threshold_add: Adding entry `%s'", name);

  pthread_mutex_lock(&threshold_lock);

  th_ptr = threshold_get(th->host, th->plugin, th->type, th->data_source);

  while ((th_ptr != NULL) && (th_ptr->next != NULL))
    th_ptr = th_ptr->next;

  if (th_ptr == NULL) /* no such threshold yet */
  {
    status = c_avl_insert(threshold_tree, name_copy, th_copy);
  } else /* th_ptr points to the last threshold in the list */
  {
    th_ptr->next = th_copy;
    /* name_copy isn't needed */
    sfree(name_copy);
  }

  pthread_mutex_unlock(&threshold_lock);

  if (status != 0) {
    ERROR("ut_threshold_add: c_avl_insert (%s) failed.", name);
    sfree(name_copy);
    sfree(th_copy);
  }

  return status;
} /* }}} int ut_threshold_add */

/*
 * Configuration
 * =============
 * The following approximately two hundred functions are used to handle the
 * configuration and fill the threshold list.
 * {{{ */
static int ut_config_type(const threshold_t *th_orig, oconfig_item_t *ci) {
  threshold_t th;
  int status = 0;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("threshold values: The `Type' block needs exactly one string "
            "argument.");
    return -1;
  }

  if (ci->children_num < 1) {
    WARNING("threshold values: The `Type' block needs at least one option.");
    return -1;
  }

  memcpy(&th, th_orig, sizeof(th));
  sstrncpy(th.type, ci->values[0].value.string, sizeof(th.type));

  th.warning_min = NAN;
  th.warning_max = NAN;
  th.failure_min = NAN;
  th.failure_max = NAN;
  th.hits = 0;
  th.hysteresis = 0;
  th.flags = UT_FLAG_INTERESTING; /* interesting by default */

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("DataSource", option->key) == 0)
      status = cf_util_get_string_buffer(option, th.data_source,
                                         sizeof(th.data_source));
    else if (strcasecmp("WarningMax", option->key) == 0)
      status = cf_util_get_double(option, &th.warning_max);
    else if (strcasecmp("FailureMax", option->key) == 0)
      status = cf_util_get_double(option, &th.failure_max);
    else if (strcasecmp("WarningMin", option->key) == 0)
      status = cf_util_get_double(option, &th.warning_min);
    else if (strcasecmp("FailureMin", option->key) == 0)
      status = cf_util_get_double(option, &th.failure_min);
    else if (strcasecmp("Interesting", option->key) == 0)
      status = cf_util_get_flag(option, &th.flags, UT_FLAG_INTERESTING);
    else if (strcasecmp("Invert", option->key) == 0)
      status = cf_util_get_flag(option, &th.flags, UT_FLAG_INVERT);
    else if (strcasecmp("Persist", option->key) == 0)
      status = cf_util_get_flag(option, &th.flags, UT_FLAG_PERSIST);
    else if (strcasecmp("PersistOK", option->key) == 0)
      status = cf_util_get_flag(option, &th.flags, UT_FLAG_PERSIST_OK);
    else if (strcasecmp("Percentage", option->key) == 0)
      status = cf_util_get_flag(option, &th.flags, UT_FLAG_PERCENTAGE);
    else if (strcasecmp("Hits", option->key) == 0)
      status = cf_util_get_int(option, &th.hits);
    else if (strcasecmp("Hysteresis", option->key) == 0)
      status = cf_util_get_double(option, &th.hysteresis);
    else {
      WARNING("threshold values: Option `%s' not allowed inside a `Type' "
              "block.",
              option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  if (status == 0) {
    status = ut_threshold_add(&th);
  }

  return status;
} /* int ut_config_type */

static int ut_config_plugin(const threshold_t *th_orig, oconfig_item_t *ci) {
  threshold_t th;
  int status = 0;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("threshold values: The `Plugin' block needs exactly one string "
            "argument.");
    return -1;
  }

  if (ci->children_num < 1) {
    WARNING("threshold values: The `Plugin' block needs at least one nested "
            "block.");
    return -1;
  }

  memcpy(&th, th_orig, sizeof(th));
  sstrncpy(th.plugin, ci->values[0].value.string, sizeof(th.plugin));

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Type", option->key) == 0)
      status = ut_config_type(&th, option);
    else if (strcasecmp("Source", option->key) == 0)
      status = cf_util_get_string_buffer(option, th.data_source,
                                         sizeof(th.data_source));
    else {
      WARNING("threshold values: Option `%s' not allowed inside a `Plugin' "
              "block.",
              option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  return status;
} /* int ut_config_plugin */

static int ut_config_host(const threshold_t *th_orig, oconfig_item_t *ci) {
  threshold_t th;
  int status = 0;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("threshold values: The `Host' block needs exactly one string "
            "argument.");
    return -1;
  }

  if (ci->children_num < 1) {
    WARNING("threshold values: The `Host' block needs at least one nested "
            "block.");
    return -1;
  }

  memcpy(&th, th_orig, sizeof(th));
  sstrncpy(th.host, ci->values[0].value.string, sizeof(th.host));

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Type", option->key) == 0)
      status = ut_config_type(&th, option);
    else if (strcasecmp("Plugin", option->key) == 0)
      status = ut_config_plugin(&th, option);
    else {
      WARNING("threshold values: Option `%s' not allowed inside a `Host' "
              "block.",
              option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  return status;
} /* int ut_config_host */
/*
 * End of the functions used to configure threshold values.
 */
/* }}} */

/*
 * int ut_report_state
 *
 * Checks if the `state' differs from the old state and creates a notification
 * if appropriate.
 * Does not fail.
 */
static int ut_report_state(const metric_t *metric_p, const threshold_t *th,
                           const gauge_t value, int state) { /* {{{ */
  int state_old;
  notification_t n;

  char *buf;
  size_t bufsize;

  int status;

  /* Check if hits matched */
  if ((th->hits != 0)) {
    int hits = uc_get_hits(metric_p);
    /* STATE_OKAY resets hits unless PERSIST_OK flag is set. Hits resets if
     * threshold is hit. */
    if (((state == STATE_OKAY) && ((th->flags & UT_FLAG_PERSIST_OK) == 0)) ||
        (hits > th->hits)) {
      DEBUG("ut_report_state: reset uc_get_hits_vl = 0");
      uc_set_hits(metric_p, 0); /* reset hit counter and notify */
    } else {
      DEBUG("ut_report_state: th->hits = %d, uc_get_hits = %d", th->hits,
            uc_get_hits(mstric_p));
      (void)uc_inc_hits(metric_p, 1); /* increase hit counter */
      return 0;
    }
  } /* end check hits */

  state_old = uc_get_state(metric_p);

  /* If the state didn't change, report if `persistent' is specified. If the
   * state is `okay', then only report if `persist_ok` flag is set. */
  if (state == state_old) {
    if (state == STATE_UNKNOWN) {
      /* From UNKNOWN to UNKNOWN. Persist doesn't apply here. */
      return 0;
    } else if ((th->flags & UT_FLAG_PERSIST) == 0)
      return 0;
    else if ((state == STATE_OKAY) && ((th->flags & UT_FLAG_PERSIST_OK) == 0))
      return 0;
  }

  if (state != state_old)
    uc_set_state(metric_p, state);

  notification_init_metric(&n, NOTIF_FAILURE, NULL, metric_p);

  buf = n.message;
  bufsize = sizeof(n.message);

  if (state == STATE_OKAY)
    n.severity = NOTIF_OKAY;
  else if (state == STATE_WARNING)
    n.severity = NOTIF_WARNING;
  else
    n.severity = NOTIF_FAILURE;

  n.time = metric_p->time;

  status = ssnprintf(buf, bufsize, "Name %s", metric_p->identity->name);
  buf += status;
  bufsize -= status;

  if (metric_p->identity->root_p != NULL) {
    c_avl_iterator_t *iter_p = c_avl_get_iterator(metric_p->identity->root_p);
    if (iter_p != NULL) {
      char *key_p = NULL;
      char *value_p = NULL;
      while ((c_avl_iterator_next(iter_p, (void **)&key_p,
                                  (void **)&value_p)) == 0) {
        if ((key_p != NULL) && (value_p != NULL)) {
          int tmp_str_len = strlen(key_p) + strlen(value_p) + 2;
          if (tmp_str_len < bufsize) {
            status = ssnprintf(buf, bufsize, "%s %s", key_p, value_p);
            buf += status;
            bufsize -= status;
          }
        }
      }
    }
    c_avl_iterator_destroy(iter_p);
  }

  plugin_notification_meta_add_string(&n, "DataSource", metric_p->ds->name);
  plugin_notification_meta_add_double(&n, "CurrentValue", value);
  plugin_notification_meta_add_double(&n, "WarningMin", th->warning_min);
  plugin_notification_meta_add_double(&n, "WarningMax", th->warning_max);
  plugin_notification_meta_add_double(&n, "FailureMin", th->failure_min);
  plugin_notification_meta_add_double(&n, "FailureMax", th->failure_max);

  /* Send an okay notification */
  if (state == STATE_OKAY) {
    if (state_old == STATE_MISSING)
      ssnprintf(buf, bufsize, ": Value is no longer missing.");
    else
      ssnprintf(buf, bufsize,
                ": All data sources are within range again. "
                "Current value of \"%s\" is %f.",
                metric_p->ds->name, value);
  } else if (state == STATE_UNKNOWN) {
    ERROR("ut_report_state: metric transition to UNKNOWN from a different "
          "state. This shouldn't happen.");
    return 0;
  } else {
    double min;
    double max;

    min = (state == STATE_ERROR) ? th->failure_min : th->warning_min;
    max = (state == STATE_ERROR) ? th->failure_max : th->warning_max;

    if (th->flags & UT_FLAG_INVERT) {
      if (!isnan(min) && !isnan(max)) {
        ssnprintf(buf, bufsize,
                  ": Data source \"%s\" is currently "
                  "%f. That is within the %s region of %f%s and %f%s.",
                  metric_p->ds->name, value,
                  (state == STATE_ERROR) ? "failure" : "warning", min,
                  ((th->flags & UT_FLAG_PERCENTAGE) != 0) ? "%" : "", max,
                  ((th->flags & UT_FLAG_PERCENTAGE) != 0) ? "%" : "");
      } else {
        ssnprintf(buf, bufsize,
                  ": Data source \"%s\" is currently "
                  "%f. That is %s the %s threshold of %f%s.",
                  metric_p->ds->name, value, isnan(min) ? "below" : "above",
                  (state == STATE_ERROR) ? "failure" : "warning",
                  isnan(min) ? max : min,
                  ((th->flags & UT_FLAG_PERCENTAGE) != 0) ? "%" : "");
      }
    } else if (th->flags & UT_FLAG_PERCENTAGE) {
      ssnprintf(buf, bufsize,
                ": Data source \"%s\" is currently "
                "%g (%.2f%%). That is %s the %s threshold of %.2f%%.",
                metric_p->ds->name, value, value,
                (value < min) ? "below" : "above",
                (state == STATE_ERROR) ? "failure" : "warning",
                (value < min) ? min : max);
    } else /* is not inverted */
    {
      ssnprintf(buf, bufsize,
                ": Data source \"%s\" is currently "
                "%f. That is %s the %s threshold of %f.",
                metric_p->ds->name, value, (value < min) ? "below" : "above",
                (state == STATE_ERROR) ? "failure" : "warning",
                (value < min) ? min : max);
    }
  }

  plugin_dispatch_notification(&n);

  plugin_notification_meta_free(n.meta);
  return 0;
} /* }}} int ut_report_state */

/*
 * int ut_check_one_data_source
 *
 * Checks one data source against the given threshold configuration. If the
 * `DataSource' option is set in the threshold, and the name does NOT match,
 * `okay' is returned. If the threshold does match, its failure and warning
 * min and max values are checked and `failure' or `warning' is returned if
 * appropriate.
 * Does not fail.
 */
static int ut_check_one_data_source(const metric_t *metric_p,
                                    const threshold_t *th,
                                    const gauge_t value) { /* {{{ */
  int is_warning = 0;
  int is_failure = 0;
  int prev_state = STATE_OKAY;

  /* check if this threshold applies to this data source */
  if ((th->data_source[0] != 0) &&
      (strcmp(metric_p->ds->name, th->data_source) != 0))
    return STATE_UNKNOWN;

  if ((th->flags & UT_FLAG_INVERT) != 0) {
    is_warning--;
    is_failure--;
  }

  /* XXX: This is an experimental code, not optimized, not fast, not reliable,
   * and probably, do not work as you expect. Enjoy! :D */
  if (th->hysteresis > 0) {
    prev_state = uc_get_state(metric_p);
    /* The purpose of hysteresis is elliminating flapping state when the value
     * oscilates around the thresholds. In other words, what is important is
     * the previous state; if the new value would trigger a transition, make
     * sure that we artificially widen the range which is considered to apply
     * for the previous state, and only trigger the notification if the value
     * is outside of this expanded range.
     *
     * There is no hysteresis for the OKAY state.
     * */
    gauge_t hysteresis_for_warning = 0, hysteresis_for_failure = 0;
    switch (prev_state) {
    case STATE_ERROR:
      hysteresis_for_failure = th->hysteresis;
      break;
    case STATE_WARNING:
      hysteresis_for_warning = th->hysteresis;
      break;
    case STATE_UNKNOWN:
    case STATE_OKAY:
      /* do nothing -- the hysteresis only applies to the non-normal states */
      break;
    }

    if ((!isnan(th->failure_min) &&
         (th->failure_min + hysteresis_for_failure > value)) ||
        (!isnan(th->failure_max) &&
         (th->failure_max - hysteresis_for_failure < value)))
      is_failure++;

    if ((!isnan(th->warning_min) &&
         (th->warning_min + hysteresis_for_warning > value)) ||
        (!isnan(th->warning_max) &&
         (th->warning_max - hysteresis_for_warning < value)))
      is_warning++;

  } else { /* no hysteresis */
    if ((!isnan(th->failure_min) && (th->failure_min > value)) ||
        (!isnan(th->failure_max) && (th->failure_max < value)))
      is_failure++;

    if ((!isnan(th->warning_min) && (th->warning_min > value)) ||
        (!isnan(th->warning_max) && (th->warning_max < value)))
      is_warning++;
  }

  if (is_failure != 0)
    return STATE_ERROR;

  if (is_warning != 0)
    return STATE_WARNING;

  return STATE_OKAY;
} /* }}} int ut_check_one_data_source */

/*
 * int ut_check_one_threshold
 *
 * Checks all data sources of a value list against the given threshold, using
 * the ut_check_one_data_source function above. Returns the worst status,
 * which is `okay' if nothing has failed or `unknown' if no valid datasource was
 * defined.
 * Returns less than zero if the data set doesn't have any data sources.
 */
static int ut_check_one_threshold(const metric_t *metric_p,
                                  const threshold_t *th,
                                  const gauge_t value) { /* {{{ */
  int ret = -1;
  gauge_t values_copy = value;

  if ((th->flags & UT_FLAG_PERCENTAGE) != 0) {
    int num = 0;
    gauge_t sum = 0.0;

    /* Prepare `sum' and `num'. */
    if (!isnan(value)) {
      num++;
      sum += value;
    }

    if ((num == 0)       /* All data sources are undefined. */
        || (sum == 0.0)) /* Sum is zero, cannot calculate percentage. */
    {
      values_copy = NAN;
    } else /* We can actually calculate the percentage. */
    {
      values_copy = 100.0;
    }
  } /* if (UT_FLAG_PERCENTAGE) */

  int status;

  status = ut_check_one_data_source(metric_p, th, values_copy);
  if (ret < status) {
    ret = status;
  }
  return ret;
} /* }}} int ut_check_one_threshold */

/*
 * int ut_check_threshold
 *
 * Gets a list of matching thresholds and searches for the worst status by one
 * of the thresholds. Then reports that status using the ut_report_state
 * function above.
 * Returns zero on success and if no threshold has been configured. Returns
 * less than zero on failure.
 */
static int ut_check_threshold(const metric_t *metric_p,
                              __attribute__((unused))
                              user_data_t *ud) { /* {{{ */
  threshold_t *th;
  gauge_t value;
  int status;

  int worst_state = -1;
  threshold_t *worst_th = NULL;

  if (threshold_tree == NULL)
    return 0;

  /* Is this lock really necessary? So far, thresholds are only inserted at
   * startup. -octo */
  pthread_mutex_lock(&threshold_lock);
  th = threshold_search(metric_p);
  pthread_mutex_unlock(&threshold_lock);
  if (th == NULL)
    return 0;

  DEBUG("ut_check_threshold: Found matching threshold(s)");

  status = uc_get_rate(metric_p, &value);
  if (status != 0)
    return 0;

  while (th != NULL) {

    status = ut_check_one_threshold(metric_p, th, value);
    if (status < 0) {
      ERROR("ut_check_threshold: ut_check_one_threshold failed.");
      return -1;
    }

    if (worst_state < status) {
      worst_state = status;
      worst_th = th;
    }

    th = th->next;
  } /* while (th) */

  status = ut_report_state(metric_p, worst_th, value, worst_state);
  if (status != 0) {
    ERROR("ut_check_threshold: ut_report_state failed.");
    return -1;
  }

  return 0;
} /* }}} int ut_check_threshold */

/*
 * int ut_missing
 *
 * This function is called whenever a value goes "missing".
 */
static int ut_missing(const metric_t *metric_p,
                      __attribute__((unused)) user_data_t *ud) { /* {{{ */
  threshold_t *th;
  cdtime_t missing_time;
  char *identifier_p = NULL;
  notification_t n;
  cdtime_t now;

  if (threshold_tree == NULL)
    return 0;

  th = threshold_search(metric_p);
  /* dispatch notifications for "interesting" values only */
  if ((th == NULL) || ((th->flags & UT_FLAG_INTERESTING) == 0))
    return 0;

  now = cdtime();
  missing_time = now - metric_p->time;
  if ((identifier_p = plugin_format_metric(metric_p)) != 0) {
    ERROR("uc_update: plugin_format_metric failed.");
  }

  notification_init_metric(&n, NOTIF_FAILURE, NULL, metric_p);
  ssnprintf(n.message, sizeof(n.message),
            "%s has not been updated for %.3f seconds.", identifier_p,
            CDTIME_T_TO_DOUBLE(missing_time));
  n.time = now;
  sfree(identifier_p);

  plugin_dispatch_notification(&n);

  return 0;
} /* }}} int ut_missing */

static int ut_config(oconfig_item_t *ci) { /* {{{ */
  int status = 0;
  int old_size = c_avl_size(threshold_tree);

  if (threshold_tree == NULL) {
    threshold_tree = c_avl_create((int (*)(const void *, const void *))strcmp);
    if (threshold_tree == NULL) {
      ERROR("ut_config: c_avl_create failed.");
      return -1;
    }
  }

  threshold_t th = {
      .warning_min = NAN,
      .warning_max = NAN,
      .failure_min = NAN,
      .failure_max = NAN,
      .flags = UT_FLAG_INTERESTING /* interesting by default */
  };

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Type", option->key) == 0)
      status = ut_config_type(&th, option);
    else if (strcasecmp("Plugin", option->key) == 0)
      status = ut_config_plugin(&th, option);
    else if (strcasecmp("Host", option->key) == 0)
      status = ut_config_host(&th, option);
    else {
      WARNING("threshold values: Option `%s' not allowed here.", option->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  /* register callbacks if this is the first time we see a valid config */
  if ((old_size == 0) && (c_avl_size(threshold_tree) > 0)) {
    plugin_register_missing("threshold", ut_missing,
                            /* user data = */ NULL);
    plugin_register_write("threshold", ut_check_threshold,
                          /* user data = */ NULL);
  }

  return status;
} /* }}} int um_config */

void module_register(void) {
  plugin_register_complex_config("threshold", ut_config);
}
