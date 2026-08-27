#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <time.h>
#include <ctype.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#define ASTRO_PORT 45821

/* Keep the current credentials for this build. */
#define ASTRO_USER "henrique"
#define ASTRO_PASS "P@ndora2024"

#define ACCOUNT_NUMB_MAX 16
#define ACCOUNT_TYPE_MAX 17
#define ACCOUNT_NAME_MAX 100

#define REG_REMOTEPLAY_ENABLE 1098973184
#define PAIR_TIMEOUT_SECONDS 300

static int running = 1;

static uint32_t g_remote_pin = 0;
static int g_pairing_active = 0;
static int g_pairing_done = 0;
static int g_pairing_error = 0;
static int g_pairing_status = 0;
static int g_pairing_native_error = 0;
static time_t g_pairing_expires = 0;
static int g_remote_init_ret = 0;

typedef struct notify_request {
  char useless1[45];
  char message[3075];
} notify_request_t;

typedef struct account_info {
  int valid;
  int slot;
  int user_id;
  char name[ACCOUNT_NAME_MAX];
  uint64_t account_id;
  char account_b64[13];
  char account_type[ACCOUNT_TYPE_MAX];
  int flags;
  int activated;
  int global_rp_enabled;
  int user_rp_enabled;
} account_info_t;

int sceKernelSendNotificationRequest(int, notify_request_t *, size_t, int);

int sceUserServiceInitialize(void *);
int sceUserServiceGetForegroundUser(int *);
int sceUserServiceGetUserName(int, char *, size_t);
int sceUserServiceTerminate(void);

int sceRegMgrGetInt(int, int *);
int sceRegMgrGetStr(int, char *, size_t);
int sceRegMgrGetBin(int, void *, size_t);
int sceRegMgrSetBin(int, const void *, size_t);
int sceRegMgrSetInt(int, int);
int sceRegMgrSetStr(int, const char *, size_t);

int sceRemoteplayInitialize(void *, size_t);
int sceRemoteplayGeneratePinCode(uint32_t *);
int sceRemoteplayConfirmDeviceRegist(int *, int *);
int sceRemoteplayNotifyPinCodeError(int);

static void notify(const char *message)
{
  notify_request_t req;
  memset(&req, 0, sizeof(req));
  snprintf(req.message, sizeof(req.message), "%s", message ? message : "");
  sceKernelSendNotificationRequest(0, &req, sizeof(req), 0);
}

/* ---------------- Registry helpers ---------------- */

static int ent_num(int slot, int base, int fallback)
{
  if (slot < 1 || slot > ACCOUNT_NUMB_MAX)
    return fallback;
  return (slot - 1) * 65536 + base;
}

static int key_user_id(int slot)
{
  return ent_num(slot, 125829376, 127140096);
}

static int key_account_name(int slot)
{
  return ent_num(slot, 125829632, 127140352);
}

static int key_account_id(int slot)
{
  return ent_num(slot, 125830400, 127141120);
}

static int key_account_flags(int slot)
{
  return ent_num(slot, 125831168, 127141888);
}

static int key_account_type(int slot)
{
  return ent_num(slot, 125874183, 127184903);
}

static int key_user_rp_enable(int slot)
{
  return ent_num(slot, 125859841, 127170561);
}

static int get_current_slot(int foreground_user)
{
  for (int slot = 1; slot <= ACCOUNT_NUMB_MAX; ++slot) {
    int registry_user = -1;
    if (sceRegMgrGetInt(key_user_id(slot), &registry_user) == 0 &&
        registry_user == foreground_user) {
      return slot;
    }
  }
  return -1;
}

static const char b64_table[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode_8(const uint8_t in[8], char out[13])
{
  int i = 0;
  int j = 0;

  while (i < 8) {
    int remain = 8 - i;
    uint8_t a = in[i++];
    uint8_t b = remain > 1 ? in[i++] : 0;
    uint8_t c = remain > 2 ? in[i++] : 0;

    out[j++] = b64_table[(a >> 2) & 0x3f];
    out[j++] = b64_table[((a & 0x03) << 4) | ((b >> 4) & 0x0f)];
    out[j++] = remain > 1
      ? b64_table[((b & 0x0f) << 2) | ((c >> 6) & 0x03)]
      : '=';
    out[j++] = remain > 2 ? b64_table[c & 0x3f] : '=';
  }

  out[12] = '\0';
}

static void account_id_to_base64(uint64_t id, char out[13])
{
  uint8_t raw[8];
  for (int i = 0; i < 8; ++i)
    raw[i] = (uint8_t)((id >> (i * 8)) & 0xff);
  base64_encode_8(raw, out);
}

static int load_account_info(account_info_t *info)
{
  if (!info)
    return -1;

  memset(info, 0, sizeof(*info));
  info->slot = -1;
  info->user_id = -1;

  int user_id = -1;
  if (sceUserServiceGetForegroundUser(&user_id) != 0)
    return -2;

  info->user_id = user_id;
  info->slot = get_current_slot(user_id);
  if (info->slot < 1 || info->slot > ACCOUNT_NUMB_MAX)
    return -3;

  if (sceUserServiceGetUserName(user_id, info->name, sizeof(info->name)) != 0 ||
      info->name[0] == '\0') {
    memset(info->name, 0, sizeof(info->name));
    sceRegMgrGetStr(key_account_name(info->slot), info->name, sizeof(info->name));
  }

  if (info->name[0] == '\0')
    snprintf(info->name, sizeof(info->name), "Usuario PS5");

  if (sceRegMgrGetBin(key_account_id(info->slot),
                      &info->account_id,
                      sizeof(info->account_id)) != 0) {
    info->account_id = 0;
  }

  sceRegMgrGetStr(key_account_type(info->slot),
                  info->account_type,
                  sizeof(info->account_type));
  sceRegMgrGetInt(key_account_flags(info->slot), &info->flags);
  sceRegMgrGetInt(REG_REMOTEPLAY_ENABLE, &info->global_rp_enabled);
  sceRegMgrGetInt(key_user_rp_enable(info->slot), &info->user_rp_enabled);

  info->activated = info->account_id != 0;
  account_id_to_base64(info->account_id, info->account_b64);
  info->valid = 1;
  return 0;
}

static uint64_t generate_account_id(const char *username)
{
  uint64_t base = 0x5EAF00DULL / 0xCA7F00DULL;

  if (username) {
    while (*username) {
      base = 0x100000001B3ULL * (base ^ (uint8_t)*username++);
    }
  }

  return base;
}

static int activate_current_account(account_info_t *out)
{
  account_info_t info;
  int ret = load_account_info(&info);
  if (ret != 0)
    return ret;

  if (info.account_id != 0) {
    if (out)
      *out = info;
    return 1;
  }

  uint64_t new_id = generate_account_id(info.name);
  if (new_id == 0)
    return -10;

  int r_id = sceRegMgrSetBin(key_account_id(info.slot),
                             &new_id,
                             sizeof(new_id));
  int r_type = sceRegMgrSetStr(key_account_type(info.slot),
                               "np",
                               ACCOUNT_TYPE_MAX);
  int r_flags = sceRegMgrSetInt(key_account_flags(info.slot), 4098);
  int r_user_rp = sceRegMgrSetInt(key_user_rp_enable(info.slot), 1);
  int r_global_rp = sceRegMgrSetInt(REG_REMOTEPLAY_ENABLE, 1);

  if (r_id || r_type || r_flags || r_user_rp || r_global_rp)
    return -20;

  ret = load_account_info(&info);
  if (ret != 0 || info.account_id == 0)
    return -21;

  if (out)
    *out = info;

  return 0;
}

/* ---------------- Remote Play helpers ---------------- */

static int ensure_remote_play_enabled(const account_info_t *info)
{
  if (!info || !info->valid)
    return -1;

  int ret = 0;
  int val = 0;

  if (sceRegMgrGetInt(REG_REMOTEPLAY_ENABLE, &val) != 0 || val != 1) {
    ret = sceRegMgrSetInt(REG_REMOTEPLAY_ENABLE, 1);
    if (ret != 0)
      return ret;
  }

  val = 0;
  if (sceRegMgrGetInt(key_user_rp_enable(info->slot), &val) != 0 || val != 1) {
    ret = sceRegMgrSetInt(key_user_rp_enable(info->slot), 1);
    if (ret != 0)
      return ret;
  }

  return 0;
}

static int generate_remote_pin(uint32_t *pin_out)
{
  account_info_t info;
  int ret = load_account_info(&info);
  if (ret != 0)
    return -100;

  if (!info.activated)
    return -101;

  ret = ensure_remote_play_enabled(&info);
  if (ret != 0)
    return ret;

  /*
   * OnionHEN tolerates a non-zero initialize result because the native
   * service may already be initialized. Keep the return for diagnostics,
   * but still try the real PIN call.
   */
  g_remote_init_ret = sceRemoteplayInitialize(NULL, 0);

  sceRemoteplayNotifyPinCodeError(1);

  uint32_t pin = 0;
  ret = sceRemoteplayGeneratePinCode(&pin);
  if (ret != 0)
    return ret;

  g_remote_pin = pin;
  g_pairing_active = 1;
  g_pairing_done = 0;
  g_pairing_error = 0;
  g_pairing_status = 0;
  g_pairing_native_error = 0;
  g_pairing_expires = time(NULL) + PAIR_TIMEOUT_SECONDS;

  if (pin_out)
    *pin_out = pin;

  return 0;
}

static void poll_pairing(void)
{
  if (!g_pairing_active)
    return;

  if (time(NULL) >= g_pairing_expires) {
    sceRemoteplayNotifyPinCodeError(1);
    g_pairing_active = 0;
    g_pairing_error = 2; /* timeout */
    return;
  }

  int pair_status = 0;
  int pair_error = 0;
  int ret = sceRemoteplayConfirmDeviceRegist(&pair_status, &pair_error);

  g_pairing_status = pair_status;
  g_pairing_native_error = pair_error;

  if (ret != 0) {
    g_pairing_active = 0;
    g_pairing_error = ret;
    return;
  }

  if (pair_status == 2) {
    g_pairing_active = 0;
    g_pairing_done = 1;
    notify("ASTRO: dispositivo Remote Play pareado");
  }
}

/* ---------------- Avatar: DXT5 DDS -> BMP ---------------- */

static uint16_t rd16(const uint8_t *p)
{
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
  return (uint32_t)p[0] |
         ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
  p[0] = (uint8_t)(v & 0xff);
  p[1] = (uint8_t)((v >> 8) & 0xff);
}

static void wr32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)(v & 0xff);
  p[1] = (uint8_t)((v >> 8) & 0xff);
  p[2] = (uint8_t)((v >> 16) & 0xff);
  p[3] = (uint8_t)((v >> 24) & 0xff);
}

static void rgb565(uint16_t c, uint8_t *r, uint8_t *g, uint8_t *b)
{
  uint8_t rr = (uint8_t)((c >> 11) & 0x1f);
  uint8_t gg = (uint8_t)((c >> 5) & 0x3f);
  uint8_t bb = (uint8_t)(c & 0x1f);

  *r = (uint8_t)((rr << 3) | (rr >> 2));
  *g = (uint8_t)((gg << 2) | (gg >> 4));
  *b = (uint8_t)((bb << 3) | (bb >> 2));
}

static int dxt5_dds_to_bmp(const uint8_t *dds,
                           size_t dds_len,
                           uint8_t **bmp_out,
                           size_t *bmp_len_out)
{
  if (!dds || dds_len < 128 || !bmp_out || !bmp_len_out)
    return -1;

  if (memcmp(dds, "DDS ", 4) != 0)
    return -2;

  uint32_t height = rd32(dds + 12);
  uint32_t width = rd32(dds + 16);

  if (width == 0 || height == 0 || width > 1024 || height > 1024)
    return -3;

  if (memcmp(dds + 84, "DXT5", 4) != 0)
    return -4;

  uint32_t blocks_x = (width + 3) / 4;
  uint32_t blocks_y = (height + 3) / 4;
  size_t required = 128 + (size_t)blocks_x * blocks_y * 16;

  if (dds_len < required)
    return -5;

  size_t rgba_len = (size_t)width * height * 4;
  uint8_t *rgba = (uint8_t *)calloc(1, rgba_len);
  if (!rgba)
    return -6;

  const uint8_t *src = dds + 128;

  for (uint32_t by = 0; by < blocks_y; ++by) {
    for (uint32_t bx = 0; bx < blocks_x; ++bx, src += 16) {
      uint8_t a0 = src[0];
      uint8_t a1 = src[1];
      uint8_t at[8];
      at[0] = a0;
      at[1] = a1;

      if (a0 > a1) {
        for (int i = 1; i <= 6; ++i)
          at[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1) / 7);
      } else {
        for (int i = 1; i <= 4; ++i)
          at[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1) / 5);
        at[6] = 0;
        at[7] = 255;
      }

      uint64_t alpha_bits = 0;
      for (int i = 0; i < 6; ++i)
        alpha_bits |= ((uint64_t)src[2 + i]) << (8 * i);

      uint16_t c0 = rd16(src + 8);
      uint16_t c1 = rd16(src + 10);
      uint8_t cr[4], cg[4], cb[4];

      rgb565(c0, &cr[0], &cg[0], &cb[0]);
      rgb565(c1, &cr[1], &cg[1], &cb[1]);

      cr[2] = (uint8_t)((2 * cr[0] + cr[1]) / 3);
      cg[2] = (uint8_t)((2 * cg[0] + cg[1]) / 3);
      cb[2] = (uint8_t)((2 * cb[0] + cb[1]) / 3);
      cr[3] = (uint8_t)((cr[0] + 2 * cr[1]) / 3);
      cg[3] = (uint8_t)((cg[0] + 2 * cg[1]) / 3);
      cb[3] = (uint8_t)((cb[0] + 2 * cb[1]) / 3);

      uint32_t color_bits = rd32(src + 12);

      for (int py = 0; py < 4; ++py) {
        for (int px = 0; px < 4; ++px) {
          uint32_t x = bx * 4 + (uint32_t)px;
          uint32_t y = by * 4 + (uint32_t)py;
          int pi = py * 4 + px;

          if (x >= width || y >= height)
            continue;

          uint8_t ci = (uint8_t)((color_bits >> (2 * pi)) & 0x03);
          uint8_t ai = (uint8_t)((alpha_bits >> (3 * pi)) & 0x07);

          uint8_t *dst = rgba + ((size_t)y * width + x) * 4;
          dst[0] = cr[ci];
          dst[1] = cg[ci];
          dst[2] = cb[ci];
          dst[3] = at[ai];
        }
      }
    }
  }

  size_t pixel_bytes = (size_t)width * height * 4;
  size_t bmp_len = 54 + pixel_bytes;
  uint8_t *bmp = (uint8_t *)malloc(bmp_len);

  if (!bmp) {
    free(rgba);
    return -7;
  }

  memset(bmp, 0, 54);
  bmp[0] = 'B';
  bmp[1] = 'M';
  wr32(bmp + 2, (uint32_t)bmp_len);
  wr32(bmp + 10, 54);
  wr32(bmp + 14, 40);
  wr32(bmp + 18, width);
  wr32(bmp + 22, height);
  wr16(bmp + 26, 1);
  wr16(bmp + 28, 32);
  wr32(bmp + 34, (uint32_t)pixel_bytes);

  uint8_t *dst = bmp + 54;
  for (int y = (int)height - 1; y >= 0; --y) {
    for (uint32_t x = 0; x < width; ++x) {
      const uint8_t *p = rgba + ((size_t)y * width + x) * 4;
      *dst++ = p[2];
      *dst++ = p[1];
      *dst++ = p[0];
      *dst++ = p[3];
    }
  }

  free(rgba);

  *bmp_out = bmp;
  *bmp_len_out = bmp_len;
  return 0;
}

static int load_avatar_bmp(const account_info_t *info,
                           uint8_t **bmp,
                           size_t *bmp_len)
{
  if (!info || !info->valid || !bmp || !bmp_len)
    return -1;

  char path[256];
  snprintf(path, sizeof(path),
           "/user/home/%08x/avatar/avatar64.dds",
           (unsigned int)info->user_id);

  FILE *f = fopen(path, "rb");
  if (!f)
    return -2;

  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -3;
  }

  long sz = ftell(f);
  if (sz <= 0 || sz > 1024 * 1024) {
    fclose(f);
    return -4;
  }

  rewind(f);

  uint8_t *dds = (uint8_t *)malloc((size_t)sz);
  if (!dds) {
    fclose(f);
    return -5;
  }

  size_t got = fread(dds, 1, (size_t)sz, f);
  fclose(f);

  if (got != (size_t)sz) {
    free(dds);
    return -6;
  }

  int ret = dxt5_dds_to_bmp(dds, got, bmp, bmp_len);
  free(dds);
  return ret;
}

/* ---------------- HTTP helpers ---------------- */

static void send_bytes(int client_fd,
                       const char *status,
                       const char *content_type,
                       const char *extra_headers,
                       const void *body,
                       size_t body_len)
{
  char header[1400];

  snprintf(header, sizeof(header),
           "HTTP/1.1 %s\r\n"
           "Content-Type: %s\r\n"
           "Content-Length: %zu\r\n"
           "Cache-Control: no-store\r\n"
           "%s"
           "Connection: close\r\n"
           "\r\n",
           status,
           content_type ? content_type : "application/octet-stream",
           body_len,
           extra_headers ? extra_headers : "");

  send(client_fd, header, strlen(header), 0);

  const uint8_t *p = (const uint8_t *)body;
  size_t left = body_len;

  while (left > 0) {
    ssize_t n = send(client_fd, p, left, 0);
    if (n <= 0)
      break;
    p += n;
    left -= (size_t)n;
  }
}

static void send_text(int client_fd,
                      const char *status,
                      const char *content_type,
                      const char *extra_headers,
                      const char *body)
{
  if (!body)
    body = "";
  send_bytes(client_fd,
             status,
             content_type,
             extra_headers,
             body,
             strlen(body));
}

static void redirect_to(int client_fd, const char *location)
{
  char headers[512];
  snprintf(headers, sizeof(headers), "Location: %s\r\n", location);
  send_text(client_fd, "302 Found", "text/plain; charset=utf-8", headers, "");
}

static int is_logged_in(const char *request)
{
  return request &&
         strstr(request, "astro_session=logged_in") != NULL;
}

static void url_decode(char *dst, const char *src)
{
  while (src && *src) {
    if (*src == '%' && src[1] && src[2]) {
      char hex[3] = {src[1], src[2], '\0'};
      *dst++ = (char)strtol(hex, NULL, 16);
      src += 3;
    } else if (*src == '+') {
      *dst++ = ' ';
      src++;
    } else {
      *dst++ = *src++;
    }
  }
  *dst = '\0';
}

static int valid_login(const char *request)
{
  const char *body = strstr(request, "\r\n\r\n");
  char decoded[2048];

  if (!body)
    return 0;

  body += 4;
  memset(decoded, 0, sizeof(decoded));
  url_decode(decoded, body);

  return strstr(decoded, "username=" ASTRO_USER) != NULL &&
         strstr(decoded, "password=" ASTRO_PASS) != NULL;
}

static void html_escape(const char *src, char *dst, size_t dst_size)
{
  if (!dst || dst_size == 0)
    return;

  size_t used = 0;
  if (!src)
    src = "";

  while (*src && used + 1 < dst_size) {
    const char *rep = NULL;

    switch (*src) {
      case '&': rep = "&amp;"; break;
      case '<': rep = "&lt;"; break;
      case '>': rep = "&gt;"; break;
      case '"': rep = "&quot;"; break;
      case '\'': rep = "&#39;"; break;
      default: break;
    }

    if (rep) {
      size_t n = strlen(rep);
      if (used + n >= dst_size)
        break;
      memcpy(dst + used, rep, n);
      used += n;
    } else {
      dst[used++] = *src;
    }

    src++;
  }

  dst[used] = '\0';
}

static void json_escape(const char *src, char *dst, size_t dst_size)
{
  if (!dst || dst_size == 0)
    return;

  size_t used = 0;
  if (!src)
    src = "";

  while (*src && used + 1 < dst_size) {
    const char *rep = NULL;

    switch (*src) {
      case '\\': rep = "\\\\"; break;
      case '"': rep = "\\\""; break;
      case '\n': rep = "\\n"; break;
      case '\r': rep = "\\r"; break;
      case '\t': rep = "\\t"; break;
      default: break;
    }

    if (rep) {
      size_t n = strlen(rep);
      if (used + n >= dst_size)
        break;
      memcpy(dst + used, rep, n);
      used += n;
    } else if ((unsigned char)*src >= 0x20) {
      dst[used++] = *src;
    }

    src++;
  }

  dst[used] = '\0';
}

/* ---------------- Pages / APIs ---------------- */

static const char *login_page =
"<!doctype html>"
"<html lang='pt-BR'><head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Astro Remote</title>"
"<style>"
"*{box-sizing:border-box}"
"body{margin:0;min-height:100vh;display:grid;place-items:center;background:#070b12;color:#edf4f6;font-family:Arial,sans-serif}"
".shell{width:min(390px,calc(100vw - 28px));padding:34px 28px;background:linear-gradient(180deg,#101926,#0b121d);border:1px solid #26384a;border-radius:28px;box-shadow:0 26px 80px #000a}"
".crest{width:64px;height:64px;border:2px solid #9fd7dd;border-radius:50%;margin:0 auto 18px;display:grid;place-items:center;font-size:27px;box-shadow:0 0 32px #78cbd044}"
"h1{text-align:center;letter-spacing:.12em;font-size:22px;margin:0 0 8px}"
".sub{text-align:center;color:#8fa4b5;margin:0 0 24px}"
"input{width:100%;padding:14px 15px;margin:7px 0;border:1px solid #26384a;border-radius:12px;background:#080e17;color:#fff;font-size:16px;outline:none}"
"input:focus{border-color:#7dcbd1;box-shadow:0 0 0 3px #7dcbd122}"
"button{width:100%;padding:14px;margin-top:13px;border:0;border-radius:12px;background:#d8f4f5;color:#071014;font-weight:800;letter-spacing:.06em;cursor:pointer}"
"</style></head><body>"
"<div class='shell'>"
"<div class='crest'>✦</div>"
"<h1>ASTRO REMOTE</h1>"
"<p class='sub'>Acesso ao seu PS5</p>"
"<form method='POST' action='/login'>"
"<input name='username' placeholder='Usuario' autocomplete='username' required>"
"<input name='password' type='password' placeholder='Senha' autocomplete='current-password' required>"
"<button type='submit'>ENTRAR</button>"
"</form></div></body></html>";

static const char *shutdown_page =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Astro encerrado</title>"
"<style>body{margin:0;min-height:100vh;display:grid;place-items:center;background:#070b12;color:#eef7f8;font-family:Arial,sans-serif}.box{padding:34px;border:1px solid #26384a;border-radius:24px;background:#0e1722;text-align:center}</style>"
"</head><body><div class='box'><h1>ASTRO ENCERRADO</h1><p>A porta foi liberada.</p></div></body></html>";

static void build_dashboard(char *out, size_t out_size)
{
  account_info_t info;
  int ret = load_account_info(&info);

  char name_html[256];
  char account_id_text[64];
  char activation_html[1800];
  char remote_html[2400];

  memset(name_html, 0, sizeof(name_html));
  memset(account_id_text, 0, sizeof(account_id_text));
  memset(activation_html, 0, sizeof(activation_html));
  memset(remote_html, 0, sizeof(remote_html));

  if (ret == 0) {
    html_escape(info.name, name_html, sizeof(name_html));
    snprintf(account_id_text, sizeof(account_id_text),
             "0x%016" PRIx64,
             info.account_id);

    if (info.activated) {
      snprintf(activation_html, sizeof(activation_html),
               "<div class='badge ok'>CONTA ATIVADA</div>"
               "<div class='meta'><span>Account ID</span><strong>%s</strong></div>"
               "<div class='meta'><span>Remote ID</span><strong>%s</strong></div>",
               account_id_text,
               info.account_b64);
    } else {
      snprintf(activation_html, sizeof(activation_html),
               "<div class='badge warn'>CONTA LOCAL NAO ATIVADA</div>"
               "<p class='muted'>O Astro pode ativar esta conta local e preparar o Remote Play.</p>"
               "<form method='POST' action='/admin/account/activate'>"
               "<button class='primary' type='submit'>ATIVAR CONTA</button>"
               "</form>");
    }

    if (info.activated) {
      if (g_pairing_done) {
        snprintf(remote_html, sizeof(remote_html),
                 "<div class='remote-state success'>✓ DISPOSITIVO PAREADO</div>"
                 "<p class='muted'>O registro do Remote Play foi confirmado pelo PS5.</p>"
                 "<form method='POST' action='/admin/remote/pin'>"
                 "<button class='secondary' type='submit'>GERAR NOVO PIN</button></form>");
      } else if (g_pairing_active) {
        int remain = (int)(g_pairing_expires - time(NULL));
        if (remain < 0) remain = 0;
        snprintf(remote_html, sizeof(remote_html),
                 "<div class='pin-label'>PIN DE VINCULO</div>"
                 "<div class='pin'>%04u <span>%04u</span></div>"
                 "<div class='remote-state' id='pairState'>Aguardando dispositivo...</div>"
                 "<p class='muted'>Expira em aproximadamente %d segundos.</p>"
                 "<form method='POST' action='/admin/remote/pin'>"
                 "<button class='secondary' type='submit'>GERAR OUTRO PIN</button></form>"
                 "<script>"
                 "async function poll(){try{const r=await fetch('/api/remote/status',{cache:'no-store'});const j=await r.json();"
                 "const e=document.getElementById('pairState');"
                 "if(j.state==='paired'){e.textContent='✓ Pareado com sucesso';e.className='remote-state success';}"
                 "else if(j.state==='error'){e.textContent='Falha no pareamento: '+j.error;e.className='remote-state danger';}"
                 "else if(j.state==='expired'){e.textContent='PIN expirado';e.className='remote-state danger';}"
                 "else{e.textContent='Aguardando dispositivo... '+j.remaining+'s';setTimeout(poll,1500);}}catch(e){setTimeout(poll,2500)}}poll();"
                 "</script>",
                 g_remote_pin / 10000,
                 g_remote_pin % 10000,
                 remain);
      } else if (g_pairing_error) {
        snprintf(remote_html, sizeof(remote_html),
                 "<div class='remote-state danger'>ULTIMA TENTATIVA FALHOU: %d</div>"
                 "<p class='muted'>Gere um novo PIN para tentar novamente.</p>"
                 "<form method='POST' action='/admin/remote/pin'>"
                 "<button class='primary' type='submit'>GERAR PIN REMOTE PLAY</button></form>",
                 g_pairing_error);
      } else {
        snprintf(remote_html, sizeof(remote_html),
                 "<div class='remote-state'>REMOTE PLAY PRONTO</div>"
                 "<p class='muted'>O PIN e gerado pelo servico nativo do PS5.</p>"
                 "<form method='POST' action='/admin/remote/pin'>"
                 "<button class='primary' type='submit'>GERAR PIN REMOTE PLAY</button></form>");
      }
    } else {
      snprintf(remote_html, sizeof(remote_html),
               "<div class='remote-state'>REMOTE PLAY BLOQUEADO</div>"
               "<p class='muted'>Ative a conta primeiro.</p>");
    }
  } else {
    snprintf(name_html, sizeof(name_html), "Usuario nao detectado");
    snprintf(activation_html, sizeof(activation_html),
             "<div class='badge danger'>ERRO AO LER CONTA: %d</div>", ret);
    snprintf(remote_html, sizeof(remote_html),
             "<div class='remote-state danger'>Conta indisponivel</div>");
  }

  snprintf(out, out_size,
"<!doctype html>"
"<html lang='pt-BR'><head>"
"<meta charset='utf-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>Astro Remote</title>"
"<style>"
"*{box-sizing:border-box}"
":root{--bg:#060a10;--panel:#0d1621;--panel2:#101d2a;--line:#26394b;--pale:#dff8f7;--aqua:#8bd3d7;--muted:#8ca0b1;--ok:#69d9ad;--warn:#e8c276;--danger:#ef8790}"
"body{margin:0;background:radial-gradient(circle at top,#122436 0,#070c13 38%%,#05080d 100%%);color:#edf5f6;font-family:Arial,sans-serif;min-height:100vh}"
".wrap{width:min(920px,calc(100%% - 28px));margin:28px auto 48px}"
".top{display:flex;justify-content:space-between;align-items:center;margin-bottom:18px}"
".brand{letter-spacing:.14em;font-weight:900}.online{font-size:12px;color:var(--ok);border:1px solid #315a50;background:#0a211d;padding:7px 10px;border-radius:999px}"
".grid{display:grid;grid-template-columns:1.1fr .9fr;gap:16px}"
".card{background:linear-gradient(180deg,#0f1925,#0a121c);border:1px solid var(--line);border-radius:24px;padding:20px;box-shadow:0 18px 50px #0007}"
".profile{display:flex;align-items:center;gap:16px;margin-bottom:18px}"
".avatar{width:78px;height:78px;border-radius:50%%;object-fit:cover;border:2px solid #8bd3d7;box-shadow:0 0 28px #8bd3d733;background:#111b27}"
"h1,h2,h3,p{margin-top:0}.profile h1{font-size:24px;margin:0 0 5px}.muted{color:var(--muted);line-height:1.5}"
".meta{display:flex;justify-content:space-between;gap:16px;padding:11px 0;border-top:1px solid #1d2c3a;font-size:13px}.meta span{color:var(--muted)}.meta strong{font-family:monospace;font-size:12px;overflow-wrap:anywhere;text-align:right}"
".badge{display:inline-block;padding:7px 10px;border-radius:999px;font-size:11px;font-weight:900;letter-spacing:.05em;margin-bottom:14px;border:1px solid #304354}.badge.ok{color:var(--ok);border-color:#315a50}.badge.warn{color:var(--warn);border-color:#5b5033}.badge.danger{color:var(--danger);border-color:#62363b}"
"button{border:0;border-radius:12px;padding:13px 16px;font-weight:900;letter-spacing:.04em;cursor:pointer;width:100%%}.primary{background:var(--pale);color:#071014}.secondary{background:#182838;color:#d9eff0;border:1px solid #30495d}.shutdown{background:#311a20;color:#f6cfd3;border:1px solid #66313b;margin-top:16px}"
".pin-label{font-size:11px;color:var(--muted);letter-spacing:.12em}.pin{font-size:42px;letter-spacing:.08em;font-weight:900;color:var(--pale);margin:8px 0 14px}.pin span{color:var(--aqua)}"
".remote-state{padding:10px 12px;border:1px solid #2c4457;border-radius:12px;background:#0a141e;margin-bottom:12px;font-size:13px}.remote-state.success{color:var(--ok);border-color:#315a50}.remote-state.danger{color:var(--danger);border-color:#62363b}"
".elf{display:flex;justify-content:space-between;padding:12px 0;border-bottom:1px solid #1d2c3a}.elf:last-child{border-bottom:0}.elf b{font-size:13px}.tag{font-size:10px;color:var(--muted);border:1px solid #304354;padding:5px 7px;border-radius:999px}"
".footer{margin-top:16px;color:#62778a;font-size:11px;text-align:center}"
"@media(max-width:760px){.grid{grid-template-columns:1fr}.wrap{margin-top:16px}.profile h1{font-size:21px}.pin{font-size:34px}.top{padding:0 4px}}"
"</style></head><body>"
"<main class='wrap'>"
"<div class='top'><div class='brand'>ASTRO REMOTE</div><div class='online'>● PS5 ONLINE</div></div>"
"<div class='grid'>"
"<section class='card'>"
"<div class='profile'><img class='avatar' src='/avatar' alt='Avatar da conta'><div><h1>%s</h1><p class='muted'>Usuario atual do PlayStation 5</p></div></div>"
"%s"
"</section>"
"<section class='card'><h2>Remote Play</h2>%s</section>"
"<section class='card'><h2>ELFs</h2>"
"<div class='elf'><b>browser.elf</b><span class='tag'>EXECUTAR</span></div>"
"<div class='elf'><b>ghostcontrol.elf</b><span class='tag'>EXECUTAR</span></div>"
"<div class='elf'><b>ftpsrv.elf</b><span class='tag'>EXECUTAR</span></div>"
"<div class='elf'><b>pegasus.elf</b><span class='tag'>EXECUTAR</span></div>"
"</section>"
"<section class='card'><h2>Astro</h2>"
"<p class='muted'>Conta, avatar e Remote Play sao lidos diretamente do console.</p>"
"<form method='POST' action='/admin/shutdown'><button class='shutdown' type='submit'>ENCERRAR ASTRO</button></form>"
"</section>"
"</div><div class='footer'>ASTRO Remote v0.3 • conta + avatar + pareamento nativo</div>"
"</main></body></html>",
           name_html,
           activation_html,
           remote_html);
}

static void send_account_api(int fd)
{
  account_info_t info;
  int ret = load_account_info(&info);

  char json[1400];
  char name_json[256];

  if (ret != 0) {
    snprintf(json, sizeof(json),
             "{\"ok\":false,\"error\":%d}", ret);
  } else {
    json_escape(info.name, name_json, sizeof(name_json));
    snprintf(json, sizeof(json),
             "{\"ok\":true,"
             "\"name\":\"%s\","
             "\"user_id\":%d,"
             "\"slot\":%d,"
             "\"activated\":%s,"
             "\"account_id\":\"0x%016" PRIx64 "\","
             "\"account_b64\":\"%s\","
             "\"account_type\":\"%s\","
             "\"flags\":%d,"
             "\"remoteplay_global\":%d,"
             "\"remoteplay_user\":%d,"
             "\"avatar\":\"/avatar\"}",
             name_json,
             info.user_id,
             info.slot,
             info.activated ? "true" : "false",
             info.account_id,
             info.account_b64,
             info.account_type,
             info.flags,
             info.global_rp_enabled,
             info.user_rp_enabled);
  }

  send_text(fd, "200 OK", "application/json; charset=utf-8", NULL, json);
}

static void send_remote_status_api(int fd)
{
  if (g_pairing_active)
    poll_pairing();

  char json[768];
  int remaining = 0;

  if (g_pairing_active) {
    remaining = (int)(g_pairing_expires - time(NULL));
    if (remaining < 0) remaining = 0;
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"state\":\"waiting\","
             "\"remaining\":%d,\"pair_status\":%d,"
             "\"pair_error\":%d,\"init_ret\":%d}",
             remaining,
             g_pairing_status,
             g_pairing_native_error,
             g_remote_init_ret);
  } else if (g_pairing_done) {
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"state\":\"paired\","
             "\"remaining\":0,\"pair_status\":2,"
             "\"pair_error\":0,\"init_ret\":%d}",
             g_remote_init_ret);
  } else if (g_pairing_error == 2) {
    snprintf(json, sizeof(json),
             "{\"ok\":false,\"state\":\"expired\","
             "\"remaining\":0,\"error\":2,\"init_ret\":%d}",
             g_remote_init_ret);
  } else if (g_pairing_error) {
    snprintf(json, sizeof(json),
             "{\"ok\":false,\"state\":\"error\","
             "\"remaining\":0,\"error\":%d,"
             "\"pair_status\":%d,\"pair_error\":%d,"
             "\"init_ret\":%d}",
             g_pairing_error,
             g_pairing_status,
             g_pairing_native_error,
             g_remote_init_ret);
  } else {
    snprintf(json, sizeof(json),
             "{\"ok\":true,\"state\":\"idle\","
             "\"remaining\":0,\"init_ret\":%d}",
             g_remote_init_ret);
  }

  send_text(fd, "200 OK", "application/json; charset=utf-8", NULL, json);
}

static void send_avatar(int fd)
{
  account_info_t info;
  int ret = load_account_info(&info);

  if (ret == 0) {
    uint8_t *bmp = NULL;
    size_t bmp_len = 0;

    if (load_avatar_bmp(&info, &bmp, &bmp_len) == 0 && bmp) {
      send_bytes(fd, "200 OK", "image/bmp", NULL, bmp, bmp_len);
      free(bmp);
      return;
    }
  }

  char name[ACCOUNT_NAME_MAX] = "A";
  if (ret == 0 && info.name[0])
    snprintf(name, sizeof(name), "%s", info.name);

  char initial = 'A';
  for (size_t i = 0; name[i]; ++i) {
    if (isalnum((unsigned char)name[i])) {
      initial = (char)toupper((unsigned char)name[i]);
      break;
    }
  }

  char svg[1024];
  snprintf(svg, sizeof(svg),
           "<svg xmlns='http://www.w3.org/2000/svg' width='128' height='128' viewBox='0 0 128 128'>"
           "<defs><radialGradient id='g'><stop stop-color='#1f4452'/><stop offset='1' stop-color='#081018'/></radialGradient></defs>"
           "<rect width='128' height='128' rx='64' fill='url(#g)'/>"
           "<circle cx='64' cy='64' r='59' fill='none' stroke='#8bd3d7' stroke-width='3'/>"
           "<text x='64' y='80' text-anchor='middle' font-family='Arial' font-size='54' font-weight='700' fill='#dff8f7'>%c</text>"
           "</svg>",
           initial);

  send_text(fd, "200 OK", "image/svg+xml; charset=utf-8", NULL, svg);
}

/* ---------------- Main ---------------- */

int main(void)
{
  int server_fd;
  int client_fd;
  int opt = 1;
  struct sockaddr_in addr;
  char buffer[8192];

  /* UserService is used for foreground user + display name. */
  sceUserServiceInitialize(NULL);

  server_fd = socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    notify("ASTRO: socket falhou");
    return 1;
  }

  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(ASTRO_PORT);

  if (bind(server_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    notify("ASTRO: bind falhou");
    close(server_fd);
    return 1;
  }

  if (listen(server_fd, 8) < 0) {
    notify("ASTRO: listen falhou");
    close(server_fd);
    return 1;
  }

  notify("ASTRO Remote v0.3 - conta e Remote Play");

  while (running) {
    client_fd = accept(server_fd, NULL, NULL);
    if (client_fd < 0)
      continue;

    memset(buffer, 0, sizeof(buffer));

    int total = 0;
    int received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

    if (received <= 0) {
      close(client_fd);
      continue;
    }

    total = received;
    buffer[total] = '\0';

    char *header_end = strstr(buffer, "\r\n\r\n");

    if (header_end) {
      int content_length = 0;
      char *cl = strstr(buffer, "Content-Length:");

      if (cl)
        content_length = atoi(cl + strlen("Content-Length:"));

      int header_size = (int)((header_end + 4) - buffer);
      int body_received = total - header_size;

      while (body_received < content_length &&
             total < (int)sizeof(buffer) - 1) {
        received = recv(client_fd,
                        buffer + total,
                        sizeof(buffer) - total - 1,
                        0);

        if (received <= 0)
          break;

        total += received;
        body_received += received;
        buffer[total] = '\0';
      }
    }

    if (strstr(buffer, "POST /login ") && valid_login(buffer)) {
      send_text(client_fd,
                "302 Found",
                "text/plain; charset=utf-8",
                "Set-Cookie: astro_session=logged_in; Path=/; HttpOnly; SameSite=Lax\r\n"
                "Location: /\r\n",
                "");
    }

    else if (strstr(buffer, "GET /avatar ") && is_logged_in(buffer)) {
      send_avatar(client_fd);
    }

    else if (strstr(buffer, "GET /api/account ") && is_logged_in(buffer)) {
      send_account_api(client_fd);
    }

    else if (strstr(buffer, "GET /api/remote/status ") && is_logged_in(buffer)) {
      send_remote_status_api(client_fd);
    }

    else if (strstr(buffer, "POST /admin/account/activate ") &&
             is_logged_in(buffer)) {
      account_info_t info;
      int ret = activate_current_account(&info);

      if (ret == 0) {
        notify("ASTRO: conta local ativada");
      } else if (ret == 1) {
        notify("ASTRO: conta ja estava ativada");
      } else {
        char msg[128];
        snprintf(msg, sizeof(msg), "ASTRO: falha ao ativar conta (%d)", ret);
        notify(msg);
      }

      redirect_to(client_fd, "/");
    }

    else if (strstr(buffer, "POST /admin/remote/pin ") &&
             is_logged_in(buffer)) {
      uint32_t pin = 0;
      int ret = generate_remote_pin(&pin);

      if (ret == 0) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "ASTRO Remote Play\nPIN: %04u %04u",
                 pin / 10000,
                 pin % 10000);
        notify(msg);
      } else {
        g_pairing_active = 0;
        g_pairing_done = 0;
        g_pairing_error = ret;

        char msg[160];
        snprintf(msg, sizeof(msg),
                 "ASTRO: falha ao gerar PIN (%d)", ret);
        notify(msg);
      }

      redirect_to(client_fd, "/");
    }

    else if (strstr(buffer, "POST /admin/shutdown ") &&
             is_logged_in(buffer)) {
      send_text(client_fd,
                "200 OK",
                "text/html; charset=utf-8",
                NULL,
                shutdown_page);

      running = 0;
      notify("ASTRO Remote encerrando");
    }

    else if (strstr(buffer, "GET / ") && is_logged_in(buffer)) {
      char dashboard[32768];
      memset(dashboard, 0, sizeof(dashboard));
      build_dashboard(dashboard, sizeof(dashboard));

      send_text(client_fd,
                "200 OK",
                "text/html; charset=utf-8",
                NULL,
                dashboard);
    }

    else {
      send_text(client_fd,
                "200 OK",
                "text/html; charset=utf-8",
                NULL,
                login_page);
    }

    close(client_fd);
  }

  if (g_pairing_active)
    sceRemoteplayNotifyPinCodeError(1);

  close(server_fd);
  sceUserServiceTerminate();

  notify("ASTRO Remote encerrado");
  return 0;
}
