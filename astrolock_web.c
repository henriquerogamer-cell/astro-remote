/*
 * Astro Lock Web
 * Standalone web front-end for offline PS5 account activation.
 *
 * The registry behavior is adapted from OffAct by John Törnblom (2024),
 * licensed under GPL-3.0-or-later. This executable intentionally remains
 * separate from astro_remote.elf.
 */
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "astrolock_offact.h"

#define ASTROLOCK_PORT 45823
#define HTTP_BUF 16384
#define ACCOUNT_FLAGS_ACTIVATED 4098

static volatile sig_atomic_t g_running = 1;

static void on_signal(int sig)
{
    (void)sig;
    g_running = 0;
}

static int send_all(int fd, const void *data, size_t len)
{
    const unsigned char *p = (const unsigned char *)data;
    while(len){
        ssize_t n = send(fd, p, len, 0);
        if(n > 0){p += n; len -= (size_t)n; continue;}
        if(n < 0 && errno == EINTR)continue;
        return -1;
    }
    return 0;
}

static void send_response(int fd, const char *status, const char *type,
                          const char *body)
{
    char header[512];
    size_t len = body ? strlen(body) : 0;
    int n = snprintf(header, sizeof(header),
        "HTTP/1.1 %s\r\n"
        "Content-Type: %s\r\n"
        "Cache-Control: no-store\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n\r\n",
        status, type, len);
    if(n <= 0 || n >= (int)sizeof(header))return;
    (void)send_all(fd, header, (size_t)n);
    if(len)(void)send_all(fd, body, len);
}

static void send_json(int fd, const char *status, const char *body)
{
    send_response(fd, status, "application/json; charset=utf-8", body);
}

static void json_escape(char *out, size_t cap, const char *in)
{
    size_t j = 0;
    if(!out || cap == 0)return;
    if(!in){out[0] = '\0'; return;}
    for(size_t i = 0; in[i] && j + 1 < cap; i++){
        unsigned char c = (unsigned char)in[i];
        if(c == '"' || c == '\\'){
            if(j + 2 >= cap)break;
            out[j++] = '\\'; out[j++] = (char)c;
        }else if(c == '\n' || c == '\r' || c == '\t'){
            if(j + 2 >= cap)break;
            out[j++] = '\\';
            out[j++] = c == '\n' ? 'n' : (c == '\r' ? 'r' : 't');
        }else if(c >= 0x20 && c < 0x7f){
            out[j++] = (char)c;
        }else{
            out[j++] = '?';
        }
    }
    out[j] = '\0';
}

static int hex_value(char c)
{
    if(c >= '0' && c <= '9')return c - '0';
    if(c >= 'a' && c <= 'f')return c - 'a' + 10;
    if(c >= 'A' && c <= 'F')return c - 'A' + 10;
    return -1;
}

static void url_decode(char *out, size_t cap, const char *in)
{
    size_t j = 0;
    if(!out || cap == 0)return;
    while(in && *in && j + 1 < cap){
        if(*in == '%' && in[1] && in[2]){
            int a = hex_value(in[1]);
            int b = hex_value(in[2]);
            if(a >= 0 && b >= 0){out[j++] = (char)((a << 4) | b); in += 3; continue;}
        }
        out[j++] = *in == '+' ? ' ' : *in;
        in++;
    }
    out[j] = '\0';
}

static int form_value(const char *request, const char *key,
                      char *out, size_t out_cap)
{
    const char *body = strstr(request, "\r\n\r\n");
    char needle[96];
    const char *p;
    char encoded[256];
    size_t n = 0;

    if(!body || !key || !out || out_cap == 0)return 0;
    body += 4;
    snprintf(needle, sizeof(needle), "%s=", key);
    p = body;
    while((p = strstr(p, needle)) != NULL){
        if(p == body || p[-1] == '&')break;
        p += strlen(needle);
    }
    if(!p)return 0;
    p += strlen(needle);
    while(*p && *p != '&' && n + 1 < sizeof(encoded))encoded[n++] = *p++;
    encoded[n] = '\0';
    url_decode(out, out_cap, encoded);
    return 1;
}

static int account_activated(uint64_t id, const char *type, int flags)
{
    return id != 0 && type && !strcmp(type, "np") && flags == ACCOUNT_FLAGS_ACTIVATED;
}

static void send_accounts(int fd)
{
    char json[12288];
    size_t used = 0;
    int count = 0;

    used += (size_t)snprintf(json + used, sizeof(json) - used,
        "{\"ok\":true,\"service\":\"astrolock\",\"port\":%d,\"accounts\":[",
        ASTROLOCK_PORT);

    for(int slot = 1; slot <= ASTRO_OFFACT_ACCOUNT_MAX; slot++){
        char name[ASTRO_OFFACT_NAME_MAX];
        char type[ASTRO_OFFACT_TYPE_MAX];
        char safe_name[ASTRO_OFFACT_NAME_MAX * 2];
        char safe_type[ASTRO_OFFACT_TYPE_MAX * 2];
        uint64_t id = 0;
        uint64_t proposed;
        int flags = 0;
        int rc_name;
        int rc_id;
        int rc_type;
        int rc_flags;
        int active;
        int n;

        memset(name, 0, sizeof(name));
        memset(type, 0, sizeof(type));
        rc_name = astro_offact_get_name(slot, name);
        if(rc_name != 0 || !name[0])continue;
        rc_id = astro_offact_get_id(slot, &id);
        rc_type = astro_offact_get_type(slot, type);
        rc_flags = astro_offact_get_flags(slot, &flags);
        proposed = id ? id : astro_offact_generate_id(name);
        active = rc_id == 0 && rc_type == 0 && rc_flags == 0 &&
                 account_activated(id, type, flags);
        json_escape(safe_name, sizeof(safe_name), name);
        json_escape(safe_type, sizeof(safe_type), type);

        n = snprintf(json + used, sizeof(json) - used,
            "%s{\"slot\":%d,\"name\":\"%s\",\"account_id\":\"0x%016llx\","
            "\"proposed_id\":\"0x%016llx\",\"type\":\"%s\",\"flags\":%d,"
            "\"activated\":%s,\"read_ok\":%s}",
            count ? "," : "", slot, safe_name,
            (unsigned long long)id, (unsigned long long)proposed,
            safe_type, flags, active ? "true" : "false",
            (rc_id == 0 && rc_type == 0 && rc_flags == 0) ? "true" : "false");
        if(n < 0 || (size_t)n >= sizeof(json) - used)break;
        used += (size_t)n;
        count++;
    }

    (void)snprintf(json + used, sizeof(json) - used,
        "],\"count\":%d,\"writes_on_load\":false}", count);
    send_json(fd, "200 OK", json);
}

static void send_activation(int fd, const char *request)
{
    char slot_s[16];
    char confirm[32];
    char id_s[64];
    char name[ASTRO_OFFACT_NAME_MAX];
    char new_type[ASTRO_OFFACT_TYPE_MAX];
    char safe_name[ASTRO_OFFACT_NAME_MAX * 2];
    uint64_t old_id = 0;
    uint64_t new_id = 0;
    uint64_t verify_id = 0;
    int verify_flags = 0;
    int slot;
    int rc;
    char verify_type[ASTRO_OFFACT_TYPE_MAX];
    char json[1024];

    if(!form_value(request, "slot", slot_s, sizeof(slot_s)) ||
       !form_value(request, "confirm", confirm, sizeof(confirm)) ||
       strcmp(confirm, "ACTIVATE")){
        send_json(fd, "400 Bad Request",
            "{\"ok\":false,\"error\":\"explicit_confirmation_required\"}");
        return;
    }

    slot = atoi(slot_s);
    if(slot < 1 || slot > ASTRO_OFFACT_ACCOUNT_MAX){
        send_json(fd, "400 Bad Request",
            "{\"ok\":false,\"error\":\"invalid_slot\"}");
        return;
    }

    memset(name, 0, sizeof(name));
    if(astro_offact_get_name(slot, name) != 0 || !name[0]){
        send_json(fd, "404 Not Found",
            "{\"ok\":false,\"error\":\"account_not_found\"}");
        return;
    }

    (void)astro_offact_get_id(slot, &old_id);

    if(form_value(request, "account_id", id_s, sizeof(id_s)) && id_s[0]){
        char *end = NULL;
        unsigned long long parsed = strtoull(id_s, &end, 0);
        if(!end || *end != '\0' || parsed == 0){
            send_json(fd, "400 Bad Request",
                "{\"ok\":false,\"error\":\"invalid_account_id\"}");
            return;
        }
        new_id = (uint64_t)parsed;
    }else{
        new_id = old_id ? old_id : astro_offact_generate_id(name);
    }

    memset(new_type, 0, sizeof(new_type));
    snprintf(new_type, sizeof(new_type), "np");

    rc = astro_offact_set_id(slot, new_id);
    if(rc != 0){
        snprintf(json, sizeof(json),
            "{\"ok\":false,\"error\":\"set_account_id_failed\",\"rc\":%d}", rc);
        send_json(fd, "500 Internal Server Error", json);
        return;
    }

    rc = astro_offact_set_type(slot, new_type);
    if(rc != 0){
        snprintf(json, sizeof(json),
            "{\"ok\":false,\"error\":\"set_account_type_failed\",\"rc\":%d}", rc);
        send_json(fd, "500 Internal Server Error", json);
        return;
    }

    rc = astro_offact_set_flags(slot, ACCOUNT_FLAGS_ACTIVATED);
    if(rc != 0){
        snprintf(json, sizeof(json),
            "{\"ok\":false,\"error\":\"set_account_flags_failed\",\"rc\":%d}", rc);
        send_json(fd, "500 Internal Server Error", json);
        return;
    }

    memset(verify_type, 0, sizeof(verify_type));
    if(astro_offact_get_id(slot, &verify_id) != 0 ||
       astro_offact_get_type(slot, verify_type) != 0 ||
       astro_offact_get_flags(slot, &verify_flags) != 0 ||
       verify_id != new_id || strcmp(verify_type, "np") ||
       verify_flags != ACCOUNT_FLAGS_ACTIVATED){
        send_json(fd, "500 Internal Server Error",
            "{\"ok\":false,\"error\":\"activation_verify_failed\"}");
        return;
    }

    json_escape(safe_name, sizeof(safe_name), name);
    snprintf(json, sizeof(json),
        "{\"ok\":true,\"activated\":true,\"slot\":%d,\"name\":\"%s\","
        "\"account_id\":\"0x%016llx\",\"type\":\"np\",\"flags\":%d,"
        "\"reboot_required\":true}",
        slot, safe_name, (unsigned long long)new_id, ACCOUNT_FLAGS_ACTIVATED);
    send_json(fd, "200 OK", json);
}

static const char PAGE[] =
"<!doctype html><html lang='pt-BR'><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Astro Lock</title><style>"
"*{box-sizing:border-box}body{margin:0;background:#070b11;color:#edf4ff;font-family:Arial,sans-serif}"
"header{padding:18px 20px;border-bottom:1px solid #253244;background:#0d141e;display:flex;gap:12px;align-items:center}"
"header b{letter-spacing:.08em}.tag{font-size:11px;padding:5px 8px;border:1px solid #35506b;border-radius:999px;color:#9fc3df}"
"main{max-width:900px;margin:auto;padding:20px}.hero,.card{border:1px solid #28394f;background:#0f1722;border-radius:14px;padding:18px;margin-bottom:14px}"
"h1{font-size:22px;margin:0 0 8px}p{color:#91a4ba;line-height:1.45}.warn{color:#e8c985}.ok{color:#99e3b3}.bad{color:#ff9ca9}"
".card h3{margin:0 0 12px}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px;font-size:13px}"
".kv{padding:8px 10px;background:#0a111a;border-radius:8px}.kv span{display:block;color:#72879d;font-size:10px;margin-bottom:3px}"
"input{width:100%;margin-top:12px;padding:11px;border-radius:8px;border:1px solid #35475e;background:#080e16;color:#edf4ff;font-family:monospace}"
"button{margin-top:10px;padding:11px 13px;border-radius:8px;border:1px solid #405978;background:#162437;color:#edf4ff;font-weight:800;cursor:pointer}"
"button.primary{width:100%;background:#dceafa;color:#081018;border-color:#dceafa}button.danger{background:#25171b;border-color:#67343d;color:#ffc0c7}"
"button:disabled{opacity:.5;cursor:not-allowed}#msg{white-space:pre-wrap;margin-top:12px}.empty{padding:24px;text-align:center;color:#8397ac}"
"@media(max-width:650px){.grid{grid-template-columns:1fr}main{padding:12px}}"
"</style></head><body><header><b>ASTRO LOCK</b><span class='tag'>OffAct Web</span></header><main>"
"<section class='hero'><h1>Ativação offline da conta PS5</h1>"
"<p>Serviço separado do Astro Remote. A lista abaixo é somente leitura. O registro só é alterado quando você confirmar uma conta.</p>"
"<p class='warn'>Depois de ativar, reinicie o PS5 antes de fazer o pareamento do Remote Play.</p><div id='msg'></div></section>"
"<div id='accounts'><div class='empty'>Lendo contas...</div></div>"
"<button id='shutdown' class='danger'>ENCERRAR ASTRO LOCK</button>"
"<script>"
"const root=location.pathname.endsWith('/')?location.pathname:location.pathname+'/';"
"const api=p=>root+p;const esc=s=>String(s??'').replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));"
"const msg=(t,c='')=>{const e=document.getElementById('msg');e.className=c;e.textContent=t};"
"async function load(){try{const r=await fetch(api('api/accounts'),{cache:'no-store'}),j=await r.json();const box=document.getElementById('accounts');box.innerHTML='';"
"if(!j.accounts||!j.accounts.length){box.innerHTML=\"<div class='empty'>Nenhuma conta encontrada.</div>\";return;}"
"j.accounts.forEach(a=>{const d=document.createElement('section');d.className='card';d.innerHTML=\"<h3>\"+esc(a.name)+\" <span class='\"+(a.activated?'ok':'warn')+\"'>\"+(a.activated?'ATIVADA':'NÃO ATIVADA')+\"</span></h3>\"+"
"\"<div class='grid'><div class='kv'><span>SLOT</span>\"+a.slot+\"</div><div class='kv'><span>TYPE</span>\"+esc(a.type||'-')+\"</div><div class='kv'><span>FLAGS</span>\"+a.flags+\"</div><div class='kv'><span>ACCOUNT ID ATUAL</span>\"+esc(a.account_id)+\"</div></div>\"+"
"\"<input class='aid' value='\"+esc(a.proposed_id)+\"' aria-label='Account ID'><button class='primary'>\"+(a.activated?'REATIVAR / REAPLICAR':'ATIVAR ESTA CONTA')+\"</button>\";"
"d.querySelector('button').onclick=async()=>{const id=d.querySelector('.aid').value.trim();if(!confirm('Ativar '+a.name+' no slot '+a.slot+'? O PS5 deverá ser reiniciado depois.'))return;"
"msg('Aplicando ativação...','warn');const body='slot='+encodeURIComponent(a.slot)+'&account_id='+encodeURIComponent(id)+'&confirm=ACTIVATE';"
"const rr=await fetch(api('api/activate'),{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});const x=await rr.json();"
"if(x.ok){msg('Conta ativada com sucesso. Reinicie o PS5 antes do Remote Play.','ok');await load()}else msg('Falha: '+(x.error||'erro desconhecido')+(x.rc!==undefined?' rc='+x.rc:''),'bad')};box.appendChild(d)});}catch(e){msg('Não foi possível ler o Astro Lock: '+e,'bad')}}"
"document.getElementById('shutdown').onclick=async()=>{if(!confirm('Encerrar o Astro Lock?'))return;try{await fetch(api('shutdown'),{method:'POST'});msg('Astro Lock encerrado.','ok')}catch(e){msg('Astro Lock encerrando.','ok')}};load();"
"</script></main></body></html>";

static int read_request(int fd, char *buf, size_t cap)
{
    size_t used = 0;
    int content_length = 0;
    size_t header_len = 0;

    if(!buf || cap < 2)return -1;
    buf[0] = '\0';
    while(used + 1 < cap){
        ssize_t n = recv(fd, buf + used, cap - used - 1, 0);
        if(n > 0){
            char *end;
            used += (size_t)n;
            buf[used] = '\0';
            end = strstr(buf, "\r\n\r\n");
            if(end){
                char *cl = strstr(buf, "Content-Length:");
                header_len = (size_t)((end + 4) - buf);
                if(cl)content_length = atoi(cl + 15);
                if(used >= header_len + (size_t)(content_length > 0 ? content_length : 0))break;
            }
            continue;
        }
        if(n == 0)break;
        if(errno == EINTR)continue;
        return -1;
    }
    return (int)used;
}

int main(void)
{
    int server;
    int opt = 1;
    struct sockaddr_in addr;

    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);
    signal(SIGPIPE, SIG_IGN);

    server = socket(AF_INET, SOCK_STREAM, 0);
    if(server < 0)return 20;
    (void)setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(ASTROLOCK_PORT);
    if(bind(server, (struct sockaddr *)&addr, sizeof(addr)) < 0){close(server); return 21;}
    if(listen(server, 4) < 0){close(server); return 22;}

    while(g_running){
        int client = accept(server, NULL, NULL);
        char request[HTTP_BUF];
        char method[16];
        char path[1024];
        char version[16];
        int n;

        if(client < 0){if(errno == EINTR)continue; break;}
        n = read_request(client, request, sizeof(request));
        if(n <= 0){close(client); continue;}
        method[0] = path[0] = version[0] = '\0';
        (void)sscanf(request, "%15s %1023s %15s", method, path, version);

        if(!strcmp(method, "GET") && (!strcmp(path, "/") || !strcmp(path, "/index.html"))){
            send_response(client, "200 OK", "text/html; charset=utf-8", PAGE);
        }else if(!strcmp(method, "GET") && !strcmp(path, "/health")){
            send_json(client, "200 OK", "{\"ok\":true,\"service\":\"astrolock\",\"writes_on_load\":false}");
        }else if(!strcmp(method, "GET") && !strcmp(path, "/api/accounts")){
            send_accounts(client);
        }else if(!strcmp(method, "POST") && !strcmp(path, "/api/activate")){
            send_activation(client, request);
        }else if(!strcmp(method, "POST") && !strcmp(path, "/shutdown")){
            send_json(client, "200 OK", "{\"ok\":true,\"stopping\":true}");
            g_running = 0;
        }else if(!strcmp(method, "GET") && !strcmp(path, "/favicon.ico")){
            send_response(client, "204 No Content", "text/plain", "");
        }else{
            send_json(client, "404 Not Found", "{\"ok\":false,\"error\":\"not_found\"}");
        }
        close(client);
    }

    close(server);
    return 0;
}
