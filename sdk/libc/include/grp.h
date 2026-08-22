#ifndef ATM_LIBC_GRP_H
#define ATM_LIBC_GRP_H

#include <unistd.h>

/* ATMKoala currently has exactly one immutable primary credential per task.
 * `getgroups` therefore reports an empty supplementary-group vector. Mutation
 * helpers are present for source portability but fail with EPERM. */
int getgroups(int size,gid_t list[]);
int setgroups(size_t size,const gid_t list[]);
int initgroups(const char *user,gid_t group);

#endif
