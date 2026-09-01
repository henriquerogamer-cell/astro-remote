/* Astro V3 Homebrew catalog.
 * The HB Store uses https://api.pkg-zone.com/store.db (SQLite). The previous
 * UI tried to call a JSON endpoint from the browser and failed on CORS/API
 * changes. Astro now downloads the official store.db itself with libcurl,
 * caches it on the PS5 and serves a small local JSON API to the dashboard.
 */
#include <curl/curl.h>

#define ASTRO_HB_DIR ASTRO_DIR "/homebrew"
#define ASTRO_HB_DB ASTRO_HB_DIR "/store.db"
#define ASTRO_HB_TMP ASTRO_HB_DIR "/store.db.tmp"
#define ASTRO_HB_URL "https://api.pkg-zone.com/store.db"
#define ASTRO_HB_TTL (6 * 60 * 60)
#define ASTRO_HB_PAGE_SIZE 36

static size_t hb_curl_write(void *ptr,size_t size,size_t nmemb,void *userdata)
{
  FILE *f=(FILE*)userdata;return fwrite(ptr,size,nmemb,f);
}

static int hb_db_valid(const char *path,int *rows)
{
  sqlite3 *db=NULL;sqlite3_stmt *st=NULL;int ok=0,n=0;
  if(sqlite3_open_v2(path,&db,SQLITE_OPEN_READONLY,NULL)!=SQLITE_OK||!db)goto done;
  if(sqlite3_prepare_v2(db,"SELECT COUNT(*) FROM homebrews",-1,&st,NULL)!=SQLITE_OK)goto done;
  if(sqlite3_step(st)==SQLITE_ROW){n=sqlite3_column_int(st,0);ok=n>0;}
 done:
  if(st)sqlite3_finalize(st);if(db)sqlite3_close(db);if(rows)*rows=n;return ok?0:-1;
}

static int hb_download_db(char *err,size_t es)
{
  v3_mkdirs(ASTRO_HB_DIR);unlink(ASTRO_HB_TMP);FILE *f=fopen(ASTRO_HB_TMP,"wb");if(!f){snprintf(err,es,"temp_open");return -1;}
  static int curl_ready=0;if(!curl_ready){if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){fclose(f);unlink(ASTRO_HB_TMP);snprintf(err,es,"curl_init");return -2;}curl_ready=1;}
  CURL *c=curl_easy_init();if(!c){fclose(f);unlink(ASTRO_HB_TMP);snprintf(err,es,"curl_easy_init");return -3;}
  curl_easy_setopt(c,CURLOPT_URL,ASTRO_HB_URL);
  curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
  curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
  curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,12L);
  curl_easy_setopt(c,CURLOPT_TIMEOUT,45L);
  curl_easy_setopt(c,CURLOPT_USERAGENT,"Astro-PS5/3");
  curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,hb_curl_write);
  curl_easy_setopt(c,CURLOPT_WRITEDATA,f);
  CURLcode cr=curl_easy_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);curl_easy_cleanup(c);fflush(f);fsync(fileno(f));fclose(f);
  if(cr!=CURLE_OK||code<200||code>=300){snprintf(err,es,"curl:%d http:%ld",(int)cr,code);unlink(ASTRO_HB_TMP);return -4;}
  struct stat st;if(stat(ASTRO_HB_TMP,&st)!=0||st.st_size<4096){snprintf(err,es,"download_invalido");unlink(ASTRO_HB_TMP);return -5;}
  int rows=0;if(hb_db_valid(ASTRO_HB_TMP,&rows)!=0){snprintf(err,es,"sqlite_invalido");unlink(ASTRO_HB_TMP);return -6;}
  unlink(ASTRO_HB_DB);if(rename(ASTRO_HB_TMP,ASTRO_HB_DB)!=0){snprintf(err,es,"rename:%d",errno);unlink(ASTRO_HB_TMP);return -7;}return rows;
}

/* 0=fresh cache, 1=downloaded, 2=stale cache after refresh failure, <0=no DB */
static int hb_ensure_db(int force,char *err,size_t es,long long *age_out)
{
  struct stat st;time_t now=time(NULL);int have=(stat(ASTRO_HB_DB,&st)==0&&S_ISREG(st.st_mode));long long age=have?(long long)(now-st.st_mtime):0;if(age<0)age=0;if(age_out)*age_out=age;
  if(!force&&have&&age<ASTRO_HB_TTL&&hb_db_valid(ASTRO_HB_DB,NULL)==0)return 0;
  int rc=hb_download_db(err,es);if(rc>=0){if(age_out)*age_out=0;return 1;}
  if(have&&hb_db_valid(ASTRO_HB_DB,NULL)==0)return 2;return -1;
}

static void hb_image_https(const char *in,char *out,size_t os)
{
  if(!in)in="";const char *old="http://api.pkg-zone.com/";if(!strncmp(in,old,strlen(old)))snprintf(out,os,"https://api.pkg-zone.com/%s",in+strlen(old));else snprintf(out,os,"%s",in);
}

static const char *hb_cat_clause(const char *cat)
{
  if(!strcmp(cat,"utility"))return "(lower(COALESCE(apptype,'')) LIKE '%utility%' OR lower(COALESCE(apptype,'')) LIKE '%tool%')";
  if(!strcmp(cat,"game"))return "(lower(COALESCE(apptype,'')) LIKE '%game%')";
  if(!strcmp(cat,"emulator"))return "(lower(COALESCE(apptype,'')) LIKE '%emulator%')";
  if(!strcmp(cat,"media"))return "(lower(COALESCE(apptype,'')) LIKE '%media%' OR lower(COALESCE(apptype,'')) LIKE '%multimedia%')";
  return "(lower(COALESCE(apptype,'')) LIKE '%utility%' OR lower(COALESCE(apptype,'')) LIKE '%tool%' OR lower(COALESCE(apptype,'')) LIKE '%homebrew%' OR lower(COALESCE(apptype,'')) LIKE '%game%' OR lower(COALESCE(apptype,'')) LIKE '%emulator%' OR lower(COALESCE(apptype,'')) LIKE '%media%' OR lower(COALESCE(apptype,'')) LIKE '%multimedia%')";
}

static void v3_homebrew_api(int fd,const char *target)
{
  char q[192]={0},cat[32]="all",sort[32]="recent",refresh[16]={0};query_value(target,"q",q,sizeof(q));query_value(target,"cat",cat,sizeof(cat));query_value(target,"sort",sort,sizeof(sort));query_value(target,"refresh",refresh,sizeof(refresh));
  char pg[24]={0};query_value(target,"page",pg,sizeof(pg));int page=atoi(pg);if(page<1)page=1;if(page>500)page=500;
  char err[160]={0};long long age=0;int src=hb_ensure_db(!strcmp(refresh,"1"),err,sizeof(err),&age);if(src<0){char ee[256],j[512];json_escape(err,ee,sizeof(ee));snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"catalog_unavailable\",\"detail\":\"%s\"}",ee);send_text(fd,"503 Service Unavailable","application/json; charset=utf-8",NULL,j);return;}
  sqlite3 *db=NULL;if(sqlite3_open_v2(ASTRO_HB_DB,&db,SQLITE_OPEN_READONLY,NULL)!=SQLITE_OK||!db){if(db)sqlite3_close(db);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"db_open\"}");return;}
  const char *cc=hb_cat_clause(cat);const char *order=!strcmp(sort,"name")?"name COLLATE NOCASE ASC":!strcmp(sort,"author")?"Author COLLATE NOCASE ASC, name COLLATE NOCASE ASC":"releaseddate DESC, name COLLATE NOCASE ASC";
  char where[1800];snprintf(where,sizeof(where),"%s AND (?1='' OR lower(COALESCE(name,'')) LIKE lower(?2) OR lower(COALESCE(id,'')) LIKE lower(?2) OR lower(COALESCE(Author,'')) LIKE lower(?2) OR lower(COALESCE(desc,'')) LIKE lower(?2))",cc);
  char pat[256];snprintf(pat,sizeof(pat),"%%%s%%",q);
  char sqlc[2400];snprintf(sqlc,sizeof(sqlc),"SELECT COUNT(*) FROM homebrews WHERE %s",where);sqlite3_stmt *ct=NULL;int total=0;if(sqlite3_prepare_v2(db,sqlc,-1,&ct,NULL)==SQLITE_OK){sqlite3_bind_text(ct,1,q,-1,SQLITE_TRANSIENT);sqlite3_bind_text(ct,2,pat,-1,SQLITE_TRANSIENT);if(sqlite3_step(ct)==SQLITE_ROW)total=sqlite3_column_int(ct,0);}if(ct)sqlite3_finalize(ct);
  int pages=(total+ASTRO_HB_PAGE_SIZE-1)/ASTRO_HB_PAGE_SIZE;if(pages<1)pages=1;if(page>pages)page=pages;int off=(page-1)*ASTRO_HB_PAGE_SIZE;
  char sql[4096];snprintf(sql,sizeof(sql),"SELECT COALESCE(id,''),COALESCE(name,''),COALESCE(desc,''),COALESCE(image,''),COALESCE(package,''),COALESCE(version,''),COALESCE(Author,''),COALESCE(apptype,''),COALESCE(pv,''),COALESCE(releaseddate,''),COALESCE(Size,'') FROM homebrews WHERE %s ORDER BY %s LIMIT %d OFFSET %d",where,order,ASTRO_HB_PAGE_SIZE,off);
  sqlite3_stmt *st=NULL;if(sqlite3_prepare_v2(db,sql,-1,&st,NULL)!=SQLITE_OK){sqlite3_close(db);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"schema_query\"}");return;}sqlite3_bind_text(st,1,q,-1,SQLITE_TRANSIENT);sqlite3_bind_text(st,2,pat,-1,SQLITE_TRANSIENT);
  size_t cap=768*1024,u=0;char *j=malloc(cap);if(!j){sqlite3_finalize(st);sqlite3_close(db);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"memory\"}");return;}
  u+=snprintf(j+u,cap-u,"{\"ok\":true,\"source\":\"%s\",\"age\":%lld,\"total\":%d,\"page\":%d,\"pages\":%d,\"items\":[",src==1?"refreshed":src==2?"stale":"cache",age,total,page,pages);int first=1;
  while(sqlite3_step(st)==SQLITE_ROW&&u+8192<cap){char v[11][2048];for(int i=0;i<11;i++){const unsigned char *z=sqlite3_column_text(st,i);json_escape(z?(const char*)z:"",v[i],sizeof(v[i]));}char imgraw[2048],imge[4096];const unsigned char *iz=sqlite3_column_text(st,3);hb_image_https(iz?(const char*)iz:"",imgraw,sizeof(imgraw));json_escape(imgraw,imge,sizeof(imge));const char *pv=(const char*)sqlite3_column_text(st,8);int ps5=pv&&(strcasestr(pv,"ps5")||strcasestr(pv,"prospero"));u+=snprintf(j+u,cap-u,"%s{\"id\":\"%s\",\"name\":\"%s\",\"desc\":\"%s\",\"image\":\"%s\",\"url\":\"%s\",\"version\":\"%s\",\"author\":\"%s\",\"type\":\"%s\",\"platform\":\"%s\",\"date\":\"%s\",\"size\":\"%s\",\"ps5\":%s}",first?"":",",v[0],v[1],v[2],imge,v[4],v[5],v[6],v[7],v[8],v[9],v[10],ps5?"true":"false");first=0;}
  sqlite3_finalize(st);sqlite3_close(db);snprintf(j+u,cap-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}

static void v3_homebrew_page(int fd)
{
  char page[98000];v3_page_start(page,sizeof(page),"Homebrew","homebrew");const char *b=
"<div class='head'><div><h1>Homebrew</h1><p>Catálogo do HB Store / PKG-Zone lido e cacheado pelo próprio Astro.</p></div><button id='refreshBtn' onclick='loadHB(1,true)'>↻ Atualizar catálogo</button></div>"
"<section class='panel'><div class='tabs'><button class='tab on' onclick=\"cat('all',this)\">Todos</button><button class='tab' onclick=\"cat('utility',this)\">Utilitários</button><button class='tab' onclick=\"cat('game',this)\">HB Games</button><button class='tab' onclick=\"cat('emulator',this)\">Emuladores</button><button class='tab' onclick=\"cat('media',this)\">Multimídia</button></div>"
"<div class='toolbar' style='margin-bottom:13px'><input id='hq' class='search' placeholder='Buscar por nome, autor ou Title ID...' onkeydown=\"if(event.key==='Enter')loadHB(1)\"><select id='ho' onchange='loadHB(1)'><option value='recent'>Mais recentes</option><option value='name'>A-Z</option><option value='author'>Autor</option></select><button onclick='loadHB(1)'>Buscar</button><span id='hs' class='muted'>Preparando catálogo...</span></div>"
"<div id='hbnote' class='notice' style='margin-bottom:12px'>O navegador conversa somente com o Astro. O Astro baixa <b>store.db</b>, consulta o SQLite localmente e mantém cache no PS5.</div><div id='hg' class='storegrid'></div><div class='row' style='justify-content:center;margin-top:15px'><button id='prev' onclick='loadHB(PAGE-1)'>← Anterior</button><span id='pg' class='muted'></span><button id='next' onclick='loadHB(PAGE+1)'>Próxima →</button></div></section>"
"<script>let CAT='all',PAGE=1,LAST=1;const E=s=>String(s??'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));function cat(c,e){CAT=c;document.querySelectorAll('.tab').forEach(x=>x.classList.toggle('on',x===e));loadHB(1)}function safe(u){return /^https?:\\/\\//i.test(String(u||''))?String(u):''}function ago(n){n=Number(n)||0;if(n<60)return'agora';if(n<3600)return Math.floor(n/60)+' min';if(n<86400)return Math.floor(n/3600)+' h';return Math.floor(n/86400)+' d'}async function loadHB(p=PAGE,force=false){if(p<1)return;PAGE=p;hs.textContent=force?'Atualizando store.db...':'Carregando catálogo...';hg.innerHTML='<div class=\"muted\">Consultando...</div>';refreshBtn.disabled=true;let u='/api/homebrew?page='+PAGE+'&cat='+encodeURIComponent(CAT)+'&sort='+encodeURIComponent(ho.value)+'&q='+encodeURIComponent(hq.value||'')+(force?'&refresh=1':'');try{let r=await fetch(u,{cache:'no-store'}),j=await r.json();if(!r.ok||!j.ok)throw new Error(j.detail||j.error||('HTTP '+r.status));PAGE=j.page||1;LAST=j.pages||1;let a=j.items||[];let src=j.source==='refreshed'?'catálogo atualizado':j.source==='stale'?'cache anterior':'cache local';hs.textContent=(j.total||0)+' itens · '+src+' · '+ago(j.age);hbnote.className='notice'+(j.source==='stale'?' bad':' good');hbnote.innerHTML=j.source==='stale'?'O PKG-Zone não respondeu agora. Exibindo o último catálogo válido salvo no PS5.':'Catálogo disponível localmente no Astro. A navegação não depende mais de CORS do PKG-Zone.';hg.innerHTML=a.map(x=>{let im=safe(x.image),ln=safe(x.url),direct=/\\.pkg(?:$|\\?)/i.test(ln);return`<article class='pkg'><div class='pkgimg' ${im?`style=\"background-image:url('${E(im)}')\"`:''}></div><div class='pkgb'><h3>${E(x.name||x.id)}</h3><div class='meta'>${E(x.author||'Autor não informado')}${x.version?' · v'+E(x.version):''}</div><div class='row'><span class='badge blue'>${E(x.type||'Homebrew')}</span>${x.ps5?`<span class='badge run'>PS5</span>`:`<span class='badge'>${E(x.platform||'HB')}</span>`}${x.size?`<span class='badge'>${E(x.size)}</span>`:''}</div><p class='muted'>${E(x.desc||'').slice(0,190)}</p><div class='row'>${ln?`<a class='btn ${direct?'primary':'ghost'} sm' target='_blank' rel='noopener' href='${E(ln)}'>${direct?'Baixar PKG':'Detalhes'}</a>`:''}<span class='meta'>${E(x.id||'')}</span></div></div></article>`}).join('')||'<div class=\"notice\">Nenhum homebrew encontrado com esses filtros.</div>';pg.textContent='Página '+PAGE+' de '+LAST;prev.disabled=PAGE<=1;next.disabled=PAGE>=LAST}catch(e){hs.textContent='Catálogo indisponível';hbnote.className='notice bad';hbnote.textContent='Não foi possível obter nem abrir um catálogo local: '+e.message;hg.innerHTML='';pg.textContent='';}finally{refreshBtn.disabled=false}}loadHB(1)</script>";
  strncat(page,b,sizeof(page)-strlen(page)-1);v3_page_end(page,sizeof(page));send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}
