#include "util_term.h"

#if defined(PICO_ON_DEVICE)

#include "pico/stdlib.h"

#include <stdio.h>

void util_term_init_raw(void)
{
}

void util_term_restore(void)
{
}

int util_term_getc_nonblock(void)
{
    int c = getchar_timeout_us(0);
    if (c == PICO_ERROR_TIMEOUT) {
        return -1;
    }
    return c & 0xff;
}

void util_term_putc(char c)
{
    putchar((unsigned char)c);
    fflush(stdout);
}

#else

#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

static struct termios oldt;
static int raw_on = 0;

void util_term_init_raw(void)
{
    if (raw_on) {
        return;
    }
    raw_on = 1;

    tcgetattr(STDIN_FILENO, &oldt);
    struct termios t = oldt;
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
}

void util_term_restore(void)
{
    if (!raw_on) {
        return;
    }
    raw_on = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
}

int util_term_getc_nonblock(void)
{
    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) {
        return (int)c;
    }
    return -1;
}

void util_term_putc(char c)
{
    fputc((unsigned char)c, stdout);
    fflush(stdout);
}

#endif
