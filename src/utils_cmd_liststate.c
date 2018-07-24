/**
 * collectd - src/utils_cmd_liststate.c
 * Copyright (C) 2018       Simone Brundu
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
 *   Simone Brundu <simone.brundu at gmail.com>
 **/

#include "collectd.h"

#include "common.h"
#include "plugin.h"

#include "utils_cache.h"
#include "utils_cmd_liststate.h"
#include "utils_parse_option.h"

cmd_status_t cmd_parse_liststate(size_t argc, char **argv,
                                 cmd_liststate_t *ret_liststate,
                                 const cmd_options_t *opts
                                 __attribute__((unused)),
                                 cmd_error_handler_t *err) {
  if (argc > 1) {
    cmd_error(CMD_PARSE_ERROR, err, "Garbage after end of command: `%s'.",
              argv[0]);
    return CMD_PARSE_ERROR;
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
      cmd_destroy_liststate(ret_liststate);
      return CMD_PARSE_ERROR;
    }

    if (strcasecmp("state", opt_key) == 0) {
      ret_liststate->state = opt_value;
    } else {
      cmd_error(CMD_PARSE_ERROR, err, "Cannot parse option `%s'.", opt_key);
      cmd_destroy_liststate(ret_liststate);
      return CMD_PARSE_ERROR;
    }
  }

  return CMD_OK;
} /* cmd_status_t cmd_parse_liststate */

#define free_everything_and_return(status)                                     \
  do {                                                                         \
    for (size_t j = 0; j < number; j++) {                                      \
      sfree(names[j]);                                                         \
      names[j] = NULL;                                                         \
    }                                                                          \
    sfree(names);                                                              \
    sfree(times);                                                              \
    sfree(states);                                                             \
    return status;                                                             \
  } while (0)

#define print_to_socket(fh, ...)                                               \
  do {                                                                         \
    if (fprintf(fh, __VA_ARGS__) < 0) {                                        \
      WARNING("handle_liststate: failed to write to socket #%i: %s",           \
              fileno(fh), STRERRNO);                                           \
      free_everything_and_return(CMD_ERROR);                                   \
    }                                                                          \
    fflush(fh);                                                                \
  } while (0)

cmd_status_t cmd_handle_liststate(FILE *fh, char *buffer) {
  cmd_error_handler_t err = {cmd_error_fh, fh};
  cmd_status_t status;
  cmd_t cmd;

  char **names = NULL;
  cdtime_t *times = NULL;
  int *states = NULL;
  size_t number = 0;

  DEBUG("utils_cmd_liststate: handle_liststate (fh = %p, buffer = %s);",
        (void *)fh, buffer);

  if ((status = cmd_parse(buffer, &cmd, NULL, &err)) != CMD_OK)
    return status;
  if (cmd.type != CMD_LISTSTATE) {
    cmd_error(CMD_UNKNOWN_COMMAND, &err, "Unexpected command: `%s'.",
              CMD_TO_STRING(cmd.type));
    free_everything_and_return(CMD_UNKNOWN_COMMAND);
  }

  status = uc_get_names_states(&names, &times, &states, &number,
                               cmd.cmd.liststate.state);
  if (status != 0) {
    DEBUG("command liststate: uc_get_names_states failed with status %i",
          status);
    cmd_error(CMD_ERROR, &err, "uc_get_names_states failed.");
    free_everything_and_return(CMD_ERROR);
  }

  print_to_socket(fh, "%i Value%s found\n", (int)number,
                  (number == 1) ? "" : "s");
  for (size_t i = 0; i < number; i++)
    print_to_socket(fh, "%.3f %s %s\n", CDTIME_T_TO_DOUBLE(times[i]), names[i],
                    STATE_TO_STRING(states[i]));

  free_everything_and_return(CMD_OK);
} /* cmd_status_t cmd_handle_liststate */

void cmd_destroy_liststate(cmd_liststate_t *liststate) {
  if (liststate == NULL)
    return;

  liststate->state = NULL;
} /* void cmd_destroy_liststate */
