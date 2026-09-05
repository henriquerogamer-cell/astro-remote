/* Final Payload Catalog bridge.
 *
 * Keep the catalog self-contained in the active Payloads page: registered
 * GitHub ELF sources are merged into CAT after the base catalog loads, and
 * the Add Source control is rendered by the catalog itself. This avoids
 * relying on the older source-page injection order.
 */

static const char astro_catalog_native_sources_js[] =
"<script>"
"(function(){"
"function catRepoKey(u){try{let x=new URL(String(u||''));if(String(x.hostname).toLowerCase()!=='github.com')return'';let p=x.pathname.split('/').filter(Boolean);if(p.length<2)return'';let r=String(p[1]);if(r.toLowerCase().endsWith('.git'))r=r.slice(0,-4);return(x.origin+'/'+p[0]+'/'+r).toLowerCase()}catch(e){return''}}"
"function catSourceExisting(s){let p=String((s&&s.pattern)||(s&&s.name)||'').toLowerCase();if(!p)return null;let a=(P||[]).filter(function(x){return String(x.name||'').toLowerCase().includes(p)});return a.find(function(x){return !!x.running})||a[0]||null}"
"function catSourceFile(s,a){let old=catSourceExisting(s);if(old&&old.name)return String(old.name);let b=String((s&&s.pattern)||(s&&s.name)||(a&&a.name)||'payload');try{b=b.normalize('NFD').replace(/[\\u0300-\\u036f]/g,'')}catch(e){}b=b.replace(/[^a-zA-Z0-9._-]+/g,'-').replace(/^-+|-+$/g,'');if(!/\\.elf$/i.test(b))b+='.elf';return b||'payload.elf'}"
"async function catSourceRelease(s){let key=catRepoKey(s&&s.url);if(!key)return null;let parts=key.split('/'),owner=parts[3],repo=parts[4];try{let r=await fetch('https://api.github.com/repos/'+encodeURIComponent(owner)+'/'+encodeURIComponent(repo)+'/releases/latest',{cache:'no-store'});if(!r.ok)return null;let j=await r.json(),a=(j.assets||[]).find(function(x){return /\\.elf$/i.test(String(x.name||''))});if(!a)return null;let sum=a.digest?String(a.digest).replace(/^sha256:/i,''):'';return{tag:j.tag_name||j.name||'latest',asset:a.name||'',url:a.browser_download_url||'',checksum:sum}}catch(e){return null}}"
"async function catMergeRegisteredSources(){let m=await fetch('/api/payload-sources',{cache:'no-store'}).then(function(r){return r.json()});let items=m.items||[],seen=new Set((CAT||[]).map(function(x){return catRepoKey(x.source)||String(x.name||'').toLowerCase()}));for(let s of items){let key=catRepoKey(s.url)||String(s.name||'').toLowerCase();if(seen.has(key))continue;let rr=await catSourceRelease(s);if(!rr||!rr.url)continue;CAT.push({name:s.name||s.pattern||rr.asset||'Fonte ELF',filename:catSourceFile(s,rr),url:rr.url,description:'ELF publicado pela fonte GitHub cadastrada no Astro. A versão acompanha automaticamente a release mais recente.',version:rr.tag||'latest',category:'Sources',checksum:rr.checksum||'',source:s.url||'',source_pattern:s.pattern||'',astro_source:true});seen.add(key)}}"
"function catRebuildCategories(){if(typeof catCategory==='undefined'||!catCategory)return;let cats=[...new Set((CAT||[]).map(function(x){return x.category||'Other'}))].sort();catCategory.innerHTML='<option value=\"all\">Todas as categorias</option>'+cats.map(function(x){return '<option value=\"'+CE(x)+'\">'+CE(x)+'</option>'}).join('')}"
"function catInstallNativeSourceBridge(){if(typeof renderCatalog!=='function'||typeof loadCatalog!=='function'||typeof catPaint!=='function'||typeof catFile!=='function'||typeof catInstall!=='function'){setTimeout(catInstallNativeSourceBridge,60);return}"
"let oldCatFile=catFile;catFile=function(x){return x&&x.filename?String(x.filename):oldCatFile(x)};"
"let oldLoadCatalog=loadCatalog;loadCatalog=async function(force){await oldLoadCatalog(force);try{await catMergeRegisteredSources();catRebuildCategories();catLoaded=true;catPaint('atlas + fontes')}catch(e){console.log('Astro catalog source merge',e)}};"
"let oldCatInstall=catInstall;catInstall=async function(i,quiet){let x=CAT[i],before=x&&x.astro_source?catLocal(x):null,wasRunning=!!(before&&before.running),oldName=before?String(before.name||''):'';let ok=await oldCatInstall(i,quiet);if(!ok||quiet||!wasRunning)return ok;let v=x&&x.version?String(x.version):'nova versão';if(confirm((x&&x.name?x.name:'Payload')+' foi atualizado no disco para '+v+'. Reiniciar agora para carregar a versão nova?')){await fetch('/api/payloads/stop?name='+encodeURIComponent(oldName),{method:'POST'});await new Promise(function(r){setTimeout(r,550)});await loadAll(false);let fresh=catLocal(x);if(!fresh){alert('Atualizado no disco, mas o Astro não encontrou o ELF para reiniciar.');return ok}let rr=await fetch('/api/payloads/launch?path='+encodeURIComponent(fresh.path),{method:'POST'});if(!rr.ok)alert('Atualizado, mas falhou ao reiniciar o payload.');else{if(typeof note==='function')note((x.name||'Payload')+' reiniciado em '+v+'.');setTimeout(function(){loadAll(true)},800)}}return ok};"
"let oldRenderCatalog=renderCatalog;renderCatalog=function(){oldRenderCatalog();try{let tabs=body.querySelector('.subtabs');if(tabs&&!document.getElementById('catAddSource')){let b=document.createElement('button');b.id='catAddSource';b.className='primary';b.textContent='＋ Adicionar fonte';b.onclick=async function(){if(typeof addSource!=='function')return;await addSource();T='catalog';renderCatalog()};tabs.appendChild(b)}}catch(e){console.log('Astro catalog add-source button',e)}};"
"}"
"if(document.readyState==='complete')setTimeout(catInstallNativeSourceBridge,0);else window.addEventListener('load',function(){setTimeout(catInstallNativeSourceBridge,0)});"
"})();"
"</script>";

static void astro_catalog_native_sources_send_text(int fd,const char *status,const char *ctype,const char *extra,const char *body)
{
  if(!body||!ctype||!strstr(ctype,"text/html")||!strstr(body,"<h1>Payloads</h1>")||!strstr(body,"payloadPanel")){
    send_text(fd,status,ctype,extra,body);
    return;
  }

  size_t bl=strlen(body),cl=strlen(astro_catalog_css),jl=strlen(astro_catalog_js),nl=strlen(astro_catalog_native_sources_js);
  size_t cap=bl+cl+jl+nl+8192;
  char *out=malloc(cap);
  if(!out){send_text(fd,status,ctype,extra,body);return;}
  snprintf(out,cap,"%s",body);

  const char *needle="<input id='up' type='file' accept='.elf' hidden>";
  char *p=strstr(out,needle);
  if(p){
    size_t pos=(size_t)(p-out)+strlen(needle);
    const char *btn="<button onclick=\"tab('catalog')\">☄ Catálogo</button>";
    size_t add=strlen(btn);
    memmove(out+pos+add,out+pos,strlen(out+pos)+1);
    memcpy(out+pos,btn,add);
  }

  char *end=strstr(out,"</head>");
  if(end){
    size_t pos=(size_t)(end-out),add=cl;
    memmove(out+pos+add,out+pos,strlen(out+pos)+1);
    memcpy(out+pos,astro_catalog_css,add);
  }

  end=strstr(out,"</body>");
  if(end){
    size_t pos=(size_t)(end-out),add=jl+nl;
    memmove(out+pos+add,out+pos,strlen(out+pos)+1);
    memcpy(out+pos,astro_catalog_js,jl);
    memcpy(out+pos+jl,astro_catalog_native_sources_js,nl);
  }

  astro_catalog_repair_js(out,cap);
  send_text(fd,status,ctype,extra,out);
  free(out);
}
