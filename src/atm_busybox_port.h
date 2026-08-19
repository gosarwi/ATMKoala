#ifndef ATM_BUSYBOX_PORT_H
#define ATM_BUSYBOX_PORT_H

/*
 * ATMKoala BusyBox-oriented native port contract v1.
 *
 * This header is for a source-adapted, statically linked BusyBox profile.
 * It does NOT make Linux BusyBox ELF binaries ABI-compatible with ATMKoala.
 */
#include "atm_posix.h"

#define ATM_BB_PORT_ABI_MAJOR 1
#define ATM_BB_PORT_ABI_MINOR 1

/* Available VFS/runtime facilities. */
#define ATM_BB_HAVE_OPENAT      0
#define ATM_BB_HAVE_POSIX_DIR   1
#define ATM_BB_HAVE_CWD         1
#define ATM_BB_HAVE_ACCESS      1
#define ATM_BB_HAVE_UMASK       1
#define ATM_BB_HAVE_TTY         1
#define ATM_BB_HAVE_LINKS       1
#define ATM_BB_HAVE_PROCFS      1
#define ATM_BB_HAVE_ENV         1
#define ATM_BB_HAVE_IOV         1
#define ATM_BB_HAVE_FSYNC       1
#define ATM_BB_HAVE_TASK_CWD    1

/* Deferred until user ELF tasks, process isolation and IPC are complete. */
#define ATM_BB_HAVE_FORK        0
#define ATM_BB_HAVE_EXECVE      0
#define ATM_BB_HAVE_WAITPID     0
#define ATM_BB_HAVE_SIGNALS     0
#define ATM_BB_HAVE_PIPES       0
#define ATM_BB_HAVE_LINUX_IOCTL 0
#define ATM_BB_HAVE_SOCKETS     0
#define ATM_BB_HAVE_MOUNT       0
#define ATM_BB_HAVE_MODULES     0

/* Initial safe applet profile: portable, file/stream/text primitives only. */
#define ATM_BB_PROFILE_COREUTILS 1
#define ATM_BB_PROFILE_SHELL     0
#define ATM_BB_PROFILE_NETWORK   0
#define ATM_BB_PROFILE_INIT      0
#define ATM_BB_PROFILE_LINUXDEV  0

#endif
