/**
 * collectd - src/utils_cmds.c
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
 *   Sebastian 'tokkee' Harl <sh at tokkee.org>
 **/

#include "utils_cmds.h"
#include "utils_cmd_putval.h"
#include "utils_parse_option.h"
#include "daemon/common.h"

#include <stdbool.h>
#include <string.h>

/*
 * public API
 */

void cmd_error (cmd_status_t status, cmd_error_handler_t *err,
		const char *format, ...)
{
	va_list ap;

	if ((err == NULL) || (err->cb == NULL))
		return;

	va_start (ap, format);
	err->cb (err->ud, status, format, ap);
	va_end (ap);
} /* void cmd_error */

cmd_status_t cmd_parse (char *buffer,
		cmd_t *ret_cmd, cmd_error_handler_t *err)
{
	char *command = NULL;
	int status;

	if ((buffer == NULL) || (ret_cmd == NULL))
	{
		errno = EINVAL;
		cmd_error (CMD_ERROR, err, "Invalid arguments to cmd_parse.");
		return CMD_ERROR;
	}

	if ((status = parse_string (&buffer, &command)) != 0)
	{
		cmd_error (CMD_PARSE_ERROR, err,
				"Failed to extract command from `%s'.", buffer);
		return (CMD_PARSE_ERROR);
	}
	assert (command != NULL);

	memset (ret_cmd, 0, sizeof (*ret_cmd));
	if (strcasecmp ("PUTVAL", command) == 0)
	{
		ret_cmd->type = CMD_PUTVAL;
		return cmd_parse_putval (buffer, &ret_cmd->cmd.putval, err);
	}
	else
	{
		ret_cmd->type = CMD_UNKNOWN;
		cmd_error (CMD_UNKNOWN_COMMAND, err,
				"Unknown command `%s'.", command);
		return (CMD_UNKNOWN_COMMAND);
	}

	return (CMD_OK);
} /* cmd_status_t cmd_parse */

void cmd_destroy (cmd_t *cmd)
{
	if (cmd == NULL)
		return;

	switch (cmd->type)
	{
		case CMD_UNKNOWN:
			/* nothing to do */
			break;
		case CMD_PUTVAL:
			cmd_destroy_putval (&cmd->cmd.putval);
			break;
	}
} /* void cmd_destroy */

void cmd_error_fh (void *ud, cmd_status_t status,
		const char *format, va_list ap)
{
	FILE *fh = ud;
	int code = -1;
	char buf[1024];

	if (status == CMD_OK)
		code = 0;

	vsnprintf (buf, sizeof(buf), format, ap);
	buf[sizeof (buf) - 1] = '\0';
	if (fprintf (fh, "%i %s\n", code, buf) < 0)
	{
		char errbuf[1024];
		WARNING ("utils_cmds: failed to write to file-handle #%i: %s",
				fileno (fh), sstrerror (errno, errbuf, sizeof (errbuf)));
		return;
	}

	fflush (fh);
} /* void cmd_error_fh */

/* vim: set sw=4 ts=4 tw=78 noexpandtab : */
