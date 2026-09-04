#include "astro_shadowmount.inc"

typedef struct service_def { const char *name; const char *prefix; int port; } service_def_t;
static const service_def_t SERVICES[] = {
  {"prospero","/p/prospero",PROSPERO_PORT},
  {"pegasus","/p/pegasus",PEGASUS_PORT},
  {"garlic","/p/garlic",GARLIC_PORT}
};

static const service_def_t *service_from_target(const char *target,const char **upstream_path)
{
  for(size_t i=0;i<sizeof(SERVICES)/sizeof(SERVICES[0]);++i){size_t n=strlen(SERVICES[i].prefix);if(strncmp(target,SERVICES[i].prefix,n)==0&&(target[n]=='/'||target[n]=='?'||target[n]=='\0')){const char *p=target+n;if(!*p)p="/";else if(*p=='?'){static char tmp[4096];snprintf(tmp,sizeof(tmp),"/%s",p);p=tmp;}*upstream_path=p;return &SERVICES[i];}}return NULL;
}

static const service_def_t *service_from_referer(const char *req)
{
  char ref[4096];if(!header_value_copy(req,"Referer",ref,sizeof(ref)))return NULL;for(size_t i=0;i<sizeof(SERVICES)/sizeof(SERVICES[0]);++i)if(strstr(ref,SERVICES[i].prefix))return &SERVICES[i];return NULL;
}

static int proxy_request(int client_fd,const char *req,size_t req_len,size_t header_len,const service_def_t *svc,const char *upstream_path)
{
  int u=socket(AF_INET,SOCK_STREAM,0);if(u<0)return -1;struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((uint16_t)svc->port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(connect(u,(struct sockaddr*)&a,sizeof(a))!=0){close(u);send_text(client_fd,"502 Bad Gateway","text/html; charset=utf-8",NULL,"<html><body style='background:#0b0f14;color:#eee;font-family:Arial;padding:40px'><h1>Serviço offline</h1><p>Inicie o payload e tente novamente.</p></body></html>");return -2;}
  char method[16],target[4096];if(parse_request_line(req,method,sizeof(method),target,sizeof(target))!=0){close(u);return -3;}const char *line_end=strstr(req,"\r\n");if(!line_end){close(u);return -4;}char first[4600];int fn=snprintf(first,sizeof(first),"%s %s HTTP/1.1\r\n",method,upstream_path);if(send_all(u,first,(size_t)fn)!=0){close(u);return -5;}
  const char *headers=line_end+2,*headers_end=req+header_len-2,*p=headers;while(p<headers_end){const char *e=strstr(p,"\r\n");if(!e||e>headers_end||e==p)break;size_t ln=(size_t)(e-p);if(!strncasecmp(p,"Host:",5)){char h[128];int n=snprintf(h,sizeof(h),"Host: 127.0.0.1:%d\r\n",svc->port);send_all(u,h,(size_t)n);}else if(!strncasecmp(p,"Connection:",11)){send_all(u,"Connection: close\r\n",19);}else{send_all(u,p,ln);send_all(u,"\r\n",2);}p=e+2;}send_all(u,"\r\n",2);
  size_t already=req_len>header_len?req_len-header_len:0;long total=content_length(req);if(already&&send_all(u,req+header_len,already)!=0){close(u);return -6;}long remain=total-(long)already;char buf[256*1024];while(remain>0){size_t want=(size_t)(remain>(long)sizeof(buf)?sizeof(buf):remain);ssize_t n=recv(client_fd,buf,want,0);if(n<=0)break;if(send_all(u,buf,(size_t)n)!=0)break;remain-=n;}for(;;){ssize_t n=recv(u,buf,sizeof(buf),0);if(n<=0)break;if(send_all(client_fd,buf,(size_t)n)!=0)break;}close(u);return 0;
}

static int valid_setup_username(const char *u)
{
  size_t n=u?strlen(u):0;if(n<3||n>=MAX_USER)return 0;for(size_t i=0;i<n;++i){unsigned char c=(unsigned char)u[i];if(!(isalnum(c)||c=='_'||c=='-'||c=='.'))return 0;}return 1;
}

static void handle_client(int fd)
{
  char *req=malloc(REQ_BUF);if(!req){close(fd);return;}size_t req_len=0,header_len=0;if(read_request(fd,req,REQ_BUF,&req_len,&header_len)!=0){free(req);close(fd);return;}char method[16],target[4096];if(parse_request_line(req,method,sizeof(method),target,sizeof(target))!=0){free(req);close(fd);return;}char *body=req+header_len;size_t body_in=req_len>header_len?req_len-header_len:0;long clen=content_length(req);
  if(clen>0&&(long)body_in<clen&&clen<32000){size_t want=(size_t)clen-body_in;while(want&&req_len+want+1<REQ_BUF){ssize_t n=recv(fd,req+req_len,want,0);if(n<=0)break;req_len+=(size_t)n;body_in+=(size_t)n;want-=(size_t)n;}req[req_len]=0;body=req+header_len;}

  if(!g_auth_ready){
    if(!strcmp(target,"/setup")&&!strcmp(method,"GET")){send_setup(fd,NULL);goto done;}
    if(!strcmp(target,"/setup")&&!strcmp(method,"POST")){char u[128]={0},p[256]={0},p2[256]={0};form_value(body,"username",u,sizeof(u));form_value(body,"password",p,sizeof(p));form_value(body,"password2",p2,sizeof(p2));if(!valid_setup_username(u)){send_setup(fd,"<p class='err'>Use 3 a 47 caracteres: letras, números, ponto, hífen ou underline.</p>");goto done;}if(strlen(p)<8||strlen(p)>=MAX_PASS){send_setup(fd,"<p class='err'>A senha precisa ter entre 8 e 95 caracteres.</p>");goto done;}if(strcmp(p,p2)){send_setup(fd,"<p class='err'>As duas senhas não são iguais.</p>");goto done;}snprintf(g_user,sizeof(g_user),"%s",u);snprintf(g_pass,sizeof(g_pass),"%s",p);g_auth_ready=1;save_auth();make_secret(g_session,sizeof(g_session),40);char h[320];snprintf(h,sizeof(h),"Set-Cookie: astro_session=%s; Path=/; HttpOnly; SameSite=Strict\r\nLocation: /dashboard\r\n",g_session);send_text(fd,"302 Found","text/plain",h,"");notify("ASTRO: acesso criado com sucesso");goto done;}
    redirect_to(fd,"/setup");goto done;
  }

  if(!strcmp(target,"/login")&&!strcmp(method,"GET")){send_login(fd,0);goto done;}
  if(!strcmp(target,"/login")&&!strcmp(method,"POST")){char u[128]={0},p[256]={0};form_value(body,"username",u,sizeof(u));form_value(body,"password",p,sizeof(p));if(!strcmp(u,g_user)&&!strcmp(p,g_pass)){char h[256];snprintf(h,sizeof(h),"Set-Cookie: astro_session=%s; Path=/; HttpOnly; SameSite=Strict\r\nLocation: /dashboard\r\n",g_session);send_text(fd,"302 Found","text/plain",h,"");}else send_login(fd,1);goto done;}
  if(!strcmp(target,"/logout")){send_text(fd,"302 Found","text/plain","Set-Cookie: astro_session=; Max-Age=0; Path=/\r\nLocation: /login\r\n","");goto done;}
  if(!logged_in(req)){redirect_to(fd,"/login");goto done;}

  if(!strcmp(target,"/")){redirect_to(fd,"/dashboard");goto done;}
  if(!strcmp(target,"/dashboard")){send_dashboard(fd);goto done;}
  if(!strcmp(target,"/payloads")){send_payloads_page(fd);goto done;}
  if(!strcmp(target,"/files")||!strncmp(target,"/files?",7)){send_files_page(fd);goto done;}
  if(!strcmp(target,"/saves")){send_saves_page(fd);goto done;}
  if(!strcmp(target,"/homebrew")){send_homebrew_page(fd);goto done;}
  if(!strcmp(target,"/services")){send_services(fd);goto done;}
  if(!strcmp(target,"/settings")){send_settings_plus_page(fd,NULL);goto done;}

  if(!strncmp(target,"/api/status",11)){send_status_api(fd);goto done;}
  if(!strncmp(target,"/api/shadowmount/paths",22)&&!strcmp(method,"GET")){shadow_paths_api(fd);goto done;}
  if(!strncmp(target,"/api/shadowmount/add",20)&&!strcmp(method,"POST")){shadow_add_api(fd,target);goto done;}
  if(!strncmp(target,"/api/shadowmount/remove",23)&&!strcmp(method,"POST")){shadow_remove_api(fd,target);goto done;}
  if(!strncmp(target,"/api/shadowmount/reset",22)&&!strcmp(method,"POST")){shadow_reset_api(fd);goto done;}
  if(!strncmp(target,"/api/storage",12)){mgr_storage_api(fd);goto done;}
  if(!strncmp(target,"/api/payloads/list",18)){mgr_payload_list_api(fd);goto done;}
  if(!strncmp(target,"/api/payloads/launch",20)&&!strcmp(method,"POST")){mgr_payload_launch_api(fd,target);goto done;}
  if(!strncmp(target,"/api/payloads/favorite",22)&&!strcmp(method,"POST")){mgr_payload_favorite_api(fd,target);goto done;}
  if(!strncmp(target,"/api/files/list",15)){send_file_list_api(fd,target);goto done;}
  if(!strncmp(target,"/api/files/download",19)){send_download(fd,target);goto done;}
  if(!strncmp(target,"/api/files/upload",17)&&!strcmp(method,"POST")){mgr_file_upload_api(fd,target,req,req_len,header_len);goto done;}
  if(!strncmp(target,"/api/files/mkdir",16)&&!strcmp(method,"POST")){mgr_file_mkdir_api(fd,target);goto done;}
  if(!strncmp(target,"/api/files/touch",16)&&!strcmp(method,"POST")){mgr_file_touch_api(fd,target);goto done;}
  if(!strncmp(target,"/api/files/delete",17)&&!strcmp(method,"POST")){mgr_file_delete_api(fd,target);goto done;}
  if(!strncmp(target,"/api/files/rename",17)&&!strcmp(method,"POST")){mgr_file_rename_api(fd,target);goto done;}
  if(!strncmp(target,"/api/files/copy",15)&&!strcmp(method,"POST")){mgr_file_copy_move_api(fd,target,0);goto done;}
  if(!strncmp(target,"/api/files/move",15)&&!strcmp(method,"POST")){mgr_file_copy_move_api(fd,target,1);goto done;}
  if(!strncmp(target,"/api/saves/backup",17)&&!strcmp(method,"POST")){mgr_backup_save_api(fd,target);goto done;}
  if(!strncmp(target,"/api/saves/clone",16)&&!strcmp(method,"POST")){mgr_clone_save_api(fd,target);goto done;}

  if(!strcmp(target,"/settings/password")&&!strcmp(method,"POST")){char p[256]={0};form_value(body,"password",p,sizeof(p));if(strlen(p)>=8&&strlen(p)<MAX_PASS){snprintf(g_pass,sizeof(g_pass),"%s",p);save_auth();make_secret(g_session,sizeof(g_session),40);send_text(fd,"302 Found","text/plain","Set-Cookie: astro_session=; Max-Age=0; Path=/\r\nLocation: /login\r\n","");}else send_settings_plus_page(fd,"<div class='notice bad'>A senha precisa ter entre 8 e 95 caracteres.</div>");goto done;}
  if(!strcmp(target,"/shutdown")&&!strcmp(method,"POST")){g_running=0;notify("ASTRO encerrado");send_text(fd,"200 OK","text/html; charset=utf-8",NULL,"<html><body style='background:#0b0f14;color:#eee;font-family:Arial;display:grid;place-items:center;min-height:100vh'><h1>Astro encerrado.</h1></body></html>");goto done;}

  const char *up=NULL;const service_def_t *svc=service_from_target(target,&up);if(svc){proxy_request(fd,req,req_len,header_len,svc,up);goto done;}
  if(strncmp(target,"/api/",5)){svc=service_from_referer(req);if(svc){proxy_request(fd,req,req_len,header_len,svc,target);goto done;}}
  send_text(fd,"404 Not Found","text/plain; charset=utf-8",NULL,"Astro: rota nao encontrada");
done:free(req);close(fd);
}

int main(void)
{
  load_auth();ensure_dir(ASTRO_DIR);ensure_dir(ASTRO_PAYLOAD_DIR);ensure_dir(ASTRO_BACKUP_DIR);int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return 1;int yes=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_ANY);a.sin_port=htons(ASTRO_PORT);if(bind(s,(struct sockaddr*)&a,sizeof(a))!=0){close(s);notify("ASTRO: falha ao abrir porta 45821");return 2;}if(listen(s,16)!=0){close(s);return 3;}notify(g_auth_ready?"ASTRO v2 online | painel pronto":"ASTRO v2 online | crie o primeiro acesso no navegador");while(g_running){struct sockaddr_in c;socklen_t cl=sizeof(c);int fd=accept(s,(struct sockaddr*)&c,&cl);if(fd<0){if(errno==EINTR)continue;break;}handle_client(fd);}close(s);return 0;
}
