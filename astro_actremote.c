/*
 * ActRemoteLink-derived fake NP sign-in bridge for Astro Remote.
 * Source logic adapted from francoataffarel/ActRemoteLink (GPL-3.0).
 * See THIRD_PARTY_NOTICES.md in this repository.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "astro_actremote.h"
#include "auth_dat.h"
#include "config_dat.h"

int sceSystemServiceParamGetInt(int, int *);
int sceRegMgrSetInt(int, int);
int sceRegMgrSetStr(int, const char *, size_t);

#define ACCOUNT_NUMB_MAX 16
#define SCE_REGMGR_ENT_KEY_REMOTEPLAY_rp_enable 1098973184

static int ent_num(int a,int b,int c,int d,int e)
{
    if(a<1||a>b)return e;
    return (a-1)*c+d;
}

static int key_user_rp_enable(int slot)
{
    return ent_num(slot,16,65536,125859841,127170561);
}

static void patch_str(unsigned char *buf,int offset,const char *str,int max_len)
{
    int len;
    memset(&buf[offset],0,(size_t)max_len);
    if(!str)return;
    len=(int)strlen(str);
    if(len>max_len-1)len=max_len-1;
    memcpy(&buf[offset],str,(size_t)len);
}

static int write_file_all(const char *path,const unsigned char *data,size_t len)
{
    int fd=open(path,O_WRONLY|O_CREAT|O_TRUNC,0644);
    ssize_t written;
    if(fd<0)return -1;
    written=write(fd,data,len);
    close(fd);
    return written==(ssize_t)len?0:-1;
}

static void write_registry_state(const unsigned char *cfg,uint32_t off)
{
    int32_t val;
    if(cfg[0x108]!=0)sceRegMgrSetStr(125830656+(int)off,(const char *)&cfg[0x108],65);
    sceRegMgrSetStr(125874188+(int)off,(const char *)&cfg[0x1AD],17);
    sceRegMgrSetStr(125874183+(int)off,(const char *)&cfg[0x177],17);
    sceRegMgrSetStr(125874190+(int)off,(const char *)&cfg[0x1BE],3);
    sceRegMgrSetStr(125874191+(int)off,(const char *)&cfg[0x1C1],6);
    sceRegMgrSetStr(125874192+(int)off,(const char *)&cfg[0x1C7],36);

    memcpy(&val,&cfg[0x48],4);sceRegMgrSetInt(125830144+(int)off,val);
    memcpy(&val,&cfg[0x4C],4);sceRegMgrSetInt(125831424+(int)off,val);
    memcpy(&val,&cfg[0x50],4);sceRegMgrSetInt(125831168+(int)off,val);
    memcpy(&val,&cfg[0x5C],4);sceRegMgrSetInt(125832960+(int)off,val);
    memcpy(&val,&cfg[0x1F4],4);sceRegMgrSetInt(125874194+(int)off,val);
    memcpy(&val,&cfg[0x1F8],4);sceRegMgrSetInt(125874185+(int)off,val);
    memcpy(&val,&cfg[0x1FC],4);sceRegMgrSetInt(125874186+(int)off,val);
    memcpy(&val,&cfg[0xA4],4);sceRegMgrSetInt(125830912+(int)off,val);
    memcpy(&val,&cfg[0xB4],4);sceRegMgrSetInt(125831936+(int)off,val);
    memcpy(&val,&cfg[0xD0],4);sceRegMgrSetInt(125832704+(int)off,val);
    memcpy(&val,&cfg[0xD4],4);sceRegMgrSetInt(125882625+(int)off,val);
    memcpy(&val,&cfg[0xDC],4);sceRegMgrSetInt(125854723+(int)off,val);
    memcpy(&val,&cfg[0xF4],4);sceRegMgrSetInt(125833216+(int)off,val);

    if(cfg[0x1100]!=0)sceRegMgrSetStr(125874189+(int)off,(const char *)&cfg[0x1100],65);
    if(cfg[0x1141]!=0)sceRegMgrSetStr(125874193+(int)off,(const char *)&cfg[0x1141],11);
    if(cfg[0x114C]!=0)sceRegMgrSetStr(125874195+(int)off,(const char *)&cfg[0x114C],65);
}

int astro_actremote_fake_signin(int slot,int user_id,const char *user_name,uint64_t account_id)
{
    unsigned char *cfg;
    const char *country="us",*lang="en",*locale="en-US";
    int32_t sys_lang=-1;
    char np_email[65],dir[256],path[256];
    uint32_t slot_off;
    int r_auth,r_config,r_rp_user,r_rp_global;

    if(slot<1||slot>ACCOUNT_NUMB_MAX)return -40;
    if(slot==1)return -41;
    if(user_id==0||!user_name||!user_name[0]||account_id==0)return -42;

    cfg=malloc(sizeof(config_dat));
    if(!cfg)return -43;
    memcpy(cfg,config_dat,sizeof(config_dat));

    if(sceSystemServiceParamGetInt(1,&sys_lang)==0){
        switch(sys_lang){
            case 0:country="jp";lang="ja";locale="ja-JP";break;
            case 2:country="fr";lang="fr";locale="fr-FR";break;
            case 3:country="es";lang="es";locale="es-ES";break;
            case 4:country="de";lang="de";locale="de-DE";break;
            case 5:country="it";lang="it";locale="it-IT";break;
            case 6:country="nl";lang="nl";locale="nl-NL";break;
            case 7:country="pt";lang="pt";locale="pt-PT";break;
            case 8:country="ru";lang="ru";locale="ru-RU";break;
            case 9:country="kr";lang="ko";locale="ko-KR";break;
            case 10:country="tw";lang="zh";locale="zh-TW";break;
            case 11:country="cn";lang="zh";locale="zh-CN";break;
            case 12:country="fi";lang="fi";locale="fi-FI";break;
            case 13:country="se";lang="sv";locale="sv-SE";break;
            case 14:country="dk";lang="da";locale="da-DK";break;
            case 15:country="no";lang="no";locale="no-NO";break;
            case 16:country="pl";lang="pl";locale="pl-PL";break;
            case 17:country="br";lang="pt";locale="pt-BR";break;
            case 18:country="gb";lang="en";locale="en-GB";break;
            case 19:country="tr";lang="tr";locale="tr-TR";break;
            case 20:country="mx";lang="es";locale="es-MX";break;
            case 21:country="sa";lang="ar";locale="ar-SA";break;
            case 22:country="ca";lang="fr";locale="fr-CA";break;
            case 23:country="cz";lang="cs";locale="cs-CZ";break;
            case 24:country="hu";lang="hu";locale="hu-HU";break;
            case 25:country="gr";lang="el";locale="el-GR";break;
            case 26:country="ro";lang="ro";locale="ro-RO";break;
            case 27:country="th";lang="th";locale="th-TH";break;
            case 28:country="vn";lang="vi";locale="vi-VN";break;
            case 29:country="id";lang="id";locale="id-ID";break;
            default:break;
        }
    }

    patch_str(cfg,0x1BE,country,3);
    patch_str(cfg,0x1C1,lang,6);
    patch_str(cfg,0x1C7,locale,36);
    patch_str(cfg,0x04,user_name,17);
    memcpy(&cfg[0x100],&account_id,8);
    patch_str(cfg,0x1AD,user_name,17);
    snprintf(np_email,sizeof(np_email),"%s@a8.%s.np.playstation.net",user_name,country);
    patch_str(cfg,0x108,np_email,65);
    patch_str(cfg,0x1100,np_email,65);

    snprintf(dir,sizeof(dir),"/system_data/priv/home/%x/np",user_id);
    mkdir(dir,0755);
    snprintf(path,sizeof(path),"/system_data/priv/home/%x/np/auth.dat",user_id);
    r_auth=write_file_all(path,auth_dat,sizeof(auth_dat));
    snprintf(path,sizeof(path),"/system_data/priv/home/%x/config.dat",user_id);
    r_config=write_file_all(path,cfg,sizeof(config_dat));

    slot_off=(uint32_t)(slot-1)*65536U;
    write_registry_state(cfg,0);
    if(slot>1)write_registry_state(cfg,slot_off);

    r_rp_user=sceRegMgrSetInt(key_user_rp_enable(slot),1);
    r_rp_global=sceRegMgrSetInt(SCE_REGMGR_ENT_KEY_REMOTEPLAY_rp_enable,1);
    free(cfg);

    if(r_auth!=0)return -44;
    if(r_config!=0)return -45;
    if(r_rp_user!=0)return -46;
    if(r_rp_global!=0)return -47;
    return 0;
}
