/* Astro V3 proxy compatibility for SPAs that use absolute /api/* URLs.
 *
 * The beta router classifies /api/ as an Astro-native root before consulting
 * the Referer. That is correct for Astro pages, but wrong for a proxied web
 * app such as Pegasus: its fetch('/api/...') then lands in Astro and gets the
 * plain-text 404 "Astro: rota nao encontrada", which the app tries to parse as
 * JSON.
 *
 * This comparator is injected only while compiling the beta router. It makes
 * the generic native-root test ignore exactly the five-byte "/api/" root. The
 * concrete Astro API route checks use longer strings (/api/status, /api/users,
 * etc.) and therefore retain normal strncmp behavior. Result:
 *
 *   request from /web/<slug>/ + target /api/foo -> beta_web_referer proxies it
 *   request from an Astro page + target /api/foo -> no web Referer, falls
 *   through and is handled by Astro's normal /api/foo routes.
 */

static int astro_proxyaware_strncmp(const char *a,const char *b,size_t n)
{
  if(a&&b&&n==5&&memcmp(b,"/api/",5)==0&&memcmp(a,"/api/",5)==0)
    return 1;
  return strncmp(a,b,n);
}
