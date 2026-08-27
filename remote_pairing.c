#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "remote_pairing.h"

int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);
int sceRegMgrGetInt(int, int *);
int sceRegMgrGetBin(int, void *, size_t);
int sceRemoteplayInitialize(void *, size_t);
int sceRemoteplayGeneratePinCode(uint32_t *);
int sceRemoteplayConfirmDeviceRegist(int *, int *);
int sceRemoteplayNotifyPinCodeError(int);

#define PAIRING_TTL_SECONDS 300

static uint32_t reg_num(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e)
{
    if(a<1||a>b)return e;
    return (a-1)*c+d;
}

static uint32_t key_user_id(uint32_t i){return reg_num(i,16u,65536u,125829376u,127140096u);}
static uint32_t key_account_id(uint32_t i){return reg_num(i,16u,65536u,125830400u,127141120u);}

static void base64_8(const uint8_t in[8],char out[24])
{
    static const char t[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    unsigned int i=0,j=0;
    while(i<8){unsigned int rem=8-i;uint32_t a=in[i++];uint32_t b=rem>1?in[i++]:0;uint32_t c=rem>2?in[i++]:0;uint32_t v=(a<<16)|(b<<8)|c;out[j++]=t[(v>>18)&63];out[j++]=t[(v>>12)&63];out[j++]=rem>1?t[(v>>6)&63]:'=';out[j++]=rem>2?t[v&63]:'=';}out[j]='\0';
}

static int current_user_and_account(int *user_out,int *idx_out,uint8_t account_id[8])
{
    int user=0,rc,idx=-1;int nonzero=0;
    sceUserServiceInitialize(NULL);
    rc=sceUserServiceGetForegroundUser(&user);if(rc!=0)return -10;
    for(int i=1;i<=16;i++){int32_t uid=0;rc=sceRegMgrGetInt((int)key_user_id((uint32_t)i),(int *)&uid);if(rc==0&&uid==user){idx=i;break;}}
    if(idx<0)return -11;
    memset(account_id,0,8);rc=sceRegMgrGetBin((int)key_account_id((uint32_t)idx),account_id,8);if(rc!=0)return -12;
    for(int i=0;i<8;i++)if(account_id[i]){nonzero=1;break;}
    if(!nonzero)return -13;
    if(user_out)*user_out=user;if(idx_out)*idx_out=idx;return 0;
}

int astro_remote_pairing_prepare(astro_remote_pairing_state_t *out)
{
    astro_remote_pairing_state_t s;uint8_t account_id[8];uint32_t pin=0;int rc;
    memset(&s,0,sizeof(s));s.rc=-1;
    rc=current_user_and_account(&s.foreground_user,&s.registry_index,account_id);if(rc!=0){s.rc=rc;if(out)*out=s;return rc;}
    base64_8(account_id,s.account_id_b64);
    rc=sceRemoteplayInitialize(NULL,0);if(rc!=0){s.rc=-20;if(out)*out=s;return s.rc;}
    (void)sceRemoteplayNotifyPinCodeError(1);
    rc=sceRemoteplayGeneratePinCode(&pin);if(rc!=0||pin==0){s.rc=-21;if(out)*out=s;return s.rc;}
    s.pin=pin;s.pin_ready=1;s.pairing_active=1;s.generated_at=time(NULL);s.expires_at=s.generated_at+PAIRING_TTL_SECONDS;s.rc=0;if(out)*out=s;return 0;
}

int astro_remote_pairing_poll(astro_remote_pairing_state_t *state)
{
    int stat=0,err=0,rc;if(!state||!state->pairing_active)return -1;
    if(time(NULL)>state->expires_at){state->pairing_active=0;state->pin_ready=0;state->rc=-30;return -30;}
    rc=sceRemoteplayConfirmDeviceRegist(&stat,&err);if(rc!=0){state->rc=-31;return -31;}
    state->pair_stat=stat;state->pair_error=err;
    if(stat==2){state->pairing_complete=1;state->pairing_active=0;state->pin_ready=0;state->rc=0;return 1;}
    if(stat==3){state->pairing_active=0;state->pin_ready=0;state->rc=-32;return -32;}
    state->rc=0;return 0;
}

int astro_remote_pairing_cancel(astro_remote_pairing_state_t *state)
{
    if(!state)return -1;if(state->pairing_active)(void)sceRemoteplayNotifyPinCodeError(1);state->pairing_active=0;state->pin_ready=0;state->rc=0;return 0;
}
