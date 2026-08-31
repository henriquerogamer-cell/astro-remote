#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <time.h>
#include <fcntl.h>
#include <dirent.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define ASTRO_PORT 45821
#define REQ_BUF 65536
#define ASTRO_DIR "/data/Astro"
#define AUTH_FILE ASTRO_DIR "/auth.cfg"
#define MAX_USER 48
#define MAX_PASS 96
#define MAX_TOKEN 65

#define PROSPERO_PORT 7070
#define PEGASUS_PORT 6970
#define GARLIC_PORT 8082

#include "astro_part1.inc"
#include "astro_part2.inc"
#include "astro_managers.inc"
#include "astro_part3.inc"
#include "astro_part4.inc"
#include "astro_part5.inc"
