/**
 * collectd - src/utils_cmd_putval.c
 * Copyright (C) 2007-2009  Florian octo Forster
 * Copyright (C) 2016       Sebastian tokkee Harl
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
 *   Sebastian tokkee Harl <sh at tokkee.org>
 **/

#include "collectd.h"

#include "utils/cmds/putval.h"
#include "utils/common/common.h"

/*
 * private helper functions
 */

static int is_quoted(const char *str, size_t len) {
  if (len < 2) {
    return 0;
  }

  return (str[0] == '"' && str[len - 1] == '"');
}

static int set_option(metric_t *metric_p, const char *key, const char *value,
                      cmd_error_handler_t *err) {
  if ((metric_p == NULL) || (key == NULL) || (value == NULL))
    return -1;

  if (strcasecmp("interval", key) == 0) {
    double tmp;
    char *endptr;

    endptr = NULL;
    errno = 0;
    tmp = strtod(value, &endptr);

    if ((errno == 0) && (endptr != NULL) && (endptr != value) && (tmp > 0.0))
      metric_p->interval = DOUBLE_TO_CDTIME_T(tmp);
  } else if (strncasecmp("meta:", key, 5) == 0) {
    const char *meta_key = key + 5;
    size_t value_len = strlen(value);

    if (metric_p->meta == NULL) {
      metric_p->meta = meta_data_create();
      if (metric_p->meta == NULL) {
        return CMD_ERROR;
      }
    }

    if (is_quoted(value, value_len)) {
      int metadata_err;
      const char *value_str = sstrndup(value + 1, value_len - 2);
      if (value_str == NULL) {
        return CMD_ERROR;
      }
      metadata_err = meta_data_add_string(metric_p->meta, meta_key, value_str);
      free((void *)value_str);
      if (metadata_err != 0) {
        return CMD_ERROR;
      }
      return CMD_OK;
    }

    cmd_error(CMD_NO_OPTION, err, "Non-string metadata not supported yet");
    return CMD_NO_OPTION;
  } else {
    return CMD_ERROR;
  }
  return CMD_OK;
} /* int set_option */

/*
 * public API
 */

cmd_status_t cmd_parse_putval(size_t argc, char **argv,
                              cmd_putval_t *ret_putval,
                              const cmd_options_t *opts,
                              cmd_error_handler_t *err) {
  cmd_status_t result;

  char *identifier;
  char *hostname;
  char *plugin;
  char *type;
  char *data_source;
  int status;

  char *identifier_copy;

  const data_set_t *ds;
  metric_t metric = METRIC_STRUCT_INIT;

  if ((ret_putval == NULL) || (opts == NULL)) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, err, "Invalid arguments to cmd_parse_putval.");
    return CMD_ERROR;
  }

  if (argc < 2) {
    cmd_error(CMD_PARSE_ERROR, err, "Missing identifier and/or value-list.");
    return CMD_PARSE_ERROR;
  }

  identifier = argv[0];

  /* parse_identifier() modifies its first argument, returning pointers into
   * it; retain the old value for later. */
  identifier_copy = sstrdup(identifier);

  status =
      parse_identifier(identifier, &hostname, &plugin, &type,
                       &data_source, opts->identifier_default_host);
  if (status != 0) {
    DEBUG("cmd_handle_putval: Cannot parse identifier `%s'.", identifier_copy);
    cmd_error(CMD_PARSE_ERROR, err, "Cannot parse identifier `%s'.",
              identifier_copy);
    sfree(identifier_copy);
    return CMD_PARSE_ERROR;
  }

  if ((strlen(type) >= sizeof(metric.type)) ||
      (strlen(plugin) >= sizeof(metric.plugin))
      ) {
    cmd_error(CMD_PARSE_ERROR, err, "Identifier too long.");
    sfree(identifier_copy);
    return CMD_PARSE_ERROR;
  }

  metric.identity = create_identity(plugin, type, data_source, hostname);
  if (metric.identity == NULL) {
    sfree(identifier_copy);
    return CMD_PARSE_ERROR;
  }

  sstrncpy(metric.plugin, plugin, sizeof(metric.plugin));
  sstrncpy(metric.type, type, sizeof(metric.type));


  metric.ds = plugin_get_ds(type);
  if (metric.ds == NULL) {
    cmd_error(CMD_PARSE_ERROR, err, "1 Type `%s' isn't defined.", type);
    destroy_identity(metric.identity);
    sfree(identifier_copy);
    return CMD_PARSE_ERROR;
  }

  hostname = NULL;
  plugin = NULL;
  type = NULL;
  data_source = NULL;

  ret_putval->raw_identifier = identifier_copy;
  if (ret_putval->raw_identifier == NULL) {
    cmd_error(CMD_ERROR, err, "malloc failed.");
    cmd_destroy_putval(ret_putval);
    destroy_identity(metric.identity);
    return CMD_ERROR;
  }

  /* All the remaining fields are part of the option list. */
  result = CMD_OK;
  for (size_t i = 1; i < argc; ++i) {
    value_list_t *tmp;

    char *key = NULL;
    char *value = NULL;

    status = cmd_parse_option(argv[i], &key, &value, err);
    if (status == CMD_OK) {
      assert(key != NULL);
      assert(value != NULL);
      int option_err = set_option(&metric, key, value, err);
      if (option_err != CMD_OK && option_err != CMD_NO_OPTION) {
        result = option_err;
        break;
      }
      continue;
    } else if (status != CMD_NO_OPTION) {
      /* parse_option failed, buffer has been modified.
       * => we need to abort */
      result = status;
      break;
    }
    /* else: cmd_parse_option did not find an option; treat this as a
     * value. */

    status = parse_values(argv[i], &metric);
    if (status != 0) {
      cmd_error(CMD_PARSE_ERROR, err, "Parsing the values string failed.");
      result = CMD_PARSE_ERROR;
      break;
    }

    tmp = realloc(ret_putval->vl,
                  (ret_putval->vl_num + 1) * sizeof(*ret_putval->vl));
    if (tmp == NULL) {
      cmd_error(CMD_ERROR, err, "realloc failed.");
      cmd_destroy_putval(ret_putval);
      result = CMD_ERROR;
      break;
    }

    ret_putval->vl = tmp;
    ret_putval->vl_num++;
    memcpy(&ret_putval->vl[ret_putval->vl_num - 1], &vl, sizeof(vl));

    /* pointer is now owned by ret_putval->vl[] */
    vl.values_len = 0;
    vl.values = NULL;
  } /* while (*buffer != 0) */
  /* Done parsing the options. */

  if (result != CMD_OK)
    cmd_destroy_putval(ret_putval);

  return result;
} /* cmd_status_t cmd_parse_putval */

void cmd_destroy_putval(cmd_putval_t *putval) {
  if (putval == NULL)
    return;

  sfree(putval->raw_identifier);
  destroy_metrics_list(putval->ml);
  putval->ml = NULL;
} /* void cmd_destroy_putval */

cmd_status_t cmd_handle_putval(FILE *fh, char *buffer) {
  cmd_error_handler_t err = {cmd_error_fh, fh};
  cmd_t cmd;

  int status;

  DEBUG("utils_cmd_putval: cmd_handle_putval (fh = %p, buffer = %s);",
        (void *)fh, buffer);

  if ((status = cmd_parse(buffer, &cmd, NULL, &err)) != CMD_OK)
    return status;
  if (cmd.type != CMD_PUTVAL) {
    cmd_error(CMD_UNKNOWN_COMMAND, &err, "Unexpected command: `%s'.",
              CMD_TO_STRING(cmd.type));
    cmd_destroy(&cmd);
    return CMD_UNKNOWN_COMMAND;
  }

  for (size_t i = 0; i < cmd.cmd.putval.vl_num; ++i)
    plugin_dispatch_values(&cmd.cmd.putval.vl[i]);

  if (fh != stdout)
    cmd_error(CMD_OK, &err, "Success: %i %s been dispatched.",
              (int)cmd.cmd.putval.vl_num,
              (cmd.cmd.putval.vl_num == 1) ? "value has" : "values have");

  cmd_destroy(&cmd);
  return CMD_OK;
} /* int cmd_handle_putval */

int cmd_create_putval(char *ret, size_t ret_len, /* {{{ */
                      const metric_t *metric_p) {
  char buffer_ident[6 * DATA_MAX_NAME_LEN];
  char buffer_values[1024];
  int status;

  status = FORMAT_VL(buffer_ident, sizeof(buffer_ident), vl);
  if (status != 0)
    return status;
  escape_string(buffer_ident, sizeof(buffer_ident));

  status = format_values(buffer_values, sizeof(buffer_values), metric_p,
                         /* store rates = */ false);
  if (status != 0)
    return status;
  escape_string(buffer_values, sizeof(buffer_values));

  snprintf(ret, ret_len, "PUTVAL %s interval=%.3f %s", buffer_ident,
           (metric_p->interval > 0) ? CDTIME_T_TO_DOUBLE(metric_p->interval)
                              : CDTIME_T_TO_DOUBLE(plugin_get_interval()),
           buffer_values);

  return 0;
} /* }}} int cmd_create_putval */
