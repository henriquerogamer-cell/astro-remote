/*
 * Astro Lock OffAct core
 *
 * Adapted from OffAct by John Törnblom (2024), licensed under GPL-3.0-or-later.
 * Upstream: https://github.com/ps5-payload-dev/offact
 */
#include <string.h>

#include "astrolock_offact.h"

int sceRegMgrGetInt(int, int *);
int sceRegMgrGetStr(int, char *, size_t);
int sceRegMgrGetBin(int, void *, size_t);
int sceRegMgrSetInt(int, int);
int sceRegMgrSetBin(int, const void *, size_t);
int sceRegMgrSetStr(int, const char *, size_t);

static int entity_number(int a, int b, int c, int d, int e)
{
    if(a < 1 || a > b)return e;
    return (a - 1) * c + d;
}

uint64_t astro_offact_generate_id(const char *name)
{
    uint64_t base = 0x5EAF00D / 0xCA7F00D;
    if(name && *name){
        do {
            base = 0x100000001B3ULL * (base ^ (unsigned char)*name++);
        } while(*name);
    }
    return base;
}

int astro_offact_get_name(int slot, char value[ASTRO_OFFACT_NAME_MAX])
{
    int key = entity_number(slot, 16, 65536, 125829632, 127140352);
    if(!value)return -1;
    value[0] = '\0';
    return sceRegMgrGetStr(key, value, ASTRO_OFFACT_NAME_MAX);
}

int astro_offact_get_id(int slot, uint64_t *value)
{
    int key = entity_number(slot, 16, 65536, 125830400, 127141120);
    if(!value)return -1;
    *value = 0;
    return sceRegMgrGetBin(key, value, sizeof(*value));
}

int astro_offact_set_id(int slot, uint64_t value)
{
    int key = entity_number(slot, 16, 65536, 125830400, 127141120);
    return sceRegMgrSetBin(key, &value, sizeof(value));
}

int astro_offact_get_type(int slot, char value[ASTRO_OFFACT_TYPE_MAX])
{
    int key = entity_number(slot, 16, 65536, 125874183, 127184903);
    if(!value)return -1;
    value[0] = '\0';
    return sceRegMgrGetStr(key, value, ASTRO_OFFACT_TYPE_MAX);
}

int astro_offact_set_type(int slot, const char value[ASTRO_OFFACT_TYPE_MAX])
{
    int key = entity_number(slot, 16, 65536, 125874183, 127184903);
    if(!value)return -1;
    return sceRegMgrSetStr(key, value, ASTRO_OFFACT_TYPE_MAX);
}

int astro_offact_get_flags(int slot, int *value)
{
    int key = entity_number(slot, 16, 65536, 125831168, 127141888);
    if(!value)return -1;
    *value = 0;
    return sceRegMgrGetInt(key, value);
}

int astro_offact_set_flags(int slot, int value)
{
    int key = entity_number(slot, 16, 65536, 125831168, 127141888);
    return sceRegMgrSetInt(key, value);
}
