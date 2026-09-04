/* Correct HTML injection for the Astro payload catalog button.
 * The first catalog injector emitted backslashes before the onclick quotes,
 * so the button rendered but its click handler was not valid HTML.
 */
static void astro_catalog_send_text_fixed(int fd,const char *status,const char *ctype,const char *extra,const char *body)
{
  if(!body||!ctype||!strstr(ctype,"text/html")||!strstr(body,"<h1>Payloads</h1>")||!strstr(body,"payloadPanel")){
    send_text(fd,status,ctype,extra,body);
    return;
  }

  size_t bl=strlen(body),cl=strlen(astro_catalog_css),jl=strlen(astro_catalog_js);
  size_t cap=bl+cl+jl+2048;
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

  send_text(fd,status,ctype,extra,out);
  free(out);
}
