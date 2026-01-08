#ifndef UTILS_CMD_FLUSHSTATE_H
#define UTILS_CMD_FLUSHSTATE_H 1

#include "utils/cmds/cmds.h"

#include <stdio.h>

cmd_status_t cmd_parse_flushstate(size_t argc, char **argv,
                                  cmd_flushstate_t *ret_flushstate,
                                  const cmd_options_t *opts,
                                  cmd_error_handler_t *err);

cmd_status_t cmd_handle_flushstate(FILE *fh, char *buffer);

void cmd_destroy_flushstate(cmd_flushstate_t *flushstate);

#endif /* UTILS_CMD_FLUSHSTATE_H */
