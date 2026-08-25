#define main astro_v12_legacy_main
#define send_api_services send_api_services_v12
#define send_status send_status_v12
#define send_dashboard send_dashboard_v12
#include "main_v12.c"
#undef send_dashboard
#undef send_status
#undef send_api_services
#undef main

static void send_api_services_v13(int fd)
{
  char pr[ASTRO_HTTP_BUF],json[24576],names[64][128];
  size_t used=0;
  int visible=0,i,pc=0;
  int ok=http_get_local(PROSPERO_PORT,"/api/processes",pr,sizeof(pr))==0;

  used+=(size_t)snprintf(json+used,sizeof(json)-used,
    "{\"ok\":true,\"manager_online\":%s,\"source\":\"ProsperoMgr /api/processes\",\"items\":[",
    ok?"true":"false");

  if(ok){
    pc=extract_processes(pr,names,64);
    for(i=0;i<pc;i++){
      astro_service_t *s=by_process(names[i]);
      if(is_hidden(names[i]))continue;
      if(!strcasecmp(names[i],"payload.elf"))continue;

      if(s&&s->port>0){
        int active=tcp_open(s->port);
        used+=(size_t)snprintf(json+used,sizeof(json)-used,
          "%s{\"id\":\"%s\",\"name\":\"%s\",\"process\":\"%s\",\"port\":%d,\"known\":true,\"active\":%s,\"url\":\"/service/%s/\"}",
          visible?",":"",s->id,s->name,s->process,s->port,active?"true":"false",s->id);
        visible++;
      }else if(!s){
        char id[48];
        slugify(names[i],id,sizeof(id));
        used+=(size_t)snprintf(json+used,sizeof(json)-used,
          "%s{\"id\":\"%s\",\"name\":\"Novo serviço detectado\",\"process\":\"%s\",\"port\":0,\"known\":false,\"active\":true}",
          visible?",":"",id,names[i]);
        visible++;
      }
    }
  }

  used+=(size_t)snprintf(json+used,sizeof(json)-used,
    "],\"count\":%d,\"hidden\":[",visible);
  for(i=0;i<hidden_count;i++)
    used+=(size_t)snprintf(json+used,sizeof(json)-used,
      "%s{\"process\":\"%s\"}",i?",":"",hidden[i]);
  snprintf(json+used,sizeof(json)-used,
    "],\"hidden_count\":%d}",hidden_count);
  send_json(fd,"200 OK",json);
}

static void send_status_v13(int fd)
{
  char j[512];
  time_t now=time(NULL);
  long up=now>=astro_started_at?(long)(now-astro_started_at):0;
  snprintf(j,sizeof(j),
    "{\"ok\":true,\"console\":\"PlayStation 5\",\"console_online\":true,\"astro_online\":true,\"http_port\":%d,\"version\":\"0.13-registry-fix\",\"uptime_seconds\":%ld,\"remote\":\"standby\",\"prospero_online\":%s}",
    ASTRO_PORT,up,tcp_open(PROSPERO_PORT)?"true":"false");
  send_json(fd,"200 OK",j);
}

static const char *dash_patch_v13=
"<style>.service-state.offline{background:#241d12;border-color:#554326;color:#e6bc73}.service-open:disabled{opacity:.45;cursor:not-allowed;background:#171d25;border-color:#303947;color:#8f9aaa}</style>"
"<script>"
"card=function(x){const c=document.createElement('div');c.className='service-card';if(x.known){const on=!!x.active;c.innerHTML=\"<div class='service-top'><h4>\"+esc(x.name)+\"</h4><span class='service-state \"+(on?'':'offline')+\"'>\"+(on?'ATIVO':'CONFIGURADO')+\"</span></div><div class='service-process'>\"+esc(x.process)+\"</div><span class='service-chip'>INTERNO · 127.0.0.1:\"+x.port+\"</span><button class='service-open' \"+(on?'':'disabled')+\">\"+(on?'Abrir dentro do Astro':'Serviço web offline')+\"</button>\";if(on)c.querySelector('.service-open').onclick=()=>location.href=x.url}else{c.innerHTML=\"<div class='service-top'><h4>Novo serviço detectado</h4><span class='service-state'>NOVO</span></div><div class='service-process'>\"+esc(x.process)+\"</div><input class='service-input nm' placeholder='Nome do serviço'><input class='service-input pt' inputmode='numeric' placeholder='Porta web'><button class='service-save'>Salvar e ativar proxy</button><button class='service-hide'>Ocultar</button>\";c.querySelector('.service-save').onclick=async()=>{const nm=c.querySelector('.nm').value||x.process,pt=c.querySelector('.pt').value;const r=await fetch('/api/services/register',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'process='+encodeURIComponent(x.process)+'&name='+encodeURIComponent(nm)+'&port='+encodeURIComponent(pt)});if(r.ok)refresh(true)};c.querySelector('.service-hide').onclick=()=>act('/api/services/hide',x.process)}return c};"
"</script>";

static void send_dashboard_v13(int fd)
{
  const char *m=strstr(dashboard_page,"</body>");
  char h[512];
  size_t a,b,p,c,t;
  if(!m){send_response(fd,"200 OK",NULL,dashboard_page);return;}
  a=(size_t)(m-dashboard_page);
  b=strlen(dash_script);
  p=strlen(dash_patch_v13);
  c=strlen(m);
  t=a+b+p+c;
  snprintf(h,sizeof(h),
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",t);
  send(fd,h,strlen(h),0);
  send(fd,dashboard_page,a,0);
  send(fd,dash_script,b,0);
  send(fd,dash_patch_v13,p,0);
  send(fd,m,c,0);
}

static void send_logout_v13(int fd)
{
  send_response(fd,"302 Found",
    "Set-Cookie: astro_session=; Path=/; Max-Age=0; HttpOnly\r\n"
    "Set-Cookie: astro_proxy=; Path=/; Max-Age=0; HttpOnly; SameSite=Lax\r\n"
    "Location: /\r\n","");
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

  server=socket(AF_INET,SOCK_STREAM,0);
  if(server<0)return 1;
  setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_addr.s_addr=INADDR_ANY;
  a.sin_port=htons(ASTRO_PORT);
  if(bind(server,(struct sockaddr*)&a,sizeof(a))<0){close(server);return 1;}
  if(listen(server,8)<0){close(server);return 1;}
  notify("ASTRO Remote v0.13 registry fix - porta 45821");

  while(running){
    int total;
    char method[16],path[ASTRO_PROXY_PATH_MAX],ver[16];
    char *he;
    const astro_service_t *svc;
    const char *backend=NULL;

    client=accept(server,NULL,NULL);
    if(client<0)continue;
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
          int r=recv(client,buf+total,sizeof(buf)-total-1,0);
          if(r<=0)break;
          total+=r;br+=r;buf[total]='\0';
        }
      }
    }

    method[0]=path[0]=ver[0]='\0';
    sscanf(buf,"%15s %2047s %15s",method,path,ver);

    /* Logout has priority over every proxy/cookie route. */
    if((!strcmp(path,"/logout")||!strncmp(path,"/logout?",8))&&logged(buf)){
      send_logout_v13(client);
      close(client);
      continue;
    }

    svc=service_from_path(path,&backend);
    if(svc&&logged(buf)){
      char mark[80];
      snprintf(mark,sizeof(mark),"/service/%s/_root",svc->id);
      if(!strncmp(path,mark,strlen(mark))){
        const char *rest=path+strlen(mark);
        char root[ASTRO_PROXY_PATH_MAX];
        snprintf(root,sizeof(root),"/%s",*rest=='/'?rest+1:rest);
        proxy_request(client,buf,total,svc,root);
      }else proxy_redirect(client,svc);
      close(client);
      continue;
    }

    if(strstr(buf,"POST /login ")&&valid_login(buf))
      send_response(client,"302 Found","Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n","");
    else if(strstr(buf,"GET /api/status ")&&logged(buf))send_status_v13(client);
    else if(strstr(buf,"GET /api/services ")&&logged(buf))send_api_services_v13(client);
    else if(strstr(buf,"POST /api/services/register ")&&logged(buf))send_register(client,buf);
    else if(strstr(buf,"POST /api/services/hide ")&&logged(buf))send_hide(client,buf,0);
    else if(strstr(buf,"POST /api/services/unhide ")&&logged(buf))send_hide(client,buf,1);
    else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){send_response(client,"200 OK",NULL,shutdown_page);running=0;}
    else if(strstr(buf,"GET / ")&&logged(buf))send_dashboard_v13(client);
    else if(logged(buf)&&(svc=service_from_cookie(buf))!=NULL)proxy_request(client,buf,total,svc,path);
    else send_response(client,"200 OK",NULL,login_page);

    close(client);
  }

  close(server);
  notify("ASTRO Remote encerrado");
  return 0;
}
