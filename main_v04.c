#include <time.h>

/*
 * Astro Remote v0.4 API bootstrap.
 *
 * Reuse the already-tested v0.3 HTTP/auth/UI implementation in the same
 * translation unit, rename its main(), and provide the new router below.
 * This lets us evolve the backend without destabilizing the approved UI.
 */
#define main astro_v03_legacy_main
#include "main_v03.c"
#undef main

static time_t astro_started_at;

static void send_json(int fd, const char *status, const char *body)
{
  char header[1024];

  snprintf(
    header,
    sizeof(header),
    "HTTP/1.1 %s\r\n"
    "Content-Type: application/json; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n"
    "\r\n",
    status,
    strlen(body)
  );

  send(fd, header, strlen(header), 0);
  send(fd, body, strlen(body), 0);
}

static void send_api_status(int fd)
{
  char json[512];
  time_t now = time(NULL);
  long uptime = 0;

  if (now >= astro_started_at) {
    uptime = (long)(now - astro_started_at);
  }

  snprintf(
    json,
    sizeof(json),
    "{"
      "\"ok\":true,"
      "\"console\":\"PlayStation 5\","
      "\"console_online\":true,"
      "\"astro_online\":true,"
      "\"http_port\":%d,"
      "\"version\":\"0.4-api\","
      "\"uptime_seconds\":%ld,"
      "\"remote\":\"standby\","
      "\"elf_count\":4,"
      "\"elf_count_source\":\"mock\""
    "}",
    ASTRO_PORT,
    uptime
  );

  send_json(fd, "200 OK", json);
}

static const char *dashboard_live_script =
"<script>"
"async function astroRefresh(){"
"try{"
"const r=await fetch('/api/status',{cache:'no-store'});"
"if(!r.ok)throw new Error('status '+r.status);"
"const s=await r.json();"
"const top=document.querySelector('.status');"
"if(top)top.innerHTML=\"<span class='dot'></span>PS5 \"+(s.console_online?'ONLINE':'OFFLINE');"
"const m=document.querySelectorAll('.metric');"
"if(m.length>=4){"
"m[0].querySelector('strong').textContent=s.console;"
"m[0].querySelector('small').textContent=s.console_online?'conectado ao Astro':'offline';"
"m[1].querySelector('strong').textContent=String(s.http_port);"
"m[1].querySelector('small').textContent='API '+s.version+' ativa';"
"m[2].querySelector('strong').textContent=s.remote==='standby'?'Standby':s.remote;"
"m[2].querySelector('small').textContent='status vindo de /api/status';"
"m[3].querySelector('strong').textContent=String(s.elf_count);"
"m[3].querySelector('small').textContent=s.elf_count_source==='mock'?'lista ainda mockada':'lista real';"
"}"
"const v=document.querySelector('.version');"
"if(v)v.textContent='Astro Remote '+s.version+' · uptime '+s.uptime_seconds+'s';"
"}catch(e){"
"const top=document.querySelector('.status');"
"if(top)top.textContent='ASTRO API OFFLINE';"
"}"
"}"
"document.addEventListener('DOMContentLoaded',()=>{"
"astroRefresh();"
"const buttons=[...document.querySelectorAll('button')];"
"const refresh=buttons.find(b=>b.textContent.indexOf('Atualizar status')>=0);"
"if(refresh)refresh.addEventListener('click',astroRefresh);"
"setInterval(astroRefresh,10000);"
"});"
"</script>";

static void send_dashboard_live(int fd)
{
  const char *marker = strstr(dashboard_page, "</body>");
  size_t before_len;
  size_t script_len = strlen(dashboard_live_script);
  size_t after_len;
  size_t total_len;
  char header[1024];

  if (!marker) {
    send_response(fd, "200 OK", NULL, dashboard_page);
    return;
  }

  before_len = (size_t)(marker - dashboard_page);
  after_len = strlen(marker);
  total_len = before_len + script_len + after_len;

  snprintf(
    header,
    sizeof(header),
    "HTTP/1.1 200 OK\r\n"
    "Content-Type: text/html; charset=utf-8\r\n"
    "Cache-Control: no-store\r\n"
    "Content-Length: %zu\r\n"
    "Connection: close\r\n"
    "\r\n",
    total_len
  );

  send(fd, header, strlen(header), 0);
  send(fd, dashboard_page, before_len, 0);
  send(fd, dashboard_live_script, script_len, 0);
  send(fd, marker, after_len, 0);
}

int main(void)
{
  int server_fd;
  int client_fd;
  int opt = 1;
  struct sockaddr_in addr;
  char buffer[8192];

  running = 1;
  astro_started_at = time(NULL);

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    notify("ASTRO: socket falhou");
    return 1;
  }

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

  notify("ASTRO Remote v0.4 API - porta 45821");

  while (running) {
    int total;
    char *header_end;

    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) {
      continue;
    }

    memset(buffer, 0, sizeof(buffer));

    total = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (total <= 0) {
      close(client_fd);
      continue;
    }

    buffer[total] = '\0';
    header_end = strstr(buffer, "\r\n\r\n");

    if (header_end) {
      int content_length = 0;
      char *cl = strstr(buffer, "Content-Length:");

      if (cl) {
        content_length = atoi(cl + strlen("Content-Length:"));
      }

      {
        int header_size = (int)((header_end + 4) - buffer);
        int body_received = total - header_size;

        while (
          body_received < content_length &&
          total < (int)sizeof(buffer) - 1
        ) {
          int received = recv(
            client_fd,
            buffer + total,
            sizeof(buffer) - total - 1,
            0
          );

          if (received <= 0) {
            break;
          }

          total += received;
          body_received += received;
          buffer[total] = '\0';
        }
      }
    }

    if (strstr(buffer, "POST /login ") && valid_login(buffer)) {
      send_response(
        client_fd,
        "302 Found",
        "Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\n"
        "Location: /\r\n",
        ""
      );
    }
    else if (strstr(buffer, "POST /logout ") && logged(buffer)) {
      send_response(
        client_fd,
        "302 Found",
        "Set-Cookie: astro_session=; Path=/; Max-Age=0; HttpOnly\r\n"
        "Location: /\r\n",
        ""
      );
    }
    else if (strstr(buffer, "GET /api/status ") && logged(buffer)) {
      send_api_status(client_fd);
    }
    else if (strstr(buffer, "POST /admin/shutdown ") && logged(buffer)) {
      send_response(client_fd, "200 OK", NULL, shutdown_page);
      running = 0;
      notify("ASTRO Remote encerrando");
    }
    else if (strstr(buffer, "GET / ") && logged(buffer)) {
      send_dashboard_live(client_fd);
    }
    else {
      send_response(client_fd, "200 OK", NULL, login_page);
    }

    close(client_fd);
  }

  close(server_fd);
  notify("ASTRO Remote encerrado");
  return 0;
}
