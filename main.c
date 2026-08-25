#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define ASTRO_PORT 45821

#define ASTRO_USER "henrique"
#define ASTRO_PASS "P@ndora2024"

static int running = 1;

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(
  int,
  notify_request_t *,
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

/* -------------------------------------------------------
 *  URL DECODER
 *  Exemplo:
 *  P%40ndora2024 -> P@ndora2024
 - *------------------------------------------------------ */

static void url_decode(char *dst, const char *src)
{
  while (*src) {

    if (
      *src == '%' &&
      src[1] &&
      src[2]
    ) {

      char hex[3];

      hex[0] = src[1];
      hex[1] = src[2];
      hex[2] = '\0';

      *dst++ = (char)strtol(
        hex,
        NULL,
        16
      );

      src += 3;
    }

    else if (*src == '+') {

      *dst++ = ' ';
      src++;
    }

    else {

      *dst++ = *src++;
    }
  }

  *dst = '\0';
}

/* -------------------------------------------------------
 *  PÁGINA LOGIN
 - *------------------------------------------------------ */

static const char *login_page =
"<!doctype html>"
"<html>"
"<head>"

"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"

"<title>Astro Remote</title>"

"<style>"

"*{box-sizing:border-box}"

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

"h1{"
"margin:0 0 10px;"
"}"

".sub{"
"color:#9ca3af;"
"margin-bottom:22px;"
"}"

"input{"
"width:100%;"
"padding:13px;"
"margin:7px 0;"
"border:1px solid #374151;"
"border-radius:9px;"
"background:#111827;"
"color:#fff;"
"font-size:16px;"
"}"

"button{"
"width:100%;"
"padding:13px;"
"margin-top:12px;"
"border:0;"
"border-radius:9px;"
"background:#10b981;"
"color:#08130f;"
"font-size:16px;"
"font-weight:bold;"
"cursor:pointer;"
"}"

"</style>"

"</head>"

"<body>"

"<div class='box'>"

"<h1>ASTRO REMOTE</h1>"

"<p class='sub'>Acesso ao PS5</p>"

"<form method='POST' action='/login'>"

"<input "
"name='username' "
"placeholder='Usuario' "
"autocomplete='username' "
"required>"

"<input "
"name='password' "
"type='password' "
"placeholder='Senha' "
"autocomplete='current-password' "
"required>"

"<button type='submit'>"
"ENTRAR"
"</button>"

"</form>"

"</div>"

"</body>"
"</html>";

/* -------------------------------------------------------
 *  DASHBOARD
 - *------------------------------------------------------ */

static const char *dashboard_page =
"<!doctype html>"
"<html>"
"<head>"

"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"

"<title>Astro Remote</title>"

"<style>"

"*{box-sizing:border-box}"

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
"width:430px;"
"background:#1f2937;"
"padding:28px;"
"border-radius:18px;"
"box-shadow:0 20px 60px rgba(0,0,0,.35);"
"}"

"h1{"
"margin-top:0;"
"}"

".online{"
"color:#4ade80;"
"}"

".item{"
"display:flex;"
"align-items:center;"
"justify-content:space-between;"
"background:#111827;"
"padding:14px;"
"margin:10px 0;"
"border-radius:10px;"
"}"

".btn{"
"background:#374151;"
"padding:8px 10px;"
"border-radius:7px;"
"font-size:12px;"
"}"

".shutdown{"
"width:100%;"
"margin-top:24px;"
"padding:13px;"
"border:0;"
"border-radius:9px;"
"background:#991b1b;"
"color:#fff;"
"font-size:14px;"
"font-weight:bold;"
"cursor:pointer;"
"}"

"</style>"

"</head>"

"<body>"

"<div class='box'>"

"<h1>ASTRO REMOTE</h1>"

"<p class='online'>"
"PS5 ONLINE"
"</p>"

"<h3>ELFs</h3>"

"<div class='item'>"
"<span>browser.elf</span>"
"<span class='btn'>EXECUTAR</span>"
"</div>"

"<div class='item'>"
"<span>ghostcontrol.elf</span>"
"<span class='btn'>EXECUTAR</span>"
"</div>"

"<div class='item'>"
"<span>ftpsrv.elf</span>"
"<span class='btn'>EXECUTAR</span>"
"</div>"

"<div class='item'>"
"<span>pegasus.elf</span>"
"<span class='btn'>EXECUTAR</span>"
"</div>"

"<form method='POST' action='/admin/shutdown'>"

"<button "
"class='shutdown' "
"type='submit'>"

"ENCERRAR ASTRO"

"</button>"

"</form>"

"</div>"

"</body>"
"</html>";

/* -------------------------------------------------------
 *  SHUTDOWN
 - *------------------------------------------------------ */

static const char *shutdown_page =
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
"background:#1f2937;"
"padding:30px;"
"border-radius:18px;"
"text-align:center;"
"}"

"</style>"

"</head>"

"<body>"

"<div class='box'>"

"<h1>ASTRO ENCERRADO</h1>"

"<p>"
"O servidor foi finalizado."
"</p>"

"<p>"
"A porta foi liberada."
"</p>"

"</div>"

"</body>"
"</html>";

/* -------------------------------------------------------
 *  HTTP RESPONSE
 - *------------------------------------------------------ */

static void send_response(
  int client_fd,
  const char *status,
  const char *extra_headers,
  const char *body
)
{
  char header[1024];

  snprintf(
    header,
    sizeof(header),

           "HTTP/1.1 %s\r\n"

           "Content-Type: "
           "text/html; charset=utf-8\r\n"

           "Content-Length: %zu\r\n"

           "%s"

           "Connection: close\r\n"

           "\r\n",

           status,

           strlen(body),

           extra_headers
           ? extra_headers
           : ""
  );

  send(
    client_fd,
    header,
    strlen(header),
       0
  );

  send(
    client_fd,
    body,
    strlen(body),
       0
  );
}

/* -------------------------------------------------------
 *  COOKIE
 - *------------------------------------------------------ */

static int is_logged_in(
  const char *request
)
{
  return strstr(
    request,
    "astro_session=logged_in"
  ) != NULL;
}

/* -------------------------------------------------------
 *  LOGIN
 - *------------------------------------------------------ */

static int valid_login(
  const char *request
)
{
  const char *body;

  char decoded[2048];

  body = strstr(
    request,
    "\r\n\r\n"
  );

  if (!body) {
    return 0;
  }

  body += 4;

  memset(
    decoded,
    0,
    sizeof(decoded)
  );

  url_decode(
    decoded,
    body
  );

  return

  strstr(
    decoded,
    "username=" ASTRO_USER
  ) != NULL

  &&

  strstr(
    decoded,
    "password=" ASTRO_PASS
  ) != NULL;
}

/* -------------------------------------------------------
 *  MAIN
 - *------------------------------------------------------ */

int main(void)
{
  int server_fd;
  int client_fd;

  int opt = 1;

  struct sockaddr_in addr;

  char buffer[8192];

  server_fd = socket(
    AF_INET,
    SOCK_STREAM,
    0
  );

  if (server_fd < 0) {

    notify(
      "ASTRO: socket falhou"
    );

    return 1;
  }

  setsockopt(
    server_fd,
    SOL_SOCKET,
    SO_REUSEADDR,
    &opt,
    sizeof(opt)
  );

  memset(
    &addr,
    0,
    sizeof(addr)
  );

  addr.sin_family =
  AF_INET;

  addr.sin_addr.s_addr =
  INADDR_ANY;

  addr.sin_port =
  htons(ASTRO_PORT);

  if (
    bind(
      server_fd,
      (struct sockaddr *)&addr,
         sizeof(addr)
    ) < 0
  ) {

    notify(
      "ASTRO: bind falhou"
    );

    close(server_fd);

    return 1;
  }

  if (
    listen(
      server_fd,
      8
    ) < 0
  ) {

    notify(
      "ASTRO: listen falhou"
    );

    close(server_fd);

    return 1;
  }

  notify(
    "ASTRO Remote v0.2 - porta 45821"
  );

  /* ---------------------------------------------------
   *      LOOP PRINCIPAL
   *   --------------------------------------------------- */

  while (running) {

    client_fd = accept(
      server_fd,
      NULL,
      NULL
    );

    if (client_fd < 0) {
      continue;
    }

    memset(
      buffer,
      0,
      sizeof(buffer)
    );

    /* -----------------------------------------------
     *          RECEBE PRIMEIRO BLOCO HTTP
     *       ----------------------------------------------- */

    int total = 0;
    int received;

    received = recv(
      client_fd,
      buffer,
      sizeof(buffer) - 1,
                    0
    );

    if (received <= 0) {

      close(client_fd);

      continue;
    }

    total = received;

    buffer[total] = '\0';

    /* -----------------------------------------------
     *          RECEBE BODY COMPLETO
     *       ----------------------------------------------- */

    char *header_end = strstr(
      buffer,
      "\r\n\r\n"
    );

    if (header_end) {

      int content_length = 0;

      char *cl = strstr(
        buffer,
        "Content-Length:"
      );

      if (cl) {

        content_length = atoi(
          cl +
          strlen(
            "Content-Length:"
          )
        );
      }

      int header_size =
      (int)(
        (header_end + 4)
        - buffer
      );

      int body_received =
      total
      - header_size;

      while (
        body_received
        < content_length
        &&
        total
        <
        (int)sizeof(buffer) - 1
      ) {

        received = recv(
          client_fd,
          buffer + total,
          sizeof(buffer)
          - total
          - 1,
          0
        );

        if (received <= 0) {
          break;
        }

        total += received;

        body_received +=
        received;

        buffer[total] =
        '\0';
      }
    }

    /* -----------------------------------------------
     *          POST /login
     *       ----------------------------------------------- */

    if (
      strstr(
        buffer,
        "POST /login "
      )
      &&
      valid_login(buffer)
    ) {

      send_response(
        client_fd,
        "302 Found",

        "Set-Cookie: "
        "astro_session=logged_in; "
        "Path=/; "
        "HttpOnly\r\n"

        "Location: /\r\n",

        ""
      );
    }

    /* -----------------------------------------------
     *          SHUTDOWN
     *       ----------------------------------------------- */

    else if (
      strstr(
        buffer,
        "POST /admin/shutdown "
      )
      &&
      is_logged_in(buffer)
    ) {

      send_response(
        client_fd,
        "200 OK",
        NULL,
        shutdown_page
      );

      running = 0;

      notify(
        "ASTRO Remote encerrando"
      );
    }

    /* -----------------------------------------------
     *          DASHBOARD
     *       ----------------------------------------------- */

    else if (
      strstr(
        buffer,
        "GET / "
      )
      &&
      is_logged_in(buffer)
    ) {

      send_response(
        client_fd,
        "200 OK",
        NULL,
        dashboard_page
      );
    }

    /* -----------------------------------------------
     *          LOGIN
     *       ----------------------------------------------- */

    else {

      send_response(
        client_fd,
        "200 OK",
        NULL,
        login_page
      );
    }

    close(client_fd);
  }

  /* ---------------------------------------------------
   *      FINALIZA
   *   --------------------------------------------------- */

  close(server_fd);

  notify(
    "ASTRO Remote encerrado"
  );

  return 0;
}
