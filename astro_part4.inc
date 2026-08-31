static int safe_path(const char *p)
{
  if (!p || p[0] != '/' || strstr(p, "..")) return 0;
  return strncmp(p,"/data",5)==0 || strncmp(p,"/user",5)==0 ||
         strncmp(p,"/mnt/usb",8)==0 || strncmp(p,"/mnt/ext",8)==0 ||
         strncmp(p,"/mnt/sandbox",12)==0;
}

static void send_file_list_api(int fd, const char *target)
{
  char path[2048] = "/data";
  query_value(target,"path",path,sizeof(path));
  if (!safe_path(path)) { send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"path\"}"); return; }
  DIR *d = opendir(path);
  if (!d) { send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"open\"}"); return; }
  size_t cap=512*1024, used=0; char *j=(char*)malloc(cap); if(!j){closedir(d);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  used += (size_t)snprintf(j+used,cap-used,"{\"ok\":true,\"path\":\"");
  char ep[4096]; json_escape(path,ep,sizeof(ep)); used += (size_t)snprintf(j+used,cap-used,"%s\",\"items\":[",ep);
  struct dirent *de; int first=1, count=0;
  while ((de=readdir(d)) && count<800) {
    if (!strcmp(de->d_name,".") || !strcmp(de->d_name,"..")) continue;
    char full[4096]; snprintf(full,sizeof(full),"%s/%s",path,de->d_name);
    struct stat st; if(stat(full,&st)!=0) continue;
    char en[1024]; json_escape(de->d_name,en,sizeof(en));
    int n=snprintf(j+used,cap-used,"%s{\"name\":\"%s\",\"dir\":%s,\"size\":%llu}",first?"":",",en,S_ISDIR(st.st_mode)?"true":"false",(unsigned long long)st.st_size);
    if (n < 0 || used + (size_t)n + 64 >= cap) break;
    used += (size_t)n;
    first = 0;
    count++;
  }
  closedir(d); snprintf(j+used,cap-used,"]}"); send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j); free(j);
}

static void send_download(int fd, const char *target)
{
  char path[2048]; if(!query_value(target,"path",path,sizeof(path)) || !safe_path(path)){send_text(fd,"400 Bad Request","text/plain",NULL,"Caminho invalido");return;}
  struct stat st; if(stat(path,&st)!=0 || !S_ISREG(st.st_mode)){send_text(fd,"404 Not Found","text/plain",NULL,"Arquivo nao encontrado");return;}
  FILE *f=fopen(path,"rb"); if(!f){send_text(fd,"403 Forbidden","text/plain",NULL,"Sem acesso");return;}
  const char *base=strrchr(path,'/'); base=base?base+1:path; char safe[512]; size_t k=0; for(size_t i=0;base[i]&&k+1<sizeof(safe);++i){char c=base[i]; safe[k++]=(c=='\"'||c=='\r'||c=='\n')?'_':c;} safe[k]=0;
  char hdr[1024]; snprintf(hdr,sizeof(hdr),"HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: %llu\r\nContent-Disposition: attachment; filename=\"%s\"\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n",(unsigned long long)st.st_size,safe); send_all(fd,hdr,strlen(hdr));
  uint8_t b[64*1024]; size_t n; while((n=fread(b,1,sizeof(b),f))>0){if(send_all(fd,b,n)!=0)break;} fclose(f);
}

static void send_files_page(int fd)
{
  char page[28000]; page_start(page,sizeof(page),"Arquivos","files");
  const char *body =
"<section class='hero'><h1>Arquivos</h1><p>Explorador nativo para navegação e download. Para operações pesadas, upload grande, ZIP, mover e copiar, o Prospero continua disponível dentro do Astro.</p></section>"
"<section class='grid'><article class='card full'><div class='row'><input id='path' value='/data' style='flex:1;min-width:260px'><button onclick='loadDir()'>Abrir</button><a class='button ghost' href='/p/prospero/'>Prospero Files</a></div><div id='crumb' class='crumb'></div><div style='overflow:auto'><table><thead><tr><th>Nome</th><th>Tipo</th><th>Tamanho</th><th></th></tr></thead><tbody id='rows'></tbody></table></div></article></section>"
"<script>const esc=s=>String(s).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));function fmt(n){if(!n)return '0 B';const u=['B','KB','MB','GB','TB'];let i=0;while(n>1024&&i<u.length-1){n/=1024;i++}return n.toFixed(i?1:0)+' '+u[i]}async function loadDir(p){const el=document.getElementById('path');p=p||el.value;const r=await fetch('/api/files/list?path='+encodeURIComponent(p));const j=await r.json();if(!j.ok){alert('Nao foi possivel abrir');return}el.value=j.path;document.getElementById('crumb').textContent=j.path;const rows=document.getElementById('rows');rows.innerHTML='';if(j.path!='/'){const up=j.path.replace(/\\/+$/,'').replace(/\\/[^/]+$/,'')||'/';rows.innerHTML+=`<tr><td><a href='#' onclick=\"loadDir('${esc(up)}');return false\">..</a></td><td>pasta</td><td></td><td></td></tr>`}for(const x of j.items){const full=(j.path.replace(/\\/$/,'')+'/'+x.name);rows.innerHTML+=`<tr><td>${x.dir?`<a href='#' onclick=\"loadDir('${esc(full)}');return false\">${esc(x.name)}</a>`:esc(x.name)}</td><td>${x.dir?'pasta':'arquivo'}</td><td>${x.dir?'':fmt(x.size)}</td><td>${x.dir?'':`<a class='button ghost' href='/api/files/download?path=${encodeURIComponent(full)}'>Baixar</a>`}</td></tr>`}}}loadDir();</script>";
  strncat(page,body,sizeof(page)-strlen(page)-1); page_end(page,sizeof(page)); send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}

static void send_saves_page(int fd)
{
  int g=port_open(GARLIC_PORT);
  char page[24000]; page_start(page,sizeof(page),"Saves","saves"); char body[16000];
  snprintf(body,sizeof(body),
"<section class='hero'><h1>Saves</h1><p>O Astro usa o Garlic SaveMgr como motor. Assim o mesmo módulo cobre backup e restore normais e também o fluxo especial de resign para levar um save do User1 ao User2.</p></section><section class='grid'>"
"<article class='card wide'><h2>Garlic SaveMgr</h2><div class='status %s'><span class='dot'></span>%s</div><p class='muted'>Quando estiver ativo na porta %d, você abre toda a interface por dentro do Astro.</p><div class='row'><a class='button' href='/p/garlic/'>Abrir Save Manager</a></div></article>"
"<article class='card'><h2>Fluxos</h2><p class='muted'>Backup → preserva usuário original.<br>Restore → volta para o mesmo usuário.<br>Clonar → resign Account ID + User ID para o usuário de destino.</p></article>"
"<article class='card full'><h2>Próxima etapa</h2><p class='muted'>Vamos transformar as ações mais usadas do Garlic em botões nativos do Astro, sem tirar o Garlic de cena. Ele continua sendo o motor, o Astro vira o painel.</p></article></section>",
  g?"on":"off",g?"ONLINE":"OFFLINE",GARLIC_PORT);
  strncat(page,body,sizeof(page)-strlen(page)-1); page_end(page,sizeof(page)); send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}

static void send_homebrew_page(int fd)
{
  char page[34000]; page_start(page,sizeof(page),"Homebrew","homebrew");
  const char *body=
"<section class='hero'><h1>Homebrew</h1><p>Catálogo PS5 dentro do Astro. Esta primeira camada lê uma fonte pública de PKGs e organiza os apps sem instalar outra Store no console.</p></section>"
"<section class='grid'><article class='card full'><div class='row'><input id='q' placeholder='Buscar homebrew...' oninput='render()' style='flex:1'><button onclick='loadStore()'>Atualizar</button></div><p id='storeState' class='muted'>Carregando catálogo...</p><div id='pkgs' class='store-grid'></div></article></section>"
"<script>const SOURCE='https://nexgen999.github.io/PS5-Super-PLDMGR-Auto-Updater/PKGjson/ps5pkg.json';let all=[];const esc=s=>String(s==null?'':s).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));async function loadStore(){const st=document.getElementById('storeState');st.textContent='Atualizando catálogo...';try{const r=await fetch(SOURCE,{cache:'no-store'});const j=await r.json();all=j.packages||j.items||[];st.textContent=all.length+' pacotes encontrados';render()}catch(e){st.textContent='Falha ao consultar catálogo. O acesso direto à fonte pode ter sido bloqueado pelo navegador.'}}function render(){const q=document.getElementById('q').value.toLowerCase();const el=document.getElementById('pkgs');el.innerHTML='';for(const x of all.filter(x=>!q||String(x.name||x.title||'').toLowerCase().includes(q)||String(x.author||'').toLowerCase().includes(q)).slice(0,120)){const name=x.name||x.title||x.filename||'Homebrew';const url=x.url||x.download||x.package||'#';el.innerHTML+=`<article class='pkg'><h3>${esc(name)}</h3><div class='meta'>${esc(x.author||'')}<br>${esc(x.description||'')}</div><span class='ver'>${esc(x.version||'')}</span><div class='row'><a class='button ghost' target='_blank' rel='noopener' href='${esc(url)}'>Fonte / PKG</a></div></article>`}}}loadStore();</script>";
  strncat(page,body,sizeof(page)-strlen(page)-1); page_end(page,sizeof(page)); send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}

static void send_settings_page(int fd, const char *msg)
{
  char page[22000]; page_start(page,sizeof(page),"Ajustes","settings"); char body[14000];
  snprintf(body,sizeof(body),
"<section class='hero'><h1>Ajustes</h1><p>O login fica salvo localmente em <code>%s</code>. A sessão muda a cada inicialização do Astro.</p></section><section class='grid'><article class='card wide'><h2>Segurança</h2>%s<form method='POST' action='/settings/password'><div class='field'><label>Nova senha</label><input name='password' type='password' minlength='8' required></div><button type='submit'>Trocar senha</button></form></article><article class='card'><h2>Astro</h2><p class='muted'>Usuário atual: <b>%s</b><br>Porta: %d</p><form method='POST' action='/shutdown'><button class='danger' type='submit'>Encerrar Astro</button></form></article></section>",
  AUTH_FILE,msg?msg:"",g_user,ASTRO_PORT);
  strncat(page,body,sizeof(page)-strlen(page)-1); page_end(page,sizeof(page)); send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}

static void send_status_api(int fd)
{
  char j[512]; snprintf(j,sizeof(j),"{\"ok\":true,\"astro_port\":%d,\"services\":{\"prospero\":%s,\"pegasus\":%s,\"garlic\":%s}}",ASTRO_PORT,port_open(PROSPERO_PORT)?"true":"false",port_open(PEGASUS_PORT)?"true":"false",port_open(GARLIC_PORT)?"true":"false");
  send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);
}
