/* Minimal X11 backend for the BK0010‑01 emulator.
 * Provides a thin wrapper used by main.c when compiled with -DUSE_X11.
 * The implementation focuses on window creation, drawing the VRAM
 * contents and handling quit events. Keyboard handling is limited to
 * the keys required for basic operation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include "bk_hw.h"
#include "x11_gui.h"

static Display *display = NULL;
static Window window;
static GC gc;
static XImage *ximage = NULL;
static int screen_num;
/* Scaling factor applied when the emulator window would be smaller than half
   of the screen. The factor is calculated during initialization and used by
   both the window creation and the drawing routine. */
static int scale_factor = 1;

/* Convert X11 keysym to the same codes used by the SDL version.
 * This is a simplified mapping sufficient for the emulator's UI.
 */
static int translate_key(KeySym ks, unsigned int state)
{
    if (state & ControlMask) {
        switch (ks) {
            case XK_g: return 0007; /* BEL */
            case XK_m: return 0015; /* set tab stop */
            case XK_p: return 0020; /* repeat */
            case XK_r: return 0022; /* cursor home */
            case XK_t: return 0024; /* clear screen */
            case XK_u: return 0025; /* line delete */
            default: break;
        }
    }

    switch (ks) {
        case XK_Escape: return 0003;
        case XK_BackSpace: return 0010;
        case XK_Return: return 0012;
        case XK_Tab: return 0211;
        case XK_Left: return 0010;
        case XK_Right: return 0037;
        case XK_Up: return 0040;
        case XK_Down: return 0041;
        case XK_Home: return 0022;
        case XK_F1: return 0016; /* RUS */
        case XK_F2: return 0017; /* LAT */
        case XK_F5: return 0014; /* reset screen */
        default: break;
    }

    if (ks >= XK_0 && ks <= XK_9) {
        static const char shifted[] = ")!@#$%^&*(";
        char c = (char)('0' + (ks - XK_0));
        if (state & ShiftMask) c = shifted[ks - XK_0];
        return (int)c;
    }
    if (ks >= XK_a && ks <= XK_z) {
        char c = (char)('a' + (ks - XK_a));
        if (state & ShiftMask) c = (char)('A' + (ks - XK_a));
        return (int)c;
    }
    if (ks == XK_space) return ' ';
    return 0;
}

int x11_gui_init(void)
{
    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "XOpenDisplay failed\n");
        return -1;
    }
    screen_num = DefaultScreen(display);
    int width = BK_SCREEN_WIDTH;
    int height = BK_SCREEN_HEIGHT;

    /* Determine a scaling factor so that the window occupies at least half of
       the screen in either dimension. */
    int screen_w = DisplayWidth(display, screen_num);
    int screen_h = DisplayHeight(display, screen_num);
    int min_w = screen_w / 2;
    int min_h = screen_h / 2;
    scale_factor = 1;
    if (width < min_w || height < min_h) {
        int sf_w = (min_w + width - 1) / width;   /* ceil division */
        int sf_h = (min_h + height - 1) / height;
        scale_factor = sf_w > sf_h ? sf_w : sf_h;
    }

    int win_w = width * scale_factor;
    int win_h = height * scale_factor;

    window = XCreateSimpleWindow(display, RootWindow(display, screen_num),
                               0, 0, win_w, win_h, 1,
                               BlackPixel(display, screen_num),
                               WhitePixel(display, screen_num));
    XStoreName(display, window, "BK0010-01 X11");
    XSelectInput(display, window, ExposureMask | KeyPressMask | StructureNotifyMask);
    XMapWindow(display, window);
    gc = XCreateGC(display, window, 0, NULL);

    // Allocate an XImage with the scaled dimensions.
    ximage = XCreateImage(display, DefaultVisual(display, screen_num),
                          DefaultDepth(display, screen_num), ZPixmap,
                          0, NULL, win_w, win_h, 32, 0);
    if (!ximage) {
        fprintf(stderr, "XCreateImage failed\n");
        return -1;
    }
    ximage->data = malloc(ximage->bytes_per_line * win_h);
    if (!ximage->data) {
        fprintf(stderr, "malloc for XImage data failed\n");
        XDestroyImage(ximage);
        return -1;
    }
    return 0;
}

void x11_gui_draw(void)
{
    byte *vram = bk_hw_vram_ptr();
    if (!vram) return;
    word vram_size = bk_hw_vram_size();
    word shift = bk_hw_shift_reg();
    // Render the original bitmap into a temporary buffer.
    uint32_t *src = (uint32_t *)malloc(BK_SCREEN_WIDTH * BK_SCREEN_HEIGHT * sizeof(uint32_t));
    if (!src) return;
    word line_bytes = BK_SCREEN_WIDTH / 8;
    word scroll = (word)(shift & 0377);
    word base_offset = (word)(((scroll - 0330) & 0377) * 0100);
    if (vram_size) base_offset %= vram_size;
    for (int y = 0; y < BK_SCREEN_HEIGHT; ++y) {
        for (int x = 0; x < BK_SCREEN_WIDTH; ++x) {
            word line_offset = (word)(y * line_bytes);
            word byte_index = (word)(base_offset + line_offset + (x >> 3));
            if (vram_size) byte_index %= vram_size;
            byte mask = (byte)(1 << (x & 7));
            byte bit = vram[byte_index] & mask;
            src[y * BK_SCREEN_WIDTH + x] = bit ? 0xFFFFFFFFu : 0x00000000u;
        }
    }

    // Scale the source bitmap into the XImage buffer using the pre‑computed scale_factor.
    uint32_t *dst = (uint32_t *)ximage->data;
    int dst_width = BK_SCREEN_WIDTH * scale_factor;
    int dst_height = BK_SCREEN_HEIGHT * scale_factor;
    for (int y = 0; y < BK_SCREEN_HEIGHT; ++y) {
        for (int sy = 0; sy < scale_factor; ++sy) {
            int dy = y * scale_factor + sy;
            for (int x = 0; x < BK_SCREEN_WIDTH; ++x) {
                uint32_t col = src[y * BK_SCREEN_WIDTH + x];
                for (int sx = 0; sx < scale_factor; ++sx) {
                    int dx = x * scale_factor + sx;
                    dst[dy * dst_width + dx] = col;
                }
            }
        }
    }
    free(src);

    XPutImage(display, window, gc, ximage, 0, 0, 0, 0,
               dst_width, dst_height);
    XFlush(display);
}

int x11_gui_handle_events(void)
{
    while (XPending(display)) {
        XEvent ev;
        XNextEvent(display, &ev);
        if (ev.type == ClientMessage) {
            // Window manager close request.
            return 1;
        }
        if (ev.type == DestroyNotify) {
            return 1;
        }
        if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            int code = translate_key(ks, ev.xkey.state);
            if (code) {
                // Forward the key code to the emulator using the same API as
                // the SDL backend – the core provides `bk_hw_handle_key` for
                // this purpose.
                bk_hw_handle_key(code);
            }
        }
    }
    return 0;
}

void x11_gui_cleanup(void)
{
    if (ximage) {
        if (ximage->data) free(ximage->data);
        XDestroyImage(ximage);
        ximage = NULL;
    }
    if (display) {
        XCloseDisplay(display);
        display = NULL;
    }
}
