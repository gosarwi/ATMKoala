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
#include <stdint.h>

#define ATM_NATIVE_MAX_IMAGE (ATM_USER_WINDOW_SIZE - ATM_PAGE_SIZE)

static uint64_t native_irq_save_disable(void){uint64_t flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");return flags;}
static void native_irq_restore(uint64_t flags){if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");}

static int native_write_word(user_space_t *space,uint64_t va,uint64_t value){
    uintptr_t phys=0;
    if(!space || paging_user_translate(space,va,&phys,0)<0) return -1;
    *(uint64_t *)(uintptr_t)phys=value;
    return 0;
}

/* Start as an ordinary kernel scheduler task. Once its execution context is
 * selected, atomically enter its owned user CR3 through the existing TSS/iret
 * gate. A user return is impossible; exit(2) marks this task as a zombie. */
static void native_app_task_entry(void){
    task_t *task=sched_current();
    user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    if(!task || !space || !space->valid || !task->user_entry || !task->user_stack_top){
        sdk_serial_write("[native] task-invalid\n");
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

int native_app_spawn_memory(const char *name,const uint8_t *image,
                            uint32_t image_size,uint32_t priority){
    if(!image || !image_size || image_size>ATM_NATIVE_MAX_IMAGE || !usermode_gate_ready()) return -1;
    user_space_t *space=(user_space_t *)kmalloc(sizeof(*space));
    elf64_user_image_t loaded;
    if(!space) return -1;
    if(paging_create_user_space(space)<0 || elf64_load_user(image,image_size,space,&loaded)<0){
        paging_destroy_user_space(space); kfree(space); return -1;
    }
    /* Static startup layout: argc=0, argv=NULL, envp=NULL, auxv terminator.
     * It is 16-byte aligned and deliberately compact until execve provides
     * argv/envp marshalling from a caller process. */
    uint64_t stack=loaded.stack_top-4*sizeof(uint64_t);
    if(native_write_word(space,stack+0,0)<0 || native_write_word(space,stack+8,0)<0 ||
       native_write_word(space,stack+16,0)<0 || native_write_word(space,stack+24,0)<0){
        paging_destroy_user_space(space); kfree(space); return -1;
    }
    uint64_t initial_brk=(loaded.load_end+ATM_PAGE_SIZE-1)&ATM_PAGE_MASK;
    if(initial_brk>=ATM_USER_STACK_TOP){paging_destroy_user_space(space);kfree(space);return -1;}
    /* task_create queues a READY task. Keep interrupts disabled from creation
     * through native metadata publication so the scheduler cannot enter CPL3
     * with address_space/user_entry/user_stack_top still zero. */
    uint64_t irq_flags=native_irq_save_disable();
    task_t *task=task_create(name&&name[0]?name:"native",native_app_task_entry,priority);
    if(!task){native_irq_restore(irq_flags);paging_destroy_user_space(space);kfree(space);return -1;}
    task->address_space=space;
    native_fd_task_init(task);
    task->user_entry=loaded.entry;
    task->user_stack_top=stack;
    task->user_brk_base=initial_brk;
    task->user_brk=initial_brk;
    task->resident_bytes=(uint64_t)task->stack_size+paging_user_mapped_bytes(space);
    int pid=(int)task->pid;
    native_irq_restore(irq_flags);
    return pid;
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
    for(int i=0;i<64;i++){
        task_yield();
        int status=0,got=task_waitpid((uint32_t)pid,&status);
        if(got==pid){sdk_serial_write(status==42?"[native] reap-ok\\n":"[native] bad-status\\n");return status==42?0:-1;}
        if(got<0){sdk_serial_write("[native] reap-fail\\n");return -1;}
    }
    sdk_serial_write("[native] reap-timeout\\n");
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
        int status=0,got=task_waitpid((uint32_t)pid,&status);
        if(got==pid){
            if(status==42) sdk_serial_write("[libc] smoke-ok\n");
            else if(status==1) sdk_serial_write("[libc] smoke-fail-calloc\n");
            else if(status==2) sdk_serial_write("[libc] smoke-fail-string\n");
            else if(status==3) sdk_serial_write("[libc] smoke-fail-realloc\n");
            else if(status==4) sdk_serial_write("[libc] smoke-fail-write\n");
            else sdk_serial_write("[libc] smoke-bad-status\n");
            return status==42?0:-1;
        }
        if(got<0) return -1;
    }
    sdk_serial_write("[libc] smoke-timeout\n");
    return -1;
}
