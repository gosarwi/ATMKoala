#ifndef ATM_NATIVE_DIR_H
#define ATM_NATIVE_DIR_H

#include <stdint.h>
#include "sched.h"
#include "atm_posix.h"

#define ATM_NATIVE_DIR_MAX 16

void    native_dir_task_init(task_t *task);
void    native_dir_task_cleanup(task_t *task);
int     native_dir_open(task_t *task,const char *path);
int     native_dir_read(task_t *task,int handle,atm_posix_dirent_t *out);
int     native_dir_close(task_t *task,int handle);
int     native_dir_selftest(void);

#endif
