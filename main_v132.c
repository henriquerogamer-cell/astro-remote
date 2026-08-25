#include <signal.h>
#include <sys/time.h>
#include "main_v13_embed.c"

#define ASTRO_IO_TIMEOUT_SEC 2

static void astro_set_io_timeout(int fd)
{
  struct timeval tv;
  tv.tv_sec=ASTRO_IO_TIMEOUT_SEC;
  tv.tv_usec=0;
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
}

static int http_get_local_v134(int port,const char *path,char *out,size_t outsz)
{
  int fd,n;
  struct sockaddr_in a;
  char req[1024];
  size_t used=0;

  if(!out||outsz<2)return -1;
  out[0]='\0';
  fd=socket(AF_INET,SOCK_STREAM,0);
  if(fd<0)return -2;
  astro_set_io_timeout(fd);
  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_port=htons(port);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(connect(fd,(struct sockaddr*)&a,sizeof(a))<0){close(fd);return -3;}
  n=snprintf(req,sizeof(req),"GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",path,port);
  if(n<=0||n>=(int)sizeof(req)){close(fd);return -4;}
  if(send(fd,req,(size_t)n,0)<=0){close(fd);return -5;}
  while(used+1<outsz){
    ssize_t r=recv(fd,out+used,outsz-used-1,0);
    if(r<=0)break;
    used+=(size_t)r;
  }
  out[used]='\0';
  close(fd);
  return used?0:-6;
}

static void send_api_services_v134(int fd)
{
  char pr[ASTRO_HTTP_BUF],json[24576],names[64][128];
  size_t used=0;
  int visible=0,i,pc=0;
  int ok=http_get_local_v134(PROSPERO_PORT,"/api/processes",pr,sizeof(pr))==0;

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

static int proxy_request_v134(int client,const char *req,int req_len,const astro_service_t *s,const char *backend)
{
  int up,n;
  struct sockaddr_in a;
  char method[16],oldp[ASTRO_PROXY_PATH_MAX],ver[16],head[ASTRO_HTTP_BUF],buf[8192];
  const char *he,*body;
  int body_len;

  if(sscanf(req,"%15s %2047s %15s",method,oldp,ver)!=3)return -1;
  he=strstr(req,"\r\n\r\n");
  if(!he)return -2;
  body=he+4;
  body_len=req_len-(int)(body-req);
  if(body_len<0)body_len=0;

  up=socket(AF_INET,SOCK_STREAM,0);
  if(up<0)return -3;
  astro_set_io_timeout(up);
  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_port=htons(s->port);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(connect(up,(struct sockaddr*)&a,sizeof(a))<0){close(up);return -4;}

  n=snprintf(head,sizeof(head),"%s %s %s\r\nHost: 127.0.0.1:%d\r\n",method,backend,ver,s->port);
  if(n<=0||n>=(int)sizeof(head)){close(up);return -5;}
  {
    const char *line=strstr(req,"\r\n");
    size_t used=(size_t)n;
    if(!line){close(up);return -6;}
    line+=2;
    while(line<he){
      const char *eol=strstr(line,"\r\n");
      size_t ll;
      if(!eol||eol>he)break;
      ll=(size_t)(eol-line);
      if(ll&&strncasecmp(line,"Host:",5)&&strncasecmp(line,"Connection:",11)&&strncasecmp(line,"Cookie:",7)){
        if(used+ll+2>=sizeof(head)){close(up);return -7;}
        memcpy(head+used,line,ll);used+=ll;
        memcpy(head+used,"\r\n",2);used+=2;
      }
      line=eol+2;
    }
    memcpy(head+used,"Connection: close\r\n\r\n",21);used+=21;
    if(send(up,head,used,0)<=0){close(up);return -8;}
  }
  if(body_len>0&&send(up,body,(size_t)body_len,0)<=0){close(up);return -9;}
  while(1){
    ssize_t r=recv(up,buf,sizeof(buf),0);
    if(r<=0)break;
    if(send(client,buf,(size_t)r,0)<=0)break;
  }
  close(up);
  return 0;
}

static const char *dash_patch_v132=
"<script>"
"const astroRefreshBase=refresh;"
"refresh=async function(force){if(!force&&document.querySelector('.service-editbox.show'))return;return astroRefreshBase(force)};"
"const astroCardBase=card;"
"card=function(x){const c=astroCardBase(x);if(x.known){const edit=c.querySelector('.service-edit'),box=c.querySelector('.service-editbox'),name=c.querySelector('.enm');if(edit&&box){edit.onclick=()=>{box.classList.add('show');if(name)name.focus()}}}return c};"
"</script>";

static void send_dashboard_v132(int fd)
{
  const char *m=strstr(dashboard_page,"</body>");
  char h[512];
  size_t a,b,p,q,c,t;
  if(!m){send_response(fd,"200 OK",NULL,dashboard_page);return;}
  a=(size_t)(m-dashboard_page);
  b=strlen(dash_script);
  p=strlen(dash_patch_v13);
  q=strlen(dash_patch_v132);
  c=strlen(m);
  t=a+b+p+q+c;
  snprintf(h,sizeof(h),
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",t);
  send(fd,h,strlen(h),0);
  send(fd,dashboard_page,a,0);
  send(fd,dash_script,b,0);
  send(fd,dash_patch_v13,p,0);
  send(fd,dash_patch_v132,q,0);
  send(fd,m,c,0);
}

static void send_no_content_v132(int fd)
{
  const char *h="HTTP/1.1 204 No Content\r\nCache-Control: max-age=86400\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
  send(fd,h,strlen(h),0);
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
  notify("ASTRO Remote v0.13.4 socket watchdog - porta 45821");

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
          int r=recv(client,buf+total,sizeof(buf)-total-1,0);
          if(r<=0)break;
          total+=r;br+=r;buf[total]='\0';
        }
      }
    }

    method[0]=path[0]=ver[0]='\0';
    sscanf(buf,"%15s %2047s %15s",method,path,ver);

    if(!strcmp(path,"/favicon.ico")){
      send_no_content_v132(client);
      close(client);
      continue;
    }

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
        proxy_request_v134(client,buf,total,svc,root);
      }else proxy_redirect(client,svc);
      close(client);
      continue;
    }

    if(strstr(buf,"POST /login ")&&valid_login(buf))
      send_response(client,"302 Found","Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n","");
    else if(strstr(buf,"GET /api/status ")&&logged(buf))send_status_v13(client);
    else if(strstr(buf,"GET /api/services ")&&logged(buf))send_api_services_v134(client);
    else if(strstr(buf,"POST /api/services/register ")&&logged(buf))send_register(client,buf);
    else if(strstr(buf,"POST /api/services/update ")&&logged(buf))send_update_service_v13(client,buf);
    else if(strstr(buf,"POST /api/services/hide ")&&logged(buf))send_hide(client,buf,0);
    else if(strstr(buf,"POST /api/services/unhide ")&&logged(buf))send_hide(client,buf,1);
    else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){send_response(client,"200 OK",NULL,shutdown_page);running=0;}
    else if(strstr(buf,"GET / ")&&logged(buf))send_dashboard_v132(client);
    else if(logged(buf)&&(svc=service_from_cookie(buf))!=NULL)proxy_request_v134(client,buf,total,svc,path);
    else send_response(client,"200 OK",NULL,login_page);

    close(client);
  }

  close(server);
  notify("ASTRO Remote encerrado");
  return 0;
}
