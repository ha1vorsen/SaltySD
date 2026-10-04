#include "plgldr.h"
#include "ipc.h"

#include "types.h"

#define DESC_BUF_RW(size) (((size) << 4) | 0xE)

//plg:ldr allows one session at a time.
int plgldr_plugin_path(char path[PLUGIN_PATH_MAX])
{
    u32 port;
    int res = ipc_connect_port(&port, "plg:ldr");
    if (res < 0)
        return res;

    for (u32 i = 0; i < PLUGIN_PATH_MAX; i++)
        path[i] = 0;

    u32 *cmd = ipc_cmdbuf();
    cmd[0] = 0x000A0002;
    cmd[1] = DESC_BUF_RW(PLUGIN_PATH_MAX - 1);
    cmd[2] = (u32)path;
    res = ipc_request(port);
    ipc_close(port);
    return res;
}

static int starts_with(const char *s, const char *prefix)
{
    while (*prefix)
        if (*s++ != *prefix++)
            return 0;
    return 1;
}

static int is_hex(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}

int plgldr_plugin_path_valid(const char *p)
{
    u32 n = 0;
    while (n < PLUGIN_PATH_MAX && p[n]) {
        if (p[n] < 0x20 || p[n] > 0x7E)
            return 0;
        n++;
    }
    if (n < 5 || n > 200 || !starts_with(p, "/luma/plugins/") ||
        !starts_with(p + n - 4, ".3gx"))
        return 0;

    const char *rest = p + 14;
    if (starts_with(rest, "default.3gx") && !rest[11])
        return 1;
    for (u32 i = 0; i < 16; i++)
        if (!is_hex(rest[i]))
            return 0;
    if (rest[16] != '/')
        return 0;
    for (const char *q = rest + 17; *q; q++)
        if (*q == '/')
            return 0;
    return rest[17] != 0;
}

int plgldr_plugin_dir(char path[PLUGIN_PATH_MAX])
{
    int res = plgldr_plugin_path(path);
    if (res < 0)
        return res;
    if (!plgldr_plugin_path_valid(path))
        return PLGLDR_PATH_REJECTED;

    u32 last_slash = 0;
    for (u32 i = 0; path[i]; i++)
        if (path[i] == '/')
            last_slash = i;
    path[last_slash] = 0;
    return 0;
}
