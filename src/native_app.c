#include "native_app.h"
#include "elf.h"
#include "usermode.h"
#include "sched.h"
#include "atm_posix.h"
#include "atm_syscall.h"
#include "native_fd.h"
#include "kmalloc.h"
#include "util.h"
#include "vfs.h"
#include "ossdk.h"
#include "linux_abi.h"
#include <stdint.h>

#define ATM_NATIVE_MAX_IMAGE (ATM_USER_WINDOW_SIZE - ATM_PAGE_SIZE)

static uint64_t native_irq_save_disable(void){uint64_t flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");return flags;}
static void native_irq_restore(uint64_t flags){if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");}

#define ATM_AT_NULL    0ULL
#define ATM_AT_PAGESZ  6ULL
#define ATM_AT_ENTRY   9ULL

static int native_write_word(user_space_t *space,uint64_t va,uint64_t value){
    uintptr_t phys=0;
    if(!space || paging_user_translate(space,va,&phys,0)<0) return -1;
    *(uint64_t *)(uintptr_t)phys=value;
    return 0;
}

static int native_write_bytes(user_space_t *space,uint64_t va,const void *src,uint64_t n){
    while(n){
        uintptr_t phys=0;
        if(!space || paging_user_translate(space,va,&phys,0)<0) return -1;
        uint64_t room=ATM_PAGE_SIZE-(va&0xfffULL);
        uint64_t take=n<room?n:room;
        kmemcpy((void *)phys,src,(size_t)take);
        va+=take; src=(const uint8_t *)src+take; n-=take;
    }
    return 0;
}

/* One bounded static-process startup record, held entirely in the mapped top
 * stack page. It follows the x86-64 process-stack shape: argc, a NULL
 * terminated argv vector, a NULL terminated envp vector, then auxv pairs.
 * The current native launcher exposes argv[0] only and an empty environment;
 * a later execve path will own caller-provided argv/envp marshalling. */
static int native_init_start_stack(user_space_t *space,const char *name,
                                   const elf64_user_image_t *loaded,uint64_t *stack_out){
    const char *program=(name&&name[0])?name:"native";
    uint64_t name_len=(uint64_t)kstrlen(program)+1;
    const uint64_t words=10; /* argc, argv[0..1], envp[0], three auxv pairs */
    if(!space||!loaded||!stack_out||name_len>ATM_PAGE_SIZE-words*8) return -1;
    uint64_t name_va=loaded->stack_top-name_len;
    uint64_t stack=(name_va-words*8)&~0xfULL;
    if(stack<ATM_USER_STACK_TOP || stack+words*8>name_va) return -1;
    if(native_write_bytes(space,name_va,program,name_len)<0 ||
       native_write_word(space,stack+0,1)<0 ||
       native_write_word(space,stack+8,name_va)<0 ||
       native_write_word(space,stack+16,0)<0 ||
       native_write_word(space,stack+24,0)<0 ||
       native_write_word(space,stack+32,ATM_AT_PAGESZ)<0 ||
       native_write_word(space,stack+40,ATM_PAGE_SIZE)<0 ||
       native_write_word(space,stack+48,ATM_AT_ENTRY)<0 ||
       native_write_word(space,stack+56,loaded->entry)<0 ||
       native_write_word(space,stack+64,ATM_AT_NULL)<0 ||
       native_write_word(space,stack+72,0)<0) return -1;
    *stack_out=stack;
    return 0;
}

/* Start as an ordinary kernel scheduler task. Once its execution context is
 * selected, atomically enter its owned user CR3 through the existing TSS/iret
 * gate. A user return is impossible; exit(2) marks this task as a zombie. */
static void native_app_task_entry(void){
    task_t *task=sched_current();
    user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    if(!task || !space || !space->valid || !task->user_entry || !task->user_stack_top){
        if(!task) sdk_serial_write("[native] task-invalid:no-task\n");
        else if(!space) sdk_serial_write("[native] task-invalid:no-space\n");
        else if(!space->valid) sdk_serial_write("[native] task-invalid:space-not-valid\n");
        else if(!task->user_entry) sdk_serial_write("[native] task-invalid:no-entry\n");
        else sdk_serial_write("[native] task-invalid:no-stack\n");
        task_exit(127);
        for(;;) __asm__ volatile("hlt");
    }
    sdk_serial_write("[native] task-enter\\n");
    atm_user_context_t ctx;
    ctx.space=space;
    ctx.entry=task->user_entry;
    ctx.stack_top=task->user_stack_top;
    ctx.kernel_stack_top=(uint64_t)(uintptr_t)((uint8_t *)task->stack_base+task->stack_size);
    usermode_enter(&ctx);
}

static int native_app_spawn_memory_common(const char *name,const uint8_t *image,
                                          uint32_t image_size,uint32_t priority,
                                          const task_t *inherit_from){
    if(!image || !image_size || image_size>ATM_NATIVE_MAX_IMAGE || !usermode_gate_ready()){
        sdk_serial_write("[native] spawn-precondition-fail\n"); return -1;
    }
    user_space_t *space=(user_space_t *)kmalloc(sizeof(*space));
    elf64_user_image_t loaded;
    if(!space){sdk_serial_write("[native] spawn-space-alloc-fail\n");return -1;}
    if(paging_create_user_space(space)<0){
        sdk_serial_write("[native] spawn-space-create-fail\n");paging_destroy_user_space(space); kfree(space); return -1;
    }
    if(elf64_load_user(image,image_size,space,&loaded)<0){
        sdk_serial_write("[native] spawn-elf-load-fail: ");sdk_serial_write(loaded.error);sdk_serial_write("\n");
        paging_destroy_user_space(space); kfree(space); return -1;
    }
    uint64_t stack=0;
    if(native_init_start_stack(space,name,&loaded,&stack)<0){
        sdk_serial_write("[native] spawn-stack-init-fail\n");paging_destroy_user_space(space); kfree(space); return -1;
    }
    uint64_t initial_brk=(loaded.load_end+ATM_PAGE_SIZE-1)&ATM_PAGE_MASK;
    if(initial_brk>=ATM_USER_STACK_TOP){sdk_serial_write("[native] spawn-brk-range-fail\n");paging_destroy_user_space(space);kfree(space);return -1;}
    /* task_create queues a READY task. Keep interrupts disabled from creation
     * through native metadata publication so the scheduler cannot enter CPL3
     * with address_space/user_entry/user_stack_top still zero. */
    uint64_t irq_flags=native_irq_save_disable();
    task_t *task=task_create(name&&name[0]?name:"native",native_app_task_entry,priority);
    if(!task){sdk_serial_write("[native] spawn-task-create-fail\n");native_irq_restore(irq_flags);paging_destroy_user_space(space);kfree(space);return -1;}
    task->address_space=space;
    if(inherit_from){
        if(native_fd_task_inherit(task,inherit_from)<0){
            task->address_space=0;
            (void)task_discard_unstarted(task);
            native_irq_restore(irq_flags);
            paging_destroy_user_space(space);kfree(space);
            return -1;
        }
    } else native_fd_task_init(task);
    task->user_entry=loaded.entry;
    task->user_stack_top=stack;
    task->user_brk_base=initial_brk;
    task->user_brk=initial_brk;
    task->resident_bytes=(uint64_t)task->stack_size+paging_user_mapped_bytes(space);
    int pid=(int)task->pid;
    native_irq_restore(irq_flags);
    return pid;
}

int native_app_spawn_memory(const char *name,const uint8_t *image,
                            uint32_t image_size,uint32_t priority){
    return native_app_spawn_memory_common(name,image,image_size,priority,0);
}

int native_app_spawn_memory_inherit(const char *name,const uint8_t *image,
                                    uint32_t image_size,uint32_t priority){
    task_t *parent=sched_current();
    if(!parent || !parent->fd_table_ready) return -1;
    return native_app_spawn_memory_common(name,image,image_size,priority,parent);
}

int native_app_spawn_path(const char *path,const char *name,uint32_t priority){
    atm_posix_stat_t st;
    if(!path || atm_posix_stat(path,&st)<0 || !st.st_size || st.st_size>ATM_NATIVE_MAX_IMAGE) return -1;
    uint8_t *image=(uint8_t *)kmalloc((size_t)st.st_size);
    if(!image) return -1;
    int fd=atm_posix_open(path,O_RDONLY,0);
    if(fd<0){kfree(image);return -1;}
    int64_t n=atm_posix_read(fd,image,st.st_size);
    atm_posix_close(fd);
    int ret=(n==(int64_t)st.st_size)?native_app_spawn_memory(name,image,(uint32_t)st.st_size,priority):-1;
    kfree(image);
    return ret;
}

/* This is deliberately narrower than POSIX execve: the syscall layer gives us
 * a checked path and at most one copied argv[0], while the native image must be
 * static x86-64 ET_EXEC. No fork, dynamic linker, signals, or multithreaded
 * process model is claimed. Failure leaves task, frame, descriptors and the
 * old address space untouched. */
int native_app_exec_current(task_t *task,registers_t *frame,const char *path,const char *name){
    atm_posix_stat_t st;
    uint8_t *image=NULL;
    user_space_t *next=NULL,*old;
    elf64_user_image_t loaded;
    uint64_t stack=0,initial_brk=0;
    int fd=-1,rc=-1;
    if(!task||!frame||!path||!path[0]||!task->address_space||!task->fd_table_ready) return -1;
    if(atm_posix_stat(path,&st)<0||!st.st_size||st.st_size>ATM_NATIVE_MAX_IMAGE) return -1;
    image=(uint8_t *)kmalloc((size_t)st.st_size);
    next=(user_space_t *)kmalloc(sizeof(*next));
    if(!image||!next) goto done;
    fd=atm_posix_open(path,O_RDONLY,0);
    if(fd<0||atm_posix_read(fd,image,st.st_size)!=(int64_t)st.st_size) goto done;
    (void)atm_posix_close(fd);fd=-1;
    if(paging_create_user_space(next)<0||elf64_load_user(image,(uint32_t)st.st_size,next,&loaded)<0||
       native_init_start_stack(next,name?name:path,&loaded,&stack)<0) goto done;
    initial_brk=(loaded.load_end+ATM_PAGE_SIZE-1)&ATM_PAGE_MASK;
    if(initial_brk>=ATM_USER_STACK_TOP) goto done;

    /* Commit only after all allocation, ELF validation and stack preparation
     * completed. Kernel mappings are shared by both CR3s, so old-space release
     * remains valid after activation of the new bounded map. */
    old=(user_space_t *)task->address_space;
    task->address_space=next;
    task->user_entry=loaded.entry;
    task->user_stack_top=stack;
    task->user_brk_base=initial_brk;
    task->user_brk=initial_brk;
    task->linux_fs_base=0;
    task->linux_clear_tid=0;
    task->resident_bytes=(uint64_t)task->stack_size+paging_user_mapped_bytes(next);
    kstrncpy(task->name,name&&name[0]?name:path,sizeof(task->name)-1);
    task->name[sizeof(task->name)-1]=0;
    native_fd_close_on_exec(task);
    linux_abi_set_fs_base(0);
    frame->rax=0;frame->rbx=0;frame->rcx=0;frame->rdx=0;frame->rsi=0;frame->rdi=0;
    frame->r8=0;frame->r9=0;frame->r10=0;frame->r11=0;frame->r12=0;frame->r13=0;frame->r14=0;frame->r15=0;
    frame->rbp=0;frame->rip=loaded.entry;frame->rsp=stack;
    if(paging_activate_user_space(next)<0) return -1; /* unreachable for validated next */
    paging_destroy_user_space(old);kfree(old);
    kfree(image);
    return 0;
done:
    if(fd>=0) (void)atm_posix_close(fd);
    if(next){paging_destroy_user_space(next);kfree(next);}
    if(image) kfree(image);
    return rc;
}

int native_app_selftest(void){
    /* static ET_EXEC image at 0x40000000:
     * mov rax, 60; mov rdi, 42; int 0x80; hlt
     * This checks the complete loader → task → iret → syscall → exit → reap path
     * without relying on a host toolchain or a libc startup object. */
    uint8_t image[0x1000];
    kmemset(image,0,sizeof(image));
    Elf64_Ehdr *h=(Elf64_Ehdr *)image;
    h->e_ident[EI_MAG0]=ELF_MAGIC0; h->e_ident[EI_MAG1]=ELF_MAGIC1;
    h->e_ident[EI_MAG2]=ELF_MAGIC2; h->e_ident[EI_MAG3]=ELF_MAGIC3;
    h->e_ident[EI_CLASS]=2; h->e_ident[EI_DATA]=1; h->e_ident[EI_VERSION]=1;
    h->e_type=ET_EXEC; h->e_machine=EM_X86_64; h->e_version=1;
    h->e_entry=ATM_USER_BASE+0x200; h->e_phoff=sizeof(Elf64_Ehdr);
    h->e_ehsize=sizeof(Elf64_Ehdr); h->e_phentsize=sizeof(Elf64_Phdr); h->e_phnum=1;
    Elf64_Phdr *p=(Elf64_Phdr *)(image+h->e_phoff);
    p->p_type=PT_LOAD; p->p_flags=PF_R|PF_X; p->p_offset=0;
    p->p_vaddr=ATM_USER_BASE; p->p_filesz=0x250; p->p_memsz=0x250; p->p_align=ATM_PAGE_SIZE;
    uint8_t *code=image+0x200;
    /* brk(0); brk(old+32); if returned break differs, exit(1), else exit(42). */
    code[0]=0x48; code[1]=0xC7; code[2]=0xC0; code[3]=ATM_SYS_BRK; code[4]=0; code[5]=0; code[6]=0;
    code[7]=0x48; code[8]=0x31; code[9]=0xFF; code[10]=0xCD; code[11]=0x80;
    code[12]=0x48; code[13]=0x89; code[14]=0xC7; code[15]=0x48; code[16]=0x83; code[17]=0xC7; code[18]=0x20;
    code[19]=0x48; code[20]=0xC7; code[21]=0xC0; code[22]=ATM_SYS_BRK; code[23]=0; code[24]=0; code[25]=0; code[26]=0xCD; code[27]=0x80;
    code[28]=0x48; code[29]=0x39; code[30]=0xF8; code[31]=0x74; code[32]=0x10;
    code[33]=0x48; code[34]=0xC7; code[35]=0xC0; code[36]=ATM_SYS_EXIT; code[37]=0; code[38]=0; code[39]=0;
    code[40]=0x48; code[41]=0xC7; code[42]=0xC7; code[43]=1; code[44]=0; code[45]=0; code[46]=0; code[47]=0xCD; code[48]=0x80;
    code[49]=0x48; code[50]=0xC7; code[51]=0xC0; code[52]=ATM_SYS_EXIT; code[53]=0; code[54]=0; code[55]=0;
    code[56]=0x48; code[57]=0xC7; code[58]=0xC7; code[59]=42; code[60]=0; code[61]=0; code[62]=0; code[63]=0xCD; code[64]=0x80; code[65]=0xF4;

    sdk_serial_write("[native] selftest-spawn\\n");
    int pid=native_app_spawn_memory("abi-probe",image,sizeof(image),10);
    if(pid<0){sdk_serial_write("[native] spawn-fail\\n");return -1;}
    sdk_serial_write("[native] spawn-ok\\n");
    int status=0,got=task_waitpid(pid,&status,0);
    if(got==pid){sdk_serial_write((TASK_WIFEXITED(status)&&TASK_WEXITSTATUS(status)==42)?"[native] blocking-reap-ok\\n":"[native] bad-status\\n");return (TASK_WIFEXITED(status)&&TASK_WEXITSTATUS(status)==42)?0:-1;}
    sdk_serial_write("[native] blocking-reap-fail\\n");
    return -1;
}

/* Linux x86-64 L0 regression: the program uses SYSCALL rather than int $0x80
 * and Linux numbers 39(getpid), 1(write), and 60(exit). */
int native_app_linux_abi_selftest(void){
    uint8_t image[0x1000];kmemset(image,0,sizeof(image));
    Elf64_Ehdr *h=(Elf64_Ehdr *)image;
    h->e_ident[EI_MAG0]=ELF_MAGIC0;h->e_ident[EI_MAG1]=ELF_MAGIC1;h->e_ident[EI_MAG2]=ELF_MAGIC2;h->e_ident[EI_MAG3]=ELF_MAGIC3;
    h->e_ident[EI_CLASS]=2;h->e_ident[EI_DATA]=1;h->e_ident[EI_VERSION]=1;h->e_type=ET_EXEC;h->e_machine=EM_X86_64;h->e_version=1;
    h->e_entry=ATM_USER_BASE+0x200;h->e_phoff=sizeof(Elf64_Ehdr);h->e_ehsize=sizeof(Elf64_Ehdr);h->e_phentsize=sizeof(Elf64_Phdr);h->e_phnum=1;
    Elf64_Phdr *p=(Elf64_Phdr *)(image+h->e_phoff);p->p_type=PT_LOAD;p->p_flags=PF_R|PF_X;p->p_offset=0;p->p_vaddr=ATM_USER_BASE;p->p_filesz=0x320;p->p_memsz=0x320;p->p_align=ATM_PAGE_SIZE;
    uint8_t *c=image+0x200;
    c[0]=0x48;c[1]=0xC7;c[2]=0xC0;c[3]=39;c[4]=0;c[5]=0;c[6]=0;c[7]=0x0F;c[8]=0x05;
    c[9]=0x48;c[10]=0x85;c[11]=0xC0;c[12]=0x7E;c[13]=0x37;
    c[14]=0x48;c[15]=0xC7;c[16]=0xC0;c[17]=1;c[18]=0;c[19]=0;c[20]=0;
    c[21]=0x48;c[22]=0xC7;c[23]=0xC7;c[24]=1;c[25]=0;c[26]=0;c[27]=0;
    c[28]=0x48;c[29]=0xBE;*((uint64_t *)(c+30))=ATM_USER_BASE+0x300;
    c[38]=0x48;c[39]=0xC7;c[40]=0xC2;c[41]=10;c[42]=0;c[43]=0;c[44]=0;c[45]=0x0F;c[46]=0x05;
    c[47]=0x48;c[48]=0x83;c[49]=0xF8;c[50]=10;c[51]=0x75;c[52]=0x10;
    c[53]=0x48;c[54]=0xC7;c[55]=0xC0;c[56]=60;c[57]=0;c[58]=0;c[59]=0;
    c[60]=0x48;c[61]=0xC7;c[62]=0xC7;c[63]=42;c[64]=0;c[65]=0;c[66]=0;c[67]=0x0F;c[68]=0x05;c[69]=0xF4;
    c[70]=0x48;c[71]=0xC7;c[72]=0xC0;c[73]=60;c[74]=0;c[75]=0;c[76]=0;
    c[77]=0x48;c[78]=0xC7;c[79]=0xC7;c[80]=1;c[81]=0;c[82]=0;c[83]=0;c[84]=0x0F;c[85]=0x05;c[86]=0xF4;
    kmemcpy(image+0x300,"linux-abi\\n",10);
    if(!linux_abi_ready()){sdk_serial_write("[linux] gate-closed\\n");return -1;}
    sdk_serial_write("[linux] l0-spawn\\n");
    int pid=native_app_spawn_memory("linux-l0",image,sizeof(image),10);if(pid<0)return -1;
    int status=0,got=task_waitpid(pid,&status,0);
    if(got==pid&&TASK_WIFEXITED(status)&&TASK_WEXITSTATUS(status)==42){sdk_serial_write("[linux] l0-ok\\n");return 0;}
    sdk_serial_write("[linux] l0-fail\\n");return -1;
}

/* Linux x86-64 L1 regression: real SYSCALL invokes brk, anonymous private
 * mmap, mprotect and munmap. It writes the mapping before removing PROT_WRITE. */
int native_app_linux_l1_selftest(void){
    uint8_t image[0x1000];kmemset(image,0,sizeof(image));
    Elf64_Ehdr *h=(Elf64_Ehdr *)image;
    h->e_ident[EI_MAG0]=ELF_MAGIC0;h->e_ident[EI_MAG1]=ELF_MAGIC1;h->e_ident[EI_MAG2]=ELF_MAGIC2;h->e_ident[EI_MAG3]=ELF_MAGIC3;
    h->e_ident[EI_CLASS]=2;h->e_ident[EI_DATA]=1;h->e_ident[EI_VERSION]=1;h->e_type=ET_EXEC;h->e_machine=EM_X86_64;h->e_version=1;
    h->e_entry=ATM_USER_BASE+0x200;h->e_phoff=sizeof(Elf64_Ehdr);h->e_ehsize=sizeof(Elf64_Ehdr);h->e_phentsize=sizeof(Elf64_Phdr);h->e_phnum=1;
    Elf64_Phdr *p=(Elf64_Phdr *)(image+h->e_phoff);p->p_type=PT_LOAD;p->p_flags=PF_R|PF_X;p->p_offset=0;p->p_vaddr=ATM_USER_BASE;p->p_filesz=0x600;p->p_memsz=0x600;p->p_align=ATM_PAGE_SIZE;
    uint8_t *c=image+0x200;uint32_t n=0,patch[8],pc=0;
#define L1B(x) do{c[n++]=(uint8_t)(x);}while(0)
#define L1Q(v) do{uint32_t _v=(uint32_t)(v);L1B(_v);L1B(_v>>8);L1B(_v>>16);L1B(_v>>24);}while(0)
#define L1RAX(v) do{L1B(0x48);L1B(0xC7);L1B(0xC0);L1Q(v);}while(0)
#define L1RDI(v) do{L1B(0x48);L1B(0xC7);L1B(0xC7);L1Q(v);}while(0)
#define L1RSI(v) do{L1B(0x48);L1B(0xC7);L1B(0xC6);L1Q(v);}while(0)
#define L1RDX(v) do{L1B(0x48);L1B(0xC7);L1B(0xC2);L1Q(v);}while(0)
#define L1SYSCALL() do{L1B(0x0F);L1B(0x05);}while(0)
#define L1FAIL(cc) do{L1B(0x0F);L1B(cc);patch[pc++]=n;n+=4;}while(0)
    /* old=brk(0); new=brk(old+8192), then require exact new break. */
    L1RAX(12);L1B(0x48);L1B(0x31);L1B(0xFF);L1SYSCALL();L1B(0x49);L1B(0x89);L1B(0xC4);
    L1B(0x48);L1B(0x89);L1B(0xC7);L1B(0x48);L1B(0x81);L1B(0xC7);L1Q(0x2000);L1RAX(12);L1SYSCALL();
    L1B(0x48);L1B(0x39);L1B(0xF8);L1FAIL(0x85);
    /* mmap(NULL,8192,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0). */
    L1RAX(9);L1B(0x48);L1B(0x31);L1B(0xFF);L1RSI(0x2000);L1RDX(3);
    L1B(0x49);L1B(0xC7);L1B(0xC2);L1Q(0x22);L1B(0x49);L1B(0xC7);L1B(0xC0);L1Q(0xffffffffu);L1B(0x4D);L1B(0x31);L1B(0xC9);L1SYSCALL();
    L1B(0x48);L1B(0x85);L1B(0xC0);L1FAIL(0x88);L1B(0x49);L1B(0x89);L1B(0xC5);L1B(0x41);L1B(0xC6);L1B(0x45);L1B(0);L1B(0x5A);
    /* mprotect(map,8192,PROT_READ) must return zero. */
    L1RAX(10);L1B(0x4C);L1B(0x89);L1B(0xEF);L1RSI(0x2000);L1RDX(1);L1SYSCALL();L1B(0x48);L1B(0x85);L1B(0xC0);L1FAIL(0x85);
    /* munmap(map,8192) must return zero. */
    L1RAX(11);L1B(0x4C);L1B(0x89);L1B(0xEF);L1RSI(0x2000);L1SYSCALL();L1B(0x48);L1B(0x85);L1B(0xC0);L1FAIL(0x85);
    L1RAX(60);L1RDI(42);L1SYSCALL();L1B(0xF4);
    uint32_t fail=n;L1RAX(60);L1RDI(1);L1SYSCALL();L1B(0xF4);
    for(uint32_t i=0;i<pc;i++){int32_t disp=(int32_t)fail-(int32_t)(patch[i]+4);kmemcpy(c+patch[i],&disp,4);}
#undef L1FAIL
#undef L1SYSCALL
#undef L1RDX
#undef L1RSI
#undef L1RDI
#undef L1RAX
#undef L1Q
#undef L1B
    if(!linux_abi_ready()){sdk_serial_write("[linux] l1-gate-closed\n");return -1;}
    sdk_serial_write("[linux] l1-spawn\n");
    int pid=native_app_spawn_memory("linux-l1",image,sizeof(image),10);if(pid<0)return -1;
    int status=0,got=task_waitpid(pid,&status,0);
    if(got==pid&&TASK_WIFEXITED(status)&&TASK_WEXITSTATUS(status)==42){sdk_serial_write("[linux] l1-ok\n");return 0;}
    sdk_serial_write("[linux] l1-fail\n");return -1;
}

/* Linux x86-64 L3 regression: AT_FDCWD-only openat, newfstatat and one or
 * more fd-backed getdents64 records through the real SYSCALL entry. */
int native_app_linux_l3_selftest(void){
    uint8_t *image=(uint8_t *)kmalloc(0x1000);
    if(!image)return -1;kmemset(image,0,0x1000);
    Elf64_Ehdr *h=(Elf64_Ehdr *)image;
    h->e_ident[EI_MAG0]=ELF_MAGIC0;h->e_ident[EI_MAG1]=ELF_MAGIC1;h->e_ident[EI_MAG2]=ELF_MAGIC2;h->e_ident[EI_MAG3]=ELF_MAGIC3;
    h->e_ident[EI_CLASS]=2;h->e_ident[EI_DATA]=1;h->e_ident[EI_VERSION]=1;h->e_type=ET_EXEC;h->e_machine=EM_X86_64;h->e_version=1;
    h->e_entry=ATM_USER_BASE+0x200;h->e_phoff=sizeof(Elf64_Ehdr);h->e_ehsize=sizeof(Elf64_Ehdr);h->e_phentsize=sizeof(Elf64_Phdr);h->e_phnum=1;
    Elf64_Phdr *p=(Elf64_Phdr *)(image+h->e_phoff);p->p_type=PT_LOAD;p->p_flags=PF_R|PF_W|PF_X;p->p_offset=0;p->p_vaddr=ATM_USER_BASE;p->p_filesz=0x820;p->p_memsz=0x1000;p->p_align=ATM_PAGE_SIZE;
    uint8_t *c=image+0x200;uint32_t n=0,patch[8],pc=0;
#define L3B(x) do{c[n++]=(uint8_t)(x);}while(0)
#define L3Q(v) do{uint32_t _v=(uint32_t)(v);L3B(_v);L3B(_v>>8);L3B(_v>>16);L3B(_v>>24);}while(0)
#define L3U(v) do{uint64_t _v=(uint64_t)(v);for(int _i=0;_i<8;_i++)L3B(_v>>(8*_i));}while(0)
#define L3RAX(v) do{L3B(0x48);L3B(0xC7);L3B(0xC0);L3Q(v);}while(0)
#define L3RDI(v) do{L3B(0x48);L3B(0xC7);L3B(0xC7);L3Q(v);}while(0)
#define L3RSI(v) do{L3B(0x48);L3B(0xC7);L3B(0xC6);L3Q(v);}while(0)
#define L3RDX(v) do{L3B(0x48);L3B(0xC7);L3B(0xC2);L3Q(v);}while(0)
#define L3R10(v) do{L3B(0x49);L3B(0xC7);L3B(0xC2);L3Q(v);}while(0)
#define L3SC() do{L3B(0x0F);L3B(0x05);}while(0)
#define L3FAIL(cc) do{L3B(0x0F);L3B(cc);patch[pc++]=n;n+=4;}while(0)
    const uint64_t root=ATM_USER_BASE+0x800,statbuf=ATM_USER_BASE+0x600,dirbuf=ATM_USER_BASE+0x900;
    /* fd=openat(AT_FDCWD,"/",O_DIRECTORY,0); require a native user fd. */
    L3RAX(257);L3RDI(0xffffff9cu);L3RSI(root);L3RDX(0x10000);L3R10(0);L3SC();
    L3B(0x48);L3B(0x83);L3B(0xF8);L3B(3);L3FAIL(0x8C);L3B(0x49);L3B(0x89);L3B(0xC4);
    /* newfstatat(AT_FDCWD,"/",statbuf,0) must report S_IFDIR. */
    L3RAX(262);L3RDI(0xffffff9cu);L3RSI(root);L3RDX(statbuf);L3R10(0);L3SC();L3B(0x48);L3B(0x85);L3B(0xC0);L3FAIL(0x85);
    L3B(0x48);L3B(0xBB);L3U(statbuf);L3B(0x8B);L3B(0x43);L3B(0x18);L3B(0x25);L3Q(0xf000);L3B(0x3D);L3Q(0x4000);L3FAIL(0x85);
    /* getdents64(fd,dirbuf,1536) must return at least one packed record. */
    L3RAX(217);L3B(0x4C);L3B(0x89);L3B(0xE7);L3RSI(dirbuf);L3RDX(0x600);L3SC();L3B(0x48);L3B(0x85);L3B(0xC0);L3FAIL(0x8E);
    L3RAX(3);L3B(0x4C);L3B(0x89);L3B(0xE7);L3SC();L3B(0x48);L3B(0x85);L3B(0xC0);L3FAIL(0x85);
    L3RAX(60);L3RDI(42);L3SC();L3B(0xF4);
    uint32_t fail=n;L3RAX(60);L3RDI(1);L3SC();L3B(0xF4);
    for(uint32_t i=0;i<pc;i++){int32_t d=(int32_t)fail-(int32_t)(patch[i]+4);kmemcpy(c+patch[i],&d,4);}
#undef L3FAIL
#undef L3SC
#undef L3R10
#undef L3RDX
#undef L3RSI
#undef L3RDI
#undef L3RAX
#undef L3U
#undef L3Q
#undef L3B
    kmemcpy(image+0x800,"/",2);
    if(!linux_abi_ready()){kfree(image);sdk_serial_write("[linux] l3-gate-closed\\n");return -1;}
    sdk_serial_write("[linux] l3-spawn\\n");int pid=native_app_spawn_memory("linux-l3",image,0x1000,10);kfree(image);if(pid<0)return -1;
    int status=0,got=task_waitpid(pid,&status,0);
    if(got==pid&&TASK_WIFEXITED(status)&&TASK_WEXITSTATUS(status)==42){sdk_serial_write("[linux] l3-ok\\n");return 0;}
    sdk_serial_write("[linux] l3-fail\\n");return -1;
}

/* End-to-end constrained exec regression. The runner opens the target as fd 3,
 * marks it FD_CLOEXEC and invokes native execve. The replacement program must
 * observe close(3) as an error before it exits 42. */
int native_app_exec_selftest(void){
    static const char path[]="/tmp/.atm-exec-probe";
    uint8_t *target=(uint8_t *)kmalloc(0x1000),*runner=(uint8_t *)kmalloc(0x1000);
    if(!target||!runner){if(target)kfree(target);if(runner)kfree(runner);return -1;}
    kmemset(target,0,0x1000);kmemset(runner,0,0x1000);
    uint8_t *images[2]={target,runner};
    for(int i=0;i<2;i++){
        Elf64_Ehdr *h=(Elf64_Ehdr *)images[i];
        h->e_ident[EI_MAG0]=ELF_MAGIC0;h->e_ident[EI_MAG1]=ELF_MAGIC1;h->e_ident[EI_MAG2]=ELF_MAGIC2;h->e_ident[EI_MAG3]=ELF_MAGIC3;
        h->e_ident[EI_CLASS]=2;h->e_ident[EI_DATA]=1;h->e_ident[EI_VERSION]=1;h->e_type=ET_EXEC;h->e_machine=EM_X86_64;h->e_version=1;
        h->e_entry=ATM_USER_BASE+0x200;h->e_phoff=sizeof(Elf64_Ehdr);h->e_ehsize=sizeof(Elf64_Ehdr);h->e_phentsize=sizeof(Elf64_Phdr);h->e_phnum=1;
        Elf64_Phdr *p=(Elf64_Phdr *)(images[i]+h->e_phoff);p->p_type=PT_LOAD;p->p_flags=PF_R|PF_X;p->p_offset=0;p->p_vaddr=ATM_USER_BASE;p->p_filesz=0x800;p->p_memsz=0x800;p->p_align=ATM_PAGE_SIZE;
    }
#define EXB(c,x) do{(c)[n++]=(uint8_t)(x);}while(0)
#define EXI(c,v) do{uint32_t _x=(uint32_t)(v);EXB(c,_x);EXB(c,_x>>8);EXB(c,_x>>16);EXB(c,_x>>24);}while(0)
#define EXRAX(c,v) do{EXB(c,0x48);EXB(c,0xC7);EXB(c,0xC0);EXI(c,v);}while(0)
#define EXRDI(c,v) do{EXB(c,0x48);EXB(c,0xC7);EXB(c,0xC7);EXI(c,v);}while(0)
#define EXRSI(c,v) do{EXB(c,0x48);EXB(c,0xC7);EXB(c,0xC6);EXI(c,v);}while(0)
#define EXRDX(c,v) do{EXB(c,0x48);EXB(c,0xC7);EXB(c,0xC2);EXI(c,v);}while(0)
#define EXINT(c) do{EXB(c,0xCD);EXB(c,0x80);}while(0)
    uint8_t *c=target+0x200;uint32_t n=0,j;
    /* Target must see fd 3 already closed by FD_CLOEXEC processing. */
    EXRAX(c,ATM_SYS_CLOSE);EXRDI(c,3);EXINT(c);EXB(c,0x48);EXB(c,0x85);EXB(c,0xC0);EXB(c,0x0F);EXB(c,0x89);j=n;n+=4;
    EXRAX(c,ATM_SYS_EXIT);EXRDI(c,42);EXINT(c);EXB(c,0xF4);
    uint32_t fail=n;EXRAX(c,ATM_SYS_EXIT);EXRDI(c,1);EXINT(c);EXB(c,0xF4);
    {int32_t d=(int32_t)fail-(int32_t)(j+4);kmemcpy(c+j,&d,4);}

    c=runner+0x200;n=0;uint32_t jumps[2],jc=0;
    /* open(path,O_RDONLY) must allocate 3; then F_SETFD(FD_CLOEXEC). */
    EXRAX(c,ATM_SYS_OPEN);EXB(c,0x48);EXB(c,0xBF);uint64_t path_va=ATM_USER_BASE+0x700;kmemcpy(c+n,&path_va,8);n+=8;EXRSI(c,0);EXRDX(c,0);EXINT(c);
    EXB(c,0x48);EXB(c,0x83);EXB(c,0xF8);EXB(c,3);EXB(c,0x0F);EXB(c,0x85);jumps[jc++]=n;n+=4;
    EXRAX(c,ATM_SYS_FCNTL);EXRDI(c,3);EXRSI(c,ATM_NATIVE_F_SETFD);EXRDX(c,ATM_NATIVE_FD_CLOEXEC);EXINT(c);
    EXB(c,0x48);EXB(c,0x85);EXB(c,0xC0);EXB(c,0x0F);EXB(c,0x85);jumps[jc++]=n;n+=4;
    EXRAX(c,ATM_SYS_EXECVE);EXB(c,0x48);EXB(c,0xBF);kmemcpy(c+n,&path_va,8);n+=8;EXB(c,0x48);EXB(c,0x31);EXB(c,0xF6);EXB(c,0x48);EXB(c,0x31);EXB(c,0xD2);EXINT(c);
    fail=n;EXRAX(c,ATM_SYS_EXIT);EXRDI(c,1);EXINT(c);EXB(c,0xF4);
    for(uint32_t i=0;i<jc;i++){int32_t d=(int32_t)fail-(int32_t)(jumps[i]+4);kmemcpy(c+jumps[i],&d,4);}
    kmemcpy(runner+0x700,path,sizeof(path));
#undef EXINT
#undef EXRDX
#undef EXRSI
#undef EXRDI
#undef EXRAX
#undef EXI
#undef EXB
    int backend=atm_posix_open(path,O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(backend<0||atm_posix_write(backend,target,0x1000)!=(int64_t)0x1000){if(backend>=0)(void)atm_posix_close(backend);kfree(runner);kfree(target);return -1;}
    (void)atm_posix_close(backend);
    sdk_serial_write("[exec] spawn\\n");
    int pid=native_app_spawn_memory("exec-runner",runner,0x1000,10),status=0;
    int got=pid<0?-1:task_waitpid(pid,&status,0);
    (void)atm_posix_unlink(path);
    kfree(runner);kfree(target);
    if(got==pid&&TASK_WIFEXITED(status)&&TASK_WEXITSTATUS(status)==42){sdk_serial_write("[exec] ok\\n");return 0;}
    sdk_serial_write("[exec] fail\\n");return -1;
}

/* True CPL3 wait regression. The child is held unstarted until a static CPL3
 * parent has been created and linked as its owner. The parent executes the
 * waitpid syscall itself, blocks, validates the copied 42<<8 status, and exits
 * 42 only after child-exit wakeup resumes its original int $0x80 frame. */
int native_app_cpl3_wait_selftest(void){
    uint8_t child_image[0x1000],parent_image[0x1000];
    kmemset(child_image,0,sizeof(child_image));kmemset(parent_image,0,sizeof(parent_image));
    uint8_t *images[2]={child_image,parent_image};
    for(int i=0;i<2;i++){
        Elf64_Ehdr *h=(Elf64_Ehdr *)images[i];
        h->e_ident[EI_MAG0]=ELF_MAGIC0;h->e_ident[EI_MAG1]=ELF_MAGIC1;h->e_ident[EI_MAG2]=ELF_MAGIC2;h->e_ident[EI_MAG3]=ELF_MAGIC3;
        h->e_ident[EI_CLASS]=2;h->e_ident[EI_DATA]=1;h->e_ident[EI_VERSION]=1;h->e_type=ET_EXEC;h->e_machine=EM_X86_64;h->e_version=1;
        h->e_entry=ATM_USER_BASE+0x200;h->e_phoff=sizeof(Elf64_Ehdr);h->e_ehsize=sizeof(Elf64_Ehdr);h->e_phentsize=sizeof(Elf64_Phdr);h->e_phnum=1;
        Elf64_Phdr *p=(Elf64_Phdr *)(images[i]+h->e_phoff);p->p_type=PT_LOAD;p->p_flags=PF_R|PF_X|PF_W;p->p_offset=0;p->p_vaddr=ATM_USER_BASE;p->p_filesz=0x400;p->p_memsz=0x400;p->p_align=ATM_PAGE_SIZE;
    }
    uint8_t *child=child_image+0x200;
    child[0]=0x48;child[1]=0xC7;child[2]=0xC0;child[3]=ATM_SYS_EXIT;child[4]=0;child[5]=0;child[6]=0;
    child[7]=0x48;child[8]=0xC7;child[9]=0xC7;child[10]=42;child[11]=0;child[12]=0;child[13]=0;child[14]=0xCD;child[15]=0x80;child[16]=0xF4;
    uint8_t *parent=parent_image+0x200;
    parent[0]=0x48;parent[1]=0xC7;parent[2]=0xC0;parent[3]=ATM_SYS_WAITPID;parent[4]=0;parent[5]=0;parent[6]=0;
    parent[7]=0x48;parent[8]=0xC7;parent[9]=0xC7;parent[10]=0xFF;parent[11]=0xFF;parent[12]=0xFF;parent[13]=0xFF;
    parent[14]=0x48;parent[15]=0xBE;*((uint64_t *)(parent+16))=ATM_USER_BASE+0x300;
    parent[24]=0x48;parent[25]=0x31;parent[26]=0xD2;parent[27]=0xCD;parent[28]=0x80;
    parent[29]=0x48;parent[30]=0x85;parent[31]=0xC0;parent[32]=0x7E;parent[33]=0x24;
    parent[34]=0x48;parent[35]=0xBB;*((uint64_t *)(parent+36))=ATM_USER_BASE+0x300;
    parent[44]=0x8B;parent[45]=0x03;parent[46]=0x3D;parent[47]=0;parent[48]=42;parent[49]=0;parent[50]=0;parent[51]=0x75;parent[52]=0x11;
    parent[53]=0x48;parent[54]=0xC7;parent[55]=0xC0;parent[56]=ATM_SYS_EXIT;parent[57]=0;parent[58]=0;parent[59]=0;
    parent[60]=0x48;parent[61]=0xC7;parent[62]=0xC7;parent[63]=42;parent[64]=0;parent[65]=0;parent[66]=0;parent[67]=0xCD;parent[68]=0x80;parent[69]=0xF4;
    parent[70]=0x48;parent[71]=0xC7;parent[72]=0xC0;parent[73]=ATM_SYS_EXIT;parent[74]=0;parent[75]=0;parent[76]=0;
    parent[77]=0x48;parent[78]=0xC7;parent[79]=0xC7;parent[80]=1;parent[81]=0;parent[82]=0;parent[83]=0;parent[84]=0xCD;parent[85]=0x80;parent[86]=0xF4;

    uint64_t flags=native_irq_save_disable();
    int child_pid=native_app_spawn_memory("wait-child",child_image,sizeof(child_image),10);
    task_t *child_task=child_pid>0?task_lookup_pid((uint32_t)child_pid):0;
    if(!child_task || task_suspend_unstarted(child_task)<0){native_irq_restore(flags);return -1;}
    int parent_pid=native_app_spawn_memory("wait-parent",parent_image,sizeof(parent_image),10);
    task_t *parent_task=parent_pid>0?task_lookup_pid((uint32_t)parent_pid):0;
    if(!parent_task){
        task_unblock(child_task);
        native_irq_restore(flags);
        return -1;
    }
    child_task->ppid=parent_task->pid;
    task_unblock(child_task);
    native_irq_restore(flags);
    sdk_serial_write("[native] cpl3-wait-spawn\\n");
    task_yield();
    int status=0,got=task_waitpid(parent_pid,&status,0);
    if(got==parent_pid && TASK_WIFEXITED(status) && TASK_WEXITSTATUS(status)==42){sdk_serial_write("[native] cpl3-wait-ok\\n");return 0;}
    sdk_serial_write("[native] cpl3-wait-fail\\n");
    return -1;
}

/* Generated by `ld -r -b binary build/libc_smoke.elf`. The fixture is built
 * through Makefile and represents an external static C application, not
 * hand-assembled test instructions. */
extern const uint8_t _binary_build_libc_smoke_elf_start[];
extern const uint8_t _binary_build_libc_smoke_elf_end[];

int native_app_libc_selftest(void){
    uint64_t bytes=(uint64_t)(_binary_build_libc_smoke_elf_end-_binary_build_libc_smoke_elf_start);
    if(!bytes || bytes>ATM_NATIVE_MAX_IMAGE) return -1;
    sdk_serial_write("[libc] smoke-spawn\n");
    int pid=native_app_spawn_memory("libc-smoke",_binary_build_libc_smoke_elf_start,(uint32_t)bytes,10);
    if(pid<0){sdk_serial_write("[libc] smoke-spawn-fail\n");return -1;}
    for(int i=0;i<64;i++){
        task_yield();
        int status=0,got=task_waitpid(pid,&status,TASK_WAIT_NOHANG);
        if(got==pid){
            int code=TASK_WIFEXITED(status)?TASK_WEXITSTATUS(status):-1;
            if(code==42) sdk_serial_write("[libc] smoke-ok\n");
            else if(code==1) sdk_serial_write("[libc] smoke-fail-calloc\n");
            else if(code==2) sdk_serial_write("[libc] smoke-fail-string\n");
            else if(code==3) sdk_serial_write("[libc] smoke-fail-realloc\n");
            else if(code==4) sdk_serial_write("[libc] smoke-fail-write\n");
            else if(code==29) sdk_serial_write("[libc] smoke-fail-waitpid\n");
            else if(code==30) sdk_serial_write("[libc] smoke-fail-pipe\n");
            else if(code==31) sdk_serial_write("[libc] smoke-fail-dirent\n");
            else if(code==33) sdk_serial_write("[libc] smoke-fail-startup\n");
            else {char status_text[40];ksnprintf(status_text,sizeof(status_text),"[libc] smoke-code-%d\\n",code);sdk_serial_write(status_text);}
            return code==42?0:-1;
        }
        if(got<0) return -1;
    }
    sdk_serial_write("[libc] smoke-timeout\n");
    return -1;
}

int native_app_pipe_ipc_selftest(void){
    /* Child ABI probe: inherited fd 3 is the read end and inherited fd 4 is
     * the write end. Close the latter, block in read(3), require byte 'Z',
     * then exit(42). This exercises descriptor inheritance + pipe sleep/wake
     * + direct CPL3 syscall resumption + parent wait/reap in one path. */
    task_t *parent=sched_current();
    if(!parent || parent->fd_table_ready) return -1;
    uint8_t image[0x1000];
    kmemset(image,0,sizeof(image));
    Elf64_Ehdr *h=(Elf64_Ehdr *)image;
    h->e_ident[EI_MAG0]=ELF_MAGIC0;h->e_ident[EI_MAG1]=ELF_MAGIC1;
    h->e_ident[EI_MAG2]=ELF_MAGIC2;h->e_ident[EI_MAG3]=ELF_MAGIC3;
    h->e_ident[EI_CLASS]=2;h->e_ident[EI_DATA]=1;h->e_ident[EI_VERSION]=1;
    h->e_type=ET_EXEC;h->e_machine=EM_X86_64;h->e_version=1;
    h->e_entry=ATM_USER_BASE+0x200;h->e_phoff=sizeof(Elf64_Ehdr);
    h->e_ehsize=sizeof(Elf64_Ehdr);h->e_phentsize=sizeof(Elf64_Phdr);h->e_phnum=1;
    Elf64_Phdr *p=(Elf64_Phdr *)(image+h->e_phoff);
    p->p_type=PT_LOAD;p->p_flags=PF_R|PF_X|PF_W;p->p_offset=0;
    p->p_vaddr=ATM_USER_BASE;p->p_filesz=0x400;p->p_memsz=0x400;p->p_align=ATM_PAGE_SIZE;
    uint8_t *code=image+0x200;uint32_t pc=0;
#define IPC_MOV_EAX(v) do{code[pc++]=0xB8;code[pc++]=(uint8_t)(v);code[pc++]=(uint8_t)((v)>>8);code[pc++]=(uint8_t)((v)>>16);code[pc++]=(uint8_t)((v)>>24);}while(0)
#define IPC_MOV_EDI(v) do{code[pc++]=0xBF;code[pc++]=(uint8_t)(v);code[pc++]=(uint8_t)((v)>>8);code[pc++]=(uint8_t)((v)>>16);code[pc++]=(uint8_t)((v)>>24);}while(0)
    IPC_MOV_EAX(ATM_SYS_CLOSE);IPC_MOV_EDI(4);code[pc++]=0xCD;code[pc++]=0x80;
    IPC_MOV_EAX(ATM_SYS_READ);IPC_MOV_EDI(3);
    code[pc++]=0x48;code[pc++]=0xBE;uint64_t dst=ATM_USER_BASE+0x300;
    for(int i=0;i<8;i++)code[pc++]=(uint8_t)(dst>>(8*i));
    code[pc++]=0xBA;code[pc++]=1;code[pc++]=0;code[pc++]=0;code[pc++]=0;code[pc++]=0xCD;code[pc++]=0x80;
    code[pc++]=0x48;code[pc++]=0x83;code[pc++]=0xF8;code[pc++]=1;
    uint32_t bad_count=pc;code[pc++]=0x75;code[pc++]=0;
    code[pc++]=0x8A;code[pc++]=0x06;code[pc++]=0x3C;code[pc++]=(uint8_t)'Z';
    uint32_t bad_byte=pc;code[pc++]=0x75;code[pc++]=0;
    IPC_MOV_EAX(ATM_SYS_EXIT);IPC_MOV_EDI(42);code[pc++]=0xCD;code[pc++]=0x80;
    uint32_t fail=pc;IPC_MOV_EAX(ATM_SYS_EXIT);IPC_MOV_EDI(1);code[pc++]=0xCD;code[pc++]=0x80;
    if(fail-(bad_count+2)>127 || fail-(bad_byte+2)>127) return -1;
    code[bad_count+1]=(uint8_t)(fail-(bad_count+2));
    code[bad_byte+1]=(uint8_t)(fail-(bad_byte+2));
#undef IPC_MOV_EAX
#undef IPC_MOV_EDI

    int rc=-1,pid=-1,fds[2]={-1,-1};
    native_fd_task_init(parent);
    if(native_fd_pipe(parent,fds)<0) goto done;
    pid=native_app_spawn_memory_inherit("pipe-ipc",image,sizeof(image),10);
    if(pid<0) goto done;
    if(native_fd_close(parent,fds[0])<0){fds[0]=-1;goto done;}
    fds[0]=-1;
    /* First switch runs the child to its empty-pipe read and returns only
     * after the direct CPL3 sleep handoff selected this parent. */
    task_yield();
    if(native_fd_write(parent,fds[1],"Z",1)!=1) goto done;
    if(native_fd_close(parent,fds[1])<0){fds[1]=-1;goto done;}
    fds[1]=-1;
    for(int i=0;i<16;i++){
        task_yield();
        int status=0,got=task_waitpid(pid,&status,TASK_WAIT_NOHANG);
        if(got==pid){rc=(TASK_WIFEXITED(status)&&TASK_WEXITSTATUS(status)==42)?0:-1;pid=-1;break;}
        if(got<0) break;
    }
done:
    if(fds[0]>=0) (void)native_fd_close(parent,fds[0]);
    if(fds[1]>=0) (void)native_fd_close(parent,fds[1]);
    if(pid>0){(void)task_kill((uint32_t)pid,1);(void)task_waitpid(pid,0,TASK_WAIT_NOHANG);}
    native_fd_task_cleanup(parent);
    return rc;
}
