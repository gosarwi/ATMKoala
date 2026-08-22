#ifndef ATM_LIBC_SYS_RESOURCE_H
#define ATM_LIBC_SYS_RESOURCE_H

#include <stdint.h>
#include <sys/time.h>

#define RUSAGE_SELF 0
#define RLIMIT_STACK 3
#define RLIMIT_NOFILE 7
#define RLIMIT_AS 9

typedef uint64_t rlim_t;
struct rlimit { rlim_t rlim_cur,rlim_max; };

/* ATMKoala exposes self-only PIT-accounted user time, resident KiB and
 * voluntary context switches. The remaining traditional rusage fields are
 * defined for source compatibility and currently reported as zero. */
struct rusage {
    struct timeval ru_utime;
    struct timeval ru_stime;
    int64_t ru_maxrss;
    int64_t ru_ixrss;
    int64_t ru_idrss;
    int64_t ru_isrss;
    int64_t ru_minflt;
    int64_t ru_majflt;
    int64_t ru_nswap;
    int64_t ru_inblock;
    int64_t ru_oublock;
    int64_t ru_msgsnd;
    int64_t ru_msgrcv;
    int64_t ru_nsignals;
    int64_t ru_nvcsw;
    int64_t ru_nivcsw;
};

int getrusage(int who,struct rusage *usage);
/* Read-only fixed limits only: RLIMIT_STACK, RLIMIT_NOFILE and RLIMIT_AS. */
int getrlimit(int resource,struct rlimit *limit);

#endif
