#ifndef ASTRO_REMOTE_PAIRING_H
#define ASTRO_REMOTE_PAIRING_H

#include <stdint.h>
#include <time.h>

typedef struct astro_remote_pairing_state {
    int rc;
    int pin_ready;
    int foreground_user;
    int registry_index;
    uint32_t pin;
    char account_id_b64[24];
    time_t generated_at;
    time_t expires_at;
} astro_remote_pairing_state_t;

/* Manual one-time pairing flow. The PIN is entered by the user from
 * Settings > System > Remote Play > Link Device on the PS5. */
int astro_remote_pairing_submit_pin(uint32_t pin, astro_remote_pairing_state_t *out);

#endif
