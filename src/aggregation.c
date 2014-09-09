/**
 * collectd - src/aggregation.c
 * Copyright (C) 2012       Florian Forster
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
 *   Florian Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include <pthread.h>

#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "meta_data.h"
#include "utils_cache.h" /* for uc_get_rate() */
#include "utils_subst.h"
#include "utils_vl_lookup.h"

#define AGG_MATCHES_ALL(str) (strcmp ("/.*/", str) == 0)
#define AGG_FUNC_PLACEHOLDER "%{aggregation}"

struct aggregation_s /* {{{ */
{
  identifier_t ident;
  unsigned int group_by;

  unsigned int regex_fields;

  char *set_host;
  char *set_plugin;
  char *set_plugin_instance;
  char *set_type_instance;

  _Bool calc_num;
  _Bool calc_sum;
  _Bool calc_average;
  _Bool calc_min;
  _Bool calc_max;
  _Bool calc_stddev;
}; /* }}} */
typedef struct aggregation_s aggregation_t;

struct agg_instance_s;
typedef struct agg_instance_s agg_instance_t;
struct agg_instance_s /* {{{ */
{
  pthread_mutex_t lock;
  identifier_t ident;

  int ds_type;

  derive_t num;
  gauge_t sum;
  gauge_t squares_sum;

  gauge_t min;
  gauge_t max;

  rate_to_value_state_t *state_num;
  rate_to_value_state_t *state_sum;
  rate_to_value_state_t *state_average;
  rate_to_value_state_t *state_min;
  rate_to_value_state_t *state_max;
  rate_to_value_state_t *state_stddev;

  agg_instance_t *next;
}; /* }}} */

static lookup_t *lookup = NULL;

static pthread_mutex_t agg_instance_list_lock = PTHREAD_MUTEX_INITIALIZER;
static agg_instance_t *agg_instance_list_head = NULL;

static _Bool agg_is_regex (char const *str) /* {{{ */
{
  size_t len;

  if (str == NULL)
    return (0);

  len = strlen (str);
  if (len < 3)
    return (0);

  if ((str[0] == '/') && (str[len - 1] == '/'))
    return (1);
  else
    return (0);
} /* }}} _Bool agg_is_regex */

static void agg_destroy (aggregation_t *agg) /* {{{ */
{
  sfree (agg);
} /* }}} void agg_destroy */

/* Frees all dynamically allocated memory within the instance. */
static void agg_instance_destroy (agg_instance_t *inst) /* {{{ */
{
  if (inst == NULL)
    return;

  /* Remove this instance from the global list of instances. */
  pthread_mutex_lock (&agg_instance_list_lock);
  if (agg_instance_list_head == inst)
    agg_instance_list_head = inst->next;
  else if (agg_instance_list_head != NULL)
  {
    agg_instance_t *prev = agg_instance_list_head;
    while ((prev != NULL) && (prev->next != inst))
      prev = prev->next;
    if (prev != NULL)
      prev->next = inst->next;
  }
  pthread_mutex_unlock (&agg_instance_list_lock);

  sfree (inst->state_num);
  sfree (inst->state_sum);
  sfree (inst->state_average);
  sfree (inst->state_min);
  sfree (inst->state_max);
  sfree (inst->state_stddev);

  memset (inst, 0, sizeof (*inst));
  inst->ds_type = -1;
  inst->min = NAN;
  inst->max = NAN;
} /* }}} void agg_instance_destroy */

static int agg_instance_create_name (agg_instance_t *inst, /* {{{ */
    value_list_t const *vl, aggregation_t const *agg)
{
#define COPY_FIELD(buffer, buffer_size, field, group_mask, all_value) do { \
  if (agg->set_ ## field != NULL) \
    sstrncpy (buffer, agg->set_ ## field, buffer_size); \
  else if ((agg->regex_fields & group_mask) \
      && (agg->group_by & group_mask)) \
    sstrncpy (buffer, vl->field, buffer_size); \
  else if ((agg->regex_fields & group_mask) \
      && (AGG_MATCHES_ALL (agg->ident.field))) \
    sstrncpy (buffer, all_value, buffer_size); \
  else \
    sstrncpy (buffer, agg->ident.field, buffer_size); \
} while (0)

  /* Host */
  COPY_FIELD (inst->ident.host, sizeof (inst->ident.host),
      host, LU_GROUP_BY_HOST, "global");

  /* Plugin */
  if (agg->set_plugin != NULL)
    sstrncpy (inst->ident.plugin, agg->set_plugin,
        sizeof (inst->ident.plugin));
  else
    sstrncpy (inst->ident.plugin, "aggregation", sizeof (inst->ident.plugin));

  /* Plugin instance */
  if (agg->set_plugin_instance != NULL)
    sstrncpy (inst->ident.plugin_instance, agg->set_plugin_instance,
        sizeof (inst->ident.plugin_instance));
  else
  {
    char tmp_plugin[DATA_MAX_NAME_LEN];
    char tmp_plugin_instance[DATA_MAX_NAME_LEN] = "";

    if ((agg->regex_fields & LU_GROUP_BY_PLUGIN)
        && (agg->group_by & LU_GROUP_BY_PLUGIN))
      sstrncpy (tmp_plugin, vl->plugin, sizeof (tmp_plugin));
    else if ((agg->regex_fields & LU_GROUP_BY_PLUGIN)
        && (AGG_MATCHES_ALL (agg->ident.plugin)))
      sstrncpy (tmp_plugin, "", sizeof (tmp_plugin));
    else
      sstrncpy (tmp_plugin, agg->ident.plugin, sizeof (tmp_plugin));

    if ((agg->regex_fields & LU_GROUP_BY_PLUGIN_INSTANCE)
        && (agg->group_by & LU_GROUP_BY_PLUGIN_INSTANCE))
      sstrncpy (tmp_plugin_instance, vl->plugin_instance,
          sizeof (tmp_plugin_instance));
    else if ((agg->regex_fields & LU_GROUP_BY_PLUGIN_INSTANCE)
        && (AGG_MATCHES_ALL (agg->ident.plugin_instance)))
      sstrncpy (tmp_plugin_instance, "", sizeof (tmp_plugin_instance));
    else
      sstrncpy (tmp_plugin_instance, agg->ident.plugin_instance,
          sizeof (tmp_plugin_instance));

    if ((strcmp ("", tmp_plugin) == 0)
        && (strcmp ("", tmp_plugin_instance) == 0))
      sstrncpy (inst->ident.plugin_instance, AGG_FUNC_PLACEHOLDER,
          sizeof (inst->ident.plugin_instance));
    else if (strcmp ("", tmp_plugin) != 0)
      ssnprintf (inst->ident.plugin_instance,
          sizeof (inst->ident.plugin_instance),
          "%s-%s", tmp_plugin, AGG_FUNC_PLACEHOLDER);
    else if (strcmp ("", tmp_plugin_instance) != 0)
      ssnprintf (inst->ident.plugin_instance,
          sizeof (inst->ident.plugin_instance),
          "%s-%s", tmp_plugin_instance, AGG_FUNC_PLACEHOLDER);
    else
      ssnprintf (inst->ident.plugin_instance,
          sizeof (inst->ident.plugin_instance),
          "%s-%s-%s", tmp_plugin, tmp_plugin_instance, AGG_FUNC_PLACEHOLDER);
  }

  /* Type */
  sstrncpy (inst->ident.type, agg->ident.type, sizeof (inst->ident.type));

  /* Type instance */
  COPY_FIELD (inst->ident.type_instance, sizeof (inst->ident.type_instance),
      type_instance, LU_GROUP_BY_TYPE_INSTANCE, "");

#undef COPY_FIELD

  return (0);
} /* }}} int agg_instance_create_name */

/* Create a new aggregation instance. */
static agg_instance_t *agg_instance_create (data_set_t const *ds, /* {{{ */
    value_list_t const *vl, aggregation_t *agg)
{
  agg_instance_t *inst;

  DEBUG ("aggregation plugin: Creating new instance.");

  inst = malloc (sizeof (*inst));
  if (inst == NULL)
  {
    ERROR ("aggregation plugin: malloc() failed.");
    return (NULL);
  }
  memset (inst, 0, sizeof (*inst));
  pthread_mutex_init (&inst->lock, /* attr = */ NULL);

  inst->ds_type = ds->ds[0].type;

  agg_instance_create_name (inst, vl, agg);

  inst->min = NAN;
  inst->max = NAN;

#define INIT_STATE(field) do { \
  inst->state_ ## field = NULL; \
  if (agg->calc_ ## field) { \
    inst->state_ ## field = malloc (sizeof (*inst->state_ ## field)); \
    if (inst->state_ ## field == NULL) { \
      agg_instance_destroy (inst); \
      ERROR ("aggregation plugin: malloc() failed."); \
      return (NULL); \
    } \
    memset (inst->state_ ## field, 0, sizeof (*inst->state_ ## field)); \
  } \
} while (0)

  INIT_STATE (num);
  INIT_STATE (sum);
  INIT_STATE (average);
  INIT_STATE (min);
  INIT_STATE (max);
  INIT_STATE (stddev);

#undef INIT_STATE

  pthread_mutex_lock (&agg_instance_list_lock);
  inst->next = agg_instance_list_head;
  agg_instance_list_head = inst;
  pthread_mutex_unlock (&agg_instance_list_lock);

  return (inst);
} /* }}} agg_instance_t *agg_instance_create */

/* Update the num, sum, min, max, ... fields of the aggregation instance, if
 * the rate of the value list is available. Value lists with more than one data
 * source are not supported and will return an error. Returns zero on success
 * and non-zero otherwise. */
static int agg_instance_update (agg_instance_t *inst, /* {{{ */
    data_set_t const *ds, value_list_t const *vl)
{
  gauge_t *rate;

  if (ds->ds_num != 1)
  {
    ERROR ("aggregation plugin: The \"%s\" type (data set) has more than one "
        "data source. This is currently not supported by this plugin. "
        "Sorry.", ds->type);
    return (EINVAL);
  }

  rate = uc_get_rate (ds, vl);
  if (rate == NULL)
  {
    char ident[6 * DATA_MAX_NAME_LEN];
    FORMAT_VL (ident, sizeof (ident), vl);
    ERROR ("aggregation plugin: Unable to read the current rate of \"%s\".",
        ident);
    return (ENOENT);
  }

  if (isnan (rate[0]))
  {
    sfree (rate);
    return (0);
  }

  pthread_mutex_lock (&inst->lock);

  inst->num++;
  inst->sum += rate[0];
  inst->squares_sum += (rate[0] * rate[0]);

  if (isnan (inst->min) || (inst->min > rate[0]))
    inst->min = rate[0];
  if (isnan (inst->max) || (inst->max < rate[0]))
    inst->max = rate[0];

  pthread_mutex_unlock (&inst->lock);

  sfree (rate);
  return (0);
} /* }}} int agg_instance_update */

static int agg_instance_read_func (agg_instance_t *inst, /* {{{ */
  char const *func, gauge_t rate, rate_to_value_state_t *state,
  value_list_t *vl, char const *pi_prefix, cdtime_t t)
{
  value_t v;
  int status;

  if (pi_prefix[0] != 0)
    subst_string (vl->plugin_instance, sizeof (vl->plugin_instance),
        pi_prefix, AGG_FUNC_PLACEHOLDER, func);
  else
    sstrncpy (vl->plugin_instance, func, sizeof (vl->plugin_instance));

  memset (&v, 0, sizeof (v));
  status = rate_to_value (&v, rate, state, inst->ds_type, t);
  if (status != 0)
  {
    /* If this is the first iteration and rate_to_value() was asked to return a
     * COUNTER or a DERIVE, it will return EAGAIN. Catch this and handle
     * gracefully. */
    if (status == EAGAIN)
      return (0);

    WARNING ("aggregation plugin: rate_to_value failed with status %i.",
        status);
    return (-1);
  }

  vl->values = &v;
  vl->values_len = 1;

  plugin_dispatch_values (vl);

  vl->values = NULL;
  vl->values_len = 0;

  return (0);
} /* }}} int agg_instance_read_func */

static int agg_instance_read (agg_instance_t *inst, cdtime_t t) /* {{{ */
{
  value_list_t vl = VALUE_LIST_INIT;

  /* Pre-set all the fields in the value list that will not change per
   * aggregation type (sum, average, ...). The struct will be re-used and must
   * therefore be dispatched using the "secure" function. */

  vl.time = t;
  vl.interval = 0;

  vl.meta = meta_data_create ();
  if (vl.meta == NULL)
  {
    ERROR ("aggregation plugin: meta_data_create failed.");
    return (-1);
  }
  meta_data_add_boolean (vl.meta, "aggregation:created", 1);

  sstrncpy (vl.host, inst->ident.host, sizeof (vl.host));
  sstrncpy (vl.plugin, inst->ident.plugin, sizeof (vl.plugin));
  sstrncpy (vl.type, inst->ident.type, sizeof (vl.type));
  sstrncpy (vl.type_instance, inst->ident.type_instance,
      sizeof (vl.type_instance));

#define READ_FUNC(func, rate) do { \
  if (inst->state_ ## func != NULL) { \
    agg_instance_read_func (inst, #func, rate, \
        inst->state_ ## func, &vl, inst->ident.plugin_instance, t); \
  } \
} while (0)

  pthread_mutex_lock (&inst->lock);

  READ_FUNC (num, (gauge_t) inst->num);

  /* All other aggregations are only defined when there have been any values
   * at all. */
  if (inst->num > 0)
  {
    READ_FUNC (sum, inst->sum);
    READ_FUNC (average, (inst->sum / ((gauge_t) inst->num)));
    READ_FUNC (min, inst->min);
    READ_FUNC (max, inst->max);
    READ_FUNC (stddev, sqrt((((gauge_t) inst->num) * inst->squares_sum)
          - (inst->sum * inst->sum)) / ((gauge_t) inst->num));
  }

  /* Reset internal state. */
  inst->num = 0;
  inst->sum = 0.0;
  inst->squares_sum = 0.0;
  inst->min = NAN;
  inst->max = NAN;

  pthread_mutex_unlock (&inst->lock);

  meta_data_destroy (vl.meta);
  vl.meta = NULL;

  return (0);
} /* }}} int agg_instance_read */

/* lookup_class_callback_t for utils_vl_lookup */
static void *agg_lookup_class_callback ( /* {{{ */
    data_set_t const *ds, value_list_t const *vl, void *user_class)
{
  return (agg_instance_create (ds, vl, (aggregation_t *) user_class));
} /* }}} void *agg_class_callback */

/* lookup_obj_callback_t for utils_vl_lookup */
static int agg_lookup_obj_callback (data_set_t const *ds, /* {{{ */
    value_list_t const *vl,
    __attribute__((unused)) void *user_class,
    void *user_obj)
{
  return (agg_instance_update ((agg_instance_t *) user_obj, ds, vl));
} /* }}} int agg_lookup_obj_callback */

/* lookup_free_class_callback_t for utils_vl_lookup */
static void agg_lookup_free_class_callback (void *user_class) /* {{{ */
{
  agg_destroy ((aggregation_t *) user_class);
} /* }}} void agg_lookup_free_class_callback */

/* lookup_free_obj_callback_t for utils_vl_lookup */
static void agg_lookup_free_obj_callback (void *user_obj) /* {{{ */
{
  agg_instance_destroy ((agg_instance_t *) user_obj);
} /* }}} void agg_lookup_free_obj_callback */

/*
 * <Plugin "aggregation">
 *   <Aggregation>
 *     Plugin "cpu"
 *     Type "cpu"
 *
 *     GroupBy Host
 *     GroupBy TypeInstance
 *
 *     CalculateNum true
 *     CalculateSum true
 *     CalculateAverage true
 *     CalculateMinimum true
 *     CalculateMaximum true
 *     CalculateStddev true
 *   </Aggregation>
 * </Plugin>
 */
static int agg_config_handle_group_by (oconfig_item_t const *ci, /* {{{ */
    aggregation_t *agg)
{
  int i;

  for (i = 0; i < ci->values_num; i++)
  {
    char const *value;

    if (ci->values[i].type != OCONFIG_TYPE_STRING)
    {
      ERROR ("aggregation plugin: Argument %i of the \"GroupBy\" option "
          "is not a string.", i + 1);
      continue;
    }

    value = ci->values[i].value.string;

    if (strcasecmp ("Host", value) == 0)
      agg->group_by |= LU_GROUP_BY_HOST;
    else if (strcasecmp ("Plugin", value) == 0)
      agg->group_by |= LU_GROUP_BY_PLUGIN;
    else if (strcasecmp ("PluginInstance", value) == 0)
      agg->group_by |= LU_GROUP_BY_PLUGIN_INSTANCE;
    else if (strcasecmp ("TypeInstance", value) == 0)
      agg->group_by |= LU_GROUP_BY_TYPE_INSTANCE;
    else if (strcasecmp ("Type", value) == 0)
      ERROR ("aggregation plugin: Grouping by type is not supported.");
    else
      WARNING ("aggregation plugin: The \"%s\" argument to the \"GroupBy\" "
          "option is invalid and will be ignored.", value);
  } /* for (ci->values) */

  return (0);
} /* }}} int agg_config_handle_group_by */

static int agg_config_aggregation (oconfig_item_t *ci) /* {{{ */
{
  aggregation_t *agg;
  _Bool is_valid;
  int status;
  int i;

  agg = malloc (sizeof (*agg));
  if (agg == NULL)
  {
    ERROR ("aggregation plugin: malloc failed.");
    return (-1);
  }
  memset (agg, 0, sizeof (*agg));

  sstrncpy (agg->ident.host, "/.*/", sizeof (agg->ident.host));
  sstrncpy (agg->ident.plugin, "/.*/", sizeof (agg->ident.plugin));
  sstrncpy (agg->ident.plugin_instance, "/.*/",
      sizeof (agg->ident.plugin_instance));
  sstrncpy (agg->ident.type, "/.*/", sizeof (agg->ident.type));
  sstrncpy (agg->ident.type_instance, "/.*/",
      sizeof (agg->ident.type_instance));

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Host", child->key) == 0)
      cf_util_get_string_buffer (child, agg->ident.host,
          sizeof (agg->ident.host));
    else if (strcasecmp ("Plugin", child->key) == 0)
      cf_util_get_string_buffer (child, agg->ident.plugin,
          sizeof (agg->ident.plugin));
    else if (strcasecmp ("PluginInstance", child->key) == 0)
      cf_util_get_string_buffer (child, agg->ident.plugin_instance,
          sizeof (agg->ident.plugin_instance));
    else if (strcasecmp ("Type", child->key) == 0)
      cf_util_get_string_buffer (child, agg->ident.type,
          sizeof (agg->ident.type));
    else if (strcasecmp ("TypeInstance", child->key) == 0)
      cf_util_get_string_buffer (child, agg->ident.type_instance,
          sizeof (agg->ident.type_instance));
    else if (strcasecmp ("SetHost", child->key) == 0)
      cf_util_get_string (child, &agg->set_host);
    else if (strcasecmp ("SetPlugin", child->key) == 0)
      cf_util_get_string (child, &agg->set_plugin);
    else if (strcasecmp ("SetPluginInstance", child->key) == 0)
      cf_util_get_string (child, &agg->set_plugin_instance);
    else if (strcasecmp ("SetTypeInstance", child->key) == 0)
      cf_util_get_string (child, &agg->set_type_instance);
    else if (strcasecmp ("GroupBy", child->key) == 0)
      agg_config_handle_group_by (child, agg);
    else if (strcasecmp ("CalculateNum", child->key) == 0)
      cf_util_get_boolean (child, &agg->calc_num);
    else if (strcasecmp ("CalculateSum", child->key) == 0)
      cf_util_get_boolean (child, &agg->calc_sum);
    else if (strcasecmp ("CalculateAverage", child->key) == 0)
      cf_util_get_boolean (child, &agg->calc_average);
    else if (strcasecmp ("CalculateMinimum", child->key) == 0)
      cf_util_get_boolean (child, &agg->calc_min);
    else if (strcasecmp ("CalculateMaximum", child->key) == 0)
      cf_util_get_boolean (child, &agg->calc_max);
    else if (strcasecmp ("CalculateStddev", child->key) == 0)
      cf_util_get_boolean (child, &agg->calc_stddev);
    else
      WARNING ("aggregation plugin: The \"%s\" key is not allowed inside "
          "<Aggregation /> blocks and will be ignored.", child->key);
  }

  if (agg_is_regex (agg->ident.host))
    agg->regex_fields |= LU_GROUP_BY_HOST;
  if (agg_is_regex (agg->ident.plugin))
    agg->regex_fields |= LU_GROUP_BY_PLUGIN;
  if (agg_is_regex (agg->ident.plugin_instance))
    agg->regex_fields |= LU_GROUP_BY_PLUGIN_INSTANCE;
  if (agg_is_regex (agg->ident.type_instance))
    agg->regex_fields |= LU_GROUP_BY_TYPE_INSTANCE;

  /* Sanity checking */
  is_valid = 1;
  if (strcmp ("/.*/", agg->ident.type) == 0) /* {{{ */
  {
    ERROR ("aggregation plugin: It appears you did not specify the required "
        "\"Type\" option in this aggregation. "
        "(Host \"%s\", Plugin \"%s\", PluginInstance \"%s\", "
        "Type \"%s\", TypeInstance \"%s\")",
        agg->ident.host, agg->ident.plugin, agg->ident.plugin_instance,
        agg->ident.type, agg->ident.type_instance);
    is_valid = 0;
  }
  else if (strchr (agg->ident.type, '/') != NULL)
  {
    ERROR ("aggregation plugin: The \"Type\" may not contain the '/' "
        "character. Especially, it may not be a regex. The current "
        "value is \"%s\".", agg->ident.type);
    is_valid = 0;
  } /* }}} */

  /* Check that there is at least one regex field without a grouping. {{{ */
  if ((agg->regex_fields & ~agg->group_by) == 0)
  {
    ERROR ("aggregation plugin: An aggregation must contain at least one "
        "wildcard. This is achieved by leaving at least one of the \"Host\", "
        "\"Plugin\", \"PluginInstance\" and \"TypeInstance\" options blank "
        "or using a regular expression and not grouping by that field. "
        "(Host \"%s\", Plugin \"%s\", PluginInstance \"%s\", "
        "Type \"%s\", TypeInstance \"%s\")",
        agg->ident.host, agg->ident.plugin, agg->ident.plugin_instance,
        agg->ident.type, agg->ident.type_instance);
    is_valid = 0;
  } /* }}} */

  /* Check that all grouping fields are regular expressions. {{{ */
  if (agg->group_by & ~agg->regex_fields)
  {
    ERROR ("aggregation plugin: Only wildcard fields (fields for which a "
        "regular expression is configured or which are left blank) can be "
        "specified in the \"GroupBy\" option. "
        "(Host \"%s\", Plugin \"%s\", PluginInstance \"%s\", "
        "Type \"%s\", TypeInstance \"%s\")",
        agg->ident.host, agg->ident.plugin, agg->ident.plugin_instance,
        agg->ident.type, agg->ident.type_instance);
    is_valid = 0;
  } /* }}} */

  if (!agg->calc_num && !agg->calc_sum && !agg->calc_average /* {{{ */
      && !agg->calc_min && !agg->calc_max && !agg->calc_stddev)
  {
    ERROR ("aggregation plugin: No aggregation function has been specified. "
        "Without this, I don't know what I should be calculating. "
        "(Host \"%s\", Plugin \"%s\", PluginInstance \"%s\", "
        "Type \"%s\", TypeInstance \"%s\")",
        agg->ident.host, agg->ident.plugin, agg->ident.plugin_instance,
        agg->ident.type, agg->ident.type_instance);
    is_valid = 0;
  } /* }}} */

  if (!is_valid) /* {{{ */
  {
    sfree (agg);
    return (-1);
  } /* }}} */

  status = lookup_add (lookup, &agg->ident, agg->group_by, agg);
  if (status != 0)
  {
    ERROR ("aggregation plugin: lookup_add failed with status %i.", status);
    sfree (agg);
    return (-1);
  }

  DEBUG ("aggregation plugin: Successfully added aggregation: "
      "(Host \"%s\", Plugin \"%s\", PluginInstance \"%s\", "
      "Type \"%s\", TypeInstance \"%s\")",
      agg->ident.host, agg->ident.plugin, agg->ident.plugin_instance,
      agg->ident.type, agg->ident.type_instance);
  return (0);
} /* }}} int agg_config_aggregation */

static int agg_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  pthread_mutex_lock (&agg_instance_list_lock);

  if (lookup == NULL)
  {
    lookup = lookup_create (agg_lookup_class_callback,
        agg_lookup_obj_callback,
        agg_lookup_free_class_callback,
        agg_lookup_free_obj_callback);
    if (lookup == NULL)
    {
      pthread_mutex_unlock (&agg_instance_list_lock);
      ERROR ("aggregation plugin: lookup_create failed.");
      return (-1);
    }
  }

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("Aggregation", child->key) == 0)
      agg_config_aggregation (child);
    else
      WARNING ("aggregation plugin: The \"%s\" key is not allowed inside "
          "<Plugin aggregation /> blocks and will be ignored.", child->key);
  }

  pthread_mutex_unlock (&agg_instance_list_lock);

  return (0);
} /* }}} int agg_config */

static int agg_read (void) /* {{{ */
{
  agg_instance_t *this;
  cdtime_t t;
  int success;

  t = cdtime ();
  success = 0;

  pthread_mutex_lock (&agg_instance_list_lock);

  /* agg_instance_list_head only holds data, after the "write" callback has
   * been called with a matching value list at least once. So on startup,
   * there's a race between the aggregations read() and write() callback. If
   * the read() callback is called first, agg_instance_list_head is NULL and
   * "success" may be zero. This is expected and should not result in an error.
   * Therefore we need to handle this case separately. */
  if (agg_instance_list_head == NULL)
  {
    pthread_mutex_unlock (&agg_instance_list_lock);
    return (0);
  }

  for (this = agg_instance_list_head; this != NULL; this = this->next)
  {
    int status;

    status = agg_instance_read (this, t);
    if (status != 0)
      WARNING ("aggregation plugin: Reading an aggregation instance "
          "failed with status %i.", status);
    else
      success++;
  }

  pthread_mutex_unlock (&agg_instance_list_lock);

  return ((success > 0) ? 0 : -1);
} /* }}} int agg_read */

static int agg_write (data_set_t const *ds, value_list_t const *vl, /* {{{ */
    __attribute__((unused)) user_data_t *user_data)
{
  _Bool created_by_aggregation = 0;
  int status;

  /* Ignore values that were created by the aggregation plugin to avoid weird
   * effects. */
  (void) meta_data_get_boolean (vl->meta, "aggregation:created",
      &created_by_aggregation);
  if (created_by_aggregation)
    return (0);

  if (lookup == NULL)
    status = ENOENT;
  else
  {
    status = lookup_search (lookup, ds, vl);
    if (status > 0)
      status = 0;
  }

  return (status);
} /* }}} int agg_write */

void module_register (void)
{
  plugin_register_complex_config ("aggregation", agg_config);
  plugin_register_read ("aggregation", agg_read);
  plugin_register_write ("aggregation", agg_write, /* user_data = */ NULL);
}

/* vim: set sw=2 sts=2 tw=78 et fdm=marker : */
