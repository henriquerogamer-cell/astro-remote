/* Native PS5 package installer for Astro.
 * Uses AppInstUtil directly. On recent firmware this is not equivalent to
 * Debug Settings: the system Package Installer can accept packages that the
 * public AppInstUtil/PlayGo route rejects. The install source is a PKG-Zone
 * URL selected from the authenticated Astro UI; arbitrary hosts and
 * paths are rejected before the system service is called.
 */

#define ASTRO_PKG_LANGUAGES 30
#define ASTRO_PKG_IDS 64

typedef char astro_pkg_language_t[8];
typedef char astro_pkg_scenario_t[3];
typedef char astro_pkg_content_id_t[0x30];

typedef struct {
  astro_pkg_content_id_t content_id;
  int content_type;
  int content_platform;
} astro_pkg_info_t;

typedef struct {
  const char *uri;
  const char *ex_uri;
  const char *playgo_scenario_id;
  const char *content_id;
  const char *content_name;
  const char *icon_url;
} astro_pkg_meta_t;

typedef struct {
  astro_pkg_language_t languages[ASTRO_PKG_LANGUAGES];
  astro_pkg_scenario_t playgo_scenario_ids[ASTRO_PKG_IDS];
  astro_pkg_content_id_t content_ids[ASTRO_PKG_IDS];
  unsigned char unknown[6480];
} astro_pkg_playgo_t;

typedef struct {
  int32_t error_code;
  int32_t version;
  char description[512];
  char type[9];
} astro_pkg_error_t;

typedef struct {
  char status[16];
  char src_type[8];
  uint32_t remain_time;
  uint64_t downloaded_size;
  uint64_t initial_chunk_size;
  uint64_t total_size;
  uint32_t promote_progress;
  astro_pkg_error_t error_info;
  int32_t local_copy_percent;
  unsigned char is_copy_only;
} astro_pkg_status_t;

int sceAppInstUtilInitialize(void);
int sceAppInstUtilInstallByPackage(astro_pkg_meta_t *,astro_pkg_info_t *,astro_pkg_playgo_t *);
int sceAppInstUtilGetInstallStatus(const char *,astro_pkg_status_t *);

static int astro_pkg_initialized=0;
static int astro_pkg_ready=0;
static char astro_pkg_content_id[0x30]={0};
static char astro_pkg_name[192]={0};

static int astro_pkg_safe_token(const char *s,size_t max)
{
  size_t n=s?strlen(s):0;if(!n||n>=max)return 0;
  for(size_t i=0;i<n;i++){unsigned char c=(unsigned char)s[i];if(!(isalnum(c)||c=='.'||c=='_'||c=='-'))return 0;}
  return 1;
}

static const char *astro_pkg_error_hint(int e)
{
  switch((uint32_t)e){
    case 0x80B21002:return "Espaço insuficiente no armazenamento.";
    case 0x80B21003:return "Parâmetros do pacote rejeitados.";
    case 0x80B21006:return "Tipo DRM do PKG não é compatível.";
    case 0x80B2100D:return "O aplicativo está aberto. Feche-o e tente novamente.";
    case 0x80B2100E:return "O PKG exige outra versão do sistema.";
    case 0x80B21013:return "Já existe uma versão mais nova instalada.";
    case 0x80B21018:return "O instalador já está ocupado.";
    case 0x80B2116F:return "Seu firmware bloqueou a instalação direta (AppInstUtil/PlayGo). No FW 12.60, use Configurações > Sistema > Debug Settings > Game > Package Installer; não é falha do download.";
    default:return "O instalador do PS5 recusou o pacote.";
  }
}

static void v3_pkg_install_api(int fd,const char *target)
{
  char id[64]={0},version[48]={0},name[192]={0},kind[24]="app";
  query_value(target,"id",id,sizeof(id));query_value(target,"version",version,sizeof(version));
  query_value(target,"name",name,sizeof(name));query_value(target,"kind",kind,sizeof(kind));
  char url[512];
  if(!strcmp(kind,"store")){
    snprintf(url,sizeof(url),"https://pkg-zone.com/update/Store-R2-PS5.pkg");
    snprintf(name,sizeof(name),"Homebrew Store");
  }else{
    if(!astro_pkg_safe_token(id,sizeof(id))||!astro_pkg_safe_token(version,sizeof(version))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"invalid_package\"}");return;}
    snprintf(url,sizeof(url),"https://pkg-zone.com/download/ps5/%s/%s",id,version);
    if(!name[0])snprintf(name,sizeof(name),"%s",id);
  }
  int rc=0;if(!astro_pkg_initialized){rc=sceAppInstUtilInitialize();if(!rc)astro_pkg_initialized=1;}
  if(rc){char j[256];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"init_failed\",\"code\":\"0x%08X\"}",(unsigned)rc);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}
  astro_pkg_meta_t meta={.uri=url,.ex_uri="",.playgo_scenario_id="",.content_id="",.content_name=name,.icon_url=""};
  astro_pkg_info_t info;astro_pkg_playgo_t playgo;memset(&info,0,sizeof(info));memset(&playgo,0,sizeof(playgo));
  rc=sceAppInstUtilInstallByPackage(&meta,&info,&playgo);
  if(rc){char he[256],j[640];json_escape(astro_pkg_error_hint(rc),he,sizeof(he));snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"install_rejected\",\"code\":\"0x%08X\",\"detail\":\"%s\"}",(unsigned)rc,he);send_text(fd,"409 Conflict","application/json; charset=utf-8",NULL,j);return;}
  snprintf(astro_pkg_content_id,sizeof(astro_pkg_content_id),"%s",info.content_id);snprintf(astro_pkg_name,sizeof(astro_pkg_name),"%s",name);astro_pkg_ready=1;
  char cid[128],nm[512],j[900];json_escape(astro_pkg_content_id,cid,sizeof(cid));json_escape(astro_pkg_name,nm,sizeof(nm));snprintf(j,sizeof(j),"{\"ok\":true,\"status\":\"queued\",\"content_id\":\"%s\",\"name\":\"%s\"}",cid,nm);send_text(fd,"202 Accepted","application/json; charset=utf-8",NULL,j);
}

static void v3_pkg_status_api(int fd)
{
  if(!astro_pkg_ready){send_text(fd,"200 OK","application/json",NULL,"{\"ok\":true,\"active\":false}");return;}
  astro_pkg_status_t st;memset(&st,0,sizeof(st));int rc=sceAppInstUtilGetInstallStatus(astro_pkg_content_id,&st);
  if(rc){char j[256];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"status_failed\",\"code\":\"0x%08X\"}",(unsigned)rc);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}
  unsigned pct=st.total_size?(unsigned)((st.downloaded_size*100)/st.total_size):st.promote_progress;if(pct>100)pct=100;
  char ss[64],nm[512],hint[256],j[1400];json_escape(st.status,ss,sizeof(ss));json_escape(astro_pkg_name,nm,sizeof(nm));json_escape(st.error_info.error_code?astro_pkg_error_hint(st.error_info.error_code):"",hint,sizeof(hint));
  snprintf(j,sizeof(j),"{\"ok\":true,\"active\":true,\"name\":\"%s\",\"status\":\"%s\",\"progress\":%u,\"downloaded\":%llu,\"total\":%llu,\"error_code\":\"0x%08X\",\"detail\":\"%s\"}",nm,ss,pct,(unsigned long long)st.downloaded_size,(unsigned long long)st.total_size,(unsigned)st.error_info.error_code,hint);send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);
}
