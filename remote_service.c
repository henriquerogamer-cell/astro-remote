#include <stdio.h>
#include <string.h>

#include "remote_service.h"
#include "remote_ps5_source.h"

static astro_remote_state_t g_remote;

static void set_phase(const char *phase)
{
  if(!phase)phase="unknown";
  snprintf(g_remote.phase,sizeof(g_remote.phase),"%s",phase);
}

static void apply_source_probe(const astro_remote_ps5_source_probe_t *p,int rc)
{
  g_remote.source_probe_rc=rc;
  g_remote.remoteplay_initialized=p->remoteplay_initialized;
  g_remote.remoteplay_enabled=p->remoteplay_enabled;
  g_remote.remoteplay_tcp_9295=p->tcp_9295_open;
  g_remote.source_probed_at=p->probed_at;
  g_remote.source_available=(rc==0);

  if(rc==0)set_phase("remoteplay_source_detected");
  else if(rc==-10)set_phase("remoteplay_init_failed");
  else if(rc==-20)set_phase("remoteplay_registry_error");
  else if(rc==-30)set_phase("remoteplay_disabled");
  else if(rc==-40)set_phase("remoteplay_port_closed");
  else set_phase("remoteplay_probe_failed");
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
  astro_remote_ps5_source_probe_t probe;
  int probe_rc;

  if(!g_remote.service_online)return -1;
  if(g_remote.session_active)return 0;

  g_remote.session_active=1;
  g_remote.video_ready=0;
  g_remote.control_ready=0;
  g_remote.source_available=0;
  g_remote.generation++;
  g_remote.session_started_at=time(NULL);
  g_remote.last_change_at=g_remote.session_started_at;
  set_phase("probing_remoteplay_source");

  memset(&probe,0,sizeof(probe));
  probe_rc=astro_remote_ps5_source_probe(&probe);
  apply_source_probe(&probe,probe_rc);
  g_remote.last_change_at=time(NULL);
  return 1;
}

int astro_remote_service_stop(void)
{
  if(!g_remote.service_online)return -1;
  if(!g_remote.session_active)return 0;
  g_remote.session_active=0;
  g_remote.video_ready=0;
  g_remote.control_ready=0;
  g_remote.source_available=0;
  g_remote.session_started_at=0;
  g_remote.last_change_at=time(NULL);
  set_phase("idle");
  return 1;
}

void astro_remote_service_snapshot(astro_remote_state_t *out)
{
  if(out)*out=g_remote;
}
