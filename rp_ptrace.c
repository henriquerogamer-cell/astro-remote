/*
 * Minimal tracer adapted from idlesauce/ps5-remoteplay-get-pin and
 * ps5-payload-dev examples. It is used only inside the astrorem worker,
 * never inside the Astro supervisor process.
 */
#include <sys/types.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>

#include <ps5/kernel.h>
#include <ps5/mdbg.h>

#include "rp_ptrace.h"

#define LIBKERNEL_HANDLE 0x2001

typedef struct reg astro_reg_t;

static void set_args(astro_reg_t *regs, uintptr_t a, uintptr_t b,
    uintptr_t c, uintptr_t d, uintptr_t e, uintptr_t f)
{
    regs->r_rdi=(register_t)a;
    regs->r_rsi=(register_t)b;
    regs->r_rdx=(register_t)c;
    regs->r_rcx=(register_t)d;
    regs->r_r8=(register_t)e;
    regs->r_r9=(register_t)f;
}

int astro_rp_tracer_init(astro_rp_tracer_t *self,int pid)
{
    uint8_t privcaps[16]={
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff,
        0xff,0xff,0xff,0xff,0xff,0xff,0xff,0xff
    };
    pid_t mypid=getpid();
    uint64_t authid;
    uint8_t caps[16]={0};
    int status=0;

    if(!self||pid<=0||mypid==pid)return -1;
    memset(self,0,sizeof(*self));

    authid=kernel_get_ucred_authid(mypid);
    if(authid==0)return -2;
    if(kernel_get_ucred_caps(mypid,caps))return -3;
    if(kernel_set_ucred_authid(mypid,0x4800000000010003ULL))return -4;
    if(kernel_set_ucred_caps(mypid,privcaps)){
        kernel_set_ucred_authid(mypid,authid);
        return -5;
    }

    if((int)syscall(SYS_ptrace,PT_ATTACH,pid,0,0)<0){
        kernel_set_ucred_authid(mypid,authid);
        kernel_set_ucred_caps(mypid,caps);
        return -6;
    }
    if(waitpid(pid,&status,0)<0){
        syscall(SYS_ptrace,PT_DETACH,pid,0,0);
        kernel_set_ucred_authid(mypid,authid);
        kernel_set_ucred_caps(mypid,caps);
        return -7;
    }

    self->pid=pid;
    self->original_authid=authid;
    memcpy(self->original_caps,caps,sizeof(caps));
    return 0;
}

int astro_rp_tracer_finalize(astro_rp_tracer_t *self)
{
    int rc=0;
    if(!self||self->pid<=0)return -1;

    if((int)syscall(SYS_ptrace,PT_DETACH,self->pid,0,0)<0)rc=-2;
    kernel_set_ucred_authid(getpid(),self->original_authid);
    kernel_set_ucred_caps(getpid(),self->original_caps);
    self->pid=0;
    return rc;
}

int astro_rp_tracer_stack_scratch(astro_rp_tracer_t *self,size_t size,
    uintptr_t *addr_out)
{
    astro_reg_t regs;
    uintptr_t addr;
    size_t reserve;

    if(!self||self->pid<=0||!addr_out||size==0)return -1;
    if((int)syscall(SYS_ptrace,PT_GETREGS,self->pid,(caddr_t)&regs,0)<0)return -2;

    /* Keep a generous gap below the current stack pointer. tracer_call()
       temporarily consumes one return-address word, so this scratch area
       stays well clear of that slot. Align down to 16 bytes. */
    reserve=(size+0x100u+15u)&~15u;
    addr=((uintptr_t)regs.r_rsp-reserve)&~(uintptr_t)15u;
    if(addr==0)return -3;
    *addr_out=addr;
    return 0;
}

uintptr_t astro_rp_tracer_call(astro_rp_tracer_t *self,uintptr_t addr,
    uintptr_t a,uintptr_t b,uintptr_t c,uintptr_t d,uintptr_t e,uintptr_t f)
{
    astro_reg_t regs,backup;
    int state=0;

    if(!self||self->pid<=0||addr==0){errno=EINVAL;return (uintptr_t)-1;}
    if((int)syscall(SYS_ptrace,PT_GETREGS,self->pid,(caddr_t)&regs,0)<0)
        return (uintptr_t)-1;
    backup=regs;

    regs.r_rip=(register_t)addr;
    set_args(&regs,a,b,c,d,e,f);

    if(self->libkernel_base==0){
        self->libkernel_base=kernel_dynlib_mapbase_addr(self->pid,LIBKERNEL_HANDLE);
        if(self->libkernel_base==0)return (uintptr_t)-1;
    }

    regs.r_rsp=(register_t)(regs.r_rsp-sizeof(uintptr_t));
    if((int)syscall(SYS_ptrace,PT_SETREGS,self->pid,(caddr_t)&regs,0)<0)
        return (uintptr_t)-1;
    if(mdbg_copyin(self->pid,&self->libkernel_base,regs.r_rsp,sizeof(self->libkernel_base)))
        return (uintptr_t)-1;

    if((int)syscall(SYS_ptrace,PT_CONTINUE,self->pid,(caddr_t)1,0)<0)
        return (uintptr_t)-1;
    if(waitpid(self->pid,&state,0)<0)return (uintptr_t)-1;
    if(!WIFSTOPPED(state)||WSTOPSIG(state)!=SIGTRAP)return (uintptr_t)-1;

    if((int)syscall(SYS_ptrace,PT_GETREGS,self->pid,(caddr_t)&regs,0)<0)
        return (uintptr_t)-1;
    if((int)syscall(SYS_ptrace,PT_SETREGS,self->pid,(caddr_t)&backup,0)<0)
        return (uintptr_t)-1;

    return (uintptr_t)regs.r_rax;
}
