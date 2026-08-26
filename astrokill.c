#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>

#define ASTRO_PROCESS_NAME "astrormt"

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *message)
{
  notify_request_t req;
  memset(&req,0,sizeof(req));
  snprintf(req.message,sizeof(req.message),"%s",message?message:"");
  sceKernelSendNotificationRequest(0,&req,sizeof(req),0);
}

static int kill_astro_processes(void)
{
  int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};
  size_t buf_size=0;
  void *buf=NULL;
  int killed=0;

  if(sysctl(mib,4,NULL,&buf_size,NULL,0)!=0||buf_size==0)
    return -1;

  buf=malloc(buf_size);
  if(!buf)
    return -2;

  if(sysctl(mib,4,buf,&buf_size,NULL,0)!=0){
    free(buf);
    return -3;
  }

  for(void *ptr=buf;ptr<(buf+buf_size);){
    struct kinfo_proc *ki=(struct kinfo_proc *)ptr;
    if(ki->ki_structsize==0)break;
    ptr+=ki->ki_structsize;

    if(ki->ki_pid<=1||ki->ki_pid==getpid())continue;
    if(strcmp(ki->ki_comm,ASTRO_PROCESS_NAME)!=0)continue;

    if(kill(ki->ki_pid,SIGKILL)==0)
      killed++;
  }

  free(buf);
  return killed;
}

static int astro_still_running(void)
{
  int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};
  size_t buf_size=0;
  void *buf=NULL;
  int found=0;

  if(sysctl(mib,4,NULL,&buf_size,NULL,0)!=0||buf_size==0)
    return -1;

  buf=malloc(buf_size);
  if(!buf)return -1;
  if(sysctl(mib,4,buf,&buf_size,NULL,0)!=0){free(buf);return -1;}

  for(void *ptr=buf;ptr<(buf+buf_size);){
    struct kinfo_proc *ki=(struct kinfo_proc *)ptr;
    if(ki->ki_structsize==0)break;
    ptr+=ki->ki_structsize;
    if(ki->ki_pid>1&&ki->ki_pid!=getpid()&&strcmp(ki->ki_comm,ASTRO_PROCESS_NAME)==0){
      found=1;
      break;
    }
  }

  free(buf);
  return found;
}

int main(void)
{
  char msg[160];
  int killed;
  int remaining;

  notify("AstroKill: procurando astrormt...");
  killed=kill_astro_processes();

  if(killed<0){
    snprintf(msg,sizeof(msg),"AstroKill: falha ao listar processos (%d)",killed);
    notify(msg);
    return 1;
  }

  if(killed==0){
    notify("AstroKill: nenhum astrormt encontrado");
    return 0;
  }

  usleep(500000);
  remaining=astro_still_running();

  if(remaining==0){
    snprintf(msg,sizeof(msg),"AstroKill: %d processo(s) Astro eliminado(s)",killed);
    notify(msg);
    return 0;
  }

  if(remaining>0){
    snprintf(msg,sizeof(msg),"AstroKill: SIGKILL enviado (%d), mas astrormt ainda existe",killed);
    notify(msg);
    return 2;
  }

  snprintf(msg,sizeof(msg),"AstroKill: SIGKILL enviado (%d), verificacao falhou",killed);
  notify(msg);
  return 3;
}
