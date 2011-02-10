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

FIRSTWORDCHAR [[:alnum:]]
INNERWORDCHAR [[:alnum:]'_#@\-\.:\/]
LASTWORDCHAR  [[:alnum:]_#@-]

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

\n {
  yyextra->start++;
  yyextra->end++;
}

. {
  yyextra->start++;
  yyextra->end++;
}

%%
