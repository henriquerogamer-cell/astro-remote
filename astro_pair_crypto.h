#ifndef ASTRO_PAIR_CRYPTO_H
#define ASTRO_PAIR_CRYPTO_H

#include <stddef.h>
#include <stdint.h>

typedef struct astro_sha256_ctx {
    uint8_t data[64];
    uint32_t datalen;
    uint64_t bitlen;
    uint32_t state[8];
} astro_sha256_ctx_t;

void astro_sha256_init(astro_sha256_ctx_t *ctx);
void astro_sha256_update(astro_sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void astro_sha256_final(astro_sha256_ctx_t *ctx, uint8_t hash[32]);
void astro_hmac_sha256(const uint8_t *key, size_t key_len, const uint8_t *data, size_t len, uint8_t out[32]);

typedef struct astro_aes128_ctx {
    uint8_t round_key[176];
} astro_aes128_ctx_t;

void astro_aes128_init(astro_aes128_ctx_t *ctx, const uint8_t key[16]);
void astro_aes128_encrypt_block(const astro_aes128_ctx_t *ctx, const uint8_t in[16], uint8_t out[16]);
void astro_aes128_cfb128(const uint8_t key[16], uint8_t iv[16], const uint8_t *in, uint8_t *out, size_t len, int encrypt);

#endif
