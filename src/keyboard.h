#ifndef KEYBOARD_H
#define KEYBOARD_H
#include <stdint.h>

/* ── Extended key codes (> 0x7F) ──────────────────────────── */
#define KEY_UP        0x100
#define KEY_DOWN      0x101
#define KEY_LEFT      0x102
#define KEY_RIGHT     0x103
#define KEY_PGUP      0x104
#define KEY_PGDN      0x105
#define KEY_HOME      0x106
#define KEY_END       0x107
#define KEY_DEL       0x108
#define KEY_INS       0x109
#define KEY_F1        0x10A
#define KEY_F2        0x10B
#define KEY_F3        0x10C
#define KEY_F4        0x10D
#define KEY_F5        0x10E
#define KEY_F6        0x10F
#define KEY_F7        0x110
#define KEY_F8        0x111
#define KEY_F9        0x112
#define KEY_F10       0x113
#define KEY_F11       0x114
#define KEY_F12       0x115

/* ── ASCII aliases ────────────────────────────────────────── */
#define KEY_ESC       0x1B
#define KEY_ENTER     0x0D
#define KEY_BACKSPACE 0x08
#define KEY_TAB       0x09

/* ── Russian layout toggle ────────────────────────────────── */
/* Alt+Shift switches EN<->RU layout */

void keyboard_init(void);

/*
 * keyboard_getkey() returns:
 *   - ASCII (0x01..0x7E) for English chars
 *   - KEY_* (>=0x100) for special keys
 *   - For Russian (UTF-8): returns KEY_UTF8 and fills keyboard_utf8_buf[]
 *     with the 2-byte UTF-8 sequence of the Cyrillic character.
 *     Caller should write those bytes to output.
 */
#define KEY_UTF8  0x200
extern char keyboard_utf8_buf[4];  /* filled when KEY_UTF8 returned */

int  keyboard_getkey(void);
int  keyboard_poll(void);
int  keyboard_shift(void);
int  keyboard_ctrl(void);
int  keyboard_alt(void);
int  keyboard_caps(void);
int  keyboard_ru(void);   /* 1 = Russian layout active */

#endif
