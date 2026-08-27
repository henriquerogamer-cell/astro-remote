#include <string.h>
#include "third_party/chiaki_compat/mbedtls/aes.h"
#include "third_party/chiaki_compat/mbedtls/md.h"

void mbedtls_aes_init(mbedtls_aes_context *ctx){memset(ctx,0,sizeof(*ctx));}
void mbedtls_aes_free(mbedtls_aes_context *ctx){memset(ctx,0,sizeof(*ctx));}
int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx,const unsigned char *key,unsigned int keybits)
{
    if(!ctx||!key||keybits!=128)return -1;
    memcpy(ctx->key,key,16);
    astro_aes128_init(&ctx->aes,key);
    return 0;
}
int mbedtls_aes_crypt_cfb128(mbedtls_aes_context *ctx,int mode,size_t length,size_t *iv_off,unsigned char iv[16],const unsigned char *input,unsigned char *output)
{
    size_t n;
    unsigned char stream[16];
    if(!ctx||!iv_off||!iv||(!input&&length)||(!output&&length))return -1;
    n=*iv_off;
    if(n>=16)return -1;
    while(length--){
        unsigned char c;
        if(n==0){astro_aes128_encrypt_block(&ctx->aes,iv,stream);memcpy(iv,stream,16);}
        c=*input++;
        if(mode==MBEDTLS_AES_ENCRYPT){c=(unsigned char)(c^iv[n]);*output++=c;iv[n]=c;}
        else{unsigned char x=(unsigned char)(c^iv[n]);*output++=x;iv[n]=c;}
        n=(n+1)&0x0f;
    }
    *iv_off=n;
    return 0;
}

static const mbedtls_md_info_t g_sha256_info={MBEDTLS_MD_SHA256};
void mbedtls_md_init(mbedtls_md_context_t *ctx){memset(ctx,0,sizeof(*ctx));}
void mbedtls_md_free(mbedtls_md_context_t *ctx){memset(ctx,0,sizeof(*ctx));}
const mbedtls_md_info_t *mbedtls_md_info_from_type(mbedtls_md_type_t type){return type==MBEDTLS_MD_SHA256?&g_sha256_info:0;}
int mbedtls_md_setup(mbedtls_md_context_t *ctx,const mbedtls_md_info_t *info,int hmac){if(!ctx||!info||info->type!=MBEDTLS_MD_SHA256||!hmac)return -1;ctx->active=0;return 0;}
int mbedtls_md_hmac_starts(mbedtls_md_context_t *ctx,const unsigned char *key,size_t keylen)
{
    unsigned char kb[64]={0},ipad[64];
    astro_sha256_ctx_t tmp;
    int i;
    if(!ctx||!key)return -1;
    if(keylen>64){unsigned char digest[32];astro_sha256_init(&tmp);astro_sha256_update(&tmp,key,keylen);astro_sha256_final(&tmp,digest);memcpy(kb,digest,32);}
    else memcpy(kb,key,keylen);
    for(i=0;i<64;i++){ipad[i]=(unsigned char)(kb[i]^0x36);ctx->opad[i]=(unsigned char)(kb[i]^0x5c);}
    astro_sha256_init(&ctx->inner);astro_sha256_update(&ctx->inner,ipad,64);ctx->active=1;return 0;
}
int mbedtls_md_hmac_update(mbedtls_md_context_t *ctx,const unsigned char *input,size_t ilen){if(!ctx||!ctx->active)return -1;astro_sha256_update(&ctx->inner,input,ilen);return 0;}
int mbedtls_md_hmac_finish(mbedtls_md_context_t *ctx,unsigned char *output)
{
    unsigned char inner[32];astro_sha256_ctx_t outer;
    if(!ctx||!ctx->active||!output)return -1;
    astro_sha256_final(&ctx->inner,inner);astro_sha256_init(&outer);astro_sha256_update(&outer,ctx->opad,64);astro_sha256_update(&outer,inner,32);astro_sha256_final(&outer,output);ctx->active=0;return 0;
}
