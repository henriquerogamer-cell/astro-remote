#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include "astro_offact.h"

int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);
int sceRegMgrGetInt(int, int *);
int sceRegMgrGetStr(int, char *, size_t);
int sceRegMgrGetBin(int, void *, size_t);
int sceRegMgrSetInt(int, int);
int sceRegMgrSetStr(int, const char *, size_t);
int sceRegMgrSetBin(int, const void *, size_t);

static int g_debug_fd=-1;
static int g_user_service_init_attempted=0;
static int g_user_service_init_rc=0;

void astro_account_set_debug_fd(int fd){g_debug_fd=fd;}

static void dbg(const char *msg)
{
    if(g_debug_fd<0||!msg)return;
    write(g_debug_fd,msg,strlen(msg));
    write(g_debug_fd,"\n",1);
}

static void dbg_int(const char *label,int v)
{
    char b[128];
    snprintf(b,sizeof(b),"%s%d",label,v);
    dbg(b);
}

static void ensure_user_service(void)
{
    if(g_user_service_init_attempted)return;
    g_user_service_init_attempted=1;
    int prio=256;
    dbg("[account] -> sceUserServiceInitialize(prio=256)");
    g_user_service_init_rc=sceUserServiceInitialize(&prio);
    dbg_int("[account] <- UserService init rc=",g_user_service_init_rc);
}

/* UserService behaves reliably in a freshly forked payload process on the
   tested PS5, while lazy initialization from the long-running web worker can
   stall. Keep that API isolated and return only rc + foreground UID. */
typedef struct foreground_result {
    int rc;
    int user;
    int init_rc;
} foreground_result_t;

static int get_foreground_user_isolated(int *user_out)
{
    int p[2],status=0,elapsed=0;
    pid_t pid;
    foreground_result_t r;
    ssize_t got=0;

    memset(&r,0,sizeof(r));
    r.rc=-1;
    if(pipe(p)!=0)return -100;
    pid=fork();
    if(pid<0){close(p[0]);close(p[1]);return -101;}

    if(pid==0){
        foreground_result_t cr;
        memset(&cr,0,sizeof(cr));
        close(p[0]);
        /* Reset inherited bookkeeping so this child always initializes its
           own UserService context. */
        g_user_service_init_attempted=0;
        g_user_service_init_rc=0;
        ensure_user_service();
        cr.init_rc=g_user_service_init_rc;
        cr.rc=sceUserServiceGetForegroundUser(&cr.user);
        write(p[1],&cr,sizeof(cr));
        close(p[1]);
        _exit(0);
    }

    close(p[1]);
    while(elapsed<2000){
        pid_t w=waitpid(pid,&status,WNOHANG);
        if(w==pid)break;
        if(w<0&&errno!=EINTR){close(p[0]);return -102;}
        usleep(20000);
        elapsed+=20;
    }
    if(elapsed>=2000){
        kill(pid,SIGKILL);
        waitpid(pid,&status,0);
        close(p[0]);
        dbg("[account] isolated foreground lookup TIMEOUT");
        return -103;
    }

    got=read(p[0],&r,sizeof(r));
    close(p[0]);
    if(got!=(ssize_t)sizeof(r))return -104;
    dbg_int("[account] isolated UserService init rc=",r.init_rc);
    dbg_int("[account] isolated foreground rc=",r.rc);
    if(r.rc!=0)return r.rc;
    if(user_out)*user_out=r.user;
    return 0;
}

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
    dbg("[account] start find_foreground_slot");
    dbg("[account] -> isolated foreground user lookup");
    rc=get_foreground_user_isolated(&user);
    dbg_int("[account] <- isolated lookup rc=",rc);
    if(rc!=0)return -10;
    dbg_int("[account] foreground user=",user);

    for(int i=1;i<=16;i++){
        int uid=0;
        char b[128];
        snprintf(b,sizeof(b),"[account] -> slot %d user_id key=%d",i,key_user_id(i));
        dbg(b);
        rc=sceRegMgrGetInt(key_user_id(i),&uid);
        snprintf(b,sizeof(b),"[account] <- slot %d rc=%d uid=%d",i,rc,uid);
        dbg(b);
        if(rc==0&&uid==user){
            dbg_int("[account] MATCH slot=",i);
            if(user_out)*user_out=user;
            if(slot_out)*slot_out=i;
            return 0;
        }
    }
    dbg("[account] foreground user not found in 16 slots");
    return -11;
}

static void terminate_text(char *s,size_t n)
{
    if(s&&n)s[n-1]='\0';
}

int astro_account_get_current(astro_account_state_t *out)
{
    astro_account_state_t s;
    int rc,slot=0,user=0;
    char b[160];
    memset(&s,0,sizeof(s));
    s.rc=-1;
    dbg("[account] astro_account_get_current BEGIN");

    rc=find_foreground_slot(&user,&slot);
    snprintf(b,sizeof(b),"[account] find_foreground_slot rc=%d user=%d slot=%d",rc,user,slot);dbg(b);
    if(rc!=0){s.rc=rc;if(out)*out=s;return rc;}
    s.foreground_user=user;
    s.registry_index=slot;

    snprintf(b,sizeof(b),"[account] -> sceRegMgrGetStr(name) key=%d",key_name(slot));dbg(b);
    rc=sceRegMgrGetStr(key_name(slot),s.account_name,sizeof(s.account_name));
    terminate_text(s.account_name,sizeof(s.account_name));
    snprintf(b,sizeof(b),"[account] <- name rc=%d value='%s'",rc,s.account_name);dbg(b);
    if(rc!=0){s.rc=-12;if(out)*out=s;return s.rc;}

    snprintf(b,sizeof(b),"[account] -> sceRegMgrGetBin(account_id) key=%d",key_account_id(slot));dbg(b);
    rc=sceRegMgrGetBin(key_account_id(slot),&s.account_id,sizeof(s.account_id));
    snprintf(b,sizeof(b),"[account] <- account_id rc=%d value=0x%016llx",rc,(unsigned long long)s.account_id);dbg(b);
    if(rc!=0){s.rc=-13;if(out)*out=s;return s.rc;}

    snprintf(b,sizeof(b),"[account] -> sceRegMgrGetStr(type) key=%d",key_type(slot));dbg(b);
    rc=sceRegMgrGetStr(key_type(slot),s.account_type,sizeof(s.account_type));
    terminate_text(s.account_type,sizeof(s.account_type));
    snprintf(b,sizeof(b),"[account] <- type rc=%d value='%s'",rc,s.account_type);dbg(b);
    if(rc!=0)s.account_type[0]='\0';

    snprintf(b,sizeof(b),"[account] -> sceRegMgrGetInt(flags) key=%d",key_flags(slot));dbg(b);
    rc=sceRegMgrGetInt(key_flags(slot),&s.account_flags);
    snprintf(b,sizeof(b),"[account] <- flags rc=%d value=%d",rc,s.account_flags);dbg(b);
    if(rc!=0)s.account_flags=0;

    s.proposed_account_id=s.account_id?s.account_id:gen_account_id(s.account_name);
    s.activated=(s.account_id!=0 && strcmp(s.account_type,"np")==0 && (s.account_flags&4098)==4098);
    s.rc=0;
    snprintf(b,sizeof(b),"[account] DONE activated=%d proposed=0x%016llx",s.activated,(unsigned long long)s.proposed_account_id);dbg(b);
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
    id=s.account_id?s.account_id:s.proposed_account_id;
    if(!id){s.rc=-20;if(out)*out=s;return s.rc;}

    rc=sceRegMgrSetBin(key_account_id(s.registry_index),&id,sizeof(id));
    if(rc!=0){s.rc=-21;if(out)*out=s;return s.rc;}
    rc=sceRegMgrSetStr(key_type(s.registry_index),type,sizeof(type));
    if(rc!=0){s.rc=-22;if(out)*out=s;return s.rc;}
    rc=sceRegMgrSetInt(key_flags(s.registry_index),flags);
    if(rc!=0){s.rc=-23;if(out)*out=s;return s.rc;}

    rc=astro_account_get_current(&s);
    if(rc==0 && s.account_id==id){s.activated=1;s.rc=0;}
    if(out)*out=s;
    return s.rc;
}
