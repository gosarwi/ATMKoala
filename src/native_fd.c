#include "native_fd.h"
#include "util.h"
#include "vfs.h"
#include "native_socket.h"
#include <stdint.h>

static int fd_valid(const task_t *task,int fd){
    return task && task->fd_table_ready && fd>=0 && fd<TASK_FD_MAX && task->fd_map[fd]>=0;
}

void native_fd_task_init(task_t *task){
    if(!task) return;
    for(int i=0;i<TASK_FD_MAX;i++) task->fd_map[i]=-1;
    native_socket_task_init(task);
    /* Kernel bootstrap fd 0–2 point at /dev/null. Native applications need
     * real console semantics, therefore each task receives independent VFS
     * opens of /dev/tty that can be safely released on task exit. */
    task->fd_map[0]=atm_posix_open("/dev/tty",O_RDONLY,0);
    task->fd_map[1]=atm_posix_open("/dev/tty",O_WRONLY,0);
    task->fd_map[2]=atm_posix_open("/dev/tty",O_WRONLY,0);
    task->fd_table_ready=1;
}

void native_fd_task_cleanup(task_t *task){
    if(!task || !task->fd_table_ready) return;
    native_socket_task_cleanup(task);
    for(int i=0;i<TASK_FD_MAX;i++){
        if(task->fd_map[i]>=0) (void)atm_posix_close(task->fd_map[i]);
        task->fd_map[i]=-1;
    }
    task->fd_table_ready=0;
}

static int native_fd_slot(task_t *task){
    if(!task || !task->fd_table_ready) return -1;
    for(int i=3;i<TASK_FD_MAX;i++) if(task->fd_map[i]<0) return i;
    return -1;
}

int native_fd_open(task_t *task,const char *path,uint32_t flags,uint32_t mode){
    int slot=native_fd_slot(task);
    if(slot<0) return -1;
    int backend=atm_posix_open(path,flags,mode);
    if(backend<0) return -1;
    task->fd_map[slot]=backend;
    return slot;
}

int native_fd_close(task_t *task,int fd){
    if(native_socket_is_fd(task,fd)) return native_socket_close(task,fd);
    if(!fd_valid(task,fd)) return -1;
    int backend=task->fd_map[fd];
    task->fd_map[fd]=-1;
    return fd<3?0:atm_posix_close(backend);
}

int64_t native_fd_read(task_t *task,int fd,void *buf,uint64_t count){
    return native_socket_is_fd(task,fd)?native_socket_recv(task,fd,buf,count,300u):
           (fd_valid(task,fd)?atm_posix_read(task->fd_map[fd],buf,count):-1);
}

int64_t native_fd_write(task_t *task,int fd,const void *buf,uint64_t count){
    return native_socket_is_fd(task,fd)?native_socket_send(task,fd,buf,count):
           (fd_valid(task,fd)?atm_posix_write(task->fd_map[fd],buf,count):-1);
}

int64_t native_fd_lseek(task_t *task,int fd,int64_t offset,int whence){
    if(native_socket_is_fd(task,fd)) return -1;
    return fd_valid(task,fd)?atm_posix_lseek(task->fd_map[fd],offset,whence):-1;
}

int native_fd_fstat(task_t *task,int fd,atm_posix_stat_t *st){
    if(native_socket_is_fd(task,fd)) return -1;
    return fd_valid(task,fd)?atm_posix_fstat(task->fd_map[fd],st):-1;
}

int native_fd_selftest(void){
    task_t test;
    kmemset(&test,0,sizeof(test));
    native_fd_task_init(&test);
    if(!fd_valid(&test,0)||!fd_valid(&test,1)||!fd_valid(&test,2)||fd_valid(&test,3)){native_fd_task_cleanup(&test);return -1;}
    int fd=native_fd_open(&test,"/tmp/.atm-native-fd-test",O_WRONLY|O_CREAT|O_TRUNC,0600);
    if(fd!=3 || native_fd_write(&test,fd,"fd",2)!=2 || native_fd_close(&test,fd)<0) return -1;
    if(native_fd_close(&test,1)<0 || fd_valid(&test,1)) return -1;
    native_fd_task_cleanup(&test);
    (void)atm_posix_unlink("/tmp/.atm-native-fd-test");
    return 0;
}
