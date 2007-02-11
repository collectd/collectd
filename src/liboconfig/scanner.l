/**
 * oconfig - src/scanner.l
 * Copyright (C) 2007  Florian octo Forster <octo at verplant.org>
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
%}
WHITE_SPACE [\ \t\b]
QUOTED_STRING \"([^\\"]+|\\.)*\"
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

\n			{return (EOL);}
"/"			{return (SLASH);}
"<"			{return (OPENBRAC);}
">"			{return (CLOSEBRAC);}
{BOOL_TRUE}		{yylval.boolean = 1; return (TRUE);}
{BOOL_FALSE}		{yylval.boolean = 0; return (FALSE);}

{IPV4_ADDR}		{yylval.string = yytext; return (UNQUOTED_STRING);}

{NUMBER}		{yylval.number = strtod (yytext, NULL); return (NUMBER);}

{QUOTED_STRING}		{yylval.string = yytext; return (QUOTED_STRING);}
{UNQUOTED_STRING}	{yylval.string = yytext; return (UNQUOTED_STRING);}
%%
