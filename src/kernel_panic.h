#ifndef KERNEL_PANIC_H
#define KERNEL_PANIC_H
/*
 * kernel_panic.h — atmkoala v0.5
 *
 * Full kernel panic screen (inspired by Linux + BSD):
 *   - Full-screen red/green panic display in VBE
 *   - Stack trace (EIP chain)
 *   - Register dump
 *   - Memory info at time of crash
 *   - Panic reason + file + line
 *   - "Press any key to reboot" option
 *
 * Usage:
 *   PANIC("Something went wrong");
 *   PANIC_FMT("Bad ptr: 0x%x", ptr);
 *   ASSERT(ptr != NULL, "Null pointer");
 */
#include "idt.h"
#include <stdint.h>

/* ── Macros ───────────────────────────────────────────────── */
#define PANIC(msg) \
    kernel_panic((msg), __FILE__, __LINE__, __func__, NULL)

#define PANIC_FMT(fmt, ...) do { \
    char _pbuf[128]; \
    ksnprintf(_pbuf, 128, fmt, ##__VA_ARGS__); \
    kernel_panic(_pbuf, __FILE__, __LINE__, __func__, NULL); \
} while(0)

#define ASSERT(cond, msg) do { \
    if (!(cond)) kernel_panic("Assert failed: " msg, \
        __FILE__, __LINE__, __func__, NULL); \
} while(0)

#define ASSERT_NOT_NULL(ptr, msg) \
    ASSERT((ptr) != NULL, msg)

/* ── API ──────────────────────────────────────────────────── */

/* Main panic — called by PANIC macro */
__attribute__((noreturn))
void kernel_panic(const char *msg,
                  const char *file,
                  int         line,
                  const char *func,
                  registers_t *regs);   /* NULL = capture from stack */

/* Called by IDT on unhandled CPU exception */
__attribute__((noreturn))
void kernel_panic_exception(registers_t *regs);

/* Non-fatal diagnostic screen. It returns after a keypress and restores Exp. */
void kernel_oops(const char *msg, const char *file, int line,
                 const char *func, registers_t *regs);

/* Install panic hooks (call from kernel_main) */
void panic_init(void);

/* EDN — Extended Debug Notice (non-fatal warning with stack) */
void edn_warn(const char *msg, const char *file, int line);

#define EDN(msg)     edn_warn((msg), __FILE__, __LINE__)
#define EDN_FMT(fmt, ...) do { \
    char _ebuf[128]; ksnprintf(_ebuf,128,fmt,##__VA_ARGS__); \
    edn_warn(_ebuf, __FILE__, __LINE__); \
} while(0)

#endif
