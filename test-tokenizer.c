#include "test.h"
#include "tokenizer.lex.h"

#define ASSERT_NEXT_WORD(word) { \
  int token_type = yylex(scanner); \
  ASSERT(token_type == TOK_WORD); \
  ASSERT(!strcmp(word, yyget_text(scanner))); \
}

#define ASSERT_DONE { \
  int token_type = yylex(scanner); \
  ASSERT(token_type == TOK_DONE); \
}

TEST(tokenizes_easy_words) {
  yyscan_t scanner;
  lexinfo charpos = {0, 0};

  yylex_init_extra(&charpos, &scanner);

  const char* string = "i love mice";
  YY_BUFFER_STATE state = yy_scan_string(string, scanner);

  ASSERT_NEXT_WORD("i");
  ASSERT_NEXT_WORD("love");
  ASSERT_NEXT_WORD("mice");
  ASSERT_DONE;

  yy_delete_buffer(state, scanner);
  yylex_destroy(scanner);

  return NO_ERROR;
}

TEST(strips_trailing_punctuation) {
  yyscan_t scanner;
  lexinfo charpos = {0, 0};

  yylex_init_extra(&charpos, &scanner);

  const char* string = "hey! this: you're <cool>";
  YY_BUFFER_STATE state = yy_scan_string(string, scanner);

  ASSERT_NEXT_WORD("hey");
  ASSERT_NEXT_WORD("this");
  ASSERT_NEXT_WORD("you're");
  ASSERT_NEXT_WORD("cool");
  ASSERT_DONE;

  yy_delete_buffer(state, scanner);
  yylex_destroy(scanner);

  return NO_ERROR;
}

