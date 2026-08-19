/* bin_init.c — atmkoala v0.5 — /bin/ stubs */
#include "vfs.h"
#include "util.h"

static void bw(const char *p, const char *c){
    int fd=vfs_open(p,O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if(fd>=0){vfs_write(fd,c,(uint32_t)kstrlen(c));vfs_close(fd);}
}

void bin_init(void){
    vfs_mkdir("/bin", 0755);
    bw("/bin/ls","#!/bin/atsh\nls $A\n");
    bw("/bin/ll","#!/bin/atsh\nls -l $A\n");
    bw("/bin/cat","#!/bin/atsh\ncat $A\n");
    bw("/bin/grep","#!/bin/atsh\ngrep $A\n");
    bw("/bin/find","#!/bin/atsh\nfind $A\n");
    bw("/bin/head","#!/bin/atsh\nhead $A\n");
    bw("/bin/tail","#!/bin/atsh\ntail $A\n");
    bw("/bin/wc","#!/bin/atsh\nwc $A\n");
    bw("/bin/sort","#!/bin/atsh\nsort $A\n");
    bw("/bin/echo","#!/bin/atsh\necho $A\n");
    bw("/bin/mkdir","#!/bin/atsh\nmkdir $A\n");
    bw("/bin/rm","#!/bin/atsh\nrm $A\n");
    bw("/bin/cp","#!/bin/atsh\ncp $A\n");
    bw("/bin/mv","#!/bin/atsh\nmv $A\n");
    bw("/bin/touch","#!/bin/atsh\ntouch $A\n");
    bw("/bin/chmod","#!/bin/atsh\nchmod $A\n");
    bw("/bin/stat","#!/bin/atsh\nstat $A\n");
    bw("/bin/pwd","#!/bin/atsh\npwd\n");
    bw("/bin/ps","#!/bin/atsh\nps\n");
    bw("/bin/kill","#!/bin/atsh\nkill $A\n");
    bw("/bin/clear","#!/bin/atsh\nclear\n");
    bw("/bin/date","#!/bin/atsh\ndate\n");
    bw("/bin/uptime","#!/bin/atsh\nuptime\n");
    bw("/bin/free","#!/bin/atsh\nfree\n");
    bw("/bin/uname","#!/bin/atsh\nuname $A\n");
    bw("/bin/ping","#!/bin/atsh\nping $A\n");
    bw("/bin/tess","#!/bin/atsh\ntess $A\n");
    bw("/bin/vi","#!/bin/atsh\ntess $A\n");
    bw("/bin/ai","#!/bin/atsh\nai $A\n");
    bw("/bin/kmod","#!/bin/atsh\nkmod $A\n");
    bw("/bin/which","#!/bin/atsh\nwhich $A\n");
    bw("/bin/man","#!/bin/atsh\nman $A\n");
}
