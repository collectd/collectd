/**
 * collectd - src/liboconfig/scanner.l
 * Copyright (C) 2007  Florian Forster
 * Copyright (C) 2008  Sebastian Harl
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
 *   Florian Forster <octo at collectd.org>
 *   Sebastian Harl <sh at tokkee.org>
 */

%{
#include <stdlib.h>
#include "oconfig.h"
#include "aux_types.h"
#include "parser.h"

/* multiline string buffer */
static char *ml_buffer = NULL;
static int   ml_pos    = 0;
static int   ml_len    = 0;

#define ml_free (ml_len - ml_pos)

static void ml_append (char *);

#ifdef yyterminate
# undef yyterminate
#endif
#define yyterminate() \
	do { free (ml_buffer); ml_buffer = NULL; ml_pos = 0; ml_len = 0; \
		return YY_NULL; } while (0)
%}
%option yylineno
%option noyywrap
%x ML
WHITE_SPACE [\ \t\b]
NON_WHITE_SPACE [^\ \t\b]
EOL (\r\n|\n)
QUOTED_STRING ([^\\"]+|\\.)*
UNQUOTED_STRING [0-9A-Za-z_]+
HEX_NUMBER 0[xX][0-9a-fA-F]+
OCT_NUMBER 0[0-7]+
DEC_NUMBER [\+\-]?[0-9]+
FLOAT_NUMBER [\+\-]?[0-9]*\.[0-9]+([eE][\+\-][0-9]+)?
NUMBER ({FLOAT_NUMBER}|{HEX_NUMBER}|{OCT_NUMBER}|{DEC_NUMBER})
BOOL_TRUE (true|yes|on)
BOOL_FALSE (false|no|off)
COMMENT #.*
PORT (6(5(5(3[0-5]|[0-2][0-9])|[0-4][0-9][0-9])|[0-4][0-9][0-9][0-9])|[1-5][0-9][0-9][0-9][0-9]|[1-9][0-9]?[0-9]?[0-9]?)

IP_BYTE (2(5[0-5]|[0-4][0-9])|1[0-9][0-9]|[1-9]?[0-9])
IPV4_ADDR {IP_BYTE}\.{IP_BYTE}\.{IP_BYTE}\.{IP_BYTE}(:{PORT})?

/* IPv6 address according to http://www.ietf.org/rfc/rfc2373.txt
 * This supports embedded IPv4 addresses as well but does not strictly check
 * for the right prefix (::0:<v4> or ::FFFF:<v4>) because there are too many
 * ways to correctly represent the zero bytes. It's up to the user to check
 * for valid addresses. */
HEX16 ([0-9A-Fa-f]{1,4})
V6_PART ({HEX16}:{HEX16}|{IPV4_ADDR})
IPV6_BASE ({HEX16}:){6}{V6_PART}|::({HEX16}:){5}{V6_PART}|({HEX16})?::({HEX16}:){4}{V6_PART}|(({HEX16}:){0,1}{HEX16})?::({HEX16}:){3}{V6_PART}|(({HEX16}:){0,2}{HEX16})?::({HEX16}:){2}{V6_PART}|(({HEX16}:){0,3}{HEX16})?::{HEX16}:{V6_PART}|(({HEX16}:){0,4}{HEX16})?::{V6_PART}|(({HEX16}:){0,5}{HEX16})?::{HEX16}|(({HEX16}:){0,6}{HEX16})?::
IPV6_ADDR ({IPV6_BASE})|(\[{IPV6_BASE}\](:{PORT})?)

%%
{WHITE_SPACE}		|
{COMMENT}		{/* ignore */}

\\{EOL}			{/* continue line */}

{EOL}			{return (EOL);}
"/"			{return (SLASH);}
"<"			{return (OPENBRAC);}
">"			{return (CLOSEBRAC);}
{BOOL_TRUE}		{yylval.boolean = 1; return (BTRUE);}
{BOOL_FALSE}		{yylval.boolean = 0; return (BFALSE);}

{IPV4_ADDR}		{yylval.string = yytext; return (UNQUOTED_STRING);}
{IPV6_ADDR}		{yylval.string = yytext; return (UNQUOTED_STRING);}

{NUMBER}		{yylval.number = strtod (yytext, NULL); return (NUMBER);}

\"{QUOTED_STRING}\"	{yylval.string = yytext; return (QUOTED_STRING);}
{UNQUOTED_STRING}	{yylval.string = yytext; return (UNQUOTED_STRING);}

\"{QUOTED_STRING}\\{EOL} {
	int len = strlen (yytext);

	ml_pos = 0;

	/* remove "\\<EOL>" */
	if ('\r' == yytext[len - 2])
		len -= 3;
	else
		len -= 2;
	yytext[len] = '\0';

	ml_append (yytext);
	BEGIN (ML);
}
<ML>^{WHITE_SPACE}+ {/* remove leading white-space */}
<ML>{NON_WHITE_SPACE}{QUOTED_STRING}\\{EOL} {
	int len = strlen (yytext);

	/* remove "\\<EOL>" */
	if ('\r' == yytext[len - 2])
		len -= 3;
	else
		len -= 2;
	yytext[len] = '\0';

	ml_append(yytext);
}
<ML>{NON_WHITE_SPACE}{QUOTED_STRING}\" {
	ml_append(yytext);
	yylval.string = ml_buffer;

	BEGIN (INITIAL);
	return (QUOTED_STRING);
}
%%
static void ml_append (char *string)
{
	int len = strlen (string);
	int s;

	if (ml_free <= len) {
		ml_len += len - ml_free + 1;
		ml_buffer = (char *)realloc (ml_buffer, ml_len);
		if (NULL == ml_buffer)
			YY_FATAL_ERROR ("out of dynamic memory in ml_append");
	}

	s = snprintf (ml_buffer + ml_pos, ml_free, "%s", string);
	if ((0 > s) || (ml_free <= s))
		YY_FATAL_ERROR ("failed to write to multiline buffer");

	ml_pos += s;
	return;
} /* ml_append */

