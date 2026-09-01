/* Temporary output repair for the Explorer page.
 * The initial Explorer build shipped one invalid JavaScript statement in the
 * storage bootstrap: `continue,used=...`. That parse error prevents the whole
 * script from running, so neither storage stats nor /data contents render.
 * Keep the Explorer source intact and repair the generated HTML before send.
 */
static void explorer_send_text_fixed(int fd, const char *status, const char *type,
                                     const char *extra, const char *body)
{
  static const char bad[] =
    "if(!x)continue,used=x.total-x.free,p=x.total?Math.round(used*100/x.total):0;";
  static const char good[] =
    "if(!x)continue;let used=x.total-x.free,p=x.total?Math.round(used*100/x.total):0;";

  if (!body) {
    send_text(fd, status, type, extra, body);
    return;
  }

  const char *hit = strstr(body, bad);
  if (!hit) {
    send_text(fd, status, type, extra, body);
    return;
  }

  size_t before = (size_t)(hit - body);
  size_t after = strlen(hit + strlen(bad));
  size_t need = before + strlen(good) + after + 1;
  char *fixed = malloc(need);
  if (!fixed) {
    send_text(fd, status, type, extra, body);
    return;
  }

  memcpy(fixed, body, before);
  memcpy(fixed + before, good, strlen(good));
  memcpy(fixed + before + strlen(good), hit + strlen(bad), after + 1);
  send_text(fd, status, type, extra, fixed);
  free(fixed);
}
