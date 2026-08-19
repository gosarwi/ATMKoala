#ifndef ATM_USERMODE_H
#define ATM_USERMODE_H

#include <stdint.h>
#include "paging.h"

typedef struct {
    user_space_t *space;
    uint64_t entry;
    uint64_t stack_top;
    uint64_t kernel_stack_top;
} atm_user_context_t;

void usermode_init(void);
int usermode_gate_ready(void);
__attribute__((noreturn)) void usermode_enter(atm_user_context_t *ctx);
/* Diagnostic-only: enters CPL 3, executes ABI_INFO via int 0x80, then loops. */
__attribute__((noreturn)) void usermode_selftest_enter(void);

#endif
