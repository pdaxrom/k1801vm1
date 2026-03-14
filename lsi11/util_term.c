#include "util_term.h"

#if defined(PICO_ON_DEVICE)

#include "pico/stdlib.h"
#include "display_backend.h"

#include <stdio.h>

int util_term_set_socket_path(const char *path, char *err, size_t err_len)
{
    (void)path;
    if (err && err_len) {
        err[0] = '\0';
    }
    return 0;
}

int util_term_init_raw(void)
{
    return 0;
}

void util_term_restore(void)
{
}

int util_term_getc_nonblock(void)
{
    return display_backend_getc_nonblock();
}

void util_term_putc(char c)
{
    putchar((unsigned char)c);
    fflush(stdout);
}

#else

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <termios.h>
#include <unistd.h>

static struct termios oldt;
static int term_active = 0;
static int term_uses_tty = 0;
static int socket_fd = -1;
static const char *socket_path = NULL;

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

int util_term_set_socket_path(const char *path, char *err, size_t err_len)
{
    if (path != NULL && strlen(path) >= sizeof(((struct sockaddr_un *)0)->sun_path)) {
        if (err && err_len) {
            snprintf(err, err_len, "socket path too long: %s", path);
        }
        return -1;
    }

    socket_path = path;
    if (err && err_len) {
        err[0] = '\0';
    }
    return 0;
}

int util_term_init_raw(void)
{
    if (term_active) {
        return 0;
    }

    if (socket_path != NULL) {
        struct sockaddr_un addr;
        const size_t path_len = strlen(socket_path);

        socket_fd = socket(AF_UNIX, SOCK_STREAM, 0);
        if (socket_fd < 0) {
            fprintf(stderr, "socket connect failed for %s: %s\n",
                    socket_path, strerror(errno));
            return -1;
        }

        memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        memcpy(addr.sun_path, socket_path, path_len + 1u);

        if (connect(socket_fd, (struct sockaddr *)&addr,
                    (socklen_t)(offsetof(struct sockaddr_un, sun_path) + path_len + 1u)) != 0) {
            fprintf(stderr, "socket connect failed for %s: %s\n",
                    socket_path, strerror(errno));
            close(socket_fd);
            socket_fd = -1;
            return -1;
        }

        {
            const int flags = fcntl(socket_fd, F_GETFL, 0);
            if (flags >= 0) {
                (void)fcntl(socket_fd, F_SETFL, flags | O_NONBLOCK);
            }
        }

        term_active = 1;
        term_uses_tty = 0;
        return 0;
    }

    tcgetattr(STDIN_FILENO, &oldt);
    struct termios t = oldt;
    t.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    t.c_cc[VMIN] = 0;
    t.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &t);

    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);

    term_active = 1;
    term_uses_tty = 1;
    return 0;
}

void util_term_restore(void)
{
    if (!term_active) {
        return;
    }

    term_active = 0;

    if (socket_fd >= 0) {
        close(socket_fd);
        socket_fd = -1;
    }

    if (term_uses_tty) {
        term_uses_tty = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    }
}

int util_term_getc_nonblock(void)
{
    if (socket_fd >= 0) {
        unsigned char c;
        ssize_t n = recv(socket_fd, &c, 1, 0);

        if (n == 1) {
            return (int)c;
        }
        return -1;
    }

    unsigned char c;
    ssize_t n = read(STDIN_FILENO, &c, 1);
    if (n == 1) {
        return (int)c;
    }
    return -1;
}

void util_term_putc(char c)
{
    if (socket_fd >= 0) {
        const unsigned char out = (unsigned char)c;
        (void)send(socket_fd, &out, 1u, MSG_NOSIGNAL);
        return;
    }

    fputc((unsigned char)c, stdout);
    fflush(stdout);
}

#endif
