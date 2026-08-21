#include <sys/utsname.h>
#include "atm_native_abi.h"
#include "internal.h"

int uname(struct utsname *name){
    if(!name) return (int)__atm_sysret(-ATM_EFAULT);
    return (int)__atm_sysret(atm_uname((atm_utsname_t *)name));
}
