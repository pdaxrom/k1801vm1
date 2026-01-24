/* Minimal X11 GUI backend for BK0010-01 emulator.
 * Provides only the functions needed by main.c when compiled with
 * -DUSE_X11.  The implementation lives in x11_gui.c.
 */

#ifndef X11_GUI_H
#define X11_GUI_H

/* Initialise the X11 display and create a window.
 * Returns 0 on success, non‑zero on failure.
 */
int x11_gui_init(void);

/* Draw the current VRAM contents to the X11 window.
 * Called once per frame.
 */
void x11_gui_draw(void);

/* Process pending X11 events.
 * Returns 1 if a quit request was received, otherwise 0.
 */
int x11_gui_handle_events(void);

/* Release X11 resources.
 */
void x11_gui_cleanup(void);

#endif /* X11_GUI_H */

