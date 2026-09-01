/* Astro V3 native PS5 clone V2.
 * Reliability pass: clone only DB-backed slots, reuse the destination user's
 * PARAMS HMAC, preserve target filesystem ownership, clone the source DB rows
 * verbatim (changing only owner fields), and verify every installed slot.
 */

#define ASTRO_NATIVE2_MAX_CANDIDATES 256

typedef struct native2_fs_template {
  uid_t uid; gid_t gid; mode_t file_mode; mode_t dir_mode; int valid;
} native2_fs_template_t;

static int native2_params_hmac_at(const char *sfo,uint8_t hmac[32],int write_hmac)
{
  int f=open(sfo,write_hmac?O_RDWR:O_RDONLY);if(f<0)return -1;uint8_t hdr[20];if(pread(f,hdr,20,0)!=20){close(f);return -2;}
  if(hdr[0]!=0x00||hdr[1]!='P'||hdr[2]!='S'||hdr[3]!='F'){close(f);return -3;}
  uint32_t ko=0,doff=0,nent=0;memcpy(&ko,hdr+8,4);memcpy(&doff,hdr+12,4);memcpy(&nent,hdr+16,4);if(nent>256){close(f);return -4;}
  for(uint32_t i=0;i<nent;i++){
    uint8_t idx[16];if(pread(f,idx,16,20+(off_t)i*16)!=16)break;uint16_t kro=0;uint32_t dro=0;memcpy(&kro,idx,2);memcpy(&dro,idx+12,4);char key[32]={0};if(pread(f,key,31,(off_t)ko+kro)<=0)continue;
    if(strcmp(key,"PARAMS"))continue;off_t pos=(off_t)doff+dro+0x08;if(write_hmac){int rc=pwrite(f,hmac,32,pos)==32?0:-5;fsync(f);close(f);return rc;}int rc=pread(f,hmac,32,pos)==32?0:-6;close(f);return rc;
  }
  close(f);return -7;
}

static int native2_hmac_nonzero(const uint8_t h[32]){for(int i=0;i<32;i++)if(h[i])return 1;return 0;}

static int native2_first_regular(const char *root,char *out,size_t os)
{
  DIR*d=opendir(root);if(!d)return 0;struct dirent*e;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char p[1200];snprintf(p,sizeof(p),"%s/%s",root,e->d_name);struct stat st;if(stat(p,&st)==0&&S_ISREG(st.st_mode)){size_t l=strlen(e->d_name);if(l>4&&!strcasecmp(e->d_name+l-4,".bin"))continue;snprintf(out,os,"%s",p);closedir(d);return 1;}}closedir(d);return 0;
}

static int native2_hmac_from_image(const char *image,const char *work,uint8_t out[32])
{
  char tmp[1200];snprintf(tmp,sizeof(tmp),"%s/hmac_probe.img",work);unlink(tmp);if(native_save_copy_file(image,tmp)!=0)return -1;chmod(tmp,0755);int rc=native_save_mount(tmp);if(rc){unlink(tmp);return -2;}char sfo[1200];snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",ASTRO_NATIVE_MOUNT);rc=native2_params_hmac_at(sfo,out,0);native_save_unmount();unlink(tmp);return rc==0&&native2_hmac_nonzero(out)?0:-3;
}

static int native2_other_title_template(const char *dstuid,const char *exclude_tid,const char *work,uint8_t hmac[32],native2_fs_template_t *fst)
{
  char base[512];snprintf(base,sizeof(base),"/user/home/%s/savedata_prospero",dstuid);struct stat bst;if(stat(base,&bst)==0&&S_ISDIR(bst.st_mode)){fst->uid=bst.st_uid;fst->gid=bst.st_gid;fst->dir_mode=bst.st_mode&07777;fst->file_mode=0644;fst->valid=1;}
  DIR*d=opendir(base);if(!d)return -1;struct dirent*t;while((t=readdir(d))){if(t->d_name[0]=='.'||!strcmp(t->d_name,exclude_tid))continue;char tr[1024],img[1200];snprintf(tr,sizeof(tr),"%s/%s",base,t->d_name);struct stat td;if(stat(tr,&td)!=0||!S_ISDIR(td.st_mode)||!native2_first_regular(tr,img,sizeof(img)))continue;struct stat fi;if(stat(img,&fi)==0){fst->uid=fi.st_uid;fst->gid=fi.st_gid;fst->file_mode=fi.st_mode&07777;fst->dir_mode=td.st_mode&07777;fst->valid=1;}if(native2_hmac_from_image(img,work,hmac)==0){closedir(d);return 0;}}
  closedir(d);return -2;
}

static int native2_oldest_backup_image(const char *uid,const char *tid,char *out,size_t os)
{
  char base[1024];snprintf(base,sizeof(base),"%s/%s/%s",ASTRO_BACKUP_DIR,uid,tid);DIR*d=opendir(base);if(!d)return 0;struct dirent*e;unsigned long long best=~0ULL;char bestdir[256]={0};while((e=readdir(d))){if(e->d_name[0]=='.')continue;char*end=NULL;unsigned long long v=strtoull(e->d_name,&end,10);if(!end||*end)continue;if(v<best){best=v;snprintf(bestdir,sizeof(bestdir),"%s",e->d_name);}}closedir(d);if(!bestdir[0])return 0;char root[1200];snprintf(root,sizeof(root),"%s/%s/current",base,bestdir);return native2_first_regular(root,out,os);
}

static int native2_target_hmac(const char *dstuid,const char *tid,const char *dstroot,const char *work,uint8_t hmac[32],native2_fs_template_t *fst,char *source,size_t ss)
{
  memset(hmac,0,32);memset(fst,0,sizeof(*fst));if(native2_other_title_template(dstuid,tid,work,hmac,fst)==0){snprintf(source,ss,"other_title");return 0;}
  char img[1200];if(native2_oldest_backup_image(dstuid,tid,img,sizeof(img))&&native2_hmac_from_image(img,work,hmac)==0){snprintf(source,ss,"backup");return 0;}
  if(native2_first_regular(dstroot,img,sizeof(img))&&native2_hmac_from_image(img,work,hmac)==0){snprintf(source,ss,"current");return 0;}
  return -1;
}

static int native2_patch_owner_hmac(const char *sfo,uint64_t aid,uint32_t uid,const uint8_t hmac[32])
{
  int rc=native_patch_ps5_owner(sfo,aid,uid);if(rc)return rc;uint8_t h[32];memcpy(h,hmac,32);return native2_params_hmac_at(sfo,h,1);
}

static int native2_stage_one(const char *src,const char *work,int index,uint64_t aid,uint32_t uid,const uint8_t hmac[32],astro_native_slot_t *out)
{
  memset(out,0,sizeof(*out));char tmp[1024];snprintf(tmp,sizeof(tmp),"%s/slot_%03d.img",work,index);if(native_save_copy_file(src,tmp)!=0)return -1;chmod(tmp,0755);int rc=native_save_mount(tmp);if(rc){unlink(tmp);return -2;}
  char sfo[1024];snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",ASTRO_NATIVE_MOUNT);rc=native_sfo_parse(sfo,&out->sfo);if(!rc&&strcasecmp(out->sfo.format,"ppr"))rc=-3;if(!rc)rc=native2_patch_owner_hmac(sfo,aid,uid,hmac);
  char icon[1024];snprintf(icon,sizeof(icon),"%s/sce_sys/icon0.png",ASTRO_NATIVE_MOUNT);struct stat ist;if(!rc&&stat(icon,&ist)==0&&S_ISREG(ist.st_mode)){snprintf(out->icon,sizeof(out->icon),"%s/icon_%03d.png",work,index);if(native_save_copy_file(icon,out->icon)!=0)out->icon[0]=0;}
  sync();native_save_unmount();if(rc){unlink(tmp);if(out->icon[0])unlink(out->icon);return rc;}struct stat st;if(stat(tmp,&st)!=0){unlink(tmp);return -4;}snprintf(out->image,sizeof(out->image),"%s",tmp);out->image_size=(unsigned long long)st.st_size;return 0;
}

static int native2_stage_from_source_db(const char *srcuid,const char *tid,const char *srcroot,const char *work,uint64_t aid,uint32_t uid,const uint8_t hmac[32],astro_native_slot_t *slots,int *count)
{
  char dbp[1024];snprintf(dbp,sizeof(dbp),"/system_data/savedata_prospero/%s/db/user/savedata.db",srcuid);sqlite3*db=NULL;if(sqlite3_open_v2(dbp,&db,SQLITE_OPEN_READONLY,NULL)!=SQLITE_OK||!db){if(db)sqlite3_close(db);return -1;}sqlite3_stmt*s=NULL;if(sqlite3_prepare_v2(db,"SELECT dir_name FROM savedata WHERE title_id=? ORDER BY id",-1,&s,NULL)!=SQLITE_OK){sqlite3_close(db);return -2;}sqlite3_bind_text(s,1,tid,-1,SQLITE_TRANSIENT);int c=0,rc=0;while(sqlite3_step(s)==SQLITE_ROW){const char*dir=(const char*)sqlite3_column_text(s,0);if(!dir||!*dir){rc=-3;break;}if(c>=ASTRO_NATIVE_MAX_SLOTS){rc=-4;break;}char p[1200];snprintf(p,sizeof(p),"%s/sdimg_%s",srcroot,dir);struct stat st;if(stat(p,&st)!=0||!S_ISREG(st.st_mode)){rc=-5;break;}rc=native2_stage_one(p,work,c,aid,uid,hmac,&slots[c]);if(rc)break;if(strcmp(slots[c].sfo.title_id,tid)||strcmp(slots[c].sfo.dir_name,dir)){rc=-6;break;}c++;}
  sqlite3_finalize(s);sqlite3_close(db);*count=c;return rc?rc:(c?0:-7);
}

static int native2_attach_source(sqlite3 *db,const char *srcpath)
{
  sqlite3_stmt*s=NULL;if(sqlite3_prepare_v2(db,"ATTACH DATABASE ? AS srcdb",-1,&s,NULL)!=SQLITE_OK)return -1;sqlite3_bind_text(s,1,srcpath,-1,SQLITE_TRANSIENT);int rc=sqlite3_step(s);sqlite3_finalize(s);return rc==SQLITE_DONE?0:-2;
}

static int native2_copy_db_rows(sqlite3 *db,const char *srcpath,const char *tid,uint64_t aid,uint32_t uid)
{
  if(native2_attach_source(db,srcpath)!=0)return -1;const char*sql=
    "INSERT INTO main.savedata (title_id,dir_name,main_title,sub_title,detail,tmp_dir_name,is_broken,user_param,blocks,free_blocks,size_kib,mtime,fake_broken,account_id,user_id,faked_owner,cloud_icon_url,cloud_revision,game_title_id,system_blocks) "
    "SELECT title_id,dir_name,main_title,sub_title,detail,tmp_dir_name,is_broken,user_param,blocks,free_blocks,size_kib,mtime,fake_broken,?, ?,faked_owner,cloud_icon_url,cloud_revision,game_title_id,system_blocks FROM srcdb.savedata WHERE title_id=?";
  sqlite3_stmt*s=NULL;if(sqlite3_prepare_v2(db,sql,-1,&s,NULL)!=SQLITE_OK){native_sql_exec(db,"DETACH DATABASE srcdb");return -2;}sqlite3_bind_int64(s,1,(sqlite3_int64)aid);sqlite3_bind_int64(s,2,(sqlite3_int64)uid);sqlite3_bind_text(s,3,tid,-1,SQLITE_TRANSIENT);int rc=sqlite3_step(s);sqlite3_finalize(s);int changes=sqlite3_changes(db);native_sql_exec(db,"DETACH DATABASE srcdb");return rc==SQLITE_DONE&&changes>0?changes:-3;
}

static int native2_db_count(sqlite3 *db,const char *tid)
{
  sqlite3_stmt*s=NULL;if(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM savedata WHERE title_id=?",-1,&s,NULL)!=SQLITE_OK)return -1;sqlite3_bind_text(s,1,tid,-1,SQLITE_TRANSIENT);int n=-1;if(sqlite3_step(s)==SQLITE_ROW)n=sqlite3_column_int(s,0);sqlite3_finalize(s);return n;
}

static void native2_apply_fs(const char *path,const native2_fs_template_t *t,int isdir)
{
  if(!t||!t->valid)return;chown(path,t->uid,t->gid);chmod(path,isdir?(t->dir_mode?t->dir_mode:0755):(t->file_mode?t->file_mode:0644));
}

static int native2_verify_slot(const char *path,uint64_t aid,uint32_t uid,const uint8_t hmac[32])
{
  int rc=native_save_mount(path);if(rc)return -1;char sfo[1200];snprintf(sfo,sizeof(sfo),"%s/sce_sys/param.sfo",ASTRO_NATIVE_MOUNT);astro_native_sfo_t info;rc=native_sfo_parse(sfo,&info);if(!rc&&info.account_id!=aid)rc=-2;if(!rc){int f=open(sfo,O_RDONLY);uint32_t suid=0;if(f>=0){if(pread(f,&suid,4,0x660)!=4)rc=-3;close(f);}else rc=-4;if(!rc&&suid!=uid)rc=-5;}if(!rc){uint8_t got[32]={0};if(native2_params_hmac_at(sfo,got,0)!=0||memcmp(got,hmac,32))rc=-6;}native_save_unmount();return rc;
}

static void save_clone_title_api(int fd,const char *target)
{
  char srcuid[64],dstuid[64],tid[96],type[16]="PS5",srcroot[1024],dstroot[1024];
  if(!query_value(target,"uid",srcuid,sizeof(srcuid))||!query_value(target,"dst",dstuid,sizeof(dstuid))||!query_value(target,"title",tid,sizeof(tid))||!strcmp(srcuid,dstuid)||!save_title_root(srcuid,tid,query_value(target,"type",type,sizeof(type))?type:"PS5",srcroot,sizeof(srcroot))||!save_title_root(dstuid,tid,type,dstroot,sizeof(dstroot))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}
  if(strcasecmp(type,"PS5")){send_text(fd,"501 Not Implemented","application/json",NULL,"{\"ok\":false,\"error\":\"native_ps4_clone_pending\"}");return;}struct stat st;if(stat(srcroot,&st)!=0||!S_ISDIR(st.st_mode)){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"source_save_not_found\"}");return;}
  uint32_t duid=(uint32_t)strtoul(dstuid,NULL,16);uint64_t aid=native_account_id_for_uid(dstuid);if(!aid){send_text(fd,"409 Conflict","application/json",NULL,"{\"ok\":false,\"error\":\"destination_account_id_not_found\"}");return;}
  native_save_elevate();v3_mkdirs(ASTRO_NATIVE_SAVE_DIR);char work[1024];snprintf(work,sizeof(work),"%s/v2_%lld_%s_%s",ASTRO_NATIVE_SAVE_DIR,(long long)time(NULL),tid,dstuid);v3_remove_tree(work);if(v3_mkdirs(work)!=0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"workdir\"}");return;}

  uint8_t hmac[32]={0};native2_fs_template_t fst;char hsrc[32]={0};if(native2_target_hmac(dstuid,tid,dstroot,work,hmac,&fst,hsrc,sizeof(hsrc))!=0){v3_remove_tree(work);send_text(fd,"409 Conflict","application/json",NULL,"{\"ok\":false,\"error\":\"destination_params_hmac_not_found\"}");return;}
  astro_native_slot_t*slots=calloc(ASTRO_NATIVE_MAX_SLOTS,sizeof(*slots));if(!slots){v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"memory\"}");return;}int count=0,rc=native2_stage_from_source_db(srcuid,tid,srcroot,work,aid,duid,hmac,slots,&count);if(rc||count==0){char j[256];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"stage_source_db\",\"rc\":%d,\"slots\":%d}",rc,count);free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}

  char bak[1200]={0};int had_dst=(stat(dstroot,&st)==0&&S_ISDIR(st.st_mode));if(had_dst&&save_snapshot_root(dstroot,dstuid,tid,bak,sizeof(bak))<0){free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"destination_backup\"}");return;}
  char db[1024],dbbak[1024],dbdir[1024],srcdb[1024];snprintf(db,sizeof(db),"/system_data/savedata_prospero/%s/db/user/savedata.db",dstuid);snprintf(srcdb,sizeof(srcdb),"/system_data/savedata_prospero/%s/db/user/savedata.db",srcuid);snprintf(dbdir,sizeof(dbdir),"/system_data/savedata_prospero/%s/db/user",dstuid);snprintf(dbbak,sizeof(dbbak),"%s/savedata.db.bak",work);int had_db=(stat(db,&st)==0&&S_ISREG(st.st_mode));if(had_db&&native_save_copy_file(db,dbbak)!=0){free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"db_backup\"}");return;}
  char metadir[1024],metabak[1024];snprintf(metadir,sizeof(metadir),"/user/home/%s/savedata_prospero_meta/user/%s",dstuid,tid);snprintf(metabak,sizeof(metabak),"%s/meta_backup",work);int had_meta=(stat(metadir,&st)==0&&S_ISDIR(st.st_mode));if(had_meta)v3_copy_tree(metadir,metabak);

  v3_mkdirs(dbdir);sqlite3*sdb=NULL;if(sqlite3_open(db,&sdb)!=SQLITE_OK||!sdb){if(sdb)sqlite3_close(sdb);free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"db_open\"}");return;}rc=native_db_prepare(sdb);if(!rc)rc=native_sql_exec(sdb,"BEGIN IMMEDIATE");if(!rc)rc=native_db_delete_title(sdb,tid);
  if(!rc){v3_remove_tree(dstroot);if(v3_mkdirs(dstroot)!=0)rc=-20;else native2_apply_fs(dstroot,&fst,1);v3_remove_tree(metadir);if(!rc&&v3_mkdirs(metadir)==0)native2_apply_fs(metadir,&fst,1);}
  for(int i=0;!rc&&i<count;i++){char dst[1200];snprintf(dst,sizeof(dst),"%s/sdimg_%s",dstroot,slots[i].sfo.dir_name);if(native_save_copy_file(slots[i].image,dst)!=0){rc=-30;break;}native2_apply_fs(dst,&fst,0);if(slots[i].icon[0]){char ip[1200];snprintf(ip,sizeof(ip),"%s/%s_icon0.png",metadir,slots[i].sfo.dir_name);if(native_save_copy_file(slots[i].icon,ip)==0)native2_apply_fs(ip,&fst,0);}}
  int copied_rows=0;if(!rc){copied_rows=native2_copy_db_rows(sdb,srcdb,tid,aid,duid);if(copied_rows!=count)rc=-31;}if(!rc&&native2_db_count(sdb,tid)!=count)rc=-32;if(!rc){if(native_sql_exec(sdb,"COMMIT")!=0)rc=-33;}else native_sql_exec(sdb,"ROLLBACK");sqlite3_close(sdb);

  for(int i=0;!rc&&i<count;i++){char dst[1200];snprintf(dst,sizeof(dst),"%s/sdimg_%s",dstroot,slots[i].sfo.dir_name);int vr=native2_verify_slot(dst,aid,duid,hmac);if(vr){rc=-100+vr;break;}}
  if(rc){native_restore_from_backup(dstroot,bak,db,dbbak,metadir,metabak,had_dst,had_db,had_meta);char j[384];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"native_clone_v2_failed\",\"rc\":%d,\"slots\":%d,\"rows\":%d,\"rolled_back\":true}",rc,count,copied_rows);free(slots);v3_remove_tree(work);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}
  free(slots);v3_remove_tree(work);char j[448];snprintf(j,sizeof(j),"{\"ok\":true,\"engine\":\"astro_native_v2\",\"mode\":\"replace\",\"slots\":%d,\"rows\":%d,\"hmac_source\":\"%s\",\"verified\":true,\"backup\":\"%s\"}",count,copied_rows,hsrc,bak);send_text(fd,"200 OK","application/json",NULL,j);
}
