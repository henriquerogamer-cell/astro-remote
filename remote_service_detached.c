#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "remote_service.h"
#include "remote_worker.h"

#define DETACHED_CONNECT_TIMEOUT_MS 250
#define DETACHED_IO_TIMEOUT_MS 900

static astro_remote_state_t g_remote;

static void set_phase(const char *phase)
{
  snprintf(g_remote.phase,sizeof(g_remote.phase),"%s",phase?phase:"unknown");
}

static void set_socket_timeout(int fd,int timeout_ms)
{
  struct timeval tv;
  tv.tv_sec=timeout_ms/1000;
  tv.tv_usec=(timeout_ms%1000)*1000;
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
}

static int connect_worker(void)
{
  int fd,flags,rc,soerr=0;
  socklen_t slen=sizeof(soerr);
  fd_set wfds;
  struct timeval tv;
  struct sockaddr_in a;

  fd=socket(AF_INET,SOCK_STREAM,0);
  if(fd<0)return -1;
  flags=fcntl(fd,F_GETFL,0);
  if(flags<0){close(fd);return -1;}
  if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);return -1;}

  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_port=htons(ASTRO_REMOTE_WORKER_PORT);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);

  rc=connect(fd,(struct sockaddr *)&a,sizeof(a));
  if(rc!=0){
    if(errno!=EINPROGRESS){close(fd);return -1;}
    FD_ZERO(&wfds);FD_SET(fd,&wfds);
    tv.tv_sec=DETACHED_CONNECT_TIMEOUT_MS/1000;
    tv.tv_usec=(DETACHED_CONNECT_TIMEOUT_MS%1000)*1000;
    rc=select(fd+1,NULL,&wfds,NULL,&tv);
    if(rc<=0){close(fd);return -1;}
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&soerr,&slen)<0||soerr!=0){close(fd);return -1;}
  }

  fcntl(fd,F_SETFL,flags);
  set_socket_timeout(fd,DETACHED_IO_TIMEOUT_MS);
  return fd;
}

static int worker_request(const char *method,const char *path,char *out,size_t outsz)
{
  int fd;
  char req[512];
  size_t used=0;

  if(out&&outsz)out[0]='\0';
  fd=connect_worker();
  if(fd<0)return -1;

  snprintf(req,sizeof(req),
    "%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",
    method,path,ASTRO_REMOTE_WORKER_PORT);
  if(send(fd,req,strlen(req),0)<=0){close(fd);return -2;}
  if(!out||outsz<2){close(fd);return 0;}

  while(used+1<outsz){
    ssize_t n=recv(fd,out+used,outsz-used-1,0);
    if(n>0){used+=(size_t)n;continue;}
    if(n==0)break;
    if(errno==EINTR)continue;
    if((errno==EAGAIN||errno==EWOULDBLOCK)&&used>0)break;
    close(fd);return used?0:-3;
  }
  close(fd);
  if(!used)return -3;
  out[used]='\0';
  return 0;
}

static int json_bool(const char *j,const char *key)
{
  char p[96];
  snprintf(p,sizeof(p),"\"%s\":true",key);
  return strstr(j,p)!=NULL;
}

static int json_int(const char *j,const char *key,int fallback)
{
  char p[96];const char *s;
  snprintf(p,sizeof(p),"\"%s\":",key);
  s=strstr(j,p);
  return s?atoi(s+strlen(p)):fallback;
}

static void json_string(const char *j,const char *key,char *out,size_t outsz)
{
  char p[96];const char *s,*e;size_t n;
  if(!out||!outsz)return;out[0]='\0';
  snprintf(p,sizeof(p),"\"%s\":\"",key);
  s=strstr(j,p);if(!s)return;s+=strlen(p);e=strchr(s,'\"');if(!e)return;
  n=(size_t)(e-s);if(n>=outsz)n=outsz-1;memcpy(out,s,n);out[n]='\0';
}

static int apply_status(const char *buf)
{
  char phase[48];
  if(!buf||!strstr(buf,"\"service\":\"astrorem\""))return -1;
  phase[0]='\0';json_string(buf,"phase",phase,sizeof(phase));if(phase[0])set_phase(phase);
  g_remote.worker_pid=json_int(buf,"pid",g_remote.worker_pid);
  g_remote.session_active=json_bool(buf,"session_active");
  g_remote.remoteplay_enabled=json_bool(buf,"remoteplay_enabled");
  g_remote.remoteplay_tcp_9295=json_bool(buf,"remoteplay_tcp_9295");
  g_remote.video_ready=json_bool(buf,"video_ready");
  g_remote.control_ready=json_bool(buf,"control_ready");
  g_remote.source_probe_rc=json_int(buf,"source_probe_rc",g_remote.source_probe_rc);
  g_remote.source_available=(g_remote.source_probe_rc==0);
  g_remote.worker_port_open=1;
  g_remote.service_online=1;
  return 0;
}

static int refresh_status(void)
{
  char buf[4096];
  int rc=worker_request("GET","/status",buf,sizeof(buf));
  if(rc!=0)return rc;
  return apply_status(buf);
}

void astro_remote_service_init(void)
{
  memset(&g_remote,0,sizeof(g_remote));
  g_remote.worker_port=ASTRO_REMOTE_WORKER_PORT;
  g_remote.source_probe_rc=-999;
  g_remote.last_change_at=time(NULL);
  set_phase("detached_remote_offline");
}

int astro_remote_service_ensure_worker(void)
{
  char buf[512];
  if(worker_request("GET","/health",buf,sizeof(buf))!=0||!strstr(buf,"\"ok\":true")){
    g_remote.service_online=0;g_remote.worker_port_open=0;set_phase("detached_remote_offline");return -1;
  }
  if(refresh_status()!=0){g_remote.service_online=0;set_phase("detached_remote_unresponsive");return -2;}
  return 0;
}

int astro_remote_service_start(void)
{
  char buf[4096];
  if(astro_remote_service_ensure_worker()!=0)return -1;
  if(worker_request("POST","/session/start",buf,sizeof(buf))!=0)return -2;
  if(apply_status(buf)!=0)return -3;
  g_remote.generation++;
  if(g_remote.session_active)g_remote.session_started_at=time(NULL);
  return 1;
}

int astro_remote_service_stop(void)
{
  char buf[4096];
  if(astro_remote_service_ensure_worker()!=0)return -1;
  if(worker_request("POST","/session/stop",buf,sizeof(buf))!=0)return -2;
  if(apply_status(buf)!=0)return -3;
  g_remote.session_started_at=0;
  return 1;
}

int astro_remote_service_enable_remoteplay(void)
{
  char buf[4096];
  if(astro_remote_service_ensure_worker()!=0)return -1;
  if(worker_request("POST","/remoteplay/enable",buf,sizeof(buf))!=0)return -2;
  if(apply_status(buf)!=0)return -3;
  return g_remote.remoteplay_enabled?1:-4;
}

int astro_remote_service_shutdown_worker(void)
{
  set_phase("detached_remote_independent");
  return 0;
}

void astro_remote_service_snapshot(astro_remote_state_t *out)
{
  if(refresh_status()!=0){
    g_remote.service_online=0;g_remote.worker_port_open=0;g_remote.worker_pid=0;
    set_phase("detached_remote_offline");
  }
  if(out)*out=g_remote;
}
