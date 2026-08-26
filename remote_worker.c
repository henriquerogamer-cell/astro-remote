#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "remote_worker.h"
#include "remote_ps5_source.h"
#include "remote_page_v137.h"

static volatile sig_atomic_t g_worker_running=1;
static int g_session_active=0;
static astro_remote_ps5_source_probe_t g_probe;
static int g_probe_rc=-999;
static char g_phase[48]="idle";

static void worker_signal(int sig)
{
  (void)sig;
  g_worker_running=0;
}

static void set_phase(const char *phase)
{
  snprintf(g_phase,sizeof(g_phase),"%s",phase?phase:"unknown");
}

static void run_safe_probe(void)
{
  memset(&g_probe,0,sizeof(g_probe));
  set_phase("probing_remoteplay_source");
  g_probe_rc=astro_remote_ps5_source_probe(&g_probe);

  if(g_probe_rc==0)set_phase("remoteplay_source_detected");
  else if(g_probe_rc==-20)set_phase("remoteplay_registry_error");
  else if(g_probe_rc==-30)set_phase("remoteplay_disabled");
  else if(g_probe_rc==-40)set_phase("remoteplay_port_closed");
  else set_phase("remoteplay_probe_failed");
}

static void stop_session(void)
{
  g_session_active=0;
  set_phase("idle");
}

static void send_json(int fd,const char *status,const char *body)
{
  char h[512];
  size_t n=strlen(body);
  snprintf(h,sizeof(h),
    "HTTP/1.1 %s\r\n"
    "Content-Type: application/json; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n\r\n",
    status,n);
  send(fd,h,strlen(h),0);
  send(fd,body,n,0);
}

static void send_html(int fd,const char *body)
{
  char h[512];
  size_t n=strlen(body);
  snprintf(h,sizeof(h),
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n\r\n",n);
  send(fd,h,strlen(h),0);
  send(fd,body,n,0);
}

static void send_status(int fd)
{
  char j[896];
  snprintf(j,sizeof(j),
    "{\"ok\":true,\"service\":\"astrorem\",\"pid\":%d,\"port\":%d,"
    "\"bind\":\"127.0.0.1\",\"session_active\":%s,\"phase\":\"%s\","
    "\"source_probe_rc\":%d,\"remoteplay_enabled\":%s,"
    "\"remoteplay_tcp_9295\":%s,\"video_ready\":false,"
    "\"control_ready\":false}",
    (int)getpid(),ASTRO_REMOTE_WORKER_PORT,
    g_session_active?"true":"false",g_phase,g_probe_rc,
    g_probe.remoteplay_enabled?"true":"false",
    g_probe.tcp_9295_open?"true":"false");
  send_json(fd,"200 OK",j);
}

int astro_remote_worker_main(void)
{
  int server,opt=1;
  struct sockaddr_in a;

  syscall(SYS_thr_set_name,-1,"astrorem");
  signal(SIGTERM,worker_signal);
  signal(SIGINT,worker_signal);
  signal(SIGPIPE,SIG_IGN);

  server=socket(AF_INET,SOCK_STREAM,0);
  if(server<0)return 10;
  setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));

  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  a.sin_port=htons(ASTRO_REMOTE_WORKER_PORT);

  if(bind(server,(struct sockaddr *)&a,sizeof(a))<0){close(server);return 11;}
  if(listen(server,4)<0){close(server);return 12;}

  set_phase("idle");

  while(g_worker_running){
    fd_set rfds;
    struct timeval tv;
    int rc;

    FD_ZERO(&rfds);
    FD_SET(server,&rfds);
    tv.tv_sec=0;
    tv.tv_usec=500000;

    rc=select(server+1,&rfds,NULL,NULL,&tv);
    if(rc<0){
      if(!g_worker_running)break;
      continue;
    }
    if(rc==0)continue;

    if(FD_ISSET(server,&rfds)){
      int c=accept(server,NULL,NULL);
      char buf[2048];
      int n;
      if(c<0)continue;
      memset(buf,0,sizeof(buf));
      n=recv(c,buf,sizeof(buf)-1,0);
      if(n>0){
        buf[n]='\0';
        if(strstr(buf,"GET / "))
          send_html(c,remote_page_v137);
        else if(strstr(buf,"GET /health "))
          send_json(c,"200 OK","{\"ok\":true,\"service\":\"astrorem\"}");
        else if(strstr(buf,"GET /status "))
          send_status(c);
        else if(strstr(buf,"POST /session/start ")){
          g_session_active=1;
          run_safe_probe();
          send_status(c);
        }
        else if(strstr(buf,"POST /session/stop ")){
          stop_session();
          send_status(c);
        }
        else if(strstr(buf,"POST /probe ")){
          if(g_session_active)run_safe_probe();
          send_status(c);
        }
        else if(strstr(buf,"POST /shutdown ")){
          send_json(c,"200 OK","{\"ok\":true,\"stopping\":true}");
          g_worker_running=0;
        }
        else
          send_json(c,"404 Not Found","{\"ok\":false,\"error\":\"not_found\"}");
      }
      close(c);
    }
  }

  close(server);
  return 0;
}
