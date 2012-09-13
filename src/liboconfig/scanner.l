/**
 * oconfig - src/scanner.l
 * Copyright (C) 2007  Florian octo Forster <octo at verplant.org>
 * Copyright (C) 2008  Sebastian tokkee Harl <sh at tokkee.org>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
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

