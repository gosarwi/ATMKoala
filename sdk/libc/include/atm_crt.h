#ifndef ATM_CRT_H
#define ATM_CRT_H

#include <stdint.h>

/* ATM static-native process entry stack, v0.9.
 * At the initial RSP: argc, argv[0..argc-1], NULL, envp[0..n-1], NULL,
 * then auxiliary-vector type/value pairs. The current launcher provides
 * argc=1 with argv[0] set to the bounded spawn name, an empty envp, and
 * AT_PAGESZ, AT_ENTRY and AT_NULL. */
#define ATM_AUXV_AT_NULL    0ULL
#define ATM_AUXV_AT_PAGESZ  6ULL
#define ATM_AUXV_AT_ENTRY   9ULL

__attribute__((noreturn)) void __atm_libc_start_main(uint64_t *stack);

#endif
