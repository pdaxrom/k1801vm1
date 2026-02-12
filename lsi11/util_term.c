#define _POSIX_C_SOURCE 200809L
#include "util_term.h"
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <stdio.h>

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
