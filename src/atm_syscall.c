#include "atm_syscall.h"
#include "atm_posix.h"
#include "sched.h"
#include "uaccess.h"
#include "native_pipe.h"
#include "native_fd.h"
#include "native_dir.h"
#include "native_socket.h"
#include "net_tcp.h"
#include "native_app.h"
#include "kmalloc.h"
#include "vfs.h"
#include "util.h"
#include "ossdk.h"
#include "atm_time.h"
#include "atm_uname.h"
#include <stdint.h>
#include <stddef.h>

/* The first native ABI deliberately caps a single copied I/O request.  Large
 * user writes are expected to be issued as several calls until process-owned
 * fd tables and asynchronous I/O arrive. */
#define ATM_SYS_IO_MAX (64u * 1024u)
#define ATM_SYS_IOV_MAX 16
#define ATM_SYS_POLL_MAX 16
#define ATM_AT_FDCWD (-100)
#define ATM_AT_SYMLINK_NOFOLLOW 0x100u
#define ATM_AT_REMOVEDIR 0x200u
#define ATM_MSG_DONTWAIT 0x40u

static uint64_t sys_result(int64_t value) { return (uint64_t)value; }

typedef struct {
    struct { int64_t tv_sec,tv_usec; } ru_utime,ru_stime;
    int64_t ru_maxrss,ru_ixrss,ru_idrss,ru_isrss,ru_minflt,ru_majflt,ru_nswap;
    int64_t ru_inblock,ru_oublock,ru_msgsnd,ru_msgrcv,ru_nsignals,ru_nvcsw,ru_nivcsw;
} atm_sys_rusage_t;
typedef struct { int64_t tms_utime,tms_stime,tms_cutime,tms_cstime; } atm_sys_tms_t;
typedef struct { uint64_t rlim_cur,rlim_max; } atm_sys_rlimit_t;
#define ATM_RLIMIT_STACK 3
#define ATM_RLIMIT_NOFILE 7
#define ATM_RLIMIT_AS 9

uint32_t atm_syscall_abi_version(void) { return ATM_SYSCALL_ABI_V22; }

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

static int64_t sys_poll_user(const user_space_t *space,task_t *task,void *user_fds,uint64_t nfds,uint64_t timeout){
    atm_native_pollfd_t fds[ATM_SYS_POLL_MAX];
    /* The native ABI accepts finite waits up to ten minutes. -1/infinite
     * waits stay unsupported because this bounded scheduler path has no
     * signal-interruption contract yet. */
    if(!space || !task || nfds>ATM_SYS_POLL_MAX || timeout>600000u) return -ATM_EINVAL;
    if(!nfds) return 0;
    if(!user_fds || user_range_valid(space,user_fds,(size_t)nfds*sizeof(fds[0]),1)<0) return -ATM_EFAULT;
    if(copy_from_user(space,fds,user_fds,(size_t)nfds*sizeof(fds[0]))<0) return -ATM_EFAULT;
    uint32_t ticks=(uint32_t)((timeout+9u)/10u),start=sched_uptime_ticks();
    int rc;
    for(;;){
        rc=native_fd_poll(task,fds,(uint32_t)nfds);
        if(rc<0) return -ATM_EINVAL;
        if(rc||!ticks||(uint32_t)(sched_uptime_ticks()-start)>=ticks) break;
        if(task_sleep_ticks_from_syscall(1)<0) return -ATM_EINVAL;
    }
    return copy_to_user(space,user_fds,fds,(size_t)nfds*sizeof(fds[0]))<0?-ATM_EFAULT:rc;
}

static int64_t sys_open_user(const user_space_t *space,task_t *task,const char *user_path,uint32_t flags,uint32_t mode){
    char path[VFS_PATH_MAX];
    if(!space || copy_string_from_user(space,path,sizeof(path),user_path)<0) return -ATM_EFAULT;
    return native_fd_open(task,path,flags,mode);
}
/* Native at-style path calls currently guarantee only AT_FDCWD lexical
 * resolution. A real directory-fd object model is intentionally deferred. */
static int64_t sys_openat_user(const user_space_t *space,task_t *task,int dirfd,const char *user_path,uint32_t flags,uint32_t mode){
    if(dirfd!=ATM_AT_FDCWD)return -ATM_EINVAL;
    return sys_open_user(space,task,user_path,flags,mode);
}
static int64_t sys_fstatat_user(const user_space_t *space,int dirfd,const char *user_path,void *user_stat,uint32_t flags);

static int64_t sys_fstat_user(const user_space_t *space,task_t *task,int fd,void *user_stat){
    atm_posix_stat_t st;
    if(!space || !user_stat) return -ATM_EFAULT;
    int rc=native_fd_fstat(task,fd,&st);
    if(rc<0) return rc;
    return copy_to_user(space,user_stat,&st,sizeof(st))<0?-ATM_EFAULT:0;
}

static int64_t sys_pipe_user(const user_space_t *space,task_t *task,void *user_fds,uint32_t flags){
    int fds[2];
    if(!space || !task || !user_fds || user_range_valid(space,user_fds,sizeof(fds),1)<0) return -ATM_EFAULT;
    if(native_fd_pipe2(task,fds,flags)<0) return -ATM_EINVAL;
    if(copy_to_user(space,user_fds,fds,sizeof(fds))<0){
        (void)native_fd_close(task,fds[0]);
        (void)native_fd_close(task,fds[1]);
        return -ATM_EFAULT;
    }
    return 0;
}

typedef struct __attribute__((packed)) {
    uint64_t d_ino;
    uint8_t d_type;
    char d_name[VFS_NAME_MAX+1];
} atm_user_dirent_t;

static int64_t sys_opendir_user(const user_space_t *space,task_t *task,const char *user_path){
    char path[VFS_PATH_MAX];
    if(!space || copy_string_from_user(space,path,sizeof(path),user_path)<0) return -ATM_EFAULT;
    return native_dir_open(task,path);
}
static int64_t sys_readdir_user(const user_space_t *space,task_t *task,int handle,void *user_entry){
    atm_posix_dirent_t entry;atm_user_dirent_t out;
    if(!space || !user_entry || user_range_valid(space,user_entry,sizeof(out),1)<0) return -ATM_EFAULT;
    int rc=native_dir_read(task,handle,&entry);
    if(rc<=0) return rc;
    out.d_ino=entry.ino;out.d_type=entry.d_type;kmemcpy(out.d_name,entry.name,sizeof(out.d_name));
    return copy_to_user(space,user_entry,&out,sizeof(out))<0?-ATM_EFAULT:1;
}
static int sys_path_copy(const user_space_t *space,const char *user_path,char path[VFS_PATH_MAX]){
    return (!space||copy_string_from_user(space,path,VFS_PATH_MAX,user_path)<0)?-ATM_EFAULT:0;
}
static int64_t sys_stat_user(const user_space_t *space,const char *user_path,void *user_stat,int follow){
    atm_posix_stat_t st;char *path=(char *)kmalloc(VFS_PATH_MAX);int64_t rc=-ATM_ENOMEM;
    if(!path)return rc;if(!user_stat){rc=-ATM_EFAULT;goto done;}
    if(sys_path_copy(space,user_path,path)<0){rc=-ATM_EFAULT;goto done;}
    rc=follow?atm_posix_stat(path,&st):atm_posix_lstat(path,&st);if(rc<0)goto done;
    rc=copy_to_user(space,user_stat,&st,sizeof(st))<0?-ATM_EFAULT:0;
 done:kfree(path);return rc;
}
static int64_t sys_fstatat_user(const user_space_t *space,int dirfd,const char *user_path,void *user_stat,uint32_t flags){
    if(dirfd!=ATM_AT_FDCWD||(flags&~ATM_AT_SYMLINK_NOFOLLOW))return -ATM_EINVAL;
    return sys_stat_user(space,user_path,user_stat,(flags&ATM_AT_SYMLINK_NOFOLLOW)?0:1);
}
static int64_t sys_truncate_user(const user_space_t *space,const char *user_path,uint64_t size){
    char *path=(char *)kmalloc(VFS_PATH_MAX);if(!path)return -ATM_ENOMEM;int64_t rc=sys_path_copy(space,user_path,path)<0?-ATM_EFAULT:atm_posix_truncate(path,size);kfree(path);return rc;
}
static int64_t sys_rename_user(const user_space_t *space,const char *user_old,const char *user_new){
    char *oldpath=(char *)kmalloc(VFS_PATH_MAX),*newpath=(char *)kmalloc(VFS_PATH_MAX);if(!oldpath||!newpath){if(oldpath)kfree(oldpath);if(newpath)kfree(newpath);return -ATM_ENOMEM;}
    int64_t rc=(sys_path_copy(space,user_old,oldpath)<0||sys_path_copy(space,user_new,newpath)<0)?-ATM_EFAULT:atm_posix_rename(oldpath,newpath);kfree(newpath);kfree(oldpath);return rc;
}
static int64_t sys_chmod_user(const user_space_t *space,const char *user_path,uint32_t mode){
    char *path=(char *)kmalloc(VFS_PATH_MAX);if(!path)return -ATM_ENOMEM;
    int64_t rc=sys_path_copy(space,user_path,path)<0?-ATM_EFAULT:atm_posix_chmod(path,mode&07777u);kfree(path);return rc;
}
static int64_t sys_chown_user(const user_space_t *space,const char *user_path,uint32_t uid,uint32_t gid){
    char *path=(char *)kmalloc(VFS_PATH_MAX);if(!path)return -ATM_ENOMEM;
    int64_t rc=sys_path_copy(space,user_path,path)<0?-ATM_EFAULT:atm_posix_chown(path,uid,gid);kfree(path);return rc;
}
static int64_t sys_getresid_user(const user_space_t *space,void *user_real,void *user_effective,void *user_saved,int group){
    uint32_t id=group?atm_posix_getgid():atm_posix_getuid();
    if(!space||!user_real||!user_effective||!user_saved||user_range_valid(space,user_real,sizeof(id),1)<0||user_range_valid(space,user_effective,sizeof(id),1)<0||user_range_valid(space,user_saved,sizeof(id),1)<0)return -ATM_EFAULT;
    if(copy_to_user(space,user_real,&id,sizeof(id))<0||copy_to_user(space,user_effective,&id,sizeof(id))<0||copy_to_user(space,user_saved,&id,sizeof(id))<0)return -ATM_EFAULT;
    return 0;
}
static int64_t sys_link_user(const user_space_t *space,const char *user_old,const char *user_new){
    char *oldpath=(char *)kmalloc(VFS_PATH_MAX),*newpath=(char *)kmalloc(VFS_PATH_MAX);if(!oldpath||!newpath){if(oldpath)kfree(oldpath);if(newpath)kfree(newpath);return -ATM_ENOMEM;}
    int64_t rc=(sys_path_copy(space,user_old,oldpath)<0||sys_path_copy(space,user_new,newpath)<0)?-ATM_EFAULT:atm_posix_link(oldpath,newpath);kfree(newpath);kfree(oldpath);return rc;
}
static int64_t sys_symlink_user(const user_space_t *space,const char *user_target,const char *user_link){
    char *target=(char *)kmalloc(VFS_PATH_MAX),*linkpath=(char *)kmalloc(VFS_PATH_MAX);if(!target||!linkpath){if(target)kfree(target);if(linkpath)kfree(linkpath);return -ATM_ENOMEM;}
    int64_t rc=(sys_path_copy(space,user_target,target)<0||sys_path_copy(space,user_link,linkpath)<0)?-ATM_EFAULT:atm_posix_symlink(target,linkpath);kfree(linkpath);kfree(target);return rc;
}
static int64_t sys_readlink_user(const user_space_t *space,const char *user_path,char *user_buf,uint64_t size){
    if(!space||!user_buf||!size||size>VFS_PATH_MAX)return -ATM_EINVAL;
    char *path=(char *)kmalloc(VFS_PATH_MAX),*target=(char *)kmalloc(VFS_PATH_MAX);if(!path||!target){if(path)kfree(path);if(target)kfree(target);return -ATM_ENOMEM;}
    int64_t rc;if(sys_path_copy(space,user_path,path)<0||user_range_valid(space,user_buf,(size_t)size,1)<0)rc=-ATM_EFAULT;else{int n=atm_posix_readlink(path,target,(size_t)size);rc=n<0?n:(n&&copy_to_user(space,user_buf,target,(size_t)n)<0?-ATM_EFAULT:n);}kfree(target);kfree(path);return rc;
}
static int64_t sys_mkdir_user(const user_space_t *space,const char *user_path,uint32_t mode);
static int64_t sys_access_user(const user_space_t *space,const char *user_path,int mode);
static int64_t sys_rmdir_user(const user_space_t *space,const char *user_path);
static int64_t sys_unlink_user(const user_space_t *space,const char *user_path);
static int64_t sys_mkdirat_user(const user_space_t *space,int dirfd,const char *path,uint32_t mode){
    return dirfd==ATM_AT_FDCWD?sys_mkdir_user(space,path,mode):-ATM_EINVAL;
}
static int64_t sys_faccessat_user(const user_space_t *space,int dirfd,const char *path,int mode,int flags){
    return dirfd==ATM_AT_FDCWD&&flags==0?sys_access_user(space,path,mode):-ATM_EINVAL;
}
static int64_t sys_unlinkat_user(const user_space_t *space,int dirfd,const char *path,uint32_t flags){
    if(dirfd!=ATM_AT_FDCWD)return -ATM_EINVAL;
    if(flags==0)return sys_unlink_user(space,path);
    return flags==ATM_AT_REMOVEDIR?sys_rmdir_user(space,path):-ATM_EINVAL;
}
static int64_t sys_renameat_user(const user_space_t *space,int olddirfd,const char *oldpath,int newdirfd,const char *newpath){
    return olddirfd==ATM_AT_FDCWD&&newdirfd==ATM_AT_FDCWD?sys_rename_user(space,oldpath,newpath):-ATM_EINVAL;
}
static int64_t sys_linkat_user(const user_space_t *space,int olddirfd,const char *oldpath,int newdirfd,const char *newpath,uint32_t flags){
    return olddirfd==ATM_AT_FDCWD&&newdirfd==ATM_AT_FDCWD&&flags==0?sys_link_user(space,oldpath,newpath):-ATM_EINVAL;
}
static int64_t sys_symlinkat_user(const user_space_t *space,const char *target,int newdirfd,const char *linkpath){
    return newdirfd==ATM_AT_FDCWD?sys_symlink_user(space,target,linkpath):-ATM_EINVAL;
}
static int64_t sys_readlinkat_user(const user_space_t *space,int dirfd,const char *path,char *buf,uint64_t size){
    return dirfd==ATM_AT_FDCWD?sys_readlink_user(space,path,buf,size):-ATM_EINVAL;
}
static int64_t sys_pread_user(const user_space_t *space,task_t *task,int fd,void *user_buf,uint64_t count,uint64_t offset){
    if(!space||(!user_buf&&count)||count>ATM_SYS_IO_MAX)return -ATM_EFAULT;
    if(!count)return 0;
    if(user_range_valid(space,user_buf,(size_t)count,1)<0)return -ATM_EFAULT;
    void *tmp=kmalloc((size_t)count);if(!tmp)return -ATM_ENOMEM;
    int64_t n=native_fd_pread(task,fd,tmp,count,offset);
    if(n>0&&copy_to_user(space,user_buf,tmp,(size_t)n)<0)n=-ATM_EFAULT;
    kfree(tmp);return n;
}
static int64_t sys_pwrite_user(const user_space_t *space,task_t *task,int fd,const void *user_buf,uint64_t count,uint64_t offset){
    if(!space||(!user_buf&&count)||count>ATM_SYS_IO_MAX)return -ATM_EFAULT;
    if(!count)return 0;
    void *tmp=kmalloc((size_t)count);if(!tmp)return -ATM_ENOMEM;
    int64_t n=copy_from_user(space,tmp,user_buf,(size_t)count)<0?-ATM_EFAULT:native_fd_pwrite(task,fd,tmp,count,offset);
    kfree(tmp);return n;
}
static int sys_iov_prepare(const user_space_t *space,const void *user_iov,int iovcnt,int output,atm_posix_iovec_t out[ATM_SYS_IOV_MAX],uint64_t *total){
    if(!space||iovcnt<0||iovcnt>ATM_SYS_IOV_MAX)return -ATM_EINVAL;
    *total=0;if(!iovcnt)return 0;
    if(!user_iov||copy_from_user(space,out,user_iov,(size_t)iovcnt*sizeof(*out))<0)return -ATM_EFAULT;
    for(int i=0;i<iovcnt;i++){
        if(out[i].iov_len&&(!out[i].iov_base||user_range_valid(space,out[i].iov_base,(size_t)out[i].iov_len,output)<0))return -ATM_EFAULT;
        if(out[i].iov_len>ATM_SYS_IO_MAX-*total)return -ATM_EINVAL;
        *total+=out[i].iov_len;
    }
    return 0;
}
static int64_t sys_readv_user(const user_space_t *space,task_t *task,int fd,const void *user_iov,int iovcnt){
    atm_posix_iovec_t iov[ATM_SYS_IOV_MAX],kernel_iov[ATM_SYS_IOV_MAX];uint64_t total=0;
    int rc=sys_iov_prepare(space,user_iov,iovcnt,1,iov,&total);if(rc<0)return rc;if(!total)return 0;
    uint8_t *tmp=(uint8_t *)kmalloc((size_t)total);if(!tmp)return -ATM_ENOMEM;uint64_t off=0;
    for(int i=0;i<iovcnt;i++){kernel_iov[i].iov_base=tmp+off;kernel_iov[i].iov_len=iov[i].iov_len;off+=iov[i].iov_len;}
    int64_t n=native_fd_readv(task,fd,kernel_iov,iovcnt);
    if(n>0){uint64_t copied=0;for(int i=0;i<iovcnt&&copied<(uint64_t)n;i++){uint64_t part=iov[i].iov_len;if(part>(uint64_t)n-copied)part=(uint64_t)n-copied;if(part&&copy_to_user(space,iov[i].iov_base,tmp+copied,(size_t)part)<0){n=-ATM_EFAULT;break;}copied+=part;}}
    kfree(tmp);return n;
}
static int64_t sys_writev_user(const user_space_t *space,task_t *task,int fd,const void *user_iov,int iovcnt){
    atm_posix_iovec_t iov[ATM_SYS_IOV_MAX],kernel_iov[ATM_SYS_IOV_MAX];uint64_t total=0;
    int rc=sys_iov_prepare(space,user_iov,iovcnt,0,iov,&total);if(rc<0)return rc;if(!total)return 0;
    uint8_t *tmp=(uint8_t *)kmalloc((size_t)total);if(!tmp)return -ATM_ENOMEM;uint64_t off=0;
    for(int i=0;i<iovcnt;i++){kernel_iov[i].iov_base=tmp+off;kernel_iov[i].iov_len=iov[i].iov_len;if(iov[i].iov_len&&copy_from_user(space,tmp+off,iov[i].iov_base,(size_t)iov[i].iov_len)<0){kfree(tmp);return -ATM_EFAULT;}off+=iov[i].iov_len;}
    int64_t n=native_fd_writev(task,fd,kernel_iov,iovcnt);kfree(tmp);return n;
}
static int64_t sys_preadv_user(const user_space_t *space,task_t *task,int fd,const void *user_iov,int iovcnt,uint64_t offset){
    atm_posix_iovec_t iov[ATM_SYS_IOV_MAX],kernel_iov[ATM_SYS_IOV_MAX];uint64_t total=0;
    int rc=sys_iov_prepare(space,user_iov,iovcnt,1,iov,&total);if(rc<0)return rc;if(!total)return 0;
    uint8_t *tmp=(uint8_t *)kmalloc((size_t)total);if(!tmp)return -ATM_ENOMEM;uint64_t off=0;
    for(int i=0;i<iovcnt;i++){kernel_iov[i].iov_base=tmp+off;kernel_iov[i].iov_len=iov[i].iov_len;off+=iov[i].iov_len;}
    int64_t n=native_fd_preadv(task,fd,kernel_iov,iovcnt,offset);
    if(n>0){uint64_t copied=0;for(int i=0;i<iovcnt&&copied<(uint64_t)n;i++){uint64_t part=iov[i].iov_len;if(part>(uint64_t)n-copied)part=(uint64_t)n-copied;if(part&&copy_to_user(space,iov[i].iov_base,tmp+copied,(size_t)part)<0){n=-ATM_EFAULT;break;}copied+=part;}}
    kfree(tmp);return n;
}
static int64_t sys_pwritev_user(const user_space_t *space,task_t *task,int fd,const void *user_iov,int iovcnt,uint64_t offset){
    atm_posix_iovec_t iov[ATM_SYS_IOV_MAX],kernel_iov[ATM_SYS_IOV_MAX];uint64_t total=0;
    int rc=sys_iov_prepare(space,user_iov,iovcnt,0,iov,&total);if(rc<0)return rc;if(!total)return 0;
    uint8_t *tmp=(uint8_t *)kmalloc((size_t)total);if(!tmp)return -ATM_ENOMEM;uint64_t off=0;
    for(int i=0;i<iovcnt;i++){kernel_iov[i].iov_base=tmp+off;kernel_iov[i].iov_len=iov[i].iov_len;if(iov[i].iov_len&&copy_from_user(space,tmp+off,iov[i].iov_base,(size_t)iov[i].iov_len)<0){kfree(tmp);return -ATM_EFAULT;}off+=iov[i].iov_len;}
    int64_t n=native_fd_pwritev(task,fd,kernel_iov,iovcnt,offset);kfree(tmp);return n;
}
static int64_t sys_getrlimit_user(const user_space_t *space,int resource,void *user_limit){
    atm_sys_rlimit_t out;
    if(!space||!user_limit||user_range_valid(space,user_limit,sizeof(out),1)<0)return -ATM_EFAULT;
    switch(resource){
    case ATM_RLIMIT_STACK:out.rlim_cur=out.rlim_max=TASK_STACK_SIZE;break;
    case ATM_RLIMIT_NOFILE:out.rlim_cur=out.rlim_max=TASK_FD_MAX;break;
    case ATM_RLIMIT_AS:out.rlim_cur=out.rlim_max=ATM_USER_WINDOW_SIZE;break;
    default:return -ATM_EINVAL;
    }
    return copy_to_user(space,user_limit,&out,sizeof(out))<0?-ATM_EFAULT:0;
}
static int64_t sys_times_user(const user_space_t *space,task_t *task,void *user_tms){
    atm_sys_tms_t out;
    if(!space||!task)return -ATM_EFAULT;
    kmemset(&out,0,sizeof(out));out.tms_utime=(int64_t)task->ticks;
    if(user_tms){
        if(user_range_valid(space,user_tms,sizeof(out),1)<0||copy_to_user(space,user_tms,&out,sizeof(out))<0)return -ATM_EFAULT;
    }
    return (int64_t)sched_uptime_ticks();
}
static int64_t sys_getrusage_user(const user_space_t *space,task_t *task,int who,void *user_usage){
    atm_sys_rusage_t out;
    if(!space||!task||who!=0)return -ATM_EINVAL;
    if(!user_usage||user_range_valid(space,user_usage,sizeof(out),1)<0)return -ATM_EFAULT;
    kmemset(&out,0,sizeof(out));
    out.ru_utime.tv_sec=(int64_t)(task->ticks/100u);
    out.ru_utime.tv_usec=(int64_t)((task->ticks%100u)*10000u);
    out.ru_maxrss=(int64_t)(task->resident_bytes/1024u);
    out.ru_nvcsw=(int64_t)task->context_switches;
    return copy_to_user(space,user_usage,&out,sizeof(out))<0?-ATM_EFAULT:0;
}
static int64_t sys_getcwd_user(const user_space_t *space,char *user_buf,uint64_t size){
    char cwd[VFS_PATH_MAX];if(!space||!user_buf||!size||size>VFS_PATH_MAX)return -ATM_EFAULT;
    if(!atm_posix_getcwd(cwd,sizeof(cwd)))return -ATM_EINVAL;
    size_t n=kstrlen(cwd)+1;if(size<n)return -ATM_EINVAL;
    return copy_to_user(space,user_buf,cwd,n)<0?-ATM_EFAULT:(int64_t)(uintptr_t)user_buf;
}
static int64_t sys_chdir_user(const user_space_t *space,const char *user_path){char path[VFS_PATH_MAX];return sys_path_copy(space,user_path,path)<0?-ATM_EFAULT:atm_posix_chdir(path);}
static int64_t sys_access_user(const user_space_t *space,const char *user_path,int mode){char path[VFS_PATH_MAX];if(mode&~7)return -ATM_EINVAL;return sys_path_copy(space,user_path,path)<0?-ATM_EFAULT:atm_posix_access(path,mode);}
static int64_t sys_mkdir_user(const user_space_t *space,const char *user_path,uint32_t mode){char path[VFS_PATH_MAX];return sys_path_copy(space,user_path,path)<0?-ATM_EFAULT:atm_posix_mkdir(path,mode);}
static int64_t sys_rmdir_user(const user_space_t *space,const char *user_path){char path[VFS_PATH_MAX];return sys_path_copy(space,user_path,path)<0?-ATM_EFAULT:atm_posix_rmdir(path);}
static int64_t sys_unlink_user(const user_space_t *space,const char *user_path){char path[VFS_PATH_MAX];return sys_path_copy(space,user_path,path)<0?-ATM_EFAULT:atm_posix_unlink(path);}

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
static int64_t sys_send_user(const user_space_t *space,task_t *task,int fd,const void *user_buf,uint64_t count,uint32_t flags){
    uint8_t *tmp;int64_t rc;
    if(!space||!task||!user_buf||!count||count>ATM_TCP_MSS||flags)return -ATM_EINVAL;
    if(user_range_valid(space,user_buf,(size_t)count,0)<0)return -ATM_EFAULT;
    tmp=(uint8_t *)kmalloc((size_t)count);if(!tmp)return -ATM_ENOMEM;
    rc=copy_from_user(space,tmp,user_buf,(size_t)count)<0?-ATM_EFAULT:native_socket_send(task,fd,tmp,count);
    kfree(tmp);return rc;
}
static int64_t sys_recv_user(const user_space_t *space,task_t *task,int fd,void *user_buf,uint64_t count,uint32_t flags){
    uint8_t *tmp;int64_t rc;uint32_t timeout;
    if(!space||!task||!user_buf||!count||count>ATM_TCP_MSS||(flags&~ATM_MSG_DONTWAIT))return -ATM_EINVAL;
    if(user_range_valid(space,user_buf,(size_t)count,1)<0)return -ATM_EFAULT;
    timeout=(flags&ATM_MSG_DONTWAIT)?1u:300u;
    tmp=(uint8_t *)kmalloc((size_t)count);if(!tmp)return -ATM_ENOMEM;
    rc=native_socket_recv(task,fd,tmp,count,timeout);
    if(rc>0&&copy_to_user(space,user_buf,tmp,(size_t)rc)<0)rc=-ATM_EFAULT;
    kfree(tmp);return rc;
}
static int64_t sys_bind_user(const user_space_t *space,task_t *task,int fd,const void *user_addr,uint64_t len){
    atm_sockaddr_in_t addr;
    if(!space || !task || !user_addr || len!=sizeof(addr)) return -ATM_EFAULT;
    if(copy_from_user(space,&addr,user_addr,sizeof(addr))<0) return -ATM_EFAULT;
    return native_socket_bind(task,fd,&addr);
}
typedef struct { uint64_t bits; } atm_select_fdset_t;
typedef struct { int64_t tv_sec; int64_t tv_usec; } atm_select_timeval_t;
static int64_t sys_select_user(const user_space_t *space,task_t *task,int nfds,void *read_ptr,void *write_ptr,void *except_ptr,const void *timeout_ptr){
    atm_select_fdset_t in_r={0},in_w={0},in_e={0},out_r={0},out_w={0},out_e={0};
    atm_select_timeval_t timeout;
    if(!space||!task||nfds<0||nfds>64) return -ATM_EINVAL;
    if(read_ptr&&copy_from_user(space,&in_r,read_ptr,sizeof(in_r))<0) return -ATM_EFAULT;
    if(write_ptr&&copy_from_user(space,&in_w,write_ptr,sizeof(in_w))<0) return -ATM_EFAULT;
    if(except_ptr&&copy_from_user(space,&in_e,except_ptr,sizeof(in_e))<0) return -ATM_EFAULT;
    uint32_t ticks=0,start=0;
    if(timeout_ptr){
        if(copy_from_user(space,&timeout,timeout_ptr,sizeof(timeout))<0) return -ATM_EFAULT;
        /* Finite waits up to ten minutes, rounded upward to 10 ms PIT ticks.
         * A NULL timeout (infinite select) remains unsupported. */
        if(timeout.tv_sec<0||timeout.tv_usec<0||timeout.tv_usec>=1000000LL||timeout.tv_sec>600) return -ATM_EINVAL;
        uint64_t raw=(uint64_t)timeout.tv_sec*100ULL+((uint64_t)timeout.tv_usec+9999ULL)/10000ULL;
        if(raw>60000ULL)return -ATM_EINVAL;ticks=(uint32_t)raw;start=sched_uptime_ticks();
    }
    uint64_t valid_mask=nfds==64?~0ULL:((1ULL<<nfds)-1ULL);
    if(((in_r.bits|in_w.bits|in_e.bits)&~valid_mask)!=0) return -ATM_EINVAL;
    atm_native_pollfd_t poll[ATM_NATIVE_POLL_MAX];uint32_t count=0;
    for(int fd=0;fd<nfds;fd++){
        uint64_t bit=1ULL<<fd;uint16_t events=0;
        if(in_r.bits&bit)events|=ATM_POLLIN;
        if(in_w.bits&bit)events|=ATM_POLLOUT;
        if(in_e.bits&bit)events|=ATM_POLLERR;
        if(!events)continue;
        if(count>=ATM_NATIVE_POLL_MAX)return -ATM_EINVAL;
        poll[count].fd=fd;poll[count].events=events;poll[count].revents=0;count++;
    }
    int poll_rc;
    for(;;){
        poll_rc=native_fd_poll(task,poll,count);
        if(poll_rc<0)return -ATM_EINVAL;
        if(poll_rc||!ticks||(uint32_t)(sched_uptime_ticks()-start)>=ticks)break;
        if(task_sleep_ticks_from_syscall(1)<0)return -ATM_EINVAL;
    }
    int ready=0;
    for(uint32_t i=0;i<count;i++){
        int fd=poll[i].fd;uint64_t bit=1ULL<<fd;uint16_t ev=poll[i].revents;int hit=0;
        if((in_r.bits&bit)&&(ev&(ATM_POLLIN|ATM_POLLHUP|ATM_POLLERR|ATM_POLLNVAL))){out_r.bits|=bit;hit=1;}
        if((in_w.bits&bit)&&(ev&(ATM_POLLOUT|ATM_POLLERR|ATM_POLLNVAL))){out_w.bits|=bit;hit=1;}
        if((in_e.bits&bit)&&(ev&(ATM_POLLERR|ATM_POLLNVAL))){out_e.bits|=bit;hit=1;}
        if(hit)ready++;
    }
    if(read_ptr&&copy_to_user(space,read_ptr,&out_r,sizeof(out_r))<0)return -ATM_EFAULT;
    if(write_ptr&&copy_to_user(space,write_ptr,&out_w,sizeof(out_w))<0)return -ATM_EFAULT;
    if(except_ptr&&copy_to_user(space,except_ptr,&out_e,sizeof(out_e))<0)return -ATM_EFAULT;
    return ready;
}

static int64_t sys_clock_gettime_user(const user_space_t *space,int clock_id,void *user_out){
    atm_timespec_t value;
    if(!space||!user_out) return -ATM_EFAULT;
    int rc=atm_clock_gettime(clock_id,&value);
    if(rc<0) return rc;
    return copy_to_user(space,user_out,&value,sizeof(value))<0?-ATM_EFAULT:0;
}
static int64_t sys_clock_getres_user(const user_space_t *space,int clock_id,void *user_out){
    atm_timespec_t value;
    if(!space||!user_out) return -ATM_EFAULT;
    int rc=atm_clock_getres(clock_id,&value);
    if(rc<0) return rc;
    return copy_to_user(space,user_out,&value,sizeof(value))<0?-ATM_EFAULT:0;
}
static int64_t sys_nanosleep_user(const user_space_t *space,task_t *task,const void *user_req,void *user_rem){
    atm_timespec_t req,zero={0,0};
    if(!space||!task||!user_req) return -ATM_EFAULT;
    if(copy_from_user(space,&req,user_req,sizeof(req))<0) return -ATM_EFAULT;
    if(req.tv_sec<0 || req.tv_nsec<0 || req.tv_nsec>=1000000000LL) return -ATM_EINVAL;
    /* PIT is 100 Hz: round upward so a successful return is never earlier
     * than the requested duration. The scheduler stores a 32-bit deadline. */
    if((uint64_t)req.tv_sec>42949671ULL) return -ATM_EINVAL;
    uint64_t ticks=(uint64_t)req.tv_sec*100ULL+((uint64_t)req.tv_nsec+9999999ULL)/10000000ULL;
    if(ticks>0xffffffffULL) return -ATM_EINVAL;
    if(user_rem && copy_to_user(space,user_rem,&zero,sizeof(zero))<0) return -ATM_EFAULT;
    return task_sleep_ticks_from_syscall((uint32_t)ticks)<0?-ATM_EINVAL:0;
}
static int64_t sys_uname_user(const user_space_t *space,void *user_out){
    atm_utsname_t value;
    if(!space||!user_out) return -ATM_EFAULT;
    atm_uname_fill(&value);
    return copy_to_user(space,user_out,&value,sizeof(value))<0?-ATM_EFAULT:0;
}
static int64_t sys_gettimeofday_user(const user_space_t *space,void *user_out){
    atm_timeval_t value;
    if(!space||!user_out) return -ATM_EFAULT;
    int rc=atm_gettimeofday(&value);
    if(rc<0) return rc;
    return copy_to_user(space,user_out,&value,sizeof(value))<0?-ATM_EFAULT:0;
}

/* The syscall copies every accepted user pointer and string into a compact
 * kernel payload before loading a replacement image. This keeps old-task state
 * untouched on malformed vectors and caps stack construction deterministically. */
static int64_t sys_exec_copy_vector(const user_space_t *space,const uint64_t *user_vector,
                                    native_exec_payload_t *payload,uint16_t *offsets,
                                    uint32_t *count,uint32_t maximum){
    if(!user_vector)return 0;
    for(uint32_t i=0;i<maximum;i++){
        uint64_t user_string=0;size_t length=0;uint32_t room;
        if(copy_from_user(space,&user_string,user_vector+i,sizeof(user_string))<0)return -ATM_EFAULT;
        if(!user_string)return 0;
        if(payload->bytes>=ATM_NATIVE_EXEC_STR_BYTES)return -ATM_EINVAL;
        room=ATM_NATIVE_EXEC_STR_BYTES-payload->bytes;
        if(strnlen_user(space,(const char *)(uintptr_t)user_string,room,&length)<0)return -ATM_EFAULT;
        offsets[i]=(uint16_t)payload->bytes;
        if(copy_from_user(space,payload->strings+payload->bytes,(const void *)(uintptr_t)user_string,length+1)<0)return -ATM_EFAULT;
        payload->bytes+=(uint32_t)length+1u;*count=i+1u;
    }
    uint64_t terminator=0;
    if(copy_from_user(space,&terminator,user_vector+maximum,sizeof(terminator))<0)return -ATM_EFAULT;
    return terminator?-ATM_EINVAL:0;
}
static int64_t sys_execve_user(const user_space_t *space,task_t *task,registers_t *frame,
                               const char *user_path,const uint64_t *user_argv,const uint64_t *user_envp){
    char path[VFS_PATH_MAX];native_exec_payload_t *payload;int64_t rc;
    if(!space||!task||!frame||copy_string_from_user(space,path,sizeof(path),user_path)<0)return -ATM_EFAULT;
    payload=(native_exec_payload_t *)kmalloc(sizeof(*payload));if(!payload)return -ATM_ENOMEM;
    kmemset(payload,0,sizeof(*payload));
    rc=sys_exec_copy_vector(space,user_argv,payload,payload->argv_off,&payload->argc,ATM_NATIVE_EXEC_MAX_ARGS);
    if(rc<0)goto done;
    if(!payload->argc){
        size_t length=kstrlen(path)+1;
        if(length>ATM_NATIVE_EXEC_STR_BYTES){rc=-ATM_EINVAL;goto done;}
        payload->argc=1;payload->argv_off[0]=0;payload->bytes=(uint32_t)length;kmemcpy(payload->strings,path,length);
    }
    rc=sys_exec_copy_vector(space,user_envp,payload,payload->env_off,&payload->envc,ATM_NATIVE_EXEC_MAX_ENV);
    if(rc<0)goto done;
    rc=native_app_exec_current(task,frame,path,payload)<0?-ATM_EINVAL:0;
 done:kfree(payload);return rc;
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
    if(requested<task->user_brk_base || requested>=ATM_USER_ANON_BASE) return task->user_brk;
    uint64_t grow_to=(requested+ATM_PAGE_SIZE-1)&ATM_PAGE_MASK;
    if(grow_to>ATM_USER_ANON_BASE) return task->user_brk;
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
    case ATM_SYS_OPENAT:
        return from_user ? sys_result(sys_openat_user(space,task,(int)r->rdi,(const char *)(uintptr_t)r->rsi,(uint32_t)r->rdx,(uint32_t)r->r10))
                         : sys_result(-ATM_ENOSYS);
    case ATM_SYS_FSTATAT:
        return from_user ? sys_result(sys_fstatat_user(space,(int)r->rdi,(const char *)(uintptr_t)r->rsi,(void *)(uintptr_t)r->rdx,(uint32_t)r->r10))
                         : sys_result(-ATM_ENOSYS);
    case ATM_SYS_CLOSE:
        return from_user ? sys_result(native_fd_close(task,(int)r->rdi))
                         : sys_result(atm_posix_close((int)r->rdi));
    case ATM_SYS_STAT:
        return from_user ? sys_result(sys_stat_user(space,(const char *)(uintptr_t)r->rdi,(void *)(uintptr_t)r->rsi,1))
                         : sys_result(atm_posix_stat((const char *)(uintptr_t)r->rdi,(atm_posix_stat_t *)(uintptr_t)r->rsi));
    case ATM_SYS_LSTAT:
        return from_user ? sys_result(sys_stat_user(space,(const char *)(uintptr_t)r->rdi,(void *)(uintptr_t)r->rsi,0))
                         : sys_result(atm_posix_lstat((const char *)(uintptr_t)r->rdi,(atm_posix_stat_t *)(uintptr_t)r->rsi));
    case ATM_SYS_FSTAT:
        if(from_user) return sys_result(sys_fstat_user(space,task,(int)r->rdi,(void *)(uintptr_t)r->rsi));
        return sys_result(atm_posix_fstat((int)r->rdi,(atm_posix_stat_t *)(uintptr_t)r->rsi));
    case ATM_SYS_LSEEK:
        return from_user ? sys_result(native_fd_lseek(task,(int)r->rdi,(int64_t)r->rsi,(int)r->rdx))
                         : sys_result(atm_posix_lseek((int)r->rdi,(int64_t)r->rsi,(int)r->rdx));
    case ATM_SYS_PREAD64:
        return from_user ? sys_result(sys_pread_user(space,task,(int)r->rdi,(void *)(uintptr_t)r->rsi,r->rdx,r->r10))
                         : sys_result(atm_posix_pread((int)r->rdi,(void *)(uintptr_t)r->rsi,r->rdx,r->r10));
    case ATM_SYS_PWRITE64:
        return from_user ? sys_result(sys_pwrite_user(space,task,(int)r->rdi,(const void *)(uintptr_t)r->rsi,r->rdx,r->r10))
                         : sys_result(atm_posix_pwrite((int)r->rdi,(const void *)(uintptr_t)r->rsi,r->rdx,r->r10));
    case ATM_SYS_READV:
        return from_user ? sys_result(sys_readv_user(space,task,(int)r->rdi,(const void *)(uintptr_t)r->rsi,(int)r->rdx))
                         : sys_result(atm_posix_readv((int)r->rdi,(const atm_posix_iovec_t *)(uintptr_t)r->rsi,(int)r->rdx));
    case ATM_SYS_WRITEV:
        return from_user ? sys_result(sys_writev_user(space,task,(int)r->rdi,(const void *)(uintptr_t)r->rsi,(int)r->rdx))
                         : sys_result(atm_posix_writev((int)r->rdi,(const atm_posix_iovec_t *)(uintptr_t)r->rsi,(int)r->rdx));
    case ATM_SYS_PREADV:
        return from_user ? sys_result(sys_preadv_user(space,task,(int)r->rdi,(const void *)(uintptr_t)r->rsi,(int)r->rdx,r->r10))
                         : sys_result(-ATM_ENOSYS);
    case ATM_SYS_PWRITEV:
        return from_user ? sys_result(sys_pwritev_user(space,task,(int)r->rdi,(const void *)(uintptr_t)r->rsi,(int)r->rdx,r->r10))
                         : sys_result(-ATM_ENOSYS);
    case ATM_SYS_FCHDIR:
        return from_user ? sys_result(native_fd_fchdir(task,(int)r->rdi))
                         : sys_result(atm_posix_fchdir((int)r->rdi));
    case ATM_SYS_GETRUSAGE:
        return from_user ? sys_result(sys_getrusage_user(space,task,(int)r->rdi,(void *)(uintptr_t)r->rsi))
                         : sys_result(-ATM_ENOSYS);
    case ATM_SYS_TIMES:
        return from_user ? sys_result(sys_times_user(space,task,(void *)(uintptr_t)r->rdi))
                         : sys_result(-ATM_ENOSYS);
    case ATM_SYS_FCHMOD:
        return from_user ? sys_result(native_fd_fchmod(task,(int)r->rdi,(uint32_t)r->rsi))
                         : sys_result(atm_posix_fchmod((int)r->rdi,(uint32_t)r->rsi));
    case ATM_SYS_FCHOWN:
        return from_user ? sys_result(native_fd_fchown(task,(int)r->rdi,(uint32_t)r->rsi,(uint32_t)r->rdx))
                         : sys_result(atm_posix_fchown((int)r->rdi,(uint32_t)r->rsi,(uint32_t)r->rdx));
    case ATM_SYS_GETRLIMIT:
        return from_user ? sys_result(sys_getrlimit_user(space,(int)r->rdi,(void *)(uintptr_t)r->rsi))
                         : sys_result(-ATM_ENOSYS);
    case ATM_SYS_FACCESSAT:
        return from_user ? sys_result(sys_faccessat_user(space,(int)r->rdi,(const char *)(uintptr_t)r->rsi,(int)r->rdx,(int)r->r10))
                         : sys_result(-ATM_ENOSYS);
    case ATM_SYS_MKDIRAT:
        return from_user ? sys_result(sys_mkdirat_user(space,(int)r->rdi,(const char *)(uintptr_t)r->rsi,(uint32_t)r->rdx)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_UNLINKAT:
        return from_user ? sys_result(sys_unlinkat_user(space,(int)r->rdi,(const char *)(uintptr_t)r->rsi,(uint32_t)r->rdx)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_RENAMEAT:
        return from_user ? sys_result(sys_renameat_user(space,(int)r->rdi,(const char *)(uintptr_t)r->rsi,(int)r->rdx,(const char *)(uintptr_t)r->r10)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_LINKAT:
        return from_user ? sys_result(sys_linkat_user(space,(int)r->rdi,(const char *)(uintptr_t)r->rsi,(int)r->rdx,(const char *)(uintptr_t)r->r10,(uint32_t)r->r8)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_SYMLINKAT:
        return from_user ? sys_result(sys_symlinkat_user(space,(const char *)(uintptr_t)r->rdi,(int)r->rsi,(const char *)(uintptr_t)r->rdx)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_READLINKAT:
        return from_user ? sys_result(sys_readlinkat_user(space,(int)r->rdi,(const char *)(uintptr_t)r->rsi,(char *)(uintptr_t)r->rdx,r->r10)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_DUP:
        return from_user ? sys_result(native_fd_dup(task,(int)r->rdi)) : sys_result(atm_posix_dup((int)r->rdi));
    case ATM_SYS_DUP2:
        return from_user ? sys_result(native_fd_dup2(task,(int)r->rdi,(int)r->rsi)) : sys_result(atm_posix_dup2((int)r->rdi,(int)r->rsi));
    case ATM_SYS_DUP3: {
        if(!from_user)return sys_result(-ATM_ENOSYS);
        int rc=native_fd_dup3(task,(int)r->rdi,(int)r->rsi,(uint32_t)r->rdx);
        return sys_result(rc<0?-ATM_EINVAL:rc);
    }
    case ATM_SYS_FSYNC:
        return from_user ? sys_result(native_fd_fsync(task,(int)r->rdi,0)) : sys_result(atm_posix_fsync((int)r->rdi));
    case ATM_SYS_FDATASYNC:
        return from_user ? sys_result(native_fd_fsync(task,(int)r->rdi,1)) : sys_result(atm_posix_fdatasync((int)r->rdi));
    case ATM_SYS_TRUNCATE:
        return from_user ? sys_result(sys_truncate_user(space,(const char *)(uintptr_t)r->rdi,r->rsi)) : sys_result(atm_posix_truncate((const char *)(uintptr_t)r->rdi,r->rsi));
    case ATM_SYS_FTRUNCATE:
        return from_user ? sys_result(native_fd_ftruncate(task,(int)r->rdi,r->rsi)) : sys_result(atm_posix_ftruncate((int)r->rdi,r->rsi));
    case ATM_SYS_GETCWD:
        if(from_user)return sys_result(sys_getcwd_user(space,(char *)(uintptr_t)r->rdi,r->rsi));
        return sys_result(atm_posix_getcwd((char *)(uintptr_t)r->rdi,(size_t)r->rsi)?(int64_t)(uintptr_t)r->rdi:-ATM_EINVAL);
    case ATM_SYS_CHDIR:
        return from_user ? sys_result(sys_chdir_user(space,(const char *)(uintptr_t)r->rdi)) : sys_result(atm_posix_chdir((const char *)(uintptr_t)r->rdi));
    case ATM_SYS_RENAME:
        return from_user ? sys_result(sys_rename_user(space,(const char *)(uintptr_t)r->rdi,(const char *)(uintptr_t)r->rsi)) : sys_result(atm_posix_rename((const char *)(uintptr_t)r->rdi,(const char *)(uintptr_t)r->rsi));
    case ATM_SYS_ACCESS:
        return from_user ? sys_result(sys_access_user(space,(const char *)(uintptr_t)r->rdi,(int)r->rsi)) : sys_result(atm_posix_access((const char *)(uintptr_t)r->rdi,(int)r->rsi));
    case ATM_SYS_MKDIR:
        return from_user ? sys_result(sys_mkdir_user(space,(const char *)(uintptr_t)r->rdi,(uint32_t)r->rsi)) : sys_result(atm_posix_mkdir((const char *)(uintptr_t)r->rdi,(uint32_t)r->rsi));
    case ATM_SYS_RMDIR:
        return from_user ? sys_result(sys_rmdir_user(space,(const char *)(uintptr_t)r->rdi)) : sys_result(atm_posix_rmdir((const char *)(uintptr_t)r->rdi));
    case ATM_SYS_LINK:
        return from_user ? sys_result(sys_link_user(space,(const char *)(uintptr_t)r->rdi,(const char *)(uintptr_t)r->rsi)) : sys_result(atm_posix_link((const char *)(uintptr_t)r->rdi,(const char *)(uintptr_t)r->rsi));
    case ATM_SYS_UNLINK:
        return from_user ? sys_result(sys_unlink_user(space,(const char *)(uintptr_t)r->rdi)) : sys_result(atm_posix_unlink((const char *)(uintptr_t)r->rdi));
    case ATM_SYS_SYMLINK:
        return from_user ? sys_result(sys_symlink_user(space,(const char *)(uintptr_t)r->rdi,(const char *)(uintptr_t)r->rsi)) : sys_result(atm_posix_symlink((const char *)(uintptr_t)r->rdi,(const char *)(uintptr_t)r->rsi));
    case ATM_SYS_READLINK:
        return from_user ? sys_result(sys_readlink_user(space,(const char *)(uintptr_t)r->rdi,(char *)(uintptr_t)r->rsi,r->rdx)) : sys_result(atm_posix_readlink((const char *)(uintptr_t)r->rdi,(char *)(uintptr_t)r->rsi,(size_t)r->rdx));
    case ATM_SYS_CHMOD:
        return from_user ? sys_result(sys_chmod_user(space,(const char *)(uintptr_t)r->rdi,(uint32_t)r->rsi)) : sys_result(atm_posix_chmod((const char *)(uintptr_t)r->rdi,(uint32_t)r->rsi));
    case ATM_SYS_CHOWN:
        return from_user ? sys_result(sys_chown_user(space,(const char *)(uintptr_t)r->rdi,(uint32_t)r->rsi,(uint32_t)r->rdx)) : sys_result(atm_posix_chown((const char *)(uintptr_t)r->rdi,(uint32_t)r->rsi,(uint32_t)r->rdx));
    case ATM_SYS_UMASK:
        return sys_result((int64_t)atm_posix_umask((uint32_t)r->rdi));
    case ATM_SYS_ISATTY:
        return from_user ? sys_result(native_fd_isatty(task,(int)r->rdi)) : sys_result(atm_posix_isatty((int)r->rdi));
    case ATM_SYS_PIPE:
        return from_user ? sys_result(sys_pipe_user(space,task,(void *)(uintptr_t)r->rdi,0)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_PIPE2:
        return from_user ? sys_result(sys_pipe_user(space,task,(void *)(uintptr_t)r->rdi,(uint32_t)r->rsi)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_GETPGID: {
        int rc=from_user?task_getpgid((int32_t)r->rdi):-1;
        return sys_result(rc<0?-ATM_EINVAL:rc);
    }
    case ATM_SYS_SETPGID: {
        int rc=from_user?task_setpgid((int32_t)r->rdi,(int32_t)r->rsi):-1;
        return sys_result(rc<0?-ATM_EINVAL:rc);
    }
    case ATM_SYS_GETSID: {
        int rc=from_user?task_getsid((int32_t)r->rdi):-1;
        return sys_result(rc<0?-ATM_EINVAL:rc);
    }
    case ATM_SYS_SETSID: {
        int rc=from_user?task_setsid():-1;
        return sys_result(rc<0?-ATM_EINVAL:rc);
    }
    case ATM_SYS_OPENDIR:
        return from_user ? sys_result(sys_opendir_user(space,task,(const char *)(uintptr_t)r->rdi)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_READDIR:
        return from_user ? sys_result(sys_readdir_user(space,task,(int)r->rdi,(void *)(uintptr_t)r->rsi)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_CLOSEDIR:
        return from_user ? sys_result(native_dir_close(task,(int)r->rdi)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_FCNTL: {
        if(!from_user) return sys_result(-ATM_ENOSYS);
        int rc=native_fd_fcntl(task,(int)r->rdi,(int)r->rsi,(uint32_t)r->rdx);
        return sys_result(rc<0?-ATM_EINVAL:rc);
    }
    case ATM_SYS_POLL:
        return from_user ? sys_result(sys_poll_user(space,task,(void *)(uintptr_t)r->rdi,r->rsi,r->rdx)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_SELECT:
        return from_user ? sys_result(sys_select_user(space,task,(int)r->rdi,(void *)(uintptr_t)r->rsi,(void *)(uintptr_t)r->rdx,(void *)(uintptr_t)r->r10,(const void *)(uintptr_t)r->r8)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_CLOCK_GETTIME:
        return from_user ? sys_result(sys_clock_gettime_user(space,(int)r->rdi,(void *)(uintptr_t)r->rsi)) : sys_result(atm_clock_gettime((int)r->rdi,(atm_timespec_t *)(uintptr_t)r->rsi));
    case ATM_SYS_GETTIMEOFDAY:
        return from_user ? sys_result(sys_gettimeofday_user(space,(void *)(uintptr_t)r->rdi)) : sys_result(atm_gettimeofday((atm_timeval_t *)(uintptr_t)r->rdi));
    case ATM_SYS_CLOCK_GETRES:
        return from_user ? sys_result(sys_clock_getres_user(space,(int)r->rdi,(void *)(uintptr_t)r->rsi)) : sys_result(atm_clock_getres((int)r->rdi,(atm_timespec_t *)(uintptr_t)r->rsi));
    case ATM_SYS_UNAME:
        if(from_user) return sys_result(sys_uname_user(space,(void *)(uintptr_t)r->rdi));
        {atm_utsname_t *out=(atm_utsname_t *)(uintptr_t)r->rdi;if(!out)return sys_result(-ATM_EFAULT);atm_uname_fill(out);return 0;}
    case ATM_SYS_NANOSLEEP:
        return from_user ? sys_result(sys_nanosleep_user(space,task,(const void *)(uintptr_t)r->rdi,(void *)(uintptr_t)r->rsi)) : sys_result(-ATM_ENOSYS);
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
    case ATM_SYS_SEND:
        return from_user ? sys_result(sys_send_user(space,task,(int)r->rdi,(const void *)(uintptr_t)r->rsi,r->rdx,(uint32_t)r->r10)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_RECV:
        return from_user ? sys_result(sys_recv_user(space,task,(int)r->rdi,(void *)(uintptr_t)r->rsi,r->rdx,(uint32_t)r->r10)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_BRK:
        return from_user ? sys_brk_native(task,r->rdi) : 0;
    case ATM_SYS_EXECVE:
        return from_user ? sys_result(sys_execve_user(space,task,r,(const char *)(uintptr_t)r->rdi,
                                                       (const uint64_t *)(uintptr_t)r->rsi,
                                                       (const uint64_t *)(uintptr_t)r->rdx)) : sys_result(-ATM_ENOSYS);
    case ATM_SYS_EXIT:
        if(from_user) task_exit_from_syscall((int)r->rdi);
        task_exit((int)r->rdi);
        return 0;
    case ATM_SYS_WAITPID: {
        int status=0;
        int *out=from_user?NULL:(int *)(uintptr_t)r->rsi;
        /* Validate the eventual output before reaping. Otherwise an EFAULT
         * would destroy the only observable child exit status. */
        if(from_user && r->rsi && (!space || user_range_valid(space,(void *)(uintptr_t)r->rsi,sizeof(status),1)<0))
            return sys_result(-ATM_EFAULT);
        int ret=task_waitpid((int32_t)r->rdi,r->rsi?&status:0,(uint32_t)r->rdx);
        if(ret>0 && r->rsi){
            if(from_user){
                if(copy_to_user(space,(void *)(uintptr_t)r->rsi,&status,sizeof(status))<0)
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
    /* ATMKoala presently has one credential per task: effective and real IDs
     * are intentionally identical until a capability/set-id model exists. */
    case ATM_SYS_GETEUID:
        return (uint64_t)atm_posix_getuid();
    case ATM_SYS_GETEGID:
        return (uint64_t)atm_posix_getgid();
    case ATM_SYS_GETRESUID:
        return from_user?sys_result(sys_getresid_user(space,(void *)(uintptr_t)r->rdi,(void *)(uintptr_t)r->rsi,(void *)(uintptr_t)r->rdx,0)):sys_result(-ATM_ENOSYS);
    case ATM_SYS_GETRESGID:
        return from_user?sys_result(sys_getresid_user(space,(void *)(uintptr_t)r->rdi,(void *)(uintptr_t)r->rsi,(void *)(uintptr_t)r->rdx,1)):sys_result(-ATM_ENOSYS);
    case ATM_SYS_GETTID: {
        task_t *t=sched_current();
        return t ? (uint64_t)t->pid : 0;
    }
    case ATM_SYS_ABI_INFO:
        return (uint64_t)ATM_SYSCALL_ABI_V22;
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
    int saved_map[TASK_FD_MAX],saved_socket_map[TASK_FD_MAX],saved_pipe_map[TASK_FD_MAX];
    uint32_t saved_fd_flags[TASK_FD_MAX];
    void *saved_dir_map[ATM_NATIVE_DIR_MAX];
    uint32_t saved_fd_ready=task->fd_table_ready;
    kmemcpy(saved_map,task->fd_map,sizeof(saved_map));
    kmemcpy(saved_socket_map,task->socket_map,sizeof(saved_socket_map));
    kmemcpy(saved_pipe_map,task->pipe_map,sizeof(saved_pipe_map));
    kmemcpy(saved_fd_flags,task->fd_flags,sizeof(saved_fd_flags));
    kmemcpy(saved_dir_map,task->dir_map,sizeof(saved_dir_map));
    int rc=-1,fd=-1,sock=-1,pipefd0=-1,pipefd1=-1,dirh=-1,stage=1;
    task->address_space=&space;
    native_fd_task_init(task);
    stage=2;
    if(native_socket_selftest()<0 || native_pipe_selftest()<0 || native_dir_selftest()<0) goto done;
    const char *path="/tmp/.atm-syscall-uaccess";
    const char *payload="abi-uaccess";
    registers_t r;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_SOCKET; r.rdi=ATM_AF_INET; r.rsi=ATM_SOCK_STREAM; r.rdx=ATM_IPPROTO_TCP;
    stage=3;
    sock=(int64_t)atm_syscall_dispatch(&r); if(sock<3){sdk_serial_write("[syscall] socket-open-fail\n");goto done;}
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_CLOSE; r.rdi=(uint64_t)sock;
    if((int64_t)atm_syscall_dispatch(&r)<0) goto done; sock=-1;
    stage=4;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_PIPE;r.rdi=ATM_USER_TOP+8;
    if((int64_t)atm_syscall_dispatch(&r)!=-ATM_EFAULT) goto done;
    stage=5;
    r.rdi=ATM_USER_BASE+0x700;
    if((int64_t)atm_syscall_dispatch(&r)<0) goto done;
    int pipefds[2];
    if(copy_from_user(&space,pipefds,(const void *)(uintptr_t)(ATM_USER_BASE+0x700),sizeof(pipefds))<0 || pipefds[0]<3 || pipefds[1]<3) goto done;
    pipefd0=pipefds[0];pipefd1=pipefds[1];
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_FCNTL;r.rdi=(uint64_t)pipefd0;r.rsi=ATM_NATIVE_F_SETFL;r.rdx=ATM_NATIVE_O_NONBLOCK;
    if((int64_t)atm_syscall_dispatch(&r)!=0)goto done;
    r.rsi=ATM_NATIVE_F_GETFL;r.rdx=0;
    if((int64_t)atm_syscall_dispatch(&r)!=(int64_t)ATM_NATIVE_O_NONBLOCK)goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_READ;r.rdi=(uint64_t)pipefd0;r.rsi=ATM_USER_BASE+0x780;r.rdx=1;
    if((int64_t)atm_syscall_dispatch(&r)!=-ATM_EAGAIN)goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_FCNTL;r.rdi=(uint64_t)pipefd0;r.rsi=ATM_NATIVE_F_SETFL;r.rdx=0;
    if((int64_t)atm_syscall_dispatch(&r)!=0)goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_POLL;r.rdi=ATM_USER_TOP+8;r.rsi=1;r.rdx=0;
    if((int64_t)atm_syscall_dispatch(&r)!=-ATM_EFAULT)goto done;
    atm_native_pollfd_t upoll;upoll.fd=pipefd0;upoll.events=ATM_POLLIN;upoll.revents=0;
    if(copy_to_user(&space,(void *)(uintptr_t)(ATM_USER_BASE+0x7a0),&upoll,sizeof(upoll))<0)goto done;
    r.rdi=ATM_USER_BASE+0x7a0;
    if((int64_t)atm_syscall_dispatch(&r)!=0)goto done;
    if(copy_from_user(&space,&upoll,(const void *)(uintptr_t)(ATM_USER_BASE+0x7a0),sizeof(upoll))<0||upoll.revents)goto done;
    if(copy_to_user(&space,(void *)(uintptr_t)(ATM_USER_BASE+0x740),"pipe",4)<0)goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_WRITE;r.rdi=(uint64_t)pipefd1;r.rsi=ATM_USER_BASE+0x740;r.rdx=4;
    if((int64_t)atm_syscall_dispatch(&r)!=4)goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_CLOSE;r.rdi=(uint64_t)pipefd1;
    if((int64_t)atm_syscall_dispatch(&r)<0)goto done;pipefd1=-1;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_READ;r.rdi=(uint64_t)pipefd0;r.rsi=ATM_USER_BASE+0x780;r.rdx=4;
    if((int64_t)atm_syscall_dispatch(&r)!=4)goto done;
    char pipeout[4];if(copy_from_user(&space,pipeout,(const void *)(uintptr_t)(ATM_USER_BASE+0x780),4)<0||kstrncmp(pipeout,"pipe",4))goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_READ;r.rdi=(uint64_t)pipefd0;r.rsi=ATM_USER_BASE+0x7c0;r.rdx=1;
    if((int64_t)atm_syscall_dispatch(&r)!=0)goto done;
    if(native_fd_close(task,pipefd0)<0)goto done;pipefd0=-1;
    stage=6;
    if(copy_to_user(&space,(void *)(uintptr_t)(ATM_USER_BASE+0x800),"/",2)<0)goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_OPENDIR;r.rdi=ATM_USER_BASE+0x800;
    dirh=(int64_t)atm_syscall_dispatch(&r);if(dirh<0)goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_READDIR;r.rdi=(uint64_t)dirh;r.rsi=ATM_USER_TOP+8;
    if((int64_t)atm_syscall_dispatch(&r)!=-ATM_EFAULT)goto done;
    r.rsi=ATM_USER_BASE+0x840;
    if((int64_t)atm_syscall_dispatch(&r)!=1)goto done;
    atm_user_dirent_t ud;if(copy_from_user(&space,&ud,(const void *)(uintptr_t)(ATM_USER_BASE+0x840),sizeof(ud))<0||!ud.d_name[0])goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_CLOSEDIR;r.rdi=(uint64_t)dirh;
    if((int64_t)atm_syscall_dispatch(&r)<0)goto done;dirh=-1;
    stage=7;
    if(copy_to_user(&space,(void *)(uintptr_t)(ATM_USER_BASE+0x80),path,kstrlen(path)+1)<0) goto done;
    r.rax=ATM_SYS_OPEN; r.rdi=ATM_USER_BASE+0x80; r.rsi=O_WRONLY|O_CREAT|O_TRUNC; r.rdx=0600;
    fd=(int64_t)atm_syscall_dispatch(&r); if(fd<0) goto done;
    stage=8;
    if(copy_to_user(&space,(void *)(uintptr_t)(ATM_USER_BASE+0x180),payload,kstrlen(payload))<0) goto done;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_WRITE; r.rdi=(uint64_t)fd; r.rsi=ATM_USER_BASE+0x180; r.rdx=kstrlen(payload);
    if((int64_t)atm_syscall_dispatch(&r)!=(int64_t)kstrlen(payload)) goto done;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_CLOSE; r.rdi=(uint64_t)fd;
    if((int64_t)atm_syscall_dispatch(&r)<0) goto done; fd=-1;
    stage=9;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_OPEN; r.rdi=ATM_USER_BASE+0x80; r.rsi=O_RDONLY;
    fd=(int64_t)atm_syscall_dispatch(&r); if(fd<0) goto done;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_READ; r.rdi=(uint64_t)fd; r.rsi=ATM_USER_BASE+0x280; r.rdx=kstrlen(payload);
    if((int64_t)atm_syscall_dispatch(&r)!=(int64_t)kstrlen(payload)) goto done;
    char out[32]; kmemset(out,0,sizeof(out));
    if(copy_from_user(&space,out,(const void *)(uintptr_t)(ATM_USER_BASE+0x280),kstrlen(payload))<0 || kstrncmp(out,payload,kstrlen(payload))) goto done;
        stage=10;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_FSTAT;
 r.rdi=(uint64_t)fd; r.rsi=ATM_USER_BASE+0x380;
    if((int64_t)atm_syscall_dispatch(&r)<0) goto done;
    /* A bad output pointer must fail before native_fd_read() advances offset. */
    stage=11;
    if(native_fd_lseek(task,fd,0,0)<0) goto done;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_READ; r.rdi=(uint64_t)fd; r.rsi=ATM_USER_TOP+8; r.rdx=kstrlen(payload);
    if((int64_t)atm_syscall_dispatch(&r)!=-ATM_EFAULT) goto done;
        stage=12;
    kmemset(&r,0,sizeof(r)); r.cs=3; r.rax=ATM_SYS_READ;
 r.rdi=(uint64_t)fd; r.rsi=ATM_USER_BASE+0x480; r.rdx=kstrlen(payload);
    if((int64_t)atm_syscall_dispatch(&r)!=(int64_t)kstrlen(payload)) goto done;
    kmemset(out,0,sizeof(out));
    if(copy_from_user(&space,out,(const void *)(uintptr_t)(ATM_USER_BASE+0x480),kstrlen(payload))<0 || kstrncmp(out,payload,kstrlen(payload))) goto done;
    stage=13;
    /* Invalid iovec storage must fail before any file data is consumed. */
    if(native_fd_lseek(task,fd,0,0)<0) goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_READV;r.rdi=(uint64_t)fd;r.rsi=ATM_USER_TOP+8;r.rdx=1;
    if((int64_t)atm_syscall_dispatch(&r)!=-ATM_EFAULT)goto done;
    stage=14;
    atm_posix_iovec_t viov;viov.iov_base=(void *)(uintptr_t)(ATM_USER_BASE+0x580);viov.iov_len=kstrlen(payload);
    if(copy_to_user(&space,(void *)(uintptr_t)(ATM_USER_BASE+0x500),&viov,sizeof(viov))<0)goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_READV;r.rdi=(uint64_t)fd;r.rsi=ATM_USER_BASE+0x500;r.rdx=1;
    if((int64_t)atm_syscall_dispatch(&r)!=(int64_t)kstrlen(payload))goto done;
    kmemset(out,0,sizeof(out));
    if(copy_from_user(&space,out,(const void *)(uintptr_t)(ATM_USER_BASE+0x580),kstrlen(payload))<0||kstrncmp(out,payload,kstrlen(payload)))goto done;
    stage=15;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_STAT;r.rdi=ATM_USER_BASE+0x80;r.rsi=ATM_USER_BASE+0x680;
    if((int64_t)atm_syscall_dispatch(&r)<0)goto done;
    atm_posix_stat_t ust;if(copy_from_user(&space,&ust,(const void *)(uintptr_t)(ATM_USER_BASE+0x680),sizeof(ust))<0||ust.st_size!=kstrlen(payload))goto done;
    stage=16;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_CHOWN;r.rdi=ATM_USER_TOP+8;r.rsi=atm_posix_getuid();r.rdx=atm_posix_getgid();
    if((int64_t)atm_syscall_dispatch(&r)!=-ATM_EFAULT)goto done;
    r.rdi=ATM_USER_BASE+0x80;
    if((int64_t)atm_syscall_dispatch(&r)<0)goto done;
    kmemset(&r,0,sizeof(r));r.cs=3;r.rax=ATM_SYS_GETEUID;
    if((uint32_t)atm_syscall_dispatch(&r)!=atm_posix_getuid())goto done;
    r.rax=ATM_SYS_GETEGID;
    if((uint32_t)atm_syscall_dispatch(&r)!=atm_posix_getgid())goto done;
    r.rax=ATM_SYS_ABI_INFO;
    if((uint32_t)atm_syscall_dispatch(&r)!=ATM_SYSCALL_ABI_V22)goto done;
    stage=17;
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
    if(pipefd0>=0) native_fd_close(task,pipefd0);
    if(pipefd1>=0) native_fd_close(task,pipefd1);
    if(dirh>=0) native_dir_close(task,dirh);
    if(fd>=0) native_fd_close(task,fd);
    native_fd_task_cleanup(task);
    (void)atm_posix_unlink(path);
    task->address_space=saved_space;
    kmemcpy(task->fd_map,saved_map,sizeof(saved_map));
    kmemcpy(task->socket_map,saved_socket_map,sizeof(saved_socket_map));
    kmemcpy(task->pipe_map,saved_pipe_map,sizeof(saved_pipe_map));
    kmemcpy(task->fd_flags,saved_fd_flags,sizeof(saved_fd_flags));
    kmemcpy(task->dir_map,saved_dir_map,sizeof(saved_dir_map));
    task->fd_table_ready=saved_fd_ready;
    paging_destroy_user_space(&space);
    return rc;
}
