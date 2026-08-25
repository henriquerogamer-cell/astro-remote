#include <time.h>

#define main astro_v03_legacy_main
#include "main_v03.c"
#undef main

#define PROSPERO_PORT 7070
#define ASTRO_HTTP_BUF 32768
#define ASTRO_PROXY_PATH_MAX 2048

typedef struct astro_service {
  const char *id;
  const char *name;
  const char *process;
  int port;
} astro_service_t;

static const astro_service_t astro_services[] = {
  {"prospero", "Prospero Manager", "ProsperoMgr.elf", 7070},
  {"pegasus", "Pegasus DL", "pegasus_dl.elf", 6970},
  {"websrv", "WebSrv", "websrv.elf", 8080}
};

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

static int astro_tcp_open(int port)
{
  int fd, rc;
  struct sockaddr_in addr;
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

static int astro_http_get_local(int port, const char *path, char *out, size_t out_size)
{
  int fd, n;
  struct sockaddr_in addr;
  char req[1024];
  size_t used = 0;

  if (!out || out_size < 2) return -1;
  out[0] = '\0';
  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -2;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(fd); return -3; }

  n = snprintf(req, sizeof(req),
    "GET %s HTTP/1.1\r\nHost: 127.0.0.1:%d\r\nConnection: close\r\n\r\n",
    path, port);
  if (n <= 0 || n >= (int)sizeof(req)) { close(fd); return -4; }
  if (send(fd, req, (size_t)n, 0) <= 0) { close(fd); return -5; }

  while (used + 1 < out_size) {
    ssize_t r = recv(fd, out + used, out_size - used - 1, 0);
    if (r <= 0) break;
    used += (size_t)r;
  }
  out[used] = '\0';
  close(fd);
  return used > 0 ? 0 : -6;
}

static int body_contains(const char *http, const char *needle)
{
  const char *body;
  if (!http || !needle) return 0;
  body = strstr(http, "\r\n\r\n");
  body = body ? body + 4 : http;
  return strstr(body, needle) != NULL;
}

static const astro_service_t *service_by_id(const char *id)
{
  size_t i;
  for (i = 0; i < sizeof(astro_services) / sizeof(astro_services[0]); i++) {
    if (!strcmp(astro_services[i].id, id)) return &astro_services[i];
  }
  return NULL;
}

static const astro_service_t *service_from_cookie(const char *request)
{
  const char *p = strstr(request, "astro_proxy=");
  char id[32];
  size_t i = 0;
  if (!p) return NULL;
  p += strlen("astro_proxy=");
  while (*p && *p != ';' && *p != '\r' && *p != '\n' && i + 1 < sizeof(id)) id[i++] = *p++;
  id[i] = '\0';
  return service_by_id(id);
}

static const astro_service_t *service_from_path(const char *path, const char **backend_path)
{
  size_t i;
  for (i = 0; i < sizeof(astro_services) / sizeof(astro_services[0]); i++) {
    char prefix[64];
    size_t len;
    snprintf(prefix, sizeof(prefix), "/service/%s", astro_services[i].id);
    len = strlen(prefix);
    if (!strncmp(path, prefix, len) && (path[len] == '/' || path[len] == '\0' || path[len] == '?')) {
      const char *p = path + len;
      if (*p == '\0') *backend_path = "/";
      else if (*p == '?') {
        static char root_query[ASTRO_PROXY_PATH_MAX];
        snprintf(root_query, sizeof(root_query), "/%s", p);
        *backend_path = root_query;
      } else *backend_path = p;
      return &astro_services[i];
    }
  }
  return NULL;
}

static void send_proxy_redirect(int fd, const astro_service_t *svc)
{
  char h[512];
  snprintf(h, sizeof(h),
    "HTTP/1.1 302 Found\r\n"
    "Set-Cookie: astro_proxy=%s; Path=/; HttpOnly; SameSite=Lax\r\n"
    "Location: /service/%s/_root\r\n"
    "Content-Length: 0\r\nConnection: close\r\n\r\n",
    svc->id, svc->id);
  send(fd, h, strlen(h), 0);
}

static int proxy_request(int client_fd, const char *request, int request_len, const astro_service_t *svc, const char *backend_path)
{
  int upstream_fd;
  struct sockaddr_in addr;
  char method[16], old_path[ASTRO_PROXY_PATH_MAX], version[16];
  char head[ASTRO_HTTP_BUF];
  char buf[8192];
  const char *header_end;
  const char *body;
  int body_len;
  int n;

  if (sscanf(request, "%15s %2047s %15s", method, old_path, version) != 3) return -1;
  header_end = strstr(request, "\r\n\r\n");
  if (!header_end) return -2;
  body = header_end + 4;
  body_len = request_len - (int)(body - request);
  if (body_len < 0) body_len = 0;

  upstream_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (upstream_fd < 0) return -3;
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(svc->port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (connect(upstream_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { close(upstream_fd); return -4; }

  n = snprintf(head, sizeof(head), "%s %s %s\r\nHost: 127.0.0.1:%d\r\n", method, backend_path, version, svc->port);
  if (n <= 0 || n >= (int)sizeof(head)) { close(upstream_fd); return -5; }

  {
    const char *line = strstr(request, "\r\n");
    size_t used = (size_t)n;
    if (!line) { close(upstream_fd); return -6; }
    line += 2;
    while (line < header_end) {
      const char *eol = strstr(line, "\r\n");
      size_t ll;
      if (!eol || eol > header_end) break;
      ll = (size_t)(eol - line);
      if (ll > 0 && strncasecmp(line, "Host:", 5) && strncasecmp(line, "Connection:", 11)) {
        if (used + ll + 2 >= sizeof(head)) { close(upstream_fd); return -7; }
        memcpy(head + used, line, ll);
        used += ll;
        memcpy(head + used, "\r\n", 2);
        used += 2;
      }
      line = eol + 2;
    }
    if (used + 21 >= sizeof(head)) { close(upstream_fd); return -8; }
    memcpy(head + used, "Connection: close\r\n\r\n", 21);
    used += 21;
    if (send(upstream_fd, head, used, 0) <= 0) { close(upstream_fd); return -9; }
  }

  if (body_len > 0 && send(upstream_fd, body, (size_t)body_len, 0) <= 0) { close(upstream_fd); return -10; }

  while (1) {
    ssize_t r = recv(upstream_fd, buf, sizeof(buf), 0);
    if (r <= 0) break;
    if (send(client_fd, buf, (size_t)r, 0) <= 0) break;
  }
  close(upstream_fd);
  return 0;
}

static void append_service(char *json, size_t json_size, size_t *used, int *count,
  const char *id, const char *name, const char *process, int port)
{
  if (!astro_tcp_open(port)) return;
  *used += (size_t)snprintf(json + *used, json_size - *used,
    "%s{\"id\":\"%s\",\"name\":\"%s\",\"process\":\"%s\",\"port\":%d,\"active\":true,\"web\":true,\"url\":\"/service/%s/\"}",
    *count ? "," : "", id, name, process, port, id);
  (*count)++;
}

static void send_api_services(int fd)
{
  char prospero[ASTRO_HTTP_BUF];
  char json[8192];
  size_t used = 0;
  int count = 0;
  int prospero_ok = astro_http_get_local(PROSPERO_PORT, "/api/processes", prospero, sizeof(prospero)) == 0;

  used += (size_t)snprintf(json + used, sizeof(json) - used,
    "{\"ok\":true,\"manager\":\"ProsperoMgr\",\"manager_online\":%s,\"source\":\"/api/processes\",\"items\":[",
    prospero_ok ? "true" : "false");

  if (prospero_ok) {
    append_service(json, sizeof(json), &used, &count, "prospero", "Prospero Manager", "ProsperoMgr.elf", 7070);
    if (body_contains(prospero, "pegasus") || body_contains(prospero, "Pegasus"))
      append_service(json, sizeof(json), &used, &count, "pegasus", "Pegasus DL", "pegasus_dl.elf", 6970);
    if (body_contains(prospero, "websrv.elf") || body_contains(prospero, "websrv-ps5.elf"))
      append_service(json, sizeof(json), &used, &count, "websrv", "WebSrv", "websrv.elf", 8080);
  }

  snprintf(json + used, sizeof(json) - used, "],\"count\":%d}", count);
  send_json(fd, "200 OK", json);
}

static void send_api_status_v10(int fd)
{
  char json[640];
  time_t now = time(NULL);
  long uptime = now >= astro_started_at ? (long)(now - astro_started_at) : 0;
  snprintf(json, sizeof(json),
    "{\"ok\":true,\"console\":\"PlayStation 5\",\"console_online\":true,\"astro_online\":true,\"http_port\":%d,\"version\":\"0.10-internal-proxy\",\"uptime_seconds\":%ld,\"remote\":\"standby\",\"prospero_online\":%s}",
    ASTRO_PORT, uptime, astro_tcp_open(PROSPERO_PORT) ? "true" : "false");
  send_json(fd, "200 OK", json);
}

static const char *dashboard_v10_script =
"<style>.service-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;padding:14px}.service-card{background:#11161d;border:1px solid #29313d;border-radius:9px;padding:14px;min-width:0;cursor:pointer}.service-card:hover{border-color:#176fdc}.service-top{display:flex;justify-content:space-between;gap:10px}.service-card h4{margin:0;font-size:14px}.service-process{color:#687487;font-size:10px;margin:5px 0 12px}.service-state{font-size:10px;padding:4px 7px;border-radius:999px;font-weight:800;background:#10251b;border:1px solid #24553a;color:#7fe4aa}.service-chip{font-size:10px;padding:4px 7px;border-radius:5px;background:#192431;border:1px solid #293c50;color:#91bce9}.service-open{margin-top:13px;height:34px;border-radius:7px;border:1px solid #176fdc;background:linear-gradient(#2084ff,#1269dc);color:#fff;font-weight:750;width:100%;cursor:pointer}@media(max-width:900px){.service-grid{grid-template-columns:1fr}}</style>"
"<script>"
"function cardForService(x){const c=document.createElement('div');c.className='service-card';c.innerHTML=\"<div class='service-top'><h4>\"+x.name+\"</h4><span class='service-state'>ATIVO</span></div><div class='service-process'>\"+x.process+\"</div><span class='service-chip'>INTERNO · 127.0.0.1:\"+x.port+\"</span><button class='service-open'>Abrir dentro do Astro</button>\";const go=()=>{location.href=x.url};c.querySelector('.service-open').addEventListener('click',e=>{e.stopPropagation();go()});c.addEventListener('click',go);return c}"
"async function astroRefresh(){try{const [sr,vr]=await Promise.all([fetch('/api/status',{cache:'no-store'}),fetch('/api/services',{cache:'no-store'})]);const s=await sr.json(),v=await vr.json();const top=document.querySelector('.status');if(top)top.innerHTML=\"<span class='dot'></span>PS5 ONLINE\";const m=document.querySelectorAll('.metric');if(m.length>=4){m[1].querySelector('strong').textContent=String(s.http_port);m[1].querySelector('small').textContent='API '+s.version+' ativa';m[3].querySelector('strong').textContent=String(v.count);m[3].querySelector('small').textContent='serviços web ativos'}const section=document.querySelector('.two .section');if(section){const head=section.querySelector('.section-head');if(head){head.querySelector('h3').textContent='Serviços ativos';head.querySelector('span').textContent=v.manager_online?'via Próspero':'Próspero offline'}let grid=section.querySelector('.service-grid');if(!grid){const old=section.querySelector('.table');if(old)old.remove();grid=document.createElement('div');grid.className='service-grid';section.appendChild(grid)}grid.innerHTML='';v.items.forEach(x=>grid.appendChild(cardForService(x)));if(!v.items.length)grid.innerHTML=\"<div style='color:#8f9aaa'>Nenhum serviço web ativo.</div>\"}}catch(e){const top=document.querySelector('.status');if(top)top.textContent='ASTRO API OFFLINE'}}"
"document.addEventListener('DOMContentLoaded',()=>{astroRefresh();setInterval(astroRefresh,8000)});"
"</script>";

static void send_dashboard_v10(int fd)
{
  const char *marker = strstr(dashboard_page, "</body>");
  char header[1024];
  size_t before_len, script_len, after_len, total_len;
  if (!marker) { send_response(fd, "200 OK", NULL, dashboard_page); return; }
  before_len = (size_t)(marker - dashboard_page);
  script_len = strlen(dashboard_v10_script);
  after_len = strlen(marker);
  total_len = before_len + script_len + after_len;
  snprintf(header, sizeof(header), "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n", total_len);
  send(fd, header, strlen(header), 0);
  send(fd, dashboard_page, before_len, 0);
  send(fd, dashboard_v10_script, script_len, 0);
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
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(ASTRO_PORT);
  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { notify("ASTRO: bind falhou"); close(server_fd); return 1; }
  if (listen(server_fd, 8) < 0) { notify("ASTRO: listen falhou"); close(server_fd); return 1; }
  notify("ASTRO Remote v0.10 internal proxy - porta 45821");

  while (running) {
    int total;
    char method[16], path[ASTRO_PROXY_PATH_MAX], version[16];
    char *header_end;
    const astro_service_t *svc;
    const char *backend_path = NULL;

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) continue;
    memset(buffer, 0, sizeof(buffer));
    total = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (total <= 0) { close(client_fd); continue; }
    buffer[total] = '\0';

    header_end = strstr(buffer, "\r\n\r\n");
    if (header_end) {
      int content_length = 0;
      char *cl = strstr(buffer, "Content-Length:");
      if (cl) content_length = atoi(cl + strlen("Content-Length:"));
      {
        int header_size = (int)((header_end + 4) - buffer);
        int body_received = total - header_size;
        while (body_received < content_length && total < (int)sizeof(buffer) - 1) {
          int received = recv(client_fd, buffer + total, sizeof(buffer) - total - 1, 0);
          if (received <= 0) break;
          total += received;
          body_received += received;
          buffer[total] = '\0';
        }
      }
    }

    method[0] = path[0] = version[0] = '\0';
    sscanf(buffer, "%15s %2047s %15s", method, path, version);

    svc = service_from_path(path, &backend_path);
    if (svc && logged(buffer)) {
      char marker[64];
      snprintf(marker, sizeof(marker), "/service/%s/_root", svc->id);
      if (!strncmp(path, marker, strlen(marker))) {
        const char *rest = path + strlen(marker);
        char root_path[ASTRO_PROXY_PATH_MAX];
        snprintf(root_path, sizeof(root_path), "/%s", *rest == '/' ? rest + 1 : rest);
        proxy_request(client_fd, buffer, total, svc, root_path);
      } else {
        send_proxy_redirect(client_fd, svc);
      }
      close(client_fd);
      continue;
    }

    if (strstr(buffer, "POST /login ") && valid_login(buffer)) {
      send_response(client_fd, "302 Found", "Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n", "");
    } else if (strstr(buffer, "POST /logout ") && logged(buffer)) {
      send_response(client_fd, "302 Found", "Set-Cookie: astro_session=; Path=/; Max-Age=0; HttpOnly\r\nSet-Cookie: astro_proxy=; Path=/; Max-Age=0; HttpOnly\r\nLocation: /\r\n", "");
    } else if (strstr(buffer, "GET /api/status ") && logged(buffer)) {
      send_api_status_v10(client_fd);
    } else if (strstr(buffer, "GET /api/services ") && logged(buffer)) {
      send_api_services(client_fd);
    } else if (strstr(buffer, "POST /admin/shutdown ") && logged(buffer)) {
      send_response(client_fd, "200 OK", NULL, shutdown_page);
      running = 0;
    } else if (strstr(buffer, "GET / ") && logged(buffer)) {
      send_dashboard_v10(client_fd);
    } else if (logged(buffer) && (svc = service_from_cookie(buffer)) != NULL) {
      proxy_request(client_fd, buffer, total, svc, path);
    } else {
      send_response(client_fd, "200 OK", NULL, login_page);
    }

    close(client_fd);
  }

  close(server_fd);
  notify("ASTRO Remote encerrado");
  return 0;
}
