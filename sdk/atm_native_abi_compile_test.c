#include "atm_native_abi.h"

int atm_native_abi_compile_probe(void){
    volatile uint64_t abi=atm_abi_info();
    volatile uint64_t pid=atm_getpid();
    volatile uint64_t brk=atm_brk(0);
    int pipefd[2]={-1,-1};
    volatile int64_t pipe_rc=atm_pipe(pipefd);
    volatile int64_t wait_rc=atm_waitpid(-1,0,ATM_WNOHANG);
    volatile int64_t kill_rc=atm_kill(-1,15);
    atm_dirent_t entry;
    volatile int64_t dir_open_rc=atm_opendir("/");
    volatile int64_t dir_read_rc=atm_readdir(0,&entry);
    volatile int64_t dir_close_rc=atm_closedir(0);
    volatile int64_t fcntl_rc=atm_fcntl(0,ATM_F_GETFL,0);
    atm_pollfd_t pollfd={0,ATM_POLLIN,0};
    volatile int64_t poll_rc=atm_poll(&pollfd,1,0);
    (void)pipe_rc;(void)wait_rc;(void)kill_rc;(void)dir_open_rc;(void)dir_read_rc;(void)dir_close_rc;(void)fcntl_rc;(void)poll_rc;
    return (abi&&pid<=0xffffffffULL&&brk)?0:0;
}
