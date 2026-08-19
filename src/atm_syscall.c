#include "atm_syscall.h"
#include "atm_posix.h"
#include "sched.h"
#include "uaccess.h"
#include "native_fd.h"
#include "native_socket.h"
#include "kmalloc.h"
#include "vfs.h"
#include "util.h"
#include "ossdk.h"
#include <stdint.h>
#include <stddef.h>

/* The first native ABI deliberately caps a single copied I/O request.  Large
 * user writes are expected to be issued as several calls until process-owned
 * fd tables and asynchronous I/O arrive. */
#define ATM_SYS_IO_MAX (64u * 1024u)

static uint64_t sys_result(int64_t value) { return (uint64_t)value; }

uint32_t atm_syscall_abi_version(void) { return ATM_SYSCALL_ABI_V2; }

static const user_space_t *sys_user_space(int from_user){
    task_t *t=sched_current();
    if(!from_user || !t || !t->address_space) return NULL;
    user_space_t *space=(user_space_t *)t->address_space;
    return space->valid?space:NULL;
}

static int64_t sys_read_user(const user_space_t *space,task_t *task,int fd,void *user_buf,uint64_t count){
    if(!space || (!user_buf && count) || count>ATM_SYS_IO_MAX) return -ATM_EFAULT;
    if(!count) return 0;
    /* Validate before native_fd_read(): on EFAULT, do not consume stream data
     * or advance a file offset that a corrected userspace pointer may retry. */
    if(user_range_valid(space,user_buf,(size_t)count,1)<0) return -ATM_EFAULT;
    void *tmp=kmalloc((size_t)count);
    if(!tmp) return -ATM_ENOMEM;
    int64_t n=native_fd_read(task,fd,tmp,count);
    if(n>0 && copy_to_user(space,user_buf,tmp,(size_t)n)<0) n=-ATM_EFAULT;
    kfree(tmp);
    return n;
}

static int64_t sys_write_user(const user_space_t *space,task_t *task,int fd,const void *user_buf,uint64_t count){
    if(!space || (!user_buf && count) || count>ATM_SYS_IO_MAX) return -ATM_EFAULT;
    if(!count) return 0;
    void *tmp=kmalloc((size_t)count);
    if(!tmp) return -ATM_ENOMEM;
    int64_t n;
    if(copy_from_user(space,tmp,user_buf,(size_t)count)<0) n=-ATM_EFAULT;
    else n=native_fd_write(task,fd,tmp,count);
    kfree(tmp);
    return n;
}

static int64_t sys_open_user(const user_space_t *space,task_t *task,const char *user_path,uint32_t flags,uint32_t mode){
    char path[VFS_PATH_MAX];
    if(!space || copy_string_from_user(space,path,sizeof(path),user_path)<0) return -ATM_EFAULT;
    return native_fd_open(task,path,flags,mode);
}

static int64_t sys_fstat_user(const user_space_t *space,task_t *task,int fd,void *user_stat){
    atm_posix_stat_t st;
    if(!space || !user_stat) return -ATM_EFAULT;
    int rc=native_fd_fstat(task,fd,&st);
    if(rc<0) return rc;
    return copy_to_user(space,user_stat,&st,sizeof(st))<0?-ATM_EFAULT:0;
}

static int64_t sys_socket_user(const user_space_t *space,task_t *task,int domain,int type,int protocol){
    if(!space || !task) return -ATM_EFAULT;
    return native_socket_open(task,domain,type,protocol);
}
static int64_t sys_connect_user(const user_space_t *space,task_t *task,int fd,const void *user_addr,uint64_t len){
    atm_sockaddr_in_t addr;
    if(!space || !task || !user_addr || len!=sizeof(addr)) return -ATM_EFAULT;
    if(copy_from_user(space,&addr,user_addr,sizeof(addr))<0) return -ATM_EFAULT;
    return native_socket_connect(task,fd,&addr,300u);
}
static int64_t sys_bind_user(const user_space_t *space,task_t *task,int fd,const void *user_addr,uint64_t len){
    atm_sockaddr_in_t addr;
    if(!space || !task || !user_addr || len!=sizeof(addr)) return -ATM_EFAULT;
    if(copy_from_user(space,&addr,user_addr,sizeof(addr))<0) return -ATM_EFAULT;
    return native_socket_bind(task,fd,&addr);
}
static int64_t sys_accept_user(const user_space_t *space,task_t *task,int fd,void *user_addr,uint64_t len){
    atm_sockaddr_in_t peer;
    if(!space || !task || (user_addr && len!=sizeof(peer)) || (!user_addr && len)) return -ATM_EFAULT;
    if(user_addr && user_range_valid(space,user_addr,sizeof(peer),1)<0) return -ATM_EFAULT;
    int child=native_socket_accept(task,fd,user_addr?&peer:NULL,300u);
    if(child<0) return child;
    if(user_addr && copy_to_user(space,user_addr,&peer,sizeof(peer))<0){(void)native_socket_close(task,child);return -ATM_EFAULT;}
    return child;
}

/* Linux-style brk returns the current break on failure. v0.9 only grows an
 * anonymous private heap and retains pages on shrink; unmapping/freeing is a
 * later allocator milestone. */
static uint64_t sys_brk_native(task_t *task,uint64_t requested){
    user_space_t *space=task?(user_space_t *)task->address_space:NULL;
    if(!task || !space || !space->valid || !task->user_brk_base) return 0;
    if(!requested) return task->user_brk;
    if(requested<task->user_brk_base || requested>=ATM_USER_STACK_TOP) return task->user_brk;
    uint64_t grow_to=(requested+ATM_PAGE_SIZE-1)&ATM_PAGE_MASK;
    if(grow_to>=ATM_USER_STACK_TOP) return task->user_brk;
    uint64_t mapped_from=(task->user_brk+ATM_PAGE_SIZE-1)&ATM_PAGE_MASK;
    for(uint64_t va=mapped_from;va<grow_to;va+=ATM_PAGE_SIZE){
        uintptr_t phys=0;
        if(paging_user_translate(space,va,&phys,0)==0) continue;
        uint8_t *page=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
        if(!page || paging_map_user_page(space,va,(uintptr_t)page,ATM_PTE_W)<0) return task->user_brk;
    }
    task->user_brk=requested;
    return task->user_brk;
}

uint64_t atm_syscall_dispatch(registers_t *r) {
    if(!r) return sys_result(-ATM_EINVAL);

    int from_user=((r->cs&3u)==3u);
    const user_space_t *space=sys_user_space(from_user);
    task_t *task=sched_current();
    switch((uint32_t)r->rax) {
    case ATM_SYS_READ:
        return from_user ? sys_result(sys_read_user(space,task,(int)r->rdi,(void *)(uintptr_t)r->rsi,r->rdx))
                         : sys_result(atm_posix_read((int)r->rdi,(void *)(uintptr_t)r->rsi,r->rdx));
    case ATM_SYS_WRITE:
        return from_user ? sys_result(sys_write_user(space,task,(int)r->rdi,(const void *)(uintptr_t)r->rsi,r->rdx))
                         : sys_result(atm_posix_write((int)r->rdi,(const void *)(uintptr_t)r->rsi,r->rdx));
    case ATM_SYS_OPEN:
        return from_user ? sys_result(sys_open_user(space,task,(const char *)(uintptr_t)r->rdi,(uint32_t)r->rsi,(uint32_t)r->rdx))
                         : sys_result(atm_posix_open((const char *)(uintptr_t)r->rdi,(uint32_t)r->rsi,(uint32_t)r->rdx));
    case ATM_SYS_CLOSE:
        return from_user ? sys_result(native_fd_close(task,(int)r->rdi))
                         : sys_result(atm_posix_close((int)r->rdi));
    case ATM_SYS_FSTAT:
        if(from_user) return sys_result(sys_fstat_user(space,task,(int)r->rdi,(void *)(uintptr_t)r->rsi));
        return sys_result(atm_posix_fstat((int)r->rdi,(atm_posix_stat_t *)(uintptr_t)r->rsi));
    case ATM_SYS_LSEEK:
        return from_user ? sys_result(native_fd_lseek(task,(int)r->rdi,(int64_t)r->rsi,(int)r->rdx))
                         : sys_result(atm_posix_lseek((int)r->rdi,(int64_t)r->rsi,(int)r->rdx));
    case ATM_SYS_SOCKET:
        return from_user ? sys_result(sys_socket_user(space,task,(int)r->rdi,(int)r->rsi,(int)r->rdx)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_CONNECT:
        return from_user ? sys_result(sys_connect_user(space,task,(int)r->rdi,(const void *)(uintptr_t)r->rsi,r->rdx)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_BIND:
        return from_user ? sys_result(sys_bind_user(space,task,(int)r->rdi,(const void *)(uintptr_t)r->rsi,r->rdx)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_LISTEN:
        return from_user ? sys_result(native_socket_listen(task,(int)r->rdi,(int)r->rsi)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_ACCEPT:
        return from_user ? sys_result(sys_accept_user(space,task,(int)r->rdi,(void *)(uintptr_t)r->rsi,r->rdx)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_BRK:
        return from_user ? sys_brk_native(task,r->rdi) : 0;
    case ATM_SYS_EXIT:
        if(from_user) task_exit_from_syscall((int)r->rdi);
        task_exit((int)r->rdi);
        return 0;
    case ATM_SYS_WAITPID: {
        int status=0;
        int *out=from_user?NULL:(int *)(uintptr_t)r->rsi;
        int ret=task_waitpid((uint32_t)r->rdi,r->rsi?&status:0);
        if(ret>0 && r->rsi){
            if(from_user){
                if(!space || copy_to_user(space,(void *)(uintptr_t)r->rsi,&status,sizeof(status))<0)
                    return sys_result(-ATM_EFAULT);
            } else *out=status;
        }
        return sys_result(ret);
    }
    case ATM_SYS_KILL:
        return sys_result(task_send_signal((uint32_t)r->rdi,(int)r->rsi));
    case ATM_SYS_GETPID: {
        task_t *t=sched_current();
        return t ? (uint64_t)t->pid : 0;
    }
    case ATM_SYS_GETPPID:
        return (uint64_t)atm_posix_getppid();
    case ATM_SYS_GETUID:
        return (uint64_t)atm_posix_getuid();
    case ATM_SYS_GETGID:
        return (uint64_t)atm_posix_getgid();
    case ATM_SYS_GETTID: {
        task_t *t=sched_current();
        return t ? (uint64_t)t->pid : 0;
    }
    case ATM_SYS_ABI_INFO:
        return (uint64_t)ATM_SYSCALL_ABI_V2;
    default:
        return sys_result(-ATM_ENOSYS);
    }
}

int atm_syscall_selftest(void){
    task_t *task=sched_current();
    if(!task) return -1;
    user_space_t space;
    uint8_t *page=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
    if(!page || paging_create_user_space(&space)<0 ||
       paging_map_user_page(&space,ATM_USER_BASE,(uintptr_t)page,ATM_PTE_W)<0) return -1;

    void *saved_space=task->address_space;
    int saved_map[TASK_FD_MAX],saved_socket_map[TASK_FD_MAX];
    uint32_t saved_fd_ready=task->fd_table_ready;
    kmemcpy(saved_map,task->fd_map,sizeof(saved_map));
    kmemcpy(saved_socket_map,task->socket_map,sizeof(saved_socket_map));
    int rc=-1,fd=-1,sock=-1,stage=1;
    task->address_space=&space;
    native_fd_task_init(task);
    stage=2;
    if(native_socket_selftest()<0) goto done;
    const char *path="/tmp/.atm-syscall-uaccess";
    const char *payload="abi-uaccess";
    registers_t r;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_SOCKET; r.rdi=ATM_AF_INET; r.rsi=ATM_SOCK_STREAM; r.rdx=ATM_IPPROTO_TCP;
    stage=3;
    sock=(int64_t)atm_syscall_dispatch(&r); if(sock<3) goto done;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_CLOSE; r.rdi=(uint64_t)sock;
    if((int64_t)atm_syscall_dispatch(&r)<0) goto done; sock=-1;
    stage=4;
    if(copy_to_user(&space,(void *)(uintptr_t)(ATM_USER_BASE+0x80),path,kstrlen(path)+1)<0) goto done;
    r.rax=ATM_SYS_OPEN; r.rdi=ATM_USER_BASE+0x80; r.rsi=O_WRONLY|O_CREAT|O_TRUNC; r.rdx=0600;
    fd=(int64_t)atm_syscall_dispatch(&r); if(fd<0) goto done;
    stage=5;
    if(copy_to_user(&space,(void *)(uintptr_t)(ATM_USER_BASE+0x180),payload,kstrlen(payload))<0) goto done;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_WRITE; r.rdi=(uint64_t)fd; r.rsi=ATM_USER_BASE+0x180; r.rdx=kstrlen(payload);
    if((int64_t)atm_syscall_dispatch(&r)!=(int64_t)kstrlen(payload)) goto done;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_CLOSE; r.rdi=(uint64_t)fd;
    if((int64_t)atm_syscall_dispatch(&r)<0) goto done; fd=-1;
    stage=6;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_OPEN; r.rdi=ATM_USER_BASE+0x80; r.rsi=O_RDONLY;
    fd=(int64_t)atm_syscall_dispatch(&r); if(fd<0) goto done;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_READ; r.rdi=(uint64_t)fd; r.rsi=ATM_USER_BASE+0x280; r.rdx=kstrlen(payload);
    if((int64_t)atm_syscall_dispatch(&r)!=(int64_t)kstrlen(payload)) goto done;
    char out[32]; kmemset(out,0,sizeof(out));
    if(copy_from_user(&space,out,(const void *)(uintptr_t)(ATM_USER_BASE+0x280),kstrlen(payload))<0 || kstrncmp(out,payload,kstrlen(payload))) goto done;
    stage=7;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_FSTAT; r.rdi=(uint64_t)fd; r.rsi=ATM_USER_BASE+0x380;
    if((int64_t)atm_syscall_dispatch(&r)<0) goto done;
    /* A bad output pointer must fail before native_fd_read() advances offset. */
    stage=8;
    if(native_fd_lseek(task,fd,0,0)<0) goto done;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_READ; r.rdi=(uint64_t)fd; r.rsi=ATM_USER_TOP+8; r.rdx=kstrlen(payload);
    if((int64_t)atm_syscall_dispatch(&r)!=-ATM_EFAULT) goto done;
    stage=9;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_READ; r.rdi=(uint64_t)fd; r.rsi=ATM_USER_BASE+0x480; r.rdx=kstrlen(payload);
    if((int64_t)atm_syscall_dispatch(&r)!=(int64_t)kstrlen(payload)) goto done;
    kmemset(out,0,sizeof(out));
    if(copy_from_user(&space,out,(const void *)(uintptr_t)(ATM_USER_BASE+0x480),kstrlen(payload))<0 || kstrncmp(out,payload,kstrlen(payload))) goto done;
    stage=10;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_WRITE; r.rdi=(uint64_t)fd; r.rsi=ATM_USER_TOP+8; r.rdx=1;
    if((int64_t)atm_syscall_dispatch(&r)!=-ATM_EFAULT) goto done;
    rc=0;
done:
    if(rc<0){
        char number[16];
        kuitoa((uint32_t)stage,number,10);
        sdk_serial_write("[syscall] usercopy-selftest-fail stage=");
        sdk_serial_write(number);
        sdk_serial_write("\n");
    }
    if(sock>=0) native_fd_close(task,sock);
    if(fd>=0) native_fd_close(task,fd);
    native_fd_task_cleanup(task);
    (void)atm_posix_unlink(path);
    task->address_space=saved_space;
    kmemcpy(task->fd_map,saved_map,sizeof(saved_map));
    kmemcpy(task->socket_map,saved_socket_map,sizeof(saved_socket_map));
    task->fd_table_ready=saved_fd_ready;
    return rc;
}
