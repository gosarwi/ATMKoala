#include "native_fd.h"
#include "atm_syscall.h"
#include "util.h"
#include "vfs.h"
#include "native_socket.h"
#include "native_pipe.h"
#include "native_dir.h"
#include "atm_syscall.h"
#include <stdint.h>

typedef struct { task_t *owner; uint8_t flags[TASK_FD_MAX]; } fd_exec_meta_t;
/* Private bounded descriptor metadata. It intentionally remains outside
 * task_t: scheduler task stacks are only 4 KiB and several regressions create
 * task-shaped fixtures on those stacks. Extra entries cover temporary tests. */
static fd_exec_meta_t fd_exec_meta[TASK_MAX*2];

static fd_exec_meta_t *fd_exec_meta_for(task_t *task,int create){
    if(!task) return NULL;
    for(uint32_t i=0;i<TASK_MAX*2;i++) if(fd_exec_meta[i].owner==task) return &fd_exec_meta[i];
    if(!create) return NULL;
    for(uint32_t i=0;i<TASK_MAX*2;i++) if(!fd_exec_meta[i].owner){
        fd_exec_meta[i].owner=task;kmemset(fd_exec_meta[i].flags,0,sizeof(fd_exec_meta[i].flags));return &fd_exec_meta[i];
    }
    return NULL;
}
static void fd_exec_reset(task_t *task){fd_exec_meta_t *m=fd_exec_meta_for(task,1);if(m)kmemset(m->flags,0,sizeof(m->flags));}
static void fd_exec_drop(task_t *task){fd_exec_meta_t *m=fd_exec_meta_for(task,0);if(m){kmemset(m,0,sizeof(*m));}}
static int fd_exec_get(task_t *task,int fd){fd_exec_meta_t *m=fd_exec_meta_for(task,0);return m&&fd>=0&&fd<TASK_FD_MAX&&m->flags[fd];}
static int fd_exec_set(task_t *task,int fd,int value){fd_exec_meta_t *m=fd_exec_meta_for(task,value?1:0);if(!m||fd<0||fd>=TASK_FD_MAX)return -1;m->flags[fd]=value?1:0;return 0;}

static int fd_valid(const task_t *task,int fd){
    return task && task->fd_table_ready && fd>=0 && fd<TASK_FD_MAX && task->fd_map[fd]>=0;
}

void native_fd_task_init(task_t *task){
    if(!task) return;
    for(int i=0;i<TASK_FD_MAX;i++){task->fd_map[i]=-1;task->fd_flags[i]=0;}
    fd_exec_reset(task);
    native_socket_task_init(task);
    native_pipe_task_init(task);
    native_dir_task_init(task);
    /* Kernel bootstrap fd 0–2 point at /dev/null. Native applications need
     * real console semantics, therefore each task receives independent VFS
     * opens of /dev/tty that can be safely released on task exit. */
    task->fd_map[0]=atm_posix_open("/dev/tty",O_RDONLY,0);
    task->fd_map[1]=atm_posix_open("/dev/tty",O_WRONLY,0);
    task->fd_map[2]=atm_posix_open("/dev/tty",O_WRONLY,0);
    task->fd_flags[0]=O_RDONLY;
    task->fd_flags[1]=O_WRONLY;
    task->fd_flags[2]=O_WRONLY;
    task->fd_table_ready=1;
}

void native_fd_task_cleanup(task_t *task){
    if(!task || !task->fd_table_ready) return;
    native_dir_task_cleanup(task);
    native_pipe_task_cleanup(task);
    native_socket_task_cleanup(task);
    for(int i=0;i<TASK_FD_MAX;i++){
        if(task->fd_map[i]>=0) (void)atm_posix_close(task->fd_map[i]);
        task->fd_map[i]=-1;
        task->fd_flags[i]=0;
    }
    task->fd_table_ready=0;
    fd_exec_drop(task);
}

void native_fd_close_on_exec(task_t *task){
    if(!task || !task->fd_table_ready) return;
    for(int fd=0;fd<TASK_FD_MAX;fd++) if(fd_exec_get(task,fd)) (void)native_fd_close(task,fd);
}

int native_fd_task_inherit(task_t *child,const task_t *parent){
    if(!child || !parent || !parent->fd_table_ready) return -1;
    for(int i=0;i<TASK_FD_MAX;i++){child->fd_map[i]=-1;child->fd_flags[i]=0;}
    fd_exec_reset(child);
    native_socket_task_init(child);
    native_pipe_task_init(child);
    native_dir_task_init(child);
    child->fd_table_ready=1;
    for(int i=0;i<ATM_NATIVE_DIR_MAX;i++) if(parent->dir_map[i]) goto fail;
    for(int fd=0;fd<TASK_FD_MAX;fd++){
        /* Sockets contain a single task owner today. Refuse inheritance rather
         * than quietly creating two task handles with unsafe close semantics. */
        if(parent->socket_map[fd]>=0) goto fail;
        if(parent->fd_map[fd]>=0){
            int backend=atm_posix_dup(parent->fd_map[fd]);
            if(backend<0) goto fail;
            child->fd_map[fd]=backend;
            child->fd_flags[fd]=parent->fd_flags[fd];
            (void)fd_exec_set(child,fd,fd_exec_get((task_t *)parent,fd));
        } else if(parent->pipe_map[fd]>=0){
            if(native_pipe_inherit(child,parent,fd)<0) goto fail;
            child->fd_flags[fd]=parent->fd_flags[fd];
            (void)fd_exec_set(child,fd,fd_exec_get((task_t *)parent,fd));
        }
    }
    return 0;
fail:
    native_fd_task_cleanup(child);
    return -1;
}

static int native_fd_slot(task_t *task){
    if(!task || !task->fd_table_ready) return -1;
    for(int i=3;i<TASK_FD_MAX;i++) if(task->fd_map[i]<0 && task->socket_map[i]<0 && task->pipe_map[i]<0) return i;
    return -1;
}

int native_fd_open(task_t *task,const char *path,uint32_t flags,uint32_t mode){
    int slot=native_fd_slot(task);
    if(slot<0) return -1;
    int backend=atm_posix_open(path,flags&~(ATM_NATIVE_O_NONBLOCK|ATM_NATIVE_O_CLOEXEC),mode);
    if(backend<0) return -1;
    task->fd_map[slot]=backend;
    task->fd_flags[slot]=flags&(ATM_NATIVE_O_ACCMODE|ATM_NATIVE_O_NONBLOCK);
    if(fd_exec_set(task,slot,(flags&ATM_NATIVE_O_CLOEXEC)!=0)<0){(void)atm_posix_close(backend);task->fd_map[slot]=-1;return -1;}
    return slot;
}

int native_fd_pipe(task_t *task,int out_fds[2]){ return native_fd_pipe2(task,out_fds,0); }
int native_fd_pipe2(task_t *task,int out_fds[2],uint32_t flags){
    int rc;
    if(!out_fds||(flags&~(ATM_NATIVE_O_CLOEXEC|ATM_NATIVE_O_NONBLOCK)))return -1;
    rc=native_pipe_create(task,out_fds);
    if(rc==0){
        task->fd_flags[out_fds[0]]=O_RDONLY|(flags&ATM_NATIVE_O_NONBLOCK);
        task->fd_flags[out_fds[1]]=O_WRONLY|(flags&ATM_NATIVE_O_NONBLOCK);
        if(fd_exec_set(task,out_fds[0],(flags&ATM_NATIVE_O_CLOEXEC)!=0)<0||fd_exec_set(task,out_fds[1],(flags&ATM_NATIVE_O_CLOEXEC)!=0)<0){(void)native_pipe_close(task,out_fds[0]);(void)native_pipe_close(task,out_fds[1]);return -1;}
    }
    return rc;
}

int native_fd_close(task_t *task,int fd){
    int rc;
    if(native_pipe_is_fd(task,fd)){rc=native_pipe_close(task,fd);if(rc==0){task->fd_flags[fd]=0;(void)fd_exec_set(task,fd,0);}return rc;}
    if(native_socket_is_fd(task,fd)){rc=native_socket_close(task,fd);if(rc==0){task->fd_flags[fd]=0;(void)fd_exec_set(task,fd,0);}return rc;}
    if(!fd_valid(task,fd)) return -1;
    int backend=task->fd_map[fd];
    task->fd_map[fd]=-1;task->fd_flags[fd]=0;(void)fd_exec_set(task,fd,0);
    return atm_posix_close(backend);
}

int64_t native_fd_read(task_t *task,int fd,void *buf,uint64_t count){
    int64_t n=native_pipe_is_fd(task,fd)?native_pipe_read(task,fd,buf,count,(task->fd_flags[fd]&ATM_NATIVE_O_NONBLOCK)!=0):
              (native_socket_is_fd(task,fd)?native_socket_recv(task,fd,buf,count,300u):
              (fd_valid(task,fd)?atm_posix_read(task->fd_map[fd],buf,count):-1));
    if(n>0 && task) task->io_read_bytes+=(uint64_t)n;
    return n;
}

int64_t native_fd_write(task_t *task,int fd,const void *buf,uint64_t count){
    int64_t n=native_pipe_is_fd(task,fd)?native_pipe_write(task,fd,buf,count,(task->fd_flags[fd]&ATM_NATIVE_O_NONBLOCK)!=0):
              (native_socket_is_fd(task,fd)?native_socket_send(task,fd,buf,count):
              (fd_valid(task,fd)?atm_posix_write(task->fd_map[fd],buf,count):-1));
    if(n>0 && task) task->io_write_bytes+=(uint64_t)n;
    return n;
}
int64_t native_fd_readv(task_t *task,int fd,const atm_posix_iovec_t *iov,int iovcnt){
    if(!iov||iovcnt<0||iovcnt>16)return -1;
    if(native_pipe_is_fd(task,fd)){
        int64_t total=0;for(int i=0;i<iovcnt;i++){int64_t n=native_pipe_read(task,fd,iov[i].iov_base,iov[i].iov_len,(task->fd_flags[fd]&ATM_NATIVE_O_NONBLOCK)!=0);if(n<0)return total?total:n;total+=n;if((uint64_t)n<iov[i].iov_len)break;}if(total>0)task->io_read_bytes+=(uint64_t)total;return total;
    }
    if(native_socket_is_fd(task,fd)||!fd_valid(task,fd))return -1;
    int64_t n=atm_posix_readv(task->fd_map[fd],iov,iovcnt);if(n>0)task->io_read_bytes+=(uint64_t)n;return n;
}
int64_t native_fd_writev(task_t *task,int fd,const atm_posix_iovec_t *iov,int iovcnt){
    if(!iov||iovcnt<0||iovcnt>16)return -1;
    if(native_pipe_is_fd(task,fd)){
        int64_t total=0;for(int i=0;i<iovcnt;i++){int64_t n=native_pipe_write(task,fd,iov[i].iov_base,iov[i].iov_len,(task->fd_flags[fd]&ATM_NATIVE_O_NONBLOCK)!=0);if(n<0)return total?total:n;total+=n;if((uint64_t)n<iov[i].iov_len)break;}if(total>0)task->io_write_bytes+=(uint64_t)total;return total;
    }
    if(native_socket_is_fd(task,fd)||!fd_valid(task,fd))return -1;
    int64_t n=atm_posix_writev(task->fd_map[fd],iov,iovcnt);if(n>0)task->io_write_bytes+=(uint64_t)n;return n;
}

int64_t native_fd_lseek(task_t *task,int fd,int64_t offset,int whence){
    if(native_socket_is_fd(task,fd)) return -1;
    return fd_valid(task,fd)?atm_posix_lseek(task->fd_map[fd],offset,whence):-1;
}
int native_fd_fchdir(task_t *task,int fd){
    if(!task||native_pipe_is_fd(task,fd)||native_socket_is_fd(task,fd)||!fd_valid(task,fd))return -ATM_EBADF;
    return atm_posix_fchdir(task->fd_map[fd])<0?-ATM_ENOTDIR:0;
}
int native_fd_fchmod(task_t *task,int fd,uint32_t mode){
    if(!task||native_pipe_is_fd(task,fd)||native_socket_is_fd(task,fd)||!fd_valid(task,fd))return -ATM_EBADF;
    return atm_posix_fchmod(task->fd_map[fd],mode);
}
int native_fd_fchown(task_t *task,int fd,uint32_t uid,uint32_t gid){
    if(!task||native_pipe_is_fd(task,fd)||native_socket_is_fd(task,fd)||!fd_valid(task,fd))return -ATM_EBADF;
    return atm_posix_fchown(task->fd_map[fd],uid,gid);
}
int64_t native_fd_pread(task_t *task,int fd,void *buf,uint64_t count,uint64_t offset){
    if(native_socket_is_fd(task,fd)) return -1;
    return fd_valid(task,fd)?atm_posix_pread(task->fd_map[fd],buf,count,offset):-1;
}
int64_t native_fd_pwrite(task_t *task,int fd,const void *buf,uint64_t count,uint64_t offset){
    if(native_socket_is_fd(task,fd)) return -1;
    return fd_valid(task,fd)?atm_posix_pwrite(task->fd_map[fd],buf,count,offset):-1;
}

/* Positional vector I/O intentionally accepts only regular VFS-backed FDs.
 * Pipes and sockets have no stable seek position in this bounded runtime. */
int64_t native_fd_preadv(task_t *task,int fd,const atm_posix_iovec_t *iov,int iovcnt,uint64_t offset){
    if(!task||!iov||iovcnt<0||iovcnt>16||native_pipe_is_fd(task,fd)||native_socket_is_fd(task,fd)||!fd_valid(task,fd))return -1;
    int64_t total=0;uint64_t pos=offset;
    for(int i=0;i<iovcnt;i++){
        if(iov[i].iov_len&&!iov[i].iov_base)return total?total:-1;
        if(iov[i].iov_len>UINT64_MAX-pos)return total?total:-1;
        int64_t n=atm_posix_pread(task->fd_map[fd],iov[i].iov_base,iov[i].iov_len,pos);
        if(n<0)return total?total:n;
        total+=n;pos+=(uint64_t)n;
        if((uint64_t)n<iov[i].iov_len)break;
    }
    if(total>0)task->io_read_bytes+=(uint64_t)total;
    return total;
}
int64_t native_fd_pwritev(task_t *task,int fd,const atm_posix_iovec_t *iov,int iovcnt,uint64_t offset){
    if(!task||!iov||iovcnt<0||iovcnt>16||native_pipe_is_fd(task,fd)||native_socket_is_fd(task,fd)||!fd_valid(task,fd))return -1;
    int64_t total=0;uint64_t pos=offset;
    for(int i=0;i<iovcnt;i++){
        if(iov[i].iov_len&&!iov[i].iov_base)return total?total:-1;
        if(iov[i].iov_len>UINT64_MAX-pos)return total?total:-1;
        int64_t n=atm_posix_pwrite(task->fd_map[fd],iov[i].iov_base,iov[i].iov_len,pos);
        if(n<0)return total?total:n;
        total+=n;pos+=(uint64_t)n;
        if((uint64_t)n<iov[i].iov_len)break;
    }
    if(total>0)task->io_write_bytes+=(uint64_t)total;
    return total;
}

int native_fd_fstat(task_t *task,int fd,atm_posix_stat_t *st){
    if(native_socket_is_fd(task,fd)) return -1;
    return fd_valid(task,fd)?atm_posix_fstat(task->fd_map[fd],st):-1;
}
int native_fd_readdir(task_t *task,int fd,atm_posix_dirent_t *out){
    if(native_socket_is_fd(task,fd)||native_pipe_is_fd(task,fd)||!fd_valid(task,fd)) return -1;
    return vfs_fd_readdir(task->fd_map[fd],out);
}
static int native_fd_dup_min(task_t *task,int fd,int min_fd){
    if(!task||min_fd<0||min_fd>=TASK_FD_MAX) return -1;
    if(!(native_pipe_is_fd(task,fd)||fd_valid(task,fd)) || native_socket_is_fd(task,fd)) return -1;
    int slot=-1;
    for(int i=min_fd;i<TASK_FD_MAX;i++) if(task->fd_map[i]<0&&task->socket_map[i]<0&&task->pipe_map[i]<0){slot=i;break;}
    if(slot<0) return -1;
    if(native_pipe_is_fd(task,fd)){int out=native_pipe_dup(task,fd,slot);if(out>=0){task->fd_flags[out]=task->fd_flags[fd];(void)fd_exec_set(task,out,0);}return out;}
    int backend=atm_posix_dup(task->fd_map[fd]);if(backend<0)return -1;
    task->fd_map[slot]=backend;task->fd_flags[slot]=task->fd_flags[fd];(void)fd_exec_set(task,slot,0);return slot;
}
int native_fd_dup(task_t *task,int fd){return native_fd_dup_min(task,fd,0);}
int native_fd_dup2(task_t *task,int fd,int newfd){
    if(!task||newfd<0||newfd>=TASK_FD_MAX)return -1;
    if(fd==newfd && (native_pipe_is_fd(task,fd)||native_socket_is_fd(task,fd)||fd_valid(task,fd)))return newfd;
    if(!(native_pipe_is_fd(task,fd)||fd_valid(task,fd)) || native_socket_is_fd(task,fd))return -1;
    if(native_pipe_is_fd(task,newfd)) (void)native_pipe_close(task,newfd);
    else if(native_socket_is_fd(task,newfd)) (void)native_socket_close(task,newfd);
    else if(fd_valid(task,newfd)) (void)atm_posix_close(task->fd_map[newfd]);
    if(native_pipe_is_fd(task,fd)){int out=native_pipe_dup(task,fd,newfd);if(out>=0){task->fd_flags[out]=task->fd_flags[fd];(void)fd_exec_set(task,out,0);}return out;}
    int backend=atm_posix_dup(task->fd_map[fd]);if(backend<0)return -1;
    task->fd_map[newfd]=backend;task->fd_flags[newfd]=task->fd_flags[fd];(void)fd_exec_set(task,newfd,0);return newfd;
}
int native_fd_dup3(task_t *task,int fd,int newfd,uint32_t flags){
    if(fd==newfd||(flags&~ATM_NATIVE_O_CLOEXEC))return -1;
    int out=native_fd_dup2(task,fd,newfd);
    if(out<0)return out;
    if(fd_exec_set(task,out,(flags&ATM_NATIVE_O_CLOEXEC)!=0)<0){(void)native_fd_close(task,out);return -1;}
    return out;
}
int native_fd_ftruncate(task_t *task,int fd,uint64_t size){
    if(native_socket_is_fd(task,fd))return -1;
    return fd_valid(task,fd)?atm_posix_ftruncate(task->fd_map[fd],size):-1;
}
int native_fd_fsync(task_t *task,int fd,int data_only){
    if(native_socket_is_fd(task,fd)||!fd_valid(task,fd))return -1;
    return data_only?atm_posix_fdatasync(task->fd_map[fd]):atm_posix_fsync(task->fd_map[fd]);
}
int native_fd_isatty(task_t *task,int fd){return task&&fd>=0&&fd<=2&&fd_valid(task,fd);}
int native_fd_fcntl(task_t *task,int fd,int cmd,uint32_t arg){
    int is_pipe=native_pipe_is_fd(task,fd);
    int is_socket=native_socket_is_fd(task,fd);
    if(!task || !(is_pipe||is_socket||fd_valid(task,fd))) return -1;
    if(cmd==ATM_NATIVE_F_DUPFD) return native_fd_dup_min(task,fd,(int)arg);
    if(cmd==ATM_NATIVE_F_DUPFD_CLOEXEC){int out=native_fd_dup_min(task,fd,(int)arg);if(out<0||fd_exec_set(task,out,1)<0){if(out>=0)(void)native_fd_close(task,out);return -1;}return out;}
    if(cmd==ATM_NATIVE_F_GETFD) return fd_exec_get(task,fd)?ATM_NATIVE_FD_CLOEXEC:0;
    if(cmd==ATM_NATIVE_F_SETFD){
        if(arg&~ATM_NATIVE_FD_CLOEXEC) return -1;
        return fd_exec_set(task,fd,(arg&ATM_NATIVE_FD_CLOEXEC)!=0);
    }
    if(cmd==ATM_NATIVE_F_GETFL) return (int)task->fd_flags[fd];
    if(cmd==ATM_NATIVE_F_SETFL){
        if(is_socket || (arg&~ATM_NATIVE_O_NONBLOCK)) return -1;
        task->fd_flags[fd]=(task->fd_flags[fd]&ATM_NATIVE_O_ACCMODE)|(arg&ATM_NATIVE_O_NONBLOCK);
        return 0;
    }
    return -1;
}
int native_fd_poll(task_t *task,atm_native_pollfd_t *fds,uint32_t nfds){
    if(!task || !fds || nfds>ATM_NATIVE_POLL_MAX) return -1;
    int ready=0;
    for(uint32_t i=0;i<nfds;i++){
        uint16_t revents=native_pipe_is_fd(task,fds[i].fd)?native_pipe_poll(task,fds[i].fd,fds[i].events):
                         (native_socket_is_fd(task,fds[i].fd)?native_socket_poll(task,fds[i].fd,fds[i].events):ATM_POLLNVAL);
        fds[i].revents=revents;
        if(revents) ready++;
    }
    return ready;
}

int native_fd_selftest(void){
    static task_t test;
    kmemset(&test,0,sizeof(test));
    native_fd_task_init(&test);
    if(!fd_valid(&test,0)||!fd_valid(&test,1)||!fd_valid(&test,2)||fd_valid(&test,3)){native_fd_task_cleanup(&test);return -1;}
    int fd=native_fd_open(&test,"/tmp/.atm-native-fd-test",O_WRONLY|O_CREAT|O_TRUNC|ATM_NATIVE_O_CLOEXEC,0600);
    if(fd!=3 || native_fd_fcntl(&test,fd,ATM_NATIVE_F_GETFD,0)!=ATM_NATIVE_FD_CLOEXEC ||
       native_fd_write(&test,fd,"fd",2)!=2 || native_fd_close(&test,fd)<0) return -1;
    if(native_fd_close(&test,1)<0 || fd_valid(&test,1)) return -1;
    int pipefd[2];char byte=0;
    if(native_fd_pipe(&test,pipefd)<0 || native_fd_fcntl(&test,pipefd[0],ATM_NATIVE_F_GETFL,0)!=(int)O_RDONLY ||
       native_fd_fcntl(&test,pipefd[0],ATM_NATIVE_F_SETFL,ATM_NATIVE_O_NONBLOCK)<0 ||
       native_fd_fcntl(&test,pipefd[0],ATM_NATIVE_F_GETFL,0)!=(int)(O_RDONLY|ATM_NATIVE_O_NONBLOCK) ||
       native_fd_read(&test,pipefd[0],&byte,1)!=-ATM_EAGAIN) return -1;
    atm_native_pollfd_t pollfd={pipefd[0],ATM_POLLIN,0};
    if(native_fd_poll(&test,&pollfd,1)!=0 || pollfd.revents) return -1;
    pollfd.fd=pipefd[1];pollfd.events=ATM_POLLOUT;pollfd.revents=0;
    if(native_fd_poll(&test,&pollfd,1)!=1 || pollfd.revents!=ATM_POLLOUT) return -1;
    int copy=native_fd_dup(&test,pipefd[0]);
    int fdup=native_fd_fcntl(&test,pipefd[0],ATM_NATIVE_F_DUPFD,12);
    int cloexec_dup=native_fd_fcntl(&test,pipefd[1],ATM_NATIVE_F_DUPFD_CLOEXEC,13);
    if(copy<0 || fdup!=12 || cloexec_dup!=13 || native_fd_fcntl(&test,cloexec_dup,ATM_NATIVE_F_GETFD,0)!=ATM_NATIVE_FD_CLOEXEC || native_fd_dup3(&test,pipefd[1],14,ATM_NATIVE_O_CLOEXEC)!=14 || native_fd_fcntl(&test,14,ATM_NATIVE_F_GETFD,0)!=ATM_NATIVE_FD_CLOEXEC || native_fd_dup3(&test,pipefd[1],pipefd[1],0)>=0 || native_fd_close(&test,fdup)<0 ||
       native_fd_fcntl(&test,pipefd[0],ATM_NATIVE_F_GETFD,0)!=0 ||
       native_fd_fcntl(&test,pipefd[0],ATM_NATIVE_F_SETFD,ATM_NATIVE_FD_CLOEXEC)<0 ||
       native_fd_fcntl(&test,pipefd[0],ATM_NATIVE_F_GETFD,0)!=ATM_NATIVE_FD_CLOEXEC ||
       native_fd_fcntl(&test,copy,ATM_NATIVE_F_GETFD,0)!=0 ||
       native_fd_fcntl(&test,copy,ATM_NATIVE_F_GETFL,0)!=(int)(O_RDONLY|ATM_NATIVE_O_NONBLOCK)) return -1;
    native_fd_close_on_exec(&test);
    if(native_pipe_is_fd(&test,pipefd[0]) || native_pipe_is_fd(&test,cloexec_dup) || native_pipe_is_fd(&test,14) || native_fd_close(&test,copy)<0 || native_fd_close(&test,pipefd[1])<0) return -1;
    if(native_fd_pipe2(&test,pipefd,ATM_NATIVE_O_CLOEXEC|ATM_NATIVE_O_NONBLOCK)<0 || native_fd_fcntl(&test,pipefd[0],ATM_NATIVE_F_GETFD,0)!=ATM_NATIVE_FD_CLOEXEC || native_fd_fcntl(&test,pipefd[0],ATM_NATIVE_F_GETFL,0)!=(int)(O_RDONLY|ATM_NATIVE_O_NONBLOCK) || native_fd_close(&test,pipefd[0])<0 || native_fd_close(&test,pipefd[1])<0 || native_fd_pipe2(&test,pipefd,1u)>=0) return -1;
    native_fd_task_cleanup(&test);
    (void)atm_posix_unlink("/tmp/.atm-native-fd-test");
    return 0;
}
