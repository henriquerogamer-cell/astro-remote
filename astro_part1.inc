static volatile int g_running = 1;
static char g_user[MAX_USER] = "astro";
static char g_pass[MAX_PASS] = {0};
static char g_session[MAX_TOKEN] = {0};

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

static void notify(const char *msg)
{
  notify_request_t req;
  memset(&req, 0, sizeof(req));
  snprintf(req.message, sizeof(req.message), "%s", msg ? msg : "");
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

static void ensure_dir(const char *path)
{
  struct stat st;
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return;
  mkdir(path, 0755);
}

static int random_bytes(uint8_t *out, size_t len)
{
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd >= 0) {
    size_t got = 0;
    while (got < len) {
      ssize_t n = read(fd, out + got, len - got);
      if (n <= 0) break;
      got += (size_t)n;
    }
    close(fd);
    if (got == len) return 0;
  }

  uint64_t x = (uint64_t)time(NULL) ^ ((uint64_t)getpid() << 32) ^ (uintptr_t)out;
  for (size_t i = 0; i < len; ++i) {
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    out[i] = (uint8_t)(x & 0xff);
  }
  return 0;
}

static void make_secret(char *out, size_t out_sz, size_t chars)
{
  static const char alphabet[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz23456789";
  uint8_t buf[64];
  if (!out || out_sz == 0) return;
  if (chars >= out_sz) chars = out_sz - 1;
  if (chars > sizeof(buf)) chars = sizeof(buf);
  random_bytes(buf, chars);
  for (size_t i = 0; i < chars; ++i)
    out[i] = alphabet[buf[i] % (sizeof(alphabet) - 1)];
  out[chars] = '\0';
}

static void save_auth(void)
{
  ensure_dir(ASTRO_DIR);
  FILE *f = fopen(AUTH_FILE, "w");
  if (!f) return;
  fprintf(f, "user=%s\npass=%s\n", g_user, g_pass);
  fclose(f);
  chmod(AUTH_FILE, 0600);
}

static void load_auth(void)
{
  ensure_dir(ASTRO_DIR);
  FILE *f = fopen(AUTH_FILE, "r");
  if (f) {
    char line[256];
    while (fgets(line, sizeof(line), f)) {
      char *nl = strpbrk(line, "\r\n");
      if (nl) *nl = '\0';
      if (strncmp(line, "user=", 5) == 0 && line[5])
        snprintf(g_user, sizeof(g_user), "%s", line + 5);
      else if (strncmp(line, "pass=", 5) == 0 && line[5])
        snprintf(g_pass, sizeof(g_pass), "%s", line + 5);
    }
    fclose(f);
  }

  if (!g_pass[0]) {
    make_secret(g_pass, sizeof(g_pass), 12);
    save_auth();
    char msg[256];
    snprintf(msg, sizeof(msg), "ASTRO pronto | usuario: %s | senha inicial: %s", g_user, g_pass);
    notify(msg);
  }

  make_secret(g_session, sizeof(g_session), 40);
}

static int send_all(int fd, const void *data, size_t len)
{
  const uint8_t *p = (const uint8_t *)data;
  while (len) {
    ssize_t n = send(fd, p, len, 0);
    if (n <= 0) return -1;
    p += (size_t)n;
    len -= (size_t)n;
  }
  return 0;
}

static void send_bytes(int fd, const char *status, const char *type,
                       const char *extra, const void *body, size_t len)
{
  char hdr[2048];
  snprintf(hdr, sizeof(hdr),
           "HTTP/1.1 %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %zu\r\n"
           "Cache-Control: no-store\r\n"
           "%s"
           "Connection: close\r\n\r\n",
           status, type ? type : "application/octet-stream", len,
           extra ? extra : "");
  send_all(fd, hdr, strlen(hdr));
  if (len && body) send_all(fd, body, len);
}

static void send_text(int fd, const char *status, const char *type,
                      const char *extra, const char *body)
{
  if (!body) body = "";
  send_bytes(fd, status, type, extra, body, strlen(body));
}

static void redirect_to(int fd, const char *location)
{
  char h[512];
  snprintf(h, sizeof(h), "Location: %s\r\n", location);
  send_text(fd, "302 Found", "text/plain; charset=utf-8", h, "");
}

static void url_decode(char *dst, size_t dst_sz, const char *src)
{
  size_t used = 0;
  if (!dst || dst_sz == 0) return;
  while (src && *src && used + 1 < dst_sz) {
    if (*src == '%' && isxdigit((unsigned char)src[1]) && isxdigit((unsigned char)src[2])) {
      char hx[3] = {src[1], src[2], 0};
      dst[used++] = (char)strtol(hx, NULL, 16);
      src += 3;
    } else if (*src == '+') {
      dst[used++] = ' ';
      src++;
    } else {
      dst[used++] = *src++;
    }
  }
  dst[used] = '\0';
}

static void json_escape(const char *src, char *dst, size_t dst_sz)
{
  size_t used = 0;
  if (!src) src = "";
  if (!dst || !dst_sz) return;
  while (*src && used + 2 < dst_sz) {
    unsigned char c = (unsigned char)*src++;
    if (c == '\\' || c == '\"') {
      dst[used++] = '\\';
      dst[used++] = (char)c;
    } else if (c == '\n' || c == '\r' || c == '\t') {
      dst[used++] = '\\';
      dst[used++] = (c == '\n') ? 'n' : (c == '\r' ? 'r' : 't');
    } else if (c >= 0x20) {
      dst[used++] = (char)c;
    }
  }
  dst[used] = '\0';
}
