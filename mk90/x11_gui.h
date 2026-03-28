/* Minimal X11 backend for the MK-90 emulator.
 * Used by main.c when compiled with -DUSE_X11.
 */

#ifndef MK90_X11_GUI_H
#define MK90_X11_GUI_H

#include <stdint.h>

/* Event types returned by x11_gui_handle_events. */
#define MK90_X11_EVENT_NONE    0
#define MK90_X11_EVENT_PRESS   1
#define MK90_X11_EVENT_RELEASE 2
#define MK90_X11_EVENT_QUIT    (-1)

/* Initialise the X11 display and create a window of width×height pixels
 * scaled by scale.  Returns 0 on success, non-zero on failure. */
int  x11_gui_init(int width, int height, int scale);

/* Draw the pre-rendered ARGB pixel buffer (width×height) to the window. */
void x11_gui_draw(const uint32_t *pixels, int width, int height);

/* Process pending X11 events.
 * On MK90_X11_EVENT_PRESS  *scan_code_out and *keycode_out are set.
 * On MK90_X11_EVENT_RELEASE *keycode_out is set, *scan_code_out is 0.
 * Returns MK90_X11_EVENT_NONE when there is nothing to process. */
int  x11_gui_handle_events(unsigned int *scan_code_out, int *keycode_out);

/* Release all X11 resources. */
void x11_gui_cleanup(void);

#endif /* MK90_X11_GUI_H */
