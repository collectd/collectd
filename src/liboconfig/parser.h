/* A Bison parser, made by GNU Bison 3.7.3.  */

/* Bison interface for Yacc-like parsers in C

   Copyright (C) 1984, 1989-1990, 2000-2015, 2018-2020 Free Software Foundation,
   Inc.

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

/* As a special exception, you may create a larger work that contains
   part or all of the Bison parser skeleton and distribute that work
   under terms of your choice, so long as that work isn't itself a
   parser generator using the skeleton or a modified version thereof
   as a parser skeleton.  Alternatively, if you modify or redistribute
   the parser skeleton itself, you may (at your option) remove this
   special exception, which will cause the skeleton and the resulting
   Bison output files to be licensed under the GNU General Public
   License without this special exception.

   This special exception was added by the Free Software Foundation in
   version 2.2 of Bison.  */

/* DO NOT RELY ON FEATURES THAT ARE NOT DOCUMENTED in the manual,
   especially those whose name start with YY_ or yy_.  They are
   private implementation details that can be changed or removed.  */

#ifndef YY_YY_SRC_LIBOCONFIG_PARSER_H_INCLUDED
# define YY_YY_SRC_LIBOCONFIG_PARSER_H_INCLUDED
/* Debug traces.  */
#ifndef YYDEBUG
# define YYDEBUG 0
#endif
#if YYDEBUG
extern int yydebug;
#endif

/* Token kinds.  */
#ifndef YYTOKENTYPE
# define YYTOKENTYPE
  enum yytokentype
  {
    YYEMPTY = -2,
    YYEOF = 0,                     /* "end of file"  */
    YYerror = 256,                 /* error  */
    YYUNDEF = 257,                 /* "invalid token"  */
    NUMBER = 258,                  /* NUMBER  */
    BTRUE = 259,                   /* BTRUE  */
    BFALSE = 260,                  /* BFALSE  */
    QUOTED_STRING = 261,           /* QUOTED_STRING  */
    UNQUOTED_STRING = 262,         /* UNQUOTED_STRING  */
    SLASH = 263,                   /* SLASH  */
    OPENBRAC = 264,                /* OPENBRAC  */
    CLOSEBRAC = 265,               /* CLOSEBRAC  */
    EOL = 266                      /* EOL  */
  };
  typedef enum yytokentype yytoken_kind_t;
#endif
/* Token kinds.  */
#define YYEOF 0
#define YYerror 256
#define YYUNDEF 257
#define NUMBER 258
#define BTRUE 259
#define BFALSE 260
#define QUOTED_STRING 261
#define UNQUOTED_STRING 262
#define SLASH 263
#define OPENBRAC 264
#define CLOSEBRAC 265
#define EOL 266

/* Value type.  */
#if ! defined YYSTYPE && ! defined YYSTYPE_IS_DECLARED
union YYSTYPE
{
#line 47 "src/liboconfig/parser.y"

	double  number;
	int     boolean;
	char   *string;
	oconfig_value_t  cv;
	oconfig_item_t   ci;
	argument_list_t  al;
	statement_list_t sl;

#line 98 "src/liboconfig/parser.h"

};
typedef union YYSTYPE YYSTYPE;
# define YYSTYPE_IS_TRIVIAL 1
# define YYSTYPE_IS_DECLARED 1
#endif


extern YYSTYPE yylval;

int yyparse (void);

#endif /* !YY_YY_SRC_LIBOCONFIG_PARSER_H_INCLUDED  */
