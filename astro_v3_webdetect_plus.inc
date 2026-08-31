/* Astro V3 deeper Web UI fingerprinting.
 * Reads a larger HTTP response and follows one local redirect before trying
 * to associate a listener with a running ELF.
 */

static int auto_http_read_path(int port,const char *path,char *title,size_t tsz,char *sample,size_t ssz,char *location,size_t lsz)
{
  if(title&&tsz)title[0]=0;if(sample&&ssz)sample[0]=0;if(location&&lsz)location[0]=0;
  int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return 0;
  struct timeval tv;tv.tv_sec=0;tv.tv_usec=350000;setsockopt(s,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));setsockopt(s,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
  struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((uint16_t)port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  if(connect(s,(struct sockaddr*)&a,sizeof(a))!=0){close(s);return 0;}
  char rq[2048];snprintf(rq,sizeof(rq),"GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\nUser-Agent: Astro-WebDetect/2\r\nAccept: text/html,application/json,*/*\r\nConnection: close\r\n\r\n",path&&*path?path:"/");
  if(send_all(s,rq,strlen(rq))!=0){close(s);return 0;}
  size_t u=0;while(u+1<ssz){ssize_t n=recv(s,sample+u,ssz-u-1,0);if(n<=0)break;u+=(size_t)n;}close(s);if(!u)return 0;sample[u]=0;
  int http=!strncmp(sample,"HTTP/",5)||auto_strcasestr_local(sample,"Content-Type:")||auto_strcasestr_local(sample,"<html")||auto_strcasestr_local(sample,"<!doctype");if(!http)return 0;
  if(title&&tsz)auto_extract_title(sample,title,tsz);
  if(location&&lsz){const char*q=auto_strcasestr_local(sample,"\r\nLocation:");if(q){q+=11;while(*q==' '||*q=='\t')q++;const char*e=strstr(q,"\r\n");if(e){size_t n=(size_t)(e-q);if(n>=lsz)n=lsz-1;memcpy(location,q,n);location[n]=0;}}}
  return 1;
}

static int auto_http_probe_deep(int port,char *title,size_t tsz,char *sample,size_t ssz)
{
  char loc[1024];if(!auto_http_read_path(port,"/",title,tsz,sample,ssz,loc,sizeof(loc)))return 0;
  if(loc[0]=='/'&&strlen(loc)<900){char t2[160],*tmp=malloc(ssz);if(tmp){if(auto_http_read_path(port,loc,t2,sizeof(t2),tmp,ssz,NULL,0)){snprintf(sample,ssz,"%s",tmp);if(t2[0])snprintf(title,tsz,"%s",t2);}free(tmp);}}
  return 1;
}

static int auto_payload_match_deep(const char *title,const char *sample,char *payload,size_t psz,char *hint,size_t hsz,int *pid)
{
  size_t sl=sample?strlen(sample):0,tl=title?strlen(title):0,cap=sl+tl+4;char*combo=malloc(cap);if(!combo)return auto_payload_match(title,sample,payload,psz,hint,hsz,pid);snprintf(combo,cap,"%s %s",title?title:"",sample?sample:"");
  char*compact=malloc(cap);if(!compact){free(combo);return auto_payload_match(title,sample,payload,psz,hint,hsz,pid);}auto_compact(combo,compact,cap);
  DIR*d=opendir(ASTRO_PAYLOAD_DIR);if(!d){free(combo);free(compact);return 0;}struct dirent*e;int best=0,best_pid=0;char best_name[256]={0},best_hint[128]={0};
  while((e=readdir(d))){if(e->d_name[0]=='.')continue;size_t ln=strlen(e->d_name);if(ln<5||strcasecmp(e->d_name+ln-4,".elf"))continue;int pp=v3_payload_pid(e->d_name);if(!pp)continue;char stem[256],key[256];auto_payload_stem(e->d_name,stem,sizeof(stem));auto_compact(stem,key,sizeof(key));int score=0;if(strlen(key)>=4&&strstr(compact,key))score=(int)strlen(key)+50;else{char tmp[256];snprintf(tmp,sizeof(tmp),"%s",stem);for(char*q=tmp;*q;q++)if(*q=='_'||*q=='-'||*q=='.')*q=' ';char*tok=strtok(tmp," ");while(tok){char tk[128];auto_compact(tok,tk,sizeof(tk));if(strlen(tk)>=5&&strstr(compact,tk)){int sc=(int)strlen(tk)+12;if(sc>score)score=sc;}tok=strtok(NULL," ");}}if(score>best){best=score;best_pid=pp;snprintf(best_name,sizeof(best_name),"%s",e->d_name);snprintf(best_hint,sizeof(best_hint),"%s",key);}}
  closedir(d);free(combo);free(compact);if(!best)return 0;snprintf(payload,psz,"%s",best_name);snprintf(hint,hsz,"%s",best_hint);if(pid)*pid=best_pid;return 1;
}

static void auto_web_discover(int force)
{
  time_t now=time(NULL);if(!force&&g_auto_web_scanned_at&&now-g_auto_web_scanned_at<AUTO_WEB_CACHE_SECONDS)return;g_auto_web_count=0;g_auto_web_scanned_at=now;
  int ports[256];int np=auto_scan_open_ports(ports,(int)(sizeof(ports)/sizeof(ports[0])));
  for(int i=0;i<np&&g_auto_web_count<AUTO_WEB_MAX;i++){
    int port=ports[i];if(port==ASTRO_PORT||port==9020||port==9021)continue;if(auto_port_known_manual(port))continue;
    char title[160];char*sample=malloc(32769);if(!sample)continue;if(!auto_http_probe_deep(port,title,sizeof(title),sample,32769)){free(sample);continue;}if(auto_web_has_port(port)){free(sample);continue;}
    astro_auto_web_t*w=&g_auto_web[g_auto_web_count];memset(w,0,sizeof(*w));w->port=port;char stem[256]={0};
    if(auto_payload_match_deep(title,sample,w->payload,sizeof(w->payload),w->hint,sizeof(w->hint),&w->pid)){auto_payload_stem(w->payload,stem,sizeof(stem));auto_slugify(stem,w->slug,sizeof(w->slug));if(!w->slug[0])snprintf(w->slug,sizeof(w->slug),"auto-%d",port);if(title[0])snprintf(w->label,sizeof(w->label),"%s",title);else snprintf(w->label,sizeof(w->label),"%s",stem[0]?stem:w->payload);}else{snprintf(w->slug,sizeof(w->slug),"auto-%d",port);snprintf(w->hint,sizeof(w->hint),"auto-%d",port);snprintf(w->label,sizeof(w->label),"%s",title[0]?title:"Web UI detectada");}
    snprintf(w->title,sizeof(w->title),"%s",title);free(sample);for(size_t k=0;k<g_auto_web_count;k++)if(!strcmp(g_auto_web[k].slug,w->slug)){size_t used=strlen(w->slug);snprintf(w->slug+used,sizeof(w->slug)-used,"-%d",port);break;}g_auto_web_count++;
  }
}

static int auto_web_lookup(const char *slug,int *port,char *label,size_t lsz,char *hint,size_t hsz)
{
  if(beta_web_lookup(slug,port,label,lsz,hint,hsz))return 1;auto_web_discover(0);for(size_t i=0;i<g_auto_web_count;i++)if(!strcmp(g_auto_web[i].slug,slug)){if(port)*port=g_auto_web[i].port;if(label&&lsz)snprintf(label,lsz,"%s",g_auto_web[i].label);if(hint&&hsz)snprintf(hint,hsz,"%s",g_auto_web[i].hint);return 1;}auto_web_discover(1);for(size_t i=0;i<g_auto_web_count;i++)if(!strcmp(g_auto_web[i].slug,slug)){if(port)*port=g_auto_web[i].port;if(label&&lsz)snprintf(label,lsz,"%s",g_auto_web[i].label);if(hint&&hsz)snprintf(hint,hsz,"%s",g_auto_web[i].hint);return 1;}return 0;
}

static void auto_webapps_api(int fd,const char *target)
{
  char force_s[16];int force=query_value(target,"scan",force_s,sizeof(force_s))?atoi(force_s):0;auto_web_discover(force?1:0);size_t cap=256*1024,u=0;char*j=malloc(cap);if(!j){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}u+=snprintf(j+u,cap-u,"{\"ok\":true,\"scan_age\":%lld,\"items\":[",(long long)(time(NULL)-g_auto_web_scanned_at));int first=1,idx=0;
  beta_seed_webapps();FILE*f=fopen(ASTRO_WEB_FILE,"r");char l[1024];while(f&&fgets(l,sizeof(l),f)&&u+4096<cap){char*nl=strpbrk(l,"\r\n");if(nl)*nl=0;if(!*l)continue;char*a=l,*b=strchr(a,'\t');if(!b)continue;*b++=0;char*c=strchr(b,'\t');if(!c)continue;*c++=0;char*d=strchr(c,'\t');if(!d)continue;*d++=0;int port=atoi(c),on=port_open(port),pid=v3_find_process(d);char es[256],el[512],eh[256];json_escape(a,es,sizeof(es));json_escape(b,el,sizeof(el));json_escape(d,eh,sizeof(eh));u+=snprintf(j+u,cap-u,"%s{\"index\":%d,\"slug\":\"%s\",\"label\":\"%s\",\"port\":%d,\"hint\":\"%s\",\"online\":%s,\"pid\":%d,\"route\":\"/web/%s/\",\"detected\":false}",first?"":",",idx++,es,el,port,eh,on?"true":"false",pid,es);first=0;}if(f)fclose(f);
  for(size_t i=0;i<g_auto_web_count&&u+4096<cap;i++){astro_auto_web_t*w=&g_auto_web[i];char es[256],el[512],eh[256],ep[768],et[512];json_escape(w->slug,es,sizeof(es));json_escape(w->label,el,sizeof(el));json_escape(w->hint,eh,sizeof(eh));json_escape(w->payload,ep,sizeof(ep));json_escape(w->title,et,sizeof(et));u+=snprintf(j+u,cap-u,"%s{\"index\":-1,\"slug\":\"%s\",\"label\":\"%s\",\"port\":%d,\"hint\":\"%s\",\"online\":true,\"pid\":%d,\"route\":\"/web/%s/\",\"detected\":true,\"payload\":\"%s\",\"title\":\"%s\"}",first?"":",",es,el,w->port,eh,w->pid,es,ep,et);first=0;}snprintf(j+u,cap-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}
