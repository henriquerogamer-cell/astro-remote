#include <time.h>

#define main astro_v03_legacy_main
#include "main_v03.c"
#undef main

#define PROSPERO_PORT 7070
#define ASTRO_HTTP_BUF 32768

static time_t astro_started_at;

static void send_json(int fd, const char *status, const char *body)
{
  char header[1024];
  snprintf(header, sizeof(header),
    "HTTP/1.1 %s\r\n"
    "Content-Type: application/json; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n\r\n",
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
  int fd;
  struct sockaddr_in addr;
  char req[1024];
  size_t used = 0;
  int n;

  if (!out || out_size < 2) return -1;
  out[0] = '\0';

  fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) return -2;

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

  if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    close(fd);
    return -3;
  }

  n = snprintf(req, sizeof(req),
    "GET %s HTTP/1.1\r\n"
    "Host: 127.0.0.1:%d\r\n"
    "Connection: close\r\n\r\n",
    path, port);

  if (n <= 0 || n >= (int)sizeof(req)) {
    close(fd);
    return -4;
  }

  if (send(fd, req, (size_t)n, 0) <= 0) {
    close(fd);
    return -5;
  }

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
  if (body) body += 4;
  else body = http;
  return strstr(body, needle) != NULL;
}

static void append_service(
  char *json,
  size_t json_size,
  size_t *used,
  int *count,
  const char *id,
  const char *name,
  const char *process,
  int port
)
{
  int active = astro_tcp_open(port);
  if (!active) return;

  *used += (size_t)snprintf(
    json + *used,
    json_size - *used,
    "%s{\"id\":\"%s\",\"name\":\"%s\",\"process\":\"%s\",\"port\":%d,\"active\":true,\"web\":true}",
    *count ? "," : "",
    id,
    name,
    process,
    port
  );
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
    /* Prospero itself is the payload manager and exposes its own web UI. */
    append_service(json, sizeof(json), &used, &count,
      "prospero", "Prospero Manager", "ProsperoMgr.elf", 7070);

    if (body_contains(prospero, "pegasus") || body_contains(prospero, "Pegasus")) {
      append_service(json, sizeof(json), &used, &count,
        "pegasus", "Pegasus DL", "pegasus_dl.elf", 6970);
    }

    if (body_contains(prospero, "websrv.elf") || body_contains(prospero, "websrv-ps5.elf")) {
      append_service(json, sizeof(json), &used, &count,
        "websrv", "WebSrv", "websrv.elf", 8080);
    }
  }

  snprintf(json + used, sizeof(json) - used, "],\"count\":%d}", count);
  send_json(fd, "200 OK", json);
}

static void send_api_status_v09(int fd)
{
  char json[640];
  time_t now = time(NULL);
  long uptime = now >= astro_started_at ? (long)(now - astro_started_at) : 0;
  int prospero_online = astro_tcp_open(PROSPERO_PORT);

  snprintf(json, sizeof(json),
    "{\"ok\":true,\"console\":\"PlayStation 5\",\"console_online\":true,\"astro_online\":true,\"http_port\":%d,\"version\":\"0.9-prospero-services\",\"uptime_seconds\":%ld,\"remote\":\"standby\",\"prospero_online\":%s,\"prospero_port\":%d}",
    ASTRO_PORT, uptime, prospero_online ? "true" : "false", PROSPERO_PORT);

  send_json(fd, "200 OK", json);
}

static const char *dashboard_v09_script =
"<style>"
".service-grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px;padding:14px}.service-card{background:#11161d;border:1px solid #29313d;border-radius:9px;padding:14px;min-width:0;cursor:pointer;transition:.15s}.service-card:hover{border-color:#176fdc;transform:translateY(-1px)}.service-top{display:flex;align-items:flex-start;justify-content:space-between;gap:10px}.service-card h4{margin:0;font-size:14px}.service-process{color:#687487;font-size:10px;margin:5px 0 12px}.service-state{font-size:10px;padding:4px 7px;border-radius:999px;font-weight:800;background:#10251b;border:1px solid #24553a;color:#7fe4aa}.service-meta{display:flex;gap:7px;flex-wrap:wrap}.service-chip{font-size:10px;padding:4px 7px;border-radius:5px;background:#192431;border:1px solid #293c50;color:#91bce9}.service-open{margin-top:13px;height:34px;border-radius:7px;border:1px solid #176fdc;background:linear-gradient(#2084ff,#1269dc);color:#fff;font-weight:750;width:100%;cursor:pointer}@media(max-width:900px){.service-grid{grid-template-columns:1fr}}"
"</style>"
"<script>"
"function serviceUrl(x){return location.protocol+'//'+location.hostname+':'+x.port+'/'}"
"function cardForService(x){const c=document.createElement('div');c.className='service-card';c.innerHTML=\"<div class='service-top'><h4>\"+x.name+\"</h4><span class='service-state'>ATIVO</span></div><div class='service-process'>\"+x.process+\"</div><div class='service-meta'><span class='service-chip'>WEB</span><span class='service-chip'>porta \"+x.port+\"</span></div><button class='service-open'>Abrir serviço</button>\";const open=()=>window.open(serviceUrl(x),'_blank');c.querySelector('.service-open').addEventListener('click',e=>{e.stopPropagation();open()});c.addEventListener('click',open);return c}"
"async function astroRefresh(){try{const [sr,vr]=await Promise.all([fetch('/api/status',{cache:'no-store'}),fetch('/api/services',{cache:'no-store'})]);if(!sr.ok||!vr.ok)throw new Error('api');const s=await sr.json(),v=await vr.json();const top=document.querySelector('.status');if(top)top.innerHTML=\"<span class='dot'></span>PS5 \"+(s.console_online?'ONLINE':'OFFLINE');const m=document.querySelectorAll('.metric');if(m.length>=4){m[0].querySelector('strong').textContent=s.console;m[0].querySelector('small').textContent='conectado ao Astro';m[1].querySelector('strong').textContent=String(s.http_port);m[1].querySelector('small').textContent='API '+s.version+' ativa';m[2].querySelector('strong').textContent='Standby';m[2].querySelector('small').textContent='Remote ainda em desenvolvimento';m[3].querySelector('strong').textContent=String(v.count);m[3].querySelector('small').textContent='serviços web ativos'}const section=document.querySelector('.two .section');if(section){const head=section.querySelector('.section-head');if(head){head.querySelector('h3').textContent='Serviços ativos';head.querySelector('span').textContent=v.manager_online?'gerenciados pelo Próspero':'Próspero offline'}let grid=section.querySelector('.service-grid');if(!grid){const old=section.querySelector('.table');if(old)old.remove();grid=document.createElement('div');grid.className='service-grid';section.appendChild(grid)}grid.innerHTML='';v.items.forEach(x=>grid.appendChild(cardForService(x)));if(!v.items.length)grid.innerHTML=\"<div style='color:#8f9aaa;padding:8px'>Nenhum serviço web ativo detectado.</div>\"}const ver=document.querySelector('.version');if(ver)ver.textContent='Astro Remote '+s.version+' · uptime '+s.uptime_seconds+'s'}catch(err){const top=document.querySelector('.status');if(top)top.textContent='ASTRO API OFFLINE'}}"
"document.addEventListener('DOMContentLoaded',()=>{astroRefresh();const b=[...document.querySelectorAll('button')].find(x=>x.textContent.indexOf('Atualizar status')>=0);if(b)b.addEventListener('click',astroRefresh);setInterval(astroRefresh,8000)});"
"</script>";

static void send_dashboard_v09(int fd)
{
  const char *marker = strstr(dashboard_page, "</body>");
  char header[1024];
  size_t before_len, script_len, after_len, total_len;

  if (!marker) {
    send_response(fd, "200 OK", NULL, dashboard_page);
    return;
  }

  before_len = (size_t)(marker - dashboard_page);
  script_len = strlen(dashboard_v09_script);
  after_len = strlen(marker);
  total_len = before_len + script_len + after_len;

  snprintf(header, sizeof(header),
    "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
    total_len);

  send(fd, header, strlen(header), 0);
  send(fd, dashboard_page, before_len, 0);
  send(fd, dashboard_v09_script, script_len, 0);
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

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    notify("ASTRO: bind falhou");
    close(server_fd);
    return 1;
  }

  if (listen(server_fd, 8) < 0) {
    notify("ASTRO: listen falhou");
    close(server_fd);
    return 1;
  }

  notify("ASTRO Remote v0.9 Prospero services - porta 45821");

  while (running) {
    int total;
    char *header_end;

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

    if (strstr(buffer, "POST /login ") && valid_login(buffer)) {
      send_response(client_fd, "302 Found", "Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n", "");
    } else if (strstr(buffer, "POST /logout ") && logged(buffer)) {
      send_response(client_fd, "302 Found", "Set-Cookie: astro_session=; Path=/; Max-Age=0; HttpOnly\r\nLocation: /\r\n", "");
    } else if (strstr(buffer, "GET /api/status ") && logged(buffer)) {
      send_api_status_v09(client_fd);
    } else if (strstr(buffer, "GET /api/services ") && logged(buffer)) {
      send_api_services(client_fd);
    } else if (strstr(buffer, "POST /admin/shutdown ") && logged(buffer)) {
      send_response(client_fd, "200 OK", NULL, shutdown_page);
      running = 0;
      notify("ASTRO Remote encerrando");
    } else if (strstr(buffer, "GET / ") && logged(buffer)) {
      send_dashboard_v09(client_fd);
    } else {
      send_response(client_fd, "200 OK", NULL, login_page);
    }

    close(client_fd);
  }

  close(server_fd);
  notify("ASTRO Remote encerrado");
  return 0;
}
