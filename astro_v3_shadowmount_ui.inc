/* ShadowMountPlus scan-path manager for Astro V3.
 * Uses Astro's existing authenticated file API, so no extra backend routes are
 * required. Only scanpath= lines in /data/shadowmount/config.ini are changed;
 * every other ShadowMountPlus option is preserved verbatim.
 */

#define ASTRO_SHADOW_CONFIG "/data/shadowmount/config.ini"

static void v3_settings_page(int fd,const char*msg)
{
  char page[52000];
  v3_page_start(page,sizeof(page),"Ajustes","settings");
  char b[42000];
  snprintf(b,sizeof(b),
    "<div class='head'><div><h1>Ajustes</h1><p>Segurança, manutenção e integração com ShadowMountPlus.</p></div></div>"
    "<section class='grid'>"
    "<article class='panel c6'><div class='kicker'>SEGURANÇA</div>%s<form method='POST' action='/settings/password'><div class='row'><input name='password' type='password' minlength='8' required placeholder='Nova senha'><button class='primary'>Trocar senha</button></div></form></article>"
    "<article class='panel c6'><div class='kicker'>SERVIDOR</div><p class='muted'>Usuário: %s<br>Porta externa do Astro: %d<br>As portas internas das Web UIs ficam escondidas pelo gateway /web/.</p><form method='POST' action='/shutdown'><button class='danger'>Encerrar Astro</button></form></article>"
    "<article class='panel full'><div class='kicker'>SHADOWMOUNTPLUS</div><div class='head' style='margin-top:6px;margin-bottom:10px'><div><h2 style='margin:0'>Pastas de varredura</h2><p>Escolha as pastas que o ShadowMountPlus procura para montar jogos automaticamente.</p></div><span id='smMode' class='badge'>Lendo...</span></div>"
    "<div id='smNotice' class='notice' style='margin-bottom:12px'>Carregando /data/shadowmount/config.ini...</div>"
    "<div class='toolbar' style='margin-bottom:12px'><input id='smPath' class='search' placeholder='/data/pegasus'><button class='primary' onclick='smAdd()'>＋ Adicionar caminho</button><button onclick=\"smQuick('/data/pegasus')\">Pegasus</button><button onclick=\"smQuick('/data/etaHEN/games')\">etaHEN/games</button><button class='green' onclick='smPegasusEta()'>Pegasus + etaHEN</button><button class='ghost' onclick='smReset()'>Usar padrões</button></div>"
    "<div id='smPaths'></div><p class='muted' style='margin-top:12px'>O Astro altera somente as linhas <code>scanpath=</code> de <code>/data/shadowmount/config.ini</code>. As demais opções são preservadas. Se o ShadowMountPlus já estiver rodando, reinicie o payload para garantir a aplicação.</p></article>"
    "</section>"
    "<script>"
    "const SMCFG='/data/shadowmount/config.ini';let SMRAW='';let SMP=[];const sme=s=>String(s??'').replace(/[&<>\\\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\\\"':'&quot;',\"'\":'&#39;'}[c]));"
    "function smParse(t){return t.split(/\\r?\\n/).map(x=>x.trim()).filter(x=>x.startsWith('scanpath=')).map(x=>x.slice(9).trim()).filter(Boolean).filter((x,i,a)=>a.indexOf(x)===i)}"
    "async function smRead(){let r=await fetch('/api/files/download?path='+encodeURIComponent(SMCFG),{cache:'no-store'});if(r.status===404)return'';if(!r.ok)throw new Error('HTTP '+r.status);return await r.text()}"
    "async function smWrite(paths){let lines=SMRAW.split(/\\r?\\n/).filter(x=>!x.trim().startsWith('scanpath='));while(lines.length&&lines[lines.length-1]==='')lines.pop();let out=lines.join('\\n');if(out)out+='\\n';if(paths.length)out+=paths.map(p=>'scanpath='+p).join('\\n')+'\\n';let r=await fetch('/api/files/upload?path='+encodeURIComponent(SMCFG),{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:out});if(!r.ok)throw new Error('HTTP '+r.status);SMRAW=out;SMP=paths;smRender()}"
    "function smRender(){const custom=SMP.length>0,mode=document.getElementById('smMode'),n=document.getElementById('smNotice'),box=document.getElementById('smPaths');mode.textContent=custom?'CAMINHOS PERSONALIZADOS':'LOCAIS PADRÃO';mode.className='badge '+(custom?'run':'');n.className='notice '+(custom?'':'good');n.innerHTML=custom?'O ShadowMountPlus está usando <b>somente</b> os caminhos abaixo. Se quiser manter etaHEN/games, deixe essa pasta na lista.':'Nenhum <code>scanpath=</code> personalizado. O ShadowMountPlus usa os locais padrão internos.';box.innerHTML='';if(!SMP.length){box.innerHTML=\"<div class='notice'>Nenhum caminho personalizado configurado.</div>\";return}for(const p of SMP){const d=document.createElement('div');d.className='savegroup';d.innerHTML=`<div class='savehead'><div class='gamefallback'>📁</div><div class='saveinfo'><b><code>${sme(p)}</code></b><span>scanpath</span></div><button class='sm danger' onclick='smRemove(${JSON.stringify(p)})'>Remover</button></div>`;box.appendChild(d)}}"
    "async function smLoad(){try{SMRAW=await smRead();SMP=smParse(SMRAW);smRender()}catch(e){const n=document.getElementById('smNotice');n.className='notice bad';n.textContent='Não foi possível ler a configuração: '+e.message}}"
    "async function smAddPath(p){p=(p||'').trim().replace(/\\/+$/,'');if(!p||p[0]!=='/'||p.includes('..')||p.includes('=')||/[\\r\\n]/.test(p)){alert('Caminho inválido.');return}if(SMP.includes(p))return;try{await smWrite([...SMP,p])}catch(e){alert('Não foi possível salvar: '+e.message)}}"
    "function smAdd(){const e=document.getElementById('smPath'),p=e.value;smAddPath(p).then(()=>{e.value=''})}function smQuick(p){document.getElementById('smPath').value=p;smAdd()}"
    "async function smPegasusEta(){let a=[...SMP];for(const p of ['/data/etaHEN/games','/data/pegasus'])if(!a.includes(p))a.push(p);try{await smWrite(a)}catch(e){alert('Não foi possível salvar: '+e.message)}}"
    "async function smRemove(p){if(!confirm('Remover '+p+' da varredura do ShadowMountPlus?'))return;try{await smWrite(SMP.filter(x=>x!==p))}catch(e){alert('Não foi possível salvar: '+e.message)}}"
    "async function smReset(){if(!confirm('Remover todos os scanpath personalizados e voltar aos locais padrão?'))return;try{await smWrite([])}catch(e){alert('Não foi possível restaurar os padrões: '+e.message)}}"
    "smLoad();</script>",
    msg?msg:"",g_user,ASTRO_PORT);
  strncat(page,b,sizeof(page)-strlen(page)-1);
  v3_page_end(page,sizeof(page));
  send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}
