#include <stddef.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#include "remote_ps5_source.h"

int sceRegMgrGetInt(long, int *);
int sceRegMgrSetInt(long, int);

#define ASTRO_REMOTEPLAY_ENABLE_KEY 1098973184L
#define ASTRO_REMOTEPLAY_SESSION_PORT 9295
#define ASTRO_CONNECT_TIMEOUT_MS 250

static int probe_loopback_tcp_timeout(int port)
{
  int fd;
  int flags;
  int rc;
  int soerr=0;
  socklen_t slen=sizeof(soerr);
  struct sockaddr_in a;
  fd_set wfds;
  struct timeval tv;

  fd=socket(AF_INET,SOCK_STREAM,0);
  if(fd<0)return 0;

  flags=fcntl(fd,F_GETFL,0);
  if(flags<0){close(fd);return 0;}
  if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);return 0;}

  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_port=htons((uint16_t)port);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);

  rc=connect(fd,(struct sockaddr*)&a,sizeof(a));
  if(rc==0){close(fd);return 1;}
  if(errno!=EINPROGRESS){close(fd);return 0;}

  FD_ZERO(&wfds);
  FD_SET(fd,&wfds);
  tv.tv_sec=ASTRO_CONNECT_TIMEOUT_MS/1000;
  tv.tv_usec=(ASTRO_CONNECT_TIMEOUT_MS%1000)*1000;
  rc=select(fd+1,NULL,&wfds,NULL,&tv);
  if(rc<=0){close(fd);return 0;}
  if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&soerr,&slen)<0){close(fd);return 0;}

  close(fd);
  return soerr==0;
}

int astro_remote_ps5_source_enable_remoteplay(void)
{
  int value=0;
  int verify=0;
  int rc=sceRegMgrGetInt(ASTRO_REMOTEPLAY_ENABLE_KEY,&value);
  if(rc!=0)return -20;

  if(value!=1){
    rc=sceRegMgrSetInt(ASTRO_REMOTEPLAY_ENABLE_KEY,1);
    if(rc!=0)return -21;
  }

  rc=sceRegMgrGetInt(ASTRO_REMOTEPLAY_ENABLE_KEY,&verify);
  if(rc!=0)return -22;
  return verify==1?0:-23;
}

int astro_remote_ps5_source_probe(astro_remote_ps5_source_probe_t *out)
{
  astro_remote_ps5_source_probe_t p;
  int enabled=0;

  memset(&p,0,sizeof(p));
  p.probed_at=time(NULL);

  /* sceRemoteplayInitialize is intentionally not called here. Public PS5
     payload research reports that it can block forever outside bigapp/
     ShellUI context. Astro only performs passive registry/socket probes. */
  p.init_rc=1;
  p.remoteplay_initialized=0;
  p.reg_rc=sceRegMgrGetInt(ASTRO_REMOTEPLAY_ENABLE_KEY,&enabled);
  p.remoteplay_enabled=(p.reg_rc==0&&enabled==1);
  p.tcp_9295_open=probe_loopback_tcp_timeout(ASTRO_REMOTEPLAY_SESSION_PORT);

  if(out)*out=p;

  if(p.reg_rc!=0)return -20;
  if(!p.remoteplay_enabled)return -30;
  if(!p.tcp_9295_open)return -40;
  return 0;
}
