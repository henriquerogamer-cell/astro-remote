#include "main_v13_embed.c"

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

  server=socket(AF_INET,SOCK_STREAM,0);
  if(server<0)return 1;
  setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));
  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_addr.s_addr=INADDR_ANY;
  a.sin_port=htons(ASTRO_PORT);
  if(bind(server,(struct sockaddr*)&a,sizeof(a))<0){close(server);return 1;}
  if(listen(server,8)<0){close(server);return 1;}
  notify("ASTRO Remote v0.13.2 editor fix - porta 45821");

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
    else if(strstr(buf,"POST /api/services/update ")&&logged(buf))send_update_service_v13(client,buf);
    else if(strstr(buf,"POST /api/services/hide ")&&logged(buf))send_hide(client,buf,0);
    else if(strstr(buf,"POST /api/services/unhide ")&&logged(buf))send_hide(client,buf,1);
    else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){send_response(client,"200 OK",NULL,shutdown_page);running=0;}
    else if(strstr(buf,"GET / ")&&logged(buf))send_dashboard_v132(client);
    else if(logged(buf)&&(svc=service_from_cookie(buf))!=NULL)proxy_request(client,buf,total,svc,path);
    else send_response(client,"200 OK",NULL,login_page);

    close(client);
  }

  close(server);
  notify("ASTRO Remote encerrado");
  return 0;
}
