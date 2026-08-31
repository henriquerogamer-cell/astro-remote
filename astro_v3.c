#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/proc.h>
#include <sys/user.h>
#include <sys/sysctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ps5/kernel.h>

#define ASTRO_PORT 45821
#define REQ_BUF 65536
#define ASTRO_DIR "/data/Astro"
#define AUTH_FILE ASTRO_DIR "/auth.cfg"
#define MAX_USER 48
#define MAX_PASS 96
#define MAX_TOKEN 65

#define ASTRO_PAYLOAD_DIR ASTRO_DIR "/payloads"
#define ASTRO_BACKUP_DIR ASTRO_DIR "/save_backups"
#define ASTRO_PROFILE_BACKUP_DIR ASTRO_DIR "/profile-backup"
#define ASTRO_FAVORITES_FILE ASTRO_DIR "/payload_favorites.txt"

#include "astro_part1.inc"
#include "astro_part2.inc"

/* Legacy core keeps its original foreground resolver internally. */
#define v3_foreground_user v3_foreground_user_legacy
#include "astro_v3_core.inc"
#undef v3_foreground_user

#include "astro_v3_profile_core.inc"
#include "astro_v3_beta_core.inc"

/* Keep the first beta pages available for reference, while the router uses
 * the polished payload/dashboard implementations below. */
#define v3_dashboard v3_dashboard_beta_legacy
#define v3_payloads_page v3_payloads_page_beta_legacy
#include "astro_v3_beta_ui.inc"
#undef v3_dashboard
#undef v3_payloads_page

#include "astro_v3_payload_polish.inc"
#include "astro_v3_profile_ui.inc"
#include "astro_v3_beta_router.inc"
