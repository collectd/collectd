/**
 * collectd - src/tests/utils_cmds_test.c
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

#include "common.h"
#include "testing.h"
#include "utils_cmds.h"

static void error_cb (void *ud, cmd_status_t status,
		const char *format, va_list ap)
{
	if (status == CMD_OK)
		return;

	printf ("ERROR[%d]: ", status);
	vprintf (format, ap);
	printf ("\n");
	fflush (stdout);
} /* void error_cb */

struct {
	char *input;
	cmd_status_t expected_status;
	cmd_type_t expected_type;
} parse_data[] = {
	/* Valid FLUSH commands. */
	{
		"FLUSH",
		CMD_OK,
		CMD_FLUSH,
	},
	{
		"FLUSH identifier=myhost/magic/MAGIC",
		CMD_OK,
		CMD_FLUSH,
	},
	{
		"FLUSH timeout=123 plugin=\"A\"",
		CMD_OK,
		CMD_FLUSH,
	},
	/* Invalid FLUSH commands. */
	{
		/* Missing 'identifier' key. */
		"FLUSH myhost/magic/MAGIC",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		/* Invalid timeout. */
		"FLUSH timeout=A",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		/* Invalid identifier. */
		"FLUSH identifier=invalid",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		/* Invalid option. */
		"FLUSH invalid=option",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},

	/* Valid GETVAL commands. */
	{
		"GETVAL myhost/magic/MAGIC",
		CMD_OK,
		CMD_GETVAL,
	},

	/* Invalid GETVAL commands. */
	{
		"GETVAL",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		"GETVAL invalid",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},

	/* Valid LISTVAL commands. */
	{
		"LISTVAL",
		CMD_OK,
		CMD_LISTVAL,
	},

	/* Invalid LISTVAL commands. */
	{
		"LISTVAL invalid",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},

	/* Valid PUTVAL commands. */
	{
		"PUTVAL myhost/magic/MAGIC N:42",
		CMD_OK,
		CMD_PUTVAL,
	},
	{
		"PUTVAL myhost/magic/MAGIC 1234:42",
		CMD_OK,
		CMD_PUTVAL,
	},
	{
		"PUTVAL myhost/magic/MAGIC 1234:42 2345:23",
		CMD_OK,
		CMD_PUTVAL,
	},
	{
		"PUTVAL myhost/magic/MAGIC interval=2 1234:42",
		CMD_OK,
		CMD_PUTVAL,
	},
	{
		"PUTVAL myhost/magic/MAGIC interval=2 1234:42 interval=5 2345:23",
		CMD_OK,
		CMD_PUTVAL,
	},

	/* Invalid PUTVAL commands. */
	{
		"PUTVAL",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		"PUTVAL invalid N:42",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		"PUTVAL myhost/magic/MAGIC A:42",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		"PUTVAL myhost/magic/MAGIC 1234:A",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		"PUTVAL myhost/magic/MAGIC",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		"PUTVAL 1234:A",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	{
		"PUTVAL myhost/magic/UNKNOWN 1234:42",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	/*
	 * As of collectd 5.x, PUTVAL accepts invalid options.
	{
		"PUTVAL myhost/magic/MAGIC invalid=2 1234:42",
		CMD_PARSE_ERROR,
		CMD_UNKNOWN,
	},
	*/

	/* Invalid commands. */
	{
		"INVALID",
		CMD_UNKNOWN_COMMAND,
		CMD_UNKNOWN,
	},
	{
		"INVALID interval=2",
		CMD_UNKNOWN_COMMAND,
		CMD_UNKNOWN,
	},
};

DEF_TEST(parse)
{
	cmd_error_handler_t err = { error_cb, NULL };
	int test_result = 0;
	size_t i;

	for (i = 0; i < STATIC_ARRAY_SIZE (parse_data); i++) {
		char *input = strdup (parse_data[i].input);

		char description[1024];
		cmd_status_t status;
		cmd_t cmd;

		_Bool result;

		memset (&cmd, 0, sizeof (cmd));

		status = cmd_parse (input, &cmd, &err);
		snprintf (description, sizeof (description),
				"cmd_parse (\"%s\") = %d (type=%d [%s]); want %d (type=%d [%s])",
				parse_data[i].input, status,
				cmd.type, CMD_TO_STRING (cmd.type),
				parse_data[i].expected_status,
				parse_data[i].expected_type,
				CMD_TO_STRING (parse_data[i].expected_type));
		result = (status == parse_data[i].expected_status)
				&& (cmd.type == parse_data[i].expected_type);
		LOG (result, description);

		/* Run all tests before failing. */
		if (! result)
			test_result = -1;

		cmd_destroy (&cmd);
		free (input);
	}

	return (test_result);
}

int main (int argc, char **argv)
{
	RUN_TEST(parse);
	END_TEST;
}
