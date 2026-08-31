static const char *find_header(const char *req, const char *name)
{
  size_t nlen = strlen(name);
  const char *p = strstr(req, "\r\n");
  if (!p) return NULL;
  p += 2;
  while (*p) {
    const char *e = strstr(p, "\r\n");
    if (!e || e == p) break;
    if ((size_t)(e - p) > nlen && strncasecmp(p, name, nlen) == 0 && p[nlen] == ':') {
      p += nlen + 1;
      while (*p == ' ' || *p == '\t') p++;
      return p;
    }
    p = e + 2;
  }
  return NULL;
}

static size_t header_value_copy(const char *req, const char *name, char *out, size_t out_sz)
{
  const char *p = find_header(req, name);
  if (!p || !out_sz) return 0;
  const char *e = strstr(p, "\r\n");
  if (!e) return 0;
  size_t n = (size_t)(e - p);
  if (n >= out_sz) n = out_sz - 1;
  memcpy(out, p, n);
  out[n] = '\0';
  return n;
}

static int parse_request_line(const char *req, char *method, size_t msz,
                              char *target, size_t tsz)
{
  const char *sp1 = strchr(req, ' ');
  if (!sp1) return -1;
  const char *sp2 = strchr(sp1 + 1, ' ');
  if (!sp2) return -1;
  size_t mn = (size_t)(sp1 - req);
  size_t tn = (size_t)(sp2 - sp1 - 1);
  if (mn >= msz || tn >= tsz) return -1;
  memcpy(method, req, mn); method[mn] = 0;
  memcpy(target, sp1 + 1, tn); target[tn] = 0;
  return 0;
}

static int read_request(int fd, char *buf, size_t cap, size_t *out_len, size_t *header_len)
{
  size_t used = 0;
  while (used + 1 < cap) {
    ssize_t n = recv(fd, buf + used, cap - used - 1, 0);
    if (n <= 0) return -1;
    used += (size_t)n;
    buf[used] = 0;
    char *h = strstr(buf, "\r\n\r\n");
    if (h) {
      *out_len = used;
      *header_len = (size_t)(h - buf) + 4;
      return 0;
    }
  }
  return -2;
}

static long content_length(const char *req)
{
  char v[64];
  if (!header_value_copy(req, "Content-Length", v, sizeof(v))) return 0;
  return strtol(v, NULL, 10);
}

static const char *query_value(const char *target, const char *key, char *out, size_t out_sz)
{
  const char *q = strchr(target, '?');
  if (!q) return NULL;
  q++;
  size_t klen = strlen(key);
  while (*q) {
    if (strncmp(q, key, klen) == 0 && q[klen] == '=') {
      const char *v = q + klen + 1;
      const char *e = strchr(v, '&');
      char tmp[2048];
      size_t n = e ? (size_t)(e - v) : strlen(v);
      if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
      memcpy(tmp, v, n); tmp[n] = 0;
      url_decode(out, out_sz, tmp);
      return out;
    }
    q = strchr(q, '&');
    if (!q) break;
    q++;
  }
  return NULL;
}

static int form_value(const char *body, const char *key, char *out, size_t out_sz)
{
  if (!body) return 0;
  size_t klen = strlen(key);
  const char *p = body;
  while (*p) {
    if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
      const char *v = p + klen + 1;
      const char *e = strchr(v, '&');
      char tmp[512];
      size_t n = e ? (size_t)(e - v) : strlen(v);
      if (n >= sizeof(tmp)) n = sizeof(tmp) - 1;
      memcpy(tmp, v, n); tmp[n] = 0;
      url_decode(out, out_sz, tmp);
      return 1;
    }
    p = strchr(p, '&');
    if (!p) break;
    p++;
  }
  return 0;
}

static int logged_in(const char *req)
{
  char cookie[2048];
  if (!header_value_copy(req, "Cookie", cookie, sizeof(cookie))) return 0;
  char needle[128];
  snprintf(needle, sizeof(needle), "astro_session=%s", g_session);
  return strstr(cookie, needle) != NULL;
}

static int port_open(int port)
{
  int s = socket(AF_INET, SOCK_STREAM, 0);
  if (s < 0) return 0;
  struct sockaddr_in a;
  memset(&a, 0, sizeof(a));
  a.sin_family = AF_INET;
  a.sin_port = htons((uint16_t)port);
  a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  int ok = (connect(s, (struct sockaddr *)&a, sizeof(a)) == 0);
  close(s);
  return ok;
}
