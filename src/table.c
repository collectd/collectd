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

#include "common.h"

#include "plugin.h"

#define log_err(...) ERROR("table plugin: " __VA_ARGS__)
#define log_warn(...) WARNING("table plugin: " __VA_ARGS__)

/*
 * private data types
 */

typedef struct {
  char *type;
  char *instance_prefix;
  size_t *instances;
  size_t instances_num;
  size_t *values;
  size_t values_num;

  const data_set_t *ds;
} tbl_result_t;

typedef struct {
  char *file;
  char *sep;
  char *plugin_name;
  char *instance;

  tbl_result_t *results;
  size_t results_num;

  size_t max_colnum;
} tbl_t;

static void tbl_result_setup(tbl_result_t *res) {
  res->type = NULL;

  res->instance_prefix = NULL;
  res->instances = NULL;
  res->instances_num = 0;

  res->values = NULL;
  res->values_num = 0;

  res->ds = NULL;
} /* tbl_result_setup */

static void tbl_result_clear(tbl_result_t *res) {
  if (res == NULL) {
    return;
  }

  sfree(res->type);

  sfree(res->instance_prefix);
  sfree(res->instances);
  res->instances_num = 0;

  sfree(res->values);
  res->values_num = 0;

  res->ds = NULL;
} /* tbl_result_clear */

static void tbl_setup(tbl_t *tbl, char *file) {
  tbl->file = sstrdup(file);
  tbl->sep = NULL;
  tbl->plugin_name = NULL;
  tbl->instance = NULL;

  tbl->results = NULL;
  tbl->results_num = 0;

  tbl->max_colnum = 0;
} /* tbl_setup */

static void tbl_clear(tbl_t *tbl) {
  if (tbl == NULL) {
    return;
  }

  sfree(tbl->file);
  sfree(tbl->sep);
  sfree(tbl->plugin_name);
  sfree(tbl->instance);

  /* (tbl->results == NULL) -> (tbl->results_num == 0) */
  assert((tbl->results != NULL) || (tbl->results_num == 0));
  for (size_t i = 0; i < tbl->results_num; ++i)
    tbl_result_clear(tbl->results + i);
  sfree(tbl->results);
  tbl->results_num = 0;

  tbl->max_colnum = 0;
} /* tbl_clear */

static tbl_t *tables;
static size_t tables_num;

/*
 * configuration handling
 */

static int tbl_config_set_s(char *name, char **var, oconfig_item_t *ci) {
  if (ci->values_num != 1 || ci->values[0].type != OCONFIG_TYPE_STRING) {
    log_err("\"%s\" expects a single string argument.", name);
    return 1;
  }

  sfree(*var);
  *var = sstrdup(ci->values[0].value.string);
  return 0;
} /* tbl_config_set_separator */

static int tbl_config_append_array_i(char *name, size_t **var, size_t *len,
                                     oconfig_item_t *ci) {
  if (ci->values_num < 1) {
    log_err("\"%s\" expects at least one argument.", name);
    return 1;
  }

  size_t num = ci->values_num;
  for (size_t i = 0; i < num; ++i) {
    if (OCONFIG_TYPE_NUMBER != ci->values[i].type) {
      log_err("\"%s\" expects numerical arguments only.", name);
      return 1;
    }
  }

  size_t *tmp = realloc(*var, ((*len) + num) * sizeof(**var));
  if (tmp == NULL) {
    log_err("realloc failed: %s.", STRERRNO);
    return -1;
  }
  *var = tmp;

  for (size_t i = 0; i < num; ++i) {
    (*var)[*len] = (size_t)ci->values[i].value.number;
    (*len)++;
  }

  return 0;
} /* tbl_config_append_array_s */

static int tbl_config_result(tbl_t *tbl, oconfig_item_t *ci) {
  if (ci->values_num != 0) {
    log_err("<Result> does not expect any arguments.");
    return 1;
  }

  tbl_result_t *res =
      realloc(tbl->results, (tbl->results_num + 1) * sizeof(*tbl->results));
  if (res == NULL) {
    log_err("realloc failed: %s.", STRERRNO);
    return -1;
  }

  tbl->results = res;

  res = tbl->results + tbl->results_num;
  tbl_result_setup(res);

  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *c = ci->children + i;

    if (strcasecmp(c->key, "Type") == 0)
      tbl_config_set_s(c->key, &res->type, c);
    else if (strcasecmp(c->key, "InstancePrefix") == 0)
      tbl_config_set_s(c->key, &res->instance_prefix, c);
    else if (strcasecmp(c->key, "InstancesFrom") == 0)
      tbl_config_append_array_i(c->key, &res->instances, &res->instances_num,
                                c);
    else if (strcasecmp(c->key, "ValuesFrom") == 0)
      tbl_config_append_array_i(c->key, &res->values, &res->values_num, c);
    else
      log_warn("Ignoring unknown config key \"%s\" "
               " in <Result>.",
               c->key);
  }

  int status = 0;
  if (res->type == NULL) {
    log_err("No \"Type\" option specified for <Result> in table \"%s\".",
            tbl->file);
    status = 1;
  }

  if (res->values == NULL) {
    log_err("No \"ValuesFrom\" option specified for <Result> in table \"%s\".",
            tbl->file);
    status = 1;
  }

  if (status != 0) {
    tbl_result_clear(res);
    return status;
  }

  tbl->results_num++;
  return 0;
} /* tbl_config_result */

static int tbl_config_table(oconfig_item_t *ci) {
  if (ci->values_num != 1 || ci->values[0].type != OCONFIG_TYPE_STRING) {
    log_err("<Table> expects a single string argument.");
    return 1;
  }

  tbl_t *tbl = realloc(tables, (tables_num + 1) * sizeof(*tables));
  if (tbl == NULL) {
    log_err("realloc failed: %s.", STRERRNO);
    return -1;
  }

  tables = tbl;

  tbl = tables + tables_num;
  tbl_setup(tbl, ci->values[0].value.string);

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *c = ci->children + i;

    if (strcasecmp(c->key, "Separator") == 0)
      tbl_config_set_s(c->key, &tbl->sep, c);
    else if (strcasecmp(c->key, "Plugin") == 0)
      tbl_config_set_s(c->key, &tbl->plugin_name, c);
    else if (strcasecmp(c->key, "Instance") == 0)
      tbl_config_set_s(c->key, &tbl->instance, c);
    else if (strcasecmp(c->key, "Result") == 0)
      tbl_config_result(tbl, c);
    else
      log_warn("Ignoring unknown config key \"%s\" "
               "in <Table %s>.",
               c->key, tbl->file);
  }

  int status = 0;
  if (tbl->sep == NULL) {
    log_err("Table \"%s\" does not specify any separator.", tbl->file);
    status = 1;
  } else {
    strunescape(tbl->sep, strlen(tbl->sep) + 1);
  }

  if (tbl->instance == NULL) {
    tbl->instance = sstrdup(tbl->file);
    replace_special(tbl->instance, strlen(tbl->instance));
  }

  if (tbl->results == NULL) {
    assert(tbl->results_num == 0);
    log_err("Table \"%s\" does not specify any (valid) results.", tbl->file);
    status = 1;
  }

  if (status != 0) {
    tbl_clear(tbl);
    return status;
  }

  for (size_t i = 0; i < tbl->results_num; ++i) {
    tbl_result_t *res = tbl->results + i;

    for (size_t j = 0; j < res->instances_num; ++j)
      if (res->instances[j] > tbl->max_colnum)
        tbl->max_colnum = res->instances[j];

    for (size_t j = 0; j < res->values_num; ++j)
      if (res->values[j] > tbl->max_colnum)
        tbl->max_colnum = res->values[j];
  }

  tables_num++;
  return 0;
} /* tbl_config_table */

static int tbl_config(oconfig_item_t *ci) {
  for (int i = 0; i < ci->children_num; ++i) {
    oconfig_item_t *c = ci->children + i;

    if (strcasecmp(c->key, "Table") == 0)
      tbl_config_table(c);
    else
      log_warn("Ignoring unknown config key \"%s\".", c->key);
  }
  return 0;
} /* tbl_config */

/*
 * result handling
 */

static int tbl_prepare(tbl_t *tbl) {
  for (size_t i = 0; i < tbl->results_num; ++i) {
    tbl_result_t *res = tbl->results + i;

    res->ds = plugin_get_ds(res->type);
    if (res->ds == NULL) {
      log_err("Unknown type \"%s\". See types.db(5) for details.", res->type);
      return -1;
    }

    if (res->values_num != res->ds->ds_num) {
      log_err("Invalid type \"%s\". Expected %" PRIsz " data source%s, "
              "got %" PRIsz ".",
              res->type, res->values_num, (1 == res->values_num) ? "" : "s",
              res->ds->ds_num);
      return -1;
    }
  }
  return 0;
} /* tbl_prepare */

static int tbl_finish(tbl_t *tbl) {
  for (size_t i = 0; i < tbl->results_num; ++i)
    tbl->results[i].ds = NULL;
  return 0;
} /* tbl_finish */

static int tbl_result_dispatch(tbl_t *tbl, tbl_result_t *res, char **fields,
                               size_t fields_num) {
  value_list_t vl = VALUE_LIST_INIT;
  value_t values[res->values_num];

  assert(res->ds);
  assert(res->values_num == res->ds->ds_num);

  for (size_t i = 0; i < res->values_num; ++i) {
    assert(res->values[i] < fields_num);
    char *value = fields[res->values[i]];
    if (parse_value(value, &values[i], res->ds->ds[i].type) != 0)
      return -1;
  }

  vl.values = values;
  vl.values_len = STATIC_ARRAY_SIZE(values);

  sstrncpy(vl.plugin, (tbl->plugin_name != NULL) ? tbl->plugin_name : "table",
           sizeof(vl.plugin));
  sstrncpy(vl.plugin_instance, tbl->instance, sizeof(vl.plugin_instance));
  sstrncpy(vl.type, res->type, sizeof(vl.type));

  if (res->instances_num == 0) {
    if (res->instance_prefix)
      sstrncpy(vl.type_instance, res->instance_prefix,
               sizeof(vl.type_instance));
  } else {
    char *instances[res->instances_num];
    char instances_str[DATA_MAX_NAME_LEN];

    for (size_t i = 0; i < res->instances_num; ++i) {
      assert(res->instances[i] < fields_num);
      instances[i] = fields[res->instances[i]];
    }

    strjoin(instances_str, sizeof(instances_str), instances,
            STATIC_ARRAY_SIZE(instances), "-");
    instances_str[sizeof(instances_str) - 1] = '\0';

    int r;
    if (res->instance_prefix == NULL)
      r = snprintf(vl.type_instance, sizeof(vl.type_instance), "%s",
                   instances_str);
    else
      r = snprintf(vl.type_instance, sizeof(vl.type_instance), "%s-%s",
                   res->instance_prefix, instances_str);
    if ((size_t)r >= sizeof(vl.type_instance))
      log_warn("Truncated type instance: %s.", vl.type_instance);
  }

  plugin_dispatch_values(&vl);
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
    log_warn("Not enough columns in line "
             "(expected at least %" PRIsz ", got %" PRIsz ").",
             tbl->max_colnum + 1, i);
    return -1;
  }

  for (i = 0; i < tbl->results_num; ++i)
    if (tbl_result_dispatch(tbl, tbl->results + i, fields,
                            STATIC_ARRAY_SIZE(fields)) != 0) {
      log_err("Failed to dispatch result.");
      continue;
    }
  return 0;
} /* tbl_parse_line */

static int tbl_read_table(tbl_t *tbl) {
  char buf[4096];

  FILE *fh = fopen(tbl->file, "r");
  if (fh == NULL) {
    log_err("Failed to open file \"%s\": %s.", tbl->file, STRERRNO);
    return -1;
  }

  buf[sizeof(buf) - 1] = '\0';
  while (fgets(buf, sizeof(buf), fh) != NULL) {
    if (buf[sizeof(buf) - 1] != '\0') {
      buf[sizeof(buf) - 1] = '\0';
      log_warn("Table %s: Truncated line: %s", tbl->file, buf);
    }

    if (tbl_parse_line(tbl, buf, sizeof(buf)) != 0) {
      log_warn("Table %s: Failed to parse line: %s", tbl->file, buf);
      continue;
    }
  }

  if (ferror(fh) != 0) {
    log_err("Failed to read from file \"%s\": %s.", tbl->file, STRERRNO);
    fclose(fh);
    return -1;
  }

  fclose(fh);
  return 0;
} /* tbl_read_table */

/*
 * collectd callbacks
 */

static int tbl_read(void) {
  int status = -1;

  if (tables_num == 0)
    return 0;

  for (size_t i = 0; i < tables_num; ++i) {
    tbl_t *tbl = tables + i;

    if (tbl_prepare(tbl) != 0) {
      log_err("Failed to prepare and parse table \"%s\".", tbl->file);
      continue;
    }

    if (tbl_read_table(tbl) == 0)
      status = 0;

    tbl_finish(tbl);
  }
  return status;
} /* tbl_read */

static int tbl_shutdown(void) {
  for (size_t i = 0; i < tables_num; ++i)
    tbl_clear(&tables[i]);
  sfree(tables);
  return 0;
} /* tbl_shutdown */

static int tbl_init(void) {
  if (tables_num == 0)
    return 0;

  plugin_register_read("table", tbl_read);
  plugin_register_shutdown("table", tbl_shutdown);
  return 0;
} /* tbl_init */

void module_register(void) {
  plugin_register_complex_config("table", tbl_config);
  plugin_register_init("table", tbl_init);
} /* module_register */
