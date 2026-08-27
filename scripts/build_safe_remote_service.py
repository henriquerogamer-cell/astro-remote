#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: build_safe_remote_service.py INPUT OUTPUT")

src = Path(sys.argv[1]).read_text()


def replace_between(text, start, end, replacement):
    a = text.find(start)
    if a < 0:
        raise SystemExit(f"start marker not found: {start}")
    b = text.find(end, a)
    if b < 0:
        raise SystemExit(f"end marker not found: {end}")
    return text[:a] + replacement.rstrip() + "\n\n" + text[b:]

safe_shutdown = r'''int astro_remote_service_shutdown_worker(void)
{
  char buf[256];
  int req_rc;

  reap_nonblocking("worker_offline");
  if(g_worker_pid<=0){
    g_remote.service_online=0;
    g_remote.session_active=0;
    g_remote.worker_port_open=0;
    g_remote.worker_stuck=0;
    g_remote.session_started_at=0;
    set_phase("worker_offline");
    return 0;
  }

  set_phase("stopping_remote_worker_cooperative");
  req_rc=worker_request("POST","/shutdown",buf,sizeof(buf));
  if(req_rc==0&&strstr(buf,"200 OK")!=NULL){
    if(wait_worker_exit_ms(3000,"worker_offline"))return 1;
  }

  /* Never SIGTERM/SIGKILL a PS5 worker that may have touched native
     Remote Play state. A forced process teardown has proven capable of
     kernel-panicking the console. Leave it alive and report pending. */
  g_remote.worker_stuck=1;
  g_remote.worker_pid=(int)g_worker_pid;
  g_remote.worker_port_open=worker_port_open();
  g_remote.service_online=g_remote.worker_port_open?worker_healthcheck():0;
  read_worker_diag(g_worker_pid);
  set_phase("remote_worker_shutdown_pending");
  g_remote.last_change_at=time(NULL);
  return -2;
}'''

safe_stop = r'''int astro_remote_service_stop(void)
{
  char buf[2048];
  int rc;

  reap_nonblocking("worker_offline");
  if(g_worker_pid<=0){
    g_remote.session_active=0;
    g_remote.session_started_at=0;
    g_remote.service_online=0;
    g_remote.worker_port_open=0;
    set_phase("worker_offline");
    return 0;
  }

  if(!worker_healthcheck()){
    read_worker_diag(g_worker_pid);
    g_remote.worker_stuck=1;
    set_phase("remote_worker_unresponsive_no_force_kill");
    return -2;
  }

  if(!g_remote.session_active){
    fetch_worker_status();
    return 0;
  }

  set_phase("stopping_remote_session");
  rc=worker_request("POST","/session/stop",buf,sizeof(buf));
  if(rc==0&&apply_worker_status(buf)==0&&!g_remote.session_active){
    g_remote.service_online=1;
    g_remote.session_started_at=0;
    g_remote.last_change_at=time(NULL);
    return 1;
  }

  /* Do not restart or kill the worker when a stop request fails. */
  read_worker_diag(g_worker_pid);
  g_remote.worker_stuck=1;
  set_phase("remote_session_stop_pending");
  g_remote.last_change_at=time(NULL);
  return -3;
}'''

src = replace_between(
    src,
    "int astro_remote_service_shutdown_worker(void)",
    "int astro_remote_service_ensure_worker(void)",
    safe_shutdown,
)
src = replace_between(
    src,
    "int astro_remote_service_stop(void)",
    "int astro_remote_service_enable_remoteplay(void)",
    safe_stop,
)

Path(sys.argv[2]).write_text(src)
