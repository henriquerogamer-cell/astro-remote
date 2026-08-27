#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "astro_avatar.h"

static uint16_t rd16(const uint8_t *p){return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t rd32(const uint8_t *p){return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static void wr16(uint8_t *p,uint16_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void wr32(uint8_t *p,uint32_t v){p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);p[2]=(uint8_t)(v>>16);p[3]=(uint8_t)(v>>24);}

static void rgb565(uint16_t c,uint8_t *r,uint8_t *g,uint8_t *b)
{
    uint8_t rr=(uint8_t)((c>>11)&31),gg=(uint8_t)((c>>5)&63),bb=(uint8_t)(c&31);
    *r=(uint8_t)((rr<<3)|(rr>>2));
    *g=(uint8_t)((gg<<2)|(gg>>4));
    *b=(uint8_t)((bb<<3)|(bb>>2));
}

static int decode_dxt5(const uint8_t *dds,size_t dds_len,uint8_t **bmp_out,size_t *bmp_len_out)
{
    uint32_t width,height,bx_count,by_count;
    size_t need,pixel_bytes,bmp_len;
    uint8_t *rgba,*bmp,*dst;
    const uint8_t *src;

    if(!dds||dds_len<128||!bmp_out||!bmp_len_out)return -1;
    if(memcmp(dds,"DDS ",4)!=0)return -2;
    height=rd32(dds+12);width=rd32(dds+16);
    if(!width||!height||width>1024||height>1024)return -3;
    if(memcmp(dds+84,"DXT5",4)!=0)return -4;

    bx_count=(width+3)/4;by_count=(height+3)/4;
    need=128+(size_t)bx_count*by_count*16;
    if(dds_len<need)return -5;

    rgba=(uint8_t *)calloc((size_t)width*height,4);
    if(!rgba)return -6;
    src=dds+128;

    for(uint32_t by=0;by<by_count;by++){
        for(uint32_t bx=0;bx<bx_count;bx++,src+=16){
            uint8_t at[8],cr[4],cg[4],cb[4];
            uint64_t alpha_bits=0;
            uint32_t color_bits;
            uint16_t c0,c1;
            at[0]=src[0];at[1]=src[1];
            if(at[0]>at[1]){
                for(int i=1;i<=6;i++)at[i+1]=(uint8_t)(((7-i)*at[0]+i*at[1])/7);
            }else{
                for(int i=1;i<=4;i++)at[i+1]=(uint8_t)(((5-i)*at[0]+i*at[1])/5);
                at[6]=0;at[7]=255;
            }
            for(int i=0;i<6;i++)alpha_bits|=((uint64_t)src[2+i])<<(8*i);
            c0=rd16(src+8);c1=rd16(src+10);
            rgb565(c0,&cr[0],&cg[0],&cb[0]);rgb565(c1,&cr[1],&cg[1],&cb[1]);
            cr[2]=(uint8_t)((2*cr[0]+cr[1])/3);cg[2]=(uint8_t)((2*cg[0]+cg[1])/3);cb[2]=(uint8_t)((2*cb[0]+cb[1])/3);
            cr[3]=(uint8_t)((cr[0]+2*cr[1])/3);cg[3]=(uint8_t)((cg[0]+2*cg[1])/3);cb[3]=(uint8_t)((cb[0]+2*cb[1])/3);
            color_bits=rd32(src+12);

            for(int py=0;py<4;py++)for(int px=0;px<4;px++){
                uint32_t x=bx*4+(uint32_t)px,y=by*4+(uint32_t)py;
                int pi=py*4+px;
                uint8_t ci,ai,*p;
                if(x>=width||y>=height)continue;
                ci=(uint8_t)((color_bits>>(2*pi))&3);
                ai=(uint8_t)((alpha_bits>>(3*pi))&7);
                p=rgba+((size_t)y*width+x)*4;
                p[0]=cr[ci];p[1]=cg[ci];p[2]=cb[ci];p[3]=at[ai];
            }
        }
    }

    pixel_bytes=(size_t)width*height*4;
    bmp_len=54+pixel_bytes;
    bmp=(uint8_t *)malloc(bmp_len);
    if(!bmp){free(rgba);return -7;}
    memset(bmp,0,54);bmp[0]='B';bmp[1]='M';wr32(bmp+2,(uint32_t)bmp_len);wr32(bmp+10,54);wr32(bmp+14,40);wr32(bmp+18,width);wr32(bmp+22,height);wr16(bmp+26,1);wr16(bmp+28,32);wr32(bmp+34,(uint32_t)pixel_bytes);
    dst=bmp+54;
    for(int y=(int)height-1;y>=0;y--)for(uint32_t x=0;x<width;x++){
        const uint8_t *p=rgba+((size_t)y*width+x)*4;
        *dst++=p[2];*dst++=p[1];*dst++=p[0];*dst++=p[3];
    }
    free(rgba);*bmp_out=bmp;*bmp_len_out=bmp_len;return 0;
}

static FILE *open_avatar(int user_id,char path[256])
{
    static const char *files[]={"avatar64.dds","avatar128.dds"};
    for(size_t f=0;f<sizeof(files)/sizeof(files[0]);f++){
        snprintf(path,256,"/user/home/%x/avatar/%s",(unsigned int)user_id,files[f]);
        FILE *fp=fopen(path,"rb");if(fp)return fp;
        snprintf(path,256,"/user/home/%08x/avatar/%s",(unsigned int)user_id,files[f]);
        fp=fopen(path,"rb");if(fp)return fp;
    }
    return NULL;
}

int astro_avatar_load_bmp(int user_id,unsigned char **bmp_out,size_t *bmp_len_out)
{
    char path[256];FILE *f;long sz;uint8_t *dds;size_t got;int rc;
    if(!bmp_out||!bmp_len_out)return -1;*bmp_out=NULL;*bmp_len_out=0;
    f=open_avatar(user_id,path);if(!f)return -2;
    if(fseek(f,0,SEEK_END)!=0){fclose(f);return -3;}
    sz=ftell(f);if(sz<=0||sz>1024*1024){fclose(f);return -4;}
    rewind(f);dds=(uint8_t *)malloc((size_t)sz);if(!dds){fclose(f);return -5;}
    got=fread(dds,1,(size_t)sz,f);fclose(f);if(got!=(size_t)sz){free(dds);return -6;}
    rc=decode_dxt5(dds,got,(uint8_t **)bmp_out,bmp_len_out);free(dds);return rc;
}
