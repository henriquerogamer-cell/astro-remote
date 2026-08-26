#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "remote_worker.h"
#include "remote_ps5_source.h"
#include "remote_pairing.h"
#include "astro_offact.h"
#include "remote_page_v137.h"

static volatile sig_atomic_t g_worker_running=1;
static int g_session_active=0;
static astro_remote_ps5_source_probe_t g_probe;
static astro_remote_ps5_protocol_probe_t g_protocol;
static astro_remote_pairing_state_t g_pairing;
static astro_account_state_t g_account;
static int g_probe_rc=-999;
static int g_protocol_rc=-999;
static int g_pairing_rc=-999;
static int g_account_rc=-999;
static int g_reboot_required=0;
static char g_phase[48]="idle";

/* Temporary account probe terminal. The probe runs in a child so a blocking
   PS5 API call cannot freeze the web worker. */
static pid_t g_account_probe_pid=-1;
static int g_account_log_fd=-1;
static char g_account_log[8192];
static size_t g_account_log_len=0;
static int g_account_probe_done=0;
static int g_account_probe_exit=0;

static void worker_signal(int sig){(void)sig;g_worker_running=0;}
static void set_phase(const char *phase){snprintf(g_phase,sizeof(g_phase),"%s",phase?phase:"unknown");}

static void json_safe_copy(char *dst,size_t dst_size,const char *src)
{
  size_t j=0;
  if(!dst||dst_size==0)return;
  if(!src){dst[0]='\0';return;}
  for(size_t i=0;src[i]&&j+1<dst_size;i++){
    unsigned char c=(unsigned char)src[i];
    if(c=='"'||c=='\\'){
      if(j+2>=dst_size)break;
      dst[j++]='\\';dst[j++]=(char)c;
    }else if(c=='\n'){
      if(j+2>=dst_size)break;
      dst[j++]='\\';dst[j++]='n';
    }else if(c=='\r'){
      if(j+2>=dst_size)break;
      dst[j++]='\\';dst[j++]='r';
    }else if(c>=0x20&&c<0x7f){
      dst[j++]=(char)c;
    }else dst[j++]='?';
  }
  dst[j]='\0';
}

static void account_log_append(const char *buf,size_t n)
{
  if(!buf||!n)return;
  if(n>=sizeof(g_account_log)-1){buf+=n-(sizeof(g_account_log)-1);n=sizeof(g_account_log)-1;g_account_log_len=0;}
  if(g_account_log_len+n>=sizeof(g_account_log)){
    size_t drop=g_account_log_len+n-(sizeof(g_account_log)-1);
    memmove(g_account_log,g_account_log+drop,g_account_log_len-drop);
    g_account_log_len-=drop;
  }
  memcpy(g_account_log+g_account_log_len,buf,n);
  g_account_log_len+=n;
  g_account_log[g_account_log_len]='\0';
}

static void poll_account_probe(void)
{
  if(g_account_log_fd>=0){
    for(;;){
      char b[512];ssize_t n=read(g_account_log_fd,b,sizeof(b));
      if(n>0){account_log_append(b,(size_t)n);continue;}
      if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;
      if(n==0){close(g_account_log_fd);g_account_log_fd=-1;}
      break;
    }
  }
  if(g_account_probe_pid>0){
    int st=0;pid_t r=waitpid(g_account_probe_pid,&st,WNOHANG);
    if(r==g_account_probe_pid){
      char b[96];
      g_account_probe_done=1;g_account_probe_exit=st;g_account_probe_pid=-1;
      snprintf(b,sizeof(b),"[probe] child exited status=%d\n",st);account_log_append(b,strlen(b));
    }
  }
}

static int start_account_probe(void)
{
  int p[2];pid_t pid;
  poll_account_probe();
  if(g_account_probe_pid>0)return 0;
  if(g_account_log_fd>=0){close(g_account_log_fd);g_account_log_fd=-1;}
  g_account_log_len=0;g_account_log[0]='\0';g_account_probe_done=0;g_account_probe_exit=0;
  account_log_append("[probe] starting isolated account probe\n",39);
  if(pipe(p)!=0){account_log_append("[probe] pipe() failed\n",22);return -1;}
  pid=fork();
  if(pid<0){close(p[0]);close(p[1]);account_log_append("[probe] fork() failed\n",22);return -2;}
  if(pid==0){
    astro_account_state_t s;int rc;
    close(p[0]);
    astro_account_set_debug_fd(p[1]);
    rc=astro_account_get_current(&s);
    dprintf(p[1],"[probe] RESULT rc=%d user=%d slot=%d name='%s' account=0x%016llx type='%s' flags=%d activated=%d\n",
      rc,s.foreground_user,s.registry_index,s.account_name,(unsigned long long)s.account_id,s.account_type,s.account_flags,s.activated);
    close(p[1]);
    _exit(rc==0?0:100+(-rc));
  }
  close(p[1]);g_account_log_fd=p[0];g_account_probe_pid=pid;
  fcntl(g_account_log_fd,F_SETFL,fcntl(g_account_log_fd,F_GETFL,0)|O_NONBLOCK);
  return 1;
}

static void send_account_debug(int fd)
{
  char safe[16384],j[18000];
  poll_account_probe();
  json_safe_copy(safe,sizeof(safe),g_account_log);
  snprintf(j,sizeof(j),"{\"ok\":true,\"running\":%s,\"done\":%s,\"pid\":%d,\"exit_status\":%d,\"log\":\"%s\"}",
    g_account_probe_pid>0?"true":"false",g_account_probe_done?"true":"false",(int)g_account_probe_pid,g_account_probe_exit,safe);
  send_json(fd,"200 OK",j);
}

static int refresh_account(void)
{
  memset(&g_account,0,sizeof(g_account));
  g_account_rc=astro_account_get_current(&g_account);
  return g_account_rc;
}

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

static int activate_foreground_account(void)
{
  int rc=refresh_account();
  if(rc!=0){set_phase("account_detection_failed");return rc;}
  if(g_account.activated){g_reboot_required=0;set_phase("account_already_activated");return 0;}
  set_phase("activating_foreground_account");
  rc=astro_account_fake_activate_current(&g_account);
  g_account_rc=rc;
  if(rc==0){g_reboot_required=1;set_phase("account_activated_reboot_required");}
  else set_phase("account_activation_failed");
  return rc;
}

static int prepare_linkdev_pairing(void)
{
  int rc;
  memset(&g_pairing,0,sizeof(g_pairing));g_pairing_rc=-999;
  rc=refresh_account();
  if(rc!=0){g_pairing_rc=rc;set_phase("account_detection_failed");return rc;}
  if(!g_account.activated){g_pairing_rc=-60;set_phase("account_activation_required");return g_pairing_rc;}
  if(g_reboot_required){g_pairing_rc=-61;set_phase("reboot_required");return g_pairing_rc;}
  set_phase("enabling_remoteplay_offline");
  rc=astro_remote_ps5_source_enable_remoteplay();
  if(rc!=0){g_pairing_rc=rc;set_phase("remoteplay_enable_failed");return rc;}
  run_safe_probe();set_phase("generating_linkdev_pin");
  g_pairing_rc=astro_remote_pairing_prepare(&g_pairing);
  if(g_pairing_rc==0&&g_pairing.pin_ready)set_phase("linkdev_pin_ready");
  else set_phase("linkdev_pairing_failed");
  return g_pairing_rc;
}

static void refresh_pairing(void)
{
  int rc;if(!g_pairing.pairing_active)return;
  rc=astro_remote_pairing_poll(&g_pairing);g_pairing_rc=g_pairing.rc;
  if(rc==1){set_phase("linkdev_pairing_complete");g_session_active=1;}
  else if(rc==-30)set_phase("linkdev_pairing_timeout");
  else if(rc==-32)set_phase("linkdev_pairing_rejected");
  else if(rc<0)set_phase("linkdev_pairing_poll_failed");
}

static void stop_session(void)
{
  g_session_active=0;astro_remote_pairing_cancel(&g_pairing);
  memset(&g_protocol,0,sizeof(g_protocol));g_protocol_rc=-999;set_phase("idle");
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

static void send_account_status(int fd,int do_refresh)
{
  char j[1024],safe_name[96],safe_type[64];
  if(do_refresh)refresh_account();
  json_safe_copy(safe_name,sizeof(safe_name),g_account.account_name);
  json_safe_copy(safe_type,sizeof(safe_type),g_account.account_type);
  snprintf(j,sizeof(j),
    "{\"ok\":true,\"account_rc\":%d,\"account_name\":\"%s\",\"account_user\":%d,\"account_slot\":%d,"
    "\"account_id_hex\":\"0x%016llx\",\"account_proposed_id_hex\":\"0x%016llx\",\"account_type\":\"%s\","
    "\"account_flags\":%d,\"account_activated\":%s,\"reboot_required\":%s}",
    g_account_rc,safe_name,g_account.foreground_user,g_account.registry_index,
    (unsigned long long)g_account.account_id,(unsigned long long)g_account.proposed_account_id,safe_type,
    g_account.account_flags,g_account.activated?"true":"false",g_reboot_required?"true":"false");
  send_json(fd,"200 OK",j);
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
  refresh_pairing();poll_account_probe();
  snprintf(j,sizeof(j),
    "{\"ok\":true,\"service\":\"astrorem\",\"pid\":%d,\"port\":%d,\"bind\":\"127.0.0.1\",\"session_active\":%s,\"phase\":\"%s\","
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
  syscall(SYS_thr_set_name,-1,"astrorem");signal(SIGTERM,worker_signal);signal(SIGINT,worker_signal);signal(SIGPIPE,SIG_IGN);
  server=socket(AF_INET,SOCK_STREAM,0);if(server<0)return 10;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(ASTRO_REMOTE_WORKER_PORT);
  if(bind(server,(struct sockaddr *)&a,sizeof(a))<0){close(server);return 11;}if(listen(server,4)<0){close(server);return 12;}
  memset(&g_protocol,0,sizeof(g_protocol));memset(&g_pairing,0,sizeof(g_pairing));memset(&g_account,0,sizeof(g_account));
  g_protocol_rc=-999;g_pairing_rc=-999;g_account_rc=-999;run_safe_probe();set_phase("idle");

  while(g_worker_running){
    fd_set rfds;struct timeval tv;int rc;FD_ZERO(&rfds);FD_SET(server,&rfds);tv.tv_sec=0;tv.tv_usec=250000;
    rc=select(server+1,&rfds,NULL,NULL,&tv);poll_account_probe();if(rc<0){if(!g_worker_running)break;continue;}if(rc==0){refresh_pairing();continue;}
    if(FD_ISSET(server,&rfds)){
      int c=accept(server,NULL,NULL);char buf[2048];int n;if(c<0)continue;memset(buf,0,sizeof(buf));n=recv(c,buf,sizeof(buf)-1,0);
      if(n>0){
        buf[n]='\0';
        if(strstr(buf,"GET / "))send_html(c,remote_page_v137);
        else if(strstr(buf,"GET /health "))send_json(c,"200 OK","{\"ok\":true,\"service\":\"astrorem\"}");
        else if(strstr(buf,"GET /status "))send_status(c);
        else if(strstr(buf,"GET /account/debug "))send_account_debug(c);
        else if(strstr(buf,"POST /account/debug/start ")){start_account_probe();send_account_debug(c);}
        else if(strstr(buf,"GET /account/status "))send_account_status(c,1);
        else if(strstr(buf,"POST /account/refresh ")){set_phase("reading_foreground_account");send_account_status(c,1);set_phase(g_account_rc==0?(g_account.activated?"account_activated":"account_activation_required"):"account_detection_failed");}
        else if(strstr(buf,"POST /account/activate ")){int arc=activate_foreground_account();if(arc==0)send_account_status(c,0);else{char x[192];snprintf(x,sizeof(x),"{\"ok\":false,\"error\":\"account_activation_failed\",\"rc\":%d}",arc);send_json(c,"500 Internal Server Error",x);}}
        else if(strstr(buf,"POST /session/start ")){int pr=run_safe_probe();if(pr==-20||pr==-30||pr==-40){g_session_active=0;g_protocol_rc=-999;memset(&g_protocol,0,sizeof(g_protocol));}else{g_session_active=1;run_protocol_probe();}send_status(c);}
        else if(strstr(buf,"POST /session/stop ")){stop_session();send_status(c);}
        else if(strstr(buf,"POST /probe ")){run_safe_probe();send_status(c);}
        else if(strstr(buf,"POST /protocol/probe ")){if(run_safe_probe()==0)run_protocol_probe();send_status(c);}
        else if(strstr(buf,"POST /pairing/prepare ")){prepare_linkdev_pairing();send_status(c);}
        else if(strstr(buf,"POST /pairing/cancel ")){astro_remote_pairing_cancel(&g_pairing);set_phase("pairing_cancelled");send_status(c);}
        else if(strstr(buf,"POST /remoteplay/enable ")){int erc=astro_remote_ps5_source_enable_remoteplay();if(erc==0){run_safe_probe();set_phase("remoteplay_enabled");send_status(c);}else{char x[192];snprintf(x,sizeof(x),"{\"ok\":false,\"error\":\"remoteplay_enable_failed\",\"rc\":%d}",erc);send_json(c,"500 Internal Server Error",x);}}
        else if(strstr(buf,"POST /shutdown ")){send_json(c,"200 OK","{\"ok\":true,\"stopping\":true}");g_worker_running=0;}
        else send_json(c,"404 Not Found","{\"ok\":false,\"error\":\"not_found\"}");
      }
      close(c);
    }
  }
  if(g_account_probe_pid>0)kill(g_account_probe_pid,SIGKILL);
  if(g_account_log_fd>=0)close(g_account_log_fd);
  close(server);return 0;
}
