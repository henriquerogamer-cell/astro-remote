#include "main_v132_embed.c"

static const char *screen_page_v135=
"<!doctype html>"
"<html lang='pt-BR'>"
"<head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1,viewport-fit=cover'>"
"<title>Astro Remote - Espelhar PS5</title>"
"<style>"
"*{box-sizing:border-box}"
"html,body{margin:0;width:100%;height:100%;background:#090d13;color:#eaf2ff;font-family:Arial,sans-serif}"
"body{overflow:hidden;background:radial-gradient(circle at 50% 0,#132033 0,#090d13 42%,#06090d 100%)}"
".shell{height:100%;display:grid;grid-template-rows:64px 1fr}"
"header{display:flex;align-items:center;gap:14px;padding:0 18px;border-bottom:1px solid #263447;background:rgba(9,13,19,.93);backdrop-filter:blur(12px)}"
"header a{color:#c9d8ea;text-decoration:none;font-weight:700}"
".brand{font-weight:900;letter-spacing:.08em}"
".dot{width:8px;height:8px;border-radius:99px;background:#6e87a8;box-shadow:0 0 18px rgba(121,161,214,.55)}"
".status{color:#8da4bf;font-size:13px}"
".spacer{flex:1}"
"button,.linkbtn{border:1px solid #34465f;background:#121a26;color:#eaf2ff;padding:9px 12px;border-radius:9px;font-weight:700;cursor:pointer;text-decoration:none;font-size:13px}"
"button:hover,.linkbtn:hover{background:#192536}"
"main{position:relative;min-height:0}"
"iframe{width:100%;height:100%;border:0;background:#000;display:none}"
".setup{position:absolute;inset:0;display:grid;place-items:center;padding:24px}"
".panel{width:min(620px,100%);padding:28px;border:1px solid #2a3a50;border-radius:18px;background:rgba(15,22,32,.95);box-shadow:0 30px 100px rgba(0,0,0,.45)}"
".crest{font-size:12px;letter-spacing:.22em;color:#8199b6;text-transform:uppercase;margin-bottom:8px}"
"h1{margin:0 0 8px;font-size:30px}"
"p{color:#9fb0c4;line-height:1.55;margin:0 0 18px}"
"label{display:block;color:#cbd9e9;font-size:13px;font-weight:700;margin-bottom:7px}"
"input{width:100%;padding:13px 14px;border-radius:10px;border:1px solid #34465f;background:#090f17;color:#fff;font-size:15px;outline:none}"
"input:focus{border-color:#708eb5;box-shadow:0 0 0 3px rgba(112,142,181,.14)}"
".row{display:flex;gap:10px;margin-top:12px;flex-wrap:wrap}"
".primary{background:#d8e7f8;color:#0a1018;border-color:#d8e7f8}"
".primary:hover{background:#fff}"
".hint{margin-top:15px;font-size:12px;color:#7388a2}"
".hidden{display:none!important}"
"@media(max-width:700px){header{padding:0 10px}.brand{font-size:13px}.status{display:none}.panel{padding:20px}h1{font-size:24px}.shell{grid-template-rows:58px}}"
"</style>"
"</head>"
"<body>"
"<div class='shell'>"
"<header>"
"<a href='/'>← Astro</a><span class='dot'></span><span class='brand'>PS5 STREAM</span><span id='status' class='status'>bridge não configurado</span><span class='spacer'></span>"
"<button id='configBtn' type='button'>Configurar</button><button id='fullBtn' type='button'>Tela cheia</button>"
"</header>"
"<main id='stage'>"
"<iframe id='stream' allow='autoplay; fullscreen; gamepad' allowfullscreen></iframe>"
"<div id='setup' class='setup'><div class='panel'>"
"<div class='crest'>Astro Stream Bridge</div><h1>Espelhar PS5</h1>"
"<p>Informe o endereço do bridge Remote Play. O endereço fica salvo somente neste navegador.</p>"
"<label for='bridge'>URL do bridge</label><input id='bridge' placeholder='http://192.168.0.50:10110' autocomplete='off'>"
"<div class='row'><button id='saveBtn' class='primary' type='button'>Conectar</button><a id='openBtn' class='linkbtn' href='#' target='_blank' rel='noopener'>Abrir em nova aba</a><button id='clearBtn' type='button'>Limpar</button></div>"
"<div class='hint'>MVP: o vídeo vem de um bridge Remote Play/WebRTC externo. O ELF Astro continua leve no PS5.</div>"
"</div></div>"
"</main></div>"
"<script>"
"(()=>{"
"const KEY='astro_stream_bridge';"
"const frame=document.getElementById('stream'),setup=document.getElementById('setup'),input=document.getElementById('bridge'),status=document.getElementById('status'),openBtn=document.getElementById('openBtn');"
"function norm(v){v=(v||'').trim();if(!v)return'';if(!/^https?:\\/\\//i.test(v))v='http://'+v;return v.replace(/\\/$/,'')}"
"function showConfig(){setup.classList.remove('hidden');frame.style.display='none';input.focus()}"
"function connect(raw){const u=norm(raw);if(!u){showConfig();return}localStorage.setItem(KEY,u);input.value=u;openBtn.href=u;frame.src=u;frame.style.display='block';setup.classList.add('hidden');status.textContent='bridge: '+u}"
"document.getElementById('saveBtn').onclick=()=>connect(input.value);"
"document.getElementById('configBtn').onclick=showConfig;"
"document.getElementById('clearBtn').onclick=()=>{localStorage.removeItem(KEY);frame.src='about:blank';input.value='';openBtn.href='#';status.textContent='bridge não configurado';showConfig()};"
"document.getElementById('fullBtn').onclick=()=>{const el=document.getElementById('stage');if(document.fullscreenElement)document.exitFullscreen();else if(el.requestFullscreen)el.requestFullscreen()};"
"input.addEventListener('keydown',e=>{if(e.key==='Enter')connect(input.value)});"
"const q=new URLSearchParams(location.search).get('bridge');const saved=q||localStorage.getItem(KEY)||'';input.value=saved;openBtn.href=norm(saved)||'#';if(saved)connect(saved);else showConfig();"
"})();"
"</script>"
"</body></html>";

static const char *dash_stream_patch_v135=
"<script>"
"(()=>{if(document.getElementById('astro-stream-fab'))return;const a=document.createElement('a');a.id='astro-stream-fab';a.href='/screen';a.textContent='ESPELHAR PS5';Object.assign(a.style,{position:'fixed',right:'18px',bottom:'18px',zIndex:'9999',padding:'11px 14px',borderRadius:'10px',border:'1px solid #435a78',background:'#121b28',color:'#eaf2ff',fontWeight:'800',fontSize:'12px',textDecoration:'none',boxShadow:'0 12px 35px rgba(0,0,0,.35)'});document.body.appendChild(a)})();"
"</script>";

static void send_dashboard_v135(int fd)
{
  const char *m=strstr(dashboard_page,"</body>");
  char h[512];
  size_t a,b,p,q,r,c,t;
  if(!m){send_response(fd,"200 OK",NULL,dashboard_page);return;}
  a=(size_t)(m-dashboard_page);
  b=strlen(dash_script);
  p=strlen(dash_patch_v13);
  q=strlen(dash_patch_v132);
  r=strlen(dash_stream_patch_v135);
  c=strlen(m);
  t=a+b+p+q+r+c;
  snprintf(h,sizeof(h),
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",t);
  send(fd,h,strlen(h),0);
  send(fd,dashboard_page,a,0);
  send(fd,dash_script,b,0);
  send(fd,dash_patch_v13,p,0);
  send(fd,dash_patch_v132,q,0);
  send(fd,dash_stream_patch_v135,r,0);
  send(fd,m,c,0);
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
  notify("ASTRO Remote v0.13.5 stream shell - porta 45821");

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
          int rcv=recv(client,buf+total,sizeof(buf)-total-1,0);
          if(rcv<=0)break;
          total+=rcv;br+=rcv;buf[total]='\0';
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

    if((!strcmp(path,"/screen")||!strncmp(path,"/screen?",8))&&logged(buf)){
      send_response(client,"200 OK","Cache-Control: no-store\r\n",screen_page_v135);
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
    else if(strstr(buf,"GET / ")&&logged(buf))send_dashboard_v135(client);
    else if(logged(buf)&&(svc=service_from_cookie(buf))!=NULL)proxy_request_v134(client,buf,total,svc,path);
    else send_response(client,"200 OK",NULL,login_page);

    close(client);
  }

  close(server);
  notify("ASTRO Remote encerrado");
  return 0;
}
