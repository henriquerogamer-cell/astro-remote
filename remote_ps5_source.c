#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>

#include "remote_ps5_source.h"

/* Native PS5 libraries, linked by the payload toolchain. */
int sceRemoteplayInitialize(void *, size_t);
int sceRegMgrGetInt(long, int *);

/* REMOTEPLAY_rp_enable registry key used by the PS5 Remote Play service. */
#define ASTRO_REMOTEPLAY_ENABLE_KEY 1098973184L
#define ASTRO_REMOTEPLAY_SESSION_PORT 9295

static int g_remoteplay_init_attempted;
static int g_remoteplay_init_rc;

static int probe_loopback_tcp(int port)
{
  int fd;
  int rc;
  struct sockaddr_in a;

  fd=socket(AF_INET,SOCK_STREAM,0);
  if(fd<0)return 0;

  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_port=htons((uint16_t)port);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
  rc=connect(fd,(struct sockaddr*)&a,sizeof(a));
  close(fd);
  return rc==0;
}

int astro_remote_ps5_source_probe(astro_remote_ps5_source_probe_t *out)
{
  astro_remote_ps5_source_probe_t p;
  int enabled=0;

  memset(&p,0,sizeof(p));
  p.probed_at=time(NULL);

  if(!g_remoteplay_init_attempted){
    g_remoteplay_init_rc=sceRemoteplayInitialize(NULL,0);
    g_remoteplay_init_attempted=1;
  }

  p.init_rc=g_remoteplay_init_rc;
  p.remoteplay_initialized=(g_remoteplay_init_rc==0);
  p.reg_rc=sceRegMgrGetInt(ASTRO_REMOTEPLAY_ENABLE_KEY,&enabled);
  p.remoteplay_enabled=(p.reg_rc==0&&enabled==1);
  p.tcp_9295_open=probe_loopback_tcp(ASTRO_REMOTEPLAY_SESSION_PORT);

  if(out)*out=p;

  if(!p.remoteplay_initialized)return -10;
  if(p.reg_rc!=0)return -20;
  if(!p.remoteplay_enabled)return -30;
  if(!p.tcp_9295_open)return -40;
  return 0;
}
