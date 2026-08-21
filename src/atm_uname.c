#include "atm_uname.h"
#include "util.h"

void atm_uname_fill(atm_utsname_t *out){
    if(!out) return;
    kmemset(out,0,sizeof(*out));
    kstrcpy(out->sysname,"ATMKoala");
    kstrcpy(out->nodename,"atmkoala");
    kstrcpy(out->release,"0.9");
    kstrcpy(out->version,"ATMKoala native POSIX profile");
    kstrcpy(out->machine,"x86_64");
    kstrcpy(out->domainname,"localdomain");
}
