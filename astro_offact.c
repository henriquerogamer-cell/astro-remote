#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "astro_offact.h"

int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);
int sceRegMgrGetInt(int, int *);
int sceRegMgrGetStr(int, char *, size_t);
int sceRegMgrGetBin(int, void *, size_t);
int sceRegMgrSetInt(int, int);
int sceRegMgrSetStr(int, const char *, size_t);
int sceRegMgrSetBin(int, const void *, size_t);

static int entity(int n,int max,int stride,int base,int fallback)
{
    if(n<1||n>max)return fallback;
    return (n-1)*stride+base;
}

static int key_user_id(int n){return entity(n,16,65536,125829376,127140096);}
static int key_name(int n){return entity(n,16,65536,125829632,127140352);}
static int key_account_id(int n){return entity(n,16,65536,125830400,127141120);}
static int key_flags(int n){return entity(n,16,65536,125831168,127141888);}
static int key_type(int n){return entity(n,16,65536,125874183,127184903);}

static uint64_t gen_account_id(const char *name)
{
    uint64_t base=0x5EAF00D/0xCA7F00D;
    if(name&&*name){
        do{base=0x100000001B3ULL*(base^(unsigned char)*name++);}while(*name);
    }
    return base;
}

static int find_foreground_slot(int *user_out,int *slot_out)
{
    int user=0,rc;
    sceUserServiceInitialize(NULL);
    rc=sceUserServiceGetForegroundUser(&user);
    if(rc!=0)return -10;
    for(int i=1;i<=16;i++){
        int uid=0;
        rc=sceRegMgrGetInt(key_user_id(i),&uid);
        if(rc==0&&uid==user){
            if(user_out)*user_out=user;
            if(slot_out)*slot_out=i;
            return 0;
        }
    }
    return -11;
}

int astro_account_get_current(astro_account_state_t *out)
{
    astro_account_state_t s;
    int rc,slot=0,user=0;
    memset(&s,0,sizeof(s));
    s.rc=-1;

    rc=find_foreground_slot(&user,&slot);
    if(rc!=0){s.rc=rc;if(out)*out=s;return rc;}
    s.foreground_user=user;
    s.registry_index=slot;

    rc=sceRegMgrGetStr(key_name(slot),s.account_name,sizeof(s.account_name));
    if(rc!=0){s.rc=-12;if(out)*out=s;return s.rc;}
    rc=sceRegMgrGetBin(key_account_id(slot),&s.account_id,sizeof(s.account_id));
    if(rc!=0){s.rc=-13;if(out)*out=s;return s.rc;}
    rc=sceRegMgrGetStr(key_type(slot),s.account_type,sizeof(s.account_type));
    if(rc!=0){s.rc=-14;if(out)*out=s;return s.rc;}
    rc=sceRegMgrGetInt(key_flags(slot),&s.account_flags);
    if(rc!=0){s.rc=-15;if(out)*out=s;return s.rc;}

    s.proposed_account_id=s.account_id?s.account_id:gen_account_id(s.account_name);
    s.activated=(s.account_id!=0 && strcmp(s.account_type,"np")==0 && (s.account_flags&4098)==4098);
    s.rc=0;
    if(out)*out=s;
    return 0;
}

int astro_account_fake_activate_current(astro_account_state_t *out)
{
    astro_account_state_t s;
    int rc=astro_account_get_current(&s);
    const char type[]="np";
    const int flags=4098;
    uint64_t id;
    if(rc!=0){if(out)*out=s;return rc;}

    /* Never select a slot supplied by the caller. We only mutate the slot
       resolved from the currently foreground user. */
    id=s.account_id?s.account_id:s.proposed_account_id;
    if(!id){s.rc=-20;if(out)*out=s;return s.rc;}

    rc=sceRegMgrSetBin(key_account_id(s.registry_index),&id,sizeof(id));
    if(rc!=0){s.rc=-21;if(out)*out=s;return s.rc;}
    rc=sceRegMgrSetStr(key_type(s.registry_index),type,sizeof(type));
    if(rc!=0){s.rc=-22;if(out)*out=s;return s.rc;}
    rc=sceRegMgrSetInt(key_flags(s.registry_index),flags);
    if(rc!=0){s.rc=-23;if(out)*out=s;return s.rc;}

    rc=astro_account_get_current(&s);
    if(rc==0 && s.account_id==id){
        s.activated=1;
        s.rc=0;
    }
    if(out)*out=s;
    return s.rc;
}
