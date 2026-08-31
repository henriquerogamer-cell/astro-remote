/* Astro V3 automatic Web UI discovery.
 *
 * Instead of requiring every payload Web UI to be pre-registered, Astro scans
 * loopback TCP listeners, probes HTTP responders and correlates their page
 * identity with running ELFs from the Astro payload library. Manual entries in
 * webapps.tsv remain supported as overrides/fallbacks.
 */

#include <sys/select.h>
#include <sys/time.h>

#define AUTO_WEB_MAX 64
#define AUTO_WEB_CACHE_SECONDS 45

typedef struct astro_auto_web {
  int port;
  int pid;
  char slug[64];
  char label[160];
  char hint[128];
  char payload[256];
  char title[160];
} astro_auto_web_t;

static astro_auto_web_t g_auto_web[AUTO_WEB_MAX];
static size_t g_auto_web_count = 0;
static time_t g_auto_web_scanned_at = 0;

static const char *auto_strcasestr_local(const char *hay, const char *needle)
{
  if (!hay || !needle || !*needle) return NULL;
  size_t n = strlen(needle);
  for (const char *p=hay; *p; ++p) {
    size_t i=0;
    while (i<n && p[i] && tolower((unsigned char)p[i])==tolower((unsigned char)needle[i])) i++;
    if (i==n) return p;
  }
  return NULL;
}

static void auto_compact(const char *in, char *out, size_t os)
{
  size_t u=0;
  if (!os) return;
  for (const unsigned char *p=(const unsigned char*)(in?in:""); *p && u+1<os; ++p)
    if (isalnum(*p)) out[u++]=(char)tolower(*p);
  out[u]=0;
}

static void auto_payload_stem(const char *name, char *out, size_t os)
{
  snprintf(out,os,"%s",name?name:"");
  char *dot=strrchr(out,'.');
  if (dot && !strcasecmp(dot,".elf")) *dot=0;

  /* Strip common trailing version suffixes: _1.4.3, -v1.2, .v0.4, etc. */
  for (char *p=out; *p; ++p) {
    if ((*p=='_'||*p=='-'||*p=='.') && p[1]) {
      const char *q=p+1;
      if (*q=='v'||*q=='V') q++;
      if (isdigit((unsigned char)*q)) {
        const char *r=q;
        int dots=0;
        while (*r && (isdigit((unsigned char)*r)||*r=='.'||*r=='-'||*r=='_'||isalpha((unsigned char)*r))) {
          if (*r=='.') dots++;
          r++;
        }
        if (dots>0 && !*r) { *p=0; break; }
      }
    }
  }
}

static void auto_slugify(const char *in, char *out, size_t os)
{
  size_t u=0; int dash=0;
  if (!os) return;
  for (const unsigned char *p=(const unsigned char*)(in?in:""); *p && u+1<os; ++p) {
    if (isalnum(*p)) { out[u++]=(char)tolower(*p); dash=0; }
    else if (!dash && u && u+1<os) { out[u++]='-'; dash=1; }
  }
  while (u && out[u-1]=='-') u--;
  out[u]=0;
}

static int auto_port_known_manual(int port)
{
  beta_seed_webapps();
  FILE *f=fopen(ASTRO_WEB_FILE,"r");
  if (!f) return 0;
  char l[1024]; int yes=0;
  while (fgets(l,sizeof(l),f)) {
    char *a=l,*b=strchr(a,'\t'); if(!b)continue; *b++=0;
    char *c=strchr(b,'\t'); if(!c)continue; *c++=0;
    char *d=strchr(c,'\t'); if(!d)continue; *d++=0;
    if (atoi(c)==port) { yes=1; break; }
  }
  fclose(f); return yes;
}

static int auto_scan_open_ports(int *out, int max_out)
{
  int found=0;
  const int batch=192;

  for (int base=1; base<=65535 && found<max_out; base+=batch) {
    int fds[batch], ports[batch], nf=0, maxfd=-1;
    fd_set wfds; FD_ZERO(&wfds);

    for (int p=base; p<base+batch && p<=65535; ++p) {
      if (p==ASTRO_PORT) continue;
      int s=socket(AF_INET,SOCK_STREAM,0);
      if (s<0) continue;
      int fl=fcntl(s,F_GETFL,0);
      if (fl>=0) fcntl(s,F_SETFL,fl|O_NONBLOCK);

      struct sockaddr_in a; memset(&a,0,sizeof(a));
      a.sin_family=AF_INET; a.sin_port=htons((uint16_t)p); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
      int rc=connect(s,(struct sockaddr*)&a,sizeof(a));
      if (rc==0) {
        if (found<max_out) out[found++]=p;
        close(s); continue;
      }
      if (errno==EINPROGRESS || errno==EWOULDBLOCK) {
        if (s<FD_SETSIZE && nf<batch) {
          fds[nf]=s; ports[nf]=p; nf++; FD_SET(s,&wfds); if(s>maxfd)maxfd=s;
        } else close(s);
      } else close(s);
    }

    if (nf>0 && maxfd>=0) {
      struct timeval tv; tv.tv_sec=0; tv.tv_usec=12000;
      int sr=select(maxfd+1,NULL,&wfds,NULL,&tv);
      if (sr>0) {
        for (int i=0;i<nf && found<max_out;i++) if(FD_ISSET(fds[i],&wfds)) {
          int err=1; socklen_t el=sizeof(err);
          if(getsockopt(fds[i],SOL_SOCKET,SO_ERROR,&err,&el)==0 && err==0) out[found++]=ports[i];
        }
      }
      for(int i=0;i<nf;i++) close(fds[i]);
    }
  }
  return found;
}

static void auto_extract_title(const char *buf, char *out, size_t os)
{
  if (!os) return; out[0]=0;
  const char *a=auto_strcasestr_local(buf,"<title");
  if (a) {
    a=strchr(a,'>');
    if (a) {
      a++;
      const char *b=auto_strcasestr_local(a,"</title>");
      if (b) {
        size_t n=(size_t)(b-a); if(n>=os)n=os-1;
        size_t u=0; int ws=0;
        for(size_t i=0;i<n && u+1<os;i++) {
          unsigned char c=(unsigned char)a[i];
          if(isspace(c)) { if(u && !ws) out[u++]=' '; ws=1; }
          else { out[u++]=(char)c; ws=0; }
        }
        while(u&&out[u-1]==' ')u--; out[u]=0;
        if(out[0]) return;
      }
    }
  }

  const char *s=auto_strcasestr_local(buf,"\r\nServer:");
  if (s) {
    s+=9; while(*s==' '||*s=='\t')s++;
    const char *e=strstr(s,"\r\n");
    if(e){size_t n=(size_t)(e-s);if(n>=os)n=os-1;memcpy(out,s,n);out[n]=0;}
  }
}

static int auto_http_probe(int port, char *title, size_t tsz, char *sample, size_t ssz)
{
  if (title&&tsz) title[0]=0;
  if (sample&&ssz) sample[0]=0;
  int s=socket(AF_INET,SOCK_STREAM,0); if(s<0)return 0;
  struct timeval tv; tv.tv_sec=0; tv.tv_usec=220000;
  setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));

  struct sockaddr_in a; memset(&a,0,sizeof(a));
  a.sin_family=AF_INET; a.sin_port=htons((uint16_t)port); a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(connect(s,(struct sockaddr*)&a,sizeof(a))!=0){close(s);return 0;}

  const char *rq="GET / HTTP/1.0\r\nHost: 127.0.0.1\r\nUser-Agent: Astro-WebDetect/1\r\nConnection: close\r\n\r\n";
  if(send_all(s,rq,strlen(rq))!=0){close(s);return 0;}
  char buf[8193]; ssize_t n=recv(s,buf,sizeof(buf)-1,0); close(s);
  if(n<=0)return 0; buf[n]=0;

  int http=!strncmp(buf,"HTTP/",5) || auto_strcasestr_local(buf,"Content-Type:") || auto_strcasestr_local(buf,"<html") || auto_strcasestr_local(buf,"<!doctype");
  if(!http)return 0;
  if(title&&tsz) auto_extract_title(buf,title,tsz);
  if(sample&&ssz){size_t z=(size_t)n;if(z>=ssz)z=ssz-1;memcpy(sample,buf,z);sample[z]=0;}
  return 1;
}

static int auto_payload_match(const char *title, const char *sample, char *payload, size_t psz, char *hint, size_t hsz, int *pid)
{
  char combo[12288]; snprintf(combo,sizeof(combo),"%s %s",title?title:"",sample?sample:"");
  char compact[12288]; auto_compact(combo,compact,sizeof(compact));

  DIR *d=opendir(ASTRO_PAYLOAD_DIR); if(!d)return 0;
  struct dirent *e; int best=0, best_pid=0; char best_name[256]={0},best_hint[128]={0};
  while((e=readdir(d))) {
    if(e->d_name[0]=='.')continue;
    size_t ln=strlen(e->d_name); if(ln<5||strcasecmp(e->d_name+ln-4,".elf"))continue;
    int pp=v3_payload_pid(e->d_name); if(!pp)continue;
    char stem[256],key[256]; auto_payload_stem(e->d_name,stem,sizeof(stem)); auto_compact(stem,key,sizeof(key));
    int score=0;
    if(strlen(key)>=4 && strstr(compact,key)) score=(int)strlen(key)+30;
    else {
      /* Also try meaningful leading tokens for names such as ProsperoMgr. */
      char tmp[256];snprintf(tmp,sizeof(tmp),"%s",stem);
      for(char*q=tmp;*q;q++)if(*q=='_'||*q=='-'||*q=='.')*q=' ';
      char *tok=strtok(tmp," ");
      while(tok){char tk[128];auto_compact(tok,tk,sizeof(tk));if(strlen(tk)>=5&&strstr(compact,tk)){int sc=(int)strlen(tk)+10;if(sc>score)score=sc;}tok=strtok(NULL," ");}
    }
    if(score>best){best=score;best_pid=pp;snprintf(best_name,sizeof(best_name),"%s",e->d_name);snprintf(best_hint,sizeof(best_hint),"%s",key);}
  }
  closedir(d);
  if(!best)return 0;
  snprintf(payload,psz,"%s",best_name);snprintf(hint,hsz,"%s",best_hint);if(pid)*pid=best_pid;return 1;
}

static int auto_web_has_port(int port)
{
  for(size_t i=0;i<g_auto_web_count;i++)if(g_auto_web[i].port==port)return 1;
  return 0;
}

static void auto_web_discover(int force)
{
  time_t now=time(NULL);
  if(!force && g_auto_web_scanned_at && now-g_auto_web_scanned_at<AUTO_WEB_CACHE_SECONDS)return;
  g_auto_web_count=0; g_auto_web_scanned_at=now;

  int ports[256]; int np=auto_scan_open_ports(ports,(int)(sizeof(ports)/sizeof(ports[0])));
  for(int i=0;i<np && g_auto_web_count<AUTO_WEB_MAX;i++) {
    int port=ports[i];
    if(port==ASTRO_PORT || port==9020 || port==9021)continue;
    if(auto_port_known_manual(port))continue;

    char title[160],sample[8193];
    if(!auto_http_probe(port,title,sizeof(title),sample,sizeof(sample)))continue;
    if(auto_web_has_port(port))continue;

    astro_auto_web_t *w=&g_auto_web[g_auto_web_count]; memset(w,0,sizeof(*w)); w->port=port;
    char stem[256]={0};
    if(auto_payload_match(title,sample,w->payload,sizeof(w->payload),w->hint,sizeof(w->hint),&w->pid)) {
      auto_payload_stem(w->payload,stem,sizeof(stem));
      auto_slugify(stem,w->slug,sizeof(w->slug));
      if(!w->slug[0])snprintf(w->slug,sizeof(w->slug),"auto-%d",port);
      if(title[0])snprintf(w->label,sizeof(w->label),"%s",title);
      else snprintf(w->label,sizeof(w->label),"%s",stem[0]?stem:w->payload);
    } else {
      snprintf(w->slug,sizeof(w->slug),"auto-%d",port);
      snprintf(w->hint,sizeof(w->hint),"auto-%d",port);
      snprintf(w->label,sizeof(w->label),"%s",title[0]?title:"Web UI detectada");
    }
    snprintf(w->title,sizeof(w->title),"%s",title);

    /* Avoid slug collisions when two listeners belong to the same payload. */
    for(size_t k=0;k<g_auto_web_count;k++)if(!strcmp(g_auto_web[k].slug,w->slug)){
      size_t used=strlen(w->slug);snprintf(w->slug+used,sizeof(w->slug)-used,"-%d",port);break;
    }
    g_auto_web_count++;
  }
}

static int auto_web_lookup(const char *slug,int *port,char *label,size_t lsz,char *hint,size_t hsz)
{
  /* Explicit registrations win. */
  if(beta_web_lookup(slug,port,label,lsz,hint,hsz))return 1;
  auto_web_discover(0);
  for(size_t i=0;i<g_auto_web_count;i++)if(!strcmp(g_auto_web[i].slug,slug)){
    if(port)*port=g_auto_web[i].port;
    if(label&&lsz)snprintf(label,lsz,"%s",g_auto_web[i].label);
    if(hint&&hsz)snprintf(hint,hsz,"%s",g_auto_web[i].hint);
    return 1;
  }
  /* One forced refresh covers a payload that started after the cached scan. */
  auto_web_discover(1);
  for(size_t i=0;i<g_auto_web_count;i++)if(!strcmp(g_auto_web[i].slug,slug)){
    if(port)*port=g_auto_web[i].port;
    if(label&&lsz)snprintf(label,lsz,"%s",g_auto_web[i].label);
    if(hint&&hsz)snprintf(hint,hsz,"%s",g_auto_web[i].hint);
    return 1;
  }
  return 0;
}

static void auto_webapps_api(int fd,const char *target)
{
  char force_s[16];int force=query_value(target,"scan",force_s,sizeof(force_s))?atoi(force_s):0;
  auto_web_discover(force?1:0);

  size_t cap=256*1024,u=0;char*j=malloc(cap);if(!j){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  u+=snprintf(j+u,cap-u,"{\"ok\":true,\"scan_age\":%lld,\"items\":[",(long long)(time(NULL)-g_auto_web_scanned_at));
  int first=1,idx=0;

  beta_seed_webapps(); FILE*f=fopen(ASTRO_WEB_FILE,"r"); char l[1024];
  while(f&&fgets(l,sizeof(l),f)&&u+4096<cap){
    char*nl=strpbrk(l,"\r\n");if(nl)*nl=0;if(!*l)continue;
    char*a=l,*b=strchr(a,'\t');if(!b)continue;*b++=0;char*c=strchr(b,'\t');if(!c)continue;*c++=0;char*d=strchr(c,'\t');if(!d)continue;*d++=0;
    int port=atoi(c),on=port_open(port),pid=v3_find_process(d);char es[256],el[512],eh[256];json_escape(a,es,sizeof(es));json_escape(b,el,sizeof(el));json_escape(d,eh,sizeof(eh));
    u+=snprintf(j+u,cap-u,"%s{\"index\":%d,\"slug\":\"%s\",\"label\":\"%s\",\"port\":%d,\"hint\":\"%s\",\"online\":%s,\"pid\":%d,\"route\":\"/web/%s/\",\"detected\":false}",first?"":",",idx++,es,el,port,eh,on?"true":"false",pid,es);first=0;
  }
  if(f)fclose(f);

  for(size_t i=0;i<g_auto_web_count&&u+4096<cap;i++){
    astro_auto_web_t*w=&g_auto_web[i];char es[256],el[512],eh[256],ep[768],et[512];json_escape(w->slug,es,sizeof(es));json_escape(w->label,el,sizeof(el));json_escape(w->hint,eh,sizeof(eh));json_escape(w->payload,ep,sizeof(ep));json_escape(w->title,et,sizeof(et));
    u+=snprintf(j+u,cap-u,"%s{\"index\":-1,\"slug\":\"%s\",\"label\":\"%s\",\"port\":%d,\"hint\":\"%s\",\"online\":true,\"pid\":%d,\"route\":\"/web/%s/\",\"detected\":true,\"payload\":\"%s\",\"title\":\"%s\"}",first?"":",",es,el,w->port,eh,w->pid,es,ep,et);first=0;
  }
  snprintf(j+u,cap-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}
