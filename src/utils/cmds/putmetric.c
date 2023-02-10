/**
 * collectd - src/utils_cmd_putmetric.c
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

#include "utils/cmds/putmetric.h"
#include "utils/common/common.h"

/*
 * private helper functions
 */

/* TODO(octo): add an option to set metric->value_type */
static int set_option(metric_t *m, char const *key, char const *value,
                      __attribute__((unused)) cmd_error_handler_t *err) {
  if ((m == NULL) || (key == NULL) || (value == NULL))
    return -1;

  printf("set_option(\"%s\", \"%s\")\n", key, value);

  if (strcasecmp("type", key) == 0) {
    if (strcasecmp("GAUGE", value) == 0) {
      m->family->type = METRIC_TYPE_GAUGE;
    } else if (strcasecmp("COUNTER", value) == 0) {
      m->family->type = METRIC_TYPE_COUNTER;
    } else if (strcasecmp("UNTYPED", value) == 0) {
      m->family->type = METRIC_TYPE_UNTYPED;
    } else {
      return CMD_ERROR;
    }
  } else if (strcasecmp("interval", key) == 0) {
    errno = 0;
    char *endptr = NULL;
    double d = strtod(value, &endptr);

    if ((errno != 0) || (endptr == NULL) || (*endptr != 0) || (d < 0)) {
      return CMD_ERROR;
    }
    m->interval = DOUBLE_TO_CDTIME_T(d);
  } else if (strcasecmp("time", key) == 0) {
    errno = 0;
    char *endptr = NULL;
    double d = strtod(value, &endptr);

    if ((errno != 0) || (endptr == NULL) || (*endptr != 0) || (d < 0)) {
      return CMD_ERROR;
    }
    m->time = DOUBLE_TO_CDTIME_T(d);
  } else if (strncasecmp("label:", key, 5) == 0) {
    char const *name = key + strlen("label:");
    return metric_label_set(m, name, value) ? CMD_ERROR : CMD_OK;
  } else {
    return CMD_ERROR;
  }
  return CMD_OK;
} /* int set_option */

/*
 * public API
 */

cmd_status_t cmd_parse_putmetric(size_t argc, char **argv,
                                 cmd_putmetric_t *ret_putmetric,
                                 __attribute__((unused))
                                 cmd_options_t const *opts,
                                 cmd_error_handler_t *errhndl) {
  if ((argc < 2) || (argv == NULL) || (ret_putmetric == NULL)) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, errhndl, "Invalid arguments to cmd_parse_putmetric.");
    return CMD_ERROR;
  }

  if (argc < 2) {
    cmd_error(CMD_PARSE_ERROR, errhndl,
              "Missing identifier and/or value-list.");
    return CMD_PARSE_ERROR;
  }

  metric_family_t *fam = calloc(1, sizeof(*fam));
  if (fam == NULL) {
    cmd_error(CMD_ERROR, errhndl, "calloc failed");
    return CMD_ERROR;
  }
  fam->type = METRIC_TYPE_UNTYPED;

  int status = metric_family_metric_append(fam, (metric_t){0});
  if (status != 0) {
    return CMD_ERROR;
  }
  metric_t *m = fam->metric.ptr;

  int next_pos = 0;
  cmd_status_t result = CMD_OK;
  for (size_t i = 0; i < argc; ++i) {
    char *key = NULL;
    char *value = NULL;

    int status = cmd_parse_option(argv[i], &key, &value, errhndl);
    if (status == CMD_OK) {
      assert(key != NULL);
      assert(value != NULL);

      result = set_option(m, key, value, errhndl);
      if (result != CMD_OK) {
        break;
      }
      continue;
    } else if (status == CMD_NO_OPTION) {
      /* Positional argument */
      if (next_pos == 0) {
        fam->name = strdup(argv[i]);
        if (fam->name == NULL) {
          cmd_error(CMD_ERROR, errhndl, "calloc failed");
          result = CMD_ERROR;
          break;
        }
        next_pos++;
        continue;
      } else if (next_pos == 1) {
        int status = parse_value(argv[i], &m->value, fam->type);
        if (status != 0) {
          cmd_error(CMD_ERROR, errhndl, "parse_value failed");
          result = CMD_ERROR;
          break;
        }
        next_pos++;
        continue;
      } else {
        /* error is handled after the loop */
        next_pos++;
        continue;
      }
    } else {
      /* parse_option failed, buffer has been modified.
       * => we need to abort */
      result = status;
      break;
    }
  }

  if ((result == CMD_OK) && (next_pos != 2)) {
    char errmsg[256];
    snprintf(errmsg, sizeof(errmsg),
             "Found %d positional argument(s), expected 2.", next_pos);
    cmd_error(CMD_PARSE_ERROR, errhndl, errmsg);
    result = CMD_ERROR;
  }

  if (result != CMD_OK) {
    metric_family_free(fam);
    return result;
  }

  *ret_putmetric = (cmd_putmetric_t){
      .family = fam,
  };
  return CMD_OK;
} /* cmd_status_t cmd_parse_putmetric */

void cmd_destroy_putmetric(cmd_putmetric_t *putmetric) {
  if (putmetric == NULL)
    return;

  metric_family_free(putmetric->family);

  (*putmetric) = (cmd_putmetric_t){0};
} /* void cmd_destroy_putmetric */

cmd_status_t cmd_handle_putmetric(FILE *fh, char *buffer) {
  cmd_error_handler_t err = {cmd_error_fh, fh};

  DEBUG("utils_cmd_putmetric: cmd_handle_putmetric (fh = %p, buffer = %s);",
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

  status = plugin_dispatch_metric_family(cmd.cmd.putmetric.family);
  if (status != 0) {
    cmd_error(CMD_ERROR, &err,
              "plugin_dispatch_metric_list failed with status %d.", status);
    cmd_destroy(&cmd);
    return CMD_ERROR;
  }

  if (fh != stdout) {
    cmd_putmetric_t *putmetric = &cmd.cmd.putmetric;
    size_t n = putmetric->family->metric.num;
    cmd_error(CMD_OK, &err, "Success: %zu %s been dispatched.", n,
              (n == 1) ? "metric has" : "metrics have");
  }

  cmd_destroy(&cmd);
  return CMD_OK;
} /* int cmd_handle_putmetric */

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
int cmd_format_putmetric(strbuf_t *buf, metric_t const *m) { /* {{{ */
  if ((buf == NULL) || (m == NULL)) {
    return EINVAL;
  }

  strbuf_print(buf, "PUTMETRIC ");
  strbuf_print(buf, m->family->name);
  switch (m->family->type) {
  case METRIC_TYPE_UNTYPED:
    /* no op */
    break;
  case METRIC_TYPE_COUNTER:
    strbuf_print(buf, " type=COUNTER");
    break;
  case METRIC_TYPE_GAUGE:
    strbuf_print(buf, " type=GAUGE");
    break;
  default:
    return EINVAL;
  }

  if (m->time != 0) {
    strbuf_printf(buf, " time=%.3f", CDTIME_T_TO_DOUBLE(m->time));
  }
  if (m->interval != 0) {
    strbuf_printf(buf, " interval=%.3f", CDTIME_T_TO_DOUBLE(m->interval));
  }

  for (size_t i = 0; i < m->label.num; i++) {
    label_pair_t *l = m->label.ptr + i;
    strbuf_printf(buf, " label:%s=\"", l->name);
    strbuf_print_escaped(buf, l->value, "\\\"\n\r\t", '\\');
    strbuf_print(buf, "\"");
  }

  strbuf_print(buf, " ");
  return value_marshal_text(buf, m->value, m->family->type);
} /* }}} int cmd_format_putmetric */
