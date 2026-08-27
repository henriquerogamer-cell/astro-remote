#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ASTRO_MAIN_PROCESS "astrormt"
#define ASTRO_WORKER_PROCESS "astrorem"
#define ASTRO_MAIN_PORT 45821
#define ASTRO_WORKER_PORT 45822

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

static int process_exists(const char *name)
{
  int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};
  size_t sz=0;
  void *buf=NULL;
  int found=0;

  if(sysctl(mib,4,NULL,&sz,NULL,0)!=0||sz==0)return -1;
  buf=malloc(sz);
  if(!buf)return -2;
  if(sysctl(mib,4,buf,&sz,NULL,0)!=0){free(buf);return -3;}

  for(char *p=(char *)buf;p<(char *)buf+sz;){
    struct kinfo_proc *ki=(struct kinfo_proc *)p;
    if(ki->ki_structsize==0)break;
    p+=ki->ki_structsize;
    if(ki->ki_pid<=1||ki->ki_pid==getpid())continue;
    if(strcmp(ki->ki_comm,name)==0){found=1;break;}
  }

  free(buf);
  return found;
}

static int port_is_open(int port)
{
  int fd,rc;
  struct sockaddr_in a;
  struct timeval tv;

  fd=socket(AF_INET,SOCK_STREAM,0);
  if(fd<0)return 0;
  tv.tv_sec=0;tv.tv_usec=300000;
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_port=htons((uint16_t)port);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  rc=connect(fd,(struct sockaddr *)&a,sizeof(a));
  close(fd);
  return rc==0;
}

static int post_local(int port,const char *path,int auth)
{
  int fd,n;
  char req[768],resp[1024];
  struct sockaddr_in a;
  struct timeval tv;

  fd=socket(AF_INET,SOCK_STREAM,0);
  if(fd<0)return -1;
  tv.tv_sec=2;tv.tv_usec=0;
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_port=htons((uint16_t)port);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(connect(fd,(struct sockaddr *)&a,sizeof(a))!=0){close(fd);return -2;}

  n=snprintf(req,sizeof(req),
    "POST %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\n%sContent-Length: 0\r\nConnection: close\r\n\r\n",
    path,port,auth?"Cookie: astro_session=logged_in\r\n":"");
  if(n<=0||n>=(int)sizeof(req)){close(fd);return -3;}
  if(send(fd,req,(size_t)n,0)<=0){close(fd);return -4;}
  n=(int)recv(fd,resp,sizeof(resp)-1,0);
  close(fd);
  if(n<=0)return -5;
  resp[n]='\0';
  if(strstr(resp," 200 "))return 0;
  if(strstr(resp," 409 "))return 1;
  return -6;
}

static int wait_gone(const char *name,int port,int timeout_ms)
{
  int elapsed=0;
  while(elapsed<=timeout_ms){
    int pe=process_exists(name);
    if(pe==0&&!port_is_open(port))return 1;
    usleep(100000);
    elapsed+=100;
  }
  return 0;
}

int main(void)
{
  char msg[256];
  int main_before=process_exists(ASTRO_MAIN_PROCESS);
  int main_rc=-99,worker_rc=-99;
  int worker_gone=0,main_gone=0;

  notify("AstroStop: encerramento cooperativo iniciado");

  if(main_before>0&&port_is_open(ASTRO_MAIN_PORT))
    main_rc=post_local(ASTRO_MAIN_PORT,"/admin/shutdown",1);

  worker_gone=wait_gone(ASTRO_WORKER_PROCESS,ASTRO_WORKER_PORT,3500);

  if(!worker_gone&&port_is_open(ASTRO_WORKER_PORT)){
    worker_rc=post_local(ASTRO_WORKER_PORT,"/shutdown",0);
    worker_gone=wait_gone(ASTRO_WORKER_PROCESS,ASTRO_WORKER_PORT,3500);
  }

  if(worker_gone&&process_exists(ASTRO_MAIN_PROCESS)>0&&port_is_open(ASTRO_MAIN_PORT)){
    main_rc=post_local(ASTRO_MAIN_PORT,"/admin/shutdown",1);
  }

  main_gone=wait_gone(ASTRO_MAIN_PROCESS,ASTRO_MAIN_PORT,3500);

  if(worker_gone&&main_gone){
    notify("AstroStop: astrorem e astrormt encerrados com seguranca");
    return 0;
  }

  snprintf(msg,sizeof(msg),
    "AstroStop: pendente main=%d worker=%d main_rc=%d worker_rc=%d. Nenhum SIGKILL enviado.",
    !main_gone,!worker_gone,main_rc,worker_rc);
  notify(msg);
  return 2;
}
