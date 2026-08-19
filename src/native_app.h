#ifndef ATM_NATIVE_APP_H
#define ATM_NATIVE_APP_H

#include <stdint.h>

/* ATM Native App ABI v1 launcher.
 * Only static x86-64 ET_EXEC images accepted by elf64_load_user() may be
 * started. A positive return value is the new task PID; a negative value
 * means that the image was rejected or resources could not be allocated. */
int native_app_spawn_memory(const char *name, const uint8_t *image,
                            uint32_t image_size, uint32_t priority);
int native_app_spawn_path(const char *path, const char *name,
                          uint32_t priority);
/* Runs a generated static ELF64 probe in CPL 3; returns 0 only if its
 * expected exit status is observed and reaped by the parent task. */
int native_app_selftest(void);
/* Runs the embedded C application built with sdk/libc CRT and returns 0 only
 * after its expected status is reaped through the native process lifecycle. */
int native_app_libc_selftest(void);

#endif
