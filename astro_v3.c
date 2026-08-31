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
#include "astro_v3_core.inc"
#include "astro_v3_ui.inc"
#include "astro_v3_router.inc"
