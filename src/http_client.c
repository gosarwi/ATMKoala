#include "http_client.h"
#include "net_tcp.h"
#include "unm.h"
#include "net.h"
#include "kmalloc.h"
#include "util.h"
#include <stdint.h>

static int ascii_lower(int c){return c>='A'&&c<='Z'?c+('a'-'A'):c;}
static int mem_case_eq(const uint8_t *a,uint32_t n,const char *b){
    uint32_t i=0;for(;i<n&&b[i];i++)if(ascii_lower(a[i])!=ascii_lower((uint8_t)b[i]))return 0;
    return i==n&&!b[i];
}
static int find_crlf(const uint8_t *buf,uint32_t from,uint32_t limit,uint32_t *at){
    for(uint32_t i=from;i+1<limit;i++)if(buf[i]=='\r'&&buf[i+1]=='\n'){*at=i;return 0;}return -1;
}
static int find_header_end(const uint8_t *buf,uint32_t size,uint32_t *end){
    for(uint32_t i=0;i+3<size;i++)if(buf[i]=='\r'&&buf[i+1]=='\n'&&buf[i+2]=='\r'&&buf[i+3]=='\n'){*end=i+4;return 0;}
    return -1;
}
static int parse_decimal(const uint8_t *s,uint32_t n,uint32_t *out){
    uint64_t v=0;if(!n)return -1;
    for(uint32_t i=0;i<n;i++){if(s[i]<'0'||s[i]>'9')return -1;v=v*10u+(uint64_t)(s[i]-'0');if(v>ATM_HTTP_RESPONSE_MAX)return -1;}
    *out=(uint32_t)v;return 0;
}
/* Parses only the header features this transport can enforce. */
static int http_parse_headers(const uint8_t *buf,uint32_t end,uint32_t *status,uint32_t *length){
    uint32_t line=0,next=0,have_length=0;*status=0;*length=0;
    if(find_crlf(buf,0,end,&next)<0||next<12||kmemcmp(buf,"HTTP/1.",7)!=0)return -1;
    if(buf[7]<'0'||buf[7]>'9'||buf[8]!=' '||buf[9]<'0'||buf[9]>'9'||buf[10]<'0'||buf[10]>'9'||buf[11]<'0'||buf[11]>'9')return -1;
    *status=(uint32_t)(buf[9]-'0')*100u+(uint32_t)(buf[10]-'0')*10u+(uint32_t)(buf[11]-'0');
    line=next+2;
    while(line+2<=end){
        if(buf[line]=='\r'&&buf[line+1]=='\n')break;
        if(find_crlf(buf,line,end,&next)<0)return -1;
        uint32_t colon=line;while(colon<next&&buf[colon]!=':')colon++;
        if(colon==line||colon==next)return -1;
        uint32_t value=colon+1;while(value<next&&(buf[value]==' '||buf[value]=='\t'))value++;
        if(mem_case_eq(buf+line,colon-line,"Content-Length")){
            if(have_length||parse_decimal(buf+value,next-value,length)<0)return -1;have_length=1;
        } else if(mem_case_eq(buf+line,colon-line,"Transfer-Encoding")) return -1;
        line=next+2;
    }
    return have_length?0:-1;
}
static int parse_url(const char *url,char host[ATM_HTTP_HOST_MAX+1],char path[ATM_HTTP_PATH_MAX+1],uint16_t *port){
    const char *p=url,*start,*slash;uint32_t host_len=0;
    if(!url||kstrncmp(p,"http://",7)!=0)return -1;p+=7;start=p;
    while(*p&&*p!='/'&&*p!=':'){if((uint8_t)*p<=0x20)return -1;p++;}
    host_len=(uint32_t)(p-start);if(!host_len||host_len>ATM_HTTP_HOST_MAX)return -1;kmemcpy(host,start,host_len);host[host_len]=0;*port=80;
    if(*p==':'){
        uint32_t value=0,digits=0;p++;
        while(*p&&*p!='/'){if(*p<'0'||*p>'9'||++digits>5)return -1;value=value*10u+(uint32_t)(*p-'0');if(value>65535u)return -1;p++;}
        if(!digits||!value)return -1;*port=(uint16_t)value;
    }
    slash=p;if(!*slash){kstrcpy(path,"/");return 0;}
    if(*slash!='/')return -1;uint32_t plen=(uint32_t)kstrlen(slash);if(!plen||plen>ATM_HTTP_PATH_MAX)return -1;
    for(uint32_t i=0;i<plen;i++)if((uint8_t)slash[i]<=0x20||slash[i]=='#')return -1;
    kmemcpy(path,slash,plen+1);return 0;
}
static int make_request(char *out,uint32_t cap,const char *host,const char *path){
    uint32_t need=5u+(uint32_t)kstrlen(path)+11u+6u+(uint32_t)kstrlen(host)+36u;
    if(need>=cap)return -1;
    kstrcpy(out,"GET ");kstrcat(out,path);kstrcat(out," HTTP/1.0\r\nHost: ");kstrcat(out,host);
    kstrcat(out,"\r\nConnection: close\r\nAccept: application/octet-stream\r\n\r\n");return (int)kstrlen(out);
}

int atm_http_get(const char *url,uint8_t *out,uint32_t cap,atm_http_response_t *response){
    char host[ATM_HTTP_HOST_MAX+1],path[ATM_HTTP_PATH_MAX+1];uint16_t port=0;uint8_t ip[4];int rc=-1;
    uint8_t *headers=0,*chunk=0;char *request=0;atm_tcp_conn_t *conn=0;
    if(response)kmemset(response,0,sizeof(*response));
    if(!url||!out||!cap||cap>ATM_HTTP_RESPONSE_MAX||!net.initialized||g_unm.state!=UNM_STATE_UP)return -1;
    if(parse_url(url,host,path,&port)<0||unm_dns_resolve(host,ip)<0)return -1;
    headers=(uint8_t *)kmalloc(1024);chunk=(uint8_t *)kmalloc(ATM_TCP_MSS);request=(char *)kmalloc(ATM_TCP_MSS);conn=(atm_tcp_conn_t *)kmalloc(sizeof(*conn));
    if(!headers||!chunk||!request||!conn)goto done;
    int request_len=make_request(request,ATM_TCP_MSS,host,path);if(request_len<0)goto done;
    if(atm_tcp_connect(conn,ip,port,ATM_HTTP_TIMEOUT_TICKS)<0||atm_tcp_send(conn,request,(uint16_t)request_len)<0)goto done;
    uint32_t used=0,header_end=0,status=0,length=0,body=0;
    while(!header_end){
        int n=atm_tcp_recv(conn,chunk,ATM_TCP_MSS,ATM_HTTP_TIMEOUT_TICKS);if(n<=0||used+(uint32_t)n>1024u)goto done;
        kmemcpy(headers+used,chunk,(uint32_t)n);used+=(uint32_t)n;
        if(find_header_end(headers,used,&header_end)==0)break;
    }
    if(http_parse_headers(headers,header_end,&status,&length)<0||status!=200u||length>cap)goto done;
    body=used-header_end;if(body>length)goto done;if(body)kmemcpy(out,headers+header_end,body);
    while(body<length){
        uint32_t want=length-body;if(want>ATM_TCP_MSS)want=ATM_TCP_MSS;
        int n=atm_tcp_recv(conn,chunk,(uint16_t)want,ATM_HTTP_TIMEOUT_TICKS);if(n<=0||(uint32_t)n>want)goto done;
        kmemcpy(out+body,chunk,(uint32_t)n);body+=(uint32_t)n;
    }
    if(response){response->status_code=status;response->content_length=length;response->body_length=body;kstrcpy(response->host,host);kstrcpy(response->path,path);}rc=0;
done:
    if(conn){(void)atm_tcp_close(conn);kfree(conn);}if(request)kfree(request);if(chunk)kfree(chunk);if(headers)kfree(headers);return rc;
}

int atm_http_selftest(void){
    static const uint8_t good[]="HTTP/1.0 200 OK\r\nContent-Length: 42\r\nConnection: close\r\n\r\n";
    static const uint8_t chunked[]="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    static const uint8_t missing[]="HTTP/1.0 200 OK\r\nConnection: close\r\n\r\n";
    uint32_t end=0,status=0,length=0;char host[ATM_HTTP_HOST_MAX+1],path[ATM_HTTP_PATH_MAX+1];uint16_t port=0;
    if(parse_url("http://127.0.0.1:8080/repo/a.atpk",host,path,&port)<0||kstrcmp(host,"127.0.0.1")||kstrcmp(path,"/repo/a.atpk")||port!=8080)return -1;
    if(parse_url("https://example.test/a",host,path,&port)==0||parse_url("http://x/has space",host,path,&port)==0)return -1;
    if(find_header_end(good,sizeof(good)-1,&end)<0||http_parse_headers(good,end,&status,&length)<0||status!=200||length!=42)return -1;
    if(find_header_end(chunked,sizeof(chunked)-1,&end)<0||http_parse_headers(chunked,end,&status,&length)==0)return -1;
    if(find_header_end(missing,sizeof(missing)-1,&end)<0||http_parse_headers(missing,end,&status,&length)==0)return -1;
    return 0;
}
