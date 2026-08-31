typedef struct astro_proc {
  pid_t pid;
  pid_t ppid;
  uid_t uid;
  int state;
  unsigned long long rss;
  char comm[64];
} astro_proc_t;

typedef struct astro_service {
  const char *slug;
  const char *label;
  const char *hint;
  int port;
} astro_service_t;

static const astro_service_t ASTRO_SERVICES[] = {
  {"pegasus", "Pegasus DL", "pegasus", 6970}
};

int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);

static int v3_mkdirs(const char *path)
{
  if (!path || !*path) return -1;
  char tmp[2048]; snprintf(tmp,sizeof(tmp),"%s",path);
  size_t n=strlen(tmp); if(n>1 && tmp[n-1]=='/')tmp[n-1]=0;
  for(char *p=tmp+1;*p;++p){if(*p=='/'){*p=0;if(mkdir(tmp,0777)!=0&&errno!=EEXIST){*p='/';return -1;}*p='/';}}
  if(mkdir(tmp,0777)!=0&&errno!=EEXIST)return -1;
  return 0;
}

static int v3_parent_dirs(const char *file)
{
  char tmp[2048];snprintf(tmp,sizeof(tmp),"%s",file?file:"");char *p=strrchr(tmp,'/');if(!p||p==tmp)return 0;*p=0;return v3_mkdirs(tmp);
}

static int v3_safe_path(const char *p)
{
  if(!p||p[0]!='/'||strstr(p,".."))return 0;
  return !strncmp(p,"/data",5)||!strncmp(p,"/user",5)||!strncmp(p,"/mnt/usb",8)||!strncmp(p,"/mnt/ext",8)||!strncmp(p,"/mnt/sandbox",12);
}

static int v3_copy_file(const char *src,const char *dst)
{
  int in=open(src,O_RDONLY);if(in<0)return -1;if(v3_parent_dirs(dst)!=0){close(in);return -2;}
  int out=open(dst,O_CREAT|O_TRUNC|O_WRONLY,0666);if(out<0){close(in);return -3;}
  char *buf=malloc(256*1024);if(!buf){close(in);close(out);return -4;}int rc=0;
  for(;;){ssize_t n=read(in,buf,256*1024);if(n==0)break;if(n<0){rc=-5;break;}size_t off=0;while(off<(size_t)n){ssize_t w=write(out,buf+off,(size_t)n-off);if(w<=0){rc=-6;break;}off+=(size_t)w;}if(rc)break;}
  fsync(out);free(buf);close(out);close(in);return rc;
}

static int v3_copy_tree(const char *src,const char *dst)
{
  struct stat st;if(lstat(src,&st)!=0)return -1;if(S_ISREG(st.st_mode))return v3_copy_file(src,dst);if(!S_ISDIR(st.st_mode))return -2;
  if(v3_mkdirs(dst)!=0)return -3;DIR *d=opendir(src);if(!d)return -4;struct dirent *e;int rc=0;
  while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char a[2048],b[2048];snprintf(a,sizeof(a),"%s/%s",src,e->d_name);snprintf(b,sizeof(b),"%s/%s",dst,e->d_name);if(v3_copy_tree(a,b)!=0){rc=-5;break;}}
  closedir(d);return rc;
}

static int v3_remove_tree(const char *path)
{
  struct stat st;if(lstat(path,&st)!=0)return -1;if(S_ISDIR(st.st_mode)){DIR*d=opendir(path);if(!d)return -2;struct dirent*e;int rc=0;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char p[2048];snprintf(p,sizeof(p),"%s/%s",path,e->d_name);if(v3_remove_tree(p)!=0){rc=-3;break;}}closedir(d);if(rc)return rc;return rmdir(path);}return unlink(path);
}

static int v3_contains_ci(const char *hay,const char *needle)
{
  if(!hay||!needle||!*needle)return 0;size_t n=strlen(needle);for(const char *p=hay;*p;++p){size_t i=0;while(i<n&&p[i]&&tolower((unsigned char)p[i])==tolower((unsigned char)needle[i]))i++;if(i==n)return 1;}return 0;
}

static int v3_process_snapshot(astro_proc_t **out,size_t *count)
{
  *out=NULL;*count=0;int mib[4]={CTL_KERN,KERN_PROC,KERN_PROC_PROC,0};size_t sz=0;if(sysctl(mib,4,NULL,&sz,NULL,0)!=0||!sz)return -1;void *raw=malloc(sz);if(!raw)return -2;if(sysctl(mib,4,raw,&sz,NULL,0)!=0){free(raw);return -3;}
  size_t cap=128,n=0;astro_proc_t *arr=calloc(cap,sizeof(*arr));if(!arr){free(raw);return -4;}
  char *ptr=(char*)raw,*end=ptr+sz;while(ptr<end){struct kinfo_proc *ki=(struct kinfo_proc*)ptr;if(ki->ki_structsize<=0)break;if(n==cap){cap*=2;astro_proc_t*t=realloc(arr,cap*sizeof(*arr));if(!t)break;arr=t;}arr[n].pid=ki->ki_pid;arr[n].ppid=ki->ki_ppid;arr[n].uid=ki->ki_uid;arr[n].state=ki->ki_stat;arr[n].rss=(unsigned long long)ki->ki_rssize*(unsigned long long)PAGE_SIZE;snprintf(arr[n].comm,sizeof(arr[n].comm),"%s",ki->ki_comm);n++;ptr+=ki->ki_structsize;}
  free(raw);*out=arr;*count=n;return 0;
}

static pid_t v3_find_process(const char *hint)
{
  astro_proc_t *p=NULL;size_t n=0;if(v3_process_snapshot(&p,&n)!=0)return 0;pid_t found=0;for(size_t i=0;i<n;i++){if(v3_contains_ci(p[i].comm,hint)){found=p[i].pid;break;}}free(p);return found;
}

static void v3_processes_api(int fd)
{
  astro_proc_t *p=NULL;size_t n=0;if(v3_process_snapshot(&p,&n)!=0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  size_t cap=1024*1024,used=0;char*j=malloc(cap);if(!j){free(p);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}used+=snprintf(j+used,cap-used,"{\"ok\":true,\"items\":[");
  for(size_t i=0;i<n&&used+512<cap;i++){char ec[256];json_escape(p[i].comm,ec,sizeof(ec));used+=snprintf(j+used,cap-used,"%s{\"pid\":%d,\"ppid\":%d,\"uid\":%u,\"state\":%d,\"rss\":%llu,\"comm\":\"%s\"}",i?",":"",p[i].pid,p[i].ppid,(unsigned)p[i].uid,p[i].state,p[i].rss,ec);}snprintf(j+used,cap-used,"]}");free(p);send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}

static int v3_foreground_user(char uid[16],char name[64],char avatar[256])
{
  int u=0;sceUserServiceInitialize(NULL);if(sceUserServiceGetForegroundUser(&u)<0)return -1;snprintf(uid,16,"%08x",(unsigned)u);
  char np[256];snprintf(np,sizeof(np),"/user/home/%s/username.dat",uid);memset(name,0,64);int f=open(np,O_RDONLY);if(f>=0){read(f,name,16);close(f);}if(!name[0])snprintf(name,64,"User %s",uid);
  snprintf(avatar,256,"/system_data/priv/cache/profile/0x%s/avatar.png",uid);return 0;
}

static void v3_profile_api(int fd)
{
  char uid[16],name[64],av[256];if(v3_foreground_user(uid,name,av)!=0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}struct stat st;char en[160];json_escape(name,en,sizeof(en));char j[512];snprintf(j,sizeof(j),"{\"ok\":true,\"user_id\":\"%s\",\"name\":\"%s\",\"avatar\":%s}",uid,en,stat(av,&st)==0?"true":"false");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);
}

static void v3_profile_avatar(int fd)
{
  char uid[16],name[64],av[256];if(v3_foreground_user(uid,name,av)!=0){send_text(fd,"404 Not Found","text/plain",NULL,"");return;}struct stat st;if(stat(av,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<=0||st.st_size>8*1024*1024){send_text(fd,"404 Not Found","text/plain",NULL,"");return;}int f=open(av,O_RDONLY);if(f<0){send_text(fd,"404 Not Found","text/plain",NULL,"");return;}void*b=malloc((size_t)st.st_size);if(!b){close(f);send_text(fd,"500 Internal Server Error","text/plain",NULL,"");return;}ssize_t n=read(f,b,(size_t)st.st_size);close(f);if(n!=st.st_size){free(b);send_text(fd,"500 Internal Server Error","text/plain",NULL,"");return;}send_bytes(fd,"200 OK","image/png",NULL,b,(size_t)n);free(b);
}

static int v3_valid_profile_name(const char *s)
{
  size_t n=s?strlen(s):0;if(n<1||n>16)return 0;for(size_t i=0;i<n;i++)if((unsigned char)s[i]<0x20)return 0;return 1;
}

static void v3_profile_name_api(int fd,const char *target)
{
  char newname[64];if(!query_value(target,"name",newname,sizeof(newname))||!v3_valid_profile_name(newname)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"name\"}");return;}
  char uid[16],old[64],av[256];if(v3_foreground_user(uid,old,av)!=0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  char src[256],bakdir[256],bak[320];snprintf(src,sizeof(src),"/user/home/%s/username.dat",uid);snprintf(bakdir,sizeof(bakdir),"%s/%s",ASTRO_PROFILE_BACKUP_DIR,uid);v3_mkdirs(bakdir);snprintf(bak,sizeof(bak),"%s/username.dat.bak",bakdir);struct stat st;if(stat(bak,&st)!=0)v3_copy_file(src,bak);
  int f=open(src,O_CREAT|O_TRUNC|O_WRONLY,0644);if(f<0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"open\"}");return;}char buf[16]={0};snprintf(buf,sizeof(buf),"%s",newname);ssize_t w=write(f,buf,sizeof(buf));fsync(f);close(f);send_text(fd,w==(ssize_t)sizeof(buf)?"200 OK":"500 Internal Server Error","application/json",NULL,w==(ssize_t)sizeof(buf)?"{\"ok\":true,\"note\":\"profile cache may refresh after user/session reload\"}":"{\"ok\":false}");
}

static int v3_is_favorite(const char *name)
{
  FILE*f=fopen(ASTRO_FAVORITES_FILE,"r");if(!f)return 0;char l[512];int y=0;while(fgets(l,sizeof(l),f)){char*n=strpbrk(l,"\r\n");if(n)*n=0;if(!strcmp(l,name)){y=1;break;}}fclose(f);return y;
}

static int v3_toggle_favorite(const char *name)
{
  char entries[128][256];int c=0,found=0;FILE*f=fopen(ASTRO_FAVORITES_FILE,"r");char l[512];if(f){while(fgets(l,sizeof(l),f)&&c<128){char*n=strpbrk(l,"\r\n");if(n)*n=0;if(!*l)continue;if(!strcmp(l,name)){found=1;continue;}snprintf(entries[c++],256,"%s",l);}fclose(f);}if(!found&&c<128)snprintf(entries[c++],256,"%s",name);f=fopen(ASTRO_FAVORITES_FILE,"w");if(!f)return -1;for(int i=0;i<c;i++)fprintf(f,"%s\n",entries[i]);fclose(f);return found?0:1;
}

static int v3_payload_file(const char *p)
{
  if(!p||strncmp(p,ASTRO_PAYLOAD_DIR,strlen(ASTRO_PAYLOAD_DIR))||strstr(p,".."))return 0;size_t n=strlen(p);return n>4&&!strcasecmp(p+n-4,".elf");
}

static void v3_payload_hint(const char *name,char *out,size_t os)
{
  snprintf(out,os,"%s",name?name:"");char*p=strrchr(out,'.');if(p&&!strcasecmp(p,".elf"))*p=0;for(char*q=out;*q;q++){if(*q=='_'||*q==' ')*q='-';}
}

static int v3_payload_pid(const char *name)
{
  char hint[256];v3_payload_hint(name,hint,sizeof(hint));pid_t p=v3_find_process(hint);if(p)return p;char *dash=strchr(hint,'-');if(dash&&dash-hint>=4){*dash=0;p=v3_find_process(hint);}return p;
}

static int v3_loader_port(void)
{
  const int ports[]={9021,9020};for(size_t i=0;i<2;i++)if(port_open(ports[i]))return ports[i];return 0;
}

static int v3_send_payload(const char *path,int *used_port)
{
  if(!v3_payload_file(path))return -1;int f=open(path,O_RDONLY);if(f<0)return -2;int ports[]={9021,9020},s=-1,port=0;for(int i=0;i<2;i++){s=socket(AF_INET,SOCK_STREAM,0);if(s<0)continue;struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((uint16_t)ports[i]);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(connect(s,(struct sockaddr*)&a,sizeof(a))==0){port=ports[i];break;}close(s);s=-1;}if(s<0){close(f);return -3;}char*b=malloc(256*1024);if(!b){close(f);close(s);return -4;}int rc=0;for(;;){ssize_t n=read(f,b,256*1024);if(n==0)break;if(n<0||send_all(s,b,(size_t)n)!=0){rc=-5;break;}}free(b);shutdown(s,SHUT_WR);close(s);close(f);if(used_port)*used_port=port;return rc;
}

static void v3_migrate_payloads_once(void)
{
  v3_mkdirs(ASTRO_PAYLOAD_DIR);DIR*d=opendir(ASTRO_PAYLOAD_DIR);int any=0;if(d){struct dirent*e;while((e=readdir(d)))if(e->d_name[0]!='.'){any=1;break;}closedir(d);}if(any)return;
  const char *old="/data/ProsperoMgr/payloads";d=opendir(old);if(!d)return;struct dirent*e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;size_t n=strlen(e->d_name);if(n<5||strcasecmp(e->d_name+n-4,".elf"))continue;char a[1024],b[1024];snprintf(a,sizeof(a),"%s/%s",old,e->d_name);snprintf(b,sizeof(b),"%s/%s",ASTRO_PAYLOAD_DIR,e->d_name);v3_copy_file(a,b);}closedir(d);
}

static void v3_payload_list_api(int fd)
{
  v3_mkdirs(ASTRO_PAYLOAD_DIR);DIR*d=opendir(ASTRO_PAYLOAD_DIR);size_t cap=512*1024,u=0;char*j=malloc(cap);if(!j){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}u+=snprintf(j+u,cap-u,"{\"ok\":true,\"dir\":\"%s\",\"loader_port\":%d,\"items\":[",ASTRO_PAYLOAD_DIR,v3_loader_port());int first=1;if(d){struct dirent*e;while((e=readdir(d))&&u+1024<cap){if(e->d_name[0]=='.')continue;char p[1024];snprintf(p,sizeof(p),"%s/%s",ASTRO_PAYLOAD_DIR,e->d_name);struct stat st;if(stat(p,&st)!=0||!S_ISREG(st.st_mode)||!v3_payload_file(p))continue;char en[512];json_escape(e->d_name,en,sizeof(en));int pid=v3_payload_pid(e->d_name);u+=snprintf(j+u,cap-u,"%s{\"name\":\"%s\",\"path\":\"%s\",\"size\":%llu,\"favorite\":%s,\"running\":%s,\"pid\":%d}",first?"":",",en,p,(unsigned long long)st.st_size,v3_is_favorite(e->d_name)?"true":"false",pid?"true":"false",pid);first=0;}closedir(d);}snprintf(j+u,cap-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}

static void v3_payload_launch_api(int fd,const char *target)
{
  char path[1024];if(!query_value(target,"path",path,sizeof(path))||!v3_payload_file(path)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}int port=0,rc=v3_send_payload(path,&port);char j[128];snprintf(j,sizeof(j),"{\"ok\":%s,\"rc\":%d,\"loader\":%d}",rc==0?"true":"false",rc,port);send_text(fd,rc==0?"200 OK":"503 Service Unavailable","application/json",NULL,j);
}

static void v3_payload_stop_api(int fd,const char *target)
{
  char name[512];if(!query_value(target,"name",name,sizeof(name))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}int pid=v3_payload_pid(name);if(!pid){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"not_running\"}");return;}int rc=kill(pid,SIGTERM);char j[128];snprintf(j,sizeof(j),"{\"ok\":%s,\"pid\":%d}",rc==0?"true":"false",pid);send_text(fd,rc==0?"200 OK":"500 Internal Server Error","application/json",NULL,j);
}

static void v3_payload_favorite_api(int fd,const char *target)
{
  char name[512];if(!query_value(target,"name",name,sizeof(name))||strchr(name,'/')){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}int s=v3_toggle_favorite(name);char j[96];snprintf(j,sizeof(j),"{\"ok\":true,\"favorite\":%s}",s==1?"true":"false");send_text(fd,"200 OK","application/json",NULL,j);
}

static int v3_recv_body_file(int fd,const char *req,size_t req_len,size_t header_len,const char *dst)
{
  if(!v3_safe_path(dst)||v3_parent_dirs(dst)!=0)return -1;long total=content_length(req);if(total<0)return -2;int o=open(dst,O_CREAT|O_TRUNC|O_WRONLY,0666);if(o<0)return -3;size_t have=req_len>header_len?req_len-header_len:0;if(have>(size_t)total)have=(size_t)total;if(have&&write(o,req+header_len,have)!=(ssize_t)have){close(o);return -4;}long remain=total-(long)have;char*b=malloc(256*1024);if(!b){close(o);return -5;}int rc=0;while(remain>0){size_t want=(size_t)(remain>256*1024?256*1024:remain);ssize_t n=recv(fd,b,want,0);if(n<=0){rc=-6;break;}size_t off=0;while(off<(size_t)n){ssize_t w=write(o,b+off,(size_t)n-off);if(w<=0){rc=-7;break;}off+=(size_t)w;}if(rc)break;remain-=n;}fsync(o);free(b);close(o);return rc;
}

static void v3_payload_upload_api(int fd,const char *target,const char *req,size_t req_len,size_t header_len)
{
  char name[512];if(!query_value(target,"name",name,sizeof(name))||strchr(name,'/')||strstr(name,"..")){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}size_t n=strlen(name);if(n<5||strcasecmp(name+n-4,".elf")){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"elf_only\"}");return;}char dst[1024];snprintf(dst,sizeof(dst),"%s/%s",ASTRO_PAYLOAD_DIR,name);int rc=v3_recv_body_file(fd,req,req_len,header_len,dst);send_text(fd,rc==0?"200 OK":"500 Internal Server Error","application/json",NULL,rc==0?"{\"ok\":true}":"{\"ok\":false}");
}

static void v3_file_list_api(int fd,const char *target)
{
  char path[2048]="/data";query_value(target,"path",path,sizeof(path));if(!v3_safe_path(path)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}DIR*d=opendir(path);if(!d){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"open\"}");return;}size_t cap=1024*1024,u=0;char*j=malloc(cap);if(!j){closedir(d);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}char ep[4096];json_escape(path,ep,sizeof(ep));u+=snprintf(j+u,cap-u,"{\"ok\":true,\"path\":\"%s\",\"items\":[",ep);struct dirent*e;int first=1;while((e=readdir(d))&&u+1024<cap){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char full[4096];snprintf(full,sizeof(full),"%s/%s",path,e->d_name);struct stat st;if(lstat(full,&st)!=0)continue;char en[1024];json_escape(e->d_name,en,sizeof(en));u+=snprintf(j+u,cap-u,"%s{\"name\":\"%s\",\"dir\":%s,\"size\":%llu,\"mtime\":%lld}",first?"":",",en,S_ISDIR(st.st_mode)?"true":"false",(unsigned long long)st.st_size,(long long)st.st_mtime);first=0;}closedir(d);snprintf(j+u,cap-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}

static void v3_file_download(int fd,const char *target)
{
  char path[2048];if(!query_value(target,"path",path,sizeof(path))||!v3_safe_path(path)){send_text(fd,"400 Bad Request","text/plain",NULL,"Caminho invalido");return;}struct stat st;if(stat(path,&st)!=0||!S_ISREG(st.st_mode)){send_text(fd,"404 Not Found","text/plain",NULL,"Arquivo nao encontrado");return;}int f=open(path,O_RDONLY);if(f<0){send_text(fd,"403 Forbidden","text/plain",NULL,"Sem acesso");return;}const char*base=strrchr(path,'/');base=base?base+1:path;char safe[512];size_t k=0;for(size_t i=0;base[i]&&k+1<sizeof(safe);i++){char c=base[i];safe[k++]=(c=='\"'||c=='\r'||c=='\n')?'_':c;}safe[k]=0;char h[1024];int hn=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %llu\r\nContent-Disposition: attachment; filename=\"%s\"\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",(unsigned long long)st.st_size,safe);send_all(fd,h,(size_t)hn);char*b=malloc(256*1024);if(b){for(;;){ssize_t n=read(f,b,256*1024);if(n<=0)break;if(send_all(fd,b,(size_t)n)!=0)break;}free(b);}close(f);
}

static void v3_file_upload_api(int fd,const char *target,const char *req,size_t req_len,size_t header_len)
{
  char path[2048];if(!query_value(target,"path",path,sizeof(path))||!v3_safe_path(path)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}int rc=v3_recv_body_file(fd,req,req_len,header_len,path);send_text(fd,rc==0?"200 OK":"500 Internal Server Error","application/json",NULL,rc==0?"{\"ok\":true}":"{\"ok\":false}");
}

static void v3_file_action_api(int fd,const char *target,const char *op)
{
  char p[2048],dst[2048],name[512];p[0]=dst[0]=name[0]=0;query_value(target,"path",p,sizeof(p));query_value(target,"dst",dst,sizeof(dst));query_value(target,"name",name,sizeof(name));int rc=-1;
  if(!strcmp(op,"mkdir")){if(v3_safe_path(p))rc=v3_mkdirs(p);}else if(!strcmp(op,"touch")){if(v3_safe_path(p)){v3_parent_dirs(p);int f=open(p,O_CREAT|O_WRONLY,0666);if(f>=0){close(f);rc=0;}}}else if(!strcmp(op,"delete")){if(v3_safe_path(p)&&strcmp(p,"/data")&&strcmp(p,"/user"))rc=v3_remove_tree(p);}else if(!strcmp(op,"rename")){if(v3_safe_path(p)&&name[0]&&!strchr(name,'/')&&!strstr(name,"..")){char d[2048];snprintf(d,sizeof(d),"%s",p);char*s=strrchr(d,'/');if(s){s[1]=0;strncat(d,name,sizeof(d)-strlen(d)-1);rc=rename(p,d);}}}else if(!strcmp(op,"copy")){if(v3_safe_path(p)&&v3_safe_path(dst))rc=v3_copy_tree(p,dst);}else if(!strcmp(op,"move")){if(v3_safe_path(p)&&v3_safe_path(dst)){v3_parent_dirs(dst);rc=rename(p,dst);if(rc!=0){rc=v3_copy_tree(p,dst);if(rc==0)rc=v3_remove_tree(p);}}}
  send_text(fd,rc==0?"200 OK":"500 Internal Server Error","application/json",NULL,rc==0?"{\"ok\":true}":"{\"ok\":false}");
}

static void v3_storage_api(int fd)
{
  struct statvfs a,b;unsigned long long at=0,af=0,bt=0,bf=0;if(statvfs("/data",&a)==0){at=(unsigned long long)a.f_blocks*a.f_frsize;af=(unsigned long long)a.f_bavail*a.f_frsize;}if(statvfs("/user",&b)==0){bt=(unsigned long long)b.f_blocks*b.f_frsize;bf=(unsigned long long)b.f_bavail*b.f_frsize;}char j[512];snprintf(j,sizeof(j),"{\"ok\":true,\"data\":{\"total\":%llu,\"free\":%llu},\"user\":{\"total\":%llu,\"free\":%llu}}",at,af,bt,bf);send_text(fd,"200 OK","application/json",NULL,j);
}

static void v3_users_api(int fd)
{
  DIR*d=opendir("/user/home");char j[8192];size_t u=0;u+=snprintf(j+u,sizeof(j)-u,"{\"ok\":true,\"users\":[");int first=1;if(d){struct dirent*e;while((e=readdir(d))&&u+512<sizeof(j)){if(e->d_name[0]=='.')continue;char np[256],name[64]={0};snprintf(np,sizeof(np),"/user/home/%s/username.dat",e->d_name);int f=open(np,O_RDONLY);if(f>=0){read(f,name,16);close(f);}if(!name[0])snprintf(name,sizeof(name),"User %s",e->d_name);char en[160];json_escape(name,en,sizeof(en));u+=snprintf(j+u,sizeof(j)-u,"%s{\"id\":\"%s\",\"name\":\"%s\"}",first?"":",",e->d_name,en);first=0;}closedir(d);}snprintf(j+u,sizeof(j)-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);
}

static void v3_scan_save_root(char **j,size_t *u,size_t cap,const char *uid,const char *root,int ps4,int *first)
{
  char base[512];snprintf(base,sizeof(base),"/user/home/%s/%s",uid,root);DIR*d=opendir(base);if(!d)return;struct dirent*t;while((t=readdir(d))&&*u+2048<cap){if(t->d_name[0]=='.')continue;char td[1024];snprintf(td,sizeof(td),"%s/%s",base,t->d_name);struct stat ts;if(stat(td,&ts)!=0||!S_ISDIR(ts.st_mode))continue;DIR*sd=opendir(td);if(!sd)continue;struct dirent*s;while((s=readdir(sd))&&*u+2048<cap){if(s->d_name[0]=='.')continue;if(ps4&&strncmp(s->d_name,"sdimg_",6))continue;size_t ln=strlen(s->d_name);if(ln>4&&!strcmp(s->d_name+ln-4,".bin"))continue;char fp[1536];snprintf(fp,sizeof(fp),"%s/%s",td,s->d_name);struct stat st;if(stat(fp,&st)!=0||!S_ISREG(st.st_mode))continue;char ep[2048],es[1024];json_escape(fp,ep,sizeof(ep));json_escape(s->d_name,es,sizeof(es));*u+=snprintf(*j+*u,cap-*u,"%s{\"uid\":\"%s\",\"title_id\":\"%s\",\"save_name\":\"%s\",\"path\":\"%s\",\"type\":\"%s\",\"size\":%llu}",*first?"":",",uid,t->d_name,es,ep,ps4?"PS4":"PS5",(unsigned long long)st.st_size);*first=0;}closedir(sd);}closedir(d);
}

static void v3_saves_api(int fd)
{
  size_t cap=2*1024*1024,u=0;char*j=malloc(cap);if(!j){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}u+=snprintf(j+u,cap-u,"{\"ok\":true,\"items\":[");DIR*h=opendir("/user/home");int first=1;if(h){struct dirent*e;while((e=readdir(h))&&u+4096<cap){if(e->d_name[0]=='.')continue;v3_scan_save_root(&j,&u,cap,e->d_name,"savedata_prospero",0,&first);v3_scan_save_root(&j,&u,cap,e->d_name,"savedata",1,&first);}closedir(h);}snprintf(j+u,cap-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}

static void v3_save_backup_api(int fd,const char *target)
{
  char path[2048],uid[32],tid[64];if(!query_value(target,"path",path,sizeof(path))||!query_value(target,"uid",uid,sizeof(uid))||!query_value(target,"title",tid,sizeof(tid))||strncmp(path,"/user/home/",11)||strstr(path,"..")||strchr(uid,'/')||strchr(tid,'/')){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}struct stat st;if(stat(path,&st)!=0||!S_ISREG(st.st_mode)){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false}");return;}time_t now=time(NULL);char dir[1024];snprintf(dir,sizeof(dir),"%s/%s/%s/%lld",ASTRO_BACKUP_DIR,uid,tid,(long long)now);v3_mkdirs(dir);const char*base=strrchr(path,'/');base=base?base+1:path;char dst[1536];snprintf(dst,sizeof(dst),"%s/%s",dir,base);int rc=v3_copy_file(path,dst);
  char bin[2048];snprintf(bin,sizeof(bin),"%s.bin",path);if(stat(bin,&st)==0&&S_ISREG(st.st_mode)){char bd[1536];snprintf(bd,sizeof(bd),"%s/%s.bin",dir,base);v3_copy_file(bin,bd);}char ed[1800];json_escape(dir,ed,sizeof(ed));char j[2048];snprintf(j,sizeof(j),"{\"ok\":%s,\"path\":\"%s\"}",rc==0?"true":"false",ed);send_text(fd,rc==0?"200 OK":"500 Internal Server Error","application/json",NULL,j);
}

static const astro_service_t *v3_service_slug(const char *slug)
{
  for(size_t i=0;i<sizeof(ASTRO_SERVICES)/sizeof(ASTRO_SERVICES[0]);i++)if(!strcmp(ASTRO_SERVICES[i].slug,slug))return &ASTRO_SERVICES[i];return NULL;
}

static void v3_services_api(int fd)
{
  char j[4096];size_t u=0;u+=snprintf(j+u,sizeof(j)-u,"{\"ok\":true,\"items\":[");for(size_t i=0;i<sizeof(ASTRO_SERVICES)/sizeof(ASTRO_SERVICES[0]);i++){int on=port_open(ASTRO_SERVICES[i].port);int pid=v3_find_process(ASTRO_SERVICES[i].hint);u+=snprintf(j+u,sizeof(j)-u,"%s{\"slug\":\"%s\",\"label\":\"%s\",\"online\":%s,\"pid\":%d,\"route\":\"/apps/%s/\"}",i?",":"",ASTRO_SERVICES[i].slug,ASTRO_SERVICES[i].label,on?"true":"false",pid,ASTRO_SERVICES[i].slug);}snprintf(j+u,sizeof(j)-u,"]}");send_text(fd,"200 OK","application/json",NULL,j);
}

static int v3_proxy_service(int client,const char *req,size_t req_len,size_t header_len,const astro_service_t *svc,const char *upath)
{
  int u=socket(AF_INET,SOCK_STREAM,0);if(u<0)return -1;struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((uint16_t)svc->port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(connect(u,(struct sockaddr*)&a,sizeof(a))!=0){close(u);send_text(client,"502 Bad Gateway","text/html; charset=utf-8",NULL,"<h1>Serviço offline</h1><p>O payload não está expondo a interface web agora.</p>");return -2;}
  char method[16],target[4096];if(parse_request_line(req,method,sizeof(method),target,sizeof(target))!=0){close(u);return -3;}char first[4600];int fn=snprintf(first,sizeof(first),"%s %s HTTP/1.1\r\n",method,upath);send_all(u,first,(size_t)fn);const char*line=strstr(req,"\r\n");const char*p=line?line+2:NULL,*end=req+header_len-2;if(!p){close(u);return -4;}while(p<end){const char*e=strstr(p,"\r\n");if(!e||e> end||e==p)break;size_t ln=(size_t)(e-p);if(!strncasecmp(p,"Host:",5)){char h[128];int n=snprintf(h,sizeof(h),"Host: 127.0.0.1:%d\r\n",svc->port);send_all(u,h,(size_t)n);}else if(!strncasecmp(p,"Connection:",11)){send_all(u,"Connection: close\r\n",19);}else{send_all(u,p,ln);send_all(u,"\r\n",2);}p=e+2;}send_all(u,"\r\n",2);size_t already=req_len>header_len?req_len-header_len:0;long total=content_length(req);if(already)send_all(u,req+header_len,already);long remain=total-(long)already;char*b=malloc(256*1024);if(!b){close(u);return -5;}while(remain>0){size_t want=(size_t)(remain>256*1024?256*1024:remain);ssize_t n=recv(client,b,want,0);if(n<=0)break;if(send_all(u,b,(size_t)n)!=0)break;remain-=n;}for(;;){ssize_t n=recv(u,b,256*1024,0);if(n<=0)break;if(send_all(client,b,(size_t)n)!=0)break;}free(b);close(u);return 0;
}

static int v3_route_service(const char *target,const astro_service_t **svc,const char **upath)
{
  if(strncmp(target,"/apps/",6))return 0;const char*p=target+6;const char*s=strchr(p,'/');if(!s)return 0;char slug[64];size_t n=(size_t)(s-p);if(n>=sizeof(slug))return 0;memcpy(slug,p,n);slug[n]=0;const astro_service_t*x=v3_service_slug(slug);if(!x)return 0;*svc=x;*upath=s;return 1;
}

static void v3_status_api(int fd)
{
  char uid[16]="",name[64]="",av[256]="";v3_foreground_user(uid,name,av);char en[160];json_escape(name,en,sizeof(en));char j[1024];snprintf(j,sizeof(j),"{\"ok\":true,\"version\":\"3.0-alpha\",\"port\":%d,\"loader\":%d,\"profile\":{\"user_id\":\"%s\",\"name\":\"%s\"}}",ASTRO_PORT,v3_loader_port(),uid,en);send_text(fd,"200 OK","application/json",NULL,j);
}
