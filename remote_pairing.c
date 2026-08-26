#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>

#include "remote_pairing.h"

int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);
int sceRegMgrGetInt(int, int *);
int sceRegMgrGetBin(int, void *, size_t);

#define PAIRING_TTL_SECONDS 300

static uint32_t reg_num(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e)
{
    if(a<1||a>b)return e;
    return (a-1)*c+d;
}

static uint32_t key_user_id(uint32_t i)
{
    return reg_num(i,16u,65536u,125829376u,127140096u);
}

static uint32_t key_account_id(uint32_t i)
{
    return reg_num(i,16u,65536u,125830400u,127141120u);
}

static void base64_8(const uint8_t in[8],char out[24])
{
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int i=0,j=0;
    while(i<8){
        unsigned int rem=8-i;
        uint32_t a=in[i++];
        uint32_t b=rem>1?in[i++]:0;
        uint32_t c=rem>2?in[i++]:0;
        uint32_t v=(a<<16)|(b<<8)|c;
        out[j++]=t[(v>>18)&63];
        out[j++]=t[(v>>12)&63];
        out[j++]=rem>1?t[(v>>6)&63]:'=';
        out[j++]=rem>2?t[v&63]:'=';
    }
    out[j]='\0';
}

static int current_user_and_account(int *user_out,int *idx_out,uint8_t account_id[8])
{
    int user=0;
    int rc;
    int idx=-1;

    sceUserServiceInitialize(NULL);
    rc=sceUserServiceGetForegroundUser(&user);
    if(rc!=0)return -10;

    for(int i=1;i<=16;i++){
        int32_t uid=0;
        rc=sceRegMgrGetInt((int)key_user_id((uint32_t)i),(int *)&uid);
        if(rc==0&&uid==user){idx=i;break;}
    }
    if(idx<0)return -11;

    memset(account_id,0,8);
    rc=sceRegMgrGetBin((int)key_account_id((uint32_t)idx),account_id,8);
    if(rc!=0)return -12;

    if(user_out)*user_out=user;
    if(idx_out)*idx_out=idx;
    return 0;
}

int astro_remote_pairing_submit_pin(uint32_t pin,astro_remote_pairing_state_t *out)
{
    astro_remote_pairing_state_t s;
    uint8_t account_id[8];
    int rc;

    memset(&s,0,sizeof(s));
    s.rc=-1;

    /* PS5 Link Device codes are eight decimal digits. Keep leading zeroes
       valid by accepting the parsed numeric range 0..99999999; the UI
       performs the exact 8-character validation before sending it here. */
    if(pin>99999999u){s.rc=-2;if(out)*out=s;return s.rc;}

    rc=current_user_and_account(&s.foreground_user,&s.registry_index,account_id);
    if(rc!=0){s.rc=rc;if(out)*out=s;return rc;}

    base64_8(account_id,s.account_id_b64);
    s.pin=pin;
    s.pin_ready=1;
    s.generated_at=time(NULL);
    s.expires_at=s.generated_at+PAIRING_TTL_SECONDS;
    s.rc=0;

    if(out)*out=s;
    return 0;
}
