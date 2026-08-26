#ifndef ASTRO_OFFACT_H
#define ASTRO_OFFACT_H

#include <stdint.h>

#define ASTRO_ACCOUNT_NAME_MAX 32
#define ASTRO_ACCOUNT_TYPE_MAX 17

typedef struct astro_account_state {
    int rc;
    int foreground_user;
    int registry_index;
    int account_flags;
    int activated;
    uint64_t account_id;
    uint64_t proposed_account_id;
    char account_name[ASTRO_ACCOUNT_NAME_MAX];
    char account_type[ASTRO_ACCOUNT_TYPE_MAX];
} astro_account_state_t;

void astro_account_set_debug_fd(int fd);
int astro_account_get_current(astro_account_state_t *out);
int astro_account_fake_activate_current(astro_account_state_t *out);

/* remote_worker.c includes remote_worker.h before this header. Keep the
   temporary debug helpers scoped to that translation unit so the PS5 SDK
   does not need libc dprintf(). */
#ifdef ASTRO_REMOTE_WORKER_H
#include <stdarg.h>
#include <stdio.h>
#include <unistd.h>
static void send_json(int fd,const char *status,const char *body);
static inline int astro_debug_dprintf(int fd,const char *fmt,...)
{
    char b[768];
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
#define dprintf astro_debug_dprintf
#endif

#endif
