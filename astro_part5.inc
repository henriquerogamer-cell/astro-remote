typedef struct service_def { const char *name; const char *prefix; int port; } service_def_t;
static const service_def_t SERVICES[] = {
  {"prospero","/p/prospero",PROSPERO_PORT}, {"pegasus","/p/pegasus",PEGASUS_PORT}, {"garlic","/p/garlic",GARLIC_PORT}
};

static const service_def_t *service_from_target(const char *target, const char **upstream_path)
{
  for (size_t i=0;i<sizeof(SERVICES)/sizeof(SERVICES[0]);++i) {
    size_t n=strlen(SERVICES[i].prefix);
    if(strncmp(target,SERVICES[i].prefix,n)==0 && (target[n]=='/'||target[n]=='?'||target[n]=='\0')) {
      const char *p=target+n; if(!*p) p="/"; else if(*p=='?') { static char tmp[4096]; snprintf(tmp,sizeof(tmp),"/%s",p); p=tmp; }
      *upstream_path=p; return &SERVICES[i];
    }
  }
  return NULL;
}

static const service_def_t *service_from_referer(const char *req)
{
  char ref[4096]; if(!header_value_copy(req,"Referer",ref,sizeof(ref))) return NULL;
  for(size_t i=0;i<sizeof(SERVICES)/sizeof(SERVICES[0]);++i) if(strstr(ref,SERVICES[i].prefix)) return &SERVICES[i];
  return NULL;
}

static int proxy_request(int client_fd, const char *req, size_t req_len, size_t header_len,
                         const service_def_t *svc, const char *upstream_path)
{
  int u=socket(AF_INET,SOCK_STREAM,0); if(u<0) return -1;
  struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons((uint16_t)svc->port); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(connect(u,(struct sockaddr*)&a,sizeof(a))!=0){close(u);send_text(client_fd,"502 Bad Gateway","text/html; charset=utf-8",NULL,"<h1>Serviço offline</h1><p>Inicie o payload no PS5 e tente novamente.</p>");return -2;}
  char method[16], target[4096]; if(parse_request_line(req,method,sizeof(method),target,sizeof(target))!=0){close(u);return -3;}
  const char *line_end=strstr(req,"\r\n"); if(!line_end){close(u);return -4;}
  char first[4600]; snprintf(first,sizeof(first),"%s %s HTTP/1.1\r\n",method,upstream_path);
  if(send_all(u,first,strlen(first))!=0){close(u);return -5;}
  const char *headers=line_end+2; const char *headers_end=req+header_len-2;
  const char *p=headers;
  while(p<headers_end){const char *e=strstr(p,"\r\n"); if(!e||e>headers_end)break; if(e==p)break; size_t ln=(size_t)(e-p);
    if(strncasecmp(p,"Host:",5)==0){char h[128];int n=snprintf(h,sizeof(h),"Host: 127.0.0.1:%d\r\n",svc->port);send_all(u,h,(size_t)n);} else if(strncasecmp(p,"Connection:",11)==0){send_all(u,"Connection: close\r\n",19);} else {send_all(u,p,ln);send_all(u,"\r\n",2);} p=e+2;}
  send_all(u,"\r\n",2);
  size_t already=req_len>header_len?req_len-header_len:0; long total_body=content_length(req); if(already){if(send_all(u,req+header_len,already)!=0){close(u);return -6;}}
  long remain=total_body-(long)already; char buf[64*1024]; while(remain>0){size_t want=(size_t)(remain > (long)sizeof(buf) ? sizeof(buf) : (size_t)remain);ssize_t n=recv(client_fd,buf,want,0);if(n<=0)break;if(send_all(u,buf,(size_t)n)!=0)break;remain-=n;}
  for(;;){ssize_t n=recv(u,buf,sizeof(buf),0);if(n<=0)break;if(send_all(client_fd,buf,(size_t)n)!=0)break;} close(u); return 0;
}

static int is_astro_api(const char *t)
{
  return strncmp(t,"/api/status",11)==0 || strncmp(t,"/api/files/list",15)==0 || strncmp(t,"/api/files/download",19)==0;
}

static void handle_client(int fd)
{
  char *req=(char*)malloc(REQ_BUF); if(!req){close(fd);return;} size_t req_len=0, header_len=0;
  if(read_request(fd,req,REQ_BUF,&req_len,&header_len)!=0){free(req);close(fd);return;}
  char method[16], target[4096]; if(parse_request_line(req,method,sizeof(method),target,sizeof(target))!=0){free(req);close(fd);return;}
  char *body=req+header_len; size_t body_in=req_len>header_len?req_len-header_len:0; long clen=content_length(req);
  if(clen>0 && (long)body_in<clen && clen<32000){size_t want=(size_t)clen-body_in; while(want && req_len+want+1<REQ_BUF){ssize_t n=recv(fd,req+req_len,want,0);if(n<=0)break;req_len+=(size_t)n;body_in+=(size_t)n;want-=(size_t)n;} req[req_len]=0;}

  if(strcmp(target,"/login")==0 && strcmp(method,"GET")==0){send_login(fd,0);goto done;}
  if(strcmp(target,"/login")==0 && strcmp(method,"POST")==0){char u[128],p[256];u[0]=p[0]=0;form_value(body,"username",u,sizeof(u));form_value(body,"password",p,sizeof(p));if(strcmp(u,g_user)==0&&strcmp(p,g_pass)==0){char h[256];snprintf(h,sizeof(h),"Set-Cookie: astro_session=%s; Path=/; HttpOnly; SameSite=Strict\r\nLocation: /dashboard\r\n",g_session);send_text(fd,"302 Found","text/plain",h,"" );}else send_login(fd,1);goto done;}
  if(strcmp(target,"/logout")==0){send_text(fd,"302 Found","text/plain","Set-Cookie: astro_session=; Max-Age=0; Path=/\r\nLocation: /login\r\n","");goto done;}
  if(!logged_in(req)){redirect_to(fd,"/login");goto done;}

  if(strcmp(target,"/")==0){redirect_to(fd,"/dashboard");goto done;}
  if(strcmp(target,"/dashboard")==0){send_dashboard(fd);goto done;}
  if(strcmp(target,"/services")==0){send_services(fd);goto done;}
  if(strcmp(target,"/files")==0){send_files_page(fd);goto done;}
  if(strcmp(target,"/saves")==0){send_saves_page(fd);goto done;}
  if(strcmp(target,"/homebrew")==0){send_homebrew_page(fd);goto done;}
  if(strcmp(target,"/settings")==0){send_settings_page(fd,NULL);goto done;}
  if(strncmp(target,"/api/status",11)==0){send_status_api(fd);goto done;}
  if(strncmp(target,"/api/files/list",15)==0){send_file_list_api(fd,target);goto done;}
  if(strncmp(target,"/api/files/download",19)==0){send_download(fd,target);goto done;}
  if(strcmp(target,"/settings/password")==0 && strcmp(method,"POST")==0){char p[256];p[0]=0;form_value(body,"password",p,sizeof(p));if(strlen(p)>=8){snprintf(g_pass,sizeof(g_pass),"%s",p);save_auth();make_secret(g_session,sizeof(g_session),40);send_text(fd,"302 Found","text/plain","Set-Cookie: astro_session=; Max-Age=0; Path=/\r\nLocation: /login\r\n","");}else send_settings_page(fd,"<p style='color:#e7a0a7'>A senha precisa ter pelo menos 8 caracteres.</p>");goto done;}
  if(strcmp(target,"/shutdown")==0 && strcmp(method,"POST")==0){g_running=0;notify("ASTRO encerrado");send_text(fd,"200 OK","text/html; charset=utf-8",NULL,"<html><body style='background:#07090d;color:#eee;font-family:Arial;display:grid;place-items:center;min-height:100vh'><h1>Astro encerrado.</h1></body></html>");goto done;}

  const char *up=NULL; const service_def_t *svc=service_from_target(target,&up);
  if(svc){proxy_request(fd,req,req_len,header_len,svc,up);goto done;}
  if(!is_astro_api(target)){svc=service_from_referer(req);if(svc){proxy_request(fd,req,req_len,header_len,svc,target);goto done;}}

  send_text(fd,"404 Not Found","text/plain; charset=utf-8",NULL,"Astro: rota nao encontrada");
done:
  free(req); close(fd);
}

int main(void)
{
  load_auth();
  int s=socket(AF_INET,SOCK_STREAM,0); if(s<0) return 1;
  int yes=1; setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));
  struct sockaddr_in a; memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_addr.s_addr=htonl(INADDR_ANY); a.sin_port=htons(ASTRO_PORT);
  if(bind(s,(struct sockaddr*)&a,sizeof(a))!=0){close(s);notify("ASTRO: falha ao abrir porta 45821");return 2;}
  if(listen(s,16)!=0){close(s);return 3;}
  notify("ASTRO v1 online | painel: http://IP-DO-PS5:45821");
  while(g_running){struct sockaddr_in c; socklen_t cl=sizeof(c); int fd=accept(s,(struct sockaddr*)&c,&cl); if(fd<0){if(errno==EINTR)continue;break;} handle_client(fd);}
  close(s); return 0;
}
