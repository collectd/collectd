/**
 *
 * collectd - src/write_memcache.c
 * Copyright (C) 2014 David Warren
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   David Warren <david at dwarren.net>
 **/

#include "collectd.h"
#include "common.h"
#include "plugin.h"

#include <regex.h>
#include <pthread.h>

#include "utils_format_json.h"

#include <libmemcached/memcached.h>

/* DEFINITIONS */

#define WARN_PREFIX "write_memcache plugin: "
#define ERROR_PREFIX "write_memcache plugin: "

#define JSON_BUFFER_SIZE 1024          /* Max size of JSON content */
#define KEY_EXPIRE_TIME 300            /* Time (sec) to set key expiry */

/*
 * Rule - one entry in per-output rule list
 */
struct wmc_output_rule_s;
typedef struct wmc_output_rule_s wmc_output_rule_t;

#define WMC_OUTPUT_MATCH_ALLOW         0x0001
#define WMC_OUTPUT_MATCH_DENY          0x0002

#define WMC_KEYMANGLE_UPPERCASE        0x0001
#define WMC_KEYMANGLE_LOWERCASE        0x0002
#define WMC_KEYMANGLE_HIDESNMP         0x0004

#define WMC_KEYMANGLE_OVERRIDE         0x1000

struct wmc_output_rule_s {
  char *text;                          /* Simple text-rule (strstr-style) */

  char *regex;                         /* String version of regex if present */
  regex_t compiled_regex;              /* Compiled version of regex */
                                       /* regex valid IIF regex != NULL */

  int action_flags;                    /* Allow deny or (default) fall thru */
  int keymangle_flags;                 /* Key mangle flags */

  char *prefix;                        /* Prefix override if != NULL */

  wmc_output_rule_t *next;             /* Next entry in ACL */
};

/*
 * Output instance - one memcache server (or set) we're writing to
 */
struct wmc_output_s;
typedef struct wmc_output_s wmc_output_t;

struct wmc_output_s {
  char *server;                        /* String version of server(s) */
  char *prefix;                        /* Key prefix */
  int keymangle_flags;                 /* Key mangle flags */

  memcached_st *conn;                  /* libmemcached conn struct */

  wmc_output_rule_t *rule_head;        /* List of wmc_output_rule_t */
  wmc_output_rule_t *rule_tail;

  wmc_output_t *next;                  /* Next in global list */
};

/*
 * Structure used for option parsing
 */
struct wmc_optval_s;
typedef struct wmc_optval_s wmc_optval_t;
struct wmc_optval_s {
  char const *keyword;

  int set_mask;
  int clear_mask;
};

/* GLOBAL STATE */

static wmc_output_t *output_head;      /* Global list of outputs */
static wmc_output_t *output_tail;

/*
 * Right now I just have one big lock around the global state.  This could
 * cause a problem if the writers hit a hang such as a suddenly down
 * server, and we logjam every write thread.  I don't know if the framework
 * handles that.  -DAW
 */
static pthread_mutex_t wmc_global_lock;

/* CODE */

/*
 * Create wmc_output_t, init, and link in
 */

static wmc_output_t *wmc_output_create()
{
  wmc_output_t *o;

  o= malloc(sizeof(wmc_output_t));
  o->server= NULL;
  o->prefix= NULL;
  o->keymangle_flags= 0;

  o->conn= NULL;

  o->rule_head= NULL;
  o->rule_tail= NULL;

  o->next= NULL;
  if (output_tail == NULL) {
    output_head= output_tail= o;
  } else {
    output_tail->next= o;
    output_tail= o;
  }

  return o;
}

/*
 * Create wmc_output_rule_t, init, and link in
 */
static wmc_output_rule_t *wmc_output_rule_create(wmc_output_t *output)
{
  wmc_output_rule_t *m;

  m= malloc(sizeof(wmc_output_rule_t));
  m->regex= NULL;
  m->text= NULL;
  m->action_flags= 0;
  m->prefix= NULL;
  m->keymangle_flags= 0;

  m->next= NULL;
  if (output->rule_tail == NULL) {
    output->rule_head= output->rule_tail= m;
  } else {
    output->rule_tail->next= m;
    output->rule_tail= m;
  }

  return m;
}

/*
 * Set a string from a config item
 */
static int wmc_set_string(oconfig_item_t *item, char **val)
{
  int rval;

  rval= 0;

  if (item->values_num != 1) {
    WARNING(WARN_PREFIX "Directive %s requires one value", item->key);
    rval= -1;
  } else if (item->values[0].type != OCONFIG_TYPE_STRING) {
    WARNING(WARN_PREFIX "Directive %s requires string value", item->key);
    rval= -1;
  } else {
    if (*val != NULL) {
      free(*val);
    }
    *val= strdup(item->values[0].value.string);
  }
  return rval;
}

/*
 * Set flags from a config item
 */
static int wmc_set_flags(oconfig_item_t *item,
                         wmc_optval_t *opt_list, int opt_cnt, int *out_val)
{
  int rval;
  int i;
  oconfig_value_t *value;
  int opt;
  wmc_optval_t *opt_found;

  rval= 0;

  if (item->values_num < 1) {
    WARNING(WARN_PREFIX "%s requires at least one keyword", item->key);
    rval= -1;
  } else {
    for (i= 0; i < item->values_num; i++) {
      value= &item->values[i];
    
      if (value->type != OCONFIG_TYPE_STRING) {
        ERROR(ERROR_PREFIX "%s keyword is not a string", item->key);
        rval= -1;
      } else {
        opt_found= NULL;
        for (opt= 0; (opt_found == NULL) && (opt < opt_cnt); opt++) {
          if (strcmp(value->value.string, opt_list[opt].keyword) == 0) {
            opt_found= &opt_list[opt];
          }
        }

        if (opt_found == NULL) {
          ERROR(ERROR_PREFIX "Keyword %s is not valid", value->value.string);
          rval= -1;
        } else {
          *out_val|= opt_found->set_mask;
          *out_val&= ~(opt_found->clear_mask);
        }
      }
    }
  }

  return rval;
}

/*
 * Keymangle option flags
 */
static wmc_optval_t wmc_keymangle_opts[]= {
  { "uppercase", WMC_KEYMANGLE_UPPERCASE, WMC_KEYMANGLE_LOWERCASE },
  { "lowercase", WMC_KEYMANGLE_LOWERCASE, WMC_KEYMANGLE_UPPERCASE },
  { "hidesnmp", WMC_KEYMANGLE_HIDESNMP, 0 }
};

/*
 * Configure a rule
 */
static int wmc_config_rule(wmc_output_rule_t *rule, oconfig_item_t *item)
{
  int rval;
  int i;
  oconfig_item_t *child;

  static wmc_optval_t action_opts[]= {
    { "allow", WMC_OUTPUT_MATCH_ALLOW, WMC_OUTPUT_MATCH_DENY },
    { "deny", WMC_OUTPUT_MATCH_DENY, WMC_OUTPUT_MATCH_ALLOW },
    { "continue", 0, WMC_OUTPUT_MATCH_ALLOW | WMC_OUTPUT_MATCH_DENY }
  };

  rval= 0;
  for (i= 0; !rval && (i < item->children_num); i++) {
    child= &item->children[i];

    if (strcasecmp(child->key, "Text") == 0) {
      rval= wmc_set_string(child, &rule->text);
    } else if (strcasecmp(child->key, "Regex") == 0) {
      rval= wmc_set_string(child, &rule->regex);
      if (!rval && (rule->regex != NULL)) {
        if (regcomp(&rule->compiled_regex,
          rule->regex, REG_ICASE | REG_NOSUB) != 0)
        { 
          ERROR(ERROR_PREFIX
            "Failed to compile regular expression '%s'", rule->regex);
          rval= -1;

          /* If compiled_regex is NULL regex must be too */
          free(rule->regex);
          rule->regex= NULL;
        }
      }
    } else if (strcasecmp(child->key, "Prefix") == 0) {
      rval= wmc_set_string(child, &rule->prefix);
    } else if (strcasecmp(child->key, "KeyMangle") == 0) {
      rval= wmc_set_flags(child, wmc_keymangle_opts,
        STATIC_ARRAY_SIZE(wmc_keymangle_opts), &rule->keymangle_flags);
      if (!rval) {
        rule->keymangle_flags|= WMC_KEYMANGLE_OVERRIDE;
      }
    } else if (strcasecmp(child->key, "Action") == 0) {
      rval= wmc_set_flags(child, action_opts,
        STATIC_ARRAY_SIZE(action_opts), &rule->action_flags);
    } else {
      ERROR(ERROR_PREFIX "Unknown rule directive '%s'", child->key);
      rval= -1;
    }
  }

  return rval;
}

/*
 * Configure an output
 */
static int wmc_config_output(wmc_output_t *output, oconfig_item_t *item)
{
  int rval;
  int i;
  oconfig_item_t *child;
  wmc_output_rule_t *rule;

  rval= 0;
  for (i= 0; !rval && (i < item->children_num); i++) {
    child= &item->children[i];

    if (strcasecmp(child->key, "Server") == 0) {
      rval= wmc_set_string(child, &output->server);
    } else if (strcasecmp(child->key, "Prefix") == 0) {
      rval= wmc_set_string(child, &output->prefix);
    } else if (strcasecmp(child->key, "KeyMangle") == 0) {
      rval= wmc_set_flags(child, wmc_keymangle_opts,
        STATIC_ARRAY_SIZE(wmc_keymangle_opts), &output->keymangle_flags);
    } else if (strcasecmp(child->key, "Rule") == 0) {
      rule= wmc_output_rule_create(output);
      rval= wmc_config_rule(rule, child);
    }
  }
  return rval;
}

/*
 * Main config function
 */
static int wmc_config_main(oconfig_item_t *item)  /* LOCKING */
{
  int rval;
  int i;
  oconfig_item_t *child;
  wmc_output_t *output;

  pthread_mutex_lock(&wmc_global_lock);

  rval= 0;
  for (i= 0; !rval && (i < item->children_num); i++) {
    child= &item->children[i];
    if (strcasecmp(child->key, "Output") == 0) {
      output= wmc_output_create();
      rval= wmc_config_output(output, child);

      if (!rval && (output->server == NULL)) {
        ERROR(ERROR_PREFIX "Output has no server directive");
        rval= -1;
      }
    } else {
      ERROR(ERROR_PREFIX "Unknown option '%s'", child->key);
      rval= -1;
    }
  }

  pthread_mutex_unlock(&wmc_global_lock);

  return rval;
}

/*
 * Init - start memcached connections
 */
static int wmc_init(void)  /* LOCKING */
{
  wmc_output_t *output;
  memcached_server_list_st server_list;

  int rval;

  pthread_mutex_lock(&wmc_global_lock);

  rval= 0;
  for (output= output_head; output != NULL; output= output->next) {
    if (output->server != NULL) {
      output->conn= memcached_create(NULL);
      if (output->conn == NULL) {
        ERROR(ERROR_PREFIX "Failed to alloc connection for %s", output->server);
        rval= -1;
      } else {
        server_list= memcached_servers_parse(output->server);
        if (server_list == NULL) {
          ERROR(ERROR_PREFIX "Failed to parse server list %s", output->server);
          rval= -1;
          memcached_free(output->conn);
          output->conn= NULL;
        } else {
          memcached_server_push(output->conn, server_list);
          memcached_server_list_free(server_list);
        }
      }
    }
  }
  pthread_mutex_unlock(&wmc_global_lock);

  return rval;
}

/*
 * store key/value in all applicable outputs
 */
static int wmc_publish(char *key, char *json)  /* LOCKING */
{
  int rval;
  int json_len;
  wmc_output_t *output;
  wmc_output_rule_t *rule;

  int keymangle_flags;
  char *prefix;
  char *mangled_key;
  char *mangle_p;

  int is_allowed;
  int is_rule;
  int rule_no;

  rval= 0;

  json_len= strlen(json);

  pthread_mutex_lock(&wmc_global_lock);

  for (output= output_head; output != NULL; output= output->next) {
    prefix= output->prefix;
    keymangle_flags= output->keymangle_flags;

    if (output->rule_head == NULL) {
      is_allowed= 1;
    } else {
      is_allowed= 0;
      is_rule= 0;
      for (rule= output->rule_head, rule_no= 1;
        !is_rule && (rule != NULL); rule= rule->next, rule_no++)
      {
        is_rule= 0;
        if (rule->text != NULL) {
          if (strstr(key, rule->text) != NULL) {
            is_rule= 1;
          }
        }
        if (rule->regex != NULL) {
          if (regexec(&rule->compiled_regex, key, 0, NULL, 0) == 0) {
            is_rule= 1;
          }
        }

        if (is_rule) {
          /* Apply side effects if hit */
          if (rule->prefix != NULL) {
            prefix= rule->prefix;
          }
          if (rule->keymangle_flags & WMC_KEYMANGLE_OVERRIDE) {
            keymangle_flags= rule->keymangle_flags;
          }

          if (rule->action_flags & WMC_OUTPUT_MATCH_ALLOW) {
            is_allowed= 1;
          } else if (rule->action_flags & WMC_OUTPUT_MATCH_DENY) {
            /* Fall thru */
          } else {
            /* Neither allow nor deny - continue to remaining rules */
            is_rule= 0;
          }
        }
      }
    }

    if (is_allowed) {
      if ((prefix != NULL) || (keymangle_flags)) {
        mangled_key= malloc(strlen(prefix) + strlen(key) + 1);
        strcpy(mangled_key, prefix);
        strcat(mangled_key, key);

        if (keymangle_flags & WMC_KEYMANGLE_HIDESNMP) {
          mangle_p= strstr(mangled_key, ".snmp.");
          if (mangle_p != NULL) {
            /* ...dangerous ground... */
            mangle_p++;
            memmove(mangle_p, &mangle_p[5], strlen(&mangle_p[5]) + 1);
          }
        }

        if (keymangle_flags & WMC_KEYMANGLE_UPPERCASE) {
          for (mangle_p= mangled_key; *mangle_p != '\0'; mangle_p++) {
            *mangle_p= toupper(*mangle_p);
          }
        } else if (keymangle_flags & WMC_KEYMANGLE_LOWERCASE) {
          for (mangle_p= mangled_key; *mangle_p != '\0'; mangle_p++) {
            *mangle_p= tolower(*mangle_p);
          }
        }
      } else {
        mangled_key= key;
      }

      if (memcached_set(output->conn, mangled_key,
        strlen(mangled_key), json, json_len, KEY_EXPIRE_TIME, 0)
        != MEMCACHED_SUCCESS)
      {
        WARNING(WARN_PREFIX "Error setting %s", key);
        rval= -1;
      }

      if (mangled_key != key) {
        free(mangled_key);
      }
    }
  }

  pthread_mutex_unlock(&wmc_global_lock);

  return rval;
}

/*
 * Format a key into a delimited string
 */
static char *wmc_format_key(const value_list_t *vl)
{
  static char *path_sep= ".";
  char *key;

  /* In case struct defs change... */
  key= malloc(
    strlen(vl->host) +
    strlen(vl->plugin) +
    strlen(vl->plugin_instance) +
    strlen(vl->type) +
    strlen(vl->type_instance) +
    (strlen(path_sep) * 4) + 1
  );

  strcpy(key, vl->host);
  strcat(key, path_sep);
  strcat(key, vl->plugin);
  if (vl->plugin_instance[0] != '\0') {
    strcat(key, path_sep);
    strcat(key, vl->plugin_instance);
  }
  if (strcmp(vl->plugin, vl->type) != 0) {
    /* Plugin and type are the same a lot of times - it's redundant */
    strcat(key, path_sep);
    strcat(key, vl->type);
  }
  if (vl->type_instance[0] != '\0') {
    strcat(key, path_sep);
    strcat(key, vl->type_instance);
  }

  return key;
}

/*
 * Main write callback function
 */
static int wmc_write(const data_set_t *ds,
                     const value_list_t *vl, user_data_t *ud)
{
  /* NOTE: No locking is done here since we don't touch global state. */

  char *key;

  char json[JSON_BUFFER_SIZE + 1];
  size_t json_fill;
  size_t json_free;

  int rval;

  rval= 0;
  json_fill= 0;
  json_free= STATIC_ARRAY_SIZE(json) - 1;


  if (strcmp(ds->type, vl->type) != 0) {
    /* I'm not sure why this is a problem, but I saw it tested elsewhere */
    ERROR(ERROR_PREFIX "DS type does not match value list type");
    rval= -1;
  }

  if (!rval) {
    /*
     * The JSON util stuff seems to be tied to write_http - I'm not sure if
     * it's part of the API.
     */
    rval= format_json_initialize(json, &json_fill, &json_free);
    if (rval) {
      ERROR(ERROR_PREFIX "format_json_initialize failed: %d", rval);
    }
  }

  if (!rval) {
    rval= format_json_value_list(json, &json_fill, &json_free, ds, vl, 1);
    if (rval) {
      ERROR(ERROR_PREFIX "format_json_value_list failed: %d", rval);
    }
  }

  if (!rval) {
    rval= format_json_finalize(json, &json_fill, &json_free);
    if (rval) {
      ERROR(ERROR_PREFIX "format_json_finalize failed: %d", rval);
    }
  }

  if (!rval) {
    key= wmc_format_key(vl);
    rval= wmc_publish(key, json);
    free(key);
  }

  return rval;
}

/*
 * Clean up global state
 */
static int wmc_rundown(void)
{
  wmc_output_t *output;
  wmc_output_rule_t *rule;
  int rval;

  rval= 0;
  while ((output= output_head) != NULL) {
    output_head= output->next;

    while ((rule= output->rule_head) != NULL) {
      output->rule_head= rule->next;

      if (rule->text != NULL) {
        free(rule->text);
      }
      if (rule->regex != NULL) {
        regfree(&rule->compiled_regex);
        free(rule->regex);
      }
      if (rule->prefix != NULL) {
        free(rule->prefix);
      }
    }

    if (output->conn != NULL) {
      memcached_free(output->conn);
    }

    if (output->server != NULL) {
      free(output->server);
    }
    if (output->prefix != NULL) {
      free(output->prefix);
    }
    free(output);
  }

  pthread_mutex_destroy(&wmc_global_lock);

  return rval;
}

/*
 * Called by framework using dlsym et al
 */
void module_register(void)
{
  output_head= NULL;
  output_tail= NULL;

  pthread_mutex_init(&wmc_global_lock, NULL);

  static char const *module_name= "write_memcache";

  plugin_register_complex_config(module_name, wmc_config_main);
  plugin_register_init(module_name, wmc_init);
  plugin_register_shutdown(module_name, wmc_rundown);
  plugin_register_write(module_name, wmc_write, NULL);
}


