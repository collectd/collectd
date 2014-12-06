/**
 * collectd - src/liboconfig/parser.y
 * Copyright (C) 2007,2008  Florian Forster
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
 */

%{
#include <stdlib.h>
#include <string.h>
#include "oconfig.h"
#include "aux_types.h"

static char *unquote (const char *orig);
static int yyerror (const char *s);

/* Lexer variables */
extern int yylineno;
extern char *yytext;

extern oconfig_item_t *ci_root;
extern char           *c_file;
%}

%start entire_file

%union {
	double  number;
	int     boolean;
	char   *string;
	oconfig_value_t  cv;
	oconfig_item_t   ci;
	argument_list_t  al;
	statement_list_t sl;
}

%token <number> NUMBER
%token <boolean> BTRUE BFALSE
%token <string> QUOTED_STRING UNQUOTED_STRING
%token SLASH OPENBRAC CLOSEBRAC EOL

%type <string> string
%type <string> identifier
/* arguments */
%type <cv> argument
%type <al> argument_list
/* blocks */
%type <ci> block_begin
%type <ci> block
%type <string> block_end
/* statements */
%type <ci> option
%type <ci> statement
%type <sl> statement_list
%type <ci> entire_file

/* pass an verbose, specific error message to yyerror() */
%error-verbose

%%
string:
	QUOTED_STRING		{$$ = unquote ($1);}
	| UNQUOTED_STRING	{$$ = strdup ($1);}
	;

argument:
	NUMBER			{$$.value.number = $1; $$.type = OCONFIG_TYPE_NUMBER;}
	| BTRUE			{$$.value.boolean = 1; $$.type = OCONFIG_TYPE_BOOLEAN;}
	| BFALSE		{$$.value.boolean = 0; $$.type = OCONFIG_TYPE_BOOLEAN;}
	| string		{$$.value.string = $1; $$.type = OCONFIG_TYPE_STRING;}
	;

argument_list:
	argument_list argument
	{
	 $$ = $1;
	 $$.argument_num++;
	 $$.argument = realloc ($$.argument, $$.argument_num * sizeof (oconfig_value_t));
	 $$.argument[$$.argument_num-1] = $2;
	}
	| argument
	{
	 $$.argument = malloc (sizeof (oconfig_value_t));
	 $$.argument[0] = $1;
	 $$.argument_num = 1;
	}
	;

identifier:
	UNQUOTED_STRING			{$$ = strdup ($1);}
	;

option:
	identifier argument_list EOL
	{
	 memset (&$$, '\0', sizeof ($$));
	 $$.key = $1;
	 $$.values = $2.argument;
	 $$.values_num = $2.argument_num;
	}
	;

block_begin:
	OPENBRAC identifier CLOSEBRAC EOL
	{
	 memset (&$$, '\0', sizeof ($$));
	 $$.key = $2;
	}
	|
	OPENBRAC identifier argument_list CLOSEBRAC EOL
	{
	 memset (&$$, '\0', sizeof ($$));
	 $$.key = $2;
	 $$.values = $3.argument;
	 $$.values_num = $3.argument_num;
	}
	;

block_end:
	OPENBRAC SLASH identifier CLOSEBRAC EOL
	{
	 $$ = $3;
	}
	;

block:
	block_begin statement_list block_end
	{
	 if (strcmp ($1.key, $3) != 0)
	 {
		printf ("block_begin = %s; block_end = %s;\n", $1.key, $3);
	 	yyerror ("Block not closed..\n");
		exit (1);
	 }
	 free ($3); $3 = NULL;
	 $$ = $1;
	 $$.children = $2.statement;
	 $$.children_num = $2.statement_num;
	}
	| block_begin block_end
	{
	 if (strcmp ($1.key, $2) != 0)
	 {
		printf ("block_begin = %s; block_end = %s;\n", $1.key, $2);
		yyerror ("Block not closed..\n");
		exit (1);
	 }
	 free ($2); $2 = NULL;
	 $$ = $1;
	 $$.children = NULL;
	 $$.children_num = 0;
	}
	;

statement:
	option		{$$ = $1;}
	| block		{$$ = $1;}
	| EOL		{$$.values_num = 0;}
	;

statement_list:
	statement_list statement
	{
	 $$ = $1;
	 if (($2.values_num > 0) || ($2.children_num > 0))
	 {
		 $$.statement_num++;
		 $$.statement = realloc ($$.statement, $$.statement_num * sizeof (oconfig_item_t));
		 $$.statement[$$.statement_num-1] = $2;
	 }
	}
	| statement
	{
	 if (($1.values_num > 0) || ($1.children_num > 0))
	 {
		 $$.statement = malloc (sizeof (oconfig_item_t));
		 $$.statement[0] = $1;
		 $$.statement_num = 1;
	 }
	 else
	 {
	 	$$.statement = NULL;
		$$.statement_num = 0;
	 }
	}
	;

entire_file:
	statement_list
	{
	 ci_root = malloc (sizeof (oconfig_item_t));
	 memset (ci_root, '\0', sizeof (oconfig_item_t));
	 ci_root->children = $1.statement;
	 ci_root->children_num = $1.statement_num;
	}
	| /* epsilon */
	{
	 ci_root = malloc (sizeof (oconfig_item_t));
	 memset (ci_root, '\0', sizeof (oconfig_item_t));
	 ci_root->children = NULL;
	 ci_root->children_num = 0;
	}
	;

%%
static int yyerror (const char *s)
{
	char *text;

	if (*yytext == '\n')
		text = "<newline>";
	else
		text = yytext;

	fprintf (stderr, "Parse error in file `%s', line %i near `%s': %s\n",
		c_file, yylineno, text, s);
	return (-1);
} /* int yyerror */

static char *unquote (const char *orig)
{
	char *ret = strdup (orig);
	int len;
	int i;

	if (ret == NULL)
		return (NULL);

	len = strlen (ret);

	if ((len < 2) || (ret[0] != '"') || (ret[len - 1] != '"'))
		return (ret);

	len -= 2;
	memmove (ret, ret + 1, len);
	ret[len] = '\0';

	for (i = 0; i < len; i++)
	{
		if (ret[i] == '\\')
		{
			memmove (ret + i, ret + (i + 1), len - i);
			len--;
		}
	}

	return (ret);
} /* char *unquote */
