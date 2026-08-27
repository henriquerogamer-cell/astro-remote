#ifndef ASTRO_MBEDTLS_MD_COMPAT_H
#define ASTRO_MBEDTLS_MD_COMPAT_H
#include <stddef.h>
#include <stdint.h>
#include "../../../astro_pair_crypto.h"
typedef enum mbedtls_md_type_t { MBEDTLS_MD_NONE=0, MBEDTLS_MD_SHA256=6 } mbedtls_md_type_t;
typedef struct mbedtls_md_info_t { mbedtls_md_type_t type; } mbedtls_md_info_t;
typedef struct mbedtls_md_context_t { astro_sha256_ctx_t inner; uint8_t opad[64]; int active; } mbedtls_md_context_t;
void mbedtls_md_init(mbedtls_md_context_t *ctx);
void mbedtls_md_free(mbedtls_md_context_t *ctx);
const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t type);
int mbedtls_md_setup(mbedtls_md_context_t *ctx,const mbedtls_md_info_t *info,int hmac);
int mbedtls_md_hmac_starts(mbedtls_md_context_t *ctx,const unsigned char *key,size_t keylen);
int mbedtls_md_hmac_update(mbedtls_md_context_t *ctx,const unsigned char *input,size_t ilen);
int mbedtls_md_hmac_finish(mbedtls_md_context_t *ctx,unsigned char *output);
#endif
