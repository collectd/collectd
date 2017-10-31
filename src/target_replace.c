/**
 * collectd - src/target_replace.c
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

#include "common.h"
#include "filter_chain.h"
#include "utils_subst.h"

#include <regex.h>

struct tr_action_s;
typedef struct tr_action_s tr_action_t;
struct tr_action_s {
  regex_t re;
  char *replacement;
  _Bool may_be_empty;

  tr_action_t *next;
};

struct tr_meta_data_action_s;
typedef struct tr_meta_data_action_s tr_meta_data_action_t;
struct tr_meta_data_action_s {
  char *key;
  regex_t re;
  char *replacement;

  tr_meta_data_action_t *next;
};

struct tr_data_s {
  tr_action_t *host;
  tr_action_t *plugin;
  tr_action_t *plugin_instance;
  /* tr_action_t *type; */
  tr_action_t *type_instance;
  tr_meta_data_action_t *meta;
};
typedef struct tr_data_s tr_data_t;

static char *tr_strdup(const char *orig) /* {{{ */
{
  size_t sz;
  char *dest;

  if (orig == NULL)
    return NULL;

  sz = strlen(orig) + 1;
  dest = malloc(sz);
  if (dest == NULL)
    return NULL;

  memcpy(dest, orig, sz);

  return dest;
} /* }}} char *tr_strdup */

static void tr_action_destroy(tr_action_t *act) /* {{{ */
{
  if (act == NULL)
    return;

  regfree(&act->re);
  sfree(act->replacement);

  if (act->next != NULL)
    tr_action_destroy(act->next);

  sfree(act);
} /* }}} void tr_action_destroy */

static void tr_meta_data_action_destroy(tr_meta_data_action_t *act) /* {{{ */
{
  if (act == NULL)
    return;

  sfree(act->key);
  regfree(&act->re);
  sfree(act->replacement);

  if (act->next != NULL)
    tr_meta_data_action_destroy(act->next);

  sfree(act);
} /* }}} void tr_meta_data_action_destroy */

static int tr_config_add_action(tr_action_t **dest, /* {{{ */
                                const oconfig_item_t *ci, _Bool may_be_empty) {
  tr_action_t *act;
  int status;

  if (dest == NULL)
    return -EINVAL;

  if ((ci->values_num != 2) || (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING)) {
    ERROR("Target `replace': The `%s' option requires exactly two string "
          "arguments.",
          ci->key);
    return -1;
  }

  act = calloc(1, sizeof(*act));
  if (act == NULL) {
    ERROR("tr_config_add_action: calloc failed.");
    return -ENOMEM;
  }

  act->replacement = NULL;
  act->may_be_empty = may_be_empty;

  status = regcomp(&act->re, ci->values[0].value.string, REG_EXTENDED);
  if (status != 0) {
    char errbuf[1024] = "";

    /* regerror assures null termination. */
    regerror(status, &act->re, errbuf, sizeof(errbuf));
    ERROR("Target `replace': Compiling the regular expression `%s' "
          "failed: %s.",
          ci->values[0].value.string, errbuf);
    sfree(act);
    return -EINVAL;
  }

  act->replacement = tr_strdup(ci->values[1].value.string);
  if (act->replacement == NULL) {
    ERROR("tr_config_add_action: tr_strdup failed.");
    tr_action_destroy(act);
    return -ENOMEM;
  }

  /* Insert action at end of list. */
  if (*dest == NULL)
    *dest = act;
  else {
    tr_action_t *prev;

    prev = *dest;
    while (prev->next != NULL)
      prev = prev->next;

    prev->next = act;
  }

  return 0;
} /* }}} int tr_config_add_action */

static int tr_config_add_meta_action(tr_meta_data_action_t **dest, /* {{{ */
                                     const oconfig_item_t *ci,
                                     _Bool should_delete) {
  tr_meta_data_action_t *act;
  int status;

  if (dest == NULL)
    return -EINVAL;

  if (should_delete) {
    if ((ci->values_num != 2) || (ci->values[0].type != OCONFIG_TYPE_STRING) ||
        (ci->values[1].type != OCONFIG_TYPE_STRING)) {
      ERROR("Target `replace': The `%s' option requires exactly two string "
            "arguments.",
            ci->key);
      return -1;
    }
  } else {
    if ((ci->values_num != 3) || (ci->values[0].type != OCONFIG_TYPE_STRING) ||
        (ci->values[1].type != OCONFIG_TYPE_STRING) ||
        (ci->values[2].type != OCONFIG_TYPE_STRING)) {
      ERROR("Target `replace': The `%s' option requires exactly three string "
            "arguments.",
            ci->key);
      return -1;
    }
  }

  if (strlen(ci->values[0].value.string) == 0) {
    ERROR("Target `replace': The `%s' option does not accept empty string as "
          "first argument.",
          ci->key);
    return -1;
  }

  act = calloc(1, sizeof(*act));
  if (act == NULL) {
    ERROR("tr_config_add_meta_action: calloc failed.");
    return -ENOMEM;
  }

  act->key = NULL;
  act->replacement = NULL;

  status = regcomp(&act->re, ci->values[1].value.string, REG_EXTENDED);
  if (status != 0) {
    char errbuf[1024] = "";

    /* regerror assures null termination. */
    regerror(status, &act->re, errbuf, sizeof(errbuf));
    ERROR("Target `replace': Compiling the regular expression `%s' "
          "failed: %s.",
          ci->values[1].value.string, errbuf);
    sfree(act->key);
    sfree(act);
    return -EINVAL;
  }

  act->key = tr_strdup(ci->values[0].value.string);
  if (act->key == NULL) {
    ERROR("tr_config_add_meta_action: tr_strdup failed.");
    tr_meta_data_action_destroy(act);
    return -ENOMEM;
  }

  if (!should_delete) {
    act->replacement = tr_strdup(ci->values[2].value.string);
    if (act->replacement == NULL) {
      ERROR("tr_config_add_meta_action: tr_strdup failed.");
      tr_meta_data_action_destroy(act);
      return -ENOMEM;
    }
  }

  /* Insert action at end of list. */
  if (*dest == NULL)
    *dest = act;
  else {
    tr_meta_data_action_t *prev;

    prev = *dest;
    while (prev->next != NULL)
      prev = prev->next;

    prev->next = act;
  }

  return 0;
} /* }}} int tr_config_add_meta_action */

static int tr_action_invoke(tr_action_t *act_head, /* {{{ */
                            char *buffer_in, size_t buffer_in_size,
                            _Bool may_be_empty) {
  int status;
  char buffer[DATA_MAX_NAME_LEN];
  regmatch_t matches[8] = {[0] = {0}};

  if (act_head == NULL)
    return -EINVAL;

  sstrncpy(buffer, buffer_in, sizeof(buffer));

  DEBUG("target_replace plugin: tr_action_invoke: <- buffer = %s;", buffer);

  for (tr_action_t *act = act_head; act != NULL; act = act->next) {
    char temp[DATA_MAX_NAME_LEN];
    char *subst_status;

    status = regexec(&act->re, buffer, STATIC_ARRAY_SIZE(matches), matches,
                     /* flags = */ 0);
    if (status == REG_NOMATCH)
      continue;
    else if (status != 0) {
      char errbuf[1024] = "";

      regerror(status, &act->re, errbuf, sizeof(errbuf));
      ERROR("Target `replace': Executing a regular expression failed: %s.",
            errbuf);
      continue;
    }

    subst_status = subst(temp, sizeof(temp), buffer, (size_t)matches[0].rm_so,
                         (size_t)matches[0].rm_eo, act->replacement);
    if (subst_status == NULL) {
      ERROR("Target `replace': subst (buffer = %s, start = %" PRIsz
            ", end = %" PRIsz ", "
            "replacement = %s) failed.",
            buffer, (size_t)matches[0].rm_so, (size_t)matches[0].rm_eo,
            act->replacement);
      continue;
    }
    sstrncpy(buffer, temp, sizeof(buffer));

    DEBUG("target_replace plugin: tr_action_invoke: -- buffer = %s;", buffer);
  } /* for (act = act_head; act != NULL; act = act->next) */

  if ((may_be_empty == 0) && (buffer[0] == 0)) {
    WARNING("Target `replace': Replacement resulted in an empty string, "
            "which is not allowed for this buffer (`host' or `plugin').");
    return 0;
  }

  DEBUG("target_replace plugin: tr_action_invoke: -> buffer = %s;", buffer);
  sstrncpy(buffer_in, buffer, buffer_in_size);

  return 0;
} /* }}} int tr_action_invoke */

static int tr_meta_data_action_invoke(/* {{{ */
                                      tr_meta_data_action_t *act_head,
                                      meta_data_t **dest) {
  int status;
  regmatch_t matches[8] = {[0] = {0}};

  if (act_head == NULL)
    return -EINVAL;

  if ((*dest) == NULL) /* nothing to do */
    return 0;

  for (tr_meta_data_action_t *act = act_head; act != NULL; act = act->next) {
    char temp[DATA_MAX_NAME_LEN];
    char *subst_status;
    int value_type;
    int meta_data_status;
    char *value;
    meta_data_t *result;

    value_type = meta_data_type(*dest, act->key);
    if (value_type == 0) /* not found */
      continue;
    if (value_type != MD_TYPE_STRING) {
      WARNING("Target `replace': Attempting replace on metadata key `%s', "
              "which isn't a string.",
              act->key);
      continue;
    }

    meta_data_status = meta_data_get_string(*dest, act->key, &value);
    if (meta_data_status != 0) {
      ERROR("Target `replace': Unable to retrieve metadata value for `%s'.",
            act->key);
      return meta_data_status;
    }

    DEBUG("target_replace plugin: tr_meta_data_action_invoke: `%s' "
          "old value = `%s'",
          act->key, value);

    status = regexec(&act->re, value, STATIC_ARRAY_SIZE(matches), matches,
                     /* flags = */ 0);
    if (status == REG_NOMATCH) {
      sfree(value);
      continue;
    } else if (status != 0) {
      char errbuf[1024] = "";

      regerror(status, &act->re, errbuf, sizeof(errbuf));
      ERROR("Target `replace': Executing a regular expression failed: %s.",
            errbuf);
      sfree(value);
      continue;
    }

    if (act->replacement == NULL) {
      /* no replacement; delete the key */
      DEBUG("target_replace plugin: tr_meta_data_action_invoke: "
            "deleting `%s'",
            act->key);
      meta_data_delete(*dest, act->key);
      sfree(value);
      continue;
    }

    subst_status = subst(temp, sizeof(temp), value, (size_t)matches[0].rm_so,
                         (size_t)matches[0].rm_eo, act->replacement);
    if (subst_status == NULL) {
      ERROR("Target `replace': subst (value = %s, start = %" PRIsz
            ", end = %" PRIsz ", "
            "replacement = %s) failed.",
            value, (size_t)matches[0].rm_so, (size_t)matches[0].rm_eo,
            act->replacement);
      sfree(value);
      continue;
    }

    DEBUG("target_replace plugin: tr_meta_data_action_invoke: `%s' "
          "value `%s' -> `%s'",
          act->key, value, temp);

    if ((result = meta_data_create()) == NULL) {
      ERROR("Target `replace': failed to create metadata for `%s'.", act->key);
      sfree(value);
      return -ENOMEM;
    }

    meta_data_status = meta_data_add_string(result, act->key, temp);
    if (meta_data_status != 0) {
      ERROR("Target `replace': Unable to set metadata value for `%s'.",
            act->key);
      meta_data_destroy(result);
      sfree(value);
      return meta_data_status;
    }

    meta_data_clone_merge(dest, result);
    meta_data_destroy(result);
    sfree(value);
  } /* for (act = act_head; act != NULL; act = act->next) */

  return 0;
} /* }}} int tr_meta_data_action_invoke */

static int tr_destroy(void **user_data) /* {{{ */
{
  tr_data_t *data;

  if (user_data == NULL)
    return -EINVAL;

  data = *user_data;
  if (data == NULL)
    return 0;

  tr_action_destroy(data->host);
  tr_action_destroy(data->plugin);
  tr_action_destroy(data->plugin_instance);
  /* tr_action_destroy (data->type); */
  tr_action_destroy(data->type_instance);
  tr_meta_data_action_destroy(data->meta);
  sfree(data);

  return 0;
} /* }}} int tr_destroy */

static int tr_create(const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  tr_data_t *data;
  int status;

  data = calloc(1, sizeof(*data));
  if (data == NULL) {
    ERROR("tr_create: calloc failed.");
    return -ENOMEM;
  }

  data->host = NULL;
  data->plugin = NULL;
  data->plugin_instance = NULL;
  /* data->type = NULL; */
  data->type_instance = NULL;
  data->meta = NULL;

  status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if ((strcasecmp("Host", child->key) == 0) ||
        (strcasecmp("Hostname", child->key) == 0))
      status = tr_config_add_action(&data->host, child,
                                    /* may be empty = */ 0);
    else if (strcasecmp("Plugin", child->key) == 0)
      status = tr_config_add_action(&data->plugin, child,
                                    /* may be empty = */ 0);
    else if (strcasecmp("PluginInstance", child->key) == 0)
      status = tr_config_add_action(&data->plugin_instance, child,
                                    /* may be empty = */ 1);
#if 0
    else if (strcasecmp ("Type", child->key) == 0)
      status = tr_config_add_action (&data->type, child,
          /* may be empty = */ 0);
#endif
    else if (strcasecmp("TypeInstance", child->key) == 0)
      status = tr_config_add_action(&data->type_instance, child,
                                    /* may be empty = */ 1);
    else if (strcasecmp("MetaData", child->key) == 0)
      status = tr_config_add_meta_action(&data->meta, child,
                                         /* should delete = */ 0);
    else if (strcasecmp("DeleteMetaData", child->key) == 0)
      status = tr_config_add_meta_action(&data->meta, child,
                                         /* should delete = */ 1);
    else {
      ERROR("Target `replace': The `%s' configuration option is not understood "
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
        && (data->type_instance == NULL) && (data->meta == NULL)) {
      ERROR("Target `replace': You need to set at least one of `Host', "
            "`Plugin', `PluginInstance' or `TypeInstance'.");
      status = -1;
    }

    break;
  }

  if (status != 0) {
    tr_destroy((void *)&data);
    return status;
  }

  *user_data = data;
  return 0;
} /* }}} int tr_create */

static int tr_invoke(const data_set_t *ds, value_list_t *vl, /* {{{ */
                     notification_meta_t __attribute__((unused)) * *meta,
                     void **user_data) {
  tr_data_t *data;

  if ((ds == NULL) || (vl == NULL) || (user_data == NULL))
    return -EINVAL;

  data = *user_data;
  if (data == NULL) {
    ERROR("Target `replace': Invoke: `data' is NULL.");
    return -EINVAL;
  }

  if (data->meta != NULL) {
    tr_meta_data_action_invoke(data->meta, &(vl->meta));
  }

#define HANDLE_FIELD(f, e)                                                     \
  if (data->f != NULL)                                                         \
  tr_action_invoke(data->f, vl->f, sizeof(vl->f), e)
  HANDLE_FIELD(host, 0);
  HANDLE_FIELD(plugin, 0);
  HANDLE_FIELD(plugin_instance, 1);
  /* HANDLE_FIELD (type, 0); */
  HANDLE_FIELD(type_instance, 1);

  return FC_TARGET_CONTINUE;
} /* }}} int tr_invoke */

void module_register(void) {
  target_proc_t tproc = {0};

  tproc.create = tr_create;
  tproc.destroy = tr_destroy;
  tproc.invoke = tr_invoke;
  fc_register_target("replace", tproc);
} /* module_register */
