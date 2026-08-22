/* native_dir.c — original task-owned directory stream layer for ATMKoala.
 * VFS retains filesystem iteration. Native tasks hold only small integer
 * handles, so no kernel DIR_t pointer is disclosed to CPL3 applications. */
#include "native_dir.h"
#include "util.h"

static int dir_valid(const task_t *task,int handle){
    return task && task->fd_table_ready && handle>=0 && handle<ATM_NATIVE_DIR_MAX && task->dir_map[handle]!=0;
}

void native_dir_task_init(task_t *task){
    if(!task) return;
    for(int i=0;i<ATM_NATIVE_DIR_MAX;i++) task->dir_map[i]=0;
}

void native_dir_task_cleanup(task_t *task){
    if(!task) return;
    for(int i=0;i<ATM_NATIVE_DIR_MAX;i++){
        if(task->dir_map[i]){
            (void)atm_posix_closedir((atm_posix_dir_t *)task->dir_map[i]);
            task->dir_map[i]=0;
        }
    }
}

int native_dir_open(task_t *task,const char *path){
    if(!task || !path || !task->fd_table_ready) return -1;
    int handle=-1;
    for(int i=0;i<ATM_NATIVE_DIR_MAX;i++) if(!task->dir_map[i]){handle=i;break;}
    if(handle<0) return -1;
    atm_posix_dir_t *dir=atm_posix_opendir(path);
    if(!dir) return -1;
    task->dir_map[handle]=dir;
    return handle;
}

int native_dir_read(task_t *task,int handle,atm_posix_dirent_t *out){
    if(!out || !dir_valid(task,handle)) return -1;
    atm_posix_dirent_t *entry=atm_posix_readdir((atm_posix_dir_t *)task->dir_map[handle]);
    if(!entry) return 0; /* end of directory */
    kmemcpy(out,entry,sizeof(*out));
    return 1;
}

int native_dir_close(task_t *task,int handle){
    if(!dir_valid(task,handle)) return -1;
    atm_posix_dir_t *dir=(atm_posix_dir_t *)task->dir_map[handle];
    task->dir_map[handle]=0;
    return atm_posix_closedir(dir);
}

int native_dir_selftest(void){
    static task_t task;atm_posix_dirent_t ent;
    kmemset(&task,0,sizeof(task));task.fd_table_ready=1;
    native_dir_task_init(&task);
    int handle=native_dir_open(&task,"/");
    if(handle!=0 || native_dir_read(&task,handle,&ent)!=1 || !ent.name[0]){
        native_dir_task_cleanup(&task);return -1;
    }
    if(native_dir_close(&task,handle)<0 || native_dir_close(&task,handle)>=0) return -1;
    return 0;
}
