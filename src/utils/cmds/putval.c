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

static int is_quoted(char const *str, size_t len) {
  if (len < 2) {
    return 0;
  }

  return (str[0] == '"' && str[len - 1] == '"');
}

/* TODO(octo): add an option to set metric->value_type */
static int set_option(metric_t *m, char const *key, char const *value,
                      cmd_error_handler_t *err) {
  if ((m == NULL) || (key == NULL) || (value == NULL))
    return -1;

  if (strcasecmp("interval", key) == 0) {
    double tmp;
    char *endptr;

    endptr = NULL;
    errno = 0;
    tmp = strtod(value, &endptr);

    if ((errno == 0) && (endptr != NULL) && (endptr != value) && (tmp > 0.0))
      m->interval = DOUBLE_TO_CDTIME_T(tmp);
  } else if (strncasecmp("meta:", key, 5) == 0) {
    char const *meta_key = key + 5;
    size_t value_len = strlen(value);

    if (m->meta == NULL) {
      m->meta = meta_data_create();
      if (m->meta == NULL) {
        return CMD_ERROR;
      }
    }

    /* TODO(octo): this should deal with escaped characters. */
    if (is_quoted(value, value_len)) {
      int metadata_err;
      char const *value_str = sstrndup(value + 1, value_len - 2);
      if (value_str == NULL) {
        return CMD_ERROR;
      }
      metadata_err = meta_data_add_string(m->meta, meta_key, value_str);
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
                              cmd_options_t const *opts,
                              cmd_error_handler_t *errhndl) {
  if ((ret_putval == NULL) || (opts == NULL)) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, errhndl, "Invalid arguments to cmd_parse_putval.");
    return CMD_ERROR;
  }

  if (argc < 2) {
    cmd_error(CMD_PARSE_ERROR, errhndl,
              "Missing identifier and/or value-list.");
    return CMD_PARSE_ERROR;
  }

  char *identifier = strdup(argv[0]);
  if (ret_putval->raw_identifier == NULL) {
    cmd_error(CMD_ERROR, errhndl, "malloc failed.");
    return CMD_ERROR;
  }

  metric_family_t *fam =
      metric_family_unmarshal_text(identifier, METRIC_TYPE_UNTYPED);
  if (fam == NULL) {
    int err = errno;
    DEBUG("cmd_handle_putval: Parsing identifier \"%s\" failed: %s.",
          identifier, STRERROR(err));
    cmd_error(CMD_PARSE_ERROR, errhndl, "Parsing identifier \"%s\" failed: %s.",
              identifier, STRERROR(err));
    free(identifier);
    return CMD_PARSE_ERROR;
  }

  (*ret_putval) = (cmd_putval_t){
      .raw_identifier = identifier,
      .family = fam,
  };

  /* All the remaining fields are part of the option list. */
  cmd_status_t result = CMD_OK;
  metric_t m = {0};
  for (size_t i = 1; i < argc; ++i) {
    char *key = NULL;
    char *value = NULL;

    int status = cmd_parse_option(argv[i], &key, &value, errhndl);
    if (status == CMD_OK) {
      assert(key != NULL);
      assert(value != NULL);
      int option_err = set_option(&m, key, value, errhndl);
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

    status = parse_value(argv[i], &m.value, fam->type);
    if (status != 0) {
      cmd_error(CMD_PARSE_ERROR, errhndl, "Parsing the values string failed.");
      result = CMD_PARSE_ERROR;
      break;
    }

    status = metric_family_metric_append(fam, m);
    if (status != 0) {
      cmd_error(CMD_ERROR, errhndl, "metric_family_metric_append failed.");
      result = CMD_ERROR;
      break;
    }
  } /* while (*buffer != 0) */
  /* Done parsing the options. */

  if (result != CMD_OK) {
    cmd_destroy_putval(ret_putval);
  }

  return result;
} /* cmd_status_t cmd_parse_putval */

void cmd_destroy_putval(cmd_putval_t *putval) {
  if (putval == NULL)
    return;

  free(putval->raw_identifier);
  metric_family_free(putval->family);

  (*putval) = (cmd_putval_t){0};
} /* void cmd_destroy_putval */

cmd_status_t cmd_handle_putval(FILE *fh, char *buffer) {
  cmd_error_handler_t err = {cmd_error_fh, fh};

  DEBUG("utils_cmd_putval: cmd_handle_putval (fh = %p, buffer = %s);",
        (void *)fh, buffer);

  cmd_t cmd = {0};
  int status;
  if ((status = cmd_parse(buffer, &cmd, NULL, &err)) != CMD_OK)
    return status;
  if (cmd.type != CMD_PUTVAL) {
    cmd_error(CMD_UNKNOWN_COMMAND, &err, "Unexpected command: `%s'.",
              CMD_TO_STRING(cmd.type));
    cmd_destroy(&cmd);
    return CMD_UNKNOWN_COMMAND;
  }

  status = plugin_dispatch_metric_family(cmd.cmd.putval.family);
  if (status != 0) {
    cmd_error(CMD_ERROR, &err,
              "plugin_dispatch_metric_list failed with status %d.", status);
    cmd_destroy(&cmd);
    return CMD_ERROR;
  }

  if (fh != stdout) {
    cmd_putval_t *putval = &cmd.cmd.putval;
    size_t n = putval->family->metric.num;
    cmd_error(CMD_OK, &err, "Success: %zu %s been dispatched.", n,
              (n == 1) ? "metric has" : "metrics have");
  }

  cmd_destroy(&cmd);
  return CMD_OK;
} /* int cmd_handle_putval */

/* TODO(octo): Improve the readability of the command.
 *
 * Currently, this assumes lines similar to:
 *
 *   PUTVAL "metric_name{key=\"value\"}" interval=10.000 42
 *
 * Encoding the labels in this way generates a lot of escaped quotes, which is
 * not ideal. An alternative representation would be:
 *
 *   PUTVAL metric_name label:key="value" interval=10.000 42
 */
int cmd_create_putval(char *ret, size_t ret_len, /* {{{ */
                      metric_t const *m) {
  if ((ret == NULL) || (ret_len == 0) || (m == NULL)) {
    return EINVAL;
  }

  strbuf_t id_buf = STRBUF_CREATE;
  int status = metric_identity(&id_buf, m);
  if (status != 0) {
    return status;
  }

  strbuf_t buf = STRBUF_CREATE_FIXED(ret, ret_len);
  strbuf_print(&buf, "PUTVAL \"");
  strbuf_print_escaped(&buf, id_buf.ptr, "\\\"\n\r\t", '\\');
  strbuf_printf(&buf, "\" interval=%.3f", CDTIME_T_TO_DOUBLE(m->interval));
  /* TODO(octo): print option to set the value type. */
  return value_marshal_text(&buf, m->value, m->family->type);
} /* }}} int cmd_create_putval */
