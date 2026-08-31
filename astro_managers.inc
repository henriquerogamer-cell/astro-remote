#define ASTRO_PAYLOAD_DIR "/data/ProsperoMgr/payloads"
#define ASTRO_BACKUP_DIR ASTRO_DIR "/save_backups"
#define ASTRO_FAVORITES_FILE ASTRO_DIR "/payload_favorites.txt"

static int mgr_safe_path(const char *p)
{
  if (!p || p[0] != '/' || strstr(p, "..")) return 0;
  return strncmp(p,"/data",5)==0 || strncmp(p,"/user",5)==0 ||
         strncmp(p,"/mnt/usb",8)==0 || strncmp(p,"/mnt/ext",8)==0 ||
         strncmp(p,"/mnt/sandbox",12)==0;
}

static int mgr_payload_path(const char *p)
{
  if (!mgr_safe_path(p)) return 0;
  size_t n = strlen(p);
  return n > 4 && (!strcasecmp(p + n - 4, ".elf") || !strcasecmp(p + n - 4, ".bin"));
}

static int mgr_mkdirs(const char *path)
{
  if (!path || !*path) return -1;
  char tmp[2048];
  snprintf(tmp,sizeof(tmp),"%s",path);
  size_t n=strlen(tmp);
  if(n>1 && tmp[n-1]=='/') tmp[n-1]=0;
  for(char *p=tmp+1; *p; ++p){
    if(*p=='/'){
      *p=0;
      struct stat st;
      if(stat(tmp,&st)!=0 && mkdir(tmp,0777)!=0 && errno!=EEXIST){*p='/';return -1;}
      *p='/';
    }
  }
  struct stat st;
  if(stat(tmp,&st)!=0 && mkdir(tmp,0777)!=0 && errno!=EEXIST) return -1;
  return 0;
}

static int mgr_parent_dirs(const char *file)
{
  char d[2048]; snprintf(d,sizeof(d),"%s",file?file:"");
  char *s=strrchr(d,'/'); if(!s || s==d) return 0; *s=0;
  return mgr_mkdirs(d);
}

static int mgr_copy_file(const char *src,const char *dst)
{
  int in=open(src,O_RDONLY); if(in<0) return -1;
  if(mgr_parent_dirs(dst)!=0){close(in);return -2;}
  int out=open(dst,O_CREAT|O_TRUNC|O_WRONLY,0666); if(out<0){close(in);return -3;}
  char buf[256*1024]; int ret=0;
  for(;;){ssize_t n=read(in,buf,sizeof(buf)); if(n==0)break; if(n<0){ret=-4;break;}
    size_t off=0; while(off<(size_t)n){ssize_t w=write(out,buf+off,(size_t)n-off);if(w<=0){ret=-5;break;}off+=(size_t)w;} if(ret)break;}
  fsync(out); close(out); close(in); return ret;
}

static int mgr_copy_tree(const char *src,const char *dst)
{
  struct stat st; if(lstat(src,&st)!=0) return -1;
  if(S_ISREG(st.st_mode)) return mgr_copy_file(src,dst);
  if(!S_ISDIR(st.st_mode)) return -2;
  if(mgr_mkdirs(dst)!=0) return -3;
  DIR *d=opendir(src); if(!d)return -4; struct dirent *e; int ret=0;
  while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
    char a[2048],b[2048];snprintf(a,sizeof(a),"%s/%s",src,e->d_name);snprintf(b,sizeof(b),"%s/%s",dst,e->d_name);
    if(mgr_copy_tree(a,b)!=0){ret=-5;break;}}
  closedir(d); return ret;
}

static int mgr_remove_tree(const char *path)
{
  struct stat st; if(lstat(path,&st)!=0)return -1;
  if(S_ISDIR(st.st_mode)){
    DIR *d=opendir(path); if(!d)return -2; struct dirent *e; int ret=0;
    while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;
      char p[2048];snprintf(p,sizeof(p),"%s/%s",path,e->d_name);if(mgr_remove_tree(p)!=0){ret=-3;break;}}
    closedir(d); if(ret)return ret; return rmdir(path);
  }
  return unlink(path);
}

static int mgr_is_favorite(const char *name)
{
  FILE *f=fopen(ASTRO_FAVORITES_FILE,"r"); if(!f)return 0; char line[512]; int yes=0;
  while(fgets(line,sizeof(line),f)){char *n=strpbrk(line,"\r\n");if(n)*n=0;if(!strcmp(line,name)){yes=1;break;}}
  fclose(f);return yes;
}

static int mgr_toggle_favorite(const char *name)
{
  ensure_dir(ASTRO_DIR);
  char entries[128][256]; int count=0,found=0; FILE *f=fopen(ASTRO_FAVORITES_FILE,"r"); char line[512];
  if(f){while(fgets(line,sizeof(line),f)&&count<128){char *n=strpbrk(line,"\r\n");if(n)*n=0;if(!*line)continue;if(!strcmp(line,name)){found=1;continue;}snprintf(entries[count++],sizeof(entries[0]),"%s",line);}fclose(f);}
  if(!found&&count<128)snprintf(entries[count++],sizeof(entries[0]),"%s",name);
  f=fopen(ASTRO_FAVORITES_FILE,"w");if(!f)return -1;for(int i=0;i<count;i++)fprintf(f,"%s\n",entries[i]);fclose(f);return found?0:1;
}

static int mgr_loader_port(void)
{
  const int ports[]={9021,9020};
  for(size_t i=0;i<sizeof(ports)/sizeof(ports[0]);++i) if(port_open(ports[i])) return ports[i];
  return 0;
}

static int mgr_send_payload(const char *path,int *used_port)
{
  if(!mgr_payload_path(path))return -1;
  int fd=open(path,O_RDONLY);if(fd<0)return -2;
  const int ports[]={9021,9020}; int s=-1,port=0;
  for(size_t i=0;i<sizeof(ports)/sizeof(ports[0]);++i){
    s=socket(AF_INET,SOCK_STREAM,0);if(s<0)continue;struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((uint16_t)ports[i]);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    if(connect(s,(struct sockaddr*)&a,sizeof(a))==0){port=ports[i];break;}close(s);s=-1;
  }
  if(s<0){close(fd);return -3;}
  char b[256*1024];int ret=0;for(;;){ssize_t n=read(fd,b,sizeof(b));if(n==0)break;if(n<0){ret=-4;break;}if(send_all(s,b,(size_t)n)!=0){ret=-5;break;}}
  shutdown(s,SHUT_WR);close(s);close(fd);if(used_port)*used_port=port;return ret;
}

static void mgr_payload_list_api(int fd)
{
  ensure_dir(ASTRO_PAYLOAD_DIR);
  DIR *d=opendir(ASTRO_PAYLOAD_DIR); size_t cap=512*1024,used=0;char *j=malloc(cap);if(!j){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  used+=(size_t)snprintf(j+used,cap-used,"{\"ok\":true,\"dir\":\"%s\",\"loader_port\":%d,\"items\":[",ASTRO_PAYLOAD_DIR,mgr_loader_port());int first=1,count=0;
  if(d){struct dirent *e;while((e=readdir(d))&&count<500){if(e->d_name[0]=='.')continue;char p[2048];snprintf(p,sizeof(p),"%s/%s",ASTRO_PAYLOAD_DIR,e->d_name);struct stat st;if(stat(p,&st)!=0||!S_ISREG(st.st_mode)||!mgr_payload_path(p))continue;char en[1024],ep[4096];json_escape(e->d_name,en,sizeof(en));json_escape(p,ep,sizeof(ep));int n=snprintf(j+used,cap-used,"%s{\"name\":\"%s\",\"path\":\"%s\",\"size\":%llu,\"favorite\":%s}",first?"":",",en,ep,(unsigned long long)st.st_size,mgr_is_favorite(e->d_name)?"true":"false");if(n<0||used+(size_t)n+64>=cap)break;used+=(size_t)n;first=0;count++;}closedir(d);}
  snprintf(j+used,cap-used,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}

static void mgr_payload_launch_api(int fd,const char *target)
{
  char p[2048];if(!query_value(target,"path",p,sizeof(p))||!mgr_payload_path(p)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"path\"}");return;}
  int port=0,r=mgr_send_payload(p,&port);char j[256];snprintf(j,sizeof(j),"{\"ok\":%s,\"rc\":%d,\"port\":%d}",r==0?"true":"false",r,port);send_text(fd,r==0?"200 OK":"503 Service Unavailable","application/json",NULL,j);
}

static void mgr_payload_favorite_api(int fd,const char *target)
{
  char name[512];if(!query_value(target,"name",name,sizeof(name))||strchr(name,'/')){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}int state=mgr_toggle_favorite(name);char j[128];snprintf(j,sizeof(j),"{\"ok\":true,\"favorite\":%s}",state==1?"true":"false");send_text(fd,"200 OK","application/json",NULL,j);
}

static int mgr_recv_to_file(int client_fd,const char *req,size_t req_len,size_t header_len,const char *dst)
{
  if(!mgr_safe_path(dst)||mgr_parent_dirs(dst)!=0)return -1;long total=content_length(req);if(total<0)return -2;int out=open(dst,O_CREAT|O_TRUNC|O_WRONLY,0666);if(out<0)return -3;
  size_t have=req_len>header_len?req_len-header_len:0;if(have>(size_t)total)have=(size_t)total;if(have&&write(out,req+header_len,have)!=(ssize_t)have){close(out);return -4;}long remain=total-(long)have;char buf[256*1024];
  while(remain>0){size_t want=(size_t)(remain>(long)sizeof(buf)?sizeof(buf):remain);ssize_t n=recv(client_fd,buf,want,0);if(n<=0){close(out);return -5;}size_t off=0;while(off<(size_t)n){ssize_t w=write(out,buf+off,(size_t)n-off);if(w<=0){close(out);return -6;}off+=(size_t)w;}remain-=n;}
  fsync(out);close(out);return 0;
}

static void mgr_file_upload_api(int fd,const char *target,const char *req,size_t req_len,size_t header_len)
{
  char path[2048];if(!query_value(target,"path",path,sizeof(path))||!mgr_safe_path(path)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"path\"}");return;}int r=mgr_recv_to_file(fd,req,req_len,header_len,path);char j[160];snprintf(j,sizeof(j),"{\"ok\":%s,\"rc\":%d}",r==0?"true":"false",r);send_text(fd,r==0?"200 OK":"500 Internal Server Error","application/json",NULL,j);
}

static void mgr_file_mkdir_api(int fd,const char *target)
{
  char p[2048];if(!query_value(target,"path",p,sizeof(p))||!mgr_safe_path(p)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}int r=mgr_mkdirs(p);send_text(fd,r==0?"200 OK":"500 Internal Server Error","application/json",NULL,r==0?"{\"ok\":true}":"{\"ok\":false}");
}

static void mgr_file_touch_api(int fd,const char *target)
{
  char p[2048];if(!query_value(target,"path",p,sizeof(p))||!mgr_safe_path(p)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}mgr_parent_dirs(p);int f=open(p,O_CREAT|O_WRONLY,0666);if(f>=0)close(f);send_text(fd,f>=0?"200 OK":"500 Internal Server Error","application/json",NULL,f>=0?"{\"ok\":true}":"{\"ok\":false}");
}

static void mgr_file_delete_api(int fd,const char *target)
{
  char p[2048];if(!query_value(target,"path",p,sizeof(p))||!mgr_safe_path(p)||!strcmp(p,"/data")||!strcmp(p,"/user")){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}int r=mgr_remove_tree(p);send_text(fd,r==0?"200 OK":"500 Internal Server Error","application/json",NULL,r==0?"{\"ok\":true}":"{\"ok\":false}");
}

static void mgr_file_rename_api(int fd,const char *target)
{
  char p[2048],name[512];if(!query_value(target,"path",p,sizeof(p))||!query_value(target,"name",name,sizeof(name))||!mgr_safe_path(p)||strchr(name,'/')||strstr(name,"..")||!name[0]){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}char dst[2048];snprintf(dst,sizeof(dst),"%s",p);char *s=strrchr(dst,'/');if(!s){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}s[1]=0;strncat(dst,name,sizeof(dst)-strlen(dst)-1);int r=rename(p,dst);send_text(fd,r==0?"200 OK":"500 Internal Server Error","application/json",NULL,r==0?"{\"ok\":true}":"{\"ok\":false}");
}

static void mgr_file_copy_move_api(int fd,const char *target,int moving)
{
  char src[2048],dst[2048];if(!query_value(target,"src",src,sizeof(src))||!query_value(target,"dst",dst,sizeof(dst))||!mgr_safe_path(src)||!mgr_safe_path(dst)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false}");return;}int r;if(moving){mgr_parent_dirs(dst);r=rename(src,dst);if(r!=0){r=mgr_copy_tree(src,dst);if(r==0)r=mgr_remove_tree(src);}}else r=mgr_copy_tree(src,dst);send_text(fd,r==0?"200 OK":"500 Internal Server Error","application/json",NULL,r==0?"{\"ok\":true}":"{\"ok\":false}");
}

static void mgr_storage_api(int fd)
{
  struct statvfs a,b;unsigned long long at=0,af=0,bt=0,bf=0;if(statvfs("/data",&a)==0){at=(unsigned long long)a.f_blocks*a.f_frsize;af=(unsigned long long)a.f_bavail*a.f_frsize;}if(statvfs("/user",&b)==0){bt=(unsigned long long)b.f_blocks*b.f_frsize;bf=(unsigned long long)b.f_bavail*b.f_frsize;}char j[512];snprintf(j,sizeof(j),"{\"ok\":true,\"data\":{\"total\":%llu,\"free\":%llu},\"user\":{\"total\":%llu,\"free\":%llu}}",at,af,bt,bf);send_text(fd,"200 OK","application/json",NULL,j);
}

static void mgr_backup_save_api(int fd,const char *target)
{
  char src[2048],uid[128]="unknown",tid[128]="unknown";if(!query_value(target,"path",src,sizeof(src))||!mgr_safe_path(src)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"path\"}");return;}query_value(target,"uid",uid,sizeof(uid));query_value(target,"title",tid,sizeof(tid));
  for(char *p=uid;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='_'&&*p!='-')*p='_';for(char *p=tid;*p;p++)if(!isalnum((unsigned char)*p)&&*p!='_'&&*p!='-')*p='_';
  const char *base=strrchr(src,'/');base=base?base+1:src;time_t now=time(NULL);char dir[2048],dst[2048];snprintf(dir,sizeof(dir),"%s/%s/%s",ASTRO_BACKUP_DIR,uid,tid);mgr_mkdirs(dir);snprintf(dst,sizeof(dst),"%s/%lld_%s",dir,(long long)now,base);int r=mgr_copy_file(src,dst);char j[4096],ej[4096];json_escape(dst,ej,sizeof(ej));snprintf(j,sizeof(j),"{\"ok\":%s,\"rc\":%d,\"path\":\"%s\"}",r==0?"true":"false",r,ej);send_text(fd,r==0?"200 OK":"500 Internal Server Error","application/json",NULL,j);
}

static int mgr_local_request(int port,const char *method,const char *path,const char *file,char *body,size_t body_sz)
{
  int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return -1;struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons((uint16_t)port);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(connect(s,(struct sockaddr*)&a,sizeof(a))!=0){close(s);return -2;}
  unsigned long long sz=0;int f=-1;if(file){struct stat st;if(stat(file,&st)!=0||!S_ISREG(st.st_mode)){close(s);return -3;}sz=(unsigned long long)st.st_size;f=open(file,O_RDONLY);if(f<0){close(s);return -4;}}
  char h[4096];int hn=snprintf(h,sizeof(h),"%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n%s\r\n",method,path,port,file?"Content-Type: application/octet-stream\r\n":"");send_all(s,h,(size_t)hn);if(file){char cl[128];int n=snprintf(cl,sizeof(cl),"Content-Length: %llu\r\n\r\n",sz);send_all(s,cl,(size_t)n);char b[256*1024];for(;;){ssize_t nr=read(f,b,sizeof(b));if(nr==0)break;if(nr<0){close(f);close(s);return -5;}if(send_all(s,b,(size_t)nr)!=0){close(f);close(s);return -6;}}close(f);}else send_all(s,"\r\n",2);
  size_t used=0;char tmp[8192];for(;;){ssize_t n=recv(s,tmp,sizeof(tmp),0);if(n<=0)break;if(body&&body_sz>1&&used<body_sz-1){size_t take=(size_t)n;if(take>body_sz-1-used)take=body_sz-1-used;memcpy(body+used,tmp,take);used+=take;}}close(s);if(body&&body_sz){body[used]=0;char *p=strstr(body,"\r\n\r\n");if(p){p+=4;memmove(body,p,strlen(p)+1);}}return 0;
}

static void mgr_clone_save_api(int fd,const char *target)
{
  char src[2048],uid[128];if(!query_value(target,"path",src,sizeof(src))||!query_value(target,"uid",uid,sizeof(uid))||!mgr_safe_path(src)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}if(!port_open(GARLIC_PORT)){send_text(fd,"503 Service Unavailable","application/json",NULL,"{\"ok\":false,\"error\":\"garlic_offline\"}");return;}
  char path[512],resp[8192];snprintf(path,sizeof(path),"/api/import_encrypted?uid=%s",uid);int r=mgr_local_request(GARLIC_PORT,"POST",path,src,resp,sizeof(resp));if(r!=0||strstr(resp,"\"error\"")){char j[9000],e[8000];json_escape(resp,e,sizeof(e));snprintf(j,sizeof(j),"{\"ok\":false,\"stage\":\"import\",\"rc\":%d,\"response\":\"%s\"}",r,e);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}
  snprintf(path,sizeof(path),"/api/import_finish?uid=%s",uid);memset(resp,0,sizeof(resp));r=mgr_local_request(GARLIC_PORT,"GET",path,NULL,resp,sizeof(resp));if(r!=0||strstr(resp,"\"error\"")){char j[9000],e[8000];json_escape(resp,e,sizeof(e));snprintf(j,sizeof(j),"{\"ok\":false,\"stage\":\"finish\",\"rc\":%d,\"response\":\"%s\"}",r,e);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}
  send_text(fd,"200 OK","application/json",NULL,"{\"ok\":true}");
}
