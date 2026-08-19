#include "atm_native_abi.h"

int atm_native_abi_compile_probe(void){
    volatile uint64_t abi=atm_abi_info();
    volatile uint64_t pid=atm_getpid();
    volatile uint64_t brk=atm_brk(0);
    return (abi&&pid<=0xffffffffULL&&brk)?0:0;
}
