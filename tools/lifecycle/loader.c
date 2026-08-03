/* minimal libretro lifecycle harness: exercises the core's no-content
   entry points under whatever sanitizers the .so carries. */

   build: gcc -O1 -g -fsanitize=address -o loader loader.c -ldl
   run:   ASAN_OPTIONS=detect_leaks=1 ./loader path/to/boom3_libretro.so
   Point it at a SANITIZE=address,undefined build of the core: it
   exercises retro_set_environment / retro_init / retro_deinit without
   content, so init-path leaks and UB report at exit. The full-game
   sweep still needs a real frontend and retail data; this covers the
   lifecycle a frontend performs before content exists.
#include <dlfcn.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdbool.h>
#include <string.h>

typedef void (*retro_log_printf_t)(int level, const char *fmt, ...);
static void log_printf(int level, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
}
static bool env_cb(unsigned cmd, void *data) {
    switch (cmd) {
    case 27: /* GET_LOG_INTERFACE */
        if (data) *(retro_log_printf_t *)data = log_printf;
        return true;
    case 3: /* GET_CAN_DUPE */ if (data) *(bool*)data = true; return true;
    case 9: /* GET_SYSTEM_DIRECTORY */ if (data) *(const char**)data = "/tmp"; return true;
    case 31: /* GET_SAVE_DIRECTORY */ if (data) *(const char**)data = "/tmp"; return true;
    default: return false;
    }
}
int main(int argc, char **argv) {
    void *h = dlopen(argv[1], RTLD_NOW);
    if (!h) { fprintf(stderr, "dlopen: %s\n", dlerror()); return 1; }
    unsigned (*api)(void) = dlsym(h, "retro_api_version");
    void (*getsys)(void*) = dlsym(h, "retro_get_system_info");
    void (*setenvcb)(void*) = dlsym(h, "retro_set_environment");
    void (*init)(void) = dlsym(h, "retro_init");
    void (*deinit)(void) = dlsym(h, "retro_deinit");
    printf("api=%u\n", api());
    char sysinfo[128] = {0};
    getsys(sysinfo);
    printf("core=%s ver=%s\n", *(char**)sysinfo, *((char**)sysinfo+1));
    setenvcb((void*)env_cb);
    init();
    deinit();
    printf("lifecycle done\n");
    dlclose(h);
    return 0;
}
