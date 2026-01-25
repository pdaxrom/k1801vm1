/* Minimal terminal handling for raw input. */
#ifndef UTIL_TERM_H_
#define UTIL_TERM_H_

/* Initialise terminal in raw, non‑blocking mode. */
void term_raw_init(void);

/* Restore original terminal settings. */
void term_raw_restore(void);

/* Return next character from stdin or -1 if none available. */
int host_getch_nonblock(void);

#endif /* UTIL_TERM_H_ */

