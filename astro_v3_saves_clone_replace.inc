/* Astro V3 cross-account clone semantics.
 * A clone must REPLACE the destination title, not merge source slots with the
 * destination user's existing slots. Garlic's delete_save endpoint removes the
 * encrypted image and its savedata.db row, so use it before re-importing.
 */

static int save_json_value(const char *obj,const char *end,const char *key,char *out,size_t os)
{
  char pat[96];snprintf(pat,sizeof(pat),"\"%s\":\"",key);const char*p=strstr(obj,pat);if(!p||p>=end)return 0;p+=strlen(pat);size_t n=0;while(p<end&&*p&&*p!='\"'){if(*p=='\\'&&p+1<end)p++;if(n+1<os)out[n++]=*p;p++;}if(!out||!os)return 0;out[n]=0;return n>0;
}

static int save_garlic_find_idx(const char *json,const char *uid,const char *tid,const char *type)
{
  const char*p=json;int idx=0;const char*want_type=(type&&!strcasecmp(type,"PS4"))?"ps4":"ps5";
  while((p=strstr(p,"\"title_id\":\""))){const char*end=strchr(p,'}');if(!end)break;char jt[96]={0},ju[64]={0},jtype[16]={0};save_json_value(p,end,"title_id",jt,sizeof(jt));save_json_value(p,end,"uid",ju,sizeof(ju));save_json_value(p,end,"type",jtype,sizeof(jtype));if(!strcmp(jt,tid)&&!strcmp(ju,uid)&&(!jtype[0]||!strcasecmp(jtype,want_type)))return idx;idx++;p=end+1;}return -1;
}

/* Delete only one user's saves for this title. Garlic rescans on every call,
 * so indices may shift; fetch the list again after each deletion. */
static int save_garlic_clear_title_user(const char *uid,const char *tid,const char *type,int *deleted)
{
  int count=0;for(int guard=0;guard<256;guard++){
    char*body=malloc(512*1024);if(!body)return -1;int rc=save_garlic_request("GET","/api/saves",NULL,body,512*1024);if(rc!=0){free(body);return -2;}int idx=save_garlic_find_idx(body,uid,tid,type);free(body);if(idx<0)break;char path[128],resp[2048];snprintf(path,sizeof(path),"/api/delete_save?idx=%d",idx);rc=save_garlic_request("GET",path,NULL,resp,sizeof(resp));if(rc!=0||strstr(resp,"\"error\""))return -3;count++;}
  if(deleted)*deleted=count;return 0;
}

static int save_clone_files_replace(const char *root,const char *dst_uid,const char *type,int *done)
{
  DIR*d=opendir(root);if(!d)return -1;struct dirent*e;int rc=0,count=0;int ps4=(type&&!strcasecmp(type,"PS4"));
  while((e=readdir(d))){if(e->d_name[0]=='.')continue;char p[2048];snprintf(p,sizeof(p),"%s/%s",root,e->d_name);struct stat st;if(stat(p,&st)!=0||!S_ISREG(st.st_mode))continue;size_t ln=strlen(e->d_name);if(ln>4&&!strcasecmp(e->d_name+ln-4,".bin"))continue;if(ps4&&strncmp(e->d_name,"sdimg_",6))continue;
    char resp[8192],path[512];
    if(ps4){char key[2048];snprintf(key,sizeof(key),"%s.bin",p);if(stat(key,&st)!=0||!S_ISREG(st.st_mode)){rc=-10;break;}if(save_garlic_request("POST","/api/upload_key",key,resp,sizeof(resp))!=0){rc=-11;break;}}
    snprintf(path,sizeof(path),"/api/import_encrypted?uid=%s%s",dst_uid,ps4?"&ps4=1":"");if(save_garlic_request("POST",path,p,resp,sizeof(resp))!=0||strstr(resp,"\"error\"")){rc=-12;break;}
    /* import_finish performs the account/user resign, re-encrypts, copies to the
     * final save directory and updates savedata.db. */
    snprintf(path,sizeof(path),"/api/import_finish?uid=%s",dst_uid);memset(resp,0,sizeof(resp));if(save_garlic_request("GET",path,NULL,resp,sizeof(resp))!=0||strstr(resp,"\"error\"")){rc=-13;break;}count++;
  }
  closedir(d);if(done)*done=count;return rc;
}

static void save_clone_title_api(int fd,const char *target)
{
  char uid[64],dst[64],tid[96],type[16]="PS5",root[1024],dstroot[1024];
  if(!query_value(target,"uid",uid,sizeof(uid))||!query_value(target,"dst",dst,sizeof(dst))||!query_value(target,"title",tid,sizeof(tid))||!strcmp(uid,dst)||!save_title_root(uid,tid,query_value(target,"type",type,sizeof(type))?type:"PS5",root,sizeof(root))||!save_title_root(dst,tid,type,dstroot,sizeof(dstroot))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}
  struct stat st;if(stat(root,&st)!=0||!S_ISDIR(st.st_mode)){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"save_not_found\"}");return;}
  if(!save_start_garlic()){send_text(fd,"503 Service Unavailable","application/json",NULL,"{\"ok\":false,\"error\":\"garlic_offline\"}");return;}

  /* Snapshot destination before destructive replacement. */
  char bak[1200]={0};int had_dst=(stat(dstroot,&st)==0&&S_ISDIR(st.st_mode));if(had_dst&&save_snapshot_root(dstroot,dst,tid,bak,sizeof(bak))<0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"destination_backup\"}");return;}

  int removed=0,rc=save_garlic_clear_title_user(dst,tid,type,&removed);if(rc!=0){char j[256];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"clear_destination\",\"rc\":%d,\"removed\":%d}",rc,removed);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}

  /* Any unindexed leftovers would recreate the old merge bug. Remove only the
   * already-backed-up destination title directory after Garlic cleaned DB rows. */
  if(stat(dstroot,&st)==0&&S_ISDIR(st.st_mode))v3_remove_tree(dstroot);

  int cloned=0;rc=save_clone_files_replace(root,dst,type,&cloned);
  if(rc!=0||cloned==0){
    /* Best-effort rollback: remove partial imports via Garlic, then re-import
     * the snapshot so savedata.db is rebuilt too. */
    int trash=0;save_garlic_clear_title_user(dst,tid,type,&trash);if(stat(dstroot,&st)==0)v3_remove_tree(dstroot);int restored=0;if(had_dst&&bak[0])save_clone_files_replace(bak,dst,type,&restored);char j[384];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"clone_failed\",\"rc\":%d,\"cloned\":%d,\"rollback\":%d}",rc,cloned,restored);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;
  }

  char j[320];snprintf(j,sizeof(j),"{\"ok\":true,\"mode\":\"replace\",\"removed\":%d,\"cloned\":%d}",removed,cloned);send_text(fd,"200 OK","application/json",NULL,j);
}
