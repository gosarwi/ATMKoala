#ifndef ATM_CRT_H
#define ATM_CRT_H

#include <stdint.h>

/* ATM static-native process entry layout, v0.9.
 * The loader currently provides argc=0, argv=NULL, envp=NULL and an auxv
 * terminator. CRT keeps the System V-aligned stack contract while leaving
 * future execve argument marshalling backward-compatible. */
typedef struct atm_start_stack {
    uint64_t argc;
    char   **argv;
    char   **envp;
    uint64_t auxv_type;
    uint64_t auxv_value;
} atm_start_stack_t;

__attribute__((noreturn)) void __atm_libc_start_main(uint64_t *stack);

#endif
