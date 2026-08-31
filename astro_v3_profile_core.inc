/* Astro v3 profile/avatar extension.
 * Keeps profile work native to Astro and only allows writes to the
 * foreground user's PS5 profile cache.
 */

static int v3_profile_dir_for_user(int user_id, char *out, size_t out_sz)
{
  if (!out || out_sz < 32) return -1;
  snprintf(out, out_sz, "/system_data/priv/cache/profile/0x%08X", (unsigned)user_id);
  return 0;
}

static int v3_foreground_user(char uid[16], char name[64], char avatar[256])
{
  int u = 0;
  sceUserServiceInitialize(NULL);
  if (sceUserServiceGetForegroundUser(&u) < 0) return -1;

  char lower[16], upper[16];
  snprintf(lower, sizeof(lower), "%08x", (unsigned)u);
  snprintf(upper, sizeof(upper), "%08X", (unsigned)u);
  snprintf(uid, 16, "%s", upper);

  memset(name, 0, 64);
  char np[256];
  snprintf(np, sizeof(np), "/user/home/%s/username.dat", lower);
  int f = open(np, O_RDONLY);
  if (f < 0) {
    snprintf(np, sizeof(np), "/user/home/%s/username.dat", upper);
    f = open(np, O_RDONLY);
  }
  if (f >= 0) {
    read(f, name, 16);
    close(f);
    name[16] = 0;
  }
  if (!name[0]) snprintf(name, 64, "User %s", upper);

  char dir[256], p[320];
  v3_profile_dir_for_user(u, dir, sizeof(dir));
  snprintf(p, sizeof(p), "%s/avatar.png", dir);
  struct stat st;
  if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
    snprintf(avatar, 256, "%s", p);
    return 0;
  }
  snprintf(p, sizeof(p), "%s/picture.png", dir);
  if (stat(p, &st) == 0 && S_ISREG(st.st_mode)) {
    snprintf(avatar, 256, "%s", p);
    return 0;
  }

  /* Keep the canonical destination even when no image exists yet. */
  snprintf(avatar, 256, "%s/avatar.png", dir);
  return 0;
}

static int v3_avatar_name_allowed(const char *name)
{
  if (!name || !*name || strchr(name, '/') || strstr(name, "..")) return 0;
  static const char *allowed[] = {
    "avatar.png", "picture.png",
    "avatar64.dds", "avatar128.dds", "avatar260.dds", "avatar440.dds",
    "picture64.dds", "picture128.dds", "picture260.dds", "picture440.dds"
  };
  for (size_t i = 0; i < sizeof(allowed)/sizeof(allowed[0]); ++i)
    if (!strcmp(name, allowed[i])) return 1;
  return 0;
}

static void v3_profile_avatar_serve(int fd)
{
  char uid[16], name[64], av[256];
  if (v3_foreground_user(uid, name, av) != 0) {
    send_text(fd, "404 Not Found", "text/plain", NULL, "");
    return;
  }
  struct stat st;
  if (stat(av, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 16*1024*1024) {
    send_text(fd, "404 Not Found", "text/plain", NULL, "");
    return;
  }
  int f = open(av, O_RDONLY);
  if (f < 0) { send_text(fd, "404 Not Found", "text/plain", NULL, ""); return; }
  void *buf = malloc((size_t)st.st_size);
  if (!buf) { close(f); send_text(fd, "500 Internal Server Error", "text/plain", NULL, ""); return; }
  size_t off = 0;
  while (off < (size_t)st.st_size) {
    ssize_t n = read(f, (char*)buf + off, (size_t)st.st_size - off);
    if (n <= 0) break;
    off += (size_t)n;
  }
  close(f);
  if (off != (size_t)st.st_size) { free(buf); send_text(fd, "500 Internal Server Error", "text/plain", NULL, ""); return; }
  send_bytes(fd, "200 OK", "image/png", "Cache-Control: no-cache, no-store\r\n", buf, off);
  free(buf);
}

static void v3_profile_avatar_prepare_api(int fd)
{
  int u = 0;
  sceUserServiceInitialize(NULL);
  if (sceUserServiceGetForegroundUser(&u) < 0 || !u) {
    send_text(fd, "412 Precondition Failed", "application/json", NULL,
              "{\"ok\":false,\"error\":\"no_foreground_user\"}");
    return;
  }

  char dir[256];
  v3_profile_dir_for_user(u, dir, sizeof(dir));
  v3_mkdirs("/system_data/priv/cache/profile");
  if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
    send_text(fd, "500 Internal Server Error", "application/json", NULL,
              "{\"ok\":false,\"error\":\"profile_dir\"}");
    return;
  }

  time_t now = time(NULL);
  char uid[16], bak[512];
  snprintf(uid, sizeof(uid), "%08X", (unsigned)u);
  snprintf(bak, sizeof(bak), "%s/%s/avatar-%lld", ASTRO_PROFILE_BACKUP_DIR, uid, (long long)now);
  v3_mkdirs(bak);

  static const char *names[] = {
    "avatar.png", "picture.png", "online.json",
    "avatar64.dds", "avatar128.dds", "avatar260.dds", "avatar440.dds",
    "picture64.dds", "picture128.dds", "picture260.dds", "picture440.dds"
  };
  int copied = 0;
  for (size_t i=0; i<sizeof(names)/sizeof(names[0]); ++i) {
    char src[512], dst[512]; struct stat st;
    snprintf(src, sizeof(src), "%s/%s", dir, names[i]);
    if (stat(src, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    snprintf(dst, sizeof(dst), "%s/%s", bak, names[i]);
    if (v3_copy_file(src, dst) == 0) copied++;
  }

  char eb[1024], j[1400];
  json_escape(bak, eb, sizeof(eb));
  snprintf(j, sizeof(j), "{\"ok\":true,\"backup\":\"%s\",\"files\":%d}", eb, copied);
  send_text(fd, "200 OK", "application/json; charset=utf-8", NULL, j);
}

static int v3_profile_write_body(int client_fd, const char *req, size_t req_len,
                                 size_t header_len, const char *dst)
{
  long total = content_length(req);
  if (total <= 0 || total > 20*1024*1024) return -1;

  char tmp[640];
  snprintf(tmp, sizeof(tmp), "%s.astro-tmp", dst);
  int out = open(tmp, O_CREAT|O_TRUNC|O_WRONLY, 0644);
  if (out < 0) return -2;

  size_t have = req_len > header_len ? req_len - header_len : 0;
  if (have > (size_t)total) have = (size_t)total;
  size_t off = 0;
  while (off < have) {
    ssize_t w = write(out, req + header_len + off, have - off);
    if (w <= 0) { close(out); unlink(tmp); return -3; }
    off += (size_t)w;
  }

  long remain = total - (long)have;
  char *buf = malloc(256*1024);
  if (!buf) { close(out); unlink(tmp); return -4; }
  int rc = 0;
  while (remain > 0) {
    size_t want = remain > 256*1024 ? 256*1024 : (size_t)remain;
    ssize_t n = recv(client_fd, buf, want, 0);
    if (n <= 0) { rc = -5; break; }
    size_t p = 0;
    while (p < (size_t)n) {
      ssize_t w = write(out, buf + p, (size_t)n - p);
      if (w <= 0) { rc = -6; break; }
      p += (size_t)w;
    }
    if (rc) break;
    remain -= n;
  }
  free(buf); fsync(out); close(out);
  if (rc) { unlink(tmp); return rc; }
  if (rename(tmp, dst) != 0) { unlink(tmp); return -7; }
  return 0;
}

static void v3_profile_avatar_upload_api(int fd, const char *target,
                                         const char *req, size_t req_len, size_t header_len)
{
  char fn[64];
  if (!query_value(target, "name", fn, sizeof(fn)) || !v3_avatar_name_allowed(fn)) {
    send_text(fd, "400 Bad Request", "application/json", NULL,
              "{\"ok\":false,\"error\":\"bad_name\"}");
    return;
  }

  int u = 0;
  sceUserServiceInitialize(NULL);
  if (sceUserServiceGetForegroundUser(&u) < 0 || !u) {
    send_text(fd, "412 Precondition Failed", "application/json", NULL,
              "{\"ok\":false,\"error\":\"no_foreground_user\"}");
    return;
  }
  char dir[256], dst[512];
  v3_profile_dir_for_user(u, dir, sizeof(dir));
  if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
    send_text(fd, "500 Internal Server Error", "application/json", NULL,
              "{\"ok\":false,\"error\":\"profile_dir\"}");
    return;
  }
  snprintf(dst, sizeof(dst), "%s/%s", dir, fn);
  int rc = v3_profile_write_body(fd, req, req_len, header_len, dst);
  char j[160];
  snprintf(j, sizeof(j), "{\"ok\":%s,\"rc\":%d}", rc==0?"true":"false", rc);
  send_text(fd, rc==0?"200 OK":"500 Internal Server Error", "application/json", NULL, j);
}

static void v3_profile_avatar_finish_api(int fd)
{
  char uid[16], name[64], av[256];
  if (v3_foreground_user(uid, name, av) != 0) {
    send_text(fd, "500 Internal Server Error", "application/json", NULL,
              "{\"ok\":false}");
    return;
  }
  notify("ASTRO: avatar aplicado | recarregue a sessão se o PS5 mantiver cache");
  send_text(fd, "200 OK", "application/json; charset=utf-8", NULL,
            "{\"ok\":true,\"note\":\"avatar files applied; shell cache may refresh after session reload\"}");
}
