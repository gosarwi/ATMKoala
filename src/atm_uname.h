#ifndef ATM_UNAME_H
#define ATM_UNAME_H

#include <stdint.h>

#define ATM_UTS_FIELD_LEN 65u

typedef struct {
    char sysname[ATM_UTS_FIELD_LEN];
    char nodename[ATM_UTS_FIELD_LEN];
    char release[ATM_UTS_FIELD_LEN];
    char version[ATM_UTS_FIELD_LEN];
    char machine[ATM_UTS_FIELD_LEN];
    char domainname[ATM_UTS_FIELD_LEN];
} atm_utsname_t;

/* Fills a fixed, NUL-terminated local identity record. It intentionally
 * exposes no network, DNS, mutable hostname, or external configuration. */
void atm_uname_fill(atm_utsname_t *out);

#endif
