/* Adapted from musl libc (MIT); freestanding ATMKoala stdlib subset. */
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdint.h>

#define ATM_ULLONG_MAX (~0ULL)
#define ATM_LLONG_MAX  0x7fffffffffffffffLL
#define ATM_LLONG_MIN  (-ATM_LLONG_MAX-1LL)

void exit(int status){ _exit(status); }
int abs(int value){ return value<0?-value:value; }
long labs(long value){ return value<0?-value:value; }
long long llabs(long long value){ return value<0?-value:value; }
div_t div(int numer,int denom){div_t r;r.quot=numer/denom;r.rem=numer%denom;return r;}
ldiv_t ldiv(long numer,long denom){ldiv_t r;r.quot=numer/denom;r.rem=numer%denom;return r;}
lldiv_t lldiv(long long numer,long long denom){lldiv_t r;r.quot=numer/denom;r.rem=numer%denom;return r;}

static int digit_value(unsigned char c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='z')return c-'a'+10;
    if(c>='A'&&c<='Z')return c-'A'+10;
    return 36;
}

/* Parses magnitude only. The caller validates sign-specific limits. */
static unsigned long long scan_uint(const char *s,char **end,int base,unsigned long long limit,int *negative){
    const char *p=s,*digits; unsigned long long value=0; int overflow=0,any=0;
    while(*p==' '||*p=='\t'||*p=='\n'||*p=='\r'||*p=='\v'||*p=='\f')p++;
    *negative=0;
    if(*p=='+'||*p=='-'){if(*p=='-')*negative=1;p++;}
    if(base==0){
        if(p[0]=='0' && (p[1]=='x'||p[1]=='X') && digit_value((unsigned char)p[2])<16){base=16;p+=2;}
        else if(p[0]=='0')base=8;
        else base=10;
    } else if(base==16 && p[0]=='0' && (p[1]=='x'||p[1]=='X') && digit_value((unsigned char)p[2])<16) p+=2;
    if(base<2||base>36){errno=EINVAL;if(end)*end=(char *)s;return 0;}
    digits=p;
    for(;;){
        int d=digit_value((unsigned char)*p);
        if(d>=base)break;
        any=1;
        if(value>(limit-(unsigned long long)d)/(unsigned long long)base)overflow=1;
        else if(!overflow)value=value*(unsigned long long)base+(unsigned long long)d;
        p++;
    }
    if(!any){if(end)*end=(char *)s;return 0;}
    (void)digits;
    if(end)*end=(char *)p;
    if(overflow){errno=ERANGE;return limit;}
    return value;
}

unsigned long long strtoull(const char *restrict s,char **restrict end,int base){
    int negative=0;unsigned long long value=scan_uint(s,end,base,ATM_ULLONG_MAX,&negative);
    return negative ? 0ULL-value : value;
}
unsigned long strtoul(const char *restrict s,char **restrict end,int base){
    unsigned long long value=strtoull(s,end,base);
    if(value>(unsigned long)ATM_ULLONG_MAX){errno=ERANGE;return (unsigned long)ATM_ULLONG_MAX;}
    return (unsigned long)value;
}
long long strtoll(const char *restrict s,char **restrict end,int base){
    int negative=0;
    /* Sign is determined inside scan_uint; scan against the negative-safe maximum. */
    unsigned long long value=scan_uint(s,end,base,0x8000000000000000ULL,&negative);
    if(!negative){
        if(value>(unsigned long long)ATM_LLONG_MAX){errno=ERANGE;return ATM_LLONG_MAX;}
        return (long long)value;
    }
    if(value==0x8000000000000000ULL)return ATM_LLONG_MIN;
    return -(long long)value;
}
long strtol(const char *restrict s,char **restrict end,int base){return (long)strtoll(s,end,base);}
int atoi(const char *s){return (int)strtol(s,0,10);}
long atol(const char *s){return strtol(s,0,10);}
long long atoll(const char *s){return strtoll(s,0,10);}

void *bsearch(const void *key,const void *base,size_t nel,size_t width,int (*cmp)(const void *,const void *)){
    const char *candidate;int sign;
    while(nel){candidate=(const char *)base+width*(nel/2);sign=cmp(key,candidate);if(sign<0)nel/=2;else if(sign>0){base=candidate+width;nel-=nel/2+1;}else return(void *)candidate;}
    return 0;
}
