#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "remote_pairing.h"

int sceRemoteplayInitialize(void *, size_t);
int sceRemoteplayGeneratePinCode(uint32_t *);
int sceRemoteplayConfirmDeviceRegist(int *, int *);
int sceRemoteplayNotifyPinCodeError(int);

#define PAIRING_TTL_SECONDS 300

/*
 * Astro no longer reads or writes the PS5 account registry.
 * Account activation and Remote Play preparation are external responsibilities
 * (OnionHEN / ActRemoteLink). Astro only asks the native Remote Play service
 * for a pairing PIN and monitors device registration.
 */
int astro_remote_pairing_prepare(astro_remote_pairing_state_t *out)
{
    astro_remote_pairing_state_t s;
    uint32_t pin=0;
    int rc,init_rc;

    memset(&s,0,sizeof(s));
    s.rc=-1;

    /* The service may already be initialized by OnionHEN. As in OnionHEN,
       a non-zero initialize result is diagnostic only; PIN generation is the
       authoritative test. */
    init_rc=sceRemoteplayInitialize(NULL,0);
    (void)init_rc;

    (void)sceRemoteplayNotifyPinCodeError(1);
    rc=sceRemoteplayGeneratePinCode(&pin);
    if(rc!=0||pin==0){
        s.rc=-21;
        if(out)*out=s;
        return s.rc;
    }

    s.pin=pin;
    s.pin_ready=1;
    s.pairing_active=1;
    s.generated_at=time(NULL);
    s.expires_at=s.generated_at+PAIRING_TTL_SECONDS;
    s.rc=0;
    if(out)*out=s;
    return 0;
}

int astro_remote_pairing_poll(astro_remote_pairing_state_t *state)
{
    int stat=0,err=0,rc;
    if(!state||!state->pairing_active)return -1;

    if(time(NULL)>state->expires_at){
        (void)sceRemoteplayNotifyPinCodeError(1);
        state->pairing_active=0;
        state->pin_ready=0;
        state->rc=-30;
        return -30;
    }

    rc=sceRemoteplayConfirmDeviceRegist(&stat,&err);
    if(rc!=0){state->rc=-31;return -31;}

    state->pair_stat=stat;
    state->pair_error=err;

    if(stat==2){
        state->pairing_complete=1;
        state->pairing_active=0;
        state->pin_ready=0;
        state->rc=0;
        return 1;
    }

    /* Status 3 is not terminal. Keep polling until status 2, native error,
       cancellation, or timeout. */
    state->rc=0;
    return 0;
}

int astro_remote_pairing_cancel(astro_remote_pairing_state_t *state)
{
    if(!state)return -1;
    if(state->pairing_active)(void)sceRemoteplayNotifyPinCodeError(1);
    state->pairing_active=0;
    state->pin_ready=0;
    state->rc=0;
    return 0;
}
