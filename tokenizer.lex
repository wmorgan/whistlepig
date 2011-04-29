%top{
  #define TOK_DONE 0
  #define TOK_NUMBER 1
  #define TOK_WORD 2

  #include "segment.h"

  typedef struct lexinfo {
    pos_t start;
    pos_t end;
  } lexinfo;
}

%option 8bit reentrant fast noyywrap
%option outfile="tokenizer.lex.c" header-file="tokenizer.lex.h"
%option extra-type="struct lexinfo*"

FIRSTWORDCHAR [^[:blank:][:punct:]\r\n]
INNERWORDCHAR [^[:blank:]]
LASTWORDCHAR  [^[:blank:][:punct:]\r\n]

%%
\-?[[:digit:]]+(\.([[:digit:]]+)?)? {
  yyextra->start = yyextra->end;
  yyextra->end += (pos_t)yyleng;
  return TOK_NUMBER;
}

{FIRSTWORDCHAR}{INNERWORDCHAR}*{LASTWORDCHAR} {
  yyextra->start = yyextra->end;
  yyextra->end += (pos_t)yyleng;
  return TOK_WORD;
}

{FIRSTWORDCHAR}{LASTWORDCHAR}? {
  yyextra->start = yyextra->end;
  yyextra->end += (pos_t)yyleng;
  return TOK_WORD;
}

[\r\n] {
  yyextra->start++;
  yyextra->end++;
}

. {
  yyextra->start++;
  yyextra->end++;
}

%%
