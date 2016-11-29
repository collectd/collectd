/**
 * collectd - src/utils_cmd_flush.c
 * Copyright (C) 2008, 2016 Sebastian Harl
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
 *   Sebastian "tokkee" Harl <sh at tokkee.org>
 *   Florian "octo" Forster <octo at collectd.org>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"
#include "utils_cmd_flush.h"
#include "utils_parse_option.h"

cmd_status_t cmd_parse_flush(size_t argc, char **argv, cmd_flush_t *ret_flush,
                             const cmd_options_t *opts,
                             cmd_error_handler_t *err) {

  if ((ret_flush == NULL) || (opts == NULL)) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, err, "Invalid arguments to cmd_parse_flush.");
    return (CMD_ERROR);
  }

  for (size_t i = 0; i < argc; i++) {
    char *opt_key;
    char *opt_value;
    int status;

    opt_key = NULL;
    opt_value = NULL;
    status = cmd_parse_option(argv[i], &opt_key, &opt_value, err);
    if (status != 0) {
      if (status == CMD_NO_OPTION)
        cmd_error(CMD_PARSE_ERROR, err, "Invalid option string `%s'.", argv[i]);
      cmd_destroy_flush(ret_flush);
      return (CMD_PARSE_ERROR);
    }

    if (strcasecmp("plugin", opt_key) == 0) {
      strarray_add(&ret_flush->plugins, &ret_flush->plugins_num, opt_value);
    } else if (strcasecmp("identifier", opt_key) == 0) {
      identifier_t *id =
          realloc(ret_flush->identifiers,
                  (ret_flush->identifiers_num + 1) * sizeof(*id));
      if (id == NULL) {
        cmd_error(CMD_ERROR, err, "realloc failed.");
        cmd_destroy_flush(ret_flush);
        return (CMD_ERROR);
      }

      ret_flush->identifiers = id;
      id = ret_flush->identifiers + ret_flush->identifiers_num;
      ret_flush->identifiers_num++;
      if (parse_identifier(opt_value, &id->host, &id->plugin,
                           &id->plugin_instance, &id->type, &id->type_instance,
                           opts->identifier_default_host) != 0) {
        cmd_error(CMD_PARSE_ERROR, err, "Invalid identifier `%s'.", opt_value);
        cmd_destroy_flush(ret_flush);
        return (CMD_PARSE_ERROR);
      }
    } else if (strcasecmp("timeout", opt_key) == 0) {
      char *endptr;

      errno = 0;
      endptr = NULL;
      ret_flush->timeout = strtod(opt_value, &endptr);

      if ((endptr == opt_value) || (errno != 0) ||
          (!isfinite(ret_flush->timeout))) {
        cmd_error(CMD_PARSE_ERROR, err,
                  "Invalid value for option `timeout': %s", opt_value);
        cmd_destroy_flush(ret_flush);
        return (CMD_PARSE_ERROR);
      } else if (ret_flush->timeout < 0.0) {
        ret_flush->timeout = 0.0;
      }
    } else {
      cmd_error(CMD_PARSE_ERROR, err, "Cannot parse option `%s'.", opt_key);
      cmd_destroy_flush(ret_flush);
      return (CMD_PARSE_ERROR);
    }
  }

  return (CMD_OK);
} /* cmd_status_t cmd_parse_flush */

cmd_status_t cmd_handle_flush(FILE *fh, char *buffer) {
  cmd_error_handler_t err = {cmd_error_fh, fh};
  cmd_t cmd;

  int success = 0;
  int error = 0;
  int status;

  if ((fh == NULL) || (buffer == NULL))
    return (-1);

  DEBUG("utils_cmd_flush: cmd_handle_flush (fh = %p, buffer = %s);", (void *)fh,
        buffer);

  if ((status = cmd_parse(buffer, &cmd, NULL, &err)) != CMD_OK)
    return (status);
  if (cmd.type != CMD_FLUSH) {
    cmd_error(CMD_UNKNOWN_COMMAND, &err, "Unexpected command: `%s'.",
              CMD_TO_STRING(cmd.type));
    cmd_destroy(&cmd);
    return (CMD_UNKNOWN_COMMAND);
  }

  for (size_t i = 0; (i == 0) || (i < cmd.cmd.flush.plugins_num); i++) {
    char *plugin = NULL;

    if (cmd.cmd.flush.plugins_num != 0)
      plugin = cmd.cmd.flush.plugins[i];

    for (size_t j = 0; (j == 0) || (j < cmd.cmd.flush.identifiers_num); j++) {
      char *identifier = NULL;
      char buffer[1024];
      int status;

      if (cmd.cmd.flush.identifiers_num != 0) {
        identifier_t *id = cmd.cmd.flush.identifiers + j;
        if (format_name(buffer, sizeof(buffer), id->host, id->plugin,
                        id->plugin_instance, id->type,
                        id->type_instance) != 0) {
          error++;
          continue;
        }
        identifier = buffer;
      }

      status = plugin_flush(plugin, DOUBLE_TO_CDTIME_T(cmd.cmd.flush.timeout),
                            identifier);
      if (status == 0)
        success++;
      else
        error++;
    }
  }

  cmd_error(CMD_OK, &err, "Done: %i successful, %i errors", success, error);

  cmd_destroy(&cmd);
  return (0);
#undef PRINT_TO_SOCK
} /* cmd_status_t cmd_handle_flush */

void cmd_destroy_flush(cmd_flush_t *flush) {
  if (flush == NULL)
    return;

  strarray_free(flush->plugins, flush->plugins_num);
  flush->plugins = NULL;
  flush->plugins_num = 0;

  sfree(flush->identifiers);
  flush->identifiers_num = 0;
} /* void cmd_destroy_flush */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */
