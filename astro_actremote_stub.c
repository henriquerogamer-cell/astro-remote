#include "astro_actremote.h"

int astro_actremote_fake_signin(int slot,
                                int user_id,
                                const char *user_name,
                                uint64_t account_id)
{
    (void)slot;
    (void)user_id;
    (void)user_name;
    (void)account_id;
    return 0;
}
