/*  sched.c — Round-robin preemptive task scheduler for atmkoala v0.5
 *
 *  - Fixed pool of TASK_MAX tasks (no dynamic alloc for task structs)
 *  - Each task gets its own 4KB stack (kmalloc'd)
 *  - PIT IRQ0 calls sched_tick() → context_switch() on ASM side
 *  - Priority: higher priority tasks run more ticks per quantum
 */
#include "sched.h"
#include "kmalloc.h"
#include "util.h"
#include "vga.h"
#include "vfs.h"
#include "paging.h"
#include "native_fd.h"
#include <stdint.h>

#define QUANTUM_BASE  2   /* ticks per quantum unit */

static task_t  task_pool[TASK_MAX];
static task_t *current_task  = 0;
static task_t *run_queue_head = 0;   /* circular singly-linked */
static uint32_t next_pid = 1;
static volatile uint32_t uptime_ticks = 0;
static volatile uint32_t idle_ticks = 0;

/* ── Helpers ───────────────────────────────────────────────── */
static task_t *alloc_task_slot(void) {
    for (int i = 0; i < TASK_MAX; i++)
        if (task_pool[i].state == TASK_UNUSED)
            return &task_pool[i];
    return NULL;
}

static int queue_add(task_t *t) {
    if(!t) return -1;
    if (!run_queue_head) {
        run_queue_head = t;
        t->next = t;
        return 0;
    }
    /* Insert before head (tail → new → head). A bounded walk prevents
     * malformed/stale links from converting task creation into a ring-0 hang. */
    task_t *tail = run_queue_head;
    for(int seen=0;seen<TASK_MAX;seen++){
        if(!tail || !tail->next) return -1;
        if(tail->next==run_queue_head){
            tail->next=t;
            t->next=run_queue_head;
            return 0;
        }
        tail=tail->next;
    }
    return -1;
}

static void queue_remove(task_t *t) {
    if (!run_queue_head || !t) return;
    /* Direct CPL 3 exit can reach this path after the single task's link was
     * already cleared by a previous scheduler handoff. Treat both self-link
     * and NULL-link head cases as an empty queue; retaining a zombie head
     * would make sched_tick spin forever after waitpid clears its slot. */
    if (run_queue_head == t && (!t->next || t->next == t)) {
        run_queue_head = NULL;
        t->next = NULL;
        return;
    }
    /* A task may be removed from an IRQ or a syscall exit path. Never trust
     * a malformed/stale next pointer enough to walk forever in ring 0. */
    task_t *prev=run_queue_head;
    for(int seen=0;seen<TASK_MAX;seen++){
        task_t *next=prev->next;
        if(!next) return;
        if(next==t){
            prev->next=t->next;
            if(run_queue_head==t) run_queue_head=t->next;
            t->next=NULL;
            return;
        }
        if(next==run_queue_head) return;
        prev=next;
    }
}

/* ── Init ──────────────────────────────────────────────────── */
void sched_init(void) {
    kmemset(task_pool, 0, sizeof(task_pool));
    current_task   = NULL;
    run_queue_head = NULL;
    next_pid       = 1;
    uptime_ticks   = 0;
    idle_ticks     = 0;

    /* Create idle task (pid=0, runs when nothing else is ready) */
    task_t *idle = &task_pool[0];
    idle->pid      = 0;
    idle->uid      = vfs_current_uid();
    idle->gid      = vfs_current_gid();
    kstrcpy(idle->cwd,"/");
    idle->umask_value = 0022;
    kstrcpy(idle->name, "idle");
    idle->state    = TASK_READY;
    idle->priority = 1;
    idle->stack_base = NULL;  /* uses boot stack */

    /* Current context becomes idle — will be filled on first switch */
    current_task = idle;
    /* Don't put idle in run queue; we call it only when queue empty */
}

/* ── task_create ───────────────────────────────────────────── */
task_t *task_create(const char *name, task_fn_t fn, uint32_t priority) {
    task_t *t = alloc_task_slot();
    if (!t) return NULL;

    void *stack = kmalloc(TASK_STACK_SIZE);
    if (!stack) return NULL;

    t->pid        = next_pid++;
    t->ppid        = current_task ? current_task->pid : 0;
    t->uid         = current_task?current_task->uid:vfs_current_uid();
    t->gid         = current_task?current_task->gid:vfs_current_gid();
    if(current_task){kstrcpy(t->cwd,current_task->cwd);t->umask_value=current_task->umask_value;}
    else {kstrcpy(t->cwd,"/");t->umask_value=0022;}
    kstrcpy(t->name, name);
    t->state      = TASK_READY;
    t->priority   = (priority < 1) ? 1 : (priority > 10) ? 10 : priority;
    t->stack_base = stack;
    t->stack_size = TASK_STACK_SIZE;
    t->ticks      = 0;
    t->exit_code  = 0;

    /* Set up initial context for context_switch (x86-64).
     * context_switch restores: r15,r14,r13,r12,rbp,rbx,rsp,rip,rflags
     * On first entry rsp points to a stack with the task function address,
     * and rip == fn so context_switch jumps directly to fn.
     */
    uint64_t *sp = (uint64_t *)((uint8_t *)stack + TASK_STACK_SIZE);
    *(--sp) = (uint64_t)(uintptr_t)fn;   /* return address if fn ever returns */

    t->ctx.r15    = 0;
    t->ctx.r14    = 0;
    t->ctx.r13    = 0;
    t->ctx.r12    = 0;
    t->ctx.rbp    = 0;
    t->ctx.rbx    = 0;
    t->ctx.rsp    = (uint64_t)(uintptr_t)sp;
    t->ctx.rip    = (uint64_t)(uintptr_t)fn;
    t->ctx.rflags = 0x0000000000000202ULL; /* IF=1 */
    t->ctx.cr3    = paging_kernel_cr3();

    if(queue_add(t)<0){
        kfree(stack);
        kmemset(t,0,sizeof(*t));
        return NULL;
    }
    return t;
}

/* ── task_exit ─────────────────────────────────────────────── */
void task_exit(int code) {
    __asm__ volatile("cli");
    if (!current_task || current_task->pid == 0) return;
    current_task->state     = TASK_ZOMBIE;
    current_task->exit_code = code;
    native_fd_task_cleanup(current_task);
    queue_remove(current_task);
    /* Keep stack and metadata until the parent reaps this zombie. */
    __asm__ volatile("sti");
    task_yield();
}

__attribute__((noreturn)) void task_exit_from_syscall(int code) {
    /* Do not invoke task_yield() here: when called from int 0x80 the CPU has
     * a CPL 3 iret frame on this kernel stack. A nested software IRQ would
     * overwrite the return path and later iretq would restore a dead user
     * context. Select the successor directly and never return to that frame. */
    __asm__ volatile("cli");
    task_t *old=current_task;
    if(!old || old->pid==0) for(;;) __asm__ volatile("hlt");
    old->state=TASK_ZOMBIE;
    old->exit_code=code;
    native_fd_task_cleanup(old);
    queue_remove(old);

    task_t *next=NULL;
    if(run_queue_head){
        task_t *scan=run_queue_head;
        for(int seen=0;seen<TASK_MAX;seen++){
            if(scan->state==TASK_READY){next=scan;break;}
            scan=scan->next;
            if(!scan || scan==run_queue_head) break;
        }
    }
    if(!next) next=&task_pool[0];
    current_task=next;
    next->state=TASK_RUNNING;
    next->ticks++;
    context_switch(&old->ctx,&next->ctx);
    for(;;) __asm__ volatile("hlt");
}

int task_kill(uint32_t pid, int code) {
    if (pid == 0) return -1; /* idle owns the boot/kernel stack */
    __asm__ volatile("cli");
    task_t *target=NULL;
    for (int i=0;i<TASK_MAX;i++) if (task_pool[i].state!=TASK_UNUSED && task_pool[i].pid==pid) { target=&task_pool[i]; break; }
    if (!target || target==current_task) { __asm__ volatile("sti"); return -1; }
    target->state=TASK_ZOMBIE; target->exit_code=code;
    queue_remove(target);
    /* Retain the zombie for waitpid. */
    __asm__ volatile("sti");
    return 0;
}

int task_waitpid(uint32_t pid,int *status) {
    task_t *parent=current_task;
    for(int i=0;i<TASK_MAX;i++){
        task_t *t=&task_pool[i];
        if(t->state==TASK_UNUSED || t->pid!=pid) continue;
        if(parent && t->ppid!=parent->pid) return -1;
        if(t->state!=TASK_ZOMBIE) return 0;
        if(status) *status=t->exit_code;
        uint32_t reaped=t->pid;
        if(t->stack_base) kfree(t->stack_base);
        kmemset(t,0,sizeof(*t));
        return (int)reaped;
    }
    return -1;
}

int task_send_signal(uint32_t pid,int sig) {
    if(pid==0 || (sig!=15 && sig!=9)) return -1;
    for(int i=0;i<TASK_MAX;i++){
        task_t *t=&task_pool[i];
        if(t->state==TASK_UNUSED || t->pid!=pid) continue;
        t->pending_signal=(uint32_t)sig;
        if(t!=current_task) return task_kill(pid,128+sig);
        return 0;
    }
    return -1;
}

/* ── task_yield ────────────────────────────────────────────── */
void task_yield(void) {
    __asm__ volatile("int $0x20");   /* trigger PIT IRQ (INT 32) */
}

/* ── task_block / unblock ──────────────────────────────────── */
void task_block(void) {
    __asm__ volatile("cli");
    if (current_task) {
        current_task->state = TASK_BLOCKED;
        queue_remove(current_task);
    }
    __asm__ volatile("sti");
    task_yield();
}

void task_unblock(task_t *t) {
    if (!t || t->state != TASK_BLOCKED) return;
    t->state = TASK_READY;
    if(queue_add(t)<0) t->state=TASK_BLOCKED;
}

/* ── sched_tick — called from PIT ISR ─────────────────────── */
static uint32_t quantum_counter = 0;

void sched_tick(void) {
    uptime_ticks++;
    if(!current_task || current_task->pid==0) idle_ticks++;

    if (!run_queue_head) {
        /* A last task may have exited into a retained zombie. Return to the
         * boot/idle context saved by its original task_yield(). */
        if(current_task && current_task->pid!=0 && current_task->state==TASK_ZOMBIE){
            task_t *old=current_task, *idle=&task_pool[0];
            current_task=idle; idle->state=TASK_RUNNING;
            context_switch(&old->ctx,&idle->ctx);
        }
        return;
    }

    uint32_t quantum = current_task->priority * QUANTUM_BASE;
    if (++quantum_counter < quantum) return;
    quantum_counter = 0;

    /* Find next READY task in circular queue */
    task_t *start = run_queue_head;
    task_t *next  = start;

    do {
        if (next->state == TASK_READY && next != current_task)
            break;
        next = next->next;
    } while (next != start);

    if (next == current_task || next->state != TASK_READY) return;

    /* Perform context switch */
    task_t *old = current_task;
    if (old->state == TASK_RUNNING) old->state = TASK_READY;

    current_task        = next;
    current_task->state = TASK_RUNNING;
    current_task->ticks++;

    context_switch(&old->ctx, &current_task->ctx);
}

/* ── Getters ───────────────────────────────────────────────── */
task_t *sched_current(void) { return current_task; }
uint32_t sched_uptime_ticks(void) { return uptime_ticks; }
uint32_t sched_idle_ticks(void) { return idle_ticks; }
uint32_t sched_task_count(void) { uint32_t n=0; for(int i=0;i<TASK_MAX;i++)if(task_pool[i].state!=TASK_UNUSED&&task_pool[i].pid!=0)n++; return n; }

/* ── ps — print task list ──────────────────────────────────── */
static const char *state_name(task_state_t s) {
    switch (s) {
        case TASK_READY:   return "ready  ";
        case TASK_RUNNING: return "running";
        case TASK_BLOCKED: return "blocked";
        case TASK_ZOMBIE:  return "zombie ";
        default:           return "unused ";
    }
}

void sched_print_tasks(void) {
    char buf[16];
    terminal_writeln("PID  PPID UID:GID STATE    PRI  TICKS  NAME");
    terminal_writeln("---  ---- ------- ------- ---  -----  ----");
    for (int i = 0; i < TASK_MAX; i++) {
        task_t *t = &task_pool[i];
        if (t->state == TASK_UNUSED) continue;
        kuitoa(t->pid,      buf, 10); terminal_write(buf);
        terminal_write("    ");
        kuitoa(t->ppid,     buf, 10); terminal_write(buf);
        terminal_write("    ");
        kuitoa(t->uid,      buf, 10); terminal_write(buf);
        terminal_write(":");
        kuitoa(t->gid,      buf, 10); terminal_write(buf);
        terminal_write("    ");
        terminal_write(state_name(t->state));
        terminal_write("  ");
        kuitoa(t->priority, buf, 10); terminal_write(buf);
        terminal_write("    ");
        kuitoa(t->ticks,    buf, 10); terminal_write(buf);
        terminal_write("  ");
        terminal_writeln(t->name);
    }
}
