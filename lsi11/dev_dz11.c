#include "dev_dz11.h"

#include "devio.h"
#include "irq.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#if !defined(PICO_ON_DEVICE)
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#endif

/* DZ11 CSR base range (octal) */
#define DZ_BASE 0160100
#define DZ_CSR  0160100
#define DZ_RBUF 0160102 /* read */
#define DZ_LPR  0160102 /* write */
#define DZ_TCR  0160104
#define DZ_MSR  0160106 /* read */
#define DZ_TDR  0160106 /* write */

/* CSR bits */
#define CSR_CLR      0000020
#define CSR_MSE      0000040
#define CSR_RIE      0000100
#define CSR_RDONE    0000200
#define CSR_V_TLINE  8
#define CSR_TLINE    0007000
#define CSR_SAE      0010000
#define CSR_SA       0020000
#define CSR_TIE      0040000
#define CSR_TRDY     0100000
#define CSR_RW       (CSR_MSE | CSR_RIE | CSR_SAE | CSR_TIE)
#define CSR_MBZ      0004037

/* RBUF bits */
#define RBUF_CHAR    0000377
#define RBUF_V_RLINE 8
#define RBUF_FRME    0020000
#define RBUF_VALID   0100000

/* LPR bits */
#define LPR_LINE_MASK 0000007
#define LPR_RCVE      0010000

/* TDR bits */
#define TDR_CHAR      0000377

/* RX silo alarm level in words (SIMH-compatible threshold) */
#define DZ_SILO_ALM 16u

static uint16_t dz_csr;
static uint16_t dz_rbuf;
static uint16_t dz_lpr;
static uint16_t dz_tcr;
static uint16_t dz_msr;
static uint16_t dz_tdr;

static uint16_t dz_silo[DZ_SILO_ALM];
static unsigned dz_scnt;
static uint8_t dz_line_rcve[DZ11_LINES];
static int dz_8bit_mode;
static int dz_initialized;

#if !defined(PICO_ON_DEVICE)
static int dz_listen_fd = -1;
static int dz_listen_port = 0;
static int dz_line_fd[DZ11_LINES] = {-1, -1, -1, -1, -1, -1, -1, -1};
#else
static int dz_listen_port = 0;
#endif

static void dz_set_err(char *err, size_t err_len, const char *fmt, ...)
{
    va_list ap;

    if (!err || err_len == 0) {
        return;
    }

    va_start(ap, fmt);
    vsnprintf(err, err_len, fmt, ap);
    va_end(ap);
}

static inline uint8_t dz_mask_char(uint8_t v)
{
    return dz_8bit_mode ? v : (uint8_t)(v & 0177);
}

static inline uint8_t dz_csr_tline(void)
{
    return (uint8_t)((dz_csr & CSR_TLINE) >> CSR_V_TLINE);
}

static inline void dz_csr_set_tline(uint8_t line)
{
    dz_csr &= (uint16_t)~CSR_TLINE;
    dz_csr |= (uint16_t)((line & 07u) << CSR_V_TLINE);
}

static int dz_find_next_tx_line(uint8_t from_line)
{
    unsigned i;

    for (i = 1; i <= DZ11_LINES; i++) {
        uint8_t line = (uint8_t)((from_line + i) & 07u);
        if (dz_tcr & (1u << line)) {
            return (int)line;
        }
    }

    return -1;
}

static void dz_update_receive_flags(void)
{
    if ((dz_csr & CSR_MSE) && dz_scnt > 0u) {
        dz_csr |= CSR_RDONE;
    } else {
        dz_csr &= (uint16_t)~CSR_RDONE;
    }

    if ((dz_csr & CSR_MSE) && (dz_csr & CSR_SAE) && dz_scnt >= DZ_SILO_ALM) {
        dz_csr |= CSR_SA;
    } else if (dz_scnt < DZ_SILO_ALM || !(dz_csr & CSR_MSE)) {
        dz_csr &= (uint16_t)~CSR_SA;
    }
}

static void dz_update_transmit_flags(void)
{
    int next;

    if (!(dz_csr & CSR_MSE)) {
        dz_csr &= (uint16_t)~CSR_TRDY;
        return;
    }

    next = dz_find_next_tx_line(dz_csr_tline());
    if (next < 0) {
        dz_csr &= (uint16_t)~CSR_TRDY;
        return;
    }

    dz_csr_set_tline((uint8_t)next);
    dz_csr |= CSR_TRDY;
}

static void dz_silo_push(uint16_t v)
{
    if (dz_scnt >= DZ_SILO_ALM) {
        return;
    }

    dz_silo[dz_scnt++] = v;
    dz_update_receive_flags();
}

static uint16_t dz_silo_pop(void)
{
    uint16_t v;

    if (dz_scnt == 0u) {
        dz_update_receive_flags();
        return 0;
    }

    v = dz_silo[0];
    if (dz_scnt > 1u) {
        memmove(&dz_silo[0], &dz_silo[1], (dz_scnt - 1u) * sizeof(dz_silo[0]));
    }
    dz_scnt--;
    dz_update_receive_flags();
    return v;
}

#if !defined(PICO_ON_DEVICE)
static void dz_close_fd(int *fdp)
{
    if (fdp && *fdp >= 0) {
        close(*fdp);
        *fdp = -1;
    }
}

static void dz_close_all_lines(void)
{
    unsigned line;

    for (line = 0; line < DZ11_LINES; line++) {
        dz_close_fd(&dz_line_fd[line]);
    }
}

static int dz_set_nonblock(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) != 0) {
        return -1;
    }
    return 0;
}

static int dz_open_listener(int port, char *err, size_t err_len)
{
    struct sockaddr_in sa;
    int fd = -1;
    int one = 1;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        dz_set_err(err, err_len, "DZ11 socket() failed: %s", strerror(errno));
        return -1;
    }

    (void)setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_ANY);
    sa.sin_port = htons((uint16_t)port);

    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) != 0) {
        dz_set_err(err, err_len, "DZ11 bind(%d) failed: %s", port, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, (int)DZ11_LINES) != 0) {
        dz_set_err(err, err_len, "DZ11 listen(%d) failed: %s", port,
                   strerror(errno));
        close(fd);
        return -1;
    }
    if (dz_set_nonblock(fd) != 0) {
        dz_set_err(err, err_len, "DZ11 nonblock failed: %s", strerror(errno));
        close(fd);
        return -1;
    }

    dz_close_fd(&dz_listen_fd);
    dz_listen_fd = fd;
    fprintf(stderr, "DZ11: listening on TCP port %d\n", port);
    return 0;
}

static void dz_disconnect_line(unsigned line)
{
    if (line < DZ11_LINES) {
        dz_close_fd(&dz_line_fd[line]);
    }
}

static int dz_find_free_line(void)
{
    unsigned line;

    for (line = 0; line < DZ11_LINES; line++) {
        if (dz_line_fd[line] < 0) {
            return (int)line;
        }
    }
    return -1;
}

static void dz_accept_clients(void)
{
    if (dz_listen_fd < 0) {
        return;
    }

    for (;;) {
        int line;
        int fd = accept(dz_listen_fd, NULL, NULL);
        if (fd < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                return;
            }
            return;
        }

        line = dz_find_free_line();
        if (line < 0) {
            close(fd);
            continue;
        }
        if (dz_set_nonblock(fd) != 0) {
            close(fd);
            continue;
        }
#ifdef SO_NOSIGPIPE
        {
            int one = 1;
            (void)setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
        }
#endif
        dz_line_fd[(unsigned)line] = fd;
    }
}

static void dz_poll_receive_lines(void)
{
    unsigned line;

    for (line = 0; line < DZ11_LINES; line++) {
        uint8_t buf[64];
        ssize_t n;
        size_t i;

        if (dz_line_fd[line] < 0) {
            continue;
        }
        if (!dz_line_rcve[line]) {
            continue;
        }
        if (dz_scnt >= DZ_SILO_ALM) {
            break;
        }

        n = recv(dz_line_fd[line], (void *)buf, sizeof(buf), 0);
        if (n == 0) {
            dz_disconnect_line(line);
            continue;
        }
        if (n < 0) {
            if (errno == EWOULDBLOCK || errno == EAGAIN) {
                continue;
            }
            dz_disconnect_line(line);
            continue;
        }

        for (i = 0; i < (size_t)n; i++) {
            uint16_t v;

            if (dz_scnt >= DZ_SILO_ALM) {
                break;
            }
            v = (uint16_t)(dz_mask_char(buf[i]) & RBUF_CHAR);
            v |= (uint16_t)((line & 07u) << RBUF_V_RLINE);
            v |= RBUF_VALID;
            dz_silo_push(v);
        }
    }
}

static void dz_transmit_line(uint8_t line, uint8_t ch)
{
    ssize_t n;

    if (line >= DZ11_LINES) {
        return;
    }
    if (dz_line_fd[line] < 0) {
        return;
    }

    n = send(dz_line_fd[line], (const void *)&ch, 1, 0);
    if (n == 1) {
        return;
    }
    if (n < 0 && (errno == EWOULDBLOCK || errno == EAGAIN)) {
        return;
    }
    dz_disconnect_line(line);
}
#else
static void dz_close_all_lines(void)
{
}

static void dz_accept_clients(void)
{
}

static void dz_poll_receive_lines(void)
{
}

static void dz_transmit_line(uint8_t line, uint8_t ch)
{
    (void)line;
    (void)ch;
}
#endif

static void dz_refresh_msr(void)
{
    uint16_t v = 0;
    unsigned line;

    for (line = 0; line < DZ11_LINES; line++) {
        int connected = 0;
#if !defined(PICO_ON_DEVICE)
        connected = (dz_line_fd[line] >= 0) ? 1 : 0;
#endif
        if (!connected) {
            continue;
        }
        if (dz_tcr & (uint16_t)(1u << (8u + line))) {
            v |= (uint16_t)(1u << (8u + line));
        } else {
            v |= (uint16_t)(1u << line);
        }
    }
    dz_msr = v;
}

static void dz_soft_clear(void)
{
    memset(dz_silo, 0, sizeof(dz_silo));
    memset(dz_line_rcve, 0, sizeof(dz_line_rcve));
    dz_scnt = 0;
    dz_csr = 0;
    dz_rbuf = 0;
    dz_lpr = 0;
    dz_tcr = 0;
    dz_msr = 0;
    dz_tdr = 0;
}

static void dz_write_csr(uint16_t data)
{
    if (data & CSR_CLR) {
        dz_soft_clear();
    }

    dz_csr &= (uint16_t)~CSR_RW;
    dz_csr |= (uint16_t)(data & CSR_RW);

    if (!(dz_csr & CSR_MSE)) {
        dz_csr &= (uint16_t)~(CSR_SA | CSR_RDONE | CSR_TRDY);
    } else {
        dz_update_receive_flags();
        dz_update_transmit_flags();
    }
}

static uint8_t dz_read8(uint16_t addr)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t v = 0;

    switch (base) {
    case DZ_CSR:
        dz_update_receive_flags();
        dz_update_transmit_flags();
        v = (uint16_t)(dz_csr & (uint16_t)~CSR_MBZ);
        break;

    case DZ_RBUF:
        if ((addr & 1) == 0) {
            dz_csr &= (uint16_t)~CSR_SA;
            if (dz_csr & CSR_MSE) {
                dz_rbuf = dz_silo_pop();
            } else {
                dz_rbuf = 0;
            }
        }
        v = dz_rbuf;
        break;

    case DZ_TCR:
        v = dz_tcr;
        break;

    case DZ_MSR:
        dz_refresh_msr();
        v = dz_msr;
        break;

    default:
        v = 0;
        break;
    }

    if (addr & 1) {
        return (uint8_t)((v >> 8) & 000377);
    }
    return (uint8_t)(v & 000377);
}

static void dz_write8(uint16_t addr, uint8_t b)
{
    uint16_t base = (uint16_t)(addr & 0177776);
    uint16_t old;
    uint16_t v;

    switch (base) {
    case DZ_CSR:
        old = dz_csr;
        if (addr & 1) {
            v = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
        } else {
            v = (uint16_t)((old & 0177400) | b);
        }
        dz_write_csr(v);
        return;

    case DZ_LPR:
        old = dz_lpr;
        if (addr & 1) {
            dz_lpr = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
        } else {
            dz_lpr = (uint16_t)((old & 0177400) | b);
        }
        dz_line_rcve[dz_lpr & LPR_LINE_MASK] = (dz_lpr & LPR_RCVE) ? 1u : 0u;
        return;

    case DZ_TCR:
        old = dz_tcr;
        if (addr & 1) {
            dz_tcr = (uint16_t)((old & 000377) | ((uint16_t)b << 8));
        } else {
            dz_tcr = (uint16_t)((old & 0177400) | b);
        }
        dz_update_transmit_flags();
        dz_refresh_msr();
        return;

    case DZ_TDR:
        if (addr & 1) {
            dz_tdr = (uint16_t)((dz_tdr & 000377) | ((uint16_t)b << 8));
            return;
        }

        dz_tdr = (uint16_t)((dz_tdr & 0177400) | b);
        if ((dz_csr & CSR_MSE) && (dz_csr & CSR_TRDY)) {
            uint8_t line = dz_csr_tline();
            if (dz_tcr & (uint16_t)(1u << line)) {
                dz_csr &= (uint16_t)~CSR_TRDY;
                dz_transmit_line(line, dz_mask_char((uint8_t)(dz_tdr & TDR_CHAR)));
                dz_update_transmit_flags();
            }
        }
        return;

    default:
        return;
    }
}

int dz11_rx_irq_pending(void)
{
    dz_update_receive_flags();

    if (!(dz_csr & CSR_RIE)) {
        return 0;
    }
    if (dz_csr & CSR_SAE) {
        return (dz_csr & CSR_SA) ? 1 : 0;
    }
    return (dz_csr & CSR_RDONE) ? 1 : 0;
}

void dz11_rx_irq_ack(void)
{
}

int dz11_tx_irq_pending(void)
{
    dz_update_transmit_flags();
    return ((dz_csr & CSR_TIE) && (dz_csr & CSR_TRDY)) ? 1 : 0;
}

void dz11_tx_irq_ack(void)
{
}

int dz11_init(void)
{
    static const io_range_t r = {DZ_BASE, DZ_BASE + 7u, dz_read8, dz_write8, "DZ11"};
    static const irq_source_t rx = {"DZ11 RX", 000300, 5, dz11_rx_irq_pending,
                                    dz11_rx_irq_ack
                                   };
    static const irq_source_t tx = {"DZ11 TX", 000304, 5, dz11_tx_irq_pending,
                                    dz11_tx_irq_ack
                                   };
    char err[128];

    if (devio_register(&r) != 0) {
        return -1;
    }
    if (irq_register(&rx) != 0) {
        return -1;
    }
    if (irq_register(&tx) != 0) {
        return -1;
    }

    dz11_reset();
    dz_initialized = 1;

    if (dz_listen_port > 0) {
#if !defined(PICO_ON_DEVICE)
        if (dz_open_listener(dz_listen_port, err, sizeof(err)) != 0) {
            fprintf(stderr, "%s\n", err);
            return -1;
        }
#else
        fprintf(stderr, "DZ11 TCP listener is not available on this target\n");
        return -1;
#endif
    }

    return 0;
}

void dz11_reset(void)
{
    dz_soft_clear();
    dz_refresh_msr();
}

void dz11_poll(void)
{
    dz_accept_clients();
    dz_poll_receive_lines();
    dz_refresh_msr();
    dz_update_receive_flags();
    dz_update_transmit_flags();
}

void dz11_shutdown(void)
{
#if !defined(PICO_ON_DEVICE)
    dz_close_all_lines();
    dz_close_fd(&dz_listen_fd);
#else
    dz_close_all_lines();
#endif
    dz_initialized = 0;
}

void dz11_set_8bit(int on)
{
    dz_8bit_mode = on ? 1 : 0;
}

int dz11_set_listen_port(int port, char *err, size_t err_len)
{
    if (port < 0 || port > 65535) {
        dz_set_err(err, err_len, "DZ11 port out of range: %d", port);
        return -1;
    }

    dz_listen_port = port;

    if (!dz_initialized) {
        return 0;
    }

#if !defined(PICO_ON_DEVICE)
    dz_close_all_lines();
    dz_close_fd(&dz_listen_fd);
    if (dz_listen_port == 0) {
        return 0;
    }
    return dz_open_listener(dz_listen_port, err, err_len);
#else
    if (dz_listen_port != 0) {
        dz_set_err(err, err_len, "DZ11 TCP listener is not available on this target");
        return -1;
    }
    return 0;
#endif
}

#ifdef LSI11_TESTS
void dz11_test_inject_rx(unsigned line, uint8_t ch, int framing_error)
{
    uint16_t v;

    if (line >= DZ11_LINES) {
        return;
    }

    v = (uint16_t)(ch & RBUF_CHAR);
    v |= (uint16_t)((line & 07u) << RBUF_V_RLINE);
    if (framing_error) {
        v |= RBUF_FRME;
    }
    v |= RBUF_VALID;
    dz_silo_push(v);
}
#endif
