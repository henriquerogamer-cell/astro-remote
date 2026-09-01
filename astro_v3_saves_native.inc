/* Astro V3 native PS5 save clone engine.
 * No external save-manager process is required. The clone is performed inside
 * Astro: copy source image -> mount -> resign SFO -> unmount/re-encrypt ->
 * replace destination title -> rebuild destination savedata.db rows.
 */
#include <sys/ioctl.h>
#include <dlfcn.h>
#include <sqlite3.h>

#define ASTRO_NATIVE_SAVE_DIR ASTRO_DIR "/native_clone"
#define ASTRO_NATIVE_MOUNT ASTRO_DIR "/native_mount"
#define ASTRO_NATIVE_MAX_SLOTS 128
#define ASTRO_NATIVE_BUF (256 * 1024)

typedef struct { uint8_t reserved; char *budgetid; } astro_mount_opt_t;
typedef struct { uint8_t dummy; } astro_umount_opt_t;

int sceFsInitMountSaveDataOpt(astro_mount_opt_t *opt);
int sceFsMountSaveData(astro_mount_opt_t *opt, const char *path, const char *mount, uint8_t *key);
int sceFsInitUmountSaveDataOpt(astro_umount_opt_t *opt);
int sceFsUmountSaveData(astro_umount_opt_t *opt, const char *mount, int handle, int ignore);
int sceRegMgrGetBin(int key, void *buf, size_t size);
int sceRegMgrGetStr(int key, char *buf, size_t size);

typedef struct astro_native_sfo {
  char title_id[32];
  char dir_name[256];
  char main_title[256];
  char sub_title[256];
  char detail[512];
  char game_title_id[32];
  char format[8];
  uint32_t user_param;
  uint32_t blocks;
  uint64_t account_id;
  uint64_t system_blocks;
} astro_native_sfo_t;

typedef struct astro_native_slot {
  astro_native_sfo_t sfo;
  char image[1024];
  char icon[1024];
  unsigned long long image_size;
} astro_native_slot_t;

static int native_save_copy_file(const char *src,const char *dst)
{
  int s=open(src,O_RDONLY);if(s<0)return -1;if(v3_parent_dirs(dst)!=0){close(s);return -2;}
  int d=open(dst,O_CREAT|O_TRUNC|O_WRONLY,0644);if(d<0){close(s);return -3;}
  char*b=malloc(ASTRO_NATIVE_BUF);if(!b){close(s);close(d);return -4;}int rc=0;
  for(;;){ssize_t n=read(s,b,ASTRO_NATIVE_BUF);if(n==0)break;if(n<0){rc=-5;break;}size_t o=0;while(o<(size_t)n){ssize_t w=write(d,b+o,(size_t)n-o);if(w<=0){rc=-6;break;}o+=(size_t)w;}if(rc)break;}
  fsync(d);free(b);close(s);close(d);return rc;
}

static void native_save_elevate(void)
{
  pid_t p=getpid();kernel_set_ucred_uid(p,0);kernel_set_ucred_authid(p,0x4800000000000010ULL);uint8_t caps[16]={0};kernel_get_ucred_caps(p,caps);caps[7]|=0x40;kernel_set_ucred_caps(p,caps);setuid(0);
}

static int native_save_read_key(const char *image,uint8_t key[0x20])
{
  uint8_t*buf=malloc(0x100);if(!buf)return -1;memset(buf,0,0x100);int f=open(image,O_RDONLY);if(f<0){free(buf);return -2;}ssize_t n=pread(f,buf,0x60,0x800);close(f);if(n!=0x60){free(buf);return -3;}
  int p=open("/dev/pfsmgr",O_RDWR);if(p<0){free(buf);return -4;}int rc=ioctl(p,0xc0845302,buf);close(p);if(rc<0){free(buf);return -5;}memcpy(key,buf+0x60,0x20);free(buf);return 0;
}

static int native_save_unmount(void)
{
  astro_umount_opt_t o;memset(&o,0,sizeof(o));sceFsInitUmountSaveDataOpt(&o);int rc=sceFsUmountSaveData(&o,ASTRO_NATIVE_MOUNT,0,0);sync();return rc;
}

static int native_save_mount(const char *image)
{
  native_save_elevate();uint8_t key[0x20]={0};int rc=native_save_read_key(image,key);if(rc)return rc;
  native_save_unmount();v3_mkdirs(ASTRO_NATIVE_MOUNT);astro_mount_opt_t o;memset(&o,0,sizeof(o));sceFsInitMountSaveDataOpt(&o);o.budgetid="system";
  signal(SIGPIPE,SIG_DFL);rc=sceFsMountSaveData(&o,image,ASTRO_NATIVE_MOUNT,key);signal(SIGPIPE,SIG_IGN);return rc<0?rc:0;
}

static int native_sfo_parse(const char *path,astro_native_sfo_t *s)
{
  memset(s,0,sizeof(*s));int f=open(path,O_RDONLY);if(f<0)return -1;struct stat st;if(fstat(f,&st)!=0||st.st_size<20||st.st_size>4*1024*1024){close(f);return -2;}uint8_t*b=malloc((size_t)st.st_size);if(!b){close(f);return -3;}ssize_t n=read(f,b,(size_t)st.st_size);close(f);if(n!=st.st_size){free(b);return -4;}
  if(b[0]!=0x00||b[1]!='P'||b[2]!='S'||b[3]!='F'){free(b);return -5;}uint32_t ko=0,doff=0,cnt=0;memcpy(&ko,b+8,4);memcpy(&doff,b+12,4);memcpy(&cnt,b+16,4);if(ko>st.st_size||doff>st.st_size||cnt>256){free(b);return -6;}
  for(uint32_t i=0;i<cnt;i++){size_t io=20+(size_t)i*16;if(io+16>(size_t)st.st_size)break;uint16_t krel=0;uint32_t len=0,drel=0;memcpy(&krel,b+io,2);memcpy(&len,b+io+4,4);memcpy(&drel,b+io+12,4);size_t kp=(size_t)ko+krel,vp=(size_t)doff+drel;if(kp>=(size_t)st.st_size||vp>=(size_t)st.st_size)continue;const char*k=(const char*)(b+kp);size_t avail=(size_t)st.st_size-vp;if(len>avail)len=(uint32_t)avail;const uint8_t*v=b+vp;
    if(!strcmp(k,"TITLE_ID"))snprintf(s->title_id,sizeof(s->title_id),"%.*s",(int)len,(const char*)v);
    else if(!strcmp(k,"SAVEDATA_DIRECTORY"))snprintf(s->dir_name,sizeof(s->dir_name),"%.*s",(int)len,(const char*)v);
    else if(!strcmp(k,"MAINTITLE")||!strcmp(k,"TITLE"))snprintf(s->main_title,sizeof(s->main_title),"%.*s",(int)len,(const char*)v);
    else if(!strcmp(k,"SUBTITLE"))snprintf(s->sub_title,sizeof(s->sub_title),"%.*s",(int)len,(const char*)v);
    else if(!strcmp(k,"DETAIL"))snprintf(s->detail,sizeof(s->detail),"%.*s",(int)len,(const char*)v);
    else if(!strcmp(k,"FORMAT"))snprintf(s->format,sizeof(s->format),"%.*s",(int)len,(const char*)v);
    else if(!strcmp(k,"GAME_TITLE_ID"))snprintf(s->game_title_id,sizeof(s->game_title_id),"%.*s",(int)len,(const char*)v);
    else if(!strcmp(k,"SAVEDATA_BLOCKS")&&len>=4)memcpy(&s->blocks,v,4);
    else if((!strcmp(k,"PARAMS")||!strcmp(k,"USER_PARAM"))&&len>=4)memcpy(&s->user_param,v,4);
    else if(!strcmp(k,"ACCOUNT_ID")&&len>=8)memcpy(&s->account_id,v,8);
    else if(!strcmp(k,"SYSTEM_BLOCKS")&&len>=8)memcpy(&s->system_blocks,v,8);
  }
  free(b);if(!s->game_title_id[0])snprintf(s->game_title_id,sizeof(s->game_title_id),"%s",s->title_id);return s->title_id[0]&&s->dir_name[0]?0:-7;
}

static uint64_t native_account_id_for_uid(const char *uidhex)
{
  char p[256],name[32]={0};snprintf(p,sizeof(p),"/user/home/%s/username.dat",uidhex);int f=open(p,O_RDONLY);if(f>=0){read(f,name,16);close(f);name[16]=0;}if(!name[0])return 0;
  for(int i=1;i<=16;i++){int nk=(i-1)*65536+125829632;char rn[32]={0};if(sceRegMgrGetStr(nk,rn,sizeof(rn))<0||!rn[0]||strcmp(name,rn))continue;int ak=(i-1)*65536+125830400;uint64_t aid=0;if(sceRegMgrGetBin(ak,&aid,sizeof(aid))>=0)return aid;}return 0;
}

static int native_patch_ps5_owner(const char *sfo,uint64_t aid,uint32_t uid)
{
  int f=open(sfo,O_RDWR);if(f<0)return -1;struct stat st;if(fstat(f,&st)!=0||st.st_size<0x664){close(f);return -2;}int rc=0;if(pwrite(f,&aid,8,0x1B8)!=8)rc=-3;if(!rc&&pwrite(f,&uid,4,0x660)!=4)rc=-4;fsync(f);close(f);return rc;
}

static int native_stage_one(const char *src,const char *work,int index,uint64_t aid,uint32_t uid,astro_native_slot_t *out)
{
  memset(out,0,sizeof(*out));char tmp[1024];snprintf(tmp,sizeof(tmp),"%s/slot_%03d.img",work,index);if(native_save_copy_file(src,tmp)!=0)return -1;chmod(tmp,0755);int rc=native_save_mount(tmp);if(rc){unlink(tmp);return -2;}
  char sfo[1024];snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",ASTRO_NATIVE_MOUNT);rc=native_sfo_parse(sfo,&out->sfo);if(!rc&&strcasecmp(out->sfo.format,"ppr"))rc=-3;if(!rc)rc=native_patch_ps5_owner(sfo,aid,uid);
  char icon[1024];snprintf(icon,sizeof(icon),"%s/sce_sys/icon0.png",ASTRO_NATIVE_MOUNT);struct stat ist;if(!rc&&stat(icon,&ist)==0&&S_ISREG(ist.st_mode)){snprintf(out->icon,sizeof(out->icon),"%s/icon_%03d.png",work,index);if(native_save_copy_file(icon,out->icon)!=0)out->icon[0]=0;}
  sync();native_save_unmount();if(rc){unlink(tmp);if(out->icon[0])unlink(out->icon);return rc;}struct stat st;if(stat(tmp,&st)!=0){unlink(tmp);return -4;}snprintf(out->image,sizeof(out->image),"%s",tmp);out->image_size=(unsigned long long)st.st_size;return 0;
}

static int native_sql_exec(sqlite3 *db,const char *sql)
{
  char*e=NULL;int rc=sqlite3_exec(db,sql,NULL,NULL,&e);if(e)sqlite3_free(e);return rc==SQLITE_OK?0:-1;
}

static int native_db_prepare(sqlite3 *db)
{
  return native_sql_exec(db,"CREATE TABLE IF NOT EXISTS savedata (id INTEGER PRIMARY KEY AUTOINCREMENT NOT NULL,title_id NOT NULL,dir_name NOT NULL,main_title NOT NULL,sub_title,detail,tmp_dir_name,is_broken,user_param,blocks,free_blocks,size_kib,mtime NOT NULL,fake_broken,account_id,user_id,faked_owner,cloud_icon_url,cloud_revision,game_title_id NOT NULL,system_blocks)");
}

static int native_db_delete_title(sqlite3 *db,const char *tid)
{
  sqlite3_stmt*s=NULL;int rc=sqlite3_prepare_v2(db,"DELETE FROM savedata WHERE title_id=?",-1,&s,NULL);if(rc!=SQLITE_OK)return -1;sqlite3_bind_text(s,1,tid,-1,SQLITE_TRANSIENT);rc=sqlite3_step(s);sqlite3_finalize(s);return rc==SQLITE_DONE?0:-2;
}

static void native_utc(char out[64])
{
  time_t now=time(NULL);struct tm*t=gmtime(&now);if(t)strftime(out,64,"%Y-%m-%dT%H:%M:%S.00Z",t);else snprintf(out,64,"1970-01-01T00:00:00.00Z");
}

static int native_db_insert(sqlite3 *db,const astro_native_slot_t *x,uint64_t aid,uint32_t uid)
{
  const char*sql="INSERT INTO savedata (title_id,dir_name,main_title,sub_title,detail,is_broken,user_param,blocks,free_blocks,size_kib,mtime,fake_broken,account_id,user_id,faked_owner,cloud_revision,game_title_id,system_blocks) VALUES (?,?,?,?,?,0,?,?,?,?,?,0,?,?,0,0,?,?)";
  sqlite3_stmt*s=NULL;if(sqlite3_prepare_v2(db,sql,-1,&s,NULL)!=SQLITE_OK)return -1;char mt[64];native_utc(mt);uint32_t blocks=x->sfo.blocks?x->sfo.blocks:(uint32_t)((x->image_size+0xfffffULL)/0x100000ULL*16ULL);if(!blocks)blocks=96;
  sqlite3_bind_text(s,1,x->sfo.title_id,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,2,x->sfo.dir_name,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,3,x->sfo.main_title[0]?x->sfo.main_title:x->sfo.title_id,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,4,x->sfo.sub_title,-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,5,x->sfo.detail,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,6,x->sfo.user_param);sqlite3_bind_int64(s,7,blocks);sqlite3_bind_int64(s,8,blocks);sqlite3_bind_int64(s,9,(sqlite3_int64)blocks*0x40);sqlite3_bind_text(s,10,mt,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,11,(sqlite3_int64)aid);sqlite3_bind_int64(s,12,uid);sqlite3_bind_text(s,13,x->sfo.game_title_id[0]?x->sfo.game_title_id:x->sfo.title_id,-1,SQLITE_TRANSIENT);sqlite3_bind_int64(s,14,(sqlite3_int64)x->sfo.system_blocks);int rc=sqlite3_step(s);sqlite3_finalize(s);return rc==SQLITE_DONE?0:-2;
}

static int native_title_regular_count(const char *root)
{
  DIR*d=opendir(root);if(!d)return 0;int n=0;struct dirent*e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char p[1024];snprintf(p,sizeof(p),"%s/%s",root,e->d_name);struct stat st;if(stat(p,&st)==0&&S_ISREG(st.st_mode)){size_t l=strlen(e->d_name);if(l>4&&!strcasecmp(e->d_name+l-4,".bin"))continue;n++;}}closedir(d);return n;
}

static int native_stage_title(const char *root,const char *tid,const char *work,uint64_t aid,uint32_t uid,astro_native_slot_t *slots,int *count)
{
  DIR*d=opendir(root);if(!d)return -1;struct dirent*e;int c=0,rc=0;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char p[1024];snprintf(p,sizeof(p),"%s/%s",root,e->d_name);struct stat st;if(stat(p,&st)!=0||!S_ISREG(st.st_mode))continue;size_t l=strlen(e->d_name);if(l>4&&!strcasecmp(e->d_name+l-4,".bin"))continue;if(c>=ASTRO_NATIVE_MAX_SLOTS){rc=-2;break;}rc=native_stage_one(p,work,c,aid,uid,&slots[c]);if(rc)break;if(strcmp(slots[c].sfo.title_id,tid)){rc=-3;break;}c++;}
  closedir(d);*count=c;return rc;
}

static int native_restore_from_backup(const char *dstroot,const char *bak,const char *db,const char *dbbak,const char *metadir,const char *metabak,int had_dst,int had_db,int had_meta)
{
  if(dstroot){v3_remove_tree(dstroot);if(had_dst&&bak&&*bak)v3_copy_tree(bak,dstroot);}if(had_db&&db&&dbbak)native_save_copy_file(dbbak,db);if(metadir){v3_remove_tree(metadir);if(had_meta&&metabak&&*metabak)v3_copy_tree(metabak,metadir);}return 0;
}

static void save_clone_title_api(int fd,const char *target)
{
  char srcuid[64],dstuid[64],tid[96],type[16]="PS5",srcroot[1024],dstroot[1024];
  if(!query_value(target,"uid",srcuid,sizeof(srcuid))||!query_value(target,"dst",dstuid,sizeof(dstuid))||!query_value(target,"title",tid,sizeof(tid))||!strcmp(srcuid,dstuid)||!save_title_root(srcuid,tid,query_value(target,"type",type,sizeof(type))?type:"PS5",srcroot,sizeof(srcroot))||!save_title_root(dstuid,tid,type,dstroot,sizeof(dstroot))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}
  if(strcasecmp(type,"PS5")){send_text(fd,"501 Not Implemented","application/json",NULL,"{\"ok\":false,\"error\":\"native_ps4_clone_pending\"}");return;}
  struct stat st;if(stat(srcroot,&st)!=0||!S_ISDIR(st.st_mode)||native_title_regular_count(srcroot)==0){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"source_save_not_found\"}");return;}
  uint32_t duid=(uint32_t)strtoul(dstuid,NULL,16);uint64_t aid=native_account_id_for_uid(dstuid);if(!aid){send_text(fd,"409 Conflict","application/json",NULL,"{\"ok\":false,\"error\":\"destination_account_id_not_found\"}");return;}
  native_save_elevate();v3_mkdirs(ASTRO_NATIVE_SAVE_DIR);char work[1024];snprintf(work,sizeof(work),"%s/%lld_%s_%s",ASTRO_NATIVE_SAVE_DIR,(long long)time(NULL),tid,dstuid);v3_remove_tree(work);if(v3_mkdirs(work)!=0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"workdir\"}");return;}
  astro_native_slot_t*slots=calloc(ASTRO_NATIVE_MAX_SLOTS,sizeof(*slots));if(!slots){v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"memory\"}");return;}int count=0,rc=native_stage_title(srcroot,tid,work,aid,duid,slots,&count);if(rc||count==0){char j[256];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"stage_source\",\"rc\":%d,\"slots\":%d}",rc,count);free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}

  char bak[1200]={0};int had_dst=(stat(dstroot,&st)==0&&S_ISDIR(st.st_mode));if(had_dst&&save_snapshot_root(dstroot,dstuid,tid,bak,sizeof(bak))<0){free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"destination_backup\"}");return;}
  char db[1024],dbbak[1024],dbdir[1024];snprintf(db,sizeof(db),"/system_data/savedata_prospero/%s/db/user/savedata.db",dstuid);snprintf(dbdir,sizeof(dbdir),"/system_data/savedata_prospero/%s/db/user",dstuid);snprintf(dbbak,sizeof(dbbak),"%s/savedata.db.bak",work);int had_db=(stat(db,&st)==0&&S_ISREG(st.st_mode));if(had_db&&native_save_copy_file(db,dbbak)!=0){free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"db_backup\"}");return;}
  char metadir[1024],metabak[1024];snprintf(metadir,sizeof(metadir),"/user/home/%s/savedata_prospero_meta/user/%s",dstuid,tid);snprintf(metabak,sizeof(metabak),"%s/meta_backup",work);int had_meta=(stat(metadir,&st)==0&&S_ISDIR(st.st_mode));if(had_meta)v3_copy_tree(metadir,metabak);

  v3_mkdirs(dbdir);sqlite3*sdb=NULL;if(sqlite3_open(db,&sdb)!=SQLITE_OK||!sdb){if(sdb)sqlite3_close(sdb);free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"db_open\"}");return;}rc=native_db_prepare(sdb);if(!rc)rc=native_sql_exec(sdb,"BEGIN IMMEDIATE");if(!rc)rc=native_db_delete_title(sdb,tid);
  if(!rc){v3_remove_tree(dstroot);if(v3_mkdirs(dstroot)!=0)rc=-20;v3_remove_tree(metadir);if(!rc)v3_mkdirs(metadir);}
  for(int i=0;!rc&&i<count;i++){char dst[1200];snprintf(dst,sizeof(dst),"%s/sdimg_%s",dstroot,slots[i].sfo.dir_name);if(native_save_copy_file(slots[i].image,dst)!=0){rc=-30;break;}chmod(dst,0644);if(slots[i].icon[0]){char ip[1200];snprintf(ip,sizeof(ip),"%s/%s_icon0.png",metadir,slots[i].sfo.dir_name);native_save_copy_file(slots[i].icon,ip);}if(native_db_insert(sdb,&slots[i],aid,duid)!=0){rc=-31;break;}}
  if(!rc){if(native_sql_exec(sdb,"COMMIT")!=0)rc=-32;}else native_sql_exec(sdb,"ROLLBACK");sqlite3_close(sdb);
  if(rc){native_restore_from_backup(dstroot,bak,db,dbbak,metadir,metabak,had_dst,had_db,had_meta);char j[320];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"native_clone_failed\",\"rc\":%d,\"staged\":%d,\"rolled_back\":true}",rc,count);free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}
  free(slots);v3_remove_tree(work);char j[384];snprintf(j,sizeof(j),"{\"ok\":true,\"engine\":\"astro_native\",\"mode\":\"replace\",\"slots\":%d,\"backup\":\"%s\"}",count,bak);send_text(fd,"200 OK","application/json",NULL,j);
}
