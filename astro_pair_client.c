/*
 * Astro Remote local PS5 registration client.
 * Protocol/cryptography behavior follows chiaki-ng registration research.
 * chiaki-ng portions are AGPL-3.0-only with OpenSSL exception; see
 * THIRD_PARTY_PAIRING.md. The upstream rpcrypt.c is fetched at build time.
 */
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <chiaki/rpcrypt.h>
#include "astro_pair_client.h"

#define RP_PORT 9295
#define RP_TARGET CHIAKI_TARGET_PS5_1
#define RP_INNER_OFF 0x1e0
#define RP_BUF_SIZE 8192
#define RP_CLIENT_TYPE "dabfa2ec873de5839bee8d3f4c0239c4282c07c25c6077a2931afcf0adc0d34f"

static int send_all(int fd,const void *data,size_t len)
{
    const unsigned char *p=(const unsigned char *)data;
    while(len){ssize_t n=send(fd,p,len,0);if(n>0){p+=n;len-=(size_t)n;continue;}if(n<0&&errno==EINTR)continue;return -1;}return 0;
}

static int random_bytes(unsigned char *out,size_t len)
{
    int fd=open("/dev/urandom",O_RDONLY);
    size_t used=0;
    if(fd>=0){while(used<len){ssize_t n=read(fd,out+used,len-used);if(n>0){used+=(size_t)n;continue;}if(n<0&&errno==EINTR)continue;break;}close(fd);if(used==len)return 0;}
    {
        uint64_t x=((uint64_t)time(NULL)<<32)^(uint64_t)getpid()^(uintptr_t)out;
        size_t i;
        for(i=0;i<len;i++){x^=x<<13;x^=x>>7;x^=x<<17;out[i]=(unsigned char)x;}
    }
    return 0;
}

static int base64_8(const unsigned char in[8],char out[16])
{
    static const char tab[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i=0,j=0;
    while(i+2<8){uint32_t v=((uint32_t)in[i]<<16)|((uint32_t)in[i+1]<<8)|in[i+2];out[j++]=tab[(v>>18)&63];out[j++]=tab[(v>>12)&63];out[j++]=tab[(v>>6)&63];out[j++]=tab[v&63];i+=3;}
    if(i<8){uint32_t v=(uint32_t)in[i]<<16;out[j++]=tab[(v>>18)&63];if(i+1<8){v|=(uint32_t)in[i+1]<<8;out[j++]=tab[(v>>12)&63];out[j++]=tab[(v>>6)&63];out[j++]='=';}else{out[j++]=tab[(v>>12)&63];out[j++]='=';out[j++]='=';}}
    out[j]='\0';return 0;
}

static int parse_account_id(const char *s,uint64_t *id,unsigned char raw[8])
{
    char *end=NULL;unsigned long long v;
    if(!s||!s[0])return -1;
    errno=0;v=strtoull(s,&end,0);
    if(errno||!end||*end||v==0)return -2;
    *id=(uint64_t)v;memcpy(raw,id,8);return 0;
}

static char *trim(char *s)
{
    char *e;while(*s&&isspace((unsigned char)*s))s++;e=s+strlen(s);while(e>s&&isspace((unsigned char)e[-1]))*--e='\0';return s;
}

static int valid_hex(const char *s,size_t max_chars)
{
    size_t n=0;if(!s||!*s)return 0;while(s[n]){if(!isxdigit((unsigned char)s[n]))return 0;n++;}return n<=max_chars&&(n%2)==0;
}

static int header_value(const char *headers,const char *name,char *out,size_t out_size)
{
    size_t nl=strlen(name);const char *p=headers;
    while(p&&*p){const char *e=strstr(p,"\r\n");size_t l=e?(size_t)(e-p):strlen(p);if(l>nl+1&&!strncasecmp(p,name,nl)&&p[nl]==':'){size_t vlen=l-nl-1;const char *v=p+nl+1;while(vlen&&(*v==' '||*v=='\t')){v++;vlen--;}if(vlen>=out_size)vlen=out_size-1;memcpy(out,v,vlen);out[vlen]='\0';return 1;}if(!e)break;p=e+2;}return 0;
}

int astro_pair_account_save(const char *account_id)
{
    uint64_t id;unsigned char raw[8];FILE *f;
    if(parse_account_id(account_id,&id,raw)!=0)return -1;
    (void)raw;mkdir("/data/AstroRemote",0755);f=fopen(ASTRO_PAIR_ACCOUNT_FILE,"w");if(!f)return -2;fprintf(f,"0x%016llx\n",(unsigned long long)id);fclose(f);chmod(ASTRO_PAIR_ACCOUNT_FILE,0600);return 0;
}

int astro_pair_account_load(char *out,size_t out_size)
{
    FILE *f;if(!out||out_size<4)return -1;out[0]='\0';f=fopen(ASTRO_PAIR_ACCOUNT_FILE,"r");if(!f)return -2;if(!fgets(out,(int)out_size,f)){fclose(f);return -3;}fclose(f);out[strcspn(out,"\r\n")]='\0';return out[0]?0:-4;
}

int astro_pair_credentials_exist(void){return access(ASTRO_PAIR_CREDENTIAL_FILE,R_OK)==0;}

static int save_credentials(const astro_pair_client_result_t *r)
{
    FILE *f;mkdir("/data/AstroRemote",0755);f=fopen(ASTRO_PAIR_CREDENTIAL_FILE,"w");if(!f)return -1;
    fprintf(f,"version=1\naccount_id=%s\nps5_regist_key=%s\nrp_key_type=%u\nrp_key=%s\nps5_mac=%s\n",r->account_id,r->regist_key_hex,(unsigned int)r->rp_key_type,r->rp_key_hex,r->ps5_mac_hex);
    fclose(f);chmod(ASTRO_PAIR_CREDENTIAL_FILE,0600);return 0;
}

static int parse_decrypted_payload(char *payload,astro_pair_client_result_t *r)
{
    char *save=NULL,*line;int have_reg=0,have_key=0,have_mac=0;
    for(line=strtok_r(payload,"\r\n",&save);line;line=strtok_r(NULL,"\r\n",&save)){
        char *colon=strchr(line,':');char *key,*val;if(!colon)continue;*colon='\0';key=trim(line);val=trim(colon+1);
        if(!strcmp(key,"PS5-RegistKey")){if(!valid_hex(val,64))return -1;snprintf(r->regist_key_hex,sizeof(r->regist_key_hex),"%s",val);have_reg=1;}
        else if(!strcmp(key,"RP-KeyType")){r->rp_key_type=(uint32_t)strtoul(val,NULL,0);}
        else if(!strcmp(key,"RP-Key")){if(!valid_hex(val,64))return -2;snprintf(r->rp_key_hex,sizeof(r->rp_key_hex),"%s",val);have_key=1;}
        else if(!strcmp(key,"PS5-Mac")){if(!valid_hex(val,24))return -3;snprintf(r->ps5_mac_hex,sizeof(r->ps5_mac_hex),"%s",val);have_mac=1;}
    }
    return have_reg&&have_key&&have_mac?0:-4;
}

int astro_pair_client_register_local(const char *account_id,uint32_t pin,astro_pair_client_result_t *out)
{
    astro_pair_client_result_t r;ChiakiRPCrypt crypt;unsigned char ambassador[16],account_raw[8];uint64_t id=0;
    unsigned char payload[1024],aeropause[16],response[RP_BUF_SIZE];char account_b64[16],inner[256],header[512],header_copy[2048],cl_s[64],reason_s[64];
    size_t payload_size,inner_len,total=0,header_len=0,body_len=0;int sock=-1,status=0,rc;struct sockaddr_in a;struct timeval tv;
    memset(&r,0,sizeof(r));r.rc=-1;if(out)*out=r;
    if(parse_account_id(account_id,&id,account_raw)!=0){r.rc=-100;goto done;}
    snprintf(r.account_id,sizeof(r.account_id),"0x%016llx",(unsigned long long)id);
    if(!pin||pin>99999999U){r.rc=-101;goto done;}
    random_bytes(ambassador,sizeof(ambassador));
    memset(payload,'A',RP_INNER_OFF);
    rc=chiaki_rpcrypt_init_regist(&crypt,RP_TARGET,ambassador,payload[0x18d]&0x1f,pin);if(rc!=CHIAKI_ERR_SUCCESS){r.rc=-110-rc;goto done;}
    rc=chiaki_rpcrypt_aeropause(RP_TARGET,payload[0]>>3,aeropause,crypt.ambassador);if(rc!=CHIAKI_ERR_SUCCESS){r.rc=-130-rc;goto done;}
    memcpy(payload+0xc7,aeropause+8,8);memcpy(payload+0x191,aeropause,8);base64_8(account_raw,account_b64);
    snprintf(inner,sizeof(inner),"Client-Type: %s\r\nNp-AccountId: %s\r\n",RP_CLIENT_TYPE,account_b64);inner_len=strlen(inner);
    if(RP_INNER_OFF+inner_len>sizeof(payload)){r.rc=-150;goto done;}memcpy(payload+RP_INNER_OFF,inner,inner_len);
    rc=chiaki_rpcrypt_encrypt(&crypt,0,payload+RP_INNER_OFF,payload+RP_INNER_OFF,inner_len);if(rc!=CHIAKI_ERR_SUCCESS){r.rc=-160-rc;goto done;}payload_size=RP_INNER_OFF+inner_len;
    snprintf(header,sizeof(header),"POST /sie/ps5/rp/sess/rgst HTTP/1.1\r\n HTTP/1.1\r\nHOST: 127.0.0.1\r\nUser-Agent: remoteplay Windows\r\nConnection: close\r\nContent-Length: %zu\r\nRP-Version: 1.0\r\n\r\n",payload_size);
    sock=socket(AF_INET,SOCK_STREAM,0);if(sock<0){r.rc=-170;goto done;}tv.tv_sec=12;tv.tv_usec=0;setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(sock,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
    memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(RP_PORT);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(connect(sock,(struct sockaddr *)&a,sizeof(a))<0){r.rc=-171;goto done;}
    if(send_all(sock,header,strlen(header))<0||send_all(sock,payload,payload_size)<0){r.rc=-172;goto done;}
    while(total<sizeof(response)){ssize_t n=recv(sock,response+total,sizeof(response)-total,0);if(n>0){total+=(size_t)n;continue;}if(n<0&&errno==EINTR)continue;if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK)){r.rc=-173;goto done;}break;}
    if(total<12){r.rc=-174;goto done;}
    {
        size_t i;for(i=0;i+3<total;i++)if(response[i]=='\r'&&response[i+1]=='\n'&&response[i+2]=='\r'&&response[i+3]=='\n'){header_len=i+4;break;}
    }
    if(!header_len||header_len>=sizeof(header_copy)){r.rc=-175;goto done;}memcpy(header_copy,response,header_len);header_copy[header_len]='\0';
    if(sscanf(header_copy,"HTTP/%*s %d",&status)!=1)status=0;r.http_status=status;
    if(header_value(header_copy,"RP-Application-Reason",reason_s,sizeof(reason_s)))r.application_reason=(uint32_t)strtoul(reason_s,NULL,16);
    if(status!=200){r.rc=-176;goto done;}
    if(!header_value(header_copy,"Content-Length",cl_s,sizeof(cl_s))){r.rc=-177;goto done;}body_len=(size_t)strtoull(cl_s,NULL,10);
    if(body_len==0||header_len+body_len>total||body_len>=RP_BUF_SIZE-header_len){r.rc=-178;goto done;}
    rc=chiaki_rpcrypt_decrypt(&crypt,0,response+header_len,response+header_len,body_len);if(rc!=CHIAKI_ERR_SUCCESS){r.rc=-180-rc;goto done;}response[header_len+body_len]='\0';
    rc=parse_decrypted_payload((char *)(response+header_len),&r);if(rc!=0){r.rc=-190+rc;goto done;}
    if(save_credentials(&r)!=0){r.rc=-200;goto done;}astro_pair_account_save(r.account_id);r.rc=0;
done:
    if(sock>=0)close(sock);if(out)*out=r;return r.rc;
}
