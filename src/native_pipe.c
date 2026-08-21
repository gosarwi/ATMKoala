/* native_pipe.c — original bounded anonymous-pipe implementation for ATMKoala.
 * It intentionally exposes a small, static POSIX-shaped subset: byte streams,
 * EOF after all writers close, EPIPE after all readers close, dup sharing, and
 * single-core blocking/wakeup.  There is no fork inheritance yet. */
#include "native_pipe.h"
#include "native_socket.h"
#include "atm_syscall.h"
#include "util.h"

#define ATM_PIPE_READ_END  0
#define ATM_PIPE_WRITE_END 1
#define ATM_PIPE_EPIPE     32

typedef struct {
    uint8_t buffer[ATM_PIPE_CAP];
    uint32_t head,tail,count;
    uint32_t read_refs,write_refs;
    task_t *reader_waiter;
    task_t *writer_waiter;
    uint32_t used;
} native_pipe_t;

static native_pipe_t pipe_pool[ATM_PIPE_MAX];

static uint64_t pipe_irq_save(void){uint64_t flags;__asm__ volatile("pushfq; popq %0; cli":"=r"(flags)::"memory");return flags;}
static void pipe_irq_restore(uint64_t flags){if(flags&(1ULL<<9))__asm__ volatile("sti":::"memory");}

static int pipe_decode(const task_t *task,int fd,int *index,int *end){
    if(!task || !task->fd_table_ready || fd<3 || fd>=TASK_FD_MAX || task->pipe_map[fd]<0) return -1;
    int encoded=task->pipe_map[fd];
    int idx=encoded>>1,which=encoded&1;
    if(idx<0 || idx>=ATM_PIPE_MAX || !pipe_pool[idx].used) return -1;
    if(index) *index=idx;
    if(end) *end=which;
    return 0;
}

static int pipe_fd_free(const task_t *task,int fd){
    return task && fd>=3 && fd<TASK_FD_MAX && task->fd_map[fd]<0 &&
           task->socket_map[fd]<0 && task->pipe_map[fd]<0;
}

static void pipe_wake_reader(native_pipe_t *pipe){
    if(pipe && pipe->reader_waiter && pipe->reader_waiter->state==TASK_BLOCKED)
        task_unblock(pipe->reader_waiter);
}
static void pipe_wake_writer(native_pipe_t *pipe){
    if(pipe && pipe->writer_waiter && pipe->writer_waiter->state==TASK_BLOCKED)
        task_unblock(pipe->writer_waiter);
}
static void pipe_maybe_free(native_pipe_t *pipe){
    if(pipe && !pipe->read_refs && !pipe->write_refs) kmemset(pipe,0,sizeof(*pipe));
}

void native_pipe_task_init(task_t *task){
    if(!task) return;
    for(int i=0;i<TASK_FD_MAX;i++) task->pipe_map[i]=-1;
}

void native_pipe_task_cleanup(task_t *task){
    if(!task) return;
    for(int fd=3;fd<TASK_FD_MAX;fd++) if(task->pipe_map[fd]>=0) (void)native_pipe_close(task,fd);
}

int native_pipe_is_fd(const task_t *task,int fd){ return pipe_decode(task,fd,0,0)==0; }

int native_pipe_create(task_t *task,int out_fds[2]){
    if(!task || !out_fds || !task->fd_table_ready) return -1;
    int read_fd=-1,write_fd=-1,slot=-1;
    for(int fd=3;fd<TASK_FD_MAX;fd++) if(pipe_fd_free(task,fd)){read_fd=fd;break;}
    for(int fd=read_fd+1;fd<TASK_FD_MAX;fd++) if(pipe_fd_free(task,fd)){write_fd=fd;break;}
    for(int i=0;i<ATM_PIPE_MAX;i++) if(!pipe_pool[i].used){slot=i;break;}
    if(read_fd<0 || write_fd<0 || slot<0) return -1;
    native_pipe_t *pipe=&pipe_pool[slot];
    kmemset(pipe,0,sizeof(*pipe));
    pipe->used=1;pipe->read_refs=1;pipe->write_refs=1;
    task->pipe_map[read_fd]=(slot<<1)|ATM_PIPE_READ_END;
    task->pipe_map[write_fd]=(slot<<1)|ATM_PIPE_WRITE_END;
    out_fds[0]=read_fd;out_fds[1]=write_fd;
    return 0;
}

int native_pipe_close(task_t *task,int fd){
    int idx,end;
    if(pipe_decode(task,fd,&idx,&end)<0) return -1;
    native_pipe_t *pipe=&pipe_pool[idx];
    task->pipe_map[fd]=-1;
    if(end==ATM_PIPE_READ_END){if(pipe->read_refs)pipe->read_refs--;pipe_wake_writer(pipe);}
    else {if(pipe->write_refs)pipe->write_refs--;pipe_wake_reader(pipe);}
    pipe_maybe_free(pipe);
    return 0;
}

int native_pipe_dup(task_t *task,int oldfd,int newfd){
    int idx,end;
    if(pipe_decode(task,oldfd,&idx,&end)<0 || !pipe_fd_free(task,newfd)) return -1;
    native_pipe_t *pipe=&pipe_pool[idx];
    if(end==ATM_PIPE_READ_END) pipe->read_refs++; else pipe->write_refs++;
    task->pipe_map[newfd]=task->pipe_map[oldfd];
    return newfd;
}

int native_pipe_inherit(task_t *child,const task_t *parent,int fd){
    int idx,end;
    if(!child || !parent || fd<0 || fd>=TASK_FD_MAX || !pipe_fd_free(child,fd) ||
       pipe_decode(parent,fd,&idx,&end)<0) return -1;
    native_pipe_t *pipe=&pipe_pool[idx];
    if(end==ATM_PIPE_READ_END) pipe->read_refs++; else pipe->write_refs++;
    child->pipe_map[fd]=parent->pipe_map[fd];
    return 0;
}

uint16_t native_pipe_poll(task_t *task,int fd,uint16_t events){
    int idx,end;
    if(pipe_decode(task,fd,&idx,&end)<0) return ATM_POLLNVAL;
    uint64_t flags=pipe_irq_save();
    native_pipe_t *pipe=&pipe_pool[idx];
    uint16_t revents=0;
    if(end==ATM_PIPE_READ_END){
        if((events&ATM_POLLIN) && (pipe->count || !pipe->write_refs)) revents|=ATM_POLLIN;
        if(!pipe->write_refs) revents|=ATM_POLLHUP;
    } else {
        if(!pipe->read_refs) revents|=ATM_POLLERR;
        else if((events&ATM_POLLOUT) && pipe->count<ATM_PIPE_CAP) revents|=ATM_POLLOUT;
    }
    pipe_irq_restore(flags);
    return revents;
}

int64_t native_pipe_read(task_t *task,int fd,void *buf,uint64_t count,int nonblock){
    int idx,end;
    if(!buf || pipe_decode(task,fd,&idx,&end)<0 || end!=ATM_PIPE_READ_END) return -1;
    if(!count) return 0;
    native_pipe_t *pipe=&pipe_pool[idx];
    for(;;){
        uint64_t flags=pipe_irq_save();
        if(pipe->count){
            uint64_t n=count<pipe->count?count:pipe->count;
            for(uint64_t i=0;i<n;i++){((uint8_t *)buf)[i]=pipe->buffer[pipe->tail];pipe->tail=(pipe->tail+1u)%ATM_PIPE_CAP;}
            pipe->count-=(uint32_t)n;
            if(pipe->reader_waiter==task) pipe->reader_waiter=0;
            pipe_wake_writer(pipe);
            pipe_irq_restore(flags);
            return (int64_t)n;
        }
        if(!pipe->write_refs){
            if(pipe->reader_waiter==task) pipe->reader_waiter=0;
            pipe_irq_restore(flags);
            return 0;
        }
        if(nonblock){pipe_irq_restore(flags);return -ATM_EAGAIN;}
        pipe->reader_waiter=task;
        /* task_block() removes the current task before another runnable writer
         * can run on this single CPU, making the waiter publication atomic. */
        pipe_irq_restore(flags);
        if(task && task->address_space) task_block_from_syscall(); else task_block();
    }
}

int64_t native_pipe_write(task_t *task,int fd,const void *buf,uint64_t count,int nonblock){
    int idx,end;
    if(!buf || pipe_decode(task,fd,&idx,&end)<0 || end!=ATM_PIPE_WRITE_END) return -1;
    if(!count) return 0;
    native_pipe_t *pipe=&pipe_pool[idx];
    for(;;){
        uint64_t flags=pipe_irq_save();
        if(!pipe->read_refs){
            if(pipe->writer_waiter==task) pipe->writer_waiter=0;
            pipe_irq_restore(flags);
            return -ATM_PIPE_EPIPE;
        }
        if(pipe->count<ATM_PIPE_CAP){
            uint64_t room=ATM_PIPE_CAP-pipe->count;
            uint64_t n=count<room?count:room;
            for(uint64_t i=0;i<n;i++){pipe->buffer[pipe->head]=((const uint8_t *)buf)[i];pipe->head=(pipe->head+1u)%ATM_PIPE_CAP;}
            pipe->count+=(uint32_t)n;
            if(pipe->writer_waiter==task) pipe->writer_waiter=0;
            pipe_wake_reader(pipe);
            pipe_irq_restore(flags);
            return (int64_t)n;
        }
        if(nonblock){pipe_irq_restore(flags);return -ATM_EAGAIN;}
        pipe->writer_waiter=task;
        pipe_irq_restore(flags);
        if(task && task->address_space) task_block_from_syscall(); else task_block();
    }
}

int native_pipe_selftest(void){
    task_t task;int fds[2],fd;
    kmemset(&task,0,sizeof(task));
    for(int i=0;i<TASK_FD_MAX;i++){task.fd_map[i]=-1;task.socket_map[i]=-1;}
    native_pipe_task_init(&task);task.fd_table_ready=1;
    if(native_pipe_create(&task,fds)<0 || fds[0]!=3 || fds[1]!=4) return -1;
    if(native_pipe_poll(&task,fds[0],ATM_POLLIN)!=0 || native_pipe_poll(&task,fds[1],ATM_POLLOUT)!=ATM_POLLOUT) return -1;
    char out[5];kmemset(out,0,sizeof(out));
    if(native_pipe_read(&task,fds[0],out,1,1)!=-ATM_EAGAIN) return -1;
    if(native_pipe_write(&task,fds[1],"pipe",4,0)!=4) return -1;
    if(native_pipe_read(&task,fds[0],out,4,0)!=4 || kstrncmp(out,"pipe",4)) return -1;
    for(fd=5;fd<TASK_FD_MAX;fd++) if(pipe_fd_free(&task,fd)) break;
    if(fd>=TASK_FD_MAX || native_pipe_dup(&task,fds[1],fd)!=fd) return -1;
    if(native_pipe_close(&task,fds[1])<0 || native_pipe_write(&task,fd,"x",1,0)!=1) return -1;
    if(native_pipe_close(&task,fd)<0 || native_pipe_read(&task,fds[0],out,1,0)!=1) return -1;
    if(native_pipe_poll(&task,fds[0],ATM_POLLIN)!=(ATM_POLLIN|ATM_POLLHUP)) return -1;
    if(native_pipe_read(&task,fds[0],out,1,0)!=0 || native_pipe_close(&task,fds[0])<0) return -1;
    return 0;
}
