#ifndef ASTRO_MBEDTLS_AES_COMPAT_H
#define ASTRO_MBEDTLS_AES_COMPAT_H
#include <stddef.h>
#include <stdint.h>
#include "../../../astro_pair_crypto.h"
#define MBEDTLS_AES_ENCRYPT 1
#define MBEDTLS_AES_DECRYPT 0
typedef struct mbedtls_aes_context { astro_aes128_ctx_t aes; uint8_t key[16]; } mbedtls_aes_context;
void mbedtls_aes_init(mbedtls_aes_context *ctx);
void mbedtls_aes_free(mbedtls_aes_context *ctx);
int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx,const unsigned char *key,unsigned int keybits);
int mbedtls_aes_crypt_cfb128(mbedtls_aes_context *ctx,int mode,size_t length,size_t *iv_off,unsigned char iv[16],const unsigned char *input,unsigned char *output);
#endif
