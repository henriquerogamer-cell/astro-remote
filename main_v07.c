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

static int already_has_path(astro_elf_item_t *items, int count, const char *path)
{
  int i;
  for (i = 0; i < count; i++) if (!strcmp(items[i].path, path)) return 1;
  return 0;
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

    if (S_ISREG(st.st_mode) && has_elf_suffix(entry->d_name) && !already_has_path(items, count, full)) {
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

static int astro_path_is_launchable(const char *path)
{
  astro_elf_item_t items[ASTRO_MAX_ELFS];
  int count = astro_collect_elfs(items, ASTRO_MAX_ELFS);
  int i;

  for (i = 0; i < count; i++) {
    if (!strcmp(items[i].path, path)) {
      if (!strcmp(items[i].name, "astro_remote.elf")) return 0;
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
  int fd;
  struct sockaddr_in addr;
  char cmd[ASTRO_PATH_MAX + 16];
  int n;

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -1;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(ASTRO_ELFLDR_PORT);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -2;
  }

  n = snprintf(cmd, sizeof(cmd), "file:%s\n", path);
  if (n <= 0 || n >= (int)sizeof(cmd)) {
    close(fd);
    return -3;
  }

  if (astro_send_all(fd, cmd, (size_t)n) != 0) {
    close(fd);
    return -4;
  }

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

static void send_api_status_v07(int fd)
{
  astro_elf_item_t items[ASTRO_MAX_ELFS];
  int elf_count = astro_collect_elfs(items, ASTRO_MAX_ELFS);
  time_t now = time(NULL);
  long uptime = now >= astro_started_at ? (long)(now - astro_started_at) : 0;
  char json[700];

  snprintf(json, sizeof(json),
    "{\"ok\":true,\"console\":\"PlayStation 5\",\"console_online\":true,\"astro_online\":true,\"http_port\":%d,\"version\":\"0.7-elf-launch\",\"uptime_seconds\":%ld,\"remote\":\"standby\",\"elf_count\":%d,\"elf_count_source\":\"payload-roots\",\"elfldr_port\":%d}",
    ASTRO_PORT, uptime, elf_count, ASTRO_ELFLDR_PORT);
  send_json(fd, "200 OK", json);
}

static void send_api_elfs_v07(int fd)
{
  astro_elf_item_t items[ASTRO_MAX_ELFS];
  int count = astro_collect_elfs(items, ASTRO_MAX_ELFS);
  char json[32768];
  size_t used = 0;
  int i;

  used += (size_t)snprintf(json + used, sizeof(json) - used,
    "{\"ok\":true,\"count\":%d,\"source\":\"curated-payload-roots\",\"items\":[", count);

  for (i = 0; i < count && used + 512 < sizeof(json); i++) {
    char esc_name[512], esc_path[2048];
    json_escape(esc_name, sizeof(esc_name), items[i].name);
    json_escape(esc_path, sizeof(esc_path), items[i].path);
    used += (size_t)snprintf(json + used, sizeof(json) - used,
      "%s{\"name\":\"%s\",\"path\":\"%s\",\"size\":%lld,\"launchable\":%s}",
      i ? "," : "", esc_name, esc_path, items[i].size,
      !strcmp(items[i].name, "astro_remote.elf") ? "false" : "true");
  }

  snprintf(json + used, sizeof(json) - used, "]}");
  send_json(fd, "200 OK", json);
}

static void send_api_launch(int fd, const char *request)
{
  char path[ASTRO_PATH_MAX];
  char esc_path[ASTRO_PATH_MAX * 2];
  char json[ASTRO_PATH_MAX * 2 + 256];
  int rc;

  memset(path, 0, sizeof(path));
  if (!astro_extract_form_path(request, path, sizeof(path))) {
    send_json(fd, "400 Bad Request", "{\"ok\":false,\"error\":\"missing_path\"}");
    return;
  }

  if (!astro_path_is_launchable(path)) {
    send_json(fd, "403 Forbidden", "{\"ok\":false,\"error\":\"path_not_launchable\"}");
    return;
  }

  rc = astro_launch_elfldr(path);
  json_escape(esc_path, sizeof(esc_path), path);

  if (rc != 0) {
    snprintf(json, sizeof(json), "{\"ok\":false,\"error\":\"elfldr_failed\",\"code\":%d,\"path\":\"%s\"}", rc, esc_path);
    send_json(fd, "502 Bad Gateway", json);
    return;
  }

  snprintf(json, sizeof(json), "{\"ok\":true,\"launched\":true,\"via\":\"elfldr\",\"port\":%d,\"path\":\"%s\"}", ASTRO_ELFLDR_PORT, esc_path);
  send_json(fd, "200 OK", json);
}

static const char *dashboard_v07_script =
"<script>"
"function fmtBytes(n){if(n<1024)return n+' B';if(n<1048576)return (n/1024).toFixed(1)+' KB';return (n/1048576).toFixed(1)+' MB'}"
"async function launchElf(path,cell){if(!cell||cell.dataset.busy==='1')return;const old=cell.textContent;cell.dataset.busy='1';cell.textContent='Iniciando...';try{const r=await fetch('/api/elfs/launch',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body:'path='+encodeURIComponent(path)});const j=await r.json();if(!r.ok||!j.ok)throw new Error(j.error||'falha');cell.textContent='Enviado';setTimeout(()=>cell.textContent='Executar',1800)}catch(e){cell.textContent='Falhou';setTimeout(()=>cell.textContent=old,2200)}finally{cell.dataset.busy='0'}}"
"async function astroRefresh(){try{const [sr,er]=await Promise.all([fetch('/api/status',{cache:'no-store'}),fetch('/api/elfs',{cache:'no-store'})]);if(!sr.ok||!er.ok)throw new Error('api');const s=await sr.json(),e=await er.json();const top=document.querySelector('.status');if(top)top.innerHTML=\"<span class='dot'></span>PS5 \"+(s.console_online?'ONLINE':'OFFLINE');const m=document.querySelectorAll('.metric');if(m.length>=4){m[0].querySelector('strong').textContent=s.console;m[1].querySelector('strong').textContent=String(s.http_port);m[1].querySelector('small').textContent='API '+s.version+' ativa';m[2].querySelector('strong').textContent='Standby';m[3].querySelector('strong').textContent=String(e.count);m[3].querySelector('small').textContent='payloads de usuario'}const tb=document.querySelector('.table tbody');if(tb){tb.innerHTML='';e.items.forEach(x=>{const tr=document.createElement('tr');const action=x.launchable?'Executar':'Astro ativo';tr.innerHTML=\"<td><b>\"+x.name+\"</b><br><small style='color:#657183'>\"+x.path+\"</small></td><td><span class='type'>ELF</span></td><td>\"+fmtBytes(x.size)+\"</td><td class='run'>\"+action+\"</td>\";const cell=tr.querySelector('.run');if(x.launchable){cell.style.cursor='pointer';cell.addEventListener('click',()=>launchElf(x.path,cell))}tb.appendChild(tr)});if(!e.items.length)tb.innerHTML=\"<tr><td colspan='4'>Nenhum payload encontrado.</td></tr>\";}const h=document.querySelector('.two .section .section-head span');if(h)h.textContent=e.count+' payload(s)';const v=document.querySelector('.version');if(v)v.textContent='Astro Remote '+s.version+' · elfldr '+s.elfldr_port+' · uptime '+s.uptime_seconds+'s';}catch(err){const top=document.querySelector('.status');if(top)top.textContent='ASTRO API OFFLINE'}}"
"document.addEventListener('DOMContentLoaded',()=>{astroRefresh();const b=[...document.querySelectorAll('button')].find(x=>x.textContent.indexOf('Atualizar status')>=0);if(b)b.addEventListener('click',astroRefresh);setInterval(astroRefresh,10000)});"
"</script>";

static void send_dashboard_v07(int fd)
{
  const char *marker = strstr(dashboard_page, "</body>");
  char header[1024];
  size_t before_len, script_len, after_len, total_len;
  if (!marker) { send_response(fd, "200 OK", NULL, dashboard_page); return; }
  before_len = (size_t)(marker - dashboard_page);
  script_len = strlen(dashboard_v07_script);
  after_len = strlen(marker);
  total_len = before_len + script_len + after_len;
  snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", total_len);
  send(fd, header, strlen(header), 0);
  send(fd, dashboard_page, before_len, 0);
  send(fd, dashboard_v07_script, script_len, 0);
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
  notify("ASTRO Remote v0.7 ELF launch - porta 45821");

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
    else if (strstr(buffer, "GET /api/status ") && logged(buffer)) send_api_status_v07(client_fd);
    else if (strstr(buffer, "GET /api/elfs ") && logged(buffer)) send_api_elfs_v07(client_fd);
    else if (strstr(buffer, "POST /api/elfs/launch ") && logged(buffer)) send_api_launch(client_fd, buffer);
    else if (strstr(buffer, "POST /admin/shutdown ") && logged(buffer)) { send_response(client_fd, "200 OK", NULL, shutdown_page); running = 0; notify("ASTRO Remote encerrando"); }
    else if (strstr(buffer, "GET / ") && logged(buffer)) send_dashboard_v07(client_fd);
    else send_response(client_fd, "200 OK", NULL, login_page);
    close(client_fd);
  }

  close(server_fd);
  notify("ASTRO Remote encerrado");
  return 0;
}
