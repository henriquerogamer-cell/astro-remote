/* Astro V3 process control.
 * Adds safe process termination from the Processos page.
 */

static int astro_process_lookup(pid_t pid, astro_proc_t *out)
{
  astro_proc_t *p=NULL;size_t n=0;if(v3_process_snapshot(&p,&n)!=0)return 0;
  int found=0;for(size_t i=0;i<n;i++){if(p[i].pid==pid){if(out)*out=p[i];found=1;break;}}
  free(p);return found;
}

static int astro_process_protected(const astro_proc_t *p)
{
  if(!p)return 1;
  return p->pid<=1||p->pid==getpid();
}

static void v3_processes_api(int fd)
{
  astro_proc_t *p=NULL;size_t n=0;if(v3_process_snapshot(&p,&n)!=0){send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  size_t cap=1024*1024,used=0;char*j=malloc(cap);if(!j){free(p);send_text(fd,"500 Internal Server Error","application/json",NULL,"{\"ok\":false}");return;}
  used+=snprintf(j+used,cap-used,"{\"ok\":true,\"items\":[");
  for(size_t i=0;i<n&&used+560<cap;i++){
    char ec[256];json_escape(p[i].comm,ec,sizeof(ec));
    used+=snprintf(j+used,cap-used,"%s{\"pid\":%d,\"ppid\":%d,\"uid\":%u,\"state\":%d,\"rss\":%llu,\"comm\":\"%s\",\"protected\":%s}",i?",":"",p[i].pid,p[i].ppid,(unsigned)p[i].uid,p[i].state,p[i].rss,ec,astro_process_protected(&p[i])?"true":"false");
  }
  snprintf(j+used,cap-used,"]}");free(p);send_text(fd,"200 OK","application/json; charset=utf-8",NULL,j);free(j);
}

static void astro_process_terminate_api(int fd,const char *target)
{
  char ps[32]={0},fs[16]={0};
  if(!query_value(target,"pid",ps,sizeof(ps))){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"pid\"}");return;}
  char *end=NULL;long v=strtol(ps,&end,10);if(!end||*end||v<=1||v>INT32_MAX){send_text(fd,"400 Bad Request","application/json",NULL,"{\"ok\":false,\"error\":\"pid\"}");return;}
  astro_proc_t p;if(!astro_process_lookup((pid_t)v,&p)){send_text(fd,"404 Not Found","application/json",NULL,"{\"ok\":false,\"error\":\"not_running\"}");return;}
  if(astro_process_protected(&p)){send_text(fd,"403 Forbidden","application/json",NULL,"{\"ok\":false,\"error\":\"protected\"}");return;}
  query_value(target,"force",fs,sizeof(fs));int force=!strcmp(fs,"1")||!strcasecmp(fs,"true");int sig=force?SIGKILL:SIGTERM;
  int rc=kill(p.pid,sig);if(rc!=0){char j[160];snprintf(j,sizeof(j),"{\"ok\":false,\"error\":\"kill\",\"errno\":%d}",errno);send_text(fd,"500 Internal Server Error","application/json",NULL,j);return;}
  char note[160];snprintf(note,sizeof(note),"ASTRO: %s PID %d %s",p.comm,p.pid,force?"forçado":"encerrado");notify(note);
  char j[160];snprintf(j,sizeof(j),"{\"ok\":true,\"pid\":%d,\"signal\":%d,\"forced\":%s}",p.pid,sig,force?"true":"false");send_text(fd,"200 OK","application/json",NULL,j);
}

static void astro_processes_router(int fd,const char *target,const char *method)
{
  if(!strncmp(target,"/api/processes/terminate",24)){
    if(strcmp(method,"POST")){send_text(fd,"405 Method Not Allowed","application/json",NULL,"{\"ok\":false,\"error\":\"method\"}");return;}
    astro_process_terminate_api(fd,target);return;
  }
  v3_processes_api(fd);
}

static void v3_processes_page(int fd)
{
  char page[62000];v3_page_start(page,sizeof(page),"Processos","processes");const char*b=
  "<div class='head'><div><h1>Processos</h1><p>Snapshot real dos processos do PS5. Encerre processos ativos sem sair do Astro.</p></div><button onclick='loadP()'>↻ Atualizar</button></div>"
  "<section class='panel'><div class='notice' style='margin-bottom:11px'>O Astro e o PID 1 são protegidos. O botão tenta encerrar normalmente primeiro; encerramento forçado só aparece se o processo continuar ativo.</div><div class='toolbar' style='margin-bottom:11px'><input id='q' class='search' placeholder='Filtrar processo...' oninput='render()'><span id='st' class='muted'></span></div><div class='tablewrap'><table><thead><tr><th>Processo</th><th>PID</th><th>PPID</th><th>UID</th><th>RSS</th><th>Ações</th></tr></thead><tbody id='r'></tbody></table></div></section>"
  "<script>let A=[];const F=n=>{let u=['B','KB','MB','GB'],i=0;while(n>=1024&&i<3){n/=1024;i++}return n.toFixed(i?1:0)+' '+u[i]};const E=s=>String(s==null?'':s).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));async function loadP(){try{let j=await fetch('/api/processes',{cache:'no-store'}).then(x=>x.json());A=j.items||[];st.textContent=A.length+' processos';render()}catch(e){st.textContent='Falha ao carregar processos'}}function render(){let x=q.value.toLowerCase();r.innerHTML=A.filter(a=>!x||String(a.comm||'').toLowerCase().includes(x)||String(a.pid).includes(x)).map(a=>`<tr><td>${E(a.comm)}</td><td>${a.pid}</td><td>${a.ppid}</td><td>${a.uid}</td><td>${F(a.rss)}</td><td>${a.protected?`<span class='badge'>Protegido</span>`:`<button class='danger sm' onclick='stopPid(${a.pid})'>Encerrar</button>`}</td></tr>`).join('')}async function stopPid(pid){let a=A.find(x=>x.pid===pid);if(!a||a.protected)return;if(!confirm('Encerrar '+a.comm+' (PID '+pid+')?'))return;st.textContent='Encerrando '+a.comm+'...';let res=await fetch('/api/processes/terminate?pid='+pid,{method:'POST'}),j=await res.json().catch(()=>({}));if(!res.ok){alert('Não foi possível encerrar: '+(j.error||res.status));await loadP();return}await new Promise(ok=>setTimeout(ok,700));let snap=await fetch('/api/processes',{cache:'no-store'}).then(x=>x.json()).catch(()=>({items:[]}));let alive=(snap.items||[]).some(x=>x.pid===pid);if(alive&&confirm(a.comm+' ainda está ativo. Forçar encerramento?')){let fr=await fetch('/api/processes/terminate?pid='+pid+'&force=1',{method:'POST'}),fj=await fr.json().catch(()=>({}));if(!fr.ok)alert('Falha ao forçar encerramento: '+(fj.error||fr.status));await new Promise(ok=>setTimeout(ok,350))}await loadP()}loadP()</script>";
  strncat(page,b,sizeof(page)-strlen(page)-1);v3_page_end(page,sizeof(page));send_text(fd,"200 OK","text/html; charset=utf-8",NULL,page);
}
