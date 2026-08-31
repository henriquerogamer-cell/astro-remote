static const char *base_css =
"*{box-sizing:border-box}"
":root{--bg:#06070a;--bg2:#0a0c12;--panel:#0e1118;--line:#28303a;--ivory:#ece7d8;--pale:#bcd9d5;--mist:#81909a;--good:#92d5b5;--bad:#db8c94;--gold:#c7aa71}"
"body{margin:0;min-height:100vh;background:radial-gradient(circle at 50% -20%,#1b2731 0,#0a0d12 32%,#050609 70%);color:var(--ivory);font-family:Inter,Segoe UI,Arial,sans-serif}"
"a{color:inherit;text-decoration:none}.shell{width:min(1180px,calc(100% - 28px));margin:22px auto 54px}"
".bar{display:flex;align-items:center;justify-content:space-between;border-bottom:1px solid #242a31;padding:10px 2px 16px;margin-bottom:18px}.brand{font-weight:900;letter-spacing:.24em}.brand small{display:block;color:var(--mist);font-size:9px;letter-spacing:.36em;margin-top:4px}.sigil{font-size:23px;color:var(--pale)}"
".nav{display:flex;gap:8px;flex-wrap:wrap;margin:0 0 18px}.nav a{padding:9px 12px;border:1px solid #26303a;border-radius:999px;color:#aab8bf;font-size:12px}.nav a:hover,.nav a.active{color:var(--ivory);border-color:#718b8e;background:#10181d}"
".hero{padding:24px;border:1px solid #2b333d;border-radius:26px;background:linear-gradient(160deg,#121720,#0a0d12);box-shadow:0 24px 60px #0008;margin-bottom:16px;position:relative;overflow:hidden}.hero:after{content:'';position:absolute;width:180px;height:180px;border:1px solid #8fb8b633;border-radius:50%;right:-70px;top:-80px;box-shadow:0 0 60px #8fb8b616}.hero h1{margin:0 0 8px;font-size:clamp(26px,5vw,48px);letter-spacing:.04em}.hero p{margin:0;color:var(--mist);max-width:680px;line-height:1.55}"
".grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}.card{grid-column:span 4;background:linear-gradient(180deg,#10151c,#090c11);border:1px solid var(--line);border-radius:22px;padding:18px;min-height:150px}.card.wide{grid-column:span 8}.card.full{grid-column:1/-1}.card h2{font-size:16px;margin:0 0 8px}.muted{color:var(--mist);font-size:13px;line-height:1.5}.status{display:inline-flex;align-items:center;gap:7px;font-size:11px;font-weight:800;letter-spacing:.06em}.dot{width:8px;height:8px;border-radius:50%;background:#64717a}.status.on{color:var(--good)}.status.on .dot{background:var(--good);box-shadow:0 0 12px #92d5b577}.status.off{color:#7e8990}.button,button{display:inline-flex;align-items:center;justify-content:center;border:1px solid #4a5f66;background:#172128;color:var(--ivory);padding:10px 13px;border-radius:12px;font-weight:800;font-size:12px;cursor:pointer}.button:hover,button:hover{background:#202d35}.button.ghost{background:transparent;border-color:#303a44;color:#aebac0}.button.danger{border-color:#61363c;background:#241317;color:#efb7bc}.row{display:flex;gap:9px;align-items:center;flex-wrap:wrap;margin-top:14px}"
".service{display:flex;justify-content:space-between;gap:12px;align-items:center;padding:12px 0;border-top:1px solid #1e252c}.service:first-of-type{border-top:0}.service b{font-size:13px}.port{font-family:monospace;color:#71818b;font-size:11px}"
"input{width:100%;background:#080b10;border:1px solid #2c3741;color:var(--ivory);border-radius:12px;padding:12px;outline:none}input:focus{border-color:#7c9b9d}.field{margin:10px 0}.field label{display:block;color:#829199;font-size:11px;margin-bottom:6px;letter-spacing:.08em;text-transform:uppercase}"
"table{width:100%;border-collapse:collapse;font-size:12px}th,td{text-align:left;padding:10px;border-bottom:1px solid #20272e}th{color:#87969e}.crumb{font-family:monospace;color:#9fb0b6;font-size:12px;word-break:break-all;margin-bottom:12px}.store-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(220px,1fr));gap:12px}.pkg{border:1px solid #283039;border-radius:18px;background:#0b0f14;padding:14px}.pkg h3{margin:0 0 5px;font-size:14px}.pkg .meta{color:#7e8c93;font-size:11px;min-height:32px}.pkg .ver{display:inline-block;margin-top:8px;color:var(--gold);font-size:11px}"
".foot{color:#56636b;font-size:10px;text-align:center;margin-top:22px}@media(max-width:800px){.card,.card.wide{grid-column:1/-1}.shell{margin-top:10px}.bar{padding-top:6px}}";

static void page_start(char *out, size_t out_sz, const char *title, const char *active)
{
  snprintf(out, out_sz,
"<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>%s · Astro</title><style>%s</style></head><body><main class='shell'>"
"<header class='bar'><div><div class='brand'>ASTRO<small>PS5 CONTROL DECK</small></div></div><div class='sigil'>✦</div></header>"
"<nav class='nav'>"
"<a class='%s' href='/dashboard'>Painel</a><a class='%s' href='/services'>Serviços</a><a class='%s' href='/files'>Arquivos</a><a class='%s' href='/saves'>Saves</a><a class='%s' href='/homebrew'>Homebrew</a><a class='%s' href='/settings'>Ajustes</a><a href='/logout'>Sair</a></nav>",
  title, base_css,
  strcmp(active,"dashboard")==0?"active":"", strcmp(active,"services")==0?"active":"",
  strcmp(active,"files")==0?"active":"", strcmp(active,"saves")==0?"active":"",
  strcmp(active,"homebrew")==0?"active":"", strcmp(active,"settings")==0?"active":"");
}

static void page_end(char *out, size_t out_sz)
{
  strncat(out, "<div class='foot'>ASTRO v1 · administração, arquivos, saves e homebrew · Remote Play fica com o Chiaki</div></main></body></html>", out_sz - strlen(out) - 1);
}

static void send_setup(int fd, const char *error)
{
  char page[15000];
  snprintf(page, sizeof(page),
"<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Configurar Astro</title><style>%s .setup{width:min(470px,calc(100%% - 28px));margin:7vh auto;padding:30px;border:1px solid #34404a;border-radius:30px;background:linear-gradient(160deg,#111821,#0b0e13);box-shadow:0 30px 90px #000a}.setup h1{margin:8px 0 8px;letter-spacing:.08em}.setup .first{display:inline-block;color:var(--gold);font-size:10px;font-weight:900;letter-spacing:.14em;border:1px solid #584b32;border-radius:999px;padding:6px 9px}.err{color:#e7a0a7;font-size:12px;padding:10px 12px;border:1px solid #5f3439;border-radius:12px;background:#211216}.hint{font-size:11px;color:#718089;line-height:1.5;margin-top:14px}</style></head><body><section class='setup'><div class='sigil'>✦</div><span class='first'>PRIMEIRO ACESSO</span><h1>Crie o acesso do Astro</h1><p class='muted'>Escolha agora o usuário e a senha que você vai usar para entrar no Astro, tanto dentro de casa quanto remotamente.</p>%s<form method='POST' action='/setup'><div class='field'><label>Usuário</label><input name='username' minlength='3' maxlength='47' required autocomplete='username' placeholder='ex: parca'></div><div class='field'><label>Senha</label><input name='password' type='password' minlength='8' maxlength='95' required autocomplete='new-password'></div><div class='field'><label>Confirmar senha</label><input name='password2' type='password' minlength='8' maxlength='95' required autocomplete='new-password'></div><button type='submit'>CRIAR ACESSO</button></form><p class='hint'>Esse cadastro acontece uma única vez. Depois disso o Astro abre sempre na tela normal de login. As credenciais ficam salvas localmente no PS5.</p></section></body></html>",
  base_css, error ? error : "");
  send_text(fd, "200 OK", "text/html; charset=utf-8", NULL, page);
}

static void send_login(int fd, int bad)
{
  char page[12000];
  snprintf(page, sizeof(page),
"<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'><title>Astro</title><style>%s .login{width:min(430px,calc(100%% - 28px));margin:10vh auto;padding:28px;border:1px solid #303842;border-radius:28px;background:#0d1117;box-shadow:0 30px 80px #0009}.login h1{letter-spacing:.22em}.err{color:#e7a0a7;font-size:12px}</style></head><body><section class='login'><div class='sigil'>✦</div><h1>ASTRO</h1><p class='muted'>Painel administrativo do seu PlayStation 5.</p>%s<form method='POST' action='/login'><div class='field'><label>Usuário</label><input name='username' required autocomplete='username'></div><div class='field'><label>Senha</label><input name='password' type='password' required autocomplete='current-password'></div><button type='submit'>Entrar</button></form></section></body></html>",
  base_css, bad ? "<p class='err'>Usuário ou senha inválidos.</p>" : "");
  send_text(fd, "200 OK", "text/html; charset=utf-8", NULL, page);
}

static void send_dashboard(int fd)
{
  int p = port_open(PROSPERO_PORT), g = port_open(PEGASUS_PORT), s = port_open(GARLIC_PORT);
  char page[30000]; page_start(page, sizeof(page), "Painel", "dashboard");
  char body[22000];
  snprintf(body, sizeof(body),
"<section class='hero'><h1>Um painel. Três trabalhos.</h1><p>Astro agora cuida de acesso remoto aos serviços do PS5, arquivos, saves e catálogo de homebrew. A transmissão de jogo saiu daqui e fica com quem faz melhor: Chiaki.</p></section>"
"<section class='grid'>"
"<article class='card'><h2>Prospero</h2><div class='status %s'><span class='dot'></span>%s</div><p class='muted'>Painel de payloads, uploads e administração.</p><div class='row'><a class='button' href='/p/prospero/'>Abrir no Astro</a></div></article>"
"<article class='card'><h2>Pegasus DL</h2><div class='status %s'><span class='dot'></span>%s</div><p class='muted'>Downloads, catálogo e biblioteca do console.</p><div class='row'><a class='button' href='/p/pegasus/'>Abrir no Astro</a></div></article>"
"<article class='card'><h2>Garlic SaveMgr</h2><div class='status %s'><span class='dot'></span>%s</div><p class='muted'>Backup, restore e resign de saves entre usuários.</p><div class='row'><a class='button' href='/p/garlic/'>Abrir Saves</a></div></article>"
"<article class='card wide'><h2>Mapa do Astro</h2><div class='service'><div><b>Payloads e serviços</b><div class='muted'>Prospero e ferramentas web por um único endereço.</div></div><span>→</span></div><div class='service'><div><b>Arquivos</b><div class='muted'>Explorador nativo + acesso completo pelo Prospero.</div></div><span>→</span></div><div class='service'><div><b>Saves</b><div class='muted'>Garlic como motor para backup, restore e User1 → User2.</div></div><span>→</span></div><div class='service'><div><b>Homebrew</b><div class='muted'>Catálogo PS5 dentro do Astro, sem depender da HB Store instalada.</div></div><span>→</span></div></article>"
"<article class='card'><h2>Estado</h2><p class='muted'>Astro escuta na porta %d. Os serviços internos continuam nas portas locais, mas podem ser acessados pelo proxy autenticado do Astro.</p><a class='button ghost' href='/api/status'>JSON de status</a></article>"
"</section>",
  p?"on":"off",p?"ONLINE":"OFFLINE", g?"on":"off",g?"ONLINE":"OFFLINE", s?"on":"off",s?"ONLINE":"OFFLINE", ASTRO_PORT);
  strncat(page, body, sizeof(page)-strlen(page)-1); page_end(page,sizeof(page));
  send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}

static void send_services(int fd)
{
  int p = port_open(PROSPERO_PORT), g = port_open(PEGASUS_PORT), s = port_open(GARLIC_PORT);
  char page[26000]; page_start(page,sizeof(page),"Serviços","services");
  char body[19000];
  snprintf(body,sizeof(body),
"<section class='hero'><h1>Serviços</h1><p>Os painéis continuam rodando localmente no PS5. O Astro os envolve com autenticação e os entrega por rotas próprias, sem você precisar decorar portas no acesso externo.</p></section><section class='grid'><article class='card full'>"
"<div class='service'><div><b>Prospero Manager</b><div class='port'>127.0.0.1:%d</div></div><div><span class='status %s'><span class='dot'></span>%s</span> &nbsp;<a class='button' href='/p/prospero/'>Abrir</a></div></div>"
"<div class='service'><div><b>Pegasus DL</b><div class='port'>127.0.0.1:%d</div></div><div><span class='status %s'><span class='dot'></span>%s</span> &nbsp;<a class='button' href='/p/pegasus/'>Abrir</a></div></div>"
"<div class='service'><div><b>Garlic SaveMgr</b><div class='port'>127.0.0.1:%d</div></div><div><span class='status %s'><span class='dot'></span>%s</span> &nbsp;<a class='button' href='/p/garlic/'>Abrir</a></div></div>"
"</article></section>",
  PROSPERO_PORT,p?"on":"off",p?"ONLINE":"OFFLINE", PEGASUS_PORT,g?"on":"off",g?"ONLINE":"OFFLINE", GARLIC_PORT,s?"on":"off",s?"ONLINE":"OFFLINE");
  strncat(page,body,sizeof(page)-strlen(page)-1); page_end(page,sizeof(page)); send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}
