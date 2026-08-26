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

int astro_account_get_current(astro_account_state_t *out);
int astro_account_fake_activate_current(astro_account_state_t *out);

#endif
