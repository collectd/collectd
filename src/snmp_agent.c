/**
 * collectd - src/snmp_agent.c
 *
 * Copyright(c) 2017-2018 Intel Corporation. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Roman Korynkevych <romanx.korynkevych@intel.com>
 *   Serhiy Pshyk <serhiyx.pshyk@intel.com>
 *   Marcin Mozejko <marcinx.mozejko@intel.com>
 **/

#include "collectd.h"

#include "utils/avltree/avltree.h"
#include "utils/common/common.h"
#include "utils_cache.h"
#include "utils_llist.h"
#include <regex.h>

#include <net-snmp/net-snmp-config.h>

#include <net-snmp/net-snmp-includes.h>

#include <net-snmp/agent/net-snmp-agent-includes.h>

#define PLUGIN_NAME "snmp_agent"
#define TYPE_STRING -1
#define GROUP_UNUSED -1
#define OID_EXISTS 1
#define MAX_KEY_SOURCES 5
#define MAX_INDEX_KEYS 5
#define MAX_MATCHES 5

/* Identifies index key source */
enum index_key_src_e {
  INDEX_HOST = 0,
  INDEX_PLUGIN,
  INDEX_PLUGIN_INSTANCE,
  INDEX_TYPE,
  INDEX_TYPE_INSTANCE
};
typedef enum index_key_src_e index_key_src_t;

struct index_key_s {
  index_key_src_t source;
  u_char type;
  char *regex; /* Pattern used to parse index key source string */
  int group;   /* If pattern gives more than one group we can specify which one
                  we want to take */
  regex_t regex_info;
};
typedef struct index_key_s index_key_t;

struct oid_s {
  oid oid[MAX_OID_LEN];
  size_t oid_len;
  u_char type;
};
typedef struct oid_s oid_t;

struct token_s {
  char *str;
  netsnmp_variable_list *key; /* Points to succeeding key */
};
typedef struct token_s token_t;

struct table_definition_s {
  char *name;
  oid_t index_oid;
  oid_t size_oid;
  llist_t *columns;
  c_avl_tree_t *instance_index;
  c_avl_tree_t *index_instance;
  c_avl_tree_t *instance_oids; /* Tells us how many OIDs registered for every
                                  instance; */
  index_key_t index_keys[MAX_INDEX_KEYS]; /* Stores information about what each
                                             index key represents */
  int index_keys_len;
  netsnmp_variable_list *index_list_cont; /* Index key container used for
                                             generating as well as parsing
                                             OIDs, not thread-safe */
  c_avl_tree_t *tokens[MAX_KEY_SOURCES];  /* Input string after regex execution
                                             will be split into sepearate
                                             tokens */

  bool tokens_done; /* Set to true when all tokens are generated */
};
typedef struct table_definition_s table_definition_t;

struct data_definition_s {
  char *name;
  char *plugin;
  char *plugin_instance;
  char *type;
  char *type_instance;
  const table_definition_t *table;
  bool is_index_key; /* indicates if table column is an index key */
  int index_key_pos; /* position in indexes list */
  oid_t *oids;
  size_t oids_len;
  double scale;
  double shift;
};
typedef struct data_definition_s data_definition_t;

struct snmp_agent_ctx_s {
  pthread_t thread;
  pthread_mutex_t lock;
  pthread_mutex_t agentx_lock;
  struct tree *tp;

  llist_t *tables;
  llist_t *scalars;
  c_avl_tree_t *registered_oids; /* AVL tree containing all registered OIDs */
};
typedef struct snmp_agent_ctx_s snmp_agent_ctx_t;

static snmp_agent_ctx_t *g_agent;
static const char *index_opts[MAX_KEY_SOURCES] = {
    "Hostname", "Plugin", "PluginInstance", "Type", "TypeInstance"};

#define CHECK_DD_TYPE(_dd, _p, _pi, _t, _ti)                                   \
  (_dd->plugin ? !strcmp(_dd->plugin, _p) : 0) &&                              \
      (_dd->plugin_instance ? !strcmp(_dd->plugin_instance, _pi) : 1) &&       \
      (_dd->type ? !strcmp(_dd->type, _t) : 0) &&                              \
      (_dd->type_instance ? !strcmp(_dd->type_instance, _ti) : 1)

static int snmp_agent_shutdown(void);
static void *snmp_agent_thread_run(void *arg);
static int snmp_agent_register_oid(oid_t *oid, Netsnmp_Node_Handler *handler);
static int snmp_agent_set_vardata(void *dst_buf, size_t *dst_buf_len,
                                  u_char asn_type, double scale, double shift,
                                  const void *value, size_t len, int type);
static int snmp_agent_unregister_oid_index(oid_t *oid, int index);
static int snmp_agent_update_instance_oids(c_avl_tree_t *tree, oid_t *index_oid,
                                           int value);
static int num_compare(const int *a, const int *b);

static u_char snmp_agent_get_asn_type(oid *oid, size_t oid_len) {
  struct tree *node = get_tree(oid, oid_len, g_agent->tp);

  return (node != NULL) ? mib_to_asn_type(node->type) : 0;
}

static char *snmp_agent_get_oid_name(oid *oid, size_t oid_len) {
  struct tree *node = get_tree(oid, oid_len, g_agent->tp);

  return (node != NULL) ? node->label : NULL;
}

static int snmp_agent_oid_to_string(char *buf, size_t buf_size,
                                    oid_t const *o) {
  char oid_str[MAX_OID_LEN][16];
  char *oid_str_ptr[MAX_OID_LEN];

  for (size_t i = 0; i < o->oid_len; i++) {
    snprintf(oid_str[i], sizeof(oid_str[i]), "%lu", (unsigned long)o->oid[i]);
    oid_str_ptr[i] = oid_str[i];
  }

  return strjoin(buf, buf_size, oid_str_ptr, o->oid_len, ".");
}

/* Prints a configuration storing list. It handles both table columns list
   and scalars list */
#if COLLECT_DEBUG
static void snmp_agent_dump_data(llist_t *list) {
  char oid_str[DATA_MAX_NAME_LEN];
  for (llentry_t *de = llist_head(list); de != NULL; de = de->next) {
    data_definition_t *dd = de->value;
    table_definition_t const *td = dd->table;

    if (dd->table != NULL)
      DEBUG(PLUGIN_NAME ":   Column:");
    else
      DEBUG(PLUGIN_NAME ": Scalar:");

    DEBUG(PLUGIN_NAME ":     Name: %s", dd->name);
    if (dd->plugin)
      DEBUG(PLUGIN_NAME ":     Plugin: %s", dd->plugin);
    if (dd->plugin_instance)
      DEBUG(PLUGIN_NAME ":     PluginInstance: %s", dd->plugin_instance);
    if (dd->is_index_key) {
      index_key_t const *index_key = &td->index_keys[dd->index_key_pos];

      DEBUG(PLUGIN_NAME ":     IndexKey:");
      DEBUG(PLUGIN_NAME ":       Source: %s", index_opts[index_key->source]);
      DEBUG(PLUGIN_NAME ":       Type: %s",
            (index_key->type == ASN_INTEGER) ? "Integer" : "String");
      if (index_key->regex)
        DEBUG(PLUGIN_NAME ":       Regex: %s", index_key->regex);
      if (index_key->group != GROUP_UNUSED)
        DEBUG(PLUGIN_NAME ":       Group: %d", index_key->group);
    }
    if (dd->type)
      DEBUG(PLUGIN_NAME ":     Type: %s", dd->type);
    if (dd->type_instance)
      DEBUG(PLUGIN_NAME ":     TypeInstance: %s", dd->type_instance);
    for (size_t i = 0; i < dd->oids_len; i++) {
      snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &dd->oids[i]);
      DEBUG(PLUGIN_NAME ":     OID[%" PRIsz "]: %s", i, oid_str);
    }
    DEBUG(PLUGIN_NAME ":   Scale: %g", dd->scale);
    DEBUG(PLUGIN_NAME ":   Shift: %g", dd->shift);
  }
}

/* Prints parsed configuration */
static void snmp_agent_dump_config(void) {
  char oid_str[DATA_MAX_NAME_LEN];

  /* Printing tables */
  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    DEBUG(PLUGIN_NAME ": Table:");
    DEBUG(PLUGIN_NAME ":   Name: %s", td->name);
    if (td->index_oid.oid_len != 0) {
      snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &td->index_oid);
      DEBUG(PLUGIN_NAME ":   IndexOID: %s", oid_str);
    }
    if (td->size_oid.oid_len != 0) {
      snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &td->size_oid);
      DEBUG(PLUGIN_NAME ":   SizeOID: %s", oid_str);
    }

    snmp_agent_dump_data(td->columns);
  }

  /* Printing scalars */
  snmp_agent_dump_data(g_agent->scalars);
}
#endif /* COLLECT_DEBUG */

static int snmp_agent_validate_config(void) {

#if COLLECT_DEBUG
  snmp_agent_dump_config();
#endif

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    if (!td->index_keys_len) {
      ERROR(PLUGIN_NAME ": Index keys not defined for '%s'", td->name);
      return -EINVAL;
    }

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      if (!dd->plugin) {
        ERROR(PLUGIN_NAME ": Plugin not defined for '%s'.'%s'", td->name,
              dd->name);
        return -EINVAL;
      }

      if (dd->plugin_instance) {
        ERROR(PLUGIN_NAME ": PluginInstance should not be defined for table "
                          "data type '%s'.'%s'",
              td->name, dd->name);
        return -EINVAL;
      }

      if (dd->oids_len == 0) {
        ERROR(PLUGIN_NAME ": No OIDs defined for '%s'.'%s'", td->name,
              dd->name);
        return -EINVAL;
      }

      if (dd->is_index_key) {
        if (dd->type || dd->type_instance) {
          ERROR(PLUGIN_NAME ": Type and TypeInstance are not valid for "
                            "index data '%s'.'%s'",
                td->name, dd->name);
          return -EINVAL;
        }

        if (dd->oids_len > 1) {
          ERROR(
              PLUGIN_NAME
              ": Only one OID should be specified for instance data '%s'.'%s'",
              td->name, dd->name);
          return -EINVAL;
        }
      } else {

        if (!dd->type) {
          ERROR(PLUGIN_NAME ": Type not defined for data '%s'.'%s'", td->name,
                dd->name);
          return -EINVAL;
        }
      }
    }
  }

  for (llentry_t *e = llist_head(g_agent->scalars); e != NULL; e = e->next) {
    data_definition_t *dd = e->value;

    if (!dd->plugin) {
      ERROR(PLUGIN_NAME ": Plugin not defined for '%s'", dd->name);
      return -EINVAL;
    }

    if (dd->oids_len == 0) {
      ERROR(PLUGIN_NAME ": No OIDs defined for '%s'", dd->name);
      return -EINVAL;
    }

    if (dd->is_index_key) {
      ERROR(PLUGIN_NAME ": Index field can't be specified for scalar data '%s'",
            dd->name);
      return -EINVAL;
    }

    if (!dd->type) {
      ERROR(PLUGIN_NAME ": Type not defined for data '%s'", dd->name);
      return -EINVAL;
    }
  }

  return 0;
}

static int snmp_agent_parse_index_key(const char *input, regex_t *regex_info,
                                      int gi, regmatch_t *m) {
  regmatch_t matches[MAX_MATCHES];

  int ret = regexec(regex_info, input, MAX_MATCHES, matches, 0);
  if (!ret) {
    if (gi > regex_info->re_nsub) {
      ERROR(PLUGIN_NAME ": Group index %d not found. Check regex config", gi);
      return -1;
    }
    *m = matches[gi];
  } else if (ret == REG_NOMATCH) {
    ERROR(PLUGIN_NAME ": No match found");
    return -1;
  } else {
    char msgbuf[100];

    regerror(ret, regex_info, msgbuf, sizeof(msgbuf));
    ERROR(PLUGIN_NAME ": Regex match failed: %s", msgbuf);
    return -1;
  }

  return 0;
}

static int snmp_agent_create_token(char const *input, int t_off, int n,
                                   c_avl_tree_t *tree,
                                   netsnmp_variable_list *index_key) {
  assert(tree != NULL);

  token_t *token = malloc(sizeof(*token));

  if (token == NULL)
    goto error;

  int *offset = malloc(sizeof(*offset));

  if (offset == NULL)
    goto free_token_error;

  int ret = 0;

  token->key = index_key;
  input += t_off;
  size_t len = strlen(input);

  if (n < len)
    len = n;

  token->str = malloc(len + 1);

  if (token->str == NULL)
    goto free_offset_error;

  /* copy at most n bytes from input with offset t_off into token->str */
  sstrncpy(token->str, input, len + 1);
  *offset = t_off;
  ret = c_avl_insert(tree, (void *)offset, (void *)token);

  if (ret == 0)
    return 0;

  sfree(token->str);

free_offset_error:
  sfree(offset);

free_token_error:
  sfree(token);

error:
  ERROR(PLUGIN_NAME ": Could not allocate memory to create token");

  return -1;
}

static int snmp_agent_delete_token(int t_off, c_avl_tree_t *tree) {
  token_t *token = NULL;
  int *offset = NULL;

  int ret = c_avl_remove(tree, &t_off, (void **)&offset, (void **)&token);

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Could not delete token");
    return -1;
  }

  sfree(token->str);
  sfree(token);
  sfree(offset);
  return 0;
}

static int snmp_agent_get_token(c_avl_tree_t *tree, int mpos) {

  int *pos;
  char *token;
  int prev_pos = 0;

  c_avl_iterator_t *it = c_avl_get_iterator(tree);
  while (c_avl_iterator_next(it, (void **)&pos, (void **)&token) == 0) {
    if (*pos >= mpos)
      break;
    else
      prev_pos = *pos;
  }

  c_avl_iterator_destroy(it);
  return prev_pos;
}

static int snmp_agent_tokenize(const char *input, c_avl_tree_t *tokens,
                               const regmatch_t *m,
                               netsnmp_variable_list *key) {
  assert(tokens != NULL);

  int ret = 0;
  int len = strlen(input);

  /* Creating first token that is going to be split later */
  if (c_avl_size(tokens) == 0) {
    ret = snmp_agent_create_token(input, 0, len, tokens, NULL);
    if (ret != 0)
      return ret;
  }

  /* Divide token that contains current match into two */
  int t_pos = snmp_agent_get_token(tokens, m->rm_so);
  ret = snmp_agent_delete_token(t_pos, tokens);

  if (ret != 0)
    return -1;

  ret = snmp_agent_create_token(input, t_pos, m->rm_so - t_pos, tokens, key);

  if (ret != 0)
    return -1;

  if (len - m->rm_eo > 1) {
    ret = snmp_agent_create_token(input, m->rm_eo, len - m->rm_eo + 1, tokens,
                                  NULL);
    if (ret != 0) {
      snmp_agent_delete_token(t_pos, tokens);
      return -1;
    }
  }

  return 0;
}

static int snmp_agent_fill_index_list(table_definition_t *td,
                                      value_list_t const *vl) {
  int ret;
  int i;
  netsnmp_variable_list *key = td->index_list_cont;
  char const *ptr;

  for (i = 0; i < td->index_keys_len; i++) {
    /* var should never be NULL */
    assert(key != NULL);
    ptr = NULL;
    const index_key_src_t source = td->index_keys[i].source;
    c_avl_tree_t *const tokens = td->tokens[source];
    /* Generating list filled with all data necessary to generate an OID */
    switch (source) {
    case INDEX_HOST:
      ptr = vl->host;
      break;
    case INDEX_PLUGIN:
      ptr = vl->plugin;
      break;
    case INDEX_PLUGIN_INSTANCE:
      ptr = vl->plugin_instance;
      break;
    case INDEX_TYPE:
      ptr = vl->type;
      break;
    case INDEX_TYPE_INSTANCE:
      ptr = vl->type_instance;
      break;
    default:
      ERROR(PLUGIN_NAME ": Unknown index key source provided");
      return -EINVAL;
    }

    /* Parsing input string if necessary */
    if (td->index_keys[i].regex) {
      regmatch_t m;

      /* Parsing input string */
      ret = snmp_agent_parse_index_key(ptr, &td->index_keys[i].regex_info,
                                       td->index_keys[i].group, &m);
      if (ret != 0) {
        ERROR(PLUGIN_NAME ": Error executing regex");
        return ret;
      }

      /* Tokenizing input string if not done yet */
      if (td->tokens_done == false)
        ret = snmp_agent_tokenize(ptr, tokens, &m, key);

      if (ret != 0)
        return -1;

      if (td->index_keys[i].type == ASN_INTEGER) {
        int val = strtol(ptr + m.rm_so, NULL, 0);

#ifdef HAVE_NETSNMP_OLD_API
        ret = snmp_set_var_value(key, (const u_char *)&val, sizeof(val));
#else
        ret = snmp_set_var_value(key, &val, sizeof(val));
#endif
      } else
#ifdef HAVE_NETSNMP_OLD_API
        ret = snmp_set_var_value(key, (const u_char *)(ptr + m.rm_so),
                                 m.rm_eo - m.rm_so);
#else
        ret = snmp_set_var_value(key, ptr + m.rm_so, m.rm_eo - m.rm_so);
#endif
    } else
#ifdef HAVE_NETSNMP_OLD_API
      ret = snmp_set_var_value(key, (const u_char *)ptr, strlen(ptr));
#else
      ret = snmp_set_var_value(key, ptr, strlen(ptr));
#endif

    if (ret != 0)
      return -1;

    key = key->next_variable;
  }

  /* Tokens for all source strings are generated */
  for (i = 0; i < MAX_KEY_SOURCES; i++)
    td->tokens_done = true;

  return 0;
}

static int snmp_agent_prep_index_list(table_definition_t const *td,
                                      netsnmp_variable_list **index_list) {
  /* Generating list having only the structure (with no values) letting us
   * know how to parse an OID*/
  for (int i = 0; i < td->index_keys_len; i++) {
    switch (td->index_keys[i].source) {
    case INDEX_HOST:
    case INDEX_PLUGIN:
    case INDEX_PLUGIN_INSTANCE:
    case INDEX_TYPE:
    case INDEX_TYPE_INSTANCE:
      snmp_varlist_add_variable(index_list, NULL, 0, td->index_keys[i].type,
                                NULL, 0);
      break;
    default:
      ERROR(PLUGIN_NAME ": Unknown index key source provided");
      return -EINVAL;
    }
  }
  return 0;
}

static int snmp_agent_generate_index(table_definition_t *td,
                                     value_list_t const *vl, oid_t *index_oid) {

  /* According to given information by index_keys list
   * index OID is going to be built
   */
  int ret = snmp_agent_fill_index_list(td, vl);
  if (ret != 0)
    return -EINVAL;

  /* Building only index part OID (without table prefix OID) */
  ret = build_oid_noalloc(index_oid->oid, sizeof(index_oid->oid),
                          &index_oid->oid_len, NULL, 0, td->index_list_cont);
  if (ret != SNMPERR_SUCCESS) {
    ERROR(PLUGIN_NAME ": Error building index OID");
    return -EINVAL;
  }

  return 0;
}

/* It appends one OID to the end of another */
static int snmp_agent_append_oid(oid_t *out, const oid_t *in) {

  if (out->oid_len + in->oid_len > MAX_OID_LEN) {
    ERROR(PLUGIN_NAME ": Cannot create OID. Output length is too long!");
    return -EINVAL;
  }
  memcpy(&out->oid[out->oid_len], in->oid, in->oid_len * sizeof(oid));
  out->oid_len += in->oid_len;

  return 0;
}

static int snmp_agent_register_oid_string(const oid_t *oid,
                                          const oid_t *index_oid,
                                          Netsnmp_Node_Handler *handler) {
  oid_t new_oid;

  memcpy(&new_oid, oid, sizeof(*oid));
  /* Concatenating two string oids */
  int ret = snmp_agent_append_oid(&new_oid, index_oid);
  if (ret != 0)
    return ret;

  return snmp_agent_register_oid(&new_oid, handler);
}

static int snmp_agent_unregister_oid(oid_t *oid) {
  int ret = c_avl_remove(g_agent->registered_oids, (void *)oid, NULL, NULL);

  if (ret != 0)
    ERROR(PLUGIN_NAME ": Could not delete registration info");

  return unregister_mib(oid->oid, oid->oid_len);
}

static int snmp_agent_unregister_oid_string(oid_t *oid,
                                            const oid_t *index_oid) {
  oid_t new_oid;
  char oid_str[DATA_MAX_NAME_LEN];

  memcpy(&new_oid, oid, sizeof(*oid));
  /* Concatenating two string oids */
  int ret = snmp_agent_append_oid(&new_oid, index_oid);
  if (ret != 0)
    return ret;

  snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &new_oid);
  DEBUG(PLUGIN_NAME ": Unregistered handler for OID (%s)", oid_str);

  return snmp_agent_unregister_oid(&new_oid);
}

static void snmp_agent_table_data_remove(data_definition_t *dd,
                                         table_definition_t *td,
                                         oid_t *index_oid) {
  int *index = NULL;
  oid_t *ind_oid = NULL;

  if (td->index_oid.oid_len) {
    if ((c_avl_get(td->instance_index, index_oid, (void **)&index) != 0) ||
        (c_avl_get(td->index_instance, index, NULL) != 0))
      return;
  } else {
    if (c_avl_get(td->instance_index, index_oid, NULL) != 0)
      return;
  }

  pthread_mutex_lock(&g_agent->agentx_lock);

  int reg_oids = -1; /* Number of registered oids for given instance */

  for (size_t i = 0; i < dd->oids_len; i++) {
    if (td->index_oid.oid_len)
      snmp_agent_unregister_oid_index(&dd->oids[i], *index);
    else
      snmp_agent_unregister_oid_string(&dd->oids[i], index_oid);

    reg_oids =
        snmp_agent_update_instance_oids(td->instance_oids, index_oid, -1);
  }

  /* Checking if any metrics are left registered */
  if (reg_oids != 0) {
    pthread_mutex_unlock(&g_agent->agentx_lock);
    return;
  }

  /* All metrics have been unregistered. Unregistering index key OIDs */
  int keys_processed = 0;
  for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
    data_definition_t *idd = de->value;

    if (!idd->is_index_key)
      continue;

    for (size_t i = 0; i < idd->oids_len; i++)
      if (td->index_oid.oid_len)
        snmp_agent_unregister_oid_index(&idd->oids[i], *index);
      else
        snmp_agent_unregister_oid_string(&idd->oids[i], index_oid);

    if (++keys_processed >= td->index_keys_len)
      break;
  }
  pthread_mutex_unlock(&g_agent->agentx_lock);

  /* All OIDs have been unregistered so we dont need this instance registered
   * as well */
  char index_str[DATA_MAX_NAME_LEN];

  if (index == NULL)
    snmp_agent_oid_to_string(index_str, sizeof(index_str), index_oid);
  else
    snprintf(index_str, sizeof(index_str), "%d", *index);

  notification_t n = {
      .severity = NOTIF_WARNING, .time = cdtime(), .plugin = PLUGIN_NAME};
  sstrncpy(n.host, hostname_g, sizeof(n.host));
  snprintf(n.message, sizeof(n.message),
           "Removed data row from table %s with index %s", td->name, index_str);
  DEBUG(PLUGIN_NAME ": %s", n.message);
  plugin_dispatch_notification(&n);

  int *val = NULL;

  c_avl_remove(td->instance_oids, index_oid, NULL, (void **)&val);
  sfree(val);

  if (index != NULL) {
    pthread_mutex_lock(&g_agent->agentx_lock);
    snmp_agent_unregister_oid_index(&td->index_oid, *index);
    pthread_mutex_unlock(&g_agent->agentx_lock);

    c_avl_remove(td->index_instance, index, NULL, (void **)&ind_oid);
    c_avl_remove(td->instance_index, index_oid, NULL, (void **)&index);
    sfree(index);
    sfree(ind_oid);
  } else {
    c_avl_remove(td->instance_index, index_oid, NULL, NULL);
  }
}

static int snmp_agent_clear_missing(const value_list_t *vl,
                                    __attribute__((unused)) user_data_t *ud) {
  if (vl == NULL)
    return -EINVAL;

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      if (!dd->is_index_key) {
        if (CHECK_DD_TYPE(dd, vl->plugin, vl->plugin_instance, vl->type,
                          vl->type_instance)) {
          oid_t *index_oid = calloc(1, sizeof(*index_oid));

          if (index_oid == NULL) {
            ERROR(PLUGIN_NAME ": Could not allocate memory for index_oid");
            return -ENOMEM;
          }

          int ret = snmp_agent_generate_index(td, vl, index_oid);

          if (ret == 0)
            snmp_agent_table_data_remove(dd, td, index_oid);
          sfree(index_oid);

          return ret;
        }
      }
    }
  }

  return 0;
}

static void snmp_agent_free_data(data_definition_t **dd) {

  if (dd == NULL || *dd == NULL)
    return;

  /* unregister scalar type OID */
  if ((*dd)->table == NULL) {
    for (size_t i = 0; i < (*dd)->oids_len; i++)
      unregister_mib((*dd)->oids[i].oid, (*dd)->oids[i].oid_len);
  }

  sfree((*dd)->name);
  sfree((*dd)->plugin);
  sfree((*dd)->plugin_instance);
  sfree((*dd)->type);
  sfree((*dd)->type_instance);
  sfree((*dd)->oids);

  sfree(*dd);

  return;
}

static void snmp_agent_free_table_columns(table_definition_t *td) {
  if (td->columns == NULL)
    return;

  for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
    data_definition_t *dd = de->value;

    if (td->index_oid.oid_len) {
      int *index;
      oid_t *index_oid;

      c_avl_iterator_t *iter = c_avl_get_iterator(td->index_instance);
      while (c_avl_iterator_next(iter, (void *)&index, (void *)&index_oid) ==
             0) {
        for (size_t i = 0; i < dd->oids_len; i++)
          snmp_agent_unregister_oid_index(&dd->oids[i], *index);
      }
      c_avl_iterator_destroy(iter);
    } else {
      oid_t *index_oid;

      c_avl_iterator_t *iter = c_avl_get_iterator(dd->table->instance_index);
      while (c_avl_iterator_next(iter, (void *)&index_oid, NULL) == 0) {
        for (size_t i = 0; i < dd->oids_len; i++)
          snmp_agent_unregister_oid_string(&dd->oids[i], index_oid);
      }
      c_avl_iterator_destroy(iter);
    }

    snmp_agent_free_data(&dd);
  }

  llist_destroy(td->columns);
  td->columns = NULL;
} /* void snmp_agent_free_table_columns */

static void snmp_agent_free_table(table_definition_t **td) {

  if (td == NULL || *td == NULL)
    return;

  if ((*td)->size_oid.oid_len)
    unregister_mib((*td)->size_oid.oid, (*td)->size_oid.oid_len);

  oid_t *index_oid;

  /* Unregister Index OIDs */
  if ((*td)->index_oid.oid_len) {
    int *index;

    c_avl_iterator_t *iter = c_avl_get_iterator((*td)->index_instance);
    while (c_avl_iterator_next(iter, (void **)&index, (void **)&index_oid) == 0)
      snmp_agent_unregister_oid_index(&(*td)->index_oid, *index);

    c_avl_iterator_destroy(iter);
  }

  /* Unregister all table columns and their registered OIDs */
  snmp_agent_free_table_columns(*td);

  void *key = NULL;
  void *value = NULL;
  int *num = NULL;

  /* Removing data from instance_oids, leaving key pointers since they are still
   * used in other AVL trees */
  c_avl_iterator_t *iter = c_avl_get_iterator((*td)->instance_oids);
  while (c_avl_iterator_next(iter, (void **)&index_oid, (void **)&num) == 0)
    sfree(num);
  c_avl_iterator_destroy(iter);
  c_avl_destroy((*td)->instance_oids);

  /* index_instance and instance_index contain the same pointers */
  c_avl_destroy((*td)->index_instance);
  (*td)->index_instance = NULL;

  if ((*td)->instance_index != NULL) {
    while (c_avl_pick((*td)->instance_index, &key, &value) == 0) {
      if (key != value)
        sfree(key);
      sfree(value);
    }
    c_avl_destroy((*td)->instance_index);
    (*td)->instance_index = NULL;
  }
  snmp_free_varbind((*td)->index_list_cont);

  int i;
  token_t *tok = NULL;

  for (i = 0; i < (*td)->index_keys_len; i++) {
    sfree((*td)->index_keys[i].regex);
    regfree(&(*td)->index_keys[i].regex_info);
  }
  for (i = 0; i < MAX_KEY_SOURCES; i++)
    if ((*td)->tokens[i] != NULL) {
      while (c_avl_pick((*td)->tokens[i], &key, (void **)&tok) == 0) {
        sfree(key);
        sfree(tok->str);
        sfree(tok);
      }
      c_avl_destroy((*td)->tokens[i]);
      (*td)->tokens[i] = NULL;
    }
  sfree((*td)->name);
  sfree(*td);

  return;
}

static int snmp_agent_parse_oid_index_keys(const table_definition_t *td,
                                           oid_t *index_oid) {
  assert(index_oid != NULL);
  int ret = parse_oid_indexes(index_oid->oid, index_oid->oid_len,
                              td->index_list_cont);
  if (ret != SNMPERR_SUCCESS)
    ERROR(PLUGIN_NAME ": index OID parse error!");
  return ret;
}

static int snmp_agent_build_name(char **name, c_avl_tree_t *tokens) {
  int *pos;
  token_t *tok;
  char str[DATA_MAX_NAME_LEN];
  char out[DATA_MAX_NAME_LEN] = {0};
  c_avl_iterator_t *it = c_avl_get_iterator(tokens);

  if (it == NULL) {
    ERROR(PLUGIN_NAME ": Error getting tokens list iterator");
    return -1;
  }

  while (c_avl_iterator_next(it, (void **)&pos, (void **)&tok) == 0) {
    strncat(out, tok->str, DATA_MAX_NAME_LEN - strlen(out) - 1);
    if (tok->key != NULL) {
      if (tok->key->type == ASN_INTEGER) {
        snprintf(str, sizeof(str), "%ld", *tok->key->val.integer);
        strncat(out, str, DATA_MAX_NAME_LEN - strlen(out) - 1);
      } else /* OCTET_STR */
        strncat(out, (char *)tok->key->val.string,
                DATA_MAX_NAME_LEN - strlen(out) - 1);
    }
  }

  c_avl_iterator_destroy(it);
  *name = strdup(out);

  if (*name == NULL) {
    ERROR(PLUGIN_NAME ": Could not allocate memory");
    return -ENOMEM;
  }

  return 0;
}

static int snmp_agent_format_name(char *name, int name_len,
                                  data_definition_t *dd, oid_t *index_oid) {

  int ret = 0;

  if (index_oid == NULL) {
    /* It's a scalar */
    format_name(name, name_len, hostname_g, dd->plugin, dd->plugin_instance,
                dd->type, dd->type_instance);
  } else {
    /* Need to parse string index OID */
    const table_definition_t *td = dd->table;
    ret = snmp_agent_parse_oid_index_keys(td, index_oid);
    if (ret != 0)
      return ret;

    int i = 0;
    netsnmp_variable_list *key = td->index_list_cont;
    char str[DATA_MAX_NAME_LEN];
    char *fields[MAX_KEY_SOURCES] = {hostname_g, dd->plugin,
                                     dd->plugin_instance, dd->type,
                                     dd->type_instance};

    /* Looking for simple keys only */
    while (key != NULL) {
      if (!td->index_keys[i].regex) {
        index_key_src_t source = td->index_keys[i].source;

        if (source < INDEX_HOST || source > INDEX_TYPE_INSTANCE) {
          ERROR(PLUGIN_NAME ": Unkown index key source!");
          return -EINVAL;
        }

        if (td->index_keys[i].type == ASN_INTEGER) {
          snprintf(str, sizeof(str), "%ld", *key->val.integer);
          fields[source] = str;
        } else /* OCTET_STR */
          fields[source] = (char *)key->val.string;
      }
      key = key->next_variable;
      i++;
    }

    /* Keys with regexes */
    for (i = 0; i < MAX_KEY_SOURCES; i++) {
      if (td->tokens[i] == NULL)
        continue;
      ret = snmp_agent_build_name(&fields[i], td->tokens[i]);
      if (ret != 0)
        return ret;
    }
    format_name(name, name_len, fields[INDEX_HOST], fields[INDEX_PLUGIN],
                fields[INDEX_PLUGIN_INSTANCE], fields[INDEX_TYPE],
                fields[INDEX_TYPE_INSTANCE]);
    for (i = 0; i < MAX_KEY_SOURCES; i++) {
      if (td->tokens[i])
        sfree(fields[i]);
    }
  }

  return 0;
}

static int snmp_agent_form_reply(struct netsnmp_request_info_s *requests,
                                 data_definition_t *dd, oid_t *index_oid,
                                 int oid_index) {
  int ret;

  if (dd->is_index_key) {
    const table_definition_t *td = dd->table;
    int ret = snmp_agent_parse_oid_index_keys(td, index_oid);

    if (ret != 0)
      return ret;

    netsnmp_variable_list *key = td->index_list_cont;
    /* Searching index key */
    for (int pos = 0; pos < dd->index_key_pos; pos++)
      key = key->next_variable;

    requests->requestvb->type = td->index_keys[dd->index_key_pos].type;

    if (requests->requestvb->type == ASN_INTEGER)
#ifdef HAVE_NETSNMP_OLD_API
      snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                               (const u_char *)key->val.integer,
                               sizeof(*key->val.integer));
#else
      snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                               key->val.integer, sizeof(*key->val.integer));
#endif
    else /* OCTET_STR */
#ifdef HAVE_NETSNMP_OLD_API
      snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                               (const u_char *)key->val.string,
                               strlen((const char *)key->val.string));
#else
      snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                               key->val.string,
                               strlen((const char *)key->val.string));
#endif

    pthread_mutex_unlock(&g_agent->lock);

    return SNMP_ERR_NOERROR;
  }

  char name[DATA_MAX_NAME_LEN];

  ret = snmp_agent_format_name(name, sizeof(name), dd, index_oid);
  if (ret != 0)
    return ret;

  DEBUG(PLUGIN_NAME ": Identifier '%s'", name);

  value_t *values;
  size_t values_num;
  const data_set_t *ds = plugin_get_ds(dd->type);
  if (ds == NULL) {
    ERROR(PLUGIN_NAME ": Data set not found for '%s' type", dd->type);
    return SNMP_NOSUCHINSTANCE;
  }

  ret = uc_get_value_by_name(name, &values, &values_num);

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to get value for '%s'", name);
    return SNMP_NOSUCHINSTANCE;
  }

  assert(ds->ds_num == values_num);
  assert(oid_index < (int)values_num);

  char data[DATA_MAX_NAME_LEN];
  size_t data_len = sizeof(data);
  ret = snmp_agent_set_vardata(
      data, &data_len, dd->oids[oid_index].type, dd->scale, dd->shift,
      &values[oid_index], sizeof(values[oid_index]), ds->ds[oid_index].type);

  sfree(values);

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to convert '%s' value to snmp data", name);
    return SNMP_NOSUCHINSTANCE;
  }

  requests->requestvb->type = dd->oids[oid_index].type;
  snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                           (const u_char *)data, data_len);

  return SNMP_ERR_NOERROR;
}

static int
snmp_agent_table_oid_handler(struct netsnmp_mib_handler_s *handler,
                             struct netsnmp_handler_registration_s *reginfo,
                             struct netsnmp_agent_request_info_s *reqinfo,
                             struct netsnmp_request_info_s *requests) {

  if (reqinfo->mode != MODE_GET) {
    DEBUG(PLUGIN_NAME ": Not supported request mode (%d)", reqinfo->mode);
    return SNMP_ERR_NOERROR;
  }

  pthread_mutex_lock(&g_agent->lock);

  oid_t oid; /* Requested OID */
  memcpy(oid.oid, requests->requestvb->name,
         sizeof(oid.oid[0]) * requests->requestvb->name_length);
  oid.oid_len = requests->requestvb->name_length;

#if COLLECT_DEBUG
  char oid_str[DATA_MAX_NAME_LEN];
  snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &oid);
  DEBUG(PLUGIN_NAME ": Get request received for table OID '%s'", oid_str);
#endif
  oid_t index_oid; /* Index part of requested OID */

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      for (size_t i = 0; i < dd->oids_len; i++) {
        int ret = snmp_oid_ncompare(oid.oid, oid.oid_len, dd->oids[i].oid,
                                    dd->oids[i].oid_len,
                                    SNMP_MIN(oid.oid_len, dd->oids[i].oid_len));
        if (ret != 0)
          continue;

        /* Calculating OID length for index part */
        index_oid.oid_len = oid.oid_len - dd->oids[i].oid_len;
        /* Fetching index part of the OID */
        memcpy(index_oid.oid, &oid.oid[dd->oids[i].oid_len],
               index_oid.oid_len * sizeof(*oid.oid));

        char index_str[DATA_MAX_NAME_LEN];
        snmp_agent_oid_to_string(index_str, sizeof(index_str), &index_oid);

        if (!td->index_oid.oid_len) {
          ret = c_avl_get(td->instance_index, &index_oid, NULL);
        } else {
          oid_t *temp_oid;

          assert(index_oid.oid_len == 1);
          ret = c_avl_get(td->index_instance, (int *)&index_oid.oid[0],
                          (void **)&temp_oid);
          memcpy(&index_oid, temp_oid, sizeof(index_oid));
        }

        if (ret != 0) {
          INFO(PLUGIN_NAME ": Non-existing index (%s) requested", index_str);
          pthread_mutex_unlock(&g_agent->lock);
          return SNMP_NOSUCHINSTANCE;
        }

        ret = snmp_agent_form_reply(requests, dd, &index_oid, i);
        pthread_mutex_unlock(&g_agent->lock);

        return ret;
      }
    }
  }

  pthread_mutex_unlock(&g_agent->lock);

  return SNMP_NOSUCHINSTANCE;
}

static int snmp_agent_table_index_oid_handler(
    struct netsnmp_mib_handler_s *handler,
    struct netsnmp_handler_registration_s *reginfo,
    struct netsnmp_agent_request_info_s *reqinfo,
    struct netsnmp_request_info_s *requests) {

  if (reqinfo->mode != MODE_GET) {
    DEBUG(PLUGIN_NAME ": Not supported request mode (%d)", reqinfo->mode);
    return SNMP_ERR_NOERROR;
  }

  pthread_mutex_lock(&g_agent->lock);

  oid_t oid;
  memcpy(oid.oid, requests->requestvb->name,
         sizeof(oid.oid[0]) * requests->requestvb->name_length);
  oid.oid_len = requests->requestvb->name_length;

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    if (td->index_oid.oid_len &&
        (snmp_oid_ncompare(
             oid.oid, oid.oid_len, td->index_oid.oid, td->index_oid.oid_len,
             SNMP_MIN(oid.oid_len, td->index_oid.oid_len)) == 0)) {

      DEBUG(PLUGIN_NAME ": Handle '%s' table index OID", td->name);

      int index = oid.oid[oid.oid_len - 1];

      int ret = c_avl_get(td->index_instance, &index, NULL);
      if (ret != 0) {
        /* nonexisting index requested */
        pthread_mutex_unlock(&g_agent->lock);
        return SNMP_NOSUCHINSTANCE;
      }

      requests->requestvb->type = ASN_INTEGER;
      snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                               (const u_char *)&index, sizeof(index));

      pthread_mutex_unlock(&g_agent->lock);

      return SNMP_ERR_NOERROR;
    }
  }

  pthread_mutex_unlock(&g_agent->lock);

  return SNMP_NOSUCHINSTANCE;
}

static int snmp_agent_table_size_oid_handler(
    struct netsnmp_mib_handler_s *handler,
    struct netsnmp_handler_registration_s *reginfo,
    struct netsnmp_agent_request_info_s *reqinfo,
    struct netsnmp_request_info_s *requests) {

  if (reqinfo->mode != MODE_GET) {
    DEBUG(PLUGIN_NAME ": Not supported request mode (%d)", reqinfo->mode);
    return SNMP_ERR_NOERROR;
  }

  pthread_mutex_lock(&g_agent->lock);

  oid_t oid;
  memcpy(oid.oid, requests->requestvb->name,
         sizeof(oid.oid[0]) * requests->requestvb->name_length);
  oid.oid_len = requests->requestvb->name_length;

  DEBUG(PLUGIN_NAME ": Get request received for table size OID");

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    if (td->size_oid.oid_len &&
        (snmp_oid_ncompare(oid.oid, oid.oid_len, td->size_oid.oid,
                           td->size_oid.oid_len,
                           SNMP_MIN(oid.oid_len, td->size_oid.oid_len)) == 0)) {
      DEBUG(PLUGIN_NAME ": Handle '%s' table size OID", td->name);

      long size;
      if (td->index_oid.oid_len)
        size = c_avl_size(td->index_instance);
      else
        size = c_avl_size(td->instance_index);

      requests->requestvb->type = ASN_INTEGER;
      snmp_set_var_typed_value(requests->requestvb, requests->requestvb->type,
                               (const u_char *)&size, sizeof(size));

      pthread_mutex_unlock(&g_agent->lock);

      return SNMP_ERR_NOERROR;
    }
  }

  pthread_mutex_unlock(&g_agent->lock);

  return SNMP_NOSUCHINSTANCE;
}

static int
snmp_agent_scalar_oid_handler(struct netsnmp_mib_handler_s *handler,
                              struct netsnmp_handler_registration_s *reginfo,
                              struct netsnmp_agent_request_info_s *reqinfo,
                              struct netsnmp_request_info_s *requests) {

  if (reqinfo->mode != MODE_GET) {
    DEBUG(PLUGIN_NAME ": Not supported request mode (%d)", reqinfo->mode);
    return SNMP_ERR_NOERROR;
  }

  pthread_mutex_lock(&g_agent->lock);

  oid_t oid;
  memcpy(oid.oid, requests->requestvb->name,
         sizeof(oid.oid[0]) * requests->requestvb->name_length);
  oid.oid_len = requests->requestvb->name_length;

#if COLLECT_DEBUG
  char oid_str[DATA_MAX_NAME_LEN];
  snmp_agent_oid_to_string(oid_str, sizeof(oid_str), &oid);
  DEBUG(PLUGIN_NAME ": Get request received for scalar OID '%s'", oid_str);
#endif

  for (llentry_t *de = llist_head(g_agent->scalars); de != NULL;
       de = de->next) {
    data_definition_t *dd = de->value;

    for (size_t i = 0; i < dd->oids_len; i++) {

      int ret = snmp_oid_compare(oid.oid, oid.oid_len, dd->oids[i].oid,
                                 dd->oids[i].oid_len);
      if (ret != 0)
        continue;

      ret = snmp_agent_form_reply(requests, dd, NULL, i);

      pthread_mutex_unlock(&g_agent->lock);

      return ret;
    }
  }

  pthread_mutex_unlock(&g_agent->lock);

  return SNMP_NOSUCHINSTANCE;
}

static int snmp_agent_register_table_oids(void) {

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    if (td->size_oid.oid_len != 0) {
      td->size_oid.type =
          snmp_agent_get_asn_type(td->size_oid.oid, td->size_oid.oid_len);
      td->size_oid.oid_len++;
      int ret = snmp_agent_register_oid(&td->size_oid,
                                        snmp_agent_table_size_oid_handler);
      if (ret != 0)
        return ret;
    }

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      for (size_t i = 0; i < dd->oids_len; i++) {
        dd->oids[i].type =
            snmp_agent_get_asn_type(dd->oids[i].oid, dd->oids[i].oid_len);
      }
    }
  }

  return 0;
}

static int snmp_agent_register_scalar_oids(void) {

  for (llentry_t *e = llist_head(g_agent->scalars); e != NULL; e = e->next) {
    data_definition_t *dd = e->value;

    for (size_t i = 0; i < dd->oids_len; i++) {

      dd->oids[i].type =
          snmp_agent_get_asn_type(dd->oids[i].oid, dd->oids[i].oid_len);

      int ret =
          snmp_agent_register_oid(&dd->oids[i], snmp_agent_scalar_oid_handler);
      if (ret != 0)
        return ret;
    }
  }

  return 0;
}

static int snmp_agent_config_data_oids(data_definition_t *dd,
                                       oconfig_item_t *ci) {
  if (ci->values_num < 1) {
    WARNING(PLUGIN_NAME ": `OIDs' needs at least one argument");
    return -EINVAL;
  }

  for (int i = 0; i < ci->values_num; i++)
    if (ci->values[i].type != OCONFIG_TYPE_STRING) {
      WARNING(PLUGIN_NAME ": `OIDs' needs only string argument");
      return -EINVAL;
    }

  if (dd->oids != NULL) {
    WARNING(PLUGIN_NAME ": OIDs can be configured only once for each data");
    return -EINVAL;
  }

  dd->oids_len = 0;
  dd->oids = calloc(ci->values_num, sizeof(*dd->oids));

  if (dd->oids == NULL)
    return -ENOMEM;
  dd->oids_len = (size_t)ci->values_num;

  for (int i = 0; i < ci->values_num; i++) {
    dd->oids[i].oid_len = MAX_OID_LEN;

    if (NULL == snmp_parse_oid(ci->values[i].value.string, dd->oids[i].oid,
                               &dd->oids[i].oid_len)) {
      ERROR(PLUGIN_NAME ": snmp_parse_oid (%s) failed",
            ci->values[i].value.string);
      sfree(dd->oids);
      dd->oids_len = 0;
      return -1;
    }
  }

  return 0;
}

static int snmp_agent_config_table_size_oid(table_definition_t *td,
                                            oconfig_item_t *ci) {
  if (ci->values_num < 1) {
    WARNING(PLUGIN_NAME ": `TableSizeOID' is empty");
    return -EINVAL;
  }

  if (ci->values[0].type != OCONFIG_TYPE_STRING) {
    WARNING(PLUGIN_NAME ": `TableSizeOID' needs only string argument");
    return -EINVAL;
  }

  td->size_oid.oid_len = MAX_OID_LEN;

  if (NULL == snmp_parse_oid(ci->values[0].value.string, td->size_oid.oid,
                             &td->size_oid.oid_len)) {
    ERROR(PLUGIN_NAME ": Failed to parse table size OID (%s)",
          ci->values[0].value.string);
    td->size_oid.oid_len = 0;
    return -EINVAL;
  }

  return 0;
}

static int snmp_agent_config_table_index_oid(table_definition_t *td,
                                             oconfig_item_t *ci) {

  if (ci->values_num < 1) {
    WARNING(PLUGIN_NAME ": `IndexOID' is empty");
    return -EINVAL;
  }

  if (ci->values[0].type != OCONFIG_TYPE_STRING) {
    WARNING(PLUGIN_NAME ": `IndexOID' needs only string argument");
    return -EINVAL;
  }

  td->index_oid.oid_len = MAX_OID_LEN;

  if (NULL == snmp_parse_oid(ci->values[0].value.string, td->index_oid.oid,
                             &td->index_oid.oid_len)) {
    ERROR(PLUGIN_NAME ": Failed to parse table index OID (%s)",
          ci->values[0].value.string);
    td->index_oid.oid_len = 0;
    return -EINVAL;
  }

  return 0;
}

/* Getting index key source that will represent table row */
static int snmp_agent_config_index_key_source(table_definition_t *td,
                                              data_definition_t *dd,
                                              oconfig_item_t *ci) {
  char *val = NULL;

  int ret = cf_util_get_string(ci, &val);
  if (ret != 0)
    return -1;

  bool match = false;

  for (int i = 0; i < MAX_KEY_SOURCES; i++) {
    if (strcasecmp(index_opts[i], (const char *)val) == 0) {
      td->index_keys[td->index_keys_len].source = i;
      td->index_keys[td->index_keys_len].group = GROUP_UNUSED;
      td->index_keys[td->index_keys_len].regex = NULL;
      match = 1;
      break;
    }
  }

  if (!match) {
    ERROR(PLUGIN_NAME ": Failed to parse index key source: '%s'", val);
    sfree(val);
    return -EINVAL;
  }

  sfree(val);
  dd->index_key_pos = td->index_keys_len++;
  dd->is_index_key = true;

  return 0;
}

/* Getting format string used to parse values from index key source */
static int snmp_agent_config_index_key_regex(table_definition_t *td,
                                             data_definition_t *dd,
                                             oconfig_item_t *ci) {
  index_key_t *index_key = &td->index_keys[dd->index_key_pos];

  int ret = cf_util_get_string(ci, &index_key->regex);
  if (ret != 0)
    return -1;

  ret = regcomp(&index_key->regex_info, index_key->regex, REG_EXTENDED);
  if (ret) {
    ERROR(PLUGIN_NAME ": Could not compile regex for %s", dd->name);
    return -1;
  }

  index_key_src_t source = index_key->source;
  if (td->tokens[source] == NULL) {
    td->tokens[source] =
        c_avl_create((int (*)(const void *, const void *))num_compare);
    if (td->tokens[source] == NULL) {
      ERROR(PLUGIN_NAME ": Could not allocate memory for AVL tree");
      return -ENOMEM;
    }
  }

  return 0;
}

static int snmp_agent_config_index_key(table_definition_t *td,
                                       data_definition_t *dd,
                                       oconfig_item_t *ci) {
  int ret = 0;

  for (int i = 0; (i < ci->children_num && ret == 0); i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("Source", option->key) == 0)
      ret = snmp_agent_config_index_key_source(td, dd, option);
    else if (strcasecmp("Regex", option->key) == 0)
      ret = snmp_agent_config_index_key_regex(td, dd, option);
    else if (strcasecmp("Group", option->key) == 0)
      ret = cf_util_get_int(option, &td->index_keys[dd->index_key_pos].group);
  }

  return ret;
}

/* This function parses configuration of both scalar and table column
 * because they have nearly the same structure */
static int snmp_agent_config_table_column(table_definition_t *td,
                                          oconfig_item_t *ci) {
  data_definition_t *dd;
  int ret = 0;
  oconfig_item_t *option_tmp = NULL;

  assert(ci != NULL);

  dd = calloc(1, sizeof(*dd));
  if (dd == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for table data definition");
    return -ENOMEM;
  }

  ret = cf_util_get_string(ci, &dd->name);
  if (ret != 0) {
    sfree(dd);
    return -1;
  }

  dd->scale = 1.0;
  dd->shift = 0.0;
  /* NULL if it's a scalar */
  dd->table = td;
  dd->is_index_key = false;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    /* First 3 options are reserved for table entry only */
    if (td != NULL && strcasecmp("IndexKey", option->key) == 0) {
      dd->is_index_key = true;
      option_tmp = option;
    } else if (strcasecmp("Plugin", option->key) == 0)
      ret = cf_util_get_string(option, &dd->plugin);
    else if (strcasecmp("PluginInstance", option->key) == 0)
      ret = cf_util_get_string(option, &dd->plugin_instance);
    else if (strcasecmp("Type", option->key) == 0)
      ret = cf_util_get_string(option, &dd->type);
    else if (strcasecmp("TypeInstance", option->key) == 0)
      ret = cf_util_get_string(option, &dd->type_instance);
    else if (strcasecmp("Shift", option->key) == 0)
      ret = cf_util_get_double(option, &dd->shift);
    else if (strcasecmp("Scale", option->key) == 0)
      ret = cf_util_get_double(option, &dd->scale);
    else if (strcasecmp("OIDs", option->key) == 0)
      ret = snmp_agent_config_data_oids(dd, option);
    else {
      WARNING(PLUGIN_NAME ": Option `%s' not allowed here", option->key);
      ret = -1;
    }

    if (ret != 0) {
      snmp_agent_free_data(&dd);
      return -1;
    }
  }

  if (dd->is_index_key) {
    ret = snmp_agent_config_index_key(td, dd, option_tmp);
    td->index_keys[dd->index_key_pos].type =
        snmp_agent_get_asn_type(dd->oids[0].oid, dd->oids[0].oid_len);

    if (ret != 0) {
      snmp_agent_free_data(&dd);
      return -1;
    }
  }

  llentry_t *entry = llentry_create(dd->name, dd);
  if (entry == NULL) {
    snmp_agent_free_data(&dd);
    return -ENOMEM;
  }

  /* Append to column list in parent table */
  if (td != NULL)
    llist_append(td->columns, entry);
  else
    llentry_destroy(entry);

  return 0;
}

/* Parses scalar configuration entry */
static int snmp_agent_config_scalar(oconfig_item_t *ci) {
  return snmp_agent_config_table_column(NULL, ci);
}

static int num_compare(const int *a, const int *b) {
  assert((a != NULL) && (b != NULL));
  if (*a < *b)
    return -1;
  else if (*a > *b)
    return 1;
  else
    return 0;
}

static int oid_compare(const oid_t *a, const oid_t *b) {
  return snmp_oid_compare(a->oid, a->oid_len, b->oid, b->oid_len);
}

static int snmp_agent_config_table(oconfig_item_t *ci) {
  table_definition_t *td;
  int ret = 0;

  assert(ci != NULL);

  td = calloc(1, sizeof(*td));
  if (td == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for table definition");
    return -ENOMEM;
  }

  ret = cf_util_get_string(ci, &td->name);
  if (ret != 0) {
    sfree(td);
    return -1;
  }

  td->columns = llist_create();
  if (td->columns == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for columns list");
    snmp_agent_free_table(&td);
    return -ENOMEM;
  }

  for (int i = 0; i < MAX_KEY_SOURCES; i++)
    td->tokens[i] = NULL;
  td->tokens_done = false;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *option = ci->children + i;

    if (strcasecmp("IndexOID", option->key) == 0)
      ret = snmp_agent_config_table_index_oid(td, option);
    else if (strcasecmp("SizeOID", option->key) == 0)
      ret = snmp_agent_config_table_size_oid(td, option);
    else if (strcasecmp("Data", option->key) == 0)
      ret = snmp_agent_config_table_column(td, option);
    else {
      WARNING(PLUGIN_NAME ": Option `%s' not allowed here", option->key);
      ret = -1;
    }

    if (ret != 0) {
      snmp_agent_free_table(&td);
      return -ENOMEM;
    }
  }

  /* Preparing index list container */
  ret = snmp_agent_prep_index_list(td, &td->index_list_cont);
  if (ret != 0)
    return -EINVAL;

  td->instance_index =
      c_avl_create((int (*)(const void *, const void *))oid_compare);
  if (td->instance_index == NULL) {
    snmp_agent_free_table(&td);
    return -ENOMEM;
  }

  td->index_instance =
      c_avl_create((int (*)(const void *, const void *))num_compare);
  if (td->index_instance == NULL) {
    snmp_agent_free_table(&td);
    return -ENOMEM;
  }

  td->instance_oids =
      c_avl_create((int (*)(const void *, const void *))oid_compare);
  if (td->instance_oids == NULL) {
    snmp_agent_free_table(&td);
    return -ENOMEM;
  }

  llentry_t *entry = llentry_create(td->name, td);
  if (entry == NULL) {
    snmp_agent_free_table(&td);
    return -ENOMEM;
  }

  llist_append(g_agent->tables, entry);

  return 0;
}

static int snmp_agent_get_value_from_ds_type(const value_t *val, int type,
                                             double scale, double shift,
                                             long *value) {
  switch (type) {
  case DS_TYPE_COUNTER:
    *value = (long)((val->counter * scale) + shift);
    break;
  case DS_TYPE_ABSOLUTE:
    *value = (long)((val->absolute * scale) + shift);
    break;
  case DS_TYPE_DERIVE:
    *value = (long)((val->derive * scale) + shift);
    break;
  case DS_TYPE_GAUGE:
    *value = (long)((val->gauge * scale) + shift);
    break;
  case TYPE_STRING:
    break;
  default:
    ERROR(PLUGIN_NAME ": Unknown data source type: %i", type);
    return -EINVAL;
  }

  return 0;
}

static int snmp_agent_set_vardata(void *data, size_t *data_len, u_char asn_type,
                                  double scale, double shift, const void *value,
                                  size_t len, int type) {

  int ret;
  netsnmp_vardata var;
  const value_t *val;
  long new_value = 0;

  val = value;
  var.string = (u_char *)data;

  ret = snmp_agent_get_value_from_ds_type(val, type, scale, shift, &new_value);
  if (ret != 0)
    return ret;

  switch (asn_type) {
  case ASN_INTEGER:
  case ASN_UINTEGER:
  case ASN_COUNTER:
  case ASN_TIMETICKS:
  case ASN_GAUGE:
    if (*data_len < sizeof(*var.integer))
      return -EINVAL;
    *var.integer = new_value;
    *data_len = sizeof(*var.integer);
    break;
  case ASN_COUNTER64:
    if (*data_len < sizeof(*var.counter64))
      return -EINVAL;
    var.counter64->high = (u_long)((int64_t)new_value >> 32);
    var.counter64->low = (u_long)((int64_t)new_value & 0xFFFFFFFF);
    *data_len = sizeof(*var.counter64);
    break;
  case ASN_OCTET_STR:
    if (type == DS_TYPE_GAUGE) {
      char buf[DATA_MAX_NAME_LEN];
      snprintf(buf, sizeof(buf), "%.2f", val->gauge);
      if (*data_len < strlen(buf))
        return -EINVAL;
      *data_len = strlen(buf);
      memcpy(var.string, buf, *data_len);
    } else {
      ERROR(PLUGIN_NAME ": Failed to convert %d ds type to %d asn type", type,
            asn_type);
      return -EINVAL;
    }
    break;
  default:
    ERROR(PLUGIN_NAME ": Failed to convert %d ds type to %d asn type", type,
          asn_type);
    return -EINVAL;
  }

  return 0;
}

static int snmp_agent_register_oid_index(oid_t *oid, int index,
                                         Netsnmp_Node_Handler *handler) {
  oid_t new_oid;
  memcpy(&new_oid, oid, sizeof(*oid));
  new_oid.oid[new_oid.oid_len++] = index;
  return snmp_agent_register_oid(&new_oid, handler);
}

static int snmp_agent_unregister_oid_index(oid_t *oid, int index) {
  oid_t new_oid;
  memcpy(&new_oid, oid, sizeof(*oid));
  new_oid.oid[new_oid.oid_len++] = index;
  return snmp_agent_unregister_oid(&new_oid);
}

static int snmp_agent_update_instance_oids(c_avl_tree_t *tree, oid_t *index_oid,
                                           int value) {
  int *oids_num; /* number of oids registered for instance */

  if (c_avl_get(tree, index_oid, (void **)&oids_num) == 0) {
    *oids_num += value;
    return *oids_num;
  } else {
    ERROR(PLUGIN_NAME ": Error updating index data");
    return -1;
  }
}

static int snmp_agent_update_index(data_definition_t *dd,
                                   table_definition_t *td, oid_t *index_oid,
                                   bool *free_index_oid) {
  int ret;
  int *index = NULL;
  int *value = NULL;

  if (c_avl_get(td->instance_index, (void *)index_oid, (void **)&index) != 0) {
    /* We'll keep index_oid stored in AVL tree */
    *free_index_oid = false;

    /* need to generate index for the table */
    if (td->index_oid.oid_len) {
      index = calloc(1, sizeof(*index));
      if (index == NULL) {
        ret = -ENOMEM;
        goto error;
      }

      *index = c_avl_size(td->instance_index) + 1;

      ret = c_avl_insert(td->instance_index, index_oid, index);
      if (ret != 0)
        goto free_index;

      ret = c_avl_insert(td->index_instance, index, index_oid);
      if (ret < 0) {
        DEBUG(PLUGIN_NAME ": Failed to update index_instance for '%s' table",
              td->name);
        goto remove_avl_index_oid;
      }

      ret = snmp_agent_register_oid_index(&td->index_oid, *index,
                                          snmp_agent_table_index_oid_handler);
      if (ret != 0)
        goto remove_avl_index;
    } else {
      /* instance as a key is required for any table */
      ret = c_avl_insert(td->instance_index, index_oid, NULL);
      if (ret != 0)
        goto error;
    }

    value = calloc(1, sizeof(*value));

    if (value == NULL) {
      ERROR(PLUGIN_NAME ": Failed to allocate memory");
      ret = -ENOMEM;
      goto unregister_index;
    }

    ret = c_avl_insert(td->instance_oids, index_oid, value);

    if (ret < 0) {
      DEBUG(PLUGIN_NAME ": Failed to update instance_oids for '%s' table",
            td->name);
      goto free_value;
    }

    int keys_processed = 0;

    /* Registering index keys OIDs */
    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *idd = de->value;
      if (!idd->is_index_key)
        continue;

      for (size_t i = 0; i < idd->oids_len; i++) {
        if (td->index_oid.oid_len)
          ret = snmp_agent_register_oid_index(&idd->oids[i], *index,
                                              snmp_agent_table_oid_handler);
        else
          ret = snmp_agent_register_oid_string(&idd->oids[i], index_oid,
                                               snmp_agent_table_oid_handler);

        if (ret != 0) {
          ERROR(PLUGIN_NAME ": Could not register OID");
          goto free_index;
        }
      }

      if (++keys_processed >= td->index_keys_len)
        break;
    }
  }

  ret = 0;

  for (size_t i = 0; i < dd->oids_len; i++) {
    if (td->index_oid.oid_len)
      ret = snmp_agent_register_oid_index(&dd->oids[i], *index,
                                          snmp_agent_table_oid_handler);
    else
      ret = snmp_agent_register_oid_string(&dd->oids[i], index_oid,
                                           snmp_agent_table_oid_handler);

    if (ret < 0)
      goto free_index;
    else if (ret == OID_EXISTS)
      break;
    else if (snmp_agent_update_instance_oids(td->instance_oids, index_oid, 1) <
             0)
      goto free_index;
  }

  if (ret != OID_EXISTS) {
    char index_str[DATA_MAX_NAME_LEN];

    if (index == NULL)
      snmp_agent_oid_to_string(index_str, sizeof(index_str), index_oid);
    else
      snprintf(index_str, sizeof(index_str), "%d", *index);

    notification_t n = {
        .severity = NOTIF_OKAY, .time = cdtime(), .plugin = PLUGIN_NAME};
    sstrncpy(n.host, hostname_g, sizeof(n.host));
    snprintf(n.message, sizeof(n.message),
             "Data added to table %s with index %s", td->name, index_str);
    DEBUG(PLUGIN_NAME ": %s", n.message);

    plugin_dispatch_notification(&n);
  }

  return 0;

free_value:
  sfree(value);
unregister_index:
  if (td->index_oid.oid_len)
    snmp_agent_unregister_oid_index(index_oid, *index);
remove_avl_index:
  if (td->index_oid.oid_len)
    c_avl_remove(td->index_instance, index, NULL, NULL);
remove_avl_index_oid:
  c_avl_remove(td->instance_index, index_oid, NULL, NULL);
free_index:
  if (index != NULL)
    sfree(index);
error:
  *free_index_oid = true;

  return ret;
}

static int snmp_agent_write(value_list_t const *vl) {
  if (vl == NULL)
    return -EINVAL;

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next) {
    table_definition_t *td = te->value;

    for (llentry_t *de = llist_head(td->columns); de != NULL; de = de->next) {
      data_definition_t *dd = de->value;

      if (!dd->is_index_key) {
        if (CHECK_DD_TYPE(dd, vl->plugin, vl->plugin_instance, vl->type,
                          vl->type_instance)) {
          oid_t *index_oid = calloc(1, sizeof(*index_oid));
          bool free_index_oid = true;

          if (index_oid == NULL) {
            ERROR(PLUGIN_NAME ": Could not allocate memory for index_oid");
            return -ENOMEM;
          }

          int ret = snmp_agent_generate_index(td, vl, index_oid);

          if (ret == 0)
            ret = snmp_agent_update_index(dd, td, index_oid, &free_index_oid);

          /* Index exists or update failed */
          if (free_index_oid)
            sfree(index_oid);

          return ret;
        }
      }
    }
  }

  return 0;
}

static int snmp_agent_collect(const data_set_t *ds, const value_list_t *vl,
                              user_data_t __attribute__((unused)) * user_data) {

  pthread_mutex_lock(&g_agent->lock);

  snmp_agent_write(vl);

  pthread_mutex_unlock(&g_agent->lock);

  return 0;
}

static int snmp_agent_preinit(void) {

  g_agent = calloc(1, sizeof(*g_agent));
  if (g_agent == NULL) {
    ERROR(PLUGIN_NAME ": Failed to allocate memory for snmp agent context");
    return -ENOMEM;
  }

  g_agent->tables = llist_create();
  g_agent->scalars = llist_create();
  g_agent->registered_oids =
      c_avl_create((int (*)(const void *, const void *))oid_compare);

  if (g_agent->tables == NULL || g_agent->scalars == NULL) {
    ERROR(PLUGIN_NAME ": llist_create() failed");
    llist_destroy(g_agent->scalars);
    llist_destroy(g_agent->tables);
    c_avl_destroy(g_agent->registered_oids);
    return -ENOMEM;
  }

  int err;
  /* make us an agentx client. */
  err = netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID, NETSNMP_DS_AGENT_ROLE,
                               1);
  if (err != 0) {
    ERROR(PLUGIN_NAME ": Failed to set agent role (%d)", err);
    llist_destroy(g_agent->scalars);
    llist_destroy(g_agent->tables);
    c_avl_destroy(g_agent->registered_oids);
    return -1;
  }

  /*
   *  For SNMP debug purposes uses snmp_set_do_debugging(1);
   */

  /* initialize the agent library */
  err = init_agent(PLUGIN_NAME);
  if (err != 0) {
    ERROR(PLUGIN_NAME ": Failed to initialize the agent library (%d)", err);
    llist_destroy(g_agent->scalars);
    llist_destroy(g_agent->tables);
    c_avl_destroy(g_agent->registered_oids);
    return -1;
  }

  init_snmp(PLUGIN_NAME);

  g_agent->tp = read_all_mibs();

  return 0;
}

static int snmp_agent_init(void) {
  int ret;

  if (g_agent == NULL || ((llist_head(g_agent->scalars) == NULL) &&
                          (llist_head(g_agent->tables) == NULL))) {
    ERROR(PLUGIN_NAME ": snmp_agent_init: plugin not configured");
    return -EINVAL;
  }

  plugin_register_shutdown(PLUGIN_NAME, snmp_agent_shutdown);

  ret = snmp_agent_register_scalar_oids();
  if (ret != 0)
    return ret;

  ret = snmp_agent_register_table_oids();
  if (ret != 0)
    return ret;

  ret = pthread_mutex_init(&g_agent->lock, NULL);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to initialize mutex, err %u", ret);
    return ret;
  }

  ret = pthread_mutex_init(&g_agent->agentx_lock, NULL);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to initialize AgentX mutex, err %u", ret);
    return ret;
  }

  /* create a second thread to listen for requests from AgentX*/
  ret = pthread_create(&g_agent->thread, NULL, &snmp_agent_thread_run, NULL);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Failed to create a separate thread, err %u", ret);
    return ret;
  }

  if (llist_head(g_agent->tables) != NULL) {
    plugin_register_write(PLUGIN_NAME, snmp_agent_collect, NULL);
    plugin_register_missing(PLUGIN_NAME, snmp_agent_clear_missing, NULL);
  }

  return 0;
}

static void *snmp_agent_thread_run(void __attribute__((unused)) * arg) {
  INFO(PLUGIN_NAME ": Thread is up and running");

  for (;;) {
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);

    pthread_mutex_lock(&g_agent->agentx_lock);
    agent_check_and_process(0); /* 0 == don't block */
    pthread_mutex_unlock(&g_agent->agentx_lock);

    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    usleep(10);
  }

  pthread_exit(0);
}

static int snmp_agent_register_oid(oid_t *oid, Netsnmp_Node_Handler *handler) {
  netsnmp_handler_registration *reg;

  if (c_avl_get(g_agent->registered_oids, (void *)oid, NULL) == 0)
    return OID_EXISTS;
  else {
    oid_t *new_oid = calloc(1, sizeof(*new_oid));
    if (new_oid == NULL) {
      ERROR(PLUGIN_NAME ": Could not allocate memory to register new OID");
      return -ENOMEM;
    }

    memcpy(new_oid, oid, sizeof(*oid));

    int ret = c_avl_insert(g_agent->registered_oids, (void *)new_oid, NULL);
    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Could not allocate memory to register new OID");
      sfree(new_oid);
      return -ENOMEM;
    }
  }

  char *oid_name = snmp_agent_get_oid_name(oid->oid, oid->oid_len - 1);
  char oid_str[DATA_MAX_NAME_LEN];

  snmp_agent_oid_to_string(oid_str, sizeof(oid_str), oid);

  if (oid_name == NULL) {
    WARNING(PLUGIN_NAME
            ": Skipped registration: OID (%s) is not found in main tree",
            oid_str);
    return 0;
  }

  reg = netsnmp_create_handler_registration(oid_name, handler, oid->oid,
                                            oid->oid_len, HANDLER_CAN_RONLY);
  if (reg == NULL) {
    ERROR(PLUGIN_NAME ": Failed to create handler registration for OID (%s)",
          oid_str);
    return -1;
  }

  pthread_mutex_lock(&g_agent->agentx_lock);

  if (netsnmp_register_instance(reg) != MIB_REGISTERED_OK) {
    ERROR(PLUGIN_NAME ": Failed to register handler for OID (%s)", oid_str);
    pthread_mutex_unlock(&g_agent->agentx_lock);
    return -1;
  }

  pthread_mutex_unlock(&g_agent->agentx_lock);

  DEBUG(PLUGIN_NAME ": Registered handler for OID (%s)", oid_str);

  return 0;
}

static int snmp_agent_free_config(void) {

  if (g_agent == NULL)
    return -EINVAL;

  for (llentry_t *te = llist_head(g_agent->tables); te != NULL; te = te->next)
    snmp_agent_free_table((table_definition_t **)&te->value);
  llist_destroy(g_agent->tables);

  for (llentry_t *de = llist_head(g_agent->scalars); de != NULL; de = de->next)
    snmp_agent_free_data((data_definition_t **)&de->value);
  llist_destroy(g_agent->scalars);

  return 0;
}

static int snmp_agent_shutdown(void) {
  int ret = 0;

  DEBUG(PLUGIN_NAME ": snmp_agent_shutdown");

  if (g_agent == NULL) {
    ERROR(PLUGIN_NAME ": snmp_agent_shutdown: plugin not initialized");
    return -EINVAL;
  }

  if (pthread_cancel(g_agent->thread) != 0)
    ERROR(PLUGIN_NAME ": snmp_agent_shutdown: failed to cancel the thread");

  if (pthread_join(g_agent->thread, NULL) != 0)
    ERROR(PLUGIN_NAME ": snmp_agent_shutdown: failed to join the thread");

  snmp_agent_free_config();

  snmp_shutdown(PLUGIN_NAME);

  pthread_mutex_destroy(&g_agent->lock);
  pthread_mutex_destroy(&g_agent->agentx_lock);

  /* Freeing registered OIDs list */
  void *oid;

  if (g_agent->registered_oids != NULL) {
    while (c_avl_pick(g_agent->registered_oids, &oid, NULL) == 0) {
      sfree(oid);
    }
    c_avl_destroy(g_agent->registered_oids);
  }

  sfree(g_agent);

  return ret;
}

static int snmp_agent_config(oconfig_item_t *ci) {
  int ret = snmp_agent_preinit();

  if (ret != 0) {
    sfree(g_agent);
    return -EINVAL;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Data", child->key) == 0) {
      ret = snmp_agent_config_scalar(child);
    } else if (strcasecmp("Table", child->key) == 0) {
      ret = snmp_agent_config_table(child);
    } else {
      ERROR(PLUGIN_NAME ": Unknown configuration option `%s'", child->key);
      ret = (-EINVAL);
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Failed to parse configuration");
      snmp_agent_free_config();
      snmp_shutdown(PLUGIN_NAME);
      sfree(g_agent);
      return -EINVAL;
    }
  }

  ret = snmp_agent_validate_config();
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Invalid configuration provided");
    snmp_agent_free_config();
    snmp_shutdown(PLUGIN_NAME);
    sfree(g_agent);
    return -EINVAL;
  }

  return 0;
}

void module_register(void) {
  plugin_register_init(PLUGIN_NAME, snmp_agent_init);
  plugin_register_complex_config(PLUGIN_NAME, snmp_agent_config);
}
