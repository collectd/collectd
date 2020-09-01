/**
 * collectd - src/utils_cmd_getval.c
 * Copyright (C) 2008       Florian octo Forster
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

#include "plugin.h"
#include "utils/common/common.h"

#include "utils/cmds/getval.h"
#include "utils/cmds/parse_option.h"
#include "utils_cache.h"

cmd_status_t cmd_parse_getval(size_t argc, char **argv,
                              cmd_getval_t *ret_getval,
                              const cmd_options_t *opts,
                              cmd_error_handler_t *err) {
  if ((ret_getval == NULL) || (opts == NULL)) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, err, "Invalid arguments to cmd_parse_getval.");
    return CMD_ERROR;
  }

  if (argc != 1) {
    if (argc == 0)
      cmd_error(CMD_PARSE_ERROR, err, "Missing identifier.");
    else
      cmd_error(CMD_PARSE_ERROR, err, "Garbage after identifier: `%s'.",
                argv[1]);
    return CMD_PARSE_ERROR;
  }

  metric_t *m = parse_legacy_identifier(argv[0]);
  if (m == NULL) {
    DEBUG("cmd_parse_getval: Cannot parse identifier \"%s\": %s", argv[0],
          STRERRNO);
    return CMD_PARSE_ERROR;
  }

  *ret_getval = (cmd_getval_t){
      .raw_identifier = strdup(argv[0]),
      .metric = m,
  };
  return CMD_OK;
} /* cmd_status_t cmd_parse_getval */

#define print_to_socket(fh, ...)                                               \
  do {                                                                         \
    if (fprintf(fh, __VA_ARGS__) < 0) {                                        \
      WARNING("cmd_handle_getval: failed to write to socket #%i: %s",          \
              fileno(fh), STRERRNO);                                           \
      return -1;                                                               \
    }                                                                          \
    fflush(fh);                                                                \
  } while (0)

cmd_status_t cmd_handle_getval(FILE *fh, char *buffer) {
  cmd_error_handler_t err = {cmd_error_fh, fh};
  cmd_status_t status;
  cmd_t cmd;

  if ((fh == NULL) || (buffer == NULL))
    return -1;

  DEBUG("utils_cmd_getval: cmd_handle_getval (fh = %p, buffer = %s);",
        (void *)fh, buffer);

  if ((status = cmd_parse(buffer, &cmd, NULL, &err)) != CMD_OK)
    return status;
  if (cmd.type != CMD_GETVAL) {
    cmd_error(CMD_UNKNOWN_COMMAND, &err, "Unexpected command: `%s'.",
              CMD_TO_STRING(cmd.type));
    cmd_destroy(&cmd);
    return CMD_UNKNOWN_COMMAND;
  }

  gauge_t value;
  /* TODO(octo): raw_identifier may need to be upgraded to a metric_t style
   * identifier. */
  status = uc_get_rate(cmd.cmd.getval.metric, &value);
  if (status != 0) {
    cmd_error(CMD_ERROR, &err, "No such value.");
    cmd_destroy(&cmd);
    return CMD_ERROR;
  }

  print_to_socket(fh, "Value found\n");
  print_to_socket(fh, GAUGE_FORMAT "\n", value);

  cmd_destroy(&cmd);

  return CMD_OK;
} /* cmd_status_t cmd_handle_getval */

void cmd_destroy_getval(cmd_getval_t *getval) {
  if (getval == NULL)
    return;

  sfree(getval->raw_identifier);
  metric_family_free(getval->metric->family);
} /* void cmd_destroy_getval */
