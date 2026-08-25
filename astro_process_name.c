#include <unistd.h>
#include <sys/syscall.h>

__attribute__((constructor))
static void astro_set_process_name(void)
{
  syscall(SYS_thr_set_name, -1, "astrormt");
}
