#include "main_v132_embed.c"
#include "remote_service.h"

static void send_remote_status_v136(int fd)
{
  astro_remote_state_t s;
  char j[768];
  time_t now=time(NULL);
  long session_up=0;
  astro_remote_service_snapshot(&s);
  if(s.session_active&&s.session_started_at>0&&now>=s.session_started_at)
    session_up=(long)(now-s.session_started_at);
  snprintf(j,sizeof(j),
    "{\"ok\":true,\"service_online\":%s,\"session_active\":%s,\"video_ready\":%s,\"control_ready\":%s,\"generation\":%lu,\"phase\":\"%s\",\"session_uptime_seconds\":%ld,\"transport\":\"internal\",\"external_bridge\":false}",
    s.service_online?"true":"false",
    s.session_active?"true":"false",
    s.video_ready?"true":"false",
    s.control_ready?"true":"false",
    s.generation,s.phase,session_up);
  send_json(fd,"200 OK",j);
}

static void send_remote_action_v136(int fd,int start)
{
  int rc=start?astro_remote_service_start():astro_remote_service_stop();
  if(rc<0){send_json(fd,"503 Service Unavailable","{\"ok\":false,\"error\":\"remote_service_offline\"}");return;}
  send_remote_status_v136(fd);
}

static const char *remote_page_v136=
"<!doctype html><html lang='pt-BR'><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
"<title>Astro Remote - Remote</title>"
"<style>"
"*{box-sizing:border-box}html,body{margin:0;min-height:100%;background:#090d13;color:#edf4ff;font-family:Arial,sans-serif}"
"body{background:radial-gradient(circle at 50% 0,#142238 0,#090d13 42%,#06080c 100%)}"
"header{height:64px;display:flex;align-items:center;gap:12px;padding:0 18px;border-bottom:1px solid #263447;background:rgba(9,13,19,.94)}"
"header a{color:#c8d6e8;text-decoration:none;font-weight:800}.brand{font-weight:900;letter-spacing:.09em}.spacer{flex:1}"
".pill{padding:6px 9px;border:1px solid #34465f;border-radius:999px;color:#9eb1c8;font-size:12px}.pill.on{color:#9ee7bb;border-color:#315841}"
"main{padding:22px;max-width:1180px;margin:0 auto}.grid{display:grid;grid-template-columns:minmax(0,1fr) 310px;gap:16px}"
".screen{aspect-ratio:16/9;background:#020304;border:1px solid #28394f;border-radius:16px;display:grid;place-items:center;overflow:hidden;box-shadow:0 28px 90px rgba(0,0,0,.38)}"
".screen-inner{text-align:center;padding:26px}.screen h2{margin:0 0 8px}.screen p{margin:0;color:#7f93aa;line-height:1.5}.dot{width:10px;height:10px;border-radius:50%;background:#4d627d;margin:0 auto 14px;box-shadow:0 0 22px rgba(91,129,177,.45)}"
".panel{border:1px solid #29394e;background:#0f1620;border-radius:16px;padding:18px;height:max-content}.panel h3{margin:0 0 16px}.row{display:flex;justify-content:space-between;gap:12px;padding:10px 0;border-bottom:1px solid #202c3b;font-size:13px}.row span:first-child{color:#8397af}.row span:last-child{text-align:right;font-weight:700}"
"button{width:100%;margin-top:12px;padding:11px;border-radius:9px;border:1px solid #405676;background:#162235;color:#eaf2ff;font-weight:800;cursor:pointer}.primary{background:#d8e7f8;color:#091019;border-color:#d8e7f8}.danger{background:#261519;border-color:#61323b;color:#ffb6bf}"
".note{margin-top:14px;color:#71859d;font-size:12px;line-height:1.5}"
"@media(max-width:820px){.grid{grid-template-columns:1fr}main{padding:12px}header{padding:0 12px}.brand{font-size:13px}}"
"</style></head><body>"
"<header><a href='/'>← Astro</a><span class='brand'>REMOTE</span><span id='servicePill' class='pill'>SERVIÇO</span><span class='spacer'></span><span id='phasePill' class='pill'>IDLE</span></header>"
"<main><div class='grid'><section class='screen'><div class='screen-inner'><div class='dot'></div><h2 id='screenTitle'>Remote pronto no Astro</h2><p id='screenText'>O módulo agora vive dentro do astro_remote.elf. Nenhum bridge externo é necessário.</p></div></section>"
"<aside class='panel'><h3>Astro Remote Service</h3>"
"<div class='row'><span>Serviço</span><span id='service'>...</span></div><div class='row'><span>Sessão</span><span id='session'>...</span></div><div class='row'><span>Vídeo</span><span id='video'>...</span></div><div class='row'><span>Controle</span><span id='control'>...</span></div><div class='row'><span>Tempo</span><span id='uptime'>...</span></div>"
"<button id='start' class='primary'>INICIAR REMOTE</button><button id='stop' class='danger'>ENCERRAR SESSÃO</button>"
"<div class='note'>Esta etapa fixa a arquitetura residente. O próximo passo é ligar a fonte de vídeo do PS5 a este serviço interno e depois entregar os frames ao navegador.</div>"
"</aside></div></main>"
"<script>"
"const $=id=>document.getElementById(id);"
"function fmt(n){n=Number(n)||0;const h=Math.floor(n/3600),m=Math.floor((n%3600)/60),s=n%60;return (h?h+'h ':'')+(m?m+'m ':'')+s+'s'}"
"async function status(){try{const r=await fetch('/api/remote/status',{cache:'no-store'}),j=await r.json();$('service').textContent=j.service_online?'ONLINE':'OFFLINE';$('session').textContent=j.session_active?'ATIVA':'PARADA';$('video').textContent=j.video_ready?'PRONTO':'AGUARDANDO FONTE';$('control').textContent=j.control_ready?'PRONTO':'AGUARDANDO';$('uptime').textContent=fmt(j.session_uptime_seconds);$('servicePill').textContent=j.service_online?'SERVIÇO ONLINE':'SERVIÇO OFFLINE';$('servicePill').className='pill '+(j.service_online?'on':'');$('phasePill').textContent=(j.phase||'idle').replaceAll('_',' ').toUpperCase();if(j.session_active){$('screenTitle').textContent='Sessão Remote residente';$('screenText').textContent=j.video_ready?'Vídeo conectado.':'Astro está aguardando a fonte de vídeo interna do PS5.'}else{$('screenTitle').textContent='Remote pronto no Astro';$('screenText').textContent='Clique em iniciar. A sessão continua no PS5 mesmo que esta aba seja fechada.'}}catch(e){$('service').textContent='SEM RESPOSTA'}}"
"async function act(path){await fetch(path,{method:'POST'});await status()}$('start').onclick=()=>act('/api/remote/start');$('stop').onclick=()=>act('/api/remote/stop');status();setInterval(status,2000);"
"</script></body></html>";

static const char *dash_remote_patch_v136=
"<style>#astro-remote-nav{display:inline-flex;align-items:center;height:34px;padding:0 11px;border:1px solid #39485d;border-radius:7px;background:#171d25;color:#d6e2f0;text-decoration:none;font-size:12px;font-weight:800;margin-right:8px}</style>"
"<script>(()=>{if(document.getElementById('astro-remote-nav'))return;const a=document.createElement('a');a.id='astro-remote-nav';a.href='/remote';a.textContent='REMOTE';const logout=[...document.querySelectorAll('a')].find(x=>x.getAttribute('href')==='/logout');if(logout&&logout.parentNode)logout.parentNode.insertBefore(a,logout);else{Object.assign(a.style,{position:'fixed',right:'18px',bottom:'18px',zIndex:'9999'});document.body.appendChild(a)}})();</script>";

static void send_dashboard_v136(int fd)
{
  const char *m=strstr(dashboard_page,"</body>");
  char h[512];
  size_t a,b,p,q,r,c,t;
  if(!m){send_response(fd,"200 OK",NULL,dashboard_page);return;}
  a=(size_t)(m-dashboard_page);b=strlen(dash_script);p=strlen(dash_patch_v13);q=strlen(dash_patch_v132);r=strlen(dash_remote_patch_v136);c=strlen(m);t=a+b+p+q+r+c;
  snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",t);
  send(fd,h,strlen(h),0);send(fd,dashboard_page,a,0);send(fd,dash_script,b,0);send(fd,dash_patch_v13,p,0);send(fd,dash_patch_v132,q,0);send(fd,dash_remote_patch_v136,r,0);send(fd,m,c,0);
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
  memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=INADDR_ANY;a.sin_port=htons(ASTRO_PORT);
  if(bind(server,(struct sockaddr*)&a,sizeof(a))<0){close(server);return 1;}
  if(listen(server,8)<0){close(server);return 1;}
  notify("ASTRO Remote v0.14.0 resident remote - porta 45821");

  while(running){
    int total;char method[16],path[ASTRO_PROXY_PATH_MAX],ver[16];char *he;const astro_service_t *svc;const char *backend=NULL;
    client=accept(server,NULL,NULL);if(client<0)continue;astro_set_io_timeout(client);memset(buf,0,sizeof(buf));total=recv(client,buf,sizeof(buf)-1,0);if(total<=0){close(client);continue;}buf[total]='\0';
    he=strstr(buf,"\r\n\r\n");if(he){int cl=0;char *p=strstr(buf,"Content-Length:");if(p)cl=atoi(p+15);{int hs=(int)((he+4)-buf),br=total-hs;while(br<cl&&total<(int)sizeof(buf)-1){int rr=recv(client,buf+total,sizeof(buf)-total-1,0);if(rr<=0)break;total+=rr;br+=rr;buf[total]='\0';}}}
    method[0]=path[0]=ver[0]='\0';sscanf(buf,"%15s %2047s %15s",method,path,ver);

    if(!strcmp(path,"/favicon.ico")){send_no_content_v132(client);close(client);continue;}
    if((!strcmp(path,"/logout")||!strncmp(path,"/logout?",8))&&logged(buf)){send_logout_v13(client);close(client);continue;}
    if((!strcmp(path,"/remote")||!strcmp(path,"/screen")||!strncmp(path,"/remote?",8)||!strncmp(path,"/screen?",8))&&logged(buf)){send_response(client,"200 OK","Cache-Control: no-store\r\n",remote_page_v136);close(client);continue;}

    svc=service_from_path(path,&backend);
    if(svc&&logged(buf)){char mark[80];snprintf(mark,sizeof(mark),"/service/%s/_root",svc->id);if(!strncmp(path,mark,strlen(mark))){const char *rest=path+strlen(mark);char root[ASTRO_PROXY_PATH_MAX];snprintf(root,sizeof(root),"/%s",*rest=='/'?rest+1:rest);proxy_request_v134(client,buf,total,svc,root);}else proxy_redirect(client,svc);close(client);continue;}

    if(strstr(buf,"POST /login ")&&valid_login(buf))send_response(client,"302 Found","Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n","");
    else if(strstr(buf,"GET /api/status ")&&logged(buf))send_status_v13(client);
    else if(strstr(buf,"GET /api/services ")&&logged(buf))send_api_services_v134(client);
    else if(strstr(buf,"POST /api/services/register ")&&logged(buf))send_register(client,buf);
    else if(strstr(buf,"POST /api/services/update ")&&logged(buf))send_update_service_v13(client,buf);
    else if(strstr(buf,"POST /api/services/hide ")&&logged(buf))send_hide(client,buf,0);
    else if(strstr(buf,"POST /api/services/unhide ")&&logged(buf))send_hide(client,buf,1);
    else if(strstr(buf,"GET /api/remote/status ")&&logged(buf))send_remote_status_v136(client);
    else if(strstr(buf,"POST /api/remote/start ")&&logged(buf))send_remote_action_v136(client,1);
    else if(strstr(buf,"POST /api/remote/stop ")&&logged(buf))send_remote_action_v136(client,0);
    else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){astro_remote_service_stop();send_response(client,"200 OK",NULL,shutdown_page);running=0;}
    else if(strstr(buf,"GET / ")&&logged(buf))send_dashboard_v136(client);
    else if(logged(buf)&&(svc=service_from_cookie(buf))!=NULL)proxy_request_v134(client,buf,total,svc,path);
    else send_response(client,"200 OK",NULL,login_page);
    close(client);
  }

  close(server);notify("ASTRO Remote encerrado");return 0;
}
