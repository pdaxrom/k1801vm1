#ifndef UJ11_KEYBOARD_TEST_H
#define UJ11_KEYBOARD_TEST_H
unsigned keyboard_scan(void);
void keyboard_send(unsigned ch);
extern unsigned keyboard_ready;
#define KEY_SCAN keyboard_scan()
#define KEY_RX keyboard_ready
#define KEY_SEND(ch) keyboard_send(ch)
#endif
