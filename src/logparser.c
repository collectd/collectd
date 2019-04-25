/**
 * collectd - src/logparser.c
 *
 * Copyright(c) 2018 Intel Corporation. All rights reserved.
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
 *   Marcin Mozejko <marcinx.mozejko@intel.com>
 **/

#include "collectd.h"

#include "utils/common/common.h"
#include "utils/message_parser/message_parser.h"
#include "utils_llist.h"

#define PLUGIN_NAME "logparser"

#define LOGPARSER_SEV_OK_STR "OK"
#define LOGPARSER_SEV_WARN_STR "WARNING"
#define LOGPARSER_SEV_FAIL_STR "FAILURE"

#define LOGPARSER_PLUGIN_INST_STR "PluginInstance"
#define LOGPARSER_TYPE_STR "Type"
#define LOGPARSER_TYPE_INST_STR "TypeInstance"
#define LOGPARSER_SEVERITY_STR "Severity"

#define MAX_STR_LEN 128
#define MAX_FIELDS 4 /* PluginInstance, Type, TypeInstance, Severity */

#define START_IDX 0
#define STOP_IDX (parser->patterns_len - 1)

typedef enum message_item_type_e {
  MSG_ITEM_PLUGIN_INST = 0,
  MSG_ITEM_TYPE,
  MSG_ITEM_TYPE_INST,
  MSG_ITEM_SEVERITY
} message_item_type_t;

typedef struct message_item_info_s {
  /* Type of message item used for special processing */
  message_item_type_t type;
  union {
    /* If set, will override message item string with this one */
    char *str_override;
    /* Used only if type is MSG_ITEM_SEVERITY */
    int severity;
  } val;
} message_item_info_t;

typedef struct message_item_user_data_s {
  /* Information telling what to do when match found */
  message_item_info_t infos[MAX_FIELDS];
  size_t infos_len;
} message_item_user_data_t;

typedef struct log_parser_s {
  char *name;
  parser_job_data_t *job;
  message_pattern_t *patterns;
  size_t patterns_len;
  bool first_read;
  char *filename;
  char *def_plugin_inst;
  char *def_type;
  char *def_type_inst;
  int def_severity;
} log_parser_t;

typedef struct logparser_ctx_s {
  log_parser_t *parsers;
  size_t parsers_len;
} logparser_ctx_t;

static logparser_ctx_t logparser_ctx;

static int logparser_shutdown(void);

static void logparser_free_user_data(void *data) {
  message_item_user_data_t *user_data = (message_item_user_data_t *)data;

  if (user_data == NULL)
    return;

  for (size_t i = 0; i < user_data->infos_len; i++) {
    if (user_data->infos[i].type != MSG_ITEM_SEVERITY)
      sfree(user_data->infos[i].val.str_override);
  }
  sfree(user_data);
}

static int logparser_config_msg_item_type(oconfig_item_t *ci,
                                          message_item_user_data_t **user_data,
                                          message_item_type_t type) {
  bool val;
  int ret;
  char *str = NULL;

  if (*user_data == NULL) {
    *user_data = calloc(1, sizeof(**user_data));
    if (*user_data == NULL) {
      ERROR(PLUGIN_NAME ": Could not allocate memory");
      return -1;
    }
    (*user_data)->infos_len = 0;
  }

  size_t i = (*user_data)->infos_len;

  switch (ci->values[0].type) {
  case OCONFIG_TYPE_STRING:
    ret = cf_util_get_string(ci, &str);
    break;
  case OCONFIG_TYPE_BOOLEAN:
    ret = cf_util_get_boolean(ci, &val);
    if (val == false || type == MSG_ITEM_SEVERITY)
      goto wrong_value;
    break;
  default:
    ERROR(PLUGIN_NAME ": Wrong type for option %s", ci->key);
    goto error;
  }

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Error getting %s option", ci->key);
    goto error;
  }

  if (type == MSG_ITEM_SEVERITY) {
    if (strcasecmp(LOGPARSER_SEV_OK_STR, str) == 0)
      (*user_data)->infos[i].val.severity = NOTIF_OKAY;
    else if (strcasecmp(LOGPARSER_SEV_WARN_STR, str) == 0)
      (*user_data)->infos[i].val.severity = NOTIF_WARNING;
    else if (strcasecmp(LOGPARSER_SEV_FAIL_STR, str) == 0)
      (*user_data)->infos[i].val.severity = NOTIF_FAILURE;
    else {
      sfree(str);
      goto wrong_value;
    }
    sfree(str);
  } else
    (*user_data)->infos[i].val.str_override = str;

  (*user_data)->infos[i].type = type;
  (*user_data)->infos_len++;
  return 0;

wrong_value:
  ERROR(PLUGIN_NAME ": Wrong value for option %s", ci->key);
error:
  sfree(*user_data);
  return -1;
}

static int logparser_config_match(oconfig_item_t *ci, log_parser_t *parser) {
  int ret;
  message_item_user_data_t *user_data = NULL;
  message_pattern_t *ptr = NULL;

  ptr = realloc(parser->patterns, sizeof(*ptr) * (parser->patterns_len + 1));
  if (ptr == NULL) {
    ERROR(PLUGIN_NAME ": Error reallocating memory for message patterns.");
    return -1;
  }

  message_pattern_t *pattern = ptr + parser->patterns_len;

  memset(pattern, 0, sizeof(*pattern));
  pattern->is_mandatory = 1;
  ret = cf_util_get_string(ci, &pattern->name);

  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Error getting match  name");
    goto free_ptr;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Regex", child->key) == 0)
      ret = cf_util_get_string(child, &pattern->regex);
    else if (strcasecmp("SubmatchIdx", child->key) == 0)
      ret = cf_util_get_int(child, &pattern->submatch_idx);
    else if (strcasecmp("ExcludeRegex", child->key) == 0)
      ret = cf_util_get_string(child, &pattern->excluderegex);
    else if (strcasecmp("IsMandatory", child->key) == 0)
      ret = cf_util_get_boolean(child, &pattern->is_mandatory);
    else if (strcasecmp(LOGPARSER_PLUGIN_INST_STR, child->key) == 0)
      ret = logparser_config_msg_item_type(child, &user_data,
                                           MSG_ITEM_PLUGIN_INST);
    else if (strcasecmp(LOGPARSER_TYPE_STR, child->key) == 0)
      ret = logparser_config_msg_item_type(child, &user_data, MSG_ITEM_TYPE);
    else if (strcasecmp(LOGPARSER_TYPE_INST_STR, child->key) == 0)
      ret =
          logparser_config_msg_item_type(child, &user_data, MSG_ITEM_TYPE_INST);
    else if (strcasecmp(LOGPARSER_SEVERITY_STR, child->key) == 0)
      ret =
          logparser_config_msg_item_type(child, &user_data, MSG_ITEM_SEVERITY);
    else {
      ERROR(PLUGIN_NAME ": Invalid configuration option \"%s\".", child->key);
      goto free_user_data;
    }
    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Error getting %s option", child->key);
      goto free_user_data;
    }
  }

  if (user_data != NULL) {
    pattern->user_data = (void *)user_data;
    pattern->free_user_data = logparser_free_user_data;
  }

  parser->patterns = ptr;
  parser->patterns_len++;

  return 0;

free_user_data:
  if (user_data != NULL)
    logparser_free_user_data(user_data);
free_ptr:
  sfree(ptr);
  return -1;
}

static int logparser_config_message(const oconfig_item_t *ci, char *filename,
                                    bool first_read) {
  char *msg_name = NULL;
  char *severity = NULL;
  int ret;
  log_parser_t *ptr;

  ret = cf_util_get_string(ci, &msg_name);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Error getting message name");
    return -1;
  }

  ptr = realloc(logparser_ctx.parsers,
                sizeof(*ptr) * (logparser_ctx.parsers_len + 1));

  if (ptr == NULL) {
    ERROR(PLUGIN_NAME ": Error reallocating memory for message parsers.");
    goto error;
  }

  logparser_ctx.parsers = ptr;

  log_parser_t *parser = ptr + logparser_ctx.parsers_len;

  memset(parser, 0, sizeof(*parser));
  parser->name = msg_name;
  parser->first_read = first_read;
  parser->filename = filename;
  parser->def_severity = NOTIF_OKAY;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("Match", child->key) == 0)
      ret = logparser_config_match(child, parser);
    else if (strcasecmp("DefaultPluginInstance", child->key) == 0)
      ret = cf_util_get_string(child, &parser->def_plugin_inst);
    else if (strcasecmp("DefaultType", child->key) == 0)
      ret = cf_util_get_string(child, &parser->def_type);
    else if (strcasecmp("DefaultTypeInstance", child->key) == 0)
      ret = cf_util_get_string(child, &parser->def_type_inst);
    else if (strcasecmp("DefaultSeverity", child->key) == 0) {
      ret = cf_util_get_string(child, &severity);
      if (strcasecmp(LOGPARSER_SEV_OK_STR, severity) == 0)
        parser->def_severity = NOTIF_OKAY;
      else if (strcasecmp(LOGPARSER_SEV_WARN_STR, severity) == 0)
        parser->def_severity = NOTIF_WARNING;
      else if (strcasecmp(LOGPARSER_SEV_FAIL_STR, severity) == 0)
        parser->def_severity = NOTIF_FAILURE;
      else {
        ERROR(PLUGIN_NAME ": Invalid severity value: \"%s\".", severity);
        sfree(severity);
        goto error;
      }
      sfree(severity);
    } else {
      ERROR(PLUGIN_NAME ": Invalid configuration option \"%s\".", child->key);
      goto error;
    }
    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Error getting %s option", child->key);
      goto error;
    }
  }
  logparser_ctx.parsers_len++;

  return 0;

error:
  sfree(msg_name);
  return -1;
}

static int logparser_config_logfile(oconfig_item_t *ci) {
  char *filename = NULL;
  bool first_read = false; // First full read
  int ret = 0;

  ret = cf_util_get_string(ci, &filename);
  if (ret != 0) {
    ERROR(PLUGIN_NAME ": Error getting filename");
    return -1;
  }

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;
    if (strcasecmp("FirstFullRead", child->key) == 0)
      ret = cf_util_get_boolean(child, &first_read);
    else if (strcasecmp("Message", child->key) == 0)
      ret = logparser_config_message(child, filename, first_read);
    else {
      ERROR(PLUGIN_NAME ": Invalid configuration option \"%s\".", child->key);
      goto error;
    }

    if (ret != 0) {
      ERROR(PLUGIN_NAME ": Error getting %s option", child->key);
      goto error;
    }
  }

  return 0;

error:
  sfree(filename);
  return -1;
}

static int logparser_validate_config(void) {
  for (size_t i = 0; i < logparser_ctx.parsers_len; i++) {
    log_parser_t *parser = logparser_ctx.parsers + i;

    if (strlen(parser->filename) < 1) {
      ERROR(PLUGIN_NAME ": Log filename in \"%s\" message can't be empty",
            parser->name);
      return -1;
    }

    if (parser->def_plugin_inst != NULL &&
        strlen(parser->def_plugin_inst) < 1) {
      ERROR(PLUGIN_NAME
            ": DefaultPluginInstance in \"%s\" message can't be empty",
            parser->name);
      return -1;
    }

    if (parser->def_type != NULL && strlen(parser->def_type) < 1) {
      ERROR(PLUGIN_NAME ": DefaultType in \"%s\" message can't be empty",
            parser->name);
      return -1;
    }

    if (parser->def_type_inst != NULL && strlen(parser->def_type_inst) < 1) {
      ERROR(PLUGIN_NAME
            ": DefaultTypeInstance in \"%s\" message can't be empty",
            parser->name);
      return -1;
    }

    if (parser->patterns_len < 2) {
      ERROR(PLUGIN_NAME ": Message \"%s\" should have at least 2 matches",
            parser->name);
      return -1;
    }

    if (parser->patterns[START_IDX].is_mandatory != 1) {
      ERROR(PLUGIN_NAME
            ": Start match \"%s\" in message \"%s\" can't be optional",
            parser->patterns[START_IDX].name, parser->name);
      return -1;
    }

    if (parser->patterns[STOP_IDX].is_mandatory != 1) {
      ERROR(PLUGIN_NAME
            ": Stop match \"%s\" in message \"%s\" can't be optional",
            parser->patterns[STOP_IDX].name, parser->name);
      return -1;
    }

    for (int j = 0; j < parser->patterns_len; j++) {
      message_pattern_t *pattern = parser->patterns + j;

      if (pattern->regex == NULL) {
        ERROR(PLUGIN_NAME
              ": Regex must be set (message: \"%s\", match: \"%s\")",
              parser->name, pattern->name);
        return -1;
      } else if (strlen(pattern->regex) < 1) {
        ERROR(PLUGIN_NAME
              ": Regex can't be empty (message: \"%s\", match: \"%s\")",
              parser->name, pattern->name);
        return -1;
      }

      if (pattern->excluderegex != NULL && strlen(pattern->excluderegex) < 1) {
        ERROR(PLUGIN_NAME
              ": ExcludeRegex can't be empty (message: \"%s\", match: \"%s\")",
              parser->name, pattern->name);
        return -1;
      }

      if (pattern->submatch_idx < -1) {
        ERROR(PLUGIN_NAME ": SubmatchIdx must be in range [-1..n]");
        return -1;
      }

      if (pattern->user_data != NULL && pattern->submatch_idx == -1)
        WARNING(PLUGIN_NAME ": Options [PluginInstance, Type, TypeInstance, "
                            "Severity] are omitted when SubmatchIdx is set to "
                            "-1 (message: \"%s\", match: \"%s\")",
                parser->name, pattern->name);
    }
  }

  return 0;
}

static int logparser_config(oconfig_item_t *ci) {
  int ret;

  for (int i = 0; i < ci->children_num; i++) {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp("Logfile", child->key) == 0) {
      ret = logparser_config_logfile(child);

      if (ret != 0) {
        return -1;
      }
    }
  }

  return logparser_validate_config();
}

#if COLLECT_DEBUG
static void logparser_print_config(void) {
  const char *severity_desc[5] = {NULL, LOGPARSER_SEV_FAIL_STR,
                                  LOGPARSER_SEV_WARN_STR, NULL,
                                  LOGPARSER_SEV_OK_STR};
  const char *item_type_desc[MAX_FIELDS] = {
      LOGPARSER_PLUGIN_INST_STR, LOGPARSER_TYPE_STR, LOGPARSER_TYPE_INST_STR,
      LOGPARSER_SEVERITY_STR};
  const char *bool_str[2] = {"False", "True"};

  DEBUG(PLUGIN_NAME ": ==========LOGPARSER CONFIG=============");
  DEBUG(PLUGIN_NAME ": Message configs count: %zu", logparser_ctx.parsers_len);

  for (size_t i = 0; i < logparser_ctx.parsers_len; i++) {
    log_parser_t *parser = logparser_ctx.parsers + i;
    DEBUG(PLUGIN_NAME ": Message: \"%s\"", parser->name);
    DEBUG(PLUGIN_NAME ":   File: \"%s\"", parser->filename);
    if (parser->def_plugin_inst != NULL)
      DEBUG(PLUGIN_NAME ":   DefaultPluginInstance: \"%s\"",
            parser->def_plugin_inst);
    if (parser->def_type != NULL)
      DEBUG(PLUGIN_NAME ":   DefaultType: \"%s\"", parser->def_type);
    if (parser->def_type_inst != NULL)
      DEBUG(PLUGIN_NAME ":   DefaultTypeInstance: \"%s\"",
            parser->def_type_inst);
    DEBUG(PLUGIN_NAME ":   DefaultSeverity: %s",
          severity_desc[parser->def_severity]);
    DEBUG(PLUGIN_NAME ":   Match configs count: %zu", parser->patterns_len);

    for (size_t j = 0; j < parser->patterns_len; j++) {
      message_pattern_t *pattern = parser->patterns + j;
      DEBUG(PLUGIN_NAME ":   Match: \"%s\"", pattern->name);
      DEBUG(PLUGIN_NAME ":     Regex: \"%s\"", pattern->regex);
      if (pattern->excluderegex != NULL)
        DEBUG(PLUGIN_NAME ":     ExcludeRegex: \"%s\"", pattern->excluderegex);
      DEBUG(PLUGIN_NAME ":     SubmatchIdx: %d", pattern->submatch_idx);
      DEBUG(PLUGIN_NAME ":     IsMandatory: %s",
            bool_str[pattern->is_mandatory]);
      if (pattern->user_data != NULL) {
        message_item_user_data_t *ud =
            (message_item_user_data_t *)pattern->user_data;
        for (size_t k = 0; k < ud->infos_len; k++) {
          if (ud->infos[k].type == MSG_ITEM_SEVERITY)
            DEBUG(PLUGIN_NAME ":     Severity: %s",
                  severity_desc[ud->infos[k].val.severity]);
          else {
            if (ud->infos[k].val.str_override != NULL)
              DEBUG(PLUGIN_NAME ":     %s: \"%s\"",
                    item_type_desc[ud->infos[k].type],
                    ud->infos[k].val.str_override);
            else
              DEBUG(PLUGIN_NAME ":     %s: %s",
                    item_type_desc[ud->infos[k].type], bool_str[1]);
          }
        }
      }
    }
  }
  DEBUG(PLUGIN_NAME ": =======================================");
}
#endif

static int logparser_init(void) {
#if COLLECT_DEBUG
  logparser_print_config();
#endif

  for (size_t i = 0; i < logparser_ctx.parsers_len; i++) {
    log_parser_t *parser = logparser_ctx.parsers + i;
    parser->job = message_parser_init(parser->filename, START_IDX, STOP_IDX,
                                      parser->patterns, parser->patterns_len);
    if (parser->job == NULL) {
      ERROR(PLUGIN_NAME ": Failed to initialize %s parser.",
            logparser_ctx.parsers[i].name);
      logparser_shutdown();
      return -1;
    }
  }

  return 0;
}

static void logparser_dispatch_notification(notification_t *n) {
  sstrncpy(n->host, hostname_g, sizeof(n->host));
  plugin_dispatch_notification(n);
  if (n->meta != NULL)
    plugin_notification_meta_free(n->meta);
}

static void logparser_process_msg(log_parser_t *parser, message_t *msg,
                                  unsigned int max_items) {
  notification_t n = {.severity = parser->def_severity,
                      .time = cdtime(),
                      .plugin = PLUGIN_NAME,
                      .meta = NULL};

  /* Writing  default values if set */
  if (parser->def_plugin_inst != NULL)
    sstrncpy(n.plugin_instance, parser->def_plugin_inst,
             sizeof(n.plugin_instance));
  if (parser->def_type != NULL)
    sstrncpy(n.type, parser->def_type, sizeof(n.type));
  if (parser->def_type_inst != NULL)
    sstrncpy(n.type_instance, parser->def_type_inst, sizeof(n.type_instance));

  for (int i = 0; i < max_items; i++) {
    message_item_t *item = msg->message_items + i;
    if (!item->value[0])
      break;

    DEBUG(PLUGIN_NAME ": [%02d] %s:%s", i, item->name, item->value);

    if (item->user_data != NULL) {
      message_item_user_data_t *user_data =
          (message_item_user_data_t *)item->user_data;

      for (size_t i = 0; i < user_data->infos_len; i++) {
        char *ptr = NULL;
        size_t size = 0;
        switch (user_data->infos[i].type) {
        case MSG_ITEM_SEVERITY:
          n.severity = user_data->infos[i].val.severity;
          break;
        case MSG_ITEM_PLUGIN_INST:
          ptr = n.plugin_instance;
          size = sizeof(n.plugin_instance);
          break;
        case MSG_ITEM_TYPE:
          ptr = n.type;
          size = sizeof(n.type);
          break;
        case MSG_ITEM_TYPE_INST:
          ptr = n.type_instance;
          size = sizeof(n.type_instance);
          break;
        default:
          ERROR(PLUGIN_NAME ": Message item has wrong type!");
          return;
        }

        if (ptr != NULL && size > 0) {
          if (user_data->infos[i].val.str_override != NULL)
            sstrncpy(ptr, user_data->infos[i].val.str_override, size);
          else
            sstrncpy(ptr, item->value, size);
        }
      }
    }

    if (plugin_notification_meta_add_string(&n, item->name, item->value))
      ERROR(PLUGIN_NAME ": Failed to add notification meta data %s:%s",
            item->name, item->value);
  }

  logparser_dispatch_notification(&n);
}

static int logparser_parser_read(log_parser_t *parser) {
  message_t *messages_storage;
  unsigned int max_item_num;
  int msg_num =
      message_parser_read(parser->job, &messages_storage, parser->first_read);

  if (msg_num < 0) {
    notification_t n = {.severity = NOTIF_FAILURE,
                        .time = cdtime(),
                        .message = "Failed to read from log file",
                        .plugin = PLUGIN_NAME,
                        .meta = NULL};
    logparser_dispatch_notification(&n);
    return -1;
  }

  max_item_num = STATIC_ARRAY_SIZE(messages_storage[0].message_items);

  DEBUG(PLUGIN_NAME ": read %d messages, %s", msg_num, parser->name);

  for (int i = 0; i < msg_num; i++) {
    message_t *msg = messages_storage + i;
    logparser_process_msg(parser, msg, max_item_num);
  }
  return 0;
}

static int logparser_read(__attribute__((unused)) user_data_t *ud) {
  int ret = 0;

  for (size_t i = 0; i < logparser_ctx.parsers_len; i++) {
    log_parser_t *parser = logparser_ctx.parsers + i;
    ret = logparser_parser_read(parser);
    if (parser->first_read)
      parser->first_read = false;

    if (ret < 0) {
      ERROR(PLUGIN_NAME ": Failed to parse %s messages from %s", parser->name,
            parser->filename);
      break;
    }
  }

  return ret;
}

static int logparser_shutdown(void) {
  if (logparser_ctx.parsers == NULL)
    return 0;

  for (size_t i = 0; i < logparser_ctx.parsers_len; i++) {
    log_parser_t *parser = logparser_ctx.parsers + i;

    if (parser->job != NULL)
      message_parser_cleanup(parser->job);

    for (size_t j = 0; j < parser->patterns_len; j++) {
      if (parser->patterns[j].free_user_data != NULL)
        parser->patterns[j].free_user_data(parser->patterns[j].user_data);

      sfree(parser->patterns[j].name);
      sfree(parser->patterns[j].regex);
      sfree(parser->patterns[j].excluderegex);
    }

    sfree(parser->patterns);
    sfree(parser->filename);
    sfree(parser->def_plugin_inst);
    sfree(parser->def_type);
    sfree(parser->def_type_inst);
    sfree(parser->name);
  }

  sfree(logparser_ctx.parsers);

  return 0;
}

void module_register(void) {
  plugin_register_complex_config(PLUGIN_NAME, logparser_config);
  plugin_register_init(PLUGIN_NAME, logparser_init);
  plugin_register_complex_read(NULL, PLUGIN_NAME, logparser_read, 0, NULL);
  plugin_register_shutdown(PLUGIN_NAME, logparser_shutdown);
}
