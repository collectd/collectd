/**
 * collectd - src/utils_cmd_listval.h
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

#ifndef UTILS_CMD_LISTVAL_H
#define UTILS_CMD_LISTVAL_H 1

#include <stdio.h>

#include "utils_cmds.h"

cmd_status_t cmd_parse_listval (size_t argc, char **argv,
    cmd_listval_t *ret_listval, const cmd_options_t *opts,
    cmd_error_handler_t *err);

cmd_status_t cmd_handle_listval (FILE *fh, char *buffer);

void cmd_destroy_listval (cmd_listval_t *listval);

#endif /* UTILS_CMD_LISTVAL_H */

/* vim: set sw=2 sts=2 ts=8 : */
