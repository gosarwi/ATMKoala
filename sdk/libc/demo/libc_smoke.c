#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include "atm_gui.h"

static int cmp_int(const void *a,const void *b){
    int x=*(const int *)a,y=*(const int *)b;
    return (x>y)-(x<y);
}

int main(int argc,char **argv,char **envp){
    (void)argc; (void)argv; (void)envp;
    static const char expected[]="[libc] static CRT + musl subset + POSIX I/O = OK\n";
    size_t n=strlen(expected);
    char *buffer=(char *)calloc(n+1,1);
    if(!buffer) return 1;
    strcpy(buffer,expected);
    if(strlen(buffer)!=n || strcmp(buffer,expected)!=0) return 2;
    char *grown=(char *)realloc(buffer,n+32);
    if(!grown) return 3;

    char words[]="Alpha,beta;GAMMA"; char *save=0;
    char *one=strtok_r(words,",;",&save),*two=strtok_r(0,",;",&save),*three=strtok_r(0,",;",&save);
    if(!one||!two||!three||strcmp(one,"Alpha")||strcmp(two,"beta")||strcasecmp(three,"gamma")) return 4;
    if(strspn("abc123","abc")!=3 || strcspn("abc123","123")!=3 || !strstr("atmkoala","koal") || *strrchr("a/b/c",'/')!='/') return 5;
    char *dupe=strndup("native-libc",6);
    if(!dupe || strcmp(dupe,"native")) return 6;
    free(dupe);
    if(!isalpha('A')||!isdigit('7')||!isxdigit('f')||!isspace('\t')||tolower('Q')!='q'||toupper('m')!='M') return 7;

    char *end=0; errno=0;
    if(strtol(" -0x2aZ",&end,0)!=-42 || !end || *end!='Z' || errno) return 8;
    errno=0; if(strtoull("18446744073709551616",&end,10)!=0xffffffffffffffffULL || errno!=ERANGE) return 9;
    int sorted[]={1,4,9,16,25}; int wanted=16; int *found=(int *)bsearch(&wanted,sorted,5,sizeof(sorted[0]),cmp_int);
    if(!found || *found!=16 || div(17,5).rem!=2 || lldiv(-17,5).quot!=-3) return 10;

    atm_gui_runtime_info_t gui;
    if(atm_gui_runtime_info(&gui)<0 || gui.abi_version!=ATM_GUI_ABI_VERSION || gui.capabilities!=0) return 11;
    if(atm_gui_window_create(NULL,NULL)!=-1 || errno!=ENOSYS) return 12;
    stat_t st;
    if(fstat(STDOUT_FILENO,&st)<0) return 13;
    int sock=socket(AF_INET,SOCK_STREAM,IPPROTO_TCP);
    if(sock<3 || close(sock)<0) return 14;
    if(write(STDOUT_FILENO,grown,n)!=(ssize_t)n) return 15;
    free(grown);
    return 42;
}
