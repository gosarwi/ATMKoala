#ifndef MOUSE_H
#define MOUSE_H

#include <stdint.h>

/* Minimal PS/2 relative mouse state in framebuffer coordinates. */
typedef struct {
    volatile int x;
    volatile int y;
    volatile uint8_t buttons;   /* bit 0=left, bit 1=right, bit 2=middle */
    volatile int available;
    volatile uint32_t packets;
    volatile uint32_t dropped_packets;
    volatile uint8_t init_status;
} mouse_state_t;

/* Safe to call when no mouse is present: available remains zero. */
void mouse_init(int screen_w, int screen_h);
const mouse_state_t *mouse_state(void);
const char *mouse_status_string(void);

#endif
