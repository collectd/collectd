/**
 * collectd - src/target_set.c
 * Copyright (C) 2008       Florian Forster
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

#include "filter_chain.h"
#include "utils/common/common.h"
#include "utils/metadata/meta_data.h"
#include "utils_subst.h"

struct ts_key_list_s {
  char *key;
  struct ts_key_list_s *next;
};
typedef struct ts_key_list_s ts_key_list_t;

static void ts_key_list_free(ts_key_list_t *l) /* {{{ */
{
  if (l == NULL)
    return;

  sfree(l->key);

  if (l->next != NULL)
    ts_key_list_free(l->next);

  sfree(l);
} /* }}} void ts_name_list_free */

struct ts_data_s {
  char *host;
  char *plugin;
  char *plugin_instance;
  /* char *type; */
  char *type_instance;
  meta_data_t *meta;
  ts_key_list_t *meta_delete;
};
typedef struct ts_data_s ts_data_t;

static int ts_util_get_key_and_string_wo_strdup(const oconfig_item_t *ci,
                                                char **ret_key,
                                                char **ret_string) /* {{{ */
{
  if ((ci->values_num != 2) || (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING)) {
    ERROR("ts_util_get_key_and_string_wo_strdup: The %s option requires "
          "exactly two string arguments.",
          ci->key);
    return -1;
  }

  *ret_key = ci->values[0].value.string;
  *ret_string = ci->values[1].value.string;

  return 0;
} /* }}} int ts_util_get_key_and_string_wo_strdup */

static int ts_config_add_string(char **dest, /* {{{ */
                                const oconfig_item_t *ci, int may_be_empty) {
  char *tmp = NULL;
  int status;

  status = cf_util_get_string(ci, &tmp);
  if (status != 0)
    return status;

  if (!may_be_empty && (strlen(tmp) == 0)) {
    ERROR("Target `set': The `%s' option does not accept empty strings.",
          ci->key);
    sfree(tmp);
    return -1;
  }

  *dest = tmp;
  return 0;
} /* }}} int ts_config_add_string */

static int ts_config_add_meta(meta_data_t **dest, /* {{{ */
                              const oconfig_item_t *ci, int may_be_empty) {
  char *key = NULL;
  char *string = NULL;
  int status;

  status = ts_util_get_key_and_string_wo_strdup(ci, &key, &string);
  if (status != 0)
    return status;

  if (strlen(key) == 0) {
    ERROR("Target `set': The `%s' option does not accept empty string as "
          "first argument.",
          ci->key);
    return -1;
  }

  if (!may_be_empty && (strlen(string) == 0)) {
    ERROR("Target `set': The `%s' option does not accept empty string as "
          "second argument.",
          ci->key);
    return -1;
  }

  if ((*dest) == NULL) {
    /* Create a new meta_data_t */
    if ((*dest = meta_data_create()) == NULL) {
      ERROR("Target `set': failed to create a meta data for `%s'.", ci->key);
      return -ENOMEM;
    }
  }

  return meta_data_add_string(*dest, key, string);
} /* }}} int ts_config_add_meta */

static int ts_config_add_meta_delete(ts_key_list_t **dest, /* {{{ */
                                     const oconfig_item_t *ci) {
  ts_key_list_t *entry = NULL;

  entry = calloc(1, sizeof(*entry));
  if (entry == NULL) {
    ERROR("ts_config_add_meta_delete: calloc failed.");
    return -ENOMEM;
  }

  if (cf_util_get_string(ci, &entry->key) != 0) {
    ts_key_list_free(entry);
    return -1; /* An error has already been reported. */
  }

  if (strlen(entry->key) == 0) {
    ERROR("Target `set': The `%s' option does not accept empty string as "
          "first argument.",
          ci->key);
    ts_key_list_free(entry);
    return -1;
  }

  entry->next = *dest;
  *dest = entry;

  return 0;
} /* }}} int ts_config_add_meta_delete */

static void ts_subst(char *dest, size_t size, const char *string, /* {{{ */
                     const value_list_t *vl) {
  char temp[DATA_MAX_NAME_LEN];

  /* Initialize the field with the template. */
  sstrncpy(dest, string, size);

  if (strchr(dest, '%') == NULL)
    return;

#define REPLACE_FIELD(t, v)                                                    \
  if (subst_string(temp, sizeof(temp), dest, t, v) != NULL)                    \
    sstrncpy(dest, temp, size);
  REPLACE_FIELD("%{host}", vl->host);
  REPLACE_FIELD("%{plugin}", vl->plugin);
  REPLACE_FIELD("%{plugin_instance}", vl->plugin_instance);
  REPLACE_FIELD("%{type}", vl->type);
  REPLACE_FIELD("%{type_instance}", vl->type_instance);

  if (vl->meta != NULL) {
    char **meta_toc = NULL;
    int status = meta_data_toc(vl->meta, &meta_toc);
    if (status <= 0)
      return;
    size_t meta_entries = (size_t)status;

    for (size_t i = 0; i < meta_entries; i++) {
      char meta_name[DATA_MAX_NAME_LEN];
      char *value_str;
      const char *key = meta_toc[i];

      snprintf(meta_name, sizeof(meta_name), "%%{meta:%s}", key);
      if (meta_data_as_string(vl->meta, key, &value_str) != 0)
        continue;

      REPLACE_FIELD(meta_name, value_str);
      sfree(value_str);
    }

    strarray_free(meta_toc, (size_t)meta_entries);
  }
} /* }}} int ts_subst */

static int ts_destroy(void **user_data) /* {{{ */
{
  ts_data_t *data;

  if (user_data == NULL)
    return -EINVAL;

  data = *user_data;
  if (data == NULL)
    return 0;

  free(data->host);
  free(data->plugin);
  free(data->plugin_instance);
  /* free (data->type); */
  free(data->type_instance);
  meta_data_destroy(data->meta);
  ts_key_list_free(data->meta_delete);
  free(data);

  return 0;
} /* }}} int ts_destroy */

static int ts_create(const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  ts_data_t *data;
  int status;

  data = calloc(1, sizeof(*data));
  if (data == NULL) {
    ERROR("ts_create: calloc failed.");
    return -ENOMEM;
  }

  data->host = NULL;
  data->plugin = NULL;
  data->plugin_instance = NULL;
  /* data->type = NULL; */
  data->type_instance = NULL;
  data->meta = NULL;
  data->meta_delete = NULL;

  status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if ((strcasecmp("Host", child->key) == 0) ||
        (strcasecmp("Hostname", child->key) == 0))
      status = ts_config_add_string(&data->host, child,
                                    /* may be empty = */ 0);
    else if (strcasecmp("Plugin", child->key) == 0)
      status = ts_config_add_string(&data->plugin, child,
                                    /* may be empty = */ 0);
    else if (strcasecmp("PluginInstance", child->key) == 0)
      status = ts_config_add_string(&data->plugin_instance, child,
                                    /* may be empty = */ 1);
#if 0
    else if (strcasecmp ("Type", child->key) == 0)
      status = ts_config_add_string (&data->type, child,
          /* may be empty = */ 0);
#endif
    else if (strcasecmp("TypeInstance", child->key) == 0)
      status = ts_config_add_string(&data->type_instance, child,
                                    /* may be empty = */ 1);
    else if (strcasecmp("MetaData", child->key) == 0)
      status = ts_config_add_meta(&data->meta, child,
                                  /* may be empty = */ 1);
    else if (strcasecmp("DeleteMetaData", child->key) == 0)
      status = ts_config_add_meta_delete(&data->meta_delete, child);
    else {
      ERROR("Target `set': The `%s' configuration option is not understood "
            "and will be ignored.",
            child->key);
      status = 0;
    }

    if (status != 0)
      break;
  }

  /* Additional sanity-checking */
  while (status == 0) {
    if ((data->host == NULL) && (data->plugin == NULL) &&
        (data->plugin_instance == NULL)
        /* && (data->type == NULL) */
        && (data->type_instance == NULL) && (data->meta == NULL) &&
        (data->meta_delete == NULL)) {
      ERROR("Target `set': You need to set at least one of `Host', "
            "`Plugin', `PluginInstance', `TypeInstance', "
            "`MetaData', or `DeleteMetaData'.");
      status = -1;
    }

    if (data->meta != NULL) {
      /* If data->meta_delete is NULL, this loop is a no-op. */
      for (ts_key_list_t *l = data->meta_delete; l != NULL; l = l->next) {
        if (meta_data_type(data->meta, l->key) != 0) {
          /* MetaData and DeleteMetaData for the same key. */
          ERROR("Target `set': Can only have one of `MetaData' or "
                "`DeleteMetaData' for any given key.");
          status = -1;
        }
      }
    }

    break;
  }

  if (status != 0) {
    ts_destroy((void *)&data);
    return status;
  }

  *user_data = data;
  return 0;
} /* }}} int ts_create */

static int ts_invoke(const data_set_t *ds, value_list_t *vl, /* {{{ */
                     notification_meta_t __attribute__((unused)) * *meta,
                     void **user_data) {
  ts_data_t *data;
  value_list_t orig;
  meta_data_t *new_meta = NULL;

  if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
    return -EINVAL;

  data = *user_data;
  if (data == NULL) {
    ERROR("Target `set': Invoke: `data' is NULL.");
    return -EINVAL;
  }

  orig = *vl;

  if (data->meta != NULL) {
    char temp[DATA_MAX_NAME_LEN * 2];
    char **meta_toc;

    if ((new_meta = meta_data_create()) == NULL) {
      ERROR("Target `set': failed to create replacement metadata.");
      return -ENOMEM;
    }

    int status = meta_data_toc(data->meta, &meta_toc);
    if (status < 0) {
      ERROR("Target `set': meta_data_toc failed with status %d.", status);
      meta_data_destroy(new_meta);
      return status;
    }
    size_t meta_entries = (size_t)status;

    for (size_t i = 0; i < meta_entries; i++) {
      const char *key = meta_toc[i];
      char *string;
      int status;

      status = meta_data_get_string(data->meta, key, &string);
      if (status) {
        ERROR("Target `set': Unable to get replacement metadata value `%s'.",
              key);
        strarray_free(meta_toc, meta_entries);
        meta_data_destroy(new_meta);
        return status;
      }

      ts_subst(temp, sizeof(temp), string, &orig);

      DEBUG("target_set: ts_invoke: setting metadata value for key `%s': "
            "`%s'.",
            key, temp);

      sfree(string);

      status = meta_data_add_string(new_meta, key, temp);
      if (status) {
        ERROR("Target `set': Unable to set metadata value `%s'.", key);
        strarray_free(meta_toc, meta_entries);
        meta_data_destroy(new_meta);
        return status;
      }
    }

    strarray_free(meta_toc, meta_entries);
  }

#define SUBST_FIELD(f)                                                         \
  if (data->f != NULL) {                                                       \
    ts_subst(vl->f, sizeof(vl->f), data->f, &orig);                            \
    DEBUG("target_set: ts_invoke: setting " #f ": `%s'.", vl->f);              \
  }
  SUBST_FIELD(host);
  SUBST_FIELD(plugin);
  SUBST_FIELD(plugin_instance);
  /* SUBST_FIELD (type); */
  SUBST_FIELD(type_instance);

  /* Need to merge the metadata in now, because of the shallow copy. */
  if (new_meta != NULL) {
    meta_data_clone_merge(&(vl->meta), new_meta);
    meta_data_destroy(new_meta);
  }

  /* If data->meta_delete is NULL, this loop is a no-op. */
  for (ts_key_list_t *l = data->meta_delete; l != NULL; l = l->next) {
    DEBUG("target_set: ts_invoke: deleting metadata value for key `%s'.",
          l->key);
    meta_data_delete(vl->meta, l->key);
  }

  return FC_TARGET_CONTINUE;
} /* }}} int ts_invoke */

void module_register(void) {
  target_proc_t tproc = {0};

  tproc.create = ts_create;
  tproc.destroy = ts_destroy;
  tproc.invoke = ts_invoke;
  fc_register_target("set", tproc);
} /* module_register */
