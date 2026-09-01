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

/* Base detector remains available to the deeper detector. */
#define auto_web_discover auto_web_discover_legacy
#define auto_web_lookup auto_web_lookup_legacy
#define auto_webapps_api auto_webapps_api_legacy
#include "astro_v3_webdetect.inc"
#undef auto_web_discover
#undef auto_web_lookup
#undef auto_webapps_api

/* Keep the deeper library-only detector as a reusable matching layer. */
#define auto_payload_match_deep auto_payload_match_deep_legacy
#define auto_web_discover auto_web_discover_plus_legacy
#define auto_web_lookup auto_web_lookup_plus_legacy
#define auto_webapps_api auto_webapps_api_plus_legacy
#include "astro_v3_webdetect_plus.inc"
#undef auto_payload_match_deep
#undef auto_web_discover
#undef auto_web_lookup
#undef auto_webapps_api

/* Active detector adds the real PS5 process table as a second source of truth. */
#include "astro_v3_webdetect_runtime.inc"

/* Keep the first beta pages/auth screens available for reference. */
#define v3_dashboard v3_dashboard_beta_legacy
#define v3_payloads_page v3_payloads_page_beta_legacy
#define v3_files_page v3_files_page_beta_legacy
#define v3_saves_page v3_saves_page_beta_legacy
#define send_login send_login_beta_legacy
#define send_setup send_setup_beta_legacy
#include "astro_v3_beta_ui.inc"
#undef send_setup
#undef send_login
#undef v3_saves_page
#undef v3_files_page
#undef v3_dashboard
#undef v3_payloads_page

/* Active login and first-use registration share the polished V3 identity. */
#include "astro_v3_auth_ui.inc"

/* File manager behaves like a desktop Explorer with a lazy folder tree.
 * The first Explorer cut had one malformed JS statement, so repair the
 * generated page before it leaves the PS5 HTTP server. */
#include "astro_v3_files_jsfix.inc"
#define send_text explorer_send_text_fixed
#include "astro_v3_files_explorer.inc"
#undef send_text

/* Save manager is title-centric. The first external-engine clone stays legacy.
 * Native V1 remains as a reusable helper layer, but its clone entry point is
 * renamed; V2 is the active verified cross-account clone engine. */
static void save_zip_ui_send_text(int fd,const char *status,const char *ctype,const char *extra,const char *body);
#define send_text save_zip_ui_send_text
#define save_clone_title_api save_clone_title_api_merge_legacy
#define v3_save_action_api v3_save_action_api_tar_legacy
#include "astro_v3_saves_cards.inc"
#undef v3_save_action_api
#undef save_clone_title_api
#undef send_text
#define save_clone_title_api save_clone_title_api_native_v1_legacy
#include "astro_v3_saves_native.inc"
#undef save_clone_title_api
#include "astro_v3_saves_native_v2.inc"
#include "astro_v3_saves_zip.inc"

/* Keep the first polished cards available while the active UI below moves
 * Web access into the payload card itself. */
#define v3_dashboard v3_dashboard_polish_legacy
#define v3_payloads_page v3_payloads_page_polish_legacy
#include "astro_v3_payload_polish.inc"
#undef v3_dashboard
#undef v3_payloads_page

/* Keep the Web-card payload library, but replace its Dashboard with the
 * runtime-aware one that also shows ELFs launched outside Astro. */
#define v3_dashboard v3_dashboard_webcard_legacy
#include "astro_v3_payload_webcard.inc"
#undef v3_dashboard
#include "astro_v3_dashboard_runtime.inc"

#include "astro_v3_profile_ui.inc"
#include "astro_v3_proxy_api_compat.inc"

/* The existing beta router keeps all authentication/file/save behavior, but
 * Web UI listing and proxy lookup resolve through the active auto detector.
 * The legacy save backup route is used as the authenticated dispatcher for
 * ZIP download, restore and native cross-account clone actions. */
#define beta_webapps_api(fd) auto_webapps_api((fd),target)
#define beta_web_lookup auto_web_lookup
#define v3_save_backup_api v3_save_action_api
#define strncmp astro_proxyaware_strncmp
#include "astro_v3_beta_router.inc"
#undef strncmp
#undef v3_save_backup_api
#undef beta_web_lookup
#undef beta_webapps_api
