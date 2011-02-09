#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "tokenizer.lex.h"

#define START_TIMER(name) \
  struct timeval name##_startt, name##_endt; \
  long name##_elapsed; \
  gettimeofday(&name##_startt, NULL);

#define RESET_TIMER(name) gettimeofday(&name##_startt, NULL);

#define MARK_TIMER(name) \
  gettimeofday(&name##_endt, NULL); \
  name##_elapsed = ((name##_endt.tv_sec - name##_startt.tv_sec) * 1000) + ((name##_endt.tv_usec - name##_startt.tv_usec) / 1000);

#define TIMER_MS(name) name##_elapsed

int new_query;

void parse_file(FILE* f) {
  yyscan_t scanner;
  lexinfo charpos = {0, 0};
  int token_type;
  const char* token;

  yylex_init_extra(&charpos, &scanner);
  yyset_in(f, scanner);
  do {
    token_type = yylex(scanner);
    token = yyget_text(scanner);
    if(random() < 50000) {
      if(!new_query) printf(" ");
      new_query = 0;
      printf("%s", token);
    }
    if(!new_query && (random() < 50000)) {
      printf("\n");
      new_query = 1;
    }
  } while(token_type != TOK_DONE);
  yylex_destroy(scanner);
}

int main(int argc, char* argv[]) {
  if(argc < 2) {
    fprintf(stderr, "Usage: %s <filename>+\n", argv[0]);
    return -1;
  }

  fprintf(stderr, "starting...\n");

  new_query = 1;

  unsigned long total_bytes = 0;
  unsigned long chunk_bytes = 0;
  START_TIMER(total);
  START_TIMER(chunk);

  for(int i = 1; i < argc; i++) {
    FILE* f = fopen(argv[i], "r");
    if(f == NULL) {
      fprintf(stderr, "can't open %s: %s\n", argv[i], strerror(errno));
      break;
    }
    parse_file(f);
    fclose(f);

    struct stat fstat;
    stat(argv[i], &fstat);
    total_bytes += fstat.st_size;
    chunk_bytes += fstat.st_size;

    MARK_TIMER(chunk);
    if(TIMER_MS(chunk) > 1000) {
      MARK_TIMER(total);
#define STUFF(k, t) k / 1024, t / 1000.0, ((float)(k) / 1024.0) / ((float)(t) / 1000.0)
      fprintf(stderr, "processed %5luk in %3.1fs = %6.1fk/s. total: %5luk in %5.1fs = %6.1fk/s\n", STUFF(chunk_bytes, TIMER_MS(chunk)), STUFF(total_bytes, TIMER_MS(total)));
      RESET_TIMER(chunk);
      chunk_bytes = 0;
    }
  }

  MARK_TIMER(total);
  fprintf(stderr, "In total, processed %luk in %.1fs = %.1fk/s\n", STUFF(total_bytes, TIMER_MS(total)));

  return 0;
}

