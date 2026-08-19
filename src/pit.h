#ifndef PIT_H
#define PIT_H

#include <stdint.h>
#include "idt.h"

void pit_install(uint32_t hz);
uint32_t pit_get_ticks(void);
void pit_sleep(uint32_t ms);

#endif
