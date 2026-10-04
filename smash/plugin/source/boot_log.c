#include "boot_log.h"

#include "types.h"

static void alongside(char out[BOOT_LOG_PATH_MAX], const char *dir, const char *name)
{
    u32 at = 0;
    out[at++] = 's';
    out[at++] = 'd';
    out[at++] = ':';
    while (*dir)
        out[at++] = *dir++;
    out[at++] = '/';
    while (*name)
        out[at++] = *name++;
    out[at] = 0;
}

int boot_log_paths_resolve(boot_log_paths *paths)
{
    char dir[PLUGIN_PATH_MAX];
    int res = plgldr_plugin_dir(dir);
    if (res < 0)
        return res;

    alongside(paths->marker, dir, "logging.enabled");
    alongside(paths->log, dir, "saltysd.log");
    return 0;
}
