/* Astro V3 beta: metadata/source registry, web-app gateway registry and title artwork. */

#define ASTRO_SOURCE_FILE ASTRO_DIR "/payload_sources.tsv"
#define ASTRO_WEB_FILE ASTRO_DIR "/webapps.tsv"

static int beta_clean_field(const char *s)
{
  if(!s || !*s) return 0;
  for(const unsigned char *p=(const unsigned char*)s; *p; ++p)
    if(*p<' ' || *p=='\t' || *p=='\r' || *p=='\n') return 0;
  return 1;
}

static int beta_safe_slug(const char *s)
{
  size_t n=s?strlen(s):0; if(n<1 || n>63) return 0;
  for(size_t i=0;i<n;i++) if(!(isalnum((unsigned char)s[i]) || s[i]=='-' || s[i]=='_')) return 0;
  return 1;
}

static void beta_seed_sources(void)
{
  struct stat st; if(stat(ASTRO_SOURCE_FILE,&st)==0) return;
  FILE *f=fopen(ASTRO_SOURCE_FILE,"w"); if(!f) return;
  fprintf(f,"kstuff\thttps://github.com/EchoStretch/kstuff\tkstuff\n");
  fprintf(f,"ftpsrv\thttps://github.com/ps5-payload-dev/ftpsrv\tftpsrv\n");
  fprintf(f,"websrv\thttps://github.com/ps5-payload-dev/websrv\twebsrv\n");
  fclose(f);
}

static void beta_seed_webapps(void)
{
  struct stat st; if(stat(ASTRO_WEB_FILE,&st)==0) return;
  FILE *f=fopen(ASTRO_WEB_FILE,"w"); if(!f) return;
  fprintf(f,"pegasus\tPegasus DL\t6970\tpegasus\n");
  fprintf(f,"prospero\tProspero Manager\t7070\tProsperoMgr\n");
  fclose(f);
}

static void beta_sources_api(int fd)
{
  beta_seed_sources(); FILE*f=fopen(ASTRO_SOURCE_FILE,"r");
  char *j=malloc(256*1024); if(!j){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  size_t u=0; u+=snprintf(j+u,256*1024-u,"{\"ok\":true,\"items\":[");
  char l[2048]; int first=1,idx=0;
  while(f && fgets(l,sizeof(l),f) && u+4096<256*1024){
    char *nl=strpbrk(l,"\r\n"); if(nl)*nl=0; if(!*l)continue;
    char *a=l,*b=strchr(a,'\t'); if(!b)continue; *b++=0; char*c=strchr(b,'\t'); if(!c)continue; *c++=0;
    char ea[512],eb[2048],ec[512]; json_escape(a,ea,sizeof(ea));json_escape(b,eb,sizeof(eb));json_escape(c,ec,sizeof(ec));
    u+=snprintf(j+u,256*1024-u,"%s{\"index\":%d,\"name\":\"%s\",\"url\":\"%s\",\"pattern\":\"%s\"}",first?"":",",idx++,ea,eb,ec); first=0;
  }
  if(f)fclose(f); snprintf(j+u,256*1024-u,"]}"); send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j); free(j);
}

static void beta_source_add_api(int fd,const char*target)
{
  char name[256],url[1536],pat[256];
  if(!query_value(target,"name",name,sizeof(name))||!query_value(target,"url",url,sizeof(url))||!query_value(target,"pattern",pat,sizeof(pat))||!beta_clean_field(name)||!beta_clean_field(url)||!beta_clean_field(pat)||(strncmp(url,"https://",8)&&strncmp(url,"http://",7))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}
  beta_seed_sources(); FILE*f=fopen(ASTRO_SOURCE_FILE,"a"); if(!f){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  fprintf(f,"%s\t%s\t%s\n",name,url,pat); fclose(f); send_text(fd,"200 OK","application/json",NULL,"{\"ok\":true}");
}

static void beta_source_delete_api(int fd,const char*target)
{
  char si[32]; if(!query_value(target,"index",si,sizeof(si))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;} int want=atoi(si);
  beta_seed_sources(); FILE*f=fopen(ASTRO_SOURCE_FILE,"r"); FILE*g=fopen(ASTRO_SOURCE_FILE ".tmp","w"); if(!f||!g){if(f)fclose(f);if(g)fclose(g);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  char l[2048];int i=0;while(fgets(l,sizeof(l),f)){if(i++!=want)fputs(l,g);}fclose(f);fclose(g);rename(ASTRO_SOURCE_FILE ".tmp",ASTRO_SOURCE_FILE);send_text(fd,"200 OK","application/json",NULL,"{\"ok\":true}");
}

static void beta_webapps_api(int fd)
{
  beta_seed_webapps(); FILE*f=fopen(ASTRO_WEB_FILE,"r"); char j[64*1024];size_t u=0;u+=snprintf(j+u,sizeof(j)-u,"{\"ok\":true,\"items\":[");char l[1024];int first=1,idx=0;
  while(f&&fgets(l,sizeof(l),f)&&u+2048<sizeof(j)){char*nl=strpbrk(l,"\r\n");if(nl)*nl=0;if(!*l)continue;char*a=l,*b=strchr(a,'\t');if(!b)continue;*b++=0;char*c=strchr(b,'\t');if(!c)continue;*c++=0;char*d=strchr(c,'\t');if(!d)continue;*d++=0;int port=atoi(c);char es[256],el[512],eh[256];json_escape(a,es,sizeof(es));json_escape(b,el,sizeof(el));json_escape(d,eh,sizeof(eh));u+=snprintf(j+u,sizeof(j)-u,"%s{\"index\":%d,\"slug\":\"%s\",\"label\":\"%s\",\"port\":%d,\"hint\":\"%s\",\"online\":%s,\"pid\":%d,\"route\":\"/web/%s/\"}",first?"":",",idx++,es,el,port,eh,port_open(port)?"true":"false",v3_find_process(d),es);first=0;}if(f)fclose(f);snprintf(j+u,sizeof(j)-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);
}

static void beta_webapp_add_api(int fd,const char*target)
{
  char slug[96],label[256],sp[32],hint[128]; if(!query_value(target,"slug",slug,sizeof(slug))||!query_value(target,"label",label,sizeof(label))||!query_value(target,"port",sp,sizeof(sp))||!query_value(target,"hint",hint,sizeof(hint))||!beta_safe_slug(slug)||!beta_clean_field(label)||!beta_clean_field(hint)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}int port=atoi(sp);if(port<1||port>65535){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"port\"}");return;}beta_seed_webapps();FILE*f=fopen(ASTRO_WEB_FILE,"a");if(!f){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}fprintf(f,"%s\t%s\t%d\t%s\n",slug,label,port,hint);fclose(f);send_text(fd,"200 OK","application/json",NULL,"{\"ok\":true}");
}

static int beta_web_lookup(const char*slug,int*port,char*label,size_t lsz,char*hint,size_t hsz)
{
  beta_seed_webapps();FILE*f=fopen(ASTRO_WEB_FILE,"r");char l[1024];int ok=0;while(f&&fgets(l,sizeof(l),f)){char*nl=strpbrk(l,"\r\n");if(nl)*nl=0;char*a=l,*b=strchr(a,'\t');if(!b)continue;*b++=0;char*c=strchr(b,'\t');if(!c)continue;*c++=0;char*d=strchr(c,'\t');if(!d)continue;*d++=0;if(strcmp(a,slug))continue;*port=atoi(c);snprintf(label,lsz,"%s",b);snprintf(hint,hsz,"%s",d);ok=1;break;}if(f)fclose(f);return ok;
}

static int beta_title_path(const char*tid,const char*leaf,char*out,size_t os)
{
  if(!tid||!*tid||strstr(tid,"..")||strchr(tid,'/'))return 0;
  const char*fmts[]={"/user/app/%s/sce_sys/%s","/user/appmeta/%s/%s","/system_data/priv/appmeta/%s/%s"};
  struct stat st;for(size_t i=0;i<sizeof(fmts)/sizeof(fmts[0]);i++){snprintf(out,os,fmts[i],tid,leaf);if(stat(out,&st)==0&&S_ISREG(st.st_mode))return 1;}return 0;
}

static void beta_title_icon_api(int fd,const char*target)
{
  char tid[64],p[512];if(!query_value(target,"tid",tid,sizeof(tid))||!beta_title_path(tid,"icon0.png",p,sizeof(p))){send_text(fd,"404 Not Found","text/plain",NULL,"");return;}struct stat st;if(stat(p,&st)!=0||st.st_size<=0||st.st_size>16*1024*1024){send_text(fd,"404 Not Found","text/plain",NULL,"");return;}int f=open(p,O_RDONLY);if(f<0){send_text(fd,"404 Not Found","text/plain",NULL,"");return;}void*b=malloc((size_t)st.st_size);if(!b){close(f);send_text(fd,"500 Internal Server Error","text/plain",NULL,"");return;}size_t o=0;while(o<(size_t)st.st_size){ssize_t n=read(f,(char*)b+o,(size_t)st.st_size-o);if(n<=0)break;o+=(size_t)n;}close(f);if(o!=(size_t)st.st_size){free(b);send_text(fd,"500 Internal Server Error","text/plain",NULL,"");return;}send_bytes(fd,"200 OK","image/png","Cache-Control: private, max-age=60\r\n",b,o);free(b);
}

static void beta_title_meta_api(int fd,const char*target)
{
  char tid[64],p[512];if(!query_value(target,"tid",tid,sizeof(tid))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}char name[256]={0};if(beta_title_path(tid,"param.json",p,sizeof(p))){int f=open(p,O_RDONLY);if(f>=0){char*buf=malloc(512*1024+1);if(buf){ssize_t n=read(f,buf,512*1024);if(n>0){buf[n]=0;char*q=strstr(buf,"\"titleName\"");if(q){q=strchr(q,':');if(q){q++;while(*q&&(*q==' '||*q=='\t'))q++;if(*q=='\"'){q++;size_t z=0;while(*q&&*q!='\"'&&z+1<sizeof(name)){if(*q=='\\'&&q[1])q++;name[z++]=*q++;}name[z]=0;}}}}free(buf);}close(f);}}char en[768];json_escape(name[0]?name:tid,en,sizeof(en));char j[1024];snprintf(j,sizeof(j),"{\"ok\":true,\"title_id\":\"%s\",\"name\":\"%s\",\"icon\":%s}",tid,en,beta_title_path(tid,"icon0.png",p,sizeof(p))?"true":"false");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);
}
