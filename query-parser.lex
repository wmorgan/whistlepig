%{
#include <string.h>
#include "whistlepig.h"
#include "query-parser.h"
#include "query-parser.tab.h"

#define YY_EXTRA_TYPE query_parse_context*
#define YY_USER_ACTION yylloc->first_line = yylineno;

#define YY_INPUT(buf,result,max_size) \
{                                     \
  if(yyextra->input[0] == 0)          \
     result = YY_NULL;                \
  else {                              \
     buf[0] = yyextra->input[0];      \
     yyextra->input++;                \
     result = 1;                      \
  }                                   \
}

%}

%option 8bit reentrant fast noyywrap yylineno
%option outfile="query-parser.lex.c" header-file="query-parser.lex.h"
%option prefix="query_parser_"
%option bison-bridge bison-locations

/* for the first char, everything is allowed except ()"-~ */
FIRSTWORDCHAR [[:alnum:]+=!@#$%\^&\*_|'/\?\.\,<>`]

/* inside a word, everything is allowed except ()" */
INNERWORDCHAR [[:alnum:]+=!@#$%\^&\*_|'/\?\.\,<>`\-~]

%%

OR return OR;

{FIRSTWORDCHAR}{INNERWORDCHAR}* {
  yylval->string = strdup(yytext);
  return WORD;
}

[\n[:blank:]] ; // nothing

. return yytext[0];

%%

