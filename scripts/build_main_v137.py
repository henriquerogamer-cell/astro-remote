#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: build_main_v137.py INPUT OUTPUT")

src = Path(sys.argv[1]).read_text()

old_init = "astro_remote_service_init();"
new_init = "astro_remote_service_init(); astro_remote_service_ensure_worker();"
if old_init not in src:
    raise SystemExit("remote init marker not found")
src = src.replace(old_init, new_init, 1)

old_shutdown = 'else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){astro_remote_service_stop();send_response(client,"200 OK",NULL,shutdown_page);running=0;}'
new_shutdown = '''else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){
      int src=astro_remote_service_stop();
      int wrc;
      if(src<0){
        send_json(client,"409 Conflict","{\\"ok\\":false,\\"error\\":\\"remote_session_stop_pending\\"}");
      }else{
        wrc=astro_remote_service_shutdown_worker();
        if(wrc<0)send_json(client,"409 Conflict","{\\"ok\\":false,\\"error\\":\\"remote_worker_shutdown_pending\\"}");
        else{send_response(client,"200 OK",NULL,shutdown_page);running=0;}
      }
    }'''
if old_shutdown not in src:
    raise SystemExit("admin shutdown marker not found")
src = src.replace(old_shutdown, new_shutdown, 1)

old_tail = 'close(server);notify("ASTRO Remote encerrado");return 0;'
new_tail = 'astro_remote_service_stop(); astro_remote_service_shutdown_worker(); close(server);notify("ASTRO Remote encerrado");return 0;'
if old_tail not in src:
    raise SystemExit("main cleanup marker not found")
src = src.replace(old_tail, new_tail, 1)

Path(sys.argv[2]).write_text(src)
