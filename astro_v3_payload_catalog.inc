/* Astro V3 Payload Catalog.
 *
 * Adds a live, installable payload catalog to the existing Payloads page.
 * The community atlas supplies metadata only; payload bytes are downloaded
 * directly from the original upstream release URLs.
 */

#define ASTRO_CATALOG_DIR ASTRO_DIR "/catalog"
#define ASTRO_CATALOG_CACHE ASTRO_CATALOG_DIR "/payloads.json"
#define ASTRO_CATALOG_TMP ASTRO_CATALOG_DIR "/payloads.tmp"
#define ASTRO_CATALOG_INSTALLED ASTRO_CATALOG_DIR "/installed.tsv"
#define ASTRO_CATALOG_BACKUPS ASTRO_CATALOG_DIR "/backups"
#define ASTRO_CATALOG_URL "https://raw.githubusercontent.com/lucaszhongsj/ps5-payloads-atlas/main/payloads.json"
#define ASTRO_CATALOG_TTL (3 * 60 * 60)
#define ASTRO_CATALOG_MAX (2 * 1024 * 1024)
#define ASTRO_CATALOG_PAYLOAD_MAX (96 * 1024 * 1024)

static const char astro_catalog_fallback[] =
"["
"{\"name\":\"OnionHEN\",\"filename\":\"OnionHEN.elf\",\"url\":\"https://github.com/aydencharles/onionHEN/releases/download/v0.0.12/OnionHEN.elf\",\"description\":\"All-in-one HEN e toolbox para PS5.\",\"version\":\"v0.0.12\",\"category\":\"Kernel\",\"checksum\":\"a5c8dafe2af2681e8014421400c4ade4183a8d8be6dad6080dbd0930fa159e00\",\"source\":\"https://github.com/aydencharles/onionHEN\"},"
"{\"name\":\"kstuff-lite (EchoStretch)\",\"filename\":\"kstuff.elf\",\"url\":\"https://github.com/EchoStretch/kstuff-lite/releases/download/v1.10/kstuff.elf\",\"description\":\"Kernel patcher leve usado por vários payloads.\",\"version\":\"v1.10\",\"category\":\"Kernel\",\"checksum\":\"82cbb1e70f8a562317d6b65269cf6aa0d7d94e22df4f1ac89a260ca0ec261252\",\"source\":\"https://github.com/EchoStretch/kstuff-lite\"},"
"{\"name\":\"kstuff\",\"filename\":\"kstuff.elf\",\"url\":\"https://github.com/EchoStretch/kstuff/releases/download/v1.6.7/kstuff.elf\",\"description\":\"Build completa do kstuff.\",\"version\":\"v1.6.7\",\"category\":\"Kernel\",\"checksum\":\"f1c1f4b2b6395644af04cbe9828aba58586acf7aacb9e01113cac92ce16e3569\",\"source\":\"https://github.com/EchoStretch/kstuff\"},"
"{\"name\":\"ShadowMount+\",\"filename\":\"shadowmount.elf\",\"url\":\"\",\"description\":\"Montagem automática de conteúdo com kstuff-lite.\",\"version\":\"latest\",\"category\":\"Kernel\",\"checksum\":\"\",\"source\":\"https://github.com/drakmor/ShadowMountPlus\",\"source_only\":true},"
"{\"name\":\"nanoDNS\",\"filename\":\"nanodns.elf\",\"url\":\"\",\"description\":\"Servidor DNS local para o PS5.\",\"version\":\"latest\",\"category\":\"Network\",\"checksum\":\"\",\"source\":\"https://github.com/drakmor/nanoDNS\",\"source_only\":true},"
"{\"name\":\"CheatRunner\",\"filename\":\"CheatRunner.elf\",\"url\":\"https://github.com/notmaj0r/CheatRunner/releases/download/v0.17/CheatRunner.elf\",\"description\":\"Gerenciador de cheats do notmaj0r.\",\"version\":\"v0.17\",\"category\":\"Tools\",\"checksum\":\"2f296d3f1e69b65439bb5dc362f0f6504f6dc9f245fa16a3683e9d7ec9533773\",\"source\":\"https://github.com/notmaj0r/CheatRunner\"},"
"{\"name\":\"elfldr\",\"filename\":\"elfldr-ps5.elf\",\"url\":\"https://github.com/ps5-payload-dev/elfldr/releases/download/v0.26/elfldr-ps5.elf\",\"description\":\"ELF loader na porta 9021.\",\"version\":\"v0.26\",\"category\":\"Kernel\",\"checksum\":\"ed6d587a057c09d6a95b2176e336d9ffc605e547ac56462128b029e47b8ccea1\",\"source\":\"https://github.com/ps5-payload-dev/elfldr\"},"
"{\"name\":\"ftpsrv\",\"filename\":\"ftpsrv-ps5.elf\",\"url\":\"https://github.com/ps5-payload-dev/ftpsrv/releases/download/v0.21.1/ftpsrv-ps5.elf\",\"description\":\"Servidor FTP leve para PS5.\",\"version\":\"v0.21.1\",\"category\":\"Files\",\"checksum\":\"7d4b31c83eae4e056580482a3a074e1db25922efc74ac1e439473b45d71a5938\",\"source\":\"https://github.com/ps5-payload-dev/ftpsrv\"},"
"{\"name\":\"ps5debug-NG\",\"filename\":\"ps5debug.elf\",\"url\":\"\",\"description\":\"Debug/trainer service para PS5.\",\"version\":\"latest\",\"category\":\"Tools\",\"checksum\":\"\",\"source\":\"https://github.com/GoldHEN/ps5debug\",\"source_only\":true},"
"{\"name\":\"ps5-app-dumper\",\"filename\":\"ps5-app-dumper.elf\",\"url\":\"\",\"description\":\"Dumper de apps/jogos PS5.\",\"version\":\"latest\",\"category\":\"Tools\",\"checksum\":\"\",\"source\":\"https://github.com/EchoStretch/ps5-app-dumper\",\"source_only\":true},"
"{\"name\":\"PS5upload\",\"filename\":\"ps5upload.elf\",\"url\":\"\",\"description\":\"Servidor de upload e gerenciamento remoto.\",\"version\":\"latest\",\"category\":\"Network\",\"checksum\":\"\",\"source\":\"https://github.com/phantomptr/ps5upload\",\"source_only\":true},"
"{\"name\":\"WebKit Autoloader\",\"filename\":\"webkit-autoloader.elf\",\"url\":\"\",\"description\":\"Autoloader para o fluxo WebKit.\",\"version\":\"latest\",\"category\":\"Tools\",\"checksum\":\"\",\"source\":\"https://github.com/Gezine/PS5-Webkit-Execution\",\"source_only\":true}"
"]";

static const char astro_catalog_extras[] =
"{\"name\":\"etaHEN\",\"filename\":\"etaHEN.elf\",\"url\":\"https://github.com/etaHEN/etaHEN/releases/download/2.5B/etaHEN-2.5B.bin\",\"description\":\"HEN completo com toolbox, cheats, WebMAN e recursos de sistema.\",\"version\":\"2.5B\",\"category\":\"Kernel\",\"checksum\":\"4845cac45095361b8983b14d6690622183b0dcfbf6d7fda16e161aa91ff0531e\",\"source\":\"https://github.com/etaHEN/etaHEN\",\"astro_extra\":true},"
"{\"name\":\"Prospero Manager\",\"filename\":\"\",\"url\":\"\",\"description\":\"Gerenciador all-in-one do notmaj0r; mantido no catálogo como referência upstream.\",\"version\":\"source\",\"category\":\"Manager\",\"checksum\":\"\",\"source\":\"https://github.com/notmaj0r/ProsperoMgr\",\"source_only\":true,\"astro_extra\":true}";

typedef struct astro_sha256_ctx {
  uint32_t state[8];
  uint64_t bits;
  uint8_t data[64];
  size_t used;
} astro_sha256_ctx_t;

static uint32_t astro_rotr32(uint32_t x,unsigned n){return (x>>n)|(x<<(32-n));}
static void astro_sha256_block(astro_sha256_ctx_t *c,const uint8_t b[64])
{
  static const uint32_t k[64]={
    0x428a2f98U,0x71374491U,0xb5c0fbcfU,0xe9b5dba5U,0x3956c25bU,0x59f111f1U,0x923f82a4U,0xab1c5ed5U,
    0xd807aa98U,0x12835b01U,0x243185beU,0x550c7dc3U,0x72be5d74U,0x80deb1feU,0x9bdc06a7U,0xc19bf174U,
    0xe49b69c1U,0xefbe4786U,0x0fc19dc6U,0x240ca1ccU,0x2de92c6fU,0x4a7484aaU,0x5cb0a9dcU,0x76f988daU,
    0x983e5152U,0xa831c66dU,0xb00327c8U,0xbf597fc7U,0xc6e00bf3U,0xd5a79147U,0x06ca6351U,0x14292967U,
    0x27b70a85U,0x2e1b2138U,0x4d2c6dfcU,0x53380d13U,0x650a7354U,0x766a0abbU,0x81c2c92eU,0x92722c85U,
    0xa2bfe8a1U,0xa81a664bU,0xc24b8b70U,0xc76c51a3U,0xd192e819U,0xd6990624U,0xf40e3585U,0x106aa070U,
    0x19a4c116U,0x1e376c08U,0x2748774cU,0x34b0bcb5U,0x391c0cb3U,0x4ed8aa4aU,0x5b9cca4fU,0x682e6ff3U,
    0x748f82eeU,0x78a5636fU,0x84c87814U,0x8cc70208U,0x90befffaU,0xa4506cebU,0xbef9a3f7U,0xc67178f2U
  };
  uint32_t w[64];
  for(int i=0;i<16;i++)w[i]=((uint32_t)b[i*4]<<24)|((uint32_t)b[i*4+1]<<16)|((uint32_t)b[i*4+2]<<8)|b[i*4+3];
  for(int i=16;i<64;i++){uint32_t s0=astro_rotr32(w[i-15],7)^astro_rotr32(w[i-15],18)^(w[i-15]>>3);uint32_t s1=astro_rotr32(w[i-2],17)^astro_rotr32(w[i-2],19)^(w[i-2]>>10);w[i]=w[i-16]+s0+w[i-7]+s1;}
  uint32_t a=c->state[0],d=c->state[3],e=c->state[4],f=c->state[5],g=c->state[6],h=c->state[7],bb=c->state[1],cc=c->state[2];
  for(int i=0;i<64;i++){uint32_t S1=astro_rotr32(e,6)^astro_rotr32(e,11)^astro_rotr32(e,25);uint32_t ch=(e&f)^((~e)&g);uint32_t t1=h+S1+ch+k[i]+w[i];uint32_t S0=astro_rotr32(a,2)^astro_rotr32(a,13)^astro_rotr32(a,22);uint32_t maj=(a&bb)^(a&cc)^(bb&cc);uint32_t t2=S0+maj;h=g;g=f;f=e;e=d+t1;d=cc;cc=bb;bb=a;a=t1+t2;}
  c->state[0]+=a;c->state[1]+=bb;c->state[2]+=cc;c->state[3]+=d;c->state[4]+=e;c->state[5]+=f;c->state[6]+=g;c->state[7]+=h;
}
static void astro_sha256_init(astro_sha256_ctx_t *c)
{
  static const uint32_t iv[8]={0x6a09e667U,0xbb67ae85U,0x3c6ef372U,0xa54ff53aU,0x510e527fU,0x9b05688cU,0x1f83d9abU,0x5be0cd19U};
  memcpy(c->state,iv,sizeof(iv));c->bits=0;c->used=0;
}
static void astro_sha256_update(astro_sha256_ctx_t *c,const void *data,size_t n)
{
  const uint8_t *p=(const uint8_t*)data;c->bits+=(uint64_t)n*8U;
  while(n){size_t take=64-c->used;if(take>n)take=n;memcpy(c->data+c->used,p,take);c->used+=take;p+=take;n-=take;if(c->used==64){astro_sha256_block(c,c->data);c->used=0;}}
}
static void astro_sha256_final(astro_sha256_ctx_t *c,uint8_t out[32])
{
  c->data[c->used++]=0x80;
  if(c->used>56){while(c->used<64)c->data[c->used++]=0;astro_sha256_block(c,c->data);c->used=0;}
  while(c->used<56)c->data[c->used++]=0;
  for(int i=7;i>=0;i--)c->data[c->used++]=(uint8_t)(c->bits>>(i*8));
  astro_sha256_block(c,c->data);
  for(int i=0;i<8;i++){out[i*4]=(uint8_t)(c->state[i]>>24);out[i*4+1]=(uint8_t)(c->state[i]>>16);out[i*4+2]=(uint8_t)(c->state[i]>>8);out[i*4+3]=(uint8_t)c->state[i];}
}
static int astro_sha256_file(const char *path,char hex[65])
{
  int f=open(path,O_RDONLY);if(f<0)return -1;uint8_t *buf=malloc(256*1024);if(!buf){close(f);return -2;}astro_sha256_ctx_t c;astro_sha256_init(&c);int rc=0;
  for(;;){ssize_t n=read(f,buf,256*1024);if(n==0)break;if(n<0){rc=-3;break;}astro_sha256_update(&c,buf,(size_t)n);}
  close(f);free(buf);if(rc)return rc;uint8_t dig[32];astro_sha256_final(&c,dig);static const char hd[]="0123456789abcdef";for(int i=0;i<32;i++){hex[i*2]=hd[dig[i]>>4];hex[i*2+1]=hd[dig[i]&15];}hex[64]=0;return 0;
}

static size_t astro_catalog_write(void *ptr,size_t size,size_t nmemb,void *userdata)
{
  return fwrite(ptr,size,nmemb,(FILE*)userdata);
}
static int astro_catalog_allowed_url(const char *u)
{
  if(!u||strncmp(u,"https://",8))return 0;
  return !strncmp(u,"https://github.com/",19)||
         !strncmp(u,"https://raw.githubusercontent.com/",34)||
         !strncmp(u,"https://git.etawen.dev/",24);
}
static int astro_catalog_safe_filename(const char *s)
{
  size_t n=s?strlen(s):0;if(n<5||n>180||strcasecmp(s+n-4,".elf"))return 0;
  for(size_t i=0;i<n;i++)if(!(isalnum((unsigned char)s[i])||s[i]=='.'||s[i]=='_'||s[i]=='-'))return 0;
  return strstr(s,"..")==NULL;
}
static int astro_catalog_checksum_valid(const char *s)
{
  if(!s||!*s)return 1;if(strlen(s)!=64)return 0;for(int i=0;i<64;i++)if(!isxdigit((unsigned char)s[i]))return 0;return 1;
}
static int astro_catalog_fetch_to(const char *url,const char *tmp,size_t max_bytes,long timeout)
{
  if(!astro_catalog_allowed_url(url))return -1;
  FILE *f=fopen(tmp,"wb");if(!f)return -2;
  CURL *c=curl_easy_init();if(!c){fclose(f);unlink(tmp);return -3;}
  curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);
  curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,15L);curl_easy_setopt(c,CURLOPT_TIMEOUT,timeout);
  curl_easy_setopt(c,CURLOPT_USERAGENT,"Astro-Remote/3 PS5");curl_easy_setopt(c,CURLOPT_ACCEPT_ENCODING,"");
  curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,astro_catalog_write);curl_easy_setopt(c,CURLOPT_WRITEDATA,f);
  CURLcode cr=astro_hb_curl_perform(c);long code=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&code);curl_easy_cleanup(c);fflush(f);fsync(fileno(f));fclose(f);
  if(cr!=CURLE_OK||code<200||code>=300){unlink(tmp);return -4;}
  struct stat st;if(stat(tmp,&st)!=0||st.st_size<=0||(uint64_t)st.st_size>max_bytes){unlink(tmp);return -5;}return 0;
}
static int astro_catalog_refresh(int force,long long *age_out)
{
  v3_mkdirs(ASTRO_CATALOG_DIR);struct stat st;time_t now=time(NULL);int have=(stat(ASTRO_CATALOG_CACHE,&st)==0&&S_ISREG(st.st_mode));long long age=have?(long long)(now-st.st_mtime):0;if(age<0)age=0;if(age_out)*age_out=age;
  if(!force&&have&&age<ASTRO_CATALOG_TTL)return 0;
  unlink(ASTRO_CATALOG_TMP);int rc=astro_catalog_fetch_to(ASTRO_CATALOG_URL,ASTRO_CATALOG_TMP,ASTRO_CATALOG_MAX,45L);
  if(rc==0){int fd=open(ASTRO_CATALOG_TMP,O_RDONLY);char probe[4097]={0};ssize_t n=fd>=0?read(fd,probe,4096):-1;if(fd>=0)close(fd);if(n>0&&strstr(probe,"\"payloads\"")){unlink(ASTRO_CATALOG_CACHE);if(rename(ASTRO_CATALOG_TMP,ASTRO_CATALOG_CACHE)==0){if(age_out)*age_out=0;return 1;}}unlink(ASTRO_CATALOG_TMP);}
  return have?2:-1;
}
static char *astro_catalog_load(size_t *sz)
{
  *sz=0;struct stat st;if(stat(ASTRO_CATALOG_CACHE,&st)!=0||st.st_size<=0||st.st_size>ASTRO_CATALOG_MAX)return NULL;int f=open(ASTRO_CATALOG_CACHE,O_RDONLY);if(f<0)return NULL;
  char *b=malloc((size_t)st.st_size+1);if(!b){close(f);return NULL;}size_t o=0;while(o<(size_t)st.st_size){ssize_t n=read(f,b+o,(size_t)st.st_size-o);if(n<=0)break;o+=(size_t)n;}close(f);b[o]=0;*sz=o;return b;
}
static int astro_catalog_array(const char *json,const char **start,size_t *len)
{
  const char *p=strstr(json,"\"payloads\"");if(!p)return 0;p=strchr(p,'[');if(!p)return 0;const char *a=p;int depth=0,in=0,esc=0;
  for(;*p;p++){char ch=*p;if(in){if(esc){esc=0;continue;}if(ch=='\\'){esc=1;continue;}if(ch=='\"')in=0;continue;}if(ch=='\"'){in=1;continue;}if(ch=='[')depth++;else if(ch==']'){depth--;if(depth==0){*start=a;*len=(size_t)(p-a+1);return 1;}}}return 0;
}
static void astro_catalog_emit_installed(char *j,size_t cap,size_t *u)
{
  FILE *f=fopen(ASTRO_CATALOG_INSTALLED,"r");char l[2048];int first=1;
  while(f&&fgets(l,sizeof(l),f)&&*u+4096<cap){char *nl=strpbrk(l,"\r\n");if(nl)*nl=0;if(!*l)continue;char *a=l,*b=strchr(a,'\t');if(!b)continue;*b++=0;char *c=strchr(b,'\t');if(!c)continue;*c++=0;
    char ea[384],eb[256],ec[2048];json_escape(a,ea,sizeof(ea));json_escape(b,eb,sizeof(eb));json_escape(c,ec,sizeof(ec));*u+=snprintf(j+*u,cap-*u,"%s{\"file\":\"%s\",\"version\":\"%s\",\"source\":\"%s\"}",first?"":",",ea,eb,ec);first=0;}
  if(f)fclose(f);
}
static void astro_catalog_record(const char *file,const char *version,const char *source)
{
  v3_mkdirs(ASTRO_CATALOG_DIR);char tmp[512];snprintf(tmp,sizeof(tmp),"%s.tmp",ASTRO_CATALOG_INSTALLED);FILE *in=fopen(ASTRO_CATALOG_INSTALLED,"r"),*out=fopen(tmp,"w");if(!out){if(in)fclose(in);return;}
  char l[2048];while(in&&fgets(l,sizeof(l),in)){char copy[2048];snprintf(copy,sizeof(copy),"%s",l);char *t=strchr(copy,'\t');if(t)*t=0;if(strcmp(copy,file))fputs(l,out);}if(in)fclose(in);
  fprintf(out,"%s\t%s\t%s\n",file,version&&*version?version:"?",source&&*source?source:"?");fflush(out);fsync(fileno(out));fclose(out);rename(tmp,ASTRO_CATALOG_INSTALLED);
}
static void astro_catalog_install_api(int fd,const char *target)
{
  char url[2048]={0},file[256]={0},ver[128]={0},src[1536]={0},sum[96]={0},name[256]={0};
  query_value(target,"url",url,sizeof(url));query_value(target,"file",file,sizeof(file));query_value(target,"version",ver,sizeof(ver));query_value(target,"source",src,sizeof(src));query_value(target,"checksum",sum,sizeof(sum));query_value(target,"name",name,sizeof(name));
  if(!astro_catalog_allowed_url(url)||!astro_catalog_safe_filename(file)||!astro_catalog_checksum_valid(sum)){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"invalid_catalog_item\"}");return;}
  v3_mkdirs(ASTRO_PAYLOAD_DIR);v3_mkdirs(ASTRO_CATALOG_BACKUPS);char dst[512],tmp[512],bak[640];snprintf(dst,sizeof(dst),"%s/%s",ASTRO_PAYLOAD_DIR,file);snprintf(tmp,sizeof(tmp),"%s/.%s.download",ASTRO_PAYLOAD_DIR,file);snprintf(bak,sizeof(bak),"%s/%s.prev",ASTRO_CATALOG_BACKUPS,file);unlink(tmp);
  int rc=astro_catalog_fetch_to(url,tmp,ASTRO_CATALOG_PAYLOAD_MAX,300L);if(rc){char out[192];snprintf(out,sizeof(out),"{\"ok\":false,\"error\":\"download\",\"code\":%d}",rc);send_text(fd,"502 Bad Gateway","application/json",NULL,out);return;}
  if(sum[0]){char got[65];if(astro_sha256_file(tmp,got)!=0||strcasecmp(got,sum)){unlink(tmp);send_text(fd,"409 Conflict","application/json",NULL,"{\"ok\":false,\"error\":\"checksum_mismatch\"}");return;}}
  struct stat st;int had=stat(dst,&st)==0;if(had){unlink(bak);if(rename(dst,bak)!=0){unlink(tmp);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"backup\"}");return;}}
  if(rename(tmp,dst)!=0){if(had)rename(bak,dst);unlink(tmp);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false,\"error\":\"commit\"}");return;}chmod(dst,0755);astro_catalog_record(file,ver,src);
  char msg[384];snprintf(msg,sizeof(msg),"ASTRO: %s %s",name[0]?name:file,had?"atualizado":"instalado");notify(msg);send_text(fd,"200 OK","application/json",NULL,"{\"ok\":true}");
}
static void beta_sources_api(int fd)
{
  long long age=0;int state=astro_catalog_refresh(0,&age);size_t rawsz=0;char *raw=state>=0?astro_catalog_load(&rawsz):NULL;const char *arr=NULL;size_t arrlen=0;int remote=raw&&astro_catalog_array(raw,&arr,&arrlen);
  beta_seed_sources();FILE *f=fopen(ASTRO_SOURCE_FILE,"r");size_t cap=3*1024*1024,u=0;char *j=malloc(cap);if(!j){if(raw)free(raw);if(f)fclose(f);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  u+=snprintf(j+u,cap-u,"{\"ok\":true,\"items\":[");char l[2048];int first=1,idx=0;
  while(f&&fgets(l,sizeof(l),f)&&u+4096<cap){char *nl=strpbrk(l,"\r\n");if(nl)*nl=0;if(!*l)continue;char *a=l,*b=strchr(a,'\t');if(!b)continue;*b++=0;char *c=strchr(b,'\t');if(!c)continue;*c++=0;char ea[512],eb[2048],ec[512];json_escape(a,ea,sizeof(ea));json_escape(b,eb,sizeof(eb));json_escape(c,ec,sizeof(ec));u+=snprintf(j+u,cap-u,"%s{\"index\":%d,\"name\":\"%s\",\"url\":\"%s\",\"pattern\":\"%s\"}",first?"":",",idx++,ea,eb,ec);first=0;}if(f)fclose(f);
  u+=snprintf(j+u,cap-u,"],\"catalog\":[");
  if(remote&&arrlen>=2){size_t inner=arrlen-2;if(inner&&u+inner+4096<cap){memcpy(j+u,arr+1,inner);u+=inner;if(inner>0)j[u++]=',';}}
  else {size_t fl=strlen(astro_catalog_fallback);if(fl>=2&&u+fl+4096<cap){memcpy(j+u,astro_catalog_fallback+1,fl-2);u+=fl-2;if(fl>2)j[u++]=',';}}
  u+=snprintf(j+u,cap-u,"%s],\"catalog_source\":\"%s\",\"catalog_age\":%lld,\"installed\":[",astro_catalog_extras,remote?(state==2?"cache-stale":"atlas"):"fallback",age);
  astro_catalog_emit_installed(j,cap,&u);u+=snprintf(j+u,cap-u,"]}");send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);if(raw)free(raw);
}
static void beta_source_add_api(int fd,const char *target)
{
  char action[64]={0};query_value(target,"action",action,sizeof(action));
  if(!strcmp(action,"catalog-install")){astro_catalog_install_api(fd,target);return;}
  if(!strcmp(action,"catalog-refresh")){long long age=0;int rc=astro_catalog_refresh(1,&age);char j[160];snprintf(j,sizeof(j),"{\"ok\":%s,\"state\":%d,\"age\":%lld}",rc>=0?"true":"false",rc,age);send_text(fd,rc>=0?"200 OK":"502 Bad Gateway","application/json",NULL,j);return;}
  beta_source_add_api_legacy(fd,target);
}

static const char astro_catalog_css[] =
"<style>"
".catHead{display:flex;gap:10px;align-items:center;flex-wrap:wrap;margin-bottom:12px}.catHead .search{max-width:420px}.catStat{font-size:10px;color:#8ea0b6}.catGrid{display:grid;grid-template-columns:repeat(auto-fill,minmax(340px,1fr));gap:12px}.catCard{background:#101821;border:1px solid #29394c;border-radius:11px;padding:14px;display:flex;flex-direction:column;min-height:220px}.catCard.featured{border-color:#315b87}.catTop{display:flex;gap:10px;align-items:flex-start}.catIcon{width:43px;height:43px;border-radius:10px;border:1px solid #30587f;background:#12263c;display:grid;place-items:center;font-size:20px}.catInfo{min-width:0;flex:1}.catName{font-size:14px;font-weight:850}.catDev{font-size:9px;color:#6f87a1;margin-top:4px}.catDesc{font-size:10px;color:#90a1b5;line-height:1.55;margin:11px 0}.catTags{display:flex;gap:6px;flex-wrap:wrap}.catTag{padding:4px 7px;border-radius:999px;background:#243345;color:#a2b4c7;font-size:8px;font-weight:850}.catTag.ok{background:#123a2c;color:#63d8a8}.catTag.up{background:#3a2c16;color:#e3c36a}.catTag.featured{background:#163354;color:#82bcff}.catBtns{display:flex;gap:6px;flex-wrap:wrap;margin-top:auto;padding-top:12px}.catWarn{padding:10px 12px;border:1px solid #5e4c27;background:#231f13;color:#d8bf77;border-radius:9px;font-size:10px;line-height:1.5;margin-bottom:12px}@media(max-width:760px){.catGrid{grid-template-columns:1fr}}"
"</style>";

static const char astro_catalog_js[] =
"<script>"
"let CAT=[],CI={},catLoaded=false;const CP=['onionhen','etahen','kstuff-lite (echostretch)','shadowmount+','nanodns','cheatrunner','elfldr','pldmgr','pegasus-dl','ps5debug-ng','ps5-app-dumper','ftpsrv','ps5upload','webkit autoloader'];"
"const CE=s=>String(s??'').replace(/[&<>\\\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\\\"':'&quot;',\\\"'\\\":'&#39;'}[c]));"
"function catDev(x){try{let u=new URL(x.source),p=u.pathname.split('/').filter(Boolean);return p[0]||u.hostname}catch(e){return'upstream'}}"
"function catFile(x){let n=String(x.name||'').toLowerCase();let m={'etahen':'etaHEN.elf','onionhen':'onionhen.elf','kstuff':'kstuff-full.elf','kstuff-lite (echostretch)':'kstuff-lite.elf','kstuff-lite (drakmor — fpkg-optimized)':'kstuff-lite-drakmor.elf','shadowmount+':'shadowmount.elf','nanodns':'nanodns.elf','cheatrunner':'cheatrunner.elf','elfldr':'elfldr.elf','ftpsrv':'ftpsrv.elf','ftpsrv (drakmor)':'ftpsrv-drakmor.elf','pldmgr':'pldmgr.elf','pegasus-dl':'pegasus-dl.elf','ps5debug-ng':'ps5debug-ng.elf','ps5-app-dumper':'ps5-app-dumper.elf','ps5upload':'ps5upload.elf','webkit autoloader':'webkit-autoloader.elf'};if(m[n])return m[n];let s=n.normalize('NFD').replace(/[\\u0300-\\u036f]/g,'').replace(/[^a-z0-9]+/g,'-').replace(/^-|-$/g,'');return(s||'payload')+'.elf'}"
"function catLocal(x){let f=catFile(x);return P.find(p=>String(p.name).toLowerCase()===f.toLowerCase())}"
"function catInstalled(x){return CI[catFile(x).toLowerCase()]||null}"
"function catPriority(x){let n=String(x.name||'').toLowerCase(),i=CP.indexOf(n);return i<0?999:i}"
"function catStatus(x){let l=catLocal(x),i=catInstalled(x);if(!l)return['Não instalado',''];if(i&&x.version&&i.version===x.version)return['Atualizado','ok'];if(i&&x.version&&i.version!==x.version)return['Atualização disponível','up'];return['Instalado','ok']}"
"function renderCatalog(){sectionTitle.style.display='none';body.className='';body.innerHTML=`<div class='subtabs'><button onclick=\\\"T='all';render()\\\">Biblioteca</button><button class='on'>☄ Catálogo Astro</button><button onclick=\\\"tab('autoload')\\\">Autoloader</button><button onclick=\\\"tab('sources')\\\">Fontes</button></div><div class='catWarn'>Payloads de kernel/HEN podem ser alternativas entre si. Confira compatibilidade com seu firmware antes de executar. O Astro baixa os arquivos diretamente da fonte upstream e valida SHA-256 quando o catálogo fornece checksum.</div><div class='catHead'><input id='catQ' class='search' placeholder='Buscar OnionHEN, kstuff, FTP, cheats...' oninput='catPaint()'><select id='catCategory' onchange='catPaint()'><option value='all'>Todas as categorias</option></select><button onclick='catRefresh()'>↻ Atualizar catálogo</button><button id='catUpdateBtn' onclick='catUpdateAll()'>⇧ Atualizar instalados</button><span id='catStat' class='catStat'>Carregando...</span></div><div id='catGrid' class='catGrid'></div>`;loadCatalog(false)}"
"async function loadCatalog(force){try{let j=await fetch('/api/payload-sources'+(force?'?catalog=refresh':''),{cache:'no-store'}).then(r=>r.json());CAT=j.catalog||[];CI={};(j.installed||[]).forEach(x=>CI[String(x.file).toLowerCase()]=x);let cats=[...new Set(CAT.map(x=>x.category||'Other'))].sort();catCategory.innerHTML='<option value=\"all\">Todas as categorias</option>'+cats.map(x=>`<option value=\"${CE(x)}\">${CE(x)}</option>`).join('');catLoaded=true;catPaint(j.catalog_source,j.catalog_age)}catch(e){catStat.textContent='Falha ao carregar catálogo';catGrid.innerHTML=`<div class='notice bad'>${CE(e.message||e)}</div>`}}"
"function catPaint(src,age){if(!catLoaded)return;let q=(catQ.value||'').toLowerCase(),c=catCategory.value;let a=CAT.filter(x=>(c==='all'||(x.category||'Other')===c)&&(!q||String(x.name).toLowerCase().includes(q)||String(x.description||'').toLowerCase().includes(q)||String(x.source||'').toLowerCase().includes(q)));a.sort((x,y)=>catPriority(x)-catPriority(y)||String(x.name).localeCompare(String(y.name)));let updates=CAT.filter(x=>{let i=catInstalled(x);return i&&x.version&&i.version!==x.version&&x.url}).length;catUpdateBtn.disabled=!updates;catStat.textContent=`${a.length} itens · ${updates} atualização(ões)`+(src?` · ${src}`:'');catGrid.innerHTML=a.map(x=>{let n=String(x.name||''),feat=catPriority(x)<999,l=catLocal(x),i=catInstalled(x),st=catStatus(x),file=catFile(x),sourceOnly=x.source_only||!x.url;let canInstall=!sourceOnly&&x.url;let action=l?(i&&x.version&&i.version===x.version?'Reinstalar':'Atualizar'):'Instalar';return `<article class='catCard ${feat?'featured':''}'><div class='catTop'><div class='catIcon'>${feat?'✦':'ϟ'}</div><div class='catInfo'><div class='catName'>${CE(n)}</div><div class='catDev'>${CE(catDev(x))} · ${CE(file)}</div></div></div><div class='catTags'>${feat?`<span class='catTag featured'>ESSENCIAL</span>`:''}<span class='catTag'>${CE(x.category||'Other')}</span>${x.version?`<span class='catTag'>${CE(x.version)}</span>`:''}<span class='catTag ${st[1]}'>${st[0]}</span></div><div class='catDesc'>${CE(x.description||'Sem descrição.')}</div><div class='catBtns'><a class='btn ghost sm' target='_blank' href='${CE(x.source||'#')}'>Fonte</a>${canInstall?`<button class='primary sm' onclick='catInstall(${CAT.indexOf(x)})'>${action}</button>`:''}${l?`<button class='runBtn sm' onclick='launch(${JSON.stringify(l.path)})'>▷ Executar</button><button class='ghost sm' onclick='catAuto(${CAT.indexOf(x)})'>＋ Autoloader</button>`:''}</div></article>`}).join('')||`<div class='empty'>Nada encontrado.</div>`}"
"async function catInstall(i,quiet=false){let x=CAT[i],f=catFile(x);if(!x||!x.url)return false;if(!quiet)catStat.textContent='Baixando '+x.name+'...';let u='/api/payload-sources/add?action=catalog-install&name='+encodeURIComponent(x.name||'')+'&url='+encodeURIComponent(x.url)+'&file='+encodeURIComponent(f)+'&version='+encodeURIComponent(x.version||'?')+'&source='+encodeURIComponent(x.source||'')+'&checksum='+encodeURIComponent(x.checksum||'');let r=await fetch(u,{method:'POST'}),j=await r.json().catch(()=>({}));if(!r.ok){if(!quiet)alert('Falha: '+(j.error||r.status));return false}await loadAll(false);let s=await fetch('/api/payload-sources',{cache:'no-store'}).then(r=>r.json());CI={};(s.installed||[]).forEach(z=>CI[String(z.file).toLowerCase()]=z);if(!quiet)catPaint();return true}"
"async function catAuto(i){let x=CAT[i],f=catFile(x);if(!catLocal(x)){alert('Instale o payload primeiro.');return}await fetch('/api/autoload?action=add&name='+encodeURIComponent(f)+'&delay=3',{method:'POST'});alert(x.name+' adicionado ao Autoloader.')}"
"async function catRefresh(){catStat.textContent='Atualizando índice...';await fetch('/api/payload-sources/add?action=catalog-refresh',{method:'POST'});await loadCatalog(true)}"
"async function catUpdateAll(){let list=CAT.map((x,i)=>({x,i})).filter(o=>{let v=catInstalled(o.x);return v&&o.x.url&&o.x.version&&v.version!==o.x.version});if(!list.length)return;if(!confirm('Atualizar '+list.length+' payload(s) instalado(s)?'))return;for(let k=0;k<list.length;k++){catStat.textContent=`Atualizando ${k+1}/${list.length}: ${list[k].x.name}`;await catInstall(list[k].i,true)}await loadAll(false);await loadCatalog(false)}"
"const astroLegacyRender=render;render=function(){if(T==='catalog')return renderCatalog();return astroLegacyRender()};"
"</script>";

static void astro_catalog_send_text(int fd,const char *status,const char *ctype,const char *extra,const char *body)
{
  if(!body||!ctype||!strstr(ctype,"text/html")||!strstr(body,"<h1>Payloads</h1>")||!strstr(body,"payloadPanel")){send_text(fd,status,ctype,extra,body);return;}
  size_t bl=strlen(body),cl=strlen(astro_catalog_css),jl=strlen(astro_catalog_js),cap=bl+cl+jl+2048;char *out=malloc(cap);if(!out){send_text(fd,status,ctype,extra,body);return;}
  snprintf(out,cap,"%s",body);
  const char *needle="<input id='up' type='file' accept='.elf' hidden>";char *p=strstr(out,needle);
  if(p){size_t pos=(size_t)(p-out)+strlen(needle);const char *btn="<button onclick=\\\"tab('catalog')\\\">☄ Catálogo</button>";size_t add=strlen(btn);memmove(out+pos+add,out+pos,strlen(out+pos)+1);memcpy(out+pos,btn,add);}
  char *end=strstr(out,"</head>");if(end){size_t pos=(size_t)(end-out),add=cl;memmove(out+pos+add,out+pos,strlen(out+pos)+1);memcpy(out+pos,astro_catalog_css,add);}
  end=strstr(out,"</body>");if(end){size_t pos=(size_t)(end-out),add=jl;memmove(out+pos+add,out+pos,strlen(out+pos)+1);memcpy(out+pos,astro_catalog_js,add);}
  send_text(fd,status,ctype,extra,out);free(out);
}
