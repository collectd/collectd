#ifndef UTILS_CMD_EVALSTATE_H
#define UTILS_CMD_EVALSTATE_H 1

#include "utils/cmds/cmds.h"

#include <stdio.h>

cmd_status_t cmd_parse_evalstate(size_t argc, char **argv,
                                 cmd_evalstate_t *ret_evalstate,
                                 const cmd_options_t *opts,
                                 cmd_error_handler_t *err);

cmd_status_t cmd_handle_evalstate(FILE *fh, char *buffer);

void cmd_destroy_evalstate(cmd_evalstate_t *evalstate);

#endif /* UTILS_CMD_EVALSTATE_H */
