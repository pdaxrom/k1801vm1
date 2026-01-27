#ifndef UTIL_TERM_H_
#define UTIL_TERM_H_

void util_term_init_raw(void);
void util_term_restore(void);

int  util_term_getc_nonblock(void); /* -1 if none */
void util_term_putc(char c);

#endif
