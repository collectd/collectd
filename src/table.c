/**
 * collectd - src/table.c
 * Copyright (C) 2009       Sebastian Harl
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
 *   Sebastian Harl <sh at tokkee.org>
 **/

/*
 * This module provides generic means to parse and dispatch tabular data.
 */

#include "collectd.h"

#include "utils/common/common.h"

#include "plugin.h"

/*
 * private data types
 */
typedef struct {
  char *key;
  int value_from;
} tbl_label_t;

typedef struct {
  char *metric_prefix;
  char *metric;
  int metric_from;
  metric_type_t type;
  char *help;
  label_set_t labels;
  tbl_label_t *labels_from;
  size_t labels_from_num;
  int value_from;
} tbl_result_t;

typedef struct {
  char *file;
  char *sep;
  int skip_lines;

  char *metric_prefix;
  label_set_t labels;

  tbl_result_t *results;
  size_t results_num;

  size_t max_colnum;
} tbl_t;

static void tbl_result_setup(tbl_result_t *res) {
  memset(res, 0, sizeof(*res));
  res->metric_from = -1;
  res->value_from = -1;
} /* tbl_result_setup */

static void tbl_result_free(tbl_result_t *res) {
  if (res == NULL)
    return;

  sfree(res->metric_prefix);
  sfree(res->metric);
  sfree(res->help);
  label_set_reset(&res->labels);

  for (size_t i = 0; i < res->labels_from_num; i++)
    sfree(res->labels_from[i].key);
  sfree(res->labels_from);

} /* tbl_result_free */

static void tbl_free(void *arg) {
  tbl_t *tbl = arg;

  if (tbl == NULL)
    return;

  sfree(tbl->file);
  sfree(tbl->sep);
  sfree(tbl->metric_prefix);
  label_set_reset(&tbl->labels);

  /* (tbl->results == NULL) -> (tbl->results_num == 0) */
  assert((tbl->results != NULL) || (tbl->results_num == 0));
  for (size_t i = 0; i < tbl->results_num; ++i)
    tbl_result_free(tbl->results + i);
  sfree(tbl->results);

  sfree(tbl);
} /* tbl_free */

static int tbl_read_table(user_data_t *user_data);

/*
 * configuration handling
 */
static int tbl_config_append_label(tbl_label_t **var, size_t *len,
                                   oconfig_item_t *ci) {
  if (ci->values_num != 2) {
    ERROR("table plugin: \"%s\" expects two arguments.", ci->key);
    return -1;
  }
  if ((OCONFIG_TYPE_STRING != ci->values[0].type) ||
      (OCONFIG_TYPE_NUMBER != ci->values[1].type)) {
    ERROR("table plugin: \"%s\" expects a string and a numerical argument.",
          ci->key);
    return -1;
  }

  tbl_label_t *tmp = realloc(*var, ((*len) + 1) * sizeof(**var));
  if (tmp == NULL) {
    ERROR("table plugin: realloc failed: %s.", STRERRNO);
    return -1;
  }
  *var = tmp;

  tmp[*len].key = strdup(ci->values[0].value.string);
  if (tmp[*len].key == NULL) {
    ERROR("table plugin: strdup failed.");
    return -1;
  }

  tmp[*len].value_from = ci->values[1].value.number;

  *len = (*len) + 1;
  return 0;
} /* tbl_config_append_array_s */

static int tbl_config_result(tbl_t *tbl, oconfig_item_t *ci) {
  if (ci->values_num != 0) {
    ERROR("table plugin: <Result> does not expect any arguments.");
    return -1;
  }

  tbl_result_t *res =
      realloc(tbl->results, (tbl->results_num + 1) * sizeof(*tbl->results));
  if (res == NULL) {
    ERROR("table plugin: realloc failed: %s.", STRERRNO);
    return -1;
  }

  tbl->results = res;

  res = tbl->results + tbl->results_num;
  tbl_result_setup(res);

  int status = 0;
  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *c = ci->children + i;

    if (strcasecmp(c->key, "Type") == 0)
      status = cf_util_get_metric_type(c, &res->type);
    else if (strcasecmp(c->key, "Help") == 0)
      status = cf_util_get_string(c, &res->help);
    else if (strcasecmp(c->key, "Metric") == 0)
      status = cf_util_get_string(c, &res->metric);
    else if (strcasecmp(c->key, "MetricFrom") == 0)
      status = cf_util_get_int(c, &res->metric_from);
    else if (strcasecmp(c->key, "MetricPrefix") == 0)
      status = cf_util_get_string(c, &res->metric_prefix);
    else if (strcasecmp(c->key, "Label") == 0)
      status = cf_util_get_label(c, &res->labels);
    else if (strcasecmp(c->key, "LabelFrom") == 0)
      status =
          tbl_config_append_label(&res->labels_from, &res->labels_from_num, c);
    else if (strcasecmp(c->key, "ValueFrom") == 0)
      status = cf_util_get_int(c, &res->value_from);
    else {
      WARNING("table plugin: Ignoring unknown config key \"%s\" "
              " in <Result>.",
              c->key);
      status = -1;
    }
    if (status != 0)
      break;
  }

  if (status != 0) {
    tbl_result_free(res);
    return status;
  }

  if (res->metric == NULL && res->metric_from < 0) {
    ERROR("table plugin: No \"Metric\" or \"MetricFrom\" option specified for "
          "<Result> in table \"%s\".",
          tbl->file);
    status = -1;
  }

  if (res->metric != NULL && res->metric_from > 0) {
    ERROR("table plugin: Only one of \"Metric\" or \"MetricFrom\" can be set in "
          "<Result> in table \"%s\".",
          tbl->file);
    status = -1;
  }

  if (res->value_from < 0) {
    ERROR("table plugin: No \"ValueFrom\" option specified for <Result> in "
          "table \"%s\".",
          tbl->file);
    status = -1;
  }

  if (status != 0) {
    tbl_result_free(res);
    return status;
  }

  tbl->results_num++;
  return 0;
} /* tbl_config_result */

static int tbl_config_table(oconfig_item_t *ci) {
  if (ci->values_num != 1 || ci->values[0].type != OCONFIG_TYPE_STRING) {
    ERROR("table plugin: <Table> expects a single string argument.");
    return 1;
  }

  tbl_t *tbl = calloc(1, sizeof(tbl_t));
  if (tbl == NULL) {
    ERROR("table plugin: realloc failed: %s.", STRERRNO);
    return -1;
  }

  tbl->file = sstrdup(ci->values[0].value.string);
  if (tbl->file == NULL) {
    ERROR("table plugin: strdup failed");
    tbl_free(tbl);
    return -1;
  }

  cdtime_t interval = 0;

  int status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *c = ci->children + i;

    if (strcasecmp(c->key, "Separator") == 0)
      status = cf_util_get_string(c, &tbl->sep);
    else if (strcasecmp(c->key, "SkipLines") == 0)
      status = cf_util_get_int(c, &tbl->skip_lines);
    else if (strcasecmp(c->key, "MetricPrefix") == 0)
      status = cf_util_get_string(c, &tbl->metric_prefix);
    else if (strcasecmp(c->key, "Label") == 0)
      status = cf_util_get_label(c, &tbl->labels);
    else if (strcasecmp(c->key, "Interval") == 0)
      status = cf_util_get_cdtime(c, &interval);
    else if (strcasecmp(c->key, "Result") == 0)
      status = tbl_config_result(tbl, c);
    else {
      WARNING("table plugin:  Option `%s' not allowed "
              "in <Table %s>.",
              c->key, tbl->file);
      status = -1;
    }
    if (status != 0)
      break;
  }

  if (status != 0) {
    tbl_free(tbl);
    return status;
  }

  if (tbl->sep == NULL) {
    ERROR("table plugin: Table \"%s\" does not specify any separator.",
          tbl->file);
    status = -1;
  } else {
    strunescape(tbl->sep, strlen(tbl->sep) + 1);
  }

  if (tbl->results == NULL) {
    assert(tbl->results_num == 0);
    ERROR("table plugin: Table \"%s\" does not specify any (valid) results.",
          tbl->file);
    status = -1;
  }

  if (status != 0) {
    tbl_free(tbl);
    return status;
  }

  for (size_t i = 0; i < tbl->results_num; ++i) {
    tbl_result_t *res = tbl->results + i;

    for (size_t j = 0; j < res->labels_from_num; ++j)
      if (res->labels_from[j].value_from > tbl->max_colnum)
        tbl->max_colnum = res->labels_from[j].value_from;

    if (res->metric_from > tbl->max_colnum)
      tbl->max_colnum = res->metric_from;

    if (res->value_from > tbl->max_colnum)
      tbl->max_colnum = res->value_from;
  }

  char *callback_name = ssnprintf_alloc("table-%s", tbl->file);
  if (callback_name == NULL) {
    ERROR("table plugin: error alloc callback string");
    tbl_free(tbl);
    return -1;
  }

  return plugin_register_complex_read(
      /* group = */ NULL,
      /* name      = */ callback_name,
      /* callback  = */ tbl_read_table,
      /* interval  = */ interval,
      &(user_data_t){
          .data = tbl,
          .free_func = tbl_free,
      });
} /* tbl_config_table */

static int tbl_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *c = ci->children + i;

    if (strcasecmp(c->key, "Table") == 0)
      tbl_config_table(c);
    else
      WARNING("table plugin: Ignoring unknown config key \"%s\".", c->key);
  }
  return 0;
} /* tbl_config */

static int tbl_result_dispatch(tbl_t *tbl, tbl_result_t *res, char **fields,
                               size_t fields_num) {
  assert(res->value_from < fields_num);
  value_t value;
  if (parse_value(fields[res->value_from], &value, res->type) != 0)
    return -1;

  metric_family_t fam = {0};

  fam.type = res->type;
  fam.help = res->help;

  strbuf_t buf = STRBUF_CREATE;

  if (tbl->metric_prefix != NULL)
    strbuf_print(&buf, tbl->metric_prefix);
  if (res->metric_prefix != NULL)
    strbuf_print(&buf, res->metric_prefix);

  if (res->metric_from >= 0) {
    assert(res->metric_from < fields_num);
    strbuf_print(&buf, fields[res->metric_from]);
  } else {
    strbuf_print(&buf, res->metric);
  }

  fam.name = buf.ptr;

  metric_t m = {0};

  for (size_t i = 0; i < tbl->labels.num; i++) {
    metric_label_set(&m, tbl->labels.ptr[i].name, tbl->labels.ptr[i].value);
  }

  for (size_t i = 0; i < res->labels.num; i++) {
    metric_label_set(&m, res->labels.ptr[i].name, res->labels.ptr[i].value);
  }

  if (res->labels_from_num >= 0) {
    for (size_t i = 0; i < res->labels_from_num; ++i) {
      assert(res->labels_from[i].value_from < fields_num);
      metric_label_set(&m, res->labels_from[i].key,
                       fields[res->labels_from[i].value_from]);
    }
  }

  m.value = value;

  metric_family_metric_append(&fam, m);

  int status = plugin_dispatch_metric_family(&fam);
  if (status != 0) {
    ERROR("table plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }

  STRBUF_DESTROY(buf);
  metric_reset(&m);
  metric_family_metric_reset(&fam);
  return 0;
} /* tbl_result_dispatch */

static int tbl_parse_line(tbl_t *tbl, char *line, size_t len) {
  char *fields[tbl->max_colnum + 1];
  size_t i = 0;

  char *ptr = line;
  char *saveptr = NULL;
  while ((fields[i] = strtok_r(ptr, tbl->sep, &saveptr)) != NULL) {
    ptr = NULL;
    i++;

    if (i > tbl->max_colnum)
      break;
  }

  if (i <= tbl->max_colnum) {
    WARNING("table plugin: Not enough columns in line "
            "(expected at least %" PRIsz ", got %" PRIsz ").",
            tbl->max_colnum + 1, i);
    return -1;
  }

  for (i = 0; i < tbl->results_num; ++i) {
    if (tbl_result_dispatch(tbl, tbl->results + i, fields,
                            STATIC_ARRAY_SIZE(fields)) != 0) {
      ERROR("table plugin: Failed to dispatch result.");
      continue;
    }
  }
  return 0;
} /* tbl_parse_line */

static int tbl_read_table(user_data_t *user_data) {
  if (user_data == NULL)
    return -1;

  tbl_t *tbl = user_data->data;

  FILE *fh = fopen(tbl->file, "r");
  if (fh == NULL) {
    ERROR("table plugin: Failed to open file \"%s\": %s.", tbl->file, STRERRNO);
    return -1;
  }

  char buf[4096];
  buf[sizeof(buf) - 1] = '\0';
  int line = 0;
  while (fgets(buf, sizeof(buf), fh) != NULL) {
    if (buf[sizeof(buf) - 1] != '\0') {
      buf[sizeof(buf) - 1] = '\0';
      WARNING("table plugin: Table %s: Truncated line: %s", tbl->file, buf);
    }
    line++;
    if (line  <= tbl->skip_lines)
      continue;

    if (tbl_parse_line(tbl, buf, sizeof(buf)) != 0) {
      WARNING("table plugin: Table %s: Failed to parse line: %s", tbl->file,
              buf);
      continue;
    }
  }

  if (ferror(fh) != 0) {
    ERROR("table plugin: Failed to read from file \"%s\": %s.", tbl->file,
          STRERRNO);
    fclose(fh);
    return -1;
  }

  fclose(fh);
  return 0;
} /* tbl_read_table */

void module_register(void) {
  plugin_register_complex_config("table", tbl_config);
} /* module_register */
