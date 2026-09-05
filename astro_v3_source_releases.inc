/* Astro V3 installable GitHub ELF sources.
 *
 * Adds Spectrum Library as a built-in source and upgrades the existing
 * Sources screen so any GitHub repository with an .elf asset in its latest
 * release can be installed/updated directly through Astro.
 */

static void astro_ensure_spectrum_source(void)
{
  beta_seed_sources();
  FILE *f=fopen(ASTRO_SOURCE_FILE,"r");
  char line[2048];int found=0;
  while(f&&fgets(line,sizeof(line),f)){
    if(strstr(line,"Phoenixx1202/Spectrum-Library")||strstr(line,"SpectrumLibrary")){found=1;break;}
  }
  if(f)fclose(f);
  if(found)return;
  f=fopen(ASTRO_SOURCE_FILE,"a");
  if(!f)return;
  fprintf(f,"Spectrum Library\thttps://github.com/Phoenixx1202/Spectrum-Library/releases\tSpectrumLibrary\n");
  fclose(f);
}

static void beta_sources_api(int fd)
{
  astro_ensure_spectrum_source();
  beta_sources_api_catalog_base(fd);
}

static const char astro_source_releases_js[] =
"<script>"
"(function(){"
"function ghReleaseApi(u){let m=String(u||'').match(/github\\.com\\/([^/]+)\\/([^/#?]+)/i);return m?`https://api.github.com/repos/${m[1]}/${m[2].replace(/\\.git$/,'')}/releases/latest`:null}"
"function srcLocal(s){let p=String(s.pattern||'').toLowerCase();return P.find(x=>String(x.name||'').toLowerCase().includes(p))||null}"
"function cleanVersion(v){return String(v||'').replace(/^v/i,'').toLowerCase()}"
"function stableSourceFile(s,r){let l=srcLocal(s);if(l)return l.name;let b=String(s.pattern||s.name||r?.assetName||'payload').normalize('NFD').replace(/[\\u0300-\\u036f]/g,'').replace(/[^a-zA-Z0-9._-]+/g,'-').replace(/^-+|-+$/g,'');if(!/\\.elf$/i.test(b))b+='.elf';return b}"
"release=async function(s){let api=ghReleaseApi(s.url);if(!api)return null;try{let r=await fetch(api,{cache:'no-store'});if(!r.ok)return null;let j=await r.json(),a=(j.assets||[]).find(x=>/\\.elf$/i.test(String(x.name||'')))||null,d=a&&a.digest?String(a.digest).replace(/^sha256:/i,''):'';return{tag:j.tag_name||j.name||'?',url:j.html_url||s.url,assetName:a?a.name:'',assetUrl:a?a.browser_download_url:'',checksum:d}}catch(e){return null}}"
"async function sourceMeta(){try{return await fetch('/api/payload-sources',{cache:'no-store'}).then(r=>r.json())}catch(e){return{installed:[]}}}"
"async function installSource(idx){let s=S.find(x=>Number(x.index)===Number(idx));if(!s)return;let rr=rel[s.index]&&rel[s.index].assetUrl?rel[s.index]:await release(s);if(!rr||!rr.assetUrl){alert('A release mais recente dessa fonte não possui arquivo .elf.');return}let file=stableSourceFile(s,rr),u='/api/payload-sources/add?action=catalog-install&name='+encodeURIComponent(s.name||file)+'&url='+encodeURIComponent(rr.assetUrl)+'&file='+encodeURIComponent(file)+'&version='+encodeURIComponent(rr.tag||'?')+'&source='+encodeURIComponent(s.url||'')+'&checksum='+encodeURIComponent(rr.checksum||'');if(typeof note==='function')note('Baixando '+(s.name||file)+'...');let res=await fetch(u,{method:'POST'}),j=await res.json().catch(()=>({}));if(!res.ok){alert('Falha ao instalar: '+(j.error||res.status));return}await loadAll(false);T='sources';renderSources()}"
"renderSources=async function(){sectionTitle.style.display='none';body.className='';await Promise.all(S.map(async s=>{if(!rel[s.index]||!rel[s.index].assetUrl)rel[s.index]=await release(s)}));let meta=await sourceMeta(),ins=meta.installed||[];body.innerHTML=`<div class='subtabs'><button class='on'>Fontes de ELF</button><button onclick=\\\"tab('web')\\\">Web fallback</button><button class='primary' onclick='addSource()'>＋ Adicionar fonte por URL</button></div><div class='notice good' style='margin-bottom:10px'>Repos GitHub com um arquivo .elf na release mais recente viram fontes instaláveis automaticamente. O Spectrum Library já vem cadastrado.</div>`+S.map(s=>{let rr=rel[s.index],l=srcLocal(s),file=stableSourceFile(s,rr),iv=ins.find(z=>String(z.file||'').toLowerCase()===String(file).toLowerCase()),lv=l&&typeof localVer==='function'?localVer(l.name):'',current=!!(l&&rr&&((iv&&cleanVersion(iv.version)===cleanVersion(rr.tag))||(lv&&cleanVersion(lv)===cleanVersion(rr.tag)))),act=!rr||!rr.assetUrl?'':(l?(current?'Reinstalar':'Atualizar'):'Instalar'),status=!rr?`<span class='pill'>Versão indisponível</span>`:rr.assetUrl?(current?`<span class='pill run'>Atualizado ${E(rr.tag)}</span>`:l?`<span class='pill ver'>Atualização ${E(rr.tag)}</span>`:`<span class='pill ver'>Disponível ${E(rr.tag)}</span>`):`<span class='pill'>Sem ELF na release</span>`;return `<article class='sourceCard'><div class='sourceTop'><div><b>${E(s.name)}</b><div class='sourceUrl'>${E(s.url)}</div><div class='muted' style='margin-top:5px'>Associa ELFs contendo: ${E(s.pattern)}${rr&&rr.assetName?' · Asset: '+E(rr.assetName):''}</div></div><div class='toolbar'>${status}<a class='btn sm' target='_blank' href='${E(s.url)}'>Abrir fonte</a>${act?`<button class='primary sm' onclick='installSource(${s.index})'>${act}</button>`:''}<button class='danger sm' onclick='delSource(${s.index})'>Remover</button></div></div></article>`}).join('')}`}
"})();"
"</script>";

static void astro_source_releases_send_text(int fd,const char *status,const char *ctype,const char *extra,const char *body)
{
  if(!body||!ctype||!strstr(ctype,"text/html")||!strstr(body,"<h1>Payloads</h1>")||!strstr(body,"payloadPanel")){
    astro_catalog_send_text_fixed(fd,status,ctype,extra,body);
    return;
  }
  size_t bl=strlen(body),jl=strlen(astro_source_releases_js),cap=bl+jl+2048;
  char *out=malloc(cap);if(!out){astro_catalog_send_text_fixed(fd,status,ctype,extra,body);return;}
  snprintf(out,cap,"%s",body);
  char *end=strstr(out,"</body>");
  if(end){size_t pos=(size_t)(end-out);memmove(out+pos+jl,out+pos,strlen(out+pos)+1);memcpy(out+pos,astro_source_releases_js,jl);}
  astro_catalog_send_text_fixed(fd,status,ctype,extra,out);
  free(out);
}
