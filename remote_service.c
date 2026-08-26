#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "remote_service.h"
#include "remote_worker.h"

#define WORKER_CONNECT_TIMEOUT_MS 200
#define WORKER_IO_TIMEOUT_MS 300

static astro_remote_state_t g_remote;
static pid_t g_worker_pid=-1;

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

static void clear_worker_diag(void)
{
  g_remote.worker_ppid=0;
  g_remote.worker_stat=0;
  g_remote.worker_wmesg[0]='\0';
  g_remote.worker_lockname[0]='\0';
}

static void read_worker_diag(pid_t pid)
{
  int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};
  size_t sz=0;
  void *buf=NULL;

  clear_worker_diag();
  if(pid<=0)return;
  if(sysctl(mib,4,NULL,&sz,NULL,0)!=0||sz==0)return;
  buf=malloc(sz);
  if(!buf)return;
  if(sysctl(mib,4,buf,&sz,NULL,0)!=0){free(buf);return;}

  for(char *p=(char *)buf;p<(char *)buf+sz;){
    struct kinfo_proc *ki=(struct kinfo_proc *)p;
    if(ki->ki_structsize==0)break;
    p+=ki->ki_structsize;
    if(ki->ki_pid!=pid)continue;
    g_remote.worker_ppid=(int)ki->ki_ppid;
    g_remote.worker_stat=(int)ki->ki_stat;
    snprintf(g_remote.worker_wmesg,sizeof(g_remote.worker_wmesg),"%s",ki->ki_wmesg);
    snprintf(g_remote.worker_lockname,sizeof(g_remote.worker_lockname),"%s",ki->ki_lockname);
    break;
  }
  free(buf);
}

static int connect_local_timeout(int port,int timeout_ms)
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
  a.sin_port=htons((uint16_t)port);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);

  rc=connect(fd,(struct sockaddr *)&a,sizeof(a));
  if(rc!=0){
    if(errno!=EINPROGRESS){close(fd);return -1;}
    FD_ZERO(&wfds);FD_SET(fd,&wfds);
    tv.tv_sec=timeout_ms/1000;
    tv.tv_usec=(timeout_ms%1000)*1000;
    rc=select(fd+1,NULL,&wfds,NULL,&tv);
    if(rc<=0){close(fd);return -1;}
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&soerr,&slen)<0||soerr!=0){close(fd);return -1;}
  }

  fcntl(fd,F_SETFL,flags);
  set_socket_timeout(fd,WORKER_IO_TIMEOUT_MS);
  return fd;
}

static int worker_port_open(void)
{
  int fd=connect_local_timeout(ASTRO_REMOTE_WORKER_PORT,WORKER_CONNECT_TIMEOUT_MS);
  if(fd<0)return 0;
  close(fd);
  return 1;
}

static int worker_healthcheck(void)
{
  int fd,n;
  char buf[512];
  const char *req="GET /health HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";

  fd=connect_local_timeout(ASTRO_REMOTE_WORKER_PORT,WORKER_CONNECT_TIMEOUT_MS);
  if(fd<0)return 0;
  if(send(fd,req,strlen(req),0)<=0){close(fd);return 0;}
  memset(buf,0,sizeof(buf));
  n=recv(fd,buf,sizeof(buf)-1,0);
  close(fd);
  if(n<=0)return 0;
  buf[n]='\0';
  return strstr(buf,"\"ok\":true")!=NULL&&strstr(buf,"astrorem")!=NULL;
}

static int json_bool(const char *j,const char *key)
{
  char p[96];
  snprintf(p,sizeof(p),"\"%s\":true",key);
  return strstr(j,p)!=NULL;
}

static int json_int(const char *j,const char *key,int fallback)
{
  char p[96];
  const char *s;
  snprintf(p,sizeof(p),"\"%s\":",key);
  s=strstr(j,p);
  if(!s)return fallback;
  return atoi(s+strlen(p));
}

static void json_string(const char *j,const char *key,char *out,size_t out_sz)
{
  char p[96];
  const char *s,*e;
  size_t n;
  if(!out||out_sz==0)return;
  out[0]='\0';
  snprintf(p,sizeof(p),"\"%s\":\"",key);
  s=strstr(j,p);
  if(!s)return;
  s+=strlen(p);
  e=strchr(s,'\"');
  if(!e)return;
  n=(size_t)(e-s);
  if(n>=out_sz)n=out_sz-1;
  memcpy(out,s,n);out[n]='\0';
}

static int fetch_worker_status(void)
{
  int fd,n;
  char buf[2048];
  char phase[48];
  const char *req="GET /status HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";

  fd=connect_local_timeout(ASTRO_REMOTE_WORKER_PORT,WORKER_CONNECT_TIMEOUT_MS);
  if(fd<0)return -1;
  if(send(fd,req,strlen(req),0)<=0){close(fd);return -1;}
  memset(buf,0,sizeof(buf));
  n=recv(fd,buf,sizeof(buf)-1,0);
  close(fd);
  if(n<=0)return -1;
  buf[n]='\0';

  phase[0]='\0';
  json_string(buf,"phase",phase,sizeof(phase));
  if(phase[0])set_phase(phase);
  g_remote.source_probe_rc=json_int(buf,"source_probe_rc",g_remote.source_probe_rc);
  g_remote.remoteplay_enabled=json_bool(buf,"remoteplay_enabled");
  g_remote.remoteplay_tcp_9295=json_bool(buf,"remoteplay_tcp_9295");
  g_remote.video_ready=json_bool(buf,"video_ready");
  g_remote.control_ready=json_bool(buf,"control_ready");
  g_remote.source_available=(g_remote.source_probe_rc==0);
  g_remote.remoteplay_initialized=0;
  g_remote.source_probed_at=time(NULL);
  return 0;
}

static void mark_worker_reaped(int status,const char *phase)
{
  g_remote.worker_exit_status=status;
  g_worker_pid=-1;
  g_remote.worker_pid=0;
  g_remote.worker_port_open=0;
  g_remote.service_online=0;
  g_remote.session_active=0;
  g_remote.video_ready=0;
  g_remote.control_ready=0;
  g_remote.source_available=0;
  g_remote.worker_stuck=0;
  g_remote.session_started_at=0;
  clear_worker_diag();
  if(phase)set_phase(phase);
  g_remote.last_change_at=time(NULL);
}

static int reap_nonblocking(const char *phase)
{
  int status=0;
  pid_t r;
  if(g_worker_pid<=0)return 1;
  r=waitpid(g_worker_pid,&status,WNOHANG);
  if(r==g_worker_pid){mark_worker_reaped(status,phase);return 1;}
  if(r<0&&errno==ECHILD){mark_worker_reaped(0,phase);return 1;}
  return 0;
}

static int wait_worker_exit_ms(int timeout_ms,const char *phase)
{
  int elapsed=0;
  while(elapsed<=timeout_ms){
    if(reap_nonblocking(phase))return 1;
    usleep(100000);
    elapsed+=100;
  }
  return 0;
}

static int graceful_worker_shutdown(void)
{
  int fd;
  char buf[256];
  const char *req="POST /shutdown HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
  fd=connect_local_timeout(ASTRO_REMOTE_WORKER_PORT,WORKER_CONNECT_TIMEOUT_MS);
  if(fd<0)return -1;
  if(send(fd,req,strlen(req),0)<=0){close(fd);return -1;}
  recv(fd,buf,sizeof(buf),0);
  close(fd);
  return 0;
}

void astro_remote_service_init(void)
{
  memset(&g_remote,0,sizeof(g_remote));
  g_worker_pid=-1;
  g_remote.worker_port=ASTRO_REMOTE_WORKER_PORT;
  g_remote.last_change_at=time(NULL);
  set_phase("idle");
}

int astro_remote_service_start(void)
{
  pid_t pid;
  int i;

  reap_nonblocking("remote_worker_exited");
  if(g_worker_pid>0){
    g_remote.worker_port_open=worker_port_open();
    if(worker_healthcheck()){
      g_remote.service_online=1;
      g_remote.session_active=1;
      fetch_worker_status();
      return 0;
    }
    if(astro_remote_service_stop()<0)return -4;
  }

  g_remote.worker_start_errno=0;
  g_remote.worker_exit_status=0;
  g_remote.worker_stuck=0;
  clear_worker_diag();
  set_phase("starting_remote_worker");

  pid=fork();
  if(pid<0){
    g_remote.worker_start_errno=errno;
    set_phase("remote_fork_failed");
    g_remote.last_change_at=time(NULL);
    return -2;
  }

  if(pid==0){
    int rc;
    int fd;
    /* Child must never keep Astro's public 45821 listener or the current
       browser connection alive. Start with a clean fd table and open only
       the localhost 45822 listener. */
    for(fd=3;fd<1024;fd++)close(fd);
    rc=astro_remote_worker_main();
    _exit(rc);
  }

  g_worker_pid=pid;
  g_remote.worker_pid=(int)pid;
  g_remote.session_active=1;
  g_remote.service_online=0;
  g_remote.worker_port_open=0;
  g_remote.generation++;
  g_remote.session_started_at=time(NULL);
  g_remote.last_change_at=g_remote.session_started_at;

  for(i=0;i<20;i++){
    if(reap_nonblocking("remote_worker_exited"))return -3;
    g_remote.worker_port_open=worker_port_open();
    if(g_remote.worker_port_open&&worker_healthcheck()){
      g_remote.service_online=1;
      fetch_worker_status();
      read_worker_diag(g_worker_pid);
      g_remote.last_change_at=time(NULL);
      return 1;
    }
    usleep(100000);
  }

  g_remote.worker_port_open=worker_port_open();
  g_remote.service_online=0;
  set_phase(g_remote.worker_port_open?"remote_worker_unresponsive":"remote_worker_start_timeout");
  read_worker_diag(g_worker_pid);
  g_remote.last_change_at=time(NULL);
  return -4;
}

int astro_remote_service_stop(void)
{
  pid_t pid;

  reap_nonblocking("idle");
  if(g_worker_pid<=0){
    g_remote.session_active=0;
    g_remote.service_online=0;
    g_remote.worker_port_open=0;
    g_remote.worker_stuck=0;
    set_phase("idle");
    return 0;
  }

  pid=g_worker_pid;
  set_phase("stopping_remote_worker");
  graceful_worker_shutdown();
  if(wait_worker_exit_ms(700,"idle"))return 1;

  kill(pid,SIGTERM);
  if(wait_worker_exit_ms(700,"idle"))return 1;

  kill(pid,SIGCONT);
  usleep(100000);
  kill(pid,SIGKILL);
  if(wait_worker_exit_ms(1500,"idle"))return 1;

  g_remote.worker_stuck=1;
  g_remote.worker_pid=(int)pid;
  g_remote.worker_port_open=worker_port_open();
  g_remote.service_online=worker_healthcheck();
  g_remote.session_active=1;
  read_worker_diag(pid);
  set_phase("remote_worker_stuck");
  g_remote.last_change_at=time(NULL);
  return -2;
}

void astro_remote_service_snapshot(astro_remote_state_t *out)
{
  reap_nonblocking("remote_worker_exited");
  if(g_worker_pid>0){
    int healthy;
    g_remote.worker_pid=(int)g_worker_pid;
    g_remote.worker_port_open=worker_port_open();
    healthy=g_remote.worker_port_open?worker_healthcheck():0;
    g_remote.service_online=healthy;
    g_remote.session_active=1;
    read_worker_diag(g_worker_pid);
    if(healthy){
      if(fetch_worker_status()!=0&&!g_remote.worker_stuck)set_phase("remote_worker_status_timeout");
    }else if(!g_remote.worker_stuck){
      set_phase(g_remote.worker_port_open?"remote_worker_unresponsive":"remote_worker_unreachable");
    }
  }
  if(out)*out=g_remote;
}
