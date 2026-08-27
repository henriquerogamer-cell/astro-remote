#ifndef ASTRO_RP_PTRACE_H
#define ASTRO_RP_PTRACE_H

#include <stdint.h>

typedef struct astro_rp_tracer {
    uint64_t original_authid;
    uint8_t original_caps[16];
    uintptr_t libkernel_base;
    int pid;
} astro_rp_tracer_t;

int astro_rp_tracer_init(astro_rp_tracer_t *self, int pid);
int astro_rp_tracer_finalize(astro_rp_tracer_t *self);
uintptr_t astro_rp_tracer_call(astro_rp_tracer_t *self, uintptr_t addr,
    uintptr_t a, uintptr_t b, uintptr_t c,
    uintptr_t d, uintptr_t e, uintptr_t f);
int astro_rp_tracer_stack_scratch(astro_rp_tracer_t *self, size_t size,
    uintptr_t *addr_out);

#endif
