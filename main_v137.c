#define main astro_v136_legacy_main
#include "main_v136.c"
#undef main
#include "remote_page_v137.h"
#include "astro_lock.h"

static pid_t g_astrolock_pid=-1;

static int ensure_astrolock_v137(void)
{
  int elapsed=0;
  if(tcp_open(ASTRO_LOCK_PORT))return 0;
  if(g_astrolock_pid>0){
    int st=0;pid_t r=waitpid(g_astrolock_pid,&st,WNOHANG);
    if(r==0)return -2;
    g_astrolock_pid=-1;
  }
  g_astrolock_pid=fork();
  if(g_astrolock_pid<0)return -3;
  if(g_astrolock_pid==0){
    int fd,rc;
    for(fd=3;fd<1024;fd++)close(fd);
    rc=astro_lock_main();
    _exit(rc);
  }
  while(elapsed<2000){
    if(tcp_open(ASTRO_LOCK_PORT))return 1;
    usleep(100000);elapsed+=100;
  }
  return -4;
}

static void request_astrolock_stop_v137(void)
{
  if(g_astrolock_pid>0)kill(g_astrolock_pid,SIGTERM);
}

static void send_remote_status_v137(int fd)
{
  astro_remote_state_t s;
  char j[1600];
  time_t now=time(NULL);
  long session_up=0;

  astro_remote_service_snapshot(&s);
  if(s.session_active&&s.session_started_at>0&&now>=s.session_started_at)
    session_up=(long)(now-s.session_started_at);

  snprintf(j,sizeof(j),
    "{\"ok\":true,\"service_online\":%s,\"session_active\":%s,"
    "\"video_ready\":%s,\"control_ready\":%s,\"generation\":%lu,"
    "\"phase\":\"%s\",\"session_uptime_seconds\":%ld,"
    "\"architecture\":\"split_process_v2\",\"external_port\":45821,"
    "\"internal_bind\":\"127.0.0.1\",\"internal_port\":%d,"
    "\"worker_pid\":%d,\"worker_ppid\":%d,\"worker_port_open\":%s,"
    "\"worker_stuck\":%s,\"worker_stat\":%d,\"worker_start_errno\":%d,"
    "\"worker_exit_status\":%d,\"worker_wmesg\":\"%s\","
    "\"worker_lockname\":\"%s\",\"source_probe_rc\":%d,"
    "\"remoteplay_enabled\":%s,\"remoteplay_tcp_9295\":%s,"
    "\"lock_port\":45823,\"lock_online\":%s,\"lock_pid\":%d,"
    "\"transport\":\"internal_localhost\",\"external_bridge\":false}",
    s.service_online?"true":"false",s.session_active?"true":"false",
    s.video_ready?"true":"false",s.control_ready?"true":"false",
    s.generation,s.phase,session_up,s.worker_port,s.worker_pid,s.worker_ppid,
    s.worker_port_open?"true":"false",s.worker_stuck?"true":"false",
    s.worker_stat,s.worker_start_errno,s.worker_exit_status,
    s.worker_wmesg,s.worker_lockname,s.source_probe_rc,
    s.remoteplay_enabled?"true":"false",s.remoteplay_tcp_9295?"true":"false",
    tcp_open(ASTRO_LOCK_PORT)?"true":"false",(int)g_astrolock_pid);
  send_json(fd,"200 OK",j);
}

static void send_remote_action_v137(int fd,int start)
{
  int rc=start?astro_remote_service_start():astro_remote_service_stop();
  if(rc<0){
    astro_remote_state_t s;char j[512];astro_remote_service_snapshot(&s);
    snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"remote_worker_action_failed\",\"rc\":%d,\"phase\":\"%s\",\"worker_pid\":%d,\"worker_stuck\":%s,\"worker_start_errno\":%d}",rc,s.phase,s.worker_pid,s.worker_stuck?"true":"false",s.worker_start_errno);
    send_json(fd,"503 Service Unavailable",j);return;
  }
  send_remote_status_v137(fd);
}

static void send_remote_enable_v137(int fd)
{
  int rc=astro_remote_service_enable_remoteplay();
  if(rc<0){astro_remote_state_t s;char j[512];astro_remote_service_snapshot(&s);snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"remoteplay_enable_failed\",\"rc\":%d,\"phase\":\"%s\",\"worker_pid\":%d}",rc,s.phase,s.worker_pid);send_json(fd,"503 Service Unavailable",j);return;}
  send_remote_status_v137(fd);
}

static void send_remote_restart_v137(int fd)
{
  int rc=astro_remote_service_shutdown_worker();
  if(rc>=0)rc=astro_remote_service_ensure_worker();
  if(rc<0){astro_remote_state_t s;char j[512];astro_remote_service_snapshot(&s);snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"remote_worker_restart_failed\",\"rc\":%d,\"phase\":\"%s\",\"worker_pid\":%d,\"worker_stuck\":%s,\"worker_stat\":%d,\"worker_wmesg\":\"%s\",\"worker_lockname\":\"%s\"}",rc,s.phase,s.worker_pid,s.worker_stuck?"true":"false",s.worker_stat,s.worker_wmesg,s.worker_lockname);send_json(fd,"503 Service Unavailable",j);return;}
  send_remote_status_v137(fd);
}

static void send_api_services_v137(int fd)
{
  char pr[ASTRO_HTTP_BUF],json[24576],names[64][128];size_t used=0;int visible=0,i,pc=0;int ok=http_get_local_v134(PROSPERO_PORT,"/api/processes",pr,sizeof(pr))==0;
  used+=(size_t)snprintf(json+used,sizeof(json)-used,"{\"ok\":true,\"manager_online\":%s,\"source\":\"ProsperoMgr + Astro managed services\",\"items\":[",ok?"true":"false");
  if(ok){pc=extract_processes(pr,names,64);for(i=0;i<pc;i++){astro_service_t *s=by_process(names[i]);if(is_hidden(names[i]))continue;if(!strcasecmp(names[i],"payload.elf"))continue;if(!strcasecmp(names[i],"astrorem"))continue;if(!strcasecmp(names[i],"astrolock"))continue;if(s&&s->port>0){int active=tcp_open(s->port);used+=(size_t)snprintf(json+used,sizeof(json)-used,"%s{\"id\":\"%s\",\"name\":\"%s\",\"process\":\"%s\",\"port\":%d,\"known\":true,\"active\":%s,\"url\":\"/service/%s/\"}",visible?",":"",s->id,s->name,s->process,s->port,active?"true":"false",s->id);visible++;}else if(!s){char id[48];slugify(names[i],id,sizeof(id));used+=(size_t)snprintf(json+used,sizeof(json)-used,"%s{\"id\":\"%s\",\"name\":\"Novo serviço detectado\",\"process\":\"%s\",\"port\":0,\"known\":false,\"active\":true}",visible?",":"",id,names[i]);visible++;}}}
  {
    astro_service_t *r=by_process("astrorem");if(r&&!is_hidden("astrorem")){int active=tcp_open(r->port);used+=(size_t)snprintf(json+used,sizeof(json)-used,"%s{\"id\":\"%s\",\"name\":\"%s\",\"process\":\"%s\",\"port\":%d,\"known\":true,\"active\":%s,\"managed\":true,\"url\":\"/service/%s/\"}",visible?",":"",r->id,r->name,r->process,r->port,active?"true":"false",r->id);visible++;}
    astro_service_t *l=by_process("astrolock");if(l&&!is_hidden("astrolock")){int active=tcp_open(l->port);used+=(size_t)snprintf(json+used,sizeof(json)-used,"%s{\"id\":\"%s\",\"name\":\"%s\",\"process\":\"%s\",\"port\":%d,\"known\":true,\"active\":%s,\"managed\":true,\"url\":\"/service/%s/\"}",visible?",":"",l->id,l->name,l->process,l->port,active?"true":"false",l->id);visible++;}
  }
  used+=(size_t)snprintf(json+used,sizeof(json)-used,"],\"count\":%d,\"hidden\":[",visible);for(i=0;i<hidden_count;i++)used+=(size_t)snprintf(json+used,sizeof(json)-used,"%s{\"process\":\"%s\"}",i?",":"",hidden[i]);snprintf(json+used,sizeof(json)-used,"],\"hidden_count\":%d}",hidden_count);send_json(fd,"200 OK",json);
}

static void redirect_remote_service_v137(int fd){send_response(fd,"302 Found","Location: /service/remote/\r\nCache-Control: no-store\r\n","");}

int main(void)
{
  int server,client,opt=1;struct sockaddr_in a;char buf[8192];
  running=1;astro_started_at=time(NULL);load_registry();add_service("remote","Astro Remote","astrorem",45822);add_service("lock","Astro Lock","astrolock",45823);load_hidden();astro_remote_service_init();signal(SIGPIPE,SIG_IGN);
  server=socket(AF_INET,SOCK_STREAM,0);if(server<0)return 1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=INADDR_ANY;a.sin_port=htons(ASTRO_PORT);if(bind(server,(struct sockaddr*)&a,sizeof(a))<0){close(server);return 1;}if(listen(server,8)<0){close(server);return 1;}
  notify("ASTRO Remote v0.16 - Remote + Lock split architecture");
  ensure_astrolock_v137();astro_remote_service_ensure_worker();

  while(running){
    int total;char method[16],path[ASTRO_PROXY_PATH_MAX],ver[16];char *he;const astro_service_t *svc;const char *backend=NULL;
    client=accept(server,NULL,NULL);if(client<0)continue;astro_set_io_timeout(client);memset(buf,0,sizeof(buf));total=recv(client,buf,sizeof(buf)-1,0);if(total<=0){close(client);continue;}buf[total]='\0';
    he=strstr(buf,"\r\n\r\n");if(he){int cl=0;char *p=strstr(buf,"Content-Length:");if(p)cl=atoi(p+15);{int hs=(int)((he+4)-buf),br=total-hs;while(br<cl&&total<(int)sizeof(buf)-1){int rr=recv(client,buf+total,sizeof(buf)-total-1,0);if(rr<=0)break;total+=rr;br+=rr;buf[total]='\0';}}}
    method[0]=path[0]=ver[0]='\0';sscanf(buf,"%15s %2047s %15s",method,path,ver);
    if(!strcmp(path,"/favicon.ico")){send_no_content_v132(client);close(client);continue;}
    if((!strcmp(path,"/logout")||!strncmp(path,"/logout?",8))&&logged(buf)){send_logout_v13(client);close(client);continue;}
    if((!strcmp(path,"/remote")||!strcmp(path,"/screen")||!strncmp(path,"/remote?",8)||!strncmp(path,"/screen?",8))&&logged(buf)){redirect_remote_service_v137(client);close(client);continue;}
    svc=service_from_path(path,&backend);if(svc&&logged(buf)){char mark[80];snprintf(mark,sizeof(mark),"/service/%s/_root",svc->id);if(!strncmp(path,mark,strlen(mark))){const char *rest=path+strlen(mark);char root[ASTRO_PROXY_PATH_MAX];snprintf(root,sizeof(root),"/%s",*rest=='/'?rest+1:rest);proxy_request_v134(client,buf,total,svc,root);}else proxy_redirect(client,svc);close(client);continue;}
    if(strstr(buf,"POST /login ")&&valid_login(buf))send_response(client,"302 Found","Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n","");
    else if(strstr(buf,"GET /api/status ")&&logged(buf))send_status_v13(client);
    else if(strstr(buf,"GET /api/services ")&&logged(buf))send_api_services_v137(client);
    else if(strstr(buf,"POST /api/services/register ")&&logged(buf))send_register(client,buf);
    else if(strstr(buf,"POST /api/services/update ")&&logged(buf))send_update_service_v13(client,buf);
    else if(strstr(buf,"POST /api/services/hide ")&&logged(buf))send_hide(client,buf,0);
    else if(strstr(buf,"POST /api/services/unhide ")&&logged(buf))send_hide(client,buf,1);
    else if(strstr(buf,"GET /api/remote/status ")&&logged(buf))send_remote_status_v137(client);
    else if(strstr(buf,"POST /api/remote/start ")&&logged(buf))send_remote_action_v137(client,1);
    else if(strstr(buf,"POST /api/remote/stop ")&&logged(buf))send_remote_action_v137(client,0);
    else if(strstr(buf,"POST /api/remote/enable ")&&logged(buf))send_remote_enable_v137(client);
    else if(strstr(buf,"POST /api/remote/restart ")&&logged(buf))send_remote_restart_v137(client);
    else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){astro_remote_service_shutdown_worker();request_astrolock_stop_v137();send_response(client,"200 OK",NULL,shutdown_page);running=0;}
    else if(strstr(buf,"GET / ")&&logged(buf))send_dashboard_v136(client);
    else if(logged(buf)&&(svc=service_from_cookie(buf))!=NULL)proxy_request_v134(client,buf,total,svc,path);
    else send_response(client,"200 OK",NULL,login_page);
    close(client);
  }
  astro_remote_service_shutdown_worker();request_astrolock_stop_v137();close(server);notify("ASTRO Remote encerrado");return 0;
}
