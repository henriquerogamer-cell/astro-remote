#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "remote_worker.h"
#include "remote_ps5_source.h"
#include "remote_pairing.h"
#include "remote_page_v137.h"

static volatile sig_atomic_t g_worker_running=1;
static int g_session_active=0;
static astro_remote_ps5_source_probe_t g_probe;
static astro_remote_ps5_protocol_probe_t g_protocol;
static astro_remote_pairing_state_t g_pairing;
static int g_probe_rc=-999;
static int g_protocol_rc=-999;
static int g_pairing_rc=-999;
static char g_phase[48]="idle";

static void worker_signal(int sig){(void)sig;g_worker_running=0;}
static void set_phase(const char *phase){snprintf(g_phase,sizeof(g_phase),"%s",phase?phase:"unknown");}

static int run_safe_probe(void)
{
  memset(&g_probe,0,sizeof(g_probe));
  set_phase("probing_remoteplay_source");
  g_probe_rc=astro_remote_ps5_source_probe(&g_probe);
  if(g_probe_rc==0)set_phase("remoteplay_source_detected");
  else if(g_probe_rc==-20)set_phase("remoteplay_registry_error");
  else if(g_probe_rc==-30)set_phase("remoteplay_disabled");
  else if(g_probe_rc==-40)set_phase("remoteplay_port_closed");
  else set_phase("remoteplay_probe_failed");
  return g_probe_rc;
}

static int run_protocol_probe(void)
{
  memset(&g_protocol,0,sizeof(g_protocol));
  set_phase("probing_remoteplay_protocol");
  g_protocol_rc=astro_remote_ps5_protocol_probe(&g_protocol);
  if(g_protocol_rc!=0)set_phase("remoteplay_protocol_probe_failed");
  else if(g_protocol.http_status==200&&g_protocol.nonce_present)set_phase("remoteplay_session_init_ready");
  else if(g_protocol.http_status>=400)set_phase("remoteplay_pairing_required");
  else set_phase("remoteplay_protocol_detected");
  return g_protocol_rc;
}

static int prepare_linkdev_pairing(void)
{
  int rc;
  memset(&g_pairing,0,sizeof(g_pairing));
  g_pairing_rc=-999;
  set_phase("enabling_remoteplay_offline");
  rc=astro_remote_ps5_source_enable_remoteplay();
  if(rc!=0){g_pairing_rc=rc;set_phase("remoteplay_enable_failed");return rc;}
  run_safe_probe();
  set_phase("generating_linkdev_pin");
  g_pairing_rc=astro_remote_pairing_prepare(&g_pairing);
  if(g_pairing_rc==0&&g_pairing.pin_ready)set_phase("linkdev_pin_ready");
  else if(g_pairing_rc==-13)set_phase("account_activation_required");
  else set_phase("linkdev_pairing_failed");
  return g_pairing_rc;
}

static void refresh_pairing(void)
{
  int rc;
  if(!g_pairing.pairing_active)return;
  rc=astro_remote_pairing_poll(&g_pairing);g_pairing_rc=g_pairing.rc;
  if(rc==1){set_phase("linkdev_pairing_complete");g_session_active=1;}
  else if(rc==-30)set_phase("linkdev_pairing_timeout");
  else if(rc==-32)set_phase("linkdev_pairing_rejected");
  else if(rc<0)set_phase("linkdev_pairing_poll_failed");
}

static void stop_session(void)
{
  g_session_active=0;
  astro_remote_pairing_cancel(&g_pairing);
  memset(&g_protocol,0,sizeof(g_protocol));g_protocol_rc=-999;
  set_phase("idle");
}

static void send_json(int fd,const char *status,const char *body)
{
  char h[512];size_t n=strlen(body);
  snprintf(h,sizeof(h),"HTTP/1.1 %s\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",status,n);
  send(fd,h,strlen(h),0);send(fd,body,n,0);
}

static void send_html(int fd,const char *body)
{
  char h[512];size_t n=strlen(body);
  snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",n);
  send(fd,h,strlen(h),0);send(fd,body,n,0);
}

static void send_status(int fd)
{
  char j[2600];
  int protocol_ready=(g_protocol_rc==0);
  int session_authenticated=(protocol_ready&&g_protocol.http_status==200&&g_protocol.nonce_present);
  int pairing_required=(protocol_ready&&!session_authenticated);
  time_t now=time(NULL);
  int pin_ready=(g_pairing.pin_ready&&g_pairing.expires_at>now);
  long seconds_left=pin_ready?(long)(g_pairing.expires_at-now):0;
  refresh_pairing();
  snprintf(j,sizeof(j),
    "{\"ok\":true,\"service\":\"astrorem\",\"pid\":%d,\"port\":%d,\"bind\":\"127.0.0.1\",\"role\":\"remoteplay_only\",\"session_active\":%s,\"phase\":\"%s\","
    "\"source_probe_rc\":%d,\"remoteplay_enabled\":%s,\"remoteplay_tcp_9295\":%s,\"protocol_probe_rc\":%d,\"protocol_ready\":%s,"
    "\"session_authenticated\":%s,\"pairing_required\":%s,\"rp_http_status\":%d,\"rp_nonce_present\":%s,\"rp_application_reason\":%u,"
    "\"rp_version\":\"%s\",\"pairing_prepare_rc\":%d,\"pairing_pin_ready\":%s,\"pairing_pin\":%u,\"pairing_seconds_left\":%ld,"
    "\"pairing_active\":%s,\"pairing_complete\":%s,\"pairing_stat\":%d,\"pairing_error\":%d,"
    "\"pairing_user\":%d,\"pairing_registry_index\":%d,\"pairing_account_id_ready\":%s,\"pairing_account_id\":\"%s\","
    "\"registration_persisted\":false,\"video_ready\":false,\"control_ready\":false}",
    (int)getpid(),ASTRO_REMOTE_WORKER_PORT,g_session_active?"true":"false",g_phase,g_probe_rc,
    g_probe.remoteplay_enabled?"true":"false",g_probe.tcp_9295_open?"true":"false",g_protocol_rc,protocol_ready?"true":"false",
    session_authenticated?"true":"false",pairing_required?"true":"false",g_protocol.http_status,g_protocol.nonce_present?"true":"false",
    (unsigned int)g_protocol.application_reason,g_protocol.rp_version,g_pairing_rc,pin_ready?"true":"false",g_pairing.pin,seconds_left,
    g_pairing.pairing_active?"true":"false",g_pairing.pairing_complete?"true":"false",g_pairing.pair_stat,g_pairing.pair_error,
    g_pairing.foreground_user,g_pairing.registry_index,g_pairing.account_id_b64[0]?"true":"false",g_pairing.account_id_b64);
  send_json(fd,"200 OK",j);
}

int astro_remote_worker_main(void)
{
  int server,opt=1;struct sockaddr_in a;
  syscall(SYS_thr_set_name,-1,"astrorem");
  signal(SIGTERM,worker_signal);signal(SIGINT,worker_signal);signal(SIGPIPE,SIG_IGN);
  server=socket(AF_INET,SOCK_STREAM,0);if(server<0)return 10;
  setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(ASTRO_REMOTE_WORKER_PORT);
  if(bind(server,(struct sockaddr *)&a,sizeof(a))<0){close(server);return 11;}
  if(listen(server,4)<0){close(server);return 12;}
  memset(&g_protocol,0,sizeof(g_protocol));memset(&g_pairing,0,sizeof(g_pairing));
  g_protocol_rc=-999;g_pairing_rc=-999;run_safe_probe();set_phase("idle");

  while(g_worker_running){
    fd_set rfds;struct timeval tv;int rc;
    FD_ZERO(&rfds);FD_SET(server,&rfds);tv.tv_sec=0;tv.tv_usec=250000;
    rc=select(server+1,&rfds,NULL,NULL,&tv);
    if(rc<0){if(!g_worker_running)break;continue;}
    if(rc==0){refresh_pairing();continue;}
    if(FD_ISSET(server,&rfds)){
      int c=accept(server,NULL,NULL);char buf[2048];int n;
      if(c<0)continue;memset(buf,0,sizeof(buf));n=recv(c,buf,sizeof(buf)-1,0);
      if(n>0){
        buf[n]='\0';
        if(strstr(buf,"GET / "))send_html(c,remote_page_v137);
        else if(strstr(buf,"GET /health "))send_json(c,"200 OK","{\"ok\":true,\"service\":\"astrorem\",\"role\":\"remoteplay_only\"}");
        else if(strstr(buf,"GET /status "))send_status(c);
        else if(strstr(buf,"POST /session/start ")){int pr=run_safe_probe();if(pr==-20||pr==-30||pr==-40){g_session_active=0;g_protocol_rc=-999;memset(&g_protocol,0,sizeof(g_protocol));}else{g_session_active=1;run_protocol_probe();}send_status(c);}
        else if(strstr(buf,"POST /session/stop ")){stop_session();send_status(c);}
        else if(strstr(buf,"POST /probe ")){run_safe_probe();send_status(c);}
        else if(strstr(buf,"POST /protocol/probe ")){if(run_safe_probe()==0)run_protocol_probe();send_status(c);}
        else if(strstr(buf,"POST /pairing/prepare ")){prepare_linkdev_pairing();send_status(c);}
        else if(strstr(buf,"POST /pairing/cancel ")){astro_remote_pairing_cancel(&g_pairing);set_phase("pairing_cancelled");send_status(c);}
        else if(strstr(buf,"POST /remoteplay/enable ")){int erc=astro_remote_ps5_source_enable_remoteplay();if(erc==0){run_safe_probe();set_phase("remoteplay_enabled");send_status(c);}else{char x[192];snprintf(x,sizeof(x),"{\"ok\":false,\"error\":\"remoteplay_enable_failed\",\"rc\":%d}",erc);send_json(c,"500 Internal Server Error",x);}}
        else if(strstr(buf,"POST /shutdown ")){send_json(c,"200 OK","{\"ok\":true,\"stopping\":true}");g_worker_running=0;}
        else if(strstr(buf,"/account/"))send_json(c,"410 Gone","{\"ok\":false,\"error\":\"account_operations_moved_to_astrolock_45823\"}");
        else send_json(c,"404 Not Found","{\"ok\":false,\"error\":\"not_found\"}");
      }
      close(c);
    }
  }
  close(server);return 0;
}
