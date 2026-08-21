/* ATMKoala freestanding dirent wrappers — original MIT-compatible project code. */
#include <dirent.h>
#include <stdlib.h>
#include <errno.h>
#include "atm_native_abi.h"
#include "internal.h"

DIR *opendir(const char *path){
    int64_t handle=atm_opendir(path);
    if(handle<0){(void)__atm_sysret(handle);return NULL;}
    DIR *dir=(DIR *)malloc(sizeof(*dir));
    if(!dir){
        (void)atm_closedir((int)handle);
        errno=ENOMEM;
        return NULL;
    }
    dir->handle=(int)handle;
    for(uint64_t i=0;i<sizeof(dir->entry);i++) ((uint8_t *)&dir->entry)[i]=0;
    return dir;
}

dirent_t *readdir(DIR *dir){
    if(!dir){errno=EINVAL;return NULL;}
    int64_t rc=atm_readdir(dir->handle,(atm_dirent_t *)&dir->entry);
    if(rc==0) return NULL;
    if(rc<0){(void)__atm_sysret(rc);return NULL;}
    return &dir->entry;
}

int closedir(DIR *dir){
    if(!dir){errno=EINVAL;return -1;}
    int rc=(int)__atm_sysret(atm_closedir(dir->handle));
    if(rc==0) free(dir);
    return rc;
}
