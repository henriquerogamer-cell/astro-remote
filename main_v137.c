#define main astro_v136_legacy_main
#include "main_v136.c"
#undef main

static void send_remote_status_v137(int fd)
{
  astro_remote_state_t s;
  char j[1400];
  time_t now=time(NULL);
  long session_up=0;

  astro_remote_service_snapshot(&s);
  if(s.session_active&&s.session_started_at>0&&now>=s.session_started_at)
    session_up=(long)(now-s.session_started_at);

  snprintf(j,sizeof(j),
    "{\"ok\":true,\"service_online\":%s,\"session_active\":%s,"
    "\"video_ready\":%s,\"control_ready\":%s,\"generation\":%lu,"
    "\"phase\":\"%s\",\"session_uptime_seconds\":%ld,"
    "\"architecture\":\"split_process\",\"external_port\":45821,"
    "\"internal_bind\":\"127.0.0.1\",\"internal_port\":%d,"
    "\"worker_pid\":%d,\"worker_ppid\":%d,\"worker_port_open\":%s,"
    "\"worker_stuck\":%s,\"worker_stat\":%d,\"worker_start_errno\":%d,"
    "\"worker_exit_status\":%d,\"worker_wmesg\":\"%s\","
    "\"worker_lockname\":\"%s\",\"source_probe_rc\":%d,"
    "\"remoteplay_enabled\":%s,\"remoteplay_tcp_9295\":%s,"
    "\"transport\":\"internal_localhost\",\"external_bridge\":false}",
    s.service_online?"true":"false",
    s.session_active?"true":"false",
    s.video_ready?"true":"false",
    s.control_ready?"true":"false",
    s.generation,s.phase,session_up,s.worker_port,s.worker_pid,s.worker_ppid,
    s.worker_port_open?"true":"false",
    s.worker_stuck?"true":"false",
    s.worker_stat,s.worker_start_errno,s.worker_exit_status,
    s.worker_wmesg,s.worker_lockname,s.source_probe_rc,
    s.remoteplay_enabled?"true":"false",
    s.remoteplay_tcp_9295?"true":"false");
  send_json(fd,"200 OK",j);
}

static void send_remote_action_v137(int fd,int start)
{
  int rc=start?astro_remote_service_start():astro_remote_service_stop();
  if(rc<0){
    astro_remote_state_t s;
    char j[512];
    astro_remote_service_snapshot(&s);
    snprintf(j,sizeof(j),
      "{\"ok\":false,\"error\":\"remote_worker_action_failed\","
      "\"rc\":%d,\"phase\":\"%s\",\"worker_pid\":%d,"
      "\"worker_stuck\":%s,\"worker_start_errno\":%d}",
      rc,s.phase,s.worker_pid,s.worker_stuck?"true":"false",s.worker_start_errno);
    send_json(fd,"503 Service Unavailable",j);
    return;
  }
  send_remote_status_v137(fd);
}

int main(void)
{
  int server,client,opt=1;
  struct sockaddr_in a;
  char buf[8192];

  running=1;
  astro_started_at=time(NULL);
  load_registry();
  load_hidden();
  astro_remote_service_init();
  signal(SIGPIPE,SIG_IGN);

  server=socket(AF_INET,SOCK_STREAM,0);
  if(server<0)return 1;
  setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_addr.s_addr=INADDR_ANY;
  a.sin_port=htons(ASTRO_PORT);
  if(bind(server,(struct sockaddr*)&a,sizeof(a))<0){close(server);return 1;}
  if(listen(server,8)<0){close(server);return 1;}
  notify("ASTRO Remote v0.15.0 split service - 45821 + localhost 45822");

  while(running){
    int total;
    char method[16],path[ASTRO_PROXY_PATH_MAX],ver[16];
    char *he;
    const astro_service_t *svc;
    const char *backend=NULL;

    client=accept(server,NULL,NULL);
    if(client<0)continue;
    astro_set_io_timeout(client);
    memset(buf,0,sizeof(buf));
    total=recv(client,buf,sizeof(buf)-1,0);
    if(total<=0){close(client);continue;}
    buf[total]='\0';

    he=strstr(buf,"\r\n\r\n");
    if(he){
      int cl=0;
      char *p=strstr(buf,"Content-Length:");
      if(p)cl=atoi(p+15);
      {
        int hs=(int)((he+4)-buf),br=total-hs;
        while(br<cl&&total<(int)sizeof(buf)-1){
          int rr=recv(client,buf+total,sizeof(buf)-total-1,0);
          if(rr<=0)break;
          total+=rr;br+=rr;buf[total]='\0';
        }
      }
    }

    method[0]=path[0]=ver[0]='\0';
    sscanf(buf,"%15s %2047s %15s",method,path,ver);

    if(!strcmp(path,"/favicon.ico")){send_no_content_v132(client);close(client);continue;}
    if((!strcmp(path,"/logout")||!strncmp(path,"/logout?",8))&&logged(buf)){send_logout_v13(client);close(client);continue;}
    if((!strcmp(path,"/remote")||!strcmp(path,"/screen")||!strncmp(path,"/remote?",8)||!strncmp(path,"/screen?",8))&&logged(buf)){send_response(client,"200 OK","Cache-Control: no-store\r\n",remote_page_v136);close(client);continue;}

    svc=service_from_path(path,&backend);
    if(svc&&logged(buf)){
      char mark[80];
      snprintf(mark,sizeof(mark),"/service/%s/_root",svc->id);
      if(!strncmp(path,mark,strlen(mark))){
        const char *rest=path+strlen(mark);
        char root[ASTRO_PROXY_PATH_MAX];
        snprintf(root,sizeof(root),"/%s",*rest=='/'?rest+1:rest);
        proxy_request_v134(client,buf,total,svc,root);
      }else proxy_redirect(client,svc);
      close(client);continue;
    }

    if(strstr(buf,"POST /login ")&&valid_login(buf))send_response(client,"302 Found","Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n","");
    else if(strstr(buf,"GET /api/status ")&&logged(buf))send_status_v13(client);
    else if(strstr(buf,"GET /api/services ")&&logged(buf))send_api_services_v134(client);
    else if(strstr(buf,"POST /api/services/register ")&&logged(buf))send_register(client,buf);
    else if(strstr(buf,"POST /api/services/update ")&&logged(buf))send_update_service_v13(client,buf);
    else if(strstr(buf,"POST /api/services/hide ")&&logged(buf))send_hide(client,buf,0);
    else if(strstr(buf,"POST /api/services/unhide ")&&logged(buf))send_hide(client,buf,1);
    else if(strstr(buf,"GET /api/remote/status ")&&logged(buf))send_remote_status_v137(client);
    else if(strstr(buf,"POST /api/remote/start ")&&logged(buf))send_remote_action_v137(client,1);
    else if(strstr(buf,"POST /api/remote/stop ")&&logged(buf))send_remote_action_v137(client,0);
    else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){
      astro_remote_service_stop();
      send_response(client,"200 OK",NULL,shutdown_page);
      running=0;
    }
    else if(strstr(buf,"GET / ")&&logged(buf))send_dashboard_v136(client);
    else if(logged(buf)&&(svc=service_from_cookie(buf))!=NULL)proxy_request_v134(client,buf,total,svc,path);
    else send_response(client,"200 OK",NULL,login_page);
    close(client);
  }

  astro_remote_service_stop();
  close(server);
  notify("ASTRO Remote encerrado");
  return 0;
}
