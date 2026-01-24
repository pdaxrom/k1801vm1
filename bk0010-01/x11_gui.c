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
    int code = 0;

    /* Ctrl‑modified shortcuts. */
    if (state & ControlMask) {
        switch (ks) {
            case XK_g: code = 0007; break;   /* BEL */
            case XK_m: code = 0015; break;   /* set tab stop */
            case XK_p: code = 0020; break;   /* repeat */
            case XK_r: code = 0022; break;   /* cursor home */
            case XK_t: code = 0024; break;   /* clear screen */
            case XK_u: code = 0025; break;   /* line delete */
            case XK_F11: return 0402; break; /* Ctrl+F11 → BK_KEY_PADD */
            case XK_F12: return 0401; break; /* Ctrl+F12 → BK_KEY_FREQ */
            default: break;
        }
        if (code) return code;
    }

    /* Regular keys. */
    switch (ks) {
        case XK_Escape:   return 0400; break;   /* ESC → BK_KEY_STOP */
        case XK_BackSpace:code = 030; break;
        case XK_Return:   code = 012; break;
        case XK_Tab:      code = 011; break;
        case XK_Left:     code = 010; break;
        case XK_Right:    code = 031; break;
        case XK_Up:       code = 032; break;
        case XK_Down:     code = 033; break;
        case XK_Home:     code = 023; break;
        case XK_F1:  code = 0201; break; /* povt */
        case XK_F2:  code = 003;  break; /* kt */
        case XK_F3:  code = 0213; break; /* -|--> */
        case XK_F4:  code = 026;  break; /* |<--- */
        case XK_F5:  code = 027;  break; /* |---> */
        case XK_F6:  code = 0202; break; /* ind su */
        case XK_F7:  code = 0204; break; /* blk red */
        case XK_F8:  code = 0200; break; /* shag */
        case XK_F9:  code = 014;  break; /* sbr */
        case XK_F10: code = 0016; break; /* RUS */
        case XK_F11: code = 0017; break; /* LAT */
        default: break;
    }

    /* Numeric keys. */
    if (!code && ks >= XK_0 && ks <= XK_9) {
        static const char shifted[] = ")!@#$%^&*(";
        char c = (char)('0' + (ks - XK_0));
        if (state & ShiftMask) c = shifted[ks - XK_0];
        code = (int)c;
    }
    /* Alphabetic keys. */
    if (!code && ks >= XK_a && ks <= XK_z) {
        char c = (char)('a' + (ks - XK_a));
        if (state & ShiftMask) c = (char)('A' + (ks - XK_a));
        code = (int)c;
    }

    if (!code) {
        switch(ks) {
            case XK_space: code = ' '; break;
            case XK_minus: code = (state & ShiftMask) ? '_' : '-'; break;
            case XK_equal: code = (state & ShiftMask) ? '+' : '='; break;
            case XK_bracketleft: code = (state & ShiftMask) ? '{' : '['; break;
            case XK_bracketright: code = (state & ShiftMask) ? '}' : ']'; break;
            case XK_semicolon: code = (state & ShiftMask) ? ':' : ';'; break;
            case XK_apostrophe: code = (state & ShiftMask) ? '\"' : '\''; break;
            case XK_comma: code = (state & ShiftMask) ? '<' : ','; break;
            case XK_period: code = (state & ShiftMask) ? '>' : '.'; break;
            case XK_slash: code = (state & ShiftMask) ? '?' : '/'; break;
            case XK_backslash: code = (state & ShiftMask) ? '|' : '\\'; break;
            case XK_grave: code = (state & ShiftMask) ? '~' : '`'; break;
            default: break;
        }
    }

    /* If Alt (left or right) is held, add 0200 to the result. */
    if (code && (state & Mod1Mask)) {
        code |= 0200;
    }

    return code;
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
    if (XPending(display)) {
        XEvent ev;
        XNextEvent(display, &ev);
        if (ev.type == ClientMessage) {
            // Window manager close request.
            return -1;
        }
        if (ev.type == DestroyNotify) {
            return -2;
        }
        if (ev.type == KeyPress) {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            int code = translate_key(ks, ev.xkey.state);
            return code;
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
