/* Astro V3 runtime Web UI correlation.
 *
 * The library is not the source of truth for running payloads. A payload may
 * have been launched by WebKit, another loader or another manager and still
 * expose a Web UI. This detector therefore falls back to the real PS5 process
 * table and can correlate abbreviated process names (pldmgr -> Payload Manager)
 * by using compact subsequence matching.
 */

static int runtime_is_elf_process(const char *name)
{
  if(!name||!*name)return 0;
  size_t n=strlen(name);
  if(n<5||strcasecmp(name+n-4,".elf"))return 0;
  if(!strcasecmp(name,"astro_remote.elf"))return 0;
  return 1;
}

static int runtime_subsequence(const char *needle,const char *hay)
{
  if(!needle||!hay||!*needle)return 0;
  while(*hay&&*needle){if(*hay==*needle)needle++;hay++;}
  return !*needle;
}

static void runtime_proc_stem(const char *comm,char *out,size_t os)
{
  snprintf(out,os,"%s",comm?comm:"");
  char *dot=strrchr(out,'.');
  if(dot&&!strcasecmp(dot,".elf"))*dot=0;
}

static int auto_payload_match_runtime(const char *title,const char *sample,char *payload,size_t psz,char *hint,size_t hsz,int *pid)
{
  /* Prefer the normal library correlation when it already knows the ELF. */
  if(auto_payload_match_deep_legacy(title,sample,payload,psz,hint,hsz,pid))return 1;

  size_t sl=sample?strlen(sample):0,tl=title?strlen(title):0,cap=sl+tl+8;
  char *combo=malloc(cap),*compact=malloc(cap),title_compact[512];
  if(!combo||!compact){free(combo);free(compact);return 0;}
  snprintf(combo,cap,"%s %s",title?title:"",sample?sample:"");
  auto_compact(combo,compact,cap);
  auto_compact(title?title:"",title_compact,sizeof(title_compact));

  astro_proc_t *procs=NULL;size_t count=0;
  if(v3_process_snapshot(&procs,&count)!=0){free(combo);free(compact);return 0;}

  int best=0,best_pid=0;char best_name[128]={0},best_hint[128]={0};
  for(size_t i=0;i<count;i++){
    if(!runtime_is_elf_process(procs[i].comm))continue;
    char stem[128],key[128];runtime_proc_stem(procs[i].comm,stem,sizeof(stem));auto_compact(stem,key,sizeof(key));
    size_t kl=strlen(key);if(kl<4)continue;

    int score=0;
    if(title_compact[0]&&strstr(title_compact,key))score=220+(int)kl;
    else if(strstr(compact,key))score=180+(int)kl;
    else if(kl>=5&&title_compact[0]&&runtime_subsequence(key,title_compact))score=140+(int)kl;
    else if(kl>=6&&runtime_subsequence(key,compact))score=110+(int)kl;

    /* One extra signal for common compressed names: compare consonant-like
     * process abbreviations against the human readable title. pldmgr is a
     * subsequence of payloadmanager without hard-coding that application. */
    if(score>best){best=score;best_pid=procs[i].pid;snprintf(best_name,sizeof(best_name),"%s",procs[i].comm);snprintf(best_hint,sizeof(best_hint),"%s",key);}
  }
  free(procs);free(combo);free(compact);
  if(!best)return 0;
  snprintf(payload,psz,"%s",best_name);snprintf(hint,hsz,"%s",best_hint);if(pid)*pid=best_pid;return 1;
}

static void auto_web_discover(int force)
{
  time_t now=time(NULL);
  if(!force&&g_auto_web_scanned_at&&now-g_auto_web_scanned_at<AUTO_WEB_CACHE_SECONDS)return;
  g_auto_web_count=0;g_auto_web_scanned_at=now;

  int ports[256];int np=auto_scan_open_ports(ports,(int)(sizeof(ports)/sizeof(ports[0])));
  for(int i=0;i<np&&g_auto_web_count<AUTO_WEB_MAX;i++){
    int port=ports[i];
    if(port==ASTRO_PORT||port==9020||port==9021)continue;
    if(auto_port_known_manual(port))continue;

    char title[160];char *sample=malloc(32769);if(!sample)continue;
    if(!auto_http_probe_deep(port,title,sizeof(title),sample,32769)){free(sample);continue;}
    if(auto_web_has_port(port)){free(sample);continue;}

    astro_auto_web_t *w=&g_auto_web[g_auto_web_count];memset(w,0,sizeof(*w));w->port=port;char stem[256]={0};
    if(auto_payload_match_runtime(title,sample,w->payload,sizeof(w->payload),w->hint,sizeof(w->hint),&w->pid)){
      auto_payload_stem(w->payload,stem,sizeof(stem));auto_slugify(stem,w->slug,sizeof(w->slug));
      if(!w->slug[0])snprintf(w->slug,sizeof(w->slug),"auto-%d",port);
      if(title[0])snprintf(w->label,sizeof(w->label),"%s",title);else snprintf(w->label,sizeof(w->label),"%s",stem[0]?stem:w->payload);
    }else{
      snprintf(w->slug,sizeof(w->slug),"auto-%d",port);snprintf(w->hint,sizeof(w->hint),"auto-%d",port);snprintf(w->label,sizeof(w->label),"%s",title[0]?title:"Web UI detectada");
    }
    snprintf(w->title,sizeof(w->title),"%s",title);free(sample);
    for(size_t k=0;k<g_auto_web_count;k++)if(!strcmp(g_auto_web[k].slug,w->slug)){size_t used=strlen(w->slug);snprintf(w->slug+used,sizeof(w->slug)-used,"-%d",port);break;}
    g_auto_web_count++;
  }
}

static int auto_web_lookup(const char *slug,int *port,char *label,size_t lsz,char *hint,size_t hsz)
{
  if(beta_web_lookup(slug,port,label,lsz,hint,hsz))return 1;
  auto_web_discover(0);
  for(size_t i=0;i<g_auto_web_count;i++)if(!strcmp(g_auto_web[i].slug,slug)){
    if(port)*port=g_auto_web[i].port;if(label&&lsz)snprintf(label,lsz,"%s",g_auto_web[i].label);if(hint&&hsz)snprintf(hint,hsz,"%s",g_auto_web[i].hint);return 1;
  }
  auto_web_discover(1);
  for(size_t i=0;i<g_auto_web_count;i++)if(!strcmp(g_auto_web[i].slug,slug)){
    if(port)*port=g_auto_web[i].port;if(label&&lsz)snprintf(label,lsz,"%s",g_auto_web[i].label);if(hint&&hsz)snprintf(hint,hsz,"%s",g_auto_web[i].hint);return 1;
  }
  return 0;
}

static void auto_webapps_api(int fd,const char *target)
{
  char force_s[16];int force=query_value(target,"scan",force_s,sizeof(force_s))?atoi(force_s):0;
  auto_web_discover(force?1:0);
  size_t cap=256*1024,u=0;char*j=malloc(cap);if(!j){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  u+=snprintf(j+u,cap-u,"{\"ok\":true,\"scan_age\":%lld,\"items\":[",(long long)(time(NULL)-g_auto_web_scanned_at));int first=1,idx=0;

  beta_seed_webapps();FILE*f=fopen(ASTRO_WEB_FILE,"r");char l[1024];
  while(f&&fgets(l,sizeof(l),f)&&u+4096<cap){
    char*nl=strpbrk(l,"\r\n");if(nl)*nl=0;if(!*l)continue;char*a=l,*b=strchr(a,'\t');if(!b)continue;*b++=0;char*c=strchr(b,'\t');if(!c)continue;*c++=0;char*d=strchr(c,'\t');if(!d)continue;*d++=0;
    int port=atoi(c),on=port_open(port),pp=v3_find_process(d);char es[256],el[512],eh[256];json_escape(a,es,sizeof(es));json_escape(b,el,sizeof(el));json_escape(d,eh,sizeof(eh));
    u+=snprintf(j+u,cap-u,"%s{\"index\":%d,\"slug\":\"%s\",\"label\":\"%s\",\"port\":%d,\"hint\":\"%s\",\"online\":%s,\"pid\":%d,\"route\":\"/web/%s/\",\"detected\":false}",first?"":",",idx++,es,el,port,eh,on?"true":"false",pp,es);first=0;
  }
  if(f)fclose(f);

  for(size_t i=0;i<g_auto_web_count&&u+4096<cap;i++){
    astro_auto_web_t*w=&g_auto_web[i];char es[256],el[512],eh[256],ep[768],et[512];json_escape(w->slug,es,sizeof(es));json_escape(w->label,el,sizeof(el));json_escape(w->hint,eh,sizeof(eh));json_escape(w->payload,ep,sizeof(ep));json_escape(w->title,et,sizeof(et));
    u+=snprintf(j+u,cap-u,"%s{\"index\":-1,\"slug\":\"%s\",\"label\":\"%s\",\"port\":%d,\"hint\":\"%s\",\"online\":true,\"pid\":%d,\"route\":\"/web/%s/\",\"detected\":true,\"payload\":\"%s\",\"title\":\"%s\"}",first?"":",",es,el,w->port,eh,w->pid,es,ep,et);first=0;
  }
  snprintf(j+u,cap-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}
