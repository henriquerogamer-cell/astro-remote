#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: build_safe_remote_worker.py INPUT OUTPUT")

src = Path(sys.argv[1]).read_text()

old = 'else if(strstr(buf,"POST /shutdown ")){send_json(c,"200 OK","{\\"ok\\":true,\\"stopping\\":true}");g_worker_running=0;}'
new = 'else if(strstr(buf,"POST /shutdown ")){stop_session();send_json(c,"200 OK","{\\"ok\\":true,\\"stopping\\":true,\\"remoteplay_cleanup\\":true}");g_worker_running=0;}'
if old not in src:
    raise SystemExit("worker shutdown route marker not found")
src = src.replace(old, new, 1)

old_tail = 'cancel_client_job();close(server);return 0;'
new_tail = 'stop_session();close(server);return 0;'
if old_tail not in src:
    raise SystemExit("worker cleanup marker not found")
src = src.replace(old_tail, new_tail, 1)

Path(sys.argv[2]).write_text(src)
