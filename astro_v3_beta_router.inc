static int v3_valid_setup_username(const char*u){size_t n=u?strlen(u):0;if(n<3||n>=MAX_USER)return 0;for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)u[i];if(!(isalnum(c)||c=='_'||c=='-'||c=='.'))return 0;}return 1;}

static int beta_web_target(const char*target,char*slug,size_t ssz,const char**upath)
{
  if(strncmp(target,"/web/",5))return 0;const char*p=target+5,*slash=strchr(p,'/');if(!slash)return 0;size_t n=(size_t)(slash-p);if(n<1||n>=ssz)return 0;memcpy(slug,p,n);slug[n]=0;if(!beta_safe_slug(slug))return 0;*upath=slash;return 1;
}

static int beta_web_referer(const char*req,char*slug,size_t ssz)
{
  char ref[2048];if(!header_value_copy(req,"Referer",ref,sizeof(ref)))return 0;char*p=strstr(ref,"/web/");if(!p)return 0;p+=5;char*e=strchr(p,'/');if(!e)return 0;size_t n=(size_t)(e-p);if(n<1||n>=ssz)return 0;memcpy(slug,p,n);slug[n]=0;return beta_safe_slug(slug);
}

static int beta_is_native_target(const char*t)
{
  const char*roots[]={"/dashboard","/payloads","/files","/saves","/homebrew","/processes","/profile","/settings","/logout","/login","/setup","/api/","/web/"};
  for(size_t i=0;i<sizeof(roots)/sizeof(roots[0]);i++)if(!strncmp(t,roots[i],strlen(roots[i])))return 1;return !strcmp(t,"/");
}

static void beta_proxy_web(int fd,const char*req,size_t req_len,size_t header_len,const char*slug,const char*upath)
{
  int port=0;char label[256],hint[128];if(!beta_web_lookup(slug,&port,label,sizeof(label),hint,sizeof(hint))){send_text(fd,"404 Not Found","text/html; charset=utf-8",NULL,"<h1>Web UI não registrada</h1>");return;}astro_service_t svc={slug,label,hint,port};v3_proxy_service(fd,req,req_len,header_len,&svc,upath);
}

static void v3_handle_client(int fd)
{
  char*req=malloc(REQ_BUF);if(!req){close(fd);return;}size_t req_len=0,header_len=0;if(read_request(fd,req,REQ_BUF,&req_len,&header_len)!=0){free(req);close(fd);return;}char method[16],target[4096];if(parse_request_line(req,method,sizeof(method),target,sizeof(target))!=0){free(req);close(fd);return;}char*body=req+header_len;size_t body_in=req_len>header_len?req_len-header_len:0;long clen=content_length(req);
  if(clen>0&&(long)body_in<clen&&clen<32000){size_t want=(size_t)clen-body_in;while(want&&req_len+want+1<REQ_BUF){ssize_t n=recv(fd,req+req_len,want,0);if(n<=0)break;req_len+=(size_t)n;body_in+=(size_t)n;want-=(size_t)n;}req[req_len]=0;body=req+header_len;}

  if(!g_auth_ready){
    if(!strcmp(target,"/setup")&&!strcmp(method,"GET")){send_setup(fd,NULL);goto done;}
    if(!strcmp(target,"/setup")&&!strcmp(method,"POST")){char u[128]={0},p[256]={0},p2[256]={0};form_value(body,"username",u,sizeof(u));form_value(body,"password",p,sizeof(p));form_value(body,"password2",p2,sizeof(p2));if(!v3_valid_setup_username(u)){send_setup(fd,"<p class='err'>Use 3 a 47 caracteres: letras, números, ponto, hífen ou underline.</p>");goto done;}if(strlen(p)<8||strlen(p)>=MAX_PASS){send_setup(fd,"<p class='err'>A senha precisa ter entre 8 e 95 caracteres.</p>");goto done;}if(strcmp(p,p2)){send_setup(fd,"<p class='err'>As duas senhas não são iguais.</p>");goto done;}snprintf(g_user,sizeof(g_user),"%s",u);snprintf(g_pass,sizeof(g_pass),"%s",p);g_auth_ready=1;save_auth();make_secret(g_session,sizeof(g_session),40);char h[320];snprintf(h,sizeof(h),"Set-Cookie: astro_session=%s; Path=/; HttpOnly; SameSite=Strict\r\nLocation: /dashboard\r\n",g_session);send_text(fd,"302 Found","text/plain",h,"");notify("ASTRO: acesso criado");goto done;}
    redirect_to(fd,"/setup");goto done;
  }

  if(!strcmp(target,"/login")&&!strcmp(method,"GET")){send_login(fd,0);goto done;}
  if(!strcmp(target,"/login")&&!strcmp(method,"POST")){char u[128]={0},p[256]={0};form_value(body,"username",u,sizeof(u));form_value(body,"password",p,sizeof(p));if(!strcmp(u,g_user)&&!strcmp(p,g_pass)){char h[256];snprintf(h,sizeof(h),"Set-Cookie: astro_session=%s; Path=/; HttpOnly; SameSite=Strict\r\nLocation: /dashboard\r\n",g_session);send_text(fd,"302 Found","text/plain",h,"");}else send_login(fd,1);goto done;}
  if(!strcmp(target,"/logout")){send_text(fd,"302 Found","text/plain","Set-Cookie: astro_session=; Max-Age=0; Path=/\r\nLocation: /login\r\n","");goto done;}
  if(!logged_in(req)){redirect_to(fd,"/login");goto done;}

  char wslug[96];const char*wpath=NULL;if(beta_web_target(target,wslug,sizeof(wslug),&wpath)){beta_proxy_web(fd,req,req_len,header_len,wslug,wpath);goto done;}
  if(!beta_is_native_target(target)&&beta_web_referer(req,wslug,sizeof(wslug))){beta_proxy_web(fd,req,req_len,header_len,wslug,target);goto done;}

  if(!strcmp(target,"/")){redirect_to(fd,"/dashboard");goto done;}
  if(!strcmp(target,"/dashboard")){v3_dashboard(fd);goto done;}
  if(!strcmp(target,"/payloads")){v3_payloads_page(fd);goto done;}
  if(!strcmp(target,"/files")){v3_files_page(fd);goto done;}
  if(!strcmp(target,"/saves")){v3_saves_page(fd);goto done;}
  if(!strcmp(target,"/homebrew")){v3_homebrew_page(fd);goto done;}
  if(!strcmp(target,"/processes")){v3_processes_page(fd);goto done;}
  if(!strcmp(target,"/profile")){v3_profile_page(fd);goto done;}
  if(!strcmp(target,"/settings")){v3_settings_page(fd,NULL);goto done;}

  if(!strncmp(target,"/api/payload-sources/add",24)&&!strcmp(method,"POST")){beta_source_add_api(fd,target);goto done;}
  if(!strncmp(target,"/api/payload-sources/delete",27)&&!strcmp(method,"POST")){beta_source_delete_api(fd,target);goto done;}
  if(!strncmp(target,"/api/payload-sources",20)){beta_sources_api(fd);goto done;}
  if(!strncmp(target,"/api/webapps/add",16)&&!strcmp(method,"POST")){beta_webapp_add_api(fd,target);goto done;}
  if(!strncmp(target,"/api/webapps",12)){beta_webapps_api(fd);goto done;}
  if(!strncmp(target,"/api/title/icon",15)){beta_title_icon_api(fd,target);goto done;}
  if(!strncmp(target,"/api/title/meta",15)){beta_title_meta_api(fd,target);goto done;}
  if(!strncmp(target,"/api/homebrew/install",21)&&!strcmp(method,"POST")){v3_pkg_install_api(fd,target);goto done;}
  if(!strncmp(target,"/api/homebrew/status",20)){v3_pkg_status_api(fd);goto done;}
  if(!strncmp(target,"/api/homebrew",13)){v3_homebrew_api(fd,target);goto done;}

  if(!strncmp(target,"/api/profile/avatar/prepare",27)&&!strcmp(method,"POST")){v3_profile_avatar_prepare_api(fd);goto done;}
  if(!strncmp(target,"/api/profile/avatar/upload",26)&&!strcmp(method,"POST")){v3_profile_avatar_upload_api(fd,target,req,req_len,header_len);goto done;}
  if(!strncmp(target,"/api/profile/avatar/finish",26)&&!strcmp(method,"POST")){v3_profile_avatar_finish_api(fd);goto done;}
  if(!strncmp(target,"/api/profile/avatar",19)){v3_profile_avatar_serve(fd);goto done;}
  if(!strncmp(target,"/api/profile/name",17)&&!strcmp(method,"POST")){v3_profile_name_api(fd,target);goto done;}
  if(!strncmp(target,"/api/profile",12)){v3_profile_api(fd);goto done;}
  if(!strncmp(target,"/api/status",11)){v3_status_api(fd);goto done;}
  if(!strncmp(target,"/api/processes",14)){v3_processes_api(fd);goto done;}
  if(!strncmp(target,"/api/storage",12)){v3_storage_api(fd);goto done;}
  if(!strncmp(target,"/api/payloads/upload",20)&&!strcmp(method,"POST")){v3_payload_upload_api(fd,target,req,req_len,header_len);goto done;}
  if(!strncmp(target,"/api/payloads/launch",20)&&!strcmp(method,"POST")){v3_payload_launch_api(fd,target);goto done;}
  if(!strncmp(target,"/api/payloads/stop",18)&&!strcmp(method,"POST")){v3_payload_stop_api(fd,target);goto done;}
  if(!strncmp(target,"/api/payloads/favorite",22)&&!strcmp(method,"POST")){v3_payload_favorite_api(fd,target);goto done;}
  if(!strncmp(target,"/api/payloads",13)){v3_payload_list_api(fd);goto done;}
  if(!strncmp(target,"/api/files/download",19)){v3_file_download(fd,target);goto done;}
  if(!strncmp(target,"/api/files/upload",17)&&!strcmp(method,"POST")){v3_file_upload_api(fd,target,req,req_len,header_len);goto done;}
  if(!strncmp(target,"/api/files/mkdir",16)&&!strcmp(method,"POST")){v3_file_action_api(fd,target,"mkdir");goto done;}
  if(!strncmp(target,"/api/files/touch",16)&&!strcmp(method,"POST")){v3_file_action_api(fd,target,"touch");goto done;}
  if(!strncmp(target,"/api/files/delete",17)&&!strcmp(method,"POST")){v3_file_action_api(fd,target,"delete");goto done;}
  if(!strncmp(target,"/api/files/rename",17)&&!strcmp(method,"POST")){v3_file_action_api(fd,target,"rename");goto done;}
  if(!strncmp(target,"/api/files/copy",15)&&!strcmp(method,"POST")){v3_file_action_api(fd,target,"copy");goto done;}
  if(!strncmp(target,"/api/files/move",15)&&!strcmp(method,"POST")){v3_file_action_api(fd,target,"move");goto done;}
  if(!strncmp(target,"/api/files",10)){v3_file_list_api(fd,target);goto done;}
  if(!strncmp(target,"/api/users",10)){v3_users_api(fd);goto done;}
  if(!strncmp(target,"/api/saves/backup",17)&&!strcmp(method,"POST")){v3_save_backup_api(fd,target);goto done;}
  if(!strncmp(target,"/api/saves",10)){v3_saves_api(fd);goto done;}

  if(!strcmp(target,"/settings/password")&&!strcmp(method,"POST")){char p[256]={0};form_value(body,"password",p,sizeof(p));if(strlen(p)>=8&&strlen(p)<MAX_PASS){snprintf(g_pass,sizeof(g_pass),"%s",p);save_auth();make_secret(g_session,sizeof(g_session),40);send_text(fd,"302 Found","text/plain","Set-Cookie: astro_session=; Max-Age=0; Path=/\r\nLocation: /login\r\n","");}else v3_settings_page(fd,"<div class='notice bad'>Senha inválida.</div>");goto done;}
  if(!strcmp(target,"/shutdown")&&!strcmp(method,"POST")){g_running=0;notify("ASTRO encerrado");send_text(fd,"200 OK","text/html; charset=utf-8",NULL,"<html><body style='background:#090d12;color:#eee;font-family:Arial;display:grid;place-items:center;min-height:100vh'><h1>Astro encerrado.</h1></body></html>");goto done;}

  send_text(fd,"404 Not Found","text/plain; charset=utf-8",NULL,"Astro: rota não encontrada");
done:free(req);close(fd);
}

int main(void)
{
  astro_set_process_name();load_auth();v3_mkdirs(ASTRO_DIR);v3_mkdirs(ASTRO_PAYLOAD_DIR);v3_mkdirs(ASTRO_BACKUP_DIR);v3_mkdirs(ASTRO_PROFILE_BACKUP_DIR);v3_migrate_payloads_once();beta_seed_sources();beta_seed_webapps();int launcher_rc=astro_launcher_install_if_needed();if(launcher_rc){char lm[96];snprintf(lm,sizeof(lm),"ASTRO: falha no atalho 0x%08X",(unsigned)launcher_rc);notify(lm);}else notify("ASTRO: atalho da aba Mídia registrado");
  int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return 1;int yes=1;setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&yes,sizeof(yes));struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=htonl(INADDR_ANY);a.sin_port=htons(ASTRO_PORT);if(bind(s,(struct sockaddr*)&a,sizeof(a))!=0){close(s);notify("ASTRO: falha ao abrir porta 45821");return 2;}if(listen(s,16)!=0){close(s);return 3;}notify(g_auth_ready?"ASTRO v3 beta online | 45821":"ASTRO v3 beta | crie o primeiro acesso");while(g_running){struct sockaddr_in c;socklen_t cl=sizeof(c);int cfd=accept(s,(struct sockaddr*)&c,&cl);if(cfd<0){if(errno==EINTR)continue;break;}v3_handle_client(cfd);}close(s);return 0;
}
