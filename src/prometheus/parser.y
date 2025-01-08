%{
#include "plugin.h"

#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

extern int yylex(void);
void yyerror(const char* s);

extern int yylineno;
extern char *yytext;
pr_item_list_t* pr_items = NULL;

%}

%union {
    char* string;
    double number;
    int64_t integer;
    pr_metric_type_t metric_type;
    pr_metric_entry_t* metric;
    pr_label_t* label;
    pr_timestamp_t* timestamp;
    pr_comment_entry_t* comment;
    pr_type_entry_t* type;
    pr_help_entry_t* help;
    pr_entry_t* entry;
    pr_item_list_t* item_list;
}

%token <string> NAME LABEL_VALUE COMMENT METRIC_HELP
%token <number> FLOAT_NUMBER
%token <integer> INTEGER_NUMBER
%token <metric_type> METRIC_TYPE
%token TYPE_DECLARATION HELP_DECLARATION OPEN_BRACE CLOSE_BRACE EQUALS COMMA
%type <number> numeric_value
%type <timestamp> timestamp
%type <label> label label_list inner_label_list
%type <metric> metric
%type <comment> comment
%type <type> type
%type <help> help
%type <entry> entry
%type <item_list> item_list

%destructor {
    free($$);
} NAME LABEL_VALUE COMMENT METRIC_HELP

%destructor {
    free($$);
} timestamp

%destructor {
    pr_delete_label_list($$);
} label label_list inner_label_list

%destructor {
    pr_delete_metric_entry($$);
} metric

%destructor {
    pr_delete_comment_entry($$);
} comment

%destructor {
    pr_delete_type_entry($$);
} type

%destructor {
    pr_delete_help_entry($$);
} help

%destructor {
    pr_delete_entry($$);
} entry


%error-verbose

%%

input:
    | item_list {
        pr_items = $1;
    }
    ;

item_list:
    entry
    {
        $$ = pr_create_item_list();
        if (!$$) {
            pr_delete_entry($1);
            return EXIT_FAILURE;
        }
        if (pr_add_entry_to_item_list($$, $1) < 0) {
            pr_delete_entry($1);
            return EXIT_FAILURE;
        }
        pr_delete_entry($1);
    }
    | entry item_list
    {
        if (pr_add_entry_to_item_list($2, $1) < 0) {
            pr_delete_entry($1);
            return EXIT_FAILURE;
        }
        $$ = $2;
        pr_delete_entry($1);
    }
    ;

entry:
    metric
    {
        $$ = pr_create_entry_from_metric($1);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }
    | comment {
        $$ = pr_create_entry_from_comment($1);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }
    | type {
        $$ = pr_create_entry_from_type($1);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }
    | help {
        $$ = pr_create_entry_from_help($1);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }
    ;

metric:
    NAME label_list numeric_value timestamp
    {
        $$ = pr_create_metric_entry($1, $2, $3, $4);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }
    ;

numeric_value:
    FLOAT_NUMBER
    {
        $$ = $1;
    }
    | INTEGER_NUMBER
    {
        $$ = $1;
    }
    ;

timestamp:
    INTEGER_NUMBER
    {
        $$ = pr_create_value_timestamp($1);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }
    |
    {
        $$ = pr_create_empty_timestamp();
        if (!$$) {
            return EXIT_FAILURE;
        }
    }
    ;

comment:
   COMMENT
    {
        $$ = pr_create_comment_entry($1);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }
    ;

label_list:
    OPEN_BRACE inner_label_list CLOSE_BRACE
    {
        $$ = $2;
    }
    |
    {
        $$ = NULL;
    }
    ;

inner_label_list:
    label {
        $$ = $1;
    }
    | label COMMA inner_label_list {
        $$ = pr_add_label_to_list($3, $1);
    }
    |
    {
        $$ = NULL;
    }
    ;

label:
    NAME EQUALS LABEL_VALUE
    {
        $$ = pr_create_label($1, $3);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }
    ;

type:
    TYPE_DECLARATION NAME METRIC_TYPE {
        $$ = pr_create_type_entry($2, $3);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }

help:
    HELP_DECLARATION NAME METRIC_HELP {
        $$ = pr_create_help_entry($2, $3);
        if (!$$) {
            return EXIT_FAILURE;
        }
    }

%%

void yyerror(const char *s) {
    ERROR("Syntax error at line %d: %s near '%s'", yylineno, s, yytext);
}
