#ifndef ASTRO_REMOTE_PAIRING_H
#define ASTRO_REMOTE_PAIRING_H

#include <stdint.h>
#include <time.h>

typedef struct astro_remote_pairing_state {
    int rc;
    int pin_ready;
    int pairing_active;
    int pairing_complete;
    int pair_stat;
    int pair_error;
    int foreground_user;
    int registry_index;
    uint32_t pin;
    char account_id_b64[24];
    time_t generated_at;
    time_t expires_at;
} astro_remote_pairing_state_t;

/* LinkDev-style server-side pairing. This bypasses ShellUI/PSN checks and
 * calls libSceRemoteplay directly from astrorem. */
int astro_remote_pairing_prepare(astro_remote_pairing_state_t *out);
int astro_remote_pairing_poll(astro_remote_pairing_state_t *state);
int astro_remote_pairing_cancel(astro_remote_pairing_state_t *state);

#endif
