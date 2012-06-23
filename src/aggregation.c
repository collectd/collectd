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
#include "plugin.h"
#include "common.h"
#include "configfile.h"
#include "meta_data.h"
#include "utils_cache.h" /* for uc_get_rate() */
#include "utils_vl_lookup.h"

#include <pthread.h>

struct aggregation_s /* {{{ */
{
  identifier_t ident;

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

/* Create a new aggregation instance. */
static agg_instance_t *agg_instance_create (value_list_t const *vl, /* {{{ */
    aggregation_t *agg)
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

#define COPY_FIELD(fld) do { \
  sstrncpy (inst->ident.fld, \
      LU_IS_ANY (agg->ident.fld) ? vl->fld : agg->ident.fld, \
      sizeof (inst->ident.fld)); \
} while (0)

  COPY_FIELD (host);
  COPY_FIELD (plugin);
  COPY_FIELD (plugin_instance);
  COPY_FIELD (type);
  COPY_FIELD (type_instance);

#undef COPY_FIELD

  inst->ds_type = -1;
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
    return (-1);

  rate = uc_get_rate (ds, vl);
  if (rate == NULL)
  {
    ERROR ("aggregation plugin: uc_get_rate() failed.");
    return (-1);
  }

  if (isnan (rate[0]))
  {
    sfree (rate);
    return (0);
  }

  inst->num++;
  inst->sum += rate[0];
  inst->squares_sum += (rate[0] * rate[0]);

  if (isnan (inst->min) || (inst->min > rate[0]))
    inst->min = rate[0];
  if (isnan (inst->max) || (inst->max < rate[0]))
    inst->max = rate[0];

  sfree (rate);
  return (0);
} /* }}} int agg_instance_update */

/* lookup_class_callback_t for utils_vl_lookup */
static void *agg_lookup_class_callback ( /* {{{ */
    __attribute__((unused)) data_set_t const *ds,
    value_list_t const *vl, void *user_class)
{
  return (agg_instance_create (vl, (aggregation_t *) user_class));
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
 *     Host "/any/"
 *     Plugin "cpu"
 *     PluginInstance "/all/"
 *     Type "cpu"
 *     TypeInstance "/any/"
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
static int agg_config_aggregation (oconfig_item_t *ci) /* {{{ */
{
  aggregation_t *agg;
  int status;
  int i;

  agg = malloc (sizeof (*agg));
  if (agg == NULL)
  {
    ERROR ("aggregation plugin: malloc failed.");
    return (-1);
  }
  memset (agg, 0, sizeof (*agg));

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

  /* TODO(octo): Check identifier:
   * - At least one wildcard.
   * - Type is set.
   */

  status = lookup_add (lookup, &agg->ident, agg);
  if (status != 0)
  {
    ERROR ("aggregation plugin: lookup_add failed with status %i.", status);
    sfree (agg);
    return (-1);
  }

  return (0);
} /* }}} int agg_config_aggregation */

static int agg_config (oconfig_item_t *ci) /* {{{ */
{
  int i;

  if (lookup == NULL)
  {
    lookup = lookup_create (agg_lookup_class_callback,
        agg_lookup_obj_callback,
        agg_lookup_free_class_callback,
        agg_lookup_free_obj_callback);
    if (lookup == NULL)
    {
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

  return (0);
} /* }}} int agg_config */

static int agg_read (void) /* {{{ */
{
  agg_instance_t *this;
  size_t i = 0;

  pthread_mutex_lock (&agg_instance_list_lock);
  for (this = agg_instance_list_head; this != NULL; this = this->next)
  {
    DEBUG ("aggregation plugin: Handling instance: host = \"%s\", "
        "plugin = \"%s\", plugin_instance = \"%s\", "
        "type = \"%s\", type_instance = \"%s\"",
        this->ident.host,
        this->ident.plugin, this->ident.plugin_instance,
        this->ident.type, this->ident.type_instance);
    i++;
  }
  pthread_mutex_unlock (&agg_instance_list_lock);

  DEBUG ("aggregation plugin: There are currently %zu instances.", i);

  return (0);
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
