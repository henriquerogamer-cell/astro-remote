#define SHADOWMOUNT_DIR "/data/shadowmount"
#define SHADOWMOUNT_CONFIG SHADOWMOUNT_DIR "/config.ini"
#define SHADOWMOUNT_MAX_PATHS 32
#define SHADOWMOUNT_PATH_MAX 1024

static char *shadow_trim(char *s)
{
  while (s && *s && isspace((unsigned char)*s)) s++;
  if (!s) return s;
  char *e = s + strlen(s);
  while (e > s && isspace((unsigned char)e[-1])) *--e = '\0';
  return s;
}

static int shadow_scan_line_path(const char *line, char *out, size_t out_sz)
{
  if (!line || !out || out_sz == 0) return 0;
  char tmp[2048];
  snprintf(tmp, sizeof(tmp), "%s", line);
  char *p = shadow_trim(tmp);
  if (strncmp(p, "scanpath=", 9) != 0) return 0;
  p = shadow_trim(p + 9);
  if (!*p) return 0;
  snprintf(out, out_sz, "%s", p);
  return 1;
}

static int shadow_path_valid(const char *p)
{
  if (!p || p[0] != '/' || strlen(p) >= SHADOWMOUNT_PATH_MAX) return 0;
  if (strstr(p, "..") || strchr(p, '\r') || strchr(p, '\n') || strchr(p, '=')) return 0;

  if (!strncmp(p, "/data", 5) && (p[5] == '\0' || p[5] == '/')) return 1;
  if (!strncmp(p, "/user", 5) && (p[5] == '\0' || p[5] == '/')) return 1;
  if (!strncmp(p, "/mnt/usb", 8)) return 1;
  if (!strncmp(p, "/mnt/ext", 8)) return 1;
  if (!strncmp(p, "/mnt/shadowmnt", 14) && (p[14] == '\0' || p[14] == '/')) return 1;
  return 0;
}

static int shadow_load_paths(char paths[][SHADOWMOUNT_PATH_MAX], int max_paths)
{
  if (!paths || max_paths <= 0) return 0;
  FILE *f = fopen(SHADOWMOUNT_CONFIG, "r");
  if (!f) return 0;

  int count = 0;
  char line[2048], path[SHADOWMOUNT_PATH_MAX];
  while (fgets(line, sizeof(line), f) && count < max_paths) {
    if (!shadow_scan_line_path(line, path, sizeof(path))) continue;
    int duplicate = 0;
    for (int i = 0; i < count; ++i) {
      if (!strcmp(paths[i], path)) { duplicate = 1; break; }
    }
    if (!duplicate) snprintf(paths[count++], SHADOWMOUNT_PATH_MAX, "%s", path);
  }
  fclose(f);
  return count;
}

static int shadow_rewrite_paths(char paths[][SHADOWMOUNT_PATH_MAX], int count)
{
  ensure_dir(SHADOWMOUNT_DIR);
  const char *tmp_path = SHADOWMOUNT_DIR "/config.ini.astro.tmp";
  FILE *in = fopen(SHADOWMOUNT_CONFIG, "r");
  FILE *out = fopen(tmp_path, "w");
  if (!out) { if (in) fclose(in); return -1; }

  int wrote_any = 0;
  char last = '\n';
  if (in) {
    char line[2048], ignored[SHADOWMOUNT_PATH_MAX];
    while (fgets(line, sizeof(line), in)) {
      if (shadow_scan_line_path(line, ignored, sizeof(ignored))) continue;
      if (fputs(line, out) == EOF) { fclose(in); fclose(out); unlink(tmp_path); return -2; }
      wrote_any = 1;
      size_t n = strlen(line);
      if (n) last = line[n - 1];
    }
    fclose(in);
  }

  if (count > 0 && wrote_any && last != '\n') fputc('\n', out);
  for (int i = 0; i < count; ++i) {
    if (fprintf(out, "scanpath=%s\n", paths[i]) < 0) {
      fclose(out); unlink(tmp_path); return -3;
    }
  }

  fflush(out);
  fsync(fileno(out));
  fclose(out);
  chmod(tmp_path, 0644);
  if (rename(tmp_path, SHADOWMOUNT_CONFIG) != 0) { unlink(tmp_path); return -4; }
  return 0;
}

static void shadow_paths_api(int fd)
{
  char paths[SHADOWMOUNT_MAX_PATHS][SHADOWMOUNT_PATH_MAX];
  int count = shadow_load_paths(paths, SHADOWMOUNT_MAX_PATHS);
  size_t cap = 48 * 1024, used = 0;
  char *j = malloc(cap);
  if (!j) { send_text(fd, "500 Internal Server Error", "application/json", NULL, "{\"ok\":false}"); return; }

  used += (size_t)snprintf(j + used, cap - used,
    "{\"ok\":true,\"mode\":\"%s\",\"config\":\"%s\",\"paths\":[",
    count ? "custom" : "default", SHADOWMOUNT_CONFIG);
  for (int i = 0; i < count; ++i) {
    char ep[2048]; json_escape(paths[i], ep, sizeof(ep));
    used += (size_t)snprintf(j + used, cap - used, "%s\"%s\"", i ? "," : "", ep);
  }
  snprintf(j + used, cap - used, "]}");
  send_text(fd, "200 OK", "application/json; charset=utf-8", NULL, j);
  free(j);
}

static void shadow_add_api(int fd, const char *target)
{
  char path[SHADOWMOUNT_PATH_MAX];
  if (!query_value(target, "path", path, sizeof(path)) || !shadow_path_valid(path)) {
    send_text(fd, "400 Bad Request", "application/json", NULL, "{\"ok\":false,\"error\":\"invalid_path\"}");
    return;
  }

  size_t n = strlen(path);
  while (n > 1 && path[n - 1] == '/') path[--n] = '\0';

  char paths[SHADOWMOUNT_MAX_PATHS][SHADOWMOUNT_PATH_MAX];
  int count = shadow_load_paths(paths, SHADOWMOUNT_MAX_PATHS);
  for (int i = 0; i < count; ++i) {
    if (!strcmp(paths[i], path)) {
      send_text(fd, "200 OK", "application/json", NULL, "{\"ok\":true,\"duplicate\":true}");
      return;
    }
  }
  if (count >= SHADOWMOUNT_MAX_PATHS) {
    send_text(fd, "409 Conflict", "application/json", NULL, "{\"ok\":false,\"error\":\"too_many_paths\"}");
    return;
  }
  snprintf(paths[count++], SHADOWMOUNT_PATH_MAX, "%s", path);
  if (shadow_rewrite_paths(paths, count) != 0) {
    send_text(fd, "500 Internal Server Error", "application/json", NULL, "{\"ok\":false,\"error\":\"write\"}");
    return;
  }
  send_text(fd, "200 OK", "application/json", NULL, "{\"ok\":true}");
}

static void shadow_remove_api(int fd, const char *target)
{
  char path[SHADOWMOUNT_PATH_MAX];
  if (!query_value(target, "path", path, sizeof(path)) || !shadow_path_valid(path)) {
    send_text(fd, "400 Bad Request", "application/json", NULL, "{\"ok\":false,\"error\":\"invalid_path\"}");
    return;
  }

  size_t n = strlen(path);
  while (n > 1 && path[n - 1] == '/') path[--n] = '\0';

  char paths[SHADOWMOUNT_MAX_PATHS][SHADOWMOUNT_PATH_MAX];
  int count = shadow_load_paths(paths, SHADOWMOUNT_MAX_PATHS), out_count = 0;
  for (int i = 0; i < count; ++i) {
    if (strcmp(paths[i], path)) {
      if (out_count != i) snprintf(paths[out_count], SHADOWMOUNT_PATH_MAX, "%s", paths[i]);
      out_count++;
    }
  }
  if (shadow_rewrite_paths(paths, out_count) != 0) {
    send_text(fd, "500 Internal Server Error", "application/json", NULL, "{\"ok\":false,\"error\":\"write\"}");
    return;
  }
  send_text(fd, "200 OK", "application/json", NULL, "{\"ok\":true}");
}

static void shadow_reset_api(int fd)
{
  char paths[SHADOWMOUNT_MAX_PATHS][SHADOWMOUNT_PATH_MAX];
  if (shadow_rewrite_paths(paths, 0) != 0) {
    send_text(fd, "500 Internal Server Error", "application/json", NULL, "{\"ok\":false,\"error\":\"write\"}");
    return;
  }
  send_text(fd, "200 OK", "application/json", NULL, "{\"ok\":true}");
}

static void send_settings_plus_page(int fd, const char *msg)
{
  char page[42000];
  page_start(page, sizeof(page), "Ajustes", "settings");
  char b[34000];
  snprintf(b, sizeof(b),
    "<div class='pagehead'><div><h1>Ajustes</h1><p>Configurações do Astro e integrações do PS5.</p></div></div>"
    "<section class='grid'>"
    "<article class='panel col8'><h2>Segurança</h2>%s<form method='POST' action='/settings/password'><div class='field'><label>Nova senha</label><input name='password' type='password' minlength='8' required></div><button class='primary' type='submit'>Trocar senha</button></form></article>"
    "<article class='panel col4'><h2>Servidor</h2><p class='muted'>Usuário: <b>%s</b><br>Porta: %d<br>Payloads: %s</p><form method='POST' action='/shutdown'><button class='red' type='submit'>Encerrar Astro</button></form></article>"
    "<article class='panel full'><div class='kicker'>SHADOWMOUNTPLUS</div><div class='pagehead' style='margin-bottom:10px'><div><h2 style='margin:0 0 6px'>Pastas de varredura</h2><p>Pastas que o ShadowMountPlus procura para montar jogos automaticamente. Não precisa estar rodando para configurar.</p></div><span id='smMode' class='badge'>Lendo...</span></div>"
    "<div id='smNotice' class='notice' style='margin-bottom:12px'>Carregando configuração...</div>"
    "<div class='toolbar' style='margin-bottom:12px'><input id='smPath' class='search' placeholder='/data/pegasus'><button class='primary' onclick='smAdd()'>＋ Adicionar caminho</button><button onclick=\"smQuick('/data/pegasus')\">Pegasus</button><button onclick=\"smQuick('/data/etaHEN/games')\">etaHEN/games</button><button class='ghost' onclick='smReset()'>Usar padrões</button></div>"
    "<div id='smPaths'></div><p class='muted' style='margin-top:12px'>Arquivo gerenciado: <code>/data/shadowmount/config.ini</code>. Se o ShadowMountPlus já estiver em execução, reinicie o payload para garantir que a nova lista seja aplicada.</p></article>"
    "</section>"
    "<script>const sme=s=>String(s??'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));"
    "async function smLoad(){try{const r=await fetch('/api/shadowmount/paths',{cache:'no-store'}),j=await r.json();if(!j.ok)throw 0;const custom=j.mode==='custom',mode=document.getElementById('smMode'),n=document.getElementById('smNotice'),box=document.getElementById('smPaths');mode.textContent=custom?'CAMINHOS PERSONALIZADOS':'LOCAIS PADRÃO';mode.className='badge '+(custom?'run':'');n.className='notice '+(custom?'':'good');n.innerHTML=custom?'O ShadowMountPlus está usando <b>somente</b> os caminhos abaixo. Se quiser manter etaHEN/games, deixe essa pasta também na lista.':'Usando os locais internos padrão do ShadowMountPlus. Ao adicionar o primeiro caminho, os padrões deixam de ser usados automaticamente.';box.innerHTML='';if(!j.paths.length){box.innerHTML=\"<div class='notice'>Nenhum <code>scanpath=</code> personalizado configurado.</div>\";return}for(const p of j.paths){const d=document.createElement('div');d.className='savecard';d.innerHTML=`<div class='saveicon'>📁</div><div class='saveinfo'><b><code>${sme(p)}</code></b><span>scanpath</span></div><button class='sm red' onclick='smRemove(${JSON.stringify(p)})'>Remover</button>`;box.appendChild(d)}}catch(e){document.getElementById('smNotice').className='notice bad';document.getElementById('smNotice').textContent='Não foi possível ler /data/shadowmount/config.ini.'}}"
    "async function smPost(url){const r=await fetch(url,{method:'POST'}),j=await r.json().catch(()=>({}));if(!r.ok||!j.ok)throw new Error(j.error||'falha');await smLoad()}"
    "async function smAdd(){let p=document.getElementById('smPath').value.trim();if(!p)return;try{await smPost('/api/shadowmount/add?path='+encodeURIComponent(p));document.getElementById('smPath').value=''}catch(e){alert('Não foi possível adicionar o caminho: '+e.message)}}"
    "function smQuick(p){document.getElementById('smPath').value=p;smAdd()}"
    "async function smRemove(p){if(!confirm('Remover '+p+' da varredura do ShadowMountPlus?'))return;try{await smPost('/api/shadowmount/remove?path='+encodeURIComponent(p))}catch(e){alert('Não foi possível remover: '+e.message)}}"
    "async function smReset(){if(!confirm('Remover todos os scanpath personalizados e voltar aos locais padrão do ShadowMountPlus?'))return;try{await smPost('/api/shadowmount/reset')}catch(e){alert('Não foi possível restaurar os padrões: '+e.message)}}smLoad();</script>",
    msg ? msg : "", g_user, ASTRO_PORT, ASTRO_PAYLOAD_DIR);
  strncat(page, b, sizeof(page) - strlen(page) - 1);
  page_end(page, sizeof(page));
  send_text(fd, "200 OK", "text/html; charset=utf-8", NULL, page);
}
