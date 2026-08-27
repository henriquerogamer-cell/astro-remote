#define ASTRO_LOCK_LOCAL_DPRINTF
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "astro_lock.h"
#include "astro_offact.h"
#include "astro_avatar.h"

static volatile sig_atomic_t g_running=1;
static volatile sig_atomic_t g_shutdown_requested=0;
static pid_t g_job_pid=-1;
static int g_job_kind=0;
static int g_job_log_fd=-1;
static int g_job_result_fd=-1;
static int g_job_done=0;
static int g_job_exit=0;
static int g_job_rc=-999;
static astro_account_state_t g_account;
static int g_account_valid=0;
static char g_log[16384];
static size_t g_log_len=0;

static void sig_handler(int sig){(void)sig;g_shutdown_requested=1;}

static void append_log(const char *s,size_t n)
{
    if(!s||!n)return;
    if(n>=sizeof(g_log)-1){s+=n-(sizeof(g_log)-1);n=sizeof(g_log)-1;g_log_len=0;}
    if(g_log_len+n>=sizeof(g_log)){
        size_t drop=g_log_len+n-(sizeof(g_log)-1);
        memmove(g_log,g_log+drop,g_log_len-drop);
        g_log_len-=drop;
    }
    memcpy(g_log+g_log_len,s,n);g_log_len+=n;g_log[g_log_len]='\0';
}
static void log_line(const char *s){append_log(s,strlen(s));append_log("\n",1);}

static void json_safe(char *dst,size_t cap,const char *src)
{
    size_t j=0;if(!dst||cap==0)return;if(!src){dst[0]='\0';return;}
    for(size_t i=0;src[i]&&j+1<cap;i++){
        unsigned char c=(unsigned char)src[i];
        if(c=='"'||c=='\\'){if(j+2>=cap)break;dst[j++]='\\';dst[j++]=(char)c;}
        else if(c=='\n'){if(j+2>=cap)break;dst[j++]='\\';dst[j++]='n';}
        else if(c=='\r'){if(j+2>=cap)break;dst[j++]='\\';dst[j++]='r';}
        else if(c>=0x20&&c<0x7f)dst[j++]=(char)c;else dst[j++]='?';
    }dst[j]='\0';
}

static void send_json(int fd,const char *status,const char *body)
{
    char h[512];size_t n=strlen(body);
    snprintf(h,sizeof(h),"HTTP/1.1 %s\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",status,n);
    send(fd,h,strlen(h),0);send(fd,body,n,0);
}

static void send_bytes(int fd,const char *status,const char *type,const void *body,size_t len)
{
    char h[512];const unsigned char *p=(const unsigned char *)body;size_t left=len;
    snprintf(h,sizeof(h),"HTTP/1.1 %s\r\nContent-Type: %s\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",status,type,len);
    send(fd,h,strlen(h),0);
    while(left){ssize_t n=send(fd,p,left,0);if(n<=0)break;p+=n;left-=(size_t)n;}
}

typedef struct lock_result {int rc;astro_account_state_t state;} lock_result_t;

static void poll_job(void)
{
    if(g_job_log_fd>=0){for(;;){char b[768];ssize_t n=read(g_job_log_fd,b,sizeof(b));if(n>0){append_log(b,(size_t)n);continue;}if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK))break;if(n==0){close(g_job_log_fd);g_job_log_fd=-1;}break;}}
    if(g_job_result_fd>=0){lock_result_t r;ssize_t n=read(g_job_result_fd,&r,sizeof(r));if(n==(ssize_t)sizeof(r)){g_job_rc=r.rc;g_account=r.state;g_account_valid=1;close(g_job_result_fd);g_job_result_fd=-1;}else if(n==0){close(g_job_result_fd);g_job_result_fd=-1;}}
    if(g_job_pid>0){int st=0;pid_t r=waitpid(g_job_pid,&st,WNOHANG);if(r==g_job_pid){char b[128];g_job_exit=st;g_job_done=1;snprintf(b,sizeof(b),"[LOCK] JOB EXIT pid=%d status=%d rc=%d",(int)g_job_pid,st,g_job_rc);log_line(b);g_job_pid=-1;}}
    if(g_shutdown_requested&&g_job_pid<=0)g_running=0;
}

static int start_job(int kind)
{
    int lp[2],rp[2];pid_t pid;if(g_job_pid>0)return -10;if(pipe(lp)!=0)return -11;if(pipe(rp)!=0){close(lp[0]);close(lp[1]);return -12;}
    g_log_len=0;g_log[0]='\0';g_job_done=0;g_job_exit=0;g_job_rc=-999;g_job_kind=kind;g_account_valid=0;memset(&g_account,0,sizeof(g_account));
    log_line(kind==2?"[LOCK] START activation job":"[LOCK] START account check job");
    pid=fork();if(pid<0){close(lp[0]);close(lp[1]);close(rp[0]);close(rp[1]);return -13;}
    if(pid==0){lock_result_t out;close(lp[0]);close(rp[0]);memset(&out,0,sizeof(out));syscall(SYS_thr_set_name,-1,kind==2?"astrolock-act":"astrolock-chk");astro_account_set_debug_fd(lp[1]);dprintf(lp[1],"[LOCK] PID %d kind=%s\n",(int)getpid(),kind==2?"activate":"check");
        if(kind==2){dprintf(lp[1],"[LOCK] STEP activate: account_id -> type=np -> flags -> verify\n");out.rc=astro_account_fake_activate_direct(&out.state);}else{dprintf(lp[1],"[LOCK] STEP check: detect foreground account\n");out.rc=astro_account_get_current_direct(&out.state);}
        dprintf(lp[1],"[LOCK] RESULT rc=%d user=%d slot=%d name='%s' account=0x%016llx type='%s' flags=%d activated=%d\n",out.rc,out.state.foreground_user,out.state.registry_index,out.state.account_name,(unsigned long long)out.state.account_id,out.state.account_type,out.state.account_flags,out.state.activated);
        write(rp[1],&out,sizeof(out));close(lp[1]);close(rp[1]);_exit(out.rc==0?0:1);}
    close(lp[1]);close(rp[1]);g_job_log_fd=lp[0];g_job_result_fd=rp[0];g_job_pid=pid;fcntl(g_job_log_fd,F_SETFL,fcntl(g_job_log_fd,F_GETFL,0)|O_NONBLOCK);fcntl(g_job_result_fd,F_SETFL,fcntl(g_job_result_fd,F_GETFL,0)|O_NONBLOCK);return 1;
}

static void send_status(int fd)
{
    char safe_log[32768],safe_name[128],safe_type[64],j[36000];poll_job();json_safe(safe_log,sizeof(safe_log),g_log);json_safe(safe_name,sizeof(safe_name),g_account.account_name);json_safe(safe_type,sizeof(safe_type),g_account.account_type);
    snprintf(j,sizeof(j),"{\"ok\":true,\"service\":\"astrolock\",\"pid\":%d,\"port\":%d,\"shutdown_requested\":%s,\"job_running\":%s,\"job_pid\":%d,\"job_kind\":%d,\"job_done\":%s,\"job_rc\":%d,\"account_valid\":%s,\"account_rc\":%d,\"account_name\":\"%s\",\"account_user\":%d,\"account_slot\":%d,\"account_id_hex\":\"0x%016llx\",\"account_proposed_id_hex\":\"0x%016llx\",\"account_type\":\"%s\",\"account_flags\":%d,\"account_activated\":%s,\"avatar_url\":\"/service/lock/_root/avatar\",\"log\":\"%s\"}",(int)getpid(),ASTRO_LOCK_PORT,g_shutdown_requested?"true":"false",g_job_pid>0?"true":"false",(int)g_job_pid,g_job_kind,g_job_done?"true":"false",g_job_rc,g_account_valid?"true":"false",g_account.rc,safe_name,g_account.foreground_user,g_account.registry_index,(unsigned long long)g_account.account_id,(unsigned long long)g_account.proposed_account_id,safe_type,g_account.account_flags,g_account.activated?"true":"false",safe_log);send_json(fd,"200 OK",j);
}

static void send_avatar(int fd)
{
    unsigned char *bmp=NULL;size_t bmp_len=0;char svg[1024];char initial='A';poll_job();
    if(g_account_valid&&astro_avatar_load_bmp(g_account.foreground_user,&bmp,&bmp_len)==0&&bmp){send_bytes(fd,"200 OK","image/bmp",bmp,bmp_len);free(bmp);return;}
    if(g_account_valid&&g_account.account_name[0]){unsigned char c=(unsigned char)g_account.account_name[0];if(c>='a'&&c<='z')c=(unsigned char)(c-'a'+'A');if((c>='A'&&c<='Z')||(c>='0'&&c<='9'))initial=(char)c;}
    snprintf(svg,sizeof(svg),"<svg xmlns='http://www.w3.org/2000/svg' width='128' height='128' viewBox='0 0 128 128'><defs><radialGradient id='g'><stop stop-color='#28465a'/><stop offset='1' stop-color='#080c12'/></radialGradient></defs><rect width='128' height='128' rx='64' fill='url(#g)'/><circle cx='64' cy='64' r='58' fill='none' stroke='#b9e4e7' stroke-width='3'/><text x='64' y='81' text-anchor='middle' font-family='Arial' font-size='54' font-weight='700' fill='#effbfc'>%c</text></svg>",initial);
    send_bytes(fd,"200 OK","image/svg+xml; charset=utf-8",svg,strlen(svg));
}

int astro_lock_main(void)
{
    int server,opt=1;struct sockaddr_in a;syscall(SYS_thr_set_name,-1,"astrolock");signal(SIGTERM,sig_handler);signal(SIGINT,sig_handler);signal(SIGPIPE,SIG_IGN);server=socket(AF_INET,SOCK_STREAM,0);if(server<0)return 20;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);a.sin_port=htons(ASTRO_LOCK_PORT);if(bind(server,(struct sockaddr *)&a,sizeof(a))<0){close(server);return 21;}if(listen(server,4)<0){close(server);return 22;}log_line("[LOCK] service online on 127.0.0.1:45823");
    (void)start_job(1);
    while(g_running){fd_set rfds;struct timeval tv;int rc;FD_ZERO(&rfds);FD_SET(server,&rfds);tv.tv_sec=0;tv.tv_usec=250000;rc=select(server+1,&rfds,NULL,NULL,&tv);poll_job();if(rc<0){if(!g_running)break;continue;}if(rc==0)continue;if(FD_ISSET(server,&rfds)){int c=accept(server,NULL,NULL);char buf[2048];int n;if(c<0)continue;memset(buf,0,sizeof(buf));n=recv(c,buf,sizeof(buf)-1,0);if(n>0){buf[n]='\0';if(strstr(buf,"GET /health "))send_json(c,"200 OK","{\"ok\":true,\"service\":\"astrolock\"}");else if(strstr(buf,"GET /status "))send_status(c);else if(strstr(buf,"GET /avatar "))send_avatar(c);else if(strstr(buf,"POST /check ")){int r=start_job(1);if(r<0){char j[128];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"job_busy\",\"rc\":%d}",r);send_json(c,"409 Conflict",j);}else send_status(c);}else if(strstr(buf,"POST /activate ")){int r=start_job(2);if(r<0){char j[128];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"job_busy\",\"rc\":%d}",r);send_json(c,"409 Conflict",j);}else send_status(c);}else if(strstr(buf,"POST /shutdown ")){g_shutdown_requested=1;if(g_job_pid>0)send_json(c,"202 Accepted","{\"ok\":true,\"deferred\":true,\"reason\":\"job_running\"}");else{send_json(c,"200 OK","{\"ok\":true,\"stopping\":true}");g_running=0;}}else send_json(c,"404 Not Found","{\"ok\":false,\"error\":\"not_found\"}");}close(c);}}
    if(g_job_log_fd>=0)close(g_job_log_fd);if(g_job_result_fd>=0)close(g_job_result_fd);close(server);return 0;
}
