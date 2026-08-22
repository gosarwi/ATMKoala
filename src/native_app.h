#ifndef ATM_NATIVE_APP_H
#define ATM_NATIVE_APP_H

#include <stdint.h>
#include "sched.h"
#include "idt.h"

#define ATM_NATIVE_EXEC_MAX_ARGS 16u
#define ATM_NATIVE_EXEC_MAX_ENV  16u
#define ATM_NATIVE_EXEC_STR_BYTES 2048u

typedef struct {
    uint32_t argc;
    uint32_t envc;
    uint32_t bytes;
    uint16_t argv_off[ATM_NATIVE_EXEC_MAX_ARGS];
    uint16_t env_off[ATM_NATIVE_EXEC_MAX_ENV];
    char strings[ATM_NATIVE_EXEC_STR_BYTES];
} native_exec_payload_t;

/* ATM Native App ABI v1 launcher.
 * Only static x86-64 ET_EXEC images accepted by elf64_load_user() may be
 * started. A positive return value is the new task PID; a negative value
 * means that the image was rejected or resources could not be allocated. */
int native_app_spawn_memory(const char *name, const uint8_t *image,
                            uint32_t image_size, uint32_t priority);
/* Kernel-internal spawn variant for controlled tests and launchers. It clones
 * VFS/pipe descriptors but intentionally rejects current single-owner sockets. */
int native_app_spawn_memory_inherit(const char *name,const uint8_t *image,
                                    uint32_t image_size,uint32_t priority);
int native_app_spawn_path(const char *path, const char *name,
                          uint32_t priority);
/* Replaces the current native static image only after path/image/stack setup
 * succeeds. The caller supplies a kernel-owned, fully bounded argv/env payload
 * and the saved CPL3 frame. Static ET_EXEC is still the only accepted image. */
int native_app_exec_current(task_t *task,registers_t *frame,
                            const char *path,const native_exec_payload_t *payload);
/* Runs a generated static ELF64 probe in CPL 3; returns 0 only if its
 * expected exit status is observed and reaped by the parent task. */
int native_app_selftest(void);
/* Runs the embedded C application built with sdk/libc CRT and returns 0 only
 * after its expected status is reaped through the native process lifecycle. */
int native_app_libc_selftest(void);
/* End-to-end static CPL3 child pipe block/wakeup/inherit regression. */
int native_app_pipe_ipc_selftest(void);
/* Internal parent-linked CPL3 waitpid block/wakeup/status-copy regression. */
int native_app_cpl3_wait_selftest(void);
/* CPL3 direct-child SIGKILL and wait-status regression. */
int native_app_cpl3_signal_selftest(void);
/* Linux x86-64 SYSCALL L0 entry and numeric-dispatch regression. */
int native_app_linux_abi_selftest(void);
/* Linux x86-64 v13 descriptor regression for pipe2 and dup3 flag translation. */
int native_app_linux_descriptor_selftest(void);
/* Linux x86-64 v14 process-group/session numeric regression. */
int native_app_linux_session_selftest(void);
/* Linux x86-64 v22 descriptor metadata, positional-vector, signal-0, fchdir, runtime/getrlimit, faccessat and namespace-at regression. */
int native_app_linux_v22_selftest(void);
/* Linux x86-64 L1 brk/mmap/munmap/mprotect regression. */
int native_app_linux_l1_selftest(void);
/* Linux x86-64 L3 openat/getdents64/newfstatat CPL3 regression. */
int native_app_linux_l3_selftest(void);
/* End-to-end CPL3 exec replacement with FD_CLOEXEC close verification. */
int native_app_exec_selftest(void);

#endif
