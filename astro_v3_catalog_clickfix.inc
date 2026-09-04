/* Correct the Payload Catalog HTML/JavaScript injected into the active page.
 *
 * v3.0.121 had two independent presentation bugs:
 *  - the toolbar button was emitted with escaped quotes in the final HTML;
 *  - the CE() HTML-escape helper contained a quote sequence that is valid as
 *    a C string but becomes invalid JavaScript in the browser.
 *
 * Keep the catalog backend untouched and repair only the generated page.
 */
static void astro_catalog_repair_js(char *page,size_t cap)
{
  if(!page||!cap)return;
  char *a=strstr(page,"const CE=s=>");
  char *b=a?strstr(a,"function catDev"):NULL;
  if(!a||!b||b<=a)return;

  /* Deliberately avoid a literal double-quote inside this JavaScript helper.
   * String.fromCharCode(34) keeps the C -> JS -> HTML escaping layers simple. */
  const char *fixed="function CE(s){return String(s??'').replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(new RegExp(String.fromCharCode(34),'g'),'&quot;').replace(/'/g,'&#39;')}";
  size_t old_len=(size_t)(b-a),new_len=strlen(fixed),cur=strlen(page);
  if(new_len>old_len&&cur+(new_len-old_len)+1>cap)return;
  memmove(a+new_len,b,strlen(b)+1);
  memcpy(a,fixed,new_len);
}

static void astro_catalog_send_text_fixed(int fd,const char *status,const char *ctype,const char *extra,const char *body)
{
  if(!body||!ctype||!strstr(ctype,"text/html")||!strstr(body,"<h1>Payloads</h1>")||!strstr(body,"payloadPanel")){
    send_text(fd,status,ctype,extra,body);
    return;
  }

  size_t bl=strlen(body),cl=strlen(astro_catalog_css),jl=strlen(astro_catalog_js);
  size_t cap=bl+cl+jl+4096;
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
    size_t pos=(size_t)(end-out),add=jl;
    memmove(out+pos+add,out+pos,strlen(out+pos)+1);
    memcpy(out+pos,astro_catalog_js,add);
  }

  astro_catalog_repair_js(out,cap);
  send_text(fd,status,ctype,extra,out);
  free(out);
}
