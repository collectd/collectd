%{
#include "plugin.h"

#include "ast.h"

#include "parser.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <math.h>

#undef ECHO
#define ECHO

static YY_BUFFER_STATE lexer_buffer;

void set_lexer_buffer(const char* str) {
    yylineno = 1;
    if (lexer_buffer) {
        yy_delete_buffer(lexer_buffer);
    }
    lexer_buffer = yy_scan_string(str);
    yy_switch_to_buffer(lexer_buffer);
}

int lexer_error(const char* s, int status_code) {
    ERROR("Unrecognized token at line %d: %s", yylineno, s);
    return status_code;
}

int yywrap(void) {
    return 1;
}

char* replace_escaped_chars(char* text, size_t text_size) {
    char* res = calloc(text_size + 1, sizeof(*res));
    if (!res) {
        return NULL;
    }
    size_t res_iter = 0;
    for (size_t i = 0; i < text_size; i++) {
        if (text[i] == '\\') {
            if (i + 1 >= text_size) {
                break;
            }
            switch (text[i + 1]) {
            case ('\\'): {
                res[res_iter++] = '\\';
                break;
            }
            case ('"'): {
                res[res_iter++] = '"';
                break;
            }
            case ('n'): {
                res[res_iter++] = '\n';
                break;
            }
            case ('r'): {
                res[res_iter++] = '\r';
                break;
            }
            case ('t'): {
                res[res_iter++] = '\t';
                break;
            }
            default: {
                break;
            }
            }
            i++;
        } else {
            res[res_iter++] = text[i];
        }
    }
    res[res_iter] = '\0';
    return res;
}

%}

%option noinput
%option nounput

%x COMMENT_TEXT
%x TYPE
%x HELP_NAME
%x HELP_HINT

%%
NaN {
    yylval.number = NAN;
    return FLOAT_NUMBER;
}

\+Inf {
    yylval.number = INFINITY;
    return FLOAT_NUMBER;
}

\-Inf {
    yylval.number = -INFINITY;
    return FLOAT_NUMBER;
}

"# TYPE" {
    BEGIN(TYPE);
    return TYPE_DECLARATION;
}

"# HELP" {
    BEGIN(HELP_NAME);
    return HELP_DECLARATION;
}

<TYPE>"counter" {
    BEGIN(INITIAL);
    yylval.metric_type = PR_COUNTER;
    return METRIC_TYPE;
}

<TYPE>"gauge" {
    BEGIN(INITIAL);
    yylval.metric_type = PR_GAUGE;
    return METRIC_TYPE;
}

<TYPE>"histogram" {
    BEGIN(INITIAL);
    yylval.metric_type = PR_HISTOGRAM;
    return METRIC_TYPE;
}

<TYPE>"summary" {
    BEGIN(INITIAL);
    yylval.metric_type = PR_SUMMARY;
    return METRIC_TYPE;
}

<TYPE>"untyped" {
    BEGIN(INITIAL);
    yylval.metric_type = PR_UNTYPED;
    return METRIC_TYPE;
}

<TYPE>[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.string = strdup(yytext);
    if (!yylval.string) {
        return EXIT_FAILURE;
    }
    return NAME;
}

"#" {
    BEGIN(COMMENT_TEXT);
}

<COMMENT_TEXT>[^\n]* {
    BEGIN(INITIAL);
    yylval.string = strndup(yytext + 1, strlen(yytext) - 1);
    if (!yylval.string) {
        return EXIT_FAILURE;
    }
    return COMMENT;
}

<HELP_NAME>[a-zA-Z_][a-zA-Z0-9_]* {
    BEGIN(HELP_HINT);
    yylval.string = strdup(yytext);
    if (!yylval.string) {
        return EXIT_FAILURE;
    }
    return NAME;
}

<HELP_HINT>[^\n]* {
    BEGIN(INITIAL);
    yylval.string = strndup(yytext + 1, strlen(yytext) - 1);
    if (!yylval.string) {
        return EXIT_FAILURE;
    }
    return METRIC_HELP;
}

\"(\\[\"\\nrt]|[^\"\\])*\" {
    yylval.string = replace_escaped_chars(yytext + 1, strlen(yytext) - 2);
    if (yylval.string == NULL) {
        return lexer_error("Couldn't allocate memory for string without escaped chars\n", EXIT_FAILURE);
    }
    return LABEL_VALUE;
}

[+-]?[0-9]+\.[0-9]+([eE][+-]?[0-9]+)? {
    yylval.number = atof(yytext);
    return FLOAT_NUMBER;
}

[+-]?[0-9]+ {
    yylval.integer = strtoll(yytext, NULL, 10);
    return INTEGER_NUMBER;
}

"{" {
    return OPEN_BRACE;
}

"}" {
    return CLOSE_BRACE;
}

= {
    return EQUALS;
}

, {
    return COMMA;
}

[a-zA-Z_][a-zA-Z0-9_]* {
    yylval.string = strdup(yytext);
    if (!yylval.string) {
        return EXIT_FAILURE;
    }
    return NAME;
}

[ \t\r]+ {}

"\n" {
    yylineno++;
}

. {}
%%
