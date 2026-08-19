/* pit.c — atmkoala v0.5 */
#include "pit.h"
#include "util.h"
#include "idt.h"
#include "sched.h"
#include <stdint.h>

#define PIT_BASE_FREQ  1193182UL
#define PIT_CMD        0x43
#define PIT_CH0_DATA   0x40

static volatile uint32_t pit_ticks = 0;
static uint32_t pit_hz = 100;

static void pit_irq_handler(registers_t *regs) {
    (void)regs;
    pit_ticks++;
    sched_tick();   /* FIX v8: вызываем sched_tick → uptime_ticks++ */
}

void pit_install(uint32_t hz) {
    if (hz == 0) hz = 100;
    pit_hz  = hz;
    pit_ticks = 0;
    uint32_t divisor = PIT_BASE_FREQ / hz;
    if (divisor == 0)     divisor = 1;
    if (divisor > 0xFFFF) divisor = 0xFFFF;
    outb(PIT_CMD,      0x36);
    outb(PIT_CH0_DATA, (uint8_t)(divisor & 0xFF));
    outb(PIT_CH0_DATA, (uint8_t)((divisor >> 8) & 0xFF));
    irq_install_handler(0, pit_irq_handler);
}

uint32_t pit_get_ticks(void) { return pit_ticks; }

void pit_sleep(uint32_t ms) {
    if (pit_hz == 0) return;
    uint32_t wait = (ms * pit_hz) / 1000;
    if (wait == 0) wait = 1;
    uint32_t start = pit_ticks;
    while ((pit_ticks - start) < wait)
        __asm__ volatile("pause");
}
