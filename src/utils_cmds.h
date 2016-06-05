/**
 * collectd - src/utils_cmds.h
 * Copyright (C) 2016 Sebastian 'tokkee' Harl
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
 *   Sebastian 'tokkee' Harl <sh at tokkee.org>
 **/

#ifndef UTILS_CMDS_H
#define UTILS_CMDS_H 1

#include "plugin.h"

#include <stdarg.h>

typedef enum {
	CMD_UNKNOWN = 0,
	CMD_PUTVAL  = 1,
} cmd_type_t;
#define CMD_TO_STRING(type) \
	((type) == CMD_PUTVAL) ? "PUTVAL" \
		: "UNKNOWN"

typedef struct {
	/* The raw identifier as provided by the user. */
	char *identifier;

	/* An array of the fully parsed identifier and all value lists, and their
	 * options as provided by the user. */
	value_list_t *vl;
	size_t vl_num;
} cmd_putval_t;

/*
 * NAME
 *   cmd_t
 *
 * DESCRIPTION
 *   The representation of a fully parsed command.
 */
typedef struct {
	cmd_type_t type;
	union {
		cmd_putval_t putval;
	} cmd;
} cmd_t;

/*
 * NAME
 *   cmd_status_t
 *
 * DESCRIPTION
 *   Status codes describing the parse result.
 */
typedef enum {
	CMD_OK              =  0,
	CMD_ERROR           = -1,
	CMD_PARSE_ERROR     = -2,
	CMD_UNKNOWN_COMMAND = -3,
} cmd_status_t;

/*
 * NAME
 *   cmd_error_handler_t
 *
 * DESCRIPTION
 *   An error handler describes a callback to be invoked when the parser
 *   encounters an error. The user data pointer will be passed to the callback
 *   as the first argument.
 */
typedef struct {
	void (*cb) (void *, cmd_status_t, const char *, va_list);
	void *ud;
} cmd_error_handler_t;

/*
 * NAME:
 *   cmd_error
 *
 * DESCRIPTION
 *   Reports an error via the specified error handler (if set).
 */
void cmd_error (cmd_status_t status, cmd_error_handler_t *err,
		const char *format, ...);

/*
 * NAME
 *   cmd_parse
 *
 * DESCRIPTION
 *   Parse a command string and populate a command object.
 *
 * PARAMETERS
 *   `buffer'  The command string to be parsed.
 *   `ret_cmd' The parse result will be stored at this location.
 *   `err'     An optional error handler to invoke on error.
 *
 * RETURN VALUE
 *   CMD_OK on success or the respective error code otherwise.
 */
cmd_status_t cmd_parse (char *buffer,
		cmd_t *ret_cmd, cmd_error_handler_t *err);

void cmd_destroy (cmd_t *cmd);

/*
 * NAME
 *   cmd_error_fh
 *
 * DESCRIPTION
 *   An error callback writing the message to an open file handle using the
 *   format expected by the unixsock or exec plugins.
 *
 * PARAMETERS
 *   `ud'     Error handler user-data pointer. This must be an open
 *            file-handle (FILE *).
 *   `status' The error status code.
 *   `format' Printf-style format string.
 *   `ap'     Variable argument list providing the arguments for the format
 *            string.
 */
void cmd_error_fh (void *ud, cmd_status_t status,
		const char *format, va_list ap);

#endif /* UTILS_CMDS_H */
