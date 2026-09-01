/* Astro V3 save library.
 * The UI exposes games, not sdimg internals. Actions operate on the whole title.
 */

#define ASTRO_GARLIC_PORT 8082
#define ASTRO_RESTORE_DIR ASTRO_DIR "/restore_uploads"
#define ASTRO_RESTORE_TMP ASTRO_DIR "/restore_tmp"

typedef struct astro_tar_header {
  char name[100]; char mode[8]; char uid[8]; char gid[8]; char size[12];
  char mtime[12]; char chksum[8]; char typeflag; char linkname[100];
  char magic[6]; char version[2]; char uname[32]; char gname[32];
  char devmajor[8]; char devminor[8]; char prefix[155]; char pad[12];
} astro_tar_header_t;

static int save_token_ok(const char *s)
{
  if(!s||!*s||strstr(s,"..")||strchr(s,'/')||strchr(s,'\\'))return 0;
  for(const unsigned char*p=(const unsigned char*)s;*p;p++)if(!(isalnum(*p)||*p=='_'||*p=='-'||*p=='.'))return 0;
  return 1;
}

static int save_title_root(const char *uid,const char *tid,const char *type,char *out,size_t os)
{
  if(!save_token_ok(uid)||!save_token_ok(tid))return 0;
  const char *leaf=(type&&!strcasecmp(type,"PS4"))?"savedata":"savedata_prospero";
  snprintf(out,os,"/user/home/%s/%s/%s",uid,leaf,tid);return 1;
}

static unsigned long long save_tree_bytes(const char *root)
{
  struct stat st;if(lstat(root,&st)!=0)return 0;if(S_ISREG(st.st_mode))return (unsigned long long)st.st_size;if(!S_ISDIR(st.st_mode))return 0;
  unsigned long long n=0;DIR*d=opendir(root);if(!d)return 0;struct dirent*e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char p[2048];snprintf(p,sizeof(p),"%s/%s",root,e->d_name);n+=save_tree_bytes(p);}closedir(d);return n;
}

static unsigned long long save_tar_payload_size(const char *root)
{
  struct stat st;if(lstat(root,&st)!=0)return 0;if(S_ISREG(st.st_mode))return 512ULL+(((unsigned long long)st.st_size+511ULL)/512ULL)*512ULL;if(!S_ISDIR(st.st_mode))return 0;
  unsigned long long n=0;DIR*d=opendir(root);if(!d)return 0;struct dirent*e;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char p[2048];snprintf(p,sizeof(p),"%s/%s",root,e->d_name);n+=save_tar_payload_size(p);}closedir(d);return n;
}

static unsigned int save_tar_checksum(astro_tar_header_t *h)
{
  memset(h->chksum,' ',sizeof(h->chksum));unsigned int s=0;const unsigned char*p=(const unsigned char*)h;for(size_t i=0;i<512;i++)s+=p[i];return s;
}

static int save_tar_header(astro_tar_header_t *h,const char *name,const struct stat *st)
{
  if(strlen(name)>=sizeof(h->name))return -1;memset(h,0,sizeof(*h));snprintf(h->name,sizeof(h->name),"%s",name);snprintf(h->mode,sizeof(h->mode),"%07o",(unsigned)(st->st_mode&0777));snprintf(h->uid,sizeof(h->uid),"%07o",0);snprintf(h->gid,sizeof(h->gid),"%07o",0);snprintf(h->size,sizeof(h->size),"%011llo",(unsigned long long)st->st_size);snprintf(h->mtime,sizeof(h->mtime),"%011llo",(unsigned long long)st->st_mtime);h->typeflag='0';memcpy(h->magic,"ustar",5);memcpy(h->version,"00",2);unsigned int sum=save_tar_checksum(h);snprintf(h->chksum,7,"%06o",sum);h->chksum[6]=0;h->chksum[7]=' ';return 0;
}

static int save_tar_send_tree(int fd,const char *root,const char *path)
{
  struct stat st;if(lstat(path,&st)!=0)return -1;if(S_ISDIR(st.st_mode)){DIR*d=opendir(path);if(!d)return -2;struct dirent*e;int rc=0;while((e=readdir(d))){if(!strcmp(e->d_name,".")||!strcmp(e->d_name,".."))continue;char p[2048];snprintf(p,sizeof(p),"%s/%s",path,e->d_name);if(save_tar_send_tree(fd,root,p)!=0){rc=-3;break;}}closedir(d);return rc;}if(!S_ISREG(st.st_mode))return 0;
  const char *rel=path+strlen(root);while(*rel=='/')rel++;astro_tar_header_t h;if(save_tar_header(&h,rel,&st)!=0)return -4;if(send_all(fd,&h,512)!=0)return -5;int f=open(path,O_RDONLY);if(f<0)return -6;char*b=malloc(256*1024);if(!b){close(f);return -7;}unsigned long long sent=0;int rc=0;for(;;){ssize_t n=read(f,b,256*1024);if(n==0)break;if(n<0||send_all(fd,b,(size_t)n)!=0){rc=-8;break;}sent+=(unsigned long long)n;}free(b);close(f);if(rc)return rc;size_t pad=(size_t)((512ULL-(sent%512ULL))%512ULL);if(pad){char z[512]={0};if(send_all(fd,z,pad)!=0)return -9;}return 0;
}

static void save_download_title_api(int fd,const char *target)
{
  char uid[64],tid[96],type[16]="PS5",root[1024];if(!query_value(target,"uid",uid,sizeof(uid))||!query_value(target,"title",tid,sizeof(tid))||!save_title_root(uid,tid,query_value(target,"type",type,sizeof(type))?type:"PS5",root,sizeof(root))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}struct stat st;if(stat(root,&st)!=0||!S_ISDIR(st.st_mode)){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"save_not_found\"}");return;}unsigned long long total=save_tar_payload_size(root)+1024ULL;char safe[180];snprintf(safe,sizeof(safe),"Astro_%s_%s.tar",tid,uid);for(char*p=safe;*p;p++)if(!(isalnum((unsigned char)*p)||*p=='_'||*p=='-'||*p=='.'))*p='_';char h[1024];int hn=snprintf(h,sizeof(h),"HTTP/1.1 200 OK\r\nContent-Type: application/x-tar\r\nContent-Disposition: attachment; filename=\"%s\"\r\nContent-Length: %llu\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",safe,total);if(send_all(fd,h,(size_t)hn)!=0)return;if(save_tar_send_tree(fd,root,root)!=0)return;char end[1024]={0};send_all(fd,end,sizeof(end));
}

static unsigned long long save_tar_octal(const char *s,size_t n)
{
  unsigned long long v=0;for(size_t i=0;i<n&&s[i];i++){if(s[i]<'0'||s[i]>'7')continue;v=(v<<3)+(unsigned)(s[i]-'0');}return v;
}

static int save_rel_ok(const char *n)
{
  if(!n||!*n||n[0]=='/'||strstr(n,"..")||strchr(n,'\\'))return 0;return 1;
}

static int save_tar_extract(const char *tar,const char *dst)
{
  int f=open(tar,O_RDONLY);if(f<0)return -1;char block[512];int rc=0;for(;;){ssize_t n=read(f,block,512);if(n==0)break;if(n!=512){rc=-2;break;}int zero=1;for(int i=0;i<512;i++)if(block[i]){zero=0;break;}if(zero)break;astro_tar_header_t*h=(astro_tar_header_t*)block;char name[256]={0};snprintf(name,sizeof(name),"%.100s",h->name);if(!save_rel_ok(name)){rc=-3;break;}unsigned long long sz=save_tar_octal(h->size,sizeof(h->size));char out[2048];snprintf(out,sizeof(out),"%s/%s",dst,name);if(h->typeflag=='5'){if(v3_mkdirs(out)!=0){rc=-4;break;}continue;}if(h->typeflag!='0'&&h->typeflag!='\0'){unsigned long long skip=((sz+511ULL)/512ULL)*512ULL;if(lseek(f,(off_t)skip,SEEK_CUR)<0){rc=-5;break;}continue;}if(v3_parent_dirs(out)!=0){rc=-6;break;}int o=open(out,O_CREAT|O_TRUNC|O_WRONLY,0666);if(o<0){rc=-7;break;}unsigned long long left=sz;char*b=malloc(256*1024);if(!b){close(o);rc=-8;break;}while(left){size_t want=(size_t)(left>256*1024?256*1024:left);ssize_t r=read(f,b,want);if(r<=0){rc=-9;break;}size_t off=0;while(off<(size_t)r){ssize_t w=write(o,b+off,(size_t)r-off);if(w<=0){rc=-10;break;}off+=(size_t)w;}if(rc)break;left-=(unsigned long long)r;}fsync(o);free(b);close(o);if(rc)break;unsigned long long pad=(512ULL-(sz%512ULL))%512ULL;if(pad&&lseek(f,(off_t)pad,SEEK_CUR)<0){rc=-11;break;}}
  close(f);return rc;
}

static int save_snapshot_root(const char *root,const char *uid,const char *tid,char *out,size_t os)
{
  struct stat st;if(stat(root,&st)!=0||!S_ISDIR(st.st_mode)){if(out&&os)out[0]=0;return 0;}time_t now=time(NULL);char dir[1024];snprintf(dir,sizeof(dir),"%s/%s/%s/%lld",ASTRO_BACKUP_DIR,uid,tid,(long long)now);if(v3_mkdirs(dir)!=0)return -1;snprintf(out,os,"%s/current",dir);return v3_copy_tree(root,out);
}

static void save_restore_title_api(int fd,const char *target)
{
  char uid[64],tid[96],type[16]="PS5",archive[2048],root[1024];if(!query_value(target,"uid",uid,sizeof(uid))||!query_value(target,"title",tid,sizeof(tid))||!query_value(target,"archive",archive,sizeof(archive))||!v3_safe_path(archive)||strncmp(archive,ASTRO_RESTORE_DIR,strlen(ASTRO_RESTORE_DIR))||!save_title_root(uid,tid,query_value(target,"type",type,sizeof(type))?type:"PS5",root,sizeof(root))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}struct stat st;if(stat(archive,&st)!=0||!S_ISREG(st.st_mode)){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"archive\"}");return;}time_t now=time(NULL);char tmp[1024];snprintf(tmp,sizeof(tmp),"%s/%s_%s_%lld",ASTRO_RESTORE_TMP,uid,tid,(long long)now);v3_remove_tree(tmp);if(v3_mkdirs(tmp)!=0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"tmp\"}");return;}int rc=save_tar_extract(archive,tmp);if(rc==0){char bak[1200];if(save_snapshot_root(root,uid,tid,bak,sizeof(bak))<0)rc=-20;if(rc==0){struct stat rs;if(stat(root,&rs)==0)v3_remove_tree(root);if(v3_copy_tree(tmp,root)!=0)rc=-21;}}
  v3_remove_tree(tmp);unlink(archive);char j[256];snprintf(j,sizeof(j),"{\"ok\":%s,\"rc\":%d}",rc==0?"true":"false",rc);send_text(fd,rc==0?"200 OK":"500 Internal Server Error","application/json",NULL,j);
}

static int save_garlic_request(const char *method,const char *path,const char *file,char *body,size_t body_sz)
{
  int s=socket(AF_INET,SOCK_STREAM,0);if(s<0)return -1;struct sockaddr_in a;memset(&a,0,sizeof(a));a.sin_family=AF_INET;a.sin_port=htons(ASTRO_GARLIC_PORT);a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);if(connect(s,(struct sockaddr*)&a,sizeof(a))!=0){close(s);return -2;}unsigned long long sz=0;int f=-1;if(file){struct stat st;if(stat(file,&st)!=0||!S_ISREG(st.st_mode)){close(s);return -3;}sz=(unsigned long long)st.st_size;f=open(file,O_RDONLY);if(f<0){close(s);return -4;}}char h[2048];int hn=snprintf(h,sizeof(h),"%s %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nContent-Type: application/octet-stream\r\nContent-Length: %llu\r\nConnection: close\r\n\r\n",method,path,ASTRO_GARLIC_PORT,sz);if(send_all(s,h,(size_t)hn)!=0){if(f>=0)close(f);close(s);return -5;}if(f>=0){char*b=malloc(256*1024);if(!b){close(f);close(s);return -6;}for(;;){ssize_t n=read(f,b,256*1024);if(n==0)break;if(n<0||send_all(s,b,(size_t)n)!=0){free(b);close(f);close(s);return -7;}}free(b);close(f);}size_t u=0;while(u+1<body_sz){ssize_t n=recv(s,body+u,body_sz-u-1,0);if(n<=0)break;u+=(size_t)n;}close(s);body[u]=0;int status=0;char *sp=strchr(body,' ');if(sp)status=atoi(sp+1);char *p=strstr(body,"\r\n\r\n");if(p){p+=4;memmove(body,p,strlen(p)+1);}return status>=200&&status<300?0:-8;
}

static int save_start_garlic(void)
{
  if(port_open(ASTRO_GARLIC_PORT))return 1;DIR*d=opendir(ASTRO_PAYLOAD_DIR);if(d){struct dirent*e;while((e=readdir(d))){if(e->d_name[0]=='.'||!v3_contains_ci(e->d_name,"garlic"))continue;size_t n=strlen(e->d_name);if(n<4||strcasecmp(e->d_name+n-4,".elf"))continue;char p[1024];snprintf(p,sizeof(p),"%s/%s",ASTRO_PAYLOAD_DIR,e->d_name);int used=0;if(v3_send_payload(p,&used)==0)break;}closedir(d);}for(int i=0;i<20;i++){if(port_open(ASTRO_GARLIC_PORT))return 1;usleep(250000);}return 0;
}

static int save_clone_files(const char *root,const char *dst_uid,int *done)
{
  DIR*d=opendir(root);if(!d)return -1;struct dirent*e;int rc=0,count=0;while((e=readdir(d))){if(e->d_name[0]=='.')continue;char p[2048];snprintf(p,sizeof(p),"%s/%s",root,e->d_name);struct stat st;if(stat(p,&st)!=0||!S_ISREG(st.st_mode))continue;size_t ln=strlen(e->d_name);if(ln>4&&!strcasecmp(e->d_name+ln-4,".bin"))continue;if(strncmp(e->d_name,"sdimg_",6))continue;char path[256],resp[8192];snprintf(path,sizeof(path),"/api/import_encrypted?uid=%s",dst_uid);if(save_garlic_request("POST",path,p,resp,sizeof(resp))!=0||strstr(resp,"\"error\"")){rc=-2;break;}snprintf(path,sizeof(path),"/api/import_finish?uid=%s",dst_uid);memset(resp,0,sizeof(resp));if(save_garlic_request("GET",path,NULL,resp,sizeof(resp))!=0||strstr(resp,"\"error\"")){rc=-3;break;}count++;}closedir(d);if(done)*done=count;return rc;
}

static void save_clone_title_api(int fd,const char *target)
{
  char uid[64],dst[64],tid[96],type[16]="PS5",root[1024],dstroot[1024];if(!query_value(target,"uid",uid,sizeof(uid))||!query_value(target,"dst",dst,sizeof(dst))||!query_value(target,"title",tid,sizeof(tid))||!strcmp(uid,dst)||!save_title_root(uid,tid,query_value(target,"type",type,sizeof(type))?type:"PS5",root,sizeof(root))||!save_title_root(dst,tid,type,dstroot,sizeof(dstroot))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"args\"}");return;}struct stat st;if(stat(root,&st)!=0||!S_ISDIR(st.st_mode)){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"save_not_found\"}");return;}char bak[1200];if(save_snapshot_root(dstroot,dst,tid,bak,sizeof(bak))<0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"destination_backup\"}");return;}if(!save_start_garlic()){send_text(fd,"503 Service Unavailable","application/json",NULL,"{\"ok\":false,\"error\":\"garlic_offline\"}");return;}int done=0,rc=save_clone_files(root,dst,&done);char j[256];snprintf(j,sizeof(j),"{\"ok\":%s,\"rc\":%d,\"cloned\":%d}",rc==0&&done>0?"true":"false",rc,done);send_text(fd,rc==0&&done>0?"200 OK":"500 Internal Server Error","application/json",NULL,j);
}

static void v3_save_action_api(int fd,const char *target)
{
  char action[32]={0};query_value(target,"action",action,sizeof(action));if(!strcmp(action,"download")){save_download_title_api(fd,target);return;}if(!strcmp(action,"restore")){save_restore_title_api(fd,target);return;}if(!strcmp(action,"clone")){save_clone_title_api(fd,target);return;}v3_save_backup_api(fd,target);
}

static void v3_saves_page(int fd)
{
  char page[120000];v3_page_start(page,sizeof(page),"Saves","saves");const char*b=
"<style>.saveLibrary{display:grid;grid-template-columns:repeat(auto-fill,minmax(330px,1fr));gap:12px}.saveCard{background:#101923;border:1px solid #26384b;border-radius:14px;overflow:hidden;transition:border .15s,transform .15s}.saveCard:hover{border-color:#3b5875;transform:translateY(-1px)}.saveTop{display:flex;gap:13px;padding:15px}.saveCover{width:82px;height:82px;object-fit:cover;border-radius:11px;background:#182536;border:1px solid #30455d;flex:0 0 auto}.saveFallback{width:82px;height:82px;border-radius:11px;background:linear-gradient(145deg,#172941,#0d1723);border:1px solid #30455d;display:grid;place-items:center;font-size:22px;font-weight:950;color:#67afff;flex:0 0 auto}.saveText{min-width:0;flex:1}.saveText h3{margin:2px 0 7px;font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}.saveText .sub{font-size:10px;color:#8093aa;line-height:1.5}.saveText .sub b{color:#b5c7db}.saveActions{display:grid;grid-template-columns:1fr 1fr 1.25fr;gap:7px;padding:11px 13px 13px;border-top:1px solid #202f40}.saveActions .btn,.saveActions button{text-align:center;justify-content:center;display:flex;align-items:center;min-height:37px}.saveUserBar{display:flex;gap:5px;flex-wrap:wrap;margin-bottom:12px}.saveCount{margin-left:auto;color:#667b92;font-size:10px}.modalShade{position:fixed;inset:0;background:#02060bc7;display:none;place-items:center;z-index:1000;padding:16px}.modalShade.on{display:grid}.saveModal{width:min(460px,100%);background:#101923;border:1px solid #30445b;border-radius:16px;padding:20px;box-shadow:0 30px 90px #000b}.saveModal h2{font-size:18px;margin:0 0 7px}.saveModal p{font-size:11px;color:#8396ad;line-height:1.6;margin:0 0 16px}.saveModal select{width:100%;height:43px}.saveToast{position:fixed;right:18px;bottom:18px;z-index:1200;background:#10231d;border:1px solid #2c6b52;color:#7fe0b9;padding:11px 14px;border-radius:10px;font-size:11px;box-shadow:0 12px 35px #0008;display:none}.saveToast.bad{background:#29171b;border-color:#71363e;color:#ff9ba4}@media(max-width:600px){.saveLibrary{grid-template-columns:1fr}.saveActions{grid-template-columns:1fr}.saveCover,.saveFallback{width:68px;height:68px}}</style>"
"<div class='head'><div><h1>Saves</h1><p>Um card por jogo. Os arquivos internos ficam escondidos e o Astro cuida deles por baixo.</p></div><button onclick='loadS()'>↻ Atualizar</button></div><div class='panel' style='margin-bottom:12px'><div class='toolbar'><input id='sq' class='search' placeholder='Buscar jogo ou Title ID...' oninput='renderS()'><span id='ss' class='saveCount'></span></div><div id='ut' class='saveUserBar' style='margin-top:12px'></div></div><div id='sv' class='saveLibrary'></div><input id='restoreFile' type='file' accept='.tar,application/x-tar' hidden>"
"<div id='cloneShade' class='modalShade' onclick=\"if(event.target===this)closeClone()\"><div class='saveModal'><div class='kicker'>CLONAR SAVE</div><h2 id='cloneTitle'>Clonar para conta</h2><p>Escolha a conta de destino. Se já existir um save desse jogo nela, o Astro faz um backup antes da clonagem.</p><select id='cloneUser'></select><div class='row' style='justify-content:flex-end'><button class='ghost' onclick='closeClone()'>Cancelar</button><button class='green' onclick='confirmClone()'>Clonar para conta</button></div></div></div><div id='saveToast' class='saveToast'></div>"
"<script>let U=[],S=[],UF='all',M={},G=[],R=null,C=null;const E=s=>String(s??'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));const F=n=>{let u=['B','KB','MB','GB'],i=0;n=Number(n)||0;while(n>=1024&&i<3){n/=1024;i++}return n.toFixed(i?1:0)+' '+u[i]};function toast(t,b=false){saveToast.textContent=t;saveToast.className='saveToast'+(b?' bad':'');saveToast.style.display='block';clearTimeout(toast.t);toast.t=setTimeout(()=>saveToast.style.display='none',4200)}function uname(id){return U.find(x=>x.id===id)?.name||id}async function loadS(){try{let [u,s]=await Promise.all([fetch('/api/users',{cache:'no-store'}).then(r=>r.json()),fetch('/api/saves',{cache:'no-store'}).then(r=>r.json())]);U=u.users||[];S=s.items||[];let map={};for(const x of S){let k=x.uid+'|'+x.title_id+'|'+(x.type||'PS5');if(!map[k])map[k]={uid:x.uid,title_id:x.title_id,type:x.type||'PS5',size:0};map[k].size+=Number(x.size||0)}G=Object.values(map);let tids=[...new Set(G.map(x=>x.title_id))];await Promise.all(tids.map(async t=>{try{M[t]=await fetch('/api/title/meta?tid='+encodeURIComponent(t),{cache:'no-store'}).then(r=>r.json())}catch(e){M[t]={name:t}}}));ut.innerHTML=`<button class='tab ${UF==='all'?'on':''}' onclick=\"setU('all')\">Todos</button>`+U.map(x=>`<button class='tab ${UF===x.id?'on':''}' onclick='setU(${JSON.stringify(x.id)})'>${E(x.name)} <span style='opacity:.6'>${G.filter(g=>g.uid===x.id).length}</span></button>`).join('');ss.textContent=G.length+' jogo(s) com save · '+U.length+' usuário(s)';renderS()}catch(e){toast('Falha ao carregar saves.',true)}}function setU(u){UF=u;document.querySelectorAll('#ut .tab').forEach((x,i)=>x.classList.toggle('on',i===0?u==='all':U[i-1]?.id===u));renderS()}function renderS(){let q=sq.value.toLowerCase(),a=G.filter(x=>(UF==='all'||x.uid===UF)&&(!q||(M[x.title_id]?.name||x.title_id).toLowerCase().includes(q)||x.title_id.toLowerCase().includes(q)));a.sort((x,y)=>(M[x.title_id]?.name||x.title_id).localeCompare(M[y.title_id]?.name||y.title_id));sv.innerHTML=a.map(x=>{let m=M[x.title_id]||{name:x.title_id};return`<article class='saveCard'><div class='saveTop'><img class='saveCover' src='/api/title/icon?tid=${encodeURIComponent(x.title_id)}' onerror=\"this.style.display='none';this.nextElementSibling.style.display='grid'\"><div class='saveFallback' style='display:none'>${x.type==='PS4'?'4':'5'}</div><div class='saveText'><h3>${E(m.name||x.title_id)}</h3><div class='sub'><b>${E(x.title_id)}</b><br>${E(uname(x.uid))} · ${E(x.type)}<br>${F(x.size)} de dados de save</div></div><span class='badge blue'>${E(x.type)}</span></div><div class='saveActions'><button class='primary' onclick='downloadSave(${JSON.stringify(x)})'>⇩ Baixar</button><button onclick='restoreSave(${JSON.stringify(x)})'>↻ Restaurar</button><button class='green' onclick='openClone(${JSON.stringify(x)})'>⇄ Clonar para conta</button></div></article>`}).join('')||`<div class='notice' style='grid-column:1/-1'>Nenhum save encontrado para este usuário.</div>`}async function downloadSave(x){toast('Preparando '+(M[x.title_id]?.name||x.title_id)+'...');try{let r=await fetch(`/api/saves/backup?action=download&uid=${encodeURIComponent(x.uid)}&title=${encodeURIComponent(x.title_id)}&type=${encodeURIComponent(x.type)}`,{method:'POST'});if(!r.ok)throw 0;let b=await r.blob(),u=URL.createObjectURL(b),a=document.createElement('a');a.href=u;a.download=`Astro_${x.title_id}_${x.uid}.tar`;document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(u),3000);toast('Save baixado.') }catch(e){toast('Falha ao baixar o save.',true)}}function restoreSave(x){R=x;restoreFile.value='';restoreFile.click()}restoreFile.onchange=async()=>{let f=restoreFile.files[0];if(!f||!R)return;if(!confirm('Restaurar '+(M[R.title_id]?.name||R.title_id)+' para '+uname(R.uid)+'? O save atual será guardado como backup antes da restauração.'))return;let tmp=`/data/Astro/restore_uploads/${Date.now()}_${R.uid}_${R.title_id}.tar`;toast('Enviando backup...');try{let up=await fetch('/api/files/upload?path='+encodeURIComponent(tmp),{method:'POST',body:f});if(!up.ok)throw 0;toast('Restaurando save...');let r=await fetch(`/api/saves/backup?action=restore&uid=${encodeURIComponent(R.uid)}&title=${encodeURIComponent(R.title_id)}&type=${encodeURIComponent(R.type)}&archive=${encodeURIComponent(tmp)}`,{method:'POST'});if(!r.ok)throw 0;toast('Save restaurado com sucesso.');loadS()}catch(e){toast('Falha ao restaurar o save.',true)}};function openClone(x){C=x;let opts=U.filter(u=>u.id!==x.uid);cloneTitle.textContent='Clonar '+(M[x.title_id]?.name||x.title_id);cloneUser.innerHTML=opts.map(u=>`<option value='${E(u.id)}'>${E(u.name)}</option>`).join('');if(!opts.length){toast('Não existe outra conta para receber este save.',true);return}cloneShade.classList.add('on')}function closeClone(){cloneShade.classList.remove('on');C=null}async function confirmClone(){if(!C||!cloneUser.value)return;let dst=cloneUser.value;if(!confirm('Clonar este jogo de '+uname(C.uid)+' para '+uname(dst)+'?'))return;cloneShade.classList.remove('on');toast('Preparando clonagem e Garlic...');try{let r=await fetch(`/api/saves/backup?action=clone&uid=${encodeURIComponent(C.uid)}&dst=${encodeURIComponent(dst)}&title=${encodeURIComponent(C.title_id)}&type=${encodeURIComponent(C.type)}`,{method:'POST'});let j=await r.json().catch(()=>({}));if(!r.ok){if(j.error==='garlic_offline')throw new Error('Garlic SaveMgr não iniciou');throw new Error('Falha na clonagem')}toast('Save clonado para '+uname(dst)+'.');loadS()}catch(e){toast(e.message||'Falha na clonagem.',true)}finally{C=null}}loadS()</script>";strncat(page,b,sizeof(page)-strlen(page)-1);v3_page_end(page,sizeof(page));send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}
