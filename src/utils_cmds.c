/**
 * collectd - src/utils_cmds.c
 * Copyright (C) 2008       Florian Forster
 * Copyright (C) 2016       Sebastian 'tokkee' Harl
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
 *   Sebastian 'tokkee' Harl <sh at tokkee.org>
 **/

#include "daemon/common.h"
#include "utils_cmd_flush.h"
#include "utils_cmd_getval.h"
#include "utils_cmd_listval.h"
#include "utils_cmd_putval.h"
#include "utils_cmds.h"
#include "utils_parse_option.h"

#include <stdbool.h>
#include <string.h>

static cmd_options_t default_options = {
    /* identifier_default_host = */ NULL,
};

/*
 * private helper functions
 */

static cmd_status_t cmd_split(char *buffer, size_t *ret_len, char ***ret_fields,
                              cmd_error_handler_t *err) {
  char *field;
  bool in_field, in_quotes;

  size_t estimate, len;
  char **fields;

  estimate = 0;
  in_field = false;
  for (char *string = buffer; *string != '\0'; ++string) {
    /* Make a quick worst-case estimate of the number of fields by
     * counting spaces and ignoring quotation marks. */
    if (!isspace((int)*string)) {
      if (!in_field) {
        estimate++;
        in_field = true;
      }
    } else {
      in_field = false;
    }
  }

  /* fields will be NULL-terminated */
  fields = malloc((estimate + 1) * sizeof(*fields));
  if (fields == NULL) {
    cmd_error(CMD_ERROR, err, "malloc failed.");
    return CMD_ERROR;
  }

#define END_FIELD()                                                            \
  do {                                                                         \
    *field = '\0';                                                             \
    field = NULL;                                                              \
    in_field = false;                                                          \
  } while (0)
#define NEW_FIELD()                                                            \
  do {                                                                         \
    field = string;                                                            \
    in_field = true;                                                           \
    assert(len < estimate);                                                    \
    fields[len] = field;                                                       \
    field++;                                                                   \
    len++;                                                                     \
  } while (0)

  len = 0;
  field = NULL;
  in_field = false;
  in_quotes = false;
  for (char *string = buffer; *string != '\0'; string++) {
    if (isspace((int)string[0])) {
      if (!in_quotes) {
        if (in_field)
          END_FIELD();

        /* skip space */
        continue;
      }
    } else if (string[0] == '"') {
      /* Note: Two consecutive quoted fields not separated by space are
       * treated as different fields. This is the collectd 5.x behavior
       * around splitting fields. */

      if (in_quotes) {
        /* end of quoted field */
        if (!in_field) /* empty quoted string */
          NEW_FIELD();
        END_FIELD();
        in_quotes = false;
        continue;
      }

      in_quotes = true;
      /* if (! in_field): add new field on next iteration
       * else: quoted string following an unquoted string (one field)
       * in either case: skip quotation mark */
      continue;
    } else if ((string[0] == '\\') && in_quotes) {
      /* Outside of quotes, a backslash is a regular character (mostly
       * for backward compatibility). */

      if (string[1] == '\0') {
        free(fields);
        cmd_error(CMD_PARSE_ERROR, err, "Backslash at end of string.");
        return CMD_PARSE_ERROR;
      }

      /* un-escape the next character; skip backslash */
      string++;
    }

    if (!in_field)
      NEW_FIELD();
    else {
      *field = string[0];
      field++;
    }
  }

  if (in_quotes) {
    free(fields);
    cmd_error(CMD_PARSE_ERROR, err, "Unterminated quoted string.");
    return CMD_PARSE_ERROR;
  }

#undef NEW_FIELD
#undef END_FIELD

  fields[len] = NULL;
  if (ret_len != NULL)
    *ret_len = len;
  if (ret_fields != NULL)
    *ret_fields = fields;
  else
    free(fields);
  return CMD_OK;
} /* int cmd_split */

/*
 * public API
 */

void cmd_error(cmd_status_t status, cmd_error_handler_t *err,
               const char *format, ...) {
  va_list ap;

  if ((err == NULL) || (err->cb == NULL))
    return;

  va_start(ap, format);
  err->cb(err->ud, status, format, ap);
  va_end(ap);
} /* void cmd_error */

cmd_status_t cmd_parsev(size_t argc, char **argv, cmd_t *ret_cmd,
                        const cmd_options_t *opts, cmd_error_handler_t *err) {
  char *command = NULL;
  cmd_status_t status;

  if ((argc < 1) || (argv == NULL) || (ret_cmd == NULL)) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, err, "Missing command.");
    return CMD_ERROR;
  }

  if (opts == NULL)
    opts = &default_options;

  memset(ret_cmd, 0, sizeof(*ret_cmd));
  command = argv[0];
  if (strcasecmp("FLUSH", command) == 0) {
    ret_cmd->type = CMD_FLUSH;
    status =
        cmd_parse_flush(argc - 1, argv + 1, &ret_cmd->cmd.flush, opts, err);
  } else if (strcasecmp("GETVAL", command) == 0) {
    ret_cmd->type = CMD_GETVAL;
    status =
        cmd_parse_getval(argc - 1, argv + 1, &ret_cmd->cmd.getval, opts, err);
  } else if (strcasecmp("LISTVAL", command) == 0) {
    ret_cmd->type = CMD_LISTVAL;
    status =
        cmd_parse_listval(argc - 1, argv + 1, &ret_cmd->cmd.listval, opts, err);
  } else if (strcasecmp("PUTVAL", command) == 0) {
    ret_cmd->type = CMD_PUTVAL;
    status =
        cmd_parse_putval(argc - 1, argv + 1, &ret_cmd->cmd.putval, opts, err);
  } else {
    ret_cmd->type = CMD_UNKNOWN;
    cmd_error(CMD_UNKNOWN_COMMAND, err, "Unknown command `%s'.", command);
    return CMD_UNKNOWN_COMMAND;
  }

  if (status != CMD_OK)
    ret_cmd->type = CMD_UNKNOWN;
  return status;
} /* cmd_status_t cmd_parsev */

cmd_status_t cmd_parse(char *buffer, cmd_t *ret_cmd, const cmd_options_t *opts,
                       cmd_error_handler_t *err) {
  char **fields = NULL;
  size_t fields_num = 0;
  cmd_status_t status;

  if ((status = cmd_split(buffer, &fields_num, &fields, err)) != CMD_OK)
    return status;

  status = cmd_parsev(fields_num, fields, ret_cmd, opts, err);
  free(fields);
  return status;
} /* cmd_status_t cmd_parse */

void cmd_destroy(cmd_t *cmd) {
  if (cmd == NULL)
    return;

  switch (cmd->type) {
  case CMD_UNKNOWN:
    /* nothing to do */
    break;
  case CMD_FLUSH:
    cmd_destroy_flush(&cmd->cmd.flush);
    break;
  case CMD_GETVAL:
    cmd_destroy_getval(&cmd->cmd.getval);
    break;
  case CMD_LISTVAL:
    cmd_destroy_listval(&cmd->cmd.listval);
    break;
  case CMD_PUTVAL:
    cmd_destroy_putval(&cmd->cmd.putval);
    break;
  }
} /* void cmd_destroy */

cmd_status_t cmd_parse_option(char *field, char **ret_key, char **ret_value,
                              cmd_error_handler_t *err) {
  char *key, *value;

  if (field == NULL) {
    errno = EINVAL;
    cmd_error(CMD_ERROR, err, "Invalid argument to cmd_parse_option.");
    return CMD_ERROR;
  }
  key = value = field;

  /* Look for the equal sign. */
  while (isalnum((int)value[0]) || (value[0] == '_') || (value[0] == ':'))
    value++;
  if ((value[0] != '=') || (value == key)) {
    /* Whether this is a fatal error is up to the caller. */
    return CMD_NO_OPTION;
  }
  *value = '\0';
  value++;

  if (ret_key != NULL)
    *ret_key = key;
  if (ret_value != NULL)
    *ret_value = value;

  return CMD_OK;
} /* cmd_status_t cmd_parse_option */

void cmd_error_fh(void *ud, cmd_status_t status, const char *format,
                  va_list ap) {
  FILE *fh = ud;
  int code = -1;
  char buf[1024];

  if (status == CMD_OK)
    code = 0;

  vsnprintf(buf, sizeof(buf), format, ap);
  buf[sizeof(buf) - 1] = '\0';
  if (fprintf(fh, "%i %s\n", code, buf) < 0) {
    WARNING("utils_cmds: failed to write to file-handle #%i: %s", fileno(fh),
            STRERRNO);
    return;
  }

  fflush(fh);
} /* void cmd_error_fh */
