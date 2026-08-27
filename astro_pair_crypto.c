#include "astro_pair_crypto.h"
#include <string.h>

#define ROTR(x,n) (((x)>>(n))|((x)<<(32-(n))))
#define CH(x,y,z) (((x)&(y))^(~(x)&(z)))
#define MAJ(x,y,z) (((x)&(y))^((x)&(z))^((y)&(z)))
#define EP0(x) (ROTR((x),2)^ROTR((x),13)^ROTR((x),22))
#define EP1(x) (ROTR((x),6)^ROTR((x),11)^ROTR((x),25))
#define SIG0(x) (ROTR((x),7)^ROTR((x),18)^((x)>>3))
#define SIG1(x) (ROTR((x),17)^ROTR((x),19)^((x)>>10))

static const uint32_t k256[64]={
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

static void sha_transform(astro_sha256_ctx_t *ctx,const uint8_t data[64])
{
    uint32_t m[64],a,b,c,d,e,f,g,h,t1,t2;
    int i;
    for(i=0;i<16;i++)m[i]=((uint32_t)data[i*4]<<24)|((uint32_t)data[i*4+1]<<16)|((uint32_t)data[i*4+2]<<8)|data[i*4+3];
    for(;i<64;i++)m[i]=SIG1(m[i-2])+m[i-7]+SIG0(m[i-15])+m[i-16];
    a=ctx->state[0];b=ctx->state[1];c=ctx->state[2];d=ctx->state[3];e=ctx->state[4];f=ctx->state[5];g=ctx->state[6];h=ctx->state[7];
    for(i=0;i<64;i++){
        t1=h+EP1(e)+CH(e,f,g)+k256[i]+m[i];
        t2=EP0(a)+MAJ(a,b,c);
        h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    ctx->state[0]+=a;ctx->state[1]+=b;ctx->state[2]+=c;ctx->state[3]+=d;
    ctx->state[4]+=e;ctx->state[5]+=f;ctx->state[6]+=g;ctx->state[7]+=h;
}

void astro_sha256_init(astro_sha256_ctx_t *ctx)
{
    ctx->datalen=0;ctx->bitlen=0;
    ctx->state[0]=0x6a09e667;ctx->state[1]=0xbb67ae85;ctx->state[2]=0x3c6ef372;ctx->state[3]=0xa54ff53a;
    ctx->state[4]=0x510e527f;ctx->state[5]=0x9b05688c;ctx->state[6]=0x1f83d9ab;ctx->state[7]=0x5be0cd19;
}

void astro_sha256_update(astro_sha256_ctx_t *ctx,const uint8_t *data,size_t len)
{
    size_t i;
    for(i=0;i<len;i++){
        ctx->data[ctx->datalen++]=data[i];
        if(ctx->datalen==64){sha_transform(ctx,ctx->data);ctx->bitlen+=512;ctx->datalen=0;}
    }
}

void astro_sha256_final(astro_sha256_ctx_t *ctx,uint8_t hash[32])
{
    uint32_t i=ctx->datalen;
    int j;
    ctx->data[i++]=0x80;
    if(i>56){while(i<64)ctx->data[i++]=0;sha_transform(ctx,ctx->data);i=0;}
    while(i<56)ctx->data[i++]=0;
    ctx->bitlen+=(uint64_t)ctx->datalen*8;
    for(j=0;j<8;j++)ctx->data[63-j]=(uint8_t)(ctx->bitlen>>(8*j));
    sha_transform(ctx,ctx->data);
    for(i=0;i<4;i++)for(j=0;j<8;j++)hash[j*4+i]=(uint8_t)(ctx->state[j]>>(24-8*i));
}

void astro_hmac_sha256(const uint8_t *key,size_t key_len,const uint8_t *data,size_t len,uint8_t out[32])
{
    uint8_t key_block[64]={0},ipad[64],opad[64],inner[32];
    astro_sha256_ctx_t ctx;
    int i;
    if(key_len>64){astro_sha256_init(&ctx);astro_sha256_update(&ctx,key,key_len);astro_sha256_final(&ctx,key_block);key_len=32;}
    else memcpy(key_block,key,key_len);
    for(i=0;i<64;i++){ipad[i]=key_block[i]^0x36;opad[i]=key_block[i]^0x5c;}
    astro_sha256_init(&ctx);astro_sha256_update(&ctx,ipad,64);astro_sha256_update(&ctx,data,len);astro_sha256_final(&ctx,inner);
    astro_sha256_init(&ctx);astro_sha256_update(&ctx,opad,64);astro_sha256_update(&ctx,inner,32);astro_sha256_final(&ctx,out);
}

static const uint8_t sbox[256]={
0x63,0x7c,0x77,0x7b,0xf2,0x6b,0x6f,0xc5,0x30,0x01,0x67,0x2b,0xfe,0xd7,0xab,0x76,0xca,0x82,0xc9,0x7d,0xfa,0x59,0x47,0xf0,0xad,0xd4,0xa2,0xaf,0x9c,0xa4,0x72,0xc0,
0xb7,0xfd,0x93,0x26,0x36,0x3f,0xf7,0xcc,0x34,0xa5,0xe5,0xf1,0x71,0xd8,0x31,0x15,0x04,0xc7,0x23,0xc3,0x18,0x96,0x05,0x9a,0x07,0x12,0x80,0xe2,0xeb,0x27,0xb2,0x75,
0x09,0x83,0x2c,0x1a,0x1b,0x6e,0x5a,0xa0,0x52,0x3b,0xd6,0xb3,0x29,0xe3,0x2f,0x84,0x53,0xd1,0x00,0xed,0x20,0xfc,0xb1,0x5b,0x6a,0xcb,0xbe,0x39,0x4a,0x4c,0x58,0xcf,
0xd0,0xef,0xaa,0xfb,0x43,0x4d,0x33,0x85,0x45,0xf9,0x02,0x7f,0x50,0x3c,0x9f,0xa8,0x51,0xa3,0x40,0x8f,0x92,0x9d,0x38,0xf5,0xbc,0xb6,0xda,0x21,0x10,0xff,0xf3,0xd2,
0xcd,0x0c,0x13,0xec,0x5f,0x97,0x44,0x17,0xc4,0xa7,0x7e,0x3d,0x64,0x5d,0x19,0x73,0x60,0x81,0x4f,0xdc,0x22,0x2a,0x90,0x88,0x46,0xee,0xb8,0x14,0xde,0x5e,0x0b,0xdb,
0xe0,0x32,0x3a,0x0a,0x49,0x06,0x24,0x5c,0xc2,0xd3,0xac,0x62,0x91,0x95,0xe4,0x79,0xe7,0xc8,0x37,0x6d,0x8d,0xd5,0x4e,0xa9,0x6c,0x56,0xf4,0xea,0x65,0x7a,0xae,0x08,
0xba,0x78,0x25,0x2e,0x1c,0xa6,0xb4,0xc6,0xe8,0xdd,0x74,0x1f,0x4b,0xbd,0x8b,0x8a,0x70,0x3e,0xb5,0x66,0x48,0x03,0xf6,0x0e,0x61,0x35,0x57,0xb9,0x86,0xc1,0x1d,0x9e,
0xe1,0xf8,0x98,0x11,0x69,0xd9,0x8e,0x94,0x9b,0x1e,0x87,0xe9,0xce,0x55,0x28,0xdf,0x8c,0xa1,0x89,0x0d,0xbf,0xe6,0x42,0x68,0x41,0x99,0x2d,0x0f,0xb0,0x54,0xbb,0x16};
static const uint8_t rcon[11]={0,1,2,4,8,0x10,0x20,0x40,0x80,0x1b,0x36};
static uint8_t xtime(uint8_t x){return (uint8_t)((x<<1)^((x>>7)*0x1b));}

void astro_aes128_init(astro_aes128_ctx_t *ctx,const uint8_t key[16])
{
    uint8_t *rk=ctx->round_key,t[4];int bytes=16,rc=1,i;
    memcpy(rk,key,16);
    while(bytes<176){
        memcpy(t,rk+bytes-4,4);
        if((bytes&15)==0){uint8_t q=t[0];t[0]=sbox[t[1]]^rcon[rc++];t[1]=sbox[t[2]];t[2]=sbox[t[3]];t[3]=sbox[q];}
        for(i=0;i<4;i++){rk[bytes]=rk[bytes-16]^t[i];bytes++;}
    }
}
static void aes_add(uint8_t s[16],const uint8_t *rk){int i;for(i=0;i<16;i++)s[i]^=rk[i];}
static void aes_sub(uint8_t s[16]){int i;for(i=0;i<16;i++)s[i]=sbox[s[i]];}
static void aes_shift(uint8_t s[16])
{
    uint8_t t[16];memcpy(t,s,16);
    s[0]=t[0];s[1]=t[5];s[2]=t[10];s[3]=t[15];s[4]=t[4];s[5]=t[9];s[6]=t[14];s[7]=t[3];
    s[8]=t[8];s[9]=t[13];s[10]=t[2];s[11]=t[7];s[12]=t[12];s[13]=t[1];s[14]=t[6];s[15]=t[11];
}
static void aes_mix(uint8_t s[16])
{
    int c;
    for(c=0;c<4;c++){
        int i=c*4;uint8_t a=s[i],b=s[i+1],d=s[i+2],e=s[i+3],x=a^b^d^e;
        s[i]^=x^xtime(a^b);s[i+1]^=x^xtime(b^d);s[i+2]^=x^xtime(d^e);s[i+3]^=x^xtime(e^a);
    }
}
void astro_aes128_encrypt_block(const astro_aes128_ctx_t *ctx,const uint8_t in[16],uint8_t out[16])
{
    uint8_t s[16];int r;memcpy(s,in,16);aes_add(s,ctx->round_key);
    for(r=1;r<10;r++){aes_sub(s);aes_shift(s);aes_mix(s);aes_add(s,ctx->round_key+16*r);}
    aes_sub(s);aes_shift(s);aes_add(s,ctx->round_key+160);memcpy(out,s,16);
}
void astro_aes128_cfb128(const uint8_t key[16],uint8_t iv[16],const uint8_t *in,uint8_t *out,size_t len,int encrypt)
{
    astro_aes128_ctx_t ctx;uint8_t stream[16];size_t off=0;
    astro_aes128_init(&ctx,key);
    while(off<len){
        size_t i,n=len-off<16?len-off:16;
        astro_aes128_encrypt_block(&ctx,iv,stream);
        for(i=0;i<n;i++){uint8_t x=in[off+i]^stream[i];out[off+i]=x;iv[i]=encrypt?x:in[off+i];}
        off+=n;
    }
}
