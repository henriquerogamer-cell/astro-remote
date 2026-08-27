#include <stddef.h>
#include "astro_avatar.h"

/*
 * Temporary crash-isolation stub.
 *
 * The Split #104 dashboard requests /avatar as soon as the account status
 * becomes valid. On consoles with a second local account this was the only
 * new code path that touched per-user avatar files directly from Astro Lock.
 * Keep the account/profile flow intact, but force Astro Lock to use its
 * generated SVG fallback until the DDS path is proven safe on-console.
 */
int astro_avatar_load_bmp(int user_id,unsigned char **bmp_out,size_t *bmp_len_out)
{
    (void)user_id;
    if(bmp_out)*bmp_out=NULL;
    if(bmp_len_out)*bmp_len_out=0;
    return -2;
}
