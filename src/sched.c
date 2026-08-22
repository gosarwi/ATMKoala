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
#include "uaccess.h"
#include "linux_abi.h"
#include <stdint.h>

#define QUANTUM_BASE  2   /* ticks per quantum unit */

static task_t  task_pool[TASK_MAX];
static task_t *current_task  = 0;
static task_t *run_queue_head = 0;   /* circular singly-linked */
static uint32_t next_pid = 1;
static volatile uint32_t uptime_ticks = 0;
static volatile uint32_t idle_ticks = 0;

/* Deadline state is deliberately outside task_t. Some bounded CPL3 fixtures
 * allocate task-shaped objects on 4 KiB stacks; scheduler-only metadata must
 * not change that public layout. */
typedef struct { uint32_t deadline; uint8_t active; } sched_sleep_meta_t;
static sched_sleep_meta_t sleep_meta[TASK_MAX];
static int task_slot_index(const task_t *t){
    return (t && t>=task_pool && t<task_pool+TASK_MAX) ? (int)(t-task_pool) : -1;
}
static void sleep_cancel_task(const task_t *t){
    int slot=task_slot_index(t);
    if(slot>=0){sleep_meta[slot].active=0;sleep_meta[slot].deadline=0;}
}

/* ── Helpers ───────────────────────────────────────────────── */
static void release_zombie_slot(task_t *t){
    if(!t || t==current_task || t->state!=TASK_ZOMBIE) return;
    if(t->address_space){
        user_space_t *space=(user_space_t *)t->address_space;
        paging_destroy_user_space(space);
        kfree(space);
    }
    if(t->stack_base) kfree(t->stack_base);
    sleep_cancel_task(t);
    kmemset(t,0,sizeof(*t));
}

static task_t *alloc_task_slot(void) {
    for (int i = 0; i < TASK_MAX; i++)
        if (task_pool[i].state == TASK_UNUSED)
            return &task_pool[i];
    /* An orphan has no possible waiter. Reclaim only a non-current zombie;
     * live orphans retain their resources until they finish. */
    for (int i = 1; i < TASK_MAX; i++) {
        task_t *t=&task_pool[i];
        if(t->state==TASK_ZOMBIE && t->ppid==TASK_NO_PARENT){
            release_zombie_slot(t);
            return t;
        }
    }
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
    kmemset(sleep_meta,0,sizeof(sleep_meta));

    /* Create idle task (pid=0, runs when nothing else is ready) */
    task_t *idle = &task_pool[0];
    idle->pid      = 0;
    idle->pgid     = 0;
    idle->sid      = 0;
    idle->uid      = vfs_current_uid();
    idle->gid      = vfs_current_gid();
    kstrcpy(idle->cwd,"/");
    idle->umask_value = 0022;
    kstrcpy(idle->name, "idle");
    idle->state    = TASK_READY;
    idle->priority = 1;
    idle->created_ticks=0;
    idle->state_changed_ticks=0;
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
    if(current_task && current_task->pid){t->pgid=current_task->pgid;t->sid=current_task->sid;}
    else {t->pgid=t->pid;t->sid=t->pid;}
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
    t->created_ticks=uptime_ticks;
    t->state_changed_ticks=uptime_ticks;
    t->resident_bytes=TASK_STACK_SIZE;
    t->io_read_bytes=0;
    t->io_write_bytes=0;
    t->context_switches=0;
    t->exit_code  = 0;
    t->exit_signal = 0;
    t->pending_signal = 0;
    t->wait_target = TASK_WAIT_ANY;
    t->wait_active = 0;
    sleep_cancel_task(t);

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

task_t *task_lookup_pid(uint32_t pid){
    for(int i=0;i<TASK_MAX;i++) if(task_pool[i].state!=TASK_UNUSED && task_pool[i].pid==pid) return &task_pool[i];
    return NULL;
}

int task_suspend_unstarted(task_t *task){
    uint64_t flags;
    if(!task || task==current_task || task->pid==0 || task->state!=TASK_READY) return -1;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    if(task->state!=TASK_READY){if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");return -1;}
    queue_remove(task);
    task->state=TASK_BLOCKED;
    task->state_changed_ticks=uptime_ticks;
    if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");
    return 0;
}

int task_discard_unstarted(task_t *task){
    uint64_t flags;
    if(!task || task==current_task || task->pid==0 || task->state!=TASK_READY) return -1;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    if(task->state!=TASK_READY){if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");return -1;}
    queue_remove(task);
    native_fd_task_cleanup(task);
    sleep_cancel_task(task);
    kmemset(task,0,sizeof(*task));
    if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");
    return 0;
}

/* ── Child ownership and termination ───────────────────────── */
static int task_is_direct_child(const task_t *parent,const task_t *child,int32_t pid){
    if(!parent || !child || child->state==TASK_UNUSED || child->ppid!=parent->pid) return 0;
    return pid==-1 || child->pid==(uint32_t)pid;
}

static int task_wait_status(const task_t *t){
    if(!t) return 0;
    if(t->exit_signal) return (int)(t->exit_signal&0x7fu);
    return (t->exit_code&0xff)<<8;
}

static void task_orphan_children(task_t *parent){
    if(!parent) return;
    for(int i=1;i<TASK_MAX;i++){
        task_t *child=&task_pool[i];
        if(child->state==TASK_UNUSED || child->ppid!=parent->pid) continue;
        child->ppid=TASK_NO_PARENT;
        /* A child that already terminated can no longer be waited for once its
         * parent exits, so reclaim it now from the still-live parent context. */
        if(child->state==TASK_ZOMBIE) release_zombie_slot(child);
    }
}

static void task_wake_waiting_parent(const task_t *child){
    if(!child || child->ppid==TASK_NO_PARENT) return;
    for(int i=0;i<TASK_MAX;i++){
        task_t *parent=&task_pool[i];
        if(parent->state==TASK_UNUSED || parent->pid!=child->ppid) continue;
        if(parent->state==TASK_BLOCKED && parent->wait_active &&
           (parent->wait_target==TASK_WAIT_ANY || parent->wait_target==child->pid)){
            parent->wait_active=0;
            task_unblock(parent);
        }
        return;
    }
}

static void task_mark_zombie(task_t *t,int code,uint32_t signal){
    if(!t || t->pid==0 || t->state==TASK_UNUSED || t->state==TASK_ZOMBIE) return;
    if(t->linux_clear_tid && t->address_space){
        uint32_t zero=0; user_space_t *space=(user_space_t *)t->address_space;
        (void)copy_to_user(space,(void *)(uintptr_t)t->linux_clear_tid,&zero,sizeof(zero));
        t->linux_clear_tid=0;
    }
    sleep_cancel_task(t);
    t->state=TASK_ZOMBIE;
    t->state_changed_ticks=uptime_ticks;
    t->exit_code=code;
    t->exit_signal=signal;
    native_fd_task_cleanup(t);
    queue_remove(t);
    task_wake_waiting_parent(t);
    task_orphan_children(t);
}

/* ── task_exit ─────────────────────────────────────────────── */
void task_exit(int code) {
    __asm__ volatile("cli");
    if (!current_task || current_task->pid == 0) return;
    task_mark_zombie(current_task,code,0);
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
    task_mark_zombie(old,code,0);

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
    next->state_changed_ticks=uptime_ticks;
    next->context_switches++;
    linux_abi_set_fs_base(next->address_space?next->linux_fs_base:0);
    context_switch(&old->ctx,&next->ctx);
    for(;;) __asm__ volatile("hlt");
}

int task_kill(uint32_t pid, int code) {
    if (pid == 0) return -1; /* idle owns the boot/kernel stack */
    __asm__ volatile("cli");
    task_t *target=NULL;
    for (int i=0;i<TASK_MAX;i++) if (task_pool[i].state!=TASK_UNUSED && task_pool[i].pid==pid) { target=&task_pool[i]; break; }
    if (!target || target==current_task) { __asm__ volatile("sti"); return -1; }
    task_mark_zombie(target,code,0);
    /* Retain the zombie for waitpid. */
    __asm__ volatile("sti");
    return 0;
}

int task_waitpid(int32_t pid,int *status,uint32_t options) {
    task_t *parent=current_task;
    if(!parent || pid==0 || pid< -1 || (options&~TASK_WAIT_NOHANG)) return -TASK_ERR_EINVAL;
    for(;;){
        int have_child=0;
        for(int i=1;i<TASK_MAX;i++){
            task_t *t=&task_pool[i];
            if(!task_is_direct_child(parent,t,pid)) continue;
            have_child=1;
            if(t->state!=TASK_ZOMBIE) continue;
            if(status) *status=task_wait_status(t);
            uint32_t reaped=t->pid;
            parent->wait_active=0;
            release_zombie_slot(t);
            return (int)reaped;
        }
        if(!have_child){parent->wait_active=0;return -TASK_ERR_ECHILD;}
        if(options&TASK_WAIT_NOHANG) return 0;
        parent->wait_target=(pid<0)?TASK_WAIT_ANY:(uint32_t)pid;
        parent->wait_active=1;
        /* A native CPL3 caller must direct-switch out of its live int $0x80
         * frame. Kernel callers retain the older task_block path. */
        if(parent->address_space) task_block_from_syscall(); else task_block();
        parent->wait_active=0;
        /* Child exit/kill wakes this task; rescan to reap only a matched zombie. */
    }
}

static task_t *task_visible_target(task_t *caller,int32_t pid){
    if(!caller)return NULL;
    if(pid==0)return caller;
    if(pid<0)return NULL;
    task_t *target=task_lookup_pid((uint32_t)pid);
    if(!target || (target!=caller && target->ppid!=caller->pid))return NULL;
    return target;
}
int task_getpgid(int32_t pid){task_t *t=task_visible_target(current_task,pid);return t?(int)t->pgid:-1;}
int task_getsid(int32_t pid){task_t *t=task_visible_target(current_task,pid);return t?(int)t->sid:-1;}
int task_setpgid(int32_t pid,int32_t pgid){
    task_t *caller=current_task,*target=task_visible_target(caller,pid);
    if(!caller||!target||pgid<0||target->state==TASK_ZOMBIE)return -1;
    uint32_t newpgid=pgid?(uint32_t)pgid:target->pid;
    if(newpgid!=target->pid){
        task_t *leader=task_lookup_pid(newpgid);
        if(!leader||leader->sid!=target->sid)return -1;
    }
    target->pgid=newpgid;return 0;
}
int task_setsid(void){
    task_t *t=current_task;
    if(!t||!t->pid||t->pgid==t->pid)return -1;
    t->sid=t->pid;t->pgid=t->pid;return (int)t->sid;
}

int task_signal_probe(int32_t pid){
    task_t *caller=current_task;
    if(!caller || pid<=0) return -TASK_ERR_EINVAL;
    task_t *target=task_lookup_pid((uint32_t)pid);
    if(!target || target->state==TASK_UNUSED || target->state==TASK_ZOMBIE) return -3; /* ESRCH */
    return (target==caller || target->ppid==caller->pid)?0:-1; /* EPERM */
}

int task_send_signal(int32_t pid,int sig) {
    if(sig==0) return task_signal_probe(pid);
    if(pid<=0 || (sig!=15 && sig!=9)) return -TASK_ERR_EINVAL;
    task_t *caller=current_task;
    task_t *target=task_lookup_pid((uint32_t)pid);
    if(!caller || !target || target->state==TASK_UNUSED || target->state==TASK_ZOMBIE) return -3; /* ESRCH */
    /* Signal delivery into the active self context would require a checked
     * user-frame delivery/return contract. Do not falsely acknowledge it. */
    if(target==caller || target->ppid!=caller->pid) return -1; /* EPERM */
    target->pending_signal=(uint32_t)sig;
    __asm__ volatile("cli");
    task_mark_zombie(target,0,(uint32_t)sig);
    __asm__ volatile("sti");
    return 0;
}

/* ── task_yield ────────────────────────────────────────────── */
void task_yield(void) {
    __asm__ volatile("int $0x20");   /* trigger PIT IRQ (INT 32) */
}

/* ── task_block / unblock ──────────────────────────────────── */
void task_block_from_syscall(void) {
    __asm__ volatile("cli");
    task_t *old=current_task;
    if(!old || old->pid==0){__asm__ volatile("sti");return;}
    old->state=TASK_BLOCKED;
    old->state_changed_ticks=uptime_ticks;
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
    next->state_changed_ticks=uptime_ticks;
    next->context_switches++;
    linux_abi_set_fs_base(next->address_space?next->linux_fs_base:0);
    context_switch(&old->ctx,&next->ctx);
    /* The old task has been re-added by task_unblock and selected again. */
}

int task_sleep_ticks_from_syscall(uint32_t ticks){
    task_t *task=current_task;
    int slot=task_slot_index(task);
    if(!task || task->pid==0 || !task->address_space || slot<0) return -1;
    if(!ticks) return 0;
    if(sleep_meta[slot].active) return -1;
    sleep_meta[slot].deadline=uptime_ticks+ticks;
    sleep_meta[slot].active=1;
    task_block_from_syscall();
    /* sched_tick clears active before requeueing the task. */
    return 0;
}

void task_block(void) {
    __asm__ volatile("cli");
    if (current_task) {
        current_task->state = TASK_BLOCKED;
        current_task->state_changed_ticks=uptime_ticks;
        queue_remove(current_task);
    }
    __asm__ volatile("sti");
    task_yield();
}

void task_unblock(task_t *t) {
    if (!t || t->state != TASK_BLOCKED) return;
    t->state = TASK_READY;
    t->state_changed_ticks=uptime_ticks;
    if(queue_add(t)<0) { t->state=TASK_BLOCKED; t->state_changed_ticks=uptime_ticks; }
}

/* ── sched_tick — called from PIT ISR ─────────────────────── */
static uint32_t quantum_counter = 0;

void sched_tick(void) {
    uptime_ticks++;
    if(current_task) current_task->ticks++;
    if(!current_task || current_task->pid==0) idle_ticks++;

    /* Wake before considering the empty-run-queue idle handoff. Signed delta
     * retains correct ordering across the 32-bit PIT tick wraparound. */
    for(int i=1;i<TASK_MAX;i++){
        task_t *task=&task_pool[i];
        if(sleep_meta[i].active && task->state==TASK_BLOCKED &&
           (int32_t)(uptime_ticks-sleep_meta[i].deadline)>=0){
            sleep_meta[i].active=0;
            task_unblock(task);
        }
    }

    if (!run_queue_head) {
        /* A last task may have exited into a retained zombie. Return to the
         * boot/idle context saved by its original task_yield(). */
        if(current_task && current_task->pid!=0 &&
           (current_task->state==TASK_ZOMBIE || current_task->state==TASK_BLOCKED)){
            task_t *old=current_task, *idle=&task_pool[0];
            current_task=idle; idle->state=TASK_RUNNING; idle->state_changed_ticks=uptime_ticks; idle->context_switches++;
            linux_abi_set_fs_base(0);
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
    current_task->state_changed_ticks=uptime_ticks;
    current_task->context_switches++;
    linux_abi_set_fs_base(current_task->address_space?current_task->linux_fs_base:0);

    context_switch(&old->ctx, &current_task->ctx);
}

/* ── Getters ───────────────────────────────────────────────── */
task_t *sched_current(void) { return current_task; }
uint32_t sched_uptime_ticks(void) { return uptime_ticks; }
uint32_t sched_idle_ticks(void) { return idle_ticks; }
uint32_t sched_task_count(void) { uint32_t n=0; for(int i=0;i<TASK_MAX;i++)if(task_pool[i].state!=TASK_UNUSED&&task_pool[i].pid!=0)n++; return n; }
uint64_t sched_total_resident_bytes(void){uint64_t total=0;for(int i=0;i<TASK_MAX;i++)if(task_pool[i].state!=TASK_UNUSED)total+=task_pool[i].resident_bytes;return total;}
uint64_t sched_busy_ticks(void){return uptime_ticks>=idle_ticks?(uint64_t)(uptime_ticks-idle_ticks):0;}
int sched_task_info(uint32_t slot,sched_task_info_t *out){
    if(!out || slot>=TASK_MAX || task_pool[slot].state==TASK_UNUSED) return -1;
    task_t *t=&task_pool[slot];kmemset(out,0,sizeof(*out));
    out->pid=t->pid;out->ppid=t->ppid;out->pgid=t->pgid;out->sid=t->sid;out->uid=t->uid;out->gid=t->gid;out->state=t->state;out->priority=t->priority;out->context_switches=t->context_switches;
    out->cpu_ticks=t->ticks;out->created_ticks=t->created_ticks;out->state_changed_ticks=t->state_changed_ticks;out->resident_bytes=t->resident_bytes;out->io_read_bytes=t->io_read_bytes;out->io_write_bytes=t->io_write_bytes;kstrcpy(out->name,t->name);return 0;
}

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
    char buf[24];
    terminal_writeln("PID PPID UID:GID STATE    PRI CPUtk MEMB RD WR SW NAME");
    terminal_writeln("--- ---- ------- ------- --- ----- ---- -- -- -- ----");
    for (uint32_t i = 0; i < TASK_MAX; i++) {
        sched_task_info_t info;
        if(sched_task_info(i,&info)<0) continue;
        kuitoa(info.pid,buf,10); terminal_write(buf); terminal_write("    ");
        kuitoa(info.ppid,buf,10); terminal_write(buf); terminal_write("    ");
        kuitoa(info.uid,buf,10); terminal_write(buf); terminal_write(":");
        kuitoa(info.gid,buf,10); terminal_write(buf); terminal_write("    ");
        terminal_write(state_name(info.state)); terminal_write("  ");
        kuitoa(info.priority,buf,10); terminal_write(buf); terminal_write("  ");
        ku64toa(info.cpu_ticks,buf,10); terminal_write(buf); terminal_write("  ");
        ku64toa(info.resident_bytes,buf,10); terminal_write(buf); terminal_write("  ");
        ku64toa(info.io_read_bytes,buf,10); terminal_write(buf); terminal_write("  ");
        ku64toa(info.io_write_bytes,buf,10); terminal_write(buf); terminal_write("  ");
        kuitoa(info.context_switches,buf,10); terminal_write(buf); terminal_write("  ");
        terminal_writeln(info.name);
    }
}
