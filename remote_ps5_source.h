#ifndef ASTRO_REMOTE_PS5_SOURCE_H
#define ASTRO_REMOTE_PS5_SOURCE_H

#include <time.h>
#include <stdint.h>

typedef struct astro_remote_ps5_source_probe {
  int init_rc;
  int reg_rc;
  int remoteplay_initialized;
  int remoteplay_enabled;
  int tcp_9295_open;
  time_t probed_at;
} astro_remote_ps5_source_probe_t;

typedef struct astro_remote_ps5_protocol_probe {
  int rc;
  int http_status;
  int nonce_present;
  uint32_t application_reason;
  char rp_version[16];
  time_t probed_at;
} astro_remote_ps5_protocol_probe_t;

int astro_remote_ps5_source_probe(astro_remote_ps5_source_probe_t *out);
int astro_remote_ps5_source_enable_remoteplay(void);
int astro_remote_ps5_protocol_probe(astro_remote_ps5_protocol_probe_t *out);

#endif
