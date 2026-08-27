#!/usr/bin/env python3
from pathlib import Path
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: build_split_remote_page.py INPUT OUTPUT")

src = Path(sys.argv[1]).read_text()

old = "<button id='pair' class='primary'>PAREAR ASTRO COM ESTE PS5</button><button id='cancel' class='danger'>CANCELAR PAREAMENTO</button><div id='message'></div>"
new = "<button id='pair' class='primary'>PAREAR ASTRO COM ESTE PS5</button><button id='cancel' class='danger'>CANCELAR PAREAMENTO</button><button id='shutdown' class='danger'>ENCERRAR ASTRO REMOTE</button><div id='message'></div>"
if old not in src:
    raise SystemExit("remote page button marker not found")
src = src.replace(old, new, 1)

old = "var busy=false;function message"
new = "var busy=false,stopped=false;function message"
if old not in src:
    raise SystemExit("remote page busy marker not found")
src = src.replace(old, new, 1)

old = "function status(){req('GET',endpoint('status'),null,function(e,w){"
new = "function status(){if(stopped)return;req('GET',endpoint('status'),null,function(e,w){"
if old not in src:
    raise SystemExit("remote page status marker not found")
src = src.replace(old, new, 1)

old = "el('cancel').onclick=function(){req('POST',endpoint('pairing/cancel'),'',function(e,w){if(e){message(e,false);return}message('Pareamento cancelado.',true);render(w)})};status();setInterval(status,1000)"
new = "el('cancel').onclick=function(){req('POST',endpoint('pairing/cancel'),'',function(e,w){if(e){message(e,false);return}message('Pareamento cancelado.',true);render(w)})};el('shutdown').onclick=function(){if(!confirm('Encerrar somente o Astro Remote? O painel principal continuara online.'))return;req('POST',endpoint('shutdown'),'',function(e,w){if(e){message('Falha ao encerrar Astro Remote: '+e,false);return}stopped=true;el('remotePill').textContent='REMOTE OFFLINE';el('remotePill').className='pill';el('phasePill').textContent='ENCERRADO';message('Astro Remote encerrado. O astrormt continua independente.',true);el('pair').disabled=true;el('cancel').disabled=true;el('shutdown').disabled=true})};status();setInterval(status,1000)"
if old not in src:
    raise SystemExit("remote page handler marker not found")
src = src.replace(old, new, 1)

Path(sys.argv[2]).write_text(src)
