/* Simple raw terminal handling (POSIX only). */

#include "util_term.h"
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>

static struct termios orig_term;
static int raw_enabled = 0;

void term_raw_init(void) {
    if (raw_enabled) return;
    if (tcgetattr(STDIN_FILENO, &orig_term) == -1) return;
    struct termios raw = orig_term;
    raw.c_lflag &= ~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    /* make reads non‑blocking */
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
    raw_enabled = 1;
}

void term_raw_restore(void) {
    if (!raw_enabled) return;
    tcsetattr(STDIN_FILENO, TCSANOW, &orig_term);
    raw_enabled = 0;
}

int host_getch_nonblock(void) {
    unsigned char ch;
    ssize_t n = read(STDIN_FILENO, &ch, 1);
    if (n == 1) return (int)ch;
    return -1;
}

