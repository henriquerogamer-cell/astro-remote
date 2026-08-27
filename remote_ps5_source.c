#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>

#include "remote_ps5_source.h"

#define ASTRO_REMOTEPLAY_SESSION_PORT 9295
#define ASTRO_CONNECT_TIMEOUT_MS 250
#define ASTRO_PROTOCOL_IO_TIMEOUT_MS 700
#define ASTRO_REG_REMOTEPLAY_ENABLE 1098973184

int sceRegMgrGetInt(int key,int *value);
int sceRegMgrSetInt(int key,int value);
int sceRemoteplayInitialize(void *mem,size_t size);

static void set_fd_timeout(int fd,int timeout_ms)
{
  struct timeval tv;
  tv.tv_sec=timeout_ms/1000;
  tv.tv_usec=(timeout_ms%1000)*1000;
  setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
  setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&tv,sizeof(tv));
}

static int connect_loopback_tcp_timeout(int port,int timeout_ms)
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
  if(fd<0)return -1;

  flags=fcntl(fd,F_GETFL,0);
  if(flags<0){close(fd);return -1;}
  if(fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0){close(fd);return -1;}

  memset(&a,0,sizeof(a));
  a.sin_family=AF_INET;
  a.sin_port=htons((uint16_t)port);
  a.sin_addr.s_addr=htonl(INADDR_LOOPBACK);

  rc=connect(fd,(struct sockaddr*)&a,sizeof(a));
  if(rc!=0){
    if(errno!=EINPROGRESS){close(fd);return -1;}
    FD_ZERO(&wfds);FD_SET(fd,&wfds);
    tv.tv_sec=timeout_ms/1000;
    tv.tv_usec=(timeout_ms%1000)*1000;
    rc=select(fd+1,NULL,&wfds,NULL,&tv);
    if(rc<=0){close(fd);return -1;}
    if(getsockopt(fd,SOL_SOCKET,SO_ERROR,&soerr,&slen)<0||soerr!=0){close(fd);return -1;}
  }

  fcntl(fd,F_SETFL,flags);
  set_fd_timeout(fd,ASTRO_PROTOCOL_IO_TIMEOUT_MS);
  return fd;
}

static int probe_loopback_tcp_timeout(int port)
{
  int fd=connect_loopback_tcp_timeout(port,ASTRO_CONNECT_TIMEOUT_MS);
  if(fd<0)return 0;
  close(fd);
  return 1;
}

static int send_all(int fd,const char *buf,size_t len)
{
  size_t off=0;
  while(off<len){
    ssize_t n=send(fd,buf+off,len-off,0);
    if(n>0){off+=(size_t)n;continue;}
    if(n<0&&errno==EINTR)continue;
    return -1;
  }
  return 0;
}

static int header_value(const char *resp,const char *key,char *out,size_t outsz)
{
  size_t keylen=strlen(key);
  const char *p=resp;
  if(!out||outsz==0)return 0;
  out[0]='\0';

  while(p&&*p){
    const char *e=strstr(p,"\r\n");
    const char *v;
    size_t n;
    if(!e)e=p+strlen(p);
    if((size_t)(e-p)>keylen+1&&!strncasecmp(p,key,keylen)&&p[keylen]==':'){
      v=p+keylen+1;
      while(v<e&&(*v==' '||*v=='\t'))v++;
      n=(size_t)(e-v);
      if(n>=outsz)n=outsz-1;
      memcpy(out,v,n);
      out[n]='\0';
      return 1;
    }
    if(!*e)break;
    p=e+2;
  }
  return 0;
}

/*
 * Pairing needs the native Sony RP service enabled. This is intentionally
 * limited to the one global Remote Play enable key used by LinkDev. Astro
 * never scans or mutates account slots here; offline account activation
 * remains Astro Lock's job.
 */
int astro_remote_ps5_source_enable_remoteplay(void)
{
  int enabled=0;
  int rc=sceRegMgrGetInt(ASTRO_REG_REMOTEPLAY_ENABLE,&enabled);
  if(rc!=0)return -41;
  if(enabled!=1){
    rc=sceRegMgrSetInt(ASTRO_REG_REMOTEPLAY_ENABLE,1);
    if(rc!=0)return -42;
  }
  (void)sceRemoteplayInitialize(NULL,0);
  usleep(100000);
  return probe_loopback_tcp_timeout(ASTRO_REMOTEPLAY_SESSION_PORT)?0:-40;
}

int astro_remote_ps5_source_probe(astro_remote_ps5_source_probe_t *out)
{
  astro_remote_ps5_source_probe_t p;

  memset(&p,0,sizeof(p));
  p.probed_at=time(NULL);
  p.init_rc=1;
  p.remoteplay_initialized=0;
  p.reg_rc=0;
  p.tcp_9295_open=probe_loopback_tcp_timeout(ASTRO_REMOTEPLAY_SESSION_PORT);
  p.remoteplay_enabled=p.tcp_9295_open;

  if(out)*out=p;
  if(!p.tcp_9295_open)return -40;
  return 0;
}

int astro_remote_ps5_protocol_probe(astro_remote_ps5_protocol_probe_t *out)
{
  astro_remote_ps5_protocol_probe_t p;
  int fd;
  char resp[2048];
  size_t used=0;
  char reason[32];
  char nonce[128];
  const char *req=
    "GET /sie/ps5/rp/sess/init HTTP/1.1\r\n"
    "Host: 127.0.0.1:9295\r\n"
    "User-Agent: remoteplay Windows\r\n"
    "Connection: close\r\n"
    "Content-Length: 0\r\n"
    "RP-Registkey: 00000000000000000000000000000000\r\n"
    "Rp-Version: 1.0\r\n"
    "\r\n";

  memset(&p,0,sizeof(p));
  p.rc=-50;
  p.probed_at=time(NULL);

  fd=connect_loopback_tcp_timeout(ASTRO_REMOTEPLAY_SESSION_PORT,ASTRO_CONNECT_TIMEOUT_MS);
  if(fd<0){if(out)*out=p;return p.rc;}

  if(send_all(fd,req,strlen(req))!=0){
    close(fd);
    p.rc=-51;
    if(out)*out=p;
    return p.rc;
  }

  memset(resp,0,sizeof(resp));
  while(used+1<sizeof(resp)){
    ssize_t n=recv(fd,resp+used,sizeof(resp)-used-1,0);
    if(n>0){used+=(size_t)n;continue;}
    if(n==0)break;
    if(errno==EINTR)continue;
    if((errno==EAGAIN||errno==EWOULDBLOCK)&&used>0)break;
    close(fd);
    p.rc=-52;
    if(out)*out=p;
    return p.rc;
  }
  close(fd);

  if(!used){p.rc=-52;if(out)*out=p;return p.rc;}
  resp[used]='\0';

  if(sscanf(resp,"HTTP/%*s %d",&p.http_status)!=1){
    p.rc=-53;
    if(out)*out=p;
    return p.rc;
  }

  header_value(resp,"RP-Version",p.rp_version,sizeof(p.rp_version));
  reason[0]='\0';
  if(header_value(resp,"RP-Application-Reason",reason,sizeof(reason)))
    p.application_reason=(uint32_t)strtoul(reason,NULL,16);
  nonce[0]='\0';
  if(header_value(resp,"RP-Nonce",nonce,sizeof(nonce))&&nonce[0])p.nonce_present=1;

  p.rc=0;
  if(out)*out=p;
  return 0;
}
