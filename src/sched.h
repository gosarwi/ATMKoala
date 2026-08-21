#ifndef SCHED_H
#define SCHED_H

#include <stdint.h>
#include <stddef.h>

#define TASK_MAX        16
#define TASK_STACK_SIZE 4096
#define TASK_CWD_MAX    1024
#define TASK_FD_MAX     32
#define TASK_NO_PARENT  0xFFFFFFFFu
#define TASK_WAIT_ANY   0xFFFFFFFFu
#define TASK_WAIT_NOHANG 0x00000001u
#define TASK_ERR_ECHILD   10
#define TASK_ERR_EINVAL   22

/* POSIX-compatible wait-status subset.  Normal task_exit(code) produces
 * (code & 0xff) << 8; task_send_signal(pid, sig) produces sig & 0x7f. */
#define TASK_WIFEXITED(status) (((status) & 0x7f) == 0)
#define TASK_WEXITSTATUS(status) (((status) >> 8) & 0xff)
#define TASK_WIFSIGNALED(status) (((status) & 0x7f) != 0)
#define TASK_WTERMSIG(status) ((status) & 0x7f)

typedef enum {
    TASK_UNUSED  = 0,
    TASK_READY   = 1,
    TASK_RUNNING = 2,
    TASK_BLOCKED = 3,
    TASK_ZOMBIE  = 4,
} task_state_t;

/* Saved CPU registers for context switch (x86-64) */
typedef struct {
    uint64_t r15, r14, r13, r12;
    uint64_t rbp, rbx;
    uint64_t rsp;
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr3;
} cpu_context_t;

typedef void (*task_fn_t)(void);

typedef struct task {
    uint32_t      pid;
    uint32_t      ppid;         /* creator PID; process hierarchy foundation */
    uint32_t      uid, gid;     /* credentials snapshot at task creation */
    char          cwd[TASK_CWD_MAX]; /* task-local POSIX working directory */
    uint32_t      umask_value;  /* task-local POSIX creation mask */
    char          name[32];
    task_state_t  state;
    cpu_context_t ctx;
    void         *stack_base;   /* allocated stack */
    uint32_t      stack_size;
    uint64_t      ticks;        /* CPU PIT ticks consumed while RUNNING */
    uint64_t      created_ticks;
    uint64_t      state_changed_ticks;
    uint64_t      resident_bytes; /* kernel stack + mapped native user pages */
    uint64_t      io_read_bytes;  /* completed native FD reads */
    uint64_t      io_write_bytes; /* completed native FD writes */
    uint32_t      context_switches;
    uint32_t      priority;     /* 1=low, 5=normal, 10=high */
    int           exit_code;
    uint32_t      exit_signal;    /* nonzero if task ended through task_send_signal */
    uint32_t      pending_signal; /* SIGTERM/SIGKILL foundation */
    uint32_t      wait_target;    /* child PID or TASK_WAIT_ANY while blocked in waitpid */
    uint32_t      wait_active;
    void         *address_space;  /* user_space_t for native CPL 3 task, else NULL */
    uint64_t      user_entry;     /* static ELF64 entry point for a native task */
    uint64_t      user_stack_top; /* prepared System V-like initial user RSP */
    uint64_t      user_brk_base;  /* first address available after ELF image */
    uint64_t      user_brk;       /* current logical process break */
    uint64_t      linux_fs_base;  /* Linux arch_prctl(ARCH_SET_FS) state */
    uint64_t      linux_clear_tid;/* Linux set_tid_address() clear-on-exit cell */
    int           fd_map[TASK_FD_MAX]; /* native user fd → backend VFS fd */
    int           socket_map[TASK_FD_MAX]; /* native user fd → kernel TCP socket slot */
    int           pipe_map[TASK_FD_MAX]; /* native user fd → pipe slot/end encoding */
    uint32_t      fd_flags[TASK_FD_MAX]; /* native descriptor status flags, e.g. O_NONBLOCK */
    void         *dir_map[16]; /* native directory handle → kernel DIR_t */
    uint32_t      fd_table_ready;
    struct task  *next;         /* run-queue list */
} task_t;

typedef struct {
    uint32_t pid, ppid, uid, gid;
    task_state_t state;
    uint32_t priority, context_switches;
    uint64_t cpu_ticks, created_ticks, state_changed_ticks;
    uint64_t resident_bytes, io_read_bytes, io_write_bytes;
    char name[32];
} sched_task_info_t;

/* Public API */
void    sched_init(void);
task_t *task_create(const char *name, task_fn_t fn, uint32_t priority);
/* Removes an unstarted READY task after launcher setup failure. The caller
 * remains responsible for any address-space object allocated outside task_create. */
int     task_discard_unstarted(task_t *task);
/* Kernel-internal regression helpers; not exposed through the native ABI. */
task_t *task_lookup_pid(uint32_t pid);
int     task_suspend_unstarted(task_t *task);
void    task_exit(int code);
/* CPL 3 exit path: switches directly to a runnable kernel context and never
 * returns through the current syscall/interrupt frame. */
__attribute__((noreturn)) void task_exit_from_syscall(int code);
/* Terminates a non-current non-idle task. Returns 0 on success. */
int     task_kill(uint32_t pid, int code);
/* pid is a positive child PID or -1 for any direct child. With
 * TASK_WAIT_NOHANG, return 0 while a matching child remains live. Without it,
 * a task sleeps until a matching child exits and then reaps its wait status. */
int     task_waitpid(int32_t pid, int *status, uint32_t options);
int     task_send_signal(uint32_t pid, int sig);
void    task_yield(void);
void    task_block(void);
/* CPL3 syscall sleep path: context-switches directly and later returns into
 * the interrupted syscall call stack after task_unblock(). */
void    task_block_from_syscall(void);
/* Blocks only the current CPL3 task until a PIT deadline. The tick count is
 * already rounded upward by the syscall layer; task metadata remains private. */
int     task_sleep_ticks_from_syscall(uint32_t ticks);
void    task_unblock(task_t *t);
void    sched_tick(void);          /* called from PIT IRQ */
task_t *sched_current(void);
void    sched_print_tasks(void);   /* ps command */
uint32_t sched_uptime_ticks(void);
uint32_t sched_idle_ticks(void);   /* ticks spent in the idle task */
uint32_t sched_task_count(void);   /* live non-idle tasks */
uint64_t sched_total_resident_bytes(void);
uint64_t sched_busy_ticks(void);
int      sched_task_info(uint32_t slot, sched_task_info_t *out);

/* Context switch (implemented in boot.s) */
extern void context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx);

#endif
