#ifndef ASTRO_PAIR_CLIENT_H
#define ASTRO_PAIR_CLIENT_H

#include <stddef.h>
#include <stdint.h>

#define ASTRO_PAIR_ACCOUNT_FILE "/data/AstroRemote/remote_account_id.conf"
#define ASTRO_PAIR_CREDENTIAL_FILE "/data/AstroRemote/remote_pairing.conf"

typedef struct astro_pair_client_result {
    int rc;
    int http_status;
    uint32_t application_reason;
    uint32_t rp_key_type;
    char account_id[32];
    char regist_key_hex[65];
    char rp_key_hex[65];
    char ps5_mac_hex[32];
} astro_pair_client_result_t;

int astro_pair_account_save(const char *account_id);
int astro_pair_account_load(char *out,size_t out_size);
int astro_pair_credentials_exist(void);
int astro_pair_client_register_local(const char *account_id,uint32_t pin,astro_pair_client_result_t *out);

#endif
