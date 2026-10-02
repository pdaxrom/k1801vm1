#ifndef UJ11_KEYBOARD_H
#define UJ11_KEYBOARD_H
#ifdef UJ11_KEYBOARD
#define BUS_KEYBOARD_PENDING 0x10u
/* Only the idle main scheduler calls this; disk/paint poll_io never drains FIFO.
 * Bit 4 of BUS_STATUS announces raw input without another MMIO read. */
void keyboard_service(unsigned events);
void keyboard_reset(void);
extern unsigned char keyboard_output_pending;
static inline __attribute__((always_inline)) void keyboard_poll(unsigned events)
{
	if ((events & BUS_KEYBOARD_PENDING) || keyboard_output_pending) {
		keyboard_service(events);
	}
}
#else
#define keyboard_service(events) ((void)0)
#define keyboard_reset() ((void)0)
#define keyboard_poll(events) ((void)0)
#endif
#endif
