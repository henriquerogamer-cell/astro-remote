/* Persistent Astro web launcher for the PS5 Media home screen. */
#define ASTRO_LAUNCHER_TITLE_ID "ASTR04582"
#define ASTRO_LAUNCHER_AUTHID 0x4801000000000013ULL
#define ASTRO_INCASSET(name,file) __asm__(".section .rodata\n.global " #name "\n.global " #name "_end\n.global " #name "_size\n.align 16\n" #name ":\n.incbin \"" file "\"\n" #name "_end:\n" #name "_size:\n.quad " #name "_end - " #name "\n.previous\n");extern const uint8_t name[];extern const size_t name##_size
ASTRO_INCASSET(astro_launcher_icon,"assets/astro_launcher.png");
static const char astro_launcher_param[]="{\n\"applicationCategoryType\":65536,\n\"titleId\":\"" ASTRO_LAUNCHER_TITLE_ID "\",\n\"deeplinkUri\":\"http://127.0.0.1:45821/home\",\n\"localizedParameters\":{\"defaultLanguage\":\"pt-BR\",\"pt-BR\":{\"titleName\":\"Astro Remote\"},\"en-US\":{\"titleName\":\"Astro Remote\"}}\n}\n";
int sceAppInstUtilAppInstallAll(void *);
int sceAppInstUtilInitialize(void);
int sceAppInstUtilTerminate(void);
static int astro_launcher_write_missing(const char *path,const void *data,size_t size){struct stat st;if(!stat(path,&st))return 0;if(errno!=ENOENT)return -1;FILE *f=fopen(path,"wb");if(!f)return -1;int ok=fwrite(data,1,size,f)==size;fflush(f);fsync(fileno(f));fclose(f);return ok?0:-1;}
static int astro_launcher_register(void){int (*install_title_dir)(const char*,const char*,void*)=0;uint32_t handle;if(!kernel_dynlib_handle(-1,"libSceAppInstUtil.sprx",&handle))install_title_dir=(void*)kernel_dynlib_resolve(-1,handle,"Wudg3Xe3heE");if(install_title_dir)return install_title_dir(ASTRO_LAUNCHER_TITLE_ID,"/user/app/",NULL);return sceAppInstUtilAppInstallAll(NULL);}
static int astro_launcher_install_if_needed(void)
{
  char base[128],sys[160],param[192],icon[192];
  /* v3.0.104/106 used an invalid 5-letter + 4-digit title id. Remove only
   * that partial launcher so ShellUI cannot retain its ignored metadata. */
  unlink("/user/app/ASTRO4582/sce_sys/param.json");
  unlink("/user/app/ASTRO4582/sce_sys/icon0.png");
  rmdir("/user/app/ASTRO4582/sce_sys");
  rmdir("/user/app/ASTRO4582");
  snprintf(base,sizeof(base),"/user/app/%s",ASTRO_LAUNCHER_TITLE_ID);
  snprintf(sys,sizeof(sys),"%s/sce_sys",base);
  snprintf(param,sizeof(param),"%s/param.json",sys);
  snprintf(icon,sizeof(icon),"%s/icon0.png",sys);

  pid_t pid=getpid();
  uint64_t saved=kernel_get_ucred_authid(pid);
  if(saved&&kernel_set_ucred_authid(pid,ASTRO_LAUNCHER_AUTHID)!=0)return -10;

  int rc=sceAppInstUtilInitialize();
  if(!rc&&mkdir(base,0755)&&errno!=EEXIST)rc=-1;
  if(!rc&&mkdir(sys,0755)&&errno!=EEXIST)rc=-2;
  if(!rc&&astro_launcher_write_missing(param,astro_launcher_param,sizeof(astro_launcher_param)-1))rc=-3;
  if(!rc&&astro_launcher_write_missing(icon,astro_launcher_icon,astro_launcher_icon_size))rc=-4;
  if(!rc){chmod(param,0644);chmod(icon,0644);rc=astro_launcher_register();}

  sceAppInstUtilTerminate();
  if(saved){for(int i=0;i<3;i++){kernel_set_ucred_authid(pid,saved);if(kernel_get_ucred_authid(pid)==saved)break;}}
  return rc;
}
