#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: build_detached_main.py INPUT OUTPUT")

src = Path(sys.argv[1]).read_text()

old = "a.href='/remote'"
new = "a.href='/service/remote/_root/'"
if old not in src:
    raise SystemExit("remote nav marker not found")
src = src.replace(old, new, 1)

old = 'else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){astro_remote_service_stop();send_response(client,"200 OK",NULL,shutdown_page);running=0;}'
new = 'else if(strstr(buf,"POST /admin/shutdown ")&&logged(buf)){send_response(client,"200 OK",NULL,shutdown_page);running=0;}'
if old not in src:
    raise SystemExit("admin shutdown marker not found")
src = src.replace(old, new, 1)

Path(sys.argv[2]).write_text(src)
