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

typedef struct notify_request { char useless1[45]; char message[3075]; } notify_request_t;
int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *message) {
  notify_request_t req;
  memset(&req, 0, sizeof(req));
  strncpy(req.message, message, sizeof(req.message) - 1);
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void url_decode(char *dst, const char *src) {
  while (*src) {
    if (*src == '%' && src[1] && src[2]) {
      char hex[3] = { src[1], src[2], '\0' };
      *dst++ = (char)strtol(hex, NULL, 16);
      src += 3;
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

static const char *login_page =
"<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Astro Remote</title><style>"
":root{--bg:#0a0d12;--panel:#151a22;--line:#2a313d;--text:#f4f7fb;--muted:#8f9aaa;--green:#38d486}*{box-sizing:border-box}html,body{height:100%}body{margin:0;background:var(--bg);color:var(--text);font-family:Inter,Segoe UI,Arial,sans-serif;display:grid;place-items:center;overflow:hidden}body:before{content:'';position:fixed;inset:0;background:linear-gradient(135deg,rgba(20,119,255,.09),transparent 34%),radial-gradient(circle at 78% 22%,rgba(20,119,255,.09),transparent 28%);pointer-events:none}.login-shell{width:min(920px,calc(100% - 32px));display:grid;grid-template-columns:1.05fr .95fr;border:1px solid #242b35;border-radius:12px;overflow:hidden;background:#10141a;box-shadow:0 30px 90px rgba(0,0,0,.45);position:relative}.brand-side{padding:46px 44px;background:linear-gradient(145deg,#111820,#0d1218 62%,#0b1016);border-right:1px solid #242b35;display:flex;flex-direction:column;min-height:520px}.brand{display:flex;align-items:center;gap:13px}.logo{width:42px;height:42px;border-radius:10px;background:linear-gradient(145deg,#2288ff,#0c4eaf);display:grid;place-items:center;font-weight:900;font-size:19px;box-shadow:0 0 26px rgba(20,119,255,.22)}.brand strong{font-size:17px}.brand small{display:block;color:var(--muted);margin-top:2px}.hero{margin:auto 0}.eyebrow{font-size:10px;letter-spacing:.16em;text-transform:uppercase;color:#6daaff;font-weight:800;margin-bottom:12px}.hero h1{margin:0;font-size:36px;line-height:1.08}.hero p{margin:14px 0 0;color:var(--muted);line-height:1.65;max-width:390px}.feature{display:flex;gap:10px;align-items:flex-start;margin-top:22px;color:#cbd4df;font-size:13px}.feature i{width:23px;height:23px;border-radius:6px;background:#102d53;border:1px solid #18477e;color:#7db5ff;display:grid;place-items:center;font-style:normal;font-size:11px;flex:none}.feature span{color:var(--muted);display:block;margin-top:2px;font-size:12px}.brand-foot{display:flex;align-items:center;justify-content:space-between;gap:12px;color:#697586;font-size:11px}.online{display:flex;align-items:center;gap:7px;color:#80e0aa}.dot{width:8px;height:8px;border-radius:50%;background:var(--green);box-shadow:0 0 10px rgba(56,212,134,.55)}.form-side{padding:48px 44px;background:var(--panel);display:flex;align-items:center}.form-wrap{width:100%}.form-wrap h2{font-size:23px;margin:0 0 6px}.form-wrap>p{margin:0 0 28px;color:var(--muted);font-size:13px;line-height:1.5}.field{margin-bottom:16px}.field label{display:block;color:#b8c2ce;font-size:12px;font-weight:700;margin:0 0 7px}.input-wrap input{width:100%;height:44px;background:#10151c;border:1px solid #2a323e;border-radius:7px;color:#edf3fb;padding:0 13px;font-size:14px;outline:none}.input-wrap input:focus{border-color:#176fdc;box-shadow:0 0 0 3px rgba(20,119,255,.10)}.login-btn{width:100%;height:44px;border:0;border-radius:7px;background:linear-gradient(#2084ff,#1269dc);color:white;font-size:13px;font-weight:800;cursor:pointer;margin-top:5px}.security{margin-top:18px;padding-top:17px;border-top:1px solid #252c36;display:flex;gap:10px;align-items:flex-start}.shield{width:29px;height:29px;border-radius:7px;background:#111d18;border:1px solid #214734;color:#81dfaa;display:grid;place-items:center;font-size:13px;flex:none}.security strong{font-size:12px;display:block}.security span{display:block;color:#697586;font-size:11px;line-height:1.45;margin-top:3px}@media(max-width:760px){body{overflow:auto;padding:16px}.login-shell{grid-template-columns:1fr}.brand-side{display:none}.form-side{padding:34px 24px;min-height:500px}}"
"</style></head><body><main class='login-shell'><section class='brand-side'><div class='brand'><div class='logo'>A</div><div><strong>Astro Remote</strong><small>PS5 Control Hub</small></div></div><div class='hero'><div class='eyebrow'>Acesso remoto ao seu console</div><h1>Seu PS5.<br>De qualquer lugar.</h1><p>Entre no Astro para administrar serviços, executar ELFs, abrir o Pegasus e acessar a futura camada Remote direto pelo navegador.</p><div class='feature'><i>R</i><div><b>Remote first</b><span>Video, controles e sessao remota integrados ao mesmo painel.</span></div></div><div class='feature'><i>ELF</i><div><b>Control Hub</b><span>Payloads, browser, arquivos e servicos centralizados.</span></div></div></div><div class='brand-foot'><span>Astro Remote v0.3</span><span class='online'><span class='dot'></span>ASTRO DISPONIVEL</span></div></section><section class='form-side'><div class='form-wrap'><h2>Entrar no Astro</h2><p>Use suas credenciais para acessar o painel do console.</p><form method='POST' action='/login'><div class='field'><label>Usuario</label><div class='input-wrap'><input name='username' autocomplete='username' placeholder='Digite seu usuario' required></div></div><div class='field'><label>Senha</label><div class='input-wrap'><input name='password' type='password' autocomplete='current-password' placeholder='Digite sua senha' required></div></div><button class='login-btn' type='submit'>Entrar no painel</button></form><div class='security'><div class='shield'>OK</div><div><strong>Sessao protegida</strong><span>O acesso ao Astro exige autenticacao antes de liberar funcoes do console e do Remote.</span></div></div></div></section></main></body></html>";

static const char *dashboard_page =
"<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Astro Remote</title><style>"
":root{--bg:#0a0d12;--side:#11151b;--panel:#151a22;--line:#2a313d;--text:#f4f7fb;--muted:#8f9aaa;--green:#38d486}*{box-sizing:border-box}html,body{height:100%}body{margin:0;background:var(--bg);color:var(--text);font-family:Inter,Segoe UI,Arial,sans-serif;font-size:14px}.app{height:100vh;display:grid;grid-template-columns:238px 1fr;overflow:hidden}.sidebar{background:var(--side);border-right:1px solid #202631;display:flex;flex-direction:column}.brand{height:68px;display:flex;align-items:center;gap:12px;padding:0 18px;border-bottom:1px solid #202631}.logo{width:34px;height:34px;border-radius:9px;background:linear-gradient(145deg,#2288ff,#0c4eaf);display:grid;place-items:center;font-weight:900}.brand small{display:block;color:var(--muted)}.nav{padding:14px 10px;display:grid;gap:4px}.nav-title{padding:10px 10px 6px;color:#606b7a;font-size:10px;text-transform:uppercase;letter-spacing:.14em;font-weight:800}.nav a{height:42px;padding:0 12px;border-radius:7px;color:#aab4c2;text-decoration:none;display:flex;align-items:center;gap:11px;font-weight:650}.nav a.active{background:linear-gradient(90deg,#176ddd,#105fca);color:#fff;box-shadow:inset 3px 0 #5eafff}.ico{width:18px;text-align:center}.sidebar-bottom{margin-top:auto;padding:12px 10px 16px;border-top:1px solid #202631}.version{padding:9px 12px;color:#657081;font-size:11px}.workspace{min-width:0;display:flex;flex-direction:column;height:100vh}.topbar{height:68px;background:#0f1319;border-bottom:1px solid #202631;display:flex;align-items:center;justify-content:space-between;padding:0 24px}.crumb{display:flex;align-items:center;gap:10px}.crumb h1{font-size:18px;margin:0}.crumb span{color:#596474}.status{display:flex;align-items:center;gap:8px;background:#111d18;border:1px solid #214734;color:#8ce5b2;border-radius:999px;padding:7px 11px;font-size:12px;font-weight:700}.dot{width:8px;height:8px;border-radius:50%;background:var(--green)}.main{overflow:auto;padding:22px 24px 34px}.content{max-width:1500px;margin:0 auto}.hero{display:flex;align-items:flex-end;justify-content:space-between;gap:18px;margin-bottom:18px}.hero h2{font-size:28px;margin:0 0 5px}.hero p{margin:0;color:var(--muted)}.actions{display:flex;gap:8px}.btn{border:0;border-radius:7px;padding:10px 14px;font-weight:750;cursor:pointer}.primary{background:linear-gradient(#2084ff,#1269dc);color:#fff}.ghost{background:#171d25;color:#cad2dc;border:1px solid #2a333f}.danger{background:#411c20;color:#ff9b9a;border:1px solid #6c2b31}.metrics{display:grid;grid-template-columns:repeat(4,minmax(0,1fr));gap:12px;margin-bottom:18px}.metric,.section{background:var(--panel);border:1px solid var(--line);border-radius:9px}.metric{padding:15px}.label{color:var(--muted);font-size:12px;margin-bottom:8px}.metric strong{font-size:19px}.metric small{display:block;color:#657183;margin-top:4px}.ok{color:#7fe4aa}.blue{color:#70b1ff}.section{margin-bottom:16px;overflow:hidden}.section-head{height:50px;padding:0 16px;display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid var(--line)}.section-head h3{margin:0;font-size:14px}.section-head span{color:var(--muted);font-size:12px}.quick-grid{display:grid;grid-template-columns:repeat(5,minmax(0,1fr))}.quick{padding:17px;border-right:1px solid var(--line);min-height:108px}.quick:last-child{border-right:0}.qicon{width:34px;height:34px;border-radius:8px;background:#0f3970;border:1px solid #185093;display:grid;place-items:center;margin-bottom:12px;color:#8ac1ff;font-weight:900}.quick strong{display:block;margin-bottom:4px}.quick small{color:var(--muted);line-height:1.45}.two{display:grid;grid-template-columns:1.1fr .9fr;gap:16px}.table{width:100%;border-collapse:collapse}.table th,.table td{text-align:left;padding:11px 14px;border-bottom:1px solid #242b35}.table th{color:#7e8999;font-size:11px;text-transform:uppercase;background:#12171e}.type{display:inline-block;padding:4px 7px;border-radius:5px;background:#1a2533;color:#8db9e8;border:1px solid #263a50;font-size:10px}.run{color:#72adff;font-weight:750}.remote{padding:16px}.remote-top{display:flex;justify-content:space-between;gap:14px;align-items:center;margin-bottom:14px}.remote-top small{color:var(--muted)}.remote-state{color:#7fe4aa;font-weight:700;font-size:12px}.preview{height:156px;border:1px solid #29323d;border-radius:8px;background:linear-gradient(135deg,#101722,#0a0f16);display:grid;place-items:center;color:#556274}.pads{display:grid;grid-template-columns:1fr 1fr;gap:8px;margin-top:10px}.pad{background:#11161d;border:1px solid #29313d;border-radius:7px;padding:10px}.pad span{float:right;color:#6fd99d;font-size:11px}@media(max-width:1000px){.metrics{grid-template-columns:repeat(2,1fr)}.quick-grid{grid-template-columns:repeat(2,1fr)}.two{grid-template-columns:1fr}}@media(max-width:760px){.app{grid-template-columns:1fr}.sidebar{display:none}.main{padding:16px}.hero{align-items:flex-start;flex-direction:column}.metrics{grid-template-columns:1fr}.quick-grid{grid-template-columns:1fr}}"
"</style></head><body><div class='app'><aside class='sidebar'><div class='brand'><div class='logo'>A</div><div><strong>Astro Remote</strong><small>PS5 Control Hub</small></div></div><nav class='nav'><div class='nav-title'>Principal</div><a class='active' href='/'><span class='ico'>H</span>Home</a><a href='#'><span class='ico'>R</span>Remote</a><a href='#'><span class='ico'>E</span>ELFs</a><a href='#'><span class='ico'>P</span>Pegasus</a><div class='nav-title'>Ferramentas</div><a href='#'><span class='ico'>W</span>Browser</a><a href='#'><span class='ico'>F</span>Arquivos</a><a href='#'><span class='ico'>X</span>Processos</a><div class='nav-title'>Sistema</div><a href='#'><span class='ico'>C</span>Configuracoes</a></nav><div class='sidebar-bottom'><form method='POST' action='/admin/shutdown'><button class='btn danger' style='width:100%' type='submit'>Encerrar Astro</button></form><form method='POST' action='/logout' style='margin-top:8px'><button class='btn ghost' style='width:100%' type='submit'>Sair</button></form><div class='version'>Astro Remote v0.3</div></div></aside><section class='workspace'><header class='topbar'><div class='crumb'><h1>Home</h1><span>/</span><span>Dashboard</span></div><div class='status'><span class='dot'></span>PS5 ONLINE</div></header><main class='main'><div class='content'><div class='hero'><div><h2>Bom dia, Henrique.</h2><p>Seu PS5 esta conectado. O Astro centraliza servicos, ELFs e a camada de acesso remoto.</p></div><div class='actions'><button class='btn ghost'>Atualizar status</button><button class='btn primary'>Abrir Remote</button></div></div><div class='metrics'><div class='metric'><div class='label'>Console</div><strong>PlayStation 5</strong><small class='ok'>conectado ao Astro</small></div><div class='metric'><div class='label'>Astro HTTP</div><strong class='blue'>45821</strong><small>interface web ativa</small></div><div class='metric'><div class='label'>Remote</div><strong>Standby</strong><small>WebRTC / Gamepad planejado</small></div><div class='metric'><div class='label'>ELFs</div><strong>4</strong><small>itens mockados</small></div></div><section class='section'><div class='section-head'><h3>Acoes rapidas</h3><span>Astro modules</span></div><div class='quick-grid'><div class='quick'><div class='qicon'>R</div><strong>Remote</strong><small>Video, controles e sessao remota.</small></div><div class='quick'><div class='qicon'>PG</div><strong>Pegasus</strong><small>Abrir biblioteca e catalogo.</small></div><div class='quick'><div class='qicon'>ELF</div><strong>Executar ELF</strong><small>Carregar payloads disponiveis.</small></div><div class='quick'><div class='qicon'>WEB</div><strong>Browser</strong><small>Iniciar navegador e URLs.</small></div><div class='quick'><div class='qicon'>FTP</div><strong>Arquivos</strong><small>Gerenciar armazenamento.</small></div></div></section><div class='two'><section class='section'><div class='section-head'><h3>Biblioteca de ELFs</h3><span>mock v0.3</span></div><table class='table'><thead><tr><th>Nome</th><th>Tipo</th><th>Status</th><th></th></tr></thead><tbody><tr><td><b>browser.elf</b></td><td><span class='type'>launcher</span></td><td>Disponivel</td><td class='run'>Executar</td></tr><tr><td><b>ghostcontrol.elf</b></td><td><span class='type'>input</span></td><td>Disponivel</td><td class='run'>Executar</td></tr><tr><td><b>ftpsrv.elf</b></td><td><span class='type'>storage</span></td><td>Disponivel</td><td class='run'>Executar</td></tr><tr><td><b>pegasus.elf</b></td><td><span class='type'>library</span></td><td>Disponivel</td><td class='run'>Executar</td></tr></tbody></table></section><section class='section'><div class='section-head'><h3>Astro Remote</h3><span>modulo exclusivo</span></div><div class='remote'><div class='remote-top'><div><strong>Sessao remota</strong><br><small>Acesso pelo navegador, sem cliente nativo.</small></div><div class='remote-state'>PRONTO</div></div><div class='preview'>Stream ainda nao iniciado</div><div class='pads'><div class='pad'><strong>Controle 1</strong><span>aguardando</span></div><div class='pad'><strong>Controle 2</strong><span>aguardando</span></div></div><div class='actions' style='margin-top:12px'><button class='btn primary'>Iniciar sessao</button><button class='btn ghost'>Configurar</button></div></div></section></div></div></main></section></div></body></html>";

static const char *shutdown_page =
"<!doctype html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><style>body{margin:0;min-height:100vh;display:grid;place-items:center;background:#0a0d12;color:#f4f7fb;font-family:Arial}.box{padding:34px;background:#151a22;border:1px solid #2a313d;border-radius:10px;text-align:center}</style></head><body><div class='box'><h1>Astro encerrado</h1><p>A porta 45821 foi liberada.</p></div></body></html>";

static void send_response(int fd, const char *status, const char *headers, const char *body) {
  char h[1024];
  snprintf(h, sizeof(h), "HTTP/1.1 %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: %zu\r\n%sConnection: close\r\n\r\n", status, strlen(body), headers ? headers : "");
  send(fd, h, strlen(h), 0);
  send(fd, body, strlen(body), 0);
}

static int logged(const char *request) {
  return strstr(request, "astro_session=logged_in") != NULL;
}

static int valid_login(const char *request) {
  const char *body = strstr(request, "\r\n\r\n");
  char decoded[2048];
  if (!body) return 0;
  body += 4;
  memset(decoded, 0, sizeof(decoded));
  url_decode(decoded, body);
  return strstr(decoded, "username=" ASTRO_USER) && strstr(decoded, "password=" ASTRO_PASS);
}

int main(void) {
  int server_fd, client_fd, opt = 1;
  struct sockaddr_in addr;
  char buffer[8192];

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) { notify("ASTRO: socket falhou"); return 1; }
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(ASTRO_PORT);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) { notify("ASTRO: bind falhou"); close(server_fd); return 1; }
  if (listen(server_fd, 8) < 0) { notify("ASTRO: listen falhou"); close(server_fd); return 1; }

  notify("ASTRO Remote v0.3 - porta 45821");

  while (running) {
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0) continue;

    memset(buffer, 0, sizeof(buffer));
    int total = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
    if (total <= 0) { close(client_fd); continue; }
    buffer[total] = '\0';

    char *header_end = strstr(buffer, "\r\n\r\n");
    if (header_end) {
      int content_length = 0;
      char *cl = strstr(buffer, "Content-Length:");
      if (cl) content_length = atoi(cl + strlen("Content-Length:"));
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

    if (strstr(buffer, "POST /login ") && valid_login(buffer)) {
      send_response(client_fd, "302 Found", "Set-Cookie: astro_session=logged_in; Path=/; HttpOnly\r\nLocation: /\r\n", "");
    } else if (strstr(buffer, "POST /logout ") && logged(buffer)) {
      send_response(client_fd, "302 Found", "Set-Cookie: astro_session=; Path=/; Max-Age=0; HttpOnly\r\nLocation: /\r\n", "");
    } else if (strstr(buffer, "POST /admin/shutdown ") && logged(buffer)) {
      send_response(client_fd, "200 OK", NULL, shutdown_page);
      running = 0;
      notify("ASTRO Remote encerrando");
    } else if (strstr(buffer, "GET / ") && logged(buffer)) {
      send_response(client_fd, "200 OK", NULL, dashboard_page);
    } else {
      send_response(client_fd, "200 OK", NULL, login_page);
    }

    close(client_fd);
  }

  close(server_fd);
  notify("ASTRO Remote encerrado");
  return 0;
}
