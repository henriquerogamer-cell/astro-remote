#include <time.h>
#include <ctype.h>
#include <sys/stat.h>

#define main astro_v03_legacy_main
#include "main_v03.c"
#undef main

#define PROSPERO_PORT 7070
#define ASTRO_HTTP_BUF 32768
#define ASTRO_PROXY_PATH_MAX 2048
#define ASTRO_MAX_SERVICES 32
#define ASTRO_REGISTRY_DIR "/data/AstroRemote"
#define ASTRO_REGISTRY_FILE "/data/AstroRemote/services.conf"

typedef struct astro_service {
  char id[48];
  char name[96];
  char process[128];
  int port;
} astro_service_t;

static astro_service_t services[ASTRO_MAX_SERVICES];
static int service_count = 0;
static time_t astro_started_at;

static void send_json(int fd, const char *status, const char *body)
{
  char h[512];
  snprintf(h,sizeof(h),"HTTP/1.1 %s\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",status,strlen(body));
  send(fd,h,strlen(h),0); send(fd,body,strlen(body),0);
}

static int tcp_open(int port)
{
  int fd,rc; struct sockaddr_in a;
  fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0)return 0;
  memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons(port); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  rc=connect(fd,(struct sockaddr*)&a,sizeof(a)); close(fd); return rc==0;
}

static int http_get_local(int port,const char *path,char *out,size_t outsz)
{
  int fd,n; struct sockaddr_in a; char req[1024]; size_t used=0;
  if(!out||outsz<2)return -1; out[0]='\0';
  fd=socket(AF_INET,SOCK_STREAM,0); if(fd<0)return -2;
  memset(&a,0,sizeof(a)); a.sin_family=AF_INET; a.sin_port=htons(port); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(connect(fd,(struct sockaddr*)&a,sizeof(a))<0){close(fd);return -3;}
  n=snprintf(req,sizeof(req),"GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",path,port);
  if(n<=0||n>=(int)sizeof(req)){close(fd);return -4;} if(send(fd,req,(size_t)n,0)<=0){close(fd);return -5;}
  while(used+1<outsz){ssize_t r=recv(fd,out+used,outsz-used-1,0);if(r<=0)break;used+=(size_t)r;}
  out[used]='\0'; close(fd); return used?0:-6;
}

static void slugify(const char *src,char *dst,size_t n)
{
  size_t j=0; while(*src&&j+1<n){unsigned char c=(unsigned char)*src++; if(isalnum(c))dst[j++]=(char)tolower(c); else if((c=='-'||c=='_'||c=='.')&&j)dst[j++]='-';}
  while(j&&dst[j-1]=='-')j--; dst[j]='\0'; if(!j)snprintf(dst,n,"service");
}

static astro_service_t *by_id(const char *id){int i;for(i=0;i<service_count;i++)if(!strcmp(services[i].id,id))return &services[i];return NULL;}
static astro_service_t *by_process(const char *p){int i;for(i=0;i<service_count;i++)if(!strcasecmp(services[i].process,p))return &services[i];return NULL;}

static void add_service(const char *id,const char *name,const char *process,int port)
{
  astro_service_t *s=by_process(process); if(s){if(port>0)s->port=port;return;} if(service_count>=ASTRO_MAX_SERVICES)return;
  s=&services[service_count++]; memset(s,0,sizeof(*s)); snprintf(s->id,sizeof(s->id),"%s",id); snprintf(s->name,sizeof(s->name),"%s",name); snprintf(s->process,sizeof(s->process),"%s",process); s->port=port;
}

static void save_registry(void)
{
  FILE *f; int i; mkdir(ASTRO_REGISTRY_DIR,0755); f=fopen(ASTRO_REGISTRY_FILE,"w"); if(!f)return;
  for(i=0;i<service_count;i++){
    astro_service_t *s=&services[i];
    if(!strcmp(s->process,"ProsperoMgr.elf")||!strcmp(s->process,"pegasus_dl.elf")||!strcmp(s->process,"websrv.elf"))continue;
    fprintf(f,"%s|%s|%s|%d\n",s->process,s->id,s->name,s->port);
  }
  fclose(f);
}

static void load_registry(void)
{
  FILE *f; char line[512];
  service_count=0;
  add_service("prospero","Prospero Manager","ProsperoMgr.elf",7070);
  add_service("pegasus","Pegasus DL","pegasus_dl.elf",6970);
  add_service("websrv","WebSrv","websrv.elf",8080);
  f=fopen(ASTRO_REGISTRY_FILE,"r"); if(!f)return;
  while(fgets(line,sizeof(line),f)){
    char *p1,*p2,*p3; int port;
    line[strcspn(line,"\r\n")]='\0'; p1=strchr(line,'|'); if(!p1)continue; *p1++='\0'; p2=strchr(p1,'|'); if(!p2)continue; *p2++='\0'; p3=strchr(p2,'|'); if(!p3)continue; *p3++='\0'; port=atoi(p3);
    if(port>0&&port<65536)add_service(p1,p2,line,port);
  }
  fclose(f);
}

static int extract_form(const char *request,const char *key,char *out,size_t n)
{
  const char *b=strstr(request,"\r\n\r\n"),*p; char enc[1024]; size_t i=0; char needle[64];
  if(!b)return 0; b+=4; snprintf(needle,sizeof(needle),"%s=",key); p=strstr(b,needle); if(!p)return 0; p+=strlen(needle);
  while(*p&&*p!='&'&&i+1<sizeof(enc))enc[i++]=*p++; enc[i]='\0'; url_decode(out,enc); out[n-1]='\0'; return out[0]!=0;
}

static int extract_processes(const char *http,char names[][128],int max)
{
  int count=0; const char *p=http;
  while((p=strstr(p,".elf"))!=NULL&&count<max){const char *s=p; const char *e=p+4; size_t len; int dup=0,i;
    while(s>http){char c=s[-1]; if(isalnum((unsigned char)c)||c=='_'||c=='-'||c=='.')s--; else break;}
    len=(size_t)(e-s); if(len>0&&len<128){memcpy(names[count],s,len);names[count][len]='\0';for(i=0;i<count;i++)if(!strcasecmp(names[i],names[count]))dup=1;if(!dup)count++;}
    p=e;
  }
  return count;
}

static const astro_service_t *service_from_path(const char *path,const char **backend)
{
  int i; static char rootq[ASTRO_PROXY_PATH_MAX];
  for(i=0;i<service_count;i++){char pre[80];size_t l;snprintf(pre,sizeof(pre),"/service/%s",services[i].id);l=strlen(pre);
    if(!strncmp(path,pre,l)&&(path[l]=='/'||path[l]=='\0'||path[l]=='?')){const char *q=path+l;if(!*q)*backend="/";else if(*q=='?'){snprintf(rootq,sizeof(rootq),"/%s",q);*backend=rootq;}else *backend=q;return &services[i];}}
  return NULL;
}

static const astro_service_t *service_from_cookie(const char *req)
{
  const char *p=strstr(req,"astro_proxy="); char id[48]; size_t i=0; if(!p)return NULL;p+=12;while(*p&&*p!=';'&&*p!='\r'&&*p!='\n'&&i+1<sizeof(id))id[i++]=*p++;id[i]='\0';return by_id(id);
}

static void proxy_redirect(int fd,const astro_service_t *s)
{
  char h[512]; snprintf(h,sizeof(h),"HTTP/1.1 302 Found\r\nSet-Cookie: astro_proxy=%s; Path=/; HttpOnly; SameSite=Lax\r\nLocation: /service/%s/_root\r\nContent-Length: 0\r\nConnection: close\r\n\r\n",s->id,s->id);send(fd,h,strlen(h),0);
}

static int proxy_request(int client,const char *req,int req_len,const astro_service_t *s,const char *backend)
{
  int up,n; struct sockaddr_in a; char method[16],oldp[ASTRO_PROXY_PATH_MAX],ver[16],head[ASTRO_HTTP_BUF],buf[8192]; const char *he,*body; int body_len;
  if(sscanf(req,"%15s %2047s %15s",method,oldp,ver)!=3)return -1; he=strstr(req,"\r\n\r\n"); if(!he)return -2; body=he+4; body_len=req_len-(int)(body-req); if(body_len<0)body_len=0;
  up=socket(AF_INET,SOCK_STREAM,0);if(up<0)return -3;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(s->port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(connect(up,(struct sockaddr*)&a,sizeof(a))<0){close(up);return -4;}
  n=snprintf(head,sizeof(head),"%s %s %s\r\nHost: 127.0.0.1:%d\r\n",method,backend,ver,s->port);if(n<=0||n>=(int)sizeof(head)){close(up);return -5;}
  {const char *line=strstr(req,"\r\n");size_t used=(size_t)n;if(!line){close(up);return -6;}line+=2;while(line<he){const char *eol=strstr(line,"\r\n");size_t ll;if(!eol||eol>he)break;ll=(size_t)(eol-line);if(ll&&strncasecmp(line,"Host:",5)&&strncasecmp(line,"Connection:",11)&&strncasecmp(line,"Cookie:",7)){if(used+ll+2>=sizeof(head)){close(up);return -7;}memcpy(head+used,line,ll);used+=ll;memcpy(head+used,"\r\n",2);used+=2;}line=eol+2;}memcpy(head+used,"Connection: close\r\n\r\n",21);used+=21;if(send(up,head,used,0)<=0){close(up);return -8;}}
  if(body_len>0&&send(up,body,(size_t)body_len,0)<=0){close(up);return -9;} while(1){ssize_t r=recv(up,buf,sizeof(buf),0);if(r<=0)break;if(send(client,buf,(size_t)r,0)<=0)break;}close(up);return 0;
}

static void send_api_services(int fd)
{
  char pr[ASTRO_HTTP_BUF],json[16384],names[64][128];size_t used=0;int n=0,i,pc=0;int ok=http_get_local(PROSPERO_PORT,"/api/processes",pr,sizeof(pr))==0;
  used+=(size_t)snprintf(json+used,sizeof(json)-used,"{\"ok\":true,\"manager_online\":%s,\"source\":\"ProsperoMgr /api/processes\",\"items\":[",ok?"true":"false");
  if(ok){pc=extract_processes(pr,names,64);for(i=0;i<pc;i++){astro_service_t *s=by_process(names[i]);if(s&&s->port>0&&tcp_open(s->port)){used+=(size_t)snprintf(json+used,sizeof(json)-used,"%s{\"id\":\"%s\",\"name\":\"%s\",\"process\":\"%s\",\"port\":%d,\"known\":true,\"active\":true,\"url\":\"/service/%s/\"}",n?",":"",s->id,s->name,s->process,s->port,s->id);n++;}else if(!s){char id[48];slugify(names[i],id,sizeof(id));used+=(size_t)snprintf(json+used,sizeof(json)-used,"%s{\"id\":\"%s\",\"name\":\"Novo serviço detectado\",\"process\":\"%s\",\"port\":0,\"known\":false,\"active\":true}",n?",":"",id,names[i]);n++;}}}
  snprintf(json+used,sizeof(json)-used,"],\"count\":%d}",n);send_json(fd,"200 OK",json);
}

static void send_register(int fd,const char *req)
{
  char process[128],name[96],port_s[16],id[48];int port;
  if(!extract_form(req,"process",process,sizeof(process))||!extract_form(req,"port",port_s,sizeof(port_s))){send_json(fd,"400 Bad Request","{\"ok\":false,\"error\":\"missing_fields\"}");return;}
  if(!extract_form(req,"name",name,sizeof(name)))snprintf(name,sizeof(name),"%s",process);port=atoi(port_s);if(port<=0||port>=65536){send_json(fd,"400 Bad Request","{\"ok\":false,\"error\":\"invalid_port\"}");return;}
  slugify(process,id,sizeof(id));add_service(id,name,process,port);save_registry();send_json(fd,"200 OK","{\"ok\":true,\"saved\":true}");
}

static void send_status(int fd)
{
  char j[512];time_t now=time(NULL);long up=now>=astro_started_at?(long)(now-astro_started_at):0;snprintf(j,sizeof(j),"{\"ok\":true,\"console\":\"PlayStation 5\",\"console_online\":true,\"astro_online\":true,\"http_port\":%d,\"version\":\"0.11-dynamic-registry\",\"uptime_seconds\":%ld,\"remote\":\"standby\",\"prospero_online\":%s}",ASTRO_PORT,up,tcp_open(PROSPERO_PORT)?"true":"false");send_json(fd,"200 OK",j);
}

static const char *dash_script=
"<style>.service-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;padding:14px}.service-card{background:#11161d;border:1px solid #29313d;border-radius:9px;padding:14px}.service-top{display:flex;justify-content:space-between;gap:10px}.service-card h4{margin:0;font-size:14px}.service-process{color:#687487;font-size:10px;margin:5px 0 12px}.service-state{font-size:10px;padding:4px 7px;border-radius:999px;font-weight:800;background:#10251b;border:1px solid #24553a;color:#7fe4aa}.service-chip{font-size:10px;padding:4px 7px;border-radius:5px;background:#192431;border:1px solid #293c50;color:#91bce9}.service-open,.service-save{margin-top:13px;height:34px;border-radius:7px;border:1px solid #176fdc;background:linear-gradient(#2084ff,#1269dc);color:#fff;font-weight:750;width:100%;cursor:pointer}.service-input{width:100%;margin-top:8px;height:34px;border-radius:7px;border:1px solid #303947;background:#0d1218;color:#fff;padding:0 10px}@media(max-width:900px){.service-grid{grid-template-columns:1fr}}</style>"
"<script>"
"function card(x){const c=document.createElement('div');c.className='service-card';if(x.known){c.innerHTML=\"<div class='service-top'><h4>\"+x.name+\"</h4><span class='service-state'>ATIVO</span></div><div class='service-process'>\"+x.process+\"</div><span class='service-chip'>INTERNO · 127.0.0.1:\"+x.port+\"</span><button class='service-open'>Abrir dentro do Astro</button>\";c.querySelector('.service-open').onclick=()=>location.href=x.url}else{c.innerHTML=\"<div class='service-top'><h4>Novo serviço detectado</h4><span class='service-state'>NOVO</span></div><div class='service-process'>\"+x.process+\"</div><input class='service-input nm' placeholder='Nome do serviço'><input class='service-input pt' inputmode='numeric' placeholder='Porta web'><button class='service-save'>Salvar e ativar proxy</button>\";c.querySelector('.service-save').onclick=async()=>{const nm=c.querySelector('.nm').value||x.process,pt=c.querySelector('.pt').value;const r=await fetch('/api/services/register',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'process='+encodeURIComponent(x.process)+'&name='+encodeURIComponent(nm)+'&port='+encodeURIComponent(pt)});if(r.ok)setTimeout(refresh,300)}}return c}"
"async function refresh(){try{const [sr,vr]=await Promise.all([fetch('/api/status',{cache:'no-store'}),fetch('/api/services',{cache:'no-store'})]);const s=await sr.json(),v=await vr.json();const top=document.querySelector('.status');if(top)top.innerHTML=\"<span class='dot'></span>PS5 ONLINE\";const m=document.querySelectorAll('.metric');if(m.length>=4){m[1].querySelector('strong').textContent=String(s.http_port);m[1].querySelector('small').textContent='API '+s.version+' ativa';m[3].querySelector('strong').textContent=String(v.count);m[3].querySelector('small').textContent='serviços/processos detectados'}const sec=document.querySelector('.two .section');if(sec){const head=sec.querySelector('.section-head');if(head){head.querySelector('h3').textContent='Serviços ativos';head.querySelector('span').textContent=v.manager_online?'via Próspero':'Próspero offline'}let g=sec.querySelector('.service-grid');if(!g){const old=sec.querySelector('.table');if(old)old.remove();g=document.createElement('div');g.className='service-grid';sec.appendChild(g)}g.innerHTML='';v.items.forEach(x=>g.appendChild(card(x)));if(!v.items.length)g.innerHTML=\"<div style='color:#8f9aaa'>Nenhum serviço ativo detectado.</div>\"}}catch(e){const top=document.querySelector('.status');if(top)top.textContent='ASTRO API OFFLINE'}}"
"document.addEventListener('DOMContentLoaded',()=>{refresh();setInterval(refresh,8000)});"
"</script>";

static void send_dashboard(int fd)
{
  const char *m=strstr(dashboard_page,"</body>");char h[512];size_t a,b,c,t;if(!m){send_response(fd,"200 OK",NULL,dashboard_page);return;}a=(size_t)(m-dashboard_page);b=strlen(dash_script);c=strlen(m);t=a+b+c;snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",t);send(fd,h,strlen(h),0);send(fd,dashboard_page,a,0);send(fd,dash_script,b,0);send(fd,m,c,0);
}

int main(void)
{
  int server,client,opt=1;struct sockaddr_in a;char buf[8192];running=1;astro_started_at=time(NULL);load_registry();
  server=socket(AF_INET,SOCK_STREAM,0);if(server<0)return 1;setsockopt(server,SOL_SOCKET,SO_REUSEADDR,&opt,sizeof(opt));memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_addr.s_addr=INADDR_ANY;a.sin_port=htons(ASTRO_PORT);if(bind(server,(struct sockaddr*)&a,sizeof(a))<0){close(server);return 1;}if(listen(server,8)<0){close(server);return 1;}notify("ASTRO Remote v0.11 dynamic registry - porta 45821");
  while(running){int total;char method[16],path[ASTRO_PROXY_PATH_MAX],ver[16];char *he;const astro_service_t *svc;const char *backend=NULL;
    client=accept(server,NULL,NULL);if(client<0)continue;memset(buf,0,sizeof(buf));total=recv(client,buf,sizeof(buf)-1,0);if(total<=0){close(client);continue;}buf[total]='\0';he=strstr(buf,"\r\n\r\n");if(he){int cl=0;char *p=strstr(buf,"Content-Length:");if(p)cl=atoi(p+15);{int hs=(int)((he+4)-buf),br=total-hs;while(br<cl&&total<(int)sizeof(buf)-1){int r=recv(client,buf+total,sizeof(buf)-total-1,0);if(r<=0)break;total+=r;br+=r;buf[total]='\0';}}}
    method[0]=path[0]=ver[0]='\0';sscanf(buf,"%15s %2047s %15s",method,path,ver);svc=service_from_path(path,&backend);
    if(svc&&logged(buf)){char mark[80];snprintf(mark,sizeof(mark),"/service/%s/_root",svc->id);if(!strncmp(path,mark,strlen(mark))){const char *rest=path+strlen(mark);char root[ASTRO_PROXY_PATH_MAX];snprintf(root,sizeof(root),"/%s",*rest=='/'?rest+1:rest);proxy_request(client,buf,total,svc,root);}else proxy_redirect(client,svc);close(client);continue;}
    if(strstr(buf,"POST /login ")&&valid_login(buf))send_response(client,"302 Found","Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n","");
    else if(strstr(buf,"POST /logout ")&&logged(buf))send_response(client,"302 Found","Set-Cookie: astro_session=; Path=/; Max-Age=0; HttpOnly\r\nSet-Cookie: astro_proxy=; Path=/; Max-Age=0; HttpOnly\r\nLocation: /\r\n","");
    else if(strstr(buf,"GET /api/status ")&&logged(buf))send_status(client);
    else if(strstr(buf,"GET /api/services ")&&logged(buf))send_api_services(client);
    else if(strstr(buf,"POST /api/services/register ")&&logged(buf))send_register(client,buf);
    else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){send_response(client,"200 OK",NULL,shutdown_page);running=0;}
    else if(strstr(buf,"GET / ")&&logged(buf))send_dashboard(client);
    else if(logged(buf)&&(svc=service_from_cookie(buf))!=NULL)proxy_request(client,buf,total,svc,path);
    else send_response(client,"200 OK",NULL,login_page);
    close(client);
  }
  close(server);notify("ASTRO Remote encerrado");return 0;
}
