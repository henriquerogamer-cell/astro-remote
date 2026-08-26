#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define ASTRO_PROCESS_NAME "astrormt"
#define ASTRO_PORT 45821

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

typedef struct astro_proc_info {
  pid_t pid;
  pid_t ppid;
  int stat;
} astro_proc_info_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *message)
{
  notify_request_t req;
  memset(&req,0,sizeof(req));
  snprintf(req.message,sizeof(req.message),"%s",message?message:"");
  sceKernelSendNotificationRequest(0,&req,sizeof(req),0);
}

static int find_astro(astro_proc_info_t *out)
{
  int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};
  size_t buf_size=0;
  void *buf=NULL;
  int found=0;

  if(out)memset(out,0,sizeof(*out));

  if(sysctl(mib,4,NULL,&buf_size,NULL,0)!=0||buf_size==0)
    return -1;

  buf=malloc(buf_size);
  if(!buf)return -2;

  if(sysctl(mib,4,buf,&buf_size,NULL,0)!=0){
    free(buf);
    return -3;
  }

  for(char *ptr=(char *)buf;ptr<(char *)buf+buf_size;){
    struct kinfo_proc *ki=(struct kinfo_proc *)ptr;
    if(ki->ki_structsize==0)break;
    ptr+=ki->ki_structsize;

    if(ki->ki_pid<=1||ki->ki_pid==getpid())continue;
    if(strcmp(ki->ki_comm,ASTRO_PROCESS_NAME)!=0)continue;

    if(out){
      out->pid=ki->ki_pid;
      out->ppid=ki->ki_ppid;
      out->stat=(int)ki->ki_stat;
    }
    found=1;
    break;
  }

  free(buf);
  return found;
}

static int astro_port_is_free(void)
{
  int fd;
  int opt=1;
  int rc;
  struct sockaddr_in addr;

  fd=socket(AF_INET,SOCK_STREAM,0);
  if(fd<0)return -1;

  setsockopt(fd,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

  memset(&addr,0,sizeof(addr));
  addr.sin_family=AF_INET;
  addr.sin_addr.s_addr=htonl(INADDR_ANY);
  addr.sin_port=htons(ASTRO_PORT);

  rc=bind(fd,(struct sockaddr *)&addr,sizeof(addr));
  close(fd);

  return rc==0?1:0;
}

int main(void)
{
  astro_proc_info_t before;
  astro_proc_info_t after;
  char msg[220];
  int found;
  int cont_rc;
  int kill_rc;
  int kill_errno=0;
  int remaining;
  int port_free;

  notify("AstroKill v2: diagnosticando astrormt...");

  found=find_astro(&before);
  if(found<0){
    snprintf(msg,sizeof(msg),"AstroKill v2: falha ao listar processos (%d)",found);
    notify(msg);
    return 1;
  }

  if(found==0){
    port_free=astro_port_is_free();
    snprintf(msg,sizeof(msg),"AstroKill v2: astrormt ausente, porta 45821 %s",
             port_free==1?"LIVRE":(port_free==0?"OCUPADA":"DESCONHECIDA"));
    notify(msg);
    return 0;
  }

  snprintf(msg,sizeof(msg),"AstroKill v2: PID %d PPID %d stat %d",
           (int)before.pid,(int)before.ppid,before.stat);
  notify(msg);

  /* If Prospero left the process stopped, resume it first. A pending
     SIGKILL should then be able to complete instead of leaving a corpse. */
  cont_rc=kill(before.pid,SIGCONT);
  usleep(150000);

  kill_rc=kill(before.pid,SIGKILL);
  if(kill_rc!=0)kill_errno=errno;

  usleep(800000);
  remaining=find_astro(&after);
  port_free=astro_port_is_free();

  if(remaining==0){
    snprintf(msg,sizeof(msg),"AstroKill v2: PID %d eliminado, 45821 %s",
             (int)before.pid,port_free==1?"LIVRE":(port_free==0?"OCUPADA":"?"));
    notify(msg);
    return 0;
  }

  if(remaining>0){
    snprintf(msg,sizeof(msg),
             "AstroKill v2: ainda existe PID %d PPID %d stat %d | CONT=%d KILL=%d errno=%d | 45821 %s",
             (int)after.pid,(int)after.ppid,after.stat,cont_rc,kill_rc,kill_errno,
             port_free==1?"LIVRE":(port_free==0?"OCUPADA":"?"));
    notify(msg);
    return 2;
  }

  snprintf(msg,sizeof(msg),"AstroKill v2: SIGKILL enviado; verificacao falhou | 45821 %s",
           port_free==1?"LIVRE":(port_free==0?"OCUPADA":"?"));
  notify(msg);
  return 3;
}
