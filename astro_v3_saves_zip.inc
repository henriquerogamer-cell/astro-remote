/* Astro V3 save ZIP transport.
 * Whole-title saves are exported as a standard ZIP (stored entries, no compression)
 * so Windows/macOS/Linux can open them without extra tools. The browser receives
 * the ZIP directly; the PS5 temp file is deleted after sending.
 */

#define ASTRO_SAVE_ZIP_DIR ASTRO_DIR "/zip_tmp"
#define SAVE_ZIP_MAX_ENTRIES 512

typedef struct save_zip_entry {
  char name[512];
  uint32_t crc;
  uint32_t size;
  uint32_t offset;
} save_zip_entry_t;

/* Helpers are implemented by astro_v3_saves_cards.inc, included after this file. */
static int save_title_root(const char *uid,const char *tid,const char *type,char *out,size_t os);
static int save_rel_ok(const char *n);
static int save_snapshot_root(const char *root,const char *uid,const char *tid,char *out,size_t os);
static void save_clone_title_api(int fd,const char *target);

static int szip_write_all(int fd,const void *buf,size_t n)
{
  const unsigned char*p=(const unsigned char*)buf;while(n){ssize_t w=write(fd,p,n);if(w<=0)return -1;p+=w;n-=(size_t)w;}return 0;
}

static int szip_u16(int fd,uint16_t v){unsigned char b[2]={(unsigned char)v,(unsigned char)(v>>8)};return szip_write_all(fd,b,2);}
static int szip_u32(int fd,uint32_t v){unsigned char b[4]={(unsigned char)v,(unsigned char)(v>>8),(unsigned char)(v>>16),(unsigned char)(v>>24)};return szip_write_all(fd,b,4);}
static uint16_t szip_r16(const unsigned char*b){return (uint16_t)(b[0]|((uint16_t)b[1]<<8));}
static uint32_t szip_r32(const unsigned char*b){return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24);}

static uint32_t szip_crc_update(uint32_t crc,const unsigned char *p,size_t n)
{
  crc=~crc;while(n--){crc^=*p++;for(int k=0;k<8;k++)crc=(crc>>1)^(0xEDB88320U&((uint32_t)-(int)(crc&1)));}return ~crc;
}

static int szip_file_crc(const char *path,uint32_t *crc,uint32_t *size)
{
  struct stat st;if(stat(path,&st)!=0||!S_ISREG(st.st_mode)||st.st_size<0||(unsigned long long)st.st_size>0xffffffffULL)return -1;
  int f=open(path,O_RDONLY);if(f<0)return -2;unsigned char*b=malloc(256*1024);if(!b){close(f);return -3;}uint32_t c=0;for(;;){ssize_t n=read(f,b,256*1024);if(n==0)break;if(n<0){free(b);close(f);return -4;}c=szip_crc_update(c,b,(size_t)n);}free(b);close(f);*crc=c;*size=(uint32_t)st.st_size;return 0;
}

static int szip_add_file(int z,const char *root,const char *path,save_zip_entry_t *e)
{
  const char*rel=path+strlen(root);while(*rel=='/')rel++;if(!save_rel_ok(rel)||strlen(rel)>=sizeof(e->name))return -1;
  uint32_t crc=0,sz=0;if(szip_file_crc(path,&crc,&sz)!=0)return -2;off_t off=lseek(z,0,SEEK_CUR);if(off<0||(unsigned long long)off>0xffffffffULL)return -3;
  snprintf(e->name,sizeof(e->name),"%s",rel);e->crc=crc;e->size=sz;e->offset=(uint32_t)off;uint16_t nl=(uint16_t)strlen(e->name);
  if(szip_u32(z,0x04034b50U)||szip_u16(z,20)||szip_u16(z,0)||szip_u16(z,0)||szip_u16(z,0)||szip_u16(z,0)||szip_u32(z,crc)||szip_u32(z,sz)||szip_u32(z,sz)||szip_u16(z,nl)||szip_u16(z,0)||szip_write_all(z,e->name,nl))return -4;
  int f=open(path,O_RDONLY);if(f<0)return -5;unsigned char*b=malloc(256*1024);if(!b){close(f);return -6;}int rc=0;for(;;){ssize_t n=read(f,b,256*1024);if(n==0)break;if(n<0||szip_write_all(z,b,(size_t)n)){rc=-7;break;}}free(b);close(f);return rc;
}

static int szip_walk(int z,const char *root,const char *path,save_zip_entry_t *es,int *count)
{
  struct stat st;if(lstat(path,&st)!=0)return -1;if(S_ISREG(st.st_mode)){if(*count>=SAVE_ZIP_MAX_ENTRIES)return -2;int rc=szip_add_file(z,root,path,&es[*count]);if(!rc)(*count)++;return rc;}if(!S_ISDIR(st.st_mode))return 0;
  DIR*d=opendir(path);if(!d)return -3;struct dirent*de;int rc=0;while((de=readdir(d))){if(!strcmp(de->d_name,".")||!strcmp(de->d_name,".."))continue;char p[2048];snprintf(p,sizeof(p),"%s/%s",path,de->d_name);if((rc=szip_walk(z,root,p,es,count))!=0)break;}closedir(d);return rc;
}

static int szip_create(const char *root,const char *zip)
{
  v3_mkdirs(ASTRO_SAVE_ZIP_DIR);int z=open(zip,O_CREAT|O_TRUNC|O_RDWR,0666);if(z<0)return -1;save_zip_entry_t*es=calloc(SAVE_ZIP_MAX_ENTRIES,sizeof(*es));if(!es){close(z);return -2;}int count=0,rc=szip_walk(z,root,root,es,&count);if(rc||count==0){free(es);close(z);unlink(zip);return rc?rc:-3;}
  off_t cd0=lseek(z,0,SEEK_CUR);if(cd0<0||(unsigned long long)cd0>0xffffffffULL){free(es);close(z);unlink(zip);return -4;}
  for(int i=0;i<count;i++){save_zip_entry_t*e=&es[i];uint16_t nl=(uint16_t)strlen(e->name);if(szip_u32(z,0x02014b50U)||szip_u16(z,20)||szip_u16(z,20)||szip_u16(z,0)||szip_u16(z,0)||szip_u16(z,0)||szip_u16(z,0)||szip_u32(z,e->crc)||szip_u32(z,e->size)||szip_u32(z,e->size)||szip_u16(z,nl)||szip_u16(z,0)||szip_u16(z,0)||szip_u16(z,0)||szip_u16(z,0)||szip_u32(z,0)||szip_u32(z,e->offset)||szip_write_all(z,e->name,nl)){rc=-5;break;}}
  off_t cd1=lseek(z,0,SEEK_CUR);if(!rc&&(cd1<0||(unsigned long long)cd1>0xffffffffULL))rc=-6;if(!rc){uint32_t cds=(uint32_t)(cd1-cd0);if(szip_u32(z,0x06054b50U)||szip_u16(z,0)||szip_u16(z,0)||szip_u16(z,(uint16_t)count)||szip_u16(z,(uint16_t)count)||szip_u32(z,cds)||szip_u32(z,(uint32_t)cd0)||szip_u16(z,0))rc=-7;}fsync(z);free(es);close(z);if(rc)unlink(zip);return rc;
}

static int szip_extract(const char *zip,const char *dst)
{
  int f=open(zip,O_RDONLY);if(f<0)return -1;int rc=0;for(;;){unsigned char h[30];ssize_t n=read(f,h,4);if(n==0)break;if(n!=4){rc=-2;break;}uint32_t sig=szip_r32(h);if(sig==0x02014b50U||sig==0x06054b50U)break;if(sig!=0x04034b50U){rc=-3;break;}if(read(f,h+4,26)!=26){rc=-4;break;}uint16_t flags=szip_r16(h+6),method=szip_r16(h+8),nl=szip_r16(h+26),xl=szip_r16(h+28);uint32_t cs=szip_r32(h+18),us=szip_r32(h+22);if((flags&8)||method!=0||cs!=us||nl<1||nl>=512){rc=-5;break;}char name[512];if(read(f,name,nl)!=(ssize_t)nl){rc=-6;break;}name[nl]=0;if(!save_rel_ok(name)){rc=-7;break;}if(xl&&lseek(f,xl,SEEK_CUR)<0){rc=-8;break;}char out[2048];snprintf(out,sizeof(out),"%s/%s",dst,name);if(v3_parent_dirs(out)!=0){rc=-9;break;}int o=open(out,O_CREAT|O_TRUNC|O_WRONLY,0666);if(o<0){rc=-10;break;}unsigned char*b=malloc(256*1024);if(!b){close(o);rc=-11;break;}uint32_t left=cs;while(left){size_t want=left>256*1024?256*1024:left;ssize_t r=read(f,b,want);if(r<=0){rc=-12;break;}if(szip_write_all(o,b,(size_t)r)){rc=-13;break;}left-=(uint32_t)r;}fsync(o);free(b);close(o);if(rc)break;}
  close(f);return rc;
}

static void szip_send_attachment(int fd,const char *path,const char *name)
{
  struct stat st;if(stat(path,&st)!=0||!S_ISREG(st.st_mode)){send_text(fd,"404 Not Found","text/plain",NULL,"");return;}int f=open(path,O_RDONLY);if(f<0){send_text(fd,"404 Not Found","text/plain",NULL,"");return;}char h[1024];int hn=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Length: %llu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",name,(unsigned long long)st.st_size);if(send_all(fd,h,(size_t)hn)==0){unsigned char*b=malloc(256*1024);if(b){for(;;){ssize_t n=read(f,b,256*1024);if(n<=0)break;if(send_all(fd,b,(size_t)n)!=0)break;}free(b);}}close(f);
}

static void save_download_title_zip_api(int fd,const char *target)
{
  char uid[64],tid[96],type[16]="PS5",root[1024];if(!query_value(target,"uid",uid,sizeof(uid))||!query_value(target,"title",tid,sizeof(tid))||!save_title_root(uid,tid,query_value(target,"type",type,sizeof(type))?type:"PS5",root,sizeof(root))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}struct stat st;if(stat(root,&st)!=0||!S_ISDIR(st.st_mode)){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"save_not_found\"}");return;}v3_mkdirs(ASTRO_SAVE_ZIP_DIR);char zip[1024],name[180];snprintf(zip,sizeof(zip),"%s/%s_%s_%lld.zip",ASTRO_SAVE_ZIP_DIR,tid,uid,(long long)time(NULL));snprintf(name,sizeof(name),"Astro_%s_%s.zip",tid,uid);for(char*p=name;*p;p++)if(!(isalnum((unsigned char)*p)||*p=='_'||*p=='-'||*p=='.'))*p='_';int rc=szip_create(root,zip);if(rc){char j[128];snprintf(j,sizeof(j),"{\"ok\":false,\"rc\":%d}",rc);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}szip_send_attachment(fd,zip,name);unlink(zip);
}

static void save_restore_title_zip_api(int fd,const char *target)
{
  char uid[64],tid[96],type[16]="PS5",archive[2048],root[1024];if(!query_value(target,"uid",uid,sizeof(uid))||!query_value(target,"title",tid,sizeof(tid))||!query_value(target,"archive",archive,sizeof(archive))||!v3_safe_path(archive)||strncmp(archive,ASTRO_RESTORE_DIR,strlen(ASTRO_RESTORE_DIR))||!save_title_root(uid,tid,query_value(target,"type",type,sizeof(type))?type:"PS5",root,sizeof(root))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}struct stat st;if(stat(archive,&st)!=0||!S_ISREG(st.st_mode)){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"archive\"}");return;}time_t now=time(NULL);char tmp[1024];snprintf(tmp,sizeof(tmp),"%s/%s_%s_%lld",ASTRO_RESTORE_TMP,uid,tid,(long long)now);v3_remove_tree(tmp);if(v3_mkdirs(tmp)!=0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"tmp\"}");return;}int rc=szip_extract(archive,tmp);if(rc==0){char bak[1200];if(save_snapshot_root(root,uid,tid,bak,sizeof(bak))<0)rc=-20;if(rc==0){struct stat rs;if(stat(root,&rs)==0)v3_remove_tree(root);if(v3_copy_tree(tmp,root)!=0)rc=-21;}}v3_remove_tree(tmp);unlink(archive);char j[256];snprintf(j,sizeof(j),"{\"ok\":%s,\"rc\":%d}",rc==0?"true":"false",rc);send_text(fd,rc==0?"200 OK":"500 Internal Server Error","application/json",NULL,j);
}

/* Active authenticated dispatcher. Clone keeps using Garlic; export/restore use ZIP. */
static void v3_save_action_api(int fd,const char *target)
{
  char action[32]={0};query_value(target,"action",action,sizeof(action));if(!strcmp(action,"download")){save_download_title_zip_api(fd,target);return;}if(!strcmp(action,"restore")){save_restore_title_zip_api(fd,target);return;}if(!strcmp(action,"clone")){save_clone_title_api(fd,target);return;}v3_save_backup_api(fd,target);
}

static char *szip_replace_all(const char *src,const char *from,const char *to)
{
  size_t fl=strlen(from),tl=strlen(to),count=0;const char*p=src;while((p=strstr(p,from))){count++;p+=fl;}size_t sl=strlen(src),nl=sl+count*(tl-fl)+1;char*out=malloc(nl);if(!out)return NULL;char*d=out;p=src;const char*q;while((q=strstr(p,from))){size_t n=(size_t)(q-p);memcpy(d,p,n);d+=n;memcpy(d,to,tl);d+=tl;p=q+fl;}strcpy(d,p);return out;
}

/* While compiling the existing card UI, this wrapper swaps its TAR wording for ZIP. */
static void save_zip_ui_send_text(int fd,const char *status,const char *ctype,const char *extra,const char *body)
{
  if(ctype&&strstr(ctype,"text/html")&&body){char*a=szip_replace_all(body,"application/x-tar","application/zip");char*b=a?szip_replace_all(a,".tar",".zip"):NULL;if(a)free(a);if(b){send_text(fd,status,ctype,extra,b);free(b);return;}}send_text(fd,status,ctype,extra,body);
}
