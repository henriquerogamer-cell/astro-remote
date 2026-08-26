#ifndef ASTRO_REMOTE_SERVICE_H
#define ASTRO_REMOTE_SERVICE_H

#include <time.h>

typedef struct astro_remote_state {
  int service_online;
  int session_active;
  int video_ready;
  int control_ready;
  int source_available;
  int remoteplay_initialized;
  int remoteplay_enabled;
  int remoteplay_tcp_9295;
  int source_probe_rc;
  unsigned long generation;
  time_t session_started_at;
  time_t last_change_at;
  time_t source_probed_at;
  char phase[48];
} astro_remote_state_t;

void astro_remote_service_init(void);
int astro_remote_service_start(void);
int astro_remote_service_stop(void);
void astro_remote_service_snapshot(astro_remote_state_t *out);

#endif
