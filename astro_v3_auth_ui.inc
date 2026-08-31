/* Astro V3 authentication screens.
 * Pure UI replacement: authentication/session behavior remains in the router.
 */

static const char *astro_auth_css=
"*{box-sizing:border-box}:root{--bg:#080d14;--panel:#101923;--panel2:#141f2c;--line:#26374b;--text:#f3f7fd;--muted:#8295ab;--blue:#2d8cff;--cyan:#57c7ff;--green:#42d59a;--red:#ff7580}"
"html,body{margin:0;min-height:100%;background:var(--bg);color:var(--text);font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Arial,sans-serif}body{min-height:100vh;overflow-x:hidden}"
".authbg{min-height:100vh;position:relative;display:grid;place-items:center;padding:28px;overflow:hidden;background:radial-gradient(circle at 18% 20%,#12325a55 0,transparent 34%),radial-gradient(circle at 82% 80%,#123a3355 0,transparent 32%),linear-gradient(145deg,#080d14 0%,#0b121c 55%,#090f17 100%)}"
".gridfx{position:absolute;inset:0;pointer-events:none;opacity:.18;background-image:linear-gradient(#5b749522 1px,transparent 1px),linear-gradient(90deg,#5b749522 1px,transparent 1px);background-size:44px 44px;mask-image:linear-gradient(to bottom,transparent,#000 15%,#000 85%,transparent)}"
".glow{position:absolute;width:420px;height:420px;border-radius:50%;filter:blur(90px);background:#1668d51c;left:-140px;top:-120px;pointer-events:none}.glow.g2{left:auto;top:auto;right:-160px;bottom:-180px;background:#19b98117}"
".authshell{position:relative;width:min(980px,100%);min-height:560px;display:grid;grid-template-columns:1.05fr .95fr;border:1px solid #2b3b50;border-radius:24px;overflow:hidden;background:#0d141eeb;box-shadow:0 35px 100px #0009,0 0 0 1px #ffffff05;backdrop-filter:blur(16px)}"
".visual{position:relative;padding:48px 46px;display:flex;flex-direction:column;justify-content:space-between;overflow:hidden;background:linear-gradient(160deg,#111d2c 0%,#0d1723 60%,#101923 100%);border-right:1px solid #26374b}"
".visual:before{content:'';position:absolute;width:430px;height:430px;border:1px solid #4f8fd533;border-radius:50%;right:-210px;top:-145px;box-shadow:0 0 0 54px #4f8fd50a,0 0 0 108px #4f8fd508}.visual:after{content:'';position:absolute;width:230px;height:230px;border:1px dashed #50d4b42b;border-radius:50%;left:-100px;bottom:-80px}"
".brandline{position:relative;z-index:2;display:flex;align-items:center;gap:14px}.logo{width:58px;height:58px;border-radius:16px;border:1px solid #3674ac;background:linear-gradient(145deg,#102845,#0a1421);position:relative;display:grid;place-items:center;box-shadow:0 0 30px #2d8cff2b,inset 0 0 28px #2d8cff12}.logo:before{content:'';width:32px;height:32px;border:2px solid #66b7ff;border-radius:50%;transform:rotate(-23deg) scaleY(.38)}.logo:after{content:'';position:absolute;width:3px;height:42px;border-radius:4px;background:linear-gradient(#76c8ff,#367dff);transform:rotate(18deg);box-shadow:0 0 12px #66b7ff}"
".brandname{font-size:22px;font-weight:950;letter-spacing:.2em}.brandsub{margin-top:4px;color:#72869e;font-size:9px;font-weight:800;letter-spacing:.25em;text-transform:uppercase}.hero{position:relative;z-index:2;max-width:430px}.eyebrow{display:inline-flex;align-items:center;gap:7px;padding:6px 9px;border-radius:999px;border:1px solid #295946;background:#11281f;color:#68dbac;font-size:9px;font-weight:850;letter-spacing:.08em;text-transform:uppercase}.eyebrow:before{content:'';width:6px;height:6px;border-radius:50%;background:#45d49c;box-shadow:0 0 9px #45d49c}.hero h1{font-size:38px;line-height:1.08;margin:18px 0 13px;letter-spacing:-.035em}.hero p{margin:0;color:#91a2b6;font-size:13px;line-height:1.65;max-width:390px}.featureline{position:relative;z-index:2;display:flex;gap:8px;flex-wrap:wrap}.feature{padding:8px 10px;border-radius:9px;background:#0b1420aa;border:1px solid #26384d;color:#8fa4bc;font-size:10px;font-weight:700}"
".formside{padding:48px 50px;display:flex;flex-direction:column;justify-content:center;background:linear-gradient(180deg,#101821,#0c131c)}.formhead{margin-bottom:27px}.formhead .tiny{color:#5f7894;font-size:9px;font-weight:900;letter-spacing:.18em;text-transform:uppercase}.formhead h2{font-size:28px;margin:8px 0 8px;letter-spacing:-.02em}.formhead p{font-size:11px;line-height:1.55;color:#8193a8;margin:0}.field{margin:0 0 15px}.field label{display:flex;justify-content:space-between;align-items:center;color:#9aabbd;font-size:10px;font-weight:750;margin:0 0 7px}.inputwrap{position:relative}.inputwrap input{width:100%;height:46px;border-radius:10px;border:1px solid #2b3c50;background:#09111a;color:#edf5ff;padding:0 43px 0 13px;outline:none;font-size:13px;transition:border .15s,box-shadow .15s,background .15s}.inputwrap input:focus{border-color:#387fc4;background:#0b1520;box-shadow:0 0 0 3px #2d8cff18}.inputwrap input::placeholder{color:#45586c}.eye{position:absolute;right:7px;top:7px;width:32px;height:32px;border:0;border-radius:8px;background:transparent;color:#72879e;cursor:pointer;font-size:15px}.eye:hover{background:#162332;color:#b7cae0}.submit{width:100%;height:46px;margin-top:6px;border:1px solid #348df0;border-radius:10px;background:linear-gradient(180deg,#3597ff,#237fe7);color:#fff;font-size:12px;font-weight:850;cursor:pointer;box-shadow:0 9px 24px #1878df2d;transition:transform .12s,filter .12s}.submit:hover{filter:brightness(1.08);transform:translateY(-1px)}.submit:active{transform:translateY(0)}.err{margin:0 0 16px;padding:10px 12px;border-radius:9px;border:1px solid #71363e;background:#29171b;color:#ff9ba4;font-size:11px}.security{display:flex;align-items:center;justify-content:center;gap:7px;margin-top:20px;color:#5f7186;font-size:9px}.security:before{content:'◆';color:#3995f2;font-size:8px}.setupnote{margin-top:17px;padding:11px;border-radius:10px;border:1px solid #26384d;background:#0b141e;color:#708399;font-size:10px;line-height:1.5}"
"@media(max-width:780px){.authbg{padding:14px}.authshell{grid-template-columns:1fr;min-height:auto;max-width:470px;border-radius:18px}.visual{padding:25px 24px 27px;border-right:0;border-bottom:1px solid #26374b;min-height:245px}.brandline .logo{width:47px;height:47px;border-radius:13px}.brandname{font-size:17px}.hero{margin-top:28px}.hero h1{font-size:27px;margin:12px 0 8px}.hero p{font-size:11px}.featureline{display:none}.formside{padding:28px 24px 30px}.formhead{margin-bottom:21px}.formhead h2{font-size:24px}.visual:before{width:300px;height:300px;right:-170px;top:-150px}}";

static void astro_auth_visual(char *out,size_t os,const char *mode)
{
  snprintf(out,os,
    "<div class='visual'>"
      "<div class='brandline'><div class='logo'></div><div><div class='brandname'>ASTRO</div><div class='brandsub'>PS5 Control Deck</div></div></div>"
      "<div class='hero'><div class='eyebrow'>Console gateway ativo</div><h1>%s</h1><p>Payloads, arquivos, saves, homebrew e interfaces Web em um único painel protegido.</p></div>"
      "<div class='featureline'><span class='feature'>⚡ Payloads</span><span class='feature'>◫ Arquivos</span><span class='feature'>◆ Saves</span><span class='feature'>🌐 Web UI</span></div>"
    "</div>",
    mode&&strcmp(mode,"setup")==0?"Seu PS5, um painel só.":"Bem-vindo de volta.");
}

static void send_login(int fd,int bad)
{
  char p[30000],visual[8000];astro_auth_visual(visual,sizeof(visual),"login");
  snprintf(p,sizeof(p),
    "<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta name='theme-color' content='#080d14'><title>Entrar · Astro</title><style>%s</style></head>"
    "<body><main class='authbg'><div class='gridfx'></div><div class='glow'></div><div class='glow g2'></div><section class='authshell'>%s"
      "<div class='formside'><div class='formhead'><div class='tiny'>Acesso protegido</div><h2>Entrar no Astro</h2><p>Use as credenciais configuradas neste console.</p></div>%s"
      "<form method='POST' action='/login' autocomplete='on'>"
        "<div class='field'><label for='user'>Usuário</label><div class='inputwrap'><input id='user' name='username' autocomplete='username' placeholder='Seu usuário' autofocus required></div></div>"
        "<div class='field'><label for='pass'>Senha</label><div class='inputwrap'><input id='pass' name='password' type='password' autocomplete='current-password' placeholder='Sua senha' required><button class='eye' type='button' aria-label='Mostrar senha' onclick=\"const i=document.getElementById('pass');i.type=i.type==='password'?'text':'password';this.textContent=i.type==='password'?'◉':'◎'\">◉</button></div></div>"
        "<button class='submit' type='submit'>Entrar no painel →</button>"
      "</form><div class='security'>Sessão autenticada pelo Astro</div></div>"
    "</section></main></body></html>",astro_auth_css,visual,bad?"<div class='err'>Usuário ou senha inválidos. Confira os dados e tente novamente.</div>":"");
  send_text(fd,"200 OK","text/html; charset=utf-8",NULL,p);
}

static void send_setup(int fd,const char *error)
{
  char p[34000],visual[8000];astro_auth_visual(visual,sizeof(visual),"setup");
  snprintf(p,sizeof(p),
    "<!doctype html><html lang='pt-BR'><head><meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<meta name='theme-color' content='#080d14'><title>Primeiro acesso · Astro</title><style>%s</style></head>"
    "<body><main class='authbg'><div class='gridfx'></div><div class='glow'></div><div class='glow g2'></div><section class='authshell'>%s"
      "<div class='formside'><div class='formhead'><div class='tiny'>Primeiro acesso</div><h2>Crie seu acesso</h2><p>Essas credenciais serão usadas tanto na rede local quanto no acesso remoto.</p></div>%s"
      "<form method='POST' action='/setup' autocomplete='off'>"
        "<div class='field'><label for='user'>Usuário</label><div class='inputwrap'><input id='user' name='username' minlength='3' maxlength='47' placeholder='3 a 47 caracteres' autofocus required></div></div>"
        "<div class='field'><label for='pass'>Senha <span style='color:#5e7288'>mín. 8 caracteres</span></label><div class='inputwrap'><input id='pass' name='password' type='password' minlength='8' maxlength='95' placeholder='Crie uma senha' required><button class='eye' type='button' onclick=\"const i=document.getElementById('pass');i.type=i.type==='password'?'text':'password';this.textContent=i.type==='password'?'◉':'◎'\">◉</button></div></div>"
        "<div class='field'><label for='pass2'>Confirmar senha</label><div class='inputwrap'><input id='pass2' name='password2' type='password' minlength='8' maxlength='95' placeholder='Repita a senha' required></div></div>"
        "<button class='submit' type='submit'>Criar acesso →</button>"
      "</form><div class='setupnote'>O Astro salva as credenciais somente no armazenamento do console. Depois deste cadastro, esta tela não aparece novamente.</div></div>"
    "</section></main></body></html>",astro_auth_css,visual,error?error:"");
  send_text(fd,"200 OK","text/html; charset=utf-8",NULL,p);
}
