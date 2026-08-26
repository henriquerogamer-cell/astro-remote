#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <sys/user.h>

#include <ps5/kernel.h>
#include <ps5/mdbg.h>

#include "remote_pairing.h"
#include "rp_ptrace.h"

int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);
int sceRegMgrGetInt(int, int *);
int sceRegMgrGetBin(int, void *, size_t);

#define PAIRING_TTL_SECONDS 120
#define PS5_KINFO_PID_OFFSET 72
#define PS5_KINFO_TDNAME_OFFSET 447

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

static int find_process(const char *name)
{
    int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};
    size_t sz=0;
    uint8_t *buf=NULL;
    int found=-1;

    if(sysctl(mib,4,NULL,&sz,NULL,0)!=0||sz==0)return -1;
    buf=malloc(sz);
    if(!buf)return -1;
    if(sysctl(mib,4,buf,&sz,NULL,0)!=0){free(buf);return -1;}

    for(uint8_t *p=buf;p<buf+sz;){
        int struct_size=*(int *)p;
        if(struct_size<=0||p+(size_t)struct_size>buf+sz)break;
        if(struct_size>PS5_KINFO_TDNAME_OFFSET){
            pid_t pid=*(pid_t *)(p+PS5_KINFO_PID_OFFSET);
            const char *tdname=(const char *)(p+PS5_KINFO_TDNAME_OFFSET);
            if(!strcmp(tdname,name)){found=(int)pid;break;}
        }
        p+=(size_t)struct_size;
    }
    free(buf);
    return found;
}

static uintptr_t resolve_symbol_for_pid(pid_t pid,const char *lib,const char *symbol)
{
    uint32_t handle=0;
    if(kernel_dynlib_handle(pid,lib,&handle)||!handle)return 0;
    return kernel_dynlib_dlsym(pid,handle,symbol);
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

static int write_remote_scratch(pid_t pid,astro_rp_tracer_t *tr,
    const void *data,size_t size,uintptr_t *addr_out,uint8_t *backup)
{
    uintptr_t addr=0;
    int rc=astro_rp_tracer_stack_scratch(tr,size,&addr);
    if(rc!=0)return -1;
    if(mdbg_copyout(pid,addr,backup,size)!=0)return -2;
    if(mdbg_copyin(pid,data,addr,size)!=0)return -3;
    *addr_out=addr;
    return 0;
}

static void restore_remote_scratch(pid_t pid,uintptr_t addr,
    const uint8_t *backup,size_t size)
{
    if(addr&&backup)mdbg_copyin(pid,backup,addr,size);
}

/* Load libSceRemoteplay inside SceShellUI. We deliberately use temporary
   stack scratch rather than calloc/free so this path does not depend on
   the firmware-specific name/export layout of ShellUI's libc. */
static int ensure_remoteplay_in_shell(pid_t shell_pid,astro_rp_tracer_t *tr)
{
    static const char *paths[]={
        "/system/common/lib/libSceRemoteplay.sprx",
        "/system_ex/common/lib/libSceRemoteplay.sprx",
        "/system/priv/lib/libSceRemoteplay.sprx"
    };
    uintptr_t load_addr;

    if(resolve_symbol_for_pid(shell_pid,"libSceRemoteplay.sprx","sceRemoteplayGeneratePinCode"))
        return 0;

    load_addr=resolve_symbol_for_pid(shell_pid,"libkernel.sprx","sceKernelLoadStartModule");
    if(!load_addr)
        load_addr=resolve_symbol_for_pid(shell_pid,"libkernel_sys.sprx","sceKernelLoadStartModule");
    if(!load_addr)return -25;

    for(size_t i=0;i<sizeof(paths)/sizeof(paths[0]);i++){
        size_t len=strlen(paths[i])+1;
        uintptr_t remote_path=0;
        uint8_t backup[128];
        if(len>sizeof(backup))continue;
        memset(backup,0,sizeof(backup));
        if(write_remote_scratch(shell_pid,tr,paths[i],len,&remote_path,backup)!=0)
            continue;

        (void)astro_rp_tracer_call(tr,load_addr,remote_path,0,0,0,0,0);
        restore_remote_scratch(shell_pid,remote_path,backup,len);

        if(resolve_symbol_for_pid(shell_pid,"libSceRemoteplay.sprx","sceRemoteplayGeneratePinCode"))
            return 0;
    }
    return -26;
}

int astro_remote_pairing_prepare(astro_remote_pairing_state_t *out)
{
    astro_remote_pairing_state_t s;
    uint8_t account_id[8];
    int shell_pid;
    uintptr_t gen_pin_addr,notify_pin_addr;
    astro_rp_tracer_t tr;
    uintptr_t mem=0;
    uintptr_t call_rc;
    uint32_t pin=0;
    uint32_t zero=0;
    uint8_t scratch_backup[sizeof(uint32_t)]={0};
    int scratch_saved=0;
    int attached=0;
    int rc;

    memset(&s,0,sizeof(s));
    s.rc=-1;

    rc=current_user_and_account(&s.foreground_user,&s.registry_index,account_id);
    if(rc!=0){s.rc=rc;if(out)*out=s;return rc;}
    base64_8(account_id,s.account_id_b64);

    shell_pid=find_process("SceShellUI");
    if(shell_pid<=0){s.rc=-20;if(out)*out=s;return s.rc;}

    rc=astro_rp_tracer_init(&tr,shell_pid);
    if(rc!=0){s.rc=-30+rc;if(out)*out=s;return s.rc;}
    attached=1;

    rc=ensure_remoteplay_in_shell(shell_pid,&tr);
    if(rc!=0){s.rc=rc;goto cleanup;}

    gen_pin_addr=resolve_symbol_for_pid(shell_pid,"libSceRemoteplay.sprx","sceRemoteplayGeneratePinCode");
    notify_pin_addr=resolve_symbol_for_pid(shell_pid,"libSceRemoteplay.sprx","sceRemoteplayNotifyPinCodeError");
    if(!gen_pin_addr){s.rc=-21;goto cleanup;}
    if(!notify_pin_addr){s.rc=-22;goto cleanup;}

    rc=write_remote_scratch(shell_pid,&tr,&zero,sizeof(zero),&mem,scratch_backup);
    if(rc!=0){s.rc=-40;goto cleanup;}
    scratch_saved=1;

    (void)astro_rp_tracer_call(&tr,notify_pin_addr,1,0,0,0,0,0);
    call_rc=astro_rp_tracer_call(&tr,gen_pin_addr,mem,0,0,0,0,0);
    if(call_rc==(uintptr_t)-1||call_rc!=0){s.rc=-41;goto cleanup;}

    if(mdbg_copyout(shell_pid,mem,&pin,sizeof(pin))!=0){s.rc=-42;goto cleanup;}
    if(pin==0){s.rc=-43;goto cleanup;}

    s.pin=pin;
    s.pin_ready=1;
    s.generated_at=time(NULL);
    s.expires_at=s.generated_at+PAIRING_TTL_SECONDS;
    s.rc=0;

cleanup:
    if(scratch_saved)restore_remote_scratch(shell_pid,mem,scratch_backup,sizeof(scratch_backup));
    if(attached)astro_rp_tracer_finalize(&tr);
    if(out)*out=s;
    return s.rc;
}
