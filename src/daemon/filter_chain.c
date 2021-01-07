/**
 * collectd - src/filter_chain.c
 * Copyright (C) 2008-2010  Florian octo Forster
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

#include "configfile.h"
#include "filter_chain.h"
#include "plugin.h"
#include "utils/common/common.h"
#include "utils_complain.h"

/*
 * Data types
 */
/* List of matches, used in fc_rule_t and for the global `match_list_head'
 * variable. */
struct fc_match_s;
typedef struct fc_match_s fc_match_t; /* {{{ */
struct fc_match_s {
  char name[DATA_MAX_NAME_LEN];
  match_proc_t proc;
  void *user_data;
  fc_match_t *next;
}; /* }}} */

/* List of targets, used in fc_rule_t and for the global `target_list_head'
 * variable. */
struct fc_target_s;
typedef struct fc_target_s fc_target_t; /* {{{ */
struct fc_target_s {
  char name[DATA_MAX_NAME_LEN];
  void *user_data;
  target_proc_t proc;
  fc_target_t *next;
}; /* }}} */

/* List of rules, used in fc_chain_t */
struct fc_rule_s;
typedef struct fc_rule_s fc_rule_t; /* {{{ */
struct fc_rule_s {
  char name[DATA_MAX_NAME_LEN];
  fc_match_t *matches;
  fc_target_t *targets;
  fc_rule_t *next;
}; /* }}} */

/* List of chains, used for `chain_list_head' */
struct fc_chain_s /* {{{ */
{
  char name[DATA_MAX_NAME_LEN];
  fc_rule_t *rules;
  fc_target_t *targets;
  fc_chain_t *next;
}; /* }}} */

/* Writer configuration. */
struct fc_writer_s;
typedef struct fc_writer_s fc_writer_t; /* {{{ */
struct fc_writer_s {
  char *plugin;
  c_complain_t complaint;
}; /* }}} */

/*
 * Global variables
 */
static fc_match_t *match_list_head;
static fc_target_t *target_list_head;
static fc_chain_t *chain_list_head;

/*
 * Private functions
 */
static void fc_free_matches(fc_match_t *m) /* {{{ */
{
  if (m == NULL)
    return;

  if (m->proc.destroy != NULL)
    (*m->proc.destroy)(&m->user_data);
  else if (m->user_data != NULL) {
    ERROR("Filter subsystem: fc_free_matches: There is user data, but no "
          "destroy functions has been specified. "
          "Memory will probably be lost!");
  }

  if (m->next != NULL)
    fc_free_matches(m->next);

  free(m);
} /* }}} void fc_free_matches */

static void fc_free_targets(fc_target_t *t) /* {{{ */
{
  if (t == NULL)
    return;

  if (t->proc.destroy != NULL)
    (*t->proc.destroy)(&t->user_data);
  else if (t->user_data != NULL) {
    ERROR("Filter subsystem: fc_free_targets: There is user data, but no "
          "destroy functions has been specified. "
          "Memory will probably be lost!");
  }

  if (t->next != NULL)
    fc_free_targets(t->next);

  free(t);
} /* }}} void fc_free_targets */

static void fc_free_rules(fc_rule_t *r) /* {{{ */
{
  if (r == NULL)
    return;

  fc_free_matches(r->matches);
  fc_free_targets(r->targets);

  if (r->next != NULL)
    fc_free_rules(r->next);

  free(r);
} /* }}} void fc_free_rules */

static void fc_free_chains(fc_chain_t *c) /* {{{ */
{
  if (c == NULL)
    return;

  fc_free_rules(c->rules);
  fc_free_targets(c->targets);

  if (c->next != NULL)
    fc_free_chains(c->next);

  free(c);
} /* }}} void fc_free_chains */

static char *fc_strdup(const char *orig) /* {{{ */
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
} /* }}} char *fc_strdup */

/*
 * Configuration.
 *
 * The configuration looks somewhat like this:
 *
 *  <Chain "PreCache">
 *    <Rule>
 *      <Match "regex">
 *        Plugin "^mysql$"
 *        Type "^mysql_command$"
 *        TypeInstance "^show_"
 *      </Match>
 *      <Target "drop">
 *      </Target>
 *    </Rule>
 *
 *    <Target "write">
 *      Plugin "rrdtool"
 *    </Target>
 *  </Chain>
 */
static int fc_config_add_match(fc_match_t **matches_head, /* {{{ */
                               oconfig_item_t *ci) {
  fc_match_t *m;
  fc_match_t *ptr;
  int status;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("Filter subsystem: `Match' blocks require "
            "exactly one string argument.");
    return -1;
  }

  ptr = match_list_head;
  while (ptr != NULL) {
    if (strcasecmp(ptr->name, ci->values[0].value.string) == 0)
      break;
    ptr = ptr->next;
  }

  if (ptr == NULL && IS_TRUE(global_option_get("AutoLoadPlugin"))) {
    char plugin_name[DATA_MAX_NAME_LEN];

    status = ssnprintf(plugin_name, sizeof(plugin_name), "match_%s",
                       ci->values[0].value.string);
    if ((status < 0) || ((size_t)status >= sizeof(plugin_name))) {
      ERROR("Automatically loading plugin \"match_%s\" failed:"
            " plugin name would have been truncated.",
            ci->values[0].value.string);
      return -1;
    }

    status = plugin_load(plugin_name, /* flags = */ 0);
    if (status != 0) {
      ERROR("Automatically loading plugin \"%s\" failed "
            "with status %i.",
            plugin_name, status);
      return status;
    }

    ptr = match_list_head;
    while (ptr != NULL) {
      if (strcasecmp(ptr->name, ci->values[0].value.string) == 0)
        break;
      ptr = ptr->next;
    }
  }

  if (ptr == NULL) {
    WARNING("Filter subsystem: Cannot find a \"%s\" match. "
            "Did you load the appropriate plugin?",
            ci->values[0].value.string);
    return -1;
  }

  m = calloc(1, sizeof(*m));
  if (m == NULL) {
    ERROR("fc_config_add_match: calloc failed.");
    return -1;
  }

  sstrncpy(m->name, ptr->name, sizeof(m->name));
  memcpy(&m->proc, &ptr->proc, sizeof(m->proc));
  m->user_data = NULL;
  m->next = NULL;

  if (m->proc.create != NULL) {
    status = (*m->proc.create)(ci, &m->user_data);
    if (status != 0) {
      WARNING("Filter subsystem: Failed to create a %s match.", m->name);
      fc_free_matches(m);
      return -1;
    }
  }

  if (*matches_head != NULL) {
    ptr = *matches_head;
    while (ptr->next != NULL)
      ptr = ptr->next;

    ptr->next = m;
  } else {
    *matches_head = m;
  }

  return 0;
} /* }}} int fc_config_add_match */

static int fc_config_add_target(fc_target_t **targets_head, /* {{{ */
                                oconfig_item_t *ci) {
  fc_target_t *t;
  fc_target_t *ptr;
  int status;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("Filter subsystem: `Target' blocks require "
            "exactly one string argument.");
    return -1;
  }

  ptr = target_list_head;
  while (ptr != NULL) {
    if (strcasecmp(ptr->name, ci->values[0].value.string) == 0)
      break;
    ptr = ptr->next;
  }

  if (ptr == NULL && IS_TRUE(global_option_get("AutoLoadPlugin"))) {
    char plugin_name[DATA_MAX_NAME_LEN];

    status = ssnprintf(plugin_name, sizeof(plugin_name), "target_%s",
                       ci->values[0].value.string);
    if ((status < 0) || ((size_t)status >= sizeof(plugin_name))) {
      ERROR("Automatically loading plugin \"target_%s\" failed:"
            " plugin name would have been truncated.",
            ci->values[0].value.string);
      return -1;
    }

    status = plugin_load(plugin_name, /* flags = */ 0);
    if (status != 0) {
      ERROR("Automatically loading plugin \"%s\" failed "
            "with status %i.",
            plugin_name, status);
      return status;
    }

    ptr = target_list_head;
    while (ptr != NULL) {
      if (strcasecmp(ptr->name, ci->values[0].value.string) == 0)
        break;
      ptr = ptr->next;
    }
  }

  if (ptr == NULL) {
    WARNING("Filter subsystem: Cannot find a \"%s\" target. "
            "Did you load the appropriate plugin?",
            ci->values[0].value.string);
    return -1;
  }

  t = calloc(1, sizeof(*t));
  if (t == NULL) {
    ERROR("fc_config_add_target: calloc failed.");
    return -1;
  }

  sstrncpy(t->name, ptr->name, sizeof(t->name));
  memcpy(&t->proc, &ptr->proc, sizeof(t->proc));
  t->user_data = NULL;
  t->next = NULL;

  if (t->proc.create != NULL) {
    status = (*t->proc.create)(ci, &t->user_data);
    if (status != 0) {
      WARNING("Filter subsystem: Failed to create a %s target.", t->name);
      fc_free_targets(t);
      return -1;
    }
  } else {
    t->user_data = NULL;
  }

  if (*targets_head != NULL) {
    ptr = *targets_head;
    while (ptr->next != NULL)
      ptr = ptr->next;

    ptr->next = t;
  } else {
    *targets_head = t;
  }

  return 0;
} /* }}} int fc_config_add_target */

static int fc_config_add_rule(fc_chain_t *chain, /* {{{ */
                              oconfig_item_t *ci) {
  fc_rule_t *rule;
  char rule_name[2 * DATA_MAX_NAME_LEN] = "Unnamed rule";
  int status = 0;

  if (ci->values_num > 1) {
    WARNING("Filter subsystem: `Rule' blocks have at most one argument.");
    return -1;
  } else if ((ci->values_num == 1) &&
             (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("Filter subsystem: `Rule' blocks expect one string argument "
            "or no argument at all.");
    return -1;
  }

  rule = calloc(1, sizeof(*rule));
  if (rule == NULL) {
    ERROR("fc_config_add_rule: calloc failed.");
    return -1;
  }

  if (ci->values_num == 1) {
    sstrncpy(rule->name, ci->values[0].value.string, sizeof(rule->name));
    snprintf(rule_name, sizeof(rule_name), "Rule \"%s\"",
             ci->values[0].value.string);
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Match", option->key) == 0)
      status = fc_config_add_match(&rule->matches, option);
    else if (strcasecmp("Target", option->key) == 0)
      status = fc_config_add_target(&rule->targets, option);
    else {
      WARNING("Filter subsystem: %s: Option `%s' not allowed "
              "inside a <Rule> block.",
              rule_name, option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (ci->children) */

  /* Additional sanity checking. */
  while (status == 0) {
    if (rule->targets == NULL) {
      WARNING("Filter subsystem: %s: No target has been specified.", rule_name);
      status = -1;
      break;
    }

    break;
  } /* while (status == 0) */

  if (status != 0) {
    fc_free_rules(rule);
    return -1;
  }

  if (chain->rules != NULL) {
    fc_rule_t *ptr;

    ptr = chain->rules;
    while (ptr->next != NULL)
      ptr = ptr->next;

    ptr->next = rule;
  } else {
    chain->rules = rule;
  }

  return 0;
} /* }}} int fc_config_add_rule */

static int fc_config_add_chain(const oconfig_item_t *ci) /* {{{ */
{
  fc_chain_t *chain = NULL;
  int status = 0;
  int new_chain = 1;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING)) {
    WARNING("Filter subsystem: <Chain> blocks require exactly one "
            "string argument.");
    return -1;
  }

  if (chain_list_head != NULL) {
    if ((chain = fc_chain_get_by_name(ci->values[0].value.string)) != NULL)
      new_chain = 0;
  }

  if (chain == NULL) {
    chain = calloc(1, sizeof(*chain));
    if (chain == NULL) {
      ERROR("fc_config_add_chain: calloc failed.");
      return -1;
    }
    sstrncpy(chain->name, ci->values[0].value.string, sizeof(chain->name));
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Rule", option->key) == 0)
      status = fc_config_add_rule(chain, option);
    else if (strcasecmp("Target", option->key) == 0)
      status = fc_config_add_target(&chain->targets, option);
    else {
      WARNING("Filter subsystem: Chain %s: Option `%s' not allowed "
              "inside a <Chain> block.",
              chain->name, option->key);
      status = -1;
    }

    if (status != 0)
      break;
  } /* for (ci->children) */

  if (status != 0) {
    fc_free_chains(chain);
    return -1;
  }

  if (chain_list_head != NULL) {
    if (!new_chain)
      return 0;

    fc_chain_t *ptr;

    ptr = chain_list_head;
    while (ptr->next != NULL)
      ptr = ptr->next;

    ptr->next = chain;
  } else {
    chain_list_head = chain;
  }

  return 0;
} /* }}} int fc_config_add_chain */

/*
 * Built-in target "jump"
 *
 * Prefix `bit' like `_b_uilt-_i_n _t_arget'
 */
static int fc_bit_jump_create(const oconfig_item_t *ci, /* {{{ */
                              void **user_data) {
  oconfig_item_t *ci_chain;

  if (ci->children_num != 1) {
    ERROR("Filter subsystem: The built-in target `jump' needs exactly "
          "one `Chain' argument!");
    return -1;
  }

  ci_chain = ci->children;
  if (strcasecmp("Chain", ci_chain->key) != 0) {
    ERROR("Filter subsystem: The built-in target `jump' does not "
          "support the configuration option `%s'.",
          ci_chain->key);
    return -1;
  }

  if ((ci_chain->values_num != 1) ||
      (ci_chain->values[0].type != OCONFIG_TYPE_STRING)) {
    ERROR("Filter subsystem: Built-in target `jump': The `Chain' option "
          "needs exactly one string argument.");
    return -1;
  }

  *user_data = fc_strdup(ci_chain->values[0].value.string);
  if (*user_data == NULL) {
    ERROR("fc_bit_jump_create: fc_strdup failed.");
    return -1;
  }

  return 0;
} /* }}} int fc_bit_jump_create */

static int fc_bit_jump_destroy(void **user_data) /* {{{ */
{
  if (user_data != NULL) {
    free(*user_data);
    *user_data = NULL;
  }

  return 0;
} /* }}} int fc_bit_jump_destroy */

static int fc_bit_jump_invoke(const data_set_t *ds, /* {{{ */
                              value_list_t *vl,
                              notification_meta_t __attribute__((unused)) *
                                  *meta,
                              void **user_data) {
  char *chain_name;
  fc_chain_t *chain;
  int status;

  chain_name = *user_data;

  for (chain = chain_list_head; chain != NULL; chain = chain->next)
    if (strcasecmp(chain_name, chain->name) == 0)
      break;

  if (chain == NULL) {
    ERROR("Filter subsystem: Built-in target `jump': There is no chain "
          "named `%s'.",
          chain_name);
    return -1;
  }

  status = fc_process_chain(ds, vl, chain);
  if (status < 0)
    return status;
  else if (status == FC_TARGET_STOP)
    return FC_TARGET_STOP;
  else
    return FC_TARGET_CONTINUE;
} /* }}} int fc_bit_jump_invoke */

static int
fc_bit_stop_invoke(const data_set_t __attribute__((unused)) * ds, /* {{{ */
                   value_list_t __attribute__((unused)) * vl,
                   notification_meta_t __attribute__((unused)) * *meta,
                   void __attribute__((unused)) * *user_data) {
  return FC_TARGET_STOP;
} /* }}} int fc_bit_stop_invoke */

static int
fc_bit_return_invoke(const data_set_t __attribute__((unused)) * ds, /* {{{ */
                     value_list_t __attribute__((unused)) * vl,
                     notification_meta_t __attribute__((unused)) * *meta,
                     void __attribute__((unused)) * *user_data) {
  return FC_TARGET_RETURN;
} /* }}} int fc_bit_return_invoke */

static int fc_bit_write_create(const oconfig_item_t *ci, /* {{{ */
                               void **user_data) {
  fc_writer_t *plugin_list = NULL;
  size_t plugin_list_len = 0;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    fc_writer_t *temp;

    if (strcasecmp("Plugin", child->key) != 0) {
      ERROR("Filter subsystem: The built-in target `write' does not "
            "support the configuration option `%s'.",
            child->key);
      continue;
    }

    for (int j = 0; j < child->values_num; j++) {
      char *plugin;

      if (child->values[j].type != OCONFIG_TYPE_STRING) {
        ERROR("Filter subsystem: Built-in target `write': "
              "The `Plugin' option accepts only string arguments.");
        continue;
      }
      plugin = child->values[j].value.string;

      temp =
          realloc(plugin_list, (plugin_list_len + 2) * (sizeof(*plugin_list)));
      if (temp == NULL) {
        ERROR("fc_bit_write_create: realloc failed.");
        continue;
      }
      plugin_list = temp;

      plugin_list[plugin_list_len].plugin = fc_strdup(plugin);
      if (plugin_list[plugin_list_len].plugin == NULL) {
        ERROR("fc_bit_write_create: fc_strdup failed.");
        continue;
      }
      C_COMPLAIN_INIT(&plugin_list[plugin_list_len].complaint);
      plugin_list_len++;
      plugin_list[plugin_list_len].plugin = NULL;
    } /* for (j = 0; j < child->values_num; j++) */
  }   /* for (i = 0; i < ci->children_num; i++) */

  *user_data = plugin_list;

  return 0;
} /* }}} int fc_bit_write_create */

static int fc_bit_write_destroy(void **user_data) /* {{{ */
{
  fc_writer_t *plugin_list;

  if ((user_data == NULL) || (*user_data == NULL))
    return 0;

  plugin_list = *user_data;

  for (size_t i = 0; plugin_list[i].plugin != NULL; i++)
    free(plugin_list[i].plugin);
  free(plugin_list);

  return 0;
} /* }}} int fc_bit_write_destroy */

static int fc_bit_write_invoke(const data_set_t *ds, /* {{{ */
                               value_list_t *vl,
                               notification_meta_t __attribute__((unused)) *
                                   *meta,
                               void **user_data) {
  fc_writer_t *plugin_list;
  int status;

  plugin_list = NULL;
  if (user_data != NULL)
    plugin_list = *user_data;

  if ((plugin_list == NULL) || (plugin_list[0].plugin == NULL)) {
    static c_complain_t write_complaint = C_COMPLAIN_INIT_STATIC;

    status = plugin_write(/* plugin = */ NULL, ds, vl);
    if (status == ENOENT) {
      /* in most cases this is a permanent error, so use the complain
       * mechanism rather than spamming the logs */
      c_complain(
          LOG_INFO, &write_complaint,
          "Filter subsystem: Built-in target `write': Dispatching value to "
          "all write plugins failed with status %i (ENOENT). "
          "Most likely this means you didn't load any write plugins.",
          status);

      plugin_log_available_writers();
    } else if (status != 0) {
      /* often, this is a permanent error (e.g. target system unavailable),
       * so use the complain mechanism rather than spamming the logs */
      c_complain(
          LOG_INFO, &write_complaint,
          "Filter subsystem: Built-in target `write': Dispatching value to "
          "all write plugins failed with status %i.",
          status);
    } else {
      assert(status == 0);
      c_release(LOG_INFO, &write_complaint,
                "Filter subsystem: "
                "Built-in target `write': Some write plugin is back to normal "
                "operation. `write' succeeded.");
    }
  } else {
    for (size_t i = 0; plugin_list[i].plugin != NULL; i++) {
      status = plugin_write(plugin_list[i].plugin, ds, vl);
      if (status != 0) {
        c_complain(
            LOG_INFO, &plugin_list[i].complaint,
            "Filter subsystem: Built-in target `write': Dispatching value to "
            "the `%s' plugin failed with status %i.",
            plugin_list[i].plugin, status);

        plugin_log_available_writers();
      } else {
        c_release(
            LOG_INFO, &plugin_list[i].complaint,
            "Filter subsystem: Built-in target `write': Plugin `%s' is back "
            "to normal operation. `write' succeeded.",
            plugin_list[i].plugin);
      }
    } /* for (i = 0; plugin_list[i] != NULL; i++) */
  }

  return FC_TARGET_CONTINUE;
} /* }}} int fc_bit_write_invoke */

static int fc_init_once(void) /* {{{ */
{
  static int done;
  target_proc_t tproc = {0};

  if (done != 0)
    return 0;

  tproc.create = fc_bit_jump_create;
  tproc.destroy = fc_bit_jump_destroy;
  tproc.invoke = fc_bit_jump_invoke;
  fc_register_target("jump", tproc);

  memset(&tproc, 0, sizeof(tproc));
  tproc.create = NULL;
  tproc.destroy = NULL;
  tproc.invoke = fc_bit_stop_invoke;
  fc_register_target("stop", tproc);

  memset(&tproc, 0, sizeof(tproc));
  tproc.create = NULL;
  tproc.destroy = NULL;
  tproc.invoke = fc_bit_return_invoke;
  fc_register_target("return", tproc);

  memset(&tproc, 0, sizeof(tproc));
  tproc.create = fc_bit_write_create;
  tproc.destroy = fc_bit_write_destroy;
  tproc.invoke = fc_bit_write_invoke;
  fc_register_target("write", tproc);

  done++;
  return 0;
} /* }}} int fc_init_once */

/*
 * Public functions
 */
/* Add a match to list of available matches. */
int fc_register_match(const char *name, match_proc_t proc) /* {{{ */
{
  fc_match_t *m;

  DEBUG("fc_register_match (%s);", name);

  m = calloc(1, sizeof(*m));
  if (m == NULL)
    return -ENOMEM;

  sstrncpy(m->name, name, sizeof(m->name));
  memcpy(&m->proc, &proc, sizeof(m->proc));

  if (match_list_head == NULL) {
    match_list_head = m;
  } else {
    fc_match_t *ptr;

    ptr = match_list_head;
    while (ptr->next != NULL)
      ptr = ptr->next;

    ptr->next = m;
  }

  return 0;
} /* }}} int fc_register_match */

/* Add a target to list of available targets. */
int fc_register_target(const char *name, target_proc_t proc) /* {{{ */
{
  fc_target_t *t;

  DEBUG("fc_register_target (%s);", name);

  t = calloc(1, sizeof(*t));
  if (t == NULL)
    return -ENOMEM;

  sstrncpy(t->name, name, sizeof(t->name));
  memcpy(&t->proc, &proc, sizeof(t->proc));

  if (target_list_head == NULL) {
    target_list_head = t;
  } else {
    fc_target_t *ptr;

    ptr = target_list_head;
    while (ptr->next != NULL)
      ptr = ptr->next;

    ptr->next = t;
  }

  return 0;
} /* }}} int fc_register_target */

fc_chain_t *fc_chain_get_by_name(const char *chain_name) /* {{{ */
{
  if (chain_name == NULL)
    return NULL;

  for (fc_chain_t *chain = chain_list_head; chain != NULL; chain = chain->next)
    if (strcasecmp(chain_name, chain->name) == 0)
      return chain;

  return NULL;
} /* }}} int fc_chain_get_by_name */

int fc_process_chain(const data_set_t *ds, value_list_t *vl, /* {{{ */
                     fc_chain_t *chain) {
  fc_target_t *target;
  int status = FC_TARGET_CONTINUE;

  if (chain == NULL)
    return -1;

  DEBUG("fc_process_chain (chain = %s);", chain->name);

  for (fc_rule_t *rule = chain->rules; rule != NULL; rule = rule->next) {
    fc_match_t *match;
    status = FC_TARGET_CONTINUE;

    if (rule->name[0] != 0) {
      DEBUG("fc_process_chain (%s): Testing the `%s' rule.", chain->name,
            rule->name);
    }

    /* N. B.: rule->matches may be NULL. */
    for (match = rule->matches; match != NULL; match = match->next) {
      /* FIXME: Pass the meta-data to match targets here (when implemented). */
      status =
          (*match->proc.match)(ds, vl, /* meta = */ NULL, &match->user_data);
      if (status < 0) {
        WARNING("fc_process_chain (%s): A match failed.", chain->name);
        break;
      } else if (status != FC_MATCH_MATCHES)
        break;
    }

    /* for-loop has been aborted: Either error or no match. */
    if (match != NULL) {
      status = FC_TARGET_CONTINUE;
      continue;
    }

    if (rule->name[0] != 0) {
      DEBUG("fc_process_chain (%s): Rule `%s' matches.", chain->name,
            rule->name);
    }

    for (target = rule->targets; target != NULL; target = target->next) {
      /* If we get here, all matches have matched the value. Execute the
       * target. */
      /* FIXME: Pass the meta-data to match targets here (when implemented). */
      status =
          (*target->proc.invoke)(ds, vl, /* meta = */ NULL, &target->user_data);
      if (status < 0) {
        WARNING("fc_process_chain (%s): A target failed.", chain->name);
        continue;
      } else if (status == FC_TARGET_CONTINUE)
        continue;
      else if (status == FC_TARGET_STOP)
        break;
      else if (status == FC_TARGET_RETURN)
        break;
      else {
        WARNING("fc_process_chain (%s): Unknown return value "
                "from target `%s': %i",
                chain->name, target->name, status);
      }
    }

    if ((status == FC_TARGET_STOP) || (status == FC_TARGET_RETURN)) {
      if (rule->name[0] != 0) {
        DEBUG("fc_process_chain (%s): Rule `%s' signaled "
              "the %s condition.",
              chain->name, rule->name,
              (status == FC_TARGET_STOP) ? "stop" : "return");
      }
      break;
    }
  } /* for (rule) */

  if ((status == FC_TARGET_STOP) || (status == FC_TARGET_RETURN))
    return status;

  DEBUG("fc_process_chain (%s): Executing the default targets.", chain->name);

  status = FC_TARGET_CONTINUE;
  for (target = chain->targets; target != NULL; target = target->next) {
    /* If we get here, all matches have matched the value. Execute the
     * target. */
    /* FIXME: Pass the meta-data to match targets here (when implemented). */
    status =
        (*target->proc.invoke)(ds, vl, /* meta = */ NULL, &target->user_data);
    if (status < 0) {
      WARNING("fc_process_chain (%s): The default target failed.", chain->name);
    } else if (status == FC_TARGET_CONTINUE)
      continue;
    else if (status == FC_TARGET_STOP)
      break;
    else if (status == FC_TARGET_RETURN)
      break;
    else {
      WARNING("fc_process_chain (%s): Unknown return value "
              "from target `%s': %i",
              chain->name, target->name, status);
    }
  }

  if ((status == FC_TARGET_STOP) || (status == FC_TARGET_RETURN)) {
    assert(target != NULL);
    DEBUG("fc_process_chain (%s): Default target `%s' signaled "
          "the %s condition.",
          chain->name, target->name,
          (status == FC_TARGET_STOP) ? "stop" : "return");
    if (status == FC_TARGET_STOP)
      return FC_TARGET_STOP;
    else
      return FC_TARGET_CONTINUE;
  }

  DEBUG("fc_process_chain (%s): Signaling `continue' at end of chain.",
        chain->name);

  return FC_TARGET_CONTINUE;
} /* }}} int fc_process_chain */

/* Iterate over all rules in the chain and execute all targets for which all
 * matches match. */
int fc_default_action(const data_set_t *ds, value_list_t *vl) /* {{{ */
{
  /* FIXME: Pass the meta-data to match targets here (when implemented). */
  return fc_bit_write_invoke(ds, vl, NULL, NULL);
} /* }}} int fc_default_action */

int fc_configure(const oconfig_item_t *ci) /* {{{ */
{
  fc_init_once();

  if (ci == NULL)
    return -EINVAL;

  if (strcasecmp("Chain", ci->key) == 0)
    return fc_config_add_chain(ci);

  WARNING("Filter subsystem: Unknown top level config option `%s'.", ci->key);

  return -1;
} /* }}} int fc_configure */
