#include <time.h>
#include <dirent.h>
#include <sys/stat.h>

#define main astro_v03_legacy_main
#include "main_v03.c"
#undef main

#define ASTRO_MAX_ELFS 64
#define ASTRO_PATH_MAX 1024
#define ASTRO_SCAN_DEPTH 4
#define ASTRO_ELFLDR_PORT 9021

typedef struct astro_elf_item {
  char name[256];
  char path[ASTRO_PATH_MAX];
  long long size;
} astro_elf_item_t;

static time_t astro_started_at;

static void send_json(int fd, const char *status, const char *body)
{
  char header[1024];
  snprintf(header, sizeof(header),
    "HTTP/1.1 %s\r\nContent-Type: application/json; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
    status, strlen(body));
  send(fd, header, strlen(header), 0);
  send(fd, body, strlen(body), 0);
}

static int has_elf_suffix(const char *name)
{
  size_t len = strlen(name);
  return len >= 4 && strcmp(name + len - 4, ".elf") == 0;
}

static int astro_scan_dir(const char *dir_path, astro_elf_item_t *items, int count, int max_items, int depth)
{
  DIR *dir;
  struct dirent *entry;

  if (depth > ASTRO_SCAN_DEPTH || count >= max_items) return count;
  dir = opendir(dir_path);
  if (!dir) return count;

  while ((entry = readdir(dir)) != NULL && count < max_items) {
    char full[ASTRO_PATH_MAX];
    struct stat st;

    if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
    if (snprintf(full, sizeof(full), "%s/%s", dir_path, entry->d_name) >= (int)sizeof(full)) continue;
    if (lstat(full, &st) != 0 || S_ISLNK(st.st_mode)) continue;

    if (S_ISDIR(st.st_mode)) {
      count = astro_scan_dir(full, items, count, max_items, depth + 1);
      continue;
    }

    if (S_ISREG(st.st_mode) && has_elf_suffix(entry->d_name)) {
      snprintf(items[count].name, sizeof(items[count].name), "%s", entry->d_name);
      snprintf(items[count].path, sizeof(items[count].path), "%s", full);
      items[count].size = (long long)st.st_size;
      count++;
    }
  }

  closedir(dir);
  return count;
}

static int astro_collect_elfs(astro_elf_item_t *items, int max_items)
{
  static const char *roots[] = {
    "/data/pldmgr/payloads",
    "/data/ProsperoMgr/payloads",
    "/data/ps5_autoloader"
  };
  int count = 0;
  size_t i;

  for (i = 0; i < sizeof(roots) / sizeof(roots[0]); i++) {
    count = astro_scan_dir(roots[i], items, count, max_items, 0);
    if (count >= max_items) break;
  }
  return count;
}

static int astro_find_path(const char *path, astro_elf_item_t *out)
{
  astro_elf_item_t items[ASTRO_MAX_ELFS];
  int count = astro_collect_elfs(items, ASTRO_MAX_ELFS);
  int i;
  for (i = 0; i < count; i++) {
    if (!strcmp(items[i].path, path)) {
      if (out) *out = items[i];
      return 1;
    }
  }
  return 0;
}

static void json_escape(char *dst, size_t dst_size, const char *src)
{
  size_t used = 0;
  if (!dst_size) return;
  while (*src && used + 1 < dst_size) {
    unsigned char c = (unsigned char)*src++;
    if (c == '"' || c == '\\') {
      if (used + 2 >= dst_size) break;
      dst[used++] = '\\'; dst[used++] = (char)c;
    } else if (c == '\n' || c == '\r' || c == '\t') {
      if (used + 2 >= dst_size) break;
      dst[used++] = '\\'; dst[used++] = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
    } else if (c >= 0x20) dst[used++] = (char)c;
  }
  dst[used] = '\0';
}

static int astro_port_open(int port)
{
  int fd;
  struct sockaddr_in addr;
  int rc;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return 0;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
  close(fd);
  return rc == 0;
}

static void astro_service_meta(const char *name, int *is_service, int *port, int *active, int *stoppable)
{
  *is_service = 0; *port = 0; *active = 0; *stoppable = 0;

  if (strstr(name, "ftpsrv")) {
    *is_service = 1; *port = 2121; *active = astro_port_open(*port); return;
  }
  if (!strcmp(name, "ProsperoMgr.elf")) {
    *is_service = 1; *port = 7070; *active = astro_port_open(*port); *stoppable = 1; return;
  }
  if (!strcmp(name, "pegasus_dl.elf")) {
    *is_service = 1; *port = 6970; *active = astro_port_open(*port); return;
  }
  if (!strcmp(name, "astro_remote.elf")) {
    *is_service = 1; *port = ASTRO_PORT; *active = 1; return;
  }
  if (!strcmp(name, "nanodns.elf") || !strcmp(name, "Ghostcontrol.elf") || !strcmp(name, "pldmgr.elf")) {
    *is_service = 1; return;
  }
}

static int astro_send_all(int fd, const char *buf, size_t len)
{
  size_t sent = 0;
  while (sent < len) {
    ssize_t n = send(fd, buf + sent, len - sent, 0);
    if (n <= 0) return -1;
    sent += (size_t)n;
  }
  return 0;
}

static int astro_launch_elfldr(const char *path)
{
  int fd, n;
  struct sockaddr_in addr;
  char cmd[ASTRO_PATH_MAX + 16];

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ASTRO_ELFLDR_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -2; }
  n = snprintf(cmd, sizeof(cmd), "file:%s\n", path);
  if (n <= 0 || n >= (int)sizeof(cmd)) { close(fd); return -3; }
  if (astro_send_all(fd, cmd, (size_t)n) != 0) { close(fd); return -4; }
  shutdown(fd, SHUT_WR);
  close(fd);
  return 0;
}

static int astro_extract_form_path(const char *request, char *out, size_t out_size)
{
  const char *body = strstr(request, "\r\n\r\n");
  const char *p;
  char encoded[ASTRO_PATH_MAX * 2];
  size_t i = 0;
  if (!body || !out_size) return 0;
  body += 4;
  p = strstr(body, "path=");
  if (!p) return 0;
  p += 5;
  while (*p && *p != '&' && i + 1 < sizeof(encoded)) encoded[i++] = *p++;
  encoded[i] = '\0';
  url_decode(out, encoded);
  out[out_size - 1] = '\0';
  return out[0] != '\0';
}

static int astro_http_shutdown_prospero(void)
{
  int fd;
  struct sockaddr_in addr;
  const char *req = "POST /api/shutdown HTTP/1.1\r\nHost: 127.0.0.1\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(7070);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -2; }
  if (astro_send_all(fd, req, strlen(req)) != 0) { close(fd); return -3; }
  shutdown(fd, SHUT_WR);
  close(fd);
  return 0;
}

static void send_api_status_v08(int fd)
{
  astro_elf_item_t items[ASTRO_MAX_ELFS];
  int elf_count = astro_collect_elfs(items, ASTRO_MAX_ELFS);
  time_t now = time(NULL);
  long uptime = now >= astro_started_at ? (long)(now - astro_started_at) : 0;
  char json[700];
  snprintf(json, sizeof(json),
    "{\"ok\":true,\"console\":\"PlayStation 5\",\"console_online\":true,\"astro_online\":true,\"http_port\":%d,\"version\":\"0.8-service-cards\",\"uptime_seconds\":%ld,\"remote\":\"standby\",\"elf_count\":%d,\"elfldr_port\":%d}",
    ASTRO_PORT, uptime, elf_count, ASTRO_ELFLDR_PORT);
  send_json(fd, "200 OK", json);
}

static void send_api_elfs_v08(int fd)
{
  astro_elf_item_t items[ASTRO_MAX_ELFS];
  int count = astro_collect_elfs(items, ASTRO_MAX_ELFS);
  char json[32768];
  size_t used = 0;
  int i;

  used += (size_t)snprintf(json + used, sizeof(json) - used,
    "{\"ok\":true,\"count\":%d,\"source\":\"curated-payload-roots\",\"items\":[", count);

  for (i = 0; i < count && used + 700 < sizeof(json); i++) {
    char esc_name[512], esc_path[2048];
    int is_service, port, active, stoppable;
    int launchable = strcmp(items[i].name, "astro_remote.elf") != 0;
    astro_service_meta(items[i].name, &is_service, &port, &active, &stoppable);
    json_escape(esc_name, sizeof(esc_name), items[i].name);
    json_escape(esc_path, sizeof(esc_path), items[i].path);
    used += (size_t)snprintf(json + used, sizeof(json) - used,
      "%s{\"name\":\"%s\",\"path\":\"%s\",\"size\":%lld,\"launchable\":%s,\"service\":%s,\"port\":%d,\"active\":%s,\"stoppable\":%s}",
      i ? "," : "", esc_name, esc_path, items[i].size,
      launchable ? "true" : "false", is_service ? "true" : "false", port,
      active ? "true" : "false", stoppable ? "true" : "false");
  }
  snprintf(json + used, sizeof(json) - used, "]}");
  send_json(fd, "200 OK", json);
}

static void send_api_launch_v08(int fd, const char *request)
{
  char path[ASTRO_PATH_MAX];
  astro_elf_item_t item;
  int rc;
  memset(path, 0, sizeof(path));

  if (!astro_extract_form_path(request, path, sizeof(path))) {
    send_json(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"missing_path\"}"); return;
  }
  if (!astro_find_path(path, &item) || !strcmp(item.name, "astro_remote.elf")) {
    send_json(fd, "403 Forbidden", "{\"ok\":false,\"error\":\"path_not_launchable\"}"); return;
  }
  rc = astro_launch_elfldr(path);
  if (rc != 0) {
    char json[256];
    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"elfldr_failed\",\"code\":%d}", rc);
    send_json(fd, "502 Bad Gateway", json); return;
  }
  send_json(fd, "200 OK", "{\"ok\":true,\"launched\":true}");
}

static void send_api_stop_v08(int fd, const char *request)
{
  char path[ASTRO_PATH_MAX];
  astro_elf_item_t item;
  int rc;
  memset(path, 0, sizeof(path));

  if (!astro_extract_form_path(request, path, sizeof(path))) {
    send_json(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"missing_path\"}"); return;
  }
  if (!astro_find_path(path, &item)) {
    send_json(fd, "404 Not Found", "{\"ok\":false,\"error\":\"payload_not_found\"}"); return;
  }
  if (strcmp(item.name, "ProsperoMgr.elf")) {
    send_json(fd, "409 Conflict", "{\"ok\":false,\"error\":\"stop_not_supported_yet\"}"); return;
  }
  rc = astro_http_shutdown_prospero();
  if (rc != 0) {
    send_json(fd, "502 Bad Gateway", "{\"ok\":false,\"error\":\"shutdown_failed\"}"); return;
  }
  send_json(fd, "200 OK", "{\"ok\":true,\"stopping\":true}");
}

static const char *dashboard_v08_script =
"<style>"
".elf-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;padding:14px}.elf-card{background:#11161d;border:1px solid #29313d;border-radius:9px;padding:14px;min-width:0}.elf-card:hover{border-color:#354253}.elf-card-top{display:flex;align-items:flex-start;justify-content:space-between;gap:10px}.elf-card h4{margin:0;font-size:14px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.elf-card-path{color:#687487;font-size:10px;margin:5px 0 12px;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.elf-meta{display:flex;gap:7px;flex-wrap:wrap;margin-bottom:13px}.elf-chip{font-size:10px;padding:4px 7px;border-radius:5px;background:#192431;border:1px solid #293c50;color:#91bce9}.elf-state{font-size:10px;padding:4px 7px;border-radius:999px;font-weight:800}.elf-state.on{background:#10251b;border:1px solid #24553a;color:#7fe4aa}.elf-state.off{background:#241d12;border:1px solid #554326;color:#e6bc73}.elf-state.neutral{background:#1a2029;border:1px solid #303947;color:#9ba7b6}.elf-actions{display:flex;gap:8px}.elf-action{height:34px;border-radius:7px;border:1px solid #2b3542;background:#171d25;color:#d7dee8;font-weight:750;padding:0 12px;cursor:pointer;flex:1}.elf-action.primary{background:linear-gradient(#2084ff,#1269dc);border-color:#176fdc;color:#fff}.elf-action.stop{background:#351a1d;border-color:#633036;color:#ff9c9c}.elf-action:disabled{opacity:.45;cursor:not-allowed}@media(max-width:900px){.elf-grid{grid-template-columns:1fr}}"
"</style>"
"<script>"
"function fmtBytes(n){if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';return (n/1048576).toFixed(1)+' MB'}"
"async function postAction(url,path,btn){if(btn.dataset.busy==='1')return;const old=btn.textContent;btn.dataset.busy='1';btn.textContent='Aguarde...';try{const r=await fetch(url,{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'path='+encodeURIComponent(path)});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.error||'falha');btn.textContent=url.indexOf('/stop')>0?'Parando...':'Iniciado';setTimeout(astroRefresh,1200)}catch(e){btn.textContent=e.message==='stop_not_supported_yet'?'Parada via Processos em breve':'Falhou';setTimeout(()=>btn.textContent=old,2200)}finally{btn.dataset.busy='0'}}"
"function cardFor(x){const c=document.createElement('div');c.className='elf-card';let state='';if(x.name==='astro_remote.elf')state=\"<span class='elf-state on'>ATIVO</span>\";else if(x.service&&x.port>0)state=x.active?\"<span class='elf-state on'>ATIVO · :\"+x.port+\"</span>\":\"<span class='elf-state off'>PARADO · :\"+x.port+\"</span>\";else if(x.service)state=\"<span class='elf-state neutral'>SERVIÇO</span>\";else state=\"<span class='elf-state neutral'>PAYLOAD</span>\";c.innerHTML=\"<div class='elf-card-top'><h4 title='\"+x.name+\"'>\"+x.name+\"</h4>\"+state+\"</div><div class='elf-card-path' title='\"+x.path+\"'>\"+x.path+\"</div><div class='elf-meta'><span class='elf-chip'>ELF</span><span class='elf-chip'>\"+fmtBytes(x.size)+\"</span></div><div class='elf-actions'></div>\";const a=c.querySelector('.elf-actions');if(x.name==='astro_remote.elf'){const b=document.createElement('button');b.className='elf-action';b.disabled=true;b.textContent='Astro ativo';a.appendChild(b);return c}if(x.service&&x.port>0&&x.active){const b=document.createElement('button');b.className='elf-action stop';b.textContent='Parar';if(!x.stoppable){b.title='Parada segura será ligada ao módulo Processos';b.addEventListener('click',()=>postAction('/api/services/stop',x.path,b))}else b.addEventListener('click',()=>postAction('/api/services/stop',x.path,b));a.appendChild(b)}else{const b=document.createElement('button');b.className='elf-action primary';b.textContent=x.service?'Iniciar':'Executar';b.addEventListener('click',()=>postAction('/api/elfs/launch',x.path,b));a.appendChild(b)}return c}"
"async function astroRefresh(){try{const [sr,er]=await Promise.all([fetch('/api/status',{cache:'no-store'}),fetch('/api/elfs',{cache:'no-store'})]);if(!sr.ok||!er.ok)throw new Error('api');const s=await sr.json(),e=await er.json();const top=document.querySelector('.status');if(top)top.innerHTML=\"<span class='dot'></span>PS5 \"+(s.console_online?'ONLINE':'OFFLINE');const m=document.querySelectorAll('.metric');if(m.length>=4){m[1].querySelector('strong').textContent=String(s.http_port);m[1].querySelector('small').textContent='API '+s.version+' ativa';m[3].querySelector('strong').textContent=String(e.count);m[3].querySelector('small').textContent='payloads e serviços'}const table=document.querySelector('.table');if(table){let grid=table.parentElement.querySelector('.elf-grid');if(!grid){grid=document.createElement('div');grid.className='elf-grid';table.replaceWith(grid)}grid.innerHTML='';e.items.forEach(x=>grid.appendChild(cardFor(x)));if(!e.items.length)grid.innerHTML=\"<div style='color:#8f9aaa'>Nenhum payload encontrado.</div>\"}const h=document.querySelector('.two .section .section-head span');if(h)h.textContent=e.count+' item(ns)';const v=document.querySelector('.version');if(v)v.textContent='Astro Remote '+s.version+' · uptime '+s.uptime_seconds+'s'}catch(err){const top=document.querySelector('.status');if(top)top.textContent='ASTRO API OFFLINE'}}"
"document.addEventListener('DOMContentLoaded',()=>{astroRefresh();const b=[...document.querySelectorAll('button')].find(x=>x.textContent.indexOf('Atualizar status')>=0);if(b)b.addEventListener('click',astroRefresh);setInterval(astroRefresh,8000)});"
"</script>";

static void send_dashboard_v08(int fd)
{
  const char *marker = strstr(dashboard_page, "</body>");
  char header[1024];
  size_t before_len, script_len, after_len, total_len;
  if (!marker) { send_response(fd, "200 OK", NULL, dashboard_page); return; }
  before_len = (size_t)(marker - dashboard_page);
  script_len = strlen(dashboard_v08_script);
  after_len = strlen(marker);
  total_len = before_len + script_len + after_len;
  snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", total_len);
  send(fd, header, strlen(header), 0);
  send(fd, dashboard_page, before_len, 0);
  send(fd, dashboard_v08_script, script_len, 0);
  send(fd, marker, after_len, 0);
}

int main(void)
{
  int server_fd, client_fd, opt = 1;
  struct sockaddr_in addr;
  char buffer[8192];
  running = 1;
  astro_started_at = time(NULL);

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) { notify("ASTRO: socket falhou"); return 1; }
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(ASTRO_PORT);
  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { notify("ASTRO: bind falhou"); close(server_fd); return 1; }
  if (listen(server_fd, 8) < 0) { notify("ASTRO: listen falhou"); close(server_fd); return 1; }
  notify("ASTRO Remote v0.8 service cards - porta 45821");

  while (running) {
    int total; char *header_end;
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) continue;
    memset(buffer, 0, sizeof(buffer));
    total = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (total <= 0) { close(client_fd); continue; }
    buffer[total] = '\0';
    header_end = strstr(buffer, "\r\n\r\n");
    if (header_end) {
      int content_length = 0; char *cl = strstr(buffer, "Content-Length:");
      if (cl) content_length = atoi(cl + strlen("Content-Length:"));
      { int header_size = (int)((header_end + 4) - buffer); int body_received = total - header_size;
        while (body_received < content_length && total < (int)sizeof(buffer) - 1) {
          int received = recv(client_fd, buffer + total, sizeof(buffer) - total - 1, 0);
          if (received <= 0) break;
          total += received; body_received += received; buffer[total] = '\0';
        }
      }
    }

    if (strstr(buffer, "POST /login ") && valid_login(buffer)) send_response(client_fd, "302 Found", "Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n", "");
    else if (strstr(buffer, "POST /logout ") && logged(buffer)) send_response(client_fd, "302 Found", "Set-Cookie: astro_session=; Path=/; Max-Age=0; HttpOnly\r\nLocation: /\r\n", "");
    else if (strstr(buffer, "GET /api/status ") && logged(buffer)) send_api_status_v08(client_fd);
    else if (strstr(buffer, "GET /api/elfs ") && logged(buffer)) send_api_elfs_v08(client_fd);
    else if (strstr(buffer, "POST /api/elfs/launch ") && logged(buffer)) send_api_launch_v08(client_fd, buffer);
    else if (strstr(buffer, "POST /api/services/stop ") && logged(buffer)) send_api_stop_v08(client_fd, buffer);
    else if (strstr(buffer, "POST /admin/shutdown ") && logged(buffer)) { send_response(client_fd, "200 OK", NULL, shutdown_page); running = 0; notify("ASTRO Remote encerrando"); }
    else if (strstr(buffer, "GET / ") && logged(buffer)) send_dashboard_v08(client_fd);
    else send_response(client_fd, "200 OK", NULL, login_page);
    close(client_fd);
  }

  close(server_fd);
  notify("ASTRO Remote encerrado");
  return 0;
}
