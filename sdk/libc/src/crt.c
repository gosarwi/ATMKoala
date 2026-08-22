/* ATMKoala native libc CRT — MIT licensed project code. */
#include <stdint.h>
#include "atm_crt.h"
#include "atm_native_abi.h"

extern int main(int argc,char **argv,char **envp);
char **environ;

__attribute__((noreturn)) void __atm_libc_start_main(uint64_t *stack){
    int argc=stack?(int)stack[0]:0;
    char **argv=stack?(char **)(stack+1):0;
    char **envp=argv?argv+argc+1:0;
    environ=envp;
    int status=main(argc,argv,envp);
    atm_exit(status);
}
