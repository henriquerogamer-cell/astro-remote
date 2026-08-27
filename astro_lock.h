#ifndef ASTRO_LOCK_H
#define ASTRO_LOCK_H

#define ASTRO_LOCK_PORT 45823

int astro_lock_main(void);

#ifdef ASTRO_LOCK_LOCAL_DPRINTF
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
static inline int astro_lock_dprintf(int fd,const char *fmt,...)
{
    char b[1024];
    va_list ap;
    int n;
    va_start(ap,fmt);
    n=vsnprintf(b,sizeof(b),fmt,ap);
    va_end(ap);
    if(n<0)return n;
    if((size_t)n>=sizeof(b))n=(int)sizeof(b)-1;
    if(n>0)write(fd,b,(size_t)n);
    return n;
}
#define dprintf astro_lock_dprintf
#endif

#endif
