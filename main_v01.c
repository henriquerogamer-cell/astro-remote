#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define ASTRO_PORT 45820

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(
  int,
  notify_request_t*,
  size_t,
  int
);

static void notify(const char *message)
{
  notify_request_t req;

  memset(&req, 0, sizeof(req));
  strncpy(
    req.message,
    message,
    sizeof(req.message) - 1
  );

  sceKernelSendNotificationRequest(
    0,
    &req,
    sizeof(req),
                                   0
  );
}

static const char *page =
"<!doctype html>"
"<html>"
"<head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Astro Remote</title>"
"<style>"
"body{"
"font-family:Arial,sans-serif;"
"background:#111827;"
"color:#fff;"
"display:flex;"
"align-items:center;"
"justify-content:center;"
"min-height:100vh;"
"margin:0;"
"}"
".box{"
"width:360px;"
"background:#1f2937;"
"padding:28px;"
"border-radius:18px;"
"box-shadow:0 20px 60px rgba(0,0,0,.35);"
"}"
"h1{margin-top:0}"
".online{color:#4ade80}"
".item{"
"background:#111827;"
"padding:14px;"
"margin:10px 0;"
"border-radius:10px;"
"}"
"</style>"
"</head>"
"<body>"
"<div class='box'>"
"<h1>ASTRO REMOTE</h1>"
"<p class='online'>PS5 ONLINE</p>"
"<h3>ELFs</h3>"
"<div class='item'>browser.elf</div>"
"<div class='item'>ghostcontrol.elf</div>"
"<div class='item'>ftpsrv.elf</div>"
"<div class='item'>pegasus.elf</div>"
"</div>"
"</body>"
"</html>";

int main(void)
{
  int server_fd;
  int client_fd;
  int opt = 1;

  struct sockaddr_in addr;
  char buffer[2048];

  server_fd = socket(
    AF_INET,
    SOCK_STREAM,
    0
  );

  if (server_fd < 0) {
    notify("ASTRO: socket() falhou");
    return 1;
  }

  setsockopt(
    server_fd,
    SOL_SOCKET,
    SO_REUSEADDR,
    &opt,
    sizeof(opt)
  );

  memset(&addr, 0, sizeof(addr));

  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(ASTRO_PORT);

  if (bind(
    server_fd,
    (struct sockaddr *)&addr,
           sizeof(addr)
  ) < 0) {
    notify("ASTRO: bind() falhou");
    close(server_fd);
    return 1;
  }

  if (listen(server_fd, 5) < 0) {
    notify("ASTRO: listen() falhou");
    close(server_fd);
    return 1;
  }

  notify("ASTRO Remote iniciado na porta 45820");

  while (1) {

    client_fd = accept(
      server_fd,
      NULL,
      NULL
    );

    if (client_fd < 0) {
      continue;
    }

    memset(buffer, 0, sizeof(buffer));

    recv(
      client_fd,
      buffer,
      sizeof(buffer) - 1,
         0
    );

    char header[512];

    snprintf(
      header,
      sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html; charset=utf-8\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n",
             strlen(page)
    );

    send(
      client_fd,
      header,
      strlen(header),
         0
    );

    send(
      client_fd,
      page,
      strlen(page),
         0
    );

    close(client_fd);
  }

  close(server_fd);

  return 0;
}w
