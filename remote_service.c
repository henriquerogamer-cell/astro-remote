#include <stdio.h>
#include <string.h>
#include "remote_service.h"

static astro_remote_state_t g_remote;

static void set_phase(const char *phase)
{
  if(!phase)phase="unknown";
  snprintf(g_remote.phase,sizeof(g_remote.phase),"%s",phase);
}

void astro_remote_service_init(void)
{
  memset(&g_remote,0,sizeof(g_remote));
  g_remote.service_online=1;
  g_remote.last_change_at=time(NULL);
  set_phase("idle");
}

int astro_remote_service_start(void)
{
  if(!g_remote.service_online)return -1;
  if(g_remote.session_active)return 0;
  g_remote.session_active=1;
  g_remote.video_ready=0;
  g_remote.control_ready=0;
  g_remote.generation++;
  g_remote.session_started_at=time(NULL);
  g_remote.last_change_at=g_remote.session_started_at;
  set_phase("waiting_video_source");
  return 1;
}

int astro_remote_service_stop(void)
{
  if(!g_remote.service_online)return -1;
  if(!g_remote.session_active)return 0;
  g_remote.session_active=0;
  g_remote.video_ready=0;
  g_remote.control_ready=0;
  g_remote.session_started_at=0;
  g_remote.last_change_at=time(NULL);
  set_phase("idle");
  return 1;
}

void astro_remote_service_snapshot(astro_remote_state_t *out)
{
  if(out)*out=g_remote;
}
