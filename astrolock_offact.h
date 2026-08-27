/*
 * Astro Lock OffAct core
 *
 * Adapted from OffAct by John Törnblom (2024), licensed under GPL-3.0-or-later.
 * Upstream: https://github.com/ps5-payload-dev/offact
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#define ASTRO_OFFACT_ACCOUNT_MAX 16
#define ASTRO_OFFACT_TYPE_MAX 17
#define ASTRO_OFFACT_NAME_MAX 32

int astro_offact_get_name(int slot, char value[ASTRO_OFFACT_NAME_MAX]);
int astro_offact_get_id(int slot, uint64_t *value);
int astro_offact_set_id(int slot, uint64_t value);
uint64_t astro_offact_generate_id(const char *name);
int astro_offact_get_type(int slot, char value[ASTRO_OFFACT_TYPE_MAX]);
int astro_offact_set_type(int slot, const char value[ASTRO_OFFACT_TYPE_MAX]);
int astro_offact_get_flags(int slot, int *value);
int astro_offact_set_flags(int slot, int value);
