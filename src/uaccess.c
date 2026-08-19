#include "uaccess.h"
#include "kmalloc.h"
#include "util.h"
#include <stdint.h>
#include <stddef.h>

static int user_copy(const user_space_t *s,uint64_t uaddr,void *kaddr,size_t len,int to_user){
    if(!s||!kaddr) return -1;
    if(!len) return 0;
    if(paging_user_range(s,uaddr,len,to_user)<0) return -1;
    uint8_t *k=(uint8_t *)kaddr;
    while(len){
        uintptr_t phys=0;
        if(paging_user_translate(s,uaddr,&phys,0)<0) return -1;
        size_t room=ATM_PAGE_SIZE-(size_t)(uaddr&0xfffULL);
        size_t n=len<room?len:room;
        if(to_user) kmemcpy((void *)phys,k,n);
        else kmemcpy(k,(const void *)phys,n);
        uaddr+=n; k+=n; len-=n;
    }
    return 0;
}

int copy_from_user(const user_space_t *s,void *dst,const void *user_src,size_t len){
    return user_copy(s,(uint64_t)(uintptr_t)user_src,dst,len,0);
}
int copy_to_user(const user_space_t *s,void *user_dst,const void *src,size_t len){
    return user_copy(s,(uint64_t)(uintptr_t)user_dst,(void *)src,len,1);
}

int user_range_valid(const user_space_t *s,const void *user_ptr,size_t len,int write_access){
    if(!s || (!user_ptr && len)) return -1;
    return paging_user_range(s,(uint64_t)(uintptr_t)user_ptr,len,write_access?1:0);
}

int strnlen_user(const user_space_t *s,const char *user_src,size_t max,size_t *len_out){
    if(!s||!user_src||!max) return -1;
    uint64_t addr=(uint64_t)(uintptr_t)user_src;
    for(size_t n=0;n<max;n++){
        char c;
        if(copy_from_user(s,&c,(const void *)(uintptr_t)(addr+n),1)<0) return -1;
        if(!c){ if(len_out)*len_out=n; return 0; }
    }
    return -1;
}
int copy_string_from_user(const user_space_t *s,char *dst,size_t dst_size,const char *user_src){
    size_t n=0;
    if(!dst||dst_size<2||strnlen_user(s,user_src,dst_size-1,&n)<0) return -1;
    if(copy_from_user(s,dst,user_src,n+1)<0) return -1;
    return 0;
}

int uaccess_selftest(void){
    user_space_t s;
    uint8_t *page=(uint8_t *)kmalloc_aligned(ATM_PAGE_SIZE,ATM_PAGE_SIZE);
    if(!page||paging_create_user_space(&s)<0) return -1;
    if(paging_map_user_page(&s,ATM_USER_BASE,(uintptr_t)page,ATM_PTE_W)<0) return -1;
    const char in[]="uaccess-ok"; char out[16];
    if(copy_to_user(&s,(void *)(uintptr_t)ATM_USER_BASE,in,sizeof(in))<0) return -1;
    if(copy_from_user(&s,out,(const void *)(uintptr_t)ATM_USER_BASE,sizeof(in))<0) return -1;
    if(kstrcmp(out,in)) return -1;
    size_t n=0;
    if(strnlen_user(&s,(const char *)(uintptr_t)ATM_USER_BASE,32,&n)<0||n!=10) return -1;
    return copy_from_user(&s,out,(const void *)(uintptr_t)(ATM_USER_TOP-1),2)<0?0:-1;
}
