/* Astro runtime identity.
 * ELF loaders commonly start generic payloads as "payload.elf". Rename the
 * running thread/process immediately so KERN_PROC and the Astro process page
 * identify this daemon as "astro".
 */
#include <sys/syscall.h>

static void astro_set_process_name(void)
{
#ifdef SYS_thr_set_name
  (void)syscall(SYS_thr_set_name, -1, "astro");
#endif
}
