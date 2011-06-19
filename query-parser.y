%{
#include <stdio.h>
#include <string.h>

#include "query.h"
#include "query-parser.h"
#include "query-parser.tab.h"

char* strdup(const char* wtf);

#define scanner context->scanner

int query_parser_lex(YYSTYPE * yylval_param,YYLTYPE * yylloc_param ,void* yyscanner);
void query_parser_error(YYLTYPE* locp, query_parse_context* context, const char* err);

%}

// see http://www.phpcompiler.org/articles/reentrantparser.html

%name-prefix="query_parser_"
//%define api.pure too advanced for the old-ass version of bison on os x apparently
%pure-parser
%locations
%defines
%error-verbose

%parse-param { query_parse_context* context }
%lex-param { void* scanner  }

%union {
  wp_query* query;
  char* string;
}

%token <string> WORD
%left <string> OR
%type <query> query atom disj parens phrase words result

%%

result: query { context->result = $$; }
;

query:           { $$ = NULL;  }
    | query atom { if($1 == NULL) $$ = $2;
                   else if($1->type == WP_QUERY_CONJ) $$ = wp_query_add($1, $2);
                   else {
                     $$ = wp_query_new_conjunction();
                     $$ = wp_query_add($$, $1);
                     $$ = wp_query_add($$, $2);
                   }
                 }
    | query disj { if($1 == NULL) $$ = $2;
                   else if($1->type == WP_QUERY_CONJ) $$ = wp_query_add($1, $2);
                   else {
                     $$ = wp_query_new_conjunction();
                     $$ = wp_query_add($$, $1);
                     $$ = wp_query_add($$, $2);
                   }
                 }

;

disj: atom OR atom { $$ = wp_query_new_disjunction(); $$ = wp_query_add($$, $1); $$ = wp_query_add($$, $3); }
    | disj OR atom { $$ = wp_query_add($1, $3); }
;

atom: WORD            { $$ = wp_query_new_term(strdup(context->default_field), $1); }
    | parens
    | phrase
    | WORD ':' WORD   { $$ = wp_query_new_term($1, $3); }
    | WORD ':' parens { $$ = wp_query_set_all_child_fields($3, $1); }
    | WORD ':' phrase { $$ = wp_query_set_all_child_fields($3, $1); }
    | '-' atom        { $$ = wp_query_new_negation(); $$ = wp_query_add($$, $2); }
    | '~' WORD        { $$ = wp_query_new_label($2); }
    | '*'             { $$ = wp_query_new_every(); }
;

phrase: '"' words '"'   { $$ = $2; }
;

words: WORD       { $$ = wp_query_new_phrase(); $$ = wp_query_add($$, wp_query_new_term(strdup(context->default_field), $1)); }
     | words WORD { $$ = wp_query_add($1, wp_query_new_term(strdup(context->default_field), $2)); }
;

parens: '(' query ')' { $$ = $2; }
;

%%
