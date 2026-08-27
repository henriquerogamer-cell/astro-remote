#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ASTRO_MAIN_PROCESS "astrormt"
#define ASTRO_MAIN_PORT 45821

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

static int post_shutdown(void)
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
  a.sin_port=htons(ASTRO_MAIN_PORT);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(connect(fd,(struct sockaddr *)&a,sizeof(a))!=0){close(fd);return -2;}

  n=snprintf(req,sizeof(req),
    "POST /admin/shutdown HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nCookie: astro_session=logged_in\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
    ASTRO_MAIN_PORT);
  if(n<=0||n>=(int)sizeof(req)){close(fd);return -3;}
  if(send(fd,req,(size_t)n,0)<=0){close(fd);return -4;}
  n=(int)recv(fd,resp,sizeof(resp)-1,0);
  close(fd);
  if(n<=0)return -5;
  resp[n]='\0';
  return strstr(resp," 200 ")?0:-6;
}

int main(void)
{
  int rc=-99;
  int elapsed=0;
  char msg[220];

  if(process_exists(ASTRO_MAIN_PROCESS)<=0){
    notify("AstroStop: astrormt ja esta encerrado");
    return 0;
  }

  notify("AstroStop: encerrando somente astrormt");
  if(port_is_open(ASTRO_MAIN_PORT))rc=post_shutdown();

  while(elapsed<=4000){
    if(process_exists(ASTRO_MAIN_PROCESS)==0&&!port_is_open(ASTRO_MAIN_PORT)){
      notify("AstroStop: astrormt encerrado; astrorem permaneceu independente");
      return 0;
    }
    usleep(100000);
    elapsed+=100;
  }

  snprintf(msg,sizeof(msg),"AstroStop: astrormt ainda ativo rc=%d. Nenhum SIGKILL enviado.",rc);
  notify(msg);
  return 2;
}
