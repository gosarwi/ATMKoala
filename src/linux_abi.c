#include "linux_abi.h"
#include "atm_syscall.h"
#include "util.h"
#include "sched.h"
#include "paging.h"
#include "kmalloc.h"
#include "uaccess.h"
#include "atm_uname.h"
#include "native_fd.h"
#include "atm_posix.h"
#include "util.h"
#include <stdint.h>

#define MSR_EFER  0xC0000080u
#define MSR_STAR  0xC0000081u
#define MSR_LSTAR 0xC0000082u
#define MSR_SFMASK 0xC0000084u
#define MSR_FS_BASE 0xC0000100u
#define EFER_SCE  0x00000001u

/* Linux x86-64 syscall numbers used in L0. */
#define LINUX_NR_READ       0u
#define LINUX_NR_WRITE      1u
#define LINUX_NR_OPEN       2u
#define LINUX_NR_CLOSE      3u
#define LINUX_NR_GETPID     39u
#define LINUX_NR_EXIT       60u
#define LINUX_NR_EXIT_GROUP 231u
#define LINUX_NR_MMAP       9u
#define LINUX_NR_MPROTECT   10u
#define LINUX_NR_MUNMAP     11u
#define LINUX_NR_BRK        12u
#define LINUX_NR_UNAME      63u
#define LINUX_NR_ARCH_PRCTL 158u
#define LINUX_NR_GETTID     186u
#define LINUX_NR_SET_TID_ADDRESS 218u
#define LINUX_NR_GETDENTS64 217u
#define LINUX_NR_OPENAT 257u
#define LINUX_NR_NEWFSTATAT 262u

#define LINUX_ARCH_SET_FS   0x1002u
#define LINUX_ARCH_GET_FS   0x1003u

#define LINUX_PROT_READ     0x1u
#define LINUX_PROT_WRITE    0x2u
#define LINUX_PROT_EXEC     0x4u
#define LINUX_MAP_PRIVATE   0x02u
#define LINUX_MAP_FIXED     0x10u
#define LINUX_MAP_ANONYMOUS 0x20u

#define LINUX_AT_FDCWD ((int64_t)-100)
#define LINUX_AT_SYMLINK_NOFOLLOW 0x100u
#define LINUX_O_ACCMODE 0x0003u
#define LINUX_O_CREAT 0x0040u
#define LINUX_O_EXCL 0x0080u
#define LINUX_O_TRUNC 0x0200u
#define LINUX_O_APPEND 0x0400u
#define LINUX_O_NONBLOCK 0x0800u
#define LINUX_O_LARGEFILE 0x8000u
#define LINUX_O_DIRECTORY 0x10000u
#define LINUX_O_NOFOLLOW 0x20000u
#define LINUX_O_CLOEXEC 0x80000u

extern void linux_syscall_entry(void);
uint64_t linux_kernel_stack_top;
uint64_t linux_saved_user_rsp;
static int linux_gate_ready;

static uint64_t linux_rdmsr(uint32_t msr){
    uint32_t lo,hi;
    __asm__ volatile("rdmsr":"=a"(lo),"=d"(hi):"c"(msr));
    return ((uint64_t)hi<<32)|lo;
}
static void linux_wrmsr(uint32_t msr,uint64_t value){
    uint32_t lo=(uint32_t)value,hi=(uint32_t)(value>>32);
    __asm__ volatile("wrmsr"::"c"(msr),"a"(lo),"d"(hi));
}

void linux_abi_init(void){
    /* STAR: kernel CS=0x08 in bits 47:32. SYSRET derives user CS=0x1b
     * from selector 0x0b in bits 63:48, and user SS=0x23. */
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");
    linux_wrmsr(MSR_EFER,linux_rdmsr(MSR_EFER)|EFER_SCE);
    linux_wrmsr(MSR_STAR,((uint64_t)0x0b<<48)|((uint64_t)0x08<<32));
    linux_wrmsr(MSR_LSTAR,(uint64_t)(uintptr_t)linux_syscall_entry);
    linux_wrmsr(MSR_SFMASK,1ULL<<9); /* Mask IF while entering kernel mode. */
    linux_gate_ready=1;
    if(flags&(1ULL<<9)) __asm__ volatile("sti":::"memory");
}
void linux_abi_set_kernel_stack(uint64_t stack_top){ linux_kernel_stack_top=stack_top; }
void linux_abi_set_fs_base(uint64_t fs_base){ linux_wrmsr(MSR_FS_BASE,fs_base); }
int linux_abi_ready(void){ return linux_gate_ready && linux_kernel_stack_top; }

static uint64_t linux_error(int error){ return (uint64_t)-(int64_t)error; }
static int linux_page_span(uint64_t addr,uint64_t len,uint64_t *end_out){
    if(!len || (addr&0xfffULL) || len>(~0ULL-(ATM_PAGE_SIZE-1))) return -1;
    uint64_t span=(len+ATM_PAGE_SIZE-1)&ATM_PAGE_MASK;
    uint64_t end=addr+span;
    if(end<=addr || addr<ATM_USER_ANON_BASE || end>ATM_USER_STACK_TOP) return -1;
    if(end_out) *end_out=end;
    return 0;
}
static int linux_range_is_free(const user_space_t *space,uint64_t start,uint64_t end){
    for(uint64_t va=start;va<end;va+=ATM_PAGE_SIZE){
        uintptr_t phys=0;
        if(paging_user_translate(space,va,&phys,0)==0) return 0;
    }
    return 1;
}
static uint64_t linux_pte_flags(uint64_t prot){
    uint64_t flags=0;
    if(prot&LINUX_PROT_WRITE) flags|=ATM_PTE_W;
    if(!(prot&LINUX_PROT_EXEC)) flags|=ATM_PTE_NX;
    return flags;
}
static void linux_update_resident(task_t *task,user_space_t *space){
    if(task&&space) task->resident_bytes=(uint64_t)task->stack_size+paging_user_mapped_bytes(space);
}
static int linux_user_word_range(const user_space_t *space,uint64_t addr){
    return addr && user_range_valid(space,(const void *)(uintptr_t)addr,sizeof(uint64_t),1)==0;
}
static uint64_t linux_arch_prctl(registers_t *r){
    task_t *task=sched_current();
    user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    if(!task||!space||!space->valid) return linux_error(ATM_EINVAL);
    if(r->rdi==LINUX_ARCH_SET_FS){
        if(r->rsi<ATM_USER_BASE || r->rsi>=ATM_USER_TOP) return linux_error(ATM_EINVAL);
        task->linux_fs_base=r->rsi;linux_abi_set_fs_base(r->rsi);return 0;
    }
    if(r->rdi==LINUX_ARCH_GET_FS){
        if(!linux_user_word_range(space,r->rsi)) return linux_error(ATM_EFAULT);
        return copy_to_user(space,(void *)(uintptr_t)r->rsi,&task->linux_fs_base,sizeof(task->linux_fs_base))<0?linux_error(ATM_EFAULT):0;
    }
    return linux_error(ATM_EINVAL);
}
static uint64_t linux_set_tid_address(registers_t *r){
    task_t *task=sched_current();
    user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    if(!task||!space||!space->valid||!r->rdi||user_range_valid(space,(const void *)(uintptr_t)r->rdi,sizeof(uint32_t),1)<0) return linux_error(ATM_EFAULT);
    task->linux_clear_tid=r->rdi;
    return task->pid;
}
static uint64_t linux_uname(registers_t *r){
    task_t *task=sched_current();user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    atm_utsname_t uts;
    if(!space||!r->rdi||user_range_valid(space,(const void *)(uintptr_t)r->rdi,sizeof(uts),1)<0) return linux_error(ATM_EFAULT);
    kmemset(&uts,0,sizeof(uts));
    kstrcpy(uts.sysname,"Linux");
    kstrcpy(uts.nodename,"atmkoala");
    kstrcpy(uts.release,"0.9-atmkoala");
    kstrcpy(uts.version,"#1 ATMKoala Linux-ABI subset");
    kstrcpy(uts.machine,"x86_64");
    kstrcpy(uts.domainname,"localdomain");
    return copy_to_user(space,(void *)(uintptr_t)r->rdi,&uts,sizeof(uts))<0?linux_error(ATM_EFAULT):0;
}
typedef struct __attribute__((packed)) {
    uint64_t st_dev,st_ino,st_nlink;
    uint32_t st_mode,st_uid,st_gid,__pad0;
    uint64_t st_rdev;
    int64_t st_size,st_blksize,st_blocks;
    int64_t st_atime_sec,st_atime_nsec,st_mtime_sec,st_mtime_nsec,st_ctime_sec,st_ctime_nsec;
    int64_t __reserved[3];
} linux_stat64_t;
typedef struct __attribute__((packed)) {
    uint64_t d_ino;
    int64_t d_off;
    uint16_t d_reclen;
    uint8_t d_type;
    char d_name[1];
} linux_dirent64_t;

static int linux_open_flags(uint64_t in,uint32_t *out){
    uint64_t allowed=LINUX_O_ACCMODE|LINUX_O_CREAT|LINUX_O_EXCL|LINUX_O_TRUNC|LINUX_O_APPEND|
                     LINUX_O_NONBLOCK|LINUX_O_LARGEFILE|LINUX_O_DIRECTORY|LINUX_O_NOFOLLOW|LINUX_O_CLOEXEC;
    uint32_t f;
    if(!out||(in&~allowed)) return -1;
    f=(uint32_t)(in&LINUX_O_ACCMODE);
    if(in&LINUX_O_CREAT)f|=O_CREAT;if(in&LINUX_O_EXCL)f|=O_EXCL;if(in&LINUX_O_TRUNC)f|=O_TRUNC;
    if(in&LINUX_O_APPEND)f|=O_APPEND;if(in&LINUX_O_NONBLOCK)f|=ATM_NATIVE_O_NONBLOCK;
    if(in&LINUX_O_DIRECTORY)f|=O_DIRECTORY;if(in&LINUX_O_NOFOLLOW)f|=O_NOFOLLOW;
    if(in&LINUX_O_CLOEXEC)f|=ATM_NATIVE_O_CLOEXEC;
    *out=f;return 0;
}
static uint64_t linux_openat(registers_t *r,int has_dirfd){
    task_t *task=sched_current();user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    char path[VFS_PATH_MAX];uint32_t flags;
    int64_t dirfd=has_dirfd?(int64_t)r->rdi:LINUX_AT_FDCWD;
    const char *user_path=(const char *)(uintptr_t)(has_dirfd?r->rsi:r->rdi);
    uint64_t user_flags=has_dirfd?r->rdx:r->rsi,mode=has_dirfd?r->r10:r->rdx;
    if(!task||!space||!space->valid||dirfd!=LINUX_AT_FDCWD||
       copy_string_from_user(space,path,sizeof(path),user_path)<0) return linux_error(ATM_EFAULT);
    if(linux_open_flags(user_flags,&flags)<0) return linux_error(ATM_EINVAL);
    int fd=native_fd_open(task,path,flags,(uint32_t)mode);
    return fd<0?linux_error(ATM_EINVAL):(uint64_t)fd;
}
static void linux_stat_pack(linux_stat64_t *out,const atm_posix_stat_t *in){
    kmemset(out,0,sizeof(*out));out->st_ino=in->st_ino;out->st_nlink=in->st_nlink;out->st_mode=in->st_mode;
    out->st_uid=in->st_uid;out->st_gid=in->st_gid;out->st_size=(int64_t)in->st_size;out->st_blksize=(int64_t)in->st_blksize;
    out->st_blocks=(int64_t)in->st_blocks;out->st_atime_sec=(int64_t)in->st_atime.sec;out->st_atime_nsec=(int64_t)in->st_atime.nsec;
    out->st_mtime_sec=(int64_t)in->st_mtime.sec;out->st_mtime_nsec=(int64_t)in->st_mtime.nsec;
    out->st_ctime_sec=(int64_t)in->st_ctime.sec;out->st_ctime_nsec=(int64_t)in->st_ctime.nsec;
}
static uint64_t linux_newfstatat(registers_t *r){
    task_t *task=sched_current();user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    char path[VFS_PATH_MAX];atm_posix_stat_t st;linux_stat64_t out;uint64_t flags=r->r10;
    if(!task||!space||!space->valid||(int64_t)r->rdi!=LINUX_AT_FDCWD||!r->rdx||
       (flags&~LINUX_AT_SYMLINK_NOFOLLOW)) return linux_error(ATM_EINVAL);
    if(copy_string_from_user(space,path,sizeof(path),(const char *)(uintptr_t)r->rsi)<0) return linux_error(ATM_EFAULT);
    if((flags&LINUX_AT_SYMLINK_NOFOLLOW?atm_posix_lstat(path,&st):atm_posix_stat(path,&st))<0) return linux_error(ATM_EINVAL);
    linux_stat_pack(&out,&st);
    return copy_to_user(space,(void *)(uintptr_t)r->rdx,&out,sizeof(out))<0?linux_error(ATM_EFAULT):0;
}
static uint64_t linux_getdents64(registers_t *r){
    task_t *task=sched_current();user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    uint8_t *user=(uint8_t *)(uintptr_t)r->rsi;uint64_t size=r->rdx,written=0,ordinal=0;
    /* A caller must provide room for the largest representable record; this
     * keeps the fd iterator non-lossy without a separate unget state. */
    if(!task||!space||!space->valid||!user||size<280||user_range_valid(space,user,(size_t)size,1)<0) return linux_error(ATM_EINVAL);
    for(;;){
        atm_posix_dirent_t entry;int got=native_fd_readdir(task,(int)r->rdi,&entry);
        if(got<0) return written?written:linux_error(ATM_EINVAL);
        if(!got) return written;
        uint64_t name_len=(uint64_t)kstrlen(entry.name)+1;
        uint64_t reclen=(19+name_len+7)&~7ULL;
        if(reclen>size-written) return written?written:linux_error(ATM_EINVAL);
        linux_dirent64_t head;kmemset(&head,0,sizeof(head));head.d_ino=entry.ino;head.d_off=(int64_t)(++ordinal);head.d_reclen=(uint16_t)reclen;head.d_type=entry.d_type;
        if(copy_to_user(space,user+written,&head,19)<0||copy_to_user(space,user+written+19,entry.name,(size_t)name_len)<0) return linux_error(ATM_EFAULT);
        uint64_t padding=reclen-19-name_len;
        if(padding){uint64_t zero=0;if(copy_to_user(space,user+written+19+name_len,&zero,(size_t)padding)<0)return linux_error(ATM_EFAULT);}
        written+=reclen;
    }
}

static uint64_t linux_mmap_anon(registers_t *r){
    task_t *task=sched_current();
    user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    uint64_t addr=r->rdi,len=r->rsi,prot=r->rdx,flags=r->r10;
    int64_t fd=(int64_t)r->r8;
    if(!task||!space||!space->valid || addr || fd!=-1 || r->r9 ||
       !(flags&LINUX_MAP_PRIVATE) || !(flags&LINUX_MAP_ANONYMOUS) ||
       (flags&LINUX_MAP_FIXED) || (prot&~(LINUX_PROT_READ|LINUX_PROT_WRITE|LINUX_PROT_EXEC)) || !prot)
        return linux_error(ATM_EINVAL);
    if(len>(ATM_USER_STACK_TOP-ATM_USER_ANON_BASE) || len>(~0ULL-(ATM_PAGE_SIZE-1))) return linux_error(ATM_ENOMEM);
    uint64_t span=(len+ATM_PAGE_SIZE-1)&ATM_PAGE_MASK;
    if(!span || span>(ATM_USER_STACK_TOP-ATM_USER_ANON_BASE)) return linux_error(ATM_ENOMEM);
    uint64_t start=(ATM_USER_STACK_TOP-span)&ATM_PAGE_MASK, end=start+span;
    for(;;){
        if(linux_range_is_free(space,start,end)) break;
        if(start<ATM_USER_ANON_BASE+ATM_PAGE_SIZE) return linux_error(ATM_ENOMEM);
        start-=ATM_PAGE_SIZE; end-=ATM_PAGE_SIZE;
    }
    uint64_t va=start;
    for(;va<end;va+=ATM_PAGE_SIZE){
        uint8_t *page=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
        if(!page){break;}
        kmemset(page,0,ATM_PAGE_SIZE);
        if(paging_map_user_page(space,va,(uintptr_t)page,linux_pte_flags(prot))<0){kfree(page);break;}
    }
    if(va!=end){
        for(uint64_t rollback=start;rollback<va;rollback+=ATM_PAGE_SIZE) (void)paging_release_user_page(space,rollback);
        return linux_error(ATM_ENOMEM);
    }
    linux_update_resident(task,space);
    return start;
}
static uint64_t linux_munmap_anon(registers_t *r){
    task_t *task=sched_current();
    user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    uint64_t end=0;
    if(!task||!space||!space->valid||linux_page_span(r->rdi,r->rsi,&end)<0) return linux_error(ATM_EINVAL);
    for(uint64_t va=r->rdi;va<end;va+=ATM_PAGE_SIZE){uintptr_t phys=0;if(paging_user_translate(space,va,&phys,0)<0)return linux_error(ATM_EINVAL);}
    for(uint64_t va=r->rdi;va<end;va+=ATM_PAGE_SIZE) (void)paging_release_user_page(space,va);
    linux_update_resident(task,space);
    return 0;
}
static uint64_t linux_mprotect_anon(registers_t *r){
    task_t *task=sched_current();
    user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    uint64_t prot=r->rdx,end=0;
    if(!task||!space||!space->valid||!prot||(prot&~(LINUX_PROT_READ|LINUX_PROT_WRITE|LINUX_PROT_EXEC))||linux_page_span(r->rdi,r->rsi,&end)<0) return linux_error(ATM_EINVAL);
    for(uint64_t va=r->rdi;va<end;va+=ATM_PAGE_SIZE){uintptr_t phys=0;if(paging_user_translate(space,va,&phys,0)<0)return linux_error(ATM_EINVAL);}
    for(uint64_t va=r->rdi;va<end;va+=ATM_PAGE_SIZE) (void)paging_protect_user_page(space,va,linux_pte_flags(prot));
    return 0;
}

uint64_t linux_syscall_dispatch(registers_t *r){
    if(!r || (r->cs&3)!=3) return (uint64_t)-(int64_t)ATM_ENOSYS;
    switch((uint32_t)r->rax){
    case LINUX_NR_ARCH_PRCTL:
        return linux_arch_prctl(r);
    case LINUX_NR_SET_TID_ADDRESS:
        return linux_set_tid_address(r);
    case LINUX_NR_GETTID:
        return sched_current()?(uint64_t)sched_current()->pid:linux_error(ATM_ENOSYS);
    case LINUX_NR_UNAME:
        return linux_uname(r);
    case LINUX_NR_MMAP:
        return linux_mmap_anon(r);
    case LINUX_NR_OPEN:
        return linux_openat(r,0);
    case LINUX_NR_OPENAT:
        return linux_openat(r,1);
    case LINUX_NR_NEWFSTATAT:
        return linux_newfstatat(r);
    case LINUX_NR_GETDENTS64:
        return linux_getdents64(r);
    case LINUX_NR_MPROTECT:
        return linux_mprotect_anon(r);
    case LINUX_NR_MUNMAP:
        return linux_munmap_anon(r);
    case LINUX_NR_BRK:
        return atm_syscall_dispatch(r);
    case LINUX_NR_READ:
    case LINUX_NR_WRITE:
    case LINUX_NR_CLOSE:
    case LINUX_NR_GETPID:
    case LINUX_NR_EXIT:
        return atm_syscall_dispatch(r);
    case LINUX_NR_EXIT_GROUP:
        r->rax=ATM_SYS_EXIT;
        return atm_syscall_dispatch(r);
    default:
        return (uint64_t)-(int64_t)ATM_ENOSYS;
    }
}
