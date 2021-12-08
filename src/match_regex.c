/**
 * collectd - src/match_regex.c
 * Copyright (C) 2008       Sebastian Harl
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
 *   Sebastian Harl <sh at tokkee.org>
 *   Florian Forster <octo at collectd.org>
 **/

/*
 * This module allows to filter and rewrite value lists based on
 * Perl-compatible regular expressions.
 */

#include "collectd.h"

#include "filter_chain.h"
#include "utils/common/common.h"
#include "utils/metadata/meta_data.h"
#include "utils_llist.h"

#include <regex.h>
#include <sys/types.h>

#define log_err(...) ERROR("`regex' match: " __VA_ARGS__)
#define log_warn(...) WARNING("`regex' match: " __VA_ARGS__)

/*
 * private data types
 */

struct mr_regex_s;
typedef struct mr_regex_s mr_regex_t;
struct mr_regex_s {
  regex_t re;
  char *re_str;

  mr_regex_t *next;
};

struct mr_match_s;
typedef struct mr_match_s mr_match_t;
struct mr_match_s {
  mr_regex_t *host;
  mr_regex_t *plugin;
  mr_regex_t *plugin_instance;
  mr_regex_t *type;
  mr_regex_t *type_instance;
  llist_t *meta; /* Maps each meta key into mr_regex_t* */
  bool invert;
};

/*
 * internal helper functions
 */
static void mr_free_regex(mr_regex_t *r) /* {{{ */
{
  if (r == NULL)
    return;

  regfree(&r->re);
  memset(&r->re, 0, sizeof(r->re));
  sfree(r->re_str);

  if (r->next != NULL)
    mr_free_regex(r->next);
} /* }}} void mr_free_regex */

static void mr_free_match(mr_match_t *m) /* {{{ */
{
  if (m == NULL)
    return;

  mr_free_regex(m->host);
  mr_free_regex(m->plugin);
  mr_free_regex(m->plugin_instance);
  mr_free_regex(m->type);
  mr_free_regex(m->type_instance);
  for (llentry_t *e = llist_head(m->meta); e != NULL; e = e->next) {
    sfree(e->key);
    mr_free_regex((mr_regex_t *)e->value);
  }
  llist_destroy(m->meta);

  sfree(m);
} /* }}} void mr_free_match */

static int mr_match_regexen(mr_regex_t *re_head, /* {{{ */
                            const char *string) {
  if (re_head == NULL)
    return FC_MATCH_MATCHES;

  for (mr_regex_t *re = re_head; re != NULL; re = re->next) {
    int status;

    status = regexec(&re->re, string,
                     /* nmatch = */ 0, /* pmatch = */ NULL,
                     /* eflags = */ 0);
    if (status == 0) {
      DEBUG("regex match: Regular expression `%s' matches `%s'.", re->re_str,
            string);
    } else {
      DEBUG("regex match: Regular expression `%s' does not match `%s'.",
            re->re_str, string);
      return FC_MATCH_NO_MATCH;
    }
  }

  return FC_MATCH_MATCHES;
} /* }}} int mr_match_regexen */

static int mr_add_regex(mr_regex_t **re_head, const char *re_str, /* {{{ */
                        const char *option) {
  mr_regex_t *re;
  int status;

  re = calloc(1, sizeof(*re));
  if (re == NULL) {
    log_err("mr_add_regex: calloc failed.");
    return -1;
  }
  re->next = NULL;

  re->re_str = strdup(re_str);
  if (re->re_str == NULL) {
    sfree(re);
    log_err("mr_add_regex: strdup failed.");
    return -1;
  }

  status = regcomp(&re->re, re->re_str, REG_EXTENDED | REG_NOSUB);
  if (status != 0) {
    char errmsg[1024];
    regerror(status, &re->re, errmsg, sizeof(errmsg));
    errmsg[sizeof(errmsg) - 1] = '\0';
    log_err("Compiling regex `%s' for `%s' failed: %s.", re->re_str, option,
            errmsg);
    sfree(re->re_str);
    sfree(re);
    return -1;
  }

  if (*re_head == NULL) {
    *re_head = re;
  } else {
    mr_regex_t *ptr;

    ptr = *re_head;
    while (ptr->next != NULL)
      ptr = ptr->next;

    ptr->next = re;
  }

  return 0;
} /* }}} int mr_add_regex */

static int mr_config_add_regex(mr_regex_t **re_head, /* {{{ */
                               oconfig_item_t *ci) {
  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    log_warn("`%s' needs exactly one string argument.", ci->key);
    return -1;
  }

  return mr_add_regex(re_head, ci->values[0].value.string, ci->key);
} /* }}} int mr_config_add_regex */

static int mr_config_add_meta_regex(llist_t **meta, /* {{{ */
                                    oconfig_item_t *ci) {
  char *meta_key;
  llentry_t *entry;
  mr_regex_t *re_head;
  int status;
  char buffer[1024];

  if ((ci->values_num != 2) || (ci->values[0].type != OCONFIG_TYPE_STRING) ||
      (ci->values[1].type != OCONFIG_TYPE_STRING)) {
    log_warn("`%s' needs exactly two string arguments.", ci->key);
    return -1;
  }

  if (*meta == NULL) {
    *meta = llist_create();
    if (*meta == NULL) {
      log_err("mr_config_add_meta_regex: llist_create failed.");
      return -1;
    }
  }

  meta_key = ci->values[0].value.string;
  entry = llist_search(*meta, meta_key);
  if (entry == NULL) {
    meta_key = strdup(meta_key);
    if (meta_key == NULL) {
      log_err("mr_config_add_meta_regex: strdup failed.");
      return -1;
    }
    entry = llentry_create(meta_key, NULL);
    if (entry == NULL) {
      log_err("mr_config_add_meta_regex: llentry_create failed.");
      sfree(meta_key);
      return -1;
    }
    /* meta_key and entry will now be freed by mr_free_match(). */
    llist_append(*meta, entry);
  }

  snprintf(buffer, sizeof(buffer), "%s `%s'", ci->key, meta_key);
  /* Can't pass &entry->value into mr_add_regex, so copy in/out. */
  re_head = entry->value;
  status = mr_add_regex(&re_head, ci->values[1].value.string, buffer);
  if (status == 0) {
    entry->value = re_head;
  }
  return status;
} /* }}} int mr_config_add_meta_regex */

static int mr_create(const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  mr_match_t *m;
  int status;

  m = calloc(1, sizeof(*m));
  if (m == NULL) {
    log_err("mr_create: calloc failed.");
    return -ENOMEM;
  }

  m->invert = false;

  status = 0;
  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if ((strcasecmp("Host", child->key) == 0) ||
        (strcasecmp("Hostname", child->key) == 0))
      status = mr_config_add_regex(&m->host, child);
    else if (strcasecmp("Plugin", child->key) == 0)
      status = mr_config_add_regex(&m->plugin, child);
    else if (strcasecmp("PluginInstance", child->key) == 0)
      status = mr_config_add_regex(&m->plugin_instance, child);
    else if (strcasecmp("Type", child->key) == 0)
      status = mr_config_add_regex(&m->type, child);
    else if (strcasecmp("TypeInstance", child->key) == 0)
      status = mr_config_add_regex(&m->type_instance, child);
    else if (strcasecmp("MetaData", child->key) == 0)
      status = mr_config_add_meta_regex(&m->meta, child);
    else if (strcasecmp("Invert", child->key) == 0)
      status = cf_util_get_boolean(child, &m->invert);
    else {
      log_err("The `%s' configuration option is not understood and "
              "will be ignored.",
              child->key);
      status = 0;
    }

    if (status != 0)
      break;
  }

  /* Additional sanity-checking */
  while (status == 0) {
    if ((m->host == NULL) && (m->plugin == NULL) &&
        (m->plugin_instance == NULL) && (m->type == NULL) &&
        (m->type_instance == NULL) && (m->meta == NULL)) {
      log_err("No (valid) regular expressions have been configured. "
              "This match will be ignored.");
      status = -1;
    }

    break;
  }

  if (status != 0) {
    mr_free_match(m);
    return status;
  }

  *user_data = m;
  return 0;
} /* }}} int mr_create */

static int mr_destroy(void **user_data) /* {{{ */
{
  if ((user_data != NULL) && (*user_data != NULL))
    mr_free_match(*user_data);
  return 0;
} /* }}} int mr_destroy */

static int mr_match(const data_set_t __attribute__((unused)) * ds, /* {{{ */
                    const value_list_t *vl,
                    notification_meta_t __attribute__((unused)) * *meta,
                    void **user_data) {
  mr_match_t *m;
  int match_value = FC_MATCH_MATCHES;
  int nomatch_value = FC_MATCH_NO_MATCH;

  if ((user_data == NULL) || (*user_data == NULL))
    return -1;

  m = *user_data;

  if (m->invert) {
    match_value = FC_MATCH_NO_MATCH;
    nomatch_value = FC_MATCH_MATCHES;
  }

  if (mr_match_regexen(m->host, vl->host) == FC_MATCH_NO_MATCH)
    return nomatch_value;
  if (mr_match_regexen(m->plugin, vl->plugin) == FC_MATCH_NO_MATCH)
    return nomatch_value;
  if (mr_match_regexen(m->plugin_instance, vl->plugin_instance) ==
      FC_MATCH_NO_MATCH)
    return nomatch_value;
  if (mr_match_regexen(m->type, vl->type) == FC_MATCH_NO_MATCH)
    return nomatch_value;
  if (mr_match_regexen(m->type_instance, vl->type_instance) ==
      FC_MATCH_NO_MATCH)
    return nomatch_value;
  for (llentry_t *e = llist_head(m->meta); e != NULL; e = e->next) {
    mr_regex_t *meta_re = (mr_regex_t *)e->value;
    char *value;
    int status;
    if (vl->meta == NULL)
      return nomatch_value;
    status = meta_data_as_string(vl->meta, e->key, &value);
    if (status == (-ENOENT)) /* key is not present */
      return nomatch_value;
    if (status != 0) /* some other problem */
      continue;      /* error will have already been printed. */
    if (mr_match_regexen(meta_re, value) == FC_MATCH_NO_MATCH) {
      sfree(value);
      return nomatch_value;
    }
    sfree(value);
  }

  return match_value;
} /* }}} int mr_match */

void module_register(void) {
  match_proc_t mproc = {0};

  mproc.create = mr_create;
  mproc.destroy = mr_destroy;
  mproc.match = mr_match;
  fc_register_match("regex", mproc);
} /* module_register */
