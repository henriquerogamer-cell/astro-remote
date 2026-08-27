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
#include "astro_pair_client.h"
#include "remote_page_v137.h"

static volatile sig_atomic_t g_worker_running=1;
static int g_session_active=0;
static astro_remote_ps5_source_probe_t g_probe;
static astro_remote_ps5_protocol_probe_t g_protocol;
static astro_remote_pairing_state_t g_pairing;
static astro_pair_client_result_t g_client_result;
static int g_probe_rc=-999;
static int g_protocol_rc=-999;
static int g_pairing_rc=-999;
static int g_pair_server_complete=0;
static int g_client_rc=-999;
static int g_registration_persisted=0;
static pid_t g_client_pid=-1;
static int g_client_pipe=-1;
static char g_pair_account[32];
static char g_phase[48]="idle";

static void worker_signal(int sig){(void)sig;g_worker_running=0;}
static void set_phase(const char *phase){snprintf(g_phase,sizeof(g_phase),"%s",phase?phase:"unknown");}

static int hex_nibble(char c)
{
  if(c>='0'&&c<='9')return c-'0';
  if(c>='a'&&c<='f')return c-'a'+10;
  if(c>='A'&&c<='F')return c-'A'+10;
  return -1;
}

static void url_decode(char *dst,size_t cap,const char *src,size_t len)
{
  size_t i=0,j=0;
  if(!cap)return;
  while(i<len&&j+1<cap){
    if(src[i]=='%'&&i+2<len){int a=hex_nibble(src[i+1]),b=hex_nibble(src[i+2]);if(a>=0&&b>=0){dst[j++]=(char)((a<<4)|b);i+=3;continue;}}
    dst[j++]=src[i]=='+'?' ':src[i];i++;
  }
  dst[j]='\0';
}

static int form_value(const char *req,const char *key,char *out,size_t outsz)
{
  const char *body=strstr(req,"\r\n\r\n");
  char needle[64];const char *p;size_t keylen,n;
  if(!body||!out||!outsz)return 0;body+=4;snprintf(needle,sizeof(needle),"%s=",key);keylen=strlen(needle);p=body;
  while((p=strstr(p,needle))!=NULL){if(p==body||p[-1]=='&')break;p+=keylen;}
  if(!p)return 0;p+=keylen;n=strcspn(p,"&\r\n");url_decode(out,outsz,p,n);return out[0]!=0;
}

static int run_safe_probe(void)
{
  memset(&g_probe,0,sizeof(g_probe));
  set_phase("probing_remoteplay_source");
  g_probe_rc=astro_remote_ps5_source_probe(&g_probe);
  if(g_probe_rc==0)set_phase("remoteplay_source_detected");
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

static int prepare_native_pairing(void)
{
  int rc;
  memset(&g_pairing,0,sizeof(g_pairing));g_pairing_rc=-999;g_pair_server_complete=0;
  set_phase("enabling_remoteplay_offline");
  rc=astro_remote_ps5_source_enable_remoteplay();
  if(rc!=0){g_pairing_rc=rc;set_phase("remoteplay_enable_failed");return rc;}
  run_safe_probe();
  set_phase("generating_astro_pair_pin");
  g_pairing_rc=astro_remote_pairing_prepare(&g_pairing);
  if(g_pairing_rc==0&&g_pairing.pin_ready)set_phase("astro_pair_pin_ready");
  else set_phase("astro_pairing_prepare_failed");
  return g_pairing_rc;
}

static void close_client_pipe(void){if(g_client_pipe>=0){close(g_client_pipe);g_client_pipe=-1;}}

static void cancel_client_job(void)
{
  if(g_client_pid>0){kill(g_client_pid,SIGTERM);waitpid(g_client_pid,NULL,0);g_client_pid=-1;}
  close_client_pipe();
}

static int start_self_pairing(const char *account_id)
{
  int pfd[2];pid_t pid;int rc;
  if(g_client_pid>0)return -210;
  if(!account_id||!account_id[0])return -211;
  rc=astro_pair_account_save(account_id);if(rc!=0)return -212;
  snprintf(g_pair_account,sizeof(g_pair_account),"%s",account_id);
  memset(&g_client_result,0,sizeof(g_client_result));g_client_rc=-999;g_registration_persisted=0;
  rc=prepare_native_pairing();if(rc!=0)return rc;
  if(pipe(pfd)!=0)return -213;
  pid=fork();
  if(pid<0){close(pfd[0]);close(pfd[1]);return -214;}
  if(pid==0){
    astro_pair_client_result_t result;
    close(pfd[0]);syscall(SYS_thr_set_name,-1,"astropair");
    memset(&result,0,sizeof(result));result.rc=astro_pair_client_register_local(account_id,g_pairing.pin,&result);
    (void)write(pfd[1],&result,sizeof(result));close(pfd[1]);_exit(result.rc==0?0:1);
  }
  close(pfd[1]);g_client_pipe=pfd[0];g_client_pid=pid;fcntl(g_client_pipe,F_SETFL,fcntl(g_client_pipe,F_GETFL,0)|O_NONBLOCK);
  set_phase("astro_self_pairing");return 0;
}

static void poll_client_job(void)
{
  if(g_client_pipe>=0){
    astro_pair_client_result_t result;ssize_t n=read(g_client_pipe,&result,sizeof(result));
    if(n==(ssize_t)sizeof(result)){
      g_client_result=result;g_client_rc=result.rc;g_registration_persisted=(result.rc==0&&astro_pair_credentials_exist());
      close_client_pipe();
      if(g_client_rc==0)set_phase(g_pair_server_complete?"astro_pairing_complete":"astro_client_registered");
      else set_phase("astro_client_registration_failed");
    }else if(n==0)close_client_pipe();
  }
  if(g_client_pid>0){int st=0;pid_t r=waitpid(g_client_pid,&st,WNOHANG);if(r==g_client_pid){g_client_pid=-1;if(g_client_rc==-999){g_client_rc=-215;set_phase("astro_client_exited_without_result");}}}
}

static void refresh_pairing(void)
{
  int rc;
  poll_client_job();
  if(!g_pairing.pairing_active)return;
  rc=astro_remote_pairing_poll(&g_pairing);g_pairing_rc=g_pairing.rc;
  if(rc==1){g_pair_server_complete=1;if(g_client_rc==0&&g_registration_persisted)set_phase("astro_pairing_complete");else set_phase("astro_server_registration_confirmed");}
  else if(rc==-30)set_phase("astro_pairing_timeout");
  else if(rc==-32)set_phase("astro_pairing_rejected");
  else if(rc<0)set_phase("astro_pairing_poll_failed");
}

static void stop_session(void)
{
  g_session_active=0;cancel_client_job();astro_remote_pairing_cancel(&g_pairing);
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

static void send_status(int fd)
{
  char j[3600],saved_account[32];
  int protocol_ready,session_authenticated,pairing_required,pin_ready,account_saved;
  time_t now;long seconds_left;
  refresh_pairing();
  protocol_ready=(g_protocol_rc==0);session_authenticated=(protocol_ready&&g_protocol.http_status==200&&g_protocol.nonce_present);pairing_required=(protocol_ready&&!session_authenticated);
  now=time(NULL);pin_ready=(g_pairing.pin_ready&&g_pairing.expires_at>now);seconds_left=pin_ready?(long)(g_pairing.expires_at-now):0;
  saved_account[0]='\0';account_saved=(astro_pair_account_load(saved_account,sizeof(saved_account))==0);
  snprintf(j,sizeof(j),
    "{\"ok\":true,\"service\":\"astrorem\",\"pid\":%d,\"port\":%d,\"bind\":\"127.0.0.1\",\"role\":\"remoteplay_self_pairing\",\"session_active\":%s,\"phase\":\"%s\","
    "\"source_probe_rc\":%d,\"remoteplay_enabled\":%s,\"remoteplay_tcp_9295\":%s,\"protocol_probe_rc\":%d,\"protocol_ready\":%s,"
    "\"session_authenticated\":%s,\"pairing_required\":%s,\"rp_http_status\":%d,\"rp_nonce_present\":%s,\"rp_application_reason\":%u,\"rp_version\":\"%s\","
    "\"pairing_prepare_rc\":%d,\"pairing_pin_ready\":%s,\"pairing_pin\":%u,\"pairing_seconds_left\":%ld,\"pairing_active\":%s,\"pairing_complete\":%s,\"pairing_stat\":%d,\"pairing_error\":%d,"
    "\"self_pairing_running\":%s,\"self_pairing_rc\":%d,\"self_pairing_http_status\":%d,\"self_pairing_application_reason\":%u,\"account_id_saved\":%s,\"account_id\":\"%s\",\"registration_persisted\":%s,"
    "\"rp_key_type\":%u,\"video_ready\":false,\"control_ready\":false}",
    (int)getpid(),ASTRO_REMOTE_WORKER_PORT,g_session_active?"true":"false",g_phase,g_probe_rc,
    g_probe.remoteplay_enabled?"true":"false",g_probe.tcp_9295_open?"true":"false",g_protocol_rc,protocol_ready?"true":"false",
    session_authenticated?"true":"false",pairing_required?"true":"false",g_protocol.http_status,g_protocol.nonce_present?"true":"false",(unsigned int)g_protocol.application_reason,g_protocol.rp_version,
    g_pairing_rc,pin_ready?"true":"false",g_pairing.pin,seconds_left,g_pairing.pairing_active?"true":"false",g_pair_server_complete?"true":"false",g_pairing.pair_stat,g_pairing.pair_error,
    g_client_pid>0?"true":"false",g_client_rc,g_client_result.http_status,(unsigned int)g_client_result.application_reason,account_saved?"true":"false",account_saved?saved_account:"",g_registration_persisted?"true":"false",
    (unsigned int)g_client_result.rp_key_type);
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
  if(bind(server,(struct sockaddr *)&a,sizeof(a))<0){close(server);return 11;}if(listen(server,4)<0){close(server);return 12;}
  memset(&g_protocol,0,sizeof(g_protocol));memset(&g_pairing,0,sizeof(g_pairing));memset(&g_client_result,0,sizeof(g_client_result));
  g_protocol_rc=-999;g_pairing_rc=-999;g_client_rc=-999;g_registration_persisted=astro_pair_credentials_exist();g_pair_account[0]='\0';
  (void)astro_pair_account_load(g_pair_account,sizeof(g_pair_account));run_safe_probe();set_phase(g_registration_persisted?"astro_already_paired":"idle");

  while(g_worker_running){
    fd_set rfds;struct timeval tv;int rc;
    FD_ZERO(&rfds);FD_SET(server,&rfds);tv.tv_sec=0;tv.tv_usec=250000;rc=select(server+1,&rfds,NULL,NULL,&tv);
    if(rc<0){if(!g_worker_running)break;continue;}if(rc==0){refresh_pairing();continue;}
    if(FD_ISSET(server,&rfds)){
      int c=accept(server,NULL,NULL);char buf[4096];int n;
      if(c<0)continue;memset(buf,0,sizeof(buf));n=recv(c,buf,sizeof(buf)-1,0);
      if(n>0){
        char account_id[64];buf[n]='\0';account_id[0]='\0';
        if(strstr(buf,"GET / "))send_html(c,remote_page_v137);
        else if(strstr(buf,"GET /health "))send_json(c,"200 OK","{\"ok\":true,\"service\":\"astrorem\",\"role\":\"remoteplay_self_pairing\"}");
        else if(strstr(buf,"GET /status "))send_status(c);
        else if(strstr(buf,"POST /session/start ")){int pr=run_safe_probe();if(pr!=0){g_session_active=0;g_protocol_rc=-999;memset(&g_protocol,0,sizeof(g_protocol));}else{g_session_active=1;run_protocol_probe();}send_status(c);}
        else if(strstr(buf,"POST /session/stop ")){stop_session();send_status(c);}
        else if(strstr(buf,"POST /probe ")){run_safe_probe();send_status(c);}
        else if(strstr(buf,"POST /protocol/probe ")){if(run_safe_probe()==0)run_protocol_probe();send_status(c);}
        else if(strstr(buf,"POST /pairing/prepare ")){prepare_native_pairing();send_status(c);}
        else if(strstr(buf,"POST /pairing/self ")){
          int sr;if(!form_value(buf,"account_id",account_id,sizeof(account_id)))astro_pair_account_load(account_id,sizeof(account_id));sr=start_self_pairing(account_id);
          if(sr==0)send_status(c);else{char x[192];snprintf(x,sizeof(x),"{\"ok\":false,\"error\":\"self_pairing_start_failed\",\"rc\":%d}",sr);send_json(c,"400 Bad Request",x);}
        }
        else if(strstr(buf,"POST /pairing/cancel ")){cancel_client_job();astro_remote_pairing_cancel(&g_pairing);set_phase("pairing_cancelled");send_status(c);}
        else if(strstr(buf,"POST /remoteplay/enable ")){int erc=astro_remote_ps5_source_enable_remoteplay();if(erc==0){run_safe_probe();set_phase("remoteplay_enabled");send_status(c);}else{char x[192];snprintf(x,sizeof(x),"{\"ok\":false,\"error\":\"remoteplay_enable_failed\",\"rc\":%d}",erc);send_json(c,"500 Internal Server Error",x);}}
        else if(strstr(buf,"POST /shutdown ")){send_json(c,"200 OK","{\"ok\":true,\"stopping\":true}");g_worker_running=0;}
        else if(strstr(buf,"/account/"))send_json(c,"410 Gone","{\"ok\":false,\"error\":\"account_activation_is_astrolock_only\"}");
        else send_json(c,"404 Not Found","{\"ok\":false,\"error\":\"not_found\"}");
      }
      close(c);
    }
  }
  cancel_client_job();close(server);return 0;
}
