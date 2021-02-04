/**
 * collectd - src/utils_db_query.c
 * Copyright (C) 2008,2009  Florian octo Forster
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
#include "utils/strbuf/strbuf.h"

/*
 * Data types
 */
struct udb_result_s; /* {{{ */
typedef struct udb_result_s udb_result_t;
struct udb_result_s {
  metric_type_t type;
  char *type_from;
  char *metric;
  char *metric_from;
  char *metric_prefix;
  char *help;
  char *help_from;
  label_set_t labels;
  char **labels_from;
  size_t labels_from_num;
  char *value_from;

  udb_result_t *next;
}; /* }}} */

struct udb_query_s /* {{{ */
{
  char *name;
  char *statement;
  void *user_data;
  char *metric_prefix;
  label_set_t labels;
  unsigned int min_version;
  unsigned int max_version;

  udb_result_t *results;
}; /* }}} */

struct udb_result_preparation_area_s /* {{{ */
{
  size_t type_pos;
  char *type_str;
  size_t metric_pos;
  char *metric_str;
  size_t help_pos;
  char *help_str;
  size_t *labels_pos;
  char **labels_buffer;
  size_t value_pos;
  char *value_str;

  struct udb_result_preparation_area_s *next;
}; /* }}} */
typedef struct udb_result_preparation_area_s udb_result_preparation_area_t;

struct udb_query_preparation_area_s /* {{{ */
{
  size_t column_num;
  char *metric_prefix;
  label_set_t labels;
  char *db_name;

  udb_result_preparation_area_t *result_prep_areas;
}; /* }}} */

/*
 * Config Private functions
 */
static int udb_config_add_string(char ***ret_array, /* {{{ */
                                 size_t *ret_array_len, oconfig_item_t *ci) {
  char **array;
  size_t array_len;

  if (ci->values_num < 1) {
    P_WARNING("The `%s' config option "
              "needs at least one argument.",
              ci->key);
    return -1;
  }

  for (int i = 0; i < ci->values_num; i++) {
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      P_WARNING("Argument %i to the `%s' option "
                "is not a string.",
                i + 1, ci->key);
      return -1;
    }
  }

  array_len = *ret_array_len;
  array = realloc(*ret_array, sizeof(char *) * (array_len + ci->values_num));
  if (array == NULL) {
    P_ERROR("udb_config_add_string: realloc failed.");
    return -1;
  }
  *ret_array = array;

  for (int i = 0; i < ci->values_num; i++) {
    array[array_len] = strdup(ci->values[i].value.string);
    if (array[array_len] == NULL) {
      P_ERROR("udb_config_add_string: strdup failed.");
      *ret_array_len = array_len;
      return -1;
    }
    array_len++;
  }

  *ret_array_len = array_len;
  return 0;
} /* }}} int udb_config_add_string */

static int udb_config_set_uint(unsigned int *ret_value, /* {{{ */
                               oconfig_item_t *ci) {

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_NUMBER)) {
    P_WARNING("The `%s' config option "
              "needs exactly one numeric argument.",
              ci->key);
    return -1;
  }

  double tmp = ci->values[0].value.number;
  if ((tmp < 0.0) || (tmp > ((double)UINT_MAX))) {
    P_WARNING("The value given for the `%s` option is out of range.", ci->key);
    return -ERANGE;
  }

  *ret_value = (unsigned int)(tmp + .5);
  return 0;
} /* }}} int udb_config_set_uint */

/*
 * Result private functions
 */
static int udb_result_submit(udb_result_t *r, /* {{{ */
                             udb_result_preparation_area_t *r_area,
                             udb_query_t const *q,
                             udb_query_preparation_area_t *q_area) {
  assert(r != NULL);

  metric_family_t fam = {0};

  if (r->type_from != NULL) {
    if (!strcasecmp(r_area->type_str, "gauge"))
      fam.type = METRIC_TYPE_GAUGE;
    else if (!strcasecmp(r_area->type_str, "counter"))
      fam.type = METRIC_TYPE_COUNTER;
    else if (!strcasecmp(r_area->type_str, "untyped"))
      fam.type = METRIC_TYPE_UNTYPED;
    else {
      P_ERROR("udb_result_submit: Parse type `%s' as `gauge', `counter' "
              "or `untyped' failed",
              r_area->type_str);
      return -1;
    }
  } else {
    fam.type = r->type;
  }

  strbuf_t buf = STRBUF_CREATE;
  if (r->metric_from != NULL) {
    if (q_area->metric_prefix != NULL)
      strbuf_print(&buf, q_area->metric_prefix);
    if (q->metric_prefix != NULL)
      strbuf_print(&buf, q->metric_prefix);
    if (r->metric_prefix != NULL)
      strbuf_print(&buf, r->metric_prefix);
    strbuf_print(&buf, r_area->metric_str);
    fam.name = buf.ptr;
  } else {
    fam.name = r->metric;
  }

  if (r->help_from != NULL) {
    fam.help = r_area->help_str;
  } else if (r->help != NULL) {
    fam.help = r->help;
  }

  metric_t m = {0};

  for (size_t i = 0; i < q_area->labels.num; i++) {
    metric_label_set(&m, q_area->labels.ptr[i].name,
                     q_area->labels.ptr[i].value);
  }

  for (size_t i = 0; i < q->labels.num; i++) {
    metric_label_set(&m, q->labels.ptr[i].name, q->labels.ptr[i].value);
  }

  for (size_t i = 0; i < r->labels.num; i++) {
    metric_label_set(&m, r->labels.ptr[i].name, r->labels.ptr[i].value);
  }

  for (size_t i = 0; i < r->labels_from_num; i++) {
    metric_label_set(&m, r->labels_from[i], r_area->labels_buffer[i]);
  }

  int ret = 0;
  char *value_str = r_area->value_str;
  value_t value;
  if (0 != parse_value(value_str, &value, fam.type)) {
    P_ERROR("udb_result_submit: Parsing `%s' as %s failed.", value_str,
            DS_TYPE_TO_STRING(r->type));
    errno = EINVAL;
    ret = -1;
  } else {
    m.value = value;

    metric_family_metric_append(&fam, m);

    int status = plugin_dispatch_metric_family(&fam);
    if (status != 0) {
      ERROR("udb_result_submit: plugin_dispatch_metric_family failed: %s",
            STRERROR(status));
    }
  }

  metric_reset(&m);
  metric_family_metric_reset(&fam);
  STRBUF_DESTROY(buf);
  return ret;
} /* }}} void udb_result_submit */

static void udb_result_finish_result(udb_result_t const *r, /* {{{ */
                                     udb_result_preparation_area_t *prep_area) {
  if ((r == NULL) || (prep_area == NULL))
    return;

  sfree(prep_area->labels_pos);
  sfree(prep_area->labels_buffer);
} /* }}} void udb_result_finish_result */

static int udb_result_handle_result(udb_result_t *r, /* {{{ */
                                    udb_query_preparation_area_t *q_area,
                                    udb_result_preparation_area_t *r_area,
                                    udb_query_t const *q,
                                    char **column_values) {
  assert(r && q_area && r_area);

  if (r->type_from != NULL)
    r_area->type_str = column_values[r_area->type_pos];

  if (r->metric_from != NULL)
    r_area->metric_str = column_values[r_area->metric_pos];

  if (r->help_from != NULL)
    r_area->help_str = column_values[r_area->help_pos];

  for (size_t i = 0; i < r->labels_from_num; i++)
    r_area->labels_buffer[i] = column_values[r_area->labels_pos[i]];

  r_area->value_str = column_values[r_area->value_pos];

  return udb_result_submit(r, r_area, q, q_area);
} /* }}} int udb_result_handle_result */

static int udb_result_prepare_result(udb_result_t const *r, /* {{{ */
                                     udb_result_preparation_area_t *prep_area,
                                     char **column_names, size_t column_num) {
  if ((r == NULL) || (prep_area == NULL))
    return -EINVAL;

#if COLLECT_DEBUG
  assert(prep_area->ds == NULL);
  assert(prep_area->instances_pos == NULL);
  assert(prep_area->values_pos == NULL);
  assert(prep_area->instances_buffer == NULL);
  assert(prep_area->values_buffer == NULL);
#endif

#define BAIL_OUT(status)                                                       \
  udb_result_finish_result(r, prep_area);                                      \
  return (status)

  if (r->labels_from_num > 0) {
    prep_area->labels_pos =
        calloc(r->labels_from_num, sizeof(*prep_area->labels_pos));
    if (prep_area->labels_pos == NULL) {
      P_ERROR("udb_result_prepare_result: calloc failed.");
      BAIL_OUT(-ENOMEM);
    }

    prep_area->labels_buffer =
        calloc(r->labels_from_num, sizeof(*prep_area->labels_buffer));
    if (prep_area->labels_buffer == NULL) {
      P_ERROR("udb_result_prepare_result: calloc failed.");
      BAIL_OUT(-ENOMEM);
    }
  }

  /* Determine the position of the type column {{{ */
  if (r->type_from != NULL) {
    size_t i;
    for (i = 0; i < column_num; i++) {
      if (strcasecmp(r->type_from, column_names[i]) == 0) {
        prep_area->type_pos = i;
        break;
      }
    }
    if (i >= column_num) {
      P_ERROR("udb_result_prepare_result: "
              "Column `%s' could not be found.",
              r->type_from);
      BAIL_OUT(-ENOENT);
    }
  } /* }}} */

  /* Determine the position of the metric column {{{ */
  if (r->metric_from != NULL) {
    size_t i;
    for (i = 0; i < column_num; i++) {
      if (strcasecmp(r->metric_from, column_names[i]) == 0) {
        prep_area->metric_pos = i;
        break;
      }
    }
    if (i >= column_num) {
      P_ERROR("udb_result_prepare_result: "
              "Column `%s' could not be found.",
              r->type_from);
      BAIL_OUT(-ENOENT);
    }
  } /* }}} */

  /* Determine the position of the metric column {{{ */
  if (r->help_from != NULL) {
    size_t i;
    for (i = 0; i < column_num; i++) {
      if (strcasecmp(r->help_from, column_names[i]) == 0) {
        prep_area->help_pos = i;
        break;
      }
    }
    if (i >= column_num) {
      P_ERROR("udb_result_prepare_result: "
              "Column `%s' could not be found.",
              r->type_from);
      BAIL_OUT(-ENOENT);
    }
  } /* }}} */

  /* Determine the position of the labels column {{{ */
  for (size_t i = 0; i < r->labels_from_num; i++) {
    size_t j;

    for (j = 0; j < column_num; j++) {
      if (strcasecmp(r->labels_from[i], column_names[j]) == 0) {
        prep_area->labels_pos[i] = j;
        break;
      }
    }

    if (j >= column_num) {
      P_ERROR("udb_result_prepare_result: "
              "Column `%s' could not be found.",
              r->labels_from[i]);
      BAIL_OUT(-ENOENT);
    }
  } /* }}} for (i = 0; i < r->labels_from_num; i++) */

  /* Determine the position of the value column {{{ */
  size_t i;
  for (i = 0; i < column_num; i++) {
    if (strcasecmp(r->value_from, column_names[i]) == 0) {
      prep_area->value_pos = i;
      break;
    }
  }
  if (i >= column_num) {
    P_ERROR("udb_result_prepare_result: "
            "Column `%s' could not be found.",
            r->value_from);
    BAIL_OUT(-ENOENT);
  } /* }}} */

#undef BAIL_OUT
  return 0;
} /* }}} int udb_result_prepare_result */

static void udb_result_free(udb_result_t *r) /* {{{ */
{
  if (r == NULL)
    return;

  sfree(r->type_from);
  sfree(r->metric);
  sfree(r->metric_from);
  sfree(r->metric_prefix);
  sfree(r->help);
  sfree(r->help_from);
  label_set_reset(&r->labels);

  for (size_t i = 0; i < r->labels_from_num; i++)
    sfree(r->labels_from[i]);
  sfree(r->labels_from);

  sfree(r->value_from);

  udb_result_free(r->next);

  sfree(r);
} /* }}} void udb_result_free */

static int udb_result_create(const char *query_name, /* {{{ */
                             udb_result_t **r_head, oconfig_item_t *ci) {
  if (ci->values_num != 0) {
    P_WARNING("The `Result' block doesn't accept "
              "any arguments. Ignoring %i argument%s.",
              ci->values_num, (ci->values_num == 1) ? "" : "s");
  }

  udb_result_t *r = calloc(1, sizeof(*r));
  if (r == NULL) {
    P_ERROR("udb_result_create: calloc failed.");
    return -1;
  }

  r->type = METRIC_TYPE_UNTYPED;
  bool type_setted = false;
  /* Fill the `udb_result_t' structure.. */
  int status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Type", child->key) == 0) {
      type_setted = true;
      status = cf_util_get_metric_type(child, &r->type);
    } else if (strcasecmp("TypeFrom", child->key) == 0)
      status = cf_util_get_string(child, &r->type_from);
    else if (strcasecmp("Help", child->key) == 0)
      status = cf_util_get_string(child, &r->help);
    else if (strcasecmp("HelpFrom", child->key) == 0)
      status = cf_util_get_string(child, &r->help_from);
    else if (strcasecmp("Metric", child->key) == 0)
      status = cf_util_get_string(child, &r->metric);
    else if (strcasecmp("MetricFrom", child->key) == 0)
      status = cf_util_get_string(child, &r->metric_from);
    else if (strcasecmp("MetricPrefix", child->key) == 0)
      status = cf_util_get_string(child, &r->metric_prefix);
    else if (strcasecmp("Label", child->key) == 0)
      status = cf_util_get_label(child, &r->labels);
    else if (strcasecmp("LabelsFrom", child->key) == 0)
      status =
          udb_config_add_string(&r->labels_from, &r->labels_from_num, child);
    else if (strcasecmp("ValueFrom", child->key) == 0)
      status = cf_util_get_string(child, &r->value_from);
    else {
      P_WARNING("Query `%s': Option `%s' not allowed here.", query_name,
                child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  /* Check that all necessary options have been given. */
  if (status == 0) {
    if (r->metric != NULL && r->metric_from != NULL) {
      P_WARNING("udb_result_create: only one of `Metric' or `MetricFrom' "
                "can be used in query `%s'",
                query_name);
      status = -1;
    }
    if (r->metric == NULL && r->metric_from == NULL) {
      P_WARNING("udb_result_create: `Metric' or `MetricFrom' not given "
                "in query `%s'",
                query_name);
      status = -1;
    }
    if (r->metric_prefix != NULL && r->metric_from == NULL) {
      P_WARNING("udb_result_create: `MetricFrom' not given "
                "in query `%s'",
                query_name);
      status = -1;
    }

    if (r->help != NULL && r->help_from != NULL) {
      P_WARNING("udb_result_create: only one of `Help' or `HelpFrom' "
                "can be used in query `%s'",
                query_name);
      status = -1;
    }
    if (r->type_from != NULL && type_setted) {
      P_WARNING("udb_result_create: only one of `Type' or `TypeFrom' "
                "can be used in query `%s'",
                query_name);
      status = -1;
    }
    if (r->value_from == NULL) {
      P_WARNING("udb_result_create: `ValueFrom' not given for "
                "result in query `%s'",
                query_name);
      status = -1;
    }
  }

  if (status != 0) {
    udb_result_free(r);
    return -1;
  }

  /* If all went well, add this result to the list of results. */
  if (*r_head == NULL) {
    *r_head = r;
  } else {
    udb_result_t *last;

    last = *r_head;
    while (last->next != NULL)
      last = last->next;

    last->next = r;
  }

  return 0;
} /* }}} int udb_result_create */

/*
 * Query private functions
 */
static void udb_query_free_one(udb_query_t *q) /* {{{ */
{
  if (q == NULL)
    return;

  sfree(q->name);
  sfree(q->statement);
  sfree(q->metric_prefix);
  label_set_reset(&q->labels);

  udb_result_free(q->results);

  sfree(q);
} /* }}} void udb_query_free_one */

/*
 * Query public functions
 */
int udb_query_create(udb_query_t ***ret_query_list, /* {{{ */
                     size_t *ret_query_list_len, oconfig_item_t *ci,
                     udb_query_create_callback_t cb) {
  udb_query_t **query_list;
  size_t query_list_len;

  udb_query_t *q;
  int status;

  if ((ret_query_list == NULL) || (ret_query_list_len == NULL))
    return -EINVAL;
  query_list = *ret_query_list;
  query_list_len = *ret_query_list_len;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    P_WARNING("udb_result_create: The `Query' block "
              "needs exactly one string argument.");
    return -1;
  }

  q = calloc(1, sizeof(*q));
  if (q == NULL) {
    P_ERROR("udb_query_create: calloc failed.");
    return -1;
  }
  q->min_version = 0;
  q->max_version = UINT_MAX;

  status = cf_util_get_string(ci, &q->name);
  if (status != 0) {
    sfree(q);
    return status;
  }

  /* Fill the `udb_query_t' structure.. */
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Statement", child->key) == 0)
      status = cf_util_get_string(child, &q->statement);
    else if (strcasecmp("Result", child->key) == 0)
      status = udb_result_create(q->name, &q->results, child);
    else if (strcasecmp("MinVersion", child->key) == 0)
      status = udb_config_set_uint(&q->min_version, child);
    else if (strcasecmp("MaxVersion", child->key) == 0)
      status = udb_config_set_uint(&q->max_version, child);
    else if (strcasecmp("MetricPrefix", child->key) == 0)
      status = cf_util_get_string(child, &q->metric_prefix);
    else if (strcasecmp("Label", child->key) == 0)
      status = cf_util_get_label(child, &q->labels);

    /* Call custom callbacks */
    else if (cb != NULL) {
      status = (*cb)(q, child);
      if (status != 0) {
        P_WARNING("The configuration callback failed "
                  "to handle `%s'.",
                  child->key);
      }
    } else {
      P_WARNING("Query `%s': Option `%s' not allowed here.", q->name,
                child->key);
      status = -1;
    }

    if (status != 0)
      break;
  }

  /* Check that all necessary options have been given. */
  if (status == 0) {
    if (q->statement == NULL) {
      P_WARNING("Query `%s': No `Statement' given.", q->name);
      status = -1;
    }
    if (q->results == NULL) {
      P_WARNING("Query `%s': No (valid) `Result' block given.", q->name);
      status = -1;
    }
  } /* if (status == 0) */

  /* If all went well, add this query to the list of queries within the
   * database structure. */
  if (status == 0) {
    udb_query_t **temp;

    temp = realloc(query_list, sizeof(*query_list) * (query_list_len + 1));
    if (temp == NULL) {
      P_ERROR("udb_query_create: realloc failed");
      status = -1;
    } else {
      query_list = temp;
      query_list[query_list_len] = q;
      query_list_len++;
    }
  }

  if (status != 0) {
    udb_query_free_one(q);
    return -1;
  }

  *ret_query_list = query_list;
  *ret_query_list_len = query_list_len;

  return 0;
} /* }}} int udb_query_create */

void udb_query_free(udb_query_t **query_list, size_t query_list_len) /* {{{ */
{
  if (query_list == NULL)
    return;

  for (size_t i = 0; i < query_list_len; i++)
    udb_query_free_one(query_list[i]);

  sfree(query_list);
} /* }}} void udb_query_free */

int udb_query_pick_from_list_by_name(const char *name, /* {{{ */
                                     udb_query_t **src_list,
                                     size_t src_list_len,
                                     udb_query_t ***dst_list,
                                     size_t *dst_list_len) {
  int num_added;

  if ((name == NULL) || (src_list == NULL) || (dst_list == NULL) ||
      (dst_list_len == NULL)) {
    P_ERROR("udb_query_pick_from_list_by_name: "
            "Invalid argument.");
    return -EINVAL;
  }

  num_added = 0;
  for (size_t i = 0; i < src_list_len; i++) {
    udb_query_t **tmp_list;
    size_t tmp_list_len;
    fprintf(
        stderr,
        "udb_query_pick_from_list_by_name: name: %s src_list[%d]->name: %s\n",
        name, (int)i, src_list[i]->name);
    if (strcasecmp(name, src_list[i]->name) != 0)
      continue;

    tmp_list_len = *dst_list_len;
    tmp_list = realloc(*dst_list, (tmp_list_len + 1) * sizeof(udb_query_t *));
    if (tmp_list == NULL) {
      P_ERROR("udb_query_pick_from_list_by_name: realloc failed.");
      return -ENOMEM;
    }

    tmp_list[tmp_list_len] = src_list[i];
    tmp_list_len++;

    *dst_list = tmp_list;
    *dst_list_len = tmp_list_len;

    num_added++;
  } /* for (i = 0; i < src_list_len; i++) */

  if (num_added <= 0) {
    P_ERROR("Cannot find query `%s'. Make sure the <Query> "
            "block is above the database definition!",
            name);
    return -ENOENT;
  } else {
    DEBUG("Added %i versions of query `%s'.", num_added, name);
  }

  return 0;
} /* }}} int udb_query_pick_from_list_by_name */

int udb_query_pick_from_list(oconfig_item_t *ci, /* {{{ */
                             udb_query_t **src_list, size_t src_list_len,
                             udb_query_t ***dst_list, size_t *dst_list_len) {
  const char *name;

  if ((ci == NULL) || (src_list == NULL) || (dst_list == NULL) ||
      (dst_list_len == NULL)) {
    P_ERROR("udb_query_pick_from_list: "
            "Invalid argument.");
    return -EINVAL;
  }

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    P_ERROR("The `%s' config option "
            "needs exactly one string argument.",
            ci->key);
    return -1;
  }
  name = ci->values[0].value.string;

  return udb_query_pick_from_list_by_name(name, src_list, src_list_len,
                                          dst_list, dst_list_len);
} /* }}} int udb_query_pick_from_list */

const char *udb_query_get_name(udb_query_t *q) /* {{{ */
{
  if (q == NULL)
    return NULL;

  return q->name;
} /* }}} const char *udb_query_get_name */

const char *udb_query_get_statement(udb_query_t *q) /* {{{ */
{
  if (q == NULL)
    return NULL;

  return q->statement;
} /* }}} const char *udb_query_get_statement */

void udb_query_set_user_data(udb_query_t *q, void *user_data) /* {{{ */
{
  if (q == NULL)
    return;

  q->user_data = user_data;
} /* }}} void udb_query_set_user_data */

void *udb_query_get_user_data(udb_query_t *q) /* {{{ */
{
  if (q == NULL)
    return NULL;

  return q->user_data;
} /* }}} void *udb_query_get_user_data */

int udb_query_check_version(udb_query_t *q, unsigned int version) /* {{{ */
{
  if (q == NULL)
    return -EINVAL;

  if ((version < q->min_version) || (version > q->max_version))
    return 0;

  return 1;
} /* }}} int udb_query_check_version */

void udb_query_finish_result(udb_query_t const *q, /* {{{ */
                             udb_query_preparation_area_t *prep_area) {
  udb_result_preparation_area_t *r_area;
  udb_result_t *r;

  if ((q == NULL) || (prep_area == NULL))
    return;

  prep_area->column_num = 0;
  sfree(prep_area->metric_prefix);
  sfree(prep_area->db_name);

  for (r = q->results, r_area = prep_area->result_prep_areas; r != NULL;
       r = r->next, r_area = r_area->next) {
    /* this may happen during error conditions of the caller */
    if (r_area == NULL)
      break;
    udb_result_finish_result(r, r_area);
  }
} /* }}} void udb_query_finish_result */

int udb_query_handle_result(udb_query_t const *q, /* {{{ */
                            udb_query_preparation_area_t *prep_area,
                            char **column_values) {
  udb_result_preparation_area_t *r_area;
  udb_result_t *r;
  int success;
  int status;

  if ((q == NULL) || (prep_area == NULL))
    return -EINVAL;

  if ((prep_area->column_num < 1) || (prep_area->db_name == NULL)) {
    P_ERROR("Query `%s': Query is not prepared; "
            "can't handle result.",
            q->name);
    return -EINVAL;
  }

#if defined(COLLECT_DEBUG) && COLLECT_DEBUG /* {{{ */
  do {
    for (size_t i = 0; i < prep_area->column_num; i++) {
      DEBUG("udb_query_handle_result (%s, %s): "
            "column[%" PRIsz "] = %s;",
            prep_area->db_name, q->name, i, column_values[i]);
    }
  } while (0);
#endif /* }}} */

  success = 0;
  for (r = q->results, r_area = prep_area->result_prep_areas; r != NULL;
       r = r->next, r_area = r_area->next) {
    status = udb_result_handle_result(r, prep_area, r_area, q, column_values);
    if (status == 0)
      success++;
  }

  if (success == 0) {
    P_ERROR("udb_query_handle_result (%s, %s): "
            "All results failed.",
            prep_area->db_name, q->name);
    return -1;
  }

  return 0;
} /* }}} int udb_query_handle_result */

int udb_query_prepare_result(udb_query_t const *q, /* {{{ */
                             udb_query_preparation_area_t *prep_area,
                             char *metric_prefix, label_set_t *labels,
                             const char *db_name, char **column_names,
                             size_t column_num) {
  udb_result_preparation_area_t *r_area;
  udb_result_t *r;
  int status;

  if ((q == NULL) || (prep_area == NULL))
    return -EINVAL;

#if COLLECT_DEBUG
  assert(prep_area->column_num == 0);
  assert(prep_area->db_name == NULL);
#endif

  prep_area->column_num = column_num;
  if (labels != NULL) {
    label_set_clone(&prep_area->labels, *labels);
  }

  prep_area->db_name = strdup(db_name);
  if (prep_area->db_name == NULL) {
    P_ERROR("Query `%s': Prepare failed: Out of memory.", q->name);
    udb_query_finish_result(q, prep_area);
    return -ENOMEM;
  }

  if (metric_prefix != NULL) {
    prep_area->metric_prefix = strdup(metric_prefix);
    if (prep_area->metric_prefix == NULL) {
      P_ERROR("Query `%s': Prepare failed: Out of memory.", q->name);
      udb_query_finish_result(q, prep_area);
      return -ENOMEM;
    }
  }

#if defined(COLLECT_DEBUG) && COLLECT_DEBUG
  do {
    for (size_t i = 0; i < column_num; i++) {
      DEBUG("udb_query_prepare_result: "
            "query = %s; column[%" PRIsz "] = %s;",
            q->name, i, column_names[i]);
    }
  } while (0);
#endif

  for (r = q->results, r_area = prep_area->result_prep_areas; r != NULL;
       r = r->next, r_area = r_area->next) {
    if (!r_area) {
      P_ERROR("Query `%s': Invalid number of result "
              "preparation areas.",
              q->name);
      udb_query_finish_result(q, prep_area);
      return -EINVAL;
    }

    status = udb_result_prepare_result(r, r_area, column_names, column_num);
    if (status != 0) {
      udb_query_finish_result(q, prep_area);
      return status;
    }
  }

  return 0;
} /* }}} int udb_query_prepare_result */

udb_query_preparation_area_t *
udb_query_allocate_preparation_area(udb_query_t *q) /* {{{ */
{
  udb_query_preparation_area_t *q_area;
  udb_result_preparation_area_t **next_r_area;
  udb_result_t *r;

  q_area = calloc(1, sizeof(*q_area));
  if (q_area == NULL)
    return NULL;

  next_r_area = &q_area->result_prep_areas;
  for (r = q->results; r != NULL; r = r->next) {
    udb_result_preparation_area_t *r_area;

    r_area = calloc(1, sizeof(*r_area));
    if (r_area == NULL) {
      udb_result_preparation_area_t *a = q_area->result_prep_areas;

      while (a != NULL) {
        udb_result_preparation_area_t *next = a->next;
        sfree(a);
        a = next;
      }

      free(q_area);
      return NULL;
    }

    *next_r_area = r_area;
    next_r_area = &r_area->next;
  }

  return q_area;
} /* }}} udb_query_preparation_area_t *udb_query_allocate_preparation_area */

void udb_query_delete_preparation_area(
    udb_query_preparation_area_t *q_area) /* {{{ */
{
  udb_result_preparation_area_t *r_area;

  if (q_area == NULL)
    return;

  r_area = q_area->result_prep_areas;
  while (r_area != NULL) {
    udb_result_preparation_area_t *area = r_area;

    r_area = r_area->next;

    sfree(area->labels_pos);
    sfree(area->labels_buffer);
    free(area);
  }

  sfree(q_area->db_name);
  sfree(q_area->metric_prefix);
  label_set_reset(&q_area->labels);

  free(q_area);
} /* }}} void udb_query_delete_preparation_area */
