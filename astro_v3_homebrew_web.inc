/* Astro V3 Homebrew catalog via the live PKG-Zone website.
 * api.pkg-zone.com/store.db is a legacy dependency and may no longer resolve.
 * The live PS5 catalog is served by https://pkg-zone.com/?console=ps5.
 * Astro downloads that public page itself, caches the HTML on the PS5 and
 * exposes a local JSON API to the UI. No browser-side CORS/API dependency.
 */

#define ASTRO_HBWEB_DIR ASTRO_DIR "/homebrew"
#define ASTRO_HBWEB_HTML ASTRO_HBWEB_DIR "/pkgzone-ps5.html"
#define ASTRO_HBWEB_TMP ASTRO_HBWEB_DIR "/pkgzone-ps5.tmp"
#define ASTRO_HBWEB_URL "https://pkg-zone.com/?console=ps5"
#define ASTRO_HBWEB_TTL (6 * 60 * 60)
#define ASTRO_HBWEB_MAX_ITEMS 128

typedef struct hbweb_item {
  char id[64];
  char name[256];
  char version[48];
  char category[48];
  char detail[256];
} hbweb_item_t;

static size_t hbweb_write_file(void *ptr,size_t size,size_t nmemb,void *userdata)
{
  return fwrite(ptr,size,nmemb,(FILE*)userdata);
}

static int hbweb_download(char *err,size_t es)
{
  v3_mkdirs(ASTRO_HBWEB_DIR);unlink(ASTRO_HBWEB_TMP);
  FILE *f=fopen(ASTRO_HBWEB_TMP,"wb");
  if(!f){snprintf(err,es,"temp_open");return -1;}
  static int ready=0;
  if(!ready){if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK){fclose(f);unlink(ASTRO_HBWEB_TMP);snprintf(err,es,"curl_init");return -2;}ready=1;}
  CURL *c=curl_easy_init();
  if(!c){fclose(f);unlink(ASTRO_HBWEB_TMP);snprintf(err,es,"curl_easy_init");return -3;}
  curl_easy_setopt(c,CURLOPT_URL,ASTRO_HBWEB_URL);
  curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);
  curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
  curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,12L);
  curl_easy_setopt(c,CURLOPT_TIMEOUT,45L);
  curl_easy_setopt(c,CURLOPT_USERAGENT,"Mozilla/5.0 (PlayStation 5; Astro/3)");
  curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
  curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,hbweb_write_file);
  curl_easy_setopt(c,CURLOPT_WRITEDATA,f);
  CURLcode cr=curl_easy_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);curl_easy_cleanup(c);
  fflush(f);fsync(fileno(f));fclose(f);
  if(cr!=CURLE_OK||code<200||code>=300){snprintf(err,es,"curl:%d http:%ld",(int)cr,code);unlink(ASTRO_HBWEB_TMP);return -4;}
  struct stat st;
  if(stat(ASTRO_HBWEB_TMP,&st)!=0||st.st_size<2048){snprintf(err,es,"html_invalido");unlink(ASTRO_HBWEB_TMP);return -5;}
  int rf=open(ASTRO_HBWEB_TMP,O_RDONLY);if(rf<0){snprintf(err,es,"html_open");unlink(ASTRO_HBWEB_TMP);return -6;}
  size_t probe=(size_t)(st.st_size>512*1024?512*1024:st.st_size);char *buf=malloc(probe+1);if(!buf){close(rf);unlink(ASTRO_HBWEB_TMP);snprintf(err,es,"memory");return -7;}
  ssize_t n=read(rf,buf,probe);close(rf);if(n<0)n=0;buf[n]=0;
  int ok=strstr(buf,"/details/")!=NULL && (strstr(buf,"Supports PS5")!=NULL||strstr(buf,"supports PS5")!=NULL||strstr(buf,"PS5")!=NULL);free(buf);
  if(!ok){snprintf(err,es,"pagina_sem_catalogo");unlink(ASTRO_HBWEB_TMP);return -8;}
  unlink(ASTRO_HBWEB_HTML);if(rename(ASTRO_HBWEB_TMP,ASTRO_HBWEB_HTML)!=0){snprintf(err,es,"rename:%d",errno);unlink(ASTRO_HBWEB_TMP);return -9;}return 0;
}

static int hbweb_ensure(int force,char *err,size_t es,long long *age_out)
{
  struct stat st;time_t now=time(NULL);int have=(stat(ASTRO_HBWEB_HTML,&st)==0&&S_ISREG(st.st_mode));long long age=have?(long long)(now-st.st_mtime):0;if(age<0)age=0;if(age_out)*age_out=age;
  if(!force&&have&&age<ASTRO_HBWEB_TTL)return 0;
  int rc=hbweb_download(err,es);if(rc==0){if(age_out)*age_out=0;return 1;}
  if(have)return 2;return -1;
}

static char *hbweb_load(size_t *sz)
{
  *sz=0;struct stat st;if(stat(ASTRO_HBWEB_HTML,&st)!=0||st.st_size<=0||st.st_size>8*1024*1024)return NULL;
  int f=open(ASTRO_HBWEB_HTML,O_RDONLY);if(f<0)return NULL;char *b=malloc((size_t)st.st_size+1);if(!b){close(f);return NULL;}size_t off=0;while(off<(size_t)st.st_size){ssize_t n=read(f,b+off,(size_t)st.st_size-off);if(n<=0)break;off+=(size_t)n;}close(f);b[off]=0;*sz=off;return b;
}

static void hbweb_entity_decode(char *s)
{
  struct {const char *a,*b;} m[]={{"&amp;","&"},{"&quot;","\""},{"&#39;","'"},{"&apos;","'"},{"&nbsp;"," "},{"&lt;","<"},{"&gt;",">"}};
  for(size_t k=0;k<sizeof(m)/sizeof(m[0]);k++){char *p;while((p=strstr(s,m[k].a))){size_t al=strlen(m[k].a),bl=strlen(m[k].b);memmove(p+bl,p+al,strlen(p+al)+1);memcpy(p,m[k].b,bl);}}
}

static void hbweb_strip_tags(const char *a,const char *z,char *out,size_t os)
{
  size_t u=0;int tag=0,space=1;for(const char *p=a;p<z&&*p&&u+1<os;p++){char c=*p;if(c=='<'){tag=1;continue;}if(c=='>'){tag=0;continue;}if(tag)continue;if((unsigned char)c<=0x20){if(!space&&u+1<os){out[u++]=' ';space=1;}continue;}out[u++]=c;space=0;}while(u&&out[u-1]==' ')u--;out[u]=0;hbweb_entity_decode(out);
}

static int hbweb_seen(hbweb_item_t *it,int n,const char *id)
{
  for(int i=0;i<n;i++)if(!strcmp(it[i].id,id))return 1;return 0;
}

static void hbweb_guess_category(const char *ctx,size_t n,char *out,size_t os)
{
  char tmp[1600];if(n>=sizeof(tmp))n=sizeof(tmp)-1;memcpy(tmp,ctx,n);tmp[n]=0;
  if(strcasestr(tmp,"Utility"))snprintf(out,os,"Utility");else if(strcasestr(tmp,"Emulator"))snprintf(out,os,"Emulator");else if(strcasestr(tmp,"Media"))snprintf(out,os,"Media");else if(strcasestr(tmp,"Game"))snprintf(out,os,"Game");else snprintf(out,os,"Homebrew");
}

static int hbweb_parse(const char *html,size_t len,hbweb_item_t *items,int max)
{
  int count=0;const char *limit=strstr(html,"Top Apps");if(!limit)limit=html+len;const char *p=html;
  while(count<max && (p=strstr(p,"/details/")) && p<limit){const char *q=p+9;char id[64];size_t k=0;while(q<limit&&*q&&*q!='\"'&&*q!='\''&&*q!='?'&&*q!='/'&&*q!='#'&&k+1<sizeof(id)){char c=*q++;if(!(isalnum((unsigned char)c)||c=='_'||c=='-'))break;id[k++]=c;}id[k]=0;if(k<4||hbweb_seen(items,count,id)){p=q;continue;}
    const char *astart=p;for(int back=0;astart>html&&back<768;back++,astart--)if(astart[0]=='<'&&astart[1]=='a')break;const char *gt=strchr(astart,'>');const char *aend=gt?strstr(gt,"</a>"):NULL;if(!gt||!aend||aend>limit||aend-gt>6000){p=q;continue;}
    char text[1024];hbweb_strip_tags(gt+1,aend,text,sizeof(text));if(!strcasestr(text,"PS5")){p=aend+4;continue;}
    hbweb_item_t *x=&items[count];memset(x,0,sizeof(*x));snprintf(x->id,sizeof(x->id),"%s",id);snprintf(x->detail,sizeof(x->detail),"https://pkg-zone.com/details/%s",id);
    char *v=text;if(*v=='v'||*v=='V'){char *e=v+1;while(*e&&!isspace((unsigned char)*e)&&*e!='|')e++;size_t vn=(size_t)(e-v);if(vn>=sizeof(x->version))vn=sizeof(x->version)-1;memcpy(x->version,v,vn);x->version[vn]=0;}
    char *name=strcasestr(text,"More Info");name=name?name+9:text;while(*name&&isspace((unsigned char)*name))name++;char clean[512];snprintf(clean,sizeof(clean),"%s",name);char *sup=strcasestr(clean,"Supports PS5");if(sup){char *after=sup+12;while(*after==' '||*after=='|'||*after=='-')after++;if(*after)memmove(clean,after,strlen(after)+1);}char *mi=strcasestr(clean,"More Info");if(mi){mi+=9;while(*mi&&isspace((unsigned char)*mi))mi++;memmove(clean,mi,strlen(mi)+1);}size_t cn=strlen(clean);while(cn&&isspace((unsigned char)clean[cn-1]))clean[--cn]=0;if(cn>=4&&!strcmp(clean+cn-4,"-//-")){clean[cn-4]=0;cn-=4;while(cn&&isspace((unsigned char)clean[cn-1]))clean[--cn]=0;}if(!clean[0])snprintf(clean,sizeof(clean),"%s",id);snprintf(x->name,sizeof(x->name),"%s",clean);
    const char *ctx=p>html+1200?p-1200:html;hbweb_guess_category(ctx,(size_t)(p-ctx),x->category,sizeof(x->category));count++;p=aend+4;
  }
  return count;
}

static int hbweb_match_cat(const hbweb_item_t *x,const char *cat)
{
  if(!cat||!strcmp(cat,"all"))return 1;if(!strcmp(cat,"utility"))return !strcasecmp(x->category,"Utility");if(!strcmp(cat,"emulator"))return !strcasecmp(x->category,"Emulator");if(!strcmp(cat,"media"))return !strcasecmp(x->category,"Media");if(!strcmp(cat,"game"))return !strcasecmp(x->category,"Game");return 1;
}

static void v3_homebrew_api(int fd,const char *target)
{
  char refresh[16]={0},q[192]={0},cat[32]="all";query_value(target,"refresh",refresh,sizeof(refresh));query_value(target,"q",q,sizeof(q));query_value(target,"cat",cat,sizeof(cat));char err[160]={0};long long age=0;int src=hbweb_ensure(!strcmp(refresh,"1"),err,sizeof(err),&age);if(src<0){char ee[256],j[512];json_escape(err,ee,sizeof(ee));snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"catalog_unavailable\",\"detail\":\"%s\"}",ee);send_text(fd,"503 Service Unavailable","application/json; charset=utf-8",NULL,j);return;}
  size_t sz=0;char *html=hbweb_load(&sz);if(!html){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"cache_read\"}");return;}hbweb_item_t items[ASTRO_HBWEB_MAX_ITEMS];int n=hbweb_parse(html,sz,items,ASTRO_HBWEB_MAX_ITEMS);free(html);if(n<=0){send_text(fd,"502 Bad Gateway","application/json",NULL,"{\"ok\":false,\"error\":\"parse_empty\"}");return;}
  size_t cap=384*1024,u=0;char *j=malloc(cap);if(!j){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"memory\"}");return;}u+=snprintf(j+u,cap-u,"{\"ok\":true,\"source\":\"%s\",\"age\":%lld,\"items\":[",src==1?"refreshed":src==2?"stale":"cache");int first=1,total=0;
  for(int i=0;i<n&&u+2048<cap;i++){if(!hbweb_match_cat(&items[i],cat))continue;if(q[0]&&!v3_contains_ci(items[i].name,q)&&!v3_contains_ci(items[i].id,q))continue;char id[160],nm[640],vr[128],ct[128],dt[640];json_escape(items[i].id,id,sizeof(id));json_escape(items[i].name,nm,sizeof(nm));json_escape(items[i].version,vr,sizeof(vr));json_escape(items[i].category,ct,sizeof(ct));json_escape(items[i].detail,dt,sizeof(dt));u+=snprintf(j+u,cap-u,"%s{\"id\":\"%s\",\"name\":\"%s\",\"version\":\"%s\",\"type\":\"%s\",\"detail\":\"%s\",\"ps5\":true}",first?"":",",id,nm,vr,ct,dt);first=0;total++;}snprintf(j+u,cap-u,"] ,\"total\":%d}",total);send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}

static void v3_homebrew_page(int fd)
{
  char page[90000];v3_page_start(page,sizeof(page),"Homebrew","homebrew");const char *b=
"<div class='head'><div><h1>Homebrew</h1><p>Catálogo PS5 lido diretamente do PKG-Zone e cacheado pelo próprio Astro.</p></div><button id='refreshBtn' onclick='loadHB(true)'>↻ Atualizar catálogo</button></div>"
"<section class='panel'><div class='tabs'><button class='tab on' onclick=\"cat('all',this)\">Todos</button><button class='tab' onclick=\"cat('utility',this)\">Utilitários</button><button class='tab' onclick=\"cat('game',this)\">HB Games</button><button class='tab' onclick=\"cat('emulator',this)\">Emuladores</button><button class='tab' onclick=\"cat('media',this)\">Multimídia</button></div>"
"<div class='toolbar' style='margin-bottom:13px'><input id='hq' class='search' placeholder='Buscar por nome ou Title ID...' onkeydown=\"if(event.key==='Enter')loadHB()\"><button onclick='loadHB()'>Buscar</button><span id='hs' class='muted'>Preparando catálogo...</span></div>"
"<div id='usbState' class='notice bad' style='margin-bottom:12px'>Procurando pendrive ou HD USB...</div><div id='hbnote' class='notice' style='margin-bottom:12px'>Na firmware 12.60, o Astro baixa o PKG para a raiz do USB. Depois instale em Debug Settings &gt; Game &gt; Package Installer.</div><div id='installState' class='notice good' style='display:none;margin-bottom:12px'></div><div id='hg' class='storegrid'></div></section>"
"<script>let USBOK=false,DLSTATE='idle';async function usbWatch(){try{let r=await fetch('/api/homebrew/status',{cache:'no-store'}),j=await r.json();USBOK=!!j.usb_present;DLSTATE=j.state||'idle';usbState.className='notice '+(USBOK?'good':'bad');usbState.textContent=USBOK?'USB encontrado em '+j.usb+'. Downloads liberados.':'USB não encontrado. Conecte um pendrive ou HD USB gravável para liberar os downloads.';document.querySelectorAll('#hg button[data-i]').forEach(b=>{b.disabled=!USBOK||DLSTATE==='downloading';b.textContent=DLSTATE==='downloading'?'Baixando...':'Baixar no USB'});if(DLSTATE==='downloading'||DLSTATE==='ready'||DLSTATE==='error'){installState.style.display='block';installState.className='notice '+(DLSTATE==='error'?'bad':'good');installState.textContent=DLSTATE==='downloading'?(j.name+' · baixando para o USB · '+j.progress+'%'):(j.detail+(j.path?' Arquivo: '+j.path:''));}}catch(e){USBOK=false;usbState.className='notice bad';usbState.textContent='Não foi possível verificar o USB.';}setTimeout(usbWatch,1500)}setTimeout(usbWatch,300);</script>"
"<script>let CAT='all',POLL=0;const E=s=>String(s??'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));function cat(c,e){CAT=c;document.querySelectorAll('.tab').forEach(x=>x.classList.toggle('on',x===e));loadHB()}function ago(n){n=Number(n)||0;if(n<60)return'agora';if(n<3600)return Math.floor(n/60)+' min';if(n<86400)return Math.floor(n/3600)+' h';return Math.floor(n/86400)+' d'}async function installPkg(x,b){if(!confirm('Baixar e instalar '+x.name+'?'))return;b.disabled=true;b.textContent='Enviando...';let u='/api/homebrew/install?kind='+(x.store?'store':'app')+'&id='+encodeURIComponent(x.id||'')+'&version='+encodeURIComponent(String(x.version||'').replace(/^v/i,''))+'&name='+encodeURIComponent(x.name||'Homebrew');try{let r=await fetch(u,{method:'POST'}),j=await r.json();if(!r.ok||!j.ok)throw new Error((j.detail||j.error||'Falha')+(j.code?' ('+j.code+')':''));installState.style.display='block';installState.className='notice good';installState.textContent=j.name+' entrou na fila de instalação.';pollInstall()}catch(e){installState.style.display='block';installState.className='notice bad';installState.textContent='Não foi possível instalar: '+e.message;b.disabled=false;b.textContent='Baixar e instalar'}}async function pollInstall(){clearTimeout(POLL);try{let r=await fetch('/api/homebrew/status',{cache:'no-store'}),j=await r.json();if(j.active){installState.style.display='block';let done=j.status==='playable',bad=j.status==='error'||j.error_code!=='0x00000000';installState.className='notice '+(bad?'bad':'good');installState.textContent=bad?(j.detail+' '+j.error_code):(done?j.name+' instalado com sucesso.':j.name+' · '+(j.status||'instalando')+' · '+j.progress+'%');if(!done&&!bad)POLL=setTimeout(pollInstall,1500)}}catch(e){POLL=setTimeout(pollInstall,3000)}}async function loadHB(force=false){hs.textContent=force?'Atualizando PKG-Zone...':'Carregando catálogo...';hg.innerHTML='<div class=\"muted\">Consultando...</div>';refreshBtn.disabled=true;let u='/api/homebrew?cat='+encodeURIComponent(CAT)+'&q='+encodeURIComponent(hq.value||'')+(force?'&refresh=1':'');try{let r=await fetch(u,{cache:'no-store'}),j=await r.json();if(!r.ok||!j.ok)throw new Error(j.detail||j.error||('HTTP '+r.status));let a=j.items||[];if(CAT==='all'&&!hq.value)a.unshift({store:true,id:'HB-STORE',name:'Homebrew Store',version:'PS5',type:'Loja oficial'});hs.textContent=(j.total||0)+' itens · '+(j.source==='stale'?'cache anterior':j.source==='refreshed'?'atualizado':'cache local')+' · '+ago(j.age);hbnote.className='notice '+(j.source==='stale'?'bad':'good');hbnote.textContent=j.source==='stale'?'PKG-Zone não respondeu agora. Exibindo o último catálogo salvo no PS5.':'Escolha um pacote e o Astro fará a instalação dentro do PS5.';hg.innerHTML=a.map((x,i)=>`<article class='pkg'><div class='pkgimg' style=\"display:grid;place-items:center;font-size:34px\">${x.store?'★':'⬡'}</div><div class='pkgb'><h3>${E(x.name||x.id)}</h3><div class='meta'>${E(x.id)}${x.version?' · '+E(x.version):''}</div><div class='row'><span class='badge blue'>${E(x.type||'Homebrew')}</span><span class='badge run'>PS5</span></div><div class='row'><button class='btn primary sm' data-i='${i}'>Baixar e instalar</button>${x.detail?`<a class='btn ghost sm' target='_blank' rel='noopener' href='${E(x.detail)}'>Detalhes</a>`:''}</div></div></article>`).join('')||'<div class=\"notice\">Nenhum homebrew encontrado.</div>';hg.querySelectorAll('button[data-i]').forEach(b=>b.onclick=()=>installPkg(a[Number(b.dataset.i)],b))}catch(e){hs.textContent='Catálogo indisponível';hbnote.className='notice bad';hbnote.textContent='Não foi possível carregar o catálogo: '+e.message;hg.innerHTML=''}finally{refreshBtn.disabled=false}}loadHB();pollInstall()</script>";
  strncat(page,b,sizeof(page)-strlen(page)-1);v3_page_end(page,sizeof(page));send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}
