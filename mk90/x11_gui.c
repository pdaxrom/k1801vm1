/* Minimal X11 backend for the MK-90 emulator.
 * Provides a thin wrapper used by main.c when compiled with -DUSE_X11.
 * Handles window creation, scaled rendering of the pre-rendered pixel
 * buffer, and keyboard / quit event dispatch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>

#include "x11_gui.h"

/* MK-90 scan-code table – mirrors mk90_keytab[] in main.c. */
static const unsigned int mk90_keytab[64] = {
    000000u, 000000u, 000000u,
    000043u, 000103u, 000143u, 000203u, 000243u, 000303u, 000343u,
    000047u, 000107u, 000147u, 000207u, 000247u, 000307u, 000347u,
    000013u, 000053u, 000113u, 000153u, 000213u, 000253u, 000313u, 000353u,
    000017u, 000057u, 000117u, 000157u, 000217u, 000257u, 000317u, 000357u,
    000023u, 000063u, 000123u, 000163u, 000223u, 000263u, 000323u, 000363u,
    000027u, 000067u, 000127u, 000167u, 000227u, 000267u, 000327u, 000367u,
    000033u, 000073u, 000133u, 000173u, 000233u, 000273u, 000333u, 000373u,
    000037u, 000077u, 000137u, 000177u, 000277u, 000337u, 000377u
};

static Display *display      = NULL;
static Window   window;
static GC       gc;
static XImage  *ximage       = NULL;
static int      screen_num;
static int      scale_factor = 1;
static int      win_width    = 0;
static int      win_height   = 0;
/* Keycode of the last pressed key – used to suppress X11 autorepeat. */
static KeyCode  last_pressed_keycode = 0;

/* Map an X11 KeySym to the corresponding MK-90 scan code.
 * Key positions mirror mk90_translate_special_key(),
 * mk90_translate_numpad_key(), and mk90_translate_pc_printable_key()
 * from main.c, using unshifted (level-0) keysyms. */
static unsigned int translate_keysym(KeySym ks)
{
    switch (ks) {
    /* --- Special / modifier keys --- */
    case XK_Tab:
    case XK_F6:
    case XK_Control_L:
    case XK_Control_R:          return mk90_keytab[49];
    case XK_Up:                 return mk90_keytab[50];
    case XK_Left:               return mk90_keytab[51];
    case XK_Right:              return mk90_keytab[54];
    case XK_BackSpace:
    case XK_Delete:             return mk90_keytab[55];
    case XK_Return:
    case XK_KP_Enter:           return mk90_keytab[56];
    case XK_Home:
    case XK_F7:                 return mk90_keytab[57];
    case XK_Down:               return mk90_keytab[58];
    case XK_End:
    case XK_Next:               return mk90_keytab[59]; /* Page Down */
    case XK_space:              return mk90_keytab[60];
    case XK_Insert:
    case XK_Prior:              return mk90_keytab[61]; /* Page Up */
    case XK_Alt_L:
    case XK_Alt_R:
    case XK_F8:                 return mk90_keytab[62];
    case XK_Shift_L:
    case XK_Shift_R:
    case XK_F9:                 return mk90_keytab[63];

    /* --- Numeric keypad (both NumLock-on digit and NumLock-off cursor) --- */
    case XK_KP_1: case XK_KP_End:      return mk90_keytab[3];
    case XK_KP_2: case XK_KP_Down:     return mk90_keytab[4];
    case XK_KP_3: case XK_KP_Next:     return mk90_keytab[5];
    case XK_KP_4: case XK_KP_Left:     return mk90_keytab[6];
    case XK_KP_5: case XK_KP_Begin:    return mk90_keytab[7];
    case XK_KP_6: case XK_KP_Right:    return mk90_keytab[10];
    case XK_KP_7: case XK_KP_Home:     return mk90_keytab[11];
    case XK_KP_8: case XK_KP_Up:       return mk90_keytab[12];
    case XK_KP_9: case XK_KP_Prior:    return mk90_keytab[13];
    case XK_KP_0: case XK_KP_Insert:   return mk90_keytab[14];
    case XK_KP_Divide:                  return mk90_keytab[15];
    case XK_KP_Subtract:                return mk90_keytab[16];
    case XK_KP_Decimal:
    case XK_KP_Delete:                  return mk90_keytab[53];

    /* --- Digit row --- */
    case XK_grave:              return mk90_keytab[47]; /* @ */
    case XK_1:                  return mk90_keytab[3];
    case XK_2:                  return mk90_keytab[4];
    case XK_3:                  return mk90_keytab[5];
    case XK_4:                  return mk90_keytab[6];
    case XK_5:                  return mk90_keytab[7];
    case XK_6:                  return mk90_keytab[10];
    case XK_7:                  return mk90_keytab[11];
    case XK_8:                  return mk90_keytab[12];
    case XK_9:                  return mk90_keytab[13];
    case XK_0:                  return mk90_keytab[14];
    case XK_minus:              return mk90_keytab[16];
    case XK_equal:              return mk90_keytab[40]; /* ^ */

    /* --- Top letter row (QWERTY physical positions) --- */
    case XK_q: case XK_Q:      return mk90_keytab[48];
    case XK_w: case XK_W:      return mk90_keytab[19];
    case XK_e: case XK_E:      return mk90_keytab[22];
    case XK_r: case XK_R:      return mk90_keytab[33];
    case XK_t: case XK_T:      return mk90_keytab[35];
    case XK_y: case XK_Y:      return mk90_keytab[44];
    case XK_u: case XK_U:      return mk90_keytab[36];
    case XK_i: case XK_I:      return mk90_keytab[25];
    case XK_o: case XK_O:      return mk90_keytab[31];
    case XK_p: case XK_P:      return mk90_keytab[32];
    case XK_bracketleft:        return mk90_keytab[41];
    case XK_bracketright:       return mk90_keytab[42];
    case XK_backslash:          return mk90_keytab[46];

    /* --- Middle letter row --- */
    case XK_a: case XK_A:      return mk90_keytab[17];
    case XK_s: case XK_S:      return mk90_keytab[34];
    case XK_d: case XK_D:      return mk90_keytab[21];
    case XK_f: case XK_F:      return mk90_keytab[37];
    case XK_g: case XK_G:      return mk90_keytab[20];
    case XK_h: case XK_H:      return mk90_keytab[38];
    case XK_j: case XK_J:      return mk90_keytab[26];
    case XK_k: case XK_K:      return mk90_keytab[27];
    case XK_l: case XK_L:      return mk90_keytab[28];
    case XK_semicolon:          return mk90_keytab[8];
    case XK_apostrophe:         return mk90_keytab[9];

    /* --- Bottom letter row --- */
    case XK_z: case XK_Z:      return mk90_keytab[24];
    case XK_x: case XK_X:      return mk90_keytab[43];
    case XK_c: case XK_C:      return mk90_keytab[39];
    case XK_v: case XK_V:      return mk90_keytab[23];
    case XK_b: case XK_B:      return mk90_keytab[18];
    case XK_n: case XK_N:      return mk90_keytab[30];
    case XK_m: case XK_M:      return mk90_keytab[29];
    case XK_comma:              return mk90_keytab[52];
    case XK_period:             return mk90_keytab[53];
    case XK_slash:              return mk90_keytab[15];

    default:                    return 0;
    }
}

int x11_gui_init(int width, int height, int scale)
{
    Atom wm_delete;

    display = XOpenDisplay(NULL);
    if (!display) {
        fprintf(stderr, "mk90: XOpenDisplay failed\n");
        return -1;
    }
    screen_num = DefaultScreen(display);

    if (scale >= 1) {
        scale_factor = scale;
    } else {
        /* Auto-scale so the window fills at least half the screen. */
        int screen_w = DisplayWidth(display, screen_num);
        int screen_h = DisplayHeight(display, screen_num);
        int sf_w = (screen_w / 2 + width  - 1) / width;
        int sf_h = (screen_h / 2 + height - 1) / height;
        scale_factor = sf_w > sf_h ? sf_w : sf_h;
        if (scale_factor < 1) {
            scale_factor = 1;
        }
    }

    win_width  = width  * scale_factor;
    win_height = height * scale_factor;

    window = XCreateSimpleWindow(display, RootWindow(display, screen_num),
                                 0, 0,
                                 (unsigned int)win_width,
                                 (unsigned int)win_height,
                                 1,
                                 BlackPixel(display, screen_num),
                                 WhitePixel(display, screen_num));
    XStoreName(display, window, "MK-90");
    XSelectInput(display, window,
                 ExposureMask | KeyPressMask | KeyReleaseMask |
                 FocusChangeMask | StructureNotifyMask);
    /* Suppress synthetic KeyRelease+KeyPress pairs during autorepeat. */
    XkbSetDetectableAutoRepeat(display, True, NULL);

    /* Receive WM_DELETE_WINDOW so closing the window triggers a quit. */
    wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, window, &wm_delete, 1);

    XMapWindow(display, window);
    gc = XCreateGC(display, window, 0, NULL);

    ximage = XCreateImage(display,
                          DefaultVisual(display, screen_num),
                          (unsigned int)DefaultDepth(display, screen_num),
                          ZPixmap, 0, NULL,
                          (unsigned int)win_width,
                          (unsigned int)win_height,
                          32, 0);
    if (!ximage) {
        fprintf(stderr, "mk90: XCreateImage failed\n");
        return -1;
    }
    ximage->data = malloc((size_t)ximage->bytes_per_line * (size_t)win_height);
    if (!ximage->data) {
        fprintf(stderr, "mk90: malloc for XImage data failed\n");
        XDestroyImage(ximage);
        ximage = NULL;
        return -1;
    }
    return 0;
}

void x11_gui_draw(const uint32_t *pixels, int width, int height)
{
    uint32_t *dst;
    int y, x, sy, sx;

    if (!pixels || !ximage) {
        return;
    }

    dst = (uint32_t *)ximage->data;

    for (y = 0; y < height; y++) {
        for (sy = 0; sy < scale_factor; sy++) {
            int dy = y * scale_factor + sy;
            for (x = 0; x < width; x++) {
                uint32_t col = pixels[y * width + x];
                for (sx = 0; sx < scale_factor; sx++) {
                    int dx = x * scale_factor + sx;
                    dst[dy * win_width + dx] = col;
                }
            }
        }
    }

    XPutImage(display, window, gc, ximage,
              0, 0, 0, 0,
              (unsigned int)win_width, (unsigned int)win_height);
    XFlush(display);
}

int x11_gui_handle_events(unsigned int *scan_code_out, int *keycode_out)
{
    XEvent ev;

    *scan_code_out = 0;
    *keycode_out   = 0;

    if (!XPending(display)) {
        return MK90_X11_EVENT_NONE;
    }

    XNextEvent(display, &ev);

    if (ev.type == ClientMessage || ev.type == DestroyNotify) {
        return MK90_X11_EVENT_QUIT;
    }

    if (ev.type == FocusOut) {
        /* Release any held key when the window loses focus. */
        if (last_pressed_keycode != 0) {
            *keycode_out = (int)last_pressed_keycode;
            last_pressed_keycode = 0;
            return MK90_X11_EVENT_RELEASE;
        }
        return MK90_X11_EVENT_NONE;
    }

    if (ev.type == KeyRelease) {
        *keycode_out = (int)ev.xkey.keycode;
        if (ev.xkey.keycode == last_pressed_keycode) {
            last_pressed_keycode = 0;
        }
        return MK90_X11_EVENT_RELEASE;
    }

    if (ev.type == KeyPress) {
        /* Suppress autorepeat: ignore if this keycode is still considered held. */
        if (ev.xkey.keycode == last_pressed_keycode) {
            return MK90_X11_EVENT_NONE;
        }
        last_pressed_keycode = ev.xkey.keycode;

        {
            KeySym ks = XLookupKeysym(&ev.xkey, 0);
            if (ks == XK_Escape) {
                return MK90_X11_EVENT_QUIT;
            }
            *scan_code_out = translate_keysym(ks);
            *keycode_out   = (int)ev.xkey.keycode;
        }
        return MK90_X11_EVENT_PRESS;
    }

    return MK90_X11_EVENT_NONE;
}

void x11_gui_cleanup(void)
{
    if (ximage) {
        free(ximage->data);
        ximage->data = NULL;
        XDestroyImage(ximage);
        ximage = NULL;
    }
    if (display) {
        XkbSetDetectableAutoRepeat(display, False, NULL);
        XCloseDisplay(display);
        display = NULL;
    }
}
